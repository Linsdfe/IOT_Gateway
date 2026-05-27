/**
 * @file edge_compute.h
 * @brief 边缘计算模块 - 传感器数据本地智能处理与分析
 *
 * EdgeCompute 在数据上云之前对传感器原始数据进行本地实时处理，
 * 实现数据滤波降噪、统计分析、阈值告警、变化检测和资源监控等功能。
 *
 * 设计目标：
 *   - 减少云端带宽消耗（变化检测 + 数据聚合）
 *   - 降低云端处理延迟（边缘预处理）
 *   - 支持离线自治（本地告警和决策）
 *   - 资源受限环境友好（嵌入式 ARM 优化）
 *
 * 数据处理流水线：
 *   Raw SensorData → 滑动窗口滤波 → 统计分析 → 阈值告警
 *                  → 变化检测 → 聚合上报
 *                  → 环形缓冲存储
 *
 * 告警机制（带滞回）：
 *   - 高温告警：温度 > highTemp 触发，回落到 highTemp - hysteresis 才解除
 *   - 低温告警：温度 < lowTemp 触发，回升到 lowTemp + hysteresis 才解除
 *   - 高湿告警、低湿告警、高光照告警：类似滞回机制
 *   - 告警状态变化时通过回调通知上层
 *
 * 集成方式：
 *   1. 在 GatewaySDK::init() 中创建 EdgeCompute 实例
 *   2. 通过 DataManager::registerCallback 注册 EdgeCompute::processData
 *   3. EdgeCompute 处理后的数据通过 getLatestProcessed() 供下游使用
 */

#ifndef EDGE_COMPUTE_H
#define EDGE_COMPUTE_H

#include "sensor_reader.h"
#include <vector>
#include <deque>
#include <mutex>
#include <functional>
#include <cstdint>
#include <string>

/**
 * @brief 边缘计算处理后的传感器数据
 *
 * 相比原始 SensorData，增加了滤波值、统计信息和告警状态。
 */
struct EdgeSensorData {
    float temperature = 0.0f;       ///< 原始温度（°C）
    float humidity = 0.0f;          ///< 原始湿度（%）
    float light = 0.0f;             ///< 原始光照（lux）
    float tempFiltered = 0.0f;      ///< 滤波后温度
    float humiFiltered = 0.0f;      ///< 滤波后湿度
    float lightFiltered = 0.0f;     ///< 滤波后光照
    float tempAvg = 0.0f;           ///< 时间窗口内温度平均值
    float humiAvg = 0.0f;           ///< 时间窗口内湿度平均值
    float lightAvg = 0.0f;          ///< 时间窗口内光照平均值
    float tempMin = 0.0f;           ///< 时间窗口内温度最小值
    float tempMax = 0.0f;           ///< 时间窗口内温度最大值
    float tempStdDev = 0.0f;        ///< 时间窗口内温度标准差
    bool  dataChanged = false;      ///< 数据是否发生显著变化（变化检测）
    bool  valid = false;            ///< 数据有效性
};

/**
 * @brief 告警级别枚举
 */
enum class AlertLevel {
    NONE,      ///< 无告警
    INFO,      ///< 提示级别
    WARN,      ///< 警告级别
    CRITICAL   ///< 严重级别
};

/**
 * @brief 告警类型枚举
 */
enum class AlertType {
    HIGH_TEMP,       ///< 高温告警
    LOW_TEMP,        ///< 低温告警
    HIGH_HUMIDITY,   ///< 高湿告警
    LOW_HUMIDITY,    ///< 低湿告警
    HIGH_LIGHT,      ///< 高光照告警
    SENSOR_FAULT,    ///< 传感器故障（连续无效数据）
    RESOURCE_WARN    ///< 资源告警（CPU/内存超限）
};

/**
 * @brief 告警事件结构
 */
struct AlertEvent {
    AlertType type;              ///< 告警类型
    AlertLevel level;            ///< 告警级别
    float currentValue;          ///< 当前触发值
    float thresholdValue;        ///< 触发阈值
    uint64_t timestamp;          ///< 告警时间戳（毫秒）
    std::string message;         ///< 告警描述信息
    bool active;                 ///< 告警是否仍然活跃
};

/**
 * @brief 滑动窗口统计信息
 */
struct WindowStats {
    float min = 0.0f;      ///< 窗口内最小值
    float max = 0.0f;      ///< 窗口内最大值
    float avg = 0.0f;      ///< 窗口内平均值
    float stdDev = 0.0f;   ///< 窗口内标准差
    int sampleCount = 0;   ///< 窗口内样本数
};

/**
 * @brief 资源使用信息
 */
struct ResourceUsage {
    float cpuPercent = 0.0f;       ///< CPU 使用率（百分比）
    long memUsedKb = 0;            ///< 内存使用量（KB）
    long memTotalKb = 0;           ///< 内存总量（KB）
    float memPercent = 0.0f;       ///< 内存使用率（百分比）
};

/** @brief 已处理数据回调函数类型 */
using EdgeDataCallback = std::function<void(const EdgeSensorData &)>;

/** @brief 告警事件回调函数类型 */
using AlertCallback = std::function<void(const AlertEvent &)>;

/**
 * @brief 边缘计算引擎
 *
 * 线程安全的边缘计算模块，集成滑动窗口滤波、统计分析、
 * 阈值告警（带滞回）和资源监控功能。
 */
class EdgeCompute {
public:
    /**
     * @brief 边缘计算配置结构
     */
    struct Config {
        bool enabled = true;               ///< 是否启用边缘计算

        int filterWindowSize = 5;           ///< 滑动平均窗口大小（采样点数）
        int statsWindowSec = 60;           ///< 统计窗口时长（秒）

        float changeThresholdTemp = 0.5f;  ///< 温度变化检测阈值（°C）
        float changeThresholdHumi = 1.0f;  ///< 湿度变化检测阈值（%）
        float changeThresholdLight = 10.0f; ///< 光照变化检测阈值（lux）

        float highTempThreshold = 40.0f;   ///< 高温告警阈值（°C）
        float lowTempThreshold = 0.0f;     ///< 低温告警阈值（°C）
        float highHumiThreshold = 90.0f;   ///< 高湿告警阈值（%）
        float lowHumiThreshold = 20.0f;    ///< 低湿告警阈值（%）
        float highLightThreshold = 1000.0f; ///< 高光照告警阈值（lux）
        float alertHysteresis = 2.0f;      ///< 告警滞回值（通用，°C/%）

        int sensorFaultTimeout = 10;        ///< 传感器故障超时（连续无效采样次数）
        int ringBufferSize = 3600;         ///< 环形缓冲区大小（约1小时，假设1秒采样）

        bool enableResourceMonitor = true;  ///< 是否启用资源监控
        float cpuWarnThreshold = 80.0f;     ///< CPU 告警阈值（%）
        float memWarnThreshold = 80.0f;     ///< 内存告警阈值（%）
    };

    EdgeCompute();
    ~EdgeCompute();

    /**
     * @brief 初始化边缘计算模块
     * @param cfg 配置参数
     * @param collectIntervalMs 传感器采集间隔（毫秒），用于计算统计窗口
     * @return true 初始化成功
     */
    bool init(const Config &cfg, int collectIntervalMs = 2000);

    /**
     * @brief 处理一条原始传感器数据
     * @param raw 原始传感器数据
     * @return 处理后的边缘计算数据
     *
     * 此函数设计为在 DataManager 的回调中调用（采集线程上下文）。
     * 内部执行完整的数据处理流水线：滤波 → 统计 → 告警 → 变化检测。
     * 处理完成后将结果写入 processedData_ 供 getLatestProcessed() 读取。
     */
    EdgeSensorData processData(const SensorData &raw);

    /**
     * @brief 获取最新处理后的数据
     * @return EdgeSensorData 快照，线程安全
     */
    EdgeSensorData getLatestProcessed();

    /**
     * @brief 获取时间窗口内的统计信息
     * @param dataType 数据类型：0=温度，1=湿度，2=光照
     * @return WindowStats 统计结果
     */
    WindowStats getWindowStats(int dataType);

    /**
     * @brief 获取当前所有活跃告警
     * @return 活跃告警列表
     */
    std::vector<AlertEvent> getActiveAlerts();

    /**
     * @brief 获取环形缓冲区中最近的数据
     * @param count 请求的数据条数
     * @return 最近 count 条已处理数据
     */
    std::vector<EdgeSensorData> getRecentData(int count);

    /**
     * @brief 获取当前资源使用情况
     * @return ResourceUsage 资源使用快照
     */
    ResourceUsage getResourceUsage();

    /**
     * @brief 注册已处理数据回调
     * @param cb 回调函数，每次 processData 完成后调用
     */
    void registerDataCallback(EdgeDataCallback cb);

    /**
     * @brief 注册告警事件回调
     * @param cb 回调函数，告警状态变化时调用
     */
    void registerAlertCallback(AlertCallback cb);

    /**
     * @brief 更新配置（运行时动态调整）
     * @param cfg 新配置
     */
    void updateConfig(const Config &cfg);

    /**
     * @brief 重置所有内部状态（清空缓冲区、告警、统计）
     */
    void reset();

    /**
     * @brief 查询模块是否已初始化
     */
    bool isInitialized() const { return initialized_; }

private:
    /**
     * @brief 滑动窗口移动平均滤波
     * @param value 当前原始值
     * @param window 滑动窗口队列引用
     * @return 滤波后的值
     */
    float movingAverage(float value, std::deque<float> &window);

    /**
     * @brief 计算队列内数据的统计信息
     * @param dataQueue 数据队列
     * @return WindowStats 统计结果
     */
    WindowStats computeStats(const std::deque<float> &dataQueue);

    /**
     * @brief 变化检测：判断数据是否发生显著变化
     * @param newVal 新值
     * @param lastSent 上次发送的值
     * @param threshold 变化阈值
     * @return true 数据发生显著变化
     */
    bool isSignificantChange(float newVal, float &lastSent, float threshold);

    /**
     * @brief 检查并更新阈值告警状态
     * @param value 当前值
     * @param highThresh 高阈值
     * @param lowThresh 低阈值
     * @param hysteresis 滞回值
     * @param isHighActive 高告警当前状态（引用）
     * @param isLowActive 低告警当前状态（引用）
     * @param highType 高告警类型
     * @param lowType 低告警类型
     *
     * 带滞回的告警机制：
     * - 上升超过 highThresh → 触发高告警
     * - 下降到 highThresh - hysteresis 以下 → 解除高告警
     * - 下降低于 lowThresh → 触发低告警
     * - 回升到 lowThresh + hysteresis 以上 → 解除低告警
     */
    void checkThresholdAlert(float value, float highThresh, float lowThresh,
                             float hysteresis, bool &isHighActive, bool &isLowActive,
                             AlertType highType, AlertType lowType);

    /**
     * @brief 触发告警事件
     * @param type 告警类型
     * @param level 告警级别
     * @param currentValue 当前值
     * @param thresholdValue 阈值
     * @param active 告警是否活跃
     * @param message 告警消息格式
     */
    void fireAlert(AlertType type, AlertLevel level,
                   float currentValue, float thresholdValue,
                   bool active, const char *message);

    /**
     * @brief 读取 /proc/self/stat 获取 CPU 时间
     */
    bool readCpuStat(unsigned long &utime, unsigned long &stime);

    /**
     * @brief 读取 /proc/self/status 获取内存使用
     */
    bool readMemInfo(long &vmRssKb);

    /**
     * @brief 更新资源使用信息
     */
    void updateResourceUsage();

    Config cfg_;                          ///< 模块配置
    bool initialized_;                    ///< 是否已初始化
    int collectIntervalMs_;               ///< 采集间隔（毫秒）

    EdgeSensorData processedData_;        ///< 最新处理后的数据
    mutable std::mutex dataMutex_;        ///< processedData_ 保护锁

    std::deque<float> filterTemp_;        ///< 温度滑动滤波窗口
    std::deque<float> filterHumi_;        ///< 湿度滑动滤波窗口
    std::deque<float> filterLight_;       ///< 光照滑动滤波窗口
    std::mutex filterMutex_;              ///< 滤波窗口保护锁

    std::deque<float> statsTemp_;         ///< 温度统计窗口
    std::deque<float> statsHumi_;         ///< 湿度统计窗口
    std::deque<float> statsLight_;        ///< 光照统计窗口
    int maxStatsSamples_;                 ///< 统计窗口最大样本数

    std::deque<EdgeSensorData> ringBuffer_; ///< 环形数据缓冲区
    std::mutex ringMutex_;                ///< 环形缓冲区保护锁

    float lastSentTemp_;                  ///< 上次上报的温度值
    float lastSentHumi_;                  ///< 上次上报的湿度值
    float lastSentLight_;                 ///< 上次上报的光照值

    bool alertHighTemp_;                  ///< 高温告警状态
    bool alertLowTemp_;                   ///< 低温告警状态
    bool alertHighHumi_;                  ///< 高湿告警状态
    bool alertLowHumi_;                   ///< 低湿告警状态
    bool alertHighLight_;                 ///< 高光照告警状态
    bool alertSensorFault_;               ///< 传感器故障告警状态
    bool alertResource_;                  ///< 资源告警状态
    int consecutiveFaults_;               ///< 连续无效数据计数
    std::recursive_mutex alertMutex_;     ///< 告警状态保护锁（递归锁，fireAlert可在checkThresholdAlert内调用）

    std::vector<AlertEvent> activeAlerts_; ///< 活跃告警列表

    std::vector<EdgeDataCallback> dataCallbacks_; ///< 数据消费者回调
    std::vector<AlertCallback> alertCallbacks_;   ///< 告警消费者回调
    std::mutex cbMutex_;                          ///< 回调列表保护锁

    ResourceUsage lastResourceUsage_;     ///< 最新资源使用快照
    mutable std::mutex resourceMutex_;     ///< 资源使用保护锁
    unsigned long prevUtime_;             ///< 上次 CPU 用户态时间
    unsigned long prevStime_;             ///< 上次 CPU 内核态时间
    uint64_t prevCpuSampleTime_;          ///< 上次 CPU 采样时间戳
};

#endif