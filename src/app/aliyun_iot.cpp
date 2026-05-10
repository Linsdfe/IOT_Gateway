#include "aliyun_iot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <openssl/hmac.h>
#include <openssl/evp.h>

std::string AliyunIotConfig::getMqttHost() const
{
    return productKey + ".iot-as-mqtt." + regionId + ".aliyuncs.com";
}

int AliyunIotConfig::getMqttPort() const
{
    return (secureMode == 3) ? 1883 : 443;
}

std::string AliyunIotConfig::getClientId() const
{
    std::ostringstream ss;
    ss << deviceName << "|securemode=" << secureMode
       << ",signmethod=" << signMethod << "|";
    return ss.str();
}

std::string AliyunIotConfig::getUsername() const
{
    return deviceName + "&" + productKey;
}

std::string AliyunIotConfig::getPassword() const
{
    std::string content = "clientId" + deviceName
                        + "deviceName" + deviceName
                        + "productKey" + productKey;
    return hmacSha1(deviceSecret, content);
}

std::string AliyunIotConfig::getPubTopic() const
{
    return "/sys/" + productKey + "/" + deviceName
         + "/thing/event/property/post";
}

std::string AliyunIotConfig::getSubTopic() const
{
    return "/sys/" + productKey + "/" + deviceName
         + "/thing/service/property/set";
}

std::string AliyunIotConfig::buildPayload(float temp, float humi,
                                          float light) const
{
    time_t now = time(nullptr);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "{\"id\":\"" << now << "\",";
    ss << "\"version\":\"1.0\",";
    ss << "\"params\":{";
    ss << "\"Temperature\":" << temp << ",";
    ss << "\"Humidity\":" << humi << ",";
    ss << "\"LightIntensity\":" << light;
    ss << "},";
    ss << "\"method\":\"thing.event.property.post\"}";
    return ss.str();
}

std::string AliyunIotConfig::hmacSha1(const std::string &key,
                                       const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    HMAC(EVP_sha1(), key.c_str(), key.length(),
         (const unsigned char *)data.c_str(), data.length(),
         digest, &digestLen);

    const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digestLen * 2);
    for (unsigned int i = 0; i < digestLen; i++) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 0x0f]);
    }
    return result;
}
