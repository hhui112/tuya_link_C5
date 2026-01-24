#include "app_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "esp_mac.h"
#include "esp_system.h"
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

#if DEBUG_STATUS_PRINT
static TimerHandle_t status_timer_handle = NULL;
#endif

/* 系统状态 */
static bool wifi_ready = false;
static bool ntp_ready = false;
static bool mqtt_ready = false;

/* ========== 属性映射表相关定义 ========== */

// 属性类型枚举
typedef enum {
    PROP_TYPE_ACTION_ENUM,      // 动作类（枚举字符串）
    PROP_TYPE_ACTION_BOOL,      // 动作类（布尔）
    PROP_TYPE_REG_BOOL,         // 寄存器（布尔）
    PROP_TYPE_REG_INT,          // 寄存器（整数）
    PROP_TYPE_REG_ENUM_STR,     // 寄存器（枚举字符串）
} property_type_t;

// 属性处理器函数指针类型
typedef void (*property_handler_t)(cJSON *item, const char *key);

// 属性映射表结构
typedef struct {
    const char *key;                // JSON中的key
    property_type_t type;           // 属性类型
    uint8_t reg_addr;              // 寄存器地址（如果是寄存器类型）
    property_handler_t handler;     // 自定义处理函数（可选）
} property_map_t;

/* ========== 调试状态打印 ========== */

#if DEBUG_STATUS_PRINT
/**
 * @brief 状态打印定时器回调函数
 * 每30秒打印一次设备的连接状态
 */
static void status_print_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;  // 未使用参数
    
    // 获取各模块连接状态
    bool wifi_ok = wifi_manager_is_connected();
    bool ble_ok = use_ble_server_is_connected();
    bool ntp_ok = ntp_manager_is_synced();
    bool mqtt_ok = mqtt_manager_is_connected();
    
    // 获取WiFi信号强度
    int rssi = device_info->wifi.rssi;
    
    // 获取当前时间（如果已同步）
    char time_str[20] = "N/A";
    if (ntp_ok) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    }
    
    // 一行简洁打印所有状态
    ESP_LOGI(TAG, "[STATUS] WiFi:%s BLE:%s NTP:%s MQTT:%s | ID:%.16s... | RSSI:%d | %s",
             wifi_ok ? "1" : "0",
             ble_ok ? "1" : "0",
             ntp_ok ? "1" : "0",
             mqtt_ok ? "1" : "0",
             device_info->tuya.device_id,
             rssi,
             time_str);
}

/**
 * @brief 启动状态打印定时器
 */
static void status_print_timer_start(void)
{
    status_timer_handle = xTimerCreate(
        "status_timer",
        pdMS_TO_TICKS(DEBUG_STATUS_INTERVAL * 1000),  // 间隔时间
        pdTRUE,                                        // 自动重载
        NULL,
        status_print_timer_callback
    );
    
    if (status_timer_handle != NULL) {
        if (xTimerStart(status_timer_handle, 0) == pdPASS) {
            ESP_LOGI(TAG, "Status print started, interval: %d sec", DEBUG_STATUS_INTERVAL);
        }
    }
}
#endif

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
    // 重置MQTT就绪标志，确保配网后重新连接
    mqtt_ready = false;
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
        ESP_LOGW(TAG, "BLE not connected, skip sending config result");
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
        ESP_LOGI(TAG, "Send config result: %s", json_str);
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
        ESP_LOGW(TAG, "ssid or password字段缺失或格式错误");
        return;
    }
    
    const char *ssid = ssid_item->valuestring;
    const char *password = pwd_item->valuestring;
    
    ESP_LOGI(TAG, "BLE config received, SSID: %s", ssid);
    
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
        ESP_LOGW(TAG, "BLE control data invalid");
        return;
    }
    
    // TODO: 这里添加具体的业务处理逻辑
    // 例如：解析mccil十六进制字符串并发送到串口
}

/* ========== 属性处理函数 ========== */

/**
 * @brief 处理门体控制（枚举：open/close/stop）
 */
static void handle_door_control(cJSON *item, const char *key)
{
    if (!cJSON_IsString(item)) return;
    
    const char *value = cJSON_GetStringValue(item);
    if (strcmp(value, "open") == 0) {
        uart_send_cmd_frame(device_info->device_mac, CMD_OPEN_DOOR);
        ESP_LOGI(TAG, "Door control: open");
    } else if (strcmp(value, "close") == 0) {
        uart_send_cmd_frame(device_info->device_mac, CMD_CLOSE_DOOR);
        ESP_LOGI(TAG, "Door control: close");
    } else if (strcmp(value, "stop") == 0) {
        uart_send_cmd_frame(device_info->device_mac, CMD_STOP_DOOR);
        ESP_LOGI(TAG, "Door control: stop");
    } else {
        ESP_LOGW(TAG, "Door control: unknown value '%s'", value);
    }
}

/**
 * @brief 处理通风开关
 */
static void handle_switch(cJSON *item, const char *key)
{
    if (!cJSON_IsBool(item)) return;
    
    if (cJSON_IsTrue(item)) {
        uart_send_key_frame(device_info->device_mac, KEY_VALUE_VENT);
        ESP_LOGI(TAG, "Ventilation: on");
    }
    }

/**
 * @brief 处理布尔型寄存器（通用）
 */
static void handle_reg_bool(cJSON *item, uint8_t reg_addr, const char *name)
{
    if (!cJSON_IsBool(item)) return;
    
    uint8_t value = cJSON_IsTrue(item) ? 1 : 0;
    uart_send_write_register(device_info->device_mac, reg_addr, value);
    ESP_LOGI(TAG, "Register %s: %d", name, value);
}

/**
 * @brief 处理整数型寄存器（通用）
 */
static void handle_reg_int(cJSON *item, uint8_t reg_addr, const char *name)
{
    if (!cJSON_IsNumber(item)) return;
    
    int value = item->valueint;
    uart_send_write_register(device_info->device_mac, reg_addr, (uint8_t)value);
    ESP_LOGI(TAG, "Register %s: %d", name, value);
}

/**
 * @brief 处理关门速度（枚举字符串映射到数值）
 */
static void handle_close_speed(cJSON *item, const char *key)
{
    if (!cJSON_IsString(item)) return;
    
    const char *speed_str = cJSON_GetStringValue(item);
    uint8_t speed_value = 0;
    
    // 字符串映射：50->5, 60->6, 70->7, 80->8, 90->9, 100->0
    if (strcmp(speed_str, "50") == 0) speed_value = 5;
    else if (strcmp(speed_str, "60") == 0) speed_value = 6;
    else if (strcmp(speed_str, "70") == 0) speed_value = 7;
    else if (strcmp(speed_str, "80") == 0) speed_value = 8;
    else if (strcmp(speed_str, "90") == 0) speed_value = 9;
    else if (strcmp(speed_str, "100") == 0) speed_value = 0;
    else {
        ESP_LOGW(TAG, "Close speed: unknown value '%s'", speed_str);
        return;
    }
    
    uart_send_write_register(device_info->device_mac, REG_CLOSE_SPEED, speed_value);
    ESP_LOGI(TAG, "Close speed: %s%%", speed_str);
}

/**
 * @brief 处理安装方向（forward/reversal -> 0/1）
 */
static void handle_install_dir(cJSON *item, const char *key)
{
    if (!cJSON_IsString(item)) return;
    
    const char *dir_str = cJSON_GetStringValue(item);
    uint8_t dir_value = 0;
    
    if (strcmp(dir_str, "forward") == 0) {
        dir_value = 0;
        ESP_LOGI(TAG, "Install dir: forward");
    } else if (strcmp(dir_str, "reversal") == 0) {
        dir_value = 1;
        ESP_LOGI(TAG, "Install dir: reversal");
    } else {
        ESP_LOGW(TAG, "Install dir: unknown value '%s'", dir_str);
        return;
    }
    
    uart_send_write_register(device_info->device_mac, REG_INSTALL_DIR, dir_value);
}

/**
 * @brief 处理庭院模式（close/mode_1/mode_2 -> 0/1/2）
 */
static void handle_courtyard_mode(cJSON *item, const char *key)
{
    if (!cJSON_IsString(item)) return;
    
    const char *mode_str = cJSON_GetStringValue(item);
    uint8_t mode_value = 0;
    
    if (strcmp(mode_str, "close") == 0) {
        mode_value = 0;
        ESP_LOGI(TAG, "Courtyard mode: close");
    } else if (strcmp(mode_str, "mode_1") == 0) {
        mode_value = 1;
        ESP_LOGI(TAG, "Courtyard mode: mode_1");
    } else if (strcmp(mode_str, "mode_2") == 0) {
        mode_value = 2;
        ESP_LOGI(TAG, "Courtyard mode: mode_2");
    } else {
        ESP_LOGW(TAG, "Courtyard mode: unknown value '%s'", mode_str);
        return;
    }
    
    uart_send_write_register(device_info->device_mac, REG_COURTYARD_MODE, mode_value);
}

/* ========== 属性映射表 ========== */

static const property_map_t property_map[] = {
    // 动作类
    {"door_cnl",            PROP_TYPE_ACTION_ENUM,   0,                      handle_door_control},
    {"switch",              PROP_TYPE_ACTION_BOOL,   0,                      handle_switch},
    
    // 寄存器类 - 布尔型
    {"child_lock",          PROP_TYPE_REG_BOOL,      REG_CHILD_LOCK,         NULL},
    {"r_child_lock",        PROP_TYPE_REG_BOOL,      REG_CHILD_LOCK,         NULL},
    {"r_ir_prot",           PROP_TYPE_REG_BOOL,      REG_INFRARED_PROTECT,   NULL},
    {"r_e_lock",            PROP_TYPE_REG_BOOL,      REG_ELECTRIC_LOCK,      NULL},
    {"r_rlrn_en",           PROP_TYPE_REG_BOOL,      REG_REMOTE_LEARN,       NULL},
    {"r_stop_term",         PROP_TYPE_REG_BOOL,      REG_STOP_TERMINAL,      NULL},
    
    // 寄存器类 - 整数型
    {"r_col_lvl",           PROP_TYPE_REG_INT,       REG_COLLISION_LEVEL,    NULL},
    {"r_auto_close_t",      PROP_TYPE_REG_INT,       REG_AUTO_CLOSE_MIN,     NULL},
    {"r_open_frc",          PROP_TYPE_REG_INT,       REG_OPEN_FORCE,         NULL},
    {"r_vent_pos",          PROP_TYPE_REG_INT,       REG_VENT_POSITION,      NULL},
    {"r_follow_fnc",        PROP_TYPE_REG_INT,       REG_FOLLOW_FUNC,        NULL},
    {"reg_maintenance_level", PROP_TYPE_REG_INT,     REG_MAINTENANCE_LEVEL,  NULL},
    
    // 寄存器类 - 枚举字符串型（需要自定义处理）
    {"r_close_spd",         PROP_TYPE_REG_ENUM_STR,  0,                      handle_close_speed},
    {"r_inst_dir",          PROP_TYPE_REG_ENUM_STR,  0,                      handle_install_dir},
    {"r_ctyard_mode",       PROP_TYPE_REG_ENUM_STR,  0,                      handle_courtyard_mode},
    
    // 表结束标记
    {NULL, 0, 0, NULL}
};

/**
 * @brief 处理MQTT控制数据（优化版：循环+映射表）
 * @param data_obj JSON中的data对象
 */
static void handle_mqtt_control_data(cJSON *data_obj)
{
    if (!data_obj || !cJSON_IsObject(data_obj)) {
        ESP_LOGW(TAG, "MQTT data invalid");
        return;
    }
    
    int processed_count = 0;
    int total_count = 0;
    
    // 遍历data对象的所有子项
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, data_obj) {
        const char *key = item->string;
        
        if (key == NULL) continue;
        
        total_count++;
        
        // 在映射表中查找对应的处理器
        bool handled = false;
        for (int i = 0; property_map[i].key != NULL; i++) {
            if (strcmp(key, property_map[i].key) == 0) {
                const property_map_t *prop = &property_map[i];
                
                // 根据类型处理
                switch (prop->type) {
                    case PROP_TYPE_ACTION_ENUM:
                    case PROP_TYPE_ACTION_BOOL:
                    case PROP_TYPE_REG_ENUM_STR:
                        if (prop->handler) {
                            prop->handler(item, key);
                            processed_count++;
                        }
                        break;
                        
                    case PROP_TYPE_REG_BOOL:
                        handle_reg_bool(item, prop->reg_addr, key);
                        processed_count++;
                        break;
                        
                    case PROP_TYPE_REG_INT:
                        handle_reg_int(item, prop->reg_addr, key);
                        processed_count++;
                        break;
                }
                
                handled = true;
                break;
            }
        }
    }
    
    if (processed_count > 0) {
        ESP_LOGI(TAG, "MQTT control processed: %d/%d", processed_count, total_count);
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
        ESP_LOGW(TAG, "MQTT message missing data field");
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
    if (msg == NULL || msg->data_len == 0) {
        ESP_LOGW(TAG, "Invalid message");
        return;
    }
    
    if (msg->data[0] != '{') {
        ESP_LOGW(TAG, "Non-JSON message, skip");
        return;
    }
    
    // 解析JSON
    cJSON *root = cJSON_Parse((const char*)msg->data);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return;
    }
    
    switch (msg->source)
    {
    case MSG_SOURCE_MQTT:
        handle_mqtt_message(root);
        break;
    case MSG_SOURCE_BLE:
        cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
        if (!cmd_item || !cJSON_IsString(cmd_item)) {
            ESP_LOGW(TAG, "BLE message missing cmd field");
            break;
        }
        
        const char *cmd = cmd_item->valuestring;
        cJSON *data_obj = cJSON_GetObjectItem(root, "data");
        if (!data_obj) {
            ESP_LOGW(TAG, "BLE message missing data field");
            break;
        }
        
        if (strcmp(cmd, "net_config") == 0) {
            handle_ble_net_config(data_obj);
        } else if (strcmp(cmd, "uart_control") == 0) {
            handle_ble_control_data(data_obj);
        } else {
            ESP_LOGW(TAG, "Unknown BLE command: %s", cmd);
        }
        break;

    default:
        ESP_LOGW(TAG, "Unknown message source: %d", msg->source);
        break;
    }
    cJSON_Delete(root);
}


/**
 * @brief 消息处理任务（高优先级，立即响应）
 */
static void message_task(void *pvParameters)
{
    esp_task_wdt_add(NULL);
    
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
            ESP_LOGI(TAG, "OTA start, current: %s, target: %s", 
                     info->current_version, info->target_version);
            break;
            
        case OTA_EVENT_PROGRESS:
            if (info->progress % 10 == 0) {  // 每10%打印一次
                ESP_LOGI(TAG, "OTA progress: %d%%", info->progress);
            }
            break;
            
        case OTA_EVENT_DOWNLOAD_COMPLETE:
            ESP_LOGI(TAG, "OTA download complete");
            break;
            
        case OTA_EVENT_SUCCESS:
            ESP_LOGI(TAG, "OTA success, restarting");
            break;
            
        case OTA_EVENT_FAILED:
            ESP_LOGE(TAG, "OTA failed, code: %d, msg: %s", 
                     info->error_code, info->error_msg);
            break;
    }
}

static void wifi_status_callback(wifi_manager_event_t event, void *event_data)
{
    switch (event) {
        case WIFI_MANAGER_EVENT_CONNECTED:
            wifi_ready = true;
            ip_event_got_ip_t* ip_event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&ip_event->ip_info.ip));

            if (wifi_manager_is_reconnected())
            {
                wifi_config_store_to_flash();
                ESP_LOGI(TAG, "BLE config success, saved to flash");
                
                char ip_str[16];
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_event->ip_info.ip));
                send_config_result(true, ip_str);
                
                wifi_manager_clear_reconnected_bit();
                
                ESP_LOGI(TAG, "Restart in 1s to apply new config");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
                return;
            }

            if (!ntp_ready) {
                ntp_manager_start();
            } else {
                if (!mqtt_ready) {
                    esp_err_t ret = mqtt_manager_start();
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(ret));
                    }
                }
            }
            break;
            
        case WIFI_MANAGER_EVENT_DISCONNECTED:
            wifi_ready = false;
            ntp_ready = false;
            mqtt_ready = false;
            ESP_LOGW(TAG, "WiFi disconnected");
            break;
            
        case WIFI_MANAGER_EVENT_PROV_FAILED:
            ESP_LOGW(TAG, "Config failed");
            send_config_result(false, NULL);
            break;
    }
}

static void ntp_status_callback(ntp_manager_event_t event, void *event_data)
{
    switch (event) {
        case NTP_MANAGER_EVENT_TIME_SYNCED:
            ntp_ready = true;
            ESP_LOGI(TAG, "NTP synced");
            if (wifi_ready && !mqtt_ready) {
                esp_err_t ret = mqtt_manager_start();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(ret));
                }
            }
            break;
            
        case NTP_MANAGER_EVENT_SYNC_FAILED:
            ESP_LOGW(TAG, "NTP sync failed");
            break;
    }
}

static void mqtt_status_callback(mqtt_manager_event_t event, void *event_data)
{
    switch (event) {
        case MQTT_MANAGER_EVENT_CONNECTED:
            mqtt_ready = true;
            ESP_LOGI(TAG, "MQTT connected");
            
            char subscribe_topic[64];
            char ota_topic[128];
            snprintf(subscribe_topic, sizeof(subscribe_topic), "tylink/%s/thing/property/set", device_info->tuya.device_id);
            mqtt_manager_subscribe(subscribe_topic, 0);
            
            snprintf(ota_topic, sizeof(ota_topic), "tylink/%s/ota/issue", device_info->tuya.device_id);
            mqtt_manager_subscribe(ota_topic, 1);

            char publish_topic[64];
            snprintf(publish_topic, sizeof(publish_topic), "tylink/%s/thing/property/report", device_info->tuya.device_id);
            mqtt_manager_publish(publish_topic, "{\"properties\":{\"online\":true}}", 1);
            
            ota_manager_start();
            break;
            
        case MQTT_MANAGER_EVENT_DISCONNECTED:
            mqtt_ready = false;
            ESP_LOGW(TAG, "MQTT disconnected");
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
                    
                    ESP_LOGI(TAG, "OTA message received, size: %d", data->data_len);
                    
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
                .data_len = (data->data_len < MAX_MSG_SIZE - 1) ? data->data_len : (MAX_MSG_SIZE - 1)
            };
            memcpy(msg.data, data->data, msg.data_len);
            msg.data[msg.data_len] = '\0';  // ✅ 添加null结尾符，确保字符串有效
            
            if (xQueueSend(g_msg_queue, &msg, 0) != pdPASS) {
                ESP_LOGW(TAG, "Message queue full");
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
    ESP_ERROR_CHECK(wifi_manager_init(wifi_status_callback));
    ESP_ERROR_CHECK(ntp_manager_init(ntp_status_callback));
    ESP_ERROR_CHECK(mqtt_manager_init(mqtt_status_callback));
    
    ota_manager_config_t ota_config = {
        .event_callback = ota_event_callback,
        .auto_check_enable = false,
        .auto_check_interval_hours = 24,
        .download_buffer_size = 8192,
    };
    ESP_ERROR_CHECK(ota_manager_init(&ota_config));
    
    ESP_ERROR_CHECK(wifi_manager_start());
    
    return ESP_OK;
}

/**
 * @brief 初始化任务看门狗
 */
esp_err_t watchdog_init(void)
{
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    
    esp_err_t ret = esp_task_wdt_init(&twdt_config);
    
    if (ret == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
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

#if DEBUG_STATUS_PRINT
    // 启动状态打印定时器
    status_print_timer_start();
#endif

    return ESP_OK;
}

