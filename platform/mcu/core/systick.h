#ifndef VOX_MCU_SYSTICK_H
#define VOX_MCU_SYSTICK_H

/*
 * systick.h — millisecond timing service for the bring-up firmware.
 *
 * Provides:
 *   - vox_systick_init(sysclk_hz)   start a 1 kHz interrupt-driven tick
 *   - vox_systick_now_ms()          monotonic ms count since init
 *   - vox_delay_ms(ms)              busy-wait that yields to interrupts
 *
 * Why it exists: the bring-up firmware was using an instruction-count
 * busy loop for delays, which drifts with cache state and optimization
 * level.  Once we run a 20 ms VOX frame loop we need real timing or
 * the algorithm's noise tracking and hang timer will misbehave.
 *
 * Rolling overflow at 2^32 ms ≈ 49 days is fine for a continuously-
 * running radio dongle that gets power-cycled.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure SysTick for a 1 ms interrupt rate from the given core clock.
 * Returns 0 on success, nonzero if the divisor doesn't fit (24-bit reload). */
int vox_systick_init(uint32_t sysclk_hz);

/* Monotonic millisecond counter. */
uint32_t vox_systick_now_ms(void);

/* Block until at least `ms` milliseconds have elapsed.  Uses WFI so the
 * core sleeps between interrupts. */
void vox_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* VOX_MCU_SYSTICK_H */
