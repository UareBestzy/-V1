#include <linux/module.h>
#include <linux/poll.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <asm/current.h>
#include <linux/delay.h>
#include <linux/pwm.h>
static int major;   // 主设备号，需要在多个函数中使用，设置为全局变量
static struct class *sg90_class;  // class_create的返回值
static struct pwm_device *sg90_pwm;

/* 4.1 打开设备驱动 */
static int sg90_open(struct inode * node, struct file * file){
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

/* 4.2 向用户空间写入中数据，与用户的read函数对应 */
static ssize_t sg90_write(struct file *file, const char __user *buf, size_t size, loff_t *offset){
    int re;
    unsigned char ker_buf[1];
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    // extern inline long copy_from_user(void *to, const void __user *from, long n)
    re = copy_from_user(ker_buf, buf, size);  // 将应用程序中的buf传入kernel_buf中
    /* 配置PWM：旋转 */
    if(ker_buf[0] == 1){   // 正转
        pwm_config(sg90_pwm, 1200000, 20000000); 
    }else if(ker_buf[0] == 0){   // 停下
        pwm_config(sg90_pwm, 1500000, 20000000);
    }   
    mdelay(500);
    pwm_config(sg90_pwm, 1500000, 20000000);   // 1.5ms
    return 1;
}

/* 4. 定义file_operations结构体 */
static struct file_operations sg90_fop = {
    .owner      = THIS_MODULE,
    .open       = sg90_open,      // 没有open函数也可以打开设备
    .write      = sg90_write,
};

/* 5.1 实现platform_driver的probe函数 */
// 在这个函数能够获取device的resource，并进行device_create
static int sg90_probe(struct platform_device *dev)  // 通过参数struct platform_device *dev获得device中的resource，去操作硬件
{
    struct device_node *node = dev->dev.of_node;
    sg90_pwm = devm_of_pwm_get(&dev->dev, node, NULL); 
    pwm_config(sg90_pwm, 1500000, 20000000);   /* 配置PWM：1.5ms，90度，周期：20000000ns = 20ms */
    pwm_set_polarity(sg90_pwm, PWM_POLARITY_NORMAL); /* 设置输出极性：占空比为高电平 */
    pwm_enable(sg90_pwm);    /* 使能PWM输出 */

    // 在获得引脚的时候就已经设置了方向，下面就不需要设置方向了

    /* 5.1.2 设置方向 */
    // gpiod_direction_input(sg90_gpio);  // 将引脚配置为输入引脚
    /* 5.1.3 获取中断号 */    
    // irq = gpiod_to_irq(sg90_echo);
    // /* 5.1.4 开中断 */
    // // 为irq中断注册了一个sg90_isr中断服务函数，上升沿触发
    // err = request_irq(irq, sg90_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "imx6ull_sg90", NULL);
    device_create(sg90_class, NULL, MKDEV(major, 0), NULL, "sg90");
    return 0; 
}

/* 5.2 实现platform_driver的remove函数 */
static int sg90_remove(struct platform_device *dev)
{
    /* 5.2.1 销毁设备 */
    device_destroy(sg90_class, MKDEV(major, 0)); /* /dev/sg900,1,... */
    // free_irq(irq, NULL);
    /* C. 5.2.4 销毁gpios */
	pwm_config(sg90_pwm, 500000, 20000000);  /* 配置PWM：0.5ms，0度 */
	pwm_free(sg90_pwm);
    return 0;
}
/* A. 5.3 实现platform_driver的of_device_id，用于支持设备树 */
static const struct of_device_id sg90_match[] = {
	{ .compatible = "imx6ull, sg90" },
	{},
};

/* 5. 定义platform_driver结构体 */
static struct platform_driver sg90_driver = {
    .probe = sg90_probe,      // 当配对时调用probe函数
    .remove = sg90_remove,    // 当取消设备时调用remove函数
    .driver = {
        .name = "sg90",      // 名字和platform_device配对（设备树下最后才匹配这个，一般来说不用它）
        .of_match_table = sg90_match,  // 定义of_match_table，支持设备树
    },
};

/* 1. 定义入口函数 */
static int __init sg90_init(void){   // __init为段属性，表示入口函数只使用一次，用过之后可以释放
    /* 1.1 注册字符设备驱动 */
    major = register_chrdev(0, "sg90", &sg90_fop);  // 参数为"主设备号（设置为0表示自动分配）、驱动名称、file_operations结构体"，返回值为主设备号，注意major为全局变量，在卸载设备驱动时要用；这个函数仅仅是注册了设备节点，如果想使用，需要通过创建设备节点（主设备号和注册的设备节点相同）
    /* 1.2 创建一个类，提供设备信息 */
    sg90_class = class_create(THIS_MODULE, "sg90_class");  // 参数为THIS_MODULE、类的名称，返回值为一个类（全局变量）；在/sys/class/文件下创建了一个hello_class的子目录
    /* 1.3 注册platform_driver */
    platform_driver_register(&sg90_driver);
    return 0;
}

/* 2. 定义出口函数 */
static void __exit sg90_exit(void){   // __exit在驱动编进内核时才起作用，这时就不需要出口函数了   
    /* 2.1 销毁platform_driver */
    platform_driver_unregister(&sg90_driver);
    /* 2.2 销毁类 */
    class_destroy(sg90_class);
    /* 2.3 卸载设备驱动 */
    unregister_chrdev(major, "sg90");   // 参数为"主设备号、驱动名称"
}

/* 3. 注册入口出口函数 */
module_init(sg90_init);     // 注册入口函数，也可以用init_module的方法注册
module_exit(sg90_exit);

MODULE_LICENSE("GPL");
