# About

This repository contains the IoTConnect SDK for Amazon FreeRTOS.

The SDK should support all boards supported by Amazon FreeRTOS, but it has been tested only with:
- Windows PC Simulator (CMake)
- Microchip ATECC608A Secure Element with Windows Simulator (both CMake and Visual Studio solution project)

### Build Instructions

- Clone the Amazon FreeRTOS repo.
- Clone this repo into the libraries/3rdparty directory of the Amazon FreeRTOS repo ensuring that submodules are pulled also:
```shell script
git clone https://github.com/aws/amazon-freertos.git --recurse-submodules
```
- Follow the Amazon FreeRTOS instructions for your board to be able to run the demo. 
For example, if running the PC simulator, you will need to set configNETWORK_INTERFACE_TO_USE in vendors/pc/boards/FreeRTOSConfig.h, 
or if using WiFi on the device, ensure to set the SSID and password in aws_clientcredential.h 
- Edit **demos/include/iot_demo_runner.h** 
and replace *RunCoreMqttMutualAuthDemo* with *RunIotConnectDemo*:
```c
...
#if defined( CONFIG_CORE_MQTT_MUTUAL_AUTH_DEMO_ENABLED )
    #define DEMO_entryFUNCTION              RunIotConnectDemo
    #if defined( democonfigMQTT_ECHO_TASK_STACK_SIZE )
        #undef democonfigDEMO_STACKSIZE
        #define democonfigDEMO_STACKSIZE    democonfigMQTT_ECHO_TASK_STACK_SIZE
    #endif
...
```  
- Edit **demos/include/aws_credential.h** and specify any value for clientcredentialIOT_THING_NAME. 
This value is used in some setups as the DHCP host name, but it is not tied to the device name for the IoTConnection.
```c
#ifndef clientcredentialIOT_THING_NAME
    #define clientcredentialIOT_THING_NAME    "foo"
#endif
```
- Edit **demos/include/aws_credential_keys.h** per your device's credentials. 
Make sure to not use keyJITR_DEVICE_CERTIFICATE_AUTHORITY_PEM - set it to NULL.
If using a secure element, you may need to follow the AWS instructions, and understand 
how to generate a certificate from a public key provided on the console during the first run.
- Append to the list of includes in **demos/common/mqtt_demo_helpers/mqtt_demo_helpers.c**:
```c
...
#include "iotconnect_sync.h"
#include "iotconnect_certs.h"
...
```
- Additionally, in **demos/common/mqtt_demo_helpersmqtt_demo_helpers.c**, locate sections below and replace the code in them with the following two snippets:
```c
...
    /* Initialize information to connect to the MQTT broker. */
    xServerInfo.pHostName = iotc_sync_get_iothub_host();
    xServerInfo.hostNameLength = strlen(iotc_sync_get_iothub_host());
    xServerInfo.port = democonfigMQTT_BROKER_PORT;

    /* Set the Secure Socket configurations. */
    xSocketConfig.enableTls = true;
    xSocketConfig.pRootCa = CERT_BALTIMORE_ROOT_CA;
    xSocketConfig.rootCaSize = sizeof(CERT_BALTIMORE_ROOT_CA);
...
            /* The client identifier is used to uniquely identify this MQTT client to
             * the MQTT broker. In a production device the identifier can be something
             * unique, such as a device serial number. */
            xConnectInfo.pClientIdentifier = iotc_sync_get_client_id();
            xConnectInfo.clientIdentifierLength = ( uint16_t ) strlen(xConnectInfo.pClientIdentifier);

            /* Use the metrics string as username to report the OS and MQTT client version
             * metrics to AWS IoT. */
            xConnectInfo.pUserName = iotc_sync_get_username();
            xConnectInfo.userNameLength = (uint16_t)strlen(xConnectInfo.pUserName);
...
```
- If not using CMake, ensure that all the files in libraries/iotc-amazon-freertos-sdk are 
added appropriately to your project as headers/sources. You can see the list of source directories 
and files that need to be compile and include paths in the [CmakeLists.txt](CmakeLists.txt) file in this directory.
- If using CMake, follow the steps in the CMake section below.   

### CMake Build Steps

- You may wish to set BUILD_CLONE_SUBMODULES=OFF in your CMake build in order to speed speed up rebuilding.
- Append this snippet to **libraries/3rdparty/CMakeLists.txt**:
```cmake
...
if (EXISTS ${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk)
    add_subdirectory(iotc-amazon-freertos-sdk)
endif()
...
```
- Append *3rdparty::iotc_amazon_freertos_sdk* to the list of dependencies
in **demos/common/mqtt_demo_helpers/CMakeLists.txt**:
```cmake
...
afr_module_dependencies(
    ${AFR_CURRENT_MODULE}
    PUBLIC
        AFR::core_mqtt
        AFR::transport_interface_secure_sockets
        AFR::backoff_algorithm
        AFR::pkcs11_helpers
        3rdparty::iotc_amazon_freertos_sdk
...
)
```
- Build the the aws_demos target