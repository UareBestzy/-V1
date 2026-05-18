#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>

static int major;
static struct class *sr501_class;
static struct gpio_desc *sr501_gpio;
static int irq;
static atomic_t time_exceeded = ATOMIC_INIT(0);

static int sr501_open(struct inode *node, struct file *file)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

static ssize_t sr501_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    int re;
    int value;
    char data;

    if (size < 1) {
        return -EINVAL;
    }

    value = gpiod_get_value(sr501_gpio);
    if (value < 0) {
        return value;
    }

    if (value == 1 || atomic_read(&time_exceeded) == 1) {
        data = 1;
        re = copy_to_user(buf, &data, 1);
    } else {
        data = 0;
        re = copy_to_user(buf, &data, 1);
    }

    if (re) {
        return -EFAULT;
    }

    atomic_set(&time_exceeded, 0);
    return 1;
}

static struct file_operations sr501_fop = {
    .owner = THIS_MODULE,
    .open = sr501_open,
    .read = sr501_read,
};

static irqreturn_t sr501_isr(int irq, void *dev_id)
{
    int value = gpiod_get_value(sr501_gpio);

    if (value > 0) {
        atomic_set(&time_exceeded, 1);
    }

    return IRQ_HANDLED;
}

static int sr501_probe(struct platform_device *dev)
{
    int err;

    sr501_gpio = gpiod_get(&dev->dev, "sr501", GPIOD_IN);
    if (IS_ERR(sr501_gpio)) {
        return PTR_ERR(sr501_gpio);
    }

    irq = gpiod_to_irq(sr501_gpio);
    if (irq < 0) {
        gpiod_put(sr501_gpio);
        return irq;
    }

    err = request_irq(irq, sr501_isr,
              IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
              "imx6ull_sr501", NULL);
    if (err) {
        gpiod_put(sr501_gpio);
        return err;
    }

    device_create(sr501_class, NULL, MKDEV(major, 0), NULL, "mysr501");
    return 0;
}

static int sr501_remove(struct platform_device *dev)
{
    device_destroy(sr501_class, MKDEV(major, 0));
    free_irq(irq, NULL);
    gpiod_put(sr501_gpio);
    return 0;
}

static const struct of_device_id sr501_match[] = {
    { .compatible = "imx6ull, sr501" },
    {},
};

static struct platform_driver sr501_driver = {
    .probe = sr501_probe,
    .remove = sr501_remove,
    .driver = {
        .name = "sr501",
        .of_match_table = sr501_match,
    },
};

static int __init sr501_init(void)
{
    major = register_chrdev(0, "sr501", &sr501_fop);
    sr501_class = class_create(THIS_MODULE, "sr501_class");
    platform_driver_register(&sr501_driver);
    return 0;
}

static void __exit sr501_exit(void)
{
    platform_driver_unregister(&sr501_driver);
    class_destroy(sr501_class);
    unregister_chrdev(major, "sr501");
}

module_init(sr501_init);
module_exit(sr501_exit);

MODULE_LICENSE("GPL");
