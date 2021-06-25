#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_log.h>
#include "esp_system.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "storage.h"

static const char *TAG = "storage";

static const char *HANDLE_NAME = "storage";

void storage_init_nvs() {
    /* Initialize NVS partition */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

esp_err_t storage_set(char *key, char *value) {
    nvs_handle handle;
    esp_err_t open_err = nvs_open(HANDLE_NAME, NVS_READWRITE, &handle);

    if (open_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open NVS handle %s; Error: %s", HANDLE_NAME, esp_err_to_name(open_err));

        return open_err;
    }

    esp_err_t set_err = nvs_set_str(handle, key, value);

    nvs_close(handle);

    return set_err;
}

esp_err_t storage_get(char *key, char *value, size_t *size) {
    nvs_handle handle;
    esp_err_t open_err = nvs_open(HANDLE_NAME, NVS_READONLY, &handle);

    if (open_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open NVS handle %s; Error: %s", HANDLE_NAME, esp_err_to_name(open_err));

        return open_err;
    }

    esp_err_t get_err = nvs_get_str(handle, key, value, size);

    nvs_close(handle);

    return get_err;
}

esp_err_t storage_size(char *key, size_t *size) {
    return storage_get(key, NULL, size);
}