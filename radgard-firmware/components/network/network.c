#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include "esp_sntp.h"
#include <esp_https_ota.h>

#include <wifi_provisioning/manager.h>

#include <wifi_provisioning/scheme_softap.h>

#include <cJSON.h>

#include "network.h"
#include "storage.h"
#include "api.h"

static const char *TAG = "network";

static const char *SSID = "Radgard Frisk";
static const char *PASSWORD = "plantsaregreat";
static const char *ENDPOINT = "setup";

/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
const int WIFI_DISCONNECTED_EVENT = BIT1;
const int WIFI_STATUS_EVENT = BIT0 | BIT1;
static EventGroupHandle_t wifi_event_group;

const int firmware_sync_completed = BIT0;
static EventGroupHandle_t firmware_sync_event_group;

static int disconnect_count = 0;

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base, int event_id, void* event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                
                storage_reset();
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (disconnect_count < 3) {
            ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
            esp_wifi_connect();
            disconnect_count += 1;
        } else {
            ESP_LOGI(TAG, "Disconnected. Skipping network init...");
            xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_EVENT);
        }
    }
}

static void wifi_init_sta(void) {
    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* Handler for the optional provisioning endpoint registered by the application.
 * The data format can be chosen by applications. Here, we are using plain ascii text.
 * Applications can choose to use other formats like protobuf, JSON, XML, etc.
 */
esp_err_t setup_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data) {
    if (inbuf) {
        // Get first `inlen` characters of inbuf as `setup`
        char *setup = malloc(inlen + 1);
        strncpy(setup, (char *) inbuf, inlen);
        setup[inlen] = '\0';
        ESP_LOGI(TAG, "Received /setup data: %s", setup);

        // Parse JSON
        cJSON *json = cJSON_Parse(setup);
        cJSON *user_id_json = cJSON_GetObjectItem(json, "userId");
        cJSON *zone_id_json = cJSON_GetObjectItem(json, "zoneId");

        char *user_id = user_id_json->valuestring;
        char *zone_id = zone_id_json->valuestring;
        free(setup);

        // Store `user_id` in NVS
        esp_err_t set_err = storage_set_str(STORAGE_USER_ID, user_id);
        if (set_err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving user_id to storage: %s", esp_err_to_name(set_err));
        }

        // Store `zone_id` in NVS
        set_err = storage_set_str(STORAGE_ZONE_ID, zone_id);
        if (set_err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving zone_id to storage: %s", esp_err_to_name(set_err));
        }

        ESP_LOGI(TAG, "Stored /setup data in NVS");
        cJSON_Delete(json);

        return ESP_OK;
    }

    return ESP_FAIL;
}

static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void network_sync_time() {
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_init();

    // Wait for time to be set
    int retry = 0;
    const int retry_count = 15;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    ESP_ERROR_CHECK(esp_event_loop_delete_default());
}

static char *replace_char(char* str, char find, char replace) {
    char *current_pos = strchr(str, find);

    while (current_pos) {
        *current_pos = replace;
        current_pos = strchr(current_pos, find);
    }

    return str;
}

static void network_firmware_sync() {
    ESP_LOGI(TAG, "Checking for firmware update");

    cJSON *firmware_update = api_get_firmware_update_url();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (firmware_update != NULL) {
        cJSON *url_json = cJSON_GetObjectItem(firmware_update, "url");
        cJSON *cert_json = cJSON_GetObjectItem(firmware_update, "cert");

        char *url = url_json->valuestring;
        char *cert = cert_json->valuestring;
        replace_char(cert, ' ', '\n');
        replace_char(cert, '_', ' ');

        ESP_LOGI(TAG, "Firmware is out of date, getting new firmware from %s with cert %s", url, cert);
        
        esp_http_client_config_t config = {
            .url = url,
            .cert_pem = cert
        };

        esp_err_t ota_err = esp_https_ota(&config);

        cJSON_Delete(firmware_update);

        if (ota_err == ESP_OK) {
            ESP_LOGI(TAG, "Firmware update successfully applied, restarting system");
            esp_restart();
        }
    } else {
        ESP_LOGI(TAG, "Firmware is the latest version");
    }

    ESP_ERROR_CHECK(esp_event_loop_delete_default());

    xEventGroupSetBits(firmware_sync_event_group, firmware_sync_completed);
    vTaskDelete(NULL);
}

bool network_start_provision_connect_wifi() {
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };
    
    /* Initialize provisioning manager with the
     * configuration parameters set above */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    bool provisioned = false;
    /* Let's find out if the device is provisioned */
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    /* If device is not yet provisioned start provisioning service */
    if (!provisioned) {
        ESP_LOGI(TAG, "Starting provisioning");

        /* What is the Device Service Name that we want
         * This translates to :
         *     - Wi-Fi SSID when scheme is wifi_prov_scheme_softap
         *     - device name when scheme is wifi_prov_scheme_ble
         */
        const char *service_name = SSID;

        /* What is the security level that we want (0 or 1):
         *      - WIFI_PROV_SECURITY_0 is simply plain text communication.
         *      - WIFI_PROV_SECURITY_1 is secure communication which consists of secure handshake
         *          using X25519 key exchange and proof of possession (pop) and AES-CTR
         *          for encryption/decryption of messages.
         */
        wifi_prov_security_t security = WIFI_PROV_SECURITY_0;

        /* Do we want a proof-of-possession (ignored if Security 0 is selected):
         *      - this should be a string with length > 0
         *      - NULL if not used
         */
        const char *pop = NULL;

        /* What is the service key (could be NULL)
         * This translates to :
         *     - Wi-Fi password when scheme is wifi_prov_scheme_softap
         *     - simply ignored when scheme is wifi_prov_scheme_ble
         */
        const char *service_key = PASSWORD;

        /* An optional endpoint that applications can create if they expect to
         * get some additional custom data during provisioning workflow.
         * The endpoint name can be anything of your choice.
         * This call must be made before starting the provisioning.
         */
        wifi_prov_mgr_endpoint_create(ENDPOINT);
        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));

        /* The handler for the optional endpoint created above.
         * This call must be made after starting the provisioning, and only if the endpoint
         * has already been created above.
         */
        wifi_prov_mgr_endpoint_register(ENDPOINT, setup_handler, NULL);

        /* Uncomment the following to wait for the provisioning to finish and then release
         * the resources of the manager. Since in this case de-initialization is triggered
         * by the default event loop handler, we don't need to call the following */
        // wifi_prov_mgr_wait();
        // wifi_prov_mgr_deinit();
    } else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");

        /* We don't need the manager as device is already provisioned,
         * so let's release it's resources */
        wifi_prov_mgr_deinit();

        /* Start Wi-Fi station */
        wifi_init_sta();
    }

    /* Wait for Wi-Fi connection */
    xEventGroupWaitBits(wifi_event_group, WIFI_STATUS_EVENT, false, false, portMAX_DELAY);

    ESP_ERROR_CHECK(esp_event_loop_delete_default());

    if (xEventGroupGetBits(wifi_event_group) == WIFI_DISCONNECTED_EVENT) {
        return false;
    }

    ESP_LOGI(TAG, "Just connected to WIFI - starting sleep to allow proper configuration");
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_LOGI(TAG, "Just connected to WIFI - ending sleep to use for API calls");

    network_sync_time();

    firmware_sync_event_group = xEventGroupCreate();
    xTaskCreate(&network_firmware_sync, "network_firmware_sync", 8192, NULL, 5, NULL);
    xEventGroupWaitBits(firmware_sync_event_group, firmware_sync_completed, false, true, portMAX_DELAY);

    return true;
}

void network_disconnect_wifi() {
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    esp_netif_deinit();
}