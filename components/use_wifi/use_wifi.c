#include "use_wifi.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_sntp.h"
#include "mqtt_client.h"
#include "mbedtls/md.h"

/* 内置配置 - WiFi参数 */
#define WIFI_SSID               "kakakaka"
#define WIFI_PASSWORD           "fzh990112"
#define WIFI_MAXIMUM_RETRY      10

/* 内置配置 - 涂鸦IoT MQTT参数 */
#define TUYA_MQTT_URL           "mqtts://m1.tuyacn.com:8883"
#define TUYA_MQTT_HOST          "m1.tuyacn.com"
#define TUYA_MQTT_PORT          8883
#define TUYA_DEVICE_ID          "tuyalink_2631a16994c01c1f45qfha"
#define TUYA_DEVICE_SECRET      "cba720b9af92e95c185bf9d633714b3e6ad95cf546b8f00124e76303861600ee"
#define TUYA_PRODUCT_ID         "owes0z4baov2vqgx"

/* 静态认证信息 - 已在MQTT.FX验证可用 */
#define TUYA_CLIENT_ID          "tuyalink_2631a16994c01c1f45qfha"
#define TUYA_USERNAME           "2631a16994c01c1f45qfha|signMethod=hmacSha256,timestamp=1754706255,secureMode=1,accessType=1"
#define TUYA_PASSWORD           "2a2496d9fc06402243680e34c60171ae7b88aebbea0ced0055d57724be9ee23f"

const char tuya_cacert_pem[] = {\
"-----BEGIN CERTIFICATE-----\n"\
"MIIDxTCCAq2gAwIBAgIBADANBgkqhkiG9w0BAQsFADCBgzELMAkGA1UEBhMCVVMx\n"\
"EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoT\n"\
"EUdvRGFkZHkuY29tLCBJbmMuMTEwLwYDVQQDEyhHbyBEYWRkeSBSb290IENlcnRp\n"\
"ZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5MDkwMTAwMDAwMFoXDTM3MTIzMTIz\n"\
"NTk1OVowgYMxCzAJBgNVBAYTAlVTMRAwDgYDVQQIEwdBcml6b25hMRMwEQYDVQQH\n"\
"EwpTY290dHNkYWxlMRowGAYDVQQKExFHb0RhZGR5LmNvbSwgSW5jLjExMC8GA1UE\n"\
"AxMoR28gRGFkZHkgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIw\n"\
"DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAL9xYgjx+lk09xvJGKP3gElY6SKD\n"\
"E6bFIEMBO4Tx5oVJnyfq9oQbTqC023CYxzIBsQU+B07u9PpPL1kwIuerGVZr4oAH\n"\
"/PMWdYA5UXvl+TW2dE6pjYIT5LY/qQOD+qK+ihVqf94Lw7YZFAXK6sOoBJQ7Rnwy\n"\
"DfMAZiLIjWltNowRGLfTshxgtDj6AozO091GB94KPutdfMh8+7ArU6SSYmlRJQVh\n"\
"GkSBjCypQ5Yj36w6gZoOKcUcqeldHraenjAKOc7xiID7S13MMuyFYkMlNAJWJwGR\n"\
"tDtwKj9useiciAF9n9T521NtYJ2/LOdYq7hfRvzOxBsDPAnrSTFcaUaz4EcCAwEA\n"\
"AaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYE\n"\
"FDqahQcQZyi27/a9BUFuIMGU2g/eMA0GCSqGSIb3DQEBCwUAA4IBAQCZ21151fmX\n"\
"WWcDYfF+OwYxdS2hII5PZYe096acvNjpL9DbWu7PdIxztDhC2gV7+AJ1uP2lsdeu\n"\
"9tfeE8tTEH6KRtGX+rcuKxGrkLAngPnon1rpN5+r5N9ss4UXnT3ZJE95kTXWXwTr\n"\
"gIOrmgIttRD02JDHBHNA7XIloKmf7J6raBKZV8aPEjoJpL1E/QYVN8Gb5DKj7Tjo\n"\
"2GTzLH4U/ALqn83/B2gX2yKQOC16jdFU8WnjXzPKej17CuPKf1855eJ1usV2GDPO\n"\
"LPAvTK33sefOT6jEm0pUBsV/fdUID+Ic/n4XuKxe9tQWskMJDE32p2u0mYRlynqI\n"\
"4uJEvlz36hz1\n"\
"-----END CERTIFICATE-----\n"};

/* 事件组位定义 */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MQTT_CONNECTED_BIT BIT2
#define MQTT_FAIL_BIT BIT3

static const char *TAG = "use_wifi";
static const char *MQTT_TAG = "MQTT_TUYA";

/* 全局变量 */
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static int s_retry_num = 0;
static bool is_initialized = false;

/* 内部函数声明 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static esp_err_t mqtt_app_start(void);
static void initialize_sntp(void);
static esp_err_t wifi_init_sta(void);
static esp_err_t tuya_publish_custom_data(const char* data);
static void generate_tuya_username(char* username, size_t size);
static void generate_tuya_password(const char* username, char* password, size_t size);

/* 初始化SNTP时间同步 */
static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "初始化SNTP时间同步");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_init();
    
    // 等待时间同步完成
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10; // 减少等待时间
    
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "等待系统时间同步... (%d/%d)", retry, retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (timeinfo.tm_year > (2016 - 1900)) {
        ESP_LOGI(TAG, "时间同步成功: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        ESP_LOGW(TAG, "时间同步失败，继续使用TLS连接");
    }
}

/* WiFi事件处理器 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // 开始连接WiFi
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    // wifi连接失败
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "重试连接WiFi: 第%d次", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi连接失败: 已达到最大重试次数 rebooting...");
            esp_restart();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi连接成功: IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        // WiFi连接成功后进行时间同步，然后启动MQTT
        ESP_LOGI(TAG, "开始时间同步...");
        initialize_sntp();
        
        ESP_LOGI(TAG, "开始连接MQTT...");
        mqtt_app_start();
    }
}

/* MQTT事件处理器 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(MQTT_TAG, "MQTT连接成功");
        xEventGroupSetBits(s_wifi_event_group, MQTT_CONNECTED_BIT);
        
        // 订阅涂鸦设备命令主题
        char cmd_topic[128];
        snprintf(cmd_topic, sizeof(cmd_topic), "tylink/%s/thing/property/set", TUYA_DEVICE_ID);
        int msg_id = esp_mqtt_client_subscribe(client, cmd_topic, 0);
        ESP_LOGI(MQTT_TAG, "订阅命令主题成功, msg_id=%d", msg_id);
        
        // 发送设备在线状态
        char online_msg[] = "{\"properties\":{\"online\":true}}";
        tuya_publish_custom_data(online_msg);
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(MQTT_TAG, "MQTT断开连接");
        xEventGroupSetBits(s_wifi_event_group, MQTT_FAIL_BIT);
        break;
        
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(MQTT_TAG, "MQTT订阅成功, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(MQTT_TAG, "MQTT发布成功, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_DATA:
        ESP_LOGI(MQTT_TAG, "收到MQTT命令:");
        ESP_LOGI(MQTT_TAG, "主题: %.*s", event->topic_len, event->topic);
        ESP_LOGI(MQTT_TAG, "数据: %.*s", event->data_len, event->data);
        // 在这里可以处理从涂鸦平台接收到的命令
        break;
        
    case MQTT_EVENT_ERROR:
        ESP_LOGE(MQTT_TAG, "MQTT错误");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(MQTT_TAG, "传输层错误: %s", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
        
    default:
        ESP_LOGD(MQTT_TAG, "其他MQTT事件:%d", event->event_id);
        break;
    }
}

/* 启动MQTT客户端 */
static esp_err_t mqtt_app_start(void)
{
    ESP_LOGI(MQTT_TAG, "开始MQTT连接...");
    
    // 按照涂鸦文档要求，Client ID格式为 tuyalink_{deviceId}
    static char client_id[64];  // 使用静态变量减少栈使用
    //snprintf(client_id, sizeof(client_id), "tuyalink_%s", TUYA_DEVICE_ID);
    
    // 动态生成用户名，包含当前时间戳
    //static char username[128];  // 使用静态变量减少栈使用
    //generate_tuya_username(username, sizeof(username));
    
    // 生成HMAC-SHA256密码
    //static char password[128];  // 使用静态变量减少栈使用
    //generate_tuya_password(username, password, sizeof(password));
    
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = TUYA_MQTT_URL,
            .verification.certificate = (const char *)tuya_cacert_pem,
            .verification.certificate_len = sizeof(tuya_cacert_pem),
        },
        .credentials = {
            .client_id = TUYA_CLIENT_ID,
            .username = TUYA_USERNAME,
            .authentication.password = TUYA_PASSWORD,
        },
        .session = {
            .keepalive = 60,
            .disable_clean_session = false,
        }
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(MQTT_TAG, "MQTT客户端初始化失败");
        return ESP_FAIL;
    }
    
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(MQTT_TAG, "MQTT客户端启动失败");
        return ret;
    }
    
    return ESP_OK;
}

/* 初始化WiFi */
static esp_err_t wifi_init_sta(void)
{
    // 创建事件组
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "创建事件组失败");
        return ESP_FAIL;
    }
    
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册WiFi事件处理器
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // 配置WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi初始化完成，开始连接到: %s", WIFI_SSID);
    return ESP_OK;
}

/* 发布自定义数据到涂鸦平台 */
static esp_err_t tuya_publish_custom_data(const char* data)
{
    if (!mqtt_client || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char topic[128];
    snprintf(topic, sizeof(topic), "tylink/%s/thing/property/report", TUYA_DEVICE_ID);
    
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, 0, 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(MQTT_TAG, "发布数据失败");
        return ESP_FAIL;
    }
    
    ESP_LOGI(MQTT_TAG, "发布数据成功, msg_id=%d", msg_id);
    return ESP_OK;
}

/* 生成涂鸦MQTT用户名 */
static void generate_tuya_username(char* username, size_t size)
{
    time_t now;
    time(&now);
    
    // 按照涂鸦文档格式: ${deviceId}|signMethod=hmacSha256,timestamp=${当前10位时间戳},secureMode=1,accessType=1
    snprintf(username, size, "%s|signMethod=hmacSha256,timestamp=%ld,secureMode=1,accessType=1", 
             TUYA_DEVICE_ID, (long)now);
}

/* 生成涂鸦MQTT密码 */
static void generate_tuya_password(const char* username, char* password, size_t size)
{
    // 从username中提取时间戳
    char timestamp_str[16] = {0};  // 减小缓冲区大小
    const char* timestamp_start = strstr(username, "timestamp=");
    if (timestamp_start) {
        timestamp_start += 10; // 跳过 "timestamp="
        const char* timestamp_end = strchr(timestamp_start, ',');
        if (timestamp_end) {
            size_t len = timestamp_end - timestamp_start;
            if (len < sizeof(timestamp_str)) {
                strncpy(timestamp_str, timestamp_start, len);
                timestamp_str[len] = '\0';
            }
        }
    }
    
    // 如果没有找到时间戳，使用当前时间
    if (strlen(timestamp_str) == 0) {
        time_t now;
        time(&now);
        snprintf(timestamp_str, sizeof(timestamp_str), "%ld", (long)now);
    }
    
    char content[128];  // 减小缓冲区大小
    snprintf(content, sizeof(content), "deviceId=%s,timestamp=%s,secureMode=1,accessType=1", 
             TUYA_DEVICE_ID, timestamp_str);
    
    // 使用HMAC-SHA256计算密码
    unsigned char hmac_result[32];
    
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    
    if (mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1) == 0) {
        mbedtls_md_hmac_starts(&ctx, (const unsigned char*)TUYA_DEVICE_SECRET, strlen(TUYA_DEVICE_SECRET));
        mbedtls_md_hmac_update(&ctx, (const unsigned char*)content, strlen(content));
        mbedtls_md_hmac_finish(&ctx, hmac_result);
    }
    mbedtls_md_free(&ctx);
    
    // 转换为十六进制字符串
    for (int i = 0; i < 32 && i * 2 + 1 < size; i++) {
        snprintf(&password[i * 2], 3, "%02x", hmac_result[i]);
    }
    
    // 移除详细的调试日志以节省栈空间
    ESP_LOGI(MQTT_TAG, "密码生成完成");
}

/* 公共API实现 */

esp_err_t use_wifi_start(void)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "WiFi组件已经初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "启动WiFi和MQTT组件");
    
    esp_err_t ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi初始化失败");
        return ret;
    }
    
    is_initialized = true;
    return ESP_OK;
}

esp_err_t use_wifi_stop(void)
{
    if (!is_initialized) {
        return ESP_OK;
    }
    
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    
    esp_wifi_stop();
    
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    is_initialized = false;
    ESP_LOGI(TAG, "WiFi和MQTT已停止");
    return ESP_OK;
}

esp_err_t use_wifi_wait_connected(uint32_t timeout_ms)
{
    if (!s_wifi_event_group) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,  // 等待所有位都设置
            timeout_ticks);
    
    if ((bits & WIFI_CONNECTED_BIT) && (bits & MQTT_CONNECTED_BIT)) {
        ESP_LOGI(TAG, "WiFi和MQTT都连接成功！");
        return ESP_OK;
    } else if (timeout_ms > 0) {
        ESP_LOGW(TAG, "等待连接超时");
        return ESP_ERR_TIMEOUT;
    } else {
        ESP_LOGE(TAG, "WiFi或MQTT连接失败");
        return ESP_FAIL;
    }
}

esp_err_t tuya_publish_sensor_data(float temperature, float humidity)
{
    char sensor_data[256];
    time_t now;
    time(&now);
    
    snprintf(sensor_data, sizeof(sensor_data), 
             "{\"properties\":{\"temperature\":%.1f,\"humidity\":%.1f,\"timestamp\":%lld}}", 
             temperature, humidity, (long long)now);
    
    return tuya_publish_custom_data(sensor_data);
}

esp_err_t tuya_send_heartbeat(void)
{
    time_t now;
    time(&now);
    
    char heartbeat_msg[128];
    snprintf(heartbeat_msg, sizeof(heartbeat_msg), 
             "{\"properties\":{\"heartbeat\":true,\"timestamp\":%lld}}", (long long)now);
    
    return tuya_publish_custom_data(heartbeat_msg);
}

bool use_wifi_is_connected(void)
{
    if (!s_wifi_event_group) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) && (bits & MQTT_CONNECTED_BIT);
} 