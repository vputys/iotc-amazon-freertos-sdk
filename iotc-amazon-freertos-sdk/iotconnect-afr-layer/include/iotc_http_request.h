//
// Copyright: Avnet 2022
// Created by Nik Markovic <nikola.markovic@avnet.com> on 6/15/22.
//

#ifndef IOTC_HTTP_REQUEST_H
#define IOTC_HTTP_REQUEST_H
#ifdef __cplusplus
extern   "C" {
#endif

#include <stdlib.h>

typedef struct IotConnectHttpRequest {
    char* host_name;
    char* resource; // path of the resource to GET/PUT
    char* payload; // if payload is not null, a POST will be issued, rather than GET.
    char* response; // We will will provide a default buffer with default size. Response will be a null terminated string.
    char* tls_cert; // provide an SSL certificate for your host (default ones provided in iotconnect_certs.h)
} IotConnectHttpRequest;

// supports get and post
// if post_data is NULL, a get is executed
int iotconnect_https_request(IotConnectHttpRequest* request);

#ifdef __cplusplus
}
#endif

#endif // IOTC_HTTP_CLIENT_H