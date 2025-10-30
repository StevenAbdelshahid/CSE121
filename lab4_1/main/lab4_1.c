// Lab 4.1 — ICM-42670 on ESP32-C3 (I2C pins fixed on the board)
// Minimal + stable: let esp-idf-lib's i2cdev manage the I2C driver.

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"      // <-- provides ESP_RETURN_ON_ERROR

#include "i2cdev.h"         // from esp-idf-lib (manages I2C driver)
#include "icm42670.h"       // from esp-idf-lib

#define TAG           "lab4_1"

// Board-wired I2C
#define I2C_PORT      I2C_NUM_0
#define I2C_SDA_GPIO  10
#define I2C_SCL_GPIO  8

// ICM addresses
#define ADDR_GND      ICM42670_I2C_ADDR_GND   // 0x68
#define ADDR_VCC      ICM42670_I2C_ADDR_VCC   // 0x69

// Simple 8-sample moving average for display steadiness
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

static esp_err_t imu_try_init(icm42670_t *imu, uint8_t addr){
    // Free any previous desc (safe if unused), then (re)create
    icm42670_free_desc(imu);
    ESP_RETURN_ON_ERROR(
        icm42670_init_desc(imu, addr, I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO),
        TAG, "init_desc");
    ESP_RETURN_ON_ERROR(icm42670_init(imu), TAG, "init");
    return ESP_OK;
}

static void task_lab4_1(void *arg){
    ESP_LOGI(TAG, "Start (I2C sda=%d scl=%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);

    // Creates mutexes/queues used by all esp-idf-lib I2C devices
    ESP_ERROR_CHECK(i2cdev_init());

    icm42670_t imu = {0};
    uint8_t addr = 0x00;

    // Try both possible addresses, no manual driver calls
    if (imu_try_init(&imu, ADDR_GND) == ESP_OK) addr = ADDR_GND;
    else if (imu_try_init(&imu, ADDR_VCC) == ESP_OK) addr = ADDR_VCC;
    else {
        ESP_LOGE(TAG, "ICM-42670 not detected at 0x68 or 0x69.");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (imu_try_init(&imu, ADDR_GND) == ESP_OK){ addr=ADDR_GND; break; }
            if (imu_try_init(&imu, ADDR_VCC) == ESP_OK){ addr=ADDR_VCC; break; }
        }
    }
    ESP_LOGI(TAG, "ICM-42670 detected @0x%02X", addr);

    // Configure accel (modest settings for stability)
    const icm42670_accel_fsr_t RANGE = ICM42670_ACCEL_RANGE_4G;
    ESP_ERROR_CHECK(icm42670_set_accel_fsr(&imu, RANGE));
    ESP_ERROR_CHECK(icm42670_set_accel_avg(&imu, ICM42670_ACCEL_AVG_8X));
    ESP_ERROR_CHECK(icm42670_set_accel_odr(&imu, ICM42670_ACCEL_ODR_200HZ));
    ESP_ERROR_CHECK(icm42670_set_accel_pwr_mode(&imu, ICM42670_ACCEL_ENABLE_LN_MODE));

    const float inv_lsb_g = 1.0f / lsb_per_g(RANGE);
    sma8_t filt; sma8_init(&filt);

    int consecutive_fail = 0;

    while (1){
        int16_t rx=0, ry=0, rz=0;
        esp_err_t e = ESP_OK;
        e = icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_X1, &rx);
        if (e == ESP_OK) e = icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_Y1, &ry);
        if (e == ESP_OK) e = icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_Z1, &rz);

        if (e != ESP_OK){
            if (++consecutive_fail >= 5){
                ESP_LOGW(TAG, "I2C errors (%s). Recreating descriptor...", esp_err_to_name(e));
                icm42670_free_desc(&imu);
                if (imu_try_init(&imu, addr) != ESP_OK){
                    uint8_t other = (addr==ADDR_GND)?ADDR_VCC:ADDR_GND;
                    if (imu_try_init(&imu, other) == ESP_OK) addr = other;
                }
                // Reapply accel config
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

        float gx = rx * inv_lsb_g;
        float gy = ry * inv_lsb_g;
        float gz = rz * inv_lsb_g;

        sma8_push(&filt, gx, gy, gz);
        float fx, fy, fz; sma8_get(&filt, &fx, &fy, &fz);

        // Super simple quadrant announcement
        const float th = 0.08f;  // deadband ~0.08 g
        const char *vert  = (fy >  th) ? "UP"   : (fy < -th ? "DOWN" : "");
        const char *horiz = (fx >  th) ? "RIGHT": (fx < -th ? "LEFT" : "");
        char msg[24]="FLAT";
        if (*vert || *horiz){
            msg[0]='\0';
            if (*vert)  strcpy(msg, vert);
            if (*horiz) { if (*vert) strcat(msg, " "); strcat(msg, horiz); }
        }
        ESP_LOGI(TAG, "%s (gx=%.3f gy=%.3f gz=%.3f) @0x%02X", msg, fx, fy, fz, addr);

        vTaskDelay(pdMS_TO_TICKS(20)); // ~50Hz print
    }
}

void app_main(void){
    xTaskCreatePinnedToCore(task_lab4_1, "lab4_1", 4096, NULL, 5, NULL, 0);
}
