/*
 * FreeRTOS V202107.00
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
//
// Copyright: Avnet 2022
// Created by Nik Markovic <nikola.markovic@avnet.com> on 6/15/22.
//

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aws_demo.h"

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

/* shadow demo helpers header. */
#include "mqtt_demo_helpers.h"

/* Transport interface implementation include header for TLS. */
#include "transport_secure_sockets.h"

#include "transport_interface.h"

//
// Copyright: Avnet, Softweb Inc. 2021
// Modified by Nik Markovic <nikola.markovic@avnet.com> on 6/24/21.
//

#include "iotc_device_client.h"
#include "iotconnect_sync.h"
#include "iotconnect.h"

static IotclConfig lib_config = { 0 };
static IotConnectClientConfig config = { 0 };


static void report_sync_error(IotclSyncResponse* response, const char* sync_response_str) {
    if (NULL == response) {
        fprintf(stderr, "Failed to obtain sync response?\n");
        return;
    }
    switch (response->ds) {
    case IOTCL_SR_DEVICE_NOT_REGISTERED:
        fprintf(stderr, "IOTC_SyncResponse error: Not registered\n");
        break;
    case IOTCL_SR_AUTO_REGISTER:
        fprintf(stderr, "IOTC_SyncResponse error: Auto Register\n");
        break;
    case IOTCL_SR_DEVICE_NOT_FOUND:
        fprintf(stderr, "IOTC_SyncResponse error: Device not found\n");
        break;
    case IOTCL_SR_DEVICE_INACTIVE:
        fprintf(stderr, "IOTC_SyncResponse error: Device inactive\n");
        break;
    case IOTCL_SR_DEVICE_MOVED:
        fprintf(stderr, "IOTC_SyncResponse error: Device moved\n");
        break;
    case IOTCL_SR_CPID_NOT_FOUND:
        fprintf(stderr, "IOTC_SyncResponse error: CPID not found\n");
        break;
    case IOTCL_SR_UNKNOWN_DEVICE_STATUS:
        fprintf(stderr, "IOTC_SyncResponse error: Unknown device status error from server\n");
        break;
    case IOTCL_SR_ALLOCATION_ERROR:
        fprintf(stderr, "IOTC_SyncResponse internal error: Allocation Error\n");
        break;
    case IOTCL_SR_PARSING_ERROR:
        fprintf(stderr,
            "IOTC_SyncResponse internal error: Parsing error. Please check parameters passed to the request.\n");
        break;
    default:
        fprintf(stderr, "WARN: report_sync_error called, but no error returned?\n");
        break;
    }
    fprintf(stderr, "Raw server response was:\n--------------\n%s\n--------------\n", sync_response_str);
}


static void on_mqtt_c2d_message(unsigned char* message, size_t message_len) {
    char* str = malloc(message_len + 1);
    memcpy(str, message, message_len);
    str[message_len] = 0;
    printf("event>>> %s\n", str);
    if (!iotcl_process_event(str)) {
        fprintf(stderr, "Error encountered while processing %s\n", str);
    }
    free(str);
}

void iotconnect_sdk_disconnect() {
    printf("Disconnecting...\n");
    if (0 == iotc_device_client_disconnect()) {
        printf("Disconnected.\n");
    }
}

bool iotconnect_sdk_is_connected() {
    return iotc_device_client_is_connected();
}

IotConnectClientConfig* iotconnect_sdk_init_and_get_config() {
    memset(&config, 0, sizeof(config));
    return &config;
}

IotclConfig* iotconnect_sdk_get_lib_config() {
    return iotcl_get_config();
}

static void on_message_intercept(IotclEventData data, IotConnectEventType type) {
    switch (type) {
    case ON_FORCE_SYNC:
        printf("Got a SYNC request request. Closing the mqtt connection.\n");
        iotc_sync_free_response();
        iotconnect_sdk_disconnect();
        break;
    case ON_CLOSE:
        printf("Got a disconnect request. Closing the mqtt connection.\n");
        iotconnect_sdk_disconnect();
        break;
    default:
        break; // not handling nay other messages
    }

    if (NULL != config.msg_cb) {
        config.msg_cb(data, type);
    }
}

int iotconnect_sdk_send_packet(const char* data) {
    return iotc_device_client_send_message(data);
}

void iotconnect_sdk_loop(unsigned int timeout_ms) {
    iotc_device_client_loop(timeout_ms);
}

///////////////////////////////////////////////////////////////////////////////////
// this the Initialization os IoTConnect SDK
int iotconnect_sdk_init() {
    int ret;

    iotc_sync_obtain_response();

    // We want to print only first 4 characters of cpid
    lib_config.device.env = config.env;
    lib_config.device.cpid = config.cpid;
    lib_config.device.duid = config.duid;

    if (!config.env || !config.cpid || !config.duid) {
        printf("Error: Device configuration is invalid. Configuration values for env, cpid and duid are required.\n");
        return -1;
    }

    lib_config.event_functions.ota_cb = config.ota_cb;
    lib_config.event_functions.cmd_cb = config.cmd_cb;
    lib_config.event_functions.msg_cb = on_message_intercept;

    lib_config.telemetry.dtg = iotc_sync_get_dtg();

    char cpid_buff[5];
    strncpy(cpid_buff, config.cpid, 4);
    cpid_buff[4] = 0;
    printf("CPID: %s***\n", cpid_buff);
    printf("ENV:  %s\n", config.env);

    if (!iotcl_init(&lib_config)) {
        fprintf(stderr, "Error: Failed to initialize the IoTConnect Lib\n");
        return -1;
    }

    IotConnectDeviceClientConfig pc;
    pc.status_cb = config.status_cb;
    pc.status_cb = config.status_cb;
    pc.c2d_msg_cb = on_mqtt_c2d_message;

    ret = iotc_device_client_init(&pc);
    if (ret) {
        fprintf(stderr, "Failed to connect!\n");
        return ret;
    }

    return ret;
}


int RunIotconnectShadowDemo(bool awsIotMqttMode,
    const char* pIdentifier,
    void* pNetworkServerInfo,
    void* pNetworkCredentialInfo,
    const void* pNetworkInterface)
{
    (void)awsIotMqttMode;
    (void)pIdentifier;
    (void)pNetworkServerInfo;
    (void)pNetworkCredentialInfo;
    (void)pNetworkInterface;
    
    iotc_sync_obtain_response();
    if (iotconnect_sdk_init()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/*-----------------------------------------------------------*/
