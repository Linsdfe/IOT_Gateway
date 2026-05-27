/**
 * @file edge_compute_test.cpp
 * @brief 边缘计算模块单元测试
 *
 * 测试覆盖：
 *   1. 初始化与配置
 *   2. 滑动窗口移动平均滤波
 *   3. 时间窗口统计分析
 *   4. 变化检测
 *   5. 阈值告警（带滞回）
 *   6. 传感器故障检测
 *   7. 环形缓冲区
 *   8. 资源监控
 *   9. 配置更新与重置
 */

#include "edge_compute.h"
#include "sensor_reader.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

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

#define ASSERT_FLOAT_EQ(a, b, eps, msg) \
    do { if (fabsf((a) - (b)) > (eps)) { \
        char _buf[128]; \
        snprintf(_buf, sizeof(_buf), "%s (%.4f != %.4f)", msg, (float)(a), (float)(b)); \
        FAIL(_buf); \
    } else { PASS(); } } while(0)

static SensorData makeData(float t, float h, float l, bool v = true)
{
    SensorData d;
    d.temperature = t;
    d.humidity = h;
    d.light = l;
    d.valid = v;
    return d;
}

static void testInit()
{
    printf("\n=== Test: Init & Config ===\n");

    TEST("default init");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        ASSERT_TRUE(ec.init(cfg, 2000), "init failed");
    }

    TEST("custom config init");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.filterWindowSize = 10;
        cfg.statsWindowSec = 120;
        cfg.ringBufferSize = 100;
        ASSERT_TRUE(ec.init(cfg, 1000), "init with custom config failed");
    }

    TEST("initial state check");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        ec.init(cfg, 2000);
        ASSERT_TRUE(ec.isInitialized(), "should be initialized");
    }
}

static void testMovingAverage()
{
    printf("\n=== Test: Moving Average Filter ===\n");

    TEST("basic averaging");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.filterWindowSize = 5;
        ec.init(cfg, 2000);

        EdgeSensorData first = ec.processData(makeData(20.0f, 50.0f, 100.0f));
        ASSERT_FLOAT_EQ(first.tempFiltered, 20.0f, 0.01f, "single sample avg");
    }

    TEST("multiple samples averaging");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.filterWindowSize = 3;
        ec.init(cfg, 2000);

        ec.processData(makeData(20.0f, 50.0f, 100.0f));
        ec.processData(makeData(22.0f, 52.0f, 110.0f));
        EdgeSensorData result = ec.processData(makeData(24.0f, 54.0f, 120.0f));

        float expected = (20.0f + 22.0f + 24.0f) / 3.0f;
        ASSERT_FLOAT_EQ(result.tempFiltered, expected, 0.01f, "3-sample temp avg");
    }

    TEST("window sliding behavior");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.filterWindowSize = 3;
        ec.init(cfg, 2000);

        ec.processData(makeData(10.0f, 40.0f, 80.0f));
        ec.processData(makeData(12.0f, 42.0f, 82.0f));
        ec.processData(makeData(14.0f, 44.0f, 84.0f));
        EdgeSensorData result = ec.processData(makeData(16.0f, 46.0f, 86.0f));

        float expected = (12.0f + 14.0f + 16.0f) / 3.0f;
        ASSERT_FLOAT_EQ(result.tempFiltered, expected, 0.01f, "sliding window");
    }
}

static void testStatistics()
{
    printf("\n=== Test: Window Statistics ===\n");

    TEST("stats with multiple samples");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.statsWindowSec = 10;
        ec.init(cfg, 2000);

        ec.processData(makeData(20.0f, 50.0f, 100.0f));
        ec.processData(makeData(22.0f, 52.0f, 120.0f));
        ec.processData(makeData(24.0f, 54.0f, 110.0f));
        ec.processData(makeData(26.0f, 56.0f, 130.0f));
        ec.processData(makeData(28.0f, 58.0f, 140.0f));
        EdgeSensorData result = ec.processData(makeData(30.0f, 60.0f, 150.0f));

        ASSERT_FLOAT_EQ(result.tempAvg, (22+24+26+28+30)/5.0f, 0.01f, "temp avg (stats window=5)");
        ASSERT_FLOAT_EQ(result.tempMin, 22.0f, 0.01f, "temp min (stats window=5)");
        ASSERT_FLOAT_EQ(result.tempMax, 30.0f, 0.01f, "temp max");
    }

    TEST("stats window rolling");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.statsWindowSec = 2;
        ec.init(cfg, 1000);
        /* maxStatsSamples = 2*1000/1000 = 2 */

        ec.processData(makeData(30.0f, 50.0f, 100.0f));
        ec.processData(makeData(20.0f, 50.0f, 100.0f));
        ec.processData(makeData(25.0f, 50.0f, 100.0f));
        ec.processData(makeData(15.0f, 50.0f, 100.0f));
        EdgeSensorData result = ec.processData(makeData(10.0f, 50.0f, 100.0f));

        ASSERT_FLOAT_EQ(result.tempAvg, (10.0f + 15.0f) / 2.0f, 0.01f, "rolling avg");
        ASSERT_FLOAT_EQ(result.tempMin, 10.0f, 0.01f, "rolling min");
        ASSERT_FLOAT_EQ(result.tempMax, 15.0f, 0.01f, "rolling max");
    }
}

static void testChangeDetection()
{
    printf("\n=== Test: Change Detection ===\n");

    TEST("initial data always changed");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.changeThresholdTemp = 0.5f;
        ec.init(cfg, 2000);

        EdgeSensorData result = ec.processData(makeData(25.0f, 60.0f, 200.0f));
        ASSERT_TRUE(result.dataChanged, "first reading should be changed");
    }

    TEST("small change not detected");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.changeThresholdTemp = 0.5f;
        cfg.changeThresholdHumi = 1.0f;
        cfg.changeThresholdLight = 10.0f;
        ec.init(cfg, 2000);

        ec.processData(makeData(25.0f, 60.0f, 200.0f));
        EdgeSensorData result = ec.processData(makeData(25.3f, 60.5f, 205.0f));
        ASSERT_TRUE(!result.dataChanged, "small change should not be detected");
    }

    TEST("significant change detected");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.changeThresholdTemp = 0.5f;
        ec.init(cfg, 2000);

        ec.processData(makeData(25.0f, 60.0f, 200.0f));
        EdgeSensorData result = ec.processData(makeData(26.0f, 60.0f, 200.0f));
        ASSERT_TRUE(result.dataChanged, "1.0C change should be detected");
    }
}

static void testThresholdAlerts()
{
    printf("\n=== Test: Threshold Alerts with Hysteresis ===\n");

    TEST("high temp alert triggered");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.highTempThreshold = 35.0f;
        cfg.alertHysteresis = 2.0f;
        ec.init(cfg, 2000);

        ec.processData(makeData(36.0f, 50.0f, 200.0f));

        auto alerts = ec.getActiveAlerts();
        bool found = false;
        for (auto &a : alerts) {
            if (a.type == AlertType::HIGH_TEMP) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found, "high temp alert should be active");
    }

    TEST("high temp alert cleared with hysteresis");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.highTempThreshold = 35.0f;
        cfg.alertHysteresis = 2.0f;
        ec.init(cfg, 2000);

        ec.processData(makeData(36.0f, 50.0f, 200.0f));
        ec.processData(makeData(34.0f, 50.0f, 200.0f));
        ec.processData(makeData(33.0f, 50.0f, 200.0f));

        auto alerts = ec.getActiveAlerts();
        bool found = false;
        for (auto &a : alerts) {
            if (a.type == AlertType::HIGH_TEMP) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(!found, "high temp alert should be cleared");
    }

    TEST("hysteresis prevents alert oscillation");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.highTempThreshold = 35.0f;
        cfg.alertHysteresis = 2.0f;
        ec.init(cfg, 2000);

        ec.processData(makeData(36.0f, 50.0f, 200.0f));

        auto alerts1 = ec.getActiveAlerts();
        bool active1 = false;
        for (auto &a : alerts1) {
            if (a.type == AlertType::HIGH_TEMP) active1 = true;
        }
        ASSERT_TRUE(active1, "should trigger");

        ec.processData(makeData(34.5f, 50.0f, 200.0f)); /* above (35-2)=33, should stay active */

        auto alerts2 = ec.getActiveAlerts();
        bool active2 = false;
        for (auto &a : alerts2) {
            if (a.type == AlertType::HIGH_TEMP) active2 = true;
        }
        ASSERT_TRUE(active2, "should still be active (hysteresis)");
    }

    TEST("alert callback invoked");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.highTempThreshold = 35.0f;
        cfg.alertHysteresis = 2.0f;
        ec.init(cfg, 2000);

        int alertCount = 0;
        ec.registerAlertCallback([&](const AlertEvent &) {
            alertCount++;
        });

        ec.processData(makeData(36.0f, 50.0f, 200.0f));
        ASSERT_TRUE(alertCount > 0, "alert callback should be called");
    }
}

static void testSensorFaultDetection()
{
    printf("\n=== Test: Sensor Fault Detection ===\n");

    TEST("sensor fault detected");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.sensorFaultTimeout = 3;
        ec.init(cfg, 2000);

        ec.processData(makeData(25.0f, 60.0f, 200.0f, false));
        ec.processData(makeData(0, 0, 0, false));
        ec.processData(makeData(0, 0, 0, false));

        auto alerts = ec.getActiveAlerts();
        bool found = false;
        for (auto &a : alerts) {
            if (a.type == AlertType::SENSOR_FAULT) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found, "sensor fault alert should be active");
    }

    TEST("sensor fault recovered");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.sensorFaultTimeout = 3;
        ec.init(cfg, 2000);

        ec.processData(makeData(25.0f, 60.0f, 200.0f, false));
        ec.processData(makeData(0, 0, 0, false));
        ec.processData(makeData(0, 0, 0, false));
        ec.processData(makeData(25.0f, 60.0f, 200.0f, true));

        auto alerts = ec.getActiveAlerts();
        bool found = false;
        for (auto &a : alerts) {
            if (a.type == AlertType::SENSOR_FAULT) found = true;
        }
        ASSERT_TRUE(!found, "sensor fault alert should be cleared");
    }
}

static void testRingBuffer()
{
    printf("\n=== Test: Ring Buffer ===\n");

    TEST("ring buffer stores data");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.ringBufferSize = 5;
        ec.init(cfg, 2000);

        for (int i = 0; i < 10; i++) {
            ec.processData(makeData(20.0f + i, 50.0f, 100.0f));
        }

        auto recent = ec.getRecentData(3);
        ASSERT_TRUE((int)recent.size() == 3, "should get 3 recent records");
        ASSERT_FLOAT_EQ(recent[0].temperature, 27.0f, 0.1f, "first recent temp");
        ASSERT_FLOAT_EQ(recent[2].temperature, 29.0f, 0.1f, "last recent temp");
    }

    TEST("ring buffer wraps correctly");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.ringBufferSize = 5;
        ec.init(cfg, 2000);

        for (int i = 0; i < 20; i++) {
            ec.processData(makeData(10.0f + i, 50.0f, 100.0f));
        }

        auto all = ec.getRecentData(100);
        ASSERT_TRUE((int)all.size() == 5, "ring buffer should keep only 5 items");
    }
}

static void testResourceMonitoring()
{
    printf("\n=== Test: Resource Monitoring ===\n");

    TEST("resource usage queryable");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.enableResourceMonitor = true;
        ec.init(cfg, 2000);

        ec.processData(makeData(25.0f, 60.0f, 200.0f));

        ResourceUsage usage = ec.getResourceUsage();
        ASSERT_TRUE(usage.memUsedKb > 0, "memory usage should be positive");
    }

    TEST("resource monitor can be disabled");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.enableResourceMonitor = false;
        ec.init(cfg, 2000);

        ec.processData(makeData(25.0f, 60.0f, 200.0f));

        ResourceUsage usage = ec.getResourceUsage();
        ASSERT_TRUE(usage.cpuPercent == 0.0f, "cpu should be 0 when disabled");
    }
}

static void testConfigUpdate()
{
    printf("\n=== Test: Config Update ===\n");

    TEST("runtime config update");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.filterWindowSize = 5;
        ec.init(cfg, 2000);

        EdgeCompute::Config newCfg = cfg;
        newCfg.filterWindowSize = 3;
        ec.updateConfig(newCfg);

        ASSERT_TRUE(true, "config update should not crash");
    }
}

static void testReset()
{
    printf("\n=== Test: Reset ===\n");

    TEST("reset clears all state");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        cfg.highTempThreshold = 35.0f;
        ec.init(cfg, 2000);

        ec.processData(makeData(36.0f, 50.0f, 200.0f));
        ec.processData(makeData(37.0f, 51.0f, 210.0f));
        ec.processData(makeData(38.0f, 52.0f, 220.0f));

        ec.reset();

        EdgeSensorData data = ec.getLatestProcessed();
        ASSERT_FLOAT_EQ(data.temperature, 0.0f, 0.01f, "data should be cleared");

        auto alerts = ec.getActiveAlerts();
        ASSERT_TRUE(alerts.empty(), "alerts should be cleared");

        auto recent = ec.getRecentData(10);
        ASSERT_TRUE(recent.empty(), "ring buffer should be cleared");
    }
}

static void testDataCallback()
{
    printf("\n=== Test: Data Callback ===\n");

    TEST("data callback invoked on process");
    {
        EdgeCompute ec;
        EdgeCompute::Config cfg;
        ec.init(cfg, 2000);

        int cbCount = 0;
        float cbTemp = 0.0f;
        ec.registerDataCallback([&](const EdgeSensorData &d) {
            cbCount++;
            cbTemp = d.temperature;
        });

        ec.processData(makeData(25.5f, 60.0f, 200.0f));

        ASSERT_TRUE(cbCount == 1, "callback should be called once");
        ASSERT_FLOAT_EQ(cbTemp, 25.5f, 0.01f, "callback temp should match");
    }
}

int main()
{
    printf("========================================\n");
    printf("  Edge Compute Module Unit Tests\n");
    printf("========================================\n");

    testInit();
    testMovingAverage();
    testStatistics();
    testChangeDetection();
    testThresholdAlerts();
    testSensorFaultDetection();
    testRingBuffer();
    testResourceMonitoring();
    testConfigUpdate();
    testReset();
    testDataCallback();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);
    printf("========================================\n");

    return g_testsFailed > 0 ? 1 : 0;
}