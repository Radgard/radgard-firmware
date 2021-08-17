/*
    Radgard Test Main
*/

#include <string.h>
#include <stdio.h>

#include <esp_sleep.h>
#include "esp_sntp.h"

#include "driver/gpio.h"

#include "network.h"

static const gpio_num_t GPIO_SD_IN1 = 18;
static const gpio_num_t GPIO_SD_IN2 = 19;
static const gpio_num_t GPIO_BSTC = 5;
static const gpio_num_t GPIO_S_OPEN = 23;
static const gpio_num_t GPIO_HE_OUT = 32;
static const gpio_num_t GPIO_RST = 33;

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

void app_main(void) {
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
        // Move to production firmware
        network_connect_wifi_update_firmware();
    } else {
        setup_gpio_pins();
        
        // Wait 5 seconds
        vTaskDelay(5000 / portTICK_RATE_MS);
        
        // Turn on solenoid
        gpio_set_level(GPIO_BSTC, 1);

        vTaskDelay(1000 / portTICK_RATE_MS);

        gpio_set_level(GPIO_SD_IN1, 1);

        vTaskDelay(100 / portTICK_RATE_MS);

        gpio_set_level(GPIO_BSTC, 0);
        gpio_set_level(GPIO_SD_IN1, 0);
        gpio_set_level(GPIO_S_OPEN, 1);

        // Wait 5 seconds
        vTaskDelay(5000 / portTICK_RATE_MS);

        // Turn off solenoid
        gpio_set_level(GPIO_BSTC, 1);

        vTaskDelay(1000 / portTICK_RATE_MS);

        gpio_set_level(GPIO_SD_IN2, 1);

        vTaskDelay(100 / portTICK_RATE_MS);

        gpio_set_level(GPIO_BSTC, 0);
        gpio_set_level(GPIO_SD_IN2, 0);
        gpio_set_level(GPIO_S_OPEN, 0);
    }

    esp_sleep_enable_ext0_wakeup(GPIO_RST, 1);
    esp_deep_sleep_start();
}