//
// Copyright: Avnet 2022
// Created by Nik Markovic <nikola.markovic@avnet.com> on 6/15/22.
//

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

/* Include config as the first non-system header. */
#include "app_config.h"

#include "iotconnect_discovery.h"
#include "iotconnect_certs.h"
#include "iotc_http_request.h"
#include "iotconnect_sync.h"

#define RESOURCE_PATH_DSICOVERY "/api/sdk/cpid/%s/lang/M_C/ver/2.0/env/%s"
#define RESOURCE_PATH_SYNC "%ssync"

static IotclDiscoveryResponse* discovery_response = NULL;
static IotclSyncResponse* sync_response = NULL;
static IotclSyncResult last_sync_result = IOTCL_SR_UNKNOWN_DEVICE_STATUS;


static void dump_response(const char* message, IotConnectHttpRequest* response) {
    printf("%s", message);
    if (response->response) {
        printf(" Response was:\r\n----\r\n%s\r\n----\r\n", response->response);
    }
    else {
        printf(" Response was empty\r\n");
    }
}

static void report_sync_error(IotclSyncResponse* response, const char* sync_response_str) {
    if (NULL == response) {
        printf("Failed to obtain sync response?\r\n");
        return;
    }
    switch (response->ds) {
    case IOTCL_SR_DEVICE_NOT_REGISTERED:
        printf("IOTC_SyncResponse error: Not registered\r\n");
        break;
    case IOTCL_SR_AUTO_REGISTER:
        printf("IOTC_SyncResponse error: Auto Register\r\n");
        break;
    case IOTCL_SR_DEVICE_NOT_FOUND:
        printf("IOTC_SyncResponse error: Device not found\r\n");
        break;
    case IOTCL_SR_DEVICE_INACTIVE:
        printf("IOTC_SyncResponse error: Device inactive\r\n");
        break;
    case IOTCL_SR_DEVICE_MOVED:
        printf("IOTC_SyncResponse error: Device moved\r\n");
        break;
    case IOTCL_SR_CPID_NOT_FOUND:
        printf("IOTC_SyncResponse error: CPID not found\r\n");
        break;
    case IOTCL_SR_UNKNOWN_DEVICE_STATUS:
        printf("IOTC_SyncResponse error: Unknown device status error from server\r\n");
        break;
    case IOTCL_SR_ALLOCATION_ERROR:
        printf("IOTC_SyncResponse internal error: Allocation Error\r\n");
        break;
    case IOTCL_SR_PARSING_ERROR:
        printf("IOTC_SyncResponse internal error: Parsing error. Please check parameters passed to the request.\r\n");
        break;
    default:
        printf("WARN: report_sync_error called, but no error returned?\r\n");
        break;
    }
    printf("Raw server response was:\r\n--------------\r\n%s\r\n--------------\r\n", sync_response_str);
}

static IotclDiscoveryResponse* run_http_discovery(const char* cpid, const char* env) {
    IotConnectHttpRequest req = { 0 };

    char resource_str_buff[sizeof(RESOURCE_PATH_DSICOVERY) + CONFIG_IOTCONNECT_CPID_MAX_LEN + CONFIG_IOTCONNECT_ENV_MAX_LEN + 10 /* slack */];
    sprintf(resource_str_buff, RESOURCE_PATH_DSICOVERY, cpid, env);

    req.host_name = IOTCONNECT_DISCOVERY_HOSTNAME;
    req.resource = resource_str_buff;
    req.tls_cert = CERT_GODADDY_INT_SECURE_G2;

    int status = iotconnect_https_request(&req);

    if (status != EXIT_SUCCESS) {
        printf("Discovery: iotconnect_https_request() error code: %x data: %s\r\n", status, req.response);
        return NULL;
    }
    if (NULL == req.response || 0 == strlen(req.response)) {
        dump_response("Discovery: Unable to obtain HTTP response,", &req);
        return NULL;
    }

    char* json_start = strstr(req.response, "{");
    if (NULL == json_start) {
        dump_response("Discovery: No json response from server.", &req);
        return NULL;
    }
    if (json_start != req.response) {
        dump_response("WARN: Expected JSON to start immediately in the returned data.", &req);
    }

    IotclDiscoveryResponse* ret = iotcl_discovery_parse_discovery_response(json_start);
    if (!ret) {
        dump_response("Discovery: Unable to parse HTTP response,", &req);
    }

    return ret;
}


static IotclSyncResponse* run_http_sync(const char* cpid, const char* uniqueid) {
    IotConnectHttpRequest req = { 0 };
    char post_data[IOTCONNECT_DISCOVERY_PROTOCOL_POST_DATA_MAX_LEN + 1] = { 0 };
    char* sync_path = malloc(strlen(discovery_response->path) + strlen("sync?") + 1);
    
    if (!sync_path) {
        printf("Failed to allocate sync_path\r\n");
        return NULL;
    }
    sprintf(sync_path, RESOURCE_PATH_SYNC, discovery_response->path);
    snprintf(post_data,
        IOTCONNECT_DISCOVERY_PROTOCOL_POST_DATA_MAX_LEN, /*total length should not exceed MTU size*/
        IOTCONNECT_DISCOVERY_PROTOCOL_POST_DATA_TEMPLATE,
        cpid,
        uniqueid
    );

    req.host_name = discovery_response->host;
    req.resource = sync_path;
    req.payload = post_data;
    req.tls_cert = CERT_GODADDY_INT_SECURE_G2;

    int status = iotconnect_https_request(&req);
    free(sync_path);

    if (status != EXIT_SUCCESS) {
        printf("Sync: iotconnect_https_request() error code: %x data: %s\r\n", status, req.response);
        return NULL;
    }

    if (NULL == req.response || 0 == strlen(req.response)) {
        dump_response("Sync: Unable to obtain HTTP response,", &req);
        return NULL;
    }

    char* json_start = strstr(req.response, "{");
    if (NULL == json_start) {
        dump_response("Sync: No json response from server.", &req);
        return NULL;
    }
    if (json_start != req.response) {
        dump_response("WARN: Expected JSON to start immediately in the returned data.", &req);
    }

    IotclSyncResponse* ret = iotcl_discovery_parse_sync_response(json_start);
    if (!ret) {
        dump_response("Sync: Unable to parse HTTP response,", &req);
    }
    last_sync_result = ret->ds;
    if (!ret || ret->ds != IOTCL_SR_OK) {
        report_sync_error(ret, req.response);
        iotcl_discovery_free_sync_response(ret);
        ret = NULL;
    }

    return ret;

}

const char* iotc_sync_get_iothub_host() {
    if (!sync_response)  iotc_sync_obtain_response();
    if (!sync_response)  return NULL;
    return sync_response->broker.host;
}

const char* iotc_sync_get_username() {
    if (!sync_response)  iotc_sync_obtain_response();
    if (!sync_response)  return NULL;
    return sync_response->broker.user_name;
}

const char* iotc_sync_get_client_id() {
    if (!sync_response)  iotc_sync_obtain_response();
    if (!sync_response)  return NULL;
    return sync_response->broker.client_id;
}

const char* iotc_sync_get_pub_topic(void) {
    if (!sync_response)  iotc_sync_obtain_response();
    if (!sync_response)  return NULL;
    return sync_response->broker.pub_topic;
}

const char* iotc_sync_get_sub_topic(void) {
    if (!sync_response)  iotc_sync_obtain_response();
    if (!sync_response)  return NULL;
    return sync_response->broker.sub_topic;
}


const char* iotc_sync_get_dtg(void) {
    if (!sync_response)  iotc_sync_obtain_response();
    if (!sync_response)  return NULL;
    return sync_response->dtg;
}


int iotc_sync_obtain_response(void) {
    iotcl_discovery_free_discovery_response(discovery_response);
    iotcl_discovery_free_sync_response(sync_response);
    discovery_response = NULL;
    sync_response = NULL;

    discovery_response = run_http_discovery(IOTCONNECT_CPID, IOTCONNECT_ENV);
    if (NULL == discovery_response) {
        // get_base_url will print the error
        return -1;
    }
    printf("Discovery response parsing successful.\r\n");

    sync_response = run_http_sync(IOTCONNECT_CPID, IOTCONNECT_DUID);
    if (NULL == sync_response) {
        // Sync_call will print the error
        return -2;
    }
    printf("Sync response parsing successful.\r\n");

    run_http_sync(IOTCONNECT_CPID, IOTCONNECT_DUID);
    return (sync_response ? EXIT_SUCCESS : EXIT_FAILURE);
}
 
void iotc_sync_free_response(void) {
    if (sync_response) {
        free(sync_response);
    }
    if (discovery_response) {
        free(discovery_response);
    }
    discovery_response = NULL;
    sync_response = NULL;
    last_sync_result = IOTCL_SR_UNKNOWN_DEVICE_STATUS;
}