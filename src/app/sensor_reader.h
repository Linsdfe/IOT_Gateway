#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include <string>

struct SensorData {
    float temperature;
    float humidity;
    float light;
    bool valid;
};

class SensorReader {
public:
    SensorReader(const std::string &i2c_dev = "/dev/i2c-1",
                 uint8_t sht30_addr = 0x44,
                 uint8_t bh1750_addr = 0x23);
    ~SensorReader();

    bool init();
    bool readSHT30(float &temp, float &humi);
    bool readBH1750(float &light);
    bool readAll(SensorData &data);

private:
    int openI2CDev(uint8_t addr);
    bool sht30SendCmd(uint8_t msb, uint8_t lsb);
    bool sht30ReadRaw(uint8_t *buf, int len);
    bool bh1750SendCmd(uint8_t cmd);
    bool bh1750ReadRaw(uint8_t *buf, int len);

    std::string i2c_dev_;
    uint8_t sht30_addr_;
    uint8_t bh1750_addr_;
    int fd_sht30_;
    int fd_bh1750_;
};

#endif
