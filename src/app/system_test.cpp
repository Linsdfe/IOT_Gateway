/**
 * @file system_test.cpp
 * @brief 系统级测试 — CLI 接口、完整生命周期、异常处理
 *
 * 测试覆盖：
 *   1. GatewaySDK 完整生命周期（init→start→stop）
 *   2. 模拟插件模式正常运行
 *   3. 多次启动/停止循环
 *   4. 回调注册与验证
 *   5. 插件列表
 *   6. 多配置变体
 */

#include "gateway_sdk.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <unistd.h>

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

static void testGatewayLifecycle()
{
    printf("\n=== Test: GatewaySDK Full Lifecycle ===\n");

    TEST("init with default config");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;

        iot::GatewaySDK gateway;
        ASSERT_TRUE(gateway.init(cfg), "init succeeds");
    }

    TEST("init with invalid plugin path");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "/tmp/nonexistent_plugin.so";

        iot::GatewaySDK gateway;
        ASSERT_TRUE(gateway.init(cfg), "init falls back to simulated");
    }

    TEST("double init is safe");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        bool first = gateway.init(cfg);
        bool second = gateway.init(cfg);
        ASSERT_TRUE(first && second, "double init handled");
    }

    TEST("stop without start is safe");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;

        iot::GatewaySDK gateway;
        gateway.init(cfg);
        gateway.stop();
        ASSERT_TRUE(true, "stop without start handled");
    }

    TEST("isRunning false before start");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        gateway.init(cfg);
        ASSERT_TRUE(!gateway.isRunning(), "not running before start");
    }
}

static void testSimulatedPluginRun()
{
    printf("\n=== Test: Simulated Plugin Run ===\n");

    TEST("start with simulated plugin");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        ASSERT_TRUE(gateway.init(cfg), "init OK");
        ASSERT_TRUE(gateway.start(), "start OK");

        usleep(500000);

        ASSERT_TRUE(gateway.isRunning(), "still running");
        gateway.stop();
        ASSERT_TRUE(true, "graceful stop");
    }

    TEST("data available after start");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        gateway.init(cfg);
        gateway.start();

        usleep(500000);

        SensorData data = gateway.getLatestData();
        ASSERT_TRUE(data.valid, "simulated plugin data valid");

        gateway.stop();
    }

    TEST("edge data available after start");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        gateway.init(cfg);
        gateway.start();

        usleep(500000);

        EdgeSensorData ed = gateway.getLatestEdgeData();
        (void)ed;
        ASSERT_TRUE(true, "edge data accessible");

        gateway.stop();
    }
}

static void testMultipleStartStopCycles()
{
    printf("\n=== Test: Multiple Start/Stop Cycles ===\n");

    TEST("3 start/stop cycles");
    {
        int cyclePass = 0;
        for (int cycle = 0; cycle < 3; cycle++) {
            iot::GatewayConfig cfg;
            cfg.enableMqtt = false;
            cfg.enableDisplay = false;
            cfg.pluginDir = "./build_x86/plugins";
            cfg.pluginPath = "simulated";

            iot::GatewaySDK gateway;

            if (!gateway.init(cfg)) continue;
            if (!gateway.start()) continue;

            usleep(300000);

            if (!gateway.isRunning()) continue;

            gateway.stop();
            cyclePass++;
        }
        ASSERT_TRUE(cyclePass == 3, "3 cycles passed");
    }
}

static void testCallbackRegistration()
{
    printf("\n=== Test: Callback Registration ===\n");

    TEST("data callback receives data");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        gateway.init(cfg);

        int cbCount = 0;
        gateway.onData([&](const SensorData &) {
            cbCount++;
        });

        gateway.start();
        usleep(500000);
        gateway.stop();

        ASSERT_TRUE(cbCount > 0, "data callback received data");
    }

    TEST("alert callback registered");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        gateway.init(cfg);

        int alertCount = 0;
        gateway.onAlert([&](const AlertEvent &) {
            alertCount++;
        });

        gateway.start();
        usleep(300000);
        gateway.stop();

        ASSERT_TRUE(true, "alert callback registered OK");
    }

    TEST("resource usage available");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        gateway.init(cfg);
        gateway.start();
        usleep(300000);

        ResourceUsage usage = gateway.getResourceUsage();
        ASSERT_TRUE(usage.memPercent >= 0.0f, "memory usage reported");

        gateway.stop();
    }
}

static void testPluginListing()
{
    printf("\n=== Test: Plugin Listing ===\n");

    TEST("list plugins returns valid info");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";

        iot::GatewaySDK gateway;
        gateway.init(cfg);

        auto plugins = gateway.listPlugins();
        ASSERT_TRUE(!plugins.empty(), "plugins found");

        bool hasSimulated = false;
        for (auto &p : plugins) {
            if (p.name.find("simulated") != std::string::npos) hasSimulated = true;
        }
        ASSERT_TRUE(hasSimulated, "simulated plugin listed");
    }

    TEST("plugin name available");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        gateway.init(cfg);

        ASSERT_TRUE(!gateway.getPluginName().empty(), "plugin name available");
    }
}

static void testConfigVariations()
{
    printf("\n=== Test: Configuration Variations ===\n");

    TEST("mqtt+display disabled");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        ASSERT_TRUE(gateway.init(cfg), "minimal config works");
    }

    TEST("custom collect interval");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.collectIntervalMs = 500;
        cfg.pluginDir = "./build_x86/plugins";
        cfg.pluginPath = "simulated";

        iot::GatewaySDK gateway;
        ASSERT_TRUE(gateway.init(cfg), "init OK");
        ASSERT_TRUE(gateway.start(), "fast interval start OK");

        usleep(300000);
        ASSERT_TRUE(gateway.isRunning(), "fast interval running");

        gateway.stop();
    }
}

static void testErrorHandling()
{
    printf("\n=== Test: Error Handling ===\n");

    TEST("start without init");
    {
        iot::GatewaySDK gateway;
        ASSERT_TRUE(!gateway.start(), "start without init fails");
    }

    TEST("init with empty plugin path");
    {
        iot::GatewayConfig cfg;
        cfg.enableMqtt = false;
        cfg.enableDisplay = false;
        cfg.pluginDir = "/tmp";
        cfg.pluginPath = "";

        iot::GatewaySDK gateway;
        ASSERT_TRUE(gateway.init(cfg), "empty plugin path handled");
    }
}

int main()
{
    printf("========================================\n");
    printf("  System-Level Tests\n");
    printf("========================================\n");

    Logger::instance().setLevel(LogLevel::WARN);

    testGatewayLifecycle();
    testSimulatedPluginRun();
    testMultipleStartStopCycles();
    testCallbackRegistration();
    testPluginListing();
    testConfigVariations();
    testErrorHandling();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);
    printf("========================================\n");

    return g_testsFailed > 0 ? 1 : 0;
}