#ifndef VOX_MCU_PINS_H
#define VOX_MCU_PINS_H

#include <stdint.h>

/* Encoded GPIO pin helper used by board pin maps. */
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
