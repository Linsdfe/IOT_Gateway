/**
 * @file main.cpp
 * @brief IoT 网关程序入口
 */

#include "gateway_sdk.h"
#include "logger.h"
#include <csignal>
#include <cstdio>
#include <unistd.h>

static iot::GatewaySDK *g_gateway = nullptr;

static void signalHandler(int sig)
{
    LOG_W("Main", "received signal %d, shutting down...", sig);
    if (g_gateway) g_gateway->stop();
}

int main(int argc, char *argv[])
{
    Logger::instance().setLevel(LogLevel::DEBUG);
    Logger::instance().setLogFile("/tmp/iot_gateway.log");

    LOG_I("Main", "========================================");
    LOG_I("Main", "  Industrial IoT Edge Gateway");
    LOG_I("Main", "  i.MX6ULL + Dynamic Plugin System");
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
    cfg.pluginDir = "/usr/lib/iot/plugins";
    cfg.enableMqtt = true;
    cfg.mqtt.enabled = true;
    cfg.mqtt.cloudMode = MqttPublisher::CloudMode::Onenet;
    
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

    bool listPlugins = false;

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
        } else if (arg == "--plugin-dir" && i + 1 < argc) {
            cfg.pluginDir = argv[++i];
        } else if (arg == "--plugin" && i + 1 < argc) {
            cfg.pluginPath = argv[++i];
        } else if (arg == "--plugin-config" && i + 1 < argc) {
            cfg.pluginConfig = argv[++i];
        } else if (arg == "--list-plugins") {
            listPlugins = true;
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
            printf("Sensor Plugin Modes (use --plugin NAME):\n");
            printf("  sht30_bh1750_i2c     User-space I2C protocol (no kernel driver)\n");
            printf("  sht30_bh1750_kernel  Linux kernel standard IIO drivers\n");
            printf("  sht30_bh1750_custom  Custom kernel drivers (sht30_driver.ko + bh1750_driver.ko)\n");
            printf("  simulated            Simulated sensor data (no hardware)\n");
            printf("\nExamples:\n");
            printf("  sudo ./iot_gateway --plugin sht30_bh1750_i2c\n");
            printf("  sudo ./iot_gateway --plugin sht30_bh1750_kernel\n");
            printf("  sudo ./iot_gateway --plugin sht30_bh1750_custom\n");
            printf("  sudo ./iot_gateway --plugin simulated\n");
            printf("\nOptions:\n");
            printf("  --plugin NAME        Plugin name or .so path\n");
            printf("  --plugin-dir DIR     Plugin directory (default: /usr/lib/iot/plugins)\n");
            printf("  --plugin-config C    Plugin config string\n");
            printf("  --list-plugins       List available plugins and exit\n");
            printf("  --no-display         Disable display\n");
            printf("  --no-mqtt            Disable MQTT cloud connection\n");
            printf("  --interval MS        Collect interval (default: 2000)\n");
            printf("  --log-level LEVEL    debug|info|warn|error\n");
            printf("  --mqtt HOST          Use plain MQTT\n");
            printf("  --onenet-pid ID      OneNET Product ID\n");
            printf("  --onenet-dn NAME     OneNET Device Name\n");
            printf("  --onenet-dk KEY      OneNET Device Key\n");
            return 0;
        }
    }

    if (listPlugins) {
        printf("Scanning plugin directory: %s\n\n", cfg.pluginDir.c_str());
        SensorReader reader(cfg.pluginDir);
        auto plugins = reader.listPlugins();
        if (plugins.empty()) {
            printf("No plugins found.\n");
        } else {
            printf("Available plugins:\n");
            for (const auto &p : plugins) {
                printf("  %-25s  %s  (api=%d)\n", p.name.c_str(), p.description.c_str(), p.apiVersion);
            }
        }
        return 0;
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

    LOG_I("Main", "gateway running with plugin '%s', press Ctrl+C to stop",
          gateway.getPluginName().c_str());

    while (gateway.isRunning()) {
        sleep(1);
    }

    gateway.stop();
    g_gateway = nullptr;
    Logger::instance().close();
    return 0;
}
