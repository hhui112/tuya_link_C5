/*
 * APP主业务逻辑模块头文件
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动APP主业务任务
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t app_main_start(void);

/**
 * @brief 启动解耦的网络和时间服务
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t system_services_start(void);

/**
 * @brief 初始化任务看门狗
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t watchdog_init(void);

#ifdef __cplusplus
}
#endif
