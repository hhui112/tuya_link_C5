idf_component_register(SRCS "use_uart.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_common mqtt_manager json)
 