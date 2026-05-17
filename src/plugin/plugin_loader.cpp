/**
 * @file plugin_loader.cpp
 * @brief 动态插件加载器实现
 *
 * 基于 POSIX dlopen/dlsym/dlclose API 实现插件的运行时加载与卸载。
 * 使用 RTLD_NOW 标志在 dlopen 时立即解析所有符号，
 * 确保插件依赖缺失时能立即报错而非延迟到调用时崩溃。
 */

#include "plugin_loader.h"
#include "logger.h"
#include <dlfcn.h>
#include <dirent.h>
#include <cstring>

PluginLoader::PluginLoader()
    : handle_(nullptr)
    , plugin_(nullptr)
    , initialized_(false)
{
}

PluginLoader::~PluginLoader()
{
    unload();
}

bool PluginLoader::load(const std::string &path)
{
    if (handle_) {
        LOG_W("Plugin", "unloading existing plugin before loading new one");
        unload();
    }

    LOG_I("Plugin", "loading plugin: %s", path.c_str());

    void *handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        LOG_E("Plugin", "dlopen failed: %s", dlerror());
        return false;
    }

    dlerror();

    SensorPluginGetFunc getFunc = (SensorPluginGetFunc)dlsym(handle, "sensor_plugin_get");
    const char *error = dlerror();
    if (error) {
        LOG_E("Plugin", "dlsym 'sensor_plugin_get' failed: %s", error);
        dlclose(handle);
        return false;
    }

    const SensorPlugin *plugin = getFunc();
    if (!plugin) {
        LOG_E("Plugin", "sensor_plugin_get() returned NULL");
        dlclose(handle);
        return false;
    }

    if (plugin->api_version != SENSOR_PLUGIN_API_VERSION) {
        LOG_E("Plugin", "API version mismatch: plugin=%d, expected=%d",
              plugin->api_version, SENSOR_PLUGIN_API_VERSION);
        dlclose(handle);
        return false;
    }

    if (!plugin->init || !plugin->deinit || !plugin->read) {
        LOG_E("Plugin", "plugin '%s' has NULL function pointers (init/deinit/read)",
              plugin->name ? plugin->name : "unknown");
        dlclose(handle);
        return false;
    }

    handle_ = handle;
    plugin_ = plugin;
    pluginPath_ = path;
    initialized_ = false;

    LOG_I("Plugin", "loaded: %s - %s (api=%d)",
          plugin_->name, plugin_->description, plugin_->api_version);
    return true;
}

void PluginLoader::unload()
{
    if (!handle_) {
        return;
    }

    if (initialized_ && plugin_ && plugin_->deinit) {
        LOG_I("Plugin", "deinitializing plugin: %s", plugin_->name);
        plugin_->deinit();
        initialized_ = false;
    }

    LOG_I("Plugin", "unloading plugin: %s",
          plugin_ ? plugin_->name : "unknown");
    dlclose(handle_);
    handle_ = nullptr;
    plugin_ = nullptr;
    pluginPath_.clear();
    initialized_ = false;
}

bool PluginLoader::init(const std::string &config)
{
    if (!plugin_) {
        LOG_E("Plugin", "no plugin loaded, cannot init");
        return false;
    }

    if (initialized_) {
        LOG_W("Plugin", "plugin already initialized, deinit first");
        plugin_->deinit();
        initialized_ = false;
    }

    LOG_I("Plugin", "initializing plugin '%s' with config: '%s'",
          plugin_->name, config.c_str());

    if (!plugin_->init(config.c_str())) {
        LOG_E("Plugin", "plugin '%s' init failed", plugin_->name);
        return false;
    }

    initialized_ = true;
    LOG_I("Plugin", "plugin '%s' initialized successfully", plugin_->name);
    return true;
}

bool PluginLoader::read(PluginSensorData &data)
{
    if (!plugin_ || !initialized_) {
        return false;
    }
    return plugin_->read(&data);
}

bool PluginLoader::isSimulated() const
{
    if (!plugin_ || !plugin_->is_simulated) {
        return false;
    }
    return plugin_->is_simulated();
}

bool PluginLoader::hotSwap(const std::string &newPath, const std::string &config)
{
    LOG_I("Plugin", "hot-swapping: %s -> %s",
          pluginPath_.c_str(), newPath.c_str());

    std::string oldPath = pluginPath_;
    std::string oldName = plugin_ ? plugin_->name : "unknown";

    unload();

    if (!load(newPath)) {
        LOG_E("Plugin", "hot-swap failed: could not load new plugin '%s'", newPath.c_str());
        return false;
    }

    if (!init(config)) {
        LOG_E("Plugin", "hot-swap failed: could not init new plugin '%s'", newPath.c_str());
        unload();
        return false;
    }

    LOG_I("Plugin", "hot-swap complete: %s -> %s", oldName.c_str(), plugin_->name);
    return true;
}

std::vector<PluginInfo> PluginLoader::scanDirectory(const std::string &dirPath)
{
    std::vector<PluginInfo> result;

    DIR *dir = opendir(dirPath.c_str());
    if (!dir) {
        LOG_W("Plugin", "cannot open plugin directory: %s", dirPath.c_str());
        return result;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;

        if (name.size() < 12) continue;

        bool is_plugin_so = (name.find("lib") == 0 &&
                             name.find("_plugin.so") != std::string::npos);

        if (!is_plugin_so) continue;

        std::string fullPath = dirPath;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += name;

        void *handle = dlopen(fullPath.c_str(), RTLD_NOW);
        if (!handle) {
            LOG_D("Plugin", "skip %s: dlopen failed: %s", name.c_str(), dlerror());
            continue;
        }

        dlerror();
        SensorPluginGetFunc getFunc = (SensorPluginGetFunc)dlsym(handle, "sensor_plugin_get");
        if (dlerror() || !getFunc) {
            dlclose(handle);
            continue;
        }

        const SensorPlugin *p = getFunc();
        if (p) {
            PluginInfo info;
            info.path = fullPath;
            info.name = p->name ? p->name : "unknown";
            info.description = p->description ? p->description : "";
            info.apiVersion = p->api_version;
            result.push_back(info);
            LOG_D("Plugin", "found: %s (%s) api=%d",
                  info.name.c_str(), info.description.c_str(), info.apiVersion);
        }

        dlclose(handle);
    }

    closedir(dir);
    LOG_I("Plugin", "scanned %s, found %zu plugin(s)", dirPath.c_str(), result.size());
    return result;
}

bool PluginLoader::isLoaded() const
{
    return handle_ != nullptr && plugin_ != nullptr;
}

bool PluginLoader::isInitialized() const
{
    return isLoaded() && initialized_;
}

std::string PluginLoader::getPluginName() const
{
    if (plugin_ && plugin_->name) {
        return plugin_->name;
    }
    return "none";
}

std::string PluginLoader::getPluginPath() const
{
    return pluginPath_;
}

const SensorPlugin *PluginLoader::getPlugin() const
{
    return plugin_;
}
