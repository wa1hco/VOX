/*
 * main.c — VOX custom-board (STM32G474CBT3) bring-up + slice C.
 *
 * Same pipeline as the Nucleo target (synthetic audio → vox_process →
 * GPIO + UART) but with custom-board-specific differences:
 *
 *   - Debug UART on USART1 PA9/PA10 instead of USART2 (no on-board
 *     ST-Link/VCP — connect a 3.3V USB-serial adapter to PA9/PA10).
 *   - LD2 conflict on PA5 doesn't apply — the custom board has a
 *     dedicated LED_MIC LED.
 *   - Tighter SRAM (32 KB) on the LQFP48 part: speexdsp's malloc
 *     footprint may force a smaller AEC tail later, but the bring-up
 *     scenario (no real audio) shouldn't push it.
 *
 * See the Nucleo main.c for the rest of the design rationale; the only
 * intended difference between the two boards at this stage is which
 * USART instance handles the debug UART.
 */

#include "stm32g4_min.h"
#include "vox_mcu_pins.h"
#include "vox_mcu_board.h"
#include "clock_init.h"
#include "systick.h"
#include "synth_audio.h"
#include "vox.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Set by vox_clock_init_pll144() at boot. */
#define SYSCLK_HZ       144000000U
#define UART_BAUD       115200U
#define VOX_SAMPLE_RATE 8000
#define VOX_FRAME_MS    20
#define VOX_FRAME_SIZE  (VOX_SAMPLE_RATE * VOX_FRAME_MS / 1000)   /* 160 */

#define USART_AF_PA9_TX  7U
#define USART_AF_PA10_RX 7U

/* ---------------- UART (USART1 → external USB-serial adapter) ------- */

static void uart_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    USART1->CR1 = 0;
    USART1->BRR = SYSCLK_HZ / UART_BAUD;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void uart_write_char(char c)
{
    while ((USART1->ISR & USART_ISR_TXE) == 0)
        ;
    USART1->TDR = (uint8_t)c;
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

/* ---------------- GPIO (same helpers as Nucleo build) --------------- */

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
    if (value)
        port->BSRR = (1U << pin);
    else
        port->BSRR = (1U << (pin + 16));
}

static void gpio_init(const VoxMcuPinConfig *cfg)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;

    gpio_set_output(cfg->pin_led_mic);
    gpio_set_output(cfg->pin_led_rx);
    gpio_set_output(cfg->pin_led_vad);
    gpio_set_output(cfg->pin_led_aec);
    gpio_set_output(cfg->pin_led_ptt);
    gpio_set_output(cfg->pin_ptt_out);

    gpio_write(cfg->pin_led_mic, 0);
    gpio_write(cfg->pin_led_rx, 0);
    gpio_write(cfg->pin_led_vad, 0);
    gpio_write(cfg->pin_led_aec, 0);
    gpio_write(cfg->pin_led_ptt, 0);
    gpio_write(cfg->pin_ptt_out, 0);

    gpio_set_analog(cfg->pin_mic_audio_in);
    gpio_set_analog(cfg->pin_rx_audio_in);

    /* PA9 (USART1_TX) AF7 / PA10 (USART1_RX) AF7. */
    GPIO_FIELD2_SET(GPIOA->MODER, 9,  GPIO_MODE_AF);
    GPIO_AF_SET(GPIOA, 9,  USART_AF_PA9_TX);
    GPIO_FIELD2_SET(GPIOA->MODER, 10, GPIO_MODE_AF);
    GPIO_AF_SET(GPIOA, 10, USART_AF_PA10_RX);
}

/* ---------------- LED state print helper (copy of Nucleo) ----------- */

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
    (void)vox_clock_init_pll144();   /* SYSCLK 16 MHz HSI → 144 MHz PLL */

    const VoxMcuPinConfig *cfg = vox_mcu_get_pin_config();

    gpio_init(cfg);
    uart_init();
    vox_systick_init(SYSCLK_HZ);

    uart_write("\r\n");
    uart_write("VOX slice-D: board=stm32g474_vox_cb  sysclk=144MHz  fs=8kHz frame=20ms\r\n");
    uart_write("Synthetic scenario: silence | RX-only | mic-only | mic+RX, 1s each.\r\n");

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
        .aec_filter_frames      = 4,    /* see memory:mcu-aec-tail-bug.md */
    };
    VoxState *vox = vox_create(&vc);
    if (!vox) {
        uart_write("FATAL: vox_create() returned NULL — heap or speexdsp init failed.\r\n");
        for (;;) { __asm__ volatile ("wfi"); }
    }
    uart_write("vox_create() ok — heap and speexdsp alive.\r\n");

    synth_audio_reset(vox_systick_now_ms());

    int16_t mic[VOX_FRAME_SIZE];
    int16_t rx[VOX_FRAME_SIZE];
    VoxLedState led_prev = {0};
    int ptt_prev = 0;
    uint32_t next_deadline = vox_systick_now_ms();

    for (;;) {
        next_deadline += VOX_FRAME_MS;
        while ((int32_t)(vox_systick_now_ms() - next_deadline) < 0)
            __asm__ volatile ("wfi");

        uint32_t now = vox_systick_now_ms();
        int phase = synth_audio_fill_frame(mic, rx, VOX_FRAME_SIZE, now);

        int ptt = vox_process(vox, mic, rx);

        VoxLedState led = {0};
        vox_get_led_state(vox, &led);

        gpio_write(cfg->pin_led_mic, led.mic_led);
        gpio_write(cfg->pin_led_rx,  led.rx_led);
        gpio_write(cfg->pin_led_vad, led.vad_led);
        gpio_write(cfg->pin_led_aec, led.aec_led);
        gpio_write(cfg->pin_led_ptt, ptt);
        gpio_write(cfg->pin_ptt_out, ptt);

        if (led_state_changed(&led, &led_prev, ptt, ptt_prev)) {
            print_state(phase, &led, ptt);
            led_prev = led;
            ptt_prev = ptt;
        }
    }
}
