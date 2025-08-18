idf_component_register(
    SRCS "use_wifi.c"
    INCLUDE_DIRS "../common"
	             "."
    REQUIRES esp_wifi nvs_flash mqtt lwip esp_netif esp_event esp-tls mbedtls json common
    PRIV_REQUIRES esp_system freertos lwip
) 