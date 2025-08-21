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

    // 初始化公共状态
    common_init();
    // 启动WiFi和MQTT连接
    ESP_ERROR_CHECK(use_wifi_start());

    // 等待连接成功
    ESP_LOGI(TAG, "等待WiFi和MQTT连接...");
    esp_err_t connect_result = use_wifi_wait_connected(0); // 永久等待

    if (connect_result == ESP_OK) {
        ESP_LOGI(TAG, "连接成功！开始IoT数据传输");

        // 主循环 - 定期发送数据
        while (1) {
            if (use_wifi_is_connected()) {
                // 获取当前IOT状态
                char current_device_status[32];
                int32_t current_test_value;
                get_current_iot_state(current_device_status, sizeof(current_device_status), &current_test_value);

                
                ESP_LOGI(TAG, "当前IOT状态 - device_status: %s, test_value: %ld", current_device_status, (long)current_test_value);
                
                // 根据设备状态执行相应操作
                if (strcmp(current_device_status, "open") == 0) {
                    ESP_LOGI(TAG, "设备处于开启状态，执行开启操作");
                } else if (strcmp(current_device_status, "close") == 0) {
                    ESP_LOGI(TAG, "设备处于关闭状态，执行关闭操作");
                }

                // 发送传感器数据（使用本地测试值）
                esp_err_t result = tuya_publish_sensor_data(current_test_value, current_device_status);
                if (result == ESP_OK) {
                    ESP_LOGI(TAG, "传感器数据发送成功: test_value=%d°C, device_status=%s", 
                             current_test_value, current_device_status);
                } else {
                    ESP_LOGW(TAG, "传感器数据发送失败");
                }

                // 模拟传感器数据变化
                g_iot_state.test_value += 1;
                if (g_iot_state.test_value > 50) g_iot_state.test_value = 10;
            } else {
                ESP_LOGW(TAG, "连接已断开，等待重连...");
            }

            vTaskDelay(10000 / portTICK_PERIOD_MS); // 10秒间隔
        }
    } else {
        ESP_LOGE(TAG, "连接失败，程序退出");
    }
}
