#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "sensor_reader.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <vector>

using DataCallback = std::function<void(const SensorData &)>;

class DataManager {
public:
    DataManager();
    ~DataManager();

    bool start(SensorReader *reader, int interval_ms = 1000);
    void stop();

    SensorData getLatestData();
    void registerCallback(DataCallback cb);

private:
    void collectLoop();

    SensorReader *reader_;
    std::thread collectThread_;
    std::mutex dataMutex_;
    std::atomic<bool> running_;
    SensorData latestData_;
    int intervalMs_;

    std::vector<DataCallback> callbacks_;
    std::mutex cbMutex_;
};

#endif
