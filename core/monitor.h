#ifndef LOCAL_MONITOR_H
#define LOCAL_MONITOR_H

#include <cstdint>
#include <string>

namespace monitor {
typedef std::int64_t Millis;
Millis monotonicMs();

struct Config {
    int tempOn = 30;
    int tempOff = 28;
    int humidityOn = 75;
    int humidityOff = 70;
    Millis staleMs = 5000;
};

struct SampleState {
    bool seen = false;
    bool lastAttemptOk = false;
    Millis lastSuccess = 0;
    Millis lastAttempt = 0;
    int error = 0;
    bool valid(Millis now, Millis staleMs) const;
    std::string status(Millis now, Millis staleMs) const;
};

enum AlarmCause { Environment = 1, Activity = 2 };

struct Snapshot {
    bool armed = false;
    bool muted = false;
    bool environmentLatched = false;
    bool pirKnown = false;
    bool pirHigh = false;
    int pirError = 0;
    int temperature = 0;
    int humidity = 0;
    double lux = 0;
    SampleState dht;
    SampleState light;
    unsigned causes = 0;
    bool fault = true;
    bool actuatorFault = false;
    bool desiredLed = true;
    bool desiredBeep = false;
    unsigned alarmEpisodes = 0;
    unsigned dhtAttempts = 0;
    unsigned dhtFailures = 0;
    unsigned lightAttempts = 0;
    unsigned lightFailures = 0;
};

// One owner (the UI/controller thread). Sampling threads deliver results only.
class Monitor {
public:
    explicit Monitor(const Config &config = Config());
    void dhtResult(bool ok, int temp, int humidity, int error, Millis now);
    void lightResult(bool ok, double lux, int error, Millis now);
    void pirResult(bool high, Millis now);
    void pirOffline(int error, Millis now);
    void setArmed(bool armed, Millis now);
    void silence(Millis now);
    void setActuatorFault(bool failed, Millis now);
    void tick(Millis now);
    const Snapshot &state() const { return state_; }
    const Config &config() const { return config_; }
private:
    void update(Millis now);
    Config config_;
    Snapshot state_;
};
}
#endif
