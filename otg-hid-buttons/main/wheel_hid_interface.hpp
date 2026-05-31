#pragma once

#include "class/hid/hid_device.h"
#include "tinyusb.h"

// clang-format off


// Report descriptor for 2 buttons hid device
static const uint8_t buttons_report_descriptor [] =
{
    0x05, 0x01, // 0x05 - usage page, 0x01 - Generic Desktop
    0x09, 0x04, // 0x09 - usage, 0x04 - Joystick

    0xA1, 0x01, // // Collection (Application) - this is some USB shit and i can not find its specs in the web rn
    0x05, 0x09, // 0x05 Usage PAge, 0x09 BUtton page
    0x19, 0x01,     //   Usage Minimum (Button 1)       — first button is #1
    0x29, 0x02,     //   Usage Maximum (Button 2)       — last button is #2
    0x15, 0x00,     //   Logical Minimum (0)            — each button value: 0 = released
    0x25, 0x01,     //   Logical Maximum (1)            — each button value: 1 = pressed
    0x75, 0x01,     //   Report Size (1)                — each button takes 1 bit
    0x95, 0x02,     //   Report Count (2)               — there are 2 buttons
    0x81, 0x02,     //   Input (Data, Variable, Abs)    — ** COMMIT: 2 bits of button data **

    // "The remaining 6 bits are padding to fill the byte"
    0x75, 0x06,     //   Report Size (6)                — 6 bits
    0x95, 0x01,     //   Report Count (1)               — one chunk
    0x81, 0x01,     //   Input (Constant)               — ** COMMIT: 6 bits of padding **

    0xC0            // End Collection

};


// ─── USB Descriptors ────────────────────────────────────────────────────────
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},   // 0: Language (English)
    "oskarek inc",          // 1: Manufacturer
    "Gear Buttons",         // 2: Product
    "000001",               // 3: Serial number
    "HID Interface",        // 4: HID interface name
};

static const uint8_t hid_configuration_descriptor[] = {
    // Config descriptor: 1 config, 1 interface, remote wakeup, 100mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // HID descriptor: interface 0, string idx 4, no boot protocol,
    //   report desc size, endpoint 0x81 (IN), 16 byte buffer, 1ms poll
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(buttons_report_descriptor),
                       0x81, 16, 1),
};

// ─── TinyUSB Callbacks (required) ───────────────────────────────────────────

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return buttons_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize)
{
}

// clang-format on
