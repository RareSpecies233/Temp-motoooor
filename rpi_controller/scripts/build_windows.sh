#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-windows"
TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/toolchains/windows-mingw64.cmake"

if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  if ! grep -Fq "CMAKE_HOME_DIRECTORY:INTERNAL=$PROJECT_ROOT" "$BUILD_DIR/CMakeCache.txt"; then
    echo "[INFO] Stale CMake cache detected in $BUILD_DIR, cleaning it."
    rm -rf "$BUILD_DIR"
  fi
fi

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  echo "[ERROR] Missing cross compiler: x86_64-w64-mingw32-g++"
  echo "[HINT] Install mingw-w64 cross toolchain first, then run this script again."
  exit 1
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" --config Release -j

echo "[OK] Windows build artifact: $BUILD_DIR/rpi5_stm32_controller.exe"
