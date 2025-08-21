idf_component_register(SRCS "use_ble_server.c"
                    INCLUDE_DIRS "."
                    INCLUDE_DIRS "../common"
                    REQUIRES "nvs_flash" "esp_system" "common" "bt")
