
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DFRobot_LCD.h"
#include "esp_log.h"
#include "driver/i2c.h"


static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
static constexpr int SDA_PIN  = 4;   // change later to your wiring
static constexpr int SCL_PIN  = 5;   // change later to your wiring
static constexpr uint8_t LCD_ADDR = 0x3E; // common for RGB1602; verify later

extern "C" void app_main(void) {
    ESP_LOGI("APP", "Starting lab3_2");
    DFRobot_LCD lcd(I2C_PORT, SDA_PIN, SCL_PIN, LCD_ADDR);
    lcd.init();
    lcd.setRGB(0, 128, 255);
    lcd.setCursor(0, 0);
    lcd.printstr("Hello CSE121!");
    lcd.setCursor(0, 1);
    lcd.printstr("Abdelshahid");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
