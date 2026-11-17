#include "sampling.h"
#include <chrono>
#include <stdexcept>

namespace monitor {
Sampler::Sampler(Devices &devices, const SampleCallbacks &callbacks,
                 const SamplingConfig &config)
    : devices_(devices), callbacks_(callbacks), config_(config)
{
    if (config.dhtPeriodMs <= 0 || config.lightPeriodMs <= 0 ||
        config.pirPollMs <= 0 || config.offlineRetryMs <= 0)
        throw std::invalid_argument("sampling intervals must be positive");
}
Sampler::~Sampler() { stop(); }
bool Sampler::waitUntil(Millis deadline)
{
    std::unique_lock<std::mutex> lock(mutex_);
    const Millis remaining = deadline - monotonicMs();
    if (remaining > 0)
        wake_.wait_for(lock, std::chrono::milliseconds(remaining),
                       [this]() { return stopping_.load(); });
    return !stopping_.load();
}
void Sampler::start()
{
    if (started_) throw std::logic_error("sampler is single-use");
    started_ = true;
    try {
        dht_ = std::thread([this]() {
            while (!stopping_) {
                const Millis start = monotonicMs();
                const DhtReading result = devices_.readDht();
                if (!stopping_ && callbacks_.dht) callbacks_.dht(result, monotonicMs());
                if (!waitUntil(start + config_.dhtPeriodMs)) break;
            }
        });
        light_ = std::thread([this]() {
            while (!stopping_) {
                const Millis start = monotonicMs();
                const LightReading result = devices_.readLight();
                if (!stopping_ && callbacks_.light) callbacks_.light(result, monotonicMs());
                if (!waitUntil(start + config_.lightPeriodMs)) break;
            }
        });
        pir_ = std::thread([this]() {
            while (!stopping_) {
                const Millis start = monotonicMs();
                const PirReading result = devices_.waitPir(config_.pirPollMs);
                if (!stopping_ && result.kind != PirKind::Timeout && callbacks_.pir)
                    callbacks_.pir(result, monotonicMs());
                // Also bounds a misbehaving backend that returns timeout immediately.
                const int interval = result.kind == PirKind::Offline ?
                    config_.offlineRetryMs : config_.pirPollMs;
                if (!waitUntil(start + interval)) break;
            }
        });
    } catch (...) {
        stop();
        throw;
    }
}
void Sampler::stop()
{
    if (!started_) return;
    stopping_ = true;
    wake_.notify_all();
    devices_.interrupt();
    if (dht_.joinable()) dht_.join();
    if (light_.joinable()) light_.join();
    if (pir_.joinable()) pir_.join();
}
}
