/**
 * @file data_manager.h
 * @brief 数据管理器 - 多线程传感器数据采集与分发
 *
 * DataManager 运行独立的数据采集线程，定时从 SensorReader 读取数据，
 * 并通过回调机制将数据分发给所有注册的消费者（如 MQTT 发布器、显示屏）。
 *
 * 架构模式：生产者-消费者模型
 * - 生产者：collectLoop 线程定时采集传感器数据
 * - 消费者：通过 registerCallback 注册的回调函数
 *
 * 线程安全：
 * - 数据读写通过 dataMutex_ 保护
 * - 回调列表通过 cbMutex_ 保护
 */

#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "sensor_reader.h"
#include <vector>
#include <functional>
#include <thread>
#include <mutex>

/** @brief 传感器数据回调函数类型，接收 SensorData 常量引用 */
using DataCallback = std::function<void(const SensorData &)>;

/**
 * @brief 数据管理器
 *
 * 负责定时采集传感器数据并分发给所有消费者。
 * 采集间隔可配置，支持动态注册回调。
 */
class DataManager {
public:
    DataManager();
    ~DataManager();

    /**
     * @brief 启动数据采集线程
     * @param reader     传感器读取器指针（不获取所有权）
     * @param interval_ms 采集间隔，单位毫秒
     * @return true 启动成功
     */
    bool start(SensorReader *reader, int interval_ms);

    /** @brief 停止数据采集线程并等待退出 */
    void stop();

    /**
     * @brief 获取最新一次采集的传感器数据
     * @return SensorData 快照，线程安全
     */
    SensorData getLatestData();

    /**
     * @brief 注册数据回调函数
     * @param cb 回调函数，每次采集成功后调用
     *
     * 回调在采集线程中执行，应避免耗时操作。
     */
    void registerCallback(DataCallback cb);

private:
    /** @brief 采集线程主循环，定时读取传感器并触发回调 */
    void collectLoop();

    SensorReader *reader_;              ///< 传感器读取器（外部拥有）
    volatile bool running_;             ///< 线程运行标志
    int intervalMs_;                    ///< 采集间隔（毫秒）
    SensorData latestData_;             ///< 最新采集数据
    std::mutex dataMutex_;              ///< latestData_ 读写保护
    std::vector<DataCallback> callbacks_; ///< 数据消费者回调列表
    std::mutex cbMutex_;                ///< callbacks_ 读写保护
    std::thread collectThread_;         ///< 采集线程
};

#endif
