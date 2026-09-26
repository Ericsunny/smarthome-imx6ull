#!/bin/sh
# ============================================================
#  智能安防监控系统 · 一键启动脚本（M5 演示用）
#  用法： sh /smarthome.sh          启动界面（自动加载驱动、清屏）
#         sh /smarthome.sh stop     关闭界面
# ============================================================

BIN=/smarthome

# ---------- 1. 触摸/显示环境（出厂方案：linuxfb + tslib 单栈）----------
#  ★ 不要再加 QT_QPA_GENERIC_PLUGINS=evdevtouch 那一套！
#    本内核上 evdevtouch 插件线程会空转（实测烧掉 95%+ CPU，20 秒 7000 jiffies），
#    换成本脚本的 tslib 单栈后触摸完全正常，CPU 降到 ~1%（20 秒 24 jiffies）。
export QT_QPA_PLATFORM=linuxfb
export QT_QPA_PLATFORM_PLUGIN_PATH=/opt/qt512/plugins/platforms
export LD_LIBRARY_PATH=/opt/qt512/lib
export QT_QPA_FONTDIR=/opt/qt512/fonts
export QT_QPA_FB_TSLIB=1
export QT_QPA_GENERIC_PLUGINS=tslib
export TSLIB_TSDEVICE=/dev/input/touchscreen0
export TSLIB_CONFFILE=/etc/ts.conf
export TSLIB_CALIBFILE=/etc/pointercal
export TSLIB_PLUGINDIR=/usr/lib/ts
export TSLIB_CONSOLEDEVICE=none

XDG_RUNTIME_DIR=/tmp/xdg
export XDG_RUNTIME_DIR
[ -d "$XDG_RUNTIME_DIR" ] || mkdir -p "$XDG_RUNTIME_DIR"

# ---------- 2. stop 分支 ----------
if [ "$1" = "stop" ]; then
    killall smarthome 2>&1 | grep -v "no process"
    exit 0
fi

# ---------- 3. 加载驱动模块（缺哪个会跳过，已加载的报错被过滤）----------
for m in led beep ap3216c dht11 hc_sr505 hc_sr04 sg90 step28byj48 tb6612; do
    [ -f "/$m.ko" ] || continue
    insmod "/$m.ko" 2>&1 | grep -v "File exists"
done
echo "--- 设备节点检查（八个都应在）---"
ls /dev/dht11 /dev/hc_sr505 /dev/hc_sr04 /dev/ap3216c /dev/led /dev/beep /dev/sg90 /dev/step28byj48 2>&1

# ---------- 4. 清场 ----------
# 出厂 splash，干掉它（偶发重绘会糊屏幕）
killall psplash 2>&1 | grep -v "no process"

# 出厂 UI（/opt/ui/systemui，Qt Quick 写的）和我们抢同一个 /dev/fb0：
# 它一收到触摸事件就重绘，把出厂图标糊到我们界面上。
# ★ 不要 kill 它！实测 kill 会在进程退出路径触发内核 bad page map 崩溃
#   （Fixing recursive fault but reboot is needed）。
#   正确做法：让它开机不启动，在板子上执行一次，永久生效——
#     cp /etc/rc.local /etc/rc.local.factory-ui.bak
#     sed -i 's|^/opt/ui/systemui|#/opt/ui/systemui|' /etc/rc.local
if ps | grep -q "[s]ystemui"; then
    echo ">>> 警告：出厂 UI systemui 还在运行，触摸时会出现图标窜块！"
    echo ">>> 请按脚本里注释的两条命令改 /etc/rc.local，重启后再演示。"
fi

echo 0 > /sys/class/vtconsole/vtcon1/bind    # 解绑 fbcon，防止控制台光标抢屏
echo 3 > /proc/sys/kernel/printk             # 关掉内核打印刷串口

# ---------- 5. 启动界面 ----------
$BIN &
echo "smarthome 已启动，PID=$!"
