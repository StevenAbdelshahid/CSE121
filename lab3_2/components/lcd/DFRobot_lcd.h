#pragma once
#include <stdint.h>
#include "esp_err.h"

class DFRobot_LCD {
public:
    DFRobot_LCD(int i2c_port, int sda_pin, int scl_pin, uint8_t i2c_addr);
    esp_err_t init();
    esp_err_t setCursor(uint8_t col, uint8_t row);
    esp_err_t printstr(const char* s);
    esp_err_t clear();
    esp_err_t home();
    esp_err_t setRGB(uint8_t r, uint8_t g, uint8_t b);

private:
    int      m_port;
    int      m_sda;
    int      m_scl;
    uint8_t  m_addr;
    bool     m_inited;

    esp_err_t sendCommand(uint8_t cmd);
    esp_err_t sendData(const uint8_t* data, size_t len);
};

