/*
    Radgard Main
*/

#include <string.h>
#include <stdio.h>

#include <esp_log.h>
#include <esp_sleep.h>
#include "esp_sntp.h"

#include "driver/gpio.h"

#include "network.h"
#include "storage.h"
#include "api.h"

static const char *TAG = "main";

static const gpio_num_t GPIO_SD_IN1 = 18;
static const gpio_num_t GPIO_SD_IN2 = 19;
static const gpio_num_t GPIO_BSTC = 5;
static const gpio_num_t GPIO_S_OPEN = 23;
static const gpio_num_t GPIO_HE_OUT = 32;

static uint32_t solenoid_status() {
    // TODO: Read HE_OUT to determine S_OPEN state
    return 0;
}

static void setup_gpio_pins() {
    gpio_pad_select_gpio(GPIO_SD_IN1);
    gpio_pad_select_gpio(GPIO_SD_IN2);
    gpio_pad_select_gpio(GPIO_BSTC);
    gpio_pad_select_gpio(GPIO_S_OPEN);
    gpio_pad_select_gpio(GPIO_HE_OUT);

    gpio_set_direction(GPIO_SD_IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_SD_IN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_BSTC, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_S_OPEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_HE_OUT, GPIO_MODE_INPUT);

    gpio_set_level(GPIO_SD_IN1, 0);
    gpio_set_level(GPIO_SD_IN2, 0);
    gpio_set_level(GPIO_BSTC, 0);
    gpio_set_level(GPIO_S_OPEN, solenoid_status());
    
    gpio_hold_en(GPIO_SD_IN1);
    gpio_hold_en(GPIO_SD_IN2);
    gpio_hold_en(GPIO_BSTC);
    gpio_hold_en(GPIO_S_OPEN);

    gpio_deep_sleep_hold_en();
}

static void get_irrigation_settings() {
    network_start_provision_connect_wifi();
    api_get_irrigation_settings();
    network_disconnect_wifi();
}

static uint64_t determine_sleep_time() {
    uint8_t time_index;
    esp_err_t get_err = storage_get_u8(STORAGE_TIME_INDEX, &time_index);
    ESP_ERROR_CHECK(get_err);

    uint32_t times_size;
    get_err = storage_get_u32(STORAGE_TIMES_LENGTH, &times_size);
    ESP_ERROR_CHECK(get_err);

    time_t now;
    time(&now);

    uint32_t start_up_time;
    if (time_index >= times_size) {
        // Finished watering plan for day; wake up at 24:00
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        uint32_t time_zone;
        get_err = storage_get_u32(STORAGE_TIME_ZONE, &time_zone);
        ESP_ERROR_CHECK(get_err);
        
        if (timeinfo.tm_hour >= time_zone) {
            timeinfo.tm_mday += 1;
        }
        timeinfo.tm_sec = 0;
        timeinfo.tm_min = 0;
        timeinfo.tm_hour = time_zone;
        
        start_up_time = (uint32_t) mktime(&timeinfo);
    } else {
        char *time_key = malloc(strlen(STORAGE_TIMES_BASE));
        sprintf(time_key, STORAGE_TIMES_BASE, time_index);
        get_err = storage_get_u32(time_key, &start_up_time);
        ESP_ERROR_CHECK(get_err);
        free(time_key);
    }

    uint64_t sleep_time_secs = start_up_time - now;
    ESP_LOGI(TAG, "Sleep time: %llu", sleep_time_secs);

    return sleep_time_secs * 1000000;
}

void app_main(void) {
    storage_init_nvs();

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Current time: %ld, hour: %d, minute: %d", now, timeinfo.tm_hour, timeinfo.tm_min);

    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    if (wakeup_cause != ESP_SLEEP_WAKEUP_TIMER) {
        // Did not wake from deep sleep [physical start of system]
        setup_gpio_pins();
        get_irrigation_settings();
    } else {
        uint32_t time_zone;
        esp_err_t get_err = storage_get_u32(STORAGE_TIME_ZONE, &time_zone);
        ESP_ERROR_CHECK(get_err);

        if ((timeinfo.tm_hour == (time_zone - 1) % 24 && timeinfo.tm_min >= 58) || (timeinfo.tm_hour == time_zone && timeinfo.tm_min <= 2)) {
            // Within daily update period [23:58 - 00:02]
            get_irrigation_settings();
        } else {
            // Turn on/off solenoid
            uint8_t time_index;
            esp_err_t get_err = storage_get_u8(STORAGE_TIME_INDEX, &time_index);
            ESP_ERROR_CHECK(get_err);

            if (time_index % 2 == 0) {
                // Turn on solenoid
                gpio_set_level(GPIO_BSTC, 1);

                vTaskDelay(250 / portTICK_RATE_MS);

                gpio_set_level(GPIO_SD_IN1, 1);

                vTaskDelay(250 / portTICK_RATE_MS);

                gpio_set_level(GPIO_BSTC, 0);
                gpio_set_level(GPIO_SD_IN1, 0);
            } else {
                // Turn off solenoid
                gpio_set_level(GPIO_BSTC, 1);

                vTaskDelay(250 / portTICK_RATE_MS);

                gpio_set_level(GPIO_SD_IN2, 1);

                vTaskDelay(250 / portTICK_RATE_MS);

                gpio_set_level(GPIO_BSTC, 0);
                gpio_set_level(GPIO_SD_IN2, 0);
            }

            gpio_set_level(GPIO_BSTC, solenoid_status());

            time_index += 1;
            storage_set_u8(STORAGE_TIME_INDEX, time_index);
        }
    }

    //uint64_t sleep_time = determine_sleep_time();
    storage_deinit_nvs();
    esp_deep_sleep(30 * 1000000);
}
