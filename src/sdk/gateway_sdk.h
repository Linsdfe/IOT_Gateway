/**
 * @file gateway_sdk.h
 * @brief IoT 网关 SDK - 统一封装传感器采集、云平台上报、屏幕显示
 *
 * GatewaySDK 是整个 IoT 网关项目的顶层接口，组合了以下子系统：
 * - SensorReader：I2C 传感器数据采集
 * - DataManager：多线程数据管理与分发
 * - MqttPublisher：MQTT 云平台数据上报
 * - DisplayManager / LvglDisplay：LCD 屏幕实时显示
 *
 * 编译时通过 USE_LVGL 宏选择显示后端：
 * - USE_LVGL=ON：使用 LVGL 图形界面（仪表盘、进度条、动画）
 * - 默认：使用简单 Framebuffer 显示（5x7 点阵字体）
 */

#ifndef GATEWAY_SDK_H
#define GATEWAY_SDK_H

#include "sensor_reader.h"
#include "data_manager.h"
#include "mqtt_publisher.h"

#if defined(USE_LVGL)
#include "lvgl_display.h"
#else
#include "display_manager.h"
#endif

#include <memory>
#include <functional>

using SensorDataCallback = std::function<void(const SensorData &)>;

namespace iot {

struct GatewayConfig {
    std::string i2cDev = "/dev/i2c-1";
    uint8_t sht30Addr = 0x44;
    uint8_t bh1750Addr = 0x23;
    int collectIntervalMs = 2000;

    bool enableMqtt = false;
    MqttPublisher::Config mqtt;

    bool enableDisplay = false;

#if defined(USE_LVGL)
    LvglDisplay::Config display;
#else
    DisplayManager::Config display;
#endif
};

class GatewaySDK {
public:
    GatewaySDK();
    ~GatewaySDK();

    bool init(const GatewayConfig &cfg);
    bool start();
    void stop();
    SensorData getLatestData();
    void onData(SensorDataCallback cb);
    bool readSensors(float &temp, float &humi, float &light);
    bool sendToCloud(const std::string &json);
    bool isRunning() const;

private:
    GatewayConfig cfg_;
    bool running_;
    std::unique_ptr<SensorReader> sensor_;
    std::unique_ptr<DataManager> dataMgr_;
    std::unique_ptr<MqttPublisher> mqtt_;

#if defined(USE_LVGL)
    std::unique_ptr<LvglDisplay> display_;
#else
    std::unique_ptr<DisplayManager> display_;
#endif
};

}

#endif
