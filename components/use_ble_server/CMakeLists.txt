# ESP32-C5 BLE 服务器组件
# 基于 ESP-IDF NimBLE GATT Server 官方示例

idf_component_register(SRCS "use_ble_server.c"
                       INCLUDE_DIRS "." "../common"
                       REQUIRES nvs_flash bt esp_system)