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
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

static int major;
static struct class *fan_class;
static struct gpio_desc *fan_ain1;
static struct gpio_desc *fan_ain2;
static struct gpio_desc *fan_pwm;
static struct gpio_desc *fan_stby;
static DEFINE_MUTEX(fan_lock);
static DEFINE_SPINLOCK(fan_pwm_lock);
static struct hrtimer fan_timer;
static unsigned char fan_speed;
static bool fan_pwm_level;
static bool fan_pwm_active;
static const unsigned int fan_period_ns = 4000000;
static const unsigned int fan_duty_percent[] = {0, 30, 60, 100};

static void fan_pwm_set(int value)
{
    gpiod_set_value(fan_pwm, value);
}

static void fan_set_direction(unsigned char dir)
{
    if (dir == 1) {
        gpiod_set_value(fan_ain1, 1);
        gpiod_set_value(fan_ain2, 0);
    } else {
        gpiod_set_value(fan_ain1, 0);
        gpiod_set_value(fan_ain2, 1);
    }
}

static enum hrtimer_restart fan_timer_func(struct hrtimer *timer)
{
    unsigned long flags;
    unsigned int duty_ns;
    unsigned int next_ns;

    spin_lock_irqsave(&fan_pwm_lock, flags);

    if (!fan_pwm_active || fan_speed == 0 || fan_speed >= ARRAY_SIZE(fan_duty_percent)) {
        spin_unlock_irqrestore(&fan_pwm_lock, flags);
        return HRTIMER_NORESTART;
    }

    duty_ns = fan_period_ns * fan_duty_percent[fan_speed] / 100;

    if (fan_pwm_level) {
        fan_pwm_set(0);
        fan_pwm_level = false;
        next_ns = fan_period_ns - duty_ns;
    } else {
        fan_pwm_set(1);
        fan_pwm_level = true;
        next_ns = duty_ns;
    }

    spin_unlock_irqrestore(&fan_pwm_lock, flags);
    hrtimer_forward_now(timer, ns_to_ktime(next_ns));
    return HRTIMER_RESTART;
}

static void fan_stop_locked(void)
{
    unsigned long flags;

    hrtimer_cancel(&fan_timer);

    spin_lock_irqsave(&fan_pwm_lock, flags);
    fan_speed = 0;
    fan_pwm_active = false;
    fan_pwm_level = false;
    spin_unlock_irqrestore(&fan_pwm_lock, flags);

    fan_pwm_set(0);
    gpiod_set_value(fan_stby, 0);
    gpiod_set_value(fan_ain1, 0);
    gpiod_set_value(fan_ain2, 0);
}

static int fan_apply(unsigned char dir, unsigned char speed)
{
    unsigned long flags;
    unsigned int duty_ns;

    if (dir != 1 && dir != 2) {
        return -EINVAL;
    }

    if (speed > 3) {
        return -EINVAL;
    }

    mutex_lock(&fan_lock);

    if (speed == 0) {
        fan_stop_locked();
        mutex_unlock(&fan_lock);
        return 0;
    }

    hrtimer_cancel(&fan_timer);
    fan_set_direction(dir);
    gpiod_set_value(fan_stby, 1);

    spin_lock_irqsave(&fan_pwm_lock, flags);
    fan_speed = speed;

    if (speed == 3) {
        fan_pwm_active = false;
        fan_pwm_level = true;
        spin_unlock_irqrestore(&fan_pwm_lock, flags);
        fan_pwm_set(1);
    } else {
        fan_pwm_active = true;
        fan_pwm_level = true;
        duty_ns = fan_period_ns * fan_duty_percent[speed] / 100;
        spin_unlock_irqrestore(&fan_pwm_lock, flags);
        fan_pwm_set(1);
        hrtimer_start(&fan_timer, ns_to_ktime(duty_ns), HRTIMER_MODE_REL);
    }

    mutex_unlock(&fan_lock);
    return 0;
}

static int fan_open(struct inode *node, struct file *file)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

static ssize_t fan_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    unsigned char ker_buf[2];

    if (size < 2) {
        return -EINVAL;
    }

    if (copy_from_user(ker_buf, buf, 2)) {
        return -EFAULT;
    }

    return fan_apply(ker_buf[0], ker_buf[1]) ? : 2;
}

static struct file_operations fan_fop = {
    .owner = THIS_MODULE,
    .open = fan_open,
    .write = fan_write,
};

static int fan_probe(struct platform_device *dev)
{
    fan_ain1 = gpiod_get(&dev->dev, "ain1", GPIOD_OUT_LOW);
    fan_ain2 = gpiod_get(&dev->dev, "ain2", GPIOD_OUT_LOW);
    fan_pwm = gpiod_get(&dev->dev, "pwm", GPIOD_OUT_LOW);
    fan_stby = gpiod_get(&dev->dev, "stby", GPIOD_OUT_LOW);

    if (IS_ERR(fan_ain1) || IS_ERR(fan_ain2) ||
        IS_ERR(fan_pwm) || IS_ERR(fan_stby)) {
        if (!IS_ERR(fan_ain1))
            gpiod_put(fan_ain1);
        if (!IS_ERR(fan_ain2))
            gpiod_put(fan_ain2);
        if (!IS_ERR(fan_pwm))
            gpiod_put(fan_pwm);
        if (!IS_ERR(fan_stby))
            gpiod_put(fan_stby);
        return -EINVAL;
    }

    hrtimer_init(&fan_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    fan_timer.function = fan_timer_func;
    fan_stop_locked();

    device_create(fan_class, NULL, MKDEV(major, 0), NULL, "fanmotor");
    return 0;
}

static int fan_remove(struct platform_device *dev)
{
    device_destroy(fan_class, MKDEV(major, 0));
    mutex_lock(&fan_lock);
    fan_stop_locked();
    mutex_unlock(&fan_lock);
    gpiod_put(fan_ain1);
    gpiod_put(fan_ain2);
    gpiod_put(fan_pwm);
    gpiod_put(fan_stby);
    return 0;
}

static const struct of_device_id fan_match[] = {
    { .compatible = "imx6ull, fanmotor" },
    {},
};

static struct platform_driver fan_driver = {
    .probe = fan_probe,
    .remove = fan_remove,
    .driver = {
        .name = "fanmotor",
        .of_match_table = fan_match,
    },
};

static int __init fan_init(void)
{
    major = register_chrdev(0, "fanmotor", &fan_fop);
    fan_class = class_create(THIS_MODULE, "fanmotor_class");
    platform_driver_register(&fan_driver);
    return 0;
}

static void __exit fan_exit(void)
{
    platform_driver_unregister(&fan_driver);
    class_destroy(fan_class);
    unregister_chrdev(major, "fanmotor");
}

module_init(fan_init);
module_exit(fan_exit);

MODULE_LICENSE("GPL");
