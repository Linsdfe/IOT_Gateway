#ifndef ONENET_IOT_H
#define ONENET_IOT_H

#include <string>

struct OnenetIotConfig {
    std::string productId;
    std::string deviceName;
    std::string deviceKey;
    std::string region;
    int tokenExpireSec;

    OnenetIotConfig()
        : region("heclouds")
        , tokenExpireSec(31536000)
    {}

    std::string getMqttHost() const;
    int getMqttPort() const;
    std::string getClientId() const;
    std::string getUsername() const;
    std::string getPassword() const;
    std::string getPubTopic() const;
    std::string getSubTopic() const;
    std::string buildPayload(float temp, float humi, float light) const;

    static std::string generateToken(const std::string &deviceKeyBase64,
                                     const std::string &res,
                                     long et,
                                     const std::string &method);
    static std::string base64Decode(const std::string &input);
    static std::string base64Encode(const unsigned char *data, size_t len);
    static std::string urlEncode(const std::string &value);
    static std::string hmacSha1(const std::string &key,
                                const std::string &data);
};

#endif
