#include "app_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "use_wifi.h"
#include "common.h"
#include <string.h>

static const char *TAG = "APP_MAIN";

/* 任务句柄 */
static TaskHandle_t message_task_handle = NULL;
static TaskHandle_t iot_task_handle = NULL;


void process_unified_message(const g_msg_queue_t* msg)
{
    if (msg == NULL) return;
    
    const char* source_str = (msg->source == MSG_SOURCE_BLE) ? "BLE" : 
                            (msg->source == MSG_SOURCE_MQTT) ? "MQTT" : "UNKNOWN";
    
    printf("收到%s消息 长度: %d\n", source_str, msg->data_len);
    printf("消息内容: %.*s\n", msg->data_len, msg->data);
    
    // TODO: 在这里添加具体的消息处理业务逻辑
    // 例如：JSON解析、命令执行、状态更新等
}

/**
 * @brief 消息处理任务（高优先级，立即响应）
 */
static void message_task(void *pvParameters)
{
    ESP_LOGI(TAG, "APP业务任务已启动");
    
    g_msg_queue_t msg; 
    while (1) {
        // 阻塞等待消息，立即处理
        if (xQueueReceive(g_msg_queue, &msg, portMAX_DELAY) == pdTRUE) {
            process_unified_message(&msg);
        }
    }
    vTaskDelete(NULL);
}

/**
 * @brief IoT业务任务（独立运行，定期处理）
 */
static void iot_task(void *pvParameters)
{
    ESP_LOGI(TAG, "IoT业务任务已启动");
    
    while (1) {
        // TODO: 在这里添加定期业务逻辑
        // 例如：
        // - 传感器数据采集
        // - MQTT数据发布
        // - BLE状态更新
        // - 系统状态检查
        
        ESP_LOGI(TAG, "📋 执行定期业务逻辑...");
        
        // 任务延时（可根据需要调整）
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10秒
    }
    vTaskDelete(NULL);
}

/**
 * @brief 启动APP主业务任务
 */
esp_err_t app_main_start(void)
{
    ESP_LOGI(TAG, "启动双任务架构");
    
    // 创建消息处理任务（高优先级）
    BaseType_t ret1 = xTaskCreate(message_task, "msg_task", 1024*6, NULL, 6, &message_task_handle);
    if (ret1 != pdPASS) {
        ESP_LOGE(TAG, "创建消息处理任务失败");
        return ESP_FAIL;
    }
    
    // 创建IoT业务任务（正常优先级）
    BaseType_t ret2 = xTaskCreate(iot_task, "iot_task", 1024*2, NULL, 5, &iot_task_handle);
    if (ret2 != pdPASS) {
        ESP_LOGE(TAG, "创建IoT业务任务失败");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief 停止APP主业务任务
 */
esp_err_t app_main_stop(void)
{
    ESP_LOGI(TAG, "停止双任务架构");
    
    if (message_task_handle != NULL) {
        vTaskDelete(message_task_handle);
        message_task_handle = NULL;
        ESP_LOGI(TAG, "消息处理任务已停止");
    }
    
    if (iot_task_handle != NULL) {
        vTaskDelete(iot_task_handle);
        iot_task_handle = NULL;
        ESP_LOGI(TAG, "IoT业务任务已停止");
    }
    
    return ESP_OK;
}
