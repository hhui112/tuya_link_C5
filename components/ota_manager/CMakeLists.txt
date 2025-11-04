idf_component_register(
    SRCS 
        "ota_manager.c"
        "tuya_ota_protocol.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_https_ota
        esp_http_client
        app_update
        mqtt_manager
        common
        nvs_flash
        json
)

