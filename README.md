# i.MX6ULL 环境监测与本地报警终端

面向固定室内的小型实验室场景：周期采集温湿度和光照、监听红外活动输入，在本地显示有效数据、故障和报警状态。当前主程序保留 **DHT11、AP3216C、HC-SR505、LED、蜂鸣器和 Qt 界面**。

本轮从多功能整机演示收敛范围，重点解决一个传感器失败拖慢其他采集、旧数据被当作当前状态，以及报警和线程退出缺少明确契约的问题。视觉、远端考勤与电机联动不参与当前主程序。

**验证状态：Windows Qt 6.11.1 原生界面和模拟业务流程已运行；C++11 核心及 Linux 后端通过 x86_64 主机测试与 ASan/UBSan。新版本尚未部署到 i.MX6ULL，Qt 5.12.9/ARM 构建及物理指标待验证。** [结果与边界](docs/improvement/results.md)

[当前架构与数据链路](docs/overview/architecture.md) · [场景与设计取舍](docs/overview/project-brief.md) · [文档入口](docs/README.md)

## 使用条件与功能边界

- 本地监测与报警不依赖摄像头、OpenCV、服务器或网络连接。
- PIR 是活动模块的高低电平，不能确认身份或精确判断人员持续在场。
- DHT11 和光照值未做计量标定；光照为 BSP 原始读数的近似换算，不用于精密测量或安全控制。
- 故障、过期、上次成功值和当前有效值分别显示；输出请求与写接口成功分开显示，物理动作仍需实测。
- 历史整机使用 ATK-i.MX6ULL / Linux 4.1.15 / Qt 5.12.9。新代码的 Windows/主机验证不能替代该环境的验收。

## 关键实现

| 问题 | 实现与取舍 | 已有验证 |
|---|---|---|
| 一个传感器重试延迟其他采集 | 温湿度、光照和 PIR 独立工作线程；每周期一次测量，取消串行三次睡眠重试 | 故意阻塞 DHT11 的替身测试中，光照仍持续更新；真板耗时未测 |
| 读取失败后仍显示旧值 | 每输入保存最近尝试、最近成功、失败次数和错误；失败立即无效，超期标 stale | 核心测试覆盖失败、恢复、时间过期、无效数值 |
| 报警和故障混用 | 独立业务核心处理布撤防、活动、超限回差、静音和故障 | 状态序列测试与 Qt 自动操作；没有现场误漏报数据 |
| 阻塞监听难以退出 | 非阻塞 PIR read 配合 poll；工作线程停止后 join，再关闭后端 | 主机可取消替身退出测试；真实内核不返回时不能保证期限 |
| 应用 poll 与驱动不匹配 | HC-SR505 补 .poll、首次电平快照和单读取者约束；应用遇旧契约 EPROTO 后退避 | Linux 5.4 x86_64 内核模块编译；未加载、未在目标 BSP 验证 |

温湿度默认每 2 秒尝试一次，光照每 1 秒一次；有效期默认 5 秒。温度 >=30℃ 或湿度 >=75% 置超限，温度 <=28℃ 且湿度 <=70% 才解除。参数是当前策略，不是硬件性能保证。

## 快速开始：Windows 模拟运行

本机已验证 Qt 为 `D:\qt_install\6.11.1\mingw_64`，编译器为 `D:\qt_install\Tools\mingw1310_64`。CMake/Ninja 不在 PATH 时传入完整路径：

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
.\tools\build-windows.ps1 -CMake $cmake -NinjaDir $ninja
.\tools\run-windows.ps1
# 自动化离屏模拟操作；不连接硬件
.\tools\run-windows.ps1 -SmokeTest
```

也可在 Qt Creator 中打开 `app/smarthome.pro`，选择 MinGW 64 位 kit，运行参数设置为 `--simulate`。Windows 未指定模拟时明确报错，不隐式生成传感器数据。

界面可模拟活动高/低、DHT11 故障、温度超限与输出失败。模拟按钮只在模拟模式出现；画面明确标注其验证范围。Windows 离屏插件需可读字体，本机使用系统微软雅黑；其他环境可通过 `MONITOR_FONT` 指定字体文件。

## Linux 构建

仅构建核心与测试，无需 Qt：

```sh
cmake -S . -B build-linux -DBUILD_GUI=OFF
cmake --build build-linux -j2
(cd build-linux && ctest -V)
```

有 Qt 5/6 Widgets 开发环境时使用 CMake 默认配置，或 `qmake app/smarthome.pro`。核心使用 C++11；Qt 6 界面使用 C++17。当前主程序没有 OpenCV 链接依赖。

Linux 真设备模式通过 `dht11`、`ap3216c`、`hc_sr505`、`led`、`beep` 字符设备访问硬件，默认路径 `/dev`。必须先核对驱动 ABI、节点占用和引脚。`tools/run-local-monitor.sh` 仅执行预构建程序，不自动加载模块、改网络、杀其他界面或改启动配置。

## 目录与历史

| 路径 | 用途 |
|---|---|
| `core/` | 与 Qt 无关的状态规则、独立采集、模拟及 Linux 设备后端 |
| `app/` | 当前 Qt Widgets 主程序 |
| `tests/` | 核心与慢输入隔离的主机测试 |
| `tools/` | 构建/启动脚本、Linux 验证脚本、待板测的 PIR 契约检查器 |
| `01-驱动层/`、`03-设备树/` | 驱动和历史 BSP 设备树；本轮修改 HC-SR505 契约 |
| `02-应用层QT/`、`05-摄像头工具/` | 历史应用与独立视觉实验，不参与当前构建 |
| `docs/improvement/`、`evidence/` | 场景、过程、测试、失败轮次与适用范围 |

[工作记录](docs/improvement/worklog.md) · [环境恢复清单](docs/improvement/board-restoration.md) · [收敛前首页](https://github.com/Ericsunny/smarthome-imx6ull/blob/e015dddc4c8d661db2c4fadc179762b7a431539b/README.md)

原项目的驱动、摄像头、考勤和执行器实验保留为历史材料；不同版本的能力与结果不合并成当前系统。历史文档中的 CPU、采样失败率、识别耗时等数字，本轮未作为新结果引用。

## License

自写驱动、应用与工具遵循 [MIT](LICENSE)；基于 NXP/正点原子 BSP 的完整设备树遵循原 BSP 授权。
