#include <stdio.h>

#define uchar unsigned char
#define uint unsigned int

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/task.h"

#include "font.h"
#include <string.h>

#define OLED_RST_PIN GPIO_NUM_1
#define OLED_CS_PIN GPIO_NUM_7
#define OLED_DC_PIN GPIO_NUM_0
#define OLED_SCLK_PIN GPIO_NUM_6
#define OLED_MOSI_PIN GPIO_NUM_5

#define USE_HORIZONTAL 0

// SCREEN SIZE - The ER-OLED2.45-1 physical resolution is 304x96
// The SH1126 GDDRAM is wider internally, so we need a column byte offset
// to address the visible area correctly.
#define COLUMNS 304
#define ROWS 96

// Column byte offset into GDDRAM where the visible 304-pixel area starts.
// The reference code uses base address 0x14 = 20 bytes (40 pixels offset).
// This centers the 304-pixel (152-byte) visible area within the GDDRAM.
#define COL_OFFSET 20

#define SH1126_BUFFER_SIZE (COLUMNS * ROWS / 2) // 14592 bytes (4-bit grayscale: 2 pixels per byte)

uint8_t framebuffer[SH1126_BUFFER_SIZE];

#define TAG "OLED_INIT"

// SPI device handle
spi_device_handle_t spi;

void SetCS(uint8_t level)
{
    gpio_set_level(OLED_CS_PIN, level);
}

void SetDC(uint8_t level)
{
    gpio_set_level(OLED_DC_PIN, level);
}

void SetRst(uint8_t level)
{
    gpio_set_level(OLED_RST_PIN, level);
}

void DelayMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void ResetDisplay(void)
{
}

// Function to send a command to the OLED
void WriteCommand(uint8_t cmd)
{
    SetDC(0); // Command mode
    SetCS(0); // Select OLED

    spi_transaction_t t = {
        .flags = 0,
        .length = 8, // Command is 8 bits
        .tx_buffer = &cmd,
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI transmit failed");
    }

    SetCS(1); // Deselect OLED
}

void WriteData(uint8_t data)
{
    SetDC(1);                       // Data mode
    gpio_set_level(OLED_CS_PIN, 0); // Select OLED

    spi_transaction_t t = {
        .flags = 0,
        .length = 8, // Data is 8 bits
        .tx_buffer = &data,
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI transmit failed");
    }

    SetCS(1); // Deselect OLED
}

void SetCol(uint8_t col)
{
    // Apply the GDDRAM column offset so we address the visible display area.
    // The SH1126 column address commands:
    //   0x10..0x1F = set higher nibble of column address
    //   0x00..0x0F = set lower nibble of column address
    // We add COL_OFFSET to translate from our framebuffer byte index (0..151)
    // to the actual GDDRAM byte column.
    uint8_t addr = col + COL_OFFSET;
    WriteCommand(0x10 | (addr >> 4));   // Set higher column address
    WriteCommand(0x00 | (addr & 0x0F)); // Set lower column address
}

void SetRow(uint8_t row)
{
    WriteCommand(0xB0);
    WriteCommand(row & 0x7F);
}

void ClearDisplay(void)
{
    for (uint8_t row = 0; row < ROWS; row++)
    {
        SetRow(row);
        SetCol(0);
        for (uint8_t col = 0; col < COLUMNS / 2; col++) // 152 bytes per row (2 pixels per byte)
        {
            WriteData(0x00); // Clear pixel data (black)
        }
    }
}

// Function to initialize the OLED
void InitDisplay(void)
{

    { // Reset the display
        SetRst(0);
        // the display require at least 10us reset pulse
        DelayMs(1);
        SetRst(1);
        DelayMs(1);
    }

    WriteCommand(0xAE); // Display off

    // SET ROW numbers - Multiplex Ratio
    WriteCommand(0xA8);
    WriteCommand(0x5F); // 96 rows

    // set position of first row to display in the display RAM
    WriteCommand(0x40);
    // set display start line to 0
    WriteCommand(0x00);

    WriteCommand(0xA0); // Set segment remap (if screen shows upside down, change to 0xA1)

    WriteCommand(0xC0); // Set COM output scan direction (if screen shows upside down, change to 0xC8)

    WriteCommand(81);   // Set contrast control (brightness)
    WriteCommand(0x80); // Contrast value (0-255)

    WriteCommand(0xD9); // Pre-charge/discharge
    WriteCommand(0x22); // Pre-charge period

    WriteCommand(0xDB); // VCOMH deselect level command
    WriteCommand(0x35); // VCOMH deselect level set value

    WriteCommand(0xd5); // Set display clock divide ratio/oscillator frequency
    WriteCommand(0x50); // Display clock divide ratio/oscillator frequency

    ClearDisplay();

    WriteCommand(0xAF); // Display on
}

// SPI and GPIO initialization
void OledSpiInit(void)
{
    // Configure GPIO pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OLED_RST_PIN) | (1ULL << OLED_CS_PIN) | (1ULL << OLED_DC_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // SPI bus configuration
    spi_bus_config_t buscfg = {
        .mosi_io_num = OLED_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = OLED_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // SPI device configuration
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
        .mode = 0,                          // SPI mode 0
        .spics_io_num = -1,                 // CS handled manually
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));
}

void SetPixel(uint16_t x, uint16_t y, uint8_t gray)
{
    if (x >= COLUMNS || y >= ROWS)
        return;
    if (gray > 15)
        gray = 15;

    // Compute byte index in framebuffer
    // Each row has COLUMNS/2 = 152 bytes, so offset = y * 152 + (x / 2)
    uint16_t byte_index = y * (COLUMNS / 2) + (x / 2);
    uint8_t current = framebuffer[byte_index];

    // SH1126 nibble ordering: HIGH nibble = left (even) pixel, LOW nibble = right (odd) pixel
    if (x & 1)
    {
        // Odd column -> lower nibble (right pixel)
        current = (current & 0xF0) | (gray);
    }
    else
    {
        // Even column -> higher nibble (left pixel)
        current = (current & 0x0F) | (gray << 4);
    }
    framebuffer[byte_index] = current;
}

void DrawChar(int16_t x, int16_t y, unsigned char c, uint8_t gray)
{
    if (c < 32 || c > 126)
        c = 32; // replace unsupported chars with space
    c -= 32;    // index in font array

    for (uint8_t row = 0; row < 8; row++)
    {
        uint8_t line = font6x8[c][row];
        for (uint8_t col = 0; col < 6; col++)
        {
            // font bits use the high bits; test bits 7..2 for 6 columns
            if (line & (0x80 >> col))
            { // 0x20 = bit5 (leftmost)
                SetPixel(x + col, y + row, gray);
            }
        }
    }
}

void DrawString(const char *str, uint8_t x, uint8_t y, uint8_t brightness)
{
    while (*str)
    {
        DrawChar(x, y, *str++, brightness);
        x += 6; // font6x8 is 6 pixels wide
    }
}

// Draw a character scaled to a specific target height (preserves font aspect)
// target_h: desired character height in pixels (e.g. 70)
void DrawCharLarge(int16_t x, int16_t y, unsigned char c, uint8_t gray, uint16_t target_h)
{
    if (c < 32 || c > 126)
        c = 32;
    c -= 32;

    // target width scaled proportionally to height (based on 6x8 source font)
    uint16_t target_w = (6 * target_h + 4) / 8; // rounded

    // For each source pixel in font (src_col, src_row) map to a block in target
    for (uint8_t src_row = 0; src_row < 8; src_row++)
    {
        // compute destination row range for this source row
        uint16_t dst_row_start = (src_row * target_h) / 8;
        uint16_t dst_row_end = ((src_row + 1) * target_h) / 8;
        if (dst_row_end > 0)
            dst_row_end -= 1;

        uint8_t line = font6x8[c][src_row];

        for (uint8_t src_col = 0; src_col < 6; src_col++)
        {
            // compute destination column range for this source column
            uint16_t dst_col_start = (src_col * target_w) / 6;
            uint16_t dst_col_end = ((src_col + 1) * target_w) / 6;
            if (dst_col_end > 0)
                dst_col_end -= 1;

            // check source pixel on/off (font6x8 uses high bits: bit7..bit2 for 6 columns)
            if (line & (0x80 >> src_col))
            {
                // fill mapped block
                for (uint16_t ry = dst_row_start; ry <= dst_row_end; ry++)
                {
                    for (uint16_t rx = dst_col_start; rx <= dst_col_end; rx++)
                    {
                        SetPixel(x + rx, y + ry, gray);
                    }
                }
            }
        }
    }
}

// Draw a string using the large scaled font. Advances by the computed target width + 1 pixel spacing.
void DrawStringLarge(const char *str, uint16_t x, uint16_t y, uint8_t brightness, uint16_t target_h)
{
    uint16_t target_w = (6 * target_h + 4) / 8; // match DrawCharLarge width calc
    while (*str)
    {
        DrawCharLarge(x, y, *str++, brightness, target_h);
        x += target_w + 1; // 1 pixel gap between chars
    }
}

void UpdateDisplay()
{
    for (uint8_t row = 0; row < ROWS; row++)
    {
        SetRow(row);
        SetCol(0);
        // Write the entire row (152 bytes = 304 pixels / 2)
        uint16_t row_offset = row * (COLUMNS / 2);
        for (uint16_t col = 0; col < COLUMNS / 2; col++)
        {
            WriteData(framebuffer[row_offset + col]);
        }
    }
}

// clang-format on

void app_main(void)
{
    OledSpiInit();

    InitDisplay();

    // Ensure framebuffer starts cleared
    memset(framebuffer, 0, sizeof(framebuffer));

    DrawString("Hello, World!", 20, 40, 15);

    UpdateDisplay();

    // int8_t color = 0;
    // int cols = COLUMNS;

    // while (1)
    // {
    //     ESP_LOGI(TAG, "cols %d, color: %d", (int)cols, (int)color);

    //     // FillScreen(color);
    //     vTaskDelay(100);
    //     color++;

    // if (color == 16)
    // {
    //     color = 0;
    // }

    // for (uint8_t row = 0; row < ROWS; row++)
    // {
    //     SetRow(row);
    //     SetCol(0);
    //     for (uint8_t col = 0; col < cols / 2; col++) // 2 pixels per byte
    //     {
    //         WriteData(color << 4 | color); // Clear pixel data
    //     }
    // }

    // for (uint8_t row = 0; row < 96; row++) // 96 rows
    // {
    //     SetRow(row); // Set the current row (your function)
    //     SetCol(0);   // Start at column 0 (left edge)

    //     for (uint8_t col_byte = 0; col_byte < 160; col_byte++) // 320 pixels = 160 bytes
    //     {
    //         uint16_t pixel_x = (uint16_t)col_byte * 2; // left pixel position (0,2,4,...,318)

    //         uint8_t gray_left = pixel_x / 20;        // 0..15
    //         uint8_t gray_right = (pixel_x + 1) / 20; // 0..15

    //         uint8_t data_byte = (gray_left << 4) | gray_right; // high nibble = left pixel, low nibble = right pixel

    //         WriteData(data_byte);
    //     }
    // }

    // for (uint8_t row = 0; row < ROWS; row++)
    // {
    //     SetRow(row);
    //     SetCol(0);
    //     for (uint8_t col = 0; col < cols / 2; col++) // 2 pixels per byte
    //     {
    //         if (row < 10 || row > 85 || col < 10 || col > 150)
    //         {
    //             color = 15;
    //         }
    //         else
    //         {
    //             color = 0;
    //         }
    //         WriteData(color << 4 | color); // Clear pixel data
    //     }
    // }

    // cols++;
    // }
}
