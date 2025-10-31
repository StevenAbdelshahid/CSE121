#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_defs.h"
#include "esp_hid_gap.h"
#include "esp_hidd.h"

static const char *TAG = "ESP_HIDD";

#define HIDD_LE_SERVICE_UUID    0x1812
#define HIDD_LE_CHAR_REPORT_UUID              0x2A4D
#define HIDD_APP_ID     0x1812

static uint16_t hid_service_handle;
static uint16_t hid_char_handle_report;
static esp_gatt_if_t gatt_if = 0;
static uint16_t conn_id = 0;
static bool is_connected = false;
static esp_hid_device_config_t *device_config = NULL;
static esp_hidd_event_cb_t event_callback = NULL;

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void hidd_event_dispatch(esp_hidd_event_t event)
{
    if (event_callback) {
        esp_hidd_event_data_t data = {0};
        event_callback(NULL, NULL, event, &data);
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "ADV data set complete, starting advertising");
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising start failed, status: %d", param->adv_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "Advertising started successfully");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising stopped");
        break;
    default:
        break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATT server registered");
        gatt_if = gatts_if;
        
        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id.inst_id = 0,
            .id.uuid.len = ESP_UUID_LEN_16,
            .id.uuid.uuid.uuid16 = HIDD_LE_SERVICE_UUID,
        };
        esp_ble_gatts_create_service(gatts_if, &service_id, 10);
        break;
        
    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG, "Service created");
        hid_service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(hid_service_handle);
        
        esp_bt_uuid_t char_uuid;
        char_uuid.len = ESP_UUID_LEN_16;
        char_uuid.uuid.uuid16 = HIDD_LE_CHAR_REPORT_UUID;
        
        esp_ble_gatts_add_char(hid_service_handle, &char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                               NULL, NULL);
        
        hidd_event_dispatch(ESP_HIDD_START_EVENT);
        break;
        
    case ESP_GATTS_ADD_CHAR_EVT:
        hid_char_handle_report = param->add_char.attr_handle;
        break;
        
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "Device connected");
        conn_id = param->connect.conn_id;
        is_connected = true;
        
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;
        conn_params.min_int = 0x10;
        conn_params.timeout = 400;
        esp_ble_gap_update_conn_params(&conn_params);
        
        hidd_event_dispatch(ESP_HIDD_CONNECT_EVENT);
        break;
        
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Device disconnected");
        is_connected = false;
        conn_id = 0;
        hidd_event_dispatch(ESP_HIDD_DISCONNECT_EVENT);
        break;
        
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.app_id == HIDD_APP_ID) {
            gatts_profile_event_handler(event, gatts_if, param);
        }
    } else {
        if (gatts_if == gatt_if || gatts_if == ESP_GATT_IF_NONE) {
            gatts_profile_event_handler(event, gatts_if, param);
        }
    }
}

esp_err_t esp_hid_gap_init(uint8_t mode)
{
    esp_err_t ret;
    
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "GAP register failed");
        return ret;
    }
    
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "GATTS register failed");
        return ret;
    }
    
    ret = esp_ble_gatts_app_register(HIDD_APP_ID);
    if (ret) {
        ESP_LOGE(TAG, "GATTS app register failed");
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t esp_hid_ble_gap_adv_start(void)
{
    esp_err_t ret;
    
    // HID service UUID
    static uint8_t adv_service_uuid128[16] = {
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
    };
    
    // Configure advertising data
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = 0x0006,
        .max_interval = 0x0010,
        .appearance = 0x03C2,  // HID Mouse
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(adv_service_uuid128),
        .p_service_uuid = adv_service_uuid128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };
    
    ret = esp_ble_gap_config_adv_data(&adv_data);
    if (ret) {
        ESP_LOGE(TAG, "Config adv data failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Advertising data configured, waiting for callback");
    return ESP_OK;
}

esp_err_t esp_ble_hidd_dev_init(esp_hid_device_config_t *config,
                                 esp_hid_transport_t transport,
                                 esp_hidd_event_cb_t callback,
                                 esp_hidd_dev_t **dev)
{
    device_config = config;
    event_callback = callback;
    *dev = (esp_hidd_dev_t *)1;
    return ESP_OK;
}

esp_err_t esp_ble_hidd_register_event_handler(esp_hidd_event_cb_t callback, void *arg)
{
    event_callback = callback;
    return ESP_OK;
}

esp_err_t esp_hidd_dev_input_set(esp_hidd_dev_t *dev, size_t map_index,
                                  size_t report_id, uint8_t *data, size_t length)
{
    if (!is_connected || gatt_if == 0) {
        return ESP_FAIL;
    }
    
    return esp_ble_gatts_send_indicate(gatt_if, conn_id, hid_char_handle_report,
                                       length, data, false);
}

esp_err_t esp_hidd_dev_deinit(esp_hidd_dev_t *dev)
{
    return ESP_OK;
}