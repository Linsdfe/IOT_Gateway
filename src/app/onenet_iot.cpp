#include "onenet_iot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

std::string OnenetIotConfig::getMqttHost() const
{
    return "mqtts.heclouds.com";
}

int OnenetIotConfig::getMqttPort() const
{
    return 1883;
}

std::string OnenetIotConfig::getClientId() const
{
    return deviceName;
}

std::string OnenetIotConfig::getUsername() const
{
    return productId;
}

std::string OnenetIotConfig::getPassword() const
{
    std::string res = "products/" + productId + "/devices/" + deviceName;
    long et = time(nullptr) + tokenExpireSec;
    return generateToken(deviceKey, res, et, "sha1");
}

std::string OnenetIotConfig::getPubTopic() const
{
    return "$sys/" + productId + "/" + deviceName + "/thing/property/post";
}

std::string OnenetIotConfig::getSubTopic() const
{
    return "$sys/" + productId + "/" + deviceName + "/thing/property/set";
}

std::string OnenetIotConfig::buildPayload(float temp, float humi,
                                           float light) const
{
    time_t now = time(nullptr);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "{\"id\":\"" << now << "\",";
    ss << "\"version\":\"1.0\",";
    ss << "\"params\":{";
    ss << "\"Temperature\":{\"value\":" << temp << "},";
    ss << "\"Humidity\":{\"value\":" << humi << "},";
    ss << "\"LightIntensity\":{\"value\":" << light << "}";
    ss << "}}";
    return ss.str();
}

std::string OnenetIotConfig::base64Decode(const std::string &input)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bio = BIO_new_mem_buf(input.c_str(), input.length());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    char *buf = new char[input.length()];
    int len = BIO_read(bio, buf, input.length());
    std::string result(buf, len);
    delete[] buf;
    BIO_free_all(bio);
    return result;
}

std::string OnenetIotConfig::base64Encode(const unsigned char *data,
                                           size_t len)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bio = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bio);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(b64, data, len);
    BIO_flush(b64);

    BUF_MEM *bufMem = nullptr;
    BIO_get_mem_ptr(b64, &bufMem);

    std::string result(bufMem->data, bufMem->length);
    BIO_free_all(b64);
    return result;
}

std::string OnenetIotConfig::urlEncode(const std::string &value)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_'
            || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2)
                    << int((unsigned char)c);
        }
    }
    return escaped.str();
}

std::string OnenetIotConfig::hmacSha1(const std::string &key,
                                       const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    HMAC(EVP_sha1(), key.c_str(), key.length(),
         (const unsigned char *)data.c_str(), data.length(),
         digest, &digestLen);

    return base64Encode(digest, digestLen);
}

std::string OnenetIotConfig::generateToken(const std::string &deviceKeyBase64,
                                            const std::string &res,
                                            long et,
                                            const std::string &method)
{
    std::string version = "2018-10-31";

    std::ostringstream ss;
    ss << et << "\n" << method << "\n" << res << "\n" << version;
    std::string stringToSign = ss.str();

    std::string rawKey = base64Decode(deviceKeyBase64);
    std::string sign = hmacSha1(rawKey, stringToSign);

    std::ostringstream token;
    token << "version=" << version
          << "&res=" << urlEncode(res)
          << "&et=" << et
          << "&method=" << method
          << "&sign=" << urlEncode(sign);

    return token.str();
}
