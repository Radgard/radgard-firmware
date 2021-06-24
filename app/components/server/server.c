#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>
#include "mdns.h"

#include "server.h"

// SSID Info
#define SSID            "Radgard"
#define PASSWORD        "plantsrg8"
#define CHANNEL         1
#define MAX_STA_CONN    4

void create_network() {
    // Init NVS Storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = SSID,
            .ssid_len = strlen(SSID),
            .channel = CHANNEL,
            .password = PASSWORD,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    // Set AP Mode & Configure Wifi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Add Alias Hostname
    /*mdns_server_t* mDNS = NULL;
    ESP_ERROR_CHECK(mdns_init(TCPIP_ADAPTER_IF_AP, &mDNS));
    ESP_ERROR_CHECK(sethostname(mDNS, "radgard"));*/
}

esp_err_t user_id_handler(uint32_t session_id, const 
                    uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, 
                    ssize_t *outlen, void *priv_data) {
    if (inbuf) {
        char log[75];
        sprintf(log, "Received data on /user-id: %s", (char *) inbuf);
        printf(log);
    }

    char response[] = "SUCCESS";
    *outbuf = (uint8_t *) strdup(response);
    if (*outbuf == NULL) {
        printf("System out of memory.");
        return ESP_ERR_NO_MEM;
    } 

    *outlen = strlen(response) + 1; 

    return ESP_OK;
}

void init_user_id_endpoint() {
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };

    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *pop = "abcd1234";

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
    ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_create("user-id"));
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, SSID, PASSWORD));
    ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_register("user-id", user_id_handler, NULL));
}

void server_start() {
    create_network();
    init_user_id_endpoint();
}