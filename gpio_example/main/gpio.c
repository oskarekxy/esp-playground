#include <stdint.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "hal/gpio_types.h"
#include "led_strip_types.h"
#include "soc/gpio_num.h"

//FreeRTOS - for delay//

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// LED //
#include "led_strip.h"

#define INPUT_PIN 2

void ConfigureGPIO()
{
  gpio_config_t input_gpio = 
    {
      .pin_bit_mask = (1ULL << INPUT_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,

    };

  gpio_config(&input_gpio);

  // gpio_set_direction(INPUT_PIN, GPIO_MODE_INPUT);
  // gpio_set_pull_mode(INPUT_PIN, GPIO_PULLUP_ONLY);

}


void ConfigureLed(led_strip_handle_t* led)
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
    .flags.with_dma = false
  };

  led_strip_new_rmt_device(&led_cfg, &rmt_cfg, led);


}

void WhiteLed(led_strip_handle_t* led , uint8_t light_level)
{

    led_strip_set_pixel(*led, 0, light_level, light_level, light_level);
    led_strip_refresh(*led);

}

void SetLedRGB(led_strip_handle_t* led , uint8_t rgb[3])
{

    led_strip_set_pixel(*led, 0, rgb[0], rgb[1], rgb[2]);
    led_strip_refresh(*led);

}



void app_main(void)
{
    //gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);
    
    ConfigureGPIO();

    led_strip_handle_t led;
    ConfigureLed(&led);
    
    WhiteLed(&led, 5U);

    while (1) {

      if(gpio_get_level(INPUT_PIN) == 0)
      {
        
      WhiteLed(&led, 2);
      }
      // vTaskDelay(pdMS_TO_TICKS(1000));
      else if(gpio_get_level(INPUT_PIN)== 1)
      {
        SetLedRGB(&led, (uint8_t[3]){5U,0U,0U});
      }  

      // vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}
