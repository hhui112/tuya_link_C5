/*
 * 参考：https://github.com/espressif/esp-idf/tree/v5.5/examples/bluetooth/ble_get_started/nimble/NimBLE_GATT_Server
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Bluetooth */
#include "esp_bt.h"

/* NimBLE stack APIs (integrated in bt component) */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/util/util.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "use_ble_server.h"
#include "common.h"

static const char *TAG = "BLE";

/* 连接状态 */
static bool connected = false;
static uint16_t conn_handle = 0;

/* 数据存储 - 增大缓冲区以支持更大的数据包 */
static uint8_t received_data[512];  // 增大到512字节，匹配MAX_MSG_SIZE
static uint16_t received_len = 0;

/* 特征句柄（用于通知） */
static uint16_t notify_chr_val_handle = 0;

static void start_advertising(void);

/* UUID 定义 - 16位UUID转换为128位UUID（蓝牙标准基础UUID） */
/* 服务UUID: A002 -> 0000A002-0000-1000-8000-00805F9B34FB */
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x02, 0xA0, 0x00, 0x00);

/* 写特征UUID: C303 -> 0000C303-0000-1000-8000-00805F9B34FB */
static const ble_uuid128_t gatt_svr_write_chr_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x03, 0xC3, 0x00, 0x00);

/* 通知特征UUID: C305 -> 0000C305-0000-1000-8000-00805F9B34FB */
static const ble_uuid128_t gatt_svr_notify_chr_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x05, 0xC5, 0x00, 0x00);

/* 打印接收到的数据 */
static void print_received_data(void)
{
    if (received_len == 0) return;
    
    ESP_LOGI(TAG, "BLE data received length: %d bytes", received_len);
    char text[513] = {0};
    int copy_len = (received_len < 512) ? received_len : 512;
    memcpy(text, received_data, copy_len);
    
    ESP_LOGI(TAG, "BLE data: %s", text);
    
    if (copy_len < received_len) {
        ESP_LOGI(TAG, "BLE data is truncated, display the first 512 bytes");
    }
}

/* 写特征回调函数 */
static int gatt_svr_write_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGI(TAG, "Write characteristic received");
        received_len = OS_MBUF_PKTLEN(ctxt->om);
        
        // 获取当前MTU信息用于调试
        uint16_t current_mtu = ble_att_mtu(conn_handle);
        uint16_t max_write_len = (current_mtu > 3) ? (current_mtu - 3) : 20;
        ESP_LOGI(TAG, "Current MTU: %d, max write len: %d, received: %d", 
                 current_mtu, max_write_len, received_len);
        
        if (received_len > sizeof(received_data)) {
            ESP_LOGW(TAG, "Received data too large: %d > %zu, truncating", 
                     received_len, sizeof(received_data));
            received_len = sizeof(received_data);
        }
        
        ble_hs_mbuf_to_flat(ctxt->om, received_data, sizeof(received_data), &received_len);
        
        // 添加调试日志：打印接收到的原始数据（完整数据，最多256字节）
        if (received_len > 0) {
            int log_len = (received_len < 256) ? received_len : 256;
            ESP_LOGI(TAG, "BLE write data received, length: %d, data: %.*s", 
                     received_len, log_len, received_data);
        }
        
        // 准备统一消息结构
        g_msg_queue_t msg = {
            .source = MSG_SOURCE_BLE,
            .type = MSG_TYPE_CONTROL,
            .data_len = (received_len < MAX_MSG_SIZE - 1) ? received_len : (MAX_MSG_SIZE - 1)
        };
        memcpy(msg.data, received_data, msg.data_len);
        msg.data[msg.data_len] = '\0';  // 添加null结尾符，确保JSON解析有效
        
        // 发送到统一消息队列
        BaseType_t xStatus = xQueueSend(g_msg_queue, &msg, 0);
        if (xStatus == pdPASS) {
            ESP_LOGI(TAG, "BLE message queued (%d bytes)", msg.data_len);
        } else {
            ESP_LOGW(TAG, "Message queue full");
        }
        
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* 通知特征回调函数 */
static int gatt_svr_notify_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        // 通知特征只用于发送数据，不支持读取
        return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        // 可能是CCCD写入（启用/禁用通知）
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* GATT 服务定义 */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* 写特征 (C303) - 用于接收数据 */
                .uuid = &gatt_svr_write_chr_uuid.u,
                .access_cb = gatt_svr_write_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                /* 通知特征 (C305) - 用于发送数据 */
                .uuid = &gatt_svr_notify_chr_uuid.u,
                .access_cb = gatt_svr_notify_chr_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &notify_chr_val_handle,  // 保存特征值句柄，用于通知
            },
            {
                0, /* No more characteristics in this service */
            }
        },
    }, {
        0, /* No more services */
    },
};

/* GAP 事件处理 */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "连接事件，状态=%d", event->connect.status);
        if (event->connect.status == 0) {
            connected = true;
            conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "设备已连接，连接句柄: %d", conn_handle);
            
            // 主动触发 MTU 协商
            int mtu_rc = ble_gattc_exchange_mtu(conn_handle, NULL, NULL);
            if (mtu_rc != 0) {
                ESP_LOGW(TAG, "MTU 协商启动失败: %d", mtu_rc);
            }
        } else {
            ESP_LOGI(TAG, "连接失败，重新开始广播");
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "设备断开连接，开始重新广播");
        connected = false;
        conn_handle = 0;
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "广播完成，重新开始");
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "连接句柄=%d, MTU=%d 字节", 
                 event->mtu.conn_handle, event->mtu.value);
        // 验证MTU值
        //uint16_t actual_mtu = ble_att_mtu(event->mtu.conn_handle);
        //ESP_LOGI(TAG, "验证实际MTU: %d 字节", actual_mtu);
        return 0;

    default:
        return 0;
    }
}

/* 开始广播 */
static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;

    memset(&fields, 0, sizeof fields);

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv_params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "广播启动失败: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "广播已启动");
}

/* NimBLE 主机同步回调 */
static void ble_app_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    start_advertising();
}

/* NimBLE 主机任务 */
static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* 初始化 GATT 服务器 */
static int gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}



/* 外部接口实现 */

esp_err_t initialize_ble_server(void)
{
    ESP_LOGI(TAG, "初始化并启动 NimBLE GATT 服务器");

    // 初始化 NimBLE 端口
    nimble_port_init();

    /* 配置主机栈 */
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    
    /* 配置 ATT MTU 大小 */
    ble_att_set_preferred_mtu(500);  // 设置首选MTU为512字节，支持更大传输

    /* 初始化服务 */
    int rc = gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT 服务器初始化失败: %d", rc);
        return ESP_FAIL;
    }

    /* 动态生成设备名称：WYDL + MAC地址后6位（最后3个字节） */
    char device_name[16];  // "WYDL" + "XXXXXX" (6 hex chars) + '\0' = 11 chars
    if (device_info && device_info->device_mac[0] != 0) {
        // 使用MAC地址后6位（最后3个字节，6个十六进制字符）
        // 例如：MAC为 12:34:56:78:9A:BC，设备名为 WYDL789ABC
        snprintf(device_name, sizeof(device_name), "WYDL%02X%02X%02X",
                 device_info->device_mac[3], 
                 device_info->device_mac[4], 
                 device_info->device_mac[5]);
    } else {
        // 如果MAC地址未读取，使用默认名称
        strncpy(device_name, "WYDL000000", sizeof(device_name) - 1);
        device_name[sizeof(device_name) - 1] = '\0';
        ESP_LOGW(TAG, "MAC not read, use default name");
    }
    
    rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "Set device name failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE device name: %s", device_name);
    }

    /* 启动 NimBLE 主机任务 */
    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "BLE 服务器初始化并启动完成");
    return ESP_OK;
}

bool use_ble_server_is_connected(void)
{
    return connected;
}

uint16_t use_ble_server_get_conn_handle(void)
{
    return conn_handle;
}

esp_err_t use_ble_server_notify_data(const uint8_t* data, uint16_t len)
{
    if (!connected || conn_handle == 0) {
        ESP_LOGW(TAG, "Device not connected, cannot send data");
        return ESP_FAIL;
    }

    if (notify_chr_val_handle == 0) {
        ESP_LOGW(TAG, "Notify characteristic handle not set");
        return ESP_ERR_INVALID_STATE;
    }

    if (len == 0 || data == NULL) {
        ESP_LOGW(TAG, "Invalid data parameters");
        return ESP_ERR_INVALID_ARG;
    }

    /* 创建mbuf并发送通知 */
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to create mbuf");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, notify_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Notify failed: %d", rc);
        os_mbuf_free_chain(om);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Notify sent, length: %d", len);
    return ESP_OK;
}

esp_err_t use_ble_server_update_device_status(const char* status, int32_t value)
{
    char msg[64];
    int len = snprintf(msg, sizeof(msg), "状态:%s,值:%ld", status, (long)value);
    
    ESP_LOGI(TAG, "更新设备状态: %s", msg);
    return use_ble_server_notify_data((const uint8_t*)msg, len);
}

uint16_t use_ble_server_get_mtu(void)
{
    if (!connected || conn_handle == 0) {
        return 23; // 默认 MTU（未连接时）
    }
    
    uint16_t current_mtu = ble_att_mtu(conn_handle);
    ESP_LOGD(TAG, "当前连接MTU: %d 字节", current_mtu);
    return current_mtu;
}

uint16_t use_ble_server_get_max_data_len(void)
{
    uint16_t mtu = use_ble_server_get_mtu();
    // ATT 写操作需要减去 3 字节开销 (操作码 + 句柄)
    return (mtu > 3) ? (mtu - 3) : 20;
}