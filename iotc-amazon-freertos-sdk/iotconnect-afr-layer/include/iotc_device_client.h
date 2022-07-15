//
// Copyright: Avnet 2022
// Created by Nik Markovic <nikola.markovic@avnet.com> on 6/15/22.
//

#ifndef IOTC_DEVICE_CLIENT_H
#define IOTC_DEVICE_CLIENT_H

#include "iotconnect_discovery.h"
#include "iotconnect.h"

#ifdef __cplusplus
extern   "C" {
#endif


typedef void (*IotConnectC2dCallback)(unsigned char* message, size_t message_len);

typedef struct {
    IotConnectC2dCallback c2d_msg_cb; // callback for inbound messages
    IotConnectStatusCallback status_cb; // callback for connection status
} IotConnectDeviceClientConfig;

int iotc_device_client_init(IotConnectDeviceClientConfig *c);

int iotc_device_client_disconnect();

bool iotc_device_client_is_connected();

int iotc_device_client_send_message(const char *message);

void iotc_device_client_loop(unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // IOTC_DEVICE_CLIENT_H