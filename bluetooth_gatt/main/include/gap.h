#include "stdio.h"
#include "esp_log.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include "led.h"

#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_APPEARANCE_HID_DEVICE 0x03C0
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

#define DEVICE_NAME "ESP32C6"

static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};
static uint8_t esp_uri[] = {BLE_GAP_URI_PREFIX_HTTPS, '/', '/', 'e', 's', 'p', 'r', 'e', 's', 's', 'i', 'f', '.', 'c', 'o', 'm'};

// functions declaration list
static void start_advertising(void);

/* Private functions */
inline static void format_addr(char *addr_str, uint8_t addr[])
{
  sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
          addr[2], addr[3], addr[4], addr[5]);
}

int gap_init(void)
{
  int rc = 0;

  // init gap service
  ble_svc_gap_init();

  // set gap device name
  rc = ble_svc_gap_device_name_set(DEVICE_NAME);
  if (rc != 0)
  {
    ESP_LOGE("GAP", "Failed to set device name to %s, error code: %d", DEVICE_NAME, rc);
    return rc;
  }

  rc = ble_svc_gap_device_appearance_set(BLE_GAP_APPEARANCE_HID_DEVICE);
  if (rc != 0)
  {
    ESP_LOGE("GAP", "Failed to set device appearance, error code: %d", rc);
    return rc;
  }

  return rc;
}

static void print_conn_desc(struct ble_gap_conn_desc *desc)
{
  /* Local variables */
  char addr_str[18] = {0};
  char TAG[] = "CONN DESC";

  /* Connection handle */
  ESP_LOGI(TAG, "connection handle: %d", desc->conn_handle);

  /* Local ID address */
  format_addr(addr_str, desc->our_id_addr.val);
  ESP_LOGI(TAG, "device id address: type=%d, value=%s",
           desc->our_id_addr.type, addr_str);

  /* Peer ID address */
  format_addr(addr_str, desc->peer_id_addr.val);
  ESP_LOGI(TAG, "peer id address: type=%d, value=%s", desc->peer_id_addr.type,
           addr_str);

  /* Connection info */
  ESP_LOGI(TAG,
           "conn_itvl=%d, conn_latency=%d, supervision_timeout=%d, "
           "encrypted=%d, authenticated=%d, bonded=%d\n",
           desc->conn_itvl, desc->conn_latency, desc->supervision_timeout,
           desc->sec_state.encrypted, desc->sec_state.authenticated,
           desc->sec_state.bonded);
}

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
  /* Local variables */
  int rc = 0;
  struct ble_gap_conn_desc desc;

  /* Handle different GAP event */
  switch (event->type)
  {

  /* Connect event */
  case BLE_GAP_EVENT_CONNECT:
    /* A new connection was established or a connection attempt failed. */
    ESP_LOGI("Event handler", "connection %s; status=%d",
             event->connect.status == 0 ? "established" : "failed",
             event->connect.status);

    /* Connection succeeded */
    if (event->connect.status == 0)
    {
      /* Check connection handle */
      rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
      if (rc != 0)
      {
        ESP_LOGE("GAP_HANDLER",
                 "failed to find connection by handle, error code: %d",
                 rc);
        return rc;
      }

      /* Print connection descriptor and turn on the LED */
      print_conn_desc(&desc);
      led_on();

      /* Try to update connection parameters */
      struct ble_gap_upd_params params = {.itvl_min = desc.conn_itvl,
                                          .itvl_max = desc.conn_itvl,
                                          .latency = 3,
                                          .supervision_timeout =
                                              desc.supervision_timeout};
      rc = ble_gap_update_params(event->connect.conn_handle, &params);
      if (rc != 0)
      {
        ESP_LOGE(
            "GAP_HANDLER",
            "failed to update connection parameters, error code: %d",
            rc);
        return rc;
      }
    }
    /* Connection failed, restart advertising */
    else
    {
      start_advertising();
    }
    return rc;

  /* Disconnect event */
  case BLE_GAP_EVENT_DISCONNECT:
    /* A connection was terminated, print connection descriptor */
    ESP_LOGI("GAP_HANDLER", "disconnected from peer; reason=%d",
             event->disconnect.reason);

    /* Turn off the LED */
    led_off();

    /* Restart advertising */
    start_advertising();
    return rc;

  /* Connection parameters update event */
  case BLE_GAP_EVENT_CONN_UPDATE:
    /* The central has updated the connection parameters. */
    ESP_LOGI("GAP_HANDLER", "connection updated; status=%d",
             event->conn_update.status);

    /* Print connection descriptor */
    rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
    if (rc != 0)
    {
      ESP_LOGE("GAP_HANDLER", "failed to find connection by handle, error code: %d",
               rc);
      return rc;
    }
    print_conn_desc(&desc);
    return rc;
  }

  return rc;
}

static void start_advertising(void)
{
  /* Local variables */
  int rc = 0;
  const char *name;
  struct ble_hs_adv_fields adv_fields = {0};
  struct ble_hs_adv_fields rsp_fields = {0};
  struct ble_gap_adv_params adv_params = {0};

  /* Set advertising flags */
  adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  /* Set device name */
  name = ble_svc_gap_device_name();
  adv_fields.name = (uint8_t *)name;
  adv_fields.name_len = strlen(name);
  adv_fields.name_is_complete = 1;

  /* Set device tx power */
  adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  adv_fields.tx_pwr_lvl_is_present = 1;

  /* Set device appearance */
  adv_fields.appearance = BLE_GAP_APPEARANCE_HID_DEVICE;
  adv_fields.appearance_is_present = 1;

  /* Set device LE role */
  adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
  adv_fields.le_role_is_present = 1;

  /* Set advertiement fields */
  rc = ble_gap_adv_set_fields(&adv_fields);
  if (rc != 0)
  {
    ESP_LOGE("BT", "failed to set advertising data, error code: %d", rc);
    return;
  }

  /* Set device address */
  rsp_fields.device_addr = addr_val;
  rsp_fields.device_addr_type = own_addr_type;
  rsp_fields.device_addr_is_present = 1;

  // Set advertising interval to 500 ms
  rsp_fields.adv_itvl = BLE_GAP_ADV_ITVL_MS(500);
  rsp_fields.adv_itvl_is_present = 1;

  /* Set URI */
  // rsp_fields.uri = esp_uri;
  // rsp_fields.uri_len = sizeof(esp_uri);

  /* Set scan response fields */
  rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
  if (rc != 0)
  {
    ESP_LOGE("BT", "failed to set scan response data, error code: %d", rc);
    return;
  }

  // Set undirected connetable mode and general discoverable mode
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  /* Set advertising interval */
  adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(500);
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(510);

  // Start advertising - when another device tries to connect call gap event handler
  rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                         gap_event_handler, NULL);
  if (rc != 0)
  {
    ESP_LOGE("BT", "failed to start advertising, error code: %d", rc);
    return;
  }
  ESP_LOGI("BT", "advertising started!");
}

// checks if bluetooth MAC addres is avilable if so starts advertising to near devices
void adv_init()
{
  int rc = 0;
  char addr_str[18] = {0};

  // check if proper bt identity address is set
  rc = ble_hs_util_ensure_addr(0);
  if (rc != 0)
  {
    ESP_LOGE("BT", "device does not have any available bt address!");
    return;
  }

  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0)
  {
    ESP_LOGE("BT", "Failed to infer address type, error code: %d", rc);
    return;
  }

  // get device address to addr_str
  rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
  if (rc != 0)
  {
    ESP_LOGE("BT", "Failed to copy device address, error code: %d", rc);
    return;
  }
  format_addr(addr_str, addr_val);
  ESP_LOGI("BT", "Device address: %s", addr_str);

  // start advertising
  start_advertising();
}
