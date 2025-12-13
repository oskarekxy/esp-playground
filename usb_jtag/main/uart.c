#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/usb_serial_jtag.h"

#include "esp_log.h"



#define BUF_SIZE 1024

void app_main(void)
{

    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
        .rx_buffer_size = BUF_SIZE,
        .tx_buffer_size = BUF_SIZE,
    };

    usb_serial_jtag_driver_install(&usb_serial_jtag_config);

    // uint8_t* data = (uint8_t*)"ABCD\n";

    // ESP_LOGI("usb_serial_jtag", "data size %d", strlen((char*)data));
    
    // // strcpy((char*)data, "abcd\n");
    
    uint8_t * data = (uint8_t*) malloc(BUF_SIZE);
    
    if (data == NULL) {
        // ESP_LOGE("usb_serial_jtag echo", "no memory for data");
        return;
    }


    while (true)
    {   
        // read data 
        int len = usb_serial_jtag_read_bytes(data, (BUF_SIZE-1), 10/portTICK_PERIOD_MS); // waits for data 10 ms

        // resent data 
        if(len > 0)
        {
            usb_serial_jtag_write_bytes((char*) data, len, 20 / portTICK_PERIOD_MS); // waits 20 ms for space in buffer if not enough space
        }

        // ESP_LOGI("usb_serial_jtag", "data size %d", sizeof(data));
        // usb_serial_jtag_write_bytes((char*) data, strlen((char*)data), 20 / portTICK_PERIOD_MS);
        // vTaskDelay(pdMS_TO_TICKS(1000)); // X ms delay
    }

}
