# 执行命令与来源

所有命令由自动化助手执行；没有 i.MX6ULL 连接、模块加载或镜像部署。路径对应本次环境，不是工具安装位置的通用保证。

## 来源

```powershell
git -C D:\vm\embedded-learning-github status --short
git -C D:\vm\embedded-learning-github rev-parse HEAD
git -C D:\vm\embedded-learning-github worktree add -b codex/imx6ull-local-monitor D:\vm\github\smarthome-imx6ull e015dddc4c8d661db2c4fadc179762b7a431539b
```

## Windows

探针：`qmake -query`；对 `01-windows-qt/probe.pro` 执行 qmake 与 mingw32-make，随后以 `QT_QPA_PLATFORM=offscreen` 执行 `qt_probe.exe`。工具版本与结果见该目录。

完整应用：

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
.\tools\build-windows.ps1 -CMake $cmake -NinjaDir $ninja
.\tools\run-windows.ps1 -SmokeTest
```

R1 用 CMake/Ninja 构建和 `ctest -V --output-junit`。R2/R3 增加 `MONITOR_TEST_CAPTURE` 保存离屏截图；R2 字体缺失，R3 注册系统字体后修正。

另在独立 `build-qmake` 目录：

```powershell
qmake -o build-qmake\Makefile app\smarthome.pro
mingw32-make -C build-qmake -j2
# 设置 QT_QPA_PLATFORM=offscreen 后
.\build-qmake\release\smarthome.exe --simulate --smoke-test
```

## Linux 主机

使用现有 `ssh rk3568` 编译 VM，传输至独立临时目录。源包 tar 仅含 CMakeLists、core、tests、tools 与 HC-SR505 驱动。源码压缩包和后续驱动传输均核对两端 SHA-256。

```sh
sh tools/verify-linux.sh /tmp/imx6ull-monitor-20261117.RHn7Lw/results-r1
# 保留失败轮次；移除未使用 ARM 头后使用新结果目录
sh tools/verify-linux.sh /tmp/imx6ull-monitor-20261117.RHn7Lw/results-r2
```

脚本保存原生 C++11 构建、ctest、PIR 契约工具编译、主机 5.4 内核模块编译、ASan/UBSan 结果。它不运行契约工具，不加载模块，也不打开真实设备节点。节点拒绝测试只使用自己创建的临时普通文件。
