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
 * @brief 停止APP主业务任务
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t app_main_stop(void);

#ifdef __cplusplus
}
#endif
