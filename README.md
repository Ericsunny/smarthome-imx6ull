# 嵌入式 Linux 多外设驱动与 Qt 应用系统（i.MX6ULL 智能安防）

在正点原子 ATK-IMX6ULL 上从零写 8 类外设的 Linux 驱动、用设备树声明硬件、再用 Qt 多线程应用把它们串成一台可布防/撤防的智能安防终端——**项目的主线是"驱动 ↔ 设备树 ↔ 应用层"这三层怎么接起来**，安防功能只是这条主线的载体。

![Arch](https://img.shields.io/badge/Board-ATK--IMX6ULL_Cortex--A7-blue) ![Kernel](https://img.shields.io/badge/Kernel-4.1.15-green) ![QT](https://img.shields.io/badge/QT-5.12.9-green) ![OpenCV](https://img.shields.io/badge/OpenCV-3.4.1-yellow)

> 平台：NXP i.MX6ULL（ARM Cortex-A7 @ 792MHz）· 内核：Linux 4.1.15 · 工具链：arm-linux-gnueabihf-gcc（linaro 4.9.4）· 应用：Qt 5.12.9 交叉编译

## 系统架构

```
┌─────────────────────────────────────────────────┐
│  Qt 应用层 (C++11, 5 线程: UI + 4 Worker)        │
│  布防/撤防状态机 · 多传感器融合报警 · 曲线图      │
│  deviceio.cpp = 所有 /dev 节点的统一封装层        │
├─────────────────────────────────────────────────┤
│  接口协议层 (/dev 节点 + read/write/ioctl)       │
│  逐设备接口契约见 04-接口协议/                    │
├─────────────────────────────────────────────────┤
│  驱动层 (手写 8 模块, ~2100 行 C)                │
│  7 × platform_driver + 设备树(GPIO/PWM/中断)     │
│  1 × i2c_driver (AP3216C, 见下"出厂驱动冲突")    │
│  + uvcvideo (USB 摄像头, 内核自带)               │
├─────────────────────────────────────────────────┤
│  i.MX6ULL 4.1.15 BSP + 设备树 (9 个自定义节点)   │
└─────────────────────────────────────────────────┘
```

每一层的连接方式都是显式的：应用只认 `/dev` 节点和 ioctl 命令字（`04-接口协议/` 是完整契约表）；驱动只认设备树里的 `compatible` 与引脚/pinctrl（`03-设备树/自定义节点摘录.dts` 是全部 9 个节点的原文）。

## 演示与文档

- **整机操作脚本**：[M5-整机演示流程.md](M5-整机演示流程.md)（布防→触发→联动→撤防全流程）
- **项目总回顾**：[00-项目总回顾.md](00-项目总回顾.md)（每个模块的踩坑与修法）
- **摄像头链路验收**：[摄像头验收文档.md](摄像头验收文档.md)
- 实物图/演示视频：视频已录制，暂为私有资料，仓库内暂缺

## 仓库结构

```
01-驱动层/       8 个模块驱动 + 8 个测试程序(*App.c, 含 beepApp) + Makefile
02-应用层QT/     Qt 工程(smarthome.pro) + deviceio 封装层 + 工作线程 + 板上一键启动脚本
03-设备树/       完整 dts(基于出厂 dts 加 9 节点) + 自定义节点摘录
04-接口协议/     8 个 /dev 节点的接口契约与已知坑
05-摄像头工具/   camcap(V4L2 抓图) + facetest(人脸链路诊断)
```

## 复现指南

**环境（作者实测组合）**

| 项 | 值 |
|---|---|
| 板卡 | 正点原子 ATK-IMX6ULL（EMMC 版，Cortex-A7 792MHz） |
| 内核 | 4.1.15（正点原子 alpha 出厂内核源码包） |
| 驱动交叉工具链 | arm-linux-gnueabihf-（linaro 4.9.4） |
| Qt/应用工具链 | poky SDK gcc 5.3（`environment-setup-cortexa7hf-neon-poky-linux-gnueabi`） |
| 依赖 | Qt 5.12.9 · OpenCV 3.4.1(ARM 版) · tslib；考勤另需 PC 端 SeetaFace2 + SQLite |
| 部署方式 | 驱动/应用经 TFTP + NFS rootfs（也可拷贝 EMMC）；设备树经 TFTP 加载 |

**1. 驱动**

```bash
cd 01-驱动层
# Makefile 的 KDIR 指向你的 4.1.15 内核树(作者开发时为正点原子出厂内核源码解包路径),改成你的
make                      # 生成 led.ko beep.ko ap3216c.ko dht11.ko hc_sr505.ko hc_sr04.ko sg90.ko step28byj48.ko
# 测试程序(交叉编译;或在板上直接 gcc)
arm-linux-gnueabihf-gcc ledApp.c -o ledApp     # 其余 *App.c 同法
```

**2. 设备树**：`03-设备树/imx6ull-alientek-emmc.dts` 基于出厂 dts 追加 9 个自定义节点（含双控制器 pinctrl），`make dtbs` 或 `dtc` 编译后按你的启动方式部署（作者经 TFTP 加载）。

**3. Qt 应用**

```bash
source /opt/fsl-imx-x11/4.1.15-2.1.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
cd 02-应用层QT && qmake && make -j4
# 板上: ./smarthome.sh (一键配置 tslib 环境并启动)
```

**4. 逐模块加载与测试**

| 模块 | /dev 节点 | 加载 | 最小测试 | 状态 |
|---|---|---|---|---|
| led | /dev/led | `insmod led.ko` | `./ledApp /dev/led 1` | 板端已测 |
| beep | /dev/beep | `insmod beep.ko` | `./beepApp /dev/beep 1` | 板端已测 |
| dht11 | /dev/dht11 | `insmod dht11.ko` | `cat /dev/dht11`（[湿][温]） | 板端已测 |
| hc_sr505 | /dev/hc_sr505 | `insmod hc_sr505.ko` | `cat`（阻塞读，1=有人） | 板端已测 |
| hc_sr04 | /dev/hc_sr04 | `insmod hc_sr04.ko` | `./hc_sr04App`（ioctl 距离 cm） | 板端已测 |
| sg90 | /dev/sg90 | `insmod sg90.ko` | `./sg90App 90`（角度 0~180） | 板端已测 |
| step28byj48 | /dev/step28byj48 | `insmod step28byj48.ko` | `./step28byj48App ±N`（正开负关窗帘） | 板端已测 |
| ap3216c | /dev/ap3216c | 见下 | ioctl 读 ALS/PS/IR | **可编译；板上受限**（见下） |

**出厂驱动冲突（ap3216c）**：出厂内核把 AP3216C 驱动**编死在内核里**（不可卸载、开机即占用 I2C 地址），自写的 `ap3216c.c`（i2c_driver + id_table，老内核 4.1.15 惯例）在仓库中可编译，但整机方案最终采用内核自带驱动、应用层按其 ioctl 接口适配（`04-接口协议/` §ap3216c 记录了判别方法：`dd` 读 0 字节即出厂驱动）。这是一次真实的"冲突→适配"工程决策，不是缺陷。

**5. 摄像头与考勤（板端已测）**：`05-摄像头工具/camcap`（V4L2 mmap + 格式枚举协商）与 `facetest`（Haar→LBPH 链路诊断）；考勤走 TCP（8 字节长度头 + JPEG + JSON）对接 PC 端 SeetaFace2 服务器，实测 0.2s/人。

## 关键技术

| 模块 | 技术 | 亮点 |
|------|------|------|
| DHT11 | 双边沿中断 + ktime_get_ns 时间戳解码 | 微秒级单总线时序，不用忙等 |
| HC-SR04 | 双边沿中断测脉宽 + div_u64 | cm = us / 58 |
| SG90 | PWM 子系统 50Hz | 0.5~2.5ms ↔ 0~180° |
| 28BYJ48 | 四相八拍 + usleep_range | 4096 拍/圈，mutex 保护 |
| AP3216C | i2c_driver，两条 msg 随机读 | id_table 匹配（老内核惯例） |
| 摄像头 | V4L2 mmap + Haar + LBPH | 格式枚举协商 + ROI 跟踪 |
| 考勤 | TCP + SeetaFace2 + SQLite | 独立收发线程（JPEG 编码 200~400ms/帧不进采集线程） |
| 报警 | 多传感器融合 | SR505+摄像头双确认，布防/撤防状态机 |

## 边界（诚实清单）

- 考勤链依赖 PC 端 SeetaFace2 服务器（单板算力不做识别服务端）；
- AP3216C 整机走内核自带驱动（见上）；
- 稳定性以整机演示 + 功能验收为准，未做 24h 连续压测；
- 演示视频为私有资料，未随仓库发布。

## License

自写的驱动、应用与工具代码以 [MIT](LICENSE) 提供；`03-设备树/` 的完整 dts 基于 NXP/正点原子 BSP 修改，遵循原 BSP 授权，不在 MIT 范围内。
