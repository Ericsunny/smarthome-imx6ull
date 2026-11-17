#include "devices.h"
#include <chrono>
#include <cerrno>

namespace monitor {
DhtReading SimDevices::readDht()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return DhtReading(dhtOk_, temp_, humidity_, dhtOk_ ? 0 : ETIMEDOUT);
}
LightReading SimDevices::readLight()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return LightReading(lightOk_, lux_, lightOk_ ? 0 : EIO);
}
PirReading SimDevices::waitPir(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                      [this]() { return pending_ || interrupted_; });
    if (interrupted_) return PirReading();
    if (!pirOnline_) return PirReading(PirKind::Offline, false, ENODEV);
    if (!pending_) return PirReading();
    pending_ = false;
    return PirReading(PirKind::Event, high_);
}
OutputResult SimDevices::applyOutputs(bool led, bool beep)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (outputFailure_) return OutputResult(false, false, EIO, EIO);
    led_ = led;
    beep_ = beep;
    return OutputResult(true, true);
}
void SimDevices::interrupt()
{
    std::lock_guard<std::mutex> lock(mutex_);
    interrupted_ = true;
    changed_.notify_all();
}
void SimDevices::setDht(bool ok, int temperature, int humidity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    dhtOk_ = ok; temp_ = temperature; humidity_ = humidity;
}
void SimDevices::setLight(bool ok, double lux)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lightOk_ = ok; lux_ = lux;
}
void SimDevices::setPir(bool online, bool high)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pirOnline_ = online; high_ = high; pending_ = true;
    changed_.notify_all();
}
void SimDevices::failOutputs(bool fail)
{
    std::lock_guard<std::mutex> lock(mutex_);
    outputFailure_ = fail;
}
bool SimDevices::led() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return led_;
}
bool SimDevices::beep() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return beep_;
}
}
