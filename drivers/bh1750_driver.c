/**
 * @file bh1750_driver.c
 * @brief BH1750 光照传感器 Linux 内核驱动模块
 *
 * 通过 I2C 子系统与 BH1750 通信，提供 sysfs 接口读取光照强度。
 * 加载后创建 sysfs 属性文件：
 *   - illuminance: 当前光照值（毫lux，如 29200 = 29.2 lux）
 *
 * 使用方式：
 *   insmod bh1750_driver.ko
 *   cat /sys/bus/i2c/drivers/bh1750/1-0023/illuminance
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/err.h>

#define BH1750_CMD_ONE_H_RES    0x20

struct bh1750_data {
    struct i2c_client *client;
    struct mutex lock;
    int illuminance;
};

static int bh1750_read_raw(struct i2c_client *client,
                           u8 *buf, int len)
{
    int ret;
    u8 cmd = BH1750_CMD_ONE_H_RES;

    ret = i2c_master_send(client, &cmd, 1);
    if (ret != 1) {
        dev_err(&client->dev, "send cmd failed: %d\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    msleep(180);

    ret = i2c_master_recv(client, buf, len);
    if (ret != len) {
        dev_err(&client->dev, "recv data failed: %d\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

static int bh1750_update_value(struct bh1750_data *data)
{
    int ret;
    u8 buf[2];
    u16 raw;

    mutex_lock(&data->lock);

    ret = bh1750_read_raw(data->client, buf, 2);
    if (ret) {
        mutex_unlock(&data->lock);
        return ret;
    }

    raw = (buf[0] << 8) | buf[1];
    data->illuminance = (int)((u32)raw * 10000 / 12);

    mutex_unlock(&data->lock);
    return 0;
}

static ssize_t illuminance_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct bh1750_data *data = dev_get_drvdata(dev);
    int ret = bh1750_update_value(data);
    if (ret)
        return ret;
    return sprintf(buf, "%d\n", data->illuminance);
}

static DEVICE_ATTR_RO(illuminance);

static struct attribute *bh1750_attrs[] = {
    &dev_attr_illuminance.attr,
    NULL
};

static const struct attribute_group bh1750_attr_group = {
    .attrs = bh1750_attrs,
};

static int bh1750_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    struct bh1750_data *data;
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

    ret = sysfs_create_group(&client->dev.kobj, &bh1750_attr_group);
    if (ret) {
        dev_err(&client->dev, "sysfs create failed: %d\n", ret);
        return ret;
    }

    dev_info(&client->dev, "BH1750 driver probed at 0x%02x\n",
             client->addr);
    return 0;
}

static int bh1750_remove(struct i2c_client *client)
{
    sysfs_remove_group(&client->dev.kobj, &bh1750_attr_group);
    dev_info(&client->dev, "BH1750 driver removed\n");
    return 0;
}

static const struct i2c_device_id bh1750_id[] = {
    { "bh1750_custom", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, bh1750_id);

static const struct of_device_id bh1750_of_match[] = {
    { .compatible = "rohm,bh1750_custom" },
    { }
};
MODULE_DEVICE_TABLE(of, bh1750_of_match);

static struct i2c_driver bh1750_driver = {
    .driver = {
        .name = "bh1750_custom",
        .of_match_table = bh1750_of_match,
    },
    .probe   = bh1750_probe,
    .remove  = bh1750_remove,
    .id_table = bh1750_id,
};

module_i2c_driver(bh1750_driver);

MODULE_AUTHOR("IoT Gateway Team");
MODULE_DESCRIPTION("BH1750 Ambient Light Sensor Driver");
MODULE_LICENSE("GPL");
