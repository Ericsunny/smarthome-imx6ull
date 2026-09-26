/*
 * DHT11 温湿度驱动
 *
 * 单总线时序: 主机拉低 >=18ms 发起始信号 -> 释放总线 -> 传感器应答
 * (80us低 + 80us高) -> 连发 40 位数据(每位 = 50us低 + 26~28us高表示0 /
 * 70us高表示1) -> 校验和 = 前4字节之和的低8位
 *
 * 实现上没有用延时采样去挖每一位, 而是 GPIO 配双边沿中断:
 * 中断里只记 ktime_get_ns 时间戳, 收满一帧在 handler 里解码+校验,
 * read 睡在等待队列上等结果。原因: 26us 和 70us 只差几十微秒,
 * 忙等采样受调度抖动影响太大, 中断时间戳是 ns 级精度而且不占 CPU。
 */
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


#define     DHT11_CNT  1
#define     DHT11_NAME "dht11"



struct dht11_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    int data_gpio;
    int irq;
    unsigned char humidity, temperature;
};

struct dht11_dev dht11;

/* ---- 一帧的采集状态, 中断和 read 两边都会碰, 用下面的自旋锁保护 ---- */
static wait_queue_head_t dht11_wait;      /* read 睡在这里, handler 收完唤醒 */
static volatile int dht11_frame_ready;    /* 1 = 一帧收完了(带 volatile, 它会被中断改写) */
static volatile int dht11_frame_err;      /* 1 = 校验没过, read 返回 -EIO */
static int dht11_edge_cnt;                /* 已经收到几个边沿 */
static DEFINE_SPINLOCK(dht11_lock);
static u64 dht11_irq_time[90];            /* 每个边沿的时间戳(ns)。
                                             一帧理论 82 个边沿, 实测恒 83(帧尾多一个),
                                             数组开 90 留裕量, 边界在 handler 里卡住 */
static int dht11_skip_cnt;                /* 要跳过的主机边沿计数, 见 handler 里说明 */


static int dht11_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &dht11;
    return 0;
}

/*
 * 把时间戳数组还原成 5 个字节。
 * 下标从 4 开始: ts[0]~ts[3] 是应答段的 4 个边沿, 数据位从第 5 个边沿起算,
 * 每 2 个边沿还原 1 位。
 */
static void parse_dht11_datas(void)
{
    int i;
	u64 low_time, high_time;
	unsigned char data = 0;    /* 正在拼凑的当前字节 */
	unsigned char datas[5];    /* 40位 = 5字节 */
	int bits = 0;              /* 已获得多少位 */
	int byte = 0;              /* 已获得多少字节 */

	/* 从索引4开始, 每2个边沿对应1位数据 */
	for (i = 4; i < 84; i += 2) {
		high_time = dht11_irq_time[i]   - dht11_irq_time[i-1];
		low_time  = dht11_irq_time[i-1] - dht11_irq_time[i-2];

		/* 低脉冲应约为50us, 放宽到40~60us之间才认为有效。
		 * 这层窗口过滤是第一道防线: 丢过边沿的时间差会大到毫秒级,
		 * 直接被拦在门外, 拦不住的交给最后的校验和兜底 */
		if (low_time > 40000 && low_time < 60000) {
			data <<= 1;
			/* 高脉冲26~28us表示0, 70us表示1, 取中间值50us判断 */
			if (high_time > 50000)
				data |= 1;   /* 得到数据1; 为0时左移即可 */
		}
		bits++;   /* 即使本次低脉冲异常也累加, 简单容错 */

		/* 凑满8位得到一个字节 */
		if (bits == 8) {
			datas[byte++] = data;
			data  = 0;    /* 一切恢复初始状态 */
			bits  = 0;
		}
	}

	/* 校验: 前4字节之和应等于第5字节 */
	if (datas[4] == (unsigned char)(datas[0] + datas[1] +
					datas[2] + datas[3])) {
		dht11.humidity = datas[0];      /* 湿度整数部分(小数部分恒为0, 丢弃) */
		dht11.temperature = datas[2];   /* 温度整数部分 */
        dht11_frame_err = 0;
        dht11_frame_ready = 1;          /* 两个分支都要置 ready, 否则 read 永远超时 */
	} else {
		dht11_frame_err = 1;            /* 校验失败, 让 read 返回 -EIO */
		dht11_frame_ready = 1;
	}
}

/*
 * 双边沿中断, 上半部只做最快的必须动作: 记时间戳。
 *
 * 前两个边沿要跳过: read 里拉低起始脉冲的下降沿、释放总线的上升沿,
 * 都会被自己的中断捕获。这两个边沿不是传感器发的, 不跳过的话整个
 * 时间戳数组错位 2 位, 解码时把低电平时长当高电平判, 校验必败。
 */
static irqreturn_t dht11_irq_handler(int irq, void *dev_id)
{
    if (dht11_skip_cnt < 2) {
        dht11_skip_cnt++;
        return IRQ_HANDLED;
    }

	if (dht11_edge_cnt < 84) {              /* 边界保护, 防数组越界 */
		dht11_irq_time[dht11_edge_cnt] = ktime_get_ns();
		dht11_edge_cnt++;
	} else {

		return IRQ_HANDLED;
	}

	/* 收满 83 个边沿 = 一帧齐了, 现场解码并唤醒 read */
	if (dht11_edge_cnt >= 83) {
		parse_dht11_datas();
        dht11_edge_cnt = 0;
		wake_up_interruptible(&dht11_wait);   /* 唤醒 read */
	}

	return IRQ_HANDLED;
}

/*
 * 一次测量的完整时序:
 *  1. 锁内复位采集状态(irqsave 版, 防复位到一半被自己的中断打断)
 *  2. request_irq —— 必须在起始脉冲之前! 传感器释放总线后 20~40us
 *     就开始应答, 要是先发脉冲再注册中断, 注册耗时会把这段边沿全丢掉,
 *     表现就是 read 全部超时
 *  3. 拉低 18ms(手册要求) -> 切回输入释放总线, 舞台交给传感器
 *  4. 等整帧完成, 100ms 超时兜底: 传感器拔线也不会永远睡;
 *     interruptible 保证 Ctrl+C 能打断
 *  5. free_irq 无条件放在所有 return 之前 —— 超时/被打断/成功三条路
 *     都会经过它, 和 request_irq 严格配对, 不会泄漏中断
 */
static ssize_t dht11_read(struct file *filp, char __user *buf,
			  size_t cnt, loff_t *offt)
{
	int ret;
	unsigned char kern_buf[2];
    unsigned long flags;

	/* 协议约定: 只收 2 字节, 湿度整数 + 温度整数 */
	if (cnt != 2)
		return -EINVAL;

	/* 每次读之前复位采集状态 */
    spin_lock_irqsave(&dht11_lock, flags);
	dht11_edge_cnt = 0;
    dht11_skip_cnt = 0;
    dht11_frame_ready = 0;
    dht11_frame_err = 0;
    spin_unlock_irqrestore(&dht11_lock, flags);

    /*  注册中断: 双边沿触发, 此后引脚变为输入方向 */
	ret = request_irq(dht11.irq, dht11_irq_handler,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "dht11", &dht11);
    if (ret)
        return ret;

	/*  发送18ms低脉冲(起始信号) */
	gpio_direction_output(dht11.data_gpio, 0);
	mdelay(18);

    /*释放总线, 之后到收满一帧之间全靠中断*/
	gpio_direction_input(dht11.data_gpio);

	/* 休眠等一帧: handler 置 frame_ready 后唤醒 */
	ret = wait_event_interruptible_timeout(dht11_wait, dht11_frame_ready,
                                           msecs_to_jiffies(100));

    free_irq(dht11.irq, &dht11);
    if( ret == 0)
    {
        return -ETIMEDOUT;    /* 超时: 传感器没接好或系统太忙丢了边沿 */
    }
    if (ret < 0){
        return ret;           /* 被信号打断(Ctrl+C) */
    }
    if (dht11_frame_err)
        return -EIO;          /* 数据收全了但校验不过 */

	kern_buf[0] = dht11.humidity;     /* 湿度整数 */
	kern_buf[1] = dht11.temperature;  /* 温度整数 */

	/* 拷贝到用户空间 */
	ret = copy_to_user(buf, kern_buf, 2);

	if(ret){
        return -EFAULT;
    }
	return 2;    /* 返回实际字节数, 不能 return 0 */
}

static int dht11_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations dht11_fops ={
    .owner = THIS_MODULE,
    .open = dht11_open,
    .read = dht11_read,
    .release = dht11_release,
};

/*
 * probe 里的顺序有讲究: 先 of_get_named_gpio 拿到编号, 后面
 * gpio_to_irq / gpio_request 才有合法输入。当初这三行写在了解析
 * GPIO 之前, 拿着 0 号去申请, 打印看着正常但中断根本不对。
 */
static int dht11_probe(struct platform_device *pdev)
{
    int ret;


    dht11.nd = pdev->dev.of_node;
    if(dht11.nd == NULL){
        printk("dht11 node not found!\r\n");
        return -EINVAL;
    }
    printk("dht11 node has been found!\r\n");

    dht11.data_gpio = of_get_named_gpio(dht11.nd, "dht11-gpios", 0);
    if(dht11.data_gpio < 0){
        printk("can't get dht11-gpio!\r\n");
        return -EINVAL;
    }
    printk("dht11-gpio num = %d\r\n", dht11.data_gpio);

    dht11.irq = gpio_to_irq(dht11.data_gpio);
    init_waitqueue_head(&dht11_wait);
    gpio_request(dht11.data_gpio, "dht11-data");

    ret = gpio_direction_input(dht11.data_gpio);   /* 空闲态 = 输入 = 释放总线 */
    if(ret < 0){
        printk("gpio direction set failed!\r\n");
        return -EINVAL;
    }


    /* 字符设备五步: 动态设备号 -> cdev -> class/device, /dev/dht11 自动生成 */
    if (dht11.major) {
        dht11.devid = MKDEV(dht11.major, 0);
        register_chrdev_region(dht11.devid, DHT11_CNT,
                              DHT11_NAME);
    } else {
        alloc_chrdev_region(&dht11.devid, 0, DHT11_CNT,
                            DHT11_NAME);
        dht11.major = MAJOR(dht11.devid);
        dht11.minor = MINOR(dht11.devid);
    }
    printk("dht11 major=%d,minor=%d\r\n",dht11.major,
          dht11.minor);

    dht11.cdev.owner = THIS_MODULE;
    cdev_init(&dht11.cdev, &dht11_fops);

    cdev_add(&dht11.cdev, dht11.devid, DHT11_CNT);

    dht11.class = class_create(THIS_MODULE, DHT11_NAME);
    if (IS_ERR(dht11.class)) {
        return PTR_ERR(dht11.class);
    }

    dht11.device = device_create(dht11.class, NULL, dht11.devid,
                                 NULL, DHT11_NAME);
    if(IS_ERR(dht11.device)) {
        return PTR_ERR(dht11.device);
    }

    return 0;
}

/* 和 probe 相反的顺序销毁, 申请和释放一一配对 */
static int dht11_remove(struct platform_device *pdev)
{
    cdev_del(&dht11.cdev);
    unregister_chrdev_region(dht11.devid, DHT11_CNT);

    device_destroy(dht11.class, dht11.devid);
    class_destroy(dht11.class);

    gpio_free(dht11.data_gpio);
    return 0;
}

static const struct of_device_id dht11_of_match[] ={
    {.compatible = "smarthome,dht11"},   /* 和设备树节点逐字符一致, 逗号后不能有空格 */
     { /* sentinel */ }

};
MODULE_DEVICE_TABLE(of,dht11_of_match);

static struct platform_driver dht11_driver = {
    .probe = dht11_probe,
    .remove = dht11_remove,
    .driver = {
        .name = "dht11_plat",
        .of_match_table = dht11_of_match,
    },
};


module_platform_driver(dht11_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sxy");
