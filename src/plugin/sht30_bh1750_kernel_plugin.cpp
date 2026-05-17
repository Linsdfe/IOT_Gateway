/**
 * @file sht30_bh1750_kernel_plugin.cpp
 * @brief 插件2: sht30_bh1750_kernel - Linux内核驱动模式
 *
 * 使用Linux内核提供的驱动，支持三种接口：
 *   1. 标准IIO子系统 (sht3x IIO + bh1750 IIO)
 *   2. 标准HWMON子系统 (sht3x HWMON)
 *   3. 自定义内核驱动sysfs (sht30_driver + bh1750_driver)
 *
 * 发现优先级：
 *   SHT3x: IIO -> HWMON -> 自定义sysfs
 *   BH1750: IIO -> 自定义sysfs
 *
 * 使用方式：
 *   sudo ./iot_gateway --plugin sht30_bh1750_kernel
 *
 * 配置字符串（可选）：
 *   "i2c_bus=1;sht3x_addr=0x44;bh1750_addr=0x23"
 */

#include "sensor_plugin.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static const int SIMULATE_THRESHOLD = 3;
static const int RECOVER_INTERVAL = 30;
static const int DEVICE_REG_DELAY_US = 200000;

enum Sht3xInterface { SHT3X_NONE, SHT3X_IIO, SHT3X_HWMON, SHT3X_CUSTOM_SYSFS };
enum Bh1750Interface { BH1750_NONE, BH1750_IIO, BH1750_CUSTOM_SYSFS };

static struct {
    int i2cBus;
    uint8_t sht3xAddr;
    uint8_t bh1750Addr;
    std::string sht3xPath;
    Sht3xInterface sht3xIf;
    std::string bh1750Path;
    Bh1750Interface bh1750If;
    bool sht3xAvailable;
    bool bh1750Available;
    bool simulated;
    int failCount;
    int simStep;
    float simTemp;
    float simHumi;
    float simLight;
    bool devicesRegistered;
} g_state;

static void parseConfig(const char *config)
{
    g_state.i2cBus = 1;
    g_state.sht3xAddr = 0x44;
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

        if (key == "i2c_bus" || key == "i2c") {
            if (val.find("/dev/i2c-") == 0)
                g_state.i2cBus = atoi(val.c_str() + 9);
            else
                g_state.i2cBus = atoi(val.c_str());
        }
        else if (key == "sht3x_addr" || key == "sht30")
            g_state.sht3xAddr = (uint8_t)strtol(val.c_str(), nullptr, 0);
        else if (key == "bh1750_addr" || key == "bh1750")
            g_state.bh1750Addr = (uint8_t)strtol(val.c_str(), nullptr, 0);

        pos = end + 1;
    }
}

static bool i2cDeviceExists(int bus, uint8_t addr)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/bus/i2c/devices/%d-%04x", bus, addr);
    struct stat st;
    return stat(path, &st) == 0;
}

static bool registerI2CDevice(int bus, const char *driver, uint8_t addr)
{
    if (i2cDeviceExists(bus, addr)) {
        fprintf(stderr, "[kernel_plugin] device already exists at bus=%d addr=0x%02x, skipping registration\n", bus, addr);
        return false;
    }

    char path[128];
    snprintf(path, sizeof(path), "/sys/bus/i2c/devices/i2c-%d/new_device", bus);

    FILE *fp = fopen(path, "w");
    if (!fp) return false;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "%s 0x%02x", driver, addr);
    bool ok = (fwrite(cmd, 1, strlen(cmd), fp) > 0);
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

static bool readSysfsInt(const std::string &path, int &value)
{
    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return false;
    if (fscanf(fp, "%d", &value) != 1) { fclose(fp); return false; }
    fclose(fp);
    return true;
}

static bool readSysfsFloat(const std::string &path, float &value)
{
    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return false;
    if (fscanf(fp, "%f", &value) != 1) { fclose(fp); return false; }
    fclose(fp);
    return true;
}

static std::string findIioDeviceByName(const char *targetName)
{
    DIR *dir = opendir("/sys/bus/iio/devices");
    if (!dir) return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0) continue;

        std::string iioPath = std::string("/sys/bus/iio/devices/") + entry->d_name;
        std::string namePath = iioPath + "/name";

        FILE *fp = fopen(namePath.c_str(), "r");
        if (fp) {
            char name[64] = {0};
            fgets(name, sizeof(name), fp);
            fclose(fp);

            char *nl = strchr(name, '\n');
            if (nl) *nl = '\0';

            if (strcmp(name, targetName) == 0) {
                closedir(dir);
                return iioPath;
            }
        }
    }
    closedir(dir);
    return "";
}

static std::string findHwmonForI2C(int bus, uint8_t addr)
{
    char i2cDev[32];
    snprintf(i2cDev, sizeof(i2cDev), "%d-%04x", bus, addr);

    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "hwmon", 5) != 0) continue;

        std::string hwmonPath = std::string("/sys/class/hwmon/") + entry->d_name;
        std::string deviceLink = hwmonPath + "/device";

        char linkTarget[256];
        ssize_t len = readlink(deviceLink.c_str(), linkTarget, sizeof(linkTarget) - 1);
        if (len > 0) {
            linkTarget[len] = '\0';
            if (strstr(linkTarget, i2cDev)) {
                closedir(dir);
                return hwmonPath;
            }
        }
    }
    closedir(dir);
    return "";
}

static std::string findCustomDriverPath(const char *driverName, int bus, uint8_t addr)
{
    char expected[64];
    snprintf(expected, sizeof(expected), "%d-%04x", bus, addr);

    std::string driverDir = std::string("/sys/bus/i2c/drivers/") + driverName;
    DIR *dir = opendir(driverDir.c_str());
    if (!dir) return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (strstr(entry->d_name, expected) != nullptr) {
            std::string result = driverDir + "/" + entry->d_name;
            closedir(dir);
            return result;
        }
    }
    closedir(dir);
    return "";
}

static bool discoverSht3x()
{
    std::string iioPath = findIioDeviceByName("sht3x");
    if (!iioPath.empty()) {
        int dummy;
        if (readSysfsInt(iioPath + "/in_temp_input", dummy)) {
            g_state.sht3xPath = iioPath;
            g_state.sht3xIf = SHT3X_IIO;
            g_state.sht3xAvailable = true;
            fprintf(stderr, "[kernel_plugin] SHT3x found via IIO: %s\n", iioPath.c_str());
            return true;
        }
    }

    std::string hwmonPath = findHwmonForI2C(g_state.i2cBus, g_state.sht3xAddr);
    if (!hwmonPath.empty()) {
        int dummy;
        if (readSysfsInt(hwmonPath + "/temp1_input", dummy)) {
            g_state.sht3xPath = hwmonPath;
            g_state.sht3xIf = SHT3X_HWMON;
            g_state.sht3xAvailable = true;
            fprintf(stderr, "[kernel_plugin] SHT3x found via HWMON: %s\n", hwmonPath.c_str());
            return true;
        }
    }

    std::string customPath = findCustomDriverPath("sht30", g_state.i2cBus, g_state.sht3xAddr);
    if (!customPath.empty()) {
        int dummy;
        if (readSysfsInt(customPath + "/temperature", dummy)) {
            g_state.sht3xPath = customPath;
            g_state.sht3xIf = SHT3X_CUSTOM_SYSFS;
            g_state.sht3xAvailable = true;
            fprintf(stderr, "[kernel_plugin] SHT3x found via custom sysfs: %s\n", customPath.c_str());
            return true;
        }
    }

    g_state.sht3xAvailable = false;
    return false;
}

static bool discoverBh1750()
{
    std::string iioPath = findIioDeviceByName("bh1750");
    if (!iioPath.empty()) {
        int rawVal;
        float floatVal;
        if (readSysfsInt(iioPath + "/in_illuminance_raw", rawVal) ||
            readSysfsInt(iioPath + "/in_illuminance_input", rawVal) ||
            readSysfsFloat(iioPath + "/in_illuminance_input", floatVal)) {
            g_state.bh1750Path = iioPath;
            g_state.bh1750If = BH1750_IIO;
            g_state.bh1750Available = true;
            fprintf(stderr, "[kernel_plugin] BH1750 found via IIO: %s\n", iioPath.c_str());
            return true;
        }
    }

    std::string customPath = findCustomDriverPath("bh1750_custom", g_state.i2cBus, g_state.bh1750Addr);
    if (!customPath.empty()) {
        int dummy;
        if (readSysfsInt(customPath + "/illuminance", dummy)) {
            g_state.bh1750Path = customPath;
            g_state.bh1750If = BH1750_CUSTOM_SYSFS;
            g_state.bh1750Available = true;
            fprintf(stderr, "[kernel_plugin] BH1750 found via custom sysfs: %s\n", customPath.c_str());
            return true;
        }
    }

    g_state.bh1750Available = false;
    return false;
}

static bool readSHT3xIio(float &temp, float &humi)
{
    int tempMc = 0, humiMp = 0;
    if (!readSysfsInt(g_state.sht3xPath + "/in_temp_input", tempMc)) return false;
    if (!readSysfsInt(g_state.sht3xPath + "/in_humidityrelative_input", humiMp)) return false;
    temp = tempMc / 1000.0f;
    humi = humiMp / 1000.0f;
    return true;
}

static bool readSHT3xHwmon(float &temp, float &humi)
{
    int tempMc = 0, humiMp = 0;
    if (!readSysfsInt(g_state.sht3xPath + "/temp1_input", tempMc)) return false;
    if (!readSysfsInt(g_state.sht3xPath + "/humidity1_input", humiMp)) return false;
    temp = tempMc / 1000.0f;
    humi = humiMp / 1000.0f;
    return true;
}

static bool readSHT3xCustomSysfs(float &temp, float &humi)
{
    int tempMc = 0, humiMp = 0;
    if (!readSysfsInt(g_state.sht3xPath + "/temperature", tempMc)) return false;
    if (!readSysfsInt(g_state.sht3xPath + "/humidity", humiMp)) return false;
    temp = tempMc / 1000.0f;
    humi = humiMp / 1000.0f;
    return true;
}

static bool readSHT3x(float &temp, float &humi)
{
    if (g_state.sht3xIf == SHT3X_IIO) return readSHT3xIio(temp, humi);
    if (g_state.sht3xIf == SHT3X_HWMON) return readSHT3xHwmon(temp, humi);
    if (g_state.sht3xIf == SHT3X_CUSTOM_SYSFS) return readSHT3xCustomSysfs(temp, humi);
    return false;
}

static bool readBH1750Iio(float &light)
{
    if (g_state.bh1750Path.empty()) return false;

    int raw = 0;
    if (readSysfsInt(g_state.bh1750Path + "/in_illuminance_raw", raw)) {
        float scale = 1.0f;
        readSysfsFloat(g_state.bh1750Path + "/in_illuminance_scale", scale);
        light = raw * scale;
        return true;
    }

    int inputVal = 0;
    if (readSysfsInt(g_state.bh1750Path + "/in_illuminance_input", inputVal)) {
        light = inputVal / 1000.0f;
        return true;
    }

    float floatVal = 0.0f;
    if (readSysfsFloat(g_state.bh1750Path + "/in_illuminance_input", floatVal)) {
        light = floatVal;
        return true;
    }

    return false;
}

static bool readBH1750CustomSysfs(float &light)
{
    if (g_state.bh1750Path.empty()) return false;

    int clux = 0;
    if (!readSysfsInt(g_state.bh1750Path + "/illuminance", clux)) return false;
    light = clux / 100.0f;
    return true;
}

static bool readBH1750(float &light)
{
    if (g_state.bh1750If == BH1750_IIO) return readBH1750Iio(light);
    if (g_state.bh1750If == BH1750_CUSTOM_SYSFS) return readBH1750CustomSysfs(light);
    return false;
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
    discoverSht3x();
    discoverBh1750();

    if (g_state.sht3xAvailable && g_state.bh1750Available) {
        g_state.simulated = false;
        g_state.failCount = 0;
        return true;
    }
    return false;
}

static bool pluginInit(const char *config)
{
    g_state.i2cBus = 1;
    g_state.sht3xAddr = 0x44;
    g_state.bh1750Addr = 0x23;
    g_state.sht3xPath.clear();
    g_state.bh1750Path.clear();
    g_state.sht3xIf = SHT3X_NONE;
    g_state.bh1750If = BH1750_NONE;
    g_state.sht3xAvailable = false;
    g_state.bh1750Available = false;
    g_state.simulated = false;
    g_state.failCount = 0;
    g_state.simStep = 0;
    g_state.simTemp = 25.0f;
    g_state.simHumi = 60.0f;
    g_state.simLight = 100.0f;
    g_state.devicesRegistered = false;
    parseConfig(config);

    fprintf(stderr, "[kernel_plugin] init: bus=%d sht3x=0x%02x bh1750=0x%02x\n",
            g_state.i2cBus, g_state.sht3xAddr, g_state.bh1750Addr);

    discoverSht3x();
    discoverBh1750();

    if (!g_state.sht3xAvailable || !g_state.bh1750Available) {
        fprintf(stderr, "[kernel_plugin] trying to register I2C devices...\n");

        bool regSht3x = registerI2CDevice(g_state.i2cBus, "sht3x", g_state.sht3xAddr);
        bool regBh1750 = registerI2CDevice(g_state.i2cBus, "bh1750", g_state.bh1750Addr);

        if (!regSht3x || !regBh1750) {
            fprintf(stderr, "[kernel_plugin] standard driver registration failed or device exists, trying custom drivers...\n");
            bool regSht30 = registerI2CDevice(g_state.i2cBus, "sht30", g_state.sht3xAddr);
            bool regBh1750Custom = registerI2CDevice(g_state.i2cBus, "bh1750_custom", g_state.bh1750Addr);
            g_state.devicesRegistered = (regSht3x || regBh1750 || regSht30 || regBh1750Custom);
        } else {
            g_state.devicesRegistered = true;
        }

        usleep(DEVICE_REG_DELAY_US);

        discoverSht3x();
        discoverBh1750();
    }

    fprintf(stderr, "[kernel_plugin] discovery result: sht3x=%s(if=%s) bh1750=%s(if=%s)\n",
            g_state.sht3xAvailable ? g_state.sht3xPath.c_str() : "NOT FOUND",
            g_state.sht3xIf == SHT3X_IIO ? "IIO" : g_state.sht3xIf == SHT3X_HWMON ? "HWMON" : g_state.sht3xIf == SHT3X_CUSTOM_SYSFS ? "CUSTOM" : "NONE",
            g_state.bh1750Available ? g_state.bh1750Path.c_str() : "NOT FOUND",
            g_state.bh1750If == BH1750_IIO ? "IIO" : g_state.bh1750If == BH1750_CUSTOM_SYSFS ? "CUSTOM" : "NONE");

    if (!g_state.sht3xAvailable || !g_state.bh1750Available) {
        g_state.simulated = true;
        fprintf(stderr, "[kernel_plugin] WARNING: sensors not available, using SIMULATED mode\n");
        fprintf(stderr, "[kernel_plugin] Make sure kernel drivers are loaded:\n");
        fprintf(stderr, "[kernel_plugin]   Standard: sudo modprobe sht3x bh1750\n");
        fprintf(stderr, "[kernel_plugin]   Custom:   sudo insmod sht30_driver.ko bh1750_driver.ko\n");
    } else {
        float t, h, l;
        if (!readSHT3x(t, h) || !readBH1750(l)) {
            g_state.simulated = true;
            fprintf(stderr, "[kernel_plugin] WARNING: sensor read failed, using SIMULATED mode\n");
        } else {
            fprintf(stderr, "[kernel_plugin] sensors OK: T=%.1f H=%.1f L=%.1f\n", t, h, l);
        }
    }

    return true;
}

static void pluginDeinit(void)
{
    if (g_state.devicesRegistered) {
        deleteI2CDevice(g_state.i2cBus, g_state.sht3xAddr);
        deleteI2CDevice(g_state.i2cBus, g_state.bh1750Addr);
        g_state.devicesRegistered = false;
    }
    g_state.simulated = false;
    g_state.failCount = 0;
    g_state.simStep = 0;
    g_state.sht3xAvailable = false;
    g_state.bh1750Available = false;
    g_state.sht3xIf = SHT3X_NONE;
    g_state.bh1750If = BH1750_NONE;
}

static bool pluginRead(PluginSensorData *data)
{
    if (g_state.simulated) {
        generateSimulated(data);
        if (g_state.simStep % RECOVER_INTERVAL == 0) tryRecover();
        return true;
    }
    bool ok = true;
    if (g_state.sht3xAvailable) {
        if (!readSHT3x(data->temperature, data->humidity)) {
            data->temperature = 0.0f; data->humidity = 0.0f; ok = false;
        }
    } else {
        data->temperature = 0.0f; data->humidity = 0.0f; ok = false;
    }
    if (g_state.bh1750Available) {
        if (!readBH1750(data->light)) { data->light = 0.0f; ok = false; }
    } else {
        data->light = 0.0f; ok = false;
    }
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

static const SensorPlugin sht30_bh1750_kernel_plugin = {
    SENSOR_PLUGIN_API_VERSION,
    "sht30_bh1750_kernel",
    "Linux kernel drivers (sht3x IIO/HWMON + bh1750 IIO + custom sysfs fallback)",
    pluginInit, pluginDeinit, pluginRead, pluginIsSimulated
};

extern "C" const SensorPlugin *sensor_plugin_get(void)
{
    return &sht30_bh1750_kernel_plugin;
}
