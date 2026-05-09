# stm32g474_nucleo — NUCLEO-G474RE dev board with HCO-mirrored shield.
#
# At this stage the board has a minimal bring-up firmware (blink + UART
# on the ST-Link VCP).  Adding peripheral drivers is the next slice.
#
# This file is included via include() from the top-level CMakeLists.txt,
# so it shares scope with the caller — plain set() (no PARENT_SCOPE) is
# what the includer sees.

set(VOX_BOARD_NAME    "stm32g474_nucleo")
set(VOX_BOARD_MCU     "STM32G474RET6")
set(VOX_BOARD_PACKAGE "LQFP64")

# Board pin descriptor (always built — sanity-checks the pin map even
# without a cross toolchain).
set(VOX_BOARD_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/board.c
)

set(VOX_BOARD_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/platform/mcu/boards/common
)

# Firmware entry point + linker script (used only when cross-compiling).
set(VOX_BOARD_MAIN_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/main.c
)

set(VOX_BOARD_LINKER_SCRIPT
    ${CMAKE_CURRENT_LIST_DIR}/stm32g474re.ld
)

# Recipe to flash the resulting firmware to the on-board ST-Link.
# Order of preference: st-flash (simple) → openocd (more flexible).
set(VOX_BOARD_FLASH_COMMAND
    "st-flash --connect-under-reset --reset write @BIN@ 0x08000000"
)
