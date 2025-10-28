idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                                 "../use_uart"
                    REQUIRES freertos log wifi_manager mqtt_manager utctime common json use_uart use_ble_server)
