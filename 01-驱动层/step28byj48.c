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
#include <linux/mutex.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

/*
 * 28BYJ48 步进电机(经 ULN2003 驱动线圈) —— 四相八拍
 * 每拍给 IN1~IN4 一组通电组合, 转子跟着磁场走一步; 顺着表走是正转,
 * 倒着走是反转。半步八拍 {1,3,2,6,4,C,8,9}, 4096 拍 = 输出轴一圈
 * (内部 1:64 齿轮箱)。拍间隔 2ms, 用 usleep_range 保证精度。
 */

#define STEP_CNT  1
#define STEP_NAME "step28byj48"

/* ioctl(_IOW('T',1,int)): 带符号步数, 正=正转(开窗帘), 负=反转(关窗帘), 走完才返回 */
#define STEP_CMD_RUN _IOW('T', 1, int)

/* 单次步数上限(防呆): 8192 拍 = 两圈 */
#define STEP_MAX_BEATS 8192

/* 每拍 2ms: 半步 8 拍, 一圈 4096 拍约 8 秒。
 * 必须用 usleep_range(走 hrtimer, 本内核 HIGH_RES_TIMERS=y);
 * msleep(2) 粒度受 HZ=100 限制实睡 10ms, 一圈要 41 秒 */
#define STEP_BEAT_MIN_US 2000
#define STEP_BEAT_MAX_US 2200

/* 半步 8 拍序列, bit0~bit3 = IN1~IN4 (ULN2003 依次给线圈通电) */
static const int step_seq[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};

struct step_dev {
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	int major;
	int minor;
	struct device_node *nd;
	int gpio[4];        /* IN1~IN4 对应的 GPIO 编号 */
	struct mutex lock;  /* 串行化步进: 两个进程同时下发会打乱拍子 */
};

static struct step_dev step;

/* 把 4 位图案写到 IN1~IN4 */
static void step_write_pattern(int pattern)
{
	int i;

	for (i = 0; i < 4; i++)
		gpio_set_value(step.gpio[i], (pattern >> i) & 0x1);
}

static int step_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &step;
	return 0;
}

static long step_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int steps, cnt, i, idx = 0;
	int ret;

	if (cmd != STEP_CMD_RUN)
		return -EINVAL;

	/* 参数在用户空间, arg 是地址, 必须 get_user 取值 */
	if (get_user(steps, (int __user *)arg))
		return -EFAULT;
	if (steps == 0)
		return 0;
	if (steps > STEP_MAX_BEATS || steps < -STEP_MAX_BEATS)
		return -EINVAL;

	cnt = (steps > 0) ? steps : -steps;

	ret = mutex_lock_interruptible(&step.lock);
	if (ret)
		return ret;

	for (i = 0; i < cnt; i++) {
		/* 长循环里检查信号, 让 Ctrl+C 能打断正在走的步进 */
		if (signal_pending(current)) {
			step_write_pattern(0);
			mutex_unlock(&step.lock);
			return -ERESTARTSYS;
		}
		/* 正转下标 +1; 反转用 +7 不用 -1 —— C 的 % 结果跟着被除数走,
		 * idx=0 时 -1%8 = -1, 拿去当数组下标就越界了 */
		idx = (idx + ((steps > 0) ? 1 : 7)) % 8;
		step_write_pattern(step_seq[idx]);
		usleep_range(STEP_BEAT_MIN_US, STEP_BEAT_MAX_US);
	}

	/* 收尾全部拉低 = ULN2003 全关 = 绕组断电(防长期单相通电发热) */
	step_write_pattern(0);
	mutex_unlock(&step.lock);
	return 0;
}

static int step_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static struct file_operations step_fops = {
	.owner = THIS_MODULE,
	.open = step_open,
	.unlocked_ioctl = step_ioctl,
	.release = step_release,
};

static int step_probe(struct platform_device *pdev)
{
	int ret, i;

	step.nd = pdev->dev.of_node;
	if (step.nd == NULL) {
		printk("step28byj48 node not found!\r\n");
		return -EINVAL;
	}
	printk("step28byj48 node has been found!\r\n");

	/* 取 IN1~IN4 四个 GPIO(设备树 in-gpios 数组, 下标 0~3) */
	for (i = 0; i < 4; i++) {
		step.gpio[i] = of_get_named_gpio(step.nd, "in-gpios", i);
		if (step.gpio[i] < 0) {
			printk("can't get in-gpios[%d]!\r\n", i);
			return -EINVAL;
		}
		printk("step28byj48 IN%d gpio num = %d\r\n", i + 1,
		       step.gpio[i]);
	}

	/* 逐个申请, 失败回滚已申请的 */
	for (i = 0; i < 4; i++) {
		ret = gpio_request(step.gpio[i], "step28byj48");
		if (ret) {
			printk("gpio[%d] request failed!\r\n", i);
			while (--i >= 0)
				gpio_free(step.gpio[i]);
			return ret;
		}
	}

	/* 初始全 0 = ULN2003 全关 = 电机断电态 */
	for (i = 0; i < 4; i++) {
		ret = gpio_direction_output(step.gpio[i], 0);
		if (ret) {
			while (--i >= 0)
				gpio_free(step.gpio[i]);
			goto fail_free_all;
		}
	}

	mutex_init(&step.lock);

	if (step.major) {
		step.devid = MKDEV(step.major, 0);
		register_chrdev_region(step.devid, STEP_CNT, STEP_NAME);
	} else {
		alloc_chrdev_region(&step.devid, 0, STEP_CNT, STEP_NAME);
		step.major = MAJOR(step.devid);
		step.minor = MINOR(step.devid);
	}
	printk("step28byj48 major=%d,minor=%d\r\n", step.major, step.minor);

	step.cdev.owner = THIS_MODULE;
	cdev_init(&step.cdev, &step_fops);
	cdev_add(&step.cdev, step.devid, STEP_CNT);

	step.class = class_create(THIS_MODULE, STEP_NAME);
	if (IS_ERR(step.class)) {
		ret = PTR_ERR(step.class);
		goto fail_unreg;
	}

	step.device = device_create(step.class, NULL, step.devid, NULL,
				    STEP_NAME);
	if (IS_ERR(step.device)) {
		ret = PTR_ERR(step.device);
		goto fail_class;
	}

	return 0;

fail_class:
	class_destroy(step.class);
fail_unreg:
	cdev_del(&step.cdev);
	unregister_chrdev_region(step.devid, STEP_CNT);
fail_free_all:
	for (i = 0; i < 4; i++)
		gpio_free(step.gpio[i]);
	return ret;
}

static int step_remove(struct platform_device *pdev)
{
	int i;

	device_destroy(step.class, step.devid);
	class_destroy(step.class);
	cdev_del(&step.cdev);
	unregister_chrdev_region(step.devid, STEP_CNT);
	step_write_pattern(0);
	for (i = 0; i < 4; i++)
		gpio_free(step.gpio[i]);
	return 0;
}

static const struct of_device_id step_of_match[] = {
	{ .compatible = "smarthome,step28byj48" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, step_of_match);

static struct platform_driver step_driver = {
	.probe = step_probe,
	.remove = step_remove,
	.driver = {
		.name = "step28byj48_plat",
		.of_match_table = step_of_match,
	},
};

module_platform_driver(step_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sxy");
