#ifndef ESP_HIDD_H
#define ESP_HIDD_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_hid_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_hidd_dev_t;

typedef enum {
    ESP_HIDD_START_EVENT = 0,
    ESP_HIDD_CONNECT_EVENT,
    ESP_HIDD_DISCONNECT_EVENT,
    ESP_HIDD_STOP_EVENT,
    ESP_HIDD_MAX_EVENT
} esp_hidd_event_t;

typedef union {
    struct {
        esp_err_t status;
    } start;
    struct {
        esp_err_t status;
    } connect;
    struct {
        esp_err_t status;
    } disconnect;
    struct {
        esp_err_t status;
    } stop;
} esp_hidd_event_data_t;

typedef void (*esp_hidd_event_cb_t)(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);

esp_err_t esp_ble_hidd_dev_init(esp_hid_device_config_t *config, 
                                 esp_hid_transport_t transport,
                                 esp_hidd_event_cb_t callback,
                                 esp_hidd_dev_t **dev);

esp_err_t esp_ble_hidd_register_event_handler(esp_hidd_event_cb_t callback, void *arg);

esp_err_t esp_hidd_dev_input_set(esp_hidd_dev_t *dev, size_t map_index, 
                                  size_t report_id, uint8_t *data, size_t length);

esp_err_t esp_hidd_dev_deinit(esp_hidd_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* ESP_HIDD_H */