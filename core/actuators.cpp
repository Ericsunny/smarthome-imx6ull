#include "actuators.h"
#include <cerrno>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace monitor {
const char *modeName(ControlMode mode)
{
    switch (mode) {
    case ControlMode::Hold: return "hold";
    case ControlMode::Manual: return "manual";
    case ControlMode::Automatic: return "auto";
    }
    return "unknown";
}
ActuatorPolicy::ActuatorPolicy(const ActuatorConfig &config) : config_(config)
{
    if (config.servoClosed < 0 || config.servoClosed > 180 ||
        config.servoOpen < 0 || config.servoOpen > 180 ||
        config.curtainTravel <= 0 || config.curtainTravel > 8192 ||
        config.curtainTravel % 8 || !std::isfinite(config.darkLux) ||
        !std::isfinite(config.brightLux) || config.darkLux < 0 ||
        config.darkLux >= config.brightLux || config.curtainIntervalMs < 0)
        throw std::invalid_argument("invalid actuator calibration/thresholds");
}
void ActuatorPolicy::servoMode(ControlMode mode)
{
    servo_.mode = mode;
    if (!servo_.busy && mode != ControlMode::Hold)
        servo_.error = 0; // Explicit operator acknowledgement.
}
void ActuatorPolicy::curtainMode(ControlMode mode) { curtain_.mode = mode; }
bool ActuatorPolicy::manualServo(int angle)
{
    if (angle < 0 || angle > 180) return false;
    servoMode(ControlMode::Manual);
    servo_.target = angle;
    return true;
}
bool ActuatorPolicy::manualCurtain(bool open)
{
    if (!curtain_.known || curtain_.error) return false;
    curtain_.mode = ControlMode::Manual;
    curtain_.target = open ? config_.curtainTravel : 0;
    return true;
}
bool ActuatorPolicy::confirmCurtainClosed()
{
    if (curtain_.busy) return false;
    curtain_.mode = ControlMode::Hold;
    curtain_.known = true;
    curtain_.accepted = curtain_.target = 0;
    curtain_.error = 0;
    return true;
}
bool ActuatorPolicy::takeServo(const Snapshot &input, const Config &config,
                              Millis now, int &angle)
{
    if (servo_.mode == ControlMode::Automatic) {
        if (!input.dht.valid(now, config.staleMs)) return false;
        servo_.target = input.environmentLatched ? config_.servoOpen : config_.servoClosed;
    }
    if (servo_.mode == ControlMode::Hold || servo_.busy || servo_.error ||
        (servo_.known && servo_.target == servo_.accepted)) return false;
    servo_.busy = true;
    angle = servoInFlight_ = servo_.target;
    return true;
}
bool ActuatorPolicy::takeCurtain(const Snapshot &input, const Config &config,
                                Millis now, int &steps, int &target)
{
    if (curtain_.mode == ControlMode::Automatic) {
        if (!input.light.valid(now, config.staleMs)) return false;
        if (input.lux >= config_.brightLux) curtain_.target = config_.curtainTravel;
        else if (input.lux <= config_.darkLux) curtain_.target = 0;
    }
    if (curtain_.mode == ControlMode::Hold || curtain_.busy || !curtain_.known ||
        curtain_.error || curtain_.target == curtain_.accepted) return false;
    if (curtain_.mode == ControlMode::Automatic && curtainCompleted_ &&
        now - lastCurtainCompletion_ < config_.curtainIntervalMs) return false;
    curtain_.busy = true;
    target = curtainInFlight_ = curtain_.target;
    steps = target - curtain_.accepted;
    return true;
}
void ActuatorPolicy::completed(bool servo, ActuatorResult result, Millis now)
{
    ChannelState &s = servo ? servo_ : curtain_;
    if (!s.busy) return;
    s.busy = false;
    s.error = result.ok ? 0 : (result.error ? result.error : EIO);
    s.known = result.ok;
    if (result.ok) s.accepted = servo ? servoInFlight_ : curtainInFlight_;
    if (!servo) {
        curtainCompleted_ = true;
        lastCurtainCompletion_ = now;
    }
}

ActuatorExecutor::ActuatorExecutor(Devices &devices, Callback callback)
    : devices_(devices), callback_(std::move(callback))
{
    servoThread_ = std::thread([this]() { run(true); });
    try { curtainThread_ = std::thread([this]() { run(false); }); }
    catch (...) {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        changed_.notify_all(); servoThread_.join(); throw;
    }
}
ActuatorExecutor::~ActuatorExecutor() { stop(); }
bool ActuatorExecutor::submitServo(int angle)
{
    return angle >= 0 && angle <= 180 && submit(true, angle);
}
bool ActuatorExecutor::submitCurtain(int steps)
{
    return steps != 0 && steps >= -8192 && steps <= 8192 && submit(false, steps);
}
bool ActuatorExecutor::submit(bool servo, int value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Job &job = servo ? servo_ : curtain_;
    if (stopping_ || job.occupied) return false;
    job.value = value;
    job.pending = job.occupied = true;
    changed_.notify_all();
    return true;
}
void ActuatorExecutor::run(bool servo)
{
    for (;;) {
        int value;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            Job &job = servo ? servo_ : curtain_;
            changed_.wait(lock, [&]() { return stopping_ || job.pending; });
            if (stopping_) return;
            value = job.value;
            job.pending = false;
            if (servo) servoAttempted_ = true;
        }
        const auto result = servo ? devices_.setServoAngle(value) : devices_.runStepper(value);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            (servo ? servo_ : curtain_).occupied = false;
        }
        if (callback_) callback_(servo, result, monotonicMs());
    }
}
ActuatorResult ActuatorExecutor::stop()
{
    if (stopped_) return release_; // Called only by the controller/destructor.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        servo_.pending = curtain_.pending = false;
    }
    changed_.notify_all();
    servoThread_.join(); curtainThread_.join();
    if (servoAttempted_) release_ = devices_.releaseServo();
    stopped_ = true;
    return release_;
}
}
