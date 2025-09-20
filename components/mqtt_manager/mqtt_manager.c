#include "mqtt_manager.h"
#include "common.h"
#include "mqtt_client.h"
#include "mbedtls/md.h"
#include <time.h>
#include <string.h>

static const char *TAG = "mqtt_mgr";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_manager_callback_t status_callback = NULL;
static bool is_initialized = false;
static bool is_connected = false;

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

static void generate_tuya_username(char* username, size_t size)
{
    time_t now;
    time(&now);
    snprintf(username, size, "%s|signMethod=hmacSha256,timestamp=%ld,secureMode=1,accessType=1", 
             TUYA_DEVICE_ID, (long)now);
}

static void generate_tuya_password(const char* username, char* password, size_t size)
{
    char timestamp_str[20] = {0};  
    const char* timestamp_start = strstr(username, "timestamp=");
    if (timestamp_start) {
        timestamp_start += strlen("timestamp=");
        const char* timestamp_end = strchr(timestamp_start, ',');
        if (timestamp_end) {
            size_t len = timestamp_end - timestamp_start;
            if (len < sizeof(timestamp_str)) {
                strncpy(timestamp_str, timestamp_start, len);
                timestamp_str[len] = '\0';
            }
        }
    }

    if (strlen(timestamp_str) == 0) {
        time_t now;
        time(&now);
        snprintf(timestamp_str, sizeof(timestamp_str), "%ld", (long)now);
    }

    char content[256];
    snprintf(content, sizeof(content), "deviceId=%s,timestamp=%s,secureMode=1,accessType=1",
             TUYA_DEVICE_ID, timestamp_str);

    unsigned char hmac_result[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    if (mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1) == 0) {
        mbedtls_md_hmac_starts(&ctx, (const unsigned char*)TUYA_DEVICE_SECRET, strlen(TUYA_DEVICE_SECRET));
        mbedtls_md_hmac_update(&ctx, (const unsigned char*)content, strlen(content));
        mbedtls_md_hmac_finish(&ctx, hmac_result);
    }
    mbedtls_md_free(&ctx);

    if (size > 64) {
        for (int i = 0; i < 32; i++) {
            sprintf(&password[i * 2], "%02x", hmac_result[i]);
        }
        password[64] = '\0';
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            is_connected = true;
            if (status_callback) {
                status_callback(MQTT_MANAGER_EVENT_CONNECTED, NULL);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            is_connected = false;
            if (status_callback) {
                status_callback(MQTT_MANAGER_EVENT_DISCONNECTED, NULL);
            }
            break;
            
        case MQTT_EVENT_DATA:
            if (status_callback) {
                mqtt_manager_data_t data = {
                    .topic = event->topic,
                    .topic_len = event->topic_len,
                    .data = event->data,
                    .data_len = event->data_len
                };
                status_callback(MQTT_MANAGER_EVENT_DATA_RECEIVED, &data);
            }
            break;
            
        default:
            break;
    }
}

esp_err_t mqtt_manager_init(mqtt_manager_callback_t callback)
{
    if (is_initialized) return ESP_OK;
    
    status_callback = callback;
    is_initialized = true;
    return ESP_OK;
}

esp_err_t mqtt_manager_start(void)
{
    if (!is_initialized) return ESP_ERR_INVALID_STATE;
    if (mqtt_client) return ESP_OK;
    
    static char client_id[64];
    static char username[128];
    static char password[128];
    
    snprintf(client_id, sizeof(client_id), "tuyalink_%s", TUYA_DEVICE_ID);
    generate_tuya_username(username, sizeof(username));
    generate_tuya_password(username, password, sizeof(password));
    
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = TUYA_MQTT_URL,
            .verification.certificate = tuya_cacert_pem,
        },
        .credentials = {
            .client_id = client_id,
            .username = username,
            .authentication.password = password,
        },
        .session = {
            .keepalive = 60,
            .disable_clean_session = false,
        }
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) return ESP_FAIL;
    
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(mqtt_client);
}

esp_err_t mqtt_manager_stop(void)
{
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    is_connected = false;
    is_initialized = false;
    return ESP_OK;
}

bool mqtt_manager_is_connected(void)
{
    return is_connected;
}

esp_err_t mqtt_manager_publish(const char *topic, const char *data, int qos)
{
    if (!mqtt_client || !is_connected) return ESP_ERR_INVALID_STATE;
    
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, 0, qos, 0);
    return (msg_id == -1) ? ESP_FAIL : ESP_OK;
}

esp_err_t mqtt_manager_subscribe(const char *topic, int qos)
{
    if (!mqtt_client || !is_connected) return ESP_ERR_INVALID_STATE;
    
    int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, qos);
    return (msg_id == -1) ? ESP_FAIL : ESP_OK;
}
