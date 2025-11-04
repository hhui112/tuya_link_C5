/**
 * @file tuya_ota_protocol.h
 * @brief 涂鸦 MQTT OTA 协议定义和解析
 * 
 * 参考文档: https://developer.tuya.com/cn/docs/iot/OTA_FIRMWARE?id=Kbt4xp0kr2u57
 */

#ifndef TUYA_OTA_PROTOCOL_H
#define TUYA_OTA_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 涂鸦 OTA 固件通道定义 ========== */
#define TUYA_OTA_CHANNEL_MAIN       0   // 主模块固件通道
#define TUYA_OTA_CHANNEL_BLE        1   // 蓝牙固件通道
#define TUYA_OTA_CHANNEL_ZIGBEE     3   // Zigbee固件通道
#define TUYA_OTA_CHANNEL_MCU        9   // MCU固件通道

/* ========== 涂鸦 OTA 业务类型 ========== */
#define TUYA_OTA_BIZ_TYPE_INIT      "INIT"    // 固件版本初始化
#define TUYA_OTA_BIZ_TYPE_UPDATE    "UPDATE"  // 固件升级完成版本更新

/* ========== 涂鸦 OTA Topic 定义 ========== */
#define TUYA_OTA_TOPIC_FIRMWARE_REPORT  "tylink/%s/ota/firmware/report"  // 设备上报版本
#define TUYA_OTA_TOPIC_FIRMWARE_GET     "tylink/%s/ota/firmware/get"     // 设备主动拉取
#define TUYA_OTA_TOPIC_ISSUE            "tylink/%s/ota/issue"            // 云端下发升级
#define TUYA_OTA_TOPIC_PROGRESS         "tylink/%s/ota/progress"         // 设备上报进度

/* ========== 涂鸦 OTA 错误码 ========== */
typedef enum {
    TUYA_OTA_ERR_UNKNOWN = 40,           // 升级失败，未知异常
    TUYA_OTA_ERR_DOWNLOAD_LOW_POWER,     // 41: 下载失败，电量不足
    TUYA_OTA_ERR_DOWNLOAD_NO_FLASH,      // 42: 下载失败，FLASH空间不足
    TUYA_OTA_ERR_DOWNLOAD_NO_RAM,        // 43: 下载失败，RAM申请出错或不足
    TUYA_OTA_ERR_DOWNLOAD_TIMEOUT,       // 44: 下载失败，下载请求超时
    TUYA_OTA_ERR_DOWNLOAD_CHECKSUM,      // 45: 下载失败，数据校验出错
    TUYA_OTA_ERR_UPGRADE_LOW_POWER,      // 46: 升级失败，电量不足
    TUYA_OTA_ERR_UPGRADE_NO_RAM,         // 47: 升级失败，RAM申请出错或不足
    TUYA_OTA_ERR_UPGRADE_VERSION,        // 48: 升级失败，版本错误
    TUYA_OTA_ERR_UPGRADE_HMAC,           // 49: 升级失败，HMAC校验错误
    TUYA_OTA_ERR_GATEWAY_BUSY = 50       // 50: 升级失败，网关繁忙
} tuya_ota_error_code_t;

/* ========== 涂鸦 OTA 数据结构 ========== */

/**
 * @brief OTA 固件通道信息
 */
typedef struct {
    uint8_t channel;      // 固件通道号
    char version[32];     // 固件版本号
} tuya_ota_channel_t;

/**
 * @brief 云端下发的 OTA 升级信息
 */
typedef struct {
    char ct_id[64];           // 升级任务ID
    uint8_t channel;          // 固件通道号
    char version[32];         // 目标版本号
    char url[512];            // 固件下载URL
    char hmac[128];           // HMAC校验值
    uint32_t size;            // 固件大小（字节）
} tuya_ota_upgrade_info_t;

/* ========== 函数声明 ========== */

/**
 * @brief 构造设备上报固件版本的JSON消息
 * 
 * @param biz_type 业务类型 ("INIT" 或 "UPDATE")
 * @param channels 固件通道数组
 * @param channel_count 通道数量
 * @param out_json 输出的JSON字符串缓冲区
 * @param json_size JSON缓冲区大小
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数错误
 *         - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t tuya_ota_build_version_report(
    const char *biz_type,
    const tuya_ota_channel_t *channels,
    uint8_t channel_count,
    char *out_json,
    size_t json_size
);

/**
 * @brief 解析云端下发的 OTA 升级消息
 * 
 * @param json_data JSON格式的OTA消息
 * @param upgrade_info 解析后的升级信息（输出）
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数错误
 *         - ESP_FAIL: 解析失败
 */
esp_err_t tuya_ota_parse_upgrade_msg(
    const char *json_data,
    tuya_ota_upgrade_info_t *upgrade_info
);

/**
 * @brief 构造设备上报升级进度的JSON消息
 * 
 * @param channel 固件通道号
 * @param progress 升级进度 (0-100，-1表示失败)
 * @param error_code 错误码（仅失败时使用）
 * @param error_msg 错误描述（仅失败时使用，最长20字符）
 * @param out_json 输出的JSON字符串缓冲区
 * @param json_size JSON缓冲区大小
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数错误
 *         - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t tuya_ota_build_progress_report(
    uint8_t channel,
    int progress,
    int error_code,
    const char *error_msg,
    char *out_json,
    size_t json_size
);

/**
 * @brief 构造 OTA Topic 路径
 * 
 * @param device_id 设备ID
 * @param topic_template Topic模板（如 TUYA_OTA_TOPIC_ISSUE）
 * @param out_topic 输出的Topic字符串缓冲区
 * @param topic_size Topic缓冲区大小
 * @return esp_err_t 
 */
esp_err_t tuya_ota_build_topic(
    const char *device_id,
    const char *topic_template,
    char *out_topic,
    size_t topic_size
);

#ifdef __cplusplus
}
#endif

#endif /* TUYA_OTA_PROTOCOL_H */

