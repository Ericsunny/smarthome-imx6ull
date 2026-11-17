#include "monitor.h"
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace monitor {
Millis monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool SampleState::valid(Millis now, Millis staleMs) const
{
    return seen && lastAttemptOk && now >= lastSuccess && now - lastSuccess <= staleMs;
}

std::string SampleState::status(Millis now, Millis staleMs) const
{
    if (!lastAttemptOk && error) return "failed";
    if (!seen) return "unknown";
    return valid(now, staleMs) ? "valid" : "stale";
}

Monitor::Monitor(const Config &config) : config_(config)
{
    if (config.tempOff >= config.tempOn || config.humidityOff >= config.humidityOn ||
        config.staleMs <= 0)
        throw std::invalid_argument("invalid thresholds or freshness interval");
}

void Monitor::dhtResult(bool ok, int temp, int humidity, int error, Millis now)
{
    ++state_.dhtAttempts;
    ok = ok && temp >= 0 && temp <= 50 && humidity >= 0 && humidity <= 100;
    state_.dht.lastAttempt = now;
    state_.dht.lastAttemptOk = ok;
    state_.dht.error = ok ? 0 : (error ? error : -1);
    if (ok) {
        state_.dht.seen = true;
        state_.dht.lastSuccess = now;
        state_.temperature = temp;
        state_.humidity = humidity;
        if (temp >= config_.tempOn || humidity >= config_.humidityOn)
            state_.environmentLatched = true;
        else if (temp <= config_.tempOff && humidity <= config_.humidityOff)
            state_.environmentLatched = false;
    } else {
        ++state_.dhtFailures;
    }
    update(now);
}

void Monitor::lightResult(bool ok, double lux, int error, Millis now)
{
    ++state_.lightAttempts;
    ok = ok && std::isfinite(lux) && lux >= 0;
    state_.light.lastAttempt = now;
    state_.light.lastAttemptOk = ok;
    state_.light.error = ok ? 0 : (error ? error : -1);
    if (ok) {
        state_.light.seen = true;
        state_.light.lastSuccess = now;
        state_.lux = lux;
    } else {
        ++state_.lightFailures;
    }
    update(now);
}

void Monitor::pirResult(bool high, Millis now)
{
    state_.pirKnown = true;
    state_.pirHigh = high;
    state_.pirError = 0;
    update(now);
}

void Monitor::pirOffline(int error, Millis now)
{
    state_.pirKnown = false;
    state_.pirHigh = false;
    state_.pirError = error ? error : -1;
    update(now);
}

void Monitor::setArmed(bool armed, Millis now)
{
    if (state_.armed != armed) state_.muted = false;
    state_.armed = armed;
    update(now);
}

void Monitor::silence(Millis now)
{
    state_.muted = state_.causes != 0;
    update(now);
}

void Monitor::tick(Millis now) { update(now); }
void Monitor::setActuatorFault(bool failed, Millis now)
{
    state_.actuatorFault = failed;
    update(now);
}

void Monitor::update(Millis now)
{
    unsigned causes = state_.environmentLatched ? unsigned(Environment) : 0;
    if (state_.armed && state_.pirKnown && state_.pirHigh) causes |= Activity;
    const unsigned newCauses = causes & ~state_.causes;
    if (newCauses) {
        state_.muted = false;
        ++state_.alarmEpisodes;
    }
    if (!causes) state_.muted = false;
    state_.causes = causes;
    state_.fault = !state_.dht.valid(now, config_.staleMs) ||
                   !state_.light.valid(now, config_.staleMs) || !state_.pirKnown ||
                   state_.actuatorFault;
    state_.desiredLed = causes || state_.fault;
    state_.desiredBeep = state_.armed && causes && !state_.muted;
}
}
