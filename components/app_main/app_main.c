#include "app_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "utctime.h"
#include "common.h"
#include <string.h>
#include <time.h>
#include "cJSON.h"

static const char *TAG = "APP_MAIN";

/* 任务句柄 */
static TaskHandle_t message_task_handle = NULL;
static TaskHandle_t iot_task_handle = NULL;

/* 系统状态 */
static bool wifi_ready = false;
static bool ntp_ready = false;
static bool mqtt_ready = false;


void mqtt_ble_data_parser_cb(const g_msg_queue_t* msg)
{
    if (msg == NULL) return;
    
    const char* source_str = (msg->source == MSG_SOURCE_BLE) ? "BLE" : (msg->source == MSG_SOURCE_MQTT) ? "MQTT" : "UNKNOWN";
    
    printf("收到 %s 消息，长度: %d\n", source_str, msg->data_len);

    uint8_t data[64] = {0}; 
    const char *cmd_str = (const char *)msg->data;  
    uint8_t cmd_len = 0;

    // 每2个字符转换为一个字节
    while (*cmd_str && *(cmd_str + 1) && (cmd_len < sizeof(data))) {
        char byte_str[3] = { cmd_str[0], cmd_str[1], '\0' };
        data[cmd_len++] = (uint8_t)strtol(byte_str, NULL, 16);
        cmd_str += 2;
    }

    if (data[0] == 0x0C) {
        printf("需要通过串口发送: ");
        for (int i = 0; i < cmd_len; i++) { printf("%02X ", data[i]);}
        printf("\n");
    }

    // TODO: 在这里添加具体的消息处理逻辑
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
            mqtt_ble_data_parser_cb(&msg);
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

        
        ESP_LOGI(TAG, "📋 执行定期业务逻辑...");
        
        // 任务延时（可根据需要调整）
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10秒
    }
    vTaskDelete(NULL);
}

static void wifi_status_callback(wifi_manager_event_t event, void *event_data)
{
    switch (event) {
        case WIFI_MANAGER_EVENT_CONNECTED:
            wifi_ready = true;
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "📶 WiFi连接成功: IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
            // WiFi连接成功后启动NTP
            if (!ntp_ready) {
                ntp_manager_start();
            }
            break;
            
        case WIFI_MANAGER_EVENT_DISCONNECTED:
            wifi_ready = false;
            ntp_ready = false;   // WiFi断开时NTP也不可用（假装）
            mqtt_ready = false;  // WiFi断开时MQTT也会断开
            ESP_LOGW(TAG, "📶 WiFi连接断开");
            break;
    }
}

static void ntp_status_callback(ntp_manager_event_t event, void *event_data)
{
    switch (event) {
        case NTP_MANAGER_EVENT_TIME_SYNCED:
            ntp_ready = true;
            ESP_LOGI(TAG, "🕐 时间同步成功");
            // 时间同步成功后启动MQTT
            if (wifi_ready) {
                mqtt_manager_start();
            }
            break;
            
        case NTP_MANAGER_EVENT_SYNC_FAILED:
            ESP_LOGW(TAG, "🕐 时间同步失败");
            break;
    }
}

static void mqtt_status_callback(mqtt_manager_event_t event, void *event_data)
{
    cJSON *root;
    cJSON *secondItem;
    cJSON *mccil_item;

    switch (event) {
        case MQTT_MANAGER_EVENT_CONNECTED:
            mqtt_ready = true;
            ESP_LOGI(TAG, "📡 MQTT连接成功");
            
            // 订阅涂鸦命令主题
            char subscribe_topic[128];
            snprintf(subscribe_topic, sizeof(subscribe_topic), "tylink/%s/thing/property/set", TUYA_DEVICE_ID);
            mqtt_manager_subscribe(subscribe_topic, 0);
            ESP_LOGI(TAG, "📥 订阅命令主题: %s", subscribe_topic);

            // 发送在线状态
            char publish_topic[128];
            snprintf(publish_topic, sizeof(publish_topic), "tylink/%s/thing/property/report", TUYA_DEVICE_ID);
            mqtt_manager_publish(publish_topic, "{\"properties\":{\"online\":true}}", 1);
            break;
            
        case MQTT_MANAGER_EVENT_DISCONNECTED:
            mqtt_ready = false;
            ESP_LOGW(TAG, "📡 MQTT连接断开");
            break;
            
        case MQTT_MANAGER_EVENT_DATA_RECEIVED:
        {
            mqtt_manager_data_t *data = (mqtt_manager_data_t *)event_data;
            ESP_LOGI(TAG, "收到MQTT消息: %.*s", data->data_len, data->data);

            root = cJSON_ParseWithLength(data->data, data->data_len);
            if (root)
            {
                secondItem = cJSON_GetObjectItem(root, "data");
                if (cJSON_IsObject(secondItem))
                {
                    mccil_item = cJSON_GetObjectItem(secondItem, "mccil");
                    if (cJSON_IsString(mccil_item) && (mccil_item->valuestring != NULL))
                    {
                        size_t mccil_len = strlen(mccil_item->valuestring);
                        ESP_LOGI(TAG, "解析到 mccil: %s, length: %u", mccil_item->valuestring, (unsigned)mccil_len);

                        g_msg_queue_t msg = {
                            .source = MSG_SOURCE_MQTT,
                            .data_len = (mccil_len < MAX_MSG_SIZE) ? mccil_len : MAX_MSG_SIZE,
                        };
                        memcpy(msg.data, mccil_item->valuestring, msg.data_len);
                        xQueueSend(g_msg_queue, &msg, portMAX_DELAY);
                    }
                    else
                    {
                        ESP_LOGW(TAG, "mccil 字段不存在或类型错误");
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "data 对象不存在或类型错误");
                }
                cJSON_Delete(root);
            }
            else
            {
                ESP_LOGW(TAG, "JSON 解析失败");
            }
        }
        break;

    }
}

/**
 * @brief 启动解耦的网络和时间服务
 */
esp_err_t system_services_start(void)
{
    ESP_LOGI(TAG, "🚀 启动系统服务");
    
    // 初始化各个管理器
    ESP_ERROR_CHECK(wifi_manager_init(wifi_status_callback));
    ESP_ERROR_CHECK(ntp_manager_init(ntp_status_callback));
    ESP_ERROR_CHECK(mqtt_manager_init(mqtt_status_callback));
    
    // 启动WiFi（其他服务将通过回调链式启动）
    ESP_ERROR_CHECK(wifi_manager_start());
    
    return ESP_OK;
}

/**
 * @brief 启动APP主业务任务
 */
esp_err_t app_main_start(void)
{
    BaseType_t ret1 = xTaskCreate(message_task, "msg_task", 1024*6, NULL, 6, &message_task_handle);
    if (ret1 != pdPASS) { ESP_LOGE(TAG, "创建消息处理任务失败");return ESP_FAIL;}

    BaseType_t ret2 = xTaskCreate(iot_task, "iot_task", 1024*2, NULL, 5, &iot_task_handle);
    if (ret2 != pdPASS) {ESP_LOGE(TAG, "创建IoT业务任务失败");return ESP_FAIL;}

    return ESP_OK;
}
