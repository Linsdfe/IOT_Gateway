/**
 * @file data_manager.cpp
 * @brief 数据管理器实现
 */

#include "data_manager.h"
#include "logger.h"
#include <unistd.h>

DataManager::DataManager()
    : reader_(nullptr)
    , running_(false)
    , intervalMs_(1000)
{
    latestData_ = {0.0f, 0.0f, 0.0f, false};
}

DataManager::~DataManager()
{
    stop();
}

bool DataManager::start(SensorReader *reader, int interval_ms)
{
    if (running_) return true;
    reader_ = reader;
    intervalMs_ = interval_ms;
    running_ = true;
    collectThread_ = std::thread(&DataManager::collectLoop, this);
    LOG_I("DataMgr", "started, interval=%dms", intervalMs_);
    return true;
}

void DataManager::stop()
{
    running_ = false;
    if (collectThread_.joinable())
        collectThread_.join();
    LOG_I("DataMgr", "stopped");
}

SensorData DataManager::getLatestData()
{
    /* 加锁读取，返回数据快照，避免外部直接访问内部状态 */
    std::lock_guard<std::mutex> lock(dataMutex_);
    return latestData_;
}

void DataManager::registerCallback(DataCallback cb)
{
    std::lock_guard<std::mutex> lock(cbMutex_);
    callbacks_.push_back(cb);
}

void DataManager::collectLoop()
{
    while (running_) {
        SensorData data;
        if (reader_ && reader_->readAll(data)) {
            /* 更新最新数据快照 */
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                latestData_ = data;
            }

            LOG_I("DataMgr", "T=%.1f C  H=%.1f %%  L=%.1f lux",
                   data.temperature, data.humidity, data.light);

            /* 通知所有消费者：在采集线程中同步调用回调
             * 设计考量：回调应快速返回，避免阻塞采集线程。
             * 如果消费者需要耗时处理，应在自身线程中异步执行。 */
            {
                std::lock_guard<std::mutex> lock(cbMutex_);
                for (auto &cb : callbacks_) {
                    cb(data);
                }
            }
        } else {
            LOG_W("DataMgr", "sensor read failed");
        }

        /* 分段 sleep 以便及时响应 stop() 请求，
         * 避免整个采集间隔期间无法退出 */
        for (int i = 0; i < intervalMs_ / 100 && running_; i++) {
            usleep(100000);
        }
    }
}
