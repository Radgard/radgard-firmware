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

const char *STORAGE_VERSION = "version";

const char *STORAGE_USER_ID = "user_id";
const char *STORAGE_ZONE_ID = "zone_id";

const char *STORAGE_TIME_ZONE = "time_zone";
const char *STORAGE_TIME_BASE = "time_%d";
const char *STORAGE_SIG_RAINS = "sig_rains";

const char *STORAGE_SOLENOID_OPEN = "solenoid_open";
const char *STORAGE_MANUAL_ON = "manual_on";

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

void storage_deinit_nvs() {
    ESP_ERROR_CHECK(nvs_flash_deinit());
}

static nvs_handle get_write_handle() {
    nvs_handle handle;
    esp_err_t open_err = nvs_open(HANDLE_NAME, NVS_READWRITE, &handle);

    if (open_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open NVS handle %s; Error: %s", HANDLE_NAME, esp_err_to_name(open_err));

        return open_err;
    }

    return handle;
}

static nvs_handle get_read_handle() {
    nvs_handle handle;
    esp_err_t open_err = nvs_open(HANDLE_NAME, NVS_READONLY, &handle);

    if (open_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open NVS handle %s; Error: %s", HANDLE_NAME, esp_err_to_name(open_err));

        return open_err;
    }

    return handle;
}

esp_err_t storage_set_str(const char *key, const char *value) {
    nvs_handle handle = get_write_handle();

    esp_err_t set_err = nvs_set_str(handle, key, value);

    nvs_close(handle);

    ESP_LOGI(TAG, "Attempted to set value (%s) to key (%s)", value, key);

    return set_err;
}

esp_err_t storage_set_u8(const char *key, uint8_t value) {
    nvs_handle handle = get_write_handle();

    esp_err_t set_err = nvs_set_u8(handle, key, value);

    nvs_close(handle);

    ESP_LOGI(TAG, "Attempted to set value (%d) to key (%s)", value, key);

    return set_err;
}

esp_err_t storage_set_u32(const char *key, uint32_t value) {
    nvs_handle handle = get_write_handle();

    esp_err_t set_err = nvs_set_u32(handle, key, value);

    nvs_close(handle);

    ESP_LOGI(TAG, "Attempted to set value (%d) to key (%s)", value, key);

    return set_err;
}

esp_err_t storage_set_blob(const char *key, const void *value, size_t size) {
    nvs_handle handle = get_write_handle();

    esp_err_t set_err = nvs_set_blob(handle, key, value, size);

    nvs_close(handle);

    ESP_LOGI(TAG, "Attempted to set blob to key (%s)", key);

    return set_err;
}

esp_err_t storage_get_str(const char *key, char *value, size_t *size) {
    nvs_handle handle = get_read_handle();

    esp_err_t get_err = nvs_get_str(handle, key, value, size);

    nvs_close(handle);

    if (value != NULL) {
        ESP_LOGI(TAG, "Attempted to get value (%s) from key (%s)", value, key);
    }

    return get_err;
}

esp_err_t storage_get_u8(const char *key, uint8_t *value) {
    nvs_handle handle = get_read_handle();

    esp_err_t get_err = nvs_get_u8(handle, key, value);

    nvs_close(handle);

    ESP_LOGI(TAG, "Attempted to get value (%d) from key (%s)", *value, key);

    return get_err;
}

esp_err_t storage_get_u32(const char *key, uint32_t *value) {
    nvs_handle handle = get_read_handle();

    esp_err_t get_err = nvs_get_u32(handle, key, value);

    nvs_close(handle);

    ESP_LOGI(TAG, "Attempted to get value (%d) from key (%s)", *value, key);

    return get_err;
}

esp_err_t storage_get_blob(const char *key, void *value, size_t *size) {
    nvs_handle handle = get_read_handle();

    esp_err_t get_err = nvs_get_blob(handle, key, value, size);

    nvs_close(handle);

    ESP_LOGI(TAG, "Attempted to get blob from key (%s)", key);

    return get_err;
}

esp_err_t storage_get_str_size(const char *key, size_t *size) {
    return storage_get_str(key, NULL, size);
}

esp_err_t storage_get_blob_size(const char *key, size_t *size) {
    return storage_get_blob(key, NULL, size);
}

esp_err_t storage_remove(const char *key) {
    nvs_handle handle = get_write_handle();

    esp_err_t erase_err = nvs_erase_key(handle, key);

    nvs_close(handle);

    ESP_LOGI(TAG, "Attempted to remove key (%s)", key);

    return erase_err;
}

void storage_reset() {
    nvs_flash_erase();
    esp_restart();
}