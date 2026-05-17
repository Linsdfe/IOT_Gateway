cmake_minimum_required(VERSION 3.10)
project(iot_gateway C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

option(USE_MOSQUITTO "Enable Mosquitto MQTT support" OFF)
option(ENABLE_DISPLAY "Enable framebuffer display" OFF)
option(USE_LVGL "Enable LVGL graphical interface" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build: -g -O0 enabled")
    set(CMAKE_C_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
endif()

set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
file(MAKE_DIRECTORY ${PLUGIN_OUTPUT_DIR})

set(SOURCES
    src/app/main.cpp
    src/app/logger.cpp
    src/app/sensor_reader.cpp
    src/app/data_manager.cpp
    src/app/mqtt_publisher.cpp
    src/app/onenet_iot.cpp
    src/sdk/gateway_sdk.cpp
    src/plugin/plugin_loader.cpp
)

if(USE_LVGL)
    list(APPEND SOURCES src/app/lvgl_display.cpp)
else()
    list(APPEND SOURCES src/app/display_manager.cpp)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/sdk
    ${CMAKE_SOURCE_DIR}/src/plugin
)

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})

# Plugin 1: sht30_bh1750_i2c - User-space I2C protocol (no kernel driver)
add_library(sht30_bh1750_i2c_plugin SHARED src/plugin/sht30_bh1750_i2c_plugin.cpp)
target_include_directories(sht30_bh1750_i2c_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/plugin)
set_target_properties(sht30_bh1750_i2c_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR} PREFIX "lib"
)
target_link_libraries(sht30_bh1750_i2c_plugin PRIVATE Threads::Threads)

# Plugin 2: sht30_bh1750_kernel - Linux kernel standard IIO drivers
add_library(sht30_bh1750_kernel_plugin SHARED src/plugin/sht30_bh1750_kernel_plugin.cpp)
target_include_directories(sht30_bh1750_kernel_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/plugin)
set_target_properties(sht30_bh1750_kernel_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR} PREFIX "lib"
)

# Plugin 3: sht30_bh1750_custom - Custom kernel drivers
add_library(sht30_bh1750_custom_plugin SHARED src/plugin/sht30_bh1750_custom_plugin.cpp)
target_include_directories(sht30_bh1750_custom_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/plugin)
set_target_properties(sht30_bh1750_custom_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR} PREFIX "lib"
)

# Plugin 4: simulated - Simulated sensor data
add_library(simulated_plugin SHARED src/plugin/simulated_plugin.cpp)
target_include_directories(simulated_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/plugin)
set_target_properties(simulated_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR} PREFIX "lib"
)

if(CMAKE_CROSSCOMPILING)
    set(SYSROOT_LIB "${CMAKE_SOURCE_DIR}/tools/sysroot/usr/lib/arm-linux-gnueabihf")
    set(SYSROOT_INC "${CMAKE_SOURCE_DIR}/tools/sysroot/usr/include")

    set(LIBSSL_SO "${SYSROOT_LIB}/libssl.so")
    set(LIBCRYPTO_SO "${SYSROOT_LIB}/libcrypto.so")

    if(EXISTS "${LIBSSL_SO}" AND EXISTS "${LIBCRYPTO_SO}")
        message(STATUS "Found OpenSSL (sysroot): ${LIBSSL_SO}")
        target_include_directories(${PROJECT_NAME} PRIVATE ${SYSROOT_INC})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBSSL_SO} ${LIBCRYPTO_SO})
    else()
        message(WARNING "OpenSSL not found in sysroot")
    endif()

    if(USE_MOSQUITTO)
        set(MOSQUITTO_H "${SYSROOT_INC}/mosquitto.h")
        set(MOSQUITTO_SO "${SYSROOT_LIB}/libmosquitto.so")

        if(EXISTS "${MOSQUITTO_H}" AND EXISTS "${MOSQUITTO_SO}")
            message(STATUS "Found Mosquitto (sysroot): ${MOSQUITTO_SO}")
            target_compile_definitions(${PROJECT_NAME} PRIVATE USE_MOSQUITTO)
            target_link_libraries(${PROJECT_NAME} PRIVATE ${MOSQUITTO_SO})
        else()
            message(WARNING "Mosquitto not found in sysroot, building without MQTT")
        endif()
    endif()

    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--allow-shlib-undefined")
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)

    if(USE_MOSQUITTO)
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
    endif()
endif()

if(USE_LVGL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_LVGL USE_LVGL ENABLE_DISPLAY)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/lvgl
        ${CMAKE_SOURCE_DIR}/src/app
    )
    set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/src/app/lv_conf.h" CACHE FILEPATH "Path to lv_conf.h" FORCE)
    add_subdirectory(third_party/lvgl)
    target_link_libraries(${PROJECT_NAME} PRIVATE lvgl)
    target_include_directories(lvgl PUBLIC ${CMAKE_SOURCE_DIR}/src/app)
    message(STATUS "LVGL graphical interface: ENABLED")
elseif(ENABLE_DISPLAY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_DISPLAY)
    message(STATUS "Framebuffer display: ENABLED")
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(TARGETS sht30_bh1750_i2c_plugin sht30_bh1750_kernel_plugin sht30_bh1750_custom_plugin simulated_plugin
    LIBRARY DESTINATION lib/iot/plugins
)
