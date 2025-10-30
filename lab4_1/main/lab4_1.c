// Lab 4.1 - Print UP/DOWN/LEFT/RIGHT from ICM-42670-P tilt
// Target: ESP32-C3 (RISC-V) + esp-idf-lib/icm42670 v1.0.7
// This reads raw accel, converts to g, smooths with a small SMA, and logs directions via ESP_LOGI.

// ---------- Includes ----------
#include <stdio.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <string.h>

#include <i2cdev.h>          // from esp-idf-lib/i2cdev
#include <icm42670.h>        // from esp-idf-lib/icm42670

// ---------- Logging tag ----------
static const char *TAG = "lab4_1";

// ---------- I2C pins ----------
// If you used the example’s Kconfig symbols, great; otherwise fallback to your board pins.
#ifndef CONFIG_EXAMPLE_I2C_MASTER_SDA
// RUST ESP32-C3 boards commonly use SDA=10, SCL=8 (adjust if your wiring differs).
#define CONFIG_EXAMPLE_I2C_MASTER_SDA 10
#endif
#ifndef CONFIG_EXAMPLE_I2C_MASTER_SCL
#define CONFIG_EXAMPLE_I2C_MASTER_SCL 8
#endif

// I2C port index (0 for ESP32-C3 default)
#define I2C_PORT 0

// Choose I2C address: tie AD0 to GND -> 0x68 (default), to VCC -> 0x69.
// If you enabled the example menuconfig flags, these get picked up; else default to GND.
#if defined(CONFIG_EXAMPLE_I2C_ADDRESS_VCC)
#define ICM_ADDR ICM42670_I2C_ADDR_VCC
#else
#define ICM_ADDR ICM42670_I2C_ADDR_GND
#endif

// ---------- Simple Moving Average (SMA) ----------
typedef struct {
    float x[8], y[8], z[8];     // ring buffers for last N samples
    int   idx;                  // current index
    int   count;                // how many samples have been filled (<= N)
} sma_t;

static void sma_init(sma_t *f) {
    for (int i = 0; i < 8; i++) { f->x[i] = f->y[i] = f->z[i] = 0.0f; }
    f->idx = 0;
    f->count = 0;
}

static void sma_push(sma_t *f, float ax, float ay, float az) {
    f->x[f->idx] = ax;  f->y[f->idx] = ay;  f->z[f->idx] = az;
    f->idx = (f->idx + 1) & 7;                  // modulo 8
    if (f->count < 8) f->count++;
}

static void sma_get(const sma_t *f, float *ax, float *ay, float *az) {
    float sx = 0, sy = 0, sz = 0;
    int n = f->count ? f->count : 1;
    for (int i = 0; i < n; i++) { sx += f->x[i]; sy += f->y[i]; sz += f->z[i]; }
    *ax = sx / n; *ay = sy / n; *az = sz / n;
}

// ---------- Helpers ----------
static float lsb_per_g_for_range(icm42670_accel_fsr_t r) {
    // From the ICM-426xx convention: 2g -> 16384 LSB/g, 4g -> 8192, 8g -> 4096, 16g -> 2048.
    // We’ll map using the enum used by v1.0.7 docs: ICM42670_ACCEL_RANGE_* . :contentReference[oaicite:1]{index=1}
    switch (r) {
        case ICM42670_ACCEL_RANGE_2G:  return 16384.0f;
        case ICM42670_ACCEL_RANGE_4G:  return 8192.0f;
        case ICM42670_ACCEL_RANGE_8G:  return 4096.0f;
        default: /*16G*/               return 2048.0f;
    }
}

// Decide direction with a little hysteresis so logs don’t flicker
typedef struct {
    float hi;  // threshold to engage
    float lo;  // threshold to release
    bool  pos_on;
    bool  neg_on;
} axis_gate_t;

static void gate_init(axis_gate_t *g, float engage_g) {
    g->hi = engage_g;
    g->lo = engage_g * 0.7f;  // ~30% hysteresis
    g->pos_on = g->neg_on = false;
}

static void gate_update(axis_gate_t *g, float v /* in g */) {
    if (!g->pos_on && v >  g->hi) g->pos_on = true;
    if ( g->pos_on && v <  g->lo) g->pos_on = false;
    if (!g->neg_on && v < -g->hi) g->neg_on = true;
    if ( g->neg_on && v > -g->lo) g->neg_on = false;
}

// ---------- Task ----------
static void task_lab4_1(void *arg) {
    ESP_LOGI(TAG, "Starting Lab 4.1 (ICM-42670 tilt logger)");

    // Init shared I2C driver
    ESP_ERROR_CHECK(i2cdev_init());

    // Describe and init device
    icm42670_t imu = {0};
    ESP_ERROR_CHECK(icm42670_init_desc(&imu, ICM_ADDR, I2C_PORT,
                                       CONFIG_EXAMPLE_I2C_MASTER_SDA,
                                       CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(icm42670_init(&imu));

    // Configure accelerometer: 4g range, 200 Hz ODR, 8x avg (LPF/AVG reduce noise)
    const icm42670_accel_fsr_t ACC_RANGE = ICM42670_ACCEL_RANGE_4G;       // enum name per v1.0.7 docs
    ESP_ERROR_CHECK(icm42670_set_accel_fsr(&imu, ACC_RANGE));             // set full-scale range
    ESP_ERROR_CHECK(icm42670_set_accel_odr(&imu, ICM42670_ACCEL_ODR_200HZ));
    ESP_ERROR_CHECK(icm42670_set_accel_avg(&imu, ICM42670_ACCEL_AVG_8X));

    // Power: accel on in low-noise (LN) mode for responsive reading
    ESP_ERROR_CHECK(icm42670_set_accel_pwr_mode(&imu, ICM42670_ACCEL_ENABLE_LN_MODE));

    // Convert LSB -> g based on range
    const float LSB_PER_G = lsb_per_g_for_range(ACC_RANGE);
    const float INV_LSB_PER_G = 1.0f / LSB_PER_G;

    // Thresholds (in g) for declaring directions (tune if you want it stricter/looser)
    axis_gate_t xgate, ygate;
    gate_init(&xgate, 0.08f);   // ~0.08 g engage; ~0.056 g release
    gate_init(&ygate, 0.08f);

    // Simple moving average
    sma_t filt; sma_init(&filt);

    // Main loop: read raw accel, convert to g, smooth, print direction
    while (1) {
        // Read raw accel X/Y/Z using generic raw register helper (v1.0.7 API) :contentReference[oaicite:2]{index=2}
        int16_t rx = 0, ry = 0, rz = 0;
        ESP_ERROR_CHECK(icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_X1, &rx));
        ESP_ERROR_CHECK(icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_Y1, &ry));
        ESP_ERROR_CHECK(icm42670_read_raw_data(&imu, ICM42670_REG_ACCEL_DATA_Z1, &rz));

        // Convert to g
        float gx = rx * INV_LSB_PER_G;
        float gy = ry * INV_LSB_PER_G;
        float gz = rz * INV_LSB_PER_G;

        // Smooth and fetch filtered values
        sma_push(&filt, gx, gy, gz);
        float fx, fy, fz;
        sma_get(&filt, &fx, &fy, &fz);

        // Update direction gates
        gate_update(&xgate, fx);
        gate_update(&ygate, fy);

        // Compose message like "UP LEFT", "DOWN", etc. (ESP_LOGI required)
        char msg[16]; msg[0] = '\0';
        bool wrote = false;

        if (ygate.pos_on) { strcat(msg, "UP"); wrote = true; }
        if (ygate.neg_on) { strcat(msg, "DOWN"); wrote = true; }

        if (xgate.pos_on) { if (wrote) strcat(msg, " "); strcat(msg, "RIGHT"); wrote = true; }
        if (xgate.neg_on) { if (wrote) strcat(msg, " "); strcat(msg, "LEFT");  wrote = true; }

        if (!wrote) strcpy(msg, "FLAT");

        ESP_LOGI(TAG, "%s  (gx=%.3f gy=%.3f gz=%.3f)", msg, fx, fy, fz);

        vTaskDelay(pdMS_TO_TICKS(20)); // ~50 Hz print rate
    }
}

void app_main(void) {
    xTaskCreatePinnedToCore(task_lab4_1, "lab4_1", 4096, NULL, 5, NULL, 0);
}
