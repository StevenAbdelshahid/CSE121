#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_hidd_prf_api.h"
#include "hid_dev.h"

static const char *TAG = "HID_MOUSE";

static uint16_t hid_conn_id = 0;
static bool connected = false;

#define HIDD_DEVICE_NAME "ESP32 Mouse"

static uint8_t hidd_service_uuid128[] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t hidd_adv_data = {
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
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void send_mouse_move(int8_t x, int8_t y)
{
    if (connected) {
        // Send mouse movement using HID profile API
        esp_hidd_send_mouse_value(hid_conn_id, 0, x, y);
    }
}

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
    case ESP_HIDD_EVENT_REG_FINISH: {
        if (param->init_finish.state == ESP_HIDD_INIT_OK) {
            esp_ble_gap_set_device_name(HIDD_DEVICE_NAME);
            esp_ble_gap_config_adv_data(&hidd_adv_data);
        }
        break;
    }
    case ESP_HIDD_EVENT_BLE_CONNECT: {
        ESP_LOGI(TAG, "ESP_HIDD_EVENT_BLE_CONNECT");
        hid_conn_id = param->connect.conn_id;
        connected = true;
        break;
    }
    case ESP_HIDD_EVENT_BLE_DISCONNECT: {
        connected = false;
        ESP_LOGI(TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT");
        esp_ble_gap_start_advertising(&hidd_adv_params);
        break;
    }
    default:
        break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&hidd_adv_params);
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        break;
    default:
        break;
    }
}

static void mouse_move_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Waiting for connection...");

    // Wait for connection
    while (!connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Connected! Starting mouse movement...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Loop the movement continuously for testing
    int cycle = 1;
    while (1) {
        ESP_LOGI(TAG, "Cycle %d: Moving mouse LEFT to RIGHT", cycle);

        // Move right for about 2 seconds (20 steps * 100ms)
        for (int i = 0; i < 20; i++) {
            send_mouse_move(10, 0);  // Move right (positive X)
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Small pause
        vTaskDelay(pdMS_TO_TICKS(500));

        // Move back left for about 2 seconds
        ESP_LOGI(TAG, "Cycle %d: Moving back from RIGHT to LEFT", cycle);
        for (int i = 0; i < 20; i++) {
            send_mouse_move(-10, 0);  // Move left (negative X)
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Pause for 5 seconds
        ESP_LOGI(TAG, "Cycle %d: Pausing for 5 seconds...", cycle);
        vTaskDelay(pdMS_TO_TICKS(5000));

        ESP_LOGI(TAG, "Cycle %d complete", cycle);
        cycle++;
    }
}

void app_main(void)
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing Bluetooth...");

    // Release classic BT memory (we only need BLE)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register GAP callback
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "GAP register failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register HID device profile
    ret = esp_hidd_profile_init();
    if (ret) {
        ESP_LOGE(TAG, "HIDD profile init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register HID callback
    ret = esp_hidd_register_callbacks(hidd_event_callback);
    if (ret) {
        ESP_LOGE(TAG, "HIDD register callbacks failed: %s", esp_err_to_name(ret));
        return;
    }

    // Set security
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    ESP_LOGI(TAG, "HID Mouse initialized. Device name: %s", HIDD_DEVICE_NAME);

    // Create mouse movement task
    xTaskCreate(mouse_move_task, "mouse_move", 4096, NULL, 5, NULL);
}
