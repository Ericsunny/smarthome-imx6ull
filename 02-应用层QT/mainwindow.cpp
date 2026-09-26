#include "mainwindow.h"
#include "deviceio.h"
#include <QtWidgets>

/* 报警联动的蜂鸣器总开关：
 *   true = 完整报警联动"有人闯入 → 蜂鸣器响，人离开自动停"
 *   false = 红外触发时只亮报警灯、不响蜂鸣器（调试模式，）
 * 界面上的"蜂鸣器 开"按钮不受此开关影响；用户手动操作后联动不再接管（m_beepAuto）。 */
static const bool kAlarmBeepOn = true;

/* 温湿度自动调控阈值（任务书功能1"超标自动调控"）：超限自动开风扇，回落自动关 */
static const int kFanTempOn = 30;    /* ℃ */
static const int kFanHumOn  = 75;    /* % */

/* 窗帘步数：2048 拍 = 半圈（模拟开/合），驱动 2ms/拍约 4 秒 */
static const int kCurtainSteps = 2048;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_beepOn(false), m_beepAuto(false), m_ledOn(false),
      m_fanOn(false), m_fanAuto(false),
      m_fanAngle(0), m_fanDir(1), m_fanTimer(nullptr),
      m_curtainOpen(false), m_curtainThread(nullptr),
      m_sensorThread(nullptr), m_alarmThread(nullptr)
{
    setWindowTitle("智能安防监控系统");
    setFixedSize(800, 480);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    /* ---------- 左侧：环境监测 ---------- */
    QLabel *envTitle = buildTitle("环境监测", "#2E86C1");
    m_tempLabel = new QLabel("-- ℃");
    m_humLabel  = new QLabel("-- %");
    m_luxLabel  = new QLabel("-- lx");
    m_distLabel = new QLabel("-- cm");
    foreach (QLabel *l, (QList<QLabel*>{ m_tempLabel, m_humLabel, m_luxLabel, m_distLabel })) {
        l->setStyleSheet("font-size: 30px; font-weight: bold; color: #1B2631;");
    }

    QVBoxLayout *envLayout = new QVBoxLayout;
    envLayout->addWidget(envTitle);
    envLayout->addWidget(new QLabel("温度"));
    envLayout->addWidget(m_tempLabel);
    envLayout->addWidget(new QLabel("湿度"));
    envLayout->addWidget(m_humLabel);
    envLayout->addWidget(new QLabel("光照"));
    envLayout->addWidget(m_luxLabel);
    envLayout->addWidget(new QLabel("距离"));
    envLayout->addWidget(m_distLabel);
    envLayout->addStretch();

    QGroupBox *envBox = new QGroupBox;
    envBox->setLayout(envLayout);

    /* ---------- 右上：安防状态 ---------- */
    QLabel *alarmTitle = buildTitle("安防状态", "#C0392B");
    m_alarmLabel = new QLabel("监测中...");
    m_alarmLabel->setAlignment(Qt::AlignCenter);
    m_alarmLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: #27AE60;"
                                "background: #EAF7EA; border-radius: 8px;");
    m_alarmLabel->setMinimumHeight(110);

    QVBoxLayout *alarmLayout = new QVBoxLayout;
    alarmLayout->addWidget(alarmTitle);
    alarmLayout->addWidget(m_alarmLabel);
    QGroupBox *alarmBox = new QGroupBox;
    alarmBox->setLayout(alarmLayout);

    /* ---------- 右下：设备控制（四按钮）---------- */
    QLabel *ctrlTitle = buildTitle("设备控制", "#7D3C98");

    m_beepBtn     = new QPushButton("蜂鸣器 开");
    m_ledBtn      = new QPushButton("报警灯 开");
    m_fanBtn      = new QPushButton("风扇 开");
    m_curtainBtn  = new QPushButton("窗帘 开");
    foreach (QPushButton *b, (QList<QPushButton*>{ m_beepBtn, m_ledBtn, m_fanBtn, m_curtainBtn })) {
        b->setMinimumHeight(45);
        b->setStyleSheet("font-size: 18px;");
    }

    QGridLayout *ctrlGrid = new QGridLayout;
    ctrlGrid->addWidget(m_beepBtn, 0, 0);
    ctrlGrid->addWidget(m_ledBtn, 0, 1);
    ctrlGrid->addWidget(m_fanBtn, 1, 0);
    ctrlGrid->addWidget(m_curtainBtn, 1, 1);
    QLabel *fanNote = new QLabel("风扇=舵机带扇叶 | 窗帘=步进电机");
    fanNote->setStyleSheet("font-size: 12px; color: #7F8C8D;");
    ctrlGrid->addWidget(fanNote, 2, 0, 1, 2);

    QVBoxLayout *ctrlLayout = new QVBoxLayout;
    ctrlLayout->addWidget(ctrlTitle);
    ctrlLayout->addLayout(ctrlGrid);
    QGroupBox *ctrlBox = new QGroupBox;
    ctrlBox->setLayout(ctrlLayout);

    /* ---------- 总布局：左 40% 环境监测，右 60% 上状态/下控制 ---------- */
    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(alarmBox, 45);
    rightLayout->addWidget(ctrlBox, 55);

    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->addWidget(envBox, 40);
    mainLayout->addLayout(rightLayout, 60);

    /* ---------- 信号槽 ---------- */
    connect(m_beepBtn, &QPushButton::clicked, this, &MainWindow::onBeepClicked);
    connect(m_ledBtn,  &QPushButton::clicked, this, &MainWindow::onLedClicked);
    connect(m_fanBtn,  &QPushButton::clicked, this, &MainWindow::onFanClicked);
    connect(m_curtainBtn, &QPushButton::clicked, this, &MainWindow::onCurtainClicked);

    /* 风扇：定时器周期转动 SG90（带扇叶），60ms 一格 ping-pong 摆动 */
    m_fanTimer = new QTimer(this);
    m_fanTimer->setInterval(60);
    connect(m_fanTimer, &QTimer::timeout, this, &MainWindow::onFanTimeout);

    /* ---------- 启动工作线程 ---------- */
    DeviceIO::instance().openAll();
    m_sensorThread = new SensorWorker(this);
    m_alarmThread  = new AlarmWorker(this);
    m_curtainThread = new CurtainWorker(this);
    connect(m_sensorThread, &SensorWorker::dht11Updated, this, &MainWindow::onDht11);
    connect(m_sensorThread, &SensorWorker::luxUpdated, this, &MainWindow::onLux);
    connect(m_sensorThread, &SensorWorker::distanceUpdated, this, &MainWindow::onDistance);
    connect(m_alarmThread,  &AlarmWorker::alarmChanged, this, &MainWindow::onAlarm);
    connect(m_curtainThread, &QThread::finished, this, &MainWindow::onCurtainDone);
    m_sensorThread->start();
    m_alarmThread->start();
}

MainWindow::~MainWindow()
{
    m_fanTimer->stop();
    if (m_sensorThread) m_sensorThread->stop();
    if (m_alarmThread)  m_alarmThread->stop();
    /* 阻塞 read 线程无法被 stop 打断，随进程退出回收 */
    m_sensorThread->wait(1500);
    /* 窗帘步进若在走，等它收尾（最长约 4.5 秒） */
    m_curtainThread->wait(6000);
}

QLabel *MainWindow::buildTitle(const QString &text, const QString &color)
{
    QLabel *l = new QLabel(text);
    l->setStyleSheet(QString("font-size: 22px; font-weight: bold; color: %1;").arg(color));
    return l;
}

/* ---------- 槽：传感器数据更新 UI ---------- */

void MainWindow::onDht11(int temp, int hum)
{
    m_tempLabel->setText(QString("%1 ℃").arg(temp));
    m_humLabel->setText(QString("%1 %").arg(hum));

    /* 温湿度超标自动调控：超阈值自动开风扇，回落自动关。
     * 用户手动操作过（m_fanAuto=false）则不接管。 */
    bool over = (temp >= kFanTempOn || hum >= kFanHumOn);
    if (over && !m_fanOn)
        startFan(true);
    else if (!over && m_fanOn && m_fanAuto)
        stopFan();
}

void MainWindow::onLux(float lux)
{
    m_luxLabel->setText(QString("%1 lx").arg((int)lux));
}

void MainWindow::onDistance(int cm)
{
    if (cm < 0)
        m_distLabel->setText("-- cm");    /* 超时/量程外 */
    else
        m_distLabel->setText(QString("%1 cm").arg(cm));
}

void MainWindow::onAlarm(int state)
{
    if (state == 1) {
        m_alarmLabel->setText("⚠ 有人闯入！");
        m_alarmLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: #FFFFFF;"
                                    "background: #E74C3C; border-radius: 8px;");
        /* 报警联动：报警灯必亮；蜂鸣器由 kAlarmBeepOn 决定是否自动响
         * （与任务书"红外探测与报警"对应） */
        DeviceIO::instance().setLed(true);
        m_ledOn = true;
        m_ledBtn->setText("报警灯 关");

        if (kAlarmBeepOn) {
            DeviceIO::instance().setBeep(true);
            m_beepOn = true;
            m_beepAuto = true;
            m_beepBtn->setText("蜂鸣器 关");
        }
    } else {
        m_alarmLabel->setText("监测中...");
        m_alarmLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: #27AE60;"
                                    "background: #EAF7EA; border-radius: 8px;");
        /* 人离开：自动响起的蜂鸣器跟着停，避免一直响个不停 */
        if (m_beepAuto) {
            DeviceIO::instance().setBeep(false);
            m_beepOn = false;
            m_beepAuto = false;
            m_beepBtn->setText("蜂鸣器 开");
        }
    }
}

/* ---------- 槽：控制按钮 ---------- */

void MainWindow::onBeepClicked()
{
    m_beepOn = !m_beepOn;
    m_beepAuto = false;              /* 手动开/关之后，不再被报警联动接管 */
    DeviceIO::instance().setBeep(m_beepOn);
    m_beepBtn->setText(m_beepOn ? "蜂鸣器 关" : "蜂鸣器 开");
}

void MainWindow::onLedClicked()
{
    m_ledOn = !m_ledOn;
    DeviceIO::instance().setLed(m_ledOn);
    m_ledBtn->setText(m_ledOn ? "报警灯 关" : "报警灯 开");
}

/* ---------- 风扇（SG90 + 扇叶）---------- */

void MainWindow::startFan(bool autoMode)
{
    m_fanOn = true;
    m_fanAuto = autoMode;
    m_fanTimer->start();
    m_fanBtn->setText(autoMode ? "风扇 关·自动" : "风扇 关");
}

void MainWindow::stopFan()
{
    m_fanOn = false;
    m_fanAuto = false;
    m_fanTimer->stop();
    m_fanBtn->setText("风扇 开");
}

void MainWindow::onFanClicked()
{
    m_fanAuto = false;               /* 手动操作后温湿度联动不再接管 */
    if (m_fanOn)
        stopFan();
    else
        startFan(false);
}

void MainWindow::onFanTimeout()
{
    /* 扇叶模拟：舵机在 0~180° 间 ping-pong 摆动 */
    m_fanAngle += 10 * m_fanDir;
    if (m_fanAngle >= 180) { m_fanAngle = 180; m_fanDir = -1; }
    if (m_fanAngle <= 0)   { m_fanAngle = 0;   m_fanDir = 1; }
    DeviceIO::instance().setServoAngle(m_fanAngle);
}

/* ---------- 窗帘（28BYJ48 步进电机）---------- */

void MainWindow::onCurtainClicked()
{
    m_curtainOpen = !m_curtainOpen;
    m_curtainBtn->setEnabled(false);
    m_curtainBtn->setText(m_curtainOpen ? "窗帘 开启中..." : "窗帘 关闭中...");
    m_curtainThread->steps = m_curtainOpen ? kCurtainSteps : -kCurtainSteps;
    m_curtainThread->start();        /* 走完发 finished → onCurtainDone */
}

void MainWindow::onCurtainDone()
{
    m_curtainBtn->setEnabled(true);
    m_curtainBtn->setText(m_curtainOpen ? "窗帘 关" : "窗帘 开");
}
