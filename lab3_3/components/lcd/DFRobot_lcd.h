#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

class DFRobot_LCD {
public:
    // i2c_port: I2C_NUM_0 or I2C_NUM_1; SDA/SCL: GPIO pins; i2c_addr: LCD 7-bit addr (default 0x3E)
    DFRobot_LCD(int i2c_port, int sda_pin, int scl_pin, uint8_t i2c_addr = 0x3E);

    esp_err_t init();                      // Init I2C + AiP31068L LCD + (optional) PCA9633 backlight
    esp_err_t setCursor(uint8_t col, uint8_t row);
    esp_err_t printstr(const char* s);
    esp_err_t clear();
    esp_err_t home();
    esp_err_t setRGB(uint8_t r, uint8_t g, uint8_t b);

private:
    esp_err_t sendCommand(uint8_t cmd);
    esp_err_t sendData(const uint8_t* data, size_t len);
    bool i2c_device_present(uint8_t addr) const;

    int m_port;
    int m_sda;
    int m_scl;
    uint8_t m_addr;        // LCD 7-bit addr (AiP31068L = 0x3E)
    bool m_inited;
    bool m_rgb_present = false;
    bool m_rgb_probed  = false;
    uint8_t m_rgb_addr = 0x60;   // PCA9633 7-bit addr (datasheet 0xC0 >> 1). Will be probed 0x60–0x67.
};
