// Lab 4.3 - Integrated BLE HID Mouse with IMU Control
// Combines lab4_1 (IMU sensor) with lab4_2 (BLE HID Mouse)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_hidd_prf_api.h"
#include "hid_dev.h"

// IMU includes
#include "i2cdev.h"
#include "icm42670.h"

static const char *TAG = "LAB4_3";

// I2C Configuration (from lab4_1)
#define I2C_PORT      I2C_NUM_0
#define I2C_SDA_GPIO  10
#define I2C_SCL_GPIO  8
#define ADDR_GND      ICM42670_I2C_ADDR_GND   // 0x68
#define ADDR_VCC      ICM42670_I2C_ADDR_VCC   // 0x69

// BLE HID State
static uint16_t hid_conn_id = 0;
static bool connected = false;

#define HIDD_DEVICE_NAME "ESP32 Air Mouse"

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

// Inclination levels for speed control
typedef enum {
    TILT_NONE = 0,
    TILT_A_BIT,     // Small tilt - slow movement
    TILT_A_LOT      // Large tilt - fast movement
} tilt_level_t;

// Direction state for acceleration tracking
typedef struct {
    int64_t start_time_ms;  // When this direction started
    tilt_level_t level;
    bool active;
} direction_state_t;

static direction_state_t dir_x = {0};
static direction_state_t dir_y = {0};

// Moving average filter (from lab4_1)
typedef struct { float bx[8], by[8], bz[8]; int idx, n; } sma8_t;
static void sma8_init(sma8_t *f){ memset(f,0,sizeof(*f)); }
static void sma8_push(sma8_t *f, float x, float y, float z){
    f->bx[f->idx]=x; f->by[f->idx]=y; f->bz[f->idx]=z;
    f->idx=(f->idx+1)&7; if(f->n<8) f->n++;
}
static void sma8_get(const sma8_t *f, float *x, float *y, float *z){
    int n=f->n?f->n:1; float sx=0,sy=0,sz=0;
    for(int i=0;i<n;i++){ sx+=f->bx[i]; sy+=f->by[i]; sz+=f->bz[i]; }
    *x=sx/n; *y=sy/n; *z=sz/n;
}

static inline float lsb_per_g(icm42670_accel_fsr_t r){
    switch(r){
        case ICM42670_ACCEL_RANGE_2G:  return 16384.0f;
        case ICM42670_ACCEL_RANGE_4G:  return 8192.0f;
        case ICM42670_ACCEL_RANGE_8G:  return 4096.0f;
        default /*16G*/:               return 2048.0f;
    }
}

// Determine tilt level from acceleration value
static tilt_level_t get_tilt_level(float g_value) {
    float abs_g = fabsf(g_value);

    if (abs_g < 0.12f) {
        return TILT_NONE;  // Deadband
    } else if (abs_g < 0.35f) {
        return TILT_A_BIT;  // Small tilt
    } else {
        return TILT_A_LOT;  // Large tilt
    }
}

// Calculate mouse delta with acceleration
static int8_t calculate_mouse_delta(direction_state_t *dir, float g_value) {
    tilt_level_t new_level = get_tilt_level(g_value);
    int64_t now_ms = esp_timer_get_time() / 1000;

    // Check if direction changed or stopped
    if (new_level == TILT_NONE || !dir->active) {
        dir->active = (new_level != TILT_NONE);
        dir->level = new_level;
        dir->start_time_ms = now_ms;
        return 0;
    }

    // Update level if changed
    if (new_level != dir->level) {
        dir->level = new_level;
        dir->start_time_ms = now_ms;
    }

    // Calculate time held in this direction (in ms)
    int64_t duration_ms = now_ms - dir->start_time_ms;

    // Base speed depends on tilt level
    int base_speed = (dir->level == TILT_A_BIT) ? 3 : 8;

    // Acceleration multiplier based on duration
    int accel_mult = 1;
    if (duration_ms > 500) {
        accel_mult = 3;  // 3x after 500ms
    } else if (duration_ms > 200) {
        accel_mult = 2;  // 2x after 200ms
    }

    int delta = base_speed * accel_mult;

    // Apply direction (negative for left/down)
    return (g_value > 0) ? delta : -delta;
}

static void send_mouse_move(int8_t x, int8_t y)
{
    if (connected) {
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
        ESP_LOGI(TAG, "BLE HID Connected");
        hid_conn_id = param->connect.conn_id;
        connected = true;
        // Reset direction states on new connection
        dir_x.active = false;
        dir_y.active = false;
        break;
    }
    case ESP_HIDD_EVENT_BLE_DISCONNECT: {
        connected = false;
        ESP_LOGI(TAG, "BLE HID Disconnected");
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

static esp_err_t imu_try_init(icm42670_t *imu, uint8_t addr){
    icm42670_free_desc(imu);
    ESP_RETURN_ON_ERROR(
        icm42670_init_desc(imu, addr, I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO),
        TAG, "init_desc");
    ESP_RETURN_ON_ERROR(icm42670_init(imu), TAG, "init");
    return ESP_OK;
}

// IMU task - reads sensor and controls mouse
static void imu_mouse_task(void *arg)
{
    ESP_LOGI(TAG, "Starting IMU task (I2C sda=%d scl=%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);

    ESP_ERROR_CHECK(i2cdev_init());

    icm42670_t imu = {0};
    uint8_t addr = 0x00;

    // Try to find IMU
    if (imu_try_init(&imu, ADDR_GND) == ESP_OK) addr = ADDR_GND;
    else if (imu_try_init(&imu, ADDR_VCC) == ESP_OK) addr = ADDR_VCC;
    else {
        ESP_LOGE(TAG, "ICM-42670 not detected!");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "ICM-42670 detected @0x%02X", addr);

    // Configure accelerometer
    const icm42670_accel_fsr_t RANGE = ICM42670_ACCEL_RANGE_4G;
    ESP_ERROR_CHECK(icm42670_set_accel_fsr(&imu, RANGE));
    ESP_ERROR_CHECK(icm42670_set_accel_avg(&imu, ICM42670_ACCEL_AVG_8X));
    ESP_ERROR_CHECK(icm42670_set_accel_odr(&imu, ICM42670_ACCEL_ODR_200HZ));
    ESP_ERROR_CHECK(icm42670_set_accel_pwr_mode(&imu, ICM42670_ACCEL_ENABLE_LN_MODE));

    const float inv_lsb_g = 1.0f / lsb_per_g(RANGE);
    sma8_t filt;
    sma8_init(&filt);

    ESP_LOGI(TAG, "Waiting for BLE connection...");
    while (!connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "BLE connected! Air mouse active!");

    int consecutive_fail = 0;

    while (1) {
        int16_t rx=0, ry=0, rz=0;
        esp_err_t e = ESP_OK;

        e = icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_X1, &rx);
        if (e == ESP_OK) e = icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_Y1, &ry);
        if (e == ESP_OK) e = icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_Z1, &rz);

        if (e != ESP_OK) {
            if (++consecutive_fail >= 5) {
                ESP_LOGW(TAG, "I2C errors, reinitializing...");
                icm42670_free_desc(&imu);
                if (imu_try_init(&imu, addr) != ESP_OK) {
                    uint8_t other = (addr==ADDR_GND)?ADDR_VCC:ADDR_GND;
                    if (imu_try_init(&imu, other) == ESP_OK) addr = other;
                }
                icm42670_set_accel_fsr(&imu, RANGE);
                icm42670_set_accel_avg(&imu, ICM42670_ACCEL_AVG_8X);
                icm42670_set_accel_odr(&imu, ICM42670_ACCEL_ODR_200HZ);
                icm42670_set_accel_pwr_mode(&imu, ICM42670_ACCEL_ENABLE_LN_MODE);
                consecutive_fail = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        consecutive_fail = 0;

        // Convert to g values
        float gx = rx * inv_lsb_g;
        float gy = ry * inv_lsb_g;
        float gz = rz * inv_lsb_g;

        // Apply smoothing filter
        sma8_push(&filt, gx, gy, gz);
        float fx, fy, fz;
        sma8_get(&filt, &fx, &fy, &fz);

        // Calculate mouse movement with acceleration
        // Note: fx = left/right tilt, fy = up/down tilt
        int8_t mouse_x = calculate_mouse_delta(&dir_x, fx);
        int8_t mouse_y = calculate_mouse_delta(&dir_y, -fy);  // Negate for natural direction

        // Send mouse movement if connected
        if (connected && (mouse_x != 0 || mouse_y != 0)) {
            send_mouse_move(mouse_x, mouse_y);
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // 50Hz update rate
    }
}

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Lab 4.3 - Air Mouse with IMU Control");

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing Bluetooth...");

    // Release classic BT memory
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

    ESP_LOGI(TAG, "BLE HID initialized. Device name: %s", HIDD_DEVICE_NAME);

    // Create IMU mouse control task
    xTaskCreatePinnedToCore(imu_mouse_task, "imu_mouse", 4096, NULL, 5, NULL, 0);
}
