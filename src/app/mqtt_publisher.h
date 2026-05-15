/**
 * @file mqtt_publisher.h
 * @brief MQTT 发布器 - 支持 OneNET 云平台的数据上报
 *
 * MqttPublisher 运行独立的发布线程，定时从 DataManager 获取最新数据，
 * 构建 JSON 载荷并通过 MQTT 协议发布到云平台。
 *
 * 支持两种云模式：
 * - Plain：直连任意 MQTT Broker
 * - Onenet：中国移动 OneNET 物联网平台（自动处理 Token 认证）
 *
 * 连接管理：
 * - 自动重连机制：连接断开后每 10 秒重试
 * - 发布失败自动标记断连，触发下次重连
 */

#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include "data_manager.h"
#include "onenet_iot.h"
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

/**
 * @brief MQTT 发布器
 *
 * 线程安全的 MQTT 数据发布器，管理连接生命周期和数据上报。
 */
class MqttPublisher {
public:
    /**
     * @brief 云平台模式枚举
     */
    enum class CloudMode {
        Plain,   ///< 直连模式，连接任意 MQTT Broker
        Onenet   ///< OneNET 模式，自动处理 Token 认证和物模型格式
    };

    /**
     * @brief MQTT 配置结构
     */
    struct Config {
        bool enabled = false;          ///< 是否启用 MQTT
        std::string host;              ///< Broker 地址
        int port = 1883;               ///< Broker 端口
        std::string clientId;          ///< 客户端标识
        std::string username;          ///< 认证用户名
        std::string password;          ///< 认证密码/Token
        std::string topic;             ///< 发布主题
        int intervalSec = 5;           ///< 发布间隔（秒）
        CloudMode cloudMode = CloudMode::Plain; ///< 云平台模式
        OnenetIotConfig onenet;        ///< OneNET 配置（仅 Onenet 模式使用）
    };

    MqttPublisher();
    ~MqttPublisher();

    /**
     * @brief 初始化 MQTT 客户端
     * @param cfg 配置参数
     * @return true 初始化成功
     *
     * 根据 cloudMode 自动填充连接参数：
     * - Onenet 模式：从 onenet 配置生成 host/port/clientId/username/password/topic
     * - Plain 模式：直接使用 cfg 中的值
     */
    bool init(const Config &cfg);

    /**
     * @brief 启动发布线程
     * @param mgr DataManager 指针，用于获取最新传感器数据
     * @return true 启动成功
     */
    bool start(DataManager *mgr);

    /** @brief 停止发布线程并断开连接 */
    void stop();

    /** @brief 查询当前连接状态 */
    bool isConnected();

    /** @brief 设置连接状态（由 MQTT 回调调用） */
    void setConnected(bool val);

    /**
     * @brief 发布数据到指定主题
     * @param topic   MQTT 主题
     * @param payload 消息载荷（JSON 字符串）
     * @return true 发布成功
     */
    bool publishData(const std::string &topic, const std::string &payload);

private:
    /** @brief 建立 MQTT 连接 */
    bool mqttConnect();

    /** @brief 断开 MQTT 连接 */
    void mqttDisconnect();

    /**
     * @brief 根据云模式构建数据载荷
     * @param data 传感器数据
     * @return JSON 格式的载荷字符串
     */
    std::string buildPayload(const SensorData &data);

    /** @brief 发布线程主循环：定时获取数据并发布 */
    void publishLoop();

    Config cfg_;                    ///< MQTT 配置
    DataManager *mgr_;              ///< 数据管理器指针
    volatile bool running_;         ///< 线程运行标志
    std::atomic<bool> connected_;   ///< 连接状态（原子变量，线程安全）
    void *mosqCtx_;                 ///< Mosquitto 客户端上下文（void* 避免头文件依赖）
    std::thread publishThread_;     ///< 发布线程
};

#endif
