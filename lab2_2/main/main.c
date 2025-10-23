#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"

#define TAG "LAB2_2"
#define SDA_PIN 10
#define SCL_PIN 8
#define SHTC3_ADDR 0x70
#define I2C_HZ 100000
#define KEEP_SENSOR_AWAKE 1   // set to 0 later for lab submission

#define CMD_WAKE   0x3517
#define CMD_SLEEP  0xB098
#define CMD_MEAS_T 0x7866     // T-first, no clock stretch

// CRC-8 checker per datasheet
static bool crc_ok(const uint8_t *d, uint8_t crc8)
{
    uint8_t c = 0xFF;
    for (int i = 0; i < 2; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) {
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
        }
    }
    return (c == crc8);
}

// transmit 16-bit command
static esp_err_t tx16(i2c_master_dev_handle_t dev, uint16_t cmd)
{
    uint8_t t[2] = { (uint8_t)(cmd >> 8), (uint8_t)cmd };
    return i2c_master_transmit(dev, t, 2, pdMS_TO_TICKS(100));
}

// single read cycle
static bool read_once(i2c_master_dev_handle_t dev,
                      uint16_t *rawT, uint16_t *rawH,
                      float *tC, float *tF, float *RH)
{
#if !KEEP_SENSOR_AWAKE
    if (tx16(dev, CMD_WAKE) != ESP_OK)
        return false;
    vTaskDelay(pdMS_TO_TICKS(2));   // wake-up >240 µs
#endif

    if (tx16(dev, CMD_MEAS_T) != ESP_OK) {
        ESP_LOGE(TAG, "measure cmd failed");
        goto sleep_and_fail;
    }

    vTaskDelay(pdMS_TO_TICKS(25));  // allow conversion

    uint8_t rx[6];
    if (i2c_master_receive(dev, rx, sizeof(rx), pdMS_TO_TICKS(100)) != ESP_OK)
        goto sleep_and_fail;
    if (!crc_ok(&rx[0], rx[2]) || !crc_ok(&rx[3], rx[5]))
        goto sleep_and_fail;

    uint16_t rT = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t rH = ((uint16_t)rx[3] << 8) | rx[4];
    float tc = -45.f + 175.f * (rT / 65535.f);
    float rh = 100.f * (rH / 65535.f);

    if (rawT) { *rawT = rT; }
    if (rawH) { *rawH = rH; }
    if (tC)  { *tC = tc; }
    if (tF)  { *tF = tc * 1.8f + 32.f; }
    if (RH)  { *RH = rh; }

#if !KEEP_SENSOR_AWAKE
    (void)tx16(dev, CMD_SLEEP);
#endif
    return true;

sleep_and_fail:
#if !KEEP_SENSOR_AWAKE
    (void)tx16(dev, CMD_SLEEP);
#endif
    return false;
}

void app_main(void)
{
    // (kept your diagnostic header line)
    printf("SHTC3 diag on I2C0 SDA=%d SCL=%d addr=0x%02X\n",
           SDA_PIN, SCL_PIN, SHTC3_ADDR);

    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t bcfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = SCL_PIN,
        .sda_io_num = SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bcfg, &bus));

    i2c_master_dev_handle_t dev = NULL;
    i2c_device_config_t dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_ADDR,
        .scl_speed_hz = I2C_HZ
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dcfg, &dev));

#if KEEP_SENSOR_AWAKE
    (void)tx16(dev, CMD_WAKE);
    vTaskDelay(pdMS_TO_TICKS(2));
#endif

    while (1) {
        float tC, tF, RH;
        uint16_t rT, rH;
        bool ok = read_once(dev, &rT, &rH, &tC, &tF, &RH);

        if (ok) {
            int c  = (int)(tC + 0.5f);
            int f  = (int)(tF + 0.5f);
            int rh = (int)(RH + 0.5f);
            // >>> formatted output per lab <<< 
            printf("Temperature is %dC (or %dF) with a %d%% humidity\n", c, f, rh);
        } else {
            printf("Read failed (I2C/CRC)\n");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
