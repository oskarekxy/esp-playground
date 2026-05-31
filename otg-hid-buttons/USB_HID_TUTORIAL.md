# USB HID: How It Actually Works

A practical guide to understanding USB HID and building a 2-button gamepad on ESP32-S3 with TinyUSB.

---

## 1. The Big Picture: What Happens When You Plug In a USB Device

USB is **host-driven**. Your PC (the host) controls everything. The device (your ESP32-S3) can never speak unless the host asks it to. Here's the full lifecycle:

### Enumeration (plug-in)

```
1. You plug in the ESP32-S3
2. Host detects it and resets the bus
3. Host asks: "What are you?" → Device sends Device Descriptor (VID, PID, class)
4. Host asks: "What can you do?" → Device sends Configuration Descriptor (interfaces, endpoints)
5. Host asks: "How is your data formatted?" → Device sends HID Report Descriptor
6. Host loads its built-in HID driver — no custom driver needed
7. Device is ready
```

### Runtime (after enumeration)

This is the part most tutorials skip. Here's what actually happens on the wire:

```
Every 10ms (configurable via bInterval in endpoint descriptor):

    Host sends an IN token to the device's interrupt endpoint
         │
         ▼
    ┌─────────────────────────────────────────┐
    │ Does the device have new data to send?  │
    │                                         │
    │  YES → Device sends a DATA packet       │
    │         (your report bytes)             │
    │         Host sends ACK                  │
    │                                         │
    │  NO  → Device sends NAK                 │
    │         (nothing to report right now)   │
    │         Host tries again next interval  │
    └─────────────────────────────────────────┘
```

**Key insight**: The host polls your device at a fixed rate. Your device doesn't "push" data — it just has data ready (or not) when the host asks. TinyUSB handles the NAK/DATA response automatically. When you call `tud_hid_report()`, you're putting data into a buffer that TinyUSB will send the next time the host polls.

---

## 2. HID Reports: The Actual Data You Send

A "report" is just a **raw byte array**. Nothing fancy. For your 2-button device, the report is literally **1 byte**:

```
Byte 0: [0][0][0][0][0][0][btn2][btn1]
         ─────padding─────  ──buttons──
```

- Button 1 pressed → you send `0x01` (binary: 00000001)
- Button 2 pressed → you send `0x02` (binary: 00000010)
- Both pressed → you send `0x03` (binary: 00000011)
- Nothing pressed → you send `0x00`

That's it. That's the entire data transfer. One byte.

But the host needs to know **how to interpret** that byte. That's what the Report Descriptor is for.

---

## 3. The Report Descriptor: Teaching the Host Your Data Format

The Report Descriptor is a binary blob you send during enumeration. It tells the host:
- "I'm a gamepad"
- "I have 2 buttons"
- "Each button is 1 bit, value 0 or 1"
- "The remaining 6 bits are padding, ignore them"

### How the encoding works

Each item in the descriptor is 1 prefix byte + 0-2 data bytes. The prefix byte encodes:

```
Prefix byte: [tag (4 bits)][type (2 bits)][size (2 bits)]

  size: 00 = 0 data bytes follow
        01 = 1 data byte follows
        10 = 2 data bytes follow

  type: 00 = Main item (Input, Output, Collection, End Collection)
        01 = Global item (Usage Page, Logical Min/Max, Report Size/Count)
        10 = Local item (Usage, Usage Min/Max)
```

### The three item types and how they interact

Think of it like filling out a form:

1. **Global items** set the context. They persist until you change them.
   - "I'm talking about buttons" (Usage Page)
   - "Values range from 0 to 1" (Logical Min/Max)
   - "Each field is 1 bit" (Report Size)
   - "There are 2 fields" (Report Count)

2. **Local items** specify details for the next Main item only. They reset after each Main item.
   - "Specifically buttons 1 and 2" (Usage Min/Max)

3. **Main items** commit a data field using the current Global+Local state.
   - "OK, that's an Input field" → this creates the actual bits in the report

### Your 2-button descriptor, decoded byte by byte

```c
const uint8_t hid_report_descriptor[] = {
    // "I'm describing a Generic Desktop device, specifically a Gamepad"
    0x05, 0x01,     // Usage Page (Generic Desktop)     — prefix 0x05 = Global, 1 byte follows
    0x09, 0x05,     // Usage (Gamepad)                  — prefix 0x09 = Local, 1 byte follows
    0xA1, 0x01,     // Collection (Application)         — prefix 0xA1 = Main, groups everything

    // "Now I'm describing buttons"
    0x05, 0x09,     //   Usage Page (Button)            — switch context to buttons
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
```

**Total: 2 bits (buttons) + 6 bits (padding) = 8 bits = 1 byte per report.**

The rule is: `Report Size × Report Count` = number of bits for that Input item. The total of all Input items must be a multiple of 8 (byte-aligned).

### Common hex prefixes cheat sheet

```
Global:                    Local:                     Main:
0x05 = Usage Page          0x09 = Usage               0x81 = Input
0x15 = Logical Minimum     0x19 = Usage Minimum       0x91 = Output
0x25 = Logical Maximum     0x29 = Usage Maximum       0xA1 = Collection
0x75 = Report Size                                    0xC0 = End Collection
0x95 = Report Count
```

### Input item flags (the byte after 0x81)

```
0x02 = Data, Variable, Absolute  — use for buttons, joystick positions
0x06 = Data, Variable, Relative  — use for mouse movement
0x01 = Constant                  — use for padding bits
```

---

## 4. How TinyUSB Works on ESP32-S3

TinyUSB is the USB stack. The `esp_tinyusb` component wraps it for ESP-IDF. Here's the actual flow:

### What you provide (callbacks TinyUSB calls):

```c
// Called during enumeration — return your report descriptor
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    return hid_report_descriptor;
}

// Called if host requests current state (GET_REPORT) — rarely used
uint16_t tud_hid_get_report_cb(...) { return 0; }

// Called if host sends data to device (SET_REPORT) — for LEDs etc, not needed here
void tud_hid_set_report_cb(...) { }
```

### What you call (sending data):

```c
// Check if host has mounted the device and endpoint is ready
if (tud_mounted() && tud_hid_ready()) {
    uint8_t report = 0x00;  // your 1-byte report
    if (button1_pressed) report |= (1 << 0);
    if (button2_pressed) report |= (1 << 1);

    tud_hid_report(0, &report, sizeof(report));
    //              ^report_id=0 means "no report ID" (single report type)
}
```

### The runtime loop:

```
┌──────────────────────────────────────────────────────────┐
│  app_main()                                              │
│                                                          │
│  1. Configure GPIOs (your buttons)                       │
│  2. Call tinyusb_driver_install() — starts USB stack     │
│  3. Loop forever:                                        │
│       - Read button states from GPIO                     │
│       - If state changed AND tud_hid_ready():            │
│           call tud_hid_report() with new state           │
│       - vTaskDelay(10ms)                                 │
│                                                          │
│  Meanwhile, TinyUSB's internal task handles:             │
│    - Responding to host enumeration requests             │
│    - Sending your report data when host polls            │
│    - Calling your callbacks as needed                    │
└──────────────────────────────────────────────────────────┘
```

---

## 5. Complete Implementation for Your Project

### File: `main/idf_component.yml`

```yaml
dependencies:
  espressif/esp_tinyusb: "^1.0"
  idf: ">=5.0"
```

### File: `main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "otg-hid-buttons.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio
)
```

### File: `main/otg-hid-buttons.c`

```c
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"

static const char *TAG = "hid-buttons";

#define BTN_GEAR_UP   14
#define BTN_GEAR_DOWN 13

// ─── HID Report Descriptor ─────────────────────────────────────────────────
// Tells the host: "I'm a gamepad with 2 buttons, sending 1 byte per report"
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x02,        //   Usage Maximum (Button 2)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x75, 0x06,        //   Report Size (6 bits)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Constant) — padding
    0xC0               // End Collection
};

// ─── USB Descriptors ────────────────────────────────────────────────────────
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},   // 0: Language (English)
    "DIY",                  // 1: Manufacturer
    "Gear Buttons",         // 2: Product
    "000001",               // 3: Serial number
    "HID Interface",        // 4: HID interface name
};

static const uint8_t hid_configuration_descriptor[] = {
    // Config descriptor: 1 config, 1 interface, remote wakeup, 100mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // HID descriptor: interface 0, string idx 4, no boot protocol,
    //   report desc size, endpoint 0x81 (IN), 16 byte buffer, 10ms poll
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor),
                       0x81, 16, 10),
};

// ─── TinyUSB Callbacks (required) ───────────────────────────────────────────

// Called during enumeration: "give me your report descriptor"
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

// Called if host does GET_REPORT (rare for gamepads)
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

// Called if host does SET_REPORT (e.g. keyboard LEDs — not needed here)
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize)
{
}

// ─── Application ────────────────────────────────────────────────────────────

void app_main(void)
{
    // Configure buttons: input, pull-up enabled, active-low
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_GEAR_UP) | (1ULL << BTN_GEAR_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,  // use default from menuconfig
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = 5,
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB HID initialized — plug into PC");

    // Main loop: read buttons, send report when state changes
    uint8_t prev_report = 0;
    while (1) {
        if (tud_mounted() && tud_hid_ready()) {
            uint8_t report = 0;
            // Buttons are active-low: pressed = GPIO reads 0
            if (!gpio_get_level(BTN_GEAR_UP))   report |= (1 << 0);  // bit 0
            if (!gpio_get_level(BTN_GEAR_DOWN)) report |= (1 << 1);  // bit 1

            if (report != prev_report) {
                tud_hid_report(0, &report, sizeof(report));
                prev_report = report;
                ESP_LOGI(TAG, "Report sent: 0x%02X", report);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## 6. Understanding the Configuration Descriptor Macros

The `TUD_HID_DESCRIPTOR` macro generates the raw bytes that tell the host about your HID interface:

```c
TUD_HID_DESCRIPTOR(itf, str_idx, boot_protocol, report_desc_len, ep_in, ep_size, poll_ms)
//                  │      │         │                │              │       │        │
//                  │      │         │                │              │       │        └─ Host polls every 10ms
//                  │      │         │                │              │       └─ Max packet: 16 bytes
//                  │      │         │                │              └─ Endpoint 1, IN direction (0x81)
//                  │      │         │                └─ Size of your report descriptor array
//                  │      │         └─ false = not a boot keyboard/mouse
//                  │      └─ String descriptor index for this interface
//                  └─ Interface number (0 = first)
```

The endpoint address `0x81` means:
- Bit 7 = 1 → IN direction (device to host)
- Bits 0-3 = 1 → endpoint number 1

---

## 7. Testing on Linux

```bash
# After flashing and plugging in:

# Check if device enumerated
lsusb | grep -i "gear\|hid"

# See kernel messages
dmesg | tail -20

# Find your input device
ls /dev/input/js*

# Test with jstest (install: sudo apt install joystick)
jstest /dev/input/js0

# Or read raw HID reports
sudo cat /dev/hidraw0 | xxd

# Or use evtest for detailed event monitoring
sudo evtest
# Select your device from the list
```

---

## 8. What to Do If It Doesn't Work

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Device not recognized at all | Descriptor error — total bits not byte-aligned | Check Report Size × Report Count adds up to multiple of 8 |
| Enumerates but no input events | `tud_mounted()` never true, or report not sent | Add logging, check USB cable supports data (not charge-only) |
| Shows as "Unknown device" | Missing/wrong string descriptors | Verify `string_descriptor_count` matches array size |
| Buttons mapped wrong | Usage Min/Max don't match your bit layout | Button 1 = bit 0, Button 2 = bit 1, etc. |
| Works on Linux, not Windows | Windows is stricter about descriptors | Ensure Collection(Application) wraps everything |

---

## 9. Adapting to Other Use Cases

### If you want keyboard keys instead of gamepad buttons:

Change the descriptor's device type and use keyboard keycodes:

```c
// Change these two lines:
0x09, 0x06,        // Usage (Keyboard) instead of Gamepad
// And use Usage Page 0x07 (Keyboard) with specific key usages
```

For a keyboard, the report format is different (modifier byte + reserved + 6 keycodes). Use `tud_hid_keyboard_report()` helper instead.

### If you want media keys (volume, play/pause):

```c
0x05, 0x0C,        // Usage Page (Consumer)
0x09, 0x01,        // Usage (Consumer Control)
// Then use specific consumer usages like:
// 0xE9 = Volume Up, 0xEA = Volume Down, 0xCD = Play/Pause
```

### If you want more buttons:

Just change `Usage Maximum` and `Report Count`, and adjust padding:
```c
0x29, 0x08,     // Usage Maximum (Button 8)  — now 8 buttons
0x95, 0x08,     // Report Count (8)          — 8 bits = 1 full byte, no padding needed!
```

---

## 10. Key Concepts Summary

| Concept | What it means for you |
|---------|----------------------|
| Host polls device | You don't push data. You put data in a buffer, host picks it up every 10ms |
| Report = raw bytes | Your report is just `uint8_t report = 0x03;` — that's buttons 1+2 pressed |
| Report Descriptor = format spec | Binary blob sent once at plug-in, tells host how to parse your bytes |
| No custom drivers | OS reads your descriptor and handles everything — gamepad just works |
| `tud_hid_report()` | Queues your data for the next host poll |
| `tud_mounted()` | True after host finishes enumeration |
| `tud_hid_ready()` | True when endpoint is free to accept new data |

---

## References

- [USB HID Spec 1.11](https://www.usb.org/document-library/device-class-definition-hid-111)
- [HID Usage Tables](https://www.usb.org/document-library/hid-usage-tables-15)
- [Linux kernel: HID report descriptor intro](https://docs.kernel.org/hid/hidintro.html)
- [ESP-IDF TinyUSB HID example](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device/tusb_hid)
- [esp_tinyusb component](https://components.espressif.com/components/espressif/esp_tinyusb)
- [Online descriptor parser](https://eleccelerator.com/usbdescreqparser/)
