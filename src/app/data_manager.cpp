#include "data_manager.h"
#include <cstdio>
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
    printf("[DataManager] started, interval=%dms\n", intervalMs_);
    return true;
}

void DataManager::stop()
{
    running_ = false;
    if (collectThread_.joinable())
        collectThread_.join();
    printf("[DataManager] stopped\n");
}

SensorData DataManager::getLatestData()
{
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
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                latestData_ = data;
            }

            printf("[DataManager] T=%.1f°C H=%.1f%% L=%.1flux\n",
                   data.temperature, data.humidity, data.light);

            {
                std::lock_guard<std::mutex> lock(cbMutex_);
                for (auto &cb : callbacks_) {
                    cb(data);
                }
            }
        } else {
            fprintf(stderr, "[DataManager] sensor read failed\n");
        }

        for (int i = 0; i < intervalMs_ / 100 && running_; i++) {
            usleep(100000);
        }
    }
}
