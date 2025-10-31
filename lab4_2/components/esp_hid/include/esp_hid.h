#ifndef ESP_HID_H
#define ESP_HID_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_HID_TRANSPORT_BLE,
    ESP_HID_TRANSPORT_BT,
    ESP_HID_TRANSPORT_USB
} esp_hid_transport_t;

typedef struct {
    const uint8_t *data;
    uint16_t len;
} esp_hid_raw_report_map_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t version;
    const char *device_name;
    const char *manufacturer_name;
    const char *serial_number;
    esp_hid_raw_report_map_t *report_maps;
    uint8_t report_maps_len;
} esp_hid_device_config_t;

/**
 * @brief Initialize HID GAP
 */
esp_err_t esp_hid_gap_init(uint8_t mode);

/**
 * @brief Start BLE advertising
 */
esp_err_t esp_hid_ble_gap_adv_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_HID_H */
