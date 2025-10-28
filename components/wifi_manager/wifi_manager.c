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

// 配网失败回滚机制：备份旧配置
typedef struct {
    char old_ssid[32];
    char old_password[64];
    bool has_backup;
} wifi_config_backup_t;

static wifi_config_backup_t config_backup = {0};

#define WIFI_CONNECTED_BIT          (1UL << 0)
#define WIFI_FAIL_BIT               (1UL << 1)
#define WIFI_RECONNECTED_BIT        (1UL << 2)
#define WIFI_PROV_MAX_RETRY         5       // 配网专用重试次数
#define WIFI_STOP_DELAY_MS          200     // WiFi停止后的等待时间

// 辅助函数：应用WiFi配置（SSID和密码）
static void apply_wifi_credentials(wifi_config_t *config, const char *ssid, const char *password)
{
    memset(config->sta.ssid, 0, sizeof(config->sta.ssid));
    memset(config->sta.password, 0, sizeof(config->sta.password));
    strncpy((char *)config->sta.ssid, ssid, sizeof(config->sta.ssid) - 1);
    strncpy((char *)config->sta.password, password, sizeof(config->sta.password) - 1);
}

// 辅助函数：重启WiFi并应用新配置
static esp_err_t restart_wifi_with_config(const char *ssid, const char *password)
{
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(WIFI_STOP_DELAY_MS));
    
    wifi_config_t config;
    esp_wifi_get_config(WIFI_IF_STA, &config);
    apply_wifi_credentials(&config, ssid, password);
    
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (ret != ESP_OK) return ret;
    
    return esp_wifi_start();
}

void wifi_manager_reconfigure(const char* ssid, const char* password, wifi_pre_reconfig_callback_t pre_reconfig_cb) 
{
    if (!wifi_event_group) {
        ESP_LOGE(TAG, "WiFi管理器未初始化");
        return;
    }
    
    ESP_LOGI(TAG, "🔄 开始WiFi重配置 (SSID: %s)", ssid);
    
    // 备份当前配置（用于失败回滚）
    wifi_config_t old_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &old_config) == ESP_OK) {
        memset(&config_backup, 0, sizeof(config_backup));
        strncpy(config_backup.old_ssid, (char*)old_config.sta.ssid, sizeof(config_backup.old_ssid) - 1);
        strncpy(config_backup.old_password, (char*)old_config.sta.password, sizeof(config_backup.old_password) - 1);
        config_backup.has_backup = true;
    }
    
    // 设置配网标志位
    xEventGroupSetBits(wifi_event_group, WIFI_RECONNECTED_BIT);
    
    // 调用应用层回调（停止MQTT等服务）
    if (pre_reconfig_cb) {
        pre_reconfig_cb();
    }

    // 重启WiFi并应用新配置
    esp_err_t ret = restart_wifi_with_config(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi重配置失败: %s", esp_err_to_name(ret));
        xEventGroupClearBits(wifi_event_group, WIFI_RECONNECTED_BIT);
        return;
    }
    
    ESP_LOGI(TAG, "✅ WiFi已启动，等待连接...");
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        
        // 检查是否是配网操作
        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
        bool is_provisioning = (bits & WIFI_RECONNECTED_BIT) != 0;
        
        // 配网时使用更短的重试次数
        int max_retry = is_provisioning ? WIFI_PROV_MAX_RETRY : WIFI_MAXIMUM_RETRY;
        
        if (retry_count < max_retry) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "%s重试: %d/%d", is_provisioning ? "配网" : "WiFi", retry_count, max_retry);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            
            // 配网失败：回滚到旧配置
            if (is_provisioning && config_backup.has_backup) {
                ESP_LOGW(TAG, "❌ 配网失败，回滚到旧配置 (SSID: %s)", config_backup.old_ssid);
                
                // 通知应用层配网失败
                if (status_callback) {
                    status_callback(WIFI_MANAGER_EVENT_PROV_FAILED, NULL);
                }
                
                xEventGroupClearBits(wifi_event_group, WIFI_RECONNECTED_BIT);
                
                // 恢复旧配置
                restart_wifi_with_config(config_backup.old_ssid, config_backup.old_password);
                config_backup.has_backup = false;
                retry_count = 0;
            } else {
                // 普通连接失败：通知应用层
                if (status_callback) {
                    status_callback(WIFI_MANAGER_EVENT_DISCONNECTED, NULL);
                }
                ESP_LOGE(TAG, "WiFi连接失败，重启设备...");
                esp_restart();
            }
            retry_count = 0;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        
        // 配网成功：清除备份
        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
        if ((bits & WIFI_RECONNECTED_BIT) && config_backup.has_backup) {
            config_backup.has_backup = false;
            ESP_LOGI(TAG, "✅ 配网成功，清除配置备份");
        }
        
        // 通知应用层（由应用层决定是否保存配置）
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
    
    // 初始化WiFi配置
    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    apply_wifi_credentials(&wifi_config, device_info->wifi.ssid, device_info->wifi.passwd);
    
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
bool wifi_manager_is_reconnected(void)
{
    if (!wifi_event_group) return false;
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & WIFI_RECONNECTED_BIT) != 0;
}

void wifi_manager_clear_reconnected_bit(void)
{
    if (!wifi_event_group) return; 
    xEventGroupClearBits(wifi_event_group, WIFI_RECONNECTED_BIT);
}


esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    if (!wifi_event_group) return ESP_ERR_INVALID_STATE;
    
    TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, timeout_ticks);
    
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}
