# LVGL + ESP-IDF + SH1126 OLED Display Tutorial

## Table of Contents
1. [Overview](#overview)
2. [How the SH1126 Display Works](#how-the-sh1126-display-works)
3. [Understanding GDDRAM and Addressing](#understanding-gddram-and-addressing)
4. [Display Orientation and Rotation](#display-orientation-and-rotation)
5. [Framebuffer Architecture](#framebuffer-architecture)
6. [Drawing Text and Images (Without LVGL)](#drawing-text-and-images-without-lvgl)
7. [Introduction to LVGL](#introduction-to-lvgl)
8. [Integrating LVGL with ESP-IDF](#integrating-lvgl-with-esp-idf)
9. [LVGL Display Driver for SH1126](#lvgl-display-driver-for-sh1126)
10. [Rendering Text with LVGL](#rendering-text-with-lvgl)
11. [Rendering Images with LVGL](#rendering-images-with-lvgl)
12. [Best Practices](#best-practices)
13. [Common Pitfalls](#common-pitfalls)

---

## Overview

This tutorial explains how OLED displays with the SH1126 driver IC work on ESP32
microcontrollers, and how to use LVGL (Light and Versatile Graphics Library) to
create UIs. It covers both the bare-metal approach (what this project uses) and
the LVGL approach for richer graphics.

**Hardware Setup:**
- MCU: ESP32-C6 (RISC-V, ESP-IDF 5.5.x)
- Display: ER-OLED2.45-1 (304x96 pixels, 4-bit grayscale, SH1126 IC)
- Interface: 4-wire SPI

---

## How the SH1126 Display Works

The SH1126 is an OLED driver IC that controls a matrix of OLED pixels. Key facts:

### Physical Specs
- **Visible resolution:** 304 columns x 96 rows
- **Color depth:** 4-bit grayscale (16 brightness levels per pixel, 0=off, 15=max)
- **Pixel packing:** 2 pixels per byte (high nibble = left pixel, low nibble = right pixel)

### Communication Protocol (4-wire SPI)
The display uses standard SPI with two extra control lines:
- **SCLK** - SPI clock
- **MOSI (SDA)** - SPI data out
- **CS** - Chip select (active LOW)
- **DC (RS)** - Data/Command select: LOW = command, HIGH = pixel data
- **RST** - Hardware reset (active LOW pulse)

### Command vs Data Mode
```
DC = 0 (LOW)  --> bytes sent are COMMANDS (configuration, addressing)
DC = 1 (HIGH) --> bytes sent are DATA (pixel values written to GDDRAM)
```

---

## Understanding GDDRAM and Addressing

The SH1126 has internal Graphics Display Data RAM (GDDRAM) that is **wider** than
the visible display area. This is a critical concept that causes confusion.

### Memory Layout
```
GDDRAM internal width: ~192 bytes (384 pixels)
Visible display:       152 bytes (304 pixels)
Column offset:         20 bytes (40 pixels from left edge of GDDRAM)

     |<--- 20 bytes --->|<------- 152 bytes ------->|<-- remaining -->|
     |    (invisible)    |    VISIBLE DISPLAY AREA    |   (invisible)   |
     |                   |  Column 0        Column 303|                 |
     ^                   ^                            ^
  GDDRAM col 0      GDDRAM col 20               GDDRAM col 171
```

### Row Addressing
Each "row" is a horizontal line across the full display width.
- Command `0xB0` followed by row number (0-95) sets which row to write
- After setting a row, column data is written sequentially left to right

### Column Addressing
Column address = byte position within the row (2 pixels per byte in 4-bit mode).
- High nibble command: `0x10 | (addr >> 4)`
- Low nibble command:  `0x00 | (addr & 0x0F)`
- **IMPORTANT:** You must add the offset (20) to reach the visible area!

### Example: Setting Position to Pixel (0, 0)
```c
// Set row 0
WriteCommand(0xB0);
WriteCommand(0x00);  // row = 0

// Set column byte 0 of VISIBLE area = GDDRAM byte 20 = 0x14
WriteCommand(0x10 | (0x14 >> 4));   // = 0x11 (high nibble)
WriteCommand(0x00 | (0x14 & 0x0F)); // = 0x04 (low nibble)
```

---

## Display Orientation and Rotation

The SH1126 supports hardware-level orientation flipping:

### Segment Remap (Left/Right flip)
- `0xA0` = Normal (column 0 maps to SEG0 - left side)
- `0xA1` = Flipped (column 0 maps to SEG303 - right side)

### COM Scan Direction (Up/Down flip)
- `0xC0` = Normal (row 0 at top)
- `0xC8` = Flipped (row 0 at bottom)

### Achieving 180-degree Rotation
```c
// Normal orientation:
WriteCommand(0xA0);
WriteCommand(0xC0);

// 180-degree rotation:
WriteCommand(0xA1);
WriteCommand(0xC8);
```

### Why "Vertical" Images Happen
If your image appears rotated 90 degrees (vertical instead of horizontal), the
causes are typically:
1. **Wrong column count** - writing more bytes per row than the display shows,
   causing pixel data to wrap into the next row
2. **Missing column offset** - writing to invisible GDDRAM area
3. **Swapped nibble order** - left/right pixels reversed within each byte
4. **Transposed X/Y** in your framebuffer indexing

---

## Framebuffer Architecture

A framebuffer is a memory buffer that holds the complete screen image. You draw
into the framebuffer, then flush it to the display in one operation.

### Why Use a Framebuffer?
- **Avoid flicker:** Update the whole screen atomically
- **Drawing primitives:** Easily set individual pixels without re-addressing
- **Compositing:** Layer text, shapes, and images before displaying

### Memory Layout for 304x96 @ 4-bit Grayscale
```c
#define COLUMNS 304
#define ROWS    96
#define BUFFER_SIZE (COLUMNS * ROWS / 2)  // = 14,592 bytes

uint8_t framebuffer[BUFFER_SIZE];
```

### Pixel-to-Byte Mapping
```
Byte index = row * (COLUMNS / 2) + (pixel_x / 2)

Within each byte:
  - HIGH nibble (bits 7-4) = left pixel (even X)
  - LOW nibble  (bits 3-0) = right pixel (odd X)

Example: pixel at (5, 10)
  byte_index = 10 * 152 + 2 = 1522
  Since X=5 is odd: gray value goes in LOW nibble
  framebuffer[1522] = (framebuffer[1522] & 0xF0) | gray_value;
```

---

## Drawing Text and Images (Without LVGL)

### Drawing Individual Pixels
```c
void SetPixel(uint16_t x, uint16_t y, uint8_t gray) {
    if (x >= COLUMNS || y >= ROWS) return;
    if (gray > 15) gray = 15;

    uint16_t byte_index = y * (COLUMNS / 2) + (x / 2);
    uint8_t current = framebuffer[byte_index];

    if (x & 1) {
        // Odd X -> LOW nibble
        current = (current & 0xF0) | gray;
    } else {
        // Even X -> HIGH nibble
        current = (current & 0x0F) | (gray << 4);
    }
    framebuffer[byte_index] = current;
}
```

### Drawing Text with Bitmap Fonts
Bitmap fonts store each character as a grid of on/off pixels:
```c
// 6x8 font: each character is 8 bytes, each byte is one row
// Bit 7 = leftmost pixel, bit 2 = rightmost (6 columns used)
void DrawChar(int16_t x, int16_t y, char c, uint8_t gray) {
    uint8_t idx = c - 32;  // ASCII offset
    for (uint8_t row = 0; row < 8; row++) {
        uint8_t line = font6x8[idx][row];
        for (uint8_t col = 0; col < 6; col++) {
            if (line & (0x80 >> col)) {
                SetPixel(x + col, y + row, gray);
            }
        }
    }
}
```

### Drawing Images (Bitmaps)
For grayscale images on SH1126, convert your image to 4-bit grayscale raw data:
```c
// Image stored as const array (2 pixels per byte, high nibble first)
const uint8_t my_image[] = { /* ... raw 4-bit grayscale data ... */ };

void DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
               const uint8_t *img) {
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint16_t img_byte = (row * w + col) / 2;
            uint8_t gray;
            if (col & 1) {
                gray = img[img_byte] & 0x0F;
            } else {
                gray = (img[img_byte] >> 4) & 0x0F;
            }
            SetPixel(x + col, y + row, gray);
        }
    }
}
```

### Flushing to Display
```c
void UpdateDisplay() {
    for (uint8_t row = 0; row < ROWS; row++) {
        SetRow(row);
        SetCol(0);  // This applies the COL_OFFSET internally
        uint16_t offset = row * (COLUMNS / 2);
        for (uint16_t col = 0; col < COLUMNS / 2; col++) {
            WriteData(framebuffer[offset + col]);
        }
    }
}
```

---


## Introduction to LVGL

LVGL (Light and Versatile Graphics Library) is an open-source embedded graphics
library that provides:
- Rich widgets (buttons, labels, sliders, charts, etc.)
- Anti-aliased font rendering
- Image decoding (PNG, BMP, etc.)
- Animations and transitions
- Touch input handling
- Theme/style system

### Why Use LVGL Instead of Raw Drawing?
| Feature | Raw Drawing | LVGL |
|---------|------------|------|
| Text rendering | Basic bitmap fonts | Anti-aliased, multi-size, Unicode |
| Images | Manual byte arrays | PNG/JPG decode, transforms |
| Layout | Manual positioning | Flexbox, grid |
| Animation | DIY timers | Built-in transitions |
| Memory | Minimal | ~32KB RAM minimum |
| Complexity | Low | Medium |

For a 304x96 4-bit grayscale display, LVGL is a good choice when you need:
- Multiple font sizes with smooth rendering
- Dynamic content updates
- Complex layouts

---

## Integrating LVGL with ESP-IDF

### Step 1: Add LVGL Component

Add to your project's `main/idf_component.yml`:
```yaml
dependencies:
  lvgl/lvgl: "~9.2"
```

Or for ESP-IDF component manager:
```yaml
dependencies:
  esp_lvgl_port: "^2"
  lvgl: "^9"
```

### Step 2: Configure LVGL (lv_conf.h)

Create `main/lv_conf.h` with key settings for your display:
```c
#ifndef LV_CONF_H
#define LV_CONF_H

// Color depth: 8 for grayscale (LVGL doesn't natively support 4-bit)
// We'll convert 8-bit to 4-bit in our flush callback
#define LV_COLOR_DEPTH 8

// Screen resolution
#define LV_HOR_RES_MAX 304
#define LV_VER_RES_MAX 96

// Memory: LVGL needs a draw buffer
// Minimum: 1/10 of screen = 304 * 10 = 3040 bytes (at 8-bit)
#define LV_MEM_SIZE (16 * 1024)  // 16KB for LVGL internal heap

// Enable features you need
#define LV_USE_LABEL 1
#define LV_USE_IMG   1
#define LV_USE_LINE  1

// Tick source
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((esp_timer_get_time() / 1000))

#endif
```

### Step 3: Initialize LVGL in Your Code

```c
#include "lvgl.h"
#include "esp_timer.h"

// Draw buffers (double-buffered for smooth rendering)
static lv_color_t buf1[304 * 10];  // 1/10 screen
static lv_color_t buf2[304 * 10];  // second buffer

void lvgl_init(void) {
    lv_init();

    // Create display
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 304 * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 304;
    disp_drv.ver_res = 96;
    disp_drv.flush_cb = sh1126_flush_cb;  // Our custom flush function
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}
```

---

## LVGL Display Driver for SH1126

The key integration point is the **flush callback** - this function converts
LVGL's internal pixel format to SH1126's 4-bit grayscale and sends it.

### Flush Callback Implementation

```c
// Convert LVGL 8-bit grayscale to SH1126 4-bit and write to display
void sh1126_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                     lv_color_t *color_map) {
    int32_t x, y;

    for (y = area->y1; y <= area->y2; y++) {
        // Set row
        SetRow(y);
        // Set starting column (byte position)
        uint8_t start_col_byte = area->x1 / 2;
        SetCol(start_col_byte);

        for (x = area->x1; x <= area->x2; x += 2) {
            // Get two adjacent pixels from LVGL buffer
            uint8_t gray_left = lv_color_brightness(*color_map++) >> 4;  // 8-bit to 4-bit
            uint8_t gray_right = 0;
            if (x + 1 <= area->x2) {
                gray_right = lv_color_brightness(*color_map++) >> 4;
            }
            // Pack into single byte: high nibble = left, low nibble = right
            WriteData((gray_left << 4) | gray_right);
        }
    }

    // Tell LVGL flush is done
    lv_disp_flush_ready(drv);
}
```

### Alternative: Framebuffer-Based Flush
For better performance, update the framebuffer and flush the dirty area:
```c
void sh1126_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                     lv_color_t *color_map) {
    // Write to framebuffer
    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            uint8_t gray = lv_color_brightness(*color_map++) >> 4;
            SetPixel(x, y, gray);
        }
    }

    // Flush only changed rows to display
    for (int32_t y = area->y1; y <= area->y2; y++) {
        SetRow(y);
        SetCol(0);
        uint16_t offset = y * (COLUMNS / 2);
        for (uint16_t col = 0; col < COLUMNS / 2; col++) {
            WriteData(framebuffer[offset + col]);
        }
    }

    lv_disp_flush_ready(drv);
}
```

### LVGL Task Loop
```c
void lvgl_task(void *pvParameters) {
    while (1) {
        lv_timer_handler();  // Process LVGL tasks
        vTaskDelay(pdMS_TO_TICKS(5));  // ~200 FPS max
    }
}

// Start LVGL processing in a FreeRTOS task
xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 5, NULL);
```

---

## Rendering Text with LVGL

### Basic Label
```c
void create_hello_label(void) {
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello, World!");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}
```

### Custom Fonts
LVGL provides a font converter tool. For 4-bit grayscale displays, use
anti-aliased fonts for the best look:

1. Use the [LVGL Font Converter](https://lvgl.io/tools/fontconverter)
2. Settings:
   - Bpp (bits per pixel): 4 (matches our grayscale depth!)
   - Size: Choose appropriate for 96px height (12-24px work well)
   - Range: 0x20-0x7F for ASCII
3. Download the .c file and include it in your project

```c
// Using a custom 16px font
LV_FONT_DECLARE(my_font_16);

lv_obj_t *label = lv_label_create(lv_scr_act());
lv_obj_set_style_text_font(label, &my_font_16, 0);
lv_label_set_text(label, "Custom Font!");
```

### Multi-line Text
```c
lv_obj_t *label = lv_label_create(lv_scr_act());
lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
lv_obj_set_width(label, 280);  // wrap width
lv_label_set_text(label, "Line 1\nLine 2\nLine 3");
lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 5);
```

### Scrolling Text (for long strings)
```c
lv_obj_t *label = lv_label_create(lv_scr_act());
lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
lv_obj_set_width(label, 200);
lv_label_set_text(label, "This is a very long text that scrolls...");
```

---

## Rendering Images with LVGL

### Converting Images for LVGL
Use the [LVGL Image Converter](https://lvgl.io/tools/imageconverter):
- Color format: Indexed 16 colors (for 4-bit grayscale)
- Or: Raw grayscale, then handle in flush
- Output: C array

### Displaying an Image
```c
// Declare the converted image
LV_IMG_DECLARE(my_logo);

void show_image(void) {
    lv_obj_t *img = lv_img_create(lv_scr_act());
    lv_img_set_src(img, &my_logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
}
```

### Raw Image Data (Without Converter)
For manually prepared 4-bit grayscale images:
```c
// Create image descriptor
static lv_img_dsc_t raw_img = {
    .header = {
        .cf = LV_IMG_CF_ALPHA_4BIT,  // 4-bit alpha channel
        .w = 64,
        .h = 64,
    },
    .data_size = 64 * 64 / 2,  // 4-bit = 2 pixels per byte
    .data = raw_image_data,    // your const uint8_t array
};
```

### Image Best Practices for Small Displays
1. **Keep images small** - on a 304x96 screen, every pixel counts
2. **Use 4-bit grayscale** - matches the hardware perfectly (no wasted bits)
3. **Pre-dither** - apply Floyd-Steinberg dithering before converting to 4-bit
4. **Consider RLE compression** for images with flat areas
5. **Store in flash** - use `const` to keep images in flash, not RAM

---


## Best Practices

### 1. SPI Performance Optimization
```c
// BAD: Sending one byte at a time (very slow!)
for (int i = 0; i < 152; i++) {
    WriteData(framebuffer[row_offset + i]);
}

// GOOD: Send entire row in one SPI transaction using DMA
void WriteDataBulk(const uint8_t *data, size_t len) {
    SetDC(1);
    SetCS(0);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_transmit(spi, &t);
    SetCS(1);
}

// Usage:
SetRow(row);
SetCol(0);
WriteDataBulk(&framebuffer[row * 152], 152);
```

### 2. Partial Display Updates
Only update the rows that changed:
```c
uint8_t dirty_rows[ROWS];  // Track which rows need updating

void MarkDirty(uint16_t y) {
    dirty_rows[y] = 1;
}

void FlushDirty(void) {
    for (uint8_t row = 0; row < ROWS; row++) {
        if (dirty_rows[row]) {
            SetRow(row);
            SetCol(0);
            WriteDataBulk(&framebuffer[row * (COLUMNS/2)], COLUMNS/2);
            dirty_rows[row] = 0;
        }
    }
}
```

### 3. Double Buffering
For flicker-free animation:
```c
uint8_t framebuffer_front[SH1126_BUFFER_SIZE];
uint8_t framebuffer_back[SH1126_BUFFER_SIZE];
uint8_t *draw_buffer = framebuffer_back;  // Draw here
uint8_t *display_buffer = framebuffer_front;  // Showing this

void SwapBuffers(void) {
    uint8_t *temp = draw_buffer;
    draw_buffer = display_buffer;
    display_buffer = temp;
    // Flush display_buffer to screen
}
```

### 4. Font Selection Guidelines
| Display Size | Recommended Font Size | Characters per Line |
|-------------|----------------------|-------------------|
| 304x96 | 8px (tiny info) | ~50 chars |
| 304x96 | 12px (readable) | ~25 chars |
| 304x96 | 16px (comfortable) | ~19 chars |
| 304x96 | 24px (headlines) | ~12 chars |
| 304x96 | 48px (large display) | ~6 chars |

### 5. Grayscale Anti-aliasing
Take advantage of 4-bit grayscale for smooth fonts:
- Full white pixel: brightness 15
- Anti-alias edge: brightness 7-10
- Background: brightness 0

### 6. Power Management
```c
// Put display to sleep when not needed
void DisplaySleep(void) {
    WriteCommand(0xAE);  // Display OFF
}

void DisplayWake(void) {
    WriteCommand(0xAF);  // Display ON
}

// Reduce brightness to save power
void SetBrightness(uint8_t level) {
    WriteCommand(0x81);   // Contrast command
    WriteCommand(level);  // 0-255
}
```

### 7. LVGL Memory Configuration for Constrained Devices
```c
// For ESP32-C6 with limited RAM:
// Use smaller draw buffers
static lv_color_t buf[304 * 5];  // Only 5 rows - saves memory

// Enable LVGL's built-in memory monitor
lv_mem_monitor_t mon;
lv_mem_monitor(&mon);
ESP_LOGI(TAG, "LVGL memory: used=%d%%, frag=%d%%",
         mon.used_pct, mon.frag_pct);
```

---

## Common Pitfalls

### 1. Image Appears Vertical / Rotated 90 degrees
**Cause:** Incorrect COLUMNS definition or missing column offset.
**Fix:** Ensure COLUMNS matches your physical display (304, not 320), and apply
the COL_OFFSET when addressing GDDRAM.

### 2. Image Shifted or Partially Visible
**Cause:** Missing GDDRAM column offset.
**Fix:** Add `COL_OFFSET = 20` to your column addressing:
```c
uint8_t addr = col + COL_OFFSET;
WriteCommand(0x10 | (addr >> 4));
WriteCommand(0x00 | (addr & 0x0F));
```

### 3. Left/Right Pixels Swapped (Mirrored Pairs)
**Cause:** Nibble order reversed in SetPixel.
**Fix:** HIGH nibble = even (left) pixel, LOW nibble = odd (right) pixel.

### 4. Screen Shows Garbage After Init
**Cause:** GDDRAM contains random data at power-on.
**Fix:** Always clear the display (write 0x00 to all positions) during init.

### 5. SPI Transmission Failures
**Cause:** Missing DMA configuration or buffer alignment issues.
**Fix:** Use `SPI_DMA_CH_AUTO` and ensure buffers are DMA-capable (not on stack).

### 6. LVGL Flickering
**Cause:** Single-buffered drawing with slow SPI updates.
**Fix:** Use double-buffered draw buffers and bulk SPI transfers.

### 7. Incorrect Font Rendering with LVGL
**Cause:** Color depth mismatch between LVGL config and display.
**Fix:** Set `LV_COLOR_DEPTH 8` and convert to 4-bit in flush callback.

### 8. Display Upside Down
**Cause:** Segment remap and COM scan direction settings.
**Fix:** Toggle between `0xA0`/`0xA1` and `0xC0`/`0xC8` in InitDisplay.

---

## Quick Reference: SH1126 Command Summary

| Command | Description | Default |
|---------|-------------|---------|
| `0xAE` | Display OFF | - |
| `0xAF` | Display ON | - |
| `0xA0` | Segment remap: normal | Yes |
| `0xA1` | Segment remap: flipped | - |
| `0xC0` | COM scan: normal (top to bottom) | Yes |
| `0xC8` | COM scan: flipped (bottom to top) | - |
| `0x81, val` | Set contrast (0x00-0xFF) | 0x80 |
| `0xA8, val` | Set multiplex ratio (rows - 1) | 0x5F (96) |
| `0xD5, val` | Set clock divide / osc freq | 0x50 |
| `0xB0, row` | Set row address (0-95) | - |
| `0x10\|hi, 0x00\|lo` | Set column address | - |
| `0x40, line` | Set display start line | 0x00 |
| `0xD9, val` | Set pre-charge period | - |
| `0xDB, val` | Set VCOMH deselect level | - |

---

## Project File Structure (Recommended)

```
sh1126_screen/
├── CMakeLists.txt              # Project-level CMake
├── sdkconfig                   # ESP-IDF menuconfig
├── main/
│   ├── CMakeLists.txt          # Component CMake
│   ├── idf_component.yml      # Dependencies (add lvgl here)
│   ├── sh1126_screen.c        # Display driver + main app
│   ├── font.h                 # Bitmap font data
│   ├── sh1126.h               # Display driver header (extract from .c)
│   └── images/                 # Converted image arrays
│       └── logo.c
└── TUTORIAL_LVGL_ESP_SH1126.md # This file
```

---

## Next Steps

1. **Extract display driver** into separate .h/.c files for reuse
2. **Add LVGL** via `idf_component.yml` and implement the flush callback
3. **Use DMA bulk transfers** for faster screen updates
4. **Create a proper LVGL theme** optimized for 4-bit grayscale
5. **Add input device** (buttons/encoder) for interactive UI

---

## Resources

- [LVGL Documentation](https://docs.lvgl.io/)
- [LVGL Font Converter](https://lvgl.io/tools/fontconverter)
- [LVGL Image Converter](https://lvgl.io/tools/imageconverter)
- [ESP-IDF SPI Master Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/spi_master.html)
- [ESP-IDF LVGL Port Component](https://components.espressif.com/components/espressif/esp_lvgl_port)
- SH1126 Datasheet (check your display vendor for the latest revision)
