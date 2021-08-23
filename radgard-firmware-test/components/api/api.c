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

#define MAX_HTTP_OUTPUT_BUFFER 4096
static const char *TAG = "api";

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

static void get_firmware_update_url() {
    const char *URL = "https://us-central1-animal-farm-e321d.cloudfunctions.net/getFirmwareUpdateUrl";
    const char *DATA = "{\"version\":\"0\"}";

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