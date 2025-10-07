#include "common.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_partition.h"  // 用于分区操作
#include "nvs.h"           // NVS 操作
#include "nvs_flash.h"     // NVS Flash 操作
#include <string.h>
#include <stdlib.h>

static const char *TAG = "common";
device_info_t *device_info;
tuya_config_t tuya_config;

// 全局状态变量定义
iot_device_state_t g_iot_state = {
    .device_status = "close",  // 默认值
    .test_value = 0            // 默认值
};

// 统一消息队列
QueueHandle_t g_msg_queue = NULL;

static void read_tuya_config(void)
{
	const esp_partition_t *partition;
	
	partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
/*
	if(partition != NULL)
	{
		esp_partition_read(partition, 0, &tuya_config, sizeof(tuya_config_t));
		memcpy(device_info->id, tuya_config.device_id, strlen(tuya_config.device_id));
		//涂鸦 参数配置
		memcpy(device_info->tuya.device_id, tuya_config.device_id, strlen(tuya_config.device_id));
		memcpy(device_info->tuya.product_id, tuya_config.product_id, strlen(tuya_config.product_id));
		memcpy(device_info->tuya.device_secret, tuya_config.device_secret, strlen(tuya_config.device_secret));

		printf("----------------get tuya_config form partition---------------------- \n");
	}else
 */
	{
		// 安全的设备id设置
		strncpy(device_info->id, TUYA_DEVICE_ID, sizeof(device_info->id) - 1);
		device_info->id[sizeof(device_info->id) - 1] = '\0';
		
		//tuya 参数配置 - 安全复制
		strncpy(device_info->tuya.product_id, TUYA_PRODUCT_ID, sizeof(device_info->tuya.product_id) - 1);
		device_info->tuya.product_id[sizeof(device_info->tuya.product_id) - 1] = '\0';
		
		strncpy(device_info->tuya.device_secret, TUYA_DEVICE_SECRET, sizeof(device_info->tuya.device_secret) - 1);
		device_info->tuya.device_secret[sizeof(device_info->tuya.device_secret) - 1] = '\0';
		
		strncpy(device_info->tuya.device_id, TUYA_DEVICE_ID, sizeof(device_info->tuya.device_id) - 1);
		device_info->tuya.device_id[sizeof(device_info->tuya.device_id) - 1] = '\0';
		
		printf("----------------use common tuya_config----------------------\n");	
	}
	printf("PRODUCT_ID:%s,DEVICE_SECRET:%s,DEVICE_ID:%s \n",device_info->tuya.product_id,device_info->tuya.device_secret,device_info->tuya.device_id);
}

//设置默认参数
static void param_config_init(void)
{
    // 安全的内存分配，检查返回值
    device_info = (device_info_t *)malloc(sizeof(device_info_t));
    if (device_info == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for device_info");
        return;
    }
    memset(device_info, 0, sizeof(device_info_t));
    
    device_info->ble = (ble_link_info_t *)malloc(sizeof(ble_link_info_t));
    if (device_info->ble == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for ble_info");
        free(device_info);
        return;
    }
    memset(device_info->ble, 0, sizeof(ble_link_info_t));

    // 创建消息队列
    g_msg_queue = xQueueCreate(10, sizeof(g_msg_queue_t));
    if (g_msg_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create message queue");
        free(device_info->ble);
        free(device_info);
        return;
    }
    
    // tuya-iot 参数配置
    read_tuya_config();

    // 安全的WiFi默认设置
    strncpy(device_info->wifi.ssid, WIFI_SSID, sizeof(device_info->wifi.ssid) - 1);
    device_info->wifi.ssid[sizeof(device_info->wifi.ssid) - 1] = '\0';
    
    strncpy(device_info->wifi.passwd, WIFI_PASSWORD, sizeof(device_info->wifi.passwd) - 1);
    device_info->wifi.passwd[sizeof(device_info->wifi.passwd) - 1] = '\0';
}

void config_store_to_flash(void)
{
    // 检查device_info是否已正确初始化
    if (device_info == NULL) {
        ESP_LOGE(TAG, "device_info 未初始化，无法保存配置");
        return;
    }
    
    char out_value[64] = {0};
    size_t len = sizeof(out_value);
    nvs_handle_t nvs_config_handler;
    
    // 打开 NVS 句柄
    esp_err_t ret = nvs_open("config_cfg", NVS_READWRITE, &nvs_config_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 尝试读取配置标志
    ret = nvs_get_str(nvs_config_handler, "configFlag", out_value, &len);
    if (ret == ESP_OK && strcmp(out_value, "ok") == 0) {
        // flash中已经存在相应参数，读取参数
        len = sizeof(device_info->wifi.ssid);
        ESP_ERROR_CHECK(nvs_get_str(nvs_config_handler, "wifiSsid", device_info->wifi.ssid, &len));
        len = sizeof(device_info->wifi.passwd);
        ESP_ERROR_CHECK(nvs_get_str(nvs_config_handler, "wifiPasswd", device_info->wifi.passwd, &len));
        ESP_LOGI(TAG, "wifiSsid: %s, wifiPasswd: %s", device_info->wifi.ssid, device_info->wifi.passwd);
        printf("----------------nvs config read ok----------------------\n");
    } else {
        // 首次初始化，写入默认配置
        ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "wifiSsid", device_info->wifi.ssid));
        ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "wifiPasswd", device_info->wifi.passwd));
        ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "configFlag", "ok"));
        ESP_ERROR_CHECK(nvs_commit(nvs_config_handler));
        ESP_LOGI(TAG, "wifiSsid: %s, wifiPasswd: %s", device_info->wifi.ssid, device_info->wifi.passwd);
        printf("----------------nvs config init ok----------------------\n");
    }
    // 提交配置
    ESP_ERROR_CHECK(nvs_commit(nvs_config_handler));
    nvs_close(nvs_config_handler);
}

void device_init(void)
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化设备参数和配置
    param_config_init();
    config_store_to_flash();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // 读取 STA 模式 MAC 地址

    printf("STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

}












/**
 * @brief 获取当前IOT设备状态
 */
void get_current_iot_state(char* device_status, size_t status_size, int32_t* test_value)
{
    if (device_status && status_size > 0) {
        strncpy(device_status, g_iot_state.device_status, status_size - 1);
        device_status[status_size - 1] = '\0';
    }
    if (test_value) {
        *test_value = g_iot_state.test_value;
    }
}

/**
 * @brief 更新设备状态
 */
void set_device_status(const char* device_status)
{
    if (device_status) {
        strncpy(g_iot_state.device_status, device_status, sizeof(g_iot_state.device_status) - 1);
        g_iot_state.device_status[sizeof(g_iot_state.device_status) - 1] = '\0';
        ESP_LOGI(TAG, "设备状态已更新: %s", g_iot_state.device_status);
    }
}

/**
 * @brief 更新测试数值
 */
void set_test_value(int32_t test_value)
{
    g_iot_state.test_value = test_value;
    ESP_LOGI(TAG, "测试数值已更新: %ld", (long)g_iot_state.test_value);
}

