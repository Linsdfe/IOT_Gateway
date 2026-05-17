/**
 * @file simulated_plugin.cpp
 * @brief 模拟数据传感器插件 - 用于无硬件环境下的测试和演示
 *
 * 生成正弦波仿真传感器数据，无需任何硬件连接。
 * 适用于：
 *   - 开发调试阶段（无传感器硬件）
 *   - 功能演示和 UI 展示
 *   - 自动化测试
 *   - 热替换演示（从真实插件切换到模拟插件）
 *
 * 配置字符串格式：
 *   "temp=25;humi=60;light=200"  设置基准值
 *   空字符串使用默认基准值
 *
 * 编译命令：
 *   g++ -shared -fPIC -o libsimulated_plugin.so simulated_plugin.cpp
 */

#include "sensor_plugin.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>

static struct {
    float baseTemp;
    float baseHumi;
    float baseLight;
    int step;
} g_sim;

static void parseConfig(const char *config)
{
    g_sim.baseTemp = 25.0f;
    g_sim.baseHumi = 60.0f;
    g_sim.baseLight = 200.0f;

    if (!config || config[0] == '\0') return;

    std::string cfg(config);
    size_t pos = 0;

    while (pos < cfg.size()) {
        size_t sep = cfg.find('=', pos);
        if (sep == std::string::npos) break;

        std::string key = cfg.substr(pos, sep - pos);
        size_t end = cfg.find(';', sep + 1);
        if (end == std::string::npos) end = cfg.size();

        std::string val = cfg.substr(sep + 1, end - sep - 1);

        if (key == "temp") {
            g_sim.baseTemp = (float)atof(val.c_str());
        } else if (key == "humi") {
            g_sim.baseHumi = (float)atof(val.c_str());
        } else if (key == "light") {
            g_sim.baseLight = (float)atof(val.c_str());
        }

        pos = end + 1;
    }
}

static bool pluginInit(const char *config)
{
    memset(&g_sim, 0, sizeof(g_sim));
    parseConfig(config);
    g_sim.step = 0;
    return true;
}

static void pluginDeinit(void)
{
    g_sim.step = 0;
}

static bool pluginRead(PluginSensorData *data)
{
    g_sim.step++;
    float t = g_sim.step * 0.05f;

    float temp = g_sim.baseTemp + 5.0f * sinf(t * 0.3f);
    float humi = g_sim.baseHumi + 15.0f * sinf(t * 0.2f + 1.0f);
    float light = g_sim.baseLight + 150.0f * sinf(t * 0.15f + 2.0f);

    if (humi < 0) humi = 0;
    if (humi > 100) humi = 100;
    if (light < 0) light = 0;

    data->temperature = temp;
    data->humidity = humi;
    data->light = light;
    data->valid = true;
    return true;
}

static bool pluginIsSimulated(void)
{
    return true;
}

static const SensorPlugin simulated_plugin = {
    SENSOR_PLUGIN_API_VERSION,
    "simulated",
    "Simulated sensor data (sine wave, no hardware required)",
    pluginInit,
    pluginDeinit,
    pluginRead,
    pluginIsSimulated
};

extern "C" const SensorPlugin *sensor_plugin_get(void)
{
    return &simulated_plugin;
}
