idf_component_register(SRCS "network.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash json api esp_https_ota)
