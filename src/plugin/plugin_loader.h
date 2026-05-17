/**
 * @file plugin_loader.h
 * @brief 动态插件加载器 - 运行时加载/卸载/热替换传感器 .so 插件
 *
 * PluginLoader 封装了 dlopen/dlsym/dlclose 等 POSIX 动态链接库操作，
 * 提供传感器插件的完整生命周期管理：
 * - 从指定路径加载 .so 文件
 * - 校验插件 API 版本兼容性
 * - 初始化/反初始化插件
 * - 运行时热替换（卸载旧插件 → 加载新插件）
 * - 扫描插件目录自动发现可用插件
 *
 * 线程安全说明：
 *   PluginLoader 本身不是线程安全的，调用方应在 DataManager
 *   采集线程之外进行插件加载/卸载操作，或自行加锁保护。
 */

#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include "sensor_plugin.h"
#include <string>
#include <vector>

/**
 * @brief 插件信息结构体
 *
 * 描述一个已发现或已加载的插件的基本信息，
 * 用于插件目录扫描和用户界面展示。
 */
struct PluginInfo {
    std::string path;        ///< .so 文件绝对路径
    std::string name;        ///< 插件名称（从 plugin->name 读取）
    std::string description; ///< 插件描述（从 plugin->description 读取）
    int apiVersion;          ///< 插件 API 版本号
};

/**
 * @brief 动态插件加载器
 *
 * 管理传感器 .so 插件的加载、初始化、数据读取和热替换。
 * 同一时刻只有一个插件处于活跃状态。
 *
 * 典型使用流程：
 * @code
 *   PluginLoader loader;
 *   loader.load("/usr/lib/iot/plugins/libsht30_bh1750_plugin.so");
 *   loader.init("i2c=/dev/i2c-1;sht30=0x44;bh1750=0x23");
 *
 *   PluginSensorData data;
 *   loader.read(data);
 *
 *   // 热替换：切换到模拟插件
 *   loader.unload();
 *   loader.load("/usr/lib/iot/plugins/libsimulated_plugin.so");
 *   loader.init("");
 * @endcode
 */
class PluginLoader {
public:
    PluginLoader();
    ~PluginLoader();

    /**
     * @brief 加载指定路径的 .so 插件
     * @param path .so 文件的绝对路径或相对路径
     * @return true 加载成功且 API 版本兼容
     *
     * 流程：
     * 1. 如果已有插件加载，先卸载（调用 deinit + dlclose）
     * 2. dlopen() 打开 .so 文件（RTLD_NOW 立即解析符号）
     * 3. dlsym() 查找 "sensor_plugin_get" 入口函数
     * 4. 调用入口函数获取 SensorPlugin 接口
     * 5. 校验 api_version 是否匹配
     *
     * 加载成功后插件处于"已加载但未初始化"状态，
     * 需要调用 init() 才能使用 read()。
     */
    bool load(const std::string &path);

    /**
     * @brief 卸载当前插件
     *
     * 流程：
     * 1. 如果插件已初始化，调用 plugin->deinit()
     * 2. dlclose() 关闭 .so 句柄
     * 3. 重置所有内部状态
     */
    void unload();

    /**
     * @brief 初始化当前已加载的插件
     * @param config 配置字符串，传递给插件的 init() 函数
     * @return true 初始化成功
     *
     * 仅在 load() 成功后调用。
     * config 格式为键值对字符串，如 "i2c=/dev/i2c-1;sht30=0x44;bh1750=0x23"
     * 具体格式由各插件自行定义和解析。
     */
    bool init(const std::string &config);

    /**
     * @brief 通过当前插件读取传感器数据
     * @param data 输出参数，填充传感器数据
     * @return true 读取成功
     *
     * 仅在 init() 成功后调用。
     * 内部调用 plugin->read(data)。
     */
    bool read(PluginSensorData &data);

    /**
     * @brief 查询当前插件是否为模拟模式
     * @return true 当前为模拟数据模式
     */
    bool isSimulated() const;

    /**
     * @brief 热替换当前插件
     * @param newPath 新插件的 .so 文件路径
     * @param config  新插件的配置字符串
     * @return true 替换成功
     *
     * 原子性操作：先卸载旧插件，再加载新插件。
     * 如果新插件加载失败，旧插件不会被恢复（调用方需处理此情况）。
     *
     * 流程：unload() → load(newPath) → init(config)
     */
    bool hotSwap(const std::string &newPath, const std::string &config);

    /**
     * @brief 扫描目录发现所有可用插件
     * @param dirPath 插件目录路径
     * @return 发现的插件信息列表
     *
     * 扫描指定目录下所有 lib*_plugin.so 文件，
     * 临时 dlopen 获取插件名称和描述，然后 dlclose。
     * 不会影响当前已加载的插件。
     */
    std::vector<PluginInfo> scanDirectory(const std::string &dirPath);

    /**
     * @brief 查询是否有插件已加载
     * @return true 有插件已加载（可能未初始化）
     */
    bool isLoaded() const;

    /**
     * @brief 查询插件是否已初始化
     * @return true 插件已加载且初始化成功
     */
    bool isInitialized() const;

    /**
     * @brief 获取当前插件的名称
     * @return 插件名称字符串，未加载时返回 "none"
     */
    std::string getPluginName() const;

    /**
     * @brief 获取当前 .so 文件路径
     * @return .so 文件路径，未加载时返回空字符串
     */
    std::string getPluginPath() const;

    /**
     * @brief 获取当前插件的原始接口指针
     * @return SensorPlugin 指针，未加载时返回 nullptr
     *
     * 用于需要直接访问插件接口的高级场景。
     */
    const SensorPlugin *getPlugin() const;

private:
    void *handle_;                  ///< dlopen 返回的 .so 句柄
    const SensorPlugin *plugin_;    ///< 插件接口指针（通过 sensor_plugin_get 获取）
    bool initialized_;              ///< 插件是否已调用 init() 成功
    std::string pluginPath_;        ///< 当前 .so 文件路径
};

#endif
