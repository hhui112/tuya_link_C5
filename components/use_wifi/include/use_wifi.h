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
 * @brief 等待WiFi和MQTT连接成功
 * 
 * @param timeout_ms 超时时间（毫秒），0表示永久等待
 * @return esp_err_t ESP_OK表示连接成功，ESP_ERR_TIMEOUT表示超时
 */
esp_err_t use_wifi_wait_connected(uint32_t timeout_ms);

/**
 * @brief 发布传感器数据到涂鸦平台
 * 
 * @param temperature 温度值
 * @param humidity 湿度值
 * @return esp_err_t 
 */
esp_err_t tuya_publish_sensor_data(float temperature, float humidity);

/**
 * @brief 发送心跳数据
 * 
 * @return esp_err_t 
 */
esp_err_t tuya_send_heartbeat(void);

/**
 * @brief 检查连接状态
 * 
 * @return true 如果WiFi和MQTT都已连接
 * @return false 如果有任何连接断开
 */
bool use_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* USE_WIFI_H */ 