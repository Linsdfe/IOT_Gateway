/**
 * @file gateway_sdk.h
 * @brief IoT 网关 SDK - 统一封装传感器采集、云平台上报、屏幕显示
 *
 * GatewaySDK 是整个 IoT 网关项目的顶层接口，组合了以下子系统：
 * - SensorReader：基于动态插件的传感器数据采集
 * - DataManager：多线程数据管理与分发
 * - MqttPublisher：MQTT 云平台数据上报
 * - DisplayManager / LvglDisplay：LCD 屏幕实时显示
 *
 * 动态插件支持：
 *   通过 pluginDir 和 pluginPath 配置传感器插件，
 *   支持运行时热替换 .so 文件切换驱动，无需重新编译主程序。
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
#include "edge_compute.h"

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

    std::string pluginDir = "/usr/lib/iot/plugins";
    std::string pluginPath;
    std::string pluginConfig;

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

    /**
     * @brief 热替换传感器插件
     * @param newPath 新插件 .so 路径
     * @param config  新插件配置字符串
     * @return true 替换成功
     *
     * 运行时切换传感器驱动，无需重启主程序。
     * 替换后 DataManager 的采集线程自动使用新插件读取数据。
     */
    bool hotSwapPlugin(const std::string &newPath, const std::string &config);

    /**
     * @brief 获取当前插件名称
     * @return 插件名称字符串
     */
    std::string getPluginName() const;

    /**
     * @brief 列出插件目录中所有可用插件
     * @return 插件信息列表
     */
    std::vector<PluginInfo> listPlugins();

    /**
     * @brief 获取边缘计算处理后的最新数据
     * @return EdgeSensorData 快照
     */
    EdgeSensorData getLatestEdgeData();

    /**
     * @brief 获取当前所有活跃告警
     * @return 告警事件列表
     */
    std::vector<AlertEvent> getActiveAlerts();

    /**
     * @brief 注册告警回调
     * @param cb 告警回调函数
     */
    void onAlert(AlertCallback cb);

    /**
     * @brief 获取资源使用情况
     * @return ResourceUsage 快照
     */
    ResourceUsage getResourceUsage();

private:
    GatewayConfig cfg_;
    bool running_;
    std::unique_ptr<SensorReader> sensor_;
    std::unique_ptr<DataManager> dataMgr_;
    std::unique_ptr<MqttPublisher> mqtt_;
    std::unique_ptr<EdgeCompute> edge_;

#if defined(USE_LVGL)
    std::unique_ptr<LvglDisplay> display_;
#else
    std::unique_ptr<DisplayManager> display_;
#endif
};

}

#endif
