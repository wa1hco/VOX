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
#include "clock_init.h"
#include "proto_transport.h"
#include "systick.h"
#include "synth_audio.h"
#include "usb/usb_init.h"
#include "vox.h"
#include "tusb.h"
#include "vox_dongle_proto.h"

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

/* ---- FFT-direction probe internals -----------------------------------
 * speexdsp doesn't expose spx_fft_init in any public header.  We
 * declare it manually here for the probe; the symbol is in the
 * libspeexdsp_static archive we link, so the linker resolves it.
 * Layouts shadowed below match the speexdsp 1.2.1 source.  This is
 * diagnostic-only code; if speex's struct order changes, the probe
 * needs to be updated by hand.
 */
extern void *spx_fft_init(int size);

/* struct kiss_config { kiss_fftr_cfg forward; kiss_fftr_cfg backward; int N; }
 * — fftwrap.c (USE_KISS_FFT path).  forward/backward are pointers, so
 * offset 0/1 of a void** view of the kiss_config struct.
 */
/* struct kiss_fftr_state { kiss_fft_cfg substate; kiss_fft_cpx *tmpbuf; ... }
 * — kiss_fftr.h.  substate is a pointer at offset 0.
 */
/* struct kiss_fft_state { int nfft; int inverse; int factors[...]; ... }
 * — kiss_fft.h.  nfft at offset 0, inverse at offset 4.
 */

/* Set by vox_clock_init_pll144() at boot.  HCLK = PCLK1 = PCLK2 = this
 * value (AHB/APB prescalers all at /1).  USART BRR and SysTick reload
 * pull from this same constant so they track together. */
#define SYSCLK_HZ       144000000U
/* Bumped from 115200 → 921600 so the protocol can carry INJECT_PCM
 * frames at 50 fps (256 kbit/s payload + 5% overhead).  ST-Link/V3
 * VCP handles 921600 cleanly; HSI16+PLL clock accuracy of ~1% leaves
 * comfortable headroom over UART's ~2% tolerance. */
#define UART_BAUD       921600U
#define VOX_SAMPLE_RATE 8000
#define VOX_FRAME_MS    20
#define VOX_FRAME_SIZE  (VOX_SAMPLE_RATE * VOX_FRAME_MS / 1000)   /* 160 */

#define USART_AF_PA2_TX 7U
#define USART_AF_PA3_RX 7U

/* ---------------- USART2 / protocol transport ----------------------- *
 * uart_init() configures the USART2 peripheral clock + GPIO AF, then
 * hands off to vox_proto_transport_init() which sets baud, enables
 * RX/TX, and arms the RXNE interrupt.  All chip→host traffic from
 * here on is framed VOX_MSG_* via vox_proto_send / vox_proto_log;
 * no ad-hoc UART writes remain.
 */
static void uart_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    /* GPIO AF for PA2/PA3 is set in gpio_init() before we get here. */
    vox_proto_transport_init(SYSCLK_HZ, UART_BAUD);
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

/* ---------------- Protocol emitters & handlers ---------------------- */

/*
 * Once at boot (and on any QUERY_HELLO from the host), send a HELLO
 * that advertises who we are and what capabilities we expose.
 */
static void emit_hello(void)
{
    VoxHello h = {0};
    h.proto_version  = VOX_PROTO_VERSION;
    /* fw_revision is a placeholder until slice I lands a real build
     * stamp.  Keep it short (< 16 chars incl. terminator). */
    const char rev[] = "vox-dev";
    for (size_t i = 0; i < sizeof(rev) && i < sizeof(h.fw_revision); i++)
        h.fw_revision[i] = rev[i];
    h.fw_build_unix  = 0;
    h.capabilities   = VOX_CAP_LOG_STREAM;
    h.hw_id          = 0x47340000u; /* "G4" + zero filler; refine later */
    (void)vox_proto_send(VOX_MSG_HELLO, &h, sizeof(h));
}

/*
 * Build a VoxStateFrameV1 from vox_core's two state structs and ship
 * it.  Called once per 20 ms frame loop iteration.
 *
 * The wire struct (vox_dongle_proto.h) is deliberately a *subset* of
 * VoxLedState + VoxDebugState — fixed layout, narrow types, no
 * residual-correlation profile (sent on request later if needed).
 * This widens the wire frame's compatibility window if vox_core's
 * internal structs grow over time.
 *
 * leds bit layout:    bit0=MIC bit1=RX bit2=VAD bit3=AEC bit4=PTT
 * flags bit layout:   bit0=voice_raw  bit1=voice_validated  bit2=rx_active
 *                     bit3=energy_ok  bit4=rx_guard_applied
 *                     bit5=ptt_reason_voice  bit6=ptt_reason_hang
 */
static void send_state_frame(uint32_t seq, uint32_t now_ms,
                             const VoxLedState *led, const VoxDebugState *dbg,
                             int ptt)
{
    VoxStateFrameV1 f = {0};
    f.seq                              = seq;
    f.timestamp_ms                     = now_ms;
    f.mic_level_raw                    = (int16_t)dbg->mic_level_raw;
    f.mic_level_post_aec               = (int16_t)dbg->mic_level_post_aec;
    f.rx_level                         = (int16_t)led->rx_level;
    f.noise_floor                      = (int16_t)dbg->noise_floor;
    f.vad_probability_raw              = (int16_t)led->vad_probability_raw;
    f.vad_probability                  = (int16_t)led->vad_probability;
    f.aec_reduction_pct                = (int16_t)led->aec_reduction_pct;
    f.snr_pct                          = (int16_t)dbg->snr_pct;

    f.leds  = (uint8_t)((led->mic_led ? 1u : 0u)        |
                        (led->rx_led  ? 1u << 1 : 0u)   |
                        (led->vad_led ? 1u << 2 : 0u)   |
                        (led->aec_led ? 1u << 3 : 0u)   |
                        (ptt          ? 1u << 4 : 0u));
    f.flags = (uint8_t)((dbg->voice_raw         ? 1u      : 0u) |
                        (dbg->voice_validated   ? 1u << 1 : 0u) |
                        (dbg->rx_active         ? 1u << 2 : 0u) |
                        (dbg->energy_ok         ? 1u << 3 : 0u) |
                        (dbg->rx_guard_applied  ? 1u << 4 : 0u) |
                        (dbg->ptt_reason_voice  ? 1u << 5 : 0u) |
                        (dbg->ptt_reason_hang   ? 1u << 6 : 0u));

    f.hang_frames                      = (int16_t)dbg->hang_frames;
    f.hang_frames_max                  = (int16_t)dbg->hang_frames_max;
    f.effective_vad_threshold          = (int16_t)dbg->effective_vad_threshold;
    f.effective_snr_threshold          = (int16_t)dbg->effective_snr_threshold;
    f.energy_margin                    = (int16_t)dbg->energy_margin;
    f.reserved0                        = 0;
    f.residual_corr_pct                = (int16_t)dbg->residual_corr_pct;
    f.residual_corr_dbfs_tenths        = (int16_t)dbg->residual_corr_dbfs_tenths;
    f.residual_corr_peak_pct           = (int16_t)dbg->residual_corr_peak_pct;
    f.residual_corr_peak_delay_samples = (int16_t)dbg->residual_corr_peak_delay_samples;

    (void)vox_proto_send(VOX_MSG_STATE_FRAME, &f, sizeof(f));
}

/*
 * Parser callback — fires once per fully decoded frame coming from
 * the host.  Today only QUERY_HELLO is wired; SET_TUNING / INJECT_PCM
 * / SET_MODE handling lands in G-bridge-3.
 */
static void on_proto_message(void *ctx, VoxMsgType type,
                             const uint8_t *payload, size_t payload_len)
{
    (void)ctx; (void)payload; (void)payload_len;
    switch (type) {
    case VOX_MSG_QUERY_HELLO:
        emit_hello();
        break;
    default:
        /* Forward-compat: silently ignore types we don't handle yet.
         * The host's capability check should keep this from happening
         * for messages we haven't advertised support for. */
        break;
    }
}

/* ---------------- main ---------------------------------------------- */

int main(void)
{
    /* Clock first — at 16 MHz HSI16 every subsequent timing constant
     * (UART baud, SysTick reload) would have to be recomputed if the
     * caller ever lowers/raises SYSCLK.  Doing it here lets the rest
     * of main() use SYSCLK_HZ as a static truth. */
    (void)vox_clock_init_pll144();

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

    /* Boot-time HELLO frame so a host that's listening when we come
     * up immediately knows who we are.  Sent again on QUERY_HELLO. */
    emit_hello();
    vox_proto_log("VOX boot: sysclk=144MHz fs=8kHz frame=20ms scenario=synth");

    /* Bring up vox_core.  speex_echo_state_init + speex_preprocess_state_init
     * malloc internally — that's the first real exercise of our _sbrk.
     *
     * aec_filter_frames=4 (80 ms tail) is required on this target —
     * the host default of 16 (320 ms) reliably stomps the preprocess
     * state's heap region during speex_echo_cancellation.  Reproduced
     * deterministically in pure speex API (no vox_core involvement);
     * see memory:mcu-aec-tail-bug.md.
     */
    vox_proto_log("calling vox_create...");
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
        .aec_filter_frames      = 4,
    };
    VoxState *vox = vox_create(&vc);
    if (!vox) {
        vox_proto_log("FATAL: vox_create() returned NULL — heap or speexdsp init failed.");
        for (;;) { __asm__ volatile ("wfi"); }
    }
    vox_proto_log("vox_create() ok");

    /* USB CDC-ACM bring-up.  Configures the chip-side peripheral (clock
     * source, GPIO AF, NVIC), then asks TinyUSB to enumerate as a CDC
     * device.  When a USB host is connected to PA11/PA12 (CN10 pins 14
     * and 12 on the Morpho header — the standard NUCLEO-G474RE has no
     * dedicated USB-user connector), /dev/ttyACMx appears within a
     * second.  The custom CB board has a proper USB micro-B for this. */
    vox_usb_init();
    tud_init(0);
    vox_proto_log("USB CDC-ACM init ok (no host on PA11/PA12 expected on Nucleo-64)");

    /* Now safe to enable the 1 ms tick — init's float-heavy paths are done. */
    vox_systick_init(SYSCLK_HZ);
    synth_audio_reset(vox_systick_now_ms());

    /* Frame loop: every 20 ms fill a frame, run vox, drive outputs.
     * 16 MHz HSI is comfortably enough to handle 50 frames/sec of an
     * 8 kHz speexdsp pipeline; a real measurement comes later. */
    int16_t mic[VOX_FRAME_SIZE];
    int16_t rx[VOX_FRAME_SIZE];
    uint32_t next_deadline = vox_systick_now_ms();
    uint32_t last_log_ms = 0;
    uint32_t frame_n = 0;

    /* Protocol parser for incoming bytes from the host.  on_proto_message
     * handles QUERY_HELLO now; SET_TUNING / INJECT_PCM / SET_MODE land
     * in G-bridge-3. */
    VoxProtoParser proto_parser;
    vox_proto_parser_init(&proto_parser);

    /* Frame loop — full VOX pipeline at 50 fps.
     *
     * Slice C: vox_process runs (with AEC tail M=4 to dodge the speex
     * preprocess-state heap stomp; see memory:mcu-aec-tail-bug.md).
     * Slice D: SYSCLK = 144 MHz.
     * Slice G1: TinyUSB CDC-ACM is up (no host on PA11/PA12 yet).
     * Slice G-bridge-1: chip→host LOG/HELLO frames go out over
     * USART2 → ST-Link VCP → host.  STATE_FRAME emission lands in
     * G-bridge-2; SET_TUNING / INJECT_PCM handling in G-bridge-3.
     */
    for (;;) {
        next_deadline += VOX_FRAME_MS;
        while ((int32_t)(vox_systick_now_ms() - next_deadline) < 0) {
            tud_task();   /* USB device task — runs while we idle */
            __asm__ volatile ("wfi");
        }

        const uint32_t now = vox_systick_now_ms();
        (void)synth_audio_fill_frame(mic, rx, VOX_FRAME_SIZE, now);

        const int ptt = vox_process(vox, mic, rx);
        tud_task();   /* belt-and-suspenders USB pump per frame */

        VoxLedState led = {0};
        vox_get_led_state(vox, &led);
        VoxDebugState dbg = {0};
        vox_get_debug_state(vox, &dbg);

        gpio_write(cfg->pin_led_mic, led.mic_led);
        gpio_write(cfg->pin_led_rx,  led.rx_led);
        gpio_write(cfg->pin_led_vad, led.vad_led);
        gpio_write(cfg->pin_led_aec, led.aec_led);
        gpio_write(cfg->pin_led_ptt, ptt);
        gpio_write(cfg->pin_ptt_out, ptt);

        /* Stream this frame's state to the host.  46-byte packed wire
         * struct + 7-byte framing = 53 B per frame × 50 fps = 2.65 KB/s.
         * Tiny fraction of the 921600-baud budget. */
        frame_n++;
        send_state_frame(frame_n, now, &led, &dbg, ptt);

        /* Drain host→chip bytes through the parser, dispatching frames
         * to on_proto_message().  Handles QUERY_HELLO today. */
        (void)vox_proto_drain_rx(&proto_parser, on_proto_message, NULL);

        /* Periodic chatter LOG — useful while G-bridge UI doesn't yet
         * subscribe to STATE_FRAME.  Dial back / remove once vox_qt
         * renders the state stream directly. */
        if ((now - last_log_ms) >= 5000) {
            vox_proto_log("alive: streaming STATE_FRAME @ 50 fps");
            last_log_ms = now;
        }
    }
}
