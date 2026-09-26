#include "deviceio.h"
#include <fcntl.h>
#include <unistd.h>
#include <QThread>
#include <QDebug>
#include <QTime>

static const char *DEV_PATH[] = {
    "/dev/dht11", "/dev/hc_sr505", "/dev/hc_sr04", "/dev/ap3216c",
    "/dev/led", "/dev/beep", "/dev/step28byj48", "/dev/sg90"
};

DeviceIO &DeviceIO::instance()
{
    static DeviceIO io;
    return io;
}

DeviceIO::DeviceIO()
{
    for (int i = 0; i < D_COUNT; ++i)
        m_fd[i] = -1;
}

void DeviceIO::openAll()
{
#ifdef SIM_DEVICES
    qDebug() << "[SIM] 模拟模式：设备全部离线，读取走模拟数据";
    return;
#else
    for (int i = 0; i < D_COUNT; ++i) {
        m_fd[i] = open(DEV_PATH[i], O_RDWR);
        if (m_fd[i] < 0)
            qDebug() << "设备离线:" << DEV_PATH[i];
    }
#endif
}

QString DeviceIO::devPath(DevId d) const
{
    return QString(DEV_PATH[d]);
}

/* ---------------- 读取 ---------------- */

bool DeviceIO::readDth11(int &temp, int &hum)
{
#ifdef SIM_DEVICES
    hum  = 40 + qrand() % 25;            /* 模拟湿度 40~64% */
    temp = 24 + qrand() % 6;             /* 模拟温度 24~29℃ */
    return true;
#else
    if (m_fd[D_DHT11] < 0)
        return false;
    unsigned char b[2] = { 0, 0 };
    /* DHT11 时序驱动偶发丢边沿(~3%)返回错误，连读三次提高成功率 */
    for (int i = 0; i < 3; ++i) {
        if (read(m_fd[D_DHT11], b, 2) == 2) {
            hum  = b[0];
            temp = b[1];
            return true;
        }
        QThread::msleep(1200);           /* DHT11 采样间隔 ≥1s */
    }
    return false;
#endif
}

bool DeviceIO::readAp3216c(float &lux)
{
#ifdef SIM_DEVICES
    lux = 100 + qrand() % 900;           /* 模拟光照 100~1000 lx */
    return true;
#else
    /* 板载 AP3216C 由内核自带驱动接管，接口为 ioctl（见 deviceio.h 注释） */
    if (m_fd[D_AP3216C] < 0)
        return false;
    unsigned short als = 0;
    if (ioctl(m_fd[D_AP3216C], AP3216C_GET_ALS, &als) < 0)
        return false;
    lux = als / 1.2;                     /* ALS 原始值近似换算 */
    return true;
#endif
}

bool DeviceIO::readHcSr505(int &state)
{
#ifdef SIM_DEVICES
    /* 模拟：每 ~8 秒随机触发一次"有人" */
    static int cnt = 0;
    if (++cnt % 8 == 0)
        state = 1;
    else
        state = 0;
    QThread::msleep(1000);
    return true;
#else
    if (m_fd[D_HCSR505] < 0)
        return false;
    return read(m_fd[D_HCSR505], &state, sizeof(state)) == sizeof(state);
#endif
}

bool DeviceIO::readHcSr04(int &distCm)
{
#ifdef SIM_DEVICES
    distCm = 30 + qrand() % 170;
    return true;
#else
    if (m_fd[D_HCSR04] < 0)
        return false;
    distCm = 0;
    if (ioctl(m_fd[D_HCSR04], 0, &distCm) < 0)
        return false;
    return distCm >= 0;
#endif
}

/* ---------------- 控制 ---------------- */

bool DeviceIO::setLed(bool on)
{
#ifdef SIM_DEVICES
    qDebug() << "[SIM] LED =" << on;
    return true;
#else
    if (m_fd[D_LED] < 0)
        return false;
    char v = on ? 1 : 0;
    return write(m_fd[D_LED], &v, 1) == 1;
#endif
}

bool DeviceIO::setBeep(bool on)
{
#ifdef SIM_DEVICES
    qDebug() << "[SIM] BEEP =" << on;
    return true;
#else
    if (m_fd[D_BEEP] < 0)
        return false;
    char v = on ? 1 : 0;
    return write(m_fd[D_BEEP], &v, 1) == 1;
#endif
}

bool DeviceIO::setServoAngle(int angle)
{
#ifdef SIM_DEVICES
    qDebug() << "[SIM] 舵机角度 =" << angle;
    return true;
#else
    if (m_fd[D_SG90] < 0)
        return false;
    if (angle < 0)
        angle = 0;
    if (angle > 180)
        angle = 180;
    return ioctl(m_fd[D_SG90], SG90_CMD_SET_ANGLE, &angle) == 0;
#endif
}

bool DeviceIO::stepRun(int steps)
{
#ifdef SIM_DEVICES
    qDebug() << "[SIM] 步进电机步数 =" << steps;
    QThread::msleep(500);                /* 模拟步进耗时 */
    return true;
#else
    /* 阻塞到走完(半圈 2048 拍约 4 秒)——调用方须放在工作线程 */
    if (m_fd[D_STEP28] < 0)
        return false;
    return ioctl(m_fd[D_STEP28], STEP_CMD_RUN, &steps) == 0;
#endif
}
