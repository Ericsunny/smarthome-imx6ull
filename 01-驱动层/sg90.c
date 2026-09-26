#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pwm.h> 


#define     sg90_CNT  1
#define     SG90_NAME "sg90"
#define     SG90_PERIOD_NS  20000000 
#define     SG90_MIN_DUTY   500000 
#define     SG90_MAX_DUTY   2500000
#define     SG90_CMD_SET_ANGLE  _IOW('S', 1, int)

/*
 * SG90 舵机: PWM 50Hz(周期 20ms), 脉宽 0.5ms~2.5ms 线性对应 0~180°。
 * 应用层通过 ioctl 下发角度, 驱动只负责把角度换算成占空比。
 */



struct sg90_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    struct pwm_device *pwm;
};

struct sg90_dev sg90;


/* 角度 -> 脉宽: 500us + 角度/180 * 2000us, 单位 ns。
 * PWM 外设自动按周期重复这个波形, CPU 设置完就可以走人 */
static void sg90_set_angle(struct sg90_dev *dev, int angle)
{
    int duty_ns;
    if (angle < 0) angle =  0;
    if (angle > 180) angle = 180;
    duty_ns = SG90_MIN_DUTY + angle * (SG90_MAX_DUTY - SG90_MIN_DUTY) / 180;
    pwm_config(dev->pwm, duty_ns, SG90_PERIOD_NS);
}

static int sg90_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &sg90;
    return 0;
}

static int sg90_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static long sg90_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int angle;
    switch (cmd) {
    case SG90_CMD_SET_ANGLE:
        /* arg 是用户空间的地址, 必须 get_user 把值取进来。
           直接 (int)arg 当数值用的话拿到的是个指针, 角度永远算错 */
        if (get_user(angle,(int __user *)arg))
            return -EFAULT;
        sg90_set_angle(&sg90, angle);
        break;
    default:
        return -EINVAL;    
    }
    return 0;
}

static struct file_operations sg90_fops ={
    .owner = THIS_MODULE,
    .open = sg90_open,
    .release = sg90_release,
    .unlocked_ioctl = sg90_ioctl,
};

static int sg90_probe(struct platform_device *pdev)
{
    sg90.nd = pdev->dev.of_node;
    if(sg90.nd == NULL){
        printk("sg90 node not found!\r\n");
        return -EINVAL;
    }
    printk("sg90 node has been found!\r\n");

    /* 向 PWM 子系统申请通道(devm 前缀 = 随平台自动释放, 不用手动 put),
     * probe 里先转到 90° 居中位再使能输出 */
    sg90.pwm = devm_pwm_get(&pdev->dev, NULL);
    if(IS_ERR(sg90.pwm)){
        printk("pwm get failed!\r\n");
        return PTR_ERR(sg90.pwm);
    }
    pwm_config(sg90.pwm, 1500000, SG90_PERIOD_NS);   /* 1.5ms = 90° */
    pwm_enable(sg90.pwm);



    if (sg90.major) {
        sg90.devid = MKDEV(sg90.major, 0);
        register_chrdev_region(sg90.devid, sg90_CNT,
                                SG90_NAME);
    } else {
        alloc_chrdev_region(&sg90.devid, 0, sg90_CNT,
                            SG90_NAME);
        sg90.major = MAJOR(sg90.devid);
        sg90.minor = MINOR(sg90.devid);
    }
    printk("sg90 major=%d,minor=%d\r\n",sg90.major,
          sg90.minor);

    sg90.cdev.owner = THIS_MODULE;
    cdev_init(&sg90.cdev, &sg90_fops);

    cdev_add(&sg90.cdev, sg90.devid, sg90_CNT);

    sg90.class = class_create(THIS_MODULE, SG90_NAME);
    if (IS_ERR(sg90.class)) {
        return PTR_ERR(sg90.class);
    }

    sg90.device = device_create(sg90.class, NULL, sg90.devid,
                                 NULL, SG90_NAME);
    if(IS_ERR(sg90.device)) {
        return PTR_ERR(sg90.device);
    }

    return 0;
}

static int sg90_remove(struct platform_device *pdev)
{
    
    pwm_disable(sg90.pwm);
    cdev_del(&sg90.cdev);
    unregister_chrdev_region(sg90.devid, sg90_CNT);

    device_destroy(sg90.class, sg90.devid);
    class_destroy(sg90.class);
    return 0;
}

static const struct of_device_id sg90_of_match[] ={
    {.compatible = "smarthome,sg90"},
     { /* sentinel */ }

};
MODULE_DEVICE_TABLE(of,sg90_of_match);

static struct platform_driver sg90_driver = {
    .probe = sg90_probe,
    .remove = sg90_remove,
    .driver = {
        .name = "sg90_plat",
        .of_match_table = sg90_of_match,
    },
};


module_platform_driver(sg90_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sxy");