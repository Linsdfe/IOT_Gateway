#include "mqtt_publisher.h"
#include "data_manager.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <unistd.h>

#ifdef USE_MOSQUITTO
#include <mosquitto.h>
#endif

MqttPublisher::MqttPublisher()
    : mgr_(nullptr)
    , running_(false)
    , connected_(false)
    , mosqCtx_(nullptr)
{
}

MqttPublisher::~MqttPublisher()
{
    stop();
}

bool MqttPublisher::init(const Config &cfg)
{
    cfg_ = cfg;
    if (!cfg_.enabled) {
        printf("[MqttPublisher] disabled, skip init\n");
        return true;
    }

    if (cfg_.cloudMode == CloudMode::Aliyun) {
        cfg_.host = cfg_.aliyun.getMqttHost();
        cfg_.port = cfg_.aliyun.getMqttPort();
        cfg_.clientId = cfg_.aliyun.getClientId();
        cfg_.username = cfg_.aliyun.getUsername();
        cfg_.password = cfg_.aliyun.getPassword();
        cfg_.topic = cfg_.aliyun.getPubTopic();
        printf("[MqttPublisher] Aliyun IoT mode\n");
    } else if (cfg_.cloudMode == CloudMode::Onenet) {
        cfg_.host = cfg_.onenet.getMqttHost();
        cfg_.port = cfg_.onenet.getMqttPort();
        cfg_.clientId = cfg_.onenet.getClientId();
        cfg_.username = cfg_.onenet.getUsername();
        cfg_.password = cfg_.onenet.getPassword();
        cfg_.topic = cfg_.onenet.getPubTopic();
        printf("[MqttPublisher] OneNET IoT mode\n");
    }

    printf("[MqttPublisher]   Host: %s:%d\n", cfg_.host.c_str(), cfg_.port);
    printf("[MqttPublisher]   ClientID: %s\n", cfg_.clientId.c_str());
    printf("[MqttPublisher]   Username: %s\n", cfg_.username.c_str());
    printf("[MqttPublisher]   Topic: %s\n", cfg_.topic.c_str());

#ifdef USE_MOSQUITTO
    mosquitto_lib_init();
    mosqCtx_ = mosquitto_new(cfg_.clientId.c_str(), true, this);
    if (!mosqCtx_) {
        fprintf(stderr, "[MqttPublisher] mosquitto_new failed\n");
        return false;
    }
    if (!cfg_.username.empty()) {
        mosquitto_username_pw_set((struct mosquitto *)mosqCtx_,
                                  cfg_.username.c_str(),
                                  cfg_.password.c_str());
    }
    printf("[MqttPublisher] init OK\n");
#else
    printf("[MqttPublisher] compiled without mosquitto, using stub mode\n");
#endif
    return true;
}

bool MqttPublisher::start(DataManager *mgr)
{
    if (!cfg_.enabled) return true;
    mgr_ = mgr;
    running_ = true;
    publishThread_ = std::thread(&MqttPublisher::publishLoop, this);
    printf("[MqttPublisher] started\n");
    return true;
}

void MqttPublisher::stop()
{
    running_ = false;
    if (publishThread_.joinable())
        publishThread_.join();
    mqttDisconnect();
#ifdef USE_MOSQUITTO
    if (mosqCtx_) {
        mosquitto_destroy((struct mosquitto *)mosqCtx_);
        mosqCtx_ = nullptr;
    }
    mosquitto_lib_cleanup();
#endif
    printf("[MqttPublisher] stopped\n");
}

bool MqttPublisher::isConnected()
{
    return connected_;
}

bool MqttPublisher::mqttConnect()
{
#ifdef USE_MOSQUITTO
    if (!mosqCtx_) return false;
    int ret = mosquitto_connect((struct mosquitto *)mosqCtx_,
                                cfg_.host.c_str(), cfg_.port, 120);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[MqttPublisher] connect to %s:%d failed: %s\n",
                cfg_.host.c_str(), cfg_.port, mosquitto_strerror(ret));
        return false;
    }
    connected_ = true;
    printf("[MqttPublisher] connected to %s:%d\n",
           cfg_.host.c_str(), cfg_.port);
    return true;
#else
    connected_ = true;
    printf("[MqttPublisher] stub connect OK\n");
    return true;
#endif
}

bool MqttPublisher::publishData(const std::string &topic,
                                const std::string &payload)
{
#ifdef USE_MOSQUITTO
    if (!mosqCtx_) return false;
    int ret = mosquitto_publish((struct mosquitto *)mosqCtx_,
                                nullptr, topic.c_str(),
                                payload.length(), payload.c_str(),
                                1, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[MqttPublisher] publish failed: %s\n",
                mosquitto_strerror(ret));
        return false;
    }
#else
    printf("[MqttPublisher] stub publish to %s: %s\n",
           topic.c_str(), payload.c_str());
#endif
    return true;
}

void MqttPublisher::mqttDisconnect()
{
#ifdef USE_MOSQUITTO
    if (mosqCtx_ && connected_) {
        mosquitto_disconnect((struct mosquitto *)mosqCtx_);
    }
#endif
    connected_ = false;
}

std::string MqttPublisher::buildPayload(const SensorData &data)
{
    if (cfg_.cloudMode == CloudMode::Aliyun) {
        return cfg_.aliyun.buildPayload(data.temperature,
                                        data.humidity,
                                        data.light);
    } else if (cfg_.cloudMode == CloudMode::Onenet) {
        return cfg_.onenet.buildPayload(data.temperature,
                                        data.humidity,
                                        data.light);
    }

    time_t now = time(nullptr);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "{\"id\":\"" << now << "\",";
    ss << "\"version\":\"1.0\",";
    ss << "\"params\":{";
    ss << "\"Temperature\":" << data.temperature << ",";
    ss << "\"Humidity\":" << data.humidity << ",";
    ss << "\"LightIntensity\":" << data.light;
    ss << "},";
    ss << "\"timestamp\":" << now << "}";
    return ss.str();
}

void MqttPublisher::publishLoop()
{
    while (running_) {
        if (!connected_) {
            if (!mqttConnect()) {
                fprintf(stderr, "[MqttPublisher] connect failed, retry in 10s\n");
                for (int i = 0; i < 100 && running_; i++)
                    usleep(100000);
                continue;
            }
        }

        if (mgr_) {
            SensorData data = mgr_->getLatestData();
            if (data.valid) {
                std::string payload = buildPayload(data);
                std::string topic = cfg_.topic.empty()
                    ? "/iot/gateway/sensor/data"
                    : cfg_.topic;
                if (!publishData(topic, payload)) {
                    connected_ = false;
                } else {
                    printf("[MqttPublisher] published to %s\n", topic.c_str());
                }
            }
        }

        for (int i = 0; i < cfg_.intervalSec * 10 && running_; i++)
            usleep(100000);
    }
}
