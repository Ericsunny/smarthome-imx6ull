#ifndef MONITOR_ACTUATORS_H
#define MONITOR_ACTUATORS_H
#include "devices.h"
#include "monitor.h"
#include <functional>
#include <thread>

namespace monitor {
enum class ControlMode { Hold, Manual, Automatic };
const char *modeName(ControlMode mode);
struct ActuatorConfig {
    int servoClosed = 0, servoOpen = 90;
    int curtainTravel = 2048; // Example only: calibrate the actual mechanism.
    double darkLux = 30, brightLux = 80;
    Millis curtainIntervalMs = 30000;
};
struct ChannelState {
    ControlMode mode = ControlMode::Hold;
    bool busy = false, known = false;
    int target = 0, accepted = 0, error = 0;
};

// Single owner: the controller/UI thread. known is a command/step estimate,
// never physical position feedback. Hold cannot cancel an in-flight ioctl.
class ActuatorPolicy {
public:
    explicit ActuatorPolicy(const ActuatorConfig &config = ActuatorConfig());
    void servoMode(ControlMode mode);
    void curtainMode(ControlMode mode);
    bool manualServo(int angle);
    bool manualCurtain(bool open);
    bool confirmCurtainClosed(); // Operator assertion; no automatic homing.
    bool takeServo(const Snapshot &input, const Config &config, Millis now, int &angle);
    bool takeCurtain(const Snapshot &input, const Config &config, Millis now,
                     int &steps, int &target);
    void completed(bool servo, ActuatorResult result, Millis now);
    const ChannelState &servo() const { return servo_; }
    const ChannelState &curtain() const { return curtain_; }
    const ActuatorConfig &config() const { return config_; }
    bool fault() const { return servo_.error || curtain_.error; }
private:
    ActuatorConfig config_;
    ChannelState servo_, curtain_;
    int servoInFlight_ = 0, curtainInFlight_ = 0;
    bool curtainCompleted_ = false;
    Millis lastCurtainCompletion_ = 0;
};

// One outstanding job per channel. Servo and blocking stepper calls use
// independent workers. stop drops unstarted work and joins started calls.
class ActuatorExecutor {
public:
    using Callback = std::function<void(bool, ActuatorResult, Millis)>;
    ActuatorExecutor(Devices &devices, Callback callback);
    ~ActuatorExecutor();
    bool submitServo(int angle);
    bool submitCurtain(int steps);
    ActuatorResult stop();
private:
    struct Job { bool occupied = false, pending = false; int value = 0; };
    bool submit(bool servo, int value);
    void run(bool servo);
    Devices &devices_;
    Callback callback_;
    std::mutex mutex_;
    std::condition_variable changed_;
    Job servo_, curtain_;
    bool stopping_ = false, stopped_ = false, servoAttempted_ = false;
    ActuatorResult release_{true, 0};
    std::thread servoThread_, curtainThread_;
};
}
#endif
