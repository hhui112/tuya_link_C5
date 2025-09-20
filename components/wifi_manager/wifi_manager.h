#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MANAGER_EVENT_CONNECTED,
    WIFI_MANAGER_EVENT_DISCONNECTED
} wifi_manager_event_t;

typedef void (*wifi_manager_callback_t)(wifi_manager_event_t event, void *event_data);

esp_err_t wifi_manager_init(wifi_manager_callback_t callback);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
