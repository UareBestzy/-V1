#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

static int major;
static struct class *motor_tb6612_class;
static struct gpio_desc *motor_in[4];

/*
 * TB6612FNG is a dual H-bridge driver. This sequence drives a bipolar
 * stepper through AIN1/AIN2/BIN1/BIN2 with full-step excitation.
 */
static const unsigned char tb6612_seq[4] = {
    0x09, /* A+, B+ */
    0x0a, /* A-, B+ */
    0x06, /* A-, B- */
    0x05, /* A+, B- */
};
static int motor_index;

static void set_motor(int index)
{
    int i;

    for (i = 0; i < 4; i++)
        gpiod_set_value(motor_in[i], !!(tb6612_seq[index] & BIT(i)));
}

static void stop_motor(void)
{
    int i;

    for (i = 0; i < 4; i++)
        gpiod_set_value(motor_in[i], 0);
}

static int motor_open(struct inode *node, struct file *file)
{
    printk("%s %s line %d\n", __FILE__, __func__, __LINE__);
    return 0;
}

static ssize_t motor_write(struct file *file, const char __user *buf,
                           size_t size, loff_t *offset)
{
    unsigned char ker_buf[1];
    int step;

    if (size < 1)
        return -EINVAL;

    if (copy_from_user(ker_buf, buf, 1))
        return -EFAULT;

    if (ker_buf[0] == 1) {
        for (step = 0; step < 200; step++) {
            set_motor(motor_index);
            mdelay(3);
            motor_index--;
            if (motor_index < 0)
                motor_index = ARRAY_SIZE(tb6612_seq) - 1;
        }
    } else if (ker_buf[0] == 2) {
        for (step = 0; step < 200; step++) {
            set_motor(motor_index);
            mdelay(3);
            motor_index++;
            if (motor_index >= ARRAY_SIZE(tb6612_seq))
                motor_index = 0;
        }
    } else {
        stop_motor();
        return -EINVAL;
    }

    stop_motor();
    return 1;
}

static const struct file_operations motor_fop = {
    .owner = THIS_MODULE,
    .open = motor_open,
    .write = motor_write,
};

static int motor_probe(struct platform_device *pdev)
{
    motor_in[0] = gpiod_get(&pdev->dev, "int1", GPIOD_OUT_LOW);
    motor_in[1] = gpiod_get(&pdev->dev, "int2", GPIOD_OUT_LOW);
    motor_in[2] = gpiod_get(&pdev->dev, "int3", GPIOD_OUT_LOW);
    motor_in[3] = gpiod_get(&pdev->dev, "int4", GPIOD_OUT_LOW);

    if (IS_ERR(motor_in[0]) || IS_ERR(motor_in[1]) ||
        IS_ERR(motor_in[2]) || IS_ERR(motor_in[3]))
        return -EINVAL;

    device_create(motor_tb6612_class, NULL, MKDEV(major, 0), NULL,
                  "motor");
    return 0;
}

static int motor_remove(struct platform_device *pdev)
{
    device_destroy(motor_tb6612_class, MKDEV(major, 0));
    gpiod_put(motor_in[0]);
    gpiod_put(motor_in[1]);
    gpiod_put(motor_in[2]);
    gpiod_put(motor_in[3]);
    return 0;
}

static const struct of_device_id motor_match[] = {
    { .compatible = "imx6ull, motor" },
    {},
};

static struct platform_driver motor_driver = {
    .probe = motor_probe,
    .remove = motor_remove,
    .driver = {
        .name = "motor",
        .of_match_table = motor_match,
    },
};

static int __init motor_init(void)
{
    major = register_chrdev(0, "motor", &motor_fop);
    motor_tb6612_class = class_create(THIS_MODULE, "motor_class");
    platform_driver_register(&motor_driver);
    return 0;
}

static void __exit motor_exit(void)
{
    platform_driver_unregister(&motor_driver);
    class_destroy(motor_tb6612_class);
    unregister_chrdev(major, "motor");
}

module_init(motor_init);
module_exit(motor_exit);

MODULE_LICENSE("GPL");
