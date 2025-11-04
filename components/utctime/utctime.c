#include "utctime.h"
#include "wifi_manager.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "ntp_mgr";
static ntp_manager_callback_t status_callback = NULL;
static bool is_initialized = false;
static bool is_synced = false;
static TaskHandle_t sync_task_handle = NULL;

#define NTP_SYNC_INTERVAL_SECONDS (3600)  // 1小时同步一次
#define NTP_RETRY_INTERVAL_SECONDS (60)   // 失败后60秒重试


static void ntp_sync_notification_cb(struct timeval *tv)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (timeinfo.tm_year > (2016 - 1900)) {
        ESP_LOGI(TAG, "时间同步成功 (北京时间): %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                 
        is_synced = true;
        if (status_callback) {
            status_callback(NTP_MANAGER_EVENT_TIME_SYNCED, NULL);
        }
    } else {
        ESP_LOGW(TAG, "时间同步异常，时间不合理，继续重试");
        is_synced = false;
    }
}

static void ntp_sync_task(void *arg)
{
    while (1) {
        // 只有WiFi连接时才进行同步检查
        if (wifi_manager_is_connected()) {
            if (!is_synced) {
                ESP_LOGW(TAG, "时间未同步, 5秒后重试");
                vTaskDelay(pdMS_TO_TICKS(5*1000));
                
                if (esp_sntp_enabled()) {
                    esp_sntp_restart();
                }
            } else {
                ESP_LOGI(TAG, "下次定期同步将在1小时后进行");
                vTaskDelay(pdMS_TO_TICKS(NTP_SYNC_INTERVAL_SECONDS * 1000));
                
                if (esp_sntp_enabled()) {
                    esp_sntp_restart();
                }
            }
        } else {
            // WiFi未连接，等待WiFi连接
            ESP_LOGD(TAG, "等待WiFi连接...");
            vTaskDelay(pdMS_TO_TICKS(5000));  // 5秒检查一次WiFi状态
        }
    }
}

esp_err_t ntp_manager_init(ntp_manager_callback_t callback)
{
    if (is_initialized) return ESP_OK;
    
    status_callback = callback;
    
    setenv("TZ", "CST-8", 1);      // 中国标准时间，UTC+8
    tzset();                       // 使时区设置生效
    
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_set_time_sync_notification_cb(ntp_sync_notification_cb);
    
    is_initialized = true;
    return ESP_OK;
}

esp_err_t ntp_manager_start(void)
{
    if (!is_initialized) return ESP_ERR_INVALID_STATE;
    
    ESP_LOGI(TAG, "开始NTP时间同步");
    esp_sntp_init();  // ESP-IDF立即开始首次同步
    
    // 创建后台定期同步任务
    xTaskCreate(ntp_sync_task, "ntp_sync", 3072, NULL, 5, &sync_task_handle);
    
    return ESP_OK;
}

esp_err_t ntp_manager_stop(void)
{
    if (sync_task_handle) {
        vTaskDelete(sync_task_handle);
        sync_task_handle = NULL;
    }
    
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    
    is_synced = false;
    is_initialized = false;
    return ESP_OK;
}

esp_err_t ntp_manager_get_time(time_t *current_time, bool *synced)
{
    if (!current_time) return ESP_ERR_INVALID_ARG;
    
    time(current_time);
    if (synced) *synced = is_synced;
    
    return ESP_OK;
}

bool ntp_manager_is_synced(void)
{
    return is_synced;
    }

esp_err_t ntp_manager_force_sync(void)
{
    if (!is_initialized) return ESP_ERR_INVALID_STATE;
    
    if (esp_sntp_enabled()) {
        esp_sntp_restart();
    }
    
    return ESP_OK;
}

