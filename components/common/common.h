#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 全局配置参数 ========== */

/* WiFi配置 */
#define WIFI_SSID               "kakakaka"
#define WIFI_PASSWORD           "fzh990112"
#define WIFI_MAXIMUM_RETRY      30

/* 涂鸦IoT MQTT配置 */
#define TUYA_PRODUCT_ID         "owes0z4baov2vqgx"
#define TUYA_MQTT_URL           "mqtts://m1.tuyacn.com:8883"
#define TUYA_DEVICE_ID          "2631a16994c01c1f45qfha"
#define TUYA_DEVICE_SECRET      "ecw9VrT7fLlgP6br"

/* BLE配置 */
#define BLE_DEVICE_NAME         "ESP32C5_BLE_SERVER"
#define BLE_SERVICE_UUID        0x00FF
#define BLE_CHAR_UUID           0xFF01

/* 事件组位定义 */
#define WIFI_CONNECTED_BIT      (1UL << 0)
#define WIFI_FAIL_BIT          (1UL << 1)
#define MQTT_CONNECTED_BIT     (1UL << 2)
#define MQTT_FAIL_BIT          (1UL << 3)
#define SNTP_SYNCED_BIT        (1UL << 4)

/* ========== 数据结构定义 ========== */

// IOT设备状态结构体
typedef struct {
    char device_status[32];  // 设备状态，如"open", "close"
    int32_t test_value;      // 测试数值
} iot_device_state_t;

// 全局状态变量声明
extern iot_device_state_t g_iot_state;

/**
 * @brief 初始化全局状态
 */
void common_init(void);

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
