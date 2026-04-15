#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-rpi5"
TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/toolchains/rpi5-aarch64.cmake"

# possible cross C++ compiler names
CXX_CANDIDATES=("aarch64-linux-gnu-g++" "aarch64-unknown-linux-gnu-g++" "aarch64-none-linux-gnu-g++")

CROSS_CXX=""
for c in "${CXX_CANDIDATES[@]}"; do
  if command -v "$c" >/dev/null 2>&1; then
    CROSS_CXX="$c"
    break
  fi
done

if [ -z "${CROSS_CXX}" ]; then
  echo "[ERROR] Missing cross compiler: aarch64-linux-gnu-g++ (or compatible)"
  echo "[HINT] Install an aarch64 cross toolchain (e.g. apt install gcc-aarch64-linux-gnu) or add it to PATH."
  exit 1
fi

CROSS_CC="${CROSS_CXX/g++/gcc}"

# Optional: allow providing a sysroot via environment variable AARCH64_SYSROOT
CMAKE_SYSROOT_OPTION=""
if [ -n "${AARCH64_SYSROOT:-}" ]; then
  echo "[INFO] Using AARCH64_SYSROOT=${AARCH64_SYSROOT}"
  CMAKE_SYSROOT_OPTION="-DCMAKE_SYSROOT=${AARCH64_SYSROOT}"
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CROSS_CC" \
  -DCMAKE_CXX_COMPILER="$CROSS_CXX" \
  $CMAKE_SYSROOT_OPTION

cmake --build "$BUILD_DIR" --config Release -j

echo "[OK] Raspberry Pi 5 build artifact: $BUILD_DIR/rpi5_stm32_controller"
