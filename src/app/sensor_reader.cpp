#include "sensor_reader.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cstring>
#include <cstdio>
#include <errno.h>

SensorReader::SensorReader(const std::string &i2c_dev,
                           uint8_t sht30_addr,
                           uint8_t bh1750_addr)
    : i2c_dev_(i2c_dev)
    , sht30_addr_(sht30_addr)
    , bh1750_addr_(bh1750_addr)
    , fd_sht30_(-1)
    , fd_bh1750_(-1)
{
}

SensorReader::~SensorReader()
{
    if (fd_sht30_ >= 0) close(fd_sht30_);
    if (fd_bh1750_ >= 0) close(fd_bh1750_);
}

int SensorReader::openI2CDev(uint8_t addr)
{
    int fd = open(i2c_dev_.c_str(), O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[SensorReader] open %s failed: %s\n",
                i2c_dev_.c_str(), strerror(errno));
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        fprintf(stderr, "[SensorReader] set addr 0x%02x failed: %s\n",
                addr, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

bool SensorReader::init()
{
    fd_sht30_ = openI2CDev(sht30_addr_);
    if (fd_sht30_ < 0) {
        fprintf(stderr, "[SensorReader] SHT30 init failed\n");
        return false;
    }

    fd_bh1750_ = openI2CDev(bh1750_addr_);
    if (fd_bh1750_ < 0) {
        fprintf(stderr, "[SensorReader] BH1750 init failed\n");
        return false;
    }

    printf("[SensorReader] init OK (SHT30=0x%02x, BH1750=0x%02x)\n",
           sht30_addr_, bh1750_addr_);
    return true;
}

bool SensorReader::sht30SendCmd(uint8_t msb, uint8_t lsb)
{
    uint8_t cmd[2] = {msb, lsb};
    if (write(fd_sht30_, cmd, 2) != 2) {
        fprintf(stderr, "[SensorReader] SHT30 write cmd failed: %s\n",
                strerror(errno));
        return false;
    }
    return true;
}

bool SensorReader::sht30ReadRaw(uint8_t *buf, int len)
{
    if (read(fd_sht30_, buf, len) != len) {
        fprintf(stderr, "[SensorReader] SHT30 read failed: %s\n",
                strerror(errno));
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
        fprintf(stderr, "[SensorReader] BH1750 write cmd failed: %s\n",
                strerror(errno));
        return false;
    }
    return true;
}

bool SensorReader::bh1750ReadRaw(uint8_t *buf, int len)
{
    if (read(fd_bh1750_, buf, len) != len) {
        fprintf(stderr, "[SensorReader] BH1750 read failed: %s\n",
                strerror(errno));
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

bool SensorReader::readAll(SensorData &data)
{
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
    data.valid = ok;
    return ok;
}
