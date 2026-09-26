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
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/math64.h>
#include <asm/uaccess.h>

/*
 * HC-SR04 超声波测距
 * 原理: TRIG 发 20us 高脉冲触发, 模块发 8 个 40kHz 声波, ECHO 拉高直到
 * 收到回波 —— 高电平时长就是声波往返时间, 距离 cm = us / 58 (340m/s 往返)。
 * 实现: ECHO 配双边沿中断, 上升沿记时刻、下降沿算脉宽并唤醒;
 * ioctl 里负责触发 + 等待 + 换算, 100ms 没回波当作量程外。
 */

#define HC_SR04_CNT  1
#define HC_SR04_NAME "hc_sr04"

/* ioctl(cmd=0, int*): 返回距离，单位 cm（设备接口协议 v1.1） */
#define HC_SR04_CMD_GET_DIST 0

/* 量程外时 ECHO 高电平最长约 38ms，超时给 100ms 裕量 */
#define HC_SR04_ECHO_TIMEOUT_MS 100

/* 声速 340m/s: 距离(cm) = 回波高电平时间(us) / 58 */
#define HC_SR04_US_PER_CM 58

struct hc_sr04_dev {
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	int major;
	int minor;
	struct device_node *nd;
	int trig_gpio;
	int echo_gpio;
	int irq;

	spinlock_t lock;         /* 保护下面的回波状态量 */
	bool echo_active;        /* ECHO 正处于高电平中 */
	volatile bool echo_done; /* 一发回波完整收完 */
	u64 rise_ns;             /* 上升沿时间戳 */
	u64 pulse_ns;            /* ECHO 高电平时长 */
};

static struct hc_sr04_dev hc_sr04;
static wait_queue_head_t hc_sr04_wait;

/* ECHO 双边沿中断: 上升沿记时刻, 下降沿算脉宽并唤醒读侧 */
static irqreturn_t hc_sr04_irq_handler(int irq, void *dev_id)
{
	struct hc_sr04_dev *dev = dev_id;
	unsigned long flags;
	u64 now = ktime_get_ns();
	int level = gpio_get_value(dev->echo_gpio);

	spin_lock_irqsave(&dev->lock, flags);
	if (level) {                    /* 上升沿: 回波开始 */
		dev->rise_ns = now;
		dev->echo_active = true;
	} else if (dev->echo_active) {  /* 下降沿: 回波结束 */
		dev->echo_active = false;
		dev->pulse_ns = now - dev->rise_ns;
		dev->echo_done = true;
		wake_up_interruptible(&hc_sr04_wait);
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

static int hc_sr04_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &hc_sr04;
	return 0;
}

static long hc_sr04_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct hc_sr04_dev *dev = filp->private_data;
	unsigned long flags;
	u64 pulse_ns;
	int distance;
	int ret;

	if (cmd != HC_SR04_CMD_GET_DIST)
		return -EINVAL;

	/* 1. 复位上次残留的回波状态 */
	spin_lock_irqsave(&dev->lock, flags);
	dev->echo_active = false;
	dev->echo_done = false;
	spin_unlock_irqrestore(&dev->lock, flags);

	/* 2. TRIG 拉高 20us 再拉低, 触发一次测距(手册要求 ≥10us) */
	gpio_set_value(dev->trig_gpio, 1);
	udelay(20);
	gpio_set_value(dev->trig_gpio, 0);

	/* 3. 等中断收完整条回波(IRQ 在 probe 里常驻, 先注册后触发) */
	ret = wait_event_interruptible_timeout(hc_sr04_wait, dev->echo_done,
			msecs_to_jiffies(HC_SR04_ECHO_TIMEOUT_MS));
	if (ret == 0)
		return -ETIMEDOUT;   /* 超时: 量程外/没接好 */
	if (ret < 0)
		return ret;          /* 被信号打断 */

	spin_lock_irqsave(&dev->lock, flags);
	pulse_ns = dev->pulse_ns;
	spin_unlock_irqrestore(&dev->lock, flags);

	/* 4. 换算距离并拷回用户空间
	 * cm = us/58 = ns/58000。u64 除法要用内核的 div_u64 —— 直接写
	 * pulse_ns/1000 编译出来会调 __aeabi_uldivmod, 老内核没导出这个
	 * 符号, insmod 会报 Unknown symbol */
	distance = (int)div_u64(pulse_ns, 1000 * HC_SR04_US_PER_CM);
	if (distance < 2)
		distance = 2;        /* 模块盲区 2cm */

	if (put_user(distance, (int __user *)arg))
		return -EFAULT;
	return 0;
}

static int hc_sr04_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static struct file_operations hc_sr04_fops = {
	.owner = THIS_MODULE,
	.open = hc_sr04_open,
	.unlocked_ioctl = hc_sr04_ioctl,
	.release = hc_sr04_release,
};

static int hc_sr04_probe(struct platform_device *pdev)
{
	int ret;

	hc_sr04.nd = pdev->dev.of_node;
	if (hc_sr04.nd == NULL) {
		printk("hc_sr04 node not found!\r\n");
		return -EINVAL;
	}
	printk("hc_sr04 node has been found!\r\n");

	hc_sr04.trig_gpio = of_get_named_gpio(hc_sr04.nd, "trig-gpios", 0);
	if (hc_sr04.trig_gpio < 0) {
		printk("can't get trig-gpio!\r\n");
		return -EINVAL;
	}
	printk("hc_sr04 trig-gpio num = %d\r\n", hc_sr04.trig_gpio);

	hc_sr04.echo_gpio = of_get_named_gpio(hc_sr04.nd, "echo-gpios", 0);
	if (hc_sr04.echo_gpio < 0) {
		printk("can't get echo-gpio!\r\n");
		return -EINVAL;
	}
	printk("hc_sr04 echo-gpio num = %d\r\n", hc_sr04.echo_gpio);

	ret = gpio_request(hc_sr04.trig_gpio, "hc_sr04-trig");
	if (ret) {
		printk("trig gpio request failed!\r\n");
		return ret;
	}
	ret = gpio_request(hc_sr04.echo_gpio, "hc_sr04-echo");
	if (ret) {
		printk("echo gpio request failed!\r\n");
		gpio_free(hc_sr04.trig_gpio);
		return ret;
	}

	ret = gpio_direction_output(hc_sr04.trig_gpio, 0); /* TRIG 空闲为低 */
	if (ret)
		goto free_gpio;
	ret = gpio_direction_input(hc_sr04.echo_gpio);
	if (ret)
		goto free_gpio;

	spin_lock_init(&hc_sr04.lock);
	init_waitqueue_head(&hc_sr04_wait);

	hc_sr04.irq = gpio_to_irq(hc_sr04.echo_gpio);
	ret = request_irq(hc_sr04.irq, hc_sr04_irq_handler,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "hc_sr04", &hc_sr04);
	if (ret) {
		printk("request_irq failed!\r\n");
		goto free_gpio;
	}

	if (hc_sr04.major) {
		hc_sr04.devid = MKDEV(hc_sr04.major, 0);
		register_chrdev_region(hc_sr04.devid, HC_SR04_CNT,
				       HC_SR04_NAME);
	} else {
		alloc_chrdev_region(&hc_sr04.devid, 0, HC_SR04_CNT,
				    HC_SR04_NAME);
		hc_sr04.major = MAJOR(hc_sr04.devid);
		hc_sr04.minor = MINOR(hc_sr04.devid);
	}
	printk("hc_sr04 major=%d,minor=%d\r\n", hc_sr04.major,
	       hc_sr04.minor);

	hc_sr04.cdev.owner = THIS_MODULE;
	cdev_init(&hc_sr04.cdev, &hc_sr04_fops);
	cdev_add(&hc_sr04.cdev, hc_sr04.devid, HC_SR04_CNT);

	hc_sr04.class = class_create(THIS_MODULE, HC_SR04_NAME);
	if (IS_ERR(hc_sr04.class)) {
		ret = PTR_ERR(hc_sr04.class);
		goto free_irq;
	}

	hc_sr04.device = device_create(hc_sr04.class, NULL, hc_sr04.devid,
				       NULL, HC_SR04_NAME);
	if (IS_ERR(hc_sr04.device)) {
		ret = PTR_ERR(hc_sr04.device);
		goto free_class;
	}

	return 0;

free_class:
	class_destroy(hc_sr04.class);
free_irq:
	free_irq(hc_sr04.irq, &hc_sr04);
free_gpio:
	gpio_free(hc_sr04.echo_gpio);
	gpio_free(hc_sr04.trig_gpio);
	return ret;
}

static int hc_sr04_remove(struct platform_device *pdev)
{
	device_destroy(hc_sr04.class, hc_sr04.devid);
	class_destroy(hc_sr04.class);
	cdev_del(&hc_sr04.cdev);
	unregister_chrdev_region(hc_sr04.devid, HC_SR04_CNT);
	free_irq(hc_sr04.irq, &hc_sr04);
	gpio_free(hc_sr04.echo_gpio);
	gpio_free(hc_sr04.trig_gpio);
	return 0;
}

static const struct of_device_id hc_sr04_of_match[] = {
	{ .compatible = "smarthome,hc-sr04" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hc_sr04_of_match);

static struct platform_driver hc_sr04_driver = {
	.probe = hc_sr04_probe,
	.remove = hc_sr04_remove,
	.driver = {
		.name = "hc_sr04_plat",
		.of_match_table = hc_sr04_of_match,
	},
};

module_platform_driver(hc_sr04_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sxy");
