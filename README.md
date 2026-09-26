# 智能安防监控系统 (I.MX6ULL)

基于正点原子 ATK-IMX6ULL 开发板的智能安防监控系统。

![Arch](https://img.shields.io/badge/Platform-I.MX6ULL_Cortex--A7-blue) ![Kernel](https://img.shields.io/badge/Kernel-4.1.15-green) ![QT](https://img.shields.io/badge/QT-5.12.9-green) ![OpenCV](https://img.shields.io/badge/OpenCV-3.4.1-yellow)

## 功能

- 🌡️ 温湿度/光照/距离实时监测（DHT11 / AP3216C / HC-SR04）
- 🚨 红外人体检测 + 摄像头人脸识别 → 多传感器融合报警
- 📷 USB 免驱摄像头实时监控 + Haar 人脸检测 + LBPH 身份识别
- 🕐 SeetaFace2 考勤打卡（PC 端服务器 + TCP 协议）
- 🌡️ 温湿度超标自动开风扇（阈值联动）
- 🌙 光照感应自动开关窗帘 + 定时开关
- 🖥️ QT 多线程界面（5 工作线程 + 信号槽通信 + 温度趋势图）

## 架构

```
┌──────────────────────────────────────────┐
│  QT 应用层 (C++11, 多线程)                │
│  UI主线程 + 4个Worker线程 + SeetaClient   │
│  布防/撤防状态机 + 多传感器融合报警        │
├──────────────────────────────────────────┤
│  接口协议层 (/dev 节点 + ioctl 命令字)     │
├──────────────────────────────────────────┤
│  Linux 驱动层 (7 个手写 platform 模块)     │
│  + uvcvideo.ko (USB摄像头)               │
│  + AP3216C 内核自带驱动 (ioctl 适配)      │
├──────────────────────────────────────────┤
│  Linux 4.1.15 + 设备树 (9 自定义节点)     │
└──────────────────────────────────────────┘
```

## 快速浏览

```
experiments 不在本仓库(见 VM) →
01-驱动层/       8 个模块驱动 + 8 个测试程序 (2040 行 C)
02-应用层QT/     QT 界面 + 4 个工作线程 + 设备封装层 (570 行 C++)
03-设备树/       完整 dts + 自定义节点摘录 (159 行)
04-接口协议/     8 设备接口备忘
05-摄像头工具/   camcap 单帧抓图 + facetest 人脸链路诊断
```

## 构建

```bash
# 驱动 (交叉编译 gcc-linaro-4.9.4)
cd smarthome-drivers
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

# QT 应用 (poky SDK gcc 5.3)
source /opt/fsl-imx-x11/4.1.15-2.1.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
cd smarthome-qt
qmake && make -j4
```

## 关键技术

| 模块 | 技术 | 亮点 |
|------|------|------|
| DHT11 | 双边沿中断 + ktime_get_ns 时间戳解码 | 微秒级单总线时序，不用忙等 |
| HC-SR04 | 双边沿中断测脉宽 + div_u64 | cm = us / 58 |
| SG90 | PWM 子系统 50Hz | 0.5~2.5ms ↔ 0~180° |
| 28BYJ48 | 四相八拍 + usleep_range | 4096 拍/圈, mutex+Ctrl+C |
| AP3216C | I2C 两条 msg 随机读 | id_table 匹配 (老内核) |
| 摄像头 | V4L2 mmap + Haar + LBPH | 格式枚举协商 + ROI 跟踪 |
| 考勤 | TCP + SeetaFace2 + SQLite | 8字节长度头 + JPEG + JSON |
| 报警 | 多传感器融合 | SR505+摄像头双确认, 布防/撤防 |
