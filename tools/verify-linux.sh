#!/bin/sh
# Native Linux host checks only. Never loads a module or opens real /dev nodes.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG=${1:-"$ROOT/build-linux-results"}
mkdir -p "$LOG" "$ROOT/build-linux"
uname -srm > "$LOG/environment.txt"
c++ --version >> "$LOG/environment.txt"
cmake --version >> "$LOG/environment.txt"
cmake -S "$ROOT" -B "$ROOT/build-linux" -DBUILD_GUI=OFF > "$LOG/configure.log" 2>&1
cmake --build "$ROOT/build-linux" -j2 > "$LOG/build.log" 2>&1
(cd "$ROOT/build-linux" && ctest -V) > "$LOG/tests.log" 2>&1
cc -std=c11 -Wall -Wextra -Werror "$ROOT/tools/pir_contract.c" -o "$ROOT/build-linux/pir_contract" > "$LOG/contract-build.log" 2>&1
# A successful compile is not a successful hardware contract run.
printf '%s\n' 'pir_contract compiled; NOT run against hardware' > "$LOG/contract-scope.txt"
mkdir -p "$ROOT/build-driver"
cp "$ROOT/01-驱动层/hc_sr505.c" "$ROOT/01-驱动层/sg90.c" "$ROOT/01-驱动层/step28byj48.c" "$ROOT/build-driver/"
printf '%s\n' 'obj-m := hc_sr505.o sg90.o step28byj48.o' > "$ROOT/build-driver/Makefile"
if [ -d "/lib/modules/$(uname -r)/build" ]; then
    make -C "/lib/modules/$(uname -r)/build" M="$ROOT/build-driver" modules > "$LOG/driver-build.log" 2>&1
    printf '%s\n' 'native host kernel module compiled; NOT loaded; target BSP NOT verified' > "$LOG/driver-scope.txt"
fi
cmake -S "$ROOT" -B "$ROOT/build-sanitized" -DBUILD_GUI=OFF \
    '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
    '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' > "$LOG/sanitized-configure.log" 2>&1
cmake --build "$ROOT/build-sanitized" -j2 > "$LOG/sanitized-build.log" 2>&1
(cd "$ROOT/build-sanitized" && ASAN_OPTIONS=detect_leaks=1 ctest -V) > "$LOG/sanitized-tests.log" 2>&1
printf '%s\n' '{"native_linux":true,"core_tests":true,"asan_ubsan":true,"hardware_tested":false}' > "$LOG/result.json"
