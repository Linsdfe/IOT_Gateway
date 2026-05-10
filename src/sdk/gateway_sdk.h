#ifndef GATEWAY_SDK_H
#define GATEWAY_SDK_H

#include "sensor_reader.h"
#include "data_manager.h"
#include "mqtt_publisher.h"
#include "display_manager.h"
#include <string>
#include <functional>
#include <memory>

namespace iot {

struct GatewayConfig {
    std::string i2cDev = "/dev/i2c-1";
    uint8_t sht30Addr = 0x44;
    uint8_t bh1750Addr = 0x23;
    int collectIntervalMs = 1000;

    MqttPublisher::Config mqtt;
    DisplayManager::Config display;

    bool enableMqtt = false;
    bool enableDisplay = false;
};

using SensorDataCallback = std::function<void(const SensorData &)>;

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
    std::unique_ptr<SensorReader> sensor_;
    std::unique_ptr<DataManager> dataMgr_;
    std::unique_ptr<MqttPublisher> mqtt_;
    std::unique_ptr<DisplayManager> display_;
    bool running_;
};

}

#endif
