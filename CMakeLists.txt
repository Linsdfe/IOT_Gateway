# CMakeLists.txt - IoT 网关项目构建配置
#
# 编译选项：
#   -DUSE_MOSQUITTO=ON    启用 MQTT 支持（需 libmosquitto-dev）
#   -DUSE_LVGL=ON         启用 LVGL 图形界面（推荐，与 ENABLE_DISPLAY 互斥）
#   -DENABLE_DISPLAY=ON   启用简单 Framebuffer 显示（备选方案）
#
# 编译示例：
#   开发板本地编译：cmake .. -DUSE_MOSQUITTO=ON -DUSE_LVGL=ON
#   交叉编译：      cmake .. -DCMAKE_TOOLCHAIN_FILE=tools/arm-linux-gnueabihf.cmake -DUSE_LVGL=ON

cmake_minimum_required(VERSION 3.10)
project(iot_gateway C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# 编译选项：MQTT / 显示 / LVGL（三选二，LVGL 优先于 Framebuffer）
option(USE_MOSQUITTO "Enable Mosquitto MQTT support" OFF)
option(ENABLE_DISPLAY "Enable framebuffer display" OFF)
option(USE_LVGL "Enable LVGL graphical interface" OFF)

# Debug 模式下禁用优化，方便 GDB 调试
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build: -g -O0 enabled")
    set(CMAKE_C_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
endif()

# 核心源文件列表（不含显示模块，显示模块根据编译选项选择）
set(SOURCES
    src/app/main.cpp
    src/app/logger.cpp
    src/app/sensor_reader.cpp
    src/app/data_manager.cpp
    src/app/mqtt_publisher.cpp
    src/app/onenet_iot.cpp
    src/sdk/gateway_sdk.cpp
)

# 显示模块二选一：LVGL 图形界面 或 简单 Framebuffer
if(USE_LVGL)
    list(APPEND SOURCES src/app/lvgl_display.cpp)
else()
    list(APPEND SOURCES src/app/display_manager.cpp)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/sdk
)

# 线程支持（pthread）
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# ========== 依赖库配置：交叉编译 vs 本地编译 ==========
if(CMAKE_CROSSCOMPILING)
    # 交叉编译模式：从 sysroot 目录查找 ARM 库
    set(SYSROOT_LIB "${CMAKE_SOURCE_DIR}/tools/sysroot/usr/lib/arm-linux-gnueabihf")
    set(SYSROOT_INC "${CMAKE_SOURCE_DIR}/tools/sysroot/usr/include")

    # OpenSSL（OneNET Token 签名需要）
    set(LIBSSL_SO "${SYSROOT_LIB}/libssl.so")
    set(LIBCRYPTO_SO "${SYSROOT_LIB}/libcrypto.so")

    if(EXISTS "${LIBSSL_SO}" AND EXISTS "${LIBCRYPTO_SO}")
        message(STATUS "Found OpenSSL (sysroot): ${LIBSSL_SO}")
        target_include_directories(${PROJECT_NAME} PRIVATE ${SYSROOT_INC})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBSSL_SO} ${LIBCRYPTO_SO})
    else()
        message(WARNING "OpenSSL not found in sysroot")
    endif()

    # Mosquitto（MQTT 通信需要）
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

    # 允许交叉编译时链接宿主机不存在的共享库
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--allow-shlib-undefined")
    message(STATUS "Cross-compile: using --allow-shlib-undefined for glibc compatibility")
else()
    # 本地编译模式：从系统路径查找库
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

# ========== LVGL 图形库集成 ==========
if(USE_LVGL)
    # 定义编译宏：ENABLE_LVGL + USE_LVGL（条件编译用）+ ENABLE_DISPLAY
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_LVGL USE_LVGL ENABLE_DISPLAY)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/lvgl
        ${CMAKE_SOURCE_DIR}/src/app
    )
    # 设置 LVGL 配置文件路径（lv_conf.h 必须在 LVGL 包含路径中）
    set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/src/app/lv_conf.h" CACHE FILEPATH "Path to lv_conf.h" FORCE)
    # 添加 LVGL 子目录（使用 LVGL 自带的 CMakeLists.txt）
    add_subdirectory(third_party/lvgl)
    target_link_libraries(${PROJECT_NAME} PRIVATE lvgl)
    # 将 lv_conf.h 所在目录加入 LVGL 的包含路径
    target_include_directories(lvgl PUBLIC ${CMAKE_SOURCE_DIR}/src/app)
    message(STATUS "LVGL graphical interface: ENABLED (lv_conf=${LV_CONF_PATH})")
elseif(ENABLE_DISPLAY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_DISPLAY)
    message(STATUS "Framebuffer display: ENABLED")
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
