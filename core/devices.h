#ifndef MONITOR_DEVICES_H
#define MONITOR_DEVICES_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace monitor {
struct DhtReading {
    bool ok;
    int temperature, humidity, error;
    DhtReading(bool good = false, int t = 0, int h = 0, int e = 0)
        : ok(good), temperature(t), humidity(h), error(e) {}
};
struct LightReading {
    bool ok;
    double lux;
    int error;
    LightReading(bool good = false, double value = 0, int e = 0)
        : ok(good), lux(value), error(e) {}
};
enum class PirKind { Event, Timeout, Offline };
struct PirReading {
    PirKind kind;
    bool high;
    int error;
    PirReading(PirKind k = PirKind::Timeout, bool value = false, int e = 0)
        : kind(k), high(value), error(e) {}
};
struct OutputResult {
    bool ledOk, beepOk;
    int ledError, beepError;
    OutputResult(bool l = false, bool b = false, int le = 0, int be = 0)
        : ledOk(l), beepOk(b), ledError(le), beepError(be) {}
};
struct ActuatorResult {
    bool ok;
    int error;
    ActuatorResult(bool good = false, int e = 0) : ok(good), error(e) {}
};

// One sampling thread per input. applyOutputs has one controller-thread caller.
class Devices {
public:
    virtual ~Devices() {}
    virtual DhtReading readDht() = 0;
    virtual LightReading readLight() = 0;
    virtual PirReading waitPir(int timeoutMs) = 0;
    virtual OutputResult applyOutputs(bool led, bool beep) = 0;
    virtual ActuatorResult setServoAngle(int angle) = 0;
    virtual ActuatorResult runStepper(int steps) = 0;
    virtual ActuatorResult releaseServo() = 0;
    virtual void interrupt() {}
};

class SimDevices : public Devices {
public:
    DhtReading readDht() override;
    LightReading readLight() override;
    PirReading waitPir(int timeoutMs) override;
    OutputResult applyOutputs(bool led, bool beep) override;
    ActuatorResult setServoAngle(int angle) override;
    ActuatorResult runStepper(int steps) override;
    ActuatorResult releaseServo() override;
    void interrupt() override;
    void setDht(bool ok, int temperature = 24, int humidity = 50);
    void setLight(bool ok, double lux = 120);
    void setPir(bool online, bool high);
    void failOutputs(bool fail);
    bool led() const;
    bool beep() const;
    void failActuators(bool servo, bool stepper);
    int servoAngle() const;
    int stepperSteps() const;
    bool servoEnabled() const;
private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool dhtOk_ = true, lightOk_ = true, pirOnline_ = true;
    int temp_ = 24, humidity_ = 50;
    double lux_ = 120;
    bool high_ = false, pending_ = true, interrupted_ = false;
    bool outputFailure_ = false, led_ = false, beep_ = false;
    bool servoFailure_ = false, stepperFailure_ = false, servoEnabled_ = false;
    int servoAngle_ = 0, stepperSteps_ = 0;
};

// Returns null outside Linux. It never substitutes simulated data implicitly.
std::unique_ptr<Devices> linuxDevices(const std::string &root = "/dev");
}
#endif
