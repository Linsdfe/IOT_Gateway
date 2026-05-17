/**
 * @file sht30_driver.c
 * @brief SHT30 温湿度传感器 Linux 内核驱动模块
 *
 * 通过 I2C 子系统与 SHT30 通信，提供 sysfs 接口读取温度和湿度。
 * 加载后创建 /sys/class/i2c-dev/i2c-1/device/1-0044/ 下的属性文件：
 *   - temperature: 当前温度值（毫摄氏度，如 25600 = 25.6°C）
 *   - humidity:    当前湿度值（毫百分比，如 67000 = 67.0%）
 *
 * 使用方式：
 *   insmod sht30_driver.ko
 *   cat /sys/bus/i2c/drivers/sht30/1-0044/temperature
 *   cat /sys/bus/i2c/drivers/sht30/1-0044/humidity
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/math64.h>

#define SHT30_CMD_MEASURE_HPM   0x2C06
#define SHT30_CMD_MSB(x)        ((x) >> 8)
#define SHT30_CMD_LSB(x)        ((x) & 0xFF)

struct sht30_data {
    struct i2c_client *client;
    struct mutex lock;
    int temperature;
    int humidity;
};

static int sht30_read_raw(struct i2c_client *client,
                          u8 *buf, int len)
{
    int ret;
    u8 cmd[2] = { SHT30_CMD_MSB(SHT30_CMD_MEASURE_HPM),
                  SHT30_CMD_LSB(SHT30_CMD_MEASURE_HPM) };

    ret = i2c_master_send(client, cmd, 2);
    if (ret != 2) {
        dev_err(&client->dev, "send cmd failed: %d\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    msleep(20);

    ret = i2c_master_recv(client, buf, len);
    if (ret != len) {
        dev_err(&client->dev, "recv data failed: %d\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

static int sht30_update_values(struct sht30_data *data)
{
    int ret;
    u8 buf[6];
    u16 raw_temp, raw_humi;

    mutex_lock(&data->lock);

    ret = sht30_read_raw(data->client, buf, 6);
    if (ret) {
        mutex_unlock(&data->lock);
        return ret;
    }

    raw_temp = (buf[0] << 8) | buf[1];
    raw_humi = (buf[3] << 8) | buf[4];

    data->temperature = -45000 + (int)div_s64((s64)175000 * raw_temp, 65535);
    data->humidity    = (int)div_s64((s64)100000 * raw_humi, 65535);

    mutex_unlock(&data->lock);
    return 0;
}

static ssize_t temperature_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct sht30_data *data = dev_get_drvdata(dev);
    int ret = sht30_update_values(data);
    if (ret)
        return ret;
    return sprintf(buf, "%d\n", data->temperature);
}

static ssize_t humidity_show(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
    struct sht30_data *data = dev_get_drvdata(dev);
    int ret = sht30_update_values(data);
    if (ret)
        return ret;
    return sprintf(buf, "%d\n", data->humidity);
}

static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RO(humidity);

static struct attribute *sht30_attrs[] = {
    &dev_attr_temperature.attr,
    &dev_attr_humidity.attr,
    NULL
};

static const struct attribute_group sht30_attr_group = {
    .attrs = sht30_attrs,
};

static int sht30_probe(struct i2c_client *client,
                       const struct i2c_device_id *id)
{
    struct sht30_data *data;
    int ret;

    if (!i2c_check_functionality(client->adapter,
                                 I2C_FUNC_I2C))
        return -EIO;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    mutex_init(&data->lock);
    i2c_set_clientdata(client, data);

    ret = sysfs_create_group(&client->dev.kobj, &sht30_attr_group);
    if (ret) {
        dev_err(&client->dev, "sysfs create failed: %d\n", ret);
        return ret;
    }

    dev_info(&client->dev, "SHT30 driver probed at 0x%02x\n",
             client->addr);
    return 0;
}

static int sht30_remove(struct i2c_client *client)
{
    sysfs_remove_group(&client->dev.kobj, &sht30_attr_group);
    dev_info(&client->dev, "SHT30 driver removed\n");
    return 0;
}

static const struct i2c_device_id sht30_id[] = {
    { "sht30", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, sht30_id);

static const struct of_device_id sht30_of_match[] = {
    { .compatible = "sensirion,sht30" },
    { }
};
MODULE_DEVICE_TABLE(of, sht30_of_match);

static struct i2c_driver sht30_driver = {
    .driver = {
        .name = "sht30",
        .of_match_table = sht30_of_match,
    },
    .probe   = sht30_probe,
    .remove  = sht30_remove,
    .id_table = sht30_id,
};

module_i2c_driver(sht30_driver);

MODULE_AUTHOR("IoT Gateway Team");
MODULE_DESCRIPTION("SHT30 Temperature and Humidity Sensor Driver");
MODULE_LICENSE("GPL");
