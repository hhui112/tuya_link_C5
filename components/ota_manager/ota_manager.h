/**
 * @file ota_manager.h
 * @brief OTA管理器 - 处理固件升级的核心模块
 * 
 * 功能：
 * - 设备启动时上报固件版本
 * - 接收并处理云端推送的OTA升级
 * - 下载固件并验证
 * - 升级后自动回滚检测
 * - 上报升级进度和结果
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== OTA 事件定义 ========== */

/**
 * @brief OTA 事件类型
 */
typedef enum {
    OTA_EVENT_START,              // OTA开始下载
    OTA_EVENT_PROGRESS,           // 下载进度更新
    OTA_EVENT_DOWNLOAD_COMPLETE,  // 下载完成
    OTA_EVENT_SUCCESS,            // OTA升级成功（即将重启）
    OTA_EVENT_FAILED              // OTA升级失败
} ota_event_t;

/**
 * @brief OTA 事件信息
 */
typedef struct {
    ota_event_t event;        // 事件类型
    uint8_t channel;          // 固件通道号
    char current_version[32]; // 当前版本
    char target_version[32];  // 目标版本
    int progress;             // 下载进度 (0-100)
    int error_code;           // 错误码（仅失败时有效）
    char error_msg[64];       // 错误描述
} ota_event_info_t;

/**
 * @brief OTA 事件回调函数类型
 */
typedef void (*ota_event_callback_t)(const ota_event_info_t *info);

/* ========== OTA 配置结构 ========== */

/**
 * @brief OTA 管理器配置
 */
typedef struct {
    ota_event_callback_t event_callback;  // 事件回调函数
    bool auto_check_enable;               // 是否启用定时检测更新
    uint32_t auto_check_interval_hours;   // 定时检测间隔（小时）
    uint32_t download_buffer_size;        // 下载缓冲区大小（字节，推荐8192以提升速度）
} ota_manager_config_t;

/**
 * @brief 默认配置
 */
#define OTA_MANAGER_CONFIG_DEFAULT() {   \
    .event_callback = NULL,              \
    .auto_check_enable = false,          \
    .auto_check_interval_hours = 24,     \
    .download_buffer_size = 8192,        \
}

/* ========== OTA 管理器 API ========== */

/**
 * @brief 初始化 OTA 管理器
 * 
 * @param config OTA配置（可为NULL使用默认配置）
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_STATE: 已经初始化
 *         - ESP_FAIL: 初始化失败
 */
esp_err_t ota_manager_init(const ota_manager_config_t *config);

/**
 * @brief 启动 OTA 管理器（执行初始化任务）
 * 
 * 功能：
 * - 检测是否为OTA升级后首次启动
 * - 验证新固件并标记有效（防止回滚）
 * - 上报当前固件版本到云端
 * 
 * @note 应在 MQTT 连接成功后调用
 * 
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t ota_manager_start(void);

/**
 * @brief 上报固件版本到云端
 * 
 * @param biz_type 业务类型（"INIT" 或 "UPDATE"）
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_FAIL: 上报失败
 */
esp_err_t ota_manager_report_version(const char *biz_type);

/**
 * @brief 处理云端下发的 OTA 升级消息
 * 
 * @param json_data 云端下发的JSON消息
 * @return esp_err_t 
 *         - ESP_OK: 开始处理升级
 *         - ESP_ERR_INVALID_ARG: 参数错误
 *         - ESP_ERR_INVALID_STATE: 状态错误（如正在升级中）
 *         - ESP_FAIL: 处理失败
 */
esp_err_t ota_manager_handle_upgrade(const char *json_data);

/**
 * @brief 主动检测更新
 * 
 * @note 发送请求到云端，如果有更新，云端会推送 OTA issue 消息
 * 
 * @return esp_err_t 
 *         - ESP_OK: 请求发送成功
 *         - ESP_FAIL: 请求失败
 */
esp_err_t ota_manager_check_update(void);

/**
 * @brief 获取当前 OTA 状态
 * 
 * @return true: OTA正在进行中
 *         false: 空闲状态
 */
bool ota_manager_is_busy(void);

/**
 * @brief 获取当前固件版本
 * 
 * @param channel 固件通道号
 * @param version 版本字符串缓冲区（输出）
 * @param size 缓冲区大小
 * @return esp_err_t 
 */
esp_err_t ota_manager_get_version(uint8_t channel, char *version, size_t size);

/**
 * @brief 停止 OTA 管理器
 * 
 * @note 如果正在升级中，会取消升级
 * 
 * @return esp_err_t 
 */
esp_err_t ota_manager_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */

