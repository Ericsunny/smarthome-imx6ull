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


#define     beep_CNT  1
#define     BEEP_NAME "beep"
#define     BEEPOFF      0
#define     BEEPON       1



struct beep_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    int beep_gpio;
};

struct beep_dev beep;




static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &beep;
    return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf,
                        size_t cnt, loff_t *offt)
{
    return 0;
}

static ssize_t beep_write(struct file *filp, const char __user *buf,
                        size_t cnt, loff_t *offt)
{
    int retvalue;
    unsigned char databuf[1];
    unsigned char beepstat;
    struct beep_dev *dev = filp->private_data;

    retvalue = copy_from_user(databuf, buf, cnt);
    if(retvalue < 0){
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    beepstat = databuf[0];

    if (beepstat == BEEPON){
        gpio_set_value(dev->beep_gpio, 0);
    }else if(beepstat == BEEPOFF){
        gpio_set_value(dev->beep_gpio, 1);
    }
    return cnt;
}

static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations beep_fops ={
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = beep_write,
    .release = led_release,
};

static int led_probe(struct platform_device *pdev)
{
    int ret;

    beep.nd = pdev->dev.of_node;
    if(beep.nd == NULL){
        printk("beep node not found!\r\n");
        return -EINVAL;
    }
    printk("beep node has been found!\r\n");

    beep.beep_gpio = of_get_named_gpio(beep.nd, "beep-gpios", 0);
    if(beep.beep_gpio < 0){
        printk("can't get led-gpio!\r\n");
        return -EINVAL;
    }
    printk("led-gpio num = %d\r\n", beep.beep_gpio);

    ret = gpio_direction_output(beep.beep_gpio, 1);
    if(ret < 0){
        printk("gpio direction set failed!\r\n");
        return -EINVAL;
    }


    if (beep.major) {
        beep.devid = MKDEV(beep.major, 0);
        register_chrdev_region(beep.devid, beep_CNT,
                                BEEP_NAME);
    } else {
        alloc_chrdev_region(&beep.devid, 0, beep_CNT,
                            BEEP_NAME);
        beep.major = MAJOR(beep.devid);
        beep.minor = MINOR(beep.devid);
    }
    printk("beep major=%d,minor=%d\r\n",beep.major,
          beep.minor);

    beep.cdev.owner = THIS_MODULE;
    cdev_init(&beep.cdev, &beep_fops);

    cdev_add(&beep.cdev, beep.devid, beep_CNT);

    beep.class = class_create(THIS_MODULE, BEEP_NAME);
    if (IS_ERR(beep.class)) {
        return PTR_ERR(beep.class);
    }

    beep.device = device_create(beep.class, NULL, beep.devid,
                                 NULL, BEEP_NAME);
    if(IS_ERR(beep.device)) {
        return PTR_ERR(beep.device);
    }

    return 0;
}

static int led_remove(struct platform_device *pdev)
{
    

    cdev_del(&beep.cdev);
    unregister_chrdev_region(beep.devid, beep_CNT);

    device_destroy(beep.class, beep.devid);
    class_destroy(beep.class);

    gpio_set_value(beep.beep_gpio, 1);
    cdev_del(&beep.cdev);
    unregister_chrdev_region(beep.devid, beep_CNT);
    device_destroy(beep.class, beep.devid);
    class_destroy(beep.class);
    return 0;
}

static const struct of_device_id beep_of_match[] ={
    {.compatible = "smarthome,beep"},
     { /* sentinel */ }

};
MODULE_DEVICE_TABLE(of,beep_of_match);

static struct platform_driver beep_driver = {
    .probe = led_probe,
    .remove = led_remove,
    .driver = {
        .name = "beep_plat",
        .of_match_table = beep_of_match,
    },
};


module_platform_driver(beep_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sxy");