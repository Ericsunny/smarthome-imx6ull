#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "workers.h"

/* 主窗口 800x480（与板上 LCD 分辨率一致）
 * 布局：左侧环境监测大字（温/湿/光/距）/ 右上安防状态 / 右下设备控制
 * 控制区四按钮：蜂鸣器 / 报警灯 / 风扇(SG90+扇叶) / 窗帘(28BYJ48)
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    /* 传感器数据到达（跨线程信号槽，自动排队到 UI 线程执行） */
    void onDht11(int temp, int hum);
    void onLux(float lux);
    void onDistance(int cm);          /* -1 = 读取失败/量程外，显示 -- */
    void onAlarm(int state);          /* 1=有人 0=无人 */

    /* 控制按钮 */
    void onBeepClicked();
    void onLedClicked();
    void onFanClicked();
    void onFanTimeout();              /* 风扇运行中：SG90 周期转动模拟扇叶 */
    void onCurtainClicked();          /* 窗帘开/关：下发步进（阻塞，走独立线程） */
    void onCurtainDone();             /* 步进走完：恢复按钮 */

private:
    QLabel  *buildTitle(const QString &text, const QString &color);
    void startFan(bool autoMode);
    void stopFan();

    QLabel      *m_tempLabel;
    QLabel      *m_humLabel;
    QLabel      *m_luxLabel;
    QLabel      *m_distLabel;      /* SR04 距离 */
    QLabel      *m_alarmLabel;     /* 安防状态大字 */

    QPushButton *m_beepBtn;
    QPushButton *m_ledBtn;
    QPushButton *m_fanBtn;
    QPushButton *m_curtainBtn;

    bool         m_beepOn;
    bool         m_beepAuto;       /* 蜂鸣器是否由报警联动自动打开 */
    bool         m_ledOn;
    bool         m_fanOn;
    bool         m_fanAuto;        /* 风扇是否由温湿度联动自动打开 */
    int          m_fanAngle;       /* SG90 扇叶摆动角度 */
    int          m_fanDir;         /* 摆动方向 +1/-1 */
    QTimer      *m_fanTimer;

    bool         m_curtainOpen;
    CurtainWorker *m_curtainThread;

    SensorWorker *m_sensorThread;
    AlarmWorker  *m_alarmThread;
};

#endif /* MAINWINDOW_H */
