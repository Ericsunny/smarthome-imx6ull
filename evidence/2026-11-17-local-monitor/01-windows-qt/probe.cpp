#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window;
    QVBoxLayout layout(&window);
    QLabel label("Qt Widgets probe");
    QPushButton button("Probe");
    layout.addWidget(&label);
    layout.addWidget(&button);
    int clicks = 0;
    QObject::connect(&button, &QPushButton::clicked, [&]() { ++clicks; });
    window.resize(320, 120);
    window.show();
    QTimer::singleShot(50, [&]() {
        button.click();
        const bool rendered = !window.grab().isNull();
        const bool pass = clicks == 1 && rendered;
        std::printf("{\"qt_version\":\"%s\",\"pointer_bits\":%d,\"clicks\":%d,"
                    "\"rendered\":%s,\"pass\":%s}\n",
                    qVersion(), int(sizeof(void *) * 8), clicks,
                    rendered ? "true" : "false", pass ? "true" : "false");
        app.exit(pass ? 0 : 1);
    });
    return app.exec();
}
