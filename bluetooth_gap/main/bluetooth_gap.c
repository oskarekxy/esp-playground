#include <stdio.h>

// FreeRTOS API
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// esp log and flash libs
#include "esp_log.h"
#include "nvs_flash.h"

// NimBLE
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

//GAP
#include "gap.h"

#define o_KEY 0x12 // o 

void ble_store_config_init(void);


static void on_stack_sync(void)
{
  adv_init();    
}

static void on_stack_reset(int reason) {
    /* On reset, print reset reason to console */
    ESP_LOGI("NimBLE", "nimble stack reset, reset reason: %d", reason);
}

static void nimble_host_config_init(void)
{
  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  // store host config
  ble_store_config_init();
}

static void nimble_host_task(void *param)
{
  ESP_LOGI("NimBLE", "NimBLE host task has been started");
  
  // This function does not return uintil nimble_port_stop() is called;
  nimble_port_run();

  // Clean task at exit 
  vTaskDelete(NULL);
}

void app_main(void)
{
    int rc;
    esp_err_t ret;
    
    // inint flash mem
    ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK((nvs_flash_erase()));
    }
    
    // init NimBLE stack
    ret = nimble_port_init();
    if(ret != ESP_OK)
    {
      ESP_LOGE("NimBLE", "Failed to initialize nimble stack, error code : %d", ret);
      return;
    }
    
    // init GAP service
    rc = gap_init();
    if (rc != 0)
    {
      ESP_LOGE("GAP", "failed to initialize GAP service, error code: %d", rc);
      return;
    }

    nimble_host_config_init();
    
    xTaskCreate(nimble_host_task, "NimBLE host", 4*1024,NULL,5,NULL);
    return;
}
