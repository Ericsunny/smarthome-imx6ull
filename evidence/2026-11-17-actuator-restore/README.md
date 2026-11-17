# 执行器恢复主机证据

日期：2026-11-17；基准 `35eb1a0`。本目录不含开发板测试。当前代码/产物 SHA256 见 `sources.json` 与 `artifacts.json`，Linux 源包传输及临时目录见每轮 `transfer.json`；Windows 环境见 `environment.log`，Linux 环境见各轮 `environment.txt`。

| 目录 | 内容与范围 |
|---|---|
| 00-baseline | 基准提交、修改前状态及关键源文件哈希 |
| 01-windows-r1 | 首轮 CMake/CTest 与 800×480 Qt 模拟流程、人工核对截图 |
| 02-linux-r1 | 核心通过，步进模块头文件错误导致验证中止；未执行 sanitizer |
| 03-linux-r2 | 修正头文件后核心 11 场景、ASan/UBSan、三个模块主机编译；未加载 |
| 04-final-windows | 最终源码 CTest 6/6 |
| 05-qmake-windows | Qt 6 qmake 新工程构建及模拟流程 |
| 06-review | 文档链接、Mermaid 语法、格式及敏感模式检查 |

## Windows 命令

在仓库根目录，工具变量取值见根目录 README：

```powershell
.\tools\build-windows.ps1 -CMake $cmake -NinjaDir $ninja
$env:MONITOR_TEST_CAPTURE = "$PWD/evidence/2026-11-17-actuator-restore/01-windows-r1/workflow.png"
.\tools\run-windows.ps1 -SmokeTest
Remove-Item Env:MONITOR_TEST_CAPTURE
# qmake 在独立 build-actuator-qmake 目录，Qt/MinGW bin 已加入 PATH
qmake ../app/smarthome.pro
mingw32-make -j2
$env:QT_QPA_PLATFORM = 'offscreen'
.\release\smarthome.exe --simulate --smoke-test
```

构建/测试输出按对应轮次保存。模拟工作流故意设置 DHT、步进和输出错误，失败计数不能解释为硬件可靠性。关闭故障检查分别通过 `MONITOR_TEST_SHUTDOWN_FAILURE` 和 `MONITOR_TEST_SERVO_SHUTDOWN_FAILURE` 注入；测试要求退出码 1 及对应错误事件。

## Linux 命令与传输

只使用编译 VM 别名 `rk3568`，没有连接 `rkboard` 或 i.MX6ULL。

```powershell
tar -czf build-transfer/actuator-source-r2.tgz CMakeLists.txt core app tests tools '01-驱动层/hc_sr505.c' '01-驱动层/sg90.c' '01-驱动层/step28byj48.c'
$remotePath = (ssh rk3568 'mktemp -d /tmp/imx6ull-actuators-20261117.XXXXXX').Trim()
Get-FileHash build-transfer/actuator-source-r2.tgz -Algorithm SHA256
scp build-transfer/actuator-source-r2.tgz "rk3568:${remotePath}/source.tgz"
ssh rk3568 "sha256sum '$remotePath/source.tgz'"
# 核对两端哈希相等，再执行：
ssh rk3568 "tar -xzf '$remotePath/source.tgz' -C '$remotePath' && sh '$remotePath/tools/verify-linux.sh' '$remotePath/logs'"
scp -r "rk3568:${remotePath}/logs/." evidence/2026-11-17-actuator-restore/03-linux-r2/
```

`tools/verify-linux.sh` 记录环境、CMake 原生构建/测试、PIR 检查工具编译、三个模块编译及 ASan/UBSan。没有执行 insmod 或真实设备契约测试。R1、R2 分别使用新临时目录，不覆盖失败轮次。

`gated_stepper_host_substitute` 在确认步进已进入闸门后才开始计额外光照更新，并检查舵机完成且步进尚未返回；等待失败也释放闸门、停止并回收线程。20ms 是测试光照周期；不是生产配置或物理端到端延迟。
