afr_glob_src(iotc_c_lib_srcs DIRECTORY "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/lib/iotc-c-lib/src" RECURSE)
afr_glob_src(iotc_c_lib_headers DIRECTORY "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/lib/iotc-c-lib/include" RECURSE)
afr_glob_src(iotc_sdk_srcs DIRECTORY "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/iotc-amazon-freertos-sdk" RECURSE)
afr_glob_src(iotc_sdk_srcs DIRECTORY "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/iotc-amazon-freertos-sdk" RECURSE)
afr_glob_src(iotc_demo_srcs DIRECTORY "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/iotconnect-demo" RECURSE)
set(cjson_srcs
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/lib/cJSON/cJSON.h"
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/lib/cJSON/cJSON_Utils.h"
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/lib/cJSON/cJSON.c"
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/lib/cJSON/cJSON_Utils.c"
        )


add_library(
        afr_3rdparty_iotc_amazon_freertos_sdk STATIC EXCLUDE_FROM_ALL
        ${iotc_c_lib_srcs}
        ${iotc_c_lib_headers}
        ${iotc_sdk_srcs}
        ${iotc_sdk_srcs}
        ${iotc_demo_srcs}
        ${cjson_srcs}
)

target_include_directories(
        afr_3rdparty_iotc_amazon_freertos_sdk
        PRIVATE
        "${AFR_3RDPARTY_DIR}/../../demos/common/mqtt_demo_helpers"
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/iotc-amazon-freertos-sdk/iotconnect-afr-layer/include"
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/lib/cJSON"
        PUBLIC
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/iotc-amazon-freertos-sdk/include"
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/lib/iotc-c-lib/include"
        "${AFR_3RDPARTY_DIR}/iotc-amazon-freertos-sdk/iotconnect-demo/config"
)
target_link_libraries(
        afr_3rdparty_iotc_amazon_freertos_sdk
        PRIVATE
        AFR::kernel
        AFR::platform
        AFR::core_mqtt
        AFR::transport_interface_secure_sockets
        AFR::backoff_algorithm
        AFR::core_http
        AFR::pkcs11_helpers
)

add_library(3rdparty::iotc_amazon_freertos_sdk ALIAS afr_3rdparty_iotc_amazon_freertos_sdk)