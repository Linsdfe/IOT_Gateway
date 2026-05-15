/**
 * @file mqtt_publisher.cpp
 * @brief MQTT 发布器实现
 *
 * 编译条件：定义 USE_MOSQUITTO 宏时使用 libmosquitto 库，
 * 否则编译为 stub 模式（仅打印日志，不实际连接）。
 *
 * 关键修复：
 * - 添加 mosquitto_loop() 调用：处理 Keep-Alive 心跳和入站消息
 *   缺少此调用会导致 Broker 在 ~180 秒后断开连接（Keep-Alive 超时）
 * - 添加连接回调：检测连接断开事件，触发自动重连
 * - 重连时刷新 Token：OneNET Token 有过期时间，重连需重新生成
 */

#include "mqtt_publisher.h"
#include "data_manager.h"
#include "logger.h"
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

#ifdef USE_MOSQUITTO

/**
 * @brief MQTT 连接回调
 *
 * 当 mosquitto_connect() 成功或失败时触发。
 * 用于确认连接是否真正建立。
 */
static void onConnect(struct mosquitto *mosq, void *obj, int rc)
{
    MqttPublisher *pub = static_cast<MqttPublisher *>(obj);
    if (rc == 0) {
        pub->setConnected(true);
        LOG_I("MQTT", "callback: connected to broker (rc=%d)", rc);
    } else {
        pub->setConnected(false);
        LOG_E("MQTT", "callback: connect failed (rc=%d): %s",
              rc, mosquitto_strerror(rc));
    }
}

/**
 * @brief MQTT 断开回调
 *
 * 当连接被 Broker 断开或网络异常时触发。
 * 标记断连状态，publishLoop 会在下次循环自动重连。
 */
static void onDisconnect(struct mosquitto *mosq, void *obj, int rc)
{
    MqttPublisher *pub = static_cast<MqttPublisher *>(obj);
    pub->setConnected(false);
    if (rc == 0) {
        LOG_I("MQTT", "callback: cleanly disconnected");
    } else {
        LOG_W("MQTT", "callback: unexpected disconnect (rc=%d): %s",
              rc, mosquitto_strerror(rc));
    }
}

/**
 * @brief MQTT 发布回调
 *
 * 当 mosquitto_publish() 的 QoS > 0 消息被确认时触发。
 * 用于检测发布是否真正成功。
 */
static void onPublish(struct mosquitto *mosq, void *obj, int mid)
{
    LOG_D("MQTT", "message %d published successfully", mid);
}

#endif

bool MqttPublisher::init(const Config &cfg)
{
    cfg_ = cfg;
    if (!cfg_.enabled) {
        LOG_I("MQTT", "disabled, skip init");
        return true;
    }

    if (cfg_.cloudMode == CloudMode::Onenet) {
        cfg_.host = cfg_.onenet.getMqttHost();
        cfg_.port = cfg_.onenet.getMqttPort();
        cfg_.clientId = cfg_.onenet.getClientId();
        cfg_.username = cfg_.onenet.getUsername();
        cfg_.password = cfg_.onenet.getPassword();
        cfg_.topic = cfg_.onenet.getPubTopic();
        LOG_I("MQTT", "OneNET IoT mode");
    }

    LOG_I("MQTT", "  Host: %s:%d", cfg_.host.c_str(), cfg_.port);
    LOG_I("MQTT", "  ClientID: %s", cfg_.clientId.c_str());
    LOG_D("MQTT", "  Username: %s", cfg_.username.c_str());
    LOG_D("MQTT", "  Password: %s", cfg_.password.c_str());
    LOG_I("MQTT", "  Topic: %s", cfg_.topic.c_str());

#ifdef USE_MOSQUITTO
    mosquitto_lib_init();
    mosqCtx_ = mosquitto_new(cfg_.clientId.c_str(), true, this);
    if (!mosqCtx_) {
        LOG_E("MQTT", "mosquitto_new failed");
        return false;
    }

    mosquitto_connect_callback_set((struct mosquitto *)mosqCtx_, onConnect);
    mosquitto_disconnect_callback_set((struct mosquitto *)mosqCtx_, onDisconnect);
    mosquitto_publish_callback_set((struct mosquitto *)mosqCtx_, onPublish);

    if (!cfg_.username.empty()) {
        mosquitto_username_pw_set((struct mosquitto *)mosqCtx_,
                                  cfg_.username.c_str(),
                                  cfg_.password.c_str());
    }
    LOG_I("MQTT", "init OK (mosquitto)");
#else
    LOG_W("MQTT", "compiled without mosquitto, using stub mode");
#endif
    return true;
}

bool MqttPublisher::start(DataManager *mgr)
{
    if (!cfg_.enabled) return true;
    mgr_ = mgr;
    running_ = true;
    publishThread_ = std::thread(&MqttPublisher::publishLoop, this);
    LOG_I("MQTT", "started");
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
    LOG_I("MQTT", "stopped");
}

bool MqttPublisher::isConnected()
{
    return connected_;
}

void MqttPublisher::setConnected(bool val)
{
    connected_ = val;
}

bool MqttPublisher::mqttConnect()
{
#ifdef USE_MOSQUITTO
    if (!mosqCtx_) return false;

    if (cfg_.cloudMode == CloudMode::Onenet) {
        cfg_.password = cfg_.onenet.getPassword();
        mosquitto_username_pw_set((struct mosquitto *)mosqCtx_,
                                  cfg_.username.c_str(),
                                  cfg_.password.c_str());
        LOG_D("MQTT", "Token refreshed for reconnection");
    }

    int ret = mosquitto_connect((struct mosquitto *)mosqCtx_,
                                cfg_.host.c_str(), cfg_.port, 120);
    if (ret != MOSQ_ERR_SUCCESS) {
        LOG_E("MQTT", "connect to %s:%d failed: %s",
              cfg_.host.c_str(), cfg_.port, mosquitto_strerror(ret));
        connected_ = false;
        return false;
    }

    ret = mosquitto_loop_start((struct mosquitto *)mosqCtx_);
    if (ret != MOSQ_ERR_SUCCESS) {
        LOG_E("MQTT", "loop_start failed: %s", mosquitto_strerror(ret));
        connected_ = false;
        return false;
    }

    LOG_I("MQTT", "connecting to %s:%d (keepalive=120s)...", cfg_.host.c_str(), cfg_.port);
    return true;
#else
    connected_ = true;
    LOG_I("MQTT", "stub connect OK");
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
        LOG_E("MQTT", "publish failed: %s", mosquitto_strerror(ret));
        connected_ = false;
        return false;
    }
#else
    LOG_D("MQTT", "stub publish to %s: %s", topic.c_str(), payload.c_str());
#endif
    return true;
}

void MqttPublisher::mqttDisconnect()
{
#ifdef USE_MOSQUITTO
    if (mosqCtx_) {
        mosquitto_loop_stop((struct mosquitto *)mosqCtx_, true);
        if (connected_) {
            mosquitto_disconnect((struct mosquitto *)mosqCtx_);
        }
    }
#endif
    connected_ = false;
}

std::string MqttPublisher::buildPayload(const SensorData &data)
{
    if (cfg_.cloudMode == CloudMode::Onenet) {
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
                LOG_W("MQTT", "connect failed, retry in 10s");
                for (int i = 0; i < 100 && running_; i++)
                    usleep(100000);
                continue;
            }
            for (int i = 0; i < 30 && running_ && !connected_; i++)
                usleep(100000);
            if (!connected_) {
                LOG_W("MQTT", "connect timeout, retry...");
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
                    LOG_I("MQTT", "published to %s", topic.c_str());
                }
            }
        }

        for (int i = 0; i < cfg_.intervalSec * 10 && running_; i++)
            usleep(100000);
    }
}
