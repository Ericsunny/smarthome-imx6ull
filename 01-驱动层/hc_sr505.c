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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/poll.h>



#define     key_CNT  1
#define     KEY_NAME     "hc_sr505"




struct hc_sr505_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    int key_gpio;
    int irqnum;
    int state;
    int event;
    spinlock_t lock;
    wait_queue_head_t r_wait;
};

struct hc_sr505_dev key;




static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &key;
    return 0;
}

/*
 * 阻塞读。SR505 检测到人 OUT 拉高保持约 8 秒后自动回低, 每次电平变化
 * 都触发中断。"事件"和"状态"分开存:
 *   event = 有没有被读走的新事件(睡眠条件, 读走清零)
 *   state = 当前电平(0 无人 / 1 有人)
 * 只存 state 不存 event 的话, 读走一次 YES 之后就没人再唤醒 read 了。
 */
static ssize_t hc_sr505_read(struct file *filp, char __user *buf,
                        size_t cnt, loff_t *offt)
{
    struct hc_sr505_dev *dev = filp->private_data;
    unsigned long flags;
    int value,ret;

    if (cnt < sizeof(int))
        return -EINVAL;


    if (filp->f_flags & O_NONBLOCK){
        /* 非阻塞: 没有新事件直接 -EAGAIN, 让 APP 自己决定要不要重试 */
        if (!dev->event)
        return -EAGAIN;
    } else {
        /* 阻塞: 睡在等待队列上等中断唤醒, 不占 CPU */
        ret = wait_event_interruptible(dev->r_wait, dev->event != 0);
        if (ret)
            return ret;
    }

    /* 锁内只取值+清标志; copy_to_user 放锁外 —— 拷用户空间可能睡, 不能持自旋锁 */
    spin_lock_irqsave(&dev->lock, flags);

    value = dev->state;
    dev->event = 0;
    spin_unlock_irqrestore(&dev->lock, flags);

    if(copy_to_user(buf, &value, sizeof(value)))
        return -EFAULT;

    return sizeof(value);
}



static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}


static struct file_operations key_fops ={
    .owner = THIS_MODULE,
    .open = led_open,
    .read = hc_sr505_read,
    .release = led_release,
};



/* 中断上半部: 读电平 -> 记状态+置事件 -> 唤醒 read。
 * SR505 是模块化输出(内部已处理), 没有机械抖动, 不需要按键那套定时器消抖 */
static irqreturn_t hc_sr505_handler(int irq, void *dev_id)
{
    struct hc_sr505_dev *dev = dev_id;
    unsigned long flags;
    int level;

    level = gpio_get_value(dev->key_gpio);

    spin_lock_irqsave(&dev->lock, flags);
    dev->state = level;
    dev->event = 1;
    spin_unlock_irqrestore(&dev->lock, flags);

    wake_up_interruptible(&dev->r_wait);
    return IRQ_HANDLED;
}

static int hc_sr505_probe(struct platform_device *pdev)
{
    int ret; 

    init_waitqueue_head(&key.r_wait);
    key.nd = pdev->dev.of_node;
    if(key.nd == NULL){
        printk("key node not found!\r\n");
        return -EINVAL;
    }
    printk("hc_sr505 node has been found!\r\n");

    key.key_gpio = of_get_named_gpio(key.nd, "out-gpios", 0);
    if(key.key_gpio < 0){
        printk("can't get out-gpios!\r\n");
        return -EINVAL;
    }
    printk("hc_sr505 gpio num = %d\r\n", key.key_gpio);

    ret = gpio_request(key.key_gpio, "key0");
    if (ret) {
        printk("gpio_request failed!\r\n");
        return ret;
    }
    ret = gpio_direction_input(key.key_gpio);
    if(ret < 0){
        printk("gpio direction set failed!\r\n");
        return ret;
    }

    key.state = 0;
    key.event = 0;
    spin_lock_init(&key.lock);

    key.irqnum = gpio_to_irq(key.key_gpio);
    if (key.irqnum < 0){
        printk("gpio_to_irq failed!\r\n");
        return key.irqnum;
    }
    
    ret =  request_irq(key.irqnum, hc_sr505_handler,
        IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
        "hc_sr505", &key);
    if(ret) {
        printk("request_irq failed!\r\n");
        return ret;
    }

    if (key.major) {
        key.devid = MKDEV(key.major, 0);
        register_chrdev_region(key.devid, key_CNT,
                                KEY_NAME);
    } else {
        alloc_chrdev_region(&key.devid, 0, key_CNT,
                            KEY_NAME);
        key.major = MAJOR(key.devid);
        key.minor = MINOR(key.devid);
    }
    printk("hc_sr505 major=%d,minor=%d\r\n",key.major,
          key.minor);

    key.cdev.owner = THIS_MODULE;
    cdev_init(&key.cdev, &key_fops);

    cdev_add(&key.cdev, key.devid, key_CNT);

    key.class = class_create(THIS_MODULE, KEY_NAME);
    if (IS_ERR(key.class)) {
        return PTR_ERR(key.class);
    }

    key.device = device_create(key.class, NULL, key.devid,
                                 NULL, KEY_NAME);
    if(IS_ERR(key.device)) {
        return PTR_ERR(key.device);
    }

    return 0;
}

static int key_remove(struct platform_device *pdev)
{
    free_irq(key.irqnum, &key);

    gpio_free(key.key_gpio);

    cdev_del(&key.cdev);
    unregister_chrdev_region(key.devid, key_CNT);
    
    device_destroy(key.class, key.devid);
    class_destroy(key.class);

    return 0;
}

static const struct of_device_id key_of_match[] ={
    {.compatible = "smarthome,hc-sr505"},
     { /* sentinel */ }

};
MODULE_DEVICE_TABLE(of,key_of_match);

static struct platform_driver key_driver = {
    .probe = hc_sr505_probe,
    .remove = key_remove,
    .driver = {
        .name = "key_plat",
        .of_match_table = key_of_match,
    },
};


module_platform_driver(key_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sxy");