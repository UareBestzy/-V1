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
static struct class *rd03_class;
static struct gpio_desc *rd03_gpio;
static int irq;
static atomic_t person_seen = ATOMIC_INIT(0);

static int rd03_open(struct inode *node, struct file *file)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

static ssize_t rd03_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    int ret;
    int value;
    char data;

    if (size < 1) {
        return -EINVAL;
    }

    value = gpiod_get_value(rd03_gpio);
    if (value < 0) {
        return value;
    }

    data = (value == 1 || atomic_read(&person_seen) == 1) ? 1 : 0;
    ret = copy_to_user(buf, &data, 1);
    if (ret) {
        return -EFAULT;
    }

    atomic_set(&person_seen, 0);
    return 1;
}

static struct file_operations rd03_fop = {
    .owner = THIS_MODULE,
    .open = rd03_open,
    .read = rd03_read,
};

static irqreturn_t rd03_isr(int irq, void *dev_id)
{
    int value = gpiod_get_value(rd03_gpio);

    if (value > 0) {
        atomic_set(&person_seen, 1);
    }

    return IRQ_HANDLED;
}

static int rd03_probe(struct platform_device *dev)
{
    int err;

    rd03_gpio = gpiod_get(&dev->dev, "rd03", GPIOD_IN);
    if (IS_ERR(rd03_gpio)) {
        return PTR_ERR(rd03_gpio);
    }

    irq = gpiod_to_irq(rd03_gpio);
    if (irq < 0) {
        gpiod_put(rd03_gpio);
        return irq;
    }

    err = request_irq(irq, rd03_isr,
              IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
              "imx6ull_rd03", NULL);
    if (err) {
        gpiod_put(rd03_gpio);
        return err;
    }

    device_create(rd03_class, NULL, MKDEV(major, 0), NULL, "myrd03");
    return 0;
}

static int rd03_remove(struct platform_device *dev)
{
    device_destroy(rd03_class, MKDEV(major, 0));
    free_irq(irq, NULL);
    gpiod_put(rd03_gpio);
    return 0;
}

static const struct of_device_id rd03_match[] = {
    { .compatible = "imx6ull, rd03" },
    {},
};

static struct platform_driver rd03_driver = {
    .probe = rd03_probe,
    .remove = rd03_remove,
    .driver = {
        .name = "rd03",
        .of_match_table = rd03_match,
    },
};

static int __init rd03_init(void)
{
    major = register_chrdev(0, "rd03", &rd03_fop);
    rd03_class = class_create(THIS_MODULE, "rd03_class");
    platform_driver_register(&rd03_driver);
    return 0;
}

static void __exit rd03_exit(void)
{
    platform_driver_unregister(&rd03_driver);
    class_destroy(rd03_class);
    unregister_chrdev(major, "rd03");
}

module_init(rd03_init);
module_exit(rd03_exit);

MODULE_LICENSE("GPL");
