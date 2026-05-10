cmake_minimum_required(VERSION 3.10)
project(iot_gateway C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

option(USE_MOSQUITTO "Enable Mosquitto MQTT support" OFF)
option(ENABLE_DISPLAY "Enable framebuffer display" OFF)

set(SOURCES
    src/app/main.cpp
    src/app/sensor_reader.cpp
    src/app/data_manager.cpp
    src/app/mqtt_publisher.cpp
    src/app/aliyun_iot.cpp
    src/app/onenet_iot.cpp
    src/app/display_manager.cpp
    src/sdk/gateway_sdk.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/sdk
)

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

if(USE_MOSQUITTO)
    if(CMAKE_CROSSCOMPILING)
        set(SYSROOT_LIB "${CMAKE_SOURCE_DIR}/tools/sysroot/usr/lib/arm-linux-gnueabihf")
        set(SYSROOT_INC "${CMAKE_SOURCE_DIR}/tools/sysroot/usr/include")

        set(MOSQUITTO_H "${SYSROOT_INC}/mosquitto.h")
        set(MOSQUITTO_SO "${SYSROOT_LIB}/libmosquitto.so")
        set(LIBSSL_SO "${SYSROOT_LIB}/libssl.so")
        set(LIBCRYPTO_SO "${SYSROOT_LIB}/libcrypto.so")

        if(EXISTS "${MOSQUITTO_H}" AND EXISTS "${MOSQUITTO_SO}")
            message(STATUS "Found Mosquitto (sysroot): ${MOSQUITTO_SO}")
            target_compile_definitions(${PROJECT_NAME} PRIVATE USE_MOSQUITTO)
            target_include_directories(${PROJECT_NAME} PRIVATE ${SYSROOT_INC})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${MOSQUITTO_SO})
            if(EXISTS "${LIBSSL_SO}" AND EXISTS "${LIBCRYPTO_SO}")
                message(STATUS "Found OpenSSL (sysroot): ${LIBSSL_SO}")
                target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBSSL_SO} ${LIBCRYPTO_SO})
            endif()
        else()
            message(WARNING "Mosquitto not found in sysroot, building without MQTT")
        endif()
    else()
        find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h)
        find_library(MOSQUITTO_LIBRARY NAMES mosquitto)

        if(MOSQUITTO_INCLUDE_DIR AND MOSQUITTO_LIBRARY)
            message(STATUS "Found Mosquitto (system): ${MOSQUITTO_LIBRARY}")
            target_compile_definitions(${PROJECT_NAME} PRIVATE USE_MOSQUITTO)
            target_include_directories(${PROJECT_NAME} PRIVATE ${MOSQUITTO_INCLUDE_DIR})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${MOSQUITTO_LIBRARY})
        else()
            message(WARNING "Mosquitto not found, building without MQTT. Install: sudo apt install libmosquitto-dev")
        endif()

        find_package(OpenSSL REQUIRED)
        target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

if(ENABLE_DISPLAY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_DISPLAY)
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
