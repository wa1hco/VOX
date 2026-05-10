# TinyUSB.cmake — Vendor TinyUSB and build the minimum subset we use.
#
# We don't use TinyUSB's hw/bsp layer (which pulls in ST HAL drivers we
# don't want).  Instead we build the core sources, the device side,
# the CDC class, and the STM32 FSDEV peripheral driver into one static
# library, configured via our own tusb_config.h.  Our application code
# provides the USB peripheral clock/pin init and the IRQ vector
# handler.
#
# Source-file selection mirrors what TinyUSB's own CMakeLists picks for
# CFG_TUSB_MCU=OPT_MCU_STM32G4 + CDC class.
#
# Reachable via the `tinyusb_dcd_stm32g4` library target.  Anything
# linking it picks up the core, CDC, and stm32_fsdev port objects, plus
# include paths for src/ and src/portable.

include(FetchContent)
include_guard(GLOBAL)

FetchContent_Declare(
    tinyusb
    GIT_REPOSITORY https://github.com/hathach/tinyusb.git
    GIT_TAG        0.20.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)
FetchContent_GetProperties(tinyusb)
if(NOT tinyusb_POPULATED)
    message(STATUS "Fetching TinyUSB 0.20.0 ...")
    FetchContent_Populate(tinyusb)
endif()

# ---- Library target -------------------------------------------------
#
# Caller must set TUSB_CONFIG_INCLUDE_DIR to a directory containing
# its tusb_config.h before including this file.
#
set(_TUSB_SRC ${tinyusb_SOURCE_DIR}/src)

if(NOT TUSB_CONFIG_INCLUDE_DIR)
    message(FATAL_ERROR
        "TUSB_CONFIG_INCLUDE_DIR must be set before include(TinyUSB.cmake) "
        "— the directory holding your tusb_config.h.")
endif()

add_library(tinyusb_dcd_stm32g4 STATIC
    ${_TUSB_SRC}/tusb.c
    ${_TUSB_SRC}/common/tusb_fifo.c
    ${_TUSB_SRC}/device/usbd.c
    ${_TUSB_SRC}/device/usbd_control.c
    ${_TUSB_SRC}/class/cdc/cdc_device.c
    ${_TUSB_SRC}/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
)

target_include_directories(tinyusb_dcd_stm32g4 PUBLIC
    ${_TUSB_SRC}
    ${TUSB_CONFIG_INCLUDE_DIR}
)

# CFG_TUSB_MCU must match the family; OPT_MCU_STM32G4 picks the
# stm32_fsdev port.
target_compile_definitions(tinyusb_dcd_stm32g4 PUBLIC
    CFG_TUSB_MCU=OPT_MCU_STM32G4
)

# Link against CMSIS Device G4 — the stm32_fsdev driver uses
# CMSIS-defined USB / RCC / NVIC names + IRQn enums.
target_link_libraries(tinyusb_dcd_stm32g4 PUBLIC cmsis_stm32g4)

# Suppress noisy upstream warnings we can't fix without diverging.
target_compile_options(tinyusb_dcd_stm32g4 PRIVATE
    -Wno-strict-prototypes
    -Wno-unused-parameter
    -Wno-unused-but-set-variable
    -Wno-misleading-indentation
)
