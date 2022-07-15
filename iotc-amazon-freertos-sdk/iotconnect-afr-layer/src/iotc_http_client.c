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
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 */

  /**
   * @file http_demo_s3_download.c
   * @brief Demonstrates usage of the HTTP library.
   */
//
// Copyright: Avnet 2022
// Created by Nik Markovic <nikola.markovic@avnet.com> on 6/15/22.
//

   /* Standard includes. */
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Include config as the first non-system header. */
#include "app_config.h"

/* Include common demo header. */
#include "aws_demo.h"

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

#include "transport_secure_sockets.h"
#include "backoff_algorithm.h"
#include "core_http_client.h"
#include "iotconnect_certs.h"
#include "pkcs11_helpers.h"

#include "iotc_http_request.h"

/*------------- Demo configurations -------------------------*/


/* Check that a transport timeout for the transport send and receive functions
 * is defined. */
#ifndef IOTC_HTTP_CLIENT_SEND_RECV_TIMEOUT_MS
#define IOTC_HTTP_CLIENT_SEND_RECV_TIMEOUT_MS    ( 5000 )
#endif

 /* Check that the size of the user buffer is defined. */
#ifndef IOTC_HTTP_CLIENT_USER_BUFFER_SIZE
#define IOTC_HTTP_CLIENT_USER_BUFFER_SIZE    ( 4096 )
#endif

#define CONNECTION_RETRY_MAX_ATTEMPTS            ( 5U )
#define CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS    ( 5000U )
#define CONNECTION_RETRY_BACKOFF_BASE_MS         ( 500U )


// The number of times to try the whole HTTP reqyuest with backoff
#ifndef MAX_HTTP_REQUEST_TRIES
#define MAX_HTTP_REQUEST_TRIES    ( 3 )
#endif

// The number of milliseconds to backof between HTTP request failures
#define HTTP_REQUEST_BACKOFF_MS    ( pdMS_TO_TICKS( 2000U ) )

/*-----------------------------------------------------------*/

/**
* @brief Each compilation unit that consumes the NetworkContext must define it.
* It should contain a single pointer to the type of your desired transport.
* When using multiple transports in the same compilation unit, define this pointer as void *.
*
* @note Transport stacks are defined in amazon-freertos/libraries/abstractions/transport/secure_sockets/transport_secure_sockets.h.
*/
struct NetworkContext
{
    SecureSocketsTransportParams_t* pParams;
};

/*-----------------------------------------------------------*/

/**
 * @brief A buffer used in the demo for storing HTTP request headers, and HTTP
 * response headers and body.
 *
 * @note This demo shows how the same buffer can be re-used for storing the HTTP
 * response after the HTTP request is sent out. However, the user can decide how
 * to use buffers to store HTTP requests and responses.
 */
static uint8_t httpClientBuffer[IOTC_HTTP_CLIENT_USER_BUFFER_SIZE];

/**
 * @brief Represents header data that will be sent in an HTTP request.
 */
static HTTPRequestHeaders_t requestHeaders;

/**
 * @brief Configurations of the initial request headers that are passed to
 * #HTTPClient_InitializeRequestHeaders.
 */
static HTTPRequestInfo_t requestInfo;

/**
 * @brief Represents a response returned from an HTTP server.
 */
static HTTPResponse_t response;

typedef BaseType_t(*TransportConnect_t)(NetworkContext_t* pxNetworkContext, IotConnectHttpRequest* request);

static BaseType_t prvBackoffForRetry(BackoffAlgorithmContext_t* pxRetryParams)
{
    BaseType_t xReturnStatus = pdFAIL;
    uint16_t usNextRetryBackOff = 0U;
    BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;

    /**
     * To calculate the backoff period for the next retry attempt, we will
     * generate a random number to provide to the backoffAlgorithm library.
     *
     * Note: The PKCS11 module is used to generate the random number as it allows access
     * to a True Random Number Generator (TRNG) if the vendor platform supports it.
     * It is recommended to use a random number generator seeded with a device-specific
     * entropy source so that probability of collisions from devices in connection retries
     * is mitigated.
     */
    uint32_t ulRandomNum = 0;
    
    if (xPkcs11GenerateRandomNumber((uint8_t*)&ulRandomNum,
        sizeof(ulRandomNum)) == pdPASS)
    {
        /* Get back-off value (in milliseconds) for the next retry attempt. */
        xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff(pxRetryParams, ulRandomNum, &usNextRetryBackOff);

        if (xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted)
        {
            LogError(("All retry attempts have exhausted. Operation will not be retried"));
        }
        else if (xBackoffAlgStatus == BackoffAlgorithmSuccess)
        {
            /* Perform the backoff delay. */
            vTaskDelay(pdMS_TO_TICKS(usNextRetryBackOff));

            xReturnStatus = pdPASS;

            LogInfo(("Retry attempt %lu out of maximum retry attempts %lu.",
                (pxRetryParams->attemptsDone + 1),
                pxRetryParams->maxRetryAttempts));
        }
    }
    else
    {
        LogError(("Unable to retry operation with broker: Random number generation failed"));
    }

    return xReturnStatus;
}

// Reworked Amazon provided demo function to allow for the host parameter
static BaseType_t connectToServerWithBackoffRetriesV2(TransportConnect_t connectFunction,
    NetworkContext_t* pxNetworkContext, IotConnectHttpRequest* r)
{
    BaseType_t xReturn = pdFAIL;
    /* Struct containing the next backoff time. */
    BackoffAlgorithmContext_t xReconnectParams;
    BaseType_t xBackoffStatus = 0U;

    configASSERT(connectFunction != NULL);
    configASSERT(pxNetworkContext != NULL);

    /* Initialize reconnect attempts and interval. */
    BackoffAlgorithm_InitializeParams(&xReconnectParams,
        CONNECTION_RETRY_BACKOFF_BASE_MS,
        CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
        CONNECTION_RETRY_MAX_ATTEMPTS);

    /* Attempt to connect to the HTTP server. If connection fails, retry after a
     * timeout. The timeout value will exponentially increase until either the
     * maximum timeout value is reached or the set number of attempts are
     * exhausted.*/
    do
    {
        xReturn = connectFunction(pxNetworkContext, r);

        if (xReturn != pdPASS)
        {
            LogWarn(("Connection to the HTTP server failed. "
                "Retrying connection with backoff and jitter."));

            /* As the connection attempt failed, we will retry the connection after an
             * exponential backoff with jitter delay. */

             /* Calculate the backoff period for the next retry attempt and perform the wait operation. */
            xBackoffStatus = prvBackoffForRetry(&xReconnectParams);
        }
    } while ((xReturn == pdFAIL) && (xBackoffStatus == pdPASS));

    return xReturn;
}

/*-----------------------------------------------------------*/
static BaseType_t prvConnectToServer(NetworkContext_t* pxNetworkContext, IotConnectHttpRequest* r)
{
    ServerInfo_t serverInfo = { 0 };
    SocketsConfig_t socketsConfig = { 0 };
    BaseType_t status = pdPASS;
    TransportSocketStatus_t networkStatus;

    serverInfo.pHostName = r->host_name;
    serverInfo.hostNameLength = strlen(r->host_name);
    serverInfo.port = 443;

    socketsConfig.enableTls = true;
    socketsConfig.pAlpnProtos = NULL;
    socketsConfig.maxFragmentLength = 0;
    socketsConfig.disableSni = false;
    socketsConfig.pRootCa = r->tls_cert;
    socketsConfig.rootCaSize = strlen(r->tls_cert) + 1;
    socketsConfig.sendTimeoutMs = IOTC_HTTP_CLIENT_SEND_RECV_TIMEOUT_MS;
    socketsConfig.recvTimeoutMs = IOTC_HTTP_CLIENT_SEND_RECV_TIMEOUT_MS;

    LogInfo(("Establishing a TLS session with %s.", r->host_name));

    networkStatus = SecureSocketsTransport_Connect(pxNetworkContext,
        &serverInfo,
        &socketsConfig);

    if (networkStatus != TRANSPORT_SOCKET_STATUS_SUCCESS) {
        status = pdFAIL;
    }
    return status;
}


static BaseType_t prvClientRequest(const TransportInterface_t* ptransportInterface, IotConnectHttpRequest* r)
{
    BaseType_t status = pdFAIL;
    HTTPStatus_t httpStatus;


    configASSERT(r->resource != NULL);

    (void)memset(&requestHeaders, 0, sizeof(requestHeaders));
    (void)memset(&requestInfo, 0, sizeof(requestInfo));
    (void)memset(&response, 0, sizeof(response));

    requestInfo.pHost = r->host_name;
    requestInfo.hostLen = strlen(r->host_name);
    requestInfo.pMethod = r->payload ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    requestInfo.methodLen = strlen(requestInfo.pMethod);
    requestInfo.pPath = r->resource;
    requestInfo.pathLen = strlen(r->resource);

    requestHeaders.pBuffer = httpClientBuffer;
    requestHeaders.bufferLen = IOTC_HTTP_CLIENT_USER_BUFFER_SIZE;

    response.pBuffer = httpClientBuffer;
    response.bufferLen = IOTC_HTTP_CLIENT_USER_BUFFER_SIZE;

    httpStatus = HTTPClient_InitializeRequestHeaders(&requestHeaders, &requestInfo);

    if (httpStatus != HTTPSuccess) {
        LogError(("Failed to initialize HTTP request headers: Error=%s.",
            HTTPClient_strerror(httpStatus)));
        return pdFAIL;
    }
    httpStatus = HTTPClient_AddHeader(&requestHeaders, 
        "Content-Type", strlen("Content-Type"),
        "application/json", strlen("application/json")
    );

    httpStatus = HTTPClient_Send(ptransportInterface,
        &requestHeaders,
        (const uint8_t *) r->payload,
        r->payload ? strlen(r->payload) : 0,
        &response,
        0
    );

    if (httpStatus != HTTPSuccess) {
        LogError(("An error occurred in downloading the file. Failed to send HTTP GET request to %s%s: Error=%s.",
            r->host_name, r->resource, HTTPClient_strerror(httpStatus)));
        return pdFAIL;
    }

    LogDebug(("Received HTTP response from %s%s...", host, path));
    LogDebug(("Response Headers:\n%.*s", (int32_t)response.headersLen, response.pHeaders));
    LogInfo(("Response Body (%lu):\n%.*s\n",
        response.bodyLen,
        (int32_t)response.bodyLen,
        response.pBody));
    r->response = (char *) response.pBody;
    r->response[response.bodyLen] = 0; // null terminate
    status = (response.statusCode == 200) ? pdPASS : pdFAIL;

    if (status != pdPASS) {
        LogError(("Received an invalid response from the server Result: %u.", response.statusCode));
    }

    return((status == pdPASS) && (httpStatus == HTTPSuccess));
}

int iotconnect_https_request(IotConnectHttpRequest* request)
{
    TransportInterface_t transportInterface;
    NetworkContext_t networkContext = { 0 };
    TransportSocketStatus_t networkStatus;

    SecureSocketsTransportParams_t secureSocketsTransportParams = { 0 };
    BaseType_t status = pdPASS;

    networkContext.pParams = &secureSocketsTransportParams;
    request->response = NULL;

    BaseType_t tries = 0;
    do {

        // keep trying and break if tries is exceeded
        status = connectToServerWithBackoffRetriesV2(prvConnectToServer, &networkContext, request);

        if (status == pdFAIL) {
            LogError(("Failed to connect to HTTP server %s. Tries so far %d...", request->host_name, tries));
        }

        if (status == pdPASS) {
            transportInterface.pNetworkContext = &networkContext;
            transportInterface.send = SecureSocketsTransport_Send;
            transportInterface.recv = SecureSocketsTransport_Recv;

            status = prvClientRequest(&transportInterface, request);

            // cleanup/disconnect
            networkStatus = SecureSocketsTransport_Disconnect(&networkContext);

            if (networkStatus != TRANSPORT_SOCKET_STATUS_SUCCESS) {
                status = pdFAIL;
                LogError(("SecureSocketsTransport_Disconnect() failed to close the network connection. Result: %d.", 
                    (int)networkStatus)
                );
            }
            break;
        }

        if (tries < MAX_HTTP_REQUEST_TRIES) {
            LogWarn(("HTTP request iteration %lu failed. Retrying...", tries));
        }
        else {
            LogError(("All %d HTTP request iterations failed.", MAX_HTTP_REQUEST_TRIES));
            break;
        }

        vTaskDelay(HTTP_REQUEST_BACKOFF_MS);
        tries++;



    } while (status != pdPASS);

    if (status == pdPASS) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
