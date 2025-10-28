#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MANAGER_EVENT_CONNECTED,
    WIFI_MANAGER_EVENT_DISCONNECTED,
    WIFI_MANAGER_EVENT_PROV_FAILED  // 配网失败
} wifi_manager_event_t;

typedef void (*wifi_manager_callback_t)(wifi_manager_event_t event, void *event_data);

// 重配置前的准备回调（用于停止依赖WiFi的服务，如MQTT）
typedef void (*wifi_pre_reconfig_callback_t)(void);

esp_err_t wifi_manager_init(wifi_manager_callback_t callback);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_is_reconnected(void);
void wifi_manager_clear_reconnected_bit(void);
void wifi_manager_reconfigure(const char* ssid, const char* password, wifi_pre_reconfig_callback_t pre_reconfig_cb);
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
