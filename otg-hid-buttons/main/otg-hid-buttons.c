#include <stdio.h>

#include "esp_log.h"

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "wheel_hid_interface.hpp"
#include "tinyusb_default_config.h"

static const char *APP_NAME = "WHEEL";

#define GPIO_INPUT_GEAR_UP 14
#define GPIO_INPUT_GEAR_DOWN 13
#define GPIO_INPUT_PINS ((1ULL << GPIO_INPUT_GEAR_UP) | (1ULL << GPIO_INPUT_GEAR_DOWN))
#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue = NULL;

static void gpio_task(void *arg);
static void gpio_isr_handler(void *arg);
static void hid_report_task(void *arg);

static uint8_t hid_report_buttons = 0;
static SemaphoreHandle_t report_mutex = NULL;

void app_main(void)
{
    report_mutex = xSemaphoreCreateMutex();

    gpio_config_t io_config = {};

    io_config.intr_type = GPIO_INTR_ANYEDGE;
    io_config.pin_bit_mask = GPIO_INPUT_PINS;
    io_config.mode = GPIO_MODE_INPUT;
    io_config.pull_up_en = 1;

    gpio_config(&io_config);

    // Create a queue that holds up to 10 items, each item being a uint32_t
    // (we use it to pass GPIO numbers from the ISR to the task).
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    xTaskCreate(gpio_task, "gpio_task", 3072, NULL, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_INPUT_GEAR_UP, gpio_isr_handler, (void *)GPIO_INPUT_GEAR_UP);
    gpio_isr_handler_add(GPIO_INPUT_GEAR_DOWN, gpio_isr_handler, (void *)GPIO_INPUT_GEAR_DOWN);

    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .port = TINYUSB_PORT_FULL_SPEED_0,
        .phy = {
            .skip_setup = false,
            .self_powered = false,
            .vbus_monitor_io = -1,
        },
        .task = TINYUSB_TASK_DEFAULT(),
        .descriptor = {
            .device = NULL,
            .string = hid_string_descriptor,
            .string_count = 5,
            .full_speed_config = hid_configuration_descriptor,
        },
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(APP_NAME, "USB HID initialized — plug into PC");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    xTaskCreate(hid_report_task, "hid_report", 2048, NULL, 5, NULL);
}

static void hid_report_task(void *arg)
{
    uint8_t prev_report = 0;
    for (;;)
    {
        if (tud_hid_ready())
        {
            uint8_t report;

            xSemaphoreTake(report_mutex, portMAX_DELAY);
            report = hid_report_buttons;
            xSemaphoreGive(report_mutex);

            if (report != prev_report)
            {
                ESP_LOGI(APP_NAME, "HID report: 0x%02X", report);
                tud_hid_report(0, &report, sizeof(report));
                prev_report = report;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void gpio_task(void *arg)
{
    uint32_t io_num;
    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            vTaskDelay(pdMS_TO_TICKS(20)); // debounce

            int level = gpio_get_level(io_num);

            xSemaphoreTake(report_mutex, portMAX_DELAY);
            if (io_num == GPIO_INPUT_GEAR_UP)
                hid_report_buttons = (hid_report_buttons & ~0x01) | (!level);
            else if (io_num == GPIO_INPUT_GEAR_DOWN)
                hid_report_buttons = (hid_report_buttons & ~0x02) | ((!level) << 1);
            xSemaphoreGive(report_mutex);

            // Drain any queued bounce events
            while (xQueueReceive(gpio_evt_queue, &io_num, 0)) {}

            ESP_LOGI(APP_NAME, "GPIO[%" PRIu32 "] val: %d", io_num, level);
        }
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    // Recover the GPIO number we stashed in the "user data" pointer at registration time.
    uint32_t gpio_num = (uint32_t)arg;

    // Push the GPIO number into the queue. The "FromISR" variant is mandatory
    // inside an ISR. The third argument (NULL) is an optional output flag that
    // would tell us whether a higher-priority task got unblocked; we don't use it.
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}