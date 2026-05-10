#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>

#define DEVICE_NAME "bh1750"
#define CLASS_NAME  "bh1750_class"
#define BH1750_ADDR 0x23

struct bh1750_data {
    struct i2c_client *client;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    dev_t dev_num;
};

static struct bh1750_data *bh1750_dev;

static int bh1750_read_raw(struct i2c_client *client, u16 *raw)
{
    u8 cmd = 0x10;
    int ret;
    u8 buf[2];

    ret = i2c_master_send(client, &cmd, 1);
    if (ret < 0) {
        dev_err(&client->dev, "BH1750: send command failed\n");
        return ret;
    }

    msleep(180);

    ret = i2c_master_recv(client, buf, 2);
    if (ret < 0) {
        dev_err(&client->dev, "BH1750: read data failed\n");
        return ret;
    }

    *raw = (buf[0] << 8) | buf[1];
    return 0;
}

static int bh1750_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int bh1750_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t bh1750_read(struct file *file, char __user *user_buf,
                           size_t count, loff_t *offset)
{
    u16 raw;
    int lux;
    char result[32];
    int len;
    int ret;

    ret = bh1750_read_raw(bh1750_dev->client, &raw);
    if (ret < 0)
        return ret;

    lux = raw * 10 / 12;

    len = snprintf(result, sizeof(result), "%d.%d\n", lux / 10, lux % 10);

    if (copy_to_user(user_buf, result, len))
        return -EFAULT;

    return len;
}

static struct file_operations bh1750_fops = {
    .owner   = THIS_MODULE,
    .open    = bh1750_open,
    .release = bh1750_release,
    .read    = bh1750_read,
};

static int bh1750_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    int ret;

    dev_info(&client->dev, "BH1750: driver probe start\n");

    bh1750_dev = kzalloc(sizeof(struct bh1750_data), GFP_KERNEL);
    if (!bh1750_dev)
        return -ENOMEM;

    bh1750_dev->client = client;

    ret = alloc_chrdev_region(&bh1750_dev->dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        dev_err(&client->dev, "BH1750: alloc chrdev region failed\n");
        goto err_free;
    }

    cdev_init(&bh1750_dev->cdev, &bh1750_fops);
    bh1750_dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&bh1750_dev->cdev, bh1750_dev->dev_num, 1);
    if (ret) {
        dev_err(&client->dev, "BH1750: cdev add failed\n");
        goto err_unregister;
    }

    bh1750_dev->class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(bh1750_dev->class)) {
        ret = PTR_ERR(bh1750_dev->class);
        goto err_cdev_del;
    }

    bh1750_dev->device = device_create(bh1750_dev->class, NULL,
                                       bh1750_dev->dev_num, NULL,
                                       DEVICE_NAME);
    if (IS_ERR(bh1750_dev->device)) {
        ret = PTR_ERR(bh1750_dev->device);
        goto err_class_destroy;
    }

    dev_info(&client->dev, "BH1750: driver loaded, device /dev/bh1750\n");
    return 0;

err_class_destroy:
    class_destroy(bh1750_dev->class);
err_cdev_del:
    cdev_del(&bh1750_dev->cdev);
err_unregister:
    unregister_chrdev_region(bh1750_dev->dev_num, 1);
err_free:
    kfree(bh1750_dev);
    return ret;
}

static int bh1750_remove(struct i2c_client *client)
{
    device_destroy(bh1750_dev->class, bh1750_dev->dev_num);
    class_destroy(bh1750_dev->class);
    cdev_del(&bh1750_dev->cdev);
    unregister_chrdev_region(bh1750_dev->dev_num, 1);
    kfree(bh1750_dev);

    dev_info(&client->dev, "BH1750: driver removed\n");
    return 0;
}

static const struct i2c_device_id bh1750_id[] = {
    {"bh1750", 0},
    {}
};
MODULE_DEVICE_TABLE(i2c, bh1750_id);

static const struct of_device_id bh1750_of_match[] = {
    { .compatible = "rohm,bh1750" },
    {}
};
MODULE_DEVICE_TABLE(of, bh1750_of_match);

static struct i2c_driver bh1750_driver = {
    .driver = {
        .name = "bh1750",
        .owner = THIS_MODULE,
        .of_match_table = bh1750_of_match,
    },
    .probe    = bh1750_probe,
    .remove   = bh1750_remove,
    .id_table = bh1750_id,
};

module_i2c_driver(bh1750_driver);

MODULE_AUTHOR("Industrial IoT Gateway");
MODULE_DESCRIPTION("BH1750 Light Sensor Driver");
MODULE_LICENSE("GPL");
