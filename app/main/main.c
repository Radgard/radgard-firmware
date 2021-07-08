/*
    Radgard Main
*/

#include <stdio.h>

#include <esp_log.h>
#include <esp_sleep.h>
#include "esp_sntp.h"

#include "network.h"
#include "api.h"

static const char *TAG = "main";

static void get_irrigation_settings() {
    network_start_provision_connect_wifi();
    api_get_irrigation_settings();
    network_disconnect_wifi();
}

void app_main(void) {
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    time_t now;
    time(&now);
    ESP_LOGI(TAG, "Current time: %ld", now);

    if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
        // Woke up from deep sleep

    } else {
        // Physical turn on
        get_irrigation_settings();
    }
    
    esp_deep_sleep(10 * 1000000);
}
