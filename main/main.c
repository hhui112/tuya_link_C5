/* ESP32-C5 WiFi + 涂鸦IoT MQTT 简化示例
   
   This example code is in the Public Domain (or CC0 licensed, at your option.)
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "use_wifi.h"

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

    // 启动WiFi和MQTT连接
    ESP_ERROR_CHECK(use_wifi_start());

    // 等待连接成功
    ESP_LOGI(TAG, "等待WiFi和MQTT连接...");
    esp_err_t connect_result = use_wifi_wait_connected(0); // 永久等待

    if (connect_result == ESP_OK) {
        ESP_LOGI(TAG, "连接成功！开始IoT数据传输");

        // 主循环 - 定期发送数据
        float device_status = 20.0;
        float humidity = 50.0;
        
        while (1) {
            if (use_wifi_is_connected()) {
                // 发送传感器数据
                esp_err_t result = tuya_publish_sensor_data(device_status, humidity);
                if (result == ESP_OK) {
                    ESP_LOGI(TAG, "传感器数据发送成功: 温度=%.1f°C, 湿度=%.1f%%", 
                             device_status, humidity);
                } else {
                    ESP_LOGW(TAG, "传感器数据发送失败");
                }

                // 发送心跳
/*
                result = tuya_send_heartbeat();
                if (result == ESP_OK) {
                    ESP_LOGI(TAG, "心跳发送成功");
                } else {
                    ESP_LOGW(TAG, "心跳发送失败");
                }
*/
                // 模拟传感器数据变化
                device_status += 0.5;
                humidity += 1.0;
                if (device_status > 30.0) device_status = 10.0;
                if (humidity > 80.0) humidity = 50.0;
            } else {
                ESP_LOGW(TAG, "连接已断开，等待重连...");
            }

            vTaskDelay(10000 / portTICK_PERIOD_MS); // 30秒间隔
        }
    } else {
        ESP_LOGE(TAG, "连接失败，程序退出");
    }
}
