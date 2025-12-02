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
QueueHandle_t g_msg_queue = NULL;

static void read_tuya_config(void)
{
	const esp_partition_t *partition;
	partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
	
	if(partition != NULL)
	{
		esp_err_t ret = esp_partition_read(partition, 0, &tuya_config, sizeof(tuya_config_t));
		
		// 确保字符串以 \0 结尾（防止越界）
		tuya_config.product_id[sizeof(tuya_config.product_id) - 1] = '\0';
		tuya_config.device_id[sizeof(tuya_config.device_id) - 1] = '\0';
		tuya_config.device_secret[sizeof(tuya_config.device_secret) - 1] = '\0';
		
		// 检查读取是否成功以及数据是否有效（非全0）
		if(ret == ESP_OK && 
		   tuya_config.product_id[0] != '\0' && 
		   tuya_config.device_id[0] != '\0' && 
		   tuya_config.device_secret[0] != '\0')
		{
			// 安全复制：使用 strncpy 代替 memcpy+strlen
			strncpy(device_info->id, tuya_config.device_id, sizeof(device_info->id) - 1);
			device_info->id[sizeof(device_info->id) - 1] = '\0';
			
			// 涂鸦参数配置 - 安全复制
			strncpy(device_info->tuya.product_id, tuya_config.product_id, sizeof(device_info->tuya.product_id) - 1);
			device_info->tuya.product_id[sizeof(device_info->tuya.product_id) - 1] = '\0';
			
			strncpy(device_info->tuya.device_id, tuya_config.device_id, sizeof(device_info->tuya.device_id) - 1);
			device_info->tuya.device_id[sizeof(device_info->tuya.device_id) - 1] = '\0';
			
			strncpy(device_info->tuya.device_secret, tuya_config.device_secret, sizeof(device_info->tuya.device_secret) - 1);
			device_info->tuya.device_secret[sizeof(device_info->tuya.device_secret) - 1] = '\0';

			ESP_LOGI(TAG, "从 storage 分区读取涂鸦配置成功");
		}
		else
		{
			ESP_LOGW(TAG, "storage 分区数据无效，使用默认配置");
			goto use_default_config;
		}
	}
	else
	{
		ESP_LOGW(TAG, "未找到 storage 分区，使用默认配置");
		goto use_default_config;
	}
	
	// 跳过默认配置
	goto config_done;
	
use_default_config:
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
	}
	
config_done:
	ESP_LOGI(TAG, "PRODUCT_ID:%s, DEVICE_ID:%s", 
	         device_info->tuya.product_id, 
	         device_info->tuya.device_id);
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

    // 读取设备MAC地址（用于UART通信）
    esp_err_t ret = esp_read_mac(device_info->device_mac, ESP_MAC_WIFI_STA);  // 读取 STA 模式 MAC 地址

    if (ret == ESP_OK) {
    ESP_LOGI(TAG, "STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             device_info->device_mac[0], device_info->device_mac[1], device_info->device_mac[2], 
             device_info->device_mac[3], device_info->device_mac[4], device_info->device_mac[5]);
    } else {
        ESP_LOGE(TAG, "读取MAC地址失败: %s", esp_err_to_name(ret));
        // 使用默认MAC地址
        uint8_t default_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
        memcpy(device_info->device_mac, default_mac, 6);
        ESP_LOGW(TAG, "使用默认MAC地址");
    }
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
        ESP_LOGI(TAG, "从 NVS 读取 WiFi 配置: %s", device_info->wifi.ssid);
    } else {
        // 首次初始化，写入默认配置
        ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "wifiSsid", device_info->wifi.ssid));
        ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "wifiPasswd", device_info->wifi.passwd));
        ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "configFlag", "ok"));
        ESP_ERROR_CHECK(nvs_commit(nvs_config_handler));
        ESP_LOGI(TAG, "初始化 NVS WiFi 配置: %s", device_info->wifi.ssid);
    }
    // 提交配置
    ESP_ERROR_CHECK(nvs_commit(nvs_config_handler));
    nvs_close(nvs_config_handler);
}

void wifi_config_store_to_flash(void)
{
    // 检查device_info是否已正确初始化
    if (device_info == NULL) {
        ESP_LOGE(TAG, "device_info 未初始化，无法保存配置");
        return;
    }
    
    nvs_handle_t nvs_config_handler;
    
    // 打开 NVS 句柄
    esp_err_t ret = nvs_open("config_cfg", NVS_READWRITE, &nvs_config_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(ret));
        return;
    }

    ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "wifiSsid", device_info->wifi.ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "wifiPasswd", device_info->wifi.passwd));
    ESP_ERROR_CHECK(nvs_set_str(nvs_config_handler, "configFlag", "ok"));
    ESP_ERROR_CHECK(nvs_commit(nvs_config_handler));
    ESP_LOGI(TAG, "WiFi 配置已保存到 Flash: %s", device_info->wifi.ssid);

    // 提交配置
    ESP_ERROR_CHECK(nvs_commit(nvs_config_handler));
    nvs_close(nvs_config_handler);
}

void device_init(void)
{
    // NVS已在main.c中初始化，这里不需要重复初始化
    
    // 初始化设备参数和配置
    param_config_init();
    config_store_to_flash();
}

