#ifndef VOX_MCU_PINS_H
#define VOX_MCU_PINS_H

/*
 * vox_mcu_pins.h — Generic GPIO pin encoding layer.
 *
 * Provides VOX_PIN(port, pin) and associated port constants for encoding
 * a GPIO port+pin index into a single uint8_t.  Board-specific pin map
 * headers (e.g. vox_pins_stm32G474CB.h) include this file and use these
 * macros to define signal assignments without duplicating the encoding logic.
 */

#include <stdint.h>
#define VOX_GPIO_PORT_A 0u
#define VOX_GPIO_PORT_B 1u
#define VOX_GPIO_PORT_C 2u
#define VOX_GPIO_PORT_D 3u
#define VOX_GPIO_PORT_E 4u
#define VOX_GPIO_PORT_H 7u

#define VOX_PIN_NONE 0xFFu
#define VOX_PIN(port, pin) ((uint8_t)((((port) & 0x0Fu) << 4) | ((pin) & 0x0Fu)))
#define VOX_PIN_PORT(pin) (((pin) >> 4) & 0x0Fu)
#define VOX_PIN_INDEX(pin) ((pin) & 0x0Fu)

#endif /* VOX_MCU_PINS_H */
