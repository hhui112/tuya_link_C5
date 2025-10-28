#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 全局配置参数 ========== */
/*设备信息 */
#define DEVICE_NAME             "ESP32C5_TETS_1"


/* WiFi配置 */
#define WIFI_SSID               "7788"
#define WIFI_PASSWORD           "77885522"
#define WIFI_MAXIMUM_RETRY      30

/* 涂鸦IoT MQTT配置 */
#define TUYA_PRODUCT_ID         "owes0z4baov2vqgx"
#define TUYA_DEVICE_ID          "2631a16994c01c1f45qfha"
#define TUYA_DEVICE_SECRET      "ecw9VrT7fLlgP6br"
#define TUYA_MQTT_URL           "mqtts://m1.tuyacn.com:8883"

/* BLE配置 */
#define BLE_SERVICE_UUID        0x00FF
#define BLE_CHAR_UUID           0xFF01

/* 事件组位定义 */
#define WIFI_CONNECTED_BIT      (1UL << 0)
#define WIFI_FAIL_BIT          (1UL << 1)
#define MQTT_CONNECTED_BIT     (1UL << 2)
#define MQTT_FAIL_BIT          (1UL << 3)
#define SNTP_SYNCED_BIT        (1UL << 4)

/* ========== 数据结构定义 ========== */

/* 消息来源枚举 */
typedef enum {
    MSG_SOURCE_BLE = 1,
    MSG_SOURCE_MQTT = 2,
    MSG_SOURCE_WIFI = 3
} msg_source_t;

/* 消息类型枚举 */
typedef enum {
    MSG_TYPE_CONTROL = 0,       // 控制指令（MQTT/BLE通用）
    MSG_TYPE_BLE_CONFIG = 1,    // BLE配网数据
    MSG_TYPE_STATUS = 2         // 状态上报（预留）
} msg_type_t;

/* 统一消息结构体 */
#define MAX_MSG_SIZE 256
typedef struct {
    msg_source_t source;            // 消息来源
    msg_type_t type;                // 消息类型
    uint16_t data_len;              // 数据长度
    uint8_t data[MAX_MSG_SIZE];     // 消息内容
} g_msg_queue_t;

typedef struct
{
    char product_id[20];
    char device_id[20];
    char device_secret[20];
} tuya_config_t;

// IOT设备状态结构体
typedef struct {
    char device_status[32];  // 设备状态，如"open", "close"
    int32_t test_value;      // 测试数值
} iot_device_state_t;

typedef struct
{
    char id[20];
    uint8_t type;
    uint8_t value[512];
    uint16_t len;
} data_rec_t;

typedef struct
{
    bool flag;
    uint16_t gatts_if;
    uint16_t conn_id;
    uint16_t handle;
    QueueHandle_t xQueue;
    data_rec_t data_rec;
}ble_link_info_t;

typedef struct
{
    bool flag;
    data_rec_t data_rec;

}mqtt_link_info_t;


//wifi信息定义
typedef struct
{        
    uint8_t flag;               // wifi连接标志位
    char ip_addr[16];           // ip地址
    int rssi;                   // wifi信号强度
    char ssid[64];              // wifi名
    char passwd[64];            // wifi密码
}wifi_link_info_t;

typedef struct
{
    char product_id[30];
    char device_id[30];
    char device_secret[30];

}tuya_link_info_t;

typedef struct {
    char id[30];
    ble_link_info_t *ble;
    tuya_link_info_t tuya;
    wifi_link_info_t wifi;
    wifi_link_info_t renet_wifi;
} device_info_t;


// 全局状态变量声明
extern iot_device_state_t g_iot_state;
extern device_info_t *device_info;

/* BLE/MQTT消息队列 */
extern QueueHandle_t g_msg_queue;

/**
 * @brief 初始化全局状态
 */
void device_init(void);

/**
 * @brief 配置存储到Flash
 */
void config_store_to_flash(void);

/**
 * @brief 配网成功后将 ssid 和 password 配置存储到Flash
 */
void wifi_config_store_to_flash(void);

/**
 * @brief 获取当前IOT设备状态
 * 
 * @param device_status 输出缓冲区，存储设备状态字符串
 * @param status_size 缓冲区大小
 * @param test_value 输出参数，存储测试数值
 */
void get_current_iot_state(char* device_status, size_t status_size, int32_t* test_value);

/**
 * @brief 更新设备状态
 * 
 * @param device_status 新的设备状态字符串
 */
void set_device_status(const char* device_status);

/**
 * @brief 更新测试数值
 * 
 * @param test_value 新的测试数值
 */
void set_test_value(int32_t test_value);


#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */
