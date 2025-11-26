/* HTTP POST Example - Lab 7.2
   Posts ESP32 temperature sensor data to a server
*/
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "driver/temperature_sensor.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sdkconfig.h"

/* Server configuration - CHANGE THIS to your server's IP */
#define SERVER_IP "192.168.43.1"  // Replace with your phone/laptop IP
#define SERVER_PORT "1234"

static const char *TAG = "lab7_2";
static temperature_sensor_handle_t temp_sensor = NULL;

static void http_post_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[512];
    char request[512];
    char payload[128];

    while(1) {
        // Read temperature from sensor
        float temperature = 0;
        esp_err_t err = temperature_sensor_get_celsius(temp_sensor, &temperature);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read temperature sensor");
            temperature = 0.0;
        }

        ESP_LOGI(TAG, "ESP32 Temperature: %.2f°C", temperature);

        // Prepare POST payload
        snprintf(payload, sizeof(payload), "ESP32_Temperature: %.2f C", temperature);

        // Prepare HTTP POST request
        snprintf(request, sizeof(request),
            "POST / HTTP/1.1\r\n"
            "Host: %s:%s\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s",
            SERVER_IP, SERVER_PORT, strlen(payload), payload);

        // DNS lookup
        int dns_err = getaddrinfo(SERVER_IP, SERVER_PORT, &hints, &res);

        if(dns_err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", dns_err, res);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "Server IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "Socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "Connected to server");
        freeaddrinfo(res);

        if (write(s, request, strlen(request)) < 0) {
            ESP_LOGE(TAG, "Socket send failed");
            close(s);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "POST request sent");

        // Read response
        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout, sizeof(receiving_timeout));

        bzero(recv_buf, sizeof(recv_buf));
        r = read(s, recv_buf, sizeof(recv_buf)-1);
        if (r > 0) {
            ESP_LOGI(TAG, "Server response: %s", recv_buf);
        }

        close(s);

        // Wait 5 seconds before next POST
        for(int countdown = 5; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "Next POST in %d seconds...", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize temperature sensor */
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
    ESP_LOGI(TAG, "Temperature sensor initialized");

    /* Connect to WiFi */
    ESP_ERROR_CHECK(example_connect());

    xTaskCreate(&http_post_task, "http_post_task", 8192, NULL, 5, NULL);
}
