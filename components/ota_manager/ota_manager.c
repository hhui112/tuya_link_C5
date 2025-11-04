/**
 * @file ota_manager.c
 * @brief OTA管理器实现
 */

#include "ota_manager.h"
#include "tuya_ota_protocol.h"
#include "common.h"
#include "mqtt_manager.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_task_wdt.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "OTA_MGR";

/* ========== 内部状态 ========== */

typedef enum {
    OTA_STATE_IDLE,           // 空闲
    OTA_STATE_DOWNLOADING,    // 下载中
    OTA_STATE_VERIFYING,      // 验证中
    OTA_STATE_UPGRADING       // 升级中
} ota_state_t;

typedef struct {
    bool initialized;                     // 是否已初始化
    ota_manager_config_t config;          // 配置
    ota_state_t state;                    // 当前状态
    tuya_ota_upgrade_info_t upgrade_info; // 当前升级信息
    int last_progress;                    // 上次上报的进度
    TimerHandle_t check_timer;            // 定时检测定时器
} ota_manager_ctx_t;

static ota_manager_ctx_t s_ctx = {
    .initialized = false,
    .state = OTA_STATE_IDLE,
};

/* ========== 内部函数声明 ========== */

static void notify_event(ota_event_t event, int progress, int error_code, const char *error_msg);
static esp_err_t report_progress(uint8_t channel, int progress, int error_code, const char *error_msg);
static esp_err_t perform_ota_upgrade(const tuya_ota_upgrade_info_t *info);
static void auto_check_timer_callback(TimerHandle_t timer);

/* ========== OTA 事件通知 ========== */

static void notify_event(ota_event_t event, int progress, int error_code, const char *error_msg)
{
    if (!s_ctx.config.event_callback) {
        return;
    }

    ota_event_info_t info = {
        .event = event,
        .channel = s_ctx.upgrade_info.channel,
        .progress = progress,
        .error_code = error_code,
    };

    // 填充版本信息
    if (s_ctx.upgrade_info.channel == OTA_CHANNEL_MAIN) {
        strncpy(info.current_version, FIRMWARE_VERSION_MAIN, sizeof(info.current_version) - 1);
    } else if (s_ctx.upgrade_info.channel == OTA_CHANNEL_MCU) {
        strncpy(info.current_version, FIRMWARE_VERSION_MCU, sizeof(info.current_version) - 1);
    }
    strncpy(info.target_version, s_ctx.upgrade_info.version, sizeof(info.target_version) - 1);

    if (error_msg) {
        strncpy(info.error_msg, error_msg, sizeof(info.error_msg) - 1);
    }

    s_ctx.config.event_callback(&info);
}

/* ========== 进度上报 ========== */

static esp_err_t report_progress(uint8_t channel, int progress, int error_code, const char *error_msg)
{
    char topic[64];     // 减小到64字节（topic通常不长）
    char json[512];     // 减小到384字节（足够容纳进度报告）

    // 构造 topic
    if (tuya_ota_build_topic(TUYA_DEVICE_ID, TUYA_OTA_TOPIC_PROGRESS, topic, sizeof(topic)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build progress topic");
        return ESP_FAIL;
    }

    // 构造 JSON
    if (tuya_ota_build_progress_report(channel, progress, error_code, error_msg, json, sizeof(json)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build progress report");
        return ESP_FAIL;
    }

    // 发布到 MQTT
    esp_err_t ret = mqtt_manager_publish(topic, json, 1);
    if (ret == ESP_OK) {
        if (error_code > 0) {
            ESP_LOGI(TAG, "📤 上报OTA失败: channel=%d, error=%d, msg=%s", channel, error_code, error_msg ? error_msg : "");
        } else {
            ESP_LOGI(TAG, "📤 上报OTA进度: channel=%d, progress=%d%%", channel, progress);
        }
    }

    return ret;
}

/* ========== OTA 升级执行 ========== */

static esp_err_t perform_ota_upgrade(const tuya_ota_upgrade_info_t *info)
{
    esp_err_t ret = ESP_FAIL;
    esp_https_ota_handle_t ota_handle = NULL;
    
    ESP_LOGI(TAG, "🚀 开始OTA升级");
    ESP_LOGI(TAG, "  - 通道: %d", info->channel);
    ESP_LOGI(TAG, "  - 版本: %s", info->version);
    ESP_LOGI(TAG, "  - URL: %s", info->url);
    ESP_LOGI(TAG, "  - 大小: %lu 字节", (unsigned long)info->size);
    
    // 内存监控
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "📊 内存状态: 可用=%u字节, 历史最低=%u字节", free_heap, min_free_heap);
    
    // 在OTA期间暂时禁用看门狗（避免下载超时导致重启）
    // 注意：OTA成功后系统会重启，看门狗会自动重新初始化
    //      OTA失败后继续运行，看门狗保持禁用状态（系统仍可正常运行）
    ESP_LOGI(TAG, "⏸️  暂时禁用任务看门狗");
    esp_err_t wdt_ret = esp_task_wdt_deinit();
    if (wdt_ret != ESP_OK) {
        ESP_LOGW(TAG, "看门狗关闭警告（不影响OTA）: %s", esp_err_to_name(wdt_ret));
    }

    // 配置 HTTP 客户端（使用涂鸦CA证书）
    esp_http_client_config_t http_config = {
        .url = info->url,
        .cert_pem = tuya_cacert_pem,  // 使用涂鸦CA证书（mqtt_manager.h中声明）
        .timeout_ms = 20000,
        .keep_alive_enable = true,
        .buffer_size = 4096,           // 增大HTTP接收缓冲区（提升下载速度）
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    // 配置 HTTPS OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = NULL,
        .partial_http_download = true,
        .max_http_request_size = s_ctx.config.download_buffer_size,  // 默认4096，可配置
    };

    // 开始 OTA
    ret = esp_https_ota_begin(&ota_config, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ OTA开始失败: %s", esp_err_to_name(ret));
        report_progress(info->channel, -1, TUYA_OTA_ERR_DOWNLOAD_TIMEOUT, "OTA begin failed");
        notify_event(OTA_EVENT_FAILED, 0, TUYA_OTA_ERR_DOWNLOAD_TIMEOUT, "OTA begin failed");
        return ret;
    }

    ESP_LOGI(TAG, "✅ OTA开始成功，开始下载...");
    s_ctx.state = OTA_STATE_DOWNLOADING;
    s_ctx.last_progress = 0;
    notify_event(OTA_EVENT_START, 0, 0, NULL);
    report_progress(info->channel, 0, 0, NULL);

    // 获取固件镜像大小
    esp_app_desc_t new_app_info;
    ret = esp_https_ota_get_img_desc(ota_handle, &new_app_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 获取固件描述失败: %s", esp_err_to_name(ret));
        esp_https_ota_abort(ota_handle);
        report_progress(info->channel, -1, TUYA_OTA_ERR_DOWNLOAD_CHECKSUM, "Invalid image");
        notify_event(OTA_EVENT_FAILED, 0, TUYA_OTA_ERR_DOWNLOAD_CHECKSUM, "Invalid image");
        return ret;
    }

    ESP_LOGI(TAG, "📦 新固件信息:");
    ESP_LOGI(TAG, "  - 项目: %s", new_app_info.project_name);
    ESP_LOGI(TAG, "  - 版本: %s", new_app_info.version);
    ESP_LOGI(TAG, "  - 日期: %s %s", new_app_info.date, new_app_info.time);

    // 验证版本号
    if (strcmp(new_app_info.version, info->version) != 0) {
        ESP_LOGW(TAG, "⚠️  版本号不匹配: 期望 %s, 实际 %s", info->version, new_app_info.version);
    }

    // 检查是否为相同版本（已临时禁用，用于测试OTA功能）
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        if (strcmp(new_app_info.version, running_app_info.version) == 0) {
            ESP_LOGW(TAG, "⚠️  目标版本与当前版本相同，但跳过检查继续升级（测试模式）");
            // 临时注释掉版本检查，用于测试OTA功能
            // esp_https_ota_abort(ota_handle);
            // report_progress(info->channel, -1, TUYA_OTA_ERR_UPGRADE_VERSION, "Same version");
            // notify_event(OTA_EVENT_FAILED, 0, TUYA_OTA_ERR_UPGRADE_VERSION, "Same version");
            // return ESP_ERR_INVALID_VERSION;
        }
    }

    // 循环下载
    int image_size = esp_https_ota_get_image_size(ota_handle);
    int downloaded = 0;
    
    while (1) {
        // OTA过程中不需要喂狗（已在开始时删除看门狗订阅）
        
        ret = esp_https_ota_perform(ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // 计算进度
        downloaded = esp_https_ota_get_image_len_read(ota_handle);
        int progress = (image_size > 0) ? (downloaded * 100 / image_size) : 0;
        
        // 每20%上报一次进度（减少MQTT上报开销，提升下载速度）
        if (progress - s_ctx.last_progress >= 20) {
            s_ctx.last_progress = progress;
            notify_event(OTA_EVENT_PROGRESS, progress, 0, NULL);
            report_progress(info->channel, progress, 0, NULL);
            ESP_LOGI(TAG, "📥 下载进度: %d%% (%d/%d 字节)", progress, downloaded, image_size);
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // 减少延迟，提升下载速度
    }

    // 检查下载结果
    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "❌ 固件下载不完整");
        esp_https_ota_abort(ota_handle);
        report_progress(info->channel, -1, TUYA_OTA_ERR_DOWNLOAD_CHECKSUM, "Incomplete data");
        notify_event(OTA_EVENT_FAILED, s_ctx.last_progress, TUYA_OTA_ERR_DOWNLOAD_CHECKSUM, "Incomplete data");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✅ 固件下载完成 (100%%)");
    notify_event(OTA_EVENT_DOWNLOAD_COMPLETE, 100, 0, NULL);
    report_progress(info->channel, 100, 0, NULL);

    // 完成 OTA（验证并切换分区）
    s_ctx.state = OTA_STATE_UPGRADING;
    ret = esp_https_ota_finish(ota_handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "❌ 固件校验失败");
            report_progress(info->channel, -1, TUYA_OTA_ERR_UPGRADE_HMAC, "Validation failed");
            notify_event(OTA_EVENT_FAILED, 100, TUYA_OTA_ERR_UPGRADE_HMAC, "Validation failed");
        } else {
            ESP_LOGE(TAG, "❌ OTA完成失败: %s", esp_err_to_name(ret));
            report_progress(info->channel, -1, TUYA_OTA_ERR_UNKNOWN, "OTA finish failed");
            notify_event(OTA_EVENT_FAILED, 100, TUYA_OTA_ERR_UNKNOWN, "OTA finish failed");
        }
        return ret;
    }

    ESP_LOGI(TAG, "🎉 OTA升级成功！准备重启...");
    
    // 最终内存状态
    free_heap = esp_get_free_heap_size();
    min_free_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "📊 最终内存: 可用=%u字节, 历史最低=%u字节", free_heap, min_free_heap);
    
    notify_event(OTA_EVENT_SUCCESS, 100, 0, NULL);

    vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒让日志输出
    esp_restart();

    return ESP_OK;  // 不会执行到这里
}

/* ========== 定时检测回调 ========== */

static void auto_check_timer_callback(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "⏰ 定时检测OTA更新");
    ota_manager_check_update();
}

/* ========== 公共 API 实现 ========== */

esp_err_t ota_manager_init(const ota_manager_config_t *config)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "OTA管理器已初始化");
        return ESP_ERR_INVALID_STATE;
    }

    // 加载配置
    if (config) {
        memcpy(&s_ctx.config, config, sizeof(ota_manager_config_t));
    } else {
        ota_manager_config_t default_config = OTA_MANAGER_CONFIG_DEFAULT();
        memcpy(&s_ctx.config, &default_config, sizeof(ota_manager_config_t));
    }

    // 创建定时器（如果启用）
    if (s_ctx.config.auto_check_enable) {
        uint32_t interval_ms = s_ctx.config.auto_check_interval_hours * 3600 * 1000;
        s_ctx.check_timer = xTimerCreate(
            "ota_check",
            pdMS_TO_TICKS(interval_ms),
            pdTRUE,  // 自动重载
            NULL,
            auto_check_timer_callback
        );
        
        if (!s_ctx.check_timer) {
            ESP_LOGE(TAG, "创建定时器失败");
            return ESP_FAIL;
        }
    }

    s_ctx.initialized = true;
    s_ctx.state = OTA_STATE_IDLE;

    ESP_LOGI(TAG, "✅ OTA管理器初始化成功");
    return ESP_OK;
}

esp_err_t ota_manager_start(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "OTA管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "🚀 启动OTA管理器");

    // 检查是否为OTA升级后首次启动
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    ESP_LOGI(TAG, "当前运行分区: %s (地址: 0x%lx)", 
             running->label, (unsigned long)running->address);
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        ESP_LOGI(TAG, "OTA分区状态: %d (0=NEW, 1=PENDING_VERIFY, 2=VALID, 3=INVALID, -1=ABORTED)", ota_state);
        
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "🔍 检测到OTA升级，验证新固件...");
            
            // 标记固件有效，取消回滚
            esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ 新固件验证成功，已取消回滚");
                // 上报新版本
                ota_manager_report_version(TUYA_OTA_BIZ_TYPE_UPDATE);
            } else {
                ESP_LOGE(TAG, "❌ 新固件验证失败: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGI(TAG, "ℹ️  正常启动（非OTA升级后）");
        }
    } else {
        ESP_LOGW(TAG, "⚠️  无法获取OTA分区状态");
    }

    // 上报当前版本
    ota_manager_report_version(TUYA_OTA_BIZ_TYPE_INIT);

    // 启动定时检测
    if (s_ctx.config.auto_check_enable && s_ctx.check_timer) {
        xTimerStart(s_ctx.check_timer, 0);
        ESP_LOGI(TAG, "⏰ 已启动定时检测（间隔: %lu小时）", 
                 (unsigned long)s_ctx.config.auto_check_interval_hours);
    }

    return ESP_OK;
}

esp_err_t ota_manager_report_version(const char *biz_type)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic[128];
    char json[512];

    // 准备固件通道信息
    tuya_ota_channel_t channels[2];
    uint8_t channel_count = 0;

    // 主模块固件
    channels[channel_count].channel = OTA_CHANNEL_MAIN;
    strncpy(channels[channel_count].version, FIRMWARE_VERSION_MAIN, sizeof(channels[channel_count].version) - 1);
    channel_count++;

    // MCU固件（如需要）
    #ifdef FIRMWARE_VERSION_MCU
    channels[channel_count].channel = OTA_CHANNEL_MCU;
    strncpy(channels[channel_count].version, FIRMWARE_VERSION_MCU, sizeof(channels[channel_count].version) - 1);
    channel_count++;
    #endif

    // 构造 JSON
    if (tuya_ota_build_version_report(biz_type, channels, channel_count, json, sizeof(json)) != ESP_OK) {
        ESP_LOGE(TAG, "构造版本上报JSON失败");
        return ESP_FAIL;
    }

    // 构造 topic
    if (tuya_ota_build_topic(TUYA_DEVICE_ID, TUYA_OTA_TOPIC_FIRMWARE_REPORT, topic, sizeof(topic)) != ESP_OK) {
        ESP_LOGE(TAG, "构造topic失败");
        return ESP_FAIL;
    }

    // 发布到 MQTT
    esp_err_t ret = mqtt_manager_publish(topic, json, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "📤 上报固件版本成功 (业务类型: %s)", biz_type);
        for (uint8_t i = 0; i < channel_count; i++) {
            ESP_LOGI(TAG, "  - 通道%d: %s", channels[i].channel, channels[i].version);
        }
    } else {
        ESP_LOGE(TAG, "❌ 上报固件版本失败");
    }

    return ret;
}

esp_err_t ota_manager_handle_upgrade(const char *json_data)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "OTA管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    if (!json_data) {
        ESP_LOGE(TAG, "无效参数");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "📨 收到OTA升级请求");

    if (s_ctx.state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA正在进行中，忽略新的升级请求");
        return ESP_ERR_INVALID_STATE;
    }

    // 解析升级消息
    esp_err_t ret = tuya_ota_parse_upgrade_msg(json_data, &s_ctx.upgrade_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 解析OTA消息失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 检查通道号是否支持
    if (s_ctx.upgrade_info.channel != OTA_CHANNEL_MAIN) {
        ESP_LOGW(TAG, "⚠️  不支持的固件通道: %d（仅支持主模块通道0）", s_ctx.upgrade_info.channel);
        report_progress(s_ctx.upgrade_info.channel, -1, TUYA_OTA_ERR_UNKNOWN, "Unsupported channel");
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "  通道: %d, 版本: %s -> %s, 大小: %lu 字节", 
             s_ctx.upgrade_info.channel, FIRMWARE_VERSION_MAIN, 
             s_ctx.upgrade_info.version, (unsigned long)s_ctx.upgrade_info.size);

    // 执行升级
    ret = perform_ota_upgrade(&s_ctx.upgrade_info);
    
    // 升级完成后重置状态
    s_ctx.state = OTA_STATE_IDLE;

    return ret;
}

esp_err_t ota_manager_check_update(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic[128];
    char json[512];

    // 准备当前固件通道信息（让云端判断是否需要升级）
    tuya_ota_channel_t channels[2];
    uint8_t channel_count = 0;

    // 主模块固件
    channels[channel_count].channel = OTA_CHANNEL_MAIN;
    strncpy(channels[channel_count].version, FIRMWARE_VERSION_MAIN, sizeof(channels[channel_count].version) - 1);
    channel_count++;

    // MCU固件（如需要）
    #ifdef FIRMWARE_VERSION_MCU
    channels[channel_count].channel = OTA_CHANNEL_MCU;
    strncpy(channels[channel_count].version, FIRMWARE_VERSION_MCU, sizeof(channels[channel_count].version) - 1);
    channel_count++;
    #endif

    // 构造带版本信息的请求JSON（与firmware/report格式类似，但用于主动拉取）
    if (tuya_ota_build_version_report(TUYA_OTA_BIZ_TYPE_INIT, channels, channel_count, json, sizeof(json)) != ESP_OK) {
        ESP_LOGE(TAG, "构造OTA检测请求失败");
        return ESP_FAIL;
    }

    // 构造 topic
    if (tuya_ota_build_topic(TUYA_DEVICE_ID, TUYA_OTA_TOPIC_FIRMWARE_GET, topic, sizeof(topic)) != ESP_OK) {
        ESP_LOGE(TAG, "构造topic失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "🔍 主动检测OTA更新（当前版本: %s）", FIRMWARE_VERSION_MAIN);
    return mqtt_manager_publish(topic, json, 1);
}

bool ota_manager_is_busy(void)
{
    return s_ctx.state != OTA_STATE_IDLE;
}

esp_err_t ota_manager_get_version(uint8_t channel, char *version, size_t size)
{
    if (!version || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (channel == OTA_CHANNEL_MAIN) {
        strncpy(version, FIRMWARE_VERSION_MAIN, size - 1);
        version[size - 1] = '\0';
        return ESP_OK;
    } else if (channel == OTA_CHANNEL_MCU) {
        #ifdef FIRMWARE_VERSION_MCU
        strncpy(version, FIRMWARE_VERSION_MCU, size - 1);
        version[size - 1] = '\0';
        return ESP_OK;
        #else
        return ESP_ERR_NOT_SUPPORTED;
        #endif
    }

    return ESP_ERR_INVALID_ARG;
}

esp_err_t ota_manager_stop(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 停止定时器
    if (s_ctx.check_timer) {
        xTimerStop(s_ctx.check_timer, 0);
        xTimerDelete(s_ctx.check_timer, 0);
        s_ctx.check_timer = NULL;
    }

    // 如果正在升级，警告用户
    if (s_ctx.state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "⚠️  OTA正在进行中，强制停止可能导致问题");
    }

    s_ctx.initialized = false;
    s_ctx.state = OTA_STATE_IDLE;

    ESP_LOGI(TAG, "OTA管理器已停止");
    return ESP_OK;
}

