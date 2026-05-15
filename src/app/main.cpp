/**
 * @file main.cpp
 * @brief IoT 网关程序入口 - 命令行参数解析、配置初始化、生命周期管理
 *
 * 程序功能：
 *   1. 解析命令行参数，构建网关配置
 *   2. 初始化日志系统（控制台 + 文件双输出）
 *   3. 创建并启动 GatewaySDK
 *   4. 注册数据回调，实时打印传感器数据
 *   5. 处理信号（Ctrl+C），优雅停止网关
 *
 * 默认行为（无参数启动）：
 *   - 自动启用 OneNET 云平台连接
 *   - 自动启用 LCD 屏幕显示
 *   - 日志级别 DEBUG
 *   - 日志文件 /tmp/iot_gateway.log
 *
 * 使用示例：
 *   sudo ./iot_gateway                          # 默认配置（OneNET + 显示）
 *   sudo ./iot_gateway --no-display             # 关闭屏幕显示
 *   sudo ./iot_gateway --no-mqtt                # 关闭云平台连接
 *   sudo ./iot_gateway --log-level warn         # 仅显示警告和错误
 *   sudo ./iot_gateway --onenet-pid XXX ...     # 覆盖 OneNET 默认参数
 */

#include "gateway_sdk.h"
#include "logger.h"
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <cstring>

static iot::GatewaySDK *g_gateway = nullptr;

static void signalHandler(int sig)
{
    LOG_W("Main", "received signal %d, shutting down...", sig);
    if (g_gateway) {
        g_gateway->stop();
    }
    _exit(0);
}

int main(int argc, char *argv[])
{
    Logger::instance().setLevel(LogLevel::DEBUG);
    Logger::instance().setLogFile("/tmp/iot_gateway.log");

    LOG_I("Main", "========================================");
    LOG_I("Main", "  Industrial IoT Edge Gateway");
    LOG_I("Main", "  i.MX6ULL + SHT30 + BH1750");
#if defined(USE_LVGL)
    LOG_I("Main", "  Display: LVGL Graphical Interface");
#else
    LOG_I("Main", "  Display: Framebuffer (5x7 Font)");
#endif
    LOG_I("Main", "========================================");

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    iot::GatewayConfig cfg;

    cfg.i2cDev = "/dev/i2c-1";
    cfg.sht30Addr = 0x44;
    cfg.bh1750Addr = 0x23;
    cfg.collectIntervalMs = 2000;

    cfg.enableMqtt = true;
    cfg.mqtt.enabled = true;
    cfg.mqtt.cloudMode = MqttPublisher::CloudMode::Onenet;
    cfg.mqtt.onenet.productId = "XUV077XBf9";
    cfg.mqtt.onenet.deviceName = "imx6ull_01";
    cfg.mqtt.onenet.deviceKey = "UG83cDgySktEQWZhNHdLRXQ2WHd6TGRaZUtSdG9CZTI=";
    cfg.mqtt.host = "mqtts.heclouds.com";
    cfg.mqtt.port = 1883;
    cfg.mqtt.intervalSec = 5;

    cfg.enableDisplay = true;
    cfg.display.enabled = true;
    cfg.display.fbDevice = "/dev/fb0";
#if defined(USE_LVGL)
    cfg.display.width = 480;
    cfg.display.height = 272;
    cfg.display.refreshMs = 33;
#endif

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--mqtt" && i + 1 < argc) {
            cfg.mqtt.cloudMode = MqttPublisher::CloudMode::Plain;
            cfg.mqtt.host = argv[++i];
        } else if (arg == "--mqtt-port" && i + 1 < argc) {
            cfg.mqtt.port = std::stoi(argv[++i]);
        } else if (arg == "--mqtt-topic" && i + 1 < argc) {
            cfg.mqtt.topic = argv[++i];
        } else if (arg == "--no-display") {
            cfg.enableDisplay = false;
            cfg.display.enabled = false;
        } else if (arg == "--no-mqtt") {
            cfg.enableMqtt = false;
            cfg.mqtt.enabled = false;
        } else if (arg == "--interval" && i + 1 < argc) {
            cfg.collectIntervalMs = std::stoi(argv[++i]);
        } else if (arg == "--onenet-pid" && i + 1 < argc) {
            cfg.mqtt.cloudMode = MqttPublisher::CloudMode::Onenet;
            cfg.mqtt.onenet.productId = argv[++i];
        } else if (arg == "--onenet-dn" && i + 1 < argc) {
            cfg.mqtt.onenet.deviceName = argv[++i];
        } else if (arg == "--onenet-dk" && i + 1 < argc) {
            cfg.mqtt.onenet.deviceKey = argv[++i];
#if defined(USE_LVGL)
        } else if (arg == "--fb-width" && i + 1 < argc) {
            cfg.display.width = std::stoi(argv[++i]);
        } else if (arg == "--fb-height" && i + 1 < argc) {
            cfg.display.height = std::stoi(argv[++i]);
        } else if (arg == "--fb-refresh" && i + 1 < argc) {
            cfg.display.refreshMs = std::stoi(argv[++i]);
#endif
        } else if (arg == "--log-level" && i + 1 < argc) {
            std::string lv = argv[++i];
            if (lv == "debug") Logger::instance().setLevel(LogLevel::DEBUG);
            else if (lv == "info") Logger::instance().setLevel(LogLevel::INFO);
            else if (lv == "warn") Logger::instance().setLevel(LogLevel::WARN);
            else if (lv == "error") Logger::instance().setLevel(LogLevel::ERROR);
        } else if (arg == "--help") {
            printf("Usage: iot_gateway [OPTIONS]\n\n");
            printf("Options:\n");
            printf("  --mqtt HOST         Use plain MQTT, connect to HOST\n");
            printf("  --mqtt-port PORT    MQTT port (default: 1883)\n");
            printf("  --mqtt-topic TOPIC  MQTT publish topic\n");
            printf("  --no-display        Disable display\n");
            printf("  --no-mqtt           Disable MQTT cloud connection\n");
            printf("  --interval MS       Collect interval in ms (default: 2000)\n");
            printf("  --log-level LEVEL   Log level: debug|info|warn|error\n");
#if defined(USE_LVGL)
            printf("\nLVGL Display Options:\n");
            printf("  --fb-width W        Framebuffer width (default: 480)\n");
            printf("  --fb-height H       Framebuffer height (default: 272)\n");
            printf("  --fb-refresh MS     Refresh interval ms (default: 33)\n");
#endif
            printf("\nOneNET IoT Options (overrides defaults):\n");
            printf("  --onenet-pid ID     Product ID\n");
            printf("  --onenet-dn NAME    Device Name\n");
            printf("  --onenet-dk KEY     Device Key (base64)\n");
            printf("\nDefaults: display=ON, OneNET=ON (PID=XUV077XBf9 DN=imx6ull_01)\n");
            return 0;
        }
    }

    if (cfg.mqtt.cloudMode == MqttPublisher::CloudMode::Onenet) {
        if (cfg.mqtt.onenet.productId.empty() ||
            cfg.mqtt.onenet.deviceName.empty() ||
            cfg.mqtt.onenet.deviceKey.empty()) {
            LOG_E("Main", "OneNET requires --onenet-pid, --onenet-dn, --onenet-dk");
            return -1;
        }
        LOG_I("Main", "OneNET IoT: PID=%s DN=%s",
              cfg.mqtt.onenet.productId.c_str(),
              cfg.mqtt.onenet.deviceName.c_str());
    }

    iot::GatewaySDK gateway;
    g_gateway = &gateway;

    if (!gateway.init(cfg)) {
        LOG_E("Main", "gateway init failed");
        return -1;
    }

    gateway.onData([](const SensorData &data) {
        LOG_I("Callback", "T=%.1f C  H=%.1f %%  L=%.1f lux",
              data.temperature, data.humidity, data.light);
    });

    if (!gateway.start()) {
        LOG_E("Main", "gateway start failed");
        return -1;
    }

    LOG_I("Main", "gateway running, press Ctrl+C to stop");

    while (gateway.isRunning()) {
        sleep(1);
    }

    gateway.stop();
    g_gateway = nullptr;

    Logger::instance().close();
    return 0;
}
