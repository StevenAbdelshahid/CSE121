#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "DFRobot_LCD.h"

static const char* TAG = "LAB3_3";

// ===== BUS / PINS (same as Lab 3.2) =====
static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
static constexpr int SDA_PIN  = 10;
static constexpr int SCL_PIN  = 8;

// ===== LCD =====
static constexpr uint8_t LCD_ADDR = 0x3E;  // AiP31068L (7-bit)

// ===== Known addresses =====
static constexpr uint8_t ADDR_SHTC3   = 0x70; // also a common mux addr
static constexpr uint8_t ADDR_SHT3X_1 = 0x44;
static constexpr uint8_t ADDR_SHT3X_2 = 0x45;
static constexpr uint8_t ADDR_AHT20   = 0x38;
static constexpr uint8_t ADDR_SI7021  = 0x40; // rare but easy to support
static constexpr uint8_t ADDR_TCA9548 = 0x70; // mux

// ---------- I2C helpers (legacy driver, same as your LCD lib) ----------
static esp_err_t i2c_probe(uint8_t addr7) {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(h);
    if (err == ESP_OK) err = i2c_master_write_byte(h, (addr7 << 1) | I2C_MASTER_WRITE, true);
    if (err == ESP_OK) err = i2c_master_stop(h);
    if (err == ESP_OK) err = i2c_master_cmd_begin(I2C_PORT, h, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(h);
    return err;
}

static esp_err_t i2c_write_bytes(uint8_t addr7, const uint8_t* bytes, size_t n) {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(h);
    if (err == ESP_OK) err = i2c_master_write_byte(h, (addr7 << 1) | I2C_MASTER_WRITE, true);
    if (err == ESP_OK && n) err = i2c_master_write(h, (uint8_t*)bytes, n, true);
    if (err == ESP_OK) err = i2c_master_stop(h);
    if (err == ESP_OK) err = i2c_master_cmd_begin(I2C_PORT, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    return err;
}

static esp_err_t i2c_read_bytes(uint8_t addr7, uint8_t* out, size_t n) {
    if (n == 0) return ESP_OK;
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(h);
    if (err == ESP_OK) err = i2c_master_write_byte(h, (addr7 << 1) | I2C_MASTER_READ, true);
    if (err == ESP_OK && n > 1) err = i2c_master_read(h, out, n - 1, I2C_MASTER_ACK);
    if (err == ESP_OK) err = i2c_master_read_byte(h, &out[n - 1], I2C_MASTER_NACK);
    if (err == ESP_OK) err = i2c_master_stop(h);
    if (err == ESP_OK) err = i2c_master_cmd_begin(I2C_PORT, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    return err;
}

// ---------- CRC used by Sensirion (SHTxx) ----------
static bool crc_sensirion_2b(const uint8_t *d, uint8_t crc8) {
    uint8_t c = 0xFF;
    for (int i = 0; i < 2; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) {
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
        }
    }
    return (c == crc8);
}

// ---------- Sensor drivers ----------
static bool shtc3_measure(float* tC, float* RH) {
    uint8_t wake[2] = { 0x35, 0x17 };
    if (i2c_write_bytes(ADDR_SHTC3, wake, 2) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
    uint8_t meas[2] = { 0x78, 0x66 }; // T-first, no stretch
    if (i2c_write_bytes(ADDR_SHTC3, meas, 2) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(25));
    uint8_t rx[6];
    if (i2c_read_bytes(ADDR_SHTC3, rx, 6) != ESP_OK) return false;
    if (!crc_sensirion_2b(&rx[0], rx[2]) || !crc_sensirion_2b(&rx[3], rx[5])) return false;
    uint16_t rT = (uint16_t(rx[0]) << 8) | rx[1];
    uint16_t rH = (uint16_t(rx[3]) << 8) | rx[4];
    *tC = -45.0f + 175.0f * (rT / 65535.0f);
    *RH = 100.0f * (rH / 65535.0f);
    uint8_t sleep[2] = { 0xB0, 0x98 };
    (void)i2c_write_bytes(ADDR_SHTC3, sleep, 2);
    return true;
}

static bool sht3x_measure(uint8_t addr, float* tC, float* RH) {
    uint8_t cmd[2] = { 0x24, 0x00 }; // single-shot, high rep, no stretch
    if (i2c_write_bytes(addr, cmd, 2) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(15));
    uint8_t rx[6];
    if (i2c_read_bytes(addr, rx, 6) != ESP_OK) return false;
    if (!crc_sensirion_2b(&rx[0], rx[2]) || !crc_sensirion_2b(&rx[3], rx[5])) return false;
    uint16_t rT = (uint16_t(rx[0]) << 8) | rx[1];
    uint16_t rH = (uint16_t(rx[3]) << 8) | rx[4];
    *tC = -45.0f + 175.0f * (rT / 65535.0f);
    *RH = 100.0f * (rH / 65535.0f);
    return true;
}

static bool aht20_init(uint8_t addr) {
    uint8_t rst = 0xBA;
    (void)i2c_write_bytes(addr, &rst, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t init_cmd[3] = { 0xBE, 0x08, 0x00 };
    return (i2c_write_bytes(addr, init_cmd, 3) == ESP_OK);
}

static bool aht20_measure(uint8_t addr, float* tC, float* RH) {
    uint8_t trig[3] = { 0xAC, 0x33, 0x00 };
    if (i2c_write_bytes(addr, trig, 3) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(80));
    uint8_t rx[6];
    if (i2c_read_bytes(addr, rx, 6) != ESP_OK) return false;
    uint32_t rH = (uint32_t(rx[1]) << 12) | (uint32_t(rx[2]) << 4) | ((rx[3] >> 4) & 0x0F);
    uint32_t rT = (uint32_t(rx[3] & 0x0F) << 16) | (uint32_t(rx[4]) << 8) | rx[5];
    *RH = (rH * 100.0f) / 1048576.0f;
    *tC = (rT * 200.0f) / 1048576.0f - 50.0f;
    return true;
}

static bool si7021_measure(float* tC, float* RH) {
    uint8_t cmd_rh = 0xE5; // Measure RH (hold)
    if (i2c_write_bytes(ADDR_SI7021, &cmd_rh, 1) != ESP_OK) return false;
    uint8_t rh2[2];
    if (i2c_read_bytes(ADDR_SI7021, rh2, 2) != ESP_OK) return false;
    uint16_t rawRH = (uint16_t(rh2[0]) << 8) | rh2[1];
    *RH = ((125.0f * rawRH) / 65536.0f) - 6.0f;

    uint8_t cmd_t = 0xE3; // Measure T (hold)
    if (i2c_write_bytes(ADDR_SI7021, &cmd_t, 1) != ESP_OK) return false;
    uint8_t t2[2];
    if (i2c_read_bytes(ADDR_SI7021, t2, 2) != ESP_OK) return false;
    uint16_t rawT = (uint16_t(t2[0]) << 8) | t2[1];
    *tC = ((175.72f * rawT) / 65536.0f) - 46.85f;
    return true;
}

// ---------- Mux control ----------
static esp_err_t tca_select(uint8_t channel) {
    uint8_t mask = (uint8_t)(1u << channel);
    return i2c_write_bytes(ADDR_TCA9548, &mask, 1);
}

// ---------- Detection ----------
enum class SensorKind { NONE, SHTC3_DIRECT, SHT3X, AHT20, SI7021 };
static SensorKind g_kind = SensorKind::NONE;
static uint8_t    g_addr = 0;
static int        g_mux_channel = -1; // -1 => no mux

static bool detect_sensor() {
    // Try direct SHTC3 @0x70
    if (i2c_probe(ADDR_SHTC3) == ESP_OK) {
        float t, h;
        if (shtc3_measure(&t, &h)) { g_kind = SensorKind::SHTC3_DIRECT; g_addr = ADDR_SHTC3; g_mux_channel = -1; return true; }
        // treat 0x70 as mux and search channels
        for (int ch = 0; ch < 8; ++ch) {
            if (tca_select(ch) != ESP_OK) continue;
            vTaskDelay(pdMS_TO_TICKS(2));

            if (i2c_probe(ADDR_SHT3X_1) == ESP_OK || i2c_probe(ADDR_SHT3X_2) == ESP_OK) {
                uint8_t addr = (i2c_probe(ADDR_SHT3X_1) == ESP_OK) ? ADDR_SHT3X_1 : ADDR_SHT3X_2;
                float tC, RH; if (sht3x_measure(addr, &tC, &RH)) { g_kind = SensorKind::SHT3X; g_addr = addr; g_mux_channel = ch; return true; }
            }
            if (i2c_probe(ADDR_AHT20) == ESP_OK) {
                if (aht20_init(ADDR_AHT20)) {
                    float tC, RH; if (aht20_measure(ADDR_AHT20, &tC, &RH)) { g_kind = SensorKind::AHT20; g_addr = ADDR_AHT20; g_mux_channel = ch; return true; }
                }
            }
            if (i2c_probe(ADDR_SI7021) == ESP_OK) {
                float tC, RH; if (si7021_measure(&tC, &RH)) { g_kind = SensorKind::SI7021; g_addr = ADDR_SI7021; g_mux_channel = ch; return true; }
            }
        }
        return false;
    }

    // Root-bus fallbacks
    if (i2c_probe(ADDR_SHT3X_1) == ESP_OK || i2c_probe(ADDR_SHT3X_2) == ESP_OK) {
        uint8_t addr = (i2c_probe(ADDR_SHT3X_1) == ESP_OK) ? ADDR_SHT3X_1 : ADDR_SHT3X_2;
        float tC, RH; if (sht3x_measure(addr, &tC, &RH)) { g_kind = SensorKind::SHT3X; g_addr = addr; g_mux_channel = -1; return true; }
    }
    if (i2c_probe(ADDR_AHT20) == ESP_OK) {
        if (aht20_init(ADDR_AHT20)) {
            float tC, RH; if (aht20_measure(ADDR_AHT20, &tC, &RH)) { g_kind = SensorKind::AHT20; g_addr = ADDR_AHT20; g_mux_channel = -1; return true; }
        }
    }
    if (i2c_probe(ADDR_SI7021) == ESP_OK) {
        float tC, RH; if (si7021_measure(&tC, &RH)) { g_kind = SensorKind::SI7021; g_addr = ADDR_SI7021; g_mux_channel = -1; return true; }
    }
    return false;
}

static bool read_sensor(float* tC, float* RH) {
    if (g_mux_channel >= 0) {
        if (tca_select((uint8_t)g_mux_channel) != ESP_OK) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    switch (g_kind) {
        case SensorKind::SHTC3_DIRECT: return shtc3_measure(tC, RH);
        case SensorKind::SHT3X:        return sht3x_measure(g_addr, tC, RH);
        case SensorKind::AHT20:        return aht20_measure(g_addr, tC, RH);
        case SensorKind::SI7021:       return si7021_measure(tC, RH);
        default:                        return false;
    }
}

// -------------------- Main --------------------
extern "C" void app_main(void) {
    // Quiet logs
    esp_log_level_set("DFRobot_LCD", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_WARN);

    // Init LCD (installs I2C driver once @100k)
    DFRobot_LCD lcd(I2C_PORT, SDA_PIN, SCL_PIN, LCD_ADDR);
    lcd.init();

    // Labels (match your photo)
    lcd.clear();
    lcd.setCursor(0, 0); lcd.printstr("Temp:");
    lcd.setCursor(0, 1); lcd.printstr("Hum :");

    const uint8_t COL_VAL = 6;  // values start column

    // Detect sensor once (no logs, no "ERR/N/A" on screen)
    (void)detect_sensor();

    char buf[8];
    while (true) {
        float tC = 0.0f, RH = 0.0f;
        if (read_sensor(&tC, &RH)) {
            int t_i  = (int)lroundf(tC);
            int rh_i = (int)lroundf(RH);

            // Fixed-width fields to overwrite old content
            snprintf(buf, sizeof(buf), "%3dC", t_i);
            lcd.setCursor(COL_VAL, 0);
            lcd.printstr(buf);

            snprintf(buf, sizeof(buf), "%3d%%", rh_i);
            lcd.setCursor(COL_VAL, 1);
            lcd.printstr(buf);
        }
        // On read fail: keep last values; don’t log or change labels.
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 Hz
    }
}
