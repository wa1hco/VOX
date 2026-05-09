# stm32g474_vox_cb — VOX HCO custom board (STM32G474CBT3, LQFP48).
#
# At this stage the board has a minimal bring-up firmware (blink + UART
# on PA9 via an external USB-serial adapter).  Adding peripheral drivers
# is the next slice.
#
# This file is included via include() from the top-level CMakeLists.txt,
# so it shares scope with the caller — plain set() (no PARENT_SCOPE) is
# what the includer sees.

set(VOX_BOARD_NAME    "stm32g474_vox_cb")
set(VOX_BOARD_MCU     "STM32G474CBT3")
set(VOX_BOARD_PACKAGE "LQFP48")

set(VOX_BOARD_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/board.c
)

set(VOX_BOARD_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/platform/mcu/boards/common
)

set(VOX_BOARD_MAIN_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/main.c
)

set(VOX_BOARD_LINKER_SCRIPT
    ${CMAKE_CURRENT_LIST_DIR}/stm32g474cb.ld
)

# Custom board has no on-board ST-Link.  Use an external SWD probe
# (Nucleo-as-ST-Link, J-Link, etc.).  st-flash works as long as some
# ST-Link is connected.
set(VOX_BOARD_FLASH_COMMAND
    "st-flash --connect-under-reset --reset write @BIN@ 0x08000000"
)
