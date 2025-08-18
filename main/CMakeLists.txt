idf_component_register(SRCS "main.c"
                    REQUIRES common
                    PRIV_REQUIRES esp_wifi nvs_flash use_wifi
                    INCLUDE_DIRS "." "../components/common")
