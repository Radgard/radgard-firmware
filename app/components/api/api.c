#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_http_client.h"

#include "api.h"
#include "storage.h"

#define MAX_HTTP_OUTPUT_BUFFER 5096
static const char *TAG = "api";

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
                output_len = 0;
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                    output_len = 0;
                }
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

static void get_irrigation_settings() {
    storage_init_nvs();
    size_t size;
    esp_err_t size_err = storage_size(STORAGE_USER_ID, &size);
    if (size_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting user-id size from storage: %s", esp_err_to_name(size_err));
        return;
    }

    char *user_id = malloc(size);
    esp_err_t get_err = storage_get(STORAGE_USER_ID, user_id, &size);
    if (get_err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting user-id from storage: %s", esp_err_to_name(get_err));
        return;
    }

    ESP_LOGI(TAG, "Fetched user-id from NVS; attempting to get irrigation settings from server");

    const char *URL = "https://us-central1-animal-farm-e321d.cloudfunctions.net/getIrrigationSettings";
    const char *data_holder = "{\"userID\":\"%s\"}";

    char *DATA = malloc(strlen(data_holder) + strlen(user_id) + 1);
    sprintf(DATA, data_holder, user_id);

    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    esp_http_client_config_t config = {
        .url = URL,
        .method = HTTP_METHOD_POST,
        .query  = "esp",
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,
        .timeout_ms = 20000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, DATA, strlen(DATA));

    esp_err_t http_err = esp_http_client_perform(client);
    if (http_err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        //ESP_LOGI(TAG, "HTTP DATA = %s", local_response_buffer);
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(http_err));
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

void api_get_irrigation_settings() {
    storage_init_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreate(&get_irrigation_settings, "get_irrigation_settings", 8192, NULL, 5, NULL);
}