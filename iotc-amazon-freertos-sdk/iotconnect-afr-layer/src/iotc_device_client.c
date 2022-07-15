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
#include <mqtt_demo_helpers.h>

/* Transport interface implementation include header for TLS. */
#include "transport_secure_sockets.h"

#include <transport_interface.h>

#include "iotconnect_sync.h"
#include "iotc_device_client.h"

/*-----------------------------------------------------------*/
struct NetworkContext
{
    SecureSocketsTransportParams_t* pParams;
};

static bool is_connected = false;
static NetworkContext_t xNetworkContext = {0};
static uint8_t ucSharedBuffer[1024];
static MQTTContext_t xMqttContext = { 0 };
static MQTTFixedBuffer_t xBuffer =
{
    .pBuffer = ucSharedBuffer,
    .size = 1024
};
static IotConnectC2dCallback c2d_msg_cb = NULL; // callback for inbound messages
static IotConnectStatusCallback status_cb = NULL; // callback for connection connection_status

/*-----------------------------------------------------------*/
static void prvEventCallback(MQTTContext_t* pxMqttContext,
    MQTTPacketInfo_t* pxPacketInfo,
    MQTTDeserializedInfo_t* pxDeserializedInfo)
{ 
    uint16_t usPacketIdentifier;

    (void)pxMqttContext;

    assert(pxDeserializedInfo != NULL);
    assert(pxMqttContext != NULL);
    assert(pxPacketInfo != NULL);

    usPacketIdentifier = pxDeserializedInfo->packetIdentifier;

    /* Handle incoming publish. The lower 4 bits of the publish packet
     * type is used for the dup, QoS, and retain flags. Hence masking
     * out the lower bits to check if the packet is publish. */
    if ((pxPacketInfo->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH)
    {
        assert(pxDeserializedInfo->pPublishInfo != NULL);
        if(c2d_msg_cb) {
            c2d_msg_cb((unsigned char*) pxDeserializedInfo->pPublishInfo->pPayload,  pxDeserializedInfo->pPublishInfo->payloadLength);
        }
    }
    else
    {
        vHandleOtherIncomingPacket(pxPacketInfo, usPacketIdentifier);
    }
}

int iotc_device_client_disconnect() {
    BaseType_t ret = DisconnectMqttSession(&xMqttContext, &xNetworkContext);
    if (ret == pdFAIL) {
        LogError(("Encountered a failure while trying to disconnect the MQTT session."));
    }
    is_connected = false;
    return (ret == pdPASS ? EXIT_SUCCESS : EXIT_FAILURE);
}

bool iotc_device_client_is_connected() {
    return (xMqttContext.connectStatus == MQTTConnected);
}

int iotc_device_client_send_message(const char* message) {
    BaseType_t ret = PublishToTopic(
        &xMqttContext,
        iotc_sync_get_pub_topic(),
        strlen(iotc_sync_get_pub_topic()),
        message,
        strlen(message)
        );

    bool connected = xMqttContext.connectStatus == MQTTConnected;
    if (pdPASS != ret) {
        LogError(("Failed to send message. Connection status: %s", message, connected ? "CONNECTED" : "DISCONNECTED"));
    }
    return (ret == pdPASS ? EXIT_SUCCESS : EXIT_FAILURE);
}

void iotc_device_client_loop(unsigned int timeout_ms) {
    BaseType_t ret = ProcessLoop(& xMqttContext, (uint32_t) timeout_ms);
    bool connected = xMqttContext.connectStatus == MQTTConnected;
    if (pdPASS != ret) {
        LogError(("Received an error from ProcessLoop! Connection status: %s", connected ? "CONNECTED" : "DISCONNECTED"));      
    }
    if (is_connected && !connected) {
        if (status_cb) {
            status_cb(IOTC_CS_MQTT_CONNECTED);
        }
        is_connected = false;
    }
}

int iotc_device_client_init(IotConnectDeviceClientConfig* c) {
    BaseType_t ret;

    c2d_msg_cb = NULL;
    status_cb = NULL;

    if (is_connected) {
        ret = DisconnectMqttSession(&xMqttContext, &xNetworkContext);
        if (ret == pdFAIL) {
            LogError(("Failed to disconnect a stale MQTT session."));
        }
    }
    is_connected = false;
    /* Remove compiler warnings about unused parameters. */

    ret = EstablishMqttSession(&xMqttContext,
        &xNetworkContext,
        &xBuffer,
        prvEventCallback);

    if (ret == pdFAIL) {
        /* Log error to indicate connection failure. */
        LogError(("Failed to connect to MQTT broker."));
        return EXIT_FAILURE;
    }
    LogInfo(("Connected to MQTT with EstablishMqttSession."));

    ret = SubscribeToTopic(&xMqttContext,
       iotc_sync_get_sub_topic(),
       (uint16_t)strlen(iotc_sync_get_sub_topic())
    );
    if (pdPASS != ret) {
        LogError(("Failed to subscribe to topic %s", iotc_sync_get_sub_topic()));
    }
    bool connected = false;
    int tries = 0;
    do {
        connected = (xMqttContext.connectStatus == MQTTConnected);
        ret = ProcessLoop(&xMqttContext, 100);
        if (ret)
        tries++;
        if (tries >= 100) {
            // 10 seconds
            LogError(("Failed to connect"));
            return EXIT_FAILURE;
        }
    } while (!connected);

    is_connected = true;

    c2d_msg_cb = c->c2d_msg_cb;
    status_cb = c->status_cb;

    return EXIT_SUCCESS;
}


