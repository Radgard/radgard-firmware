/*
    Radgard Main
*/

#include <stdio.h>

#include <esp_log.h>
#include <esp_sleep.h>
#include "esp_sntp.h"

#include "network.h"
#include "storage.h"
#include "api.h"

static const char *TAG = "main";

static void get_irrigation_settings() {
    network_start_provision_connect_wifi();
    api_get_irrigation_settings();
    network_disconnect_wifi();
}

static void determine_sleep_time() {
    uint64_t sleep_time_secs = 10;
    esp_deep_sleep(sleep_time_secs * 1000000);
}

void app_main(void) {
    storage_init_nvs();

    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Current time: %ld, hour: %d, minute: %d", now, timeinfo.tm_hour, timeinfo.tm_min);

    uint32_t time_zone;
    esp_err_t get_err = storage_get_u32(STORAGE_TIME_ZONE, &time_zone);
    if (get_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting time_zone from storage: %s", esp_err_to_name(get_err));
        time_zone = 0;
    }

    if (wakeup_cause != ESP_SLEEP_WAKEUP_TIMER) {
        // Did not wake from deep sleep [physical start of system]
        get_irrigation_settings();
    } else if ((timeinfo.tm_hour == (time_zone - 1) % 24 && timeinfo.tm_min >= 58) || (timeinfo.tm_hour == time_zone && timeinfo.tm_min <= 2)) {
        // Within daily update period [23:58 - 00:02]
        get_irrigation_settings();
    } else {
        // Turn on/off solenoid
    }

    determine_sleep_time();

    storage_deinit_nvs();
}
