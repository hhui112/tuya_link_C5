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

/* 数据存储 */
static uint8_t received_data[256];
static uint16_t received_len = 0;

static void start_advertising(void);

/* UUID 定义 */
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11,
                     0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x33, 0x33);

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

/* GATT 服务器读写回调 */
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "APP 读取特征值");
        if (received_len > 0) {
            os_mbuf_append(ctxt->om, received_data, received_len);
        } else {
            const char *msg = "ESP32-C5 Ready";
            os_mbuf_append(ctxt->om, msg, strlen(msg));
        }
        return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGI(TAG, "APP 写入特征值");
        received_len = OS_MBUF_PKTLEN(ctxt->om);
        if (received_len > sizeof(received_data)) {
            received_len = sizeof(received_data);
        }
        
        ble_hs_mbuf_to_flat(ctxt->om, received_data, sizeof(received_data), &received_len);
        
        // 准备统一消息结构
        g_msg_queue_t msg = {
            .source = MSG_SOURCE_BLE,
            .type = MSG_TYPE_CONTROL,  // 默认为控制指令，后续由统一处理函数判断
            .data_len = (received_len < MAX_MSG_SIZE) ? received_len : MAX_MSG_SIZE
        };
        memcpy(msg.data, received_data, msg.data_len);
        
        // 发送到统一消息队列
        BaseType_t xStatus = xQueueSend(g_msg_queue, &msg, 0);
        if (xStatus == pdPASS) {
            ESP_LOGI(TAG, "BLE消息已入队 (%d字节)", msg.data_len);
        } else {
            ESP_LOGW(TAG, "消息队列已满");
            print_received_data();  // 作为备用处理
        }
        
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
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid = &gatt_svr_chr_uuid.u,
            .access_cb = gatt_svr_chr_access,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        }, {
            0, /* No more characteristics in this service */
        } },
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

    /* 设置设备名称 */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGW(TAG, "设置设备名称失败: %d", rc);
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
    if (!connected) {
        ESP_LOGW(TAG, "设备未连接，无法发送数据");
        return ESP_FAIL;
    }

    /* 更新本地数据 */
    if (len <= sizeof(received_data)) {
        memcpy(received_data, data, len);
        received_len = len;
        ESP_LOGI(TAG, "数据已更新到特征值: %d 字节", len);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "数据长度超限: %d > %d", len, sizeof(received_data));
        return ESP_ERR_INVALID_SIZE;
    }
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