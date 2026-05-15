/**
 * @file onenet_iot.h
 * @brief OneNET 物联网平台配置与 Token 认证
 *
 * 实现中国移动 OneNET IoT 平台的 MQTT 连接参数生成，
 * 包括 Token 签名算法（HMAC-SHA1 + Base64）和物模型数据格式。
 *
 * OneNET MQTT 连接参数：
 * - Broker: mqtts.heclouds.com:1883
 * - ClientID: {deviceName}
 * - Username: {productId}
 * - Password: Token 签名（基于 deviceKey 的 HMAC-SHA1）
 *
 * Token 生成流程：
 * 1. 构造资源路径 res = products/{productId}/devices/{deviceName}
 * 2. 计算签名 sig = HMAC-SHA1(deviceKey, et + "\n" + method)
 * 3. 拼接 Token = version=2018-10-31&res={url_encode(res)}&et={et}&method=sha1&sign={url_encode(base64(sig))}
 */

#ifndef ONENET_IOT_H
#define ONENET_IOT_H

#include <string>

/**
 * @brief OneNET IoT 平台配置结构
 *
 * 包含 OneNET 设备的三元组信息（产品ID、设备名、设备密钥），
 * 以及自动生成的 MQTT 连接参数和 Token 签名。
 */
struct OnenetIotConfig {
    std::string productId;     ///< 产品ID，在 OneNET 控制台创建产品时获得
    std::string deviceName;    ///< 设备名称，在产品下创建设备时指定
    std::string deviceKey;     ///< 设备密钥（Base64 编码），用于生成 Token 签名
    std::string region;        ///< 区域标识，默认为空（使用默认区域）
    int tokenExpireSec;        ///< Token 过期时间（秒），默认 86400（24小时）

    OnenetIotConfig()
        : tokenExpireSec(86400) {}

    /** @brief 获取 MQTT Broker 地址 */
    std::string getMqttHost() const;

    /** @brief 获取 MQTT Broker 端口 */
    int getMqttPort() const;

    /** @brief 获取 MQTT ClientID，即设备名称 */
    std::string getClientId() const;

    /** @brief 获取 MQTT 用户名，即产品ID */
    std::string getUsername() const;

    /**
     * @brief 获取 MQTT 密码（Token 签名）
     *
     * 基于 HMAC-SHA1 算法动态生成，每次调用重新计算。
     * Token 格式：version=2018-10-31&res=...&et=...&method=sha1&sign=...
     */
    std::string getPassword() const;

    /** @brief 获取数据上报主题 */
    std::string getPubTopic() const;

    /**
     * @brief 构建物模型属性上报 JSON
     * @param temp  温度值
     * @param humi  湿度值
     * @param light 光照值
     * @return JSON 字符串，符合 OneNET 物模型格式
     *
     * 输出格式：
     * {"id":"timestamp","version":"1.0","params":{"Temperature":{"value":25.6},...}}
     */
    std::string buildPayload(float temp, float humi, float light) const;

    /**
     * @brief 生成 OneNET Token 签名
     * @param deviceKeyBase64 Base64 编码的设备密钥
     * @param res             资源路径
     * @param et              过期时间戳（Unix 时间戳）
     * @param method          签名方法（"sha1"）
     * @return 完整的 Token 字符串
     */
    static std::string generateToken(const std::string &deviceKeyBase64,
                                     const std::string &res,
                                     long et,
                                     const std::string &method);
};

#endif
