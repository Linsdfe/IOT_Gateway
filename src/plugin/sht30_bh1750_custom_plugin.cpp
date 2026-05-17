/**
 * @file sht30_bh1750_custom_plugin.cpp
 * @brief 插件3: sht30_bh1750_custom - 自定义内核驱动程序模式
 *
 * 使用自行开发的内核驱动模块（sht30_driver.ko + bh1750_driver.ko），
 * 通过自定义sysfs属性接口读取传感器数据。
 *
 * 与Linux标准内核驱动的区别：
 *   - sht30_bh1750_kernel: 使用Linux主线内核的IIO/HWMON子系统驱动
 *   - sht30_bh1750_custom: 使用自行开发的内核驱动模块
 *
 * 自定义驱动的sysfs路径（自动发现或手动配置）：
 *   /sys/bus/i2c/drivers/sht30/1-0044/temperature       → 毫摄氏度
 *   /sys/bus/i2c/drivers/sht30/1-0044/humidity          → 毫百分比
 *   /sys/bus/i2c/drivers/bh1750_custom/1-0023/illuminance → 厘lux（0.01 lux）
 *
 * 前提条件：
 *   sudo insmod sht30_driver.ko
 *   sudo insmod bh1750_driver.ko
 *   加载设备树覆盖（可选，用于自动探测）：
 *   sudo dtoverlay sht30_overlay.dtbo
 *   sudo dtoverlay bh1750_overlay.dtbo
 *
 * 使用方式：
 *   sudo ./iot_gateway --plugin sht30_bh1750_custom
 *
 * 配置字符串（可选）：
 *   "sht30_path=/sys/bus/i2c/drivers/sht30/1-0044;bh1750_path=/sys/bus/i2c/drivers/bh1750_custom/1-0023"
 *   "i2c_bus=1;sht30_addr=0x44;bh1750_addr=0x23"
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

static struct {
    int i2cBus;
    uint8_t sht30Addr;
    uint8_t bh1750Addr;
    std::string sht30Path;
    std::string bh1750Path;
    bool sht30Available;
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
    g_state.sht30Addr = 0x44;
    g_state.bh1750Addr = 0x23;
    g_state.sht30Path = "";
    g_state.bh1750Path = "";

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

        if (key == "sht30_path") g_state.sht30Path = val;
        else if (key == "bh1750_path") g_state.bh1750Path = val;
        else if (key == "i2c_bus" || key == "i2c") {
            if (val.find("/dev/i2c-") == 0)
                g_state.i2cBus = atoi(val.c_str() + 9);
            else
                g_state.i2cBus = atoi(val.c_str());
        }
        else if (key == "sht30_addr" || key == "sht30")
            g_state.sht30Addr = (uint8_t)strtol(val.c_str(), nullptr, 0);
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

static bool readSysfsInt(const std::string &path, int &value)
{
    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return false;
    if (fscanf(fp, "%d", &value) != 1) { fclose(fp); return false; }
    fclose(fp);
    return true;
}

static bool registerI2CDevice(int bus, const char *driver, uint8_t addr)
{
    if (i2cDeviceExists(bus, addr)) {
        fprintf(stderr, "[custom_plugin] device already exists at bus=%d addr=0x%02x, skipping registration\n", bus, addr);
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

static bool releaseI2CDevice(int bus, uint8_t addr)
{
    if (!i2cDeviceExists(bus, addr)) return true;
    unbindDriver(bus, addr);
    usleep(50000);
    deleteI2CDevice(bus, addr);
    usleep(50000);
    return !i2cDeviceExists(bus, addr);
}

static std::string findDriverDevicePath(const char *driverName, int bus, uint8_t addr)
{
    char i2cDev[32];
    snprintf(i2cDev, sizeof(i2cDev), "%d-%04x", bus, addr);

    char driverDir[128];
    snprintf(driverDir, sizeof(driverDir), "/sys/bus/i2c/drivers/%s", driverName);

    DIR *dir = opendir(driverDir);
    if (!dir) return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strstr(entry->d_name, i2cDev) != nullptr) {
            std::string result = std::string(driverDir) + "/" + entry->d_name;
            closedir(dir);
            return result;
        }
    }
    closedir(dir);
    return "";
}

static bool discoverSht30()
{
    if (!g_state.sht30Path.empty()) {
        int dummy;
        if (readSysfsInt(g_state.sht30Path + "/temperature", dummy)) {
            g_state.sht30Available = true;
            return true;
        }
    }

    std::string discovered = findDriverDevicePath("sht30", g_state.i2cBus, g_state.sht30Addr);
    if (!discovered.empty()) {
        int dummy;
        if (readSysfsInt(discovered + "/temperature", dummy)) {
            g_state.sht30Path = discovered;
            g_state.sht30Available = true;
            return true;
        }
    }

    g_state.sht30Available = false;
    return false;
}

static bool discoverBh1750()
{
    if (!g_state.bh1750Path.empty()) {
        int dummy;
        if (readSysfsInt(g_state.bh1750Path + "/illuminance", dummy)) {
            g_state.bh1750Available = true;
            return true;
        }
    }

    std::string discovered = findDriverDevicePath("bh1750_custom", g_state.i2cBus, g_state.bh1750Addr);
    if (!discovered.empty()) {
        int dummy;
        if (readSysfsInt(discovered + "/illuminance", dummy)) {
            g_state.bh1750Path = discovered;
            g_state.bh1750Available = true;
            return true;
        }
    }

    g_state.bh1750Available = false;
    return false;
}

static bool readSHT30(float &temp, float &humi)
{
    int tempMc = 0, humiMp = 0;
    if (!readSysfsInt(g_state.sht30Path + "/temperature", tempMc)) return false;
    if (!readSysfsInt(g_state.sht30Path + "/humidity", humiMp)) return false;
    temp = tempMc / 1000.0f;
    humi = humiMp / 1000.0f;
    return true;
}

static bool readBH1750(float &light)
{
    int luxCl = 0;
    if (!readSysfsInt(g_state.bh1750Path + "/illuminance", luxCl)) return false;
    light = luxCl / 100.0f;
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
    discoverSht30();
    discoverBh1750();
    if (g_state.sht30Available && g_state.bh1750Available) {
        g_state.simulated = false;
        g_state.failCount = 0;
        return true;
    }
    return false;
}

static bool pluginInit(const char *config)
{
    g_state.i2cBus = 1;
    g_state.sht30Addr = 0x44;
    g_state.bh1750Addr = 0x23;
    g_state.sht30Path.clear();
    g_state.bh1750Path.clear();
    g_state.sht30Available = false;
    g_state.bh1750Available = false;
    g_state.simulated = false;
    g_state.failCount = 0;
    g_state.simStep = 0;
    g_state.simTemp = 25.0f;
    g_state.simHumi = 60.0f;
    g_state.simLight = 100.0f;
    g_state.devicesRegistered = false;
    parseConfig(config);

    fprintf(stderr, "[custom_plugin] init: bus=%d sht30=0x%02x bh1750=0x%02x\n",
            g_state.i2cBus, g_state.sht30Addr, g_state.bh1750Addr);

    discoverSht30();
    discoverBh1750();
    fprintf(stderr, "[custom_plugin] discover: sht30=%s bh1750=%s\n",
            g_state.sht30Available ? g_state.sht30Path.c_str() : "NOT FOUND",
            g_state.bh1750Available ? g_state.bh1750Path.c_str() : "NOT FOUND");

    if (!g_state.sht30Available || !g_state.bh1750Available) {
        fprintf(stderr, "[custom_plugin] releasing standard driver bindings...\n");
        bool r1 = releaseI2CDevice(g_state.i2cBus, g_state.sht30Addr);
        bool r2 = releaseI2CDevice(g_state.i2cBus, g_state.bh1750Addr);
        if (r1 && r2) {
            fprintf(stderr, "[custom_plugin] I2C devices released, registering to custom drivers...\n");
        } else {
            fprintf(stderr, "[custom_plugin] WARNING: partial release, attempting registration anyway...\n");
        }

        bool regSht30 = registerI2CDevice(g_state.i2cBus, "sht30", g_state.sht30Addr);
        bool regBh1750 = registerI2CDevice(g_state.i2cBus, "bh1750_custom", g_state.bh1750Addr);
        g_state.devicesRegistered = (regSht30 || regBh1750);
        usleep(DEVICE_REG_DELAY_US);

        discoverSht30();
        discoverBh1750();
        fprintf(stderr, "[custom_plugin] after register: sht30=%s bh1750=%s\n",
                g_state.sht30Available ? g_state.sht30Path.c_str() : "NOT FOUND",
                g_state.bh1750Available ? g_state.bh1750Path.c_str() : "NOT FOUND");
    }

    if (!g_state.sht30Available || !g_state.bh1750Available) {
        g_state.simulated = true;
        fprintf(stderr, "[custom_plugin] WARNING: sensors not available, using SIMULATED mode\n");
        fprintf(stderr, "[custom_plugin] Make sure kernel modules are loaded:\n");
        fprintf(stderr, "[custom_plugin]   sudo insmod sht30_driver.ko\n");
        fprintf(stderr, "[custom_plugin]   sudo insmod bh1750_driver.ko\n");
    } else {
        float t, h, l;
        if (!readSHT30(t, h) || !readBH1750(l)) {
            g_state.simulated = true;
            fprintf(stderr, "[custom_plugin] WARNING: sensor read failed, using SIMULATED mode\n");
        } else {
            fprintf(stderr, "[custom_plugin] sensors OK: T=%.1f H=%.1f L=%.1f\n", t, h, l);
        }
    }

    return true;
}

static void pluginDeinit(void)
{
    if (g_state.devicesRegistered) {
        deleteI2CDevice(g_state.i2cBus, g_state.sht30Addr);
        deleteI2CDevice(g_state.i2cBus, g_state.bh1750Addr);
        g_state.devicesRegistered = false;

        fprintf(stderr, "[custom_plugin] restoring standard driver I2C devices...\n");
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
    }
    g_state.simulated = false;
    g_state.failCount = 0;
    g_state.simStep = 0;
    g_state.sht30Available = false;
    g_state.bh1750Available = false;
}

static bool pluginRead(PluginSensorData *data)
{
    if (g_state.simulated) {
        generateSimulated(data);
        if (g_state.simStep % RECOVER_INTERVAL == 0) tryRecover();
        return true;
    }
    bool ok = true;
    if (g_state.sht30Available) {
        if (!readSHT30(data->temperature, data->humidity)) {
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

static const SensorPlugin sht30_bh1750_custom_plugin = {
    SENSOR_PLUGIN_API_VERSION,
    "sht30_bh1750_custom",
    "Custom kernel drivers (sht30_driver.ko + bh1750_driver.ko via custom sysfs)",
    pluginInit, pluginDeinit, pluginRead, pluginIsSimulated
};

extern "C" const SensorPlugin *sensor_plugin_get(void)
{
    return &sht30_bh1750_custom_plugin;
}
