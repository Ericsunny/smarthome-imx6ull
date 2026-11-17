#include "mainwindow.h"
#include <QApplication>
#include <QFontDatabase>
#include <QTimer>
#include <csignal>
#include <cstdio>
#include <cstring>

namespace {
volatile std::sig_atomic_t stopRequested = 0;
void requestStop(int) { stopRequested = 1; }
}
int main(int argc, char **argv)
{
    bool simulated = false, smoke = false;
    std::string root = "/dev";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--simulate")) simulated = true;
        else if (!std::strcmp(argv[i], "--smoke-test")) smoke = true;
        else if (!std::strcmp(argv[i], "--device-root") && i + 1 < argc) root = argv[++i];
        else if (!std::strcmp(argv[i], "--help")) {
            std::puts("smarthome [--simulate] [--smoke-test] [--device-root /dev]");
            return 0;
        } else { std::fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]); return 2; }
    }
    if (smoke && !simulated) {
        std::fputs("--smoke-test requires explicit --simulate\n", stderr); return 2;
    }
    std::unique_ptr<monitor::Devices> devices = simulated ?
        std::unique_ptr<monitor::Devices>(new monitor::SimDevices) : monitor::linuxDevices(root);
    if (!devices) {
        std::fputs("real devices require Linux; use --simulate on Windows\n", stderr); return 2;
    }
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    QApplication app(argc, argv);
    QString fontFile = qEnvironmentVariable("MONITOR_FONT");
#ifdef Q_OS_WIN
    // The offscreen plugin does not always discover Windows system fonts.
    if (fontFile.isEmpty()) fontFile = qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc";
#endif
    if (!fontFile.isEmpty()) {
        const int id = QFontDatabase::addApplicationFont(fontFile);
        if (id < 0) { std::fputs("cannot load MONITOR_FONT/system font\n", stderr); return 2; }
        const auto families = QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) return 2;
        app.setFont(QFont(families.first(), 10));
    }
    MainWindow window(std::move(devices), simulated, smoke);
    QObject::connect(&app, &QApplication::aboutToQuit, &window, &MainWindow::shutdown);
    QTimer stopTimer;
    QObject::connect(&stopTimer, &QTimer::timeout, [&]() { if (stopRequested) app.quit(); });
    stopTimer.start(100);
    window.show();
    if (smoke) window.startSmokeTest();
    const int result = app.exec();
    window.shutdown();
    return result ? result : (window.shutdownSucceeded() ? 0 : 1);
}
