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


#define     GPIOLED_CNT  1
#define     GPIOLED_NAME "led"
#define     LEDOFF      0
#define     LEDON       1



struct gpioled_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    int led_gpio;
};

struct gpioled_dev gpioled;




static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &gpioled;
    return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf,
                        size_t cnt, loff_t *offt)
{
    return 0;
}

static ssize_t led_write(struct file *filp, const char __user *buf,
                        size_t cnt, loff_t *offt)
{
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;
    struct gpioled_dev *dev = filp->private_data;

    retvalue = copy_from_user(databuf, buf, cnt);
    if(retvalue < 0){
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    ledstat = databuf[0];

    if (ledstat == LEDON){
        gpio_set_value(dev->led_gpio, 0);
    }else if(ledstat == LEDOFF){
        gpio_set_value(dev->led_gpio, 1);
    }
    return cnt;
}

static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations gpioled_fops ={
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

static int led_probe(struct platform_device *pdev)
{
    int ret;

    gpioled.nd = pdev->dev.of_node;
    if(gpioled.nd == NULL){
        printk("gpioled node not found!\r\n");
        return -EINVAL;
    }
    printk("gpioled node has been found!\r\n");

    gpioled.led_gpio = of_get_named_gpio(gpioled.nd, "led-gpio", 0);
    if(gpioled.led_gpio < 0){
        printk("can't get led-gpio!\r\n");
        return -EINVAL;
    }
    printk("led-gpio num = %d\r\n", gpioled.led_gpio);

    ret = gpio_direction_output(gpioled.led_gpio, 1);
    if(ret < 0){
        printk("gpio direction set failed!\r\n");
        return -EINVAL;
    }


    if (gpioled.major) {
        gpioled.devid = MKDEV(gpioled.major, 0);
        register_chrdev_region(gpioled.devid, GPIOLED_CNT,
                                GPIOLED_NAME);
    } else {
        alloc_chrdev_region(&gpioled.devid, 0, GPIOLED_CNT,
                            GPIOLED_NAME);
        gpioled.major = MAJOR(gpioled.devid);
        gpioled.minor = MINOR(gpioled.devid);
    }
    printk("gpioled major=%d,minor=%d\r\n",gpioled.major,
          gpioled.minor);

    gpioled.cdev.owner = THIS_MODULE;
    cdev_init(&gpioled.cdev, &gpioled_fops);

    cdev_add(&gpioled.cdev, gpioled.devid, GPIOLED_CNT);

    gpioled.class = class_create(THIS_MODULE, GPIOLED_NAME);
    if (IS_ERR(gpioled.class)) {
        return PTR_ERR(gpioled.class);
    }

    gpioled.device = device_create(gpioled.class, NULL, gpioled.devid,
                                 NULL, GPIOLED_NAME);
    if(IS_ERR(gpioled.device)) {
        return PTR_ERR(gpioled.device);
    }

    return 0;
}

static int led_remove(struct platform_device *pdev)
{
    

    cdev_del(&gpioled.cdev);
    unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);

    device_destroy(gpioled.class, gpioled.devid);
    class_destroy(gpioled.class);

    gpio_set_value(gpioled.led_gpio, 1);
    cdev_del(&gpioled.cdev);
    unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);
    device_destroy(gpioled.class, gpioled.devid);
    class_destroy(gpioled.class);
    return 0;
}

static const struct of_device_id gpioled_of_match[] ={
    {.compatible = "atkalpha-gpioled"},
     { /* sentinel */ }

};
MODULE_DEVICE_TABLE(of,gpioled_of_match);

static struct platform_driver gpioled_driver = {
    .probe = led_probe,
    .remove = led_remove,
    .driver = {
        .name = "gpioled_plat",
        .of_match_table = gpioled_of_match,
    },
};


module_platform_driver(gpioled_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sxy");