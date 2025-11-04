#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 全局配置参数 ========== */
/*设备信息 */
#define DEVICE_NAME             "ESP32C5_TEST_1"


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

/* 固件版本配置 */
// 注意：版本号必须采用 xx.yy.zz 格式（涂鸦OTA协议要求），范围 0.0.0 到 99.99.99
// 请确保与 CMakeLists.txt 中的 PROJECT_VER 保持一致
#define FIRMWARE_VERSION_MAIN   "1.0.3"  // 主模块固件版本（原QS_TUYA_ESPC5_V101）
#define FIRMWARE_VERSION_MCU    "1.0.3"  // MCU固件版本（原QS_MCU_V100）
#define OTA_CHANNEL_MAIN        0        // 主模块固件通道
#define OTA_CHANNEL_MCU         9        // MCU固件通道

/* ========== 数据结构定义 ========== */

/* 消息来源枚举 */
typedef enum {
    MSG_SOURCE_BLE = 1,
    MSG_SOURCE_MQTT = 2
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

typedef struct
{
    bool flag;
    uint16_t gatts_if;
    uint16_t conn_id;
    uint16_t handle;
    QueueHandle_t xQueue;
} ble_link_info_t;


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
    uint8_t device_mac[6];
    ble_link_info_t *ble;
    tuya_link_info_t tuya;
    wifi_link_info_t wifi;
    wifi_link_info_t renet_wifi;
} device_info_t;


// 全局变量声明
extern device_info_t *device_info;
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


#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */
