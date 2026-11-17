#ifndef MONITOR_SAMPLING_H
#define MONITOR_SAMPLING_H
#include "devices.h"
#include "monitor.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace monitor {
struct SamplingConfig {
    int dhtPeriodMs = 2000;
    int lightPeriodMs = 1000;
    int pirPollMs = 200;
    int offlineRetryMs = 1000;
};
struct SampleCallbacks {
    std::function<void(DhtReading, Millis)> dht;
    std::function<void(LightReading, Millis)> light;
    std::function<void(PirReading, Millis)> pir;
};
class Sampler {
public:
    Sampler(Devices &devices, const SampleCallbacks &callbacks,
            const SamplingConfig &config = SamplingConfig());
    ~Sampler();
    void start();
    void stop();
private:
    bool waitUntil(Millis deadline);
    Devices &devices_;
    SampleCallbacks callbacks_;
    SamplingConfig config_;
    std::atomic<bool> stopping_{false};
    bool started_ = false;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread dht_, light_, pir_;
};
}
#endif
