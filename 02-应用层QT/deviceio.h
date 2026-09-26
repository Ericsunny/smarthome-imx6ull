#ifndef DEVICEIO_H
#define DEVICEIO_H

#include <QString>
#include <sys/ioctl.h>

/* ioctl 命令字——与《设备接口协议.md》/驱动完全一致（放在头文件供 UI 与封装层共用） */
#define SG90_CMD_SET_ANGLE   _IOW('S', 1, int)
#define STEP_CMD_RUN         _IOW('T', 1, int)   /* 28BYJ48: 带符号步数,正=开窗帘 */

/* 板载 AP3216C：内核自带驱动（CONFIG_LITEON_AP3216C=y）已接管该设备，
 * 其接口为 ioctl（定义见内核 include/linux/ap3216c.h），不是 read */
#define AP3216C_GET_IR       _IOR('D', 0, unsigned short)
#define AP3216C_GET_ALS      _IOR('D', 1, unsigned short)
#define AP3216C_GET_PS       _IOR('D', 2, unsigned short)

/*
 * 设备访问封装层——QT 应用访问硬件的唯一入口
 * 接口定义见《设备接口协议.md》，与驱动层一一对应
 *
 * SIM_DEVICES 宏（M3 阶段）：驱动未就绪，读取/控制全部走模拟数据，
 * open 也跳过（设备一律"离线"）；去掉宏后为真设备访问。
 */

enum DevId {
    D_DHT11 = 0,    /* /dev/dht11    read 2字节: [0]湿度 [1]温度 */
    D_HCSR505,      /* /dev/hc_sr505 read 阻塞 4字节int: 0/1 */
    D_HCSR04,       /* /dev/hc_sr04  ioctl(0,&dist) 距离cm */
    D_AP3216C,      /* /dev/ap3216c  内核自带驱动，ioctl GET_ALS=光照 */
    D_LED,          /* /dev/led      write 1字节: 1亮/0灭 */
    D_BEEP,         /* /dev/beep     write 1字节: 1响/0停 */
    D_STEP28,       /* /dev/step28byj48 ioctl RUN 带符号步数(28BYJ48 窗帘) */
    D_SG90,         /* /dev/sg90     ioctl SET_ANGLE 0~180 */
    D_COUNT
};

class DeviceIO
{
public:
    static DeviceIO &instance();

    void openAll();                              /* 打开全部设备；失败的=离线 */
    bool online(DevId d) const { return m_fd[d] >= 0; }
    QString devPath(DevId d) const;

    /* ---- 读取 ---- */
    bool readDth11(int &temp, int &hum);         /* 出参：温度℃ / 湿度% */
    bool readAp3216c(float &lux);                /* 出参：光照 lux（ioctl 读 ALS）*/
    bool readHcSr505(int &state);                /* 阻塞！出参：1有人/0无人 */
    bool readHcSr04(int &distCm);                /* 出参：距离 cm，-1=超时 */

    /* ---- 控制 ---- */
    bool setLed(bool on);
    bool setBeep(bool on);
    bool setServoAngle(int angle);               /* 0~180°（SG90 带扇叶=风扇）*/
    bool stepRun(int steps);                     /* 28BYJ48 步进,阻塞到走完,正=开窗帘 */

private:
    DeviceIO();
    int m_fd[D_COUNT];
};

#endif /* DEVICEIO_H */
