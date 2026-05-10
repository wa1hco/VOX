#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/*
 * tusb_config.h — TinyUSB compile-time configuration for the VOX dongle.
 *
 * Just enough to enumerate as a CDC-ACM device and shuttle bytes both
 * ways at full speed.  No host-mode, no other classes, no DMA.
 *
 * TinyUSB's defaults are mostly fine for our minimal CDC use; we set
 * only what we need to override.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- MCU + RTOS selection ------------------------------------ */
#define CFG_TUSB_MCU                OPT_MCU_STM32G4
#define CFG_TUSB_OS                 OPT_OS_NONE

/* USB peripheral type on G4: full-speed device-only (the stm32_fsdev
 * port).  Don't confuse with the OTG-FS peripheral on bigger STM32s. */

/* Speed: full-speed only (12 Mbps).  G4 USB doesn't do high-speed. */
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED

/* ---------- Device-mode core ---------------------------------------- */
#define CFG_TUD_ENABLED             1
#define CFG_TUH_ENABLED             0   /* no host mode */

/* Endpoint 0 max packet size — 64 is the standard FS value. */
#define CFG_TUD_ENDPOINT0_SIZE      64

/* ---------- Classes enabled ----------------------------------------- */
#define CFG_TUD_CDC                 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_VENDOR              0
#define CFG_TUD_AUDIO               0

/* ---------- CDC-specific buffer sizes ------------------------------- */
/* TinyUSB's default CDC RX/TX FIFOs are 64 bytes; bump them so we
 * have headroom for a full state-frame burst (~50 frames/sec × ~50
 * bytes per state frame = 2.5 KB/s, plus occasional INJECT_PCM at 640
 * bytes per frame).  256 is comfortable and only ~1 KB total per
 * interface. */
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      256
#define CFG_TUD_CDC_EP_BUFSIZE      64   /* one FS bulk packet */

/* ---------- Memory + alignment -------------------------------------- */
/* The stm32_fsdev port wants USB packet buffers in regular RAM (it
 * copies them through the chip's PMA itself), so the section attribute
 * doesn't matter for us. */
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))

/* Default debug level (0 = silent).  Bump to 1 or 2 during bring-up. */
#define CFG_TUSB_DEBUG              0

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
