#include "DFRobot_LCD.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "DFRobot_LCD";

// LCD + Backlight defaults commonly used by DFRobot RGB1602
// LCD controller (ST7032i-like) on 0x3E, backlight (PCA9633-like) on 0x62
static constexpr uint8_t LCD_I2C_ADDR      = 0x3E;  // change if your board differs
static constexpr uint8_t RGB_I2C_ADDR      = 0x62;  // change if your board differs

// ST7032i control bytes
static constexpr uint8_t CTRL_CMD  = 0x00;  // following byte(s) are command(s)
static constexpr uint8_t CTRL_DATA = 0x40;  // following byte(s) are data (DDRAM)

// Simple helpers to write one byte command or a data buffer
static esp_err_t i2c_write_cmd_byte(i2c_port_t port, uint8_t dev, uint8_t cmd)
{
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(h);
    if (err == ESP_OK) err = i2c_master_write_byte(h, (dev << 1) | I2C_MASTER_WRITE, true);
    if (err == ESP_OK) err = i2c_master_write_byte(h, CTRL_CMD, true);
    if (err == ESP_OK) err = i2c_master_write_byte(h, cmd, true);
    if (err == ESP_OK) err = i2c_master_stop(h);
    if (err == ESP_OK) err = i2c_master_cmd_begin(port, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "i2c_write_cmd_byte(0x%02X) failed: %s", cmd, esp_err_to_name(err));
    return err;
}

static esp_err_t i2c_write_data(i2c_port_t port, uint8_t dev, const uint8_t* data, size_t len)
{
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(h);
    if (err == ESP_OK) err = i2c_master_write_byte(h, (dev << 1) | I2C_MASTER_WRITE, true);
    if (err == ESP_OK) err = i2c_master_write_byte(h, CTRL_DATA, true);
    if (err == ESP_OK) err = i2c_master_write(h, (uint8_t*)data, len, true);
    if (err == ESP_OK) err = i2c_master_stop(h);
    if (err == ESP_OK) err = i2c_master_cmd_begin(port, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "i2c_write_data(len=%u) failed: %s", (unsigned)len, esp_err_to_name(err));
    return err;
}

// ---- Class ----

DFRobot_LCD::DFRobot_LCD(int i2c_port, int sda_pin, int scl_pin, uint8_t i2c_addr)
: m_port(i2c_port), m_sda(sda_pin), m_scl(scl_pin), m_addr(i2c_addr ? i2c_addr : LCD_I2C_ADDR), m_inited(false) {}

esp_err_t DFRobot_LCD::init() {
    // I2C config
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)m_sda;
    conf.scl_io_num = (gpio_num_t)m_scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000; // 100kHz to be safe
    ESP_RETURN_ON_ERROR(i2c_param_config((i2c_port_t)m_port, &conf), TAG, "i2c_param_config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install((i2c_port_t)m_port, I2C_MODE_MASTER, 0, 0, 0), TAG, "i2c_driver_install failed");

    ESP_LOGI(TAG, "I2C ready: port=%d SDA=%d SCL=%d LCD_addr=0x%02X", m_port, m_sda, m_scl, m_addr);

    // ---- LCD init sequence for ST7032i-like controller ----
    // Reference sequence (common working set):
    // Function Set: 0x38 (8-bit, 2 lines, normal instr)
    // Function Set: 0x39 (extend instr)
    // Bias/OSC:     0x14
    // Contrast low: 0x70 | (contrast & 0x0F)
    // Power/Icon/Contrast high: 0x5C | ((contrast >> 4) & 0x03)  (typ 0x5C..0x5F)
    // Follower:     0x6C
    // wait 200ms
    // Function Set: 0x38 (back to normal)
    // Display ON:   0x0C
    // Clear:        0x01
    // Entry Mode:   0x06

    uint8_t contrast = 0x28; // reasonable mid contrast; adjust later if needed

    ESP_RETURN_ON_ERROR(sendCommand(0x38), TAG, "FS 0x38 failed");
    ESP_RETURN_ON_ERROR(sendCommand(0x39), TAG, "FS 0x39 failed");
    ESP_RETURN_ON_ERROR(sendCommand(0x14), TAG, "Bias/OSC failed");
    ESP_RETURN_ON_ERROR(sendCommand(uint8_t(0x70 | (contrast & 0x0F))), TAG, "Contrast low failed");
    ESP_RETURN_ON_ERROR(sendCommand(uint8_t(0x5C | ((contrast >> 4) & 0x03))), TAG, "Power/contrast high failed");
    ESP_RETURN_ON_ERROR(sendCommand(0x6C), TAG, "Follower failed");
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_RETURN_ON_ERROR(sendCommand(0x38), TAG, "FS back failed");
    ESP_RETURN_ON_ERROR(sendCommand(0x0C), TAG, "Display ON failed");
    ESP_RETURN_ON_ERROR(clear(),           TAG, "Clear failed");
    ESP_RETURN_ON_ERROR(sendCommand(0x06), TAG, "Entry mode failed");

    m_inited = true;
    return ESP_OK;
}

esp_err_t DFRobot_LCD::setCursor(uint8_t col, uint8_t row) {
    if (!m_inited) return ESP_ERR_INVALID_STATE;
    // DDRAM base addresses: row0=0x00, row1=0x40
    uint8_t addr = (row == 0) ? (0x00 + col) : (0x40 + col);
    return sendCommand(uint8_t(0x80 | addr)); // Set DDRAM address
}

esp_err_t DFRobot_LCD::printstr(const char* s) {
    if (!m_inited) return ESP_ERR_INVALID_STATE;
    if (!s) return ESP_ERR_INVALID_ARG;
    // Send as data bytes
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    size_t len = 0;
    while (p[len] != 0) ++len;
    return sendData(p, len);
}

esp_err_t DFRobot_LCD::clear() {
    if (!m_inited) return ESP_ERR_INVALID_STATE;
    esp_err_t err = sendCommand(0x01);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(2)); // clear needs >1.5ms
    return err;
}

esp_err_t DFRobot_LCD::home() {
    if (!m_inited) return ESP_ERR_INVALID_STATE;
    esp_err_t err = sendCommand(0x02);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(2)); // home needs >1.5ms
    return err;
}

esp_err_t DFRobot_LCD::setRGB(uint8_t r, uint8_t g, uint8_t b) {
    if (!m_inited) return ESP_ERR_INVALID_STATE;
    // Simple PCA9633-style: MODE1=0x00, LEDOUT=0x08 (individual PWM), PWM0..2 set
    // Some modules wire R,G,B to PWM2,PWM1,PWM0—common mapping below, adjust if colors look swapped.
    auto wr = [&](uint8_t reg, uint8_t val) -> esp_err_t {
        i2c_cmd_handle_t h = i2c_cmd_link_create();
        esp_err_t err = i2c_master_start(h);
        if (err == ESP_OK) err = i2c_master_write_byte(h, (RGB_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        if (err == ESP_OK) err = i2c_master_write_byte(h, reg, true);
        if (err == ESP_OK) err = i2c_master_write_byte(h, val, true);
        if (err == ESP_OK) err = i2c_master_stop(h);
        if (err == ESP_OK) err = i2c_master_cmd_begin((i2c_port_t)m_port, h, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(h);
        return err;
    };

    // Init mode & outputs (idempotent)
    ESP_RETURN_ON_ERROR(wr(0x00, 0x00), TAG, "RGB MODE1");
    ESP_RETURN_ON_ERROR(wr(0x08, 0xAA), TAG, "RGB LEDOUT"); // 0b10101010 -> PWM on outputs

    // Set PWM (registers 0x02,0x03,0x04 are common; some boards use 0x04,0x03,0x02)
    // Try RGB = PWM2,PWM1,PWM0 mapping first (adjust if colors swapped).
    ESP_RETURN_ON_ERROR(wr(0x04, r), TAG, "RGB R");
    ESP_RETURN_ON_ERROR(wr(0x03, g), TAG, "RGB G");
    ESP_RETURN_ON_ERROR(wr(0x02, b), TAG, "RGB B");

    return ESP_OK;
}

// --- low-level wrappers using helpers above ---
esp_err_t DFRobot_LCD::sendCommand(uint8_t cmd) {
    return i2c_write_cmd_byte((i2c_port_t)m_port, m_addr, cmd);
}

esp_err_t DFRobot_LCD::sendData(const uint8_t* data, size_t len) {
    return i2c_write_data((i2c_port_t)m_port, m_addr, data, len);
}
