#include "driver/gpio.h"
#include "soc/gpio_num.h"
#include "hal/gpio_types.h"

#include "led_strip.h"
#include "led_strip_types.h"

#include "esp_log.h"

led_strip_handle_t led_strip_cfg;

void ConfigureLed()
{

    led_strip_config_t led_cfg =
        {
            .strip_gpio_num = GPIO_NUM_8,
            .max_leds = 1,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .led_model = LED_MODEL_WS2812,
            .flags.invert_out = false

        };

    led_strip_rmt_config_t rmt_cfg =
        {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            // .resolution_hz = LED_STRIP_RMT_RES_HZ,
            .resolution_hz = (10 * 1000 * 1000),
            .flags.with_dma = false,
        };

    ESP_LOGI("LED SETUP", "Before new rmt device");
    led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &led_strip_cfg);
    ESP_LOGI("LED SETUP", "After new rmt device");
}

void led_on()
{

    led_strip_set_pixel(led_strip_cfg, 0, 5U, 5U, 5U);
    led_strip_refresh(led_strip_cfg);
}

void led_off()
{

    led_strip_set_pixel(led_strip_cfg, 0, 0U, 0U, 0U);
    led_strip_refresh(led_strip_cfg);
}