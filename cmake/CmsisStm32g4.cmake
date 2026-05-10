# CmsisStm32g4.cmake — Vendor CMSIS Core + CMSIS Device for STM32G4.
#
# Two pieces, two FetchContents:
#
#   - ARM CMSIS Core: cortex-m4 standard headers (core_cm4.h, NVIC defines,
#     basic intrinsics).  Pulled from the official CMSIS_5 repo, shallow
#     clone with no submodules so we don't drag in the ~500 MB worth of
#     DSP/RTOS/compiler test data we don't use.
#
#   - ST CMSIS Device G4: chip-specific peripheral memory map, register
#     layouts, IRQn enum (stm32g474xx.h, system_stm32g4xx.h).  Pulled
#     from ST's standalone repo.
#
# Together they replace our handwritten stm32g4_min.h's peripheral
# struct definitions.  Our own helper macros (GPIO_FIELD2_SET, etc.)
# stay in stm32g4_min.h, which becomes a thin shim over <stm32g474xx.h>.
#
# Result: the rest of the firmware can include <stm32g474xx.h> and use
# the same RCC/GPIO/USART/USB/etc. definitions every other STM32G4
# project on the planet uses, with the same field names.

include(FetchContent)
include_guard(GLOBAL)

# ---- ARM CMSIS Core (cortex-m4 headers) -----------------------------
FetchContent_Declare(
    cmsis_core
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS_5.git
    GIT_TAG        5.9.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)
FetchContent_GetProperties(cmsis_core)
if(NOT cmsis_core_POPULATED)
    message(STATUS "Fetching ARM CMSIS Core 5.9.0 ...")
    FetchContent_Populate(cmsis_core)
endif()

# ---- ST CMSIS Device for STM32G4 ------------------------------------
FetchContent_Declare(
    cmsis_device_g4
    GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_g4.git
    GIT_TAG        v1.2.6
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(cmsis_device_g4)
if(NOT cmsis_device_g4_POPULATED)
    message(STATUS "Fetching ST CMSIS Device G4 v1.2.6 ...")
    FetchContent_Populate(cmsis_device_g4)
endif()

# ---- INTERFACE library exposing both as a single dependency ---------
# Anything that links cmsis_stm32g4 picks up both include paths plus
# the STM32G474xx chip-select define.
add_library(cmsis_stm32g4 INTERFACE)
target_include_directories(cmsis_stm32g4 INTERFACE
    ${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include
    ${cmsis_device_g4_SOURCE_DIR}/Include
)
target_compile_definitions(cmsis_stm32g4 INTERFACE
    STM32G474xx              # selects which device header stm32g4xx.h pulls in
)
