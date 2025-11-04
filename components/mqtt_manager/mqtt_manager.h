#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MQTT_MANAGER_EVENT_CONNECTED,
    MQTT_MANAGER_EVENT_DISCONNECTED,
    MQTT_MANAGER_EVENT_DATA_RECEIVED
} mqtt_manager_event_t;

typedef struct {
    char *topic;
    int topic_len;
    char *data;
    int data_len;
} mqtt_manager_data_t;

typedef void (*mqtt_manager_callback_t)(mqtt_manager_event_t event, void *event_data);

// 涂鸦CA证书（用于MQTT和OTA下载）
extern const char tuya_cacert_pem[];

esp_err_t mqtt_manager_init(mqtt_manager_callback_t callback);
esp_err_t mqtt_manager_start(void);
esp_err_t mqtt_manager_stop(void);
bool mqtt_manager_is_connected(void);
esp_err_t mqtt_manager_publish(const char *topic, const char *data, int qos);
esp_err_t mqtt_manager_subscribe(const char *topic, int qos);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MANAGER_H */
