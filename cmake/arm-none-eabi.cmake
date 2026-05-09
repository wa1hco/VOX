# Cross-compile toolchain for STM32G474 (Cortex-M4F) bare-metal builds.
#
# Use with:
#   cmake -S . -B build-<board> \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#         -DBOARD=<stm32g474_nucleo|stm32g474_vox_cb>
#
# Requirements:
#   - arm-none-eabi-gcc on $PATH (apt: gcc-arm-none-eabi binutils-arm-none-eabi)
#
# Driver/runtime status:
#   At present the MCU build produces a pin-table static library only —
#   no startup, linker script, or HAL/LL drivers are wired up yet.  When
#   those land (hand-rolled LL, no CubeMX), append their flags here and
#   in each board.cmake.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Pick the cross compiler.  Allow override via -DARM_TOOLCHAIN_PREFIX=... .
if(NOT DEFINED ARM_TOOLCHAIN_PREFIX)
    set(ARM_TOOLCHAIN_PREFIX arm-none-eabi-)
endif()

set(CMAKE_C_COMPILER   ${ARM_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${ARM_TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${ARM_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR           ${ARM_TOOLCHAIN_PREFIX}ar)
set(CMAKE_OBJCOPY      ${ARM_TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP      ${ARM_TOOLCHAIN_PREFIX}objdump)
set(CMAKE_SIZE         ${ARM_TOOLCHAIN_PREFIX}size)

# CMake otherwise tries to link a test executable to validate the compiler,
# which fails on a bare-metal target without a startup file.  Skip it.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Cortex-M4 with hardware FPU (single precision), Thumb-2 only.
#
# Optimization: default to -O2 unless the user passes
# CMAKE_BUILD_TYPE=Debug.  -O0 is *not* viable for this codebase on this
# part — speexdsp's filterbank_new uses double-precision atan/log via
# the soft-float helpers (Cortex-M4F has only single-precision FPU).
# At 16 MHz HSI with -O0 those calls take many seconds and the chip
# appears hung.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(VOX_MCU_CFLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
)

# Apply to all C/C++/ASM compiles in this build tree.
add_compile_options(${VOX_MCU_CFLAGS})

# Don't search the host system for libraries/headers when cross-compiling.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
