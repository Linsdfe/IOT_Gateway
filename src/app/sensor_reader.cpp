/**
 * @file sensor_reader.cpp
 * @brief 传感器读取器实现 - 基于动态插件架构
 */

#include "sensor_reader.h"
#include "logger.h"
#include <cmath>
#include <dirent.h>
#include <sys/stat.h>

SensorReader::SensorReader(const std::string &pluginDir)
    : pluginDir_(pluginDir)
    , builtinSimulated_(false)
    , simStep_(0)
    , simTemp_(25.0f)
    , simHumi_(60.0f)
    , simLight_(100.0f)
{
}

SensorReader::~SensorReader()
{
    if (loader_.isLoaded()) {
        loader_.unload();
    }
}

std::string SensorReader::resolvePluginName(const std::string &name)
{
    if (name.empty()) return "";

    if (name.find('/') != std::string::npos) return name;

    if (name.size() > 3 && name.substr(0, 3) == "lib" &&
        name.size() > 3 && name.substr(name.size() - 3) == ".so") {
        return name;
    }

    std::string soPath = pluginDir_;
    if (!soPath.empty() && soPath.back() != '/') soPath += '/';
    soPath += "lib" + name + "_plugin.so";

    struct stat st;
    if (stat(soPath.c_str(), &st) == 0) {
        LOG_D("Sensor", "resolved plugin '%s' -> %s", name.c_str(), soPath.c_str());
        return soPath;
    }

    soPath = pluginDir_;
    if (!soPath.empty() && soPath.back() != '/') soPath += '/';
    soPath += "lib" + name + ".so";

    if (stat(soPath.c_str(), &st) == 0) {
        LOG_D("Sensor", "resolved plugin '%s' -> %s", name.c_str(), soPath.c_str());
        return soPath;
    }

    LOG_D("Sensor", "scanning plugin dir for name '%s'", name.c_str());
    std::vector<PluginInfo> plugins = loader_.scanDirectory(pluginDir_);
    for (const auto &p : plugins) {
        if (p.name == name) {
            LOG_D("Sensor", "resolved plugin '%s' -> %s (by scan)", name.c_str(), p.path.c_str());
            return p.path;
        }
    }

    LOG_W("Sensor", "cannot resolve plugin name '%s'", name.c_str());
    return "";
}

bool SensorReader::init()
{
    if (!pluginPath_.empty()) {
        std::string resolved = resolvePluginName(pluginPath_);
        if (!resolved.empty()) {
            LOG_I("Sensor", "loading specified plugin: %s (resolved from '%s')",
                  resolved.c_str(), pluginPath_.c_str());
            if (loader_.load(resolved)) {
                if (loader_.init(pluginConfig_)) {
                    LOG_I("Sensor", "plugin '%s' loaded and initialized", loader_.getPluginName().c_str());
                    return true;
                }
                LOG_W("Sensor", "plugin '%s' init failed, falling back", loader_.getPluginName().c_str());
                loader_.unload();
            }
        }
        LOG_W("Sensor", "specified plugin failed, trying plugin directory");
    }

    if (!pluginDir_.empty()) {
        LOG_I("Sensor", "scanning plugin directory: %s", pluginDir_.c_str());
        std::vector<PluginInfo> plugins = loader_.scanDirectory(pluginDir_);

        if (!plugins.empty()) {
            const PluginInfo &first = plugins[0];
            LOG_I("Sensor", "found %zu plugin(s), loading: %s", plugins.size(), first.name.c_str());

            if (loader_.load(first.path)) {
                std::string config = pluginConfig_.empty() ? "" : pluginConfig_;
                if (loader_.init(config)) {
                    LOG_I("Sensor", "plugin '%s' loaded and initialized", loader_.getPluginName().c_str());
                    return true;
                }
                LOG_W("Sensor", "plugin '%s' init failed", first.name.c_str());
                loader_.unload();
            }
        }
    }

    builtinSimulated_ = true;
    LOG_W("Sensor", "no plugin available, using BUILTIN SIMULATED MODE");
    return true;
}

bool SensorReader::readAll(SensorData &data)
{
    if (builtinSimulated_) {
        generateBuiltinSimulated(data);
        return true;
    }

    if (!loader_.isInitialized()) {
        generateBuiltinSimulated(data);
        return true;
    }

    PluginSensorData pdata;
    if (loader_.read(pdata)) {
        data.temperature = pdata.temperature;
        data.humidity = pdata.humidity;
        data.light = pdata.light;
        data.valid = pdata.valid;
        return true;
    }

    data.temperature = 0.0f;
    data.humidity = 0.0f;
    data.light = 0.0f;
    data.valid = false;
    return false;
}

bool SensorReader::isSimulated() const
{
    if (builtinSimulated_) return true;
    return loader_.isSimulated();
}

void SensorReader::setPluginDir(const std::string &dir) { pluginDir_ = dir; }
void SensorReader::setPluginPath(const std::string &path) { pluginPath_ = path; }
void SensorReader::setPluginConfig(const std::string &config) { pluginConfig_ = config; }

bool SensorReader::hotSwapPlugin(const std::string &newPath, const std::string &config)
{
    std::string resolved = resolvePluginName(newPath);
    if (resolved.empty()) resolved = newPath;

    LOG_I("Sensor", "hot-swapping plugin to: %s", resolved.c_str());
    builtinSimulated_ = false;

    if (loader_.hotSwap(resolved, config)) {
        LOG_I("Sensor", "hot-swap successful, now using: %s", loader_.getPluginName().c_str());
        return true;
    }

    builtinSimulated_ = true;
    LOG_E("Sensor", "hot-swap failed, falling back to builtin simulated mode");
    return false;
}

std::vector<PluginInfo> SensorReader::listPlugins() { return loader_.scanDirectory(pluginDir_); }

std::string SensorReader::getPluginName() const
{
    if (builtinSimulated_) return "builtin_simulated";
    return loader_.getPluginName();
}

std::string SensorReader::getPluginPath() const
{
    if (builtinSimulated_) return "";
    return loader_.getPluginPath();
}

PluginLoader &SensorReader::loader() { return loader_; }

void SensorReader::generateBuiltinSimulated(SensorData &data)
{
    simStep_++;
    float t = simStep_ * 0.05f;
    simTemp_ = 25.0f + 5.0f * sinf(t * 0.3f);
    simHumi_ = 60.0f + 15.0f * sinf(t * 0.2f + 1.0f);
    simLight_ = 200.0f + 150.0f * sinf(t * 0.15f + 2.0f);
    if (simHumi_ < 0) simHumi_ = 0;
    if (simHumi_ > 100) simHumi_ = 100;
    if (simLight_ < 0) simLight_ = 0;
    data.temperature = simTemp_;
    data.humidity = simHumi_;
    data.light = simLight_;
    data.valid = true;
}
