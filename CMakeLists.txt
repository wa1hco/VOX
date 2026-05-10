cmake_minimum_required(VERSION 3.16)
project(VOX C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Build options
#
# Two build modes:
#   1) Host build (default) — compiles vox_linux + vox_qt for the dev machine.
#      No toolchain file needed.  PLATFORM_LINUX=ON, PLATFORM_MCU=OFF.
#   2) Cross build for an MCU board — pass cmake/arm-none-eabi.cmake as the
#      toolchain file and BOARD=<dir under platform/mcu/boards/>.
#      The host targets are skipped automatically when cross-compiling.
#
# Examples:
#   # Linux desktop simulation
#   cmake -S . -B build && cmake --build build
#
#   # Cross-compile pin scaffold for the Nucleo G474RE shield
#   cmake -S . -B build-nucleo \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#         -DBOARD=stm32g474_nucleo
#   cmake --build build-nucleo
#
#   # Cross-compile pin scaffold for the VOX custom board
#   cmake -S . -B build-vox-cb \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#         -DBOARD=stm32g474_vox_cb
#   cmake --build build-vox-cb
# ---------------------------------------------------------------------------

option(PLATFORM_LINUX "Build Linux simulation (vox_linux, vox_qt)" ON)
option(BUILD_QT_GUI   "Build Qt GUI Linux simulator"               ON)
set(BOARD "" CACHE STRING
    "MCU board name under platform/mcu/boards/ (e.g. stm32g474_nucleo, stm32g474_vox_cb).  Empty = no MCU build.")

# When cross-compiling for an MCU, skip the host targets and require BOARD.
if(CMAKE_CROSSCOMPILING)
    set(PLATFORM_LINUX OFF CACHE BOOL "" FORCE)
    set(BUILD_QT_GUI   OFF CACHE BOOL "" FORCE)
    if(BOARD STREQUAL "")
        message(FATAL_ERROR
            "Cross-compiling but BOARD is empty.  Pass -DBOARD=<name> "
            "(e.g. stm32g474_nucleo, stm32g474_vox_cb).")
    endif()
endif()

# ---------------------------------------------------------------------------
# vox_core — platform-independent VOX algorithm.
#
# Same .c files build on both targets.  The only difference is *which*
# speexdsp implementation they link against:
#   - Host build:  system libspeexdsp-dev via pkg-config (1.2.1 on Ubuntu 24.04).
#   - Cross build: SpeexDSP-1.2.1 fetched and built in-tree by
#                  cmake/SpeexDSP.cmake.
# Both pin to 1.2.1, so MCU and Linux behave identically and tuning
# carries across.
# ---------------------------------------------------------------------------
add_library(vox_core
    src/vox.c
    src/aec.c
    src/vad.c
)
target_include_directories(vox_core PUBLIC src)
target_link_libraries(vox_core PRIVATE m)

if(NOT CMAKE_CROSSCOMPILING)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SPEEXDSP REQUIRED speexdsp)
    target_include_directories(vox_core PRIVATE ${SPEEXDSP_INCLUDE_DIRS})
    target_link_libraries(vox_core PRIVATE ${SPEEXDSP_LIBRARIES})
else()
    include(${CMAKE_SOURCE_DIR}/cmake/SpeexDSP.cmake)
    target_link_libraries(vox_core PRIVATE speexdsp_static)
endif()

# ---------------------------------------------------------------------------
# CMSIS + TinyUSB — only needed by the cross build.  The host doesn't
# compile any of the chip code, so dragging them in there would be
# wasted FetchContent.
# ---------------------------------------------------------------------------
if(CMAKE_CROSSCOMPILING)
    include(${CMAKE_SOURCE_DIR}/cmake/CmsisStm32g4.cmake)
    set(TUSB_CONFIG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/platform/mcu/core/usb)
    include(${CMAKE_SOURCE_DIR}/cmake/TinyUSB.cmake)
endif()

# ---------------------------------------------------------------------------
# vox_dongle_proto — wire protocol shared by GUI and firmware.
#
# Always built — tiny, no external deps, compiles for both host and
# cross targets.  The header defines packed wire structs and message
# constants; the .c file implements CRC + framer + parser.
# ---------------------------------------------------------------------------
add_library(vox_dongle_proto STATIC
    tools/vox_dongle_proto.c
)
target_include_directories(vox_dongle_proto PUBLIC tools)

# ---------------------------------------------------------------------------
# Linux simulation executables (host build only)
# ---------------------------------------------------------------------------
if(PLATFORM_LINUX)
    find_package(ALSA REQUIRED)
    pkg_check_modules(PULSE REQUIRED libpulse-simple libpulse)

    add_library(vox_linux_audio
        platform/linux/audio_io.c
    )
    target_include_directories(vox_linux_audio PRIVATE platform/linux ${PULSE_INCLUDE_DIRS})
    target_link_libraries(vox_linux_audio PRIVATE ALSA::ALSA ${PULSE_LIBRARIES})

    add_executable(vox_linux
        platform/linux/main.c
    )
    target_include_directories(vox_linux PRIVATE src platform/linux ${PULSE_INCLUDE_DIRS})
    target_link_libraries(vox_linux PRIVATE vox_core vox_linux_audio ALSA::ALSA ${PULSE_LIBRARIES} ${SPEEXDSP_LIBRARIES})

    if(BUILD_QT_GUI)
        find_package(Qt6 QUIET COMPONENTS Widgets)
        if(Qt6_FOUND)
            set(QT_WIDGETS_LIB Qt6::Widgets)
        else()
            find_package(Qt5 QUIET COMPONENTS Widgets)
            if(Qt5_FOUND)
                set(QT_WIDGETS_LIB Qt5::Widgets)
            endif()
        endif()

        if(QT_WIDGETS_LIB)
            add_executable(vox_qt
                platform/linux/main_qt.cpp
            )
            set_target_properties(vox_qt PROPERTIES AUTOMOC ON)
            target_include_directories(vox_qt PRIVATE src platform/linux)
            target_link_libraries(vox_qt PRIVATE vox_core vox_linux_audio ${QT_WIDGETS_LIB})
        else()
            message(STATUS "Qt Widgets not found: skipping vox_qt target")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# MCU board target (selected by -DBOARD=<name>)
#
# Each board owns a board.cmake fragment that sets:
#   VOX_BOARD_NAME, VOX_BOARD_MCU, VOX_BOARD_PACKAGE
#   VOX_BOARD_SOURCES         (list of .c files)
#   VOX_BOARD_INCLUDE_DIRS    (list of include dirs, including
#                              boards/common/ for the shared pin assignment)
# This level wires the board sources together with the shared MCU core
# (decimator, pin encoding, board interface) into a single library.
# ---------------------------------------------------------------------------
if(NOT BOARD STREQUAL "")
    set(_VOX_BOARD_DIR ${CMAKE_SOURCE_DIR}/platform/mcu/boards/${BOARD})
    if(NOT EXISTS ${_VOX_BOARD_DIR}/board.cmake)
        message(FATAL_ERROR
            "BOARD=${BOARD} not found.  Looking for ${_VOX_BOARD_DIR}/board.cmake.\n"
            "Available boards:\n"
            "  - stm32g474_nucleo  (NUCLEO-G474RE dev board with HCO-mirrored shield)\n"
            "  - stm32g474_vox_cb  (VOX HCO custom board, STM32G474CBT3 LQFP48)")
    endif()

    include(${_VOX_BOARD_DIR}/board.cmake)
    message(STATUS "VOX board: ${VOX_BOARD_NAME} (${VOX_BOARD_MCU}, ${VOX_BOARD_PACKAGE})")

    # The board descriptor library compiles in both host-gcc-sanity-check
    # mode and cross-compile mode.  It carries no instructions, just data.
    add_library(vox_mcu_board
        platform/mcu/core/vox_mcu_decimator.c
        ${VOX_BOARD_SOURCES}
    )
    target_include_directories(vox_mcu_board PUBLIC
        platform/mcu/core
        ${VOX_BOARD_INCLUDE_DIRS}
    )
    # NOTE: vox_mcu_board deliberately does NOT depend on vox_core.  The
    # board descriptor + CIC decimator are pure C with only <stdint.h>
    # dependencies; they have nothing to do with speexdsp-based AEC/VAD.
    # When the algorithm code is ready to run on the MCU (post-speexdsp
    # port or replacement), the firmware exe below will link it in.

    # The firmware executable, startup file, and linker script only make
    # sense when actually cross-compiling.  Skip in host-sanity mode.
    if(CMAKE_CROSSCOMPILING)
        set(_VOX_FW_NAME vox_${VOX_BOARD_NAME})

        add_executable(${_VOX_FW_NAME}
            platform/mcu/core/startup_stm32g474.c
            platform/mcu/core/clock_init.c
            platform/mcu/core/systick.c
            platform/mcu/core/synth_audio.c
            platform/mcu/core/usb/usb_init.c
            platform/mcu/core/usb/usb_descriptors.c
            platform/mcu/core/usb/usb_irq_glue.c
            ${VOX_BOARD_MAIN_SOURCES}
        )
        target_include_directories(${_VOX_FW_NAME} PRIVATE
            platform/mcu/core
            ${VOX_BOARD_INCLUDE_DIRS}
        )
        # vox_core pulls in speexdsp_static (its PRIVATE link).  vox_mcu_board
        # provides the per-board pin descriptor that main.c reads.
        # speexdsp_static is also explicit so main.c's bring-up diagnostic
        # prints can reach <speex/...> headers directly; remove this once
        # the diagnostics aren't needed.
        # cmsis_stm32g4 (INTERFACE) supplies <stm32g474xx.h> + <core_cm4.h>.
        target_link_libraries(${_VOX_FW_NAME} PRIVATE
            vox_mcu_board
            vox_core
            speexdsp_static
            cmsis_stm32g4
            tinyusb_dcd_stm32g4
        )

        # Use the board's linker script and produce a .map file alongside
        # the .elf for size/footprint inspection.  --specs=nosys.specs
        # gives us libc stubs that don't drag in the host filesystem.
        target_link_options(${_VOX_FW_NAME} PRIVATE
            -T${VOX_BOARD_LINKER_SCRIPT}
            -Wl,-Map=${_VOX_FW_NAME}.map,--cref
            -Wl,--gc-sections
            --specs=nosys.specs
            --specs=nano.specs
            -nostartfiles
            -mcpu=cortex-m4
            -mthumb
            -mfpu=fpv4-sp-d16
            -mfloat-abi=hard
        )
        # Re-link if the linker script changes.
        set_target_properties(${_VOX_FW_NAME} PROPERTIES
            LINK_DEPENDS ${VOX_BOARD_LINKER_SCRIPT}
            SUFFIX .elf
        )

        # Post-link: produce a flat .bin (what st-flash wants) and print size.
        add_custom_command(TARGET ${_VOX_FW_NAME} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary
                    $<TARGET_FILE:${_VOX_FW_NAME}>
                    $<TARGET_FILE_DIR:${_VOX_FW_NAME}>/${_VOX_FW_NAME}.bin
            COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${_VOX_FW_NAME}>
            COMMENT "Building ${_VOX_FW_NAME}.bin and reporting size"
            VERBATIM
        )

        # `cmake --build <dir> --target flash` writes the .bin to flash.
        # The board.cmake provides the command template; @BIN@ gets the
        # actual path to the built .bin file.
        string(REPLACE "@BIN@"
            "$<TARGET_FILE_DIR:${_VOX_FW_NAME}>/${_VOX_FW_NAME}.bin"
            _VOX_FLASH_CMD ${VOX_BOARD_FLASH_COMMAND})
        separate_arguments(_VOX_FLASH_CMD_LIST UNIX_COMMAND ${_VOX_FLASH_CMD})

        add_custom_target(flash
            COMMAND ${_VOX_FLASH_CMD_LIST}
            DEPENDS ${_VOX_FW_NAME}
            COMMENT "Flashing ${_VOX_FW_NAME}.bin to ${VOX_BOARD_MCU}"
            VERBATIM
            USES_TERMINAL
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests (host build only — they link Linux audio + speexdsp)
# ---------------------------------------------------------------------------
if(PLATFORM_LINUX)
    enable_testing()
    add_subdirectory(tests)
endif()
