idf_component_register(SRCS "network.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash wifi_provisioning storage)
