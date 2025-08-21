#ifndef USE_WIFI_H
#define USE_WIFI_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化并启动WiFi和MQTT连接
 * 
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t use_wifi_start(void);

/**
 * @brief 停止WiFi和MQTT连接
 * 
 * @return esp_err_t 
 */
esp_err_t use_wifi_stop(void);

/**
 * @brief 等待WiFi、SNTP和MQTT连接成功
 * 
 * @param timeout_ms 超时时间（毫秒），0表示永久等待
 * @return esp_err_t ESP_OK表示连接成功，ESP_ERR_TIMEOUT表示超时
 */
esp_err_t use_wifi_wait_connected(uint32_t timeout_ms);

/**
 * @brief 发布传感器数据到涂鸦平台
 * 
 * @param test_value 测试数值
 * @param device_status 设备状态字符串
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t tuya_publish_sensor_data(uint8_t test_value, char* device_status);

/**
 * @brief 发送心跳数据到涂鸦平台
 * 
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t tuya_send_heartbeat(void);

/**
 * @brief 检查完整连接状态
 * 
 * @return true 如果WiFi、SNTP和MQTT都已连接
 * @return false 如果有任何连接断开
 */
bool use_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* USE_WIFI_H */ 