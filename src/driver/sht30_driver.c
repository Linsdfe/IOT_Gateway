#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>

#define DEVICE_NAME "sht30"
#define CLASS_NAME  "sht30_class"
#define SHT30_ADDR 0x44

struct sht30_data {
    struct i2c_client *client;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    dev_t dev_num;
};

static struct sht30_data *sht30_dev;

static int sht30_read_raw(struct i2c_client *client, u8 *buf)
{
    u8 cmd[2] = {0x2C, 0x06};
    int ret;

    ret = i2c_master_send(client, cmd, 2);
    if (ret < 0) {
        dev_err(&client->dev, "SHT30: send command failed\n");
        return ret;
    }

    msleep(20);

    ret = i2c_master_recv(client, buf, 6);
    if (ret < 0) {
        dev_err(&client->dev, "SHT30: read data failed\n");
        return ret;
    }

    return 0;
}

static void sht30_convert(u8 *raw, int *temp, int *humi)
{
    u16 temp_raw, humi_raw;

    temp_raw = (raw[0] << 8) | raw[1];
    *temp = -4500 + (17500 * temp_raw / 65535);

    humi_raw = (raw[3] << 8) | raw[4];
    *humi = (10000 * humi_raw / 65535);
}

static int sht30_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int sht30_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t sht30_read(struct file *file, char __user *user_buf,
                          size_t count, loff_t *offset)
{
    u8 raw_data[6];
    int temp, humi;
    char result[32];
    int len;
    int ret;

    ret = sht30_read_raw(sht30_dev->client, raw_data);
    if (ret < 0)
        return ret;

    sht30_convert(raw_data, &temp, &humi);

    len = snprintf(result, sizeof(result), "%d.%02d,%d.%02d\n",
                   temp / 100, abs(temp % 100),
                   humi / 100, humi % 100);

    if (copy_to_user(user_buf, result, len))
        return -EFAULT;

    return len;
}

static struct file_operations sht30_fops = {
    .owner   = THIS_MODULE,
    .open    = sht30_open,
    .release = sht30_release,
    .read    = sht30_read,
};

static int sht30_probe(struct i2c_client *client,
                       const struct i2c_device_id *id)
{
    int ret;

    dev_info(&client->dev, "SHT30: driver probe start\n");

    sht30_dev = kzalloc(sizeof(struct sht30_data), GFP_KERNEL);
    if (!sht30_dev)
        return -ENOMEM;

    sht30_dev->client = client;

    ret = alloc_chrdev_region(&sht30_dev->dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        dev_err(&client->dev, "SHT30: alloc chrdev region failed\n");
        goto err_free;
    }

    cdev_init(&sht30_dev->cdev, &sht30_fops);
    sht30_dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&sht30_dev->cdev, sht30_dev->dev_num, 1);
    if (ret) {
        dev_err(&client->dev, "SHT30: cdev add failed\n");
        goto err_unregister;
    }

    sht30_dev->class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(sht30_dev->class)) {
        ret = PTR_ERR(sht30_dev->class);
        goto err_cdev_del;
    }

    sht30_dev->device = device_create(sht30_dev->class, NULL,
                                      sht30_dev->dev_num, NULL,
                                      DEVICE_NAME);
    if (IS_ERR(sht30_dev->device)) {
        ret = PTR_ERR(sht30_dev->device);
        goto err_class_destroy;
    }

    dev_info(&client->dev, "SHT30: driver loaded, device /dev/sht30\n");
    return 0;

err_class_destroy:
    class_destroy(sht30_dev->class);
err_cdev_del:
    cdev_del(&sht30_dev->cdev);
err_unregister:
    unregister_chrdev_region(sht30_dev->dev_num, 1);
err_free:
    kfree(sht30_dev);
    return ret;
}

static int sht30_remove(struct i2c_client *client)
{
    device_destroy(sht30_dev->class, sht30_dev->dev_num);
    class_destroy(sht30_dev->class);
    cdev_del(&sht30_dev->cdev);
    unregister_chrdev_region(sht30_dev->dev_num, 1);
    kfree(sht30_dev);

    dev_info(&client->dev, "SHT30: driver removed\n");
    return 0;
}

static const struct i2c_device_id sht30_id[] = {
    {"sht30", 0},
    {}
};
MODULE_DEVICE_TABLE(i2c, sht30_id);

static const struct of_device_id sht30_of_match[] = {
    { .compatible = "sensirion,sht30" },
    {}
};
MODULE_DEVICE_TABLE(of, sht30_of_match);

static struct i2c_driver sht30_driver = {
    .driver = {
        .name = "sht30",
        .owner = THIS_MODULE,
        .of_match_table = sht30_of_match,
    },
    .probe    = sht30_probe,
    .remove   = sht30_remove,
    .id_table = sht30_id,
};

module_i2c_driver(sht30_driver);

MODULE_AUTHOR("Industrial IoT Gateway");
MODULE_DESCRIPTION("SHT30 Temperature and Humidity Sensor Driver");
MODULE_LICENSE("GPL");
