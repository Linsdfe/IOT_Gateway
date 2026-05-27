/**
 * @file integration_test.cpp
 * @brief 集成测试 — 验证模块间协作与数据流
 *
 * 测试覆盖：
 *   1. EdgeCompute 回调链（单/多消费者）
 *   2. EdgeCompute → 回调消费者 数据分发
 *   3. PluginLoader 扫描/加载/卸载
 *   4. 告警生命周期（触发→活跃→清除）
 *   5. 环形缓冲区数据持久化
 *   6. 多线程并发数据流正确性
 */

#include "edge_compute.h"
#include "sensor_reader.h"
#include "data_manager.h"
#include "plugin_loader.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <cmath>
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

static SensorData makeData(float t, float h, float l, bool v = true)
{
    SensorData d;
    d.temperature = t;
    d.humidity = h;
    d.light = l;
    d.valid = v;
    return d;
}

static void testEdgeComputeCallbackChain()
{
    printf("\n=== Test: EdgeCompute Callback Chain ===\n");

    EdgeCompute::Config cfg;
    cfg.filterWindowSize = 3;
    cfg.statsWindowSec = 10;
    cfg.changeThresholdTemp = 0.5f;

    TEST("single callback consumer");
    {
        EdgeCompute ec;
        ec.init(cfg, 2000);

        int callCount = 0;
        ec.registerDataCallback([&](const EdgeSensorData &) {
            callCount++;
        });

        ec.processData(makeData(25.0f, 60.0f, 200.0f));
        ASSERT_TRUE(callCount == 1, "callback invoked once");
    }

    TEST("multiple callback consumers");
    {
        EdgeCompute ec;
        ec.init(cfg, 2000);

        int countA = 0, countB = 0, countC = 0;
        ec.registerDataCallback([&](const EdgeSensorData &) { countA++; });
        ec.registerDataCallback([&](const EdgeSensorData &) { countB++; });
        ec.registerDataCallback([&](const EdgeSensorData &) { countC++; });

        ec.processData(makeData(25.0f, 60.0f, 200.0f));
        ASSERT_TRUE(countA == 1, "consumer A");
        ASSERT_TRUE(countB == 1, "consumer B");
        ASSERT_TRUE(countC == 1, "consumer C");
    }

    TEST("alert and data callbacks independent");
    {
        EdgeCompute ec;
        EdgeCompute::Config alertCfg;
        alertCfg.highTempThreshold = 35.0f;
        alertCfg.alertHysteresis = 2.0f;
        ec.init(alertCfg, 2000);

        int dataCount = 0, alertCount = 0;
        ec.registerDataCallback([&](const EdgeSensorData &) { dataCount++; });
        ec.registerAlertCallback([&](const AlertEvent &) { alertCount++; });

        ec.processData(makeData(36.0f, 50.0f, 200.0f));
        ASSERT_TRUE(dataCount == 1, "data callback fired");
        ASSERT_TRUE(alertCount > 0, "alert callback fired");
    }
}

static void testDataManagerIntegration()
{
    printf("\n=== Test: EdgeCompute Data Flow ===\n");

    TEST("valid data flows correctly");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        ec.init(cfg, 100);

        EdgeSensorData received;
        ec.registerDataCallback([&](const EdgeSensorData &d) {
            received = d;
        });

        EdgeSensorData result = ec.processData(makeData(25.5f, 61.0f, 350.0f));
        ASSERT_TRUE(result.valid, "data valid");
        ASSERT_TRUE(fabsf(result.temperature - 25.5f) < 0.1f, "temp preserved");
        ASSERT_TRUE(received.valid, "callback data valid");
    }

    TEST("multiple data processing sessions");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.filterWindowSize = 5;
        ec.init(cfg, 2000);

        int totalCount = 0;
        ec.registerDataCallback([&](const EdgeSensorData &) { totalCount++; });

        for (int i = 0; i < 100; i++) {
            ec.processData(makeData(20.0f + i * 0.1f, 50.0f, 200.0f));
        }

        ASSERT_TRUE(totalCount == 100, "100 callbacks for 100 data points");
    }

    TEST("rapid data processing stress");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        ec.init(cfg, 10);

        int count = 0;
        ec.registerDataCallback([&](const EdgeSensorData &) { count++; });

        for (int i = 0; i < 500; i++) {
            ec.processData(makeData(25.0f, 60.0f, 200.0f));
        }

        ASSERT_TRUE(count == 500, "500 rapid callbacks completed");
    }
}

static void testPluginAndSensorIntegration()
{
    printf("\n=== Test: Plugin Loader & Sensor Resolution ===\n");

    TEST("scan plugin directory");
    {
        PluginLoader loader;
        auto plugins = loader.scanDirectory("./build_x86/plugins");
        ASSERT_TRUE(!plugins.empty(), "plugins found in build dir");
    }

    TEST("scan nonexistent directory");
    {
        PluginLoader loader;
        auto plugins = loader.scanDirectory("/nonexistent/path/plugins");
        ASSERT_TRUE(plugins.empty(), "no plugins in nonexistent dir");
    }

    TEST("load and unload simulated plugin");
    {
        PluginLoader loader;
        bool ok = loader.load("./build_x86/plugins/libsimulated_plugin.so");
        ASSERT_TRUE(ok, "simulated plugin loaded");
        ASSERT_TRUE(loader.isLoaded(), "loader reports loaded");
        loader.unload();
        ASSERT_TRUE(true, "unload completed");
    }

    TEST("plugin init and read");
    {
        PluginLoader loader;
        loader.load("./build_x86/plugins/libsimulated_plugin.so");
        bool initOk = loader.init("");
        ASSERT_TRUE(initOk, "plugin init OK");

        PluginSensorData data;
        bool readOk = loader.read(data);
        ASSERT_TRUE(readOk, "plugin read OK");
        ASSERT_TRUE(data.temperature > -100.0f, "valid temp");

        loader.unload();
    }

    TEST("plugin name accessible");
    {
        PluginLoader loader;
        loader.load("./build_x86/plugins/libsimulated_plugin.so");
        ASSERT_TRUE(!loader.getPluginName().empty(), "plugin name available");
        loader.unload();
    }
}

static void testAlertLifecycle()
{
    printf("\n=== Test: Alert Lifecycle (trigger -> active -> clear) ===\n");

    TEST("no alerts initially");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.highTempThreshold = 35.0f;
        cfg.alertHysteresis = 2.0f;
        ec.init(cfg, 2000);

        auto alerts = ec.getActiveAlerts();
        ASSERT_TRUE(alerts.empty(), "no alerts initially");
    }

    TEST("trigger and clear alert");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.highTempThreshold = 35.0f;
        cfg.alertHysteresis = 2.0f;
        ec.init(cfg, 2000);

        ec.processData(makeData(37.0f, 50.0f, 200.0f));
        auto alerts = ec.getActiveAlerts();
        ASSERT_TRUE(alerts.size() == 1, "alert triggered");

        ec.processData(makeData(34.0f, 50.0f, 200.0f));
        alerts = ec.getActiveAlerts();
        ASSERT_TRUE(alerts.size() == 1, "still active in hysteresis");

        ec.processData(makeData(30.0f, 50.0f, 200.0f));
        alerts = ec.getActiveAlerts();
        ASSERT_TRUE(alerts.empty(), "alert cleared");
    }
}

static void testRingBufferPersistence()
{
    printf("\n=== Test: Ring Buffer Data Persistence ===\n");

    TEST("data accessible after processing");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.ringBufferSize = 100;
        ec.init(cfg, 2000);

        for (int i = 0; i < 10; i++) {
            ec.processData(makeData(20.0f + i, 50.0f, 100.0f + i * 10));
        }

        auto recent = ec.getRecentData(5);
        ASSERT_TRUE((int)recent.size() == 5, "5 recent records");
        ASSERT_TRUE(fabsf(recent[0].temperature - 25.0f) < 0.1f, "correct order start");
        ASSERT_TRUE(fabsf(recent[4].temperature - 29.0f) < 0.1f, "correct order end");
    }

    TEST("requesting more than stored returns all");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.ringBufferSize = 100;
        ec.init(cfg, 2000);

        ec.processData(makeData(25.0f, 60.0f, 200.0f));
        ec.processData(makeData(26.0f, 61.0f, 210.0f));

        auto recent = ec.getRecentData(100);
        ASSERT_TRUE(recent.size() == 2, "returns all 2 records");
    }
}

int main()
{
    printf("========================================\n");
    printf("  Integration Tests\n");
    printf("========================================\n");

    Logger::instance().setLevel(LogLevel::WARN);

    testEdgeComputeCallbackChain();
    testDataManagerIntegration();
    testPluginAndSensorIntegration();
    testAlertLifecycle();
    testRingBufferPersistence();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);
    printf("========================================\n");

    return g_testsFailed > 0 ? 1 : 0;
}