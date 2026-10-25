#ifndef WORKERS_H
#define WORKERS_H

#include <QThread>
#include <atomic>
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
    std::atomic<bool> m_stop;
};

/*
 * 报警监听线程：waitAlarm 内部 poll 等中断事件（驱动侧中断+等待队列，
 * 零 CPU 消耗），有人/无人状态变化时发信号。设备离线时每秒超时一次，
 * 循环头即可见 stop 标志——退出程序时线程最多 1 秒内结束。
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
            /* waitAlarm 内部 poll(1s 超时): 有事件返回 true, 超时返回
             * false——两种情况都会回到循环头看 m_stop, 退出程序时线程
             * 最多 1 秒内结束(原来的阻塞 read 打不断, 析构时 abort) */
            if (DeviceIO::instance().waitAlarm(1000, state))
                emit alarmChanged(state);        /* 1=有人 0=无人 */
            /* 设备离线时 waitAlarm 同样走超时路径, 不再需要降级空转 */
        }
    }

signals:
    void alarmChanged(int state);

private:
    std::atomic<bool> m_stop;
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
