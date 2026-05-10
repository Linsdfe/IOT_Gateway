#include "gateway_sdk.h"
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <cstring>

static iot::GatewaySDK *g_gateway = nullptr;

static void signalHandler(int sig)
{
    printf("\n[Main] received signal %d, shutting down...\n", sig);
    if (g_gateway) {
        g_gateway->stop();
    }
    _exit(0);
}

int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("  Industrial IoT Edge Gateway\n");
    printf("  i.MX6ULL + SHT30 + BH1750\n");
    printf("========================================\n");

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    iot::GatewayConfig cfg;

    cfg.i2cDev = "/dev/i2c-1";
    cfg.sht30Addr = 0x44;
    cfg.bh1750Addr = 0x23;
    cfg.collectIntervalMs = 2000;

    cfg.mqtt.enabled = false;
    cfg.mqtt.host = "localhost";
    cfg.mqtt.port = 1883;
    cfg.mqtt.intervalSec = 5;

    cfg.display.enabled = false;
    cfg.display.fbDevice = "/dev/fb0";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--mqtt" && i + 1 < argc) {
            cfg.enableMqtt = true;
            cfg.mqtt.enabled = true;
            cfg.mqtt.cloudMode = MqttPublisher::CloudMode::Plain;
            cfg.mqtt.host = argv[++i];
        } else if (arg == "--mqtt-port" && i + 1 < argc) {
            cfg.mqtt.port = std::stoi(argv[++i]);
        } else if (arg == "--mqtt-topic" && i + 1 < argc) {
            cfg.mqtt.topic = argv[++i];
        } else if (arg == "--display") {
            cfg.enableDisplay = true;
            cfg.display.enabled = true;
        } else if (arg == "--interval" && i + 1 < argc) {
            cfg.collectIntervalMs = std::stoi(argv[++i]);
        } else if (arg == "--aliyun-pk" && i + 1 < argc) {
            cfg.enableMqtt = true;
            cfg.mqtt.enabled = true;
            cfg.mqtt.cloudMode = MqttPublisher::CloudMode::Aliyun;
            cfg.mqtt.aliyun.productKey = argv[++i];
        } else if (arg == "--aliyun-dn" && i + 1 < argc) {
            cfg.mqtt.aliyun.deviceName = argv[++i];
        } else if (arg == "--aliyun-ds" && i + 1 < argc) {
            cfg.mqtt.aliyun.deviceSecret = argv[++i];
        } else if (arg == "--aliyun-region" && i + 1 < argc) {
            cfg.mqtt.aliyun.regionId = argv[++i];
        } else if (arg == "--onenet-pid" && i + 1 < argc) {
            cfg.enableMqtt = true;
            cfg.mqtt.enabled = true;
            cfg.mqtt.cloudMode = MqttPublisher::CloudMode::Onenet;
            cfg.mqtt.onenet.productId = argv[++i];
        } else if (arg == "--onenet-dn" && i + 1 < argc) {
            cfg.mqtt.onenet.deviceName = argv[++i];
        } else if (arg == "--onenet-dk" && i + 1 < argc) {
            cfg.mqtt.onenet.deviceKey = argv[++i];
        } else if (arg == "--help") {
            printf("Usage: iot_gateway [OPTIONS]\n\n");
            printf("Options:\n");
            printf("  --mqtt HOST         Enable MQTT, connect to HOST\n");
            printf("  --mqtt-port PORT    MQTT port (default: 1883)\n");
            printf("  --mqtt-topic TOPIC  MQTT publish topic\n");
            printf("  --display           Enable framebuffer display\n");
            printf("  --interval MS       Collect interval in ms (default: 2000)\n");
            printf("\nAliyun IoT Options:\n");
            printf("  --aliyun-pk KEY     ProductKey\n");
            printf("  --aliyun-dn NAME    DeviceName\n");
            printf("  --aliyun-ds SECRET  DeviceSecret\n");
            printf("  --aliyun-region ID  Region (default: cn-shanghai)\n");
            printf("\nOneNET IoT Options:\n");
            printf("  --onenet-pid ID     Product ID\n");
            printf("  --onenet-dn NAME    Device Name\n");
            printf("  --onenet-dk KEY     Device Key (base64)\n");
            printf("\nExamples:\n");
            printf("  Local MQTT:\n");
            printf("    sudo ./iot_gateway --mqtt localhost\n");
            printf("  OneNET:\n");
            printf("    sudo ./iot_gateway --onenet-pid XUV077XBf9 --onenet-dn imx6ull_01 --onenet-dk UG83cDg...\n");
            printf("  Aliyun IoT:\n");
            printf("    sudo ./iot_gateway --aliyun-pk a1XXXX --aliyun-dn MyDevice --aliyun-ds XXXXXX\n");
            printf("  Full features:\n");
            printf("    sudo ./iot_gateway --display --onenet-pid XUV077XBf9 --onenet-dn imx6ull_01 --onenet-dk UG83cDg...\n");
            return 0;
        }
    }

    if (cfg.mqtt.cloudMode == MqttPublisher::CloudMode::Aliyun) {
        if (cfg.mqtt.aliyun.productKey.empty() ||
            cfg.mqtt.aliyun.deviceName.empty() ||
            cfg.mqtt.aliyun.deviceSecret.empty()) {
            fprintf(stderr, "[Main] Aliyun IoT requires --aliyun-pk, --aliyun-dn, --aliyun-ds\n");
            return -1;
        }
        printf("[Main] Aliyun IoT: PK=%s DN=%s\n",
               cfg.mqtt.aliyun.productKey.c_str(),
               cfg.mqtt.aliyun.deviceName.c_str());
    }

    if (cfg.mqtt.cloudMode == MqttPublisher::CloudMode::Onenet) {
        if (cfg.mqtt.onenet.productId.empty() ||
            cfg.mqtt.onenet.deviceName.empty() ||
            cfg.mqtt.onenet.deviceKey.empty()) {
            fprintf(stderr, "[Main] OneNET requires --onenet-pid, --onenet-dn, --onenet-dk\n");
            return -1;
        }
        printf("[Main] OneNET IoT: PID=%s DN=%s\n",
               cfg.mqtt.onenet.productId.c_str(),
               cfg.mqtt.onenet.deviceName.c_str());
    }

    iot::GatewaySDK gateway;
    g_gateway = &gateway;

    if (!gateway.init(cfg)) {
        fprintf(stderr, "[Main] gateway init failed\n");
        return -1;
    }

    gateway.onData([](const SensorData &data) {
        printf("[Callback] T=%.1f C  H=%.1f %%  L=%.1f lux\n",
               data.temperature, data.humidity, data.light);
    });

    if (!gateway.start()) {
        fprintf(stderr, "[Main] gateway start failed\n");
        return -1;
    }

    printf("[Main] gateway running, press Ctrl+C to stop\n");

    while (gateway.isRunning()) {
        sleep(1);
    }

    gateway.stop();
    g_gateway = nullptr;

    return 0;
}
