/* 
* @Author: FZH
* @Date: 2025-08-21 16:09:32
* @LastEditors: FZH
* @LastEditTime: 2025-08-21 16:09:32
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "use_wifi.h"
#include "use_ble_server.h"
#include "common.h"
#include "app_main.h"

static const char *TAG = "main";

void app_main(void)
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    device_init();
    
    // 初始化并启动BLE服务器
    initialize_ble_server();
    
    // 启动WiFi和MQTT连接
    ESP_ERROR_CHECK(use_wifi_start());

    // 等待连接成功
    ESP_LOGI(TAG, "等待WiFi和MQTT连接...");
    esp_err_t connect_result = use_wifi_wait_connected(0); // 永久等待

    if (connect_result == ESP_OK) {
        ESP_LOGI(TAG, "连接成功！启动业务逻辑任务");

        // 启动APP主业务任务
        ESP_ERROR_CHECK(app_main_start());
        
        ESP_LOGI(TAG, "系统初始化完成，业务逻辑任务已启动");
        
        // 主循环变为空闲循环，实际业务由app_main任务处理
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(60000)); // 60秒空闲延时
        }
    } else {
        ESP_LOGE(TAG, "连接失败，程序退出");
    }
}
