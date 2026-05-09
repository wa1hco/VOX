/*
 * main.c — Nucleo G474RE bring-up + slice C: vox_core in the loop.
 *
 * Pipeline (no analog hardware required):
 *   1) SysTick at 1 kHz drives a 20 ms frame scheduler (160 samples
 *      per frame at 8 kHz, matching the Linux sim's frame size).
 *   2) synth_audio_fill_frame() generates a deterministic scenario:
 *      silence → RX-only → mic-only → mic+RX, looping every 4 s.
 *   3) vox_process() runs over each frame; the same code path the
 *      Linux sim uses, linked here against the same speexdsp 1.2.1.
 *   4) GPIO writes drive LED_{MIC,RX,VAD,AEC,PTT} and PTT_OUT from
 *      VoxLedState.  LD2 (Nucleo's user LED on PA5) doubles as LED_MIC.
 *   5) UART2 (ST-Link VCP, /dev/ttyACM0) prints LED-state transitions
 *      and the scenario phase, mirroring vox_linux's transition output.
 *
 * Why this slice exists: it validates the heap (speexdsp mallocs at
 * init), confirms vox_core links and runs on Cortex-M4F, and
 * demonstrates the GPIO output path — all without depending on the
 * analog front end being built yet.
 *
 * What it does NOT do: ADC, DMA, real audio.  That comes when the
 * mic/speaker analog gets wired up; this code's frame source then
 * swaps from synth_audio to the ADC ring.
 */

#include "stm32g4_min.h"
#include "vox_mcu_pins.h"
#include "vox_mcu_board.h"
#include "systick.h"
#include "synth_audio.h"
#include "vox.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

/* sbrk(0) returns the current heap break without allocating; useful
 * for instrumentation prints during bring-up. */
extern void *_sbrk(int incr);
extern uint32_t _heap_start;

#define SYSCLK_HZ       16000000U      /* HSI16 default; PLL comes later */
#define UART_BAUD       115200U
#define VOX_SAMPLE_RATE 8000
#define VOX_FRAME_MS    20
#define VOX_FRAME_SIZE  (VOX_SAMPLE_RATE * VOX_FRAME_MS / 1000)   /* 160 */

#define USART_AF_PA2_TX 7U
#define USART_AF_PA3_RX 7U

/* ---------------- UART (USART2 → ST-Link VCP) ----------------------- */

static void uart_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;

    USART2->CR1 = 0;
    USART2->BRR = SYSCLK_HZ / UART_BAUD;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void uart_write_char(char c)
{
    while ((USART2->ISR & USART_ISR_TXE) == 0)
        ;
    USART2->TDR = (uint8_t)c;
}

static void uart_write(const char *s)
{
    while (*s)
        uart_write_char(*s++);
}

static void uart_write_u32(uint32_t v)
{
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (v == 0) { uart_write_char('0'); return; }
    while (v > 0 && i > 0) {
        buf[--i] = '0' + (v % 10);
        v /= 10;
    }
    uart_write(&buf[i]);
}

static void uart_write_i32(int32_t v)
{
    if (v < 0) {
        uart_write_char('-');
        uart_write_u32((uint32_t)(-v));
    } else {
        uart_write_u32((uint32_t)v);
    }
}

/* ---------------- GPIO ---------------------------------------------- */
/*
 * The board pin descriptor encodes each signal as (port,pin) packed in
 * a uint8_t (vox_mcu_pins.h).  We read it at boot and drive each signal
 * via that handle — same pattern the Linux sim uses to track LEDs by
 * symbolic name.  Direct GPIOA/GPIOB writes elsewhere in this file are
 * for fixed-purpose pins (UART, eventual ADC) where the encoding adds
 * no value.
 */

static GPIO_TypeDef *gpio_port(uint8_t pin_handle)
{
    switch (VOX_PIN_PORT(pin_handle)) {
    case VOX_GPIO_PORT_A: return GPIOA;
    case VOX_GPIO_PORT_B: return GPIOB;
    case VOX_GPIO_PORT_C: return GPIOC;
    default:              return NULL;
    }
}

static void gpio_set_output(uint8_t pin_handle)
{
    GPIO_TypeDef *port = gpio_port(pin_handle);
    if (!port) return;
    uint32_t pin = VOX_PIN_INDEX(pin_handle);
    GPIO_FIELD2_SET(port->MODER, pin, GPIO_MODE_OUTPUT);
}

static void gpio_set_analog(uint8_t pin_handle)
{
    GPIO_TypeDef *port = gpio_port(pin_handle);
    if (!port) return;
    uint32_t pin = VOX_PIN_INDEX(pin_handle);
    GPIO_FIELD2_SET(port->MODER, pin, GPIO_MODE_ANALOG);
}

static void gpio_write(uint8_t pin_handle, int value)
{
    GPIO_TypeDef *port = gpio_port(pin_handle);
    if (!port) return;
    uint32_t pin = VOX_PIN_INDEX(pin_handle);
    /* BSRR low half = set, high half = reset (atomic). */
    if (value)
        port->BSRR = (1U << pin);
    else
        port->BSRR = (1U << (pin + 16));
}

static void gpio_init(const VoxMcuPinConfig *cfg)
{
    /* Enable GPIOA + GPIOB clocks (LEDs straddle both ports). */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;

    /* LEDs + PTT — outputs. */
    gpio_set_output(cfg->pin_led_mic);
    gpio_set_output(cfg->pin_led_rx);
    gpio_set_output(cfg->pin_led_vad);
    gpio_set_output(cfg->pin_led_aec);
    gpio_set_output(cfg->pin_led_ptt);
    gpio_set_output(cfg->pin_ptt_out);

    /* All off at boot. */
    gpio_write(cfg->pin_led_mic, 0);
    gpio_write(cfg->pin_led_rx, 0);
    gpio_write(cfg->pin_led_vad, 0);
    gpio_write(cfg->pin_led_aec, 0);
    gpio_write(cfg->pin_led_ptt, 0);
    gpio_write(cfg->pin_ptt_out, 0);

    /* Mic/RX ADC pins → analog mode so they don't fight the
     * (eventually wired) analog front end.  No ADC sampling in this
     * slice, but configuring the pin correctly costs nothing. */
    gpio_set_analog(cfg->pin_mic_audio_in);
    gpio_set_analog(cfg->pin_rx_audio_in);

    /* PA2 (USART2_TX) AF7. */
    GPIO_FIELD2_SET(GPIOA->MODER, 2, GPIO_MODE_AF);
    GPIO_AF_SET(GPIOA, 2, USART_AF_PA2_TX);
    /* PA3 (USART2_RX) AF7. */
    GPIO_FIELD2_SET(GPIOA->MODER, 3, GPIO_MODE_AF);
    GPIO_AF_SET(GPIOA, 3, USART_AF_PA3_RX);
}

/* ---------------- LED state print helper ---------------------------- */

static void print_state(int phase, const VoxLedState *led, int ptt)
{
    uart_write("phase=");
    uart_write_u32((uint32_t)phase);
    uart_write(" MIC=");  uart_write_char(led->mic_led ? '1' : '0');
    uart_write(" RX=");   uart_write_char(led->rx_led  ? '1' : '0');
    uart_write(" VAD=");  uart_write_char(led->vad_led ? '1' : '0');
    uart_write(" AEC=");  uart_write_char(led->aec_led ? '1' : '0');
    uart_write(" PTT=");  uart_write_char(ptt          ? '1' : '0');
    uart_write("  mic="); uart_write_i32(led->mic_level);
    uart_write(" rx=");   uart_write_i32(led->rx_level);
    uart_write(" vad%="); uart_write_i32(led->vad_probability);
    uart_write(" red%="); uart_write_i32(led->aec_reduction_pct);
    uart_write("\r\n");
}

static int led_state_changed(const VoxLedState *a, const VoxLedState *b, int ptt_a, int ptt_b)
{
    return a->mic_led != b->mic_led ||
           a->rx_led  != b->rx_led  ||
           a->vad_led != b->vad_led ||
           a->aec_led != b->aec_led ||
           ptt_a      != ptt_b;
}

/* ---------------- main ---------------------------------------------- */

int main(void)
{
    /* Order matters: GPIO/UART must be alive before we try to print
     * any "vox_create failed" diagnostic from the heap path. */
    const VoxMcuPinConfig *cfg = vox_mcu_get_pin_config();

    gpio_init(cfg);
    uart_init();
    /* Defer SysTick start until AFTER vox_create — speexdsp's filterbank_new
     * uses heavy FPU math during init.  SysTick interrupts would trigger
     * lazy FPU stacking on every 1 ms tick, and on at least one chip we
     * saw saved-xPSR.T corruption (UsageFault → HardFault).  Once init
     * is done we enable the tick for the frame loop. */

    uart_write("\r\n");
    uart_write("VOX slice-C: board=stm32g474_nucleo  sysclk=16MHz  fs=8kHz frame=20ms\r\n");
    uart_write("Synthetic scenario: silence | RX-only | mic-only | mic+RX, 1s each.\r\n");

    /* Bring up vox_core.  speex_echo_state_init + speex_preprocess_state_init
     * malloc internally — that's the first real exercise of our _sbrk. */
    uart_write("heap_start=");
    uart_write_u32((uint32_t)&_heap_start);
    uart_write(" calling vox_create...\r\n");
    VoxConfig vc = {
        .sample_rate            = VOX_SAMPLE_RATE,
        .frame_size             = VOX_FRAME_SIZE,
        .hang_ms                = 500,
        .mic_led_threshold      = 300,
        .rx_led_threshold       = 300,
        .vad_led_prob_threshold = 60,
        .aec_led_reduction_pct  = 20,
        .rx_guard_vad_boost     = 15,
        .rx_guard_snr_pct       = 145,
    };
    VoxState *vox = vox_create(&vc);
    if (!vox) {
        uart_write("FATAL: vox_create() returned NULL — heap or speexdsp init failed.\r\n");
        for (;;) { __asm__ volatile ("wfi"); }
    }
    uart_write("vox_create() ok — heap and speexdsp alive.\r\n");

    /* Now safe to enable the 1 ms tick — init's float-heavy paths are done. */
    vox_systick_init(SYSCLK_HZ);
    synth_audio_reset(vox_systick_now_ms());

    /* Frame loop: every 20 ms fill a frame, run vox, drive outputs.
     * 16 MHz HSI is comfortably enough to handle 50 frames/sec of an
     * 8 kHz speexdsp pipeline; a real measurement comes later. */
    int16_t mic[VOX_FRAME_SIZE];
    int16_t rx[VOX_FRAME_SIZE];
    VoxLedState led_prev = {0};
    int ptt_prev = 0;
    uint32_t next_deadline = vox_systick_now_ms();

    /*
     * KNOWN ISSUE — slice C, vox_process disabled for now.
     *
     * The first call to vox_process() reliably trips a fatal-error
     * path inside speexdsp 1.2.1: kiss_fftr2() finds its forward FFT
     * config has substate->inverse == 1 (i.e. it's actually a backward
     * config).  Stack frames at the trap show
     *   preprocess_analysis -> spx_fft -> kiss_fftr2 -> _speex_fatal -> exit
     *
     * What we ruled out:
     *   - heap exhaustion (malloc(40000) succeeds; vox_create allocs
     *     ~50 KB and finishes cleanly)
     *   - stack overflow during init (16 KB stack reserved; raising
     *     it didn't change the symptom)
     *   - VAR_ARRAYS / USE_ALLOCA confusion (removing both didn't help)
     *   - SysTick interrupting FPU context (deferring SysTick init
     *     until after vox_create didn't help)
     *   - synthetic audio causing NaN/Inf into speex (all-zeros input
     *     produces the same crash on the first vox_process call)
     *   - optimization level (still fails at -O2)
     *
     * What we have not yet tried:
     *   - FIXED_POINT speexdsp instead of FLOATING_POINT
     *     (avoids FPU entirely; would isolate FPU/lazy-stacking bugs)
     *   - newer gcc-arm-none-eabi (current: 13.2.1)
     *   - inspecting t->forward->substate->inverse via openocd to see
     *     whether it's wrong from the moment spx_fft_init returns or
     *     gets corrupted later
     *
     * For now, the firmware exercises the rest of the chain (heap,
     * SysTick, GPIO, UART, scenario player) at a 20 ms cadence so we
     * have a stable baseline to bisect from.  vox_process gets wired
     * back in once the FFT-direction trap is understood.
     */
    uint32_t frame_n = 0;
    int blink = 0;
    for (;;) {
        next_deadline += VOX_FRAME_MS;
        while ((int32_t)(vox_systick_now_ms() - next_deadline) < 0)
            __asm__ volatile ("wfi");

        frame_n++;
        uint32_t now = vox_systick_now_ms();
        int phase = synth_audio_fill_frame(mic, rx, VOX_FRAME_SIZE, now);
        (void)phase;

        /* TODO: restore vox_process once the speexdsp FFT-direction
         * trap is resolved.  See block comment above for details. */

        if ((frame_n % 50) == 0) {                /* every second */
            blink = !blink;
            gpio_write(cfg->pin_led_ptt, blink);
            uart_write("alive n="); uart_write_u32(frame_n);
            uart_write(" ms="); uart_write_u32(now);
            uart_write(" phase="); uart_write_u32((uint32_t)phase);
            uart_write(" mic_avg=");
            int32_t s = 0;
            for (int i = 0; i < VOX_FRAME_SIZE; i++)
                s += mic[i] < 0 ? -mic[i] : mic[i];
            uart_write_u32((uint32_t)(s / VOX_FRAME_SIZE));
            uart_write(" rx_avg=");
            s = 0;
            for (int i = 0; i < VOX_FRAME_SIZE; i++)
                s += rx[i] < 0 ? -rx[i] : rx[i];
            uart_write_u32((uint32_t)(s / VOX_FRAME_SIZE));
            uart_write("\r\n");
        }

        (void)led_prev; (void)ptt_prev;
    }
}
