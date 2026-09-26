#ifndef WORKERS_H
#define WORKERS_H

#include <QThread>
#include "deviceio.h"

/*
 * 传感器采集线程：1 秒一轮，读温湿度和光照，通过信号槽把数据送到 UI 线程
 * QThread::run() 重写方式（本应用规模下最直观的用法）
 */
class SensorWorker : public QThread
{
    Q_OBJECT
public:
    explicit SensorWorker(QObject *parent = nullptr)
        : QThread(parent), m_stop(false) {}
    void stop() { m_stop = true; }

protected:
    void run() override
    {
        while (!m_stop) {
            int t = 0, h = 0;
            float lux = 0;
            int dist = -1;
            if (DeviceIO::instance().readDth11(t, h))
                emit dht11Updated(t, h);
            if (DeviceIO::instance().readAp3216c(lux))
                emit luxUpdated(lux);
            /* SR04: ioctl 失败(量程外/超时)发 -1, UI 显示 "--" */
            if (!DeviceIO::instance().readHcSr04(dist))
                dist = -1;
            emit distanceUpdated(dist);
            /* 分片睡眠：1 秒一轮，stop 后最多 100ms 内退出 */
            for (int i = 0; i < 10 && !m_stop; ++i)
                msleep(100);
        }
    }

signals:
    void dht11Updated(int temp, int hum);
    void luxUpdated(float lux);
    void distanceUpdated(int cm);

private:
    volatile bool m_stop;
};

/*
 * 报警监听线程：阻塞 read hc_sr505（驱动内是中断+等待队列，零 CPU 消耗），
 * 有人/无人状态变化时发信号。设备离线时降级为 1 秒空转（模拟模式下即此路径）。
 * 注意：阻塞 read 无法用 stop() 打断——退出程序时线程随进程结束（可接受）。
 */
class AlarmWorker : public QThread
{
    Q_OBJECT
public:
    explicit AlarmWorker(QObject *parent = nullptr)
        : QThread(parent), m_stop(false) {}
    void stop() { m_stop = true; }

protected:
    void run() override
    {
        while (!m_stop) {
            int state = -1;
            if (DeviceIO::instance().readHcSr505(state)) {
                emit alarmChanged(state);        /* 1=有人 0=无人 */
            } else {
                for (int i = 0; i < 10 && !m_stop; ++i)
                    msleep(100);                 /* 设备离线：降级空转 */
            }
        }
    }

signals:
    void alarmChanged(int state);

private:
    volatile bool m_stop;
};

/*
 * 窗帘步进线程：28BYJ48 驱动的 ioctl 会阻塞到走完（半圈 2048 拍约 4 秒），
 * 必须放在独立线程跑，否则 UI 线程卡死。start() 前设置 steps；
 * 走完自动发 QThread 自带的 finished() 信号，UI 借此恢复按钮。
 */
class CurtainWorker : public QThread
{
    Q_OBJECT
public:
    explicit CurtainWorker(QObject *parent = nullptr)
        : QThread(parent), steps(0) {}
    int steps;

protected:
    void run() override
    {
        DeviceIO::instance().stepRun(steps);
    }
};

#endif /* WORKERS_H */
