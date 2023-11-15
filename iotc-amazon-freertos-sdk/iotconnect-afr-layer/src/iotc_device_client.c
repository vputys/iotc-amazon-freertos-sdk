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

//#include "aws_demo.h"

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

/* shadow demo helpers header. */
//#include <mqtt_demo_helpers.h>

/* Transport interface implementation include header for TLS. */
#include "tls_socket.h"

#include <transport_interface.h>
#include "core_mqtt.h"
#include "using_mbedtls_pkcs11.h"

#include "iotconnect_sync.h"
#include "iotc_device_client.h"
#include "mqtt_demo_config.h"
#include "backoff_algorithm.h"


#define DUMP(...) SEGGER_RTT_printf(0, __VA_ARGS__); SEGGER_RTT_printf(0, "\r\n")

#define IOTC_DEVLogError(x) DUMP x
#define IOTC_DEVLogWarn(x) DUMP x
#define IOTC_DEVLogInfo(x) DUMP x
#define IOTC_DEVLogDebug(x) DUMP x

/*-----------------------------------------------------------*/
typedef struct SecureSocketsTransportParams {
    TlsTransportParams_t *pParams
} SecureSocketsTransportParams_t;

#if 0
struct NetworkContext
{
    SecureSocketsTransportParams_t* pParams;
};
#endif

extern NetworkCredentials_t xNetworkCredentials;

struct NetworkContext
{
    TlsTransportParams_t *pParams;
    //SecureSocketsTransportParams_t* pParams;
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
    BaseType_t ret = MQTT_Disconnect( &xMqttContext );
    if (ret == MQTTSuccess) {
        LogError(("Encountered a failure while trying to disconnect the MQTT session."));
    }
    is_connected = false;
    return (ret == pdPASS ? EXIT_SUCCESS : EXIT_FAILURE);
}

bool iotc_device_client_is_connected() {
    return (xMqttContext.connectStatus == MQTTConnected);
}

int iotc_device_client_send_message(const char* message) {
    #ifdef FIXED_PUB
    BaseType_t ret = PublishToTopic(
        &xMqttContext,
        iotc_sync_get_pub_topic(),
        strlen(iotc_sync_get_pub_topic()),
        message,
        strlen(message)
        );

    bool connected = xMqttContext.connectStatus == MQTTConnected;
    if (pdPASS != ret) {
        LogError(("Failed to send message %s. Connection status: %s", message, connected ? "CONNECTED" : "DISCONNECTED"));
    }
    return (ret == pdPASS ? EXIT_SUCCESS : EXIT_FAILURE);
    #endif
    return 1;
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
#define mqttexampleTOPIC_COUNT 1
#define mqttexampleRETRY_BACKOFF_BASE_MS                  ( 500U )
#define mqttexampleRETRY_MAX_BACKOFF_DELAY_MS 5000U
#define mqttexampleRETRY_MAX_ATTEMPTS 5U
#define mqttexamplePROCESS_LOOP_TIMEOUT_MS                ( 200U )
#define mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS         ( 200U )

static MQTTStatus_t prvProcessLoopWithTimeout( MQTTContext_t * pMqttContext,
                                               uint32_t ulTimeoutMs )
{
    uint32_t ulMqttProcessLoopTimeoutTime;
    uint32_t ulCurrentTime;

    MQTTStatus_t eMqttStatus = MQTTSuccess;

    ulCurrentTime = pMqttContext->getTime();
    ulMqttProcessLoopTimeoutTime = ulCurrentTime + ulTimeoutMs;

    /* Call MQTT_ProcessLoop multiple times a timeout happens, or
     * MQTT_ProcessLoop fails. */
    while( ( ulCurrentTime < ulMqttProcessLoopTimeoutTime ) &&
           ( eMqttStatus == MQTTSuccess) )
    {
        eMqttStatus = MQTT_ProcessLoop( pMqttContext, 100);
#if 0
        if( eMqttStatus == MQTTNeedMoreBytes )
        {
            eMqttStatus = MQTTSuccess;
        }
#endif

        ulCurrentTime = pMqttContext->getTime();
    }
    return eMqttStatus;
}

// taken from BasicTLSMQTTExample.c 
static void prvMQTTSubscribeWithBackoffRetries( MQTTContext_t * pxMQTTContext , char *iotc_topic, uint32_t iotc_topic_len)
{
    MQTTStatus_t xResult = MQTTSuccess;
    BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;
    BackoffAlgorithmContext_t xRetryParams;
    uint16_t usNextRetryBackOff = 0U;
    MQTTSubscribeInfo_t xMQTTSubscription[ mqttexampleTOPIC_COUNT ];
    bool xFailedSubscribeToTopic = false;
    uint32_t ulTopicCount = 0U;
    uint16_t usSubscribePacketIdentifier = 0;
    /* Some fields not used by this demo so start with everything at 0. */
    ( void ) memset( ( void * ) &xMQTTSubscription, 0x00, sizeof( xMQTTSubscription ) );

    /* Get a unique packet id. */
    
    usSubscribePacketIdentifier = MQTT_GetPacketId( pxMQTTContext );

    /* Populate subscription list. */
    for(ulTopicCount = 0; ulTopicCount < mqttexampleTOPIC_COUNT; ulTopicCount++)
    {
        xMQTTSubscription[ ulTopicCount ].qos = MQTTQoS2;
        xMQTTSubscription[ ulTopicCount ].pTopicFilter = iotc_topic;
        xMQTTSubscription[ ulTopicCount ].topicFilterLength = iotc_topic_len;
    }

    /* Initialize context for backoff retry attempts if SUBSCRIBE request fails. */
    BackoffAlgorithm_InitializeParams( &xRetryParams,
                                       mqttexampleRETRY_BACKOFF_BASE_MS,
                                       mqttexampleRETRY_MAX_BACKOFF_DELAY_MS,
                                       mqttexampleRETRY_MAX_ATTEMPTS );

    do
    {
        /* The client is now connected to the broker. Subscribe to the topic
         * as specified in mqttexampleTOPIC at the top of this file by sending a
         * subscribe packet then waiting for a subscribe acknowledgment (SUBACK).
         * This client will then publish to the same topic it subscribed to, so it
         * will expect all the messages it sends to the broker to be sent back to it
         * from the broker. This demo uses QOS2 in Subscribe, therefore, the Publish
         * messages received from the broker will have QOS2. */
        xResult = MQTT_Subscribe( pxMQTTContext,
                                  xMQTTSubscription,
                                  sizeof( xMQTTSubscription ) / sizeof( MQTTSubscribeInfo_t ),
                                  usSubscribePacketIdentifier );
        configASSERT( xResult == MQTTSuccess );

        for(ulTopicCount = 0; ulTopicCount < mqttexampleTOPIC_COUNT; ulTopicCount++)
        {
            IOTC_DEVLogInfo( ( "SUBSCRIBE sent for topic %s to broker.\n\n",
                       iotc_topic ) );
        }

        /* Process incoming packet from the broker. After sending the subscribe, the
         * client may receive a publish before it receives a subscribe ack. Therefore,
         * call generic incoming packet processing function. Since this demo is
         * subscribing to the topic to which no one is publishing, probability of
         * receiving Publish message before subscribe ack is zero; but application
         * must be ready to receive any packet.  This demo uses the generic packet
         * processing function everywhere to highlight this fact. */
        xResult = prvProcessLoopWithTimeout( pxMQTTContext, mqttexamplePROCESS_LOOP_TIMEOUT_MS );
        configASSERT( xResult == MQTTSuccess );

        /* Reset flag before checking suback responses. */
        xFailedSubscribeToTopic = false;

        /* Check if recent subscription request has been rejected. #xTopicFilterContext is updated
         * in the event callback to reflect the status of the SUBACK sent by the broker. It represents
         * either the QoS level granted by the server upon subscription, or acknowledgement of
         * server rejection of the subscription request. */
        for( ulTopicCount = 0; ulTopicCount < mqttexampleTOPIC_COUNT; ulTopicCount++ )
        {
            #if 0
            if( xTopicFilterContext[ ulTopicCount ].xSubAckStatus == MQTTSubAckFailure )
            {
                xFailedSubscribeToTopic = true;

                /* Generate a random number and calculate backoff value (in milliseconds) for
                 * the next connection retry.
                 * Note: It is recommended to seed the random number generator with a device-specific
                 * entropy source so that possibility of multiple devices retrying failed network operations
                 * at similar intervals can be avoided. */
                
                xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &xRetryParams, uxRand(), &usNextRetryBackOff );

                if( xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted )
                {
                    IOTC_DEVLogInfo( ( "Server rejected subscription request. All retry attempts have exhausted. Topic=%s",
                                xTopicFilterContext[ ulTopicCount ].pcTopicFilter ) );
                }
                else if( xBackoffAlgStatus == BackoffAlgorithmSuccess )
                {
                    IOTC_DEVLogWarn( ( "Server rejected subscription request. Attempting to re-subscribe to topic %s.",
                               xTopicFilterContext[ ulTopicCount ].pcTopicFilter ) );
                    /* Backoff before the next re-subscribe attempt. */
                    vTaskDelay( pdMS_TO_TICKS( usNextRetryBackOff ) );
                }
                

                break;
            }
            #endif
        }

        configASSERT( xBackoffAlgStatus != BackoffAlgorithmRetriesExhausted );
    } while( ( xFailedSubscribeToTopic == true ) && ( xBackoffAlgStatus == BackoffAlgorithmSuccess ) );
}

//#define testIoTUsername "SharedAccessSignature sr=poc-iotconnect-iothub-030-eu2.azure-devices.net%2Fdevices%2Favtds-justatoken&sig=%s"

static TlsTransportStatus_t prvConnectToServerWithBackoffRetries( NetworkCredentials_t * pxNetworkCredentials,
                                                                  NetworkContext_t * pxNetworkContext )
{
    TlsTransportStatus_t xNetworkStatus;
    BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;
    BackoffAlgorithmContext_t xReconnectParams;
    uint16_t usNextRetryBackOff = 0U;

    /* Set the credentials for establishing a TLS connection. */
    pxNetworkCredentials->pRootCa = ( const unsigned char * ) democonfigROOT_CA_PEM;
    pxNetworkCredentials->rootCaSize = sizeof( democonfigROOT_CA_PEM );
    pxNetworkCredentials->disableSni = democonfigDISABLE_SNI;
#if 1 // used in Renesas mbedTLS
#if 1
    pxNetworkCredentials->pUserName = iotc_sync_get_username();
    pxNetworkCredentials->userNameSize = strlen(pxNetworkCredentials->pUserName);
    //char pass_buff[250];
    //pass_buff = iotc_sync_get_pass();
    pxNetworkCredentials->pPassword = iotc_sync_get_pass();
    pxNetworkCredentials->passwordSize = strlen(pxNetworkCredentials->pPassword);
    IOTC_DEVLogInfo(("pass obtained from sync: _%s_", pxNetworkCredentials->pPassword));
#endif

    //
    // Names/values in strings are from AWS PKCS11 to MbedTLS stack properties!!!
    //
    pxNetworkCredentials->pClientCertLabel = "Device Cert";   /**< @brief String representing the PKCS #11 label for the client certificate. */
    pxNetworkCredentials->pPrivateKeyLabel = "Device Priv TLS Key";   /**< @brief String representing the PKCS #11 label for the private key. */
#if 0
    // Really unclear how these are used, epecially choosing between
    // - pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS
    // - pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS
    // - pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS
    // - pkcs11configLABEL_CODE_VERIFICATION_KEY
    // - pkcs11configLABEL_JITP_CERTIFICATE
    // - pkcs11configLABEL_ROOT_CERTIFICATE
    //
    pxNetworkCredentials->pClientCertLabel = pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS;   /**< @brief String representing the PKCS #11 label for the client certificate. */
    pxNetworkCredentials->pPrivateKeyLabel = pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS;   /**< @brief String representing the PKCS #11 label for the private key. */
#endif
#endif

    /* Initialize reconnect attempts and interval.*/
    BackoffAlgorithm_InitializeParams( &xReconnectParams,
                                       mqttexampleRETRY_BACKOFF_BASE_MS,
                                       mqttexampleRETRY_MAX_BACKOFF_DELAY_MS,
                                       mqttexampleRETRY_MAX_ATTEMPTS );

    /* Attempt to connect to the MQTT broker. If connection fails, retry after
     * a timeout. Timeout value will exponentially increase until maximum
     * attempts are reached.
     */
    do
    {
        /* Establish a TLS session with the MQTT broker. This example connects to
         * the MQTT broker as specified in democonfigMQTT_BROKER_ENDPOINT and
         * democonfigMQTT_BROKER_PORT at the top of this file. */
        IOTC_DEVLogInfo( ( "Creating a TLS connection to %s:%u.\r\n",
                   democonfigMQTT_BROKER_ENDPOINT,
                   democonfigMQTT_BROKER_PORT ) );
        /* Attempt to create a server-authenticated TLS connection. */
        xNetworkStatus = TLS_Socket_Connect(pxNetworkContext,democonfigMQTT_BROKER_ENDPOINT,
                                               democonfigMQTT_BROKER_PORT,
                                               pxNetworkCredentials,
                                               mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS,
                                               mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS );

        if( xNetworkStatus != TLS_TRANSPORT_SUCCESS )
        {
            /* Generate a random number and calculate backoff value (in milliseconds) for
             * the next connection retry. */
            xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &xReconnectParams, uxRand(), &usNextRetryBackOff );

            if( xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted )
            {
                IOTC_DEVLogInfo( ( "Connection to the broker failed, all attempts exhausted." ) );
            }
            else if( xBackoffAlgStatus == BackoffAlgorithmSuccess )
            {
                IOTC_DEVLogWarn( ( "Connection to the broker failed. "
                           "Retrying connection with backoff and jitter." ) );
                vTaskDelay( pdMS_TO_TICKS( usNextRetryBackOff ) );
            }
        }
    } while( ( xNetworkStatus != TLS_TRANSPORT_SUCCESS ) && ( xBackoffAlgStatus == BackoffAlgorithmSuccess ) );

    return xNetworkStatus;
}

char netw_buff[256];
static uint32_t ulGlobalEntryTimeMs;
#define MILLISECONDS_PER_SECOND                           ( 1000U )
#define MILLISECONDS_PER_TICK                             ( MILLISECONDS_PER_SECOND / configTICK_RATE_HZ )

static uint32_t prvGetTimeMs( void )
{
    TickType_t xTickCount = 0;
    uint32_t ulTimeMs = 0UL;

    /* Get the current tick count. */
    xTickCount = xTaskGetTickCount();

    /* Convert the ticks to milliseconds. */
    ulTimeMs = ( uint32_t ) xTickCount * MILLISECONDS_PER_TICK;

    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
     * elapsed time in the application. */
    ulTimeMs = ( uint32_t ) ( ulTimeMs - ulGlobalEntryTimeMs );

    return ulTimeMs;
}

int iotc_device_client_init(IotConnectDeviceClientConfig* c) {
    BaseType_t ret = 0;
    ulGlobalEntryTimeMs = prvGetTimeMs();
    // TODO: explain why 1
    xMqttContext.nextPacketId = 1;
    xMqttContext.networkBuffer.pBuffer = netw_buff;
    xMqttContext.networkBuffer.size = 256;
    xMqttContext.transportInterface.recv = TLS_Socket_recv;
    xMqttContext.transportInterface.send = TLS_Socket_send;
    xMqttContext.getTime = prvGetTimeMs;
     xMqttContext.transportInterface.pNetworkContext = (NetworkContext_t*)calloc(1, sizeof(NetworkContext_t));
    if (!xMqttContext.transportInterface.pNetworkContext){
        IOTC_DEVLogError(("Failed to allocate mqtt NetworkContext_t"));
        return -1;
    }
    #if 0
    xMqttContext.transportInterface.pNetworkContext->pParams = (TlsTransportParams_t*)calloc(1, sizeof(TlsTransportParams_t));
    if (!xMqttContext.transportInterface.pNetworkContext->pParams){
        IOTC_DEVLogError(("Failed to allocate mqtt TlsTransportParams_t"));
        free(xMqttContext.transportInterface.pNetworkContext);
        xMqttContext.transportInterface.pNetworkContext = NULL;
        return -1;
    }
    #endif

    c2d_msg_cb = NULL;
    status_cb = NULL;
    int xNetworkStatus = 0;
    if( FreeRTOS_IsNetworkUp() == pdFALSE )
    {
        IOTC_DEVLogInfo( ( "Waiting for the network link up event..." ) );

        while( FreeRTOS_IsNetworkUp() == pdFALSE )
        {
            vTaskDelay( pdMS_TO_TICKS( 1000U ) );
        }
    }


    if (is_connected) {
        ret = MQTT_Disconnect( &xMqttContext );
        //ret = DisconnectMqttSession(&xMqttContext, &xNetworkContext);
        if (ret == MQTTSuccess) {
            LogError(("Failed to disconnect a stale MQTT session."));
        }
    }
    is_connected = false;

    if(xNetworkContext.pParams){
        free(xNetworkContext.pParams);
        xNetworkContext.pParams = NULL;
    }

    xNetworkContext.pParams = (TlsTransportParams_t*)calloc(1, sizeof(TlsTransportParams_t));

    if (!xNetworkContext.pParams){
        IOTC_DEVLogInfo(("Failed to calloc xnetworkcontext"));
        return -1;
    }

    xMqttContext.transportInterface.pNetworkContext = &xNetworkContext;


    /* Remove compiler warnings about unused parameters. */
    // TODO: look here
    xNetworkStatus = prvConnectToServerWithBackoffRetries( &xNetworkCredentials,
                                                               &xNetworkContext );

    /*
    ret = EstablishMqttSession(&xMqttContext,
        &xNetworkContext,
        &xBuffer,
        prvEventCallback);
    */
   /*
    if (ret == pdFAIL) {
        //LogError(("Failed to connect to MQTT broker."));
        //return EXIT_FAILURE;
    }*/
    IOTC_DEVLogInfo(("Connected to MQTT with EstablishMqttSession."));

    // TODO: fails on this now
    prvMQTTSubscribeWithBackoffRetries(&xMqttContext, iotc_sync_get_sub_topic(), strlen(iotc_sync_get_sub_topic()));

    /*
    ret = SubscribeToTopic(&xMqttContext,
       iotc_sync_get_sub_topic(),
       (uint16_t)strlen(iotc_sync_get_sub_topic())
    ); */
    #if 0
    if (pdPASS != ret) {
        LogError(("Failed to subscribe to topic %s", iotc_sync_get_sub_topic()));
    } else {
        is_connected = true;
        xMqttContext.connectStatus = MQTTConnected;
    }
    #endif

    is_connected = true;
        xMqttContext.connectStatus = MQTTConnected;

    // I assume code below is done in prvMQTTSubscribeWithBackoffRetries above
#if 0
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
#endif
    

    c2d_msg_cb = c->c2d_msg_cb;
    status_cb = c->status_cb;

    return EXIT_SUCCESS;
}


