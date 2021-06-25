idf_component_register(SRCS "server.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash wifi_provisioning storage)
