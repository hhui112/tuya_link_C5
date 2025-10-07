#include "wifi_manager.h"
#include "common.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "wifi_mgr";
static EventGroupHandle_t wifi_event_group = NULL;
static wifi_manager_callback_t status_callback = NULL;
static int retry_count = 0;
static bool is_initialized = false;
// static TaskHandle_t reconnect_task_handle = NULL;

#define WIFI_CONNECTED_BIT  (1UL << 0)
#define WIFI_FAIL_BIT       (1UL << 1)

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        
        if (retry_count < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "wifi retry_count: %d", retry_count);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            if (status_callback) {
                status_callback(WIFI_MANAGER_EVENT_DISCONNECTED, NULL);
            }
            // 重置重试计数，准备下次重连(需要重启吗？)
            retry_count = 0;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if (status_callback) {
            status_callback(WIFI_MANAGER_EVENT_CONNECTED, event_data);
        }
    }
}

esp_err_t wifi_manager_init(wifi_manager_callback_t callback)
{
    if (is_initialized) return ESP_OK;
    
    status_callback = callback;
    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group) return ESP_FAIL;
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    is_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (!is_initialized) return ESP_ERR_INVALID_STATE;
    return esp_wifi_start();
}

esp_err_t wifi_manager_stop(void)
{
    if (!is_initialized) return ESP_OK;
    esp_wifi_stop();
    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }
    is_initialized = false;
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    if (!wifi_event_group) return false;
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    if (!wifi_event_group) return ESP_ERR_INVALID_STATE;
    
    TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, timeout_ticks);
    
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}
