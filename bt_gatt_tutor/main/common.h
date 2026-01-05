#pragma once

// std libs //
#include <assert.h>
#include <stdio.h>
#include <string.h>

// esp libs //
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h" // menuconfig definitions

// FreeRTOS //
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// NimBLE //
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nibmle/ble.h"
#include "nimble/nimble_port.h"
#include "nibmle/nible_port_freertos.h"

// Definitions //
#define TAG "NimBLE_app"

#define DEVICE_NAME "ESP_NimBLE"