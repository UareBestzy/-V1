#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/uaccess.h>

static int major;
static struct class *dht11_class;
static struct gpio_desc *dht11_gpio;

static int dht11_open(struct inode *node, struct file *file)
{
    printk("%s %s line %d\n", __FILE__, __func__, __LINE__);
    return 0;
}

static int dht11_wait_for_ready(void)
{
    int timeout_us = 20000;

    while (gpiod_get_value(dht11_gpio) && --timeout_us)
        udelay(1);
    if (!timeout_us)
        return -ETIMEDOUT;

    timeout_us = 200;
    while (!gpiod_get_value(dht11_gpio) && --timeout_us)
        udelay(1);
    if (!timeout_us)
        return -ETIMEDOUT;

    timeout_us = 200;
    while (gpiod_get_value(dht11_gpio) && --timeout_us)
        udelay(1);
    if (!timeout_us)
        return -ETIMEDOUT;

    return 0;
}

static int dht11_read_byte(unsigned char *buf)
{
    int i;
    unsigned char data = 0;

    for (i = 0; i < 8; i++) {
        int timeout_us = 400;

        while (!gpiod_get_value(dht11_gpio) && --timeout_us)
            udelay(1);
        if (!timeout_us)
            return -ETIMEDOUT;

        udelay(40);

        if (gpiod_get_value(dht11_gpio)) {
            data = (data << 1) | 1;

            timeout_us = 400;
            while (gpiod_get_value(dht11_gpio) && --timeout_us)
                udelay(1);
            if (!timeout_us)
                return -ETIMEDOUT;
        } else {
            data <<= 1;
        }
    }

    *buf = data;
    return 0;
}

static ssize_t dht11_read(struct file *file, char __user *buf,
                          size_t size, loff_t *offset)
{
    unsigned char data[5];
    unsigned long flags;
    int i;

    if (size < 4)
        return -EINVAL;

    local_irq_save(flags);

    gpiod_direction_output(dht11_gpio, 1);
    mdelay(30);
    gpiod_set_value(dht11_gpio, 0);
    mdelay(20);
    gpiod_set_value(dht11_gpio, 1);
    udelay(40);
    gpiod_direction_input(dht11_gpio);
    udelay(2);

    if (dht11_wait_for_ready())
        goto again;

    for (i = 0; i < 5; i++) {
        if (dht11_read_byte(&data[i]))
            goto again;
    }

    gpiod_direction_output(dht11_gpio, 1);
    local_irq_restore(flags);

    if (data[4] != (unsigned char)(data[0] + data[1] + data[2] + data[3]))
        return -EIO;

    if (copy_to_user(buf, data, 4))
        return -EFAULT;

    printk("dht11 humidity=%u.%u temperature=%u.%u\n",
           data[0], data[1], data[2], data[3]);
    return 4;

again:
    gpiod_direction_output(dht11_gpio, 1);
    local_irq_restore(flags);
    return -EAGAIN;
}

static int dht11_release(struct inode *node, struct file *file)
{
    printk("%s %s line %d\n", __FILE__, __func__, __LINE__);
    return 0;
}

static const struct file_operations dht11_fop = {
    .owner = THIS_MODULE,
    .open = dht11_open,
    .read = dht11_read,
    .release = dht11_release,
};

static int dht11_probe(struct platform_device *pdev)
{
    dht11_gpio = gpiod_get(&pdev->dev, "dht11", GPIOD_OUT_HIGH);
    if (IS_ERR(dht11_gpio))
        return PTR_ERR(dht11_gpio);

    device_create(dht11_class, NULL, MKDEV(major, 0), NULL, "querydht11");
    return 0;
}

static int dht11_remove(struct platform_device *pdev)
{
    device_destroy(dht11_class, MKDEV(major, 0));
    gpiod_put(dht11_gpio);
    return 0;
}

static const struct of_device_id dht11_match[] = {
    { .compatible = "imx6ull, dht11" },
    {},
};

static struct platform_driver dht11_driver = {
    .probe = dht11_probe,
    .remove = dht11_remove,
    .driver = {
        .name = "dht11",
        .of_match_table = dht11_match,
    },
};

static int __init dht11_init(void)
{
    major = register_chrdev(0, "dht11", &dht11_fop);
    if (major < 0)
        return major;

    dht11_class = class_create(THIS_MODULE, "dht11_class");
    if (IS_ERR(dht11_class)) {
        unregister_chrdev(major, "dht11");
        return PTR_ERR(dht11_class);
    }

    return platform_driver_register(&dht11_driver);
}

static void __exit dht11_exit(void)
{
    platform_driver_unregister(&dht11_driver);
    class_destroy(dht11_class);
    unregister_chrdev(major, "dht11");
}

module_init(dht11_init);
module_exit(dht11_exit);

MODULE_LICENSE("GPL");
