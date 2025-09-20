#ifndef UTCTIME_H
#define UTCTIME_H

#include "esp_err.h"
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NTP_MANAGER_EVENT_TIME_SYNCED,
    NTP_MANAGER_EVENT_SYNC_FAILED
} ntp_manager_event_t;

typedef void (*ntp_manager_callback_t)(ntp_manager_event_t event, void *event_data);

esp_err_t ntp_manager_init(ntp_manager_callback_t callback); // 初始化UTC同步
esp_err_t ntp_manager_start(void); // 启动UTC同步
esp_err_t ntp_manager_stop(void); // 停止UTC同步
esp_err_t ntp_manager_get_time(time_t *current_time, bool *is_synced); // 获取UTC时间：时间戳，同步状态
bool ntp_manager_is_synced(void);   // 检查UTC同步状态
esp_err_t ntp_manager_force_sync(void); // 手动触发UTC同步

#ifdef __cplusplus
}
#endif

#endif /* UTCTIME_H */
