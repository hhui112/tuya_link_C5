#include "common.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "common";

// 全局状态变量定义
iot_device_state_t g_iot_state = {
    .device_status = "close",  // 默认值
    .test_value = 0            // 默认值
};

/**
 * @brief 初始化全局状态
 */
void common_init(void)
{
    // 设置默认状态
    strncpy(g_iot_state.device_status, "close", sizeof(g_iot_state.device_status) - 1);
    g_iot_state.device_status[sizeof(g_iot_state.device_status) - 1] = '\0';
    g_iot_state.test_value = 10;
    
    ESP_LOGI(TAG, "全局状态初始化完成 - device_status: %s, test_value: %ld", 
             g_iot_state.device_status, (long)g_iot_state.test_value);
}

/**
 * @brief 获取当前IOT设备状态
 */
void get_current_iot_state(char* device_status, size_t status_size, int32_t* test_value)
{
    if (device_status && status_size > 0) {
        strncpy(device_status, g_iot_state.device_status, status_size - 1);
        device_status[status_size - 1] = '\0';
    }
    if (test_value) {
        *test_value = g_iot_state.test_value;
    }
}

/**
 * @brief 更新设备状态
 */
void set_device_status(const char* device_status)
{
    if (device_status) {
        strncpy(g_iot_state.device_status, device_status, sizeof(g_iot_state.device_status) - 1);
        g_iot_state.device_status[sizeof(g_iot_state.device_status) - 1] = '\0';
        ESP_LOGI(TAG, "设备状态已更新: %s", g_iot_state.device_status);
    }
}

/**
 * @brief 更新测试数值
 */
void set_test_value(int32_t test_value)
{
    g_iot_state.test_value = test_value;
    ESP_LOGI(TAG, "测试数值已更新: %ld", (long)g_iot_state.test_value);
}
