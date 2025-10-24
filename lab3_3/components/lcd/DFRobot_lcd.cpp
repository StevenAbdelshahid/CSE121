#include "DFRobot_LCD.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "DFRobot_LCD";

// AiP31068L (LCD) default 7-bit address
static constexpr uint8_t LCD_I2C_ADDR = 0x3E;  // datasheet 8-bit 0x7C >> 1

// Control bytes for AiP31068L I2C (Co=0): 0x00=command, 0x40=data
static constexpr uint8_t CTRL_CMD  = 0x00;
static constexpr uint8_t CTRL_DATA = 0x40;

DFRobot_LCD::DFRobot_LCD(int i2c_port, int sda_pin, int scl_pin, uint8_t i2c_addr)
: m_port(i2c_port), m_sda(sda_pin), m_scl(scl_pin),
  m_addr(i2c_addr ? i2c_addr : LCD_I2C_ADDR), m_inited(false) {}

// ---- low-level I2C helpers ----
static esp_err_t i2c_write_cmd_byte(i2c_port_t port, uint8_t dev7, uint8_t cmd) {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(h);
    if (err == ESP_OK) err = i2c_master_write_byte(h, (dev7 << 1) | I2C_MASTER_WRITE, true);
    if (err == ESP_OK) err = i2c_master_write_byte(h, CTRL_CMD, true);
    if (err == ESP_OK) err = i2c_master_write_byte(h, cmd, true);
    if (err == ESP_OK) err = i2c_master_stop(h);
    if (err == ESP_OK) err = i2c_master_cmd_begin(port, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "cmd 0x%02X failed: %s", cmd, esp_err_to_name(err));
    return err;
}

static esp_err_t i2c_write_data(i2c_port_t port, uint8_t dev7, const uint8_t* data, size_t len) {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(h);
    if (err == ESP_OK) err = i2c_master_write_byte(h, (dev7 << 1) | I2C_MASTER_WRITE, true);
    if (err == ESP_OK) err = i2c_master_write_byte(h, CTRL_DATA, true);
    if (err == ESP_OK) err = i2c_master_write(h, (uint8_t*)data, len, true);
    if (err == ESP_OK) err = i2c_master_stop(h);
    if (err == ESP_OK) err = i2c_master_cmd_begin(port, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "data len=%u failed: %s", (unsigned)len, esp_err_to_name(err));
    return err;
}

bool DFRobot_LCD::i2c_device_present(uint8_t addr7) const {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (addr7 << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(h);
    esp_err_t err = i2c_master_cmd_begin((i2c_port_t)m_port, h, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(h);
    return (err == ESP_OK);
}

// ---- public API ----
esp_err_t DFRobot_LCD::init() {
    // I2C master @100k (meets datasheet)
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)m_sda;
    conf.scl_io_num = (gpio_num_t)m_scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;
    ESP_RETURN_ON_ERROR(i2c_param_config((i2c_port_t)m_port, &conf), TAG, "i2c_param_config");
    ESP_RETURN_ON_ERROR(i2c_driver_install((i2c_port_t)m_port, I2C_MODE_MASTER, 0, 0, 0), TAG, "i2c_driver_install");

    ESP_LOGI(TAG, "I2C ready: port=%d SDA=%d SCL=%d LCD_addr=0x%02X", m_port, m_sda, m_scl, m_addr);

    // Power-up settle
    vTaskDelay(pdMS_TO_TICKS(50));

    // If LCD not present, keep API no-op so app doesn't crash
    if (!i2c_device_present(m_addr)) {
        ESP_LOGW(TAG, "LCD not detected at 0x%02X; skipping init (no-op).", m_addr);
        m_inited = true;
        return ESP_OK;
    }

    // -------- AiP31068L init (per datasheet) --------
    const uint8_t contrast = 0x3F;        // 0..63; start max per your dim issue
    const uint8_t pwr_contrast_hi = 0x5F; // Bon=1, Ion=1, C5..C4=11b

    // Enter extended instruction set
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, 0x38), TAG, "FS IS0");
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, 0x39), TAG, "FS IS1");

    // Bias/OSC
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, 0x14), TAG, "Bias/OSC");

    // Contrast low 4 bits
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, (uint8_t)(0x70 | (contrast & 0x0F))), TAG, "Contrast low");

    // Power/Icon/Contrast high (booster/follower + C5..C4)
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, (uint8_t)(pwr_contrast_hi | ((contrast >> 4) & 0x03))), TAG, "Pwr/Contrast hi");

    // Follower ON, then wait long enough (datasheet ~>200 ms)
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, 0x6C), TAG, "Follower ON");
    vTaskDelay(pdMS_TO_TICKS(250));

    // Back to IS0 & display config
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, 0x38), TAG, "FS IS0");
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, 0x0C), TAG, "Display ON"); // D=1,C=0,B=0
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, 0x01), TAG, "Clear");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, 0x06), TAG, "Entry mode"); // I/D=1, S=0

    // Probe PCA9633 once (datasheet default 8-bit 0xC0 → 7-bit 0x60); allow 0x60–0x67
    m_rgb_probed  = true;
    m_rgb_present = false;
    for (uint8_t a = 0x60; a <= 0x67; ++a) {
        if (i2c_device_present(a)) {
            m_rgb_present = true;
            m_rgb_addr = a;
            ESP_LOGI(TAG, "RGB chip (PCA9633) detected at 0x%02X", m_rgb_addr);
            break;
        }
    }
    if (!m_rgb_present) {
        ESP_LOGW(TAG, "RGB chip not detected (0x60–0x67); backlight control disabled");
    }

    m_inited = true;
    return ESP_OK;
}

esp_err_t DFRobot_LCD::setCursor(uint8_t col, uint8_t row) {
    if (!m_inited) return ESP_ERR_INVALID_STATE;
    uint8_t addr = (row == 0) ? (uint8_t)(0x00 + col) : (uint8_t)(0x40 + col);
    return sendCommand((uint8_t)(0x80 | addr));  // Set DDRAM address
}

esp_err_t DFRobot_LCD::printstr(const char* s) {
    if (!m_inited) return ESP_ERR_INVALID_STATE;
    if (!s) return ESP_ERR_INVALID_ARG;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    size_t len = 0; while (p[len] != 0) ++len;
    return sendData(p, len);
}

esp_err_t DFRobot_LCD::clear() {
    esp_err_t err = sendCommand(0x01);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(2));
    return err;
}

esp_err_t DFRobot_LCD::home() {
    esp_err_t err = sendCommand(0x02);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(2));
    return err;
}

esp_err_t DFRobot_LCD::setRGB(uint8_t r, uint8_t g, uint8_t b) {
    if (!m_inited) return ESP_ERR_INVALID_STATE;

    if (!m_rgb_probed) {
        m_rgb_present = false;
        for (uint8_t a = 0x60; a <= 0x67; ++a) { if (i2c_device_present(a)) { m_rgb_present = true; m_rgb_addr = a; break; } }
        m_rgb_probed = true;
    }
    if (!m_rgb_present) return ESP_OK; // no backlight driver present -> no-op

    auto wr = [&](uint8_t reg, uint8_t val) -> esp_err_t {
        i2c_cmd_handle_t h = i2c_cmd_link_create();
        esp_err_t err = i2c_master_start(h);
        if (err == ESP_OK) err = i2c_master_write_byte(h, (m_rgb_addr << 1) | I2C_MASTER_WRITE, true);
        if (err == ESP_OK) err = i2c_master_write_byte(h, reg, true);
        if (err == ESP_OK) err = i2c_master_write_byte(h, val, true);
        if (err == ESP_OK) err = i2c_master_stop(h);
        if (err == ESP_OK) err = i2c_master_cmd_begin((i2c_port_t)m_port, h, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(h);
        return err;
    };

    // PCA9633: MODE1 normal, MODE2 OUTDRV=1 (totem-pole), LEDOUT per-channel PWM
    ESP_RETURN_ON_ERROR(wr(0x00, 0x00), TAG, "PCA9633 MODE1");
    ESP_RETURN_ON_ERROR(wr(0x01, 0x04), TAG, "PCA9633 MODE2 OUTDRV");
    ESP_RETURN_ON_ERROR(wr(0x08, 0xAA), TAG, "PCA9633 LEDOUT PWM");

    // Channel PWM (typical Waveshare mapping: B=PWM0(0x02), G=PWM1(0x03), R=PWM2(0x04))
    ESP_RETURN_ON_ERROR(wr(0x04, r), TAG, "PCA9633 R");
    ESP_RETURN_ON_ERROR(wr(0x03, g), TAG, "PCA9633 G");
    ESP_RETURN_ON_ERROR(wr(0x02, b), TAG, "PCA9633 B");

    return ESP_OK;
}

// --- wrappers ---
esp_err_t DFRobot_LCD::sendCommand(uint8_t cmd) {
    return i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, cmd);
}
esp_err_t DFRobot_LCD::sendData(const uint8_t* data, size_t len) {
    return i2c_write_data((i2c_port_t)m_port, m_addr, data, len);
}
