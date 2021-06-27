#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

//#include "esp_tls.h"
#include "esp_crt_bundle.h"

#include "api.h"
#include "storage.h"

static const char *TAG = "api";

esp_err_t api_get_irrigation_settings() {
    storage_init_nvs();
    size_t size;
    esp_err_t size_err = storage_size(STORAGE_USER_ID, &size);
    if (size_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting user-id size from storage: %s", esp_err_to_name(size_err));
        return ESP_FAIL;
    }

    char *user_id = malloc(size);
    esp_err_t get_err = storage_get(STORAGE_USER_ID, user_id, &size);
    if (get_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting user-id from storage: %s", esp_err_to_name(get_err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Fetched user-id from NVS; attempting to get irrigation settings from server");

    const char *URL = "https://us-central1-animal-farm-e321d.cloudfunctions.net/getIrrigationSettings";
    const char *data_holder = "{\"userID\":\"%s\"}";
    const char *request_holder = "POST %s HTTP/1.0\r\n"
                                "User-Agent: esp-idf/1.0 esp32\r\n"
                                "Connection: close\r\n"
                                "Host: WEB_SERVER\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: 13\r\n"
                                "\r\n"
                                "%s";

    char *DATA = malloc(strlen(data_holder) + strlen(user_id) + 1);
    sprintf(DATA, data_holder, user_id);

    char *REQUEST = malloc(strlen(request_holder) + strlen(URL) + strlen(DATA) + 1);
    sprintf(REQUEST, request_holder, URL, DATA);

    

    return ESP_OK;
}