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
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>

#define AP3216C_CNT 1
#define AP3216C_NAME "ap3216c"

#define AP3216C_SYSTEMCONG   0x00   
#define AP3216C_IRDATALOW    0x0A   
#define AP3216C_IRDATAHIGH   0x0B   
#define AP3216C_ALSDATALOW   0x0C   
#define AP3216C_ALSDATAHIGH  0x0D   
#define AP3216C_PSDATALOW    0x0E   
#define AP3216C_PSDATAHIGH   0x0F

struct ap3216c_dev {
    void *private_data;
    unsigned short  ir,als, ps;
    struct miscdevice miscdev;
};


static struct ap3216c_dev ap3216c;

/*
 * I2C 随机读的标准两段式:
 *   msg[0] 写寄存器号(flags=0 表示写) —— 把芯片内部的地址指针拨过去
 *   msg[1] 置 I2C_M_RD 读数据
 * 两段之间控制器自动插 repeated start, 中间不让出总线。
 * i2c_transfer 返回成功提交的条数, 这里必须 == 2。
 */
static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *val, int len)
{
    struct i2c_client *client = (struct i2c_client *)dev->private_data;
    struct i2c_msg msg[2];
    int ret;

    msg[0].addr = client->addr;
    msg[0].flags = 0;
    msg[0].buf = &reg;
    msg[0].len = 1;
    msg[1].addr = client->addr;
    msg[1].flags = I2C_M_RD;
    msg[1].buf = val;
    msg[1].len = len;
    
    ret =  i2c_transfer(client->adapter, msg, 2);
    if(ret != 2){
        printk("i2c read failed!\r\n");
        return -EREMOTEIO;
    }
    return 0;
}

/* 写不用分两条 msg: 首字节是寄存器号, 后面直接跟数据载荷, 一条发完 */
static int ap3216c_write_regs(struct ap3216c_dev *dev, u8 reg, u8 *buf, u8 len)
{
    u8 b[256];
    struct i2c_msg msg;
    struct i2c_client *client = (struct i2c_client *)dev->private_data;
    int ret;

    b[0] = reg;
    memcpy(&b[1], buf, len);
    msg.addr = client->addr;
    msg.flags = 0;
    msg.buf = b;
    msg.len = len + 1;
    
    ret = i2c_transfer(client->adapter, &msg, 1);
    if (ret != 1){
        printk("i2c write faile!\r\n");
        return -EREMOTEIO;
    }
    return 0;
}

/*
 * 读 6 个数据寄存器(0x0A~0x0F), 再按数据手册的位定义拼成三个值。
 * 注意 IR/PS 的低位字节里混着标志位, 拼之前要掩掉:
 *   IR: buf[0] 的 bit7 是溢出标志, 只有 bit[1:0] 是数据, 有效值 10 位
 *   ALS: 16 位, 高字节在 0x0D 低字节在 0x0C
 *   PS: buf[4] 的 bit6 是物体靠近标志, 数据在 bit[3:0] 和 buf[5] 的 bit[5:0]
 * 溢出/无效时直接给 0, 不把标志位混进数值里。
 */
static void ap3216c_readdata(struct ap3216c_dev *dev)
{
    unsigned char i = 0;
    unsigned char buf[6];

    for (i = 0; i < 6; i++)
        ap3216c_read_regs(dev, AP3216C_IRDATALOW + i, &buf[i], 1);

    if(buf[0] & 0x80)
        dev->ir = 0;
    else    
        dev->ir = ((unsigned short)buf[1] << 2) | (buf[0] & 0x03);
    
    dev->als = ((unsigned short)buf[3] << 8) | buf[2];

    if (buf[4] & 0x40)
        dev->ps = 0;
    else
        dev->ps = ((unsigned short)(buf[5] & 0x3F) << 4) | (buf[4] & 0x0F);
}

static int ap3216c_open(struct inode *inode, struct file *filp)
{
    unsigned char value = 0x03;
    filp->private_data = &ap3216c;

    /* AP3216C 上电默认掉电模式, 每次打开重写系统配置: 0x03 = 开 ALS + PS + IR。
     * 放在 open 而不是 probe, 芯片重新上电后重新 open 一次就能自愈 */
    ap3216c_write_regs(&ap3216c, AP3216C_SYSTEMCONG, &value, 1);
    return 0;
}

static ssize_t ap3216c_read(struct file *filp, char __user *buf, 
    size_t cnt, loff_t *off)
{
    struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data;
    unsigned short data[3];
   
    ap3216c_readdata(dev);

    data[0] = dev->ir;
    data[1] = dev->als;
    data[2] = dev->ps;

    if(copy_to_user(buf, data, sizeof(data))) 
        return -EFAULT;
    return sizeof(data);
}

static int ap3216c_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations ap3216c_fops = {
    .owner = THIS_MODULE,
    .open = ap3216c_open,
    .read = ap3216c_read,
    .release = ap3216c_release,
};

static const struct of_device_id ap3216c_of_match[] = {
    { .compatible = "ap3216c"},
    { /*sentinel*/}
};

static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;

    ap3216c.private_data = client;   /* i2c_client 里带着总线和从机地址, 读写全靠它 */

    /* misc 设备一步注册: 主设备号固定 10, 次号动态分配, /dev/ap3216c 自动生成。
     * 比完整五步省事, 适合这种单节点的简单设备 */
    ap3216c.miscdev.minor = MISC_DYNAMIC_MINOR;
    ap3216c.miscdev.name = AP3216C_NAME;
    ap3216c.miscdev.fops = &ap3216c_fops;
    ret = misc_register(&ap3216c.miscdev);
    if (ret < 0){
        printk("misc register failed!\r\n");
        return ret;
    }

    printk("ap3216c probe ok! i2c addr=0x%02x, /dev/%s created\r\n",
            client->addr, AP3216C_NAME);
    return 0;
}

static int ap3216c_remove(struct i2c_client *client)
{
    misc_deregister(&ap3216c.miscdev);
    return 0;
}

/*
 * i2c 驱动的两张匹配表都要带:
 *   id_table       按设备名(client->name)匹配 —— 4.1 内核有 id_table 时优先走这条
 *   of_match_table 按设备树 compatible 匹配
 * 当初只写了 of_match_table, 驱动注册成功但 probe 死活不执行,
 * 用设备的 modalias 和 modinfo 输出对照才发现两边名字对不上,
 * 补上 id_table 就通了。
 */
static const struct i2c_device_id ap3216c_id[] = {
    { "ap3216c", 0},
    { /*sentinel */}
};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

static struct i2c_driver ap3216c_driver = {
    .probe = ap3216c_probe,
    .remove = ap3216c_remove,
    .id_table = ap3216c_id,
    .driver = {
        .name = "ap3216c_sm",
        .owner = THIS_MODULE,
        .of_match_table = ap3216c_of_match,  
    },
};

module_i2c_driver(ap3216c_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sxy");