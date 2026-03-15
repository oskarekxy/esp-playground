#include <stdio.h>
#include <string.h>

#define uchar unsigned char
#define uint unsigned int

// FreeRTOS must be included first before anything using TickType_t
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lvgl/demos/lv_demos.h"
#include "lvgl/examples/lv_examples.h"
#include "lvgl/lvgl.h"

#include "font.h"

#define OLED_RST_PIN GPIO_NUM_1
#define OLED_CS_PIN GPIO_NUM_7
#define OLED_DC_PIN GPIO_NUM_0
#define OLED_SCLK_PIN GPIO_NUM_6
#define OLED_MOSI_PIN GPIO_NUM_5

// SCREEN SIZE
#define COLUMNS 320
#define ROWS 96

#define SH1126_BUFFER_SIZE (COLUMNS * ROWS / 2) // 15360 bytes, 2 pixels per byte (4-bit grayscale)

#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_I1))

#define TAG "OLED"

// --- Globals ---
uint8_t framebuffer[SH1126_BUFFER_SIZE];
spi_device_handle_t spi;
static TickType_t time_0 = 0;

// --- Forward declarations ---
uint32_t my_get_millis(void);
void my_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map);

// -------------------------------------------------------------------------
// Timer
// -------------------------------------------------------------------------
void my_timer_init(void)
{
    time_0 = xTaskGetTickCount();
}

uint32_t my_get_millis(void)
{
    TickType_t now = xTaskGetTickCount();
    return (now - time_0) * portTICK_PERIOD_MS;
}

// -------------------------------------------------------------------------
// GPIO helpers
// -------------------------------------------------------------------------
void SetCS(uint8_t level) { gpio_set_level(OLED_CS_PIN, level); }
void SetDC(uint8_t level) { gpio_set_level(OLED_DC_PIN, level); }
void SetRst(uint8_t level) { gpio_set_level(OLED_RST_PIN, level); }
void DelayMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// -------------------------------------------------------------------------
// SPI helpers
// -------------------------------------------------------------------------
void WriteCommand(uint8_t cmd)
{
    SetDC(0); // Command mode
    SetCS(0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    if (spi_device_transmit(spi, &t) != ESP_OK)
        ESP_LOGE(TAG, "SPI command failed");
    SetCS(1);
}

// -------------------------------------------------------------------------
// Display addressing
// -------------------------------------------------------------------------
void SetCol(uint8_t col)
{
    // col is the byte address (2 pixels per byte), so col = pixel_x / 2
    WriteCommand(0x10 | (col >> 4));   // High nibble
    WriteCommand(0x00 | (col & 0x0F)); // Low nibble
}

void SetRow(uint8_t row)
{
    // Double-byte command per SH1126 datasheet section 14
    WriteCommand(0xB0);
    WriteCommand(row & 0x7F);
    // Note: leaves DC=0, caller must set DC=1 before sending data
}

// -------------------------------------------------------------------------
// Display update — batched SPI (one transaction per row, not per byte)
// -------------------------------------------------------------------------
void UpdateDisplayArea(int y1, int y2)
{
    for (int row = y1; row <= y2; row++)
    {
        SetRow(row); // sends commands, leaves DC=0
        SetCol(0);   // sends commands, leaves DC=0

        SetDC(1); // switch to data mode AFTER all commands for this row
        SetCS(0);
        spi_transaction_t t = {
            .length = (COLUMNS / 2) * 8, // 160 bytes * 8 bits
            .tx_buffer = &framebuffer[row * (COLUMNS / 2)],
        };
        spi_device_transmit(spi, &t);
        SetCS(1);
    }
}

void UpdateDisplay(void)
{
    UpdateDisplayArea(0, ROWS - 1);
}

// -------------------------------------------------------------------------
// Clear display RAM directly (used before LVGL takes over)
// -------------------------------------------------------------------------
void ClearDisplay(void)
{
    memset(framebuffer, 0x00, SH1126_BUFFER_SIZE); // 0x00 = all pixels black
    UpdateDisplay();
}

// -------------------------------------------------------------------------
// Pixel / drawing helpers
// -------------------------------------------------------------------------
void SetPixel(uint16_t x, uint16_t y, uint8_t gray)
{
    if (x >= COLUMNS || y >= ROWS)
        return;
    if (gray > 15)
        gray = 15;

    uint16_t byte_index = y * (COLUMNS / 2) + (x / 2);
    uint8_t current = framebuffer[byte_index];

    if (x & 1)
        current = (current & 0x0F) | (gray << 4); // odd pixel -> high nibble
    else
        current = (current & 0xF0) | (gray); // even pixel -> low nibble

    framebuffer[byte_index] = current;
}

void DrawChar(int16_t x, int16_t y, unsigned char c, uint8_t gray)
{
    if (c < 32 || c > 126)
        c = 32;
    c -= 32;
    for (uint8_t row = 0; row < 8; row++)
    {
        uint8_t line = font6x8[c][row];
        for (uint8_t col = 0; col < 6; col++)
        {
            if (line & (0x80 >> col))
                SetPixel(x + col, y + row, gray);
        }
    }
}

void DrawString(const char *str, uint8_t x, uint8_t y, uint8_t brightness)
{
    while (*str)
    {
        DrawChar(x, y, *str++, brightness);
        x += CHAR_WIDTH;
    }
}

// -------------------------------------------------------------------------
// Display initialisation
// -------------------------------------------------------------------------
void InitDisplay(void)
{
    SetRst(0);
    DelayMs(1); // >= 10us reset pulse
    SetRst(1);
    DelayMs(1);

    WriteCommand(0xAE); // Display off

    WriteCommand(0xA8); // Set multiplex ratio
    WriteCommand(0x5F); // 96 rows (0x5F = 95)

    WriteCommand(0x40); // Set display start line
    WriteCommand(0x00); // Start line = 0

    WriteCommand(0xA0); // Segment remap (flip horizontally: 0xA1)
    WriteCommand(0xC0); // COM scan direction  (flip vertically:  0xC8)

    WriteCommand(0x51); // Set contrast (brightness)
    WriteCommand(0x80); // Contrast value 128/255

    WriteCommand(0xD9); // Pre-charge period
    WriteCommand(0x22);

    WriteCommand(0xDB); // VCOMH deselect level
    WriteCommand(0x35);

    WriteCommand(0xD5); // Clock divide ratio / oscillator frequency
    WriteCommand(0x50);

    ClearDisplay();

    WriteCommand(0xAF); // Display on
}

// -------------------------------------------------------------------------
// SPI + GPIO initialisation
// -------------------------------------------------------------------------
void OledSpiInit(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OLED_RST_PIN) | (1ULL << OLED_CS_PIN) | (1ULL << OLED_DC_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    spi_bus_config_t buscfg = {
        .mosi_io_num = OLED_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = OLED_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = COLUMNS / 2, // one row at a time
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
        .mode = 0,
        .spics_io_num = -1, // CS handled manually
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));
}

// -------------------------------------------------------------------------
// LVGL flush callback
// -------------------------------------------------------------------------
void my_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    px_map += 8;
    uint32_t width = (area->x2 - area->x1 + 1);

    for (int y = area->y1; y <= area->y2; y++)
    {
        uint32_t row_offset = (y - area->y1) * width;
        for (int x = area->x1; x <= area->x2; x++)
        {
            uint32_t index = row_offset + (x - area->x1);
            uint32_t byte_index = index / 8;
            uint32_t bit_index = index % 8;
            uint8_t color = (px_map[byte_index] >> (7 - bit_index)) & 0x01;
            SetPixel(x, y, color ? 0x0 : 0xF);
        }
    }

    // Only send to physical display when ALL areas for this frame are done
    if (lv_display_flush_is_last(display))
    {
        UpdateDisplay();
    }

    lv_display_flush_ready(display);
}

// -------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------
void app_main(void)
{
    OledSpiInit();
    InitDisplay();

    // Init LVGL before creating any display/widget objects
    lv_init();

    my_timer_init();
    lv_tick_set_cb(my_get_millis);

    lv_display_t *display1 = lv_display_create(ROWS, COLUMNS);
    lv_display_set_color_format(display1, LV_COLOR_FORMAT_I1);

    static uint8_t buf1[ROWS * COLUMNS / 10 * BYTES_PER_PIXEL];
    lv_display_set_buffers(display1, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display1, my_flush_cb);

    lv_demo_benchmark();

    while (1)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}