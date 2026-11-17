#ifndef LOCAL_MONITOR_MAINWINDOW_H
#define LOCAL_MONITOR_MAINWINDOW_H
#include "bridge.h"
#include "../core/monitor.h"
#include "../core/sampling.h"
#include <QJsonObject>
#include <QMainWindow>
#include <memory>

class QLabel;
class QPushButton;
class QPlainTextEdit;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::unique_ptr<monitor::Devices> devices, bool simulated, bool smoke);
    ~MainWindow() override;
    void shutdown();
    bool shutdownSucceeded() const { return stopped_ && feedback_.ledOk && feedback_.beepOk; }
    void startSmokeTest();
private:
    void refresh();
    void record(const QString &event);
    void applyOutputs(bool force = false);
    QJsonObject summary(bool pass) const;
    std::unique_ptr<monitor::Devices> devices_;
    monitor::SimDevices *sim_;
    monitor::Monitor monitor_;
    SampleBridge bridge_;
    std::unique_ptr<monitor::Sampler> sampler_;
    QLabel *mode_, *dht_, *light_, *pir_, *alarm_, *outputs_;
    QPushButton *arm_, *silence_;
    QPlainTextEdit *events_;
    QTimer *timer_;
    monitor::OutputResult feedback_;
    monitor::Millis lastOutput_ = -10000;
    bool commandedLed_ = false, commandedBeep_ = false;
    bool outputAttempted_ = false, stopped_ = false;
    bool smokePass_ = true;
    bool simHigh_ = false, simDhtFailed_ = false, simHot_ = false;
    bool simOutputFailed_ = false;
};
#endif
