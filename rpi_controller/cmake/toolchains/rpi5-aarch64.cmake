#
# Raspberry Pi 5 (aarch64) CMake toolchain file
# Usage:
#  - Ensure aarch64 cross compilers are installed (e.g. aarch64-linux-gnu-gcc/aarch64-linux-gnu-g++)
#  - Optionally set environment variable AARCH64_SYSROOT to point to a sysroot and it will be passed as CMAKE_SYSROOT
#
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc CACHE STRING "C compiler")
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++ CACHE STRING "C++ compiler")
endif()

if(DEFINED ENV{AARCH64_SYSROOT})
  set(CMAKE_SYSROOT $ENV{AARCH64_SYSROOT})
endif()

if(NOT DEFINED CMAKE_SYSROOT)
  set(CMAKE_SYSROOT "" CACHE STRING "Optional sysroot for cross-compilation")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Allow overriding compilers from the environment or command line.
