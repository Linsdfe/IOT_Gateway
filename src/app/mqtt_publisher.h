#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include "sensor_reader.h"
#include "aliyun_iot.h"
#include "onenet_iot.h"
#include <thread>
#include <atomic>
#include <string>
#include <mutex>

class MqttPublisher {
public:
    enum class CloudMode {
        Plain,
        Aliyun,
        Onenet
    };

    struct Config {
        std::string host;
        int port;
        std::string clientId;
        std::string username;
        std::string password;
        std::string topic;
        int intervalSec;
        bool enabled;

        AliyunIotConfig aliyun;
        OnenetIotConfig onenet;
        CloudMode cloudMode;

        Config()
            : port(1883)
            , intervalSec(5)
            , enabled(false)
            , cloudMode(CloudMode::Plain)
        {}
    };

    MqttPublisher();
    ~MqttPublisher();

    bool init(const Config &cfg);
    bool start(class DataManager *mgr);
    void stop();
    bool isConnected();
    bool publishData(const std::string &topic, const std::string &payload);

private:
    void publishLoop();
    std::string buildPayload(const SensorData &data);
    bool mqttConnect();
    void mqttDisconnect();

    Config cfg_;
    DataManager *mgr_;
    std::thread publishThread_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    void *mosqCtx_;
};

#endif
