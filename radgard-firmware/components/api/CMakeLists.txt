idf_component_register(SRCS "api.c"
                    INCLUDE_DIRS "include"
                    REQUIRES storage esp-tls esp_http_client json)
