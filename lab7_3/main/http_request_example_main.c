/* Lab 7.3 - Weather Station Integration
   Combines GET and POST requests:
   1. GET location from server
   2. GET weather from wttr.in
   3. Read local temperature
   4. POST both temperatures to server
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

static const char *TAG = "lab7_3";
static temperature_sensor_handle_t temp_sensor = NULL;

/* Helper function to perform HTTP GET request */
static int http_get(const char *host, const char *port, const char *path, char *response, size_t response_size)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    int s, r;
    char request[512];

    // Prepare GET request
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s:%s\r\n"
        "User-Agent: curl/7.68.0\r\n"
        "\r\n",
        path, host, port);

    // DNS lookup
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "GET: DNS lookup failed for %s", host);
        return -1;
    }

    // Create socket and connect
    s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
        ESP_LOGE(TAG, "GET: Failed to allocate socket");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "GET: Socket connect failed errno=%d", errno);
        close(s);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    // Send request
    if (write(s, request, strlen(request)) < 0) {
        ESP_LOGE(TAG, "GET: Socket send failed");
        close(s);
        return -1;
    }

    // Read response
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    bzero(response, response_size);
    int total_read = 0;
    while (total_read < response_size - 1) {
        r = read(s, response + total_read, response_size - total_read - 1);
        if (r <= 0) break;
        total_read += r;
    }

    close(s);
    return total_read;
}

/* Helper function to perform HTTP POST request */
static int http_post(const char *host, const char *port, const char *path, const char *payload, char *response, size_t response_size)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    int s, r;
    char request[1024];

    // Prepare POST request
    snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        path, host, port, strlen(payload), payload);

    // DNS lookup
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "POST: DNS lookup failed for %s", host);
        return -1;
    }

    // Create socket and connect
    s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
        ESP_LOGE(TAG, "POST: Failed to allocate socket");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "POST: Socket connect failed errno=%d", errno);
        close(s);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    // Send request
    if (write(s, request, strlen(request)) < 0) {
        ESP_LOGE(TAG, "POST: Socket send failed");
        close(s);
        return -1;
    }

    // Read response
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    bzero(response, response_size);
    r = read(s, response, response_size - 1);

    close(s);
    return r;
}

/* Extract body from HTTP response */
static char* extract_body(char *response)
{
    char *body = strstr(response, "\r\n\r\n");
    if (body) {
        return body + 4;
    }
    return NULL;
}

static void weather_station_task(void *pvParameters)
{
    char response[2048];
    char location[128];
    char weather_path[256];
    char outdoor_temp[64];
    char payload[512];
    char *body;

    while(1) {
        ESP_LOGI(TAG, "=== Starting Weather Station Cycle ===");

        // Step 1: GET location from server
        ESP_LOGI(TAG, "Step 1: Getting location from server...");
        if (http_get(SERVER_IP, SERVER_PORT, "/location", response, sizeof(response)) > 0) {
            body = extract_body(response);
            if (body) {
                // Remove newlines and extra whitespace
                int i = 0;
                while (body[i] && body[i] != '\n' && body[i] != '\r' && i < sizeof(location) - 1) {
                    location[i] = body[i];
                    i++;
                }
                location[i] = '\0';
                ESP_LOGI(TAG, "Server location: %s", location);
            } else {
                ESP_LOGE(TAG, "Failed to extract location from response");
                vTaskDelay(10000 / portTICK_PERIOD_MS);
                continue;
            }
        } else {
            ESP_LOGE(TAG, "Failed to get location from server");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        // Step 2: GET weather from wttr.in for that location
        ESP_LOGI(TAG, "Step 2: Getting weather for %s from wttr.in...", location);
        snprintf(weather_path, sizeof(weather_path), "/%s?format=%%t", location);
        if (http_get("wttr.in", "80", weather_path, response, sizeof(response)) > 0) {
            body = extract_body(response);
            if (body) {
                // Extract temperature (remove newlines)
                int i = 0;
                while (body[i] && body[i] != '\n' && body[i] != '\r' && i < sizeof(outdoor_temp) - 1) {
                    outdoor_temp[i] = body[i];
                    i++;
                }
                outdoor_temp[i] = '\0';
                ESP_LOGI(TAG, "Outdoor temperature: %s", outdoor_temp);
            } else {
                ESP_LOGE(TAG, "Failed to extract weather from response");
                strcpy(outdoor_temp, "N/A");
            }
        } else {
            ESP_LOGE(TAG, "Failed to get weather from wttr.in");
            strcpy(outdoor_temp, "N/A");
        }

        // Step 3: Read local temperature sensor
        ESP_LOGI(TAG, "Step 3: Reading ESP32 temperature sensor...");
        float sensor_temp = 0;
        esp_err_t err = temperature_sensor_get_celsius(temp_sensor, &sensor_temp);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read temperature sensor");
            sensor_temp = 0.0;
        }
        ESP_LOGI(TAG, "ESP32 sensor temperature: %.2f°C", sensor_temp);

        // Step 4: POST both temperatures to server
        ESP_LOGI(TAG, "Step 4: Posting data to server...");
        snprintf(payload, sizeof(payload),
            "Location: %s\n"
            "Outdoor Temperature: %s\n"
            "ESP32 Sensor Temperature: %.2f C",
            location, outdoor_temp, sensor_temp);

        if (http_post(SERVER_IP, SERVER_PORT, "/", payload, response, sizeof(response)) > 0) {
            ESP_LOGI(TAG, "Successfully posted data to server");
            ESP_LOGI(TAG, "Server response: %s", response);
        } else {
            ESP_LOGE(TAG, "Failed to post data to server");
        }

        // Log summary
        ESP_LOGI(TAG, "\n=== Weather Station Summary ===");
        ESP_LOGI(TAG, "Location: %s", location);
        ESP_LOGI(TAG, "Outdoor: %s", outdoor_temp);
        ESP_LOGI(TAG, "ESP32: %.2f°C", sensor_temp);
        ESP_LOGI(TAG, "================================\n");

        // Wait 10 seconds before next cycle
        for(int countdown = 10; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "Next cycle in %d seconds...", countdown);
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

    xTaskCreate(&weather_station_task, "weather_station_task", 16384, NULL, 5, NULL);
}
