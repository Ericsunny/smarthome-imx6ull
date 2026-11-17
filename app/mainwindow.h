#ifndef LOCAL_MONITOR_MAINWINDOW_H
#define LOCAL_MONITOR_MAINWINDOW_H
#include "bridge.h"
#include "../core/monitor.h"
#include "../core/sampling.h"
#include "../core/actuators.h"
#include <QJsonObject>
#include <QMainWindow>
#include <memory>

class QLabel;
class QPushButton;
class QPlainTextEdit;
class QTimer;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::unique_ptr<monitor::Devices> devices, bool simulated, bool smoke);
    ~MainWindow() override;
    void shutdown();
    bool shutdownSucceeded() const {
        return stopped_ && feedback_.ledOk && feedback_.beepOk && servoRelease_.ok;
    }
    void startSmokeTest();
private:
    void refresh();
    void record(const QString &event);
    void applyOutputs(bool force = false);
    void dispatchActuators(monitor::Millis now);
    QJsonObject summary(bool pass) const;
    std::unique_ptr<monitor::Devices> devices_;
    monitor::SimDevices *sim_;
    monitor::Monitor monitor_;
    monitor::ActuatorPolicy actuators_;
    SampleBridge bridge_;
    std::unique_ptr<monitor::Sampler> sampler_;
    std::unique_ptr<monitor::ActuatorExecutor> executor_;
    QLabel *mode_, *dht_, *light_, *pir_, *alarm_, *outputs_;
    QLabel *servoState_, *curtainState_, *controlInputs_;
    QTabWidget *tabs_;
    QPushButton *arm_, *silence_;
    QPlainTextEdit *events_;
    QTimer *timer_;
    monitor::OutputResult feedback_;
    monitor::ActuatorResult servoRelease_{true, 0};
    monitor::Millis lastOutput_ = -10000;
    bool commandedLed_ = false, commandedBeep_ = false;
    bool outputAttempted_ = false, stopped_ = false;
    bool smokePass_ = true;
    bool simHigh_ = false, simDhtFailed_ = false, simHot_ = false;
    bool simOutputFailed_ = false;
};
#endif
