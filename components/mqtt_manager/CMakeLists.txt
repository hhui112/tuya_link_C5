idf_component_register(SRCS "mqtt_manager.c"
                    INCLUDE_DIRS "."
                                "../common"
                    REQUIRES mqtt esp_event mbedtls common)
