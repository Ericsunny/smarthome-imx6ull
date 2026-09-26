#include <QApplication>
#include <QDebug>
#include <QThread>
#include "mainwindow.h"
#include "deviceio.h"

/*
 * 用法：
 *   ./smarthome              正常启动 QT 界面
 *   ./smarthome --selftest   无界面自测：逐项操作硬件并打印结果
 *                            
 */
static int runSelfTest()
{
    DeviceIO &io = DeviceIO::instance();
    io.openAll();

    qDebug() << "===== smarthome 硬件自测 =====";
    int t = 0, h = 0;
    float lux = 0;

    qDebug() << "温湿度读取:" << (io.readDth11(t, h) ? "成功" : "失败")
             << "温度=" << t << "湿度=" << h;
    qDebug() << "光照读取  :" << (io.readAp3216c(lux) ? "成功" : "失败")
             << "lux=" << lux;

    qDebug() << "LED 开(灯应亮):" << io.setLed(true);
    QThread::msleep(1500);
    qDebug() << "LED 关(灯应灭):" << io.setLed(false);
    QThread::msleep(500);

    qDebug() << "蜂鸣器 开(应响):" << io.setBeep(true);
    QThread::msleep(1500);
    qDebug() << "蜂鸣器 关(应停):" << io.setBeep(false);

    qDebug() << "===== 自测结束 =====";
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (argc > 1 && QString(argv[1]) == "--selftest")
        return runSelfTest();

    MainWindow w;
    w.show();

    return app.exec();
}
