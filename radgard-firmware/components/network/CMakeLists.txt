idf_component_register(SRCS "network.c"
                    INCLUDE_DIRS "include"
                    REQUIRES wifi_provisioning json storage api esp_https_ota
                    EMBED_TXTFILES ../../server_certs/ca_cert.pem)
