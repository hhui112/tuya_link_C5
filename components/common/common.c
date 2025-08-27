#include "common.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "common";
device_info_t *device_info;

// 全局状态变量定义
iot_device_state_t g_iot_state = {
    .device_status = "close",  // 默认值
    .test_value = 0            // 默认值
};

// 统一消息队列
QueueHandle_t g_msg_queue = NULL;

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


//设置默认参数
static void param_config_init(void)
{
    g_msg_queue = xQueueCreate(10, sizeof(g_msg_queue_t));

}


void device_init(void)
{
	param_config_init();
	
}


