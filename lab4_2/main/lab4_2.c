#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_hidd.h"
#include "esp_hid_gap.h"

static const char *TAG = "HID_MOUSE";

// HID Mouse Report Descriptor
static const uint8_t hid_mouse_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x01,        //     Input (Constant) - Padding
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0               // End Collection
};

// HID Report Map characteristic value
static esp_hid_raw_report_map_t report_maps[] = {
    {
        .data = hid_mouse_descriptor,
        .len = sizeof(hid_mouse_descriptor)
    }
};

// HID device config
static esp_hid_device_config_t hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "ESP32 Mouse",
    .manufacturer_name = "Espressif",
    .serial_number = "1234567890",
    .report_maps = report_maps,
    .report_maps_len = 1
};

typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
} __attribute__((packed)) hid_mouse_input_report_t;

static esp_hidd_dev_t *hid_dev = NULL;
static bool connected = false;

static void hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;

    switch (event) {
    case ESP_HIDD_START_EVENT: {
        ESP_LOGI(TAG, "HID device started");
        break;
    }
    case ESP_HIDD_CONNECT_EVENT: {
        connected = true;
        ESP_LOGI(TAG, "HID device connected");
        break;
    }
    case ESP_HIDD_DISCONNECT_EVENT: {
        connected = false;
        ESP_LOGI(TAG, "HID device disconnected");
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_STOP_EVENT: {
        ESP_LOGI(TAG, "HID device stopped");
        break;
    }
    default:
        break;
    }
}

static void send_mouse_report(int8_t x, int8_t y)
{
    if (connected && hid_dev) {
        hid_mouse_input_report_t report = {
            .buttons = 0,
            .x = x,
            .y = y,
            .wheel = 0
        };
        
        esp_hidd_dev_input_set(hid_dev, 0, 0, (uint8_t *)&report, sizeof(report));
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
    
    // Move mouse from left to right
    ESP_LOGI(TAG, "Moving mouse LEFT to RIGHT");
    
    // Move right for about 2 seconds (20 steps * 100ms)
    for (int i = 0; i < 20; i++) {
        send_mouse_report(10, 0);  // Move right (positive X)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Small pause
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Move back left for about 2 seconds
    ESP_LOGI(TAG, "Moving back from RIGHT to LEFT");
    for (int i = 0; i < 20; i++) {
        send_mouse_report(-10, 0);  // Move left (negative X)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Pause for 5 seconds
    ESP_LOGI(TAG, "Pausing for 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    ESP_LOGI(TAG, "Mouse movement demonstration complete");
    
    // Keep task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
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

    // Set device name BEFORE initializing HID
    ESP_LOGI(TAG, "Setting device name to: %s", hid_config.device_name);
    ret = esp_ble_gap_set_device_name(hid_config.device_name);
    if (ret) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
    }

    // Initialize HID GAP
    ESP_LOGI(TAG, "Initializing HID GAP...");
    ret = esp_hid_gap_init(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "HID GAP init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register HID device
    ESP_LOGI(TAG, "Registering HID device...");
    ret = esp_ble_hidd_register_event_handler(hidd_event_callback, NULL);
    if (ret) {
        ESP_LOGE(TAG, "HID event handler register failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize HID device
    ret = esp_ble_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE, hidd_event_callback, &hid_dev);
    if (ret) {
        ESP_LOGE(TAG, "HID device init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Starting BLE advertising...");
    ret = esp_hid_ble_gap_adv_start();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BLE advertising started successfully!");
    } else {
        ESP_LOGE(TAG, "BLE advertising start failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "HID Mouse Device initialized. Device name: %s, MAC: ", hid_config.device_name);
    const uint8_t *mac = esp_bt_dev_get_address();
    ESP_LOGI(TAG, "BT MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Create mouse movement task
    xTaskCreate(mouse_move_task, "mouse_move", 4096, NULL, 5, NULL);
}