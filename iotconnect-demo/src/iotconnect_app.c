#include <stdlib.h>
#include <stdio.h>
#include <string.h>

//#include "iot_network.h"

#include "iotconnect.h"
#include "iotconnect_common.h"
#include "app_config.h"

#define APP_VERSION "00.01.00"

#define DUMP(...) SEGGER_RTT_printf(0, __VA_ARGS__); SEGGER_RTT_printf(0, "\r\n")

#define IOTC_APPLogError(x) DUMP x
#define IOTC_APPLogWarn(x) DUMP x
#define IOTC_APPLogInfo(x) DUMP x
#define IOTC_APPLogDebug(x) DUMP x


static void on_connection_status(IotConnectConnectionStatus status) {
    // Add your own status handling
    switch (status) {
        case IOTC_CS_MQTT_CONNECTED:
            IOTC_APPLogInfo(("IoTConnect Client Connected\n"));
            break;
        case IOTC_CS_MQTT_DISCONNECTED:
            IOTC_APPLogInfo(("IoTConnect Client Disconnected\n"));
            break;
        default:
            IOTC_APPLogInfo(("IoTConnect Client ERROR\n"));
            break;
    }
}

static void command_status(IotclEventData data, bool status, const char *command_name, const char *message) {
    const char *ack = iotcl_create_ack_string_and_destroy_event(data, status, message);
    IOTC_APPLogInfo(("command: %s status=%s: %s\n", command_name, status ? "OK" : "Failed", message));
    IOTC_APPLogInfo(("Sent CMD ack: %s\n", ack));
    iotconnect_sdk_send_packet(ack);
    free((void *) ack);
}

static void on_command(IotclEventData data) {
    char *command = iotcl_clone_command(data);
    if (NULL != command) {
        command_status(data, false, command, "Not implemented");
        free((void *) command);
    } else {
        command_status(data, false, "?", "Internal error");
    }
}

static bool is_app_version_same_as_ota(const char *version) {
    return strcmp(APP_VERSION, version) == 0;
}

static bool app_needs_ota_update(const char *version) {
    return strcmp(APP_VERSION, version) < 0;
}

static void on_ota(IotclEventData data) {
    const char *message = NULL;
    char *url = iotcl_clone_download_url(data, 0);
    bool success = false;
    if (NULL != url) {
        IOTC_APPLogInfo(("Download URL is: %s\n", url));
        const char *version = iotcl_clone_sw_version(data);
        if (is_app_version_same_as_ota(version)) {
            IOTC_APPLogInfo(("OTA request for same version %s. Sending success\n", version));
            success = true;
            message = "Version is matching";
        } else if (app_needs_ota_update(version)) {
            IOTC_APPLogInfo(("OTA update is required for version %s.\n", version));
            success = false;
            message = "Not implemented";
        } else {
            IOTC_APPLogInfo(("Device firmware version %s is newer than OTA version %s. Sending failure\n", APP_VERSION,
                   version));
            // Not sure what to do here. The app version is better than OTA version.
            // Probably a development version, so return failure?
            // The user should decide here.
            success = false;
            message = "Device firmware version is newer";
        }

        free((void *) url);
        free((void *) version);
    } else {
        // compatibility with older events
        // This app does not support FOTA with older back ends, but the user can add the functionality
        const char *command = iotcl_clone_command(data);
        if (NULL != command) {
            // URL will be inside the command
            IOTC_APPLogInfo(("Command is: %s\n", command));
            message = "Old back end URLS are not supported by the app";
            free((void *) command);
        }
    }
    const char *ack = iotcl_create_ack_string_and_destroy_event(data, success, message);
    if (NULL != ack) {
        IOTC_APPLogInfo(("Sent OTA ack: %s\n", ack));
        iotconnect_sdk_send_packet(ack);
        free((void *) ack);
    }
}


static void publish_telemetry() {
    IotclMessageHandle msg = iotcl_telemetry_create(iotconnect_sdk_get_lib_config());
    IOTC_APPLogInfo(( "publish_telemetry: %d", uxTaskGetStackHighWaterMark(NULL) )); 
    // Optional. The first time you create a data point, the current timestamp will be automatically added
    // TelemetryAddWith* calls are only required if sending multiple data points in one packet.
    iotcl_telemetry_add_with_iso_time(msg, iotcl_iso_timestamp_now());
    iotcl_telemetry_set_string(msg, "version", APP_VERSION);
    iotcl_telemetry_set_number(msg, "cpu", 3.123); // test floating point numbers

    const char *str = iotcl_create_serialized_string(msg, false);
    iotcl_telemetry_destroy(msg);
    IOTC_APPLogInfo(("Sending: %s\n", str));
    iotconnect_sdk_send_packet(str); // underlying code will report an error
    iotcl_destroy_serialized(str);
}

int iotconnect_app_main(void) {

    IotConnectClientConfig *config = iotconnect_sdk_init_and_get_config();
    config->cpid = IOTCONNECT_CPID;
    config->env = IOTCONNECT_ENV;
    config->duid = IOTCONNECT_DUID;

    config->status_cb = on_connection_status;
    config->ota_cb = on_ota;
    config->cmd_cb = on_command;


    // run a dozen connect/send/disconnect cycles with each cycle being about a minute
    for (int j = 0; j < 10; j++) {
        int ret = iotconnect_sdk_init();
        if (0 != ret) {
            IOTC_APPLogError((stderr, "IoTConnect exited with error code %d\n", ret));
            return ret;
        }

        // send 10 messages
        for (int i = 0; iotconnect_sdk_is_connected() && i < 10; i++) {

            publish_telemetry();
            // repeat approximately evey ~5 seconds
            #ifdef FIXED_PROCESS_LOOP
            for (int k = 0; k < 5; k++) {
                iotconnect_sdk_loop(1000);
            }
            #endif
        }
        iotconnect_sdk_disconnect();
    }

    return 0;
}

int RunIotConnectDemo()
{
    //( void ) awsIotMqttMode;
    //( void ) pIdentifier;
    //( void ) pNetworkServerInfo;
    //( void ) pNetworkCredentialInfo;
    //( void ) pNetworkInterface;

    return iotconnect_app_main();
}
