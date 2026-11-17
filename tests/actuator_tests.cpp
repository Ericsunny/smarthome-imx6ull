#include "actuators.h"
#include "sampling.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
#define CHECK(c) do { if (!(c)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #c); } while (0)
using namespace monitor;
void inputs(Monitor &m, Millis now, int temperature = 24, double lux = 120)
{
    m.dhtResult(true, temperature, 50, 0, now);
    m.lightResult(true, lux, 0, now);
    m.pirResult(false, now);
}
void modesAndFreshness()
{
    Monitor m; ActuatorPolicy p; int angle;
    inputs(m, 100, 32);
    CHECK(!p.takeServo(m.state(), m.config(), 100, angle));
    p.servoMode(ControlMode::Automatic);
    CHECK(p.takeServo(m.state(), m.config(), 100, angle) && angle == 90);
    CHECK(!p.takeServo(m.state(), m.config(), 101, angle));
    p.completed(true, ActuatorResult(true), 102);
    CHECK(p.servo().known && p.servo().accepted == 90);
    CHECK(!p.takeServo(m.state(), m.config(), 103, angle));
    m.dhtResult(true, 29, 50, 0, 104);
    CHECK(!p.takeServo(m.state(), m.config(), 104, angle));
    m.dhtResult(true, 28, 50, 0, 105);
    m.dhtResult(false, 0, 0, 5, 106);
    CHECK(!p.takeServo(m.state(), m.config(), 106, angle));
    inputs(m, 107);
    CHECK(!p.takeServo(m.state(), m.config(), 6000, angle));
    CHECK(p.manualServo(45));
    CHECK(p.takeServo(m.state(), m.config(), 6000, angle) && angle == 45);
    p.completed(true, ActuatorResult(true), 6001);
    inputs(m, 6002, 32);
    CHECK(!p.takeServo(m.state(), m.config(), 6002, angle)); // Manual persists.
    p.servoMode(ControlMode::Automatic);
    CHECK(p.takeServo(m.state(), m.config(), 6002, angle) && angle == 90);
    p.completed(true, ActuatorResult(false, 5), 6003);
    CHECK(p.fault() && !p.servo().known);
    CHECK(!p.takeServo(m.state(), m.config(), 6004, angle)); // No blind retry.
    p.servoMode(ControlMode::Hold);
    CHECK(p.fault());
    p.servoMode(ControlMode::Automatic);
    CHECK(p.takeServo(m.state(), m.config(), 6005, angle));
    m.setActuatorFault(true, 6005);
    CHECK(m.state().desiredLed && !m.state().desiredBeep);
}
void curtainReferenceDwellAndLatestTarget()
{
    Monitor m; ActuatorPolicy p; int steps, target;
    inputs(m, 100);
    p.curtainMode(ControlMode::Automatic);
    CHECK(!p.takeCurtain(m.state(), m.config(), 100, steps, target));
    CHECK(!p.manualCurtain(true));
    CHECK(p.confirmCurtainClosed());
    p.curtainMode(ControlMode::Automatic);
    CHECK(p.takeCurtain(m.state(), m.config(), 100, steps, target));
    CHECK(steps == 2048 && target == 2048);
    CHECK(!p.confirmCurtainClosed());
    inputs(m, 101, 24, 10);
    CHECK(!p.takeCurtain(m.state(), m.config(), 101, steps, target));
    CHECK(p.curtain().target == 0); // Latest target, no queue of relative steps.
    p.completed(false, ActuatorResult(true), 102);
    CHECK(p.curtain().accepted == 2048);
    inputs(m, 30000, 24, 50);
    CHECK(!p.takeCurtain(m.state(), m.config(), 30000, steps, target));
    CHECK(p.takeCurtain(m.state(), m.config(), 30102, steps, target));
    CHECK(steps == -2048 && target == 0);
    p.completed(false, ActuatorResult(true), 30103);
    inputs(m, 30104, 24, 120);
    CHECK(!p.takeCurtain(m.state(), m.config(), 30104, steps, target));
    CHECK(p.manualCurtain(true)); // Manual bypasses dwell and input freshness.
    CHECK(p.takeCurtain(m.state(), m.config(), 40000, steps, target));
    p.completed(false, ActuatorResult(false, 4), 40001);
    CHECK(!p.curtain().known && p.fault());
    p.curtainMode(ControlMode::Automatic);
    CHECK(!p.takeCurtain(m.state(), m.config(), 40002, steps, target));
    CHECK(!p.manualCurtain(false));
    CHECK(p.confirmCurtainClosed());
    CHECK(!p.fault() && p.curtain().mode == ControlMode::Hold);
    p.curtainMode(ControlMode::Automatic);
    CHECK(!p.takeCurtain(m.state(), m.config(), 80000, steps, target)); // Stale.
    inputs(m, 80001, 24, 50);
    CHECK(!p.takeCurtain(m.state(), m.config(), 80001, steps, target)); // Hysteresis band.
    inputs(m, 80002, 24, 120);
    m.lightResult(false, 0, 5, 80003);
    CHECK(!p.takeCurtain(m.state(), m.config(), 80003, steps, target));
}
class GatedStepper : public SimDevices {
public:
    ActuatorResult runStepper(int steps) override
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        entered = true; gate.notify_all();
        gate.wait_for(lock, std::chrono::seconds(3), [&]() { return released; });
        lock.unlock();
        return SimDevices::runStepper(steps);
    }
    void releaseGate()
    {
        std::lock_guard<std::mutex> lock(gateMutex);
        released = true; gate.notify_all();
    }
    std::mutex gateMutex;
    std::condition_variable gate;
    bool entered = false, released = false;
};
void slowMotionIsolationAndShutdown()
{
    GatedStepper devices;
    std::mutex mutex; std::condition_variable changed;
    int light = 0, servoDone = 0, curtainDone = 0;
    ActuatorExecutor executor(devices, [&](bool servo, ActuatorResult r, Millis) {
        std::lock_guard<std::mutex> lock(mutex);
        if (r.ok) { if (servo) ++servoDone; else ++curtainDone; }
        changed.notify_all();
    });
    SampleCallbacks callbacks;
    callbacks.light = [&](LightReading r, Millis) {
        std::lock_guard<std::mutex> lock(mutex);
        if (r.ok) ++light;
        changed.notify_all();
    };
    SamplingConfig config; config.lightPeriodMs = 20; config.pirPollMs = 20;
    Sampler sampler(devices, callbacks, config); sampler.start();
    const bool submitted = executor.submitCurtain(2048);
    bool entered;
    {
        std::unique_lock<std::mutex> lock(devices.gateMutex);
        entered = devices.gate.wait_for(lock, std::chrono::seconds(1), [&]() { return devices.entered; });
    }
    const bool duplicateRejected = !executor.submitCurtain(-2048);
    int baseline;
    { std::lock_guard<std::mutex> lock(mutex); baseline = light; }
    const bool servoSubmitted = executor.submitServo(90);
    bool independent; int observed;
    {
        std::unique_lock<std::mutex> lock(mutex);
        independent = changed.wait_for(lock, std::chrono::seconds(1), [&]() {
            return servoDone == 1 && light >= baseline + 4;
        });
        observed = light - baseline;
        independent = independent && curtainDone == 0;
    }
    devices.releaseGate();
    const auto released = executor.stop(); sampler.stop();
    CHECK(submitted && entered && duplicateRejected && servoSubmitted && independent);
    CHECK(released.ok && !devices.servoEnabled() && devices.stepperSteps() == 2048);
    CHECK(!executor.submitServo(0) && executor.stop().ok);
    std::printf("{\"measurement\":\"gated_stepper_host_substitute\",\"light_updates_while_blocked\":%d,"
                "\"servo_completed_while_blocked\":true,\"duplicate_step_rejected\":true,\"board_proof\":false}\n", observed);
}
void invalidConfigAndReleaseFailure()
{
    ActuatorConfig bad; bad.curtainTravel = 2049;
    bool rejected = false;
    try { ActuatorPolicy policy(bad); } catch (const std::invalid_argument &) { rejected = true; }
    CHECK(rejected);
    SimDevices devices;
    std::mutex mutex; std::condition_variable changed; bool done = false;
    ActuatorExecutor executor(devices, [&](bool, ActuatorResult, Millis) {
        std::lock_guard<std::mutex> lock(mutex); done = true; changed.notify_all();
    });
    CHECK(!executor.submitServo(181) && !executor.submitCurtain(8193));
    CHECK(executor.submitServo(90));
    bool completed;
    { std::unique_lock<std::mutex> lock(mutex);
      completed = changed.wait_for(lock, std::chrono::seconds(1), [&]() { return done; }); }
    devices.failActuators(true, false);
    CHECK(!executor.stop().ok && completed && devices.servoEnabled());
}
}
int main()
{
    const std::vector<std::pair<const char *, std::function<void()> > > tests = {
        {"servo_modes_freshness_hysteresis_failure", modesAndFreshness},
        {"curtain_reference_dwell_latest_target_failure", curtainReferenceDwellAndLatestTarget},
        {"slow_stepper_isolation_shutdown", slowMotionIsolationAndShutdown},
        {"calibration_validation_disable_failure", invalidConfigAndReleaseFailure}
    };
    unsigned failures = 0;
    for (const auto &test : tests) {
        try { test.second(); std::printf("PASS %s\n", test.first); }
        catch (const std::exception &e) { ++failures; std::printf("FAIL %s: %s\n", test.first, e.what()); }
    }
    std::printf("{\"suite\":\"actuator_core\",\"tests\":%u,\"failures\":%u,\"board_proof\":false}\n", unsigned(tests.size()), failures);
    return failures ? 1 : 0;
}
