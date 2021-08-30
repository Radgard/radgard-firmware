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

static uint32_t get_irrigation_fetch_time(uint32_t time_zone) {
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_hour > time_zone + 1 || (timeinfo.tm_hour == time_zone + 1 && timeinfo.tm_min >= 30)) {
        timeinfo.tm_mday += 1;
    }
    timeinfo.tm_sec = 0;
    timeinfo.tm_min = 30;
    timeinfo.tm_hour = time_zone + 1;

    uint32_t start_up_time = (uint32_t) mktime(&timeinfo);

    return start_up_time;
}

static uint64_t determine_sleep_time() {
    uint64_t sleep_time_secs = 0;

    time_t now;
    struct tm timeinfo;

    time(&now);

    uint32_t time_zone;
    esp_err_t get_err = storage_get_u32(STORAGE_TIME_ZONE, &time_zone);

    // System time hasn't been configured properly
    if (now < 946684800 || get_err != ESP_OK) {
        // Wake up again in 30 minutes to set system time or get time zone
        sleep_time_secs = 1800;
        ESP_LOGI(TAG, "Sleep time: %llu", sleep_time_secs);
    } else {
        now -= time_zone * 3600;

        localtime_r(&now, &timeinfo);

        uint8_t day = timeinfo.tm_wday;
        char *day_times_key = malloc(strlen(STORAGE_TIME_BASE));
        sprintf(day_times_key, STORAGE_TIME_BASE, day);

        size_t day_times_size = 0;
        get_err = storage_get_blob_size(day_times_key, &day_times_size);

        size_t sig_rains_size = 0;
        get_err = storage_get_blob_size(STORAGE_SIG_RAINS, &sig_rains_size);

        if (day_times_size != 0 && sig_rains_size != 0) {
            uint32_t *day_times = malloc(day_times_size);
            esp_err_t day_times_get_err = storage_get_blob(day_times_key, day_times, &day_times_size);

            uint8_t *sig_rains = malloc(sig_rains_size);
            esp_err_t sig_rains_get_err = storage_get_blob(STORAGE_SIG_RAINS, sig_rains, &sig_rains_size);

            if (day_times_get_err == ESP_OK && sig_rains_get_err == ESP_OK) {
                uint8_t day_times_length = day_times_size / sizeof(uint32_t);

                time(&now);
                localtime_r(&now, &timeinfo);

                if (timeinfo.tm_hour < time_zone) {
                    timeinfo.tm_mday -= 1;
                }

                timeinfo.tm_sec = 0;
                timeinfo.tm_min = 0;
                timeinfo.tm_hour = time_zone;

                uint32_t day_start = (uint32_t) mktime(&timeinfo);

                uint32_t start_up_time = 0;
                for (int i = 0; i < day_times_length; i++) {
                    if (now + 5 < day_start + day_times[i]) {
                        start_up_time = day_start + day_times[i];

                        if (i % 2 == 0) {
                            storage_set_u8(STORAGE_SOLENOID_OPEN, 1);
                        } else {
                            storage_set_u8(STORAGE_SOLENOID_OPEN, 0);
                        }

                        break;
                    }
                }

                // Wake up at 01:30 for irrigation fetch
                if (start_up_time == 0 || sig_rains[day]) {
                    start_up_time = get_irrigation_fetch_time(time_zone);

                    if (sig_rains[day]) {
                        sig_rains[day] = 0;
                        storage_set_blob(STORAGE_SIG_RAINS, sig_rains, sig_rains_size);
                    }
                }

                sleep_time_secs = start_up_time - now;
                ESP_LOGI(TAG, "Sleep time: %d - %llu = %llu", start_up_time, (uint64_t) now, sleep_time_secs);
            } else {
                // Wake up again in 30 minutes to get irrigation settings
                storage_remove(STORAGE_SOLENOID_OPEN);
                sleep_time_secs = 1800;
                ESP_LOGI(TAG, "Sleep time: %llu", sleep_time_secs);
            }

            free(day_times);
            free(sig_rains);
        } else {
            time(&now);
            localtime_r(&now, &timeinfo);

            uint32_t start_up_time = get_irrigation_fetch_time(time_zone);

            sleep_time_secs = start_up_time - now;
            ESP_LOGI(TAG, "Sleep time: %d - %llu = %llu", start_up_time, (uint64_t) now, sleep_time_secs);
        }

        free(day_times_key);
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
                if (timeinfo.tm_hour == time_zone || (timeinfo.tm_hour == time_zone + 1 && timeinfo.tm_min < 60) || (timeinfo.tm_hour == time_zone + 2 && timeinfo.tm_min <= 30)) {
                    // Within daily update period [00:00 - 2:30]
                    ESP_LOGI(TAG, "Starting system from deep sleep - fetching latest irrigation settings");
                    get_irrigation_settings();
                } else {
                    // Turn on/off solenoid
                    uint8_t solenoid_open;
                    esp_err_t get_err = storage_get_u8(STORAGE_SOLENOID_OPEN, &solenoid_open);

                    if (get_err == ESP_OK) {
                        setup_gpio_pins();
                        hold_dis_gpio_pins();

                        if (solenoid_open) {
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
                    } else {
                        ESP_LOGI(TAG, "Starting system from deep sleep - didn't have solenoid configuration");
                        get_irrigation_settings();
                    }
                }
            } else {
                ESP_LOGI(TAG, "Starting system from deep sleep - didn't have initial server update, fetching latest irrigation settings");
                get_irrigation_settings();
            }
        }
    } else {
        // Did not wake from deep sleep [physical start of system]
        ESP_LOGI(TAG, "Starting system from physical start");
        storage_set_u8(STORAGE_VERSION, 9);

        setup_gpio_pins();
        hold_en_gpio_pins();
        get_irrigation_settings();
    }

    uint64_t sleep_time = determine_sleep_time();
    storage_deinit_nvs();
    esp_sleep_enable_ext0_wakeup(GPIO_RST, 1);
    esp_deep_sleep(sleep_time);
}
