/**
 * @file edge_compute.cpp
 * @brief 边缘计算模块实现
 *
 * 实现数据处理流水线：
 *   1. 滑动窗口移动平均滤波（消除传感器噪声）
 *   2. 时间窗口统计分析（min/max/avg/stddev）
 *   3. 带滞回的阈值告警（防止告警风暴）
 *   4. 变化检测（减少冗余云上报）
 *   5. 传感器故障检测（连续无效数据判定）
 *   6. 资源占用监控（CPU/内存 through /proc）
 *
 * 资源监控说明：
 *   - CPU 使用率通过 /proc/self/stat 的 utime+stime 差值计算
 *   - 内存使用通过 /proc/self/status 的 VmRSS 字段获取
 *   - 直接读取 proc 文件系统，无额外依赖
 */

#include "edge_compute.h"
#include "logger.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/time.h>
#include <unistd.h>

EdgeCompute::EdgeCompute()
    : initialized_(false)
    , collectIntervalMs_(2000)
    , maxStatsSamples_(30)
    , lastSentTemp_(0.0f)
    , lastSentHumi_(0.0f)
    , lastSentLight_(0.0f)
    , alertHighTemp_(false)
    , alertLowTemp_(false)
    , alertHighHumi_(false)
    , alertLowHumi_(false)
    , alertHighLight_(false)
    , alertSensorFault_(false)
    , alertResource_(false)
    , consecutiveFaults_(0)
    , prevUtime_(0)
    , prevStime_(0)
    , prevCpuSampleTime_(0)
{
}

EdgeCompute::~EdgeCompute()
{
}

bool EdgeCompute::init(const Config &cfg, int collectIntervalMs)
{
    cfg_ = cfg;
    collectIntervalMs_ = collectIntervalMs;

    maxStatsSamples_ = (cfg_.statsWindowSec * 1000) / collectIntervalMs_;
    if (maxStatsSamples_ < 1) maxStatsSamples_ = 1;

    /* 预分配滤波窗口大小，避免运行时动态分配 */
    filterTemp_.clear();
    filterHumi_.clear();
    filterLight_.clear();

    statsTemp_.clear();
    statsHumi_.clear();
    statsLight_.clear();

    ringBuffer_.clear();

    lastSentTemp_ = 0.0f;
    lastSentHumi_ = 0.0f;
    lastSentLight_ = 0.0f;

    alertHighTemp_ = false;
    alertLowTemp_ = false;
    alertHighHumi_ = false;
    alertLowHumi_ = false;
    alertHighLight_ = false;
    alertSensorFault_ = false;
    alertResource_ = false;
    consecutiveFaults_ = 0;

    activeAlerts_.clear();

    /* 初始化 CPU 采样基准 */
    if (cfg_.enableResourceMonitor) {
        updateResourceUsage();
    }

    initialized_ = true;
    LOG_I("Edge", "init OK: filter=%d stats=%ds(%d samples) ring=%d",
          cfg_.filterWindowSize, cfg_.statsWindowSec, maxStatsSamples_,
          cfg_.ringBufferSize);
    return true;
}

float EdgeCompute::movingAverage(float value, std::deque<float> &window)
{
    window.push_back(value);

    while ((int)window.size() > cfg_.filterWindowSize) {
        window.pop_front();
    }

    if (window.empty()) return value;

    double sum = 0.0;
    for (float v : window) {
        sum += v;
    }
    return (float)(sum / window.size());
}

WindowStats EdgeCompute::computeStats(const std::deque<float> &dataQueue)
{
    WindowStats stats;

    if (dataQueue.empty()) {
        return stats;
    }

    stats.sampleCount = (int)dataQueue.size();

    stats.min = dataQueue[0];
    stats.max = dataQueue[0];
    double sum = 0.0;

    for (float v : dataQueue) {
        if (v < stats.min) stats.min = v;
        if (v > stats.max) stats.max = v;
        sum += v;
    }

    stats.avg = (float)(sum / dataQueue.size());

    /* 计算标准差 */
    double variance = 0.0;
    for (float v : dataQueue) {
        double diff = v - stats.avg;
        variance += diff * diff;
    }
    variance /= dataQueue.size();
    stats.stdDev = (float)sqrt(variance);

    return stats;
}

bool EdgeCompute::isSignificantChange(float newVal, float &lastSent,
                                       float threshold)
{
    if (fabsf(newVal - lastSent) >= threshold) {
        lastSent = newVal;
        return true;
    }
    return false;
}

void EdgeCompute::checkThresholdAlert(float value, float highThresh,
                                       float lowThresh, float hysteresis,
                                       bool &isHighActive, bool &isLowActive,
                                       AlertType highType, AlertType lowType)
{
    /* 高阈值告警（带滞回） */
    if (!isHighActive && value > highThresh) {
        isHighActive = true;
        fireAlert(highType, AlertLevel::WARN, value, highThresh,
                  true, "value %.1f exceeds high threshold %.1f");
    } else if (isHighActive && value <= highThresh - hysteresis) {
        isHighActive = false;
        fireAlert(highType, AlertLevel::NONE, value, highThresh,
                  false, "value %.1f recovered below %.1f");
    }

    /* 低阈值告警（带滞回） */
    if (!isLowActive && value < lowThresh) {
        isLowActive = true;
        fireAlert(lowType, AlertLevel::WARN, value, lowThresh,
                  true, "value %.1f below low threshold %.1f");
    } else if (isLowActive && value >= lowThresh + hysteresis) {
        isLowActive = false;
        fireAlert(lowType, AlertLevel::NONE, value, lowThresh,
                  false, "value %.1f recovered above %.1f");
    }
}

void EdgeCompute::fireAlert(AlertType type, AlertLevel level,
                              float currentValue, float thresholdValue,
                              bool active, const char *message)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t timestamp = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    AlertEvent event;
    event.type = type;
    event.level = level;
    event.currentValue = currentValue;
    event.thresholdValue = thresholdValue;
    event.timestamp = timestamp;
    event.active = active;

    char buf[256];
    const char *typeStr = "UNKNOWN";
    switch (type) {
    case AlertType::HIGH_TEMP:     typeStr = "HIGH_TEMP"; break;
    case AlertType::LOW_TEMP:      typeStr = "LOW_TEMP"; break;
    case AlertType::HIGH_HUMIDITY: typeStr = "HIGH_HUMIDITY"; break;
    case AlertType::LOW_HUMIDITY:  typeStr = "LOW_HUMIDITY"; break;
    case AlertType::HIGH_LIGHT:    typeStr = "HIGH_LIGHT"; break;
    case AlertType::SENSOR_FAULT:  typeStr = "SENSOR_FAULT"; break;
    case AlertType::RESOURCE_WARN: typeStr = "RESOURCE_WARN"; break;
    }
    snprintf(buf, sizeof(buf), message, currentValue, thresholdValue);
    event.message = std::string(typeStr) + ": " + buf;

    {
        std::lock_guard<std::recursive_mutex> lock(alertMutex_);

        bool found = false;
        for (auto it = activeAlerts_.begin(); it != activeAlerts_.end(); ) {
            if (it->type == type) {
                found = true;
                if (!active) {
                    it = activeAlerts_.erase(it);
                    continue;
                }
                it->level = level;
                it->currentValue = currentValue;
                it->timestamp = timestamp;
                it->active = active;
                it->message = event.message;
            }
            ++it;
        }

        if (active && !found) {
            activeAlerts_.push_back(event);
        }
    }

    const char *action = active ? "TRIGGERED" : "CLEARED";
    if (level >= AlertLevel::WARN || !active) {
        LOG_W("Edge", "[ALERT %s] %s: %s", action, typeStr, event.message.c_str());
    } else {
        LOG_I("Edge", "[ALERT %s] %s: %s", action, typeStr, event.message.c_str());
    }

    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        for (auto &cb : alertCallbacks_) {
            cb(event);
        }
    }
}

EdgeSensorData EdgeCompute::processData(const SensorData &raw)
{
    EdgeSensorData result;

    result.temperature = raw.temperature;
    result.humidity = raw.humidity;
    result.light = raw.light;
    result.valid = raw.valid;

    if (!raw.valid) {
        consecutiveFaults_++;
        if (consecutiveFaults_ >= cfg_.sensorFaultTimeout && !alertSensorFault_) {
            alertSensorFault_ = true;
            fireAlert(AlertType::SENSOR_FAULT, AlertLevel::CRITICAL,
                  (float)consecutiveFaults_, (float)cfg_.sensorFaultTimeout,
                  true, "sensor fault: %.0f consecutive invalid readings");
        }
        processedData_ = result;
        return result;
    }

    if (alertSensorFault_) {
        alertSensorFault_ = false;
        consecutiveFaults_ = 0;
        fireAlert(AlertType::SENSOR_FAULT, AlertLevel::NONE, 0,
                  (float)cfg_.sensorFaultTimeout, false,
                  "sensor recovered");
    }

    /* 步骤1：滑动窗口移动平均滤波 */
    {
        std::lock_guard<std::mutex> lock(filterMutex_);
        result.tempFiltered = movingAverage(raw.temperature, filterTemp_);
        result.humiFiltered = movingAverage(raw.humidity, filterHumi_);
        result.lightFiltered = movingAverage(raw.light, filterLight_);
    }

    /* 步骤2：维护统计窗口 */
    {
        statsTemp_.push_back(raw.temperature);
        statsHumi_.push_back(raw.humidity);
        statsLight_.push_back(raw.light);

        while ((int)statsTemp_.size() > maxStatsSamples_) statsTemp_.pop_front();
        while ((int)statsHumi_.size() > maxStatsSamples_) statsHumi_.pop_front();
        while ((int)statsLight_.size() > maxStatsSamples_) statsLight_.pop_front();
    }

    /* 步骤3：计算统计信息 */
    WindowStats tempStats = computeStats(statsTemp_);
    WindowStats humiStats = computeStats(statsHumi_);
    WindowStats lightStats = computeStats(statsLight_);

    result.tempAvg = tempStats.avg;
    result.humiAvg = humiStats.avg;
    result.lightAvg = lightStats.avg;
    result.tempMin = tempStats.min;
    result.tempMax = tempStats.max;
    result.tempStdDev = tempStats.stdDev;

    /* 步骤4：变化检测（使用滤波后的值判断） */
    bool tempChanged = isSignificantChange(result.tempFiltered,
                                            lastSentTemp_,
                                            cfg_.changeThresholdTemp);
    bool humiChanged = isSignificantChange(result.humiFiltered,
                                            lastSentHumi_,
                                            cfg_.changeThresholdHumi);
    bool lightChanged = isSignificantChange(result.lightFiltered,
                                             lastSentLight_,
                                             cfg_.changeThresholdLight);
    result.dataChanged = tempChanged || humiChanged || lightChanged;

    /* 步骤5：阈值告警检测 */
    {
        std::lock_guard<std::recursive_mutex> lock(alertMutex_);
        checkThresholdAlert(raw.temperature,
                            cfg_.highTempThreshold, cfg_.lowTempThreshold,
                            cfg_.alertHysteresis,
                            alertHighTemp_, alertLowTemp_,
                            AlertType::HIGH_TEMP, AlertType::LOW_TEMP);

        checkThresholdAlert(raw.humidity,
                            cfg_.highHumiThreshold, cfg_.lowHumiThreshold,
                            cfg_.alertHysteresis,
                            alertHighHumi_, alertLowHumi_,
                            AlertType::HIGH_HUMIDITY, AlertType::LOW_HUMIDITY);

        if (!alertHighLight_ && raw.light > cfg_.highLightThreshold) {
            alertHighLight_ = true;
            fireAlert(AlertType::HIGH_LIGHT, AlertLevel::INFO,
                      raw.light, cfg_.highLightThreshold, true,
                      "light %.1f lux exceeds threshold %.1f lux");
        } else if (alertHighLight_ && raw.light <= cfg_.highLightThreshold - cfg_.alertHysteresis) {
            alertHighLight_ = false;
            fireAlert(AlertType::HIGH_LIGHT, AlertLevel::NONE,
                      raw.light, cfg_.highLightThreshold, false,
                      "light %.1f lux recovered below %.1f lux");
        }
    }

    /* 步骤6：资源监控 */
    if (cfg_.enableResourceMonitor) {
        updateResourceUsage();
        if (!alertResource_ &&
            (lastResourceUsage_.cpuPercent > cfg_.cpuWarnThreshold ||
             lastResourceUsage_.memPercent > cfg_.memWarnThreshold)) {
            alertResource_ = true;
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "CPU=%.1f%% MEM=%.1f%%",
                     lastResourceUsage_.cpuPercent,
                     lastResourceUsage_.memPercent);
            fireAlert(AlertType::RESOURCE_WARN, AlertLevel::WARN,
                      lastResourceUsage_.cpuPercent,
                      cfg_.cpuWarnThreshold, true, msg);
        } else if (alertResource_ &&
                   lastResourceUsage_.cpuPercent <= cfg_.cpuWarnThreshold - 10.0f &&
                   lastResourceUsage_.memPercent <= cfg_.memWarnThreshold - 10.0f) {
            alertResource_ = false;
            fireAlert(AlertType::RESOURCE_WARN, AlertLevel::NONE,
                      lastResourceUsage_.cpuPercent,
                      cfg_.cpuWarnThreshold, false, "resource usage back to normal");
        }
    }

    /* 步骤7：写入环形缓冲区 */
    {
        std::lock_guard<std::mutex> lock(ringMutex_);
        ringBuffer_.push_back(result);
        while ((int)ringBuffer_.size() > cfg_.ringBufferSize) {
            ringBuffer_.pop_front();
        }
    }

    /* 步骤8：更新最新数据快照 */
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        processedData_ = result;
    }

    /* 步骤9：通知数据回调消费者 */
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        for (auto &cb : dataCallbacks_) {
            cb(result);
        }
    }

    return result;
}

EdgeSensorData EdgeCompute::getLatestProcessed()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return processedData_;
}

WindowStats EdgeCompute::getWindowStats(int dataType)
{
    switch (dataType) {
    case 0: return computeStats(statsTemp_);
    case 1: return computeStats(statsHumi_);
    case 2: return computeStats(statsLight_);
    default: return WindowStats();
    }
}

std::vector<AlertEvent> EdgeCompute::getActiveAlerts()
{
    std::lock_guard<std::recursive_mutex> lock(alertMutex_);
    return activeAlerts_;
}

std::vector<EdgeSensorData> EdgeCompute::getRecentData(int count)
{
    std::lock_guard<std::mutex> lock(ringMutex_);

    std::vector<EdgeSensorData> result;
    if (ringBuffer_.empty()) return result;

    int start = (int)ringBuffer_.size() - count;
    if (start < 0) start = 0;

    for (int i = start; i < (int)ringBuffer_.size(); i++) {
        result.push_back(ringBuffer_[i]);
    }

    return result;
}

ResourceUsage EdgeCompute::getResourceUsage()
{
    std::lock_guard<std::mutex> lock(resourceMutex_);
    return lastResourceUsage_;
}

void EdgeCompute::registerDataCallback(EdgeDataCallback cb)
{
    std::lock_guard<std::mutex> lock(cbMutex_);
    dataCallbacks_.push_back(cb);
}

void EdgeCompute::registerAlertCallback(AlertCallback cb)
{
    std::lock_guard<std::mutex> lock(cbMutex_);
    alertCallbacks_.push_back(cb);
}

void EdgeCompute::updateConfig(const Config &cfg)
{
    cfg_ = cfg;
    maxStatsSamples_ = (cfg_.statsWindowSec * 1000) / collectIntervalMs_;
    if (maxStatsSamples_ < 1) maxStatsSamples_ = 1;

    /* 如果滤波窗口变小，截断现有数据 */
    {
        std::lock_guard<std::mutex> lock(filterMutex_);
        while ((int)filterTemp_.size() > cfg_.filterWindowSize) filterTemp_.pop_front();
        while ((int)filterHumi_.size() > cfg_.filterWindowSize) filterHumi_.pop_front();
        while ((int)filterLight_.size() > cfg_.filterWindowSize) filterLight_.pop_front();
    }

    LOG_I("Edge", "config updated: filter=%d stats=%ds",
          cfg_.filterWindowSize, cfg_.statsWindowSec);
}

void EdgeCompute::reset()
{
    std::lock_guard<std::mutex> lock1(dataMutex_);
    std::lock_guard<std::mutex> lock2(filterMutex_);
    std::lock_guard<std::mutex> lock3(ringMutex_);
    std::lock_guard<std::recursive_mutex> lock4(alertMutex_);

    processedData_ = EdgeSensorData();

    filterTemp_.clear();
    filterHumi_.clear();
    filterLight_.clear();

    statsTemp_.clear();
    statsHumi_.clear();
    statsLight_.clear();

    ringBuffer_.clear();

    lastSentTemp_ = 0.0f;
    lastSentHumi_ = 0.0f;
    lastSentLight_ = 0.0f;

    alertHighTemp_ = false;
    alertLowTemp_ = false;
    alertHighHumi_ = false;
    alertLowHumi_ = false;
    alertHighLight_ = false;
    alertSensorFault_ = false;
    alertResource_ = false;
    consecutiveFaults_ = 0;

    activeAlerts_.clear();

    LOG_I("Edge", "reset complete");
}

bool EdgeCompute::readCpuStat(unsigned long &utime, unsigned long &stime)
{
    std::ifstream statFile("/proc/self/stat");
    if (!statFile.is_open()) {
        return false;
    }

    std::string line;
    std::getline(statFile, line);
    statFile.close();

    std::istringstream iss(line);
    std::string token;
    for (int i = 0; i < 13; i++) {
        iss >> token;
    }

    iss >> utime >> stime;

    return !iss.fail();
}

bool EdgeCompute::readMemInfo(long &vmRssKb)
{
    std::ifstream statusFile("/proc/self/status");
    if (!statusFile.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(statusFile, line)) {
        if (line.find("VmRSS:") == 0) {
            std::istringstream iss(line);
            std::string label;
            long value;
            iss >> label >> value;
            vmRssKb = value;
            statusFile.close();
            return true;
        }
    }

    statusFile.close();
    return false;
}

void EdgeCompute::updateResourceUsage()
{
    unsigned long utime = 0, stime = 0;
    long vmRssKb = 0;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t now = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    std::lock_guard<std::mutex> lock(resourceMutex_);

    if (readMemInfo(vmRssKb)) {
        lastResourceUsage_.memUsedKb = vmRssKb;
    }

    static long s_cachedMemTotal = 0;
    if (s_cachedMemTotal == 0) {
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo.is_open()) {
            std::string line;
            while (std::getline(meminfo, line)) {
                if (line.find("MemTotal:") == 0) {
                    std::istringstream iss(line);
                    std::string label;
                    long value;
                    iss >> label >> value;
                    s_cachedMemTotal = value;
                    break;
                }
            }
            meminfo.close();
        }
    }
    lastResourceUsage_.memTotalKb = s_cachedMemTotal;

    if (s_cachedMemTotal > 0) {
        lastResourceUsage_.memPercent =
            (float)lastResourceUsage_.memUsedKb * 100.0f / (float)s_cachedMemTotal;
    }

    if (readCpuStat(utime, stime)) {
        if (prevCpuSampleTime_ > 0 && now > prevCpuSampleTime_) {
            unsigned long elapsed = now - prevCpuSampleTime_;
            unsigned long cpuDelta = (utime - prevUtime_) + (stime - prevStime_);

            /* cpuDelta 单位为 jiffies（通常 100Hz），转换为百分比 */
            long clkTck = sysconf(_SC_CLK_TCK);
            if (clkTck > 0 && elapsed > 0) {
                float cpuPercent = (float)cpuDelta * 100.0f /
                                   ((float)elapsed * (float)clkTck / 1000.0f);
                if (cpuPercent > 100.0f) cpuPercent = 100.0f;
                if (cpuPercent < 0.0f) cpuPercent = 0.0f;
                lastResourceUsage_.cpuPercent = cpuPercent;
            }
        }

        prevUtime_ = utime;
        prevStime_ = stime;
        prevCpuSampleTime_ = now;
    }
}