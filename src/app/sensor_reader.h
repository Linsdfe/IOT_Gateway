/**
 * @file sensor_reader.h
 * @brief 传感器读取器 - 基于动态插件架构的传感器数据采集接口
 *
 * SensorReader 是主程序与传感器插件之间的桥梁：
 * - 通过 PluginLoader 动态加载 .so 插件
 * - 支持短名称解析（--plugin sht30_bh1750_i2c 自动映射到 .so 路径）
 * - 提供统一的 readAll() 接口给 DataManager
 * - 支持运行时热替换插件（切换驱动无需重启主程序）
 * - 自动发现插件目录中的可用插件
 *
 * 插件短名称映射：
 *   sht30_bh1750_i2c    → libsht30_bh1750_i2c_plugin.so
 *   sht30_bh1750_kernel → libsht30_bh1750_kernel_plugin.so
 *   sht30_bh1750_custom → libsht30_bh1750_custom_plugin.so
 *   simulated           → libsimulated_plugin.so
 */

#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include "sensor_plugin.h"
#include "plugin_loader.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>

struct SensorData {
    float temperature;
    float humidity;
    float light;
    bool  valid;
};

class SensorReader {
public:
    SensorReader(const std::string &pluginDir = "/usr/lib/iot/plugins");
    ~SensorReader();

    bool init();
    bool readAll(SensorData &data);
    bool isSimulated() const;

    void setPluginDir(const std::string &dir);
    void setPluginPath(const std::string &path);
    void setPluginConfig(const std::string &config);

    bool hotSwapPlugin(const std::string &newPath, const std::string &config);
    std::vector<PluginInfo> listPlugins();
    std::string getPluginName() const;
    std::string getPluginPath() const;
    PluginLoader &loader();

    /**
     * @brief 解析插件名称为 .so 完整路径
     * @param name 插件短名称（如 "sht30_bh1750_i2c"）或完整路径
     * @return .so 文件完整路径，未找到返回空字符串
     *
     * 解析策略：
     * 1. 如果 name 包含 '/' 或以 "lib" 开头且以 ".so" 结尾，视为完整路径直接返回
     * 2. 否则在 pluginDir_ 中查找 lib<name>_plugin.so
     * 3. 如果找不到文件，扫描插件目录按插件名匹配
     */
    std::string resolvePluginName(const std::string &name);

private:
    void generateBuiltinSimulated(SensorData &data);

    PluginLoader loader_;
    std::string pluginDir_;
    std::string pluginPath_;
    std::string pluginConfig_;

    bool builtinSimulated_;
    int simStep_;
    float simTemp_;
    float simHumi_;
    float simLight_;
};

#endif
