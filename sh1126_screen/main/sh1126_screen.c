#include <stdio.h>

#define uchar unsigned char
#define uint unsigned int

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/task.h"

#define OLED_RST_PIN GPIO_NUM_1
#define OLED_CS_PIN GPIO_NUM_7
#define OLED_DC_PIN GPIO_NUM_0
#define OLED_SCLK_PIN GPIO_NUM_6
#define OLED_MOSI_PIN GPIO_NUM_5

#define USE_HORIZONTAL 0

// SCREEN SIZE
#define Max_Column 256
#define Max_Row 96

#define TAG "OLED_INIT"

// SPI device handle
spi_device_handle_t spi;

// Function to send a command to the OLED
void Write_Instruction(uint8_t cmd)
{
    gpio_set_level(OLED_DC_PIN, 0); // Command mode
    gpio_set_level(OLED_CS_PIN, 0); // Select OLED

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

    gpio_set_level(OLED_CS_PIN, 1); // Deselect OLED
}

void WriteData(uint8_t data)
{
    gpio_set_level(OLED_DC_PIN, 1); // Data mode
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

    gpio_set_level(OLED_CS_PIN, 1); // Deselect OLED
}

// Function to initialize the OLED
void Initial(void)
{
    // Reset the OLED
    gpio_set_level(OLED_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(OLED_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(OLED_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Send initialization commands
    Write_Instruction(0xAE); // Set Display Off

    Write_Instruction(0xD5); // Set Display Clock Divide Ratio/Oscillator Frequency
    Write_Instruction(0x50); // 50 125hz

    Write_Instruction(0xD9); // Set Discharge/Precharge Period
    Write_Instruction(0x2F); // 1F

    Write_Instruction(0x40); // Set Display Start Line 40
    Write_Instruction(0x00); // 30

    Write_Instruction(0xA4); // Set Entire Display OFF/ON

    Write_Instruction(0xA6); // Set Normal/Reverse Display

    Write_Instruction(0xA8); // Set Multiplex Ratio
    Write_Instruction(0x5F);

    Write_Instruction(0xAD); // DC-DC Setting
    Write_Instruction(0x80); // DC-DC is disable

    Write_Instruction(0xD3); // Set Display Offset
    Write_Instruction(0x00);

    Write_Instruction(0xDB); // Set VCOM Deselect Level
    Write_Instruction(0x24); // 0x30

    Write_Instruction(0xDC); // Set VSEGM Level
    Write_Instruction(0x05); // 0x30

    Write_Instruction(0x30); // Set Discharge VSL Level 1.5*VREF

    Write_Instruction(0x81); // The Contrast Control Mode Set
    Write_Instruction(0x40); // Contrast value

    if (USE_HORIZONTAL == 0)
    {
        Write_Instruction(0xA0); // Set Segment Re-map
        Write_Instruction(0xC0); // Set Common Output Scan Direction
    }
    else
    {
        Write_Instruction(0xA1); // Set Segment Re-map
        Write_Instruction(0xC8); // Set Common Output Scan Direction
    }
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

void FillScreen(uint8_t color)
{
    for (int i = 0; i < Max_Column * Max_Row; i++)
    {
        WriteData(color);
    }
}

// clang-format on

void app_main(void)
{
    OledSpiInit();

    Initial();

    int8_t color = 0;

    while (1)
    {
        ESP_LOGI(TAG, "color %d", (int)color);

        FillScreen(color);
        vTaskDelay(100);
        color++;

        if (color == 255)
        {
            color = 0;
        }
    }
}
