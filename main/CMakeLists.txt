idf_component_register(SRCS "main.c"
                    REQUIRES common use_ble_server esp_psram app_main
                    PRIV_REQUIRES esp_wifi nvs_flash)
