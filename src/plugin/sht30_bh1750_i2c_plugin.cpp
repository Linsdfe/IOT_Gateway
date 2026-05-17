/**
 * @file sht30_bh1750_i2c_plugin.cpp
 * @brief 插件1: sht30_bh1750_i2c - 自定义I2C通信协议（无驱动模式）
 *
 * 完全在用户空间实现I2C通信协议，不依赖任何内核驱动模块。
 * 直接通过 /dev/i2c-N 设备文件操作I2C总线，包含：
 *   - 设备探测（open + ioctl I2C_SLAVE）
 *   - 数据读写（write 发送命令，read 读取原始数据）
 *   - CRC校验（SHT30 数据完整性校验）
 *   - 错误处理（连续失败自动降级模拟，定期尝试恢复）
 *
 * 使用方式：
 *   sudo ./iot_gateway --plugin sht30_bh1750_i2c
 *
 * 配置字符串（可选）：
 *   "i2c=/dev/i2c-1;sht30=0x44;bh1750=0x23"
 */

#include "sensor_plugin.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <string>
#include <dirent.h>

static const int SIMULATE_THRESHOLD = 3;
static const int RECOVER_INTERVAL = 30;

static struct {
    std::string i2cDev;
    int i2cBus;
    uint8_t sht30Addr;
    uint8_t bh1750Addr;
    int fdSht30;
    int fdBh1750;
    bool simulated;
    int failCount;
    int simStep;
    float simTemp;
    float simHumi;
    float simLight;
    bool devicesDeleted;
} g_state;

static void parseConfig(const char *config)
{
    g_state.i2cDev = "/dev/i2c-1";
    g_state.i2cBus = 1;
    g_state.sht30Addr = 0x44;
    g_state.bh1750Addr = 0x23;

    if (!config || config[0] == '\0') return;

    std::string cfg(config);
    size_t pos = 0;

    while (pos < cfg.size()) {
        size_t sep = cfg.find('=', pos);
        if (sep == std::string::npos) break;
        std::string key = cfg.substr(pos, sep - pos);
        size_t end = cfg.find(';', sep + 1);
        if (end == std::string::npos) end = cfg.size();
        std::string val = cfg.substr(sep + 1, end - sep - 1);

        if (key == "i2c") {
            g_state.i2cDev = val;
            if (val.find("/dev/i2c-") == 0)
                g_state.i2cBus = atoi(val.c_str() + 9);
        }
        else if (key == "sht30") g_state.sht30Addr = (uint8_t)strtol(val.c_str(), nullptr, 0);
        else if (key == "bh1750") g_state.bh1750Addr = (uint8_t)strtol(val.c_str(), nullptr, 0);

        pos = end + 1;
    }
}

static int openI2CDev(uint8_t addr)
{
    int fd = open(g_state.i2cDev.c_str(), O_RDWR);
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE, addr) < 0) { close(fd); return -1; }
    return fd;
}

static bool i2cDeviceExists(int bus, uint8_t addr)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/bus/i2c/devices/%d-%04x", bus, addr);
    struct stat st;
    return stat(path, &st) == 0;
}

static bool unbindDriver(int bus, uint8_t addr)
{
    char i2cDevName[32];
    snprintf(i2cDevName, sizeof(i2cDevName), "%d-%04x", bus, addr);

    char driverLink[128];
    snprintf(driverLink, sizeof(driverLink), "/sys/bus/i2c/devices/%s/driver", i2cDevName);

    char driverPath[256];
    ssize_t len = readlink(driverLink, driverPath, sizeof(driverPath) - 1);
    if (len <= 0) return true;

    driverPath[len] = '\0';
    const char *driverName = strrchr(driverPath, '/');
    if (!driverName) return false;
    driverName++;

    char unbindPath[256];
    snprintf(unbindPath, sizeof(unbindPath), "/sys/bus/i2c/drivers/%s/unbind", driverName);

    FILE *fp = fopen(unbindPath, "w");
    if (!fp) return false;
    bool ok = (fwrite(i2cDevName, 1, strlen(i2cDevName), fp) > 0);
    fclose(fp);
    return ok;
}

static bool deleteI2CDevice(int bus, uint8_t addr)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/bus/i2c/devices/i2c-%d/delete_device", bus);
    FILE *fp = fopen(path, "w");
    if (!fp) return false;
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "0x%02x", addr);
    bool ok = (fwrite(cmd, 1, strlen(cmd), fp) > 0);
    fclose(fp);
    return ok;
}

static bool releaseI2CDevice(int bus, uint8_t addr)
{
    if (!i2cDeviceExists(bus, addr)) return true;
    unbindDriver(bus, addr);
    usleep(50000);
    deleteI2CDevice(bus, addr);
    usleep(50000);
    return !i2cDeviceExists(bus, addr);
}

static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc <<= 1;
        }
    }
    return crc;
}

static bool sht30SendCmd(uint8_t msb, uint8_t lsb)
{
    uint8_t cmd[2] = {msb, lsb};
    return write(g_state.fdSht30, cmd, 2) == 2;
}

static bool sht30ReadRaw(uint8_t *buf, int len)
{
    return read(g_state.fdSht30, buf, len) == len;
}

static bool readSHT30(float &temp, float &humi)
{
    if (!sht30SendCmd(0x2C, 0x06)) return false;
    usleep(20000);

    uint8_t buf[6];
    if (!sht30ReadRaw(buf, 6)) return false;

    if (crc8(buf, 2) != buf[2] || crc8(buf + 3, 2) != buf[5]) return false;

    uint16_t rawTemp = (buf[0] << 8) | buf[1];
    uint16_t rawHumi = (buf[3] << 8) | buf[4];

    temp = -45.0f + 175.0f * rawTemp / 65535.0f;
    humi = 100.0f * rawHumi / 65535.0f;
    return true;
}

static bool bh1750SendCmd(uint8_t cmd)
{
    return write(g_state.fdBh1750, &cmd, 1) == 1;
}

static bool bh1750ReadRaw(uint8_t *buf, int len)
{
    return read(g_state.fdBh1750, buf, len) == len;
}

static bool readBH1750(float &light)
{
    if (!bh1750SendCmd(0x20)) return false;
    usleep(180000);

    uint8_t buf[2];
    if (!bh1750ReadRaw(buf, 2)) return false;

    uint16_t raw = (buf[0] << 8) | buf[1];
    light = raw / 1.2f;
    return true;
}

static void generateSimulated(PluginSensorData *data)
{
    g_state.simStep++;
    float t = g_state.simStep * 0.05f;
    g_state.simTemp = 25.0f + 5.0f * sinf(t * 0.3f);
    g_state.simHumi = 60.0f + 15.0f * sinf(t * 0.2f + 1.0f);
    g_state.simLight = 200.0f + 150.0f * sinf(t * 0.15f + 2.0f);
    if (g_state.simHumi < 0) g_state.simHumi = 0;
    if (g_state.simHumi > 100) g_state.simHumi = 100;
    if (g_state.simLight < 0) g_state.simLight = 0;
    data->temperature = g_state.simTemp;
    data->humidity = g_state.simHumi;
    data->light = g_state.simLight;
    data->valid = true;
}

static bool tryRecover()
{
    if (g_state.fdSht30 >= 0) { close(g_state.fdSht30); g_state.fdSht30 = -1; }
    if (g_state.fdBh1750 >= 0) { close(g_state.fdBh1750); g_state.fdBh1750 = -1; }
    g_state.fdSht30 = openI2CDev(g_state.sht30Addr);
    g_state.fdBh1750 = openI2CDev(g_state.bh1750Addr);
    if (g_state.fdSht30 >= 0 && g_state.fdBh1750 >= 0) {
        float temp, humi, light;
        if (readSHT30(temp, humi) && readBH1750(light)) {
            g_state.simulated = false;
            g_state.failCount = 0;
            return true;
        }
    }
    if (g_state.fdSht30 >= 0) { close(g_state.fdSht30); g_state.fdSht30 = -1; }
    if (g_state.fdBh1750 >= 0) { close(g_state.fdBh1750); g_state.fdBh1750 = -1; }
    return false;
}

static bool pluginInit(const char *config)
{
    g_state.i2cDev.clear();
    g_state.i2cBus = 1;
    g_state.sht30Addr = 0x44;
    g_state.bh1750Addr = 0x23;
    g_state.fdSht30 = -1;
    g_state.fdBh1750 = -1;
    g_state.simulated = false;
    g_state.failCount = 0;
    g_state.simStep = 0;
    g_state.simTemp = 25.0f;
    g_state.simHumi = 60.0f;
    g_state.simLight = 100.0f;
    g_state.devicesDeleted = false;
    parseConfig(config);

    fprintf(stderr, "[i2c_plugin] init: bus=%d dev=%s sht30=0x%02x bh1750=0x%02x\n",
            g_state.i2cBus, g_state.i2cDev.c_str(), g_state.sht30Addr, g_state.bh1750Addr);

    if (i2cDeviceExists(g_state.i2cBus, g_state.sht30Addr) ||
        i2cDeviceExists(g_state.i2cBus, g_state.bh1750Addr)) {
        fprintf(stderr, "[i2c_plugin] kernel driver occupies I2C devices, releasing...\n");
        bool r1 = releaseI2CDevice(g_state.i2cBus, g_state.sht30Addr);
        bool r2 = releaseI2CDevice(g_state.i2cBus, g_state.bh1750Addr);
        if (r1 && r2) {
            g_state.devicesDeleted = true;
            fprintf(stderr, "[i2c_plugin] I2C devices released successfully\n");
        } else {
            fprintf(stderr, "[i2c_plugin] WARNING: failed to release some I2C devices\n");
        }
    }

    g_state.fdSht30 = openI2CDev(g_state.sht30Addr);
    if (g_state.fdSht30 < 0) {
        fprintf(stderr, "[i2c_plugin] WARNING: cannot open I2C for SHT30 at 0x%02x\n", g_state.sht30Addr);
        g_state.simulated = true;
    }
    g_state.fdBh1750 = openI2CDev(g_state.bh1750Addr);
    if (g_state.fdBh1750 < 0) {
        fprintf(stderr, "[i2c_plugin] WARNING: cannot open I2C for BH1750 at 0x%02x\n", g_state.bh1750Addr);
        g_state.simulated = true;
    }

    if (!g_state.simulated) {
        float temp, humi, light;
        if (!readSHT30(temp, humi) || !readBH1750(light)) {
            fprintf(stderr, "[i2c_plugin] WARNING: I2C communication failed, falling back to SIMULATED mode\n");
            g_state.simulated = true;
        } else {
            fprintf(stderr, "[i2c_plugin] sensors OK: T=%.1f H=%.1f L=%.1f\n", temp, humi, light);
        }
    }

    return true;
}

static void pluginDeinit(void)
{
    if (g_state.fdSht30 >= 0) { close(g_state.fdSht30); g_state.fdSht30 = -1; }
    if (g_state.fdBh1750 >= 0) { close(g_state.fdBh1750); g_state.fdBh1750 = -1; }

    if (g_state.devicesDeleted) {
        fprintf(stderr, "[i2c_plugin] restoring kernel driver I2C devices...\n");
        char path[128];
        snprintf(path, sizeof(path), "/sys/bus/i2c/devices/i2c-%d/new_device", g_state.i2cBus);
        FILE *fp = fopen(path, "w");
        if (fp) {
            fwrite("sht3x 0x44", 1, 11, fp);
            fclose(fp);
        }
        fp = fopen(path, "w");
        if (fp) {
            fwrite("bh1750 0x23", 1, 11, fp);
            fclose(fp);
        }
        g_state.devicesDeleted = false;
    }

    g_state.simulated = false;
    g_state.failCount = 0;
    g_state.simStep = 0;
}

static bool pluginRead(PluginSensorData *data)
{
    if (g_state.simulated) {
        generateSimulated(data);
        if (g_state.simStep % RECOVER_INTERVAL == 0) tryRecover();
        return true;
    }
    bool ok = true;
    if (!readSHT30(data->temperature, data->humidity)) {
        data->temperature = 0.0f; data->humidity = 0.0f; ok = false;
    }
    if (!readBH1750(data->light)) { data->light = 0.0f; ok = false; }
    if (!ok) {
        g_state.failCount++;
        if (g_state.failCount >= SIMULATE_THRESHOLD) {
            g_state.simulated = true;
            generateSimulated(data);
            return true;
        }
    } else { g_state.failCount = 0; }
    data->valid = ok;
    return ok;
}

static bool pluginIsSimulated(void) { return g_state.simulated; }

static const SensorPlugin sht30_bh1750_i2c_plugin = {
    SENSOR_PLUGIN_API_VERSION,
    "sht30_bh1750_i2c",
    "User-space I2C protocol (no kernel driver required, direct /dev/i2c-N access)",
    pluginInit, pluginDeinit, pluginRead, pluginIsSimulated
};

extern "C" const SensorPlugin *sensor_plugin_get(void)
{
    return &sht30_bh1750_i2c_plugin;
}
