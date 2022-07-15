cmake_minimum_required(VERSION 3.0.2)

project(iotc-amazon-freertos-sdk)

#cJSON
set(ENABLE_CJSON_TEST OFF CACHE BOOL "CJson - Build Tests")
set(ENABLE_CUSTOM_COMPILER_FLAGS OFF CACHE BOOL "CJson - Custom Compiler Flags")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../lib/cJSON cJSON EXCLUDE_FROM_ALL)

# iotc-c-lib
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/../lib/iotc-c-lib/src CLibSources)

include_directories(iotconnect-afr-layer/include)
include_directories(include)

file(GLOB SdkSources src/*.c iotconnect-afr-layer/src/*.c)

add_library(iotc-amazon-freertos-sdk STATIC ${cJSON} ${CLibSources} ${SdkSources})

message(IOTC adding ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(iotc-amazon-freertos-sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/cJSON)
target_include_directories(iotc-amazon-freertos-sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/iotc-c-lib/include)
target_include_directories(iotc-amazon-freertos-sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(iotc-amazon-freertos-sdk)


