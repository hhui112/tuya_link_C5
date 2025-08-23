/*
 * ESP32-C5 简洁 BLE 服务器头文件
 * 基于 ESP-IDF NimBLE GATT Server 官方示例
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE 服务器
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t use_ble_server_init(void);

/**
 * @brief 启动 BLE 服务器
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t use_ble_server_start(void);

/**
 * @brief 检查设备连接状态
 * @return true 已连接，false 未连接
 */
bool use_ble_server_is_connected(void);

/**
 * @brief 获取连接句柄
 * @return 连接句柄（0表示未连接）
 */
uint16_t use_ble_server_get_conn_handle(void);

/**
 * @brief 更新特征值数据
 * @param data 数据指针
 * @param len 数据长度
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t use_ble_server_notify_data(const uint8_t* data, uint16_t len);

/**
 * @brief 更新设备状态（兼容接口）
 * @param status 状态字符串
 * @param value 数值
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t use_ble_server_update_device_status(const char* status, int32_t value);

/**
 * @brief 获取当前连接的 MTU 大小
 * @return MTU 字节数（未连接时返回默认值23）
 */
uint16_t use_ble_server_get_mtu(void);

/**
 * @brief 获取单次传输的最大数据长度
 * @return 最大数据字节数（MTU - 3字节ATT开销）
 */
uint16_t use_ble_server_get_max_data_len(void);

#ifdef __cplusplus
}
#endif