/**
 * @file tuya_ota_protocol.c
 * @brief 涂鸦 MQTT OTA 协议实现
 */

#include "tuya_ota_protocol.h"
#include "common.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

static const char *TAG = "TUYA_OTA";

/**
 * @brief 生成消息ID（32位随机字符串）
 */
static void generate_msg_id(char *msg_id, size_t size)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(msg_id, size, "%08lx%08lx", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);
}

/**
 * @brief 获取当前时间戳（毫秒）
 */
static uint64_t get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

esp_err_t tuya_ota_build_version_report(
    const char *biz_type,
    const tuya_ota_channel_t *channels,
    uint8_t channel_count,
    char *out_json,
    size_t json_size
)
{
    if (!biz_type || !channels || channel_count == 0 || !out_json || json_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    // 生成消息ID
    char msg_id[32];
    generate_msg_id(msg_id, sizeof(msg_id));
    
    // 添加基本字段
    cJSON_AddStringToObject(root, "msgId", msg_id);
    cJSON_AddNumberToObject(root, "time", get_timestamp_ms());

    // 添加data对象
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(data, "bizType", biz_type);
    
    // 如果是INIT类型，添加PID
    if (strcmp(biz_type, TUYA_OTA_BIZ_TYPE_INIT) == 0) {
        cJSON_AddStringToObject(data, "pid", TUYA_PRODUCT_ID);
    }
    
    // 添加固件通道数组
    cJSON *channel_array = cJSON_CreateArray();
    if (!channel_array) {
        cJSON_Delete(data);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    for (uint8_t i = 0; i < channel_count; i++) {
        cJSON *channel_obj = cJSON_CreateObject();
        if (!channel_obj) {
            cJSON_Delete(channel_array);
            cJSON_Delete(data);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        
        cJSON_AddNumberToObject(channel_obj, "channel", channels[i].channel);
        cJSON_AddStringToObject(channel_obj, "version", channels[i].version);
        cJSON_AddItemToArray(channel_array, channel_obj);
    }
    
    cJSON_AddItemToObject(data, "otaChannel", channel_array);
    cJSON_AddItemToObject(root, "data", data);

    // 转换为字符串
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    if (strlen(json_str) >= json_size) {
        ESP_LOGE(TAG, "JSON buffer too small");
        cJSON_free(json_str);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    strncpy(out_json, json_str, json_size - 1);
    out_json[json_size - 1] = '\0';

    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Built version report: %s", out_json);
    return ESP_OK;
}

esp_err_t tuya_ota_parse_upgrade_msg(
    const char *json_data,
    tuya_ota_upgrade_info_t *upgrade_info
)
{
    if (!json_data || !upgrade_info) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    memset(upgrade_info, 0, sizeof(tuya_ota_upgrade_info_t));

    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGE(TAG, "JSON解析失败");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;

    // 解析 data 对象
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
        ESP_LOGE(TAG, "缺少 'data' 字段");
        goto cleanup;
    }

    // 解析 ctId（可选）
    cJSON *ct_id = cJSON_GetObjectItem(data, "ctId");
    if (ct_id && cJSON_IsString(ct_id)) {
        strncpy(upgrade_info->ct_id, ct_id->valuestring, sizeof(upgrade_info->ct_id) - 1);
    }

    // 解析 channel（必需）
    cJSON *channel = cJSON_GetObjectItem(data, "channel");
    if (!channel || !cJSON_IsNumber(channel)) {
        ESP_LOGE(TAG, "缺少或无效的 'channel' 字段");
        goto cleanup;
    }
    upgrade_info->channel = (uint8_t)channel->valueint;

    // 解析 version（必需）
    cJSON *version = cJSON_GetObjectItem(data, "version");
    if (!version || !cJSON_IsString(version)) {
        ESP_LOGE(TAG, "缺少或无效的 'version' 字段");
        goto cleanup;
    }
    strncpy(upgrade_info->version, version->valuestring, sizeof(upgrade_info->version) - 1);

    // 解析 url（必需）
    cJSON *url = cJSON_GetObjectItem(data, "url");
    if (!url || !cJSON_IsString(url)) {
        ESP_LOGE(TAG, "缺少或无效的 'url' 字段");
        goto cleanup;
    }
    strncpy(upgrade_info->url, url->valuestring, sizeof(upgrade_info->url) - 1);

    // 解析 hmac（可选）
    cJSON *hmac = cJSON_GetObjectItem(data, "hmac");
    if (hmac && cJSON_IsString(hmac)) {
        strncpy(upgrade_info->hmac, hmac->valuestring, sizeof(upgrade_info->hmac) - 1);
    }

    // 解析 size（可选）
    cJSON *size = cJSON_GetObjectItem(data, "size");
    if (size) {
        if (cJSON_IsNumber(size)) {
            upgrade_info->size = (uint32_t)size->valueint;
        } else if (cJSON_IsString(size)) {
            upgrade_info->size = (uint32_t)atol(size->valuestring);
        }
    }

    ESP_LOGD(TAG, "OTA消息解析成功: channel=%d, version=%s, size=%lu", 
             upgrade_info->channel, upgrade_info->version, (unsigned long)upgrade_info->size);

    ret = ESP_OK;

cleanup:
    cJSON_Delete(root);
    return ret;
}

esp_err_t tuya_ota_build_progress_report(
    uint8_t channel,
    int progress,
    int error_code,
    const char *error_msg,
    char *out_json,
    size_t json_size
)
{
    if (!out_json || json_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    // 生成消息ID
    char msg_id[32];
    generate_msg_id(msg_id, sizeof(msg_id));
    
    // 添加基本字段
    cJSON_AddStringToObject(root, "msgId", msg_id);
    cJSON_AddNumberToObject(root, "time", get_timestamp_ms());

    // 添加data对象
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddNumberToObject(data, "channel", channel);
    
    // 如果进度>=0，表示升级中或成功
    if (progress >= 0) {
        cJSON_AddNumberToObject(data, "progress", progress);
    }
    
    // 如果有错误码，添加错误信息
    if (error_code > 0) {
        cJSON_AddNumberToObject(data, "errorCode", error_code);
        if (error_msg) {
            cJSON_AddStringToObject(data, "errorMsg", error_msg);
        }
    }
    
    cJSON_AddItemToObject(root, "data", data);

    // 转换为字符串
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    if (strlen(json_str) >= json_size) {
        ESP_LOGE(TAG, "JSON buffer too small");
        cJSON_free(json_str);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    strncpy(out_json, json_str, json_size - 1);
    out_json[json_size - 1] = '\0';

    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGD(TAG, "Built progress report: %s", out_json);
    return ESP_OK;
}

esp_err_t tuya_ota_build_topic(
    const char *device_id,
    const char *topic_template,
    char *out_topic,
    size_t topic_size
)
{
    if (!device_id || !topic_template || !out_topic || topic_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(out_topic, topic_size, topic_template, device_id);
    if (written < 0 || written >= topic_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

