#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <freertos/event_groups.h>

#include "esp_log.h"
#include "esp_system.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_sntp.h"

#include <cJSON.h>

#include "api.h"
#include "storage.h"

#define MAX_HTTP_OUTPUT_BUFFER 4096
static const char *TAG = "api";

const int irrigation_settings_fetched_event = BIT0;
static EventGroupHandle_t irrigation_settings_event_group;

const int firmware_update_url_fetched_event = BIT0;
static EventGroupHandle_t firmware_update_url_event_group;

static cJSON *firmware_update = NULL;

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }

                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }

                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");

            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }

            output_len = 0;

            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);

            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }

                output_len = 0;

                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }

            break;
    }
    
    return ESP_OK;
}

static void reset_sig_rains() {
    ESP_LOGI(TAG, "Failed to get latest irrigation settings -- resetting sig_rains");

    size_t sig_rains_size = 7 * sizeof(uint8_t);
    uint8_t *sig_rains = malloc(sig_rains_size);

    for (int i = 0; i < 7; i++) {
        sig_rains[i] = 0;
    }

    storage_set_blob(STORAGE_SIG_RAINS, sig_rains, sig_rains_size);

    free(sig_rains);
}

static void get_irrigation_settings() {
    size_t size;
    esp_err_t size_err = storage_get_str_size(STORAGE_USER_ID, &size);
    if (size_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting user_id size from storage: %s", esp_err_to_name(size_err));
        
        vTaskDelete(NULL);
    }

    char *user_id = malloc(size);
    esp_err_t get_err = storage_get_str(STORAGE_USER_ID, user_id, &size);
    if (get_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting user_id from storage: %s", esp_err_to_name(get_err));
        
        vTaskDelete(NULL);
    }

    size_err = storage_get_str_size(STORAGE_ZONE_ID, &size);
    if (size_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting zone_id size from storage: %s", esp_err_to_name(size_err));
        
        vTaskDelete(NULL);
    }

    char *zone_id = malloc(size);
    get_err = storage_get_str(STORAGE_ZONE_ID, zone_id, &size);
    if (get_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting zone_id from storage: %s", esp_err_to_name(get_err));

        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "Fetched user_id and zone_id from NVS; attempting to get irrigation settings from server");

    const char *URL = "https://us-central1-animal-farm-e321d.cloudfunctions.net/getIrrigationSettings2";
    const char *data_holder = "{\"userId\":\"%s\",\"zoneId\":\"%s\"}";

    char *DATA = malloc(strlen(data_holder) + strlen(user_id) + strlen(zone_id) + 1);
    sprintf(DATA, data_holder, user_id, zone_id);
    free(user_id);
    free(zone_id);

    ESP_LOGI(TAG, "Posting data to getIrrigationSettings: %s", DATA);

    char irrigation_settings[MAX_HTTP_OUTPUT_BUFFER] = {0};

    esp_http_client_config_t config = {
        .url = URL,
        .method = HTTP_METHOD_POST,
        .event_handler = _http_event_handler,
        .user_data = irrigation_settings,
        .timeout_ms = 10000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, DATA, strlen(DATA));

    esp_err_t http_err = esp_http_client_perform(client);
    if (http_err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                status_code,
                esp_http_client_get_content_length(client));

        if (status_code == 200) {
            ESP_LOGI(TAG, "HTTP DATA = %s", irrigation_settings);

            cJSON *json = cJSON_Parse(irrigation_settings);
            cJSON *time_zone_json = cJSON_GetObjectItem(json, "time_zone");
            cJSON *times_json = cJSON_GetObjectItem(json, "times");
            cJSON *sig_rains_json = cJSON_GetObjectItem(json, "sig_rains");

            uint32_t time_zone = (uint32_t) time_zone_json->valuedouble;

            storage_set_u32(STORAGE_TIME_ZONE, time_zone);

            uint32_t times_length = cJSON_GetArraySize(times_json);

            for (int i = 0; i < times_length; i++) {
                cJSON *day_times_json = cJSON_GetArrayItem(times_json, i);

                uint32_t day_times_length = cJSON_GetArraySize(day_times_json);
                size_t day_times_size = day_times_length * sizeof(uint32_t);

                uint32_t *day_times = malloc(day_times_size);

                for (int j = 0; j < day_times_length; j++) {
                    uint32_t day_time = (uint32_t) cJSON_GetArrayItem(day_times_json, j)->valuedouble;
                    day_times[j] = day_time;
                }

                char *day_times_key = malloc(strlen(STORAGE_TIME_BASE));
                sprintf(day_times_key, STORAGE_TIME_BASE, i);

                storage_set_blob(day_times_key, day_times, day_times_size);

                free(day_times);
                free(day_times_key);
            }

            uint32_t sig_rains_length = cJSON_GetArraySize(sig_rains_json);
            size_t sig_rains_size = sig_rains_length * sizeof(uint8_t);
            uint8_t *sig_rains = malloc(sig_rains_size);

            for (int i = 0; i < sig_rains_length; i++) {
                uint8_t sig_rain = (uint8_t) cJSON_IsTrue(cJSON_GetArrayItem(sig_rains_json, i));
                sig_rains[i] = sig_rain;
            }

            storage_set_blob(STORAGE_SIG_RAINS, sig_rains, sig_rains_size);

            free(sig_rains);
            cJSON_Delete(json);
        } else {
            reset_sig_rains();
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(http_err));
        reset_sig_rains();
    }

    free(DATA);
    esp_http_client_cleanup(client);
    xEventGroupSetBits(irrigation_settings_event_group, irrigation_settings_fetched_event);
    vTaskDelete(NULL);
}

void api_get_irrigation_settings() {
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    irrigation_settings_event_group = xEventGroupCreate();

    xTaskCreate(&get_irrigation_settings, "get_irrigation_settings", 8192, NULL, 5, NULL);
    xEventGroupWaitBits(irrigation_settings_event_group, irrigation_settings_fetched_event, false, true, portMAX_DELAY);
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
}

static void get_firmware_update_url() {
    uint8_t version;
    esp_err_t version_err = storage_get_u8(STORAGE_VERSION, &version);
    if (version_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting version from storage: %s", esp_err_to_name(version_err));
        
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "Fetched version from NVS; attempting to get firmware update url from server");

    const char *URL = "https://us-central1-animal-farm-e321d.cloudfunctions.net/getFirmwareUpdateUrl";
    const char *data_holder = "{\"version\":\"%d\"}";

    char *DATA = malloc(strlen(data_holder) + sizeof(uint8_t) + 1);
    sprintf(DATA, data_holder, version);

    ESP_LOGI(TAG, "Posting data to getFirmwareUpdateUrl: %s", DATA);

    char firmware_update_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    esp_http_client_config_t config = {
        .url = URL,
        .method = HTTP_METHOD_POST,
        .event_handler = _http_event_handler,
        .user_data = firmware_update_buffer,
        .timeout_ms = 10000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, DATA, strlen(DATA));

    esp_err_t http_err = esp_http_client_perform(client);
    if (http_err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d", status_code, content_length);

        if (status_code == 200) {
            ESP_LOGI(TAG, "HTTP DATA = %s", firmware_update_buffer);

            firmware_update = cJSON_Parse(firmware_update_buffer);
        }
    }

    free(DATA);
    esp_http_client_cleanup(client);
    xEventGroupSetBits(firmware_update_url_event_group, firmware_update_url_fetched_event);
    vTaskDelete(NULL);
}

cJSON *api_get_firmware_update_url() {
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    firmware_update_url_event_group = xEventGroupCreate();

    xTaskCreate(&get_firmware_update_url, "get_firmware_update_url", 8192, NULL, 5, NULL);
    xEventGroupWaitBits(firmware_update_url_event_group, firmware_update_url_fetched_event, false, true, portMAX_DELAY);
    ESP_ERROR_CHECK(esp_event_loop_delete_default());

    return firmware_update;
}