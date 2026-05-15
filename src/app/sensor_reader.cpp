/**
 * @file sensor_reader.cpp
 * @brief I2C 传感器读取器实现
 *
 * SHT30 通信流程：
 *   1. 发送命令码 0x2C06（单次测量，高重复性）
 *   2. 等待 20ms 测量完成
 *   3. 读取 6 字节：[温度MSB][温度LSB][CRC][湿度MSB][湿度LSB][CRC]
 *   4. 转换公式：温度 = -45 + 175 × raw / 65535
 *              湿度 = 100 × raw / 65535
 *
 * BH1750 通信流程：
 *   1. 发送命令码 0x10（连续高分辨率模式，1 lux 精度）
 *   2. 等待 180ms 测量完成
 *   3. 读取 2 字节：[MSB][LSB]
 *   4. 转换公式：光照 = raw / 1.2
 *
 * 模拟数据模式：
 *   当传感器连续3次读取失败后，自动切换到模拟数据模式。
 *   模拟数据以正弦波形式周期性变化，模拟真实传感器波动。
 *   每隔30次读取尝试恢复真实传感器连接。
 */

#include "sensor_reader.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <errno.h>

static const int SIMULATE_THRESHOLD = 3;
static const int RECOVER_INTERVAL = 30;

SensorReader::SensorReader(const std::string &i2c_dev,
                           uint8_t sht30_addr,
                           uint8_t bh1750_addr)
    : i2cDev_(i2c_dev)
    , sht30Addr_(sht30_addr)
    , bh1750Addr_(bh1750_addr)
    , fd_sht30_(-1)
    , fd_bh1750_(-1)
    , simulated_(false)
    , failCount_(0)
    , simStep_(0)
    , simTemp_(25.0f)
    , simHumi_(60.0f)
    , simLight_(100.0f)
{
}

SensorReader::~SensorReader()
{
    if (fd_sht30_ >= 0) close(fd_sht30_);
    if (fd_bh1750_ >= 0) close(fd_bh1750_);
}

int SensorReader::openI2CDev(uint8_t addr)
{
    int fd = open(i2cDev_.c_str(), O_RDWR);
    if (fd < 0) {
        LOG_E("Sensor", "open %s failed: %s", i2cDev_.c_str(), strerror(errno));
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        LOG_E("Sensor", "set addr 0x%02x failed: %s", addr, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

bool SensorReader::init()
{
    fd_sht30_ = openI2CDev(sht30Addr_);
    if (fd_sht30_ < 0) {
        LOG_W("Sensor", "SHT30 not available, will use simulated data");
        simulated_ = true;
    }

    fd_bh1750_ = openI2CDev(bh1750Addr_);
    if (fd_bh1750_ < 0) {
        LOG_W("Sensor", "BH1750 not available, will use simulated data");
        simulated_ = true;
    }

    if (simulated_) {
        LOG_I("Sensor", "init OK (SIMULATED MODE - sensors not detected on %s)",
              i2cDev_.c_str());
    } else {
        LOG_I("Sensor", "init OK (SHT30=0x%02x, BH1750=0x%02x)",
              sht30Addr_, bh1750Addr_);
    }
    return true;
}

bool SensorReader::sht30SendCmd(uint8_t msb, uint8_t lsb)
{
    uint8_t cmd[2] = {msb, lsb};
    if (write(fd_sht30_, cmd, 2) != 2) {
        LOG_E("Sensor", "SHT30 write cmd failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool SensorReader::sht30ReadRaw(uint8_t *buf, int len)
{
    if (read(fd_sht30_, buf, len) != len) {
        LOG_E("Sensor", "SHT30 read failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool SensorReader::readSHT30(float &temp, float &humi)
{
    if (!sht30SendCmd(0x2C, 0x06))
        return false;

    usleep(20000);

    uint8_t buf[6];
    if (!sht30ReadRaw(buf, 6))
        return false;

    uint16_t raw_temp = (buf[0] << 8) | buf[1];
    uint16_t raw_humi = (buf[3] << 8) | buf[4];

    temp = -45.0f + 175.0f * raw_temp / 65535.0f;
    humi = 100.0f * raw_humi / 65535.0f;

    return true;
}

bool SensorReader::bh1750SendCmd(uint8_t cmd)
{
    if (write(fd_bh1750_, &cmd, 1) != 1) {
        LOG_E("Sensor", "BH1750 write cmd failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool SensorReader::bh1750ReadRaw(uint8_t *buf, int len)
{
    if (read(fd_bh1750_, buf, len) != len) {
        LOG_E("Sensor", "BH1750 read failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool SensorReader::readBH1750(float &light)
{
    if (!bh1750SendCmd(0x10))
        return false;

    usleep(180000);

    uint8_t buf[2];
    if (!bh1750ReadRaw(buf, 2))
        return false;

    uint16_t raw = (buf[0] << 8) | buf[1];
    light = raw / 1.2f;

    return true;
}

void SensorReader::generateSimulated(SensorData &data)
{
    simStep_++;
    float t = simStep_ * 0.05f;

    simTemp_ = 25.0f + 5.0f * sinf(t * 0.3f);
    simHumi_ = 60.0f + 15.0f * sinf(t * 0.2f + 1.0f);
    simLight_ = 200.0f + 150.0f * sinf(t * 0.15f + 2.0f);

    if (simHumi_ < 0) simHumi_ = 0;
    if (simHumi_ > 100) simHumi_ = 100;
    if (simLight_ < 0) simLight_ = 0;

    data.temperature = simTemp_;
    data.humidity = simHumi_;
    data.light = simLight_;
    data.valid = true;
}

bool SensorReader::tryRecover()
{
    if (fd_sht30_ >= 0) {
        close(fd_sht30_);
        fd_sht30_ = -1;
    }
    if (fd_bh1750_ >= 0) {
        close(fd_bh1750_);
        fd_bh1750_ = -1;
    }

    fd_sht30_ = openI2CDev(sht30Addr_);
    fd_bh1750_ = openI2CDev(bh1750Addr_);

    if (fd_sht30_ >= 0 && fd_bh1750_ >= 0) {
        float temp, humi, light;
        if (readSHT30(temp, humi) && readBH1750(light)) {
            simulated_ = false;
            failCount_ = 0;
            LOG_I("Sensor", "recovered from simulated mode - real sensors active");
            return true;
        }
    }

    if (fd_sht30_ >= 0) { close(fd_sht30_); fd_sht30_ = -1; }
    if (fd_bh1750_ >= 0) { close(fd_bh1750_); fd_bh1750_ = -1; }
    return false;
}

bool SensorReader::readAll(SensorData &data)
{
    if (simulated_) {
        generateSimulated(data);
        if (simStep_ % RECOVER_INTERVAL == 0) {
            tryRecover();
        }
        return true;
    }

    bool ok = true;
    if (!readSHT30(data.temperature, data.humidity)) {
        data.temperature = 0.0f;
        data.humidity = 0.0f;
        ok = false;
    }
    if (!readBH1750(data.light)) {
        data.light = 0.0f;
        ok = false;
    }

    if (!ok) {
        failCount_++;
        if (failCount_ >= SIMULATE_THRESHOLD) {
            simulated_ = true;
            LOG_W("Sensor", "switched to SIMULATED MODE after %d failures", failCount_);
            generateSimulated(data);
            return true;
        }
    } else {
        failCount_ = 0;
    }

    data.valid = ok;
    return ok;
}
