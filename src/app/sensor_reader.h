/**
 * @file sensor_reader.h
 * @brief I2C 传感器读取器 - SHT30 温湿度传感器 + BH1750 光照传感器
 *
 * 通过 Linux i2c-dev 用户空间接口与传感器通信，无需内核驱动。
 * SHT30 使用 I2C 地址 0x44，BH1750 使用 I2C 地址 0x23。
 *
 * 支持模拟数据模式：当传感器不可用时（I2C总线故障或传感器未连接），
 * 自动切换为模拟数据模式，生成周期性变化的仿真数据，使 LVGL 界面
 * 可以正常展示。每隔30次读取尝试恢复真实传感器连接。
 */

#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include <cstdint>
#include <string>

/**
 * @brief 传感器数据结构体
 *
 * 由 SensorReader::readAll() 填充，通过 DataManager 分发给各消费者。
 * valid 字段标识数据是否来自真实传感器（模拟模式下 valid=true）。
 */
struct SensorData {
    float temperature;  ///< 温度（°C），SHT30 量程 -40~125°C，精度 ±0.2°C
    float humidity;     ///< 湿度（%），SHT30 量程 0~100%，精度 ±2%
    float light;        ///< 光照（lux），BH1750 量程 1~65535 lux，精度 1 lux
    bool  valid;        ///< 数据有效性标志，true 表示数据可用
};

/**
 * @brief I2C 传感器读取器
 *
 * 封装 SHT30 和 BH1750 的 I2C 通信逻辑，提供统一的 readAll() 接口。
 *
 * 工作模式：
 * - 正常模式：通过 /dev/i2c-N 直接与传感器通信
 * - 模拟模式：传感器不可用时生成正弦波仿真数据
 *
 * 模式切换逻辑：
 * 1. init() 时传感器不可用 → 立即进入模拟模式
 * 2. 正常模式下连续3次读取失败 → 切换到模拟模式
 * 3. 模拟模式下每30次读取尝试恢复 → 成功后切回正常模式
 */
class SensorReader {
public:
    /**
     * @brief 构造传感器读取器
     * @param i2c_dev     I2C 总线设备路径，默认 "/dev/i2c-1"
     * @param sht30_addr  SHT30 I2C 地址，默认 0x44（ADDR 引脚接地）
     * @param bh1750_addr BH1750 I2C 地址，默认 0x23（ADDR 引脚接地）
     */
    SensorReader(const std::string &i2c_dev = "/dev/i2c-1",
                 uint8_t sht30_addr = 0x44,
                 uint8_t bh1750_addr = 0x23);

    ~SensorReader();

    /**
     * @brief 初始化传感器连接
     * @return true 初始化成功（包括模拟模式）
     *
     * 打开 I2C 总线并尝试连接两个传感器。
     * 即使传感器不可用也返回 true（进入模拟模式）。
     */
    bool init();

    /**
     * @brief 读取所有传感器数据
     * @param data 输出参数，填充传感器数据
     * @return true 数据有效（包括模拟数据）
     *
     * 依次读取 SHT30（温度+湿度）和 BH1750（光照）。
     * 模拟模式下生成正弦波仿真数据。
     */
    bool readAll(SensorData &data);

    /** @brief 仅读取 SHT30 温湿度数据 */
    bool readSHT30(float &temp, float &humi);

    /** @brief 仅读取 BH1750 光照数据 */
    bool readBH1750(float &light);

    /** @brief 查询当前是否为模拟数据模式 */
    bool isSimulated() const { return simulated_; }

private:
    /** @brief 打开 I2C 设备并设置从机地址，返回文件描述符 */
    int openI2CDev(uint8_t addr);

    /** @brief 向 SHT30 发送2字节命令 */
    bool sht30SendCmd(uint8_t msb, uint8_t lsb);

    /** @brief 从 SHT30 读取原始数据 */
    bool sht30ReadRaw(uint8_t *buf, int len);

    /** @brief 向 BH1750 发送1字节命令 */
    bool bh1750SendCmd(uint8_t cmd);

    /** @brief 从 BH1750 读取原始数据 */
    bool bh1750ReadRaw(uint8_t *buf, int len);

    /**
     * @brief 生成模拟传感器数据
     * @param data 输出参数，填充正弦波仿真数据
     *
     * 温度：25±5°C（周期约 4.2s）
     * 湿度：60±15%（周期约 6.3s）
     * 光照：200±150 lux（周期约 8.4s）
     */
    void generateSimulated(SensorData &data);

    /**
     * @brief 尝试恢复真实传感器连接
     * @return true 恢复成功
     *
     * 重新打开 I2C 设备并尝试读取传感器数据。
     * 成功后自动切回正常模式。
     */
    bool tryRecover();

    std::string i2cDev_;        ///< I2C 总线设备路径
    uint8_t sht30Addr_;         ///< SHT30 I2C 从机地址
    uint8_t bh1750Addr_;        ///< BH1750 I2C 从机地址
    int fd_sht30_;              ///< SHT30 文件描述符（-1 表示未打开）
    int fd_bh1750_;             ///< BH1750 文件描述符（-1 表示未打开）
    bool simulated_;            ///< 是否处于模拟数据模式
    int failCount_;             ///< 连续读取失败计数（达到阈值后切换模拟模式）
    int simStep_;               ///< 模拟数据步进计数器
    float simTemp_;             ///< 模拟温度值
    float simHumi_;             ///< 模拟湿度值
    float simLight_;            ///< 模拟光照值
};

#endif
