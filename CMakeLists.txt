cmake_minimum_required(VERSION 3.16)
project(VOX C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(PLATFORM_LINUX "Build for Linux simulation" ON)
option(BUILD_QT_GUI "Build Qt GUI Linux simulator" ON)
option(PLATFORM_STM32F411CEUX6 "Build STM32F411CEUx6 MCU scaffold" OFF)

# Find SpeexDSP
find_package(PkgConfig REQUIRED)
pkg_check_modules(SPEEXDSP REQUIRED speexdsp)

# Core library (platform-independent)
add_library(vox_core
    src/vox.c
    src/aec.c
    src/vad.c
)
target_include_directories(vox_core PUBLIC src)
target_include_directories(vox_core PRIVATE ${SPEEXDSP_INCLUDE_DIRS})
target_link_libraries(vox_core PRIVATE ${SPEEXDSP_LIBRARIES} m)

# Linux simulation executable
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

# STM32F411CEUx6 MCU scaffold (board pin map + MCU integration entry points)
if(PLATFORM_STM32F411CEUX6)
    add_library(vox_stm32f411ceux6_board
        platform/mcu/stm32f411ceux6_board.c
    )
    target_include_directories(vox_stm32f411ceux6_board PUBLIC platform/mcu)
    target_link_libraries(vox_stm32f411ceux6_board PUBLIC vox_core)
endif()

# Tests
enable_testing()
add_subdirectory(tests)
