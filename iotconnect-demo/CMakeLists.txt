cmake_minimum_required(VERSION 3.0.2)

project(iotconnect-demo)

add_library(iotconnect-demo src/iotconnect_app.c)

target_include_directories(iotconnect-demo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/config)

link_libraries(iotconnect-demo)
