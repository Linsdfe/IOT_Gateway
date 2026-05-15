/**
 * @file onenet_iot.cpp
 * @brief OneNET 物联网平台配置与 Token 认证实现
 *
 * 依赖 OpenSSL 库提供 HMAC-SHA1、Base64 编解码功能。
 * 辅助函数（base64Decode/base64Encode/urlEncode/hmacSha1）
 * 为文件内部使用，不暴露到头文件中。
 */

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

namespace {

/**
 * @brief Base64 解码
 * @param input Base64 编码的字符串
 * @return 解码后的原始字节串
 *
 * 使用 OpenSSL BIO 链实现，BIO_FLAGS_BASE64_NO_NL 表示输入不含换行符。
 */
std::string base64Decode(const std::string &input)
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

/**
 * @brief Base64 编码
 * @param data 原始字节数据
 * @param len  数据长度
 * @return Base64 编码字符串
 */
std::string base64Encode(const unsigned char *data, size_t len)
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

/**
 * @brief URL 编码（RFC 3986）
 * @param value 待编码字符串
 * @return 编码后的字符串
 *
 * 字母数字和 -_.~ 保持原样，其余字符编码为 %XX。
 */
std::string urlEncode(const std::string &value)
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

/**
 * @brief HMAC-SHA1 签名计算
 * @param key  签名密钥（原始字节）
 * @param data 待签名数据
 * @return Base64 编码的签名结果
 */
std::string hmacSha1(const std::string &key, const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    HMAC(EVP_sha1(), key.c_str(), key.length(),
         (const unsigned char *)data.c_str(), data.length(),
         digest, &digestLen);

    return base64Encode(digest, digestLen);
}

}

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

std::string OnenetIotConfig::generateToken(const std::string &deviceKeyBase64,
                                            const std::string &res,
                                            long et,
                                            const std::string &method)
{
    std::string version = "2018-10-31";

    /* 构造待签名字符串：过期时间 + 换行 + 方法 + 换行 + 资源路径 + 换行 + 版本 */
    std::ostringstream ss;
    ss << et << "\n" << method << "\n" << res << "\n" << version;
    std::string stringToSign = ss.str();

    /* deviceKey 在 OneNET 控制台上是 Base64 编码的，需先解码为原始字节 */
    std::string rawKey = base64Decode(deviceKeyBase64);

    /* 使用原始密钥对待签名字符串计算 HMAC-SHA1，结果 Base64 编码 */
    std::string sign = hmacSha1(rawKey, stringToSign);

    /* 拼接最终 Token：各参数需 URL 编码 */
    std::ostringstream token;
    token << "version=" << version
          << "&res=" << urlEncode(res)
          << "&et=" << et
          << "&method=" << method
          << "&sign=" << urlEncode(sign);

    return token.str();
}
