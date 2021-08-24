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
static const gpio_num_t GPIO_RST = 33;

static void hold_en_gpio_pins() {
    gpio_hold_en(GPIO_SD_IN1);
    gpio_hold_en(GPIO_SD_IN2);
    gpio_hold_en(GPIO_BSTC);
    gpio_hold_en(GPIO_S_OPEN);

    gpio_deep_sleep_hold_en();
}

static void hold_dis_gpio_pins() {
    gpio_hold_dis(GPIO_SD_IN1);
    gpio_hold_dis(GPIO_SD_IN2);
    gpio_hold_dis(GPIO_BSTC);
    gpio_hold_dis(GPIO_S_OPEN);
}

static void setup_gpio_pins() {
    gpio_pad_select_gpio(GPIO_SD_IN1);
    gpio_pad_select_gpio(GPIO_SD_IN2);
    gpio_pad_select_gpio(GPIO_BSTC);
    gpio_pad_select_gpio(GPIO_S_OPEN);
    gpio_pad_select_gpio(GPIO_HE_OUT);
    gpio_pad_select_gpio(GPIO_RST);

    gpio_set_direction(GPIO_SD_IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_SD_IN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_BSTC, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_S_OPEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_HE_OUT, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_RST, GPIO_MODE_INPUT);

    gpio_set_level(GPIO_SD_IN1, 0);
    gpio_set_level(GPIO_SD_IN2, 0);
    gpio_set_level(GPIO_BSTC, 0);
    gpio_set_level(GPIO_S_OPEN, 0);
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
    if (get_err != ESP_OK) {
        times_size = 0;
    }

    time_t now;
    time(&now);

    uint64_t sleep_time_secs = 0;

    // System time hasn't been configured properly
    if (now < 946684800) {
        // Wake up again in an hour to set system time
        sleep_time_secs = 3600;
        ESP_LOGI(TAG, "Sleep time: %llu", sleep_time_secs);
    } else {
        uint32_t start_up_time;
        if (time_index >= times_size) {
            // Finished watering plan for day; wake up at 01:30
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);

            uint32_t time_zone;
            get_err = storage_get_u32(STORAGE_TIME_ZONE, &time_zone);
            if (get_err == ESP_OK) {
                if (timeinfo.tm_hour >= time_zone) {
                    timeinfo.tm_mday += 1;
                }
                timeinfo.tm_sec = 0;
                timeinfo.tm_min = 30;
                timeinfo.tm_hour = time_zone + 1;
            } else {
                timeinfo.tm_hour += 1;
            }
            
            start_up_time = (uint32_t) mktime(&timeinfo);
        } else {
            char *time_key = malloc(strlen(STORAGE_TIMES_BASE));
            sprintf(time_key, STORAGE_TIMES_BASE, time_index);
            get_err = storage_get_u32(time_key, &start_up_time);
            ESP_ERROR_CHECK(get_err);
            free(time_key);
        }

        uint64_t sleep_time_secs = start_up_time - now;
        ESP_LOGI(TAG, "Sleep time: %d - %llu = %llu", start_up_time, (uint64_t) now, sleep_time_secs);
    }

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
    if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
        storage_reset();
    } else if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
        // System time hasn't been configured properly
        if (now < 946684800) {
            ESP_LOGI(TAG, "Starting system from deep sleep - system time has not been initially set");
            get_irrigation_settings();
        } else {
            uint32_t time_zone;
            esp_err_t get_err = storage_get_u32(STORAGE_TIME_ZONE, &time_zone);

            if (get_err == ESP_OK) {
                if ((timeinfo.tm_hour == time_zone || timeinfo.tm_hour == time_zone + 1) && (timeinfo.tm_min < 60)) {
                    // Within daily update period [00:00 - 1:59]
                    ESP_LOGI(TAG, "Starting system from deep sleep - fetching latest irrigation settings");
                    get_irrigation_settings();
                } else {
                    // Turn on/off solenoid
                    uint8_t time_index;
                    esp_err_t get_err = storage_get_u8(STORAGE_TIME_INDEX, &time_index);
                    ESP_ERROR_CHECK(get_err);

                    setup_gpio_pins();
                    hold_dis_gpio_pins();

                    if (time_index % 2 == 0) {
                        // Turn on solenoid
                        ESP_LOGI(TAG, "Starting system from deep sleep - turning on solenoid");
                        gpio_set_level(GPIO_BSTC, 1);

                        vTaskDelay(1000 / portTICK_RATE_MS);

                        gpio_set_level(GPIO_SD_IN1, 1);

                        vTaskDelay(100 / portTICK_RATE_MS);

                        gpio_set_level(GPIO_BSTC, 0);
                        gpio_set_level(GPIO_SD_IN1, 0);
                        gpio_set_level(GPIO_S_OPEN, 1);
                    } else {
                        // Turn off solenoid
                        ESP_LOGI(TAG, "Starting system from deep sleep - turning off solenoid");
                        gpio_set_level(GPIO_BSTC, 1);

                        vTaskDelay(1000 / portTICK_RATE_MS);

                        gpio_set_level(GPIO_SD_IN2, 1);

                        vTaskDelay(100 / portTICK_RATE_MS);

                        gpio_set_level(GPIO_BSTC, 0);
                        gpio_set_level(GPIO_SD_IN2, 0);
                        gpio_set_level(GPIO_S_OPEN, 0);
                    }

                    hold_en_gpio_pins();

                    time_index += 1;
                    storage_set_u8(STORAGE_TIME_INDEX, time_index);
                }
            } else {
                ESP_LOGI(TAG, "Starting system from deep sleep - didn't have initial server update, fetching latest irrigation settings");
                get_irrigation_settings();
            }
        }
    } else {
        // Did not wake from deep sleep [physical start of system]
        ESP_LOGI(TAG, "Starting system from physical start");
        storage_set_u8(STORAGE_VERSION, 6);

        setup_gpio_pins();
        hold_en_gpio_pins();
        get_irrigation_settings();
    }

    uint64_t sleep_time = determine_sleep_time();
    storage_deinit_nvs();
    esp_sleep_enable_ext0_wakeup(GPIO_RST, 1);
    esp_deep_sleep(sleep_time);
}
