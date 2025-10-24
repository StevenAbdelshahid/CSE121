#include <stdio.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DFRobot_LCD.h"
#include "esp_log.h"

// Match your wiring (ESP32-C3 devkit):
static constexpr int I2C_PORT = I2C_NUM_0;
static constexpr int SDA_PIN  = 10;   // SDA
static constexpr int SCL_PIN  = 8;    // SCL
static constexpr uint8_t LCD_ADDR = 0x3E; // AiP31068L 7-bit

extern "C" void app_main(void) {
    ESP_LOGI("APP", "Starting lab3_2");

    DFRobot_LCD lcd(I2C_PORT, SDA_PIN, SCL_PIN, LCD_ADDR);
    lcd.init();

    // Backlight color (no-op if PCA9633 not found)
    lcd.setRGB(0, 128, 255);

    // Lab 3.2 text
    lcd.setCursor(0, 0);
    lcd.printstr("Hello CSE121!");
    lcd.setCursor(0, 1);
    lcd.printstr("Abdelshahid");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
