/*
 * usb_descriptors.c — USB descriptor table for the VOX dongle.
 *
 * One CDC-ACM (virtual COM port) interface.  Two interfaces total:
 *   - Interface 0: CDC Communication Interface Class (notifications)
 *   - Interface 1: CDC Data Interface Class (the bulk pipes)
 *
 * Endpoints (post-config):
 *   - EP0  IN/OUT  — control (TinyUSB owns this)
 *   - EP1  IN      — CDC notifications (interrupt, 16-byte)
 *   - EP2  OUT     — CDC data OUT (bulk, 64-byte)
 *   - EP2  IN      — CDC data IN  (bulk, 64-byte)
 *
 * Vendor/Product IDs: using PID.codes 0x1209/0x0001 generic test pair
 * for development.  These are explicitly OK for hobbyist development;
 * we'll register a real PID before any production deployment.
 */

#include "tusb.h"

/* ---------- Device descriptor --------------------------------------- */

#define VOX_USB_VID        0x1209  /* PID.codes generic */
#define VOX_USB_PID        0x0001  /* PID.codes test PID */
#define VOX_USB_BCD_DEVICE 0x0100  /* device version 1.00 */

/* String indices (must match the order in string_desc_arr below). */
enum {
    STRID_LANGID  = 0,
    STRID_MFR     = 1,
    STRID_PRODUCT = 2,
    STRID_SERIAL  = 3,
    STRID_CDC     = 4,
};

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,        /* USB 2.0 */

    /* Use the IAD class so OS picks up CDC + future composite
     * interfaces correctly.  For a single CDC the actual interface
     * descriptors carry the CDC class codes. */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = VOX_USB_VID,
    .idProduct          = VOX_USB_PID,
    .bcdDevice          = VOX_USB_BCD_DEVICE,

    .iManufacturer      = STRID_MFR,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,

    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/* ---------- Configuration descriptor --------------------------------- */
/*
 * Single configuration with one CDC-ACM interface block.  TinyUSB's
 * TUD_CDC_DESCRIPTOR helper expands to the right sequence of headers.
 */

#define EPNUM_CDC_NOTIF   0x81   /* IN endpoint for CDC notifications  */
#define EPNUM_CDC_OUT     0x02   /* OUT endpoint for CDC data          */
#define EPNUM_CDC_IN      0x82   /* IN endpoint for CDC data           */

enum {
    ITF_CDC_COMM,
    ITF_CDC_DATA,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

uint8_t const desc_configuration[] = {
    /* config descriptor: 1 config, ITF_NUM_TOTAL interfaces, 100 mA bus-powered */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    /* CDC-ACM interface block.  TinyUSB's helper macro emits all of:
     *   IAD, CDC_COMM IF, header/CMF/ACM/Union func descs, NOTIF EP,
     *   CDC_DATA IF, OUT/IN bulk EPs.
     */
    TUD_CDC_DESCRIPTOR(ITF_CDC_COMM, STRID_CDC, EPNUM_CDC_NOTIF, 16,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;   /* only one config */
    return desc_configuration;
}

/* ---------- String descriptors -------------------------------------- */
/*
 * String descriptor 0 is the LangID array.  All other strings are
 * UTF-16 LE.  TinyUSB packs UTF-16 from our 8-bit C-string at runtime
 * via the helper below.
 */

static char const *string_desc_arr[] = {
    [STRID_LANGID]  = (const char[]){ 0x09, 0x04 },  /* English (US) */
    [STRID_MFR]     = "VOX",
    [STRID_PRODUCT] = "VOX dongle (CDC)",
    [STRID_SERIAL]  = "0001",
    [STRID_CDC]     = "VOX CDC interface",
};

static uint16_t _desc_str_buf[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    size_t chr_count;
    if (index == STRID_LANGID) {
        memcpy(&_desc_str_buf[1], string_desc_arr[STRID_LANGID], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
            return NULL;

        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        const size_t max_chars = sizeof(_desc_str_buf) / sizeof(_desc_str_buf[0]) - 1;
        if (chr_count > max_chars)
            chr_count = max_chars;

        for (size_t i = 0; i < chr_count; i++)
            _desc_str_buf[i + 1] = (uint16_t)str[i];
    }

    /* First word: type (3 = string) | length in bytes (2 + 2*chr_count). */
    _desc_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str_buf;
}
