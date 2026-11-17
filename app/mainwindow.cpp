#include "mainwindow.h"
#include <QApplication>
#include <QGridLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>

namespace {
QString quality(const monitor::SampleState &s, monitor::Millis now,
                monitor::Millis freshness)
{
    const QString status = QString::fromStdString(s.status(now, freshness));
    const QString age = s.seen ? QString::number(now - s.lastSuccess) : "--";
    return QString("%1 | 最近成功距今 %2 ms | error=%3").arg(status, age).arg(s.error);
}
}

MainWindow::MainWindow(std::unique_ptr<monitor::Devices> devices, bool simulated,
                       bool smoke)
    : devices_(std::move(devices)),
      sim_(simulated ? dynamic_cast<monitor::SimDevices *>(devices_.get()) : nullptr)
{
    setWindowTitle(QStringLiteral("环境监测与本地报警"));
    resize(800, 480);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    mode_ = new QLabel(simulated ? QStringLiteral("模拟设备 · 不代表开发板验证") :
                                  QStringLiteral("Linux 设备 · 默认撤防"));
    mode_->setStyleSheet("font-weight:bold;color:#9b3d00");
    layout->addWidget(mode_);
    dht_ = new QLabel;
    light_ = new QLabel;
    pir_ = new QLabel;
    alarm_ = new QLabel;
    outputs_ = new QLabel;
    for (QLabel *label : {dht_, light_, pir_, alarm_, outputs_}) {
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    auto *buttons = new QGridLayout;
    arm_ = new QPushButton(QStringLiteral("布防"));
    silence_ = new QPushButton(QStringLiteral("当前报警静音"));
    buttons->addWidget(arm_, 0, 0);
    buttons->addWidget(silence_, 0, 1);
    connect(arm_, &QPushButton::clicked, this, [this]() {
        monitor_.setArmed(!monitor_.state().armed, monitor::monotonicMs());
        record(monitor_.state().armed ? "armed" : "disarmed");
        refresh();
    });
    connect(silence_, &QPushButton::clicked, this, [this]() {
        monitor_.silence(monitor::monotonicMs()); record("silenced"); refresh();
    });
    if (sim_) {
        auto *motion = new QPushButton(QStringLiteral("模拟活动高/低"));
        auto *failure = new QPushButton(QStringLiteral("模拟 DHT11 故障/恢复"));
        auto *hot = new QPushButton(QStringLiteral("模拟温度超限/恢复"));
        auto *outputFail = new QPushButton(QStringLiteral("模拟输出失败/恢复"));
        buttons->addWidget(motion, 1, 0);
        buttons->addWidget(failure, 1, 1);
        buttons->addWidget(hot, 2, 0);
        buttons->addWidget(outputFail, 2, 1);
        connect(motion, &QPushButton::clicked, this, [this]() {
            simHigh_ = !simHigh_; sim_->setPir(true, simHigh_);
        });
        connect(failure, &QPushButton::clicked, this, [this]() {
            simDhtFailed_ = !simDhtFailed_; sim_->setDht(!simDhtFailed_);
        });
        connect(hot, &QPushButton::clicked, this, [this]() {
            simHot_ = !simHot_; simDhtFailed_ = false;
            sim_->setDht(true, simHot_ ? 32 : 24);
        });
        connect(outputFail, &QPushButton::clicked, this, [this]() {
            simOutputFailed_ = !simOutputFailed_; sim_->failOutputs(simOutputFailed_);
            applyOutputs(true); refresh();
        });
    }
    layout->addLayout(buttons);
    events_ = new QPlainTextEdit;
    events_->setReadOnly(true);
    events_->document()->setMaximumBlockCount(100);
    layout->addWidget(events_);
    setCentralWidget(central);

    connect(&bridge_, &SampleBridge::dht, this,
        [this](bool ok, int t, int h, int error, qint64 time) {
            monitor_.dhtResult(ok, t, h, error, time);
            record(ok ? "dht_ok" : "dht_failed"); refresh();
        }, Qt::QueuedConnection);
    connect(&bridge_, &SampleBridge::light, this,
        [this](bool ok, double lux, int error, qint64 time) {
            monitor_.lightResult(ok, lux, error, time);
            record(ok ? "light_ok" : "light_failed"); refresh();
        }, Qt::QueuedConnection);
    connect(&bridge_, &SampleBridge::pir, this,
        [this](int kind, bool high, int error, qint64 time) {
            if (kind == int(monitor::PirKind::Event)) monitor_.pirResult(high, time);
            else monitor_.pirOffline(error, time);
            record(kind == int(monitor::PirKind::Event) ? "pir_event" : "pir_offline");
            refresh();
        }, Qt::QueuedConnection);
    monitor::SampleCallbacks callbacks;
    callbacks.dht = [this](monitor::DhtReading r, monitor::Millis t) {
        emit bridge_.dht(r.ok, r.temperature, r.humidity, r.error, t);
    };
    callbacks.light = [this](monitor::LightReading r, monitor::Millis t) {
        emit bridge_.light(r.ok, r.lux, r.error, t);
    };
    callbacks.pir = [this](monitor::PirReading r, monitor::Millis t) {
        emit bridge_.pir(int(r.kind), r.high, r.error, t);
    };
    monitor::SamplingConfig config;
    if (smoke) {
        config.dhtPeriodMs = 50; config.lightPeriodMs = 30; config.pirPollMs = 20;
    }
    sampler_.reset(new monitor::Sampler(*devices_, callbacks, config));
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::refresh);
    timer_->start(200);
    refresh();
    sampler_->start();
}

MainWindow::~MainWindow() { shutdown(); }

void MainWindow::record(const QString &event)
{
    QJsonObject object;
    object["event"] = event;
    object["monotonic_ms"] = double(monitor::monotonicMs());
    object["simulated"] = sim_ != nullptr;
    object["causes"] = int(monitor_.state().causes);
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    events_->appendPlainText(QString::fromUtf8(json));
    std::printf("%s\n", json.constData());
    std::fflush(stdout);
}

void MainWindow::applyOutputs(bool force)
{
    const auto &s = monitor_.state();
    const auto now = monitor::monotonicMs();
    const bool changed = !outputAttempted_ || s.desiredLed != commandedLed_ ||
                         s.desiredBeep != commandedBeep_;
    if (!force && !changed && (feedback_.ledOk && feedback_.beepOk)) return;
    if (!force && !changed && now - lastOutput_ < 1000) return;
    feedback_ = devices_->applyOutputs(s.desiredLed, s.desiredBeep);
    commandedLed_ = s.desiredLed;
    commandedBeep_ = s.desiredBeep;
    lastOutput_ = now;
    outputAttempted_ = true;
}

void MainWindow::refresh()
{
    if (stopped_) return;
    const auto now = monitor::monotonicMs();
    monitor_.tick(now);
    const auto &s = monitor_.state();
    dht_->setText(QStringLiteral("温湿度：%1℃ / %2%（上次成功值）\n%3")
        .arg(s.dht.seen ? QString::number(s.temperature) : "--")
        .arg(s.dht.seen ? QString::number(s.humidity) : "--")
        .arg(quality(s.dht, now, monitor_.config().staleMs)));
    light_->setText(QStringLiteral("光照估算：%1 lx（上次成功值）\n%2")
        .arg(s.light.seen ? QString::number(s.lux, 'f', 1) : "--")
        .arg(quality(s.light, now, monitor_.config().staleMs)));
    pir_->setText(QStringLiteral("PIR：%1 | error=%2（电平不等同于人员身份/持续在场）")
        .arg(s.pirKnown ? (s.pirHigh ? "HIGH" : "LOW") : "unknown/offline").arg(s.pirError));
    alarm_->setText(QStringLiteral("%1 | 环境超限=%2 | 活动报警=%3 | 故障=%4 | 静音=%5")
        .arg(s.armed ? QStringLiteral("布防") : QStringLiteral("撤防"))
        .arg(s.environmentLatched).arg(bool(s.causes & monitor::Activity))
        .arg(s.fault).arg(s.muted));
    arm_->setText(s.armed ? QStringLiteral("撤防") : QStringLiteral("布防"));
    silence_->setEnabled(s.causes != 0 && !s.muted);
    applyOutputs();
    outputs_->setText(QStringLiteral("请求 LED=%1 / 蜂鸣=%2 | 接口成功 LED=%3 / 蜂鸣=%4 | error=%5/%6")
        .arg(s.desiredLed).arg(s.desiredBeep).arg(feedback_.ledOk).arg(feedback_.beepOk)
        .arg(feedback_.ledError).arg(feedback_.beepError));
}

void MainWindow::shutdown()
{
    if (stopped_) return;
    stopped_ = true;
    timer_->stop();
    sampler_->stop();
    // Workers are joined before any backend or QObject is destroyed.
    feedback_ = devices_->applyOutputs(false, false);
    record(feedback_.ledOk && feedback_.beepOk ? "shutdown_outputs_off" :
                                               "shutdown_output_error");
}

QJsonObject MainWindow::summary(bool pass) const
{
    QJsonObject result;
    result["test"] = "qt_simulated_workflow";
    result["qt_version"] = qVersion();
    result["simulated"] = true;
    result["pass"] = pass;
    result["dht_attempts"] = int(monitor_.state().dhtAttempts);
    result["dht_failures"] = int(monitor_.state().dhtFailures);
    result["light_attempts"] = int(monitor_.state().lightAttempts);
    result["rendered"] = !const_cast<MainWindow *>(this)->grab().isNull();
    return result;
}

void MainWindow::startSmokeTest()
{
    QTimer::singleShot(200, this, [this]() {
        smokePass_ &= !monitor_.state().fault && !monitor_.state().armed;
        arm_->click(); sim_->setPir(true, true);
    });
    QTimer::singleShot(400, this, [this]() {
        smokePass_ &= monitor_.state().desiredBeep && sim_->beep();
        silence_->click();
        smokePass_ &= !monitor_.state().desiredBeep && !sim_->beep();
    });
    QTimer::singleShot(600, this, [this]() {
        arm_->click(); sim_->setDht(false);
    });
    QTimer::singleShot(700, this, [this]() {
        sim_->failOutputs(true); applyOutputs(true); refresh();
        smokePass_ &= !feedback_.ledOk && !feedback_.beepOk;
        record("output_failure_observed");
    });
    QTimer::singleShot(800, this, [this]() {
        sim_->failOutputs(false); applyOutputs(true); refresh();
        smokePass_ &= feedback_.ledOk && feedback_.beepOk;
        record("output_recovered");
    });
    QTimer::singleShot(1000, this, [this]() {
        const auto &s = monitor_.state();
        smokePass_ &= !s.armed && !s.desiredBeep && s.dhtFailures > 0 &&
                      s.light.valid(monitor::monotonicMs(), monitor_.config().staleMs);
        smokePass_ &= !grab().isNull();
        const QString capture = qEnvironmentVariable("MONITOR_TEST_CAPTURE");
        if (!capture.isEmpty()) smokePass_ &= grab().save(capture);
        const auto result = summary(smokePass_);
        const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Compact);
        std::printf("%s\n", json.constData());
        if (qEnvironmentVariableIsSet("MONITOR_TEST_SHUTDOWN_FAILURE"))
            sim_->failOutputs(true);
        shutdown();
        QApplication::exit(smokePass_ ? 0 : 1);
    });
}
