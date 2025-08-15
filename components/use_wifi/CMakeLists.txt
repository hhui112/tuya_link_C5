idf_component_register(
    SRCS "use_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash mqtt lwip esp_netif esp_event esp-tls mbedtls
    PRIV_REQUIRES esp_system freertos lwip
) 