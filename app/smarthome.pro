QT += widgets
TEMPLATE = app
TARGET = smarthome
CONFIG += console
CONFIG -= app_bundle
greaterThan(QT_MAJOR_VERSION, 5): CONFIG += c++17
else: CONFIG += c++11
SOURCES += main.cpp mainwindow.cpp ../core/monitor.cpp ../core/devices.cpp \
           ../core/linux_devices.cpp ../core/sampling.cpp
HEADERS += mainwindow.h bridge.h ../core/monitor.h ../core/devices.h ../core/sampling.h
unix: LIBS += -pthread
