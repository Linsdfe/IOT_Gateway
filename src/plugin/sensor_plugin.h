/**
 * @file sensor_plugin.h
 * @brief 传感器动态插件接口 - 定义所有 .so 插件必须遵循的 C 接口规范
 *
 * 设计原则：
 *   1. 纯 C 接口（extern "C"），避免 C++ ABI 兼容性问题
 *   2. 版本化 API，主程序可校验插件兼容性
 *   3. 每个插件以 .so 形式存在，通过 dlopen/dlsym 运行时加载
 *   4. 插件可热替换：替换 .so 文件后重新加载即可切换驱动
 *
 * 插件开发步骤：
 *   1. #include "sensor_plugin.h"
 *   2. 实现所有函数指针指向的函数
 *   3. 定义一个 const SensorPlugin 结构体实例
 *   4. 导出 sensor_plugin_get() 函数返回该实例
 *   5. 编译为 .so：gcc -shared -fPIC -o libxxx_plugin.so xxx_plugin.c
 *
 * 插件加载流程：
 *   主程序 → dlopen("libxxx_plugin.so") → dlsym("sensor_plugin_get")
 *         → 调用 sensor_plugin_get() 获取 SensorPlugin*
 *         → 校验 api_version → 调用 init() → 循环调用 read()
 *         → 需要切换时调用 deinit() → dlclose() → dlopen 新 .so
 */

#ifndef SENSOR_PLUGIN_H
#define SENSOR_PLUGIN_H

#include <stdbool.h>

#define SENSOR_PLUGIN_API_VERSION 1

/**
 * @brief 插件传感器数据结构
 *
 * 与主程序 SensorData 结构体对应，插件填充此结构体返回数据。
 * 使用纯 C 类型确保 ABI 兼容性。
 */
typedef struct {
    float temperature;  ///< 温度（°C）
    float humidity;     ///< 湿度（%）
    float light;        ///< 光照（lux）
    bool  valid;        ///< 数据有效性标志
} PluginSensorData;

/**
 * @brief 传感器插件接口结构体
 *
 * 每个插件必须定义一个此结构体的常量实例，
 * 并通过 sensor_plugin_get() 导出。
 *
 * 生命周期：init() → [read() × N] → deinit()
 *
 * 热替换流程：
 *   1. 主程序调用旧插件的 deinit()
 *   2. 主程序 dlclose() 旧 .so
 *   3. 主程序 dlopen() 新 .so
 *   4. 主程序调用新插件的 init()
 *   5. 主程序继续调用新插件的 read()
 */
typedef struct {
    /**
     * @brief 插件 API 版本号
     *
     * 必须等于 SENSOR_PLUGIN_API_VERSION，否则主程序拒绝加载。
     * 用于防止主程序升级后加载不兼容的旧插件。
     */
    int api_version;

    /**
     * @brief 插件名称（只读字符串）
     *
     * 用于日志和调试，如 "sht30_bh1750_i2c"、"simulated"
     */
    const char *name;

    /**
     * @brief 插件描述（只读字符串）
     *
     * 详细说明插件功能，如 "SHT30+BH1750 via I2C bus"
     */
    const char *description;

    /**
     * @brief 初始化插件
     * @param config 配置字符串，格式由插件自定义（如 "i2c=/dev/i2c-1;sht30=0x44;bh1750=0x23"）
     * @return true 初始化成功，false 初始化失败
     *
     * 主程序在 dlopen 后调用此函数。
     * config 参数为键值对字符串，插件自行解析。
     */
    bool (*init)(const char *config);

    /**
     * @brief 反初始化插件
     *
     * 主程序在 dlclose 前调用此函数，释放插件持有的资源。
     * 插件应关闭文件描述符、释放内存等。
     */
    void (*deinit)(void);

    /**
     * @brief 读取传感器数据
     * @param data 输出参数，插件填充此结构体
     * @return true 读取成功且 data 有效，false 读取失败
     *
     * 主程序在采集循环中周期性调用此函数。
     * 插件应避免在此函数中执行耗时操作（如长 sleep），
     * 传感器测量等待应在 init() 中完成或使用非阻塞方式。
     */
    bool (*read)(PluginSensorData *data);

    /**
     * @brief 查询插件是否为模拟模式
     * @return true 当前为模拟数据模式
     *
     * 主程序根据此值调整日志级别和显示提示。
     */
    bool (*is_simulated)(void);

} SensorPlugin;

/**
 * @brief 插件入口函数类型
 *
 * 每个 .so 必须导出此签名的函数，名称固定为 "sensor_plugin_get"。
 * 主程序通过 dlsym 查找此符号获取插件接口。
 *
 * @return 指向插件 SensorPlugin 实例的只读指针
 *
 * 示例导出代码：
 * @code
 *   static const SensorPlugin plugin = { ... };
 *   extern "C" const SensorPlugin *sensor_plugin_get(void) {
 *       return &plugin;
 *   }
 * @endcode
 */
typedef const SensorPlugin *(*SensorPluginGetFunc)(void);

#endif
