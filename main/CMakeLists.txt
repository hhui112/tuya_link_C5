idf_component_register(SRCS "main.c"
                    REQUIRES common use_ble_server esp_psram
                    PRIV_REQUIRES esp_wifi nvs_flash use_wifi app_main
                    INCLUDE_DIRS "." "../components/common" "../components/use_ble_server")
