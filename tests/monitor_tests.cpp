#include "monitor.h"
#include "sampling.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <vector>
#ifdef __linux__
#include <cstdlib>
#include <unistd.h>
#endif

namespace {
#define CHECK(c) do { if (!(c)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #c); } while (0)
void healthy(monitor::Monitor &m, monitor::Millis now = 100)
{
    m.dhtResult(true, 24, 50, 0, now);
    m.lightResult(true, 120, 0, now);
    m.pirResult(false, now);
}
void startupAndMotion()
{
    monitor::Monitor m;
    CHECK(!m.state().armed && m.state().fault && !m.state().desiredBeep);
    healthy(m);
    CHECK(!m.state().fault && !m.state().desiredLed);
    m.pirResult(true, 101);
    CHECK(!m.state().desiredBeep && m.state().causes == 0);
    m.setArmed(true, 102);
    CHECK(m.state().desiredBeep && m.state().causes == monitor::Activity);
    CHECK(m.state().alarmEpisodes == 1);
    m.silence(103);
    for (int i = 0; i < 100; ++i) m.pirResult(true, 104 + i);
    CHECK(m.state().muted && !m.state().desiredBeep && m.state().alarmEpisodes == 1);
    m.pirResult(false, 210);
    m.pirResult(true, 211);
    CHECK(m.state().desiredBeep && m.state().alarmEpisodes == 2);
    m.setArmed(false, 212);
    CHECK(!m.state().desiredBeep && !(m.state().causes & monitor::Activity));
}
void environmentalHysteresisAndFailure()
{
    monitor::Monitor m;
    healthy(m);
    m.dhtResult(true, 30, 50, 0, 110);
    CHECK(m.state().environmentLatched && m.state().desiredLed && !m.state().desiredBeep);
    m.setArmed(true, 111);
    CHECK(m.state().desiredBeep);
    m.dhtResult(true, 29, 50, 0, 112);
    CHECK(m.state().environmentLatched);
    m.dhtResult(false, 0, 0, 110, 113);
    CHECK(m.state().fault && m.state().environmentLatched);
    CHECK(m.state().temperature == 29 && m.state().dhtFailures == 1);
    m.tick(7000);
    CHECK(m.state().environmentLatched && m.state().desiredBeep);
    m.dhtResult(true, 28, 71, 0, 7001);
    CHECK(m.state().environmentLatched);
    m.dhtResult(true, 28, 70, 0, 7002);
    CHECK(!m.state().environmentLatched && !m.state().desiredBeep);
}
void freshnessAndIndependentCauses()
{
    monitor::Monitor m;
    healthy(m);
    m.tick(5101);
    CHECK(m.state().fault && m.state().dht.status(5101, 5000) == "stale");
    CHECK(!m.state().desiredBeep);
    healthy(m, 5200);
    m.setArmed(true, 5201);
    m.pirResult(true, 5202);
    m.silence(5203);
    m.dhtResult(true, 31, 50, 0, 5204);
    CHECK(m.state().desiredBeep && !m.state().muted);
    CHECK(m.state().causes == (monitor::Environment | monitor::Activity));
    m.pirOffline(19, 5205);
    CHECK(m.state().fault && !m.state().pirKnown);
    CHECK(m.state().causes == monitor::Environment);
    m.dhtResult(true, 24, 50, 0, 5206);
    CHECK(m.state().fault && !m.state().desiredBeep);
    m.lightResult(false, 0, 5, 5207);
    CHECK(m.state().light.status(5207, 5000) == "failed");
}
void invalidDataAndConfig()
{
    monitor::Monitor m;
    m.dhtResult(true, 99, 50, 0, 100);
    CHECK(!m.state().dht.seen && m.state().dhtFailures == 1);
    m.lightResult(true, -1, 0, 100);
    CHECK(!m.state().light.seen && m.state().lightFailures == 1);
    monitor::Config bad;
    bad.tempOff = bad.tempOn;
    bool threw = false;
    try { monitor::Monitor rejected(bad); } catch (const std::invalid_argument &) { threw = true; }
    CHECK(threw);
}

class GatedDht : public monitor::SimDevices {
public:
    monitor::DhtReading readDht() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait_for(lock, std::chrono::seconds(3), [this]() { return released; });
        return monitor::DhtReading(false, 0, 0, 110);
    }
    void interrupt() override
    {
        { std::lock_guard<std::mutex> lock(mutex); released = true; condition.notify_all(); }
        monitor::SimDevices::interrupt();
    }
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false, released = false;
};
void samplingIsolationAndStop()
{
    GatedDht devices;
    std::mutex mutex;
    std::condition_variable samples;
    std::vector<monitor::Millis> lightTimes;
    int dhtCompleted = 0;
    monitor::SampleCallbacks callbacks;
    callbacks.dht = [&](monitor::DhtReading, monitor::Millis) {
        std::lock_guard<std::mutex> lock(mutex); ++dhtCompleted;
    };
    callbacks.light = [&](monitor::LightReading r, monitor::Millis time) {
        std::lock_guard<std::mutex> lock(mutex);
        if (r.ok) lightTimes.push_back(time);
        samples.notify_all();
    };
    monitor::SamplingConfig config;
    config.dhtPeriodMs = 2000; config.lightPeriodMs = 20; config.pirPollMs = 20;
    monitor::Sampler sampler(devices, callbacks, config);
    sampler.start();
    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(devices.mutex);
        entered = devices.condition.wait_for(lock, std::chrono::seconds(2),
                                              [&]() { return devices.entered; });
    }
    bool observed = false;
    std::size_t baseline = 0;
    {
        std::unique_lock<std::mutex> lock(mutex);
        baseline = lightTimes.size();
        observed = samples.wait_for(lock, std::chrono::seconds(2),
                                     [&]() { return lightTimes.size() >= baseline + 4; });
    }
    int completedWhileBlocked;
    { std::lock_guard<std::mutex> lock(mutex); completedWhileBlocked = dhtCompleted; }
    const auto stopStart = monitor::monotonicMs();
    sampler.stop();
    const auto stopMs = monitor::monotonicMs() - stopStart;
    CHECK(entered && observed && completedWhileBlocked == 0);
    CHECK(stopMs < 1000);
    bool singleUse = false;
    try { sampler.start(); } catch (const std::logic_error &) { singleUse = true; }
    CHECK(singleUse);
    monitor::Millis maximumGap = 0;
    for (std::size_t i = baseline + 1; i < lightTimes.size(); ++i)
        if (lightTimes[i] - lightTimes[i-1] > maximumGap) maximumGap = lightTimes[i] - lightTimes[i-1];
    std::printf("{\"measurement\":\"gated_dht_host_substitute\",\"light_samples_while_dht_blocked\":%u,"
                "\"max_light_gap_ms\":%lld,\"stop_ms\":%lld,\"board_proof\":false}\n",
                unsigned(lightTimes.size() - baseline), (long long)maximumGap, (long long)stopMs);
}
void outputFailureAndRecovery()
{
    monitor::SimDevices devices;
    auto ok = devices.applyOutputs(true, true);
    CHECK(ok.ledOk && ok.beepOk && devices.beep());
    devices.failOutputs(true);
    auto failed = devices.applyOutputs(false, false);
    CHECK(!failed.ledOk && !failed.beepOk && devices.beep());
    devices.failOutputs(false);
    ok = devices.applyOutputs(false, false);
    CHECK(ok.ledOk && ok.beepOk && !devices.beep());
}
void linuxNodeRejection()
{
#ifdef __linux__
    char path[] = "/tmp/local-monitor-node-test-XXXXXX";
    CHECK(mkdtemp(path) != nullptr);
    const std::string root(path);
    for (const char *node : {"dht11", "ap3216c", "hc_sr505", "led", "beep", "sg90", "step28byj48"})
        std::ofstream(root + "/" + node) << "do not overwrite";
    auto devices = monitor::linuxDevices(root);
    const auto dht = devices->readDht();
    const auto light = devices->readLight();
    const auto pir = devices->waitPir(20);
    const auto output = devices->applyOutputs(false, false);
    CHECK(!dht.ok && !light.ok && pir.kind == monitor::PirKind::Offline);
    CHECK(!output.ledOk && !output.beepOk);
    CHECK(!devices->setServoAngle(90).ok && !devices->runStepper(2048).ok &&
          !devices->releaseServo().ok);
    std::ifstream led(root + "/led");
    std::string content;
    std::getline(led, content);
    CHECK(content == "do not overwrite");
    devices.reset();
    for (const char *node : {"dht11", "ap3216c", "hc_sr505", "led", "beep", "sg90", "step28byj48"})
        unlink((root + "/" + node).c_str());
    rmdir(root.c_str());
#else
    CHECK(!monitor::linuxDevices());
#endif
}
}
int main()
{
    const std::vector<std::pair<const char *, std::function<void()> > > tests = {
        {"startup_motion_mute_rearm", startupAndMotion},
        {"environment_hysteresis_failure", environmentalHysteresisAndFailure},
        {"freshness_independent_causes", freshnessAndIndependentCauses},
        {"invalid_data_config", invalidDataAndConfig},
        {"sampling_isolation_stop", samplingIsolationAndStop},
        {"output_failure_recovery", outputFailureAndRecovery},
        {"platform_node_rejection", linuxNodeRejection}
    };
    unsigned failures = 0;
    for (const auto &test : tests) {
        try { test.second(); std::printf("PASS %s\n", test.first); }
        catch (const std::exception &e) { ++failures; std::printf("FAIL %s: %s\n", test.first, e.what()); }
    }
    std::printf("{\"suite\":\"monitor_core\",\"tests\":%u,\"failures\":%u,\"board_proof\":false}\n",
                unsigned(tests.size()), failures);
    return failures ? 1 : 0;
}
