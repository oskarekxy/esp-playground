// GAP (Generic Access Profile) related definitions and functions

#pragma once

#include "host/ble_gap.h"             // GAP functions - ble_gap_adv_start(), ble_gap_conn_find()
#include "services/gap/ble_svc_gap.h" // GAP service - ble_svc_gap_init(), ble_svc_gap_device_name_set()

// Definitions //

#define BLE_GAP_APPEARANCE_TAG 0x0200 // generic tag - helps to show proper icon on central device
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

// Functions //

void adv_init(void);
int gap_init(void)