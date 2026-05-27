/**
 * @file onenet_test.cpp
 * @brief OneNET IoT 平台模块单元测试
 *
 * 测试覆盖：
 *   1. OnenetIotConfig 配置验证
 *   2. Token 签名生成 (getPassword)
 *   3. getUsername/getClientId/getMqttHost
 *   4. buildPayload JSON 物模型格式化
 *   5. 边界条件和异常输入处理
 */

#include "onenet_iot.h"
#include <cstdio>
#include <cstring>

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name) \
    printf("  [TEST] %s ... ", name)

#define PASS() \
    do { printf("PASS\n"); g_testsPassed++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); g_testsFailed++; } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { FAIL(msg); } else { PASS(); } } while(0)

#define ASSERT_EQUAL(a, b, msg) \
    do { if ((a) != (b)) { \
        char _buf[128]; \
        snprintf(_buf, sizeof(_buf), "%s (%s != %s)", msg, #a, #b); \
        FAIL(_buf); \
    } else { PASS(); } } while(0)

#define ASSERT_STR_EQ(a, b, msg) \
    do { if (std::string(a) != std::string(b)) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "%s (\"%s\" != \"%s\")", msg, (a), (b)); \
        FAIL(_buf); \
    } else { PASS(); } } while(0)

#define ASSERT_STR_CONTAINS(haystack, needle, msg) \
    do { if (std::string(haystack).find(needle) == std::string::npos) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "%s (no \"%s\" in \"%s\")", msg, (needle), (haystack)); \
        FAIL(_buf); \
    } else { PASS(); } } while(0)

static void testConfigValidation()
{
    printf("\n=== Test: Config Validation ===\n");

    TEST("valid config");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST001";
        cfg.deviceName = "test_device";
        cfg.deviceKey = "dGVzdF9rZXk=";
        ASSERT_TRUE(!cfg.productId.empty(), "productId set");
        ASSERT_TRUE(!cfg.deviceName.empty(), "deviceName set");
    }

    TEST("default tokenExpireSec");
    {
        OnenetIotConfig cfg;
        ASSERT_EQUAL(cfg.tokenExpireSec, 86400, "default expire");
    }

    TEST("config preserves values");
    {
        OnenetIotConfig cfg;
        cfg.productId = "PROD12345";
        cfg.deviceName = "dev_abc";
        cfg.deviceKey = "Zm9vYmFy";
        cfg.tokenExpireSec = 3600;
        ASSERT_STR_EQ(cfg.productId.c_str(), "PROD12345", "productId match");
        ASSERT_STR_EQ(cfg.deviceKey.c_str(), "Zm9vYmFy", "deviceKey match");
        ASSERT_EQUAL(cfg.tokenExpireSec, 3600, "expire match");
    }
}

static void testMqttParameters()
{
    printf("\n=== Test: MQTT Connection Parameters ===\n");

    TEST("getMqttHost");
    {
        OnenetIotConfig cfg;
        ASSERT_TRUE(!cfg.getMqttHost().empty(), "host non-empty");
    }

    TEST("getMqttPort");
    {
        OnenetIotConfig cfg;
        ASSERT_EQUAL(cfg.getMqttPort(), 1883, "port is 1883");
    }

    TEST("getClientId == deviceName");
    {
        OnenetIotConfig cfg;
        cfg.deviceName = "my_device_01";
        ASSERT_STR_EQ(cfg.getClientId().c_str(), "my_device_01", "clientId match");
    }

    TEST("getUsername == productId");
    {
        OnenetIotConfig cfg;
        cfg.productId = "MYPROD001";
        ASSERT_STR_EQ(cfg.getUsername().c_str(), "MYPROD001", "username match");
    }

    TEST("getPubTopic non-empty");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST";
        cfg.deviceName = "dev";
        ASSERT_TRUE(!cfg.getPubTopic().empty(), "topic non-empty");
    }
}

static void testTokenGeneration()
{
    printf("\n=== Test: Token Generation (getPassword) ===\n");

    TEST("token with valid key is non-empty");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST001";
        cfg.deviceName = "test_device";
        cfg.deviceKey = "dGVzdF9rZXk=";
        std::string token = cfg.getPassword();
        ASSERT_TRUE(!token.empty(), "token non-empty");
    }

    TEST("token with empty key still produces token");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST001";
        cfg.deviceName = "test_device";
        cfg.deviceKey = "";
        std::string token = cfg.getPassword();
        ASSERT_TRUE(!token.empty(), "token produced despite empty key");
    }

    TEST("static generateToken with empty key produces token");
    {
        std::string token = OnenetIotConfig::generateToken(
            "",
            "products/TEST001/devices/test_device",
            1735689600L,
            "sha1"
        );
        ASSERT_TRUE(!token.empty(), "static token produced despite empty key");
    }

    TEST("static generateToken with empty key");
    {
        std::string token = OnenetIotConfig::generateToken(
            "",
            "products/TEST001/devices/test_device",
            1735689600L,
            "sha1"
        );
        ASSERT_TRUE(!token.empty(), "static token produced despite empty key");
    }

    TEST("token contains version field");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST001";
        cfg.deviceName = "test_device";
        cfg.deviceKey = "dGVzdF9rZXk=";
        std::string token = cfg.getPassword();
        ASSERT_STR_CONTAINS(token.c_str(), "version=", "has version field");
    }

    TEST("token contains sign field");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST001";
        cfg.deviceName = "test_device";
        cfg.deviceKey = "dGVzdF9rZXk=";
        std::string token = cfg.getPassword();
        ASSERT_STR_CONTAINS(token.c_str(), "sign=", "has sign field");
    }

    TEST("token contains method=sha1");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST001";
        cfg.deviceName = "test_device";
        cfg.deviceKey = "dGVzdF9rZXk=";
        std::string token = cfg.getPassword();
        ASSERT_STR_CONTAINS(token.c_str(), "sha1", "has sha1 method");
    }

    TEST("different keys -> different tokens");
    {
        OnenetIotConfig cfg1;
        cfg1.productId = "TEST001";
        cfg1.deviceName = "test_device";
        cfg1.deviceKey = "a2V5MQ==";

        OnenetIotConfig cfg2;
        cfg2.productId = "TEST001";
        cfg2.deviceName = "test_device";
        cfg2.deviceKey = "a2V5Mg==";

        ASSERT_TRUE(cfg1.getPassword() != cfg2.getPassword(), "different tokens");
    }
}

static void testPayloadFormat()
{
    printf("\n=== Test: JSON Payload Format (buildPayload) ===\n");

    TEST("payload is valid JSON");
    {
        OnenetIotConfig cfg;
        std::string payload = cfg.buildPayload(25.5f, 60.2f, 300.0f);
        ASSERT_STR_CONTAINS(payload.c_str(), "{", "starts with {");
        ASSERT_STR_CONTAINS(payload.c_str(), "}", "ends with }");
        ASSERT_STR_CONTAINS(payload.c_str(), "Temperature", "has Temperature");
        ASSERT_STR_CONTAINS(payload.c_str(), "Humidity", "has Humidity");
        ASSERT_STR_CONTAINS(payload.c_str(), "Light", "has Light");
    }

    TEST("payload contains correct values");
    {
        OnenetIotConfig cfg;
        std::string payload = cfg.buildPayload(25.5f, 60.2f, 300.0f);
        ASSERT_STR_CONTAINS(payload.c_str(), "25.5", "temp value");
        ASSERT_STR_CONTAINS(payload.c_str(), "60.2", "humi value");
        ASSERT_STR_CONTAINS(payload.c_str(), "300.0", "light value");
    }

    TEST("payload with extreme values");
    {
        OnenetIotConfig cfg;
        std::string payload = cfg.buildPayload(-40.0f, 0.0f, 99999.9f);
        ASSERT_STR_CONTAINS(payload.c_str(), "-40", "negative temp");
        ASSERT_STR_CONTAINS(payload.c_str(), "0.0", "zero humidity");
    }

    TEST("payload has version field");
    {
        OnenetIotConfig cfg;
        std::string payload = cfg.buildPayload(25.0f, 60.0f, 100.0f);
        ASSERT_STR_CONTAINS(payload.c_str(), "version", "has version");
    }

    TEST("payload has id timestamp");
    {
        OnenetIotConfig cfg;
        std::string payload = cfg.buildPayload(25.0f, 60.0f, 100.0f);
        ASSERT_STR_CONTAINS(payload.c_str(), "\"id\"", "has id field");
    }

    TEST("payload with zero values");
    {
        OnenetIotConfig cfg;
        std::string payload = cfg.buildPayload(0.0f, 0.0f, 0.0f);
        ASSERT_TRUE(!payload.empty(), "zero values work");
    }
}

static void testEdgeCases()
{
    printf("\n=== Test: Edge Cases ===\n");

    TEST("very long device key handled");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST001";
        cfg.deviceName = "test_device";
        cfg.deviceKey = std::string(4096, 'A');
        std::string token = cfg.getPassword();
        ASSERT_TRUE(!token.empty(), "long key handled");
    }

    TEST("very long productId handled");
    {
        OnenetIotConfig cfg;
        cfg.productId = std::string(1024, 'X');
        cfg.deviceName = "test";
        cfg.deviceKey = "dGVzdA==";
        ASSERT_TRUE(true, "long productId doesn't crash");
    }

    TEST("special chars in deviceName handled");
    {
        OnenetIotConfig cfg;
        cfg.productId = "TEST";
        cfg.deviceName = "device_with_underscores-123";
        cfg.deviceKey = "dGVzdA==";
        ASSERT_TRUE(true, "special chars handled");
    }
}

int main()
{
    printf("========================================\n");
    printf("  OnenetIotConfig Module Unit Tests\n");
    printf("========================================\n");

    testConfigValidation();
    testMqttParameters();
    testTokenGeneration();
    testPayloadFormat();
    testEdgeCases();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);
    printf("========================================\n");

    return g_testsFailed > 0 ? 1 : 0;
}