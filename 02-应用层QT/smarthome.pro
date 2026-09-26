QT += widgets

TARGET = smarthome

CONFIG += c++11

# M4 真设备模式（2026-09-10 起）：LED/beep/ap3216c/dht11 真实驱动已就绪
# 需要临时回退模拟模式（板子不在手边调试 UI）时，恢复下面这行再 qmake：
# DEFINES += SIM_DEVICES

SOURCES += main.cpp mainwindow.cpp deviceio.cpp
HEADERS += mainwindow.h deviceio.h workers.h cameraworker.h

# 摄像头监控（2026-09-20 起）：V4L2 采集 + OpenCV Haar 人脸检测
# OpenCV 为自编 ARM 版（poky gcc 5.3, world 模式），在 VM 的 ~/opencv-arm
INCLUDEPATH += /home/alientek/opencv-arm/include
LIBS += -L/home/alientek/opencv-arm/lib -lopencv_world
