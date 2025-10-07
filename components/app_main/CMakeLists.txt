idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos log wifi_manager mqtt_manager utctime common json)
