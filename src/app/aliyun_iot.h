#ifndef ALIYUN_IOT_H
#define ALIYUN_IOT_H

#include <string>

struct AliyunIotConfig {
    std::string productKey;
    std::string deviceName;
    std::string deviceSecret;
    std::string regionId;
    int secureMode;
    std::string signMethod;

    AliyunIotConfig()
        : regionId("cn-shanghai")
        , secureMode(3)
        , signMethod("hmacsha1")
    {}

    std::string getMqttHost() const;
    int getMqttPort() const;
    std::string getClientId() const;
    std::string getUsername() const;
    std::string getPassword() const;
    std::string getPubTopic() const;
    std::string getSubTopic() const;
    std::string buildPayload(float temp, float humi, float light) const;

    static std::string hmacSha1(const std::string &key,
                                const std::string &data);
};

#endif
