#include "app_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "esp_mac.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "utctime.h"
#include "common.h"
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "use_uart.h"
#include "use_ble_server.h"
#include "ota_manager.h"

static const char *TAG = "APP_MAIN";

/* 看门狗配置 */
#define WATCHDOG_TIMEOUT_SEC    30  // 看门狗超时时间（秒）

/* 任务句柄 */
static TaskHandle_t message_task_handle = NULL;

/* 系统状态 */
static bool wifi_ready = false;
static bool ntp_ready = false;
static bool mqtt_ready = false;

/* ========== 消息处理框架 ========== */

/**
 * @brief WiFi重配置前的准备工作（回调函数）
 * 用于停止依赖WiFi的服务，解耦wifi_manager和mqtt_manager
 */
static void wifi_pre_reconfig_handler(void)
{
    // 停止MQTT连接
    if (mqtt_manager_is_connected()) {
        mqtt_manager_stop();
    }
    // 可以在这里添加其他需要停止的服务
}

/**
 * @brief 发送配网结果到BLE客户端
 * @param success 配网是否成功
 * @param ip_str IP地址（成功时传入，失败时传NULL）
 */
static void send_config_result(bool success, const char* ip_str)
{
    if (!use_ble_server_is_connected()) {
        ESP_LOGW(TAG, "BLE未连接，跳过发送配网结果");
        return;
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "net_config_response");
    cJSON_AddStringToObject(root, "status", success ? "success" : "failed");
    
    cJSON *data = cJSON_CreateObject();
    if (success && ip_str) {
        cJSON_AddStringToObject(data, "ip", ip_str);
    } else {
        cJSON_AddStringToObject(data, "message", "配网失败");
    }
    cJSON_AddItemToObject(root, "data", data);
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        use_ble_server_notify_data((uint8_t*)json_str, strlen(json_str));
        ESP_LOGI(TAG, "📤 发送配网结果: %s", json_str);
        free(json_str);
    }
    cJSON_Delete(root);
}

/**
 * @brief 处理BLE配网指令
 * @param data_obj JSON中的data对象
 */
static void handle_ble_net_config(cJSON *data_obj)
{
    cJSON *ssid_item = cJSON_GetObjectItem(data_obj, "ssid");
    cJSON *pwd_item = cJSON_GetObjectItem(data_obj, "password");
    
    if (!ssid_item || !cJSON_IsString(ssid_item) || 
        !pwd_item || !cJSON_IsString(pwd_item)) {
        ESP_LOGW(TAG, "⚠️ ssid或password字段缺失或格式错误");
        return;
    }
    
    const char *ssid = ssid_item->valuestring;
    const char *password = pwd_item->valuestring;
    
    ESP_LOGI(TAG, "🔧 收到BLE配网指令 (SSID: %s)", ssid);
    
    // 存入device_info（内存）
    strncpy(device_info->wifi.ssid, ssid, sizeof(device_info->wifi.ssid) - 1);
    device_info->wifi.ssid[sizeof(device_info->wifi.ssid) - 1] = '\0';
    strncpy(device_info->wifi.passwd, password, sizeof(device_info->wifi.passwd) - 1);
    device_info->wifi.passwd[sizeof(device_info->wifi.passwd) - 1] = '\0';
    
    // 触发WiFi重配置（传入准备回调，让应用层停止MQTT等服务）
    wifi_manager_reconfigure(device_info->wifi.ssid, device_info->wifi.passwd, wifi_pre_reconfig_handler);
}

/**
 * @brief 处理BLE控制数据
 * @param data_obj JSON中的data对象
 */
static void handle_ble_control_data(cJSON *data_obj)
{
    if (!data_obj || !cJSON_IsObject(data_obj)) {
        ESP_LOGW(TAG, "⚠️ uart_control的data字段不是对象");
        return;
    }
    
    ESP_LOGI(TAG, "📋 处理BLE控制数据");
    
    // 提取mccil字段（十六进制字符串）
    cJSON *mccil_item = cJSON_GetObjectItem(data_obj, "mccil");
    if (mccil_item && cJSON_IsString(mccil_item)) {
        ESP_LOGI(TAG, "   mccil: %s", mccil_item->valuestring);
    }
    
    // 提取其他字段（可选，框架层面只打印）
    cJSON *up_item = cJSON_GetObjectItem(data_obj, "up");
    if (up_item && cJSON_IsString(up_item)) {
        ESP_LOGI(TAG, "   up: %s", up_item->valuestring);
    }
    
    cJSON *set_item = cJSON_GetObjectItem(data_obj, "set");
    if (set_item && cJSON_IsString(set_item)) {
        ESP_LOGI(TAG, "   set: %s", set_item->valuestring);
    }
    
    // TODO: 这里添加具体的业务处理逻辑
    // 例如：解析mccil十六进制字符串并发送到串口
}

/**
 * @brief 处理MQTT控制数据
 * @param data_obj JSON中的data对象
 */
static void handle_mqtt_control_data(cJSON *data_obj)
{
    if (!data_obj || !cJSON_IsObject(data_obj)) {
        ESP_LOGW(TAG, "⚠️ MQTT的data字段不是对象");
        return;
    }
    
    ESP_LOGI(TAG, "📋 处理MQTT控制数据");
    
    // 提取mccil字段（十六进制字符串）
    cJSON *mccil_item = cJSON_GetObjectItem(data_obj, "mccil");
    if (mccil_item && cJSON_IsString(mccil_item)) {
        ESP_LOGI(TAG, "   mccil: %s", mccil_item->valuestring);
    }
    
    // 处理开门/上升指令
    cJSON *up = cJSON_GetObjectItem(data_obj, "up");
    if (up && cJSON_IsBool(up)) {
        bool up_value = cJSON_IsTrue(up);
        if (up_value) {
            uart_send_cmd_frame(device_info->device_mac, CMD_OPEN_DOOR);
            ESP_LOGI(TAG, "   开门/上升: true");
        }
    }
    
    // 处理关门/下降指令
    cJSON *down = cJSON_GetObjectItem(data_obj, "down");
    if (down && cJSON_IsBool(down)) {
        bool down_value = cJSON_IsTrue(down);
        if (down_value) {
            uart_send_cmd_frame(device_info->device_mac, CMD_CLOSE_DOOR);
            ESP_LOGI(TAG, "   关门/下降: true");
        }
    }
    
    // 处理通风开关
    cJSON *switch_item = cJSON_GetObjectItem(data_obj, "switch");
    if (switch_item && cJSON_IsBool(switch_item)) {
        bool switch_value = cJSON_IsTrue(switch_item);
        if (switch_value) {
            uart_send_cmd_frame(device_info->device_mac, KEY_VALUE_VENT);
            ESP_LOGI(TAG, "   通风功能: true");
        }
    }
    
    // 处理童锁（写寄存器，0关闭/1开启）
    cJSON *child_lock = cJSON_GetObjectItem(data_obj, "child_lock");
    if (child_lock && cJSON_IsBool(child_lock)) {
        uint8_t lock_value = cJSON_IsTrue(child_lock) ? 1 : 0;
        uart_send_write_register(device_info->device_mac, REG_CHILD_LOCK, lock_value);
        ESP_LOGI(TAG, "   童锁功能: %s", lock_value ? "开启" : "关闭");
    }
    
    // 处理遇阻反弹级别
    cJSON *collision_level = cJSON_GetObjectItem(data_obj, "reg_collision_level");
    if (collision_level && cJSON_IsNumber(collision_level)) {
        uart_send_write_register(device_info->device_mac, REG_OPEN_FORCE, collision_level->valueint);
        ESP_LOGI(TAG, "   遇阻反弹级别: %d", collision_level->valueint);
    }
}

/**
 * @brief 处理MQTT消息（直接提取data对象）
 * @param root 已解析的JSON根对象
 */
static void handle_mqtt_message(cJSON *root)
{
    // MQTT格式: {"data": {...}, "time": 1761534368}
    // 直接提取data对象
    cJSON *data_obj = cJSON_GetObjectItem(root, "data");
    if (!data_obj) {
        ESP_LOGW(TAG, "⚠️ MQTT消息缺少data字段");
        return;
    }
    
    // 调用统一的控制数据处理函数
    handle_mqtt_control_data(data_obj);
}

/**
 * @brief 统一消息解析回调（框架入口）
 * @param msg 来自队列的消息
 */
void mqtt_ble_data_parser_cb(const g_msg_queue_t* msg)
{
    // 参数校验
    if (msg == NULL || msg->data_len == 0) {ESP_LOGW(TAG, "⚠️ 无效消息");return;}
    const char* source_str = (msg->source == MSG_SOURCE_BLE) ? "BLE" : (msg->source == MSG_SOURCE_MQTT) ? "MQTT" : "UNKNOWN";
    ESP_LOGI(TAG, "📥 收到 %s 消息 (%d字节)", source_str, msg->data_len);
    if (msg->data[0] != '{') {ESP_LOGW(TAG, "⚠️ 非JSON格式消息，跳过处理");return;}


    
    // 解析JSON
    cJSON *root = cJSON_ParseWithLength((const char*)msg->data, msg->data_len);
    if (!root) {ESP_LOGW(TAG, "⚠️ JSON解析失败");return;}
    
    switch (msg->source)
    {
    case MSG_SOURCE_MQTT:
        handle_mqtt_message(root);
        break;
    case MSG_SOURCE_BLE:
        // 提取cmd字段
        cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
        if (!cmd_item || !cJSON_IsString(cmd_item)) {ESP_LOGW(TAG, "⚠️ BLE消息缺少cmd字段");break;}
        
        const char *cmd = cmd_item->valuestring;
        ESP_LOGI(TAG, "📌 BLE命令: %s", cmd);
        
        // 提取data字段
        cJSON *data_obj = cJSON_GetObjectItem(root, "data");
        if (!data_obj) {ESP_LOGW(TAG, "⚠️ BLE消息缺少data字段");break;}
        
        // 根据cmd路由到不同的处理函数
        if (strcmp(cmd, "net_config") == 0) 
        {
            handle_ble_net_config(data_obj);
        } else if (strcmp(cmd, "uart_control") == 0) 
        {
            handle_ble_control_data(data_obj);
        } else {
            ESP_LOGW(TAG, "⚠️ 未知的BLE命令: %s", cmd);
        }
        break;

    default:
        ESP_LOGW(TAG, "⚠️ 未知的消息来源: %d", msg->source);
        break;

    }
    cJSON_Delete(root);
}


/**
 * @brief 消息处理任务（高优先级，立即响应）
 */
static void message_task(void *pvParameters)
{
    ESP_LOGI(TAG, "APP业务任务已启动");
    
    // 订阅任务看门狗
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "消息处理任务已订阅看门狗");
    
    g_msg_queue_t msg; 
    while (1) {
        // 喂狗：告诉看门狗任务还活着
        esp_task_wdt_reset();
        
        // 阻塞等待消息，3秒超时（小于看门狗5秒超时）
        if (xQueueReceive(g_msg_queue, &msg, pdMS_TO_TICKS(3000)) == pdTRUE) {
            mqtt_ble_data_parser_cb(&msg);
        }
    }
    vTaskDelete(NULL);
}

/**
 * @brief OTA事件回调函数
 */
static void ota_event_callback(const ota_event_info_t *info)
{
    switch (info->event) {
        case OTA_EVENT_START:
            ESP_LOGI(TAG, "🚀 OTA升级开始");
            ESP_LOGI(TAG, "  - 当前版本: %s", info->current_version);
            ESP_LOGI(TAG, "  - 目标版本: %s", info->target_version);
            break;
            
        case OTA_EVENT_PROGRESS:
            ESP_LOGI(TAG, "📥 OTA下载进度: %d%%", info->progress);
            break;
            
        case OTA_EVENT_DOWNLOAD_COMPLETE:
            ESP_LOGI(TAG, "✅ OTA下载完成，开始验证和升级...");
            break;
            
        case OTA_EVENT_SUCCESS:
            ESP_LOGI(TAG, "🎉 OTA升级成功！设备即将重启...");
            break;
            
        case OTA_EVENT_FAILED:
            ESP_LOGE(TAG, "❌ OTA升级失败");
            ESP_LOGE(TAG, "  - 错误码: %d", info->error_code);
            ESP_LOGE(TAG, "  - 错误信息: %s", info->error_msg);
            break;
    }
}

static void wifi_status_callback(wifi_manager_event_t event, void *event_data)
{
    switch (event) {
        case WIFI_MANAGER_EVENT_CONNECTED:
            wifi_ready = true;
            ip_event_got_ip_t* ip_event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "📶 WiFi连接成功: IP地址:" IPSTR, IP2STR(&ip_event->ip_info.ip));

            // 检查是否是BLE配网操作
            if (wifi_manager_is_reconnected())
            {
                // 配网成功，保存配置到Flash（直接写入新配置）
                wifi_config_store_to_flash();
                ESP_LOGI(TAG, "✅ BLE配网成功，配置已保存到Flash");
                
                // 发送配网成功通知到BLE
                char ip_str[16];
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_event->ip_info.ip));
                send_config_result(true, ip_str);
                
                // 清除配网标志位
                wifi_manager_clear_reconnected_bit();
            }

            // WiFi连接成功后启动NTP（无论是配网还是普通连接）
            if (!ntp_ready) {
                ESP_LOGI(TAG, "🕐 启动NTP时间同步...");
                ntp_manager_start();
            }
            break;
            
        case WIFI_MANAGER_EVENT_DISCONNECTED:
            wifi_ready = false;
            ntp_ready = false;   // WiFi断开时NTP也不可用
            mqtt_ready = false;  // WiFi断开时MQTT也会断开
            ESP_LOGW(TAG, "📶 WiFi连接断开");
            break;
            
        case WIFI_MANAGER_EVENT_PROV_FAILED:
            ESP_LOGW(TAG, "❌ 配网失败");
            // 发送配网失败通知到BLE
            send_config_result(false, NULL);
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
    switch (event) {
        case MQTT_MANAGER_EVENT_CONNECTED:
            mqtt_ready = true;
            ESP_LOGI(TAG, "📡 MQTT连接成功");
            
            // 订阅涂鸦命令主题
            char subscribe_topic[64];
            char ota_topic[128];
            snprintf(subscribe_topic, sizeof(subscribe_topic), "tylink/%s/thing/property/set", TUYA_DEVICE_ID);
            mqtt_manager_subscribe(subscribe_topic, 0);
            
            // 订阅OTA升级主题
            snprintf(ota_topic, sizeof(ota_topic), "tylink/%s/ota/issue", TUYA_DEVICE_ID);
            mqtt_manager_subscribe(ota_topic, 1);
            ESP_LOGI(TAG, "📥 已订阅控制和OTA主题");

            // 发送在线状态
            char publish_topic[64];
            snprintf(publish_topic, sizeof(publish_topic), "tylink/%s/thing/property/report", TUYA_DEVICE_ID);
            mqtt_manager_publish(publish_topic, "{\"properties\":{\"online\":true}}", 1);
            
            // 启动OTA管理器（上报版本、检测回滚）
            ota_manager_start();
            break;
            
        case MQTT_MANAGER_EVENT_DISCONNECTED:
            mqtt_ready = false;
            ESP_LOGW(TAG, "📡 MQTT连接断开");
            break;
            
        case MQTT_MANAGER_EVENT_DATA_RECEIVED:
        {
            mqtt_manager_data_t *data = (mqtt_manager_data_t *)event_data;
            
            // 检查是否为OTA升级消息
            char topic_str[128] = {0};
            if (data->topic_len < sizeof(topic_str)) {
                memcpy(topic_str, data->topic, data->topic_len);
                topic_str[data->topic_len] = '\0';
                
                // 如果是OTA消息，直接处理，不入队
                if (strstr(topic_str, "/ota/issue") != NULL) {
                    // 使用动态分配减少栈压力（MQTT任务栈有限）
                    char *json_data = (char *)malloc(data->data_len + 1);
                    if (!json_data) {
                        ESP_LOGE(TAG, "OTA消息内存分配失败");
                        break;
                    }
                    
                    memcpy(json_data, data->data, data->data_len);
                    json_data[data->data_len] = '\0';
                    
                    ESP_LOGI(TAG, "📨 收到OTA升级消息 (大小: %d字节)", data->data_len);
                    
                    esp_err_t ret = ota_manager_handle_upgrade(json_data);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "OTA升级失败: %s", esp_err_to_name(ret));
                    }
                    
                    free(json_data);  // 释放内存
                    break;  // OTA消息不入队
                }
            }
            
            // 非OTA消息：转发到队列，由统一处理函数解析
            g_msg_queue_t msg = {
                .source = MSG_SOURCE_MQTT,
                .type = MSG_TYPE_CONTROL,
                .data_len = (data->data_len < MAX_MSG_SIZE) ? data->data_len : MAX_MSG_SIZE
            };
            memcpy(msg.data, data->data, msg.data_len);
            
            if (xQueueSend(g_msg_queue, &msg, 0) == pdPASS) {
                ESP_LOGI(TAG, "📨 MQTT消息已入队");
            } else {
                ESP_LOGW(TAG, "⚠️ 消息队列已满");
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
    
    // 初始化OTA管理器
    ota_manager_config_t ota_config = {
        .event_callback = ota_event_callback,
        .auto_check_enable = false,           // 禁用自动检测（可改为true启用）
        .auto_check_interval_hours = 24,      // 24小时检测一次
        .download_buffer_size = 8192,         // 8KB下载缓冲区（提升下载速度）
    };
    ESP_ERROR_CHECK(ota_manager_init(&ota_config));
    ESP_LOGI(TAG, "✅ OTA管理器初始化成功");
    
    // 启动WiFi（其他服务将通过回调链式启动）
    ESP_ERROR_CHECK(wifi_manager_start());
    
    return ESP_OK;
}

/**
 * @brief 初始化任务看门狗
 */
esp_err_t watchdog_init(void)
{
    ESP_LOGI(TAG, "🐕 检查任务看门狗状态...");
    
    // 配置任务看门狗
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000,  // 超时时间（毫秒）
        .idle_core_mask = 0,                         // 不监控空闲任务
        .trigger_panic = true                        // 超时时触发panic重启
    };
    
    esp_err_t ret = esp_task_wdt_init(&twdt_config);
    
    if (ret == ESP_ERR_INVALID_STATE) {
        // 看门狗已经初始化（系统自动初始化），直接使用即可
        ESP_LOGI(TAG, "✅ 任务看门狗已由系统初始化（超时: %d秒）", WATCHDOG_TIMEOUT_SEC);
        return ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "任务看门狗初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ 任务看门狗初始化成功（超时: %d秒）", WATCHDOG_TIMEOUT_SEC);
    return ESP_OK;
}

/**
 * @brief 启动APP主业务任务
 */
esp_err_t app_main_start(void)
{
    BaseType_t ret1 = xTaskCreate(message_task, "msg_task", 1024*6, NULL, 6, &message_task_handle);
    if (ret1 != pdPASS) { ESP_LOGE(TAG, "创建消息处理任务失败");return ESP_FAIL;}

    // 暂时无该任务 先注释掉
    // BaseType_t ret2 = xTaskCreate(iot_task, "iot_task", 1024*2, NULL, 5, &iot_task_handle);
    // if (ret2 != pdPASS) {ESP_LOGE(TAG, "创建IoT业务任务失败");return ESP_FAIL;}

    BaseType_t ret3 = start_uart_receive_task();
    if (ret3 != pdPASS) {ESP_LOGE(TAG, "创建uart业务任务失败");return ESP_FAIL;}

    return ESP_OK;
}
