#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "use_uart.h"

static const char *TAG = "uart";
static QueueHandle_t uart1_queue;  // UART1事件队列
static bool uart_initialized = false;  // UART初始化标志

#define BUF_SIZE (1024)

// UART错误统计
typedef struct {
    uint32_t fifo_overflow;
    uint32_t buffer_full;
    uint32_t parity_error;
    uint32_t frame_error;
} uart_error_stats_t;

static uart_error_stats_t uart_errors = {0};

// 帧缓冲区（用于数据重组）
#define FRAME_BUFFER_SIZE 256
static uint8_t frame_buffer[FRAME_BUFFER_SIZE];
static uint32_t frame_buffer_count = 0;



/**
 * @brief 发送数据（同步，立即发送）
 */
void uart_send_data(const uint8_t *data, size_t len)
{
    // 参数校验
    if (data == NULL || len == 0) {
        ESP_LOGW(TAG, "uart_send_data: 无效参数 (data=%p, len=%zu)", data, len);
        return;
    }
    
    // 检查UART是否已初始化
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART未初始化，无法发送数据");
        return;
    }
    
    // 发送数据到UART
    int written = uart_write_bytes(UART_PORT_NUM, data, len);
    if (written < 0) {
        ESP_LOGE(TAG, "UART发送失败: %d", written);
        return;
    }
    
    // 等待发送完成
    esp_err_t err = uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "UART等待发送完成超时");
    }
    
    ESP_LOGI(TAG, "UART1已发送 %d/%zu 字节", written, len);
}

// 大端序转换辅助函数
static uint16_t bytes_to_uint16_be(uint8_t high, uint8_t low) {
    return ((uint16_t)high << 8) | low;
}

static uint32_t bytes_to_uint24_be(uint8_t high, uint8_t mid, uint8_t low) {
    return ((uint32_t)high << 16) | ((uint32_t)mid << 8) | low;
}

/**
 * @brief 解析设备状态帧（0x82/0x85应答）
 */
static void parse_device_status(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(device_status_frame_t)) {
        ESP_LOGW(TAG, "设备状态帧长度不足: %d < %d", len, sizeof(device_status_frame_t));
        return;
    }
    
    const device_status_frame_t *frame = (const device_status_frame_t *)data;
    
    const char *type_str = (frame->type == FRAME_TYPE_KEY_RESP) ? "键值应答" : "指令应答";
    bool pos_valid = (frame->position_mode & 0x80) ? true : false;
    uint8_t work_mode = frame->position_mode & 0x7F;
    
    uint16_t motor_stroke = bytes_to_uint16_be(data[4], data[5]);
    uint16_t current_pos = bytes_to_uint16_be(data[6], data[7]);
    uint16_t motor_current = bytes_to_uint16_be(data[10], data[11]);
    uint16_t auto_close = bytes_to_uint16_be(data[12], data[13]);
    uint16_t light_timeout = bytes_to_uint16_be(data[14], data[15]);
    uint32_t run_count = bytes_to_uint24_be(data[16], data[17], data[18]);
    
    ESP_LOGI(TAG, "═══ %s (0x%02X) ═══", type_str, frame->type);
    ESP_LOGI(TAG, "位置有效: %s, 工作模式: %d", pos_valid ? "是" : "否", work_mode);
    ESP_LOGI(TAG, "型号ID: 0x%02X, 电机总行程: %d", frame->model_id, motor_stroke);
    ESP_LOGI(TAG, "当前位置: %d, 设置超时: %d秒", current_pos, frame->timeout);
    ESP_LOGI(TAG, "报警提示: %d, 电机电流: %d", frame->alarm, motor_current);
    ESP_LOGI(TAG, "自动关门: %d秒, 灯超时: %d秒", auto_close, light_timeout);
    ESP_LOGI(TAG, "运行次数: %lu, 保养提示: %d", run_count, frame->maintenance);
}

/**
 * @brief 获取寄存器名称
 */
static const char* get_register_name(uint8_t reg_addr)
{
    switch (reg_addr) {
        case 0x10: return "遇阻反弹档位";
        case 0x11: return "儿童锁使能";
        case 0x12: return "红外保护使能";
        case 0x13: return "自动关门分钟数";
        case 0x14: return "开门力度档位";
        case 0x15: return "关门速度档位";
        case 0x16: return "安装方向";
        case 0x17: return "电子锁使能";
        case 0x18: return "庭院功能模式";
        case 0x19: return "遥控器学习使能";
        case 0x1A: return "STOP端子";
        case 0x1B: return "通风位置";
        case 0x1C: return "跟随功能";
        case 0x1D: return "保养等级";
        default: return "未知寄存器";
    }
}

/**
 * @brief 解析单个寄存器应答（0x83/0x84应答）
 */
static void parse_register_resp(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(register_resp_frame_t)) {
        ESP_LOGW(TAG, "寄存器应答帧长度不足: %d < %d", len, sizeof(register_resp_frame_t));
        return;
    }
    
    const register_resp_frame_t *frame = (const register_resp_frame_t *)data;
    
    const char *type_str = (frame->type == FRAME_TYPE_READ_RESP) ? "读应答" : "写应答";
    const char *reg_name = get_register_name(frame->reg_addr);
    
    ESP_LOGI(TAG, "═══ 寄存器%s (0x%02X) ═══", type_str, frame->type);
    ESP_LOGI(TAG, "寄存器: 0x%02X (%s)", frame->reg_addr, reg_name);
    ESP_LOGI(TAG, "当前值: %d, 范围: [%d, %d]", frame->value, frame->min_value, frame->max_value);
}

/**
 * @brief 解析所有寄存器应答（0x83功能码0xFF应答）
 */
static void parse_all_registers(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(all_registers_frame_t)) {
        ESP_LOGW(TAG, "所有寄存器应答帧长度不足: %d < %d", len, sizeof(all_registers_frame_t));
        return;
    }
    
    const all_registers_frame_t *frame = (const all_registers_frame_t *)data;
    
    ESP_LOGI(TAG, "═══ 读所有寄存器应答 (0x%02X, 功能码: 0x%02X) ═══", frame->type, frame->func_code);
    ESP_LOGI(TAG, "遇阻反弹: %d档, 儿童锁: %s", frame->collision_level, frame->child_lock ? "启用" : "关闭");
    ESP_LOGI(TAG, "红外保护: %s, 自动关门: %d分钟", frame->infrared_protect ? "启用" : "关闭", frame->auto_close_min);
    ESP_LOGI(TAG, "开门力度: %d档, 关门速度: %d%%", frame->open_force, frame->close_speed * 10);
    ESP_LOGI(TAG, "电子锁: %s, 安装方向: %s", frame->electric_lock ? "启用" : "关闭", frame->install_dir ? "反转" : "正转");
    ESP_LOGI(TAG, "庭院模式: %d, 遥控学习: %s", frame->courtyard_mode, frame->remote_learn ? "启用" : "关闭");
    ESP_LOGI(TAG, "STOP端子: %s, 通风位置: %d", frame->stop_terminal ? "常闭" : "常开", frame->vent_position);
    ESP_LOGI(TAG, "跟随功能: %d, 保养等级: %d, 保养提示: %d", frame->follow_func, frame->maintenance_level, frame->maintenance_tip);
}

/**
 * @brief 解析所有寄存器应答-格式2（0x83功能码0xFE应答）
 */
static void parse_all_registers_fe(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(all_registers_fe_frame_t)) {
        ESP_LOGW(TAG, "所有寄存器FE应答帧长度不足: %d < %d", len, sizeof(all_registers_fe_frame_t));
        return;
    }
    
    const all_registers_fe_frame_t *frame = (const all_registers_fe_frame_t *)data;
    
    ESP_LOGI(TAG, "═══ 读所有寄存器应答-格式2 (0x%02X, 功能码: 0x%02X) ═══", frame->type, frame->func_code);
    
    // 计算实际有多少组寄存器数据
    int valid_groups = 0;
    for (int i = 0; i < 4; i++) {
        if (frame->regs[i].reg_addr != 0x00) {
            valid_groups++;
        }
    }
    
    ESP_LOGI(TAG, "包含 %d 组寄存器数据:", valid_groups);
    
    // 解析每组寄存器数据
    for (int i = 0; i < 4; i++) {
        const register_group_t *reg = &frame->regs[i];
        
        // 遇到0x00寄存器地址表示结束
        if (reg->reg_addr == 0x00) {
            break;
        }
        
        const char *reg_name = get_register_name(reg->reg_addr);
        ESP_LOGI(TAG, "  [%d] 寄存器: 0x%02X (%s)", i+1, reg->reg_addr, reg_name);
        ESP_LOGI(TAG, "      当前值: %d, 范围: [%d, %d]", reg->value, reg->min_value, reg->max_value);
    }
}

/**
 * @brief 解析完整的一帧数据
 */
static void parse_complete_frame(const uint8_t *frame, uint16_t len)
{
    if (frame == NULL || len < 2) {
        return;
    }
    
    uint8_t frame_len = frame[0];  // 长度字段
    uint8_t frame_type = frame[1]; // 类型字段
    
    // 打印完整帧
    ESP_LOGI(TAG, "收到完整帧 %d字节 (长度:0x%02X, 类型:0x%02X)", len, frame_len, frame_type);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, frame, len, ESP_LOG_INFO);
    
    // 根据长度和类型分发解析
    if (frame_len == 0x14 && (frame_type == FRAME_TYPE_KEY_RESP || frame_type == FRAME_TYPE_CMD_RESP)) {
        // 设备状态应答
        parse_device_status(frame, len);
    }
    else if (frame_len == 0x06 && (frame_type == FRAME_TYPE_READ_RESP || frame_type == FRAME_TYPE_WRITE_RESP)) {
        // 单个寄存器应答
        parse_register_resp(frame, len);
    }
    else if (frame_len == 0x14 && frame_type == FRAME_TYPE_READ_RESP) {
        // 读所有寄存器应答，根据功能码区分
        if (len >= 3) {
            uint8_t func_code = frame[2];
            if (func_code == 0xFF) {
                // 功能码0xFF：固定格式（连续的参数值）
                parse_all_registers(frame, len);
            } else if (func_code == 0xFE) {
                // 功能码0xFE：四元组格式（寄存器地址+值+上下限）
                parse_all_registers_fe(frame, len);
            } else {
                ESP_LOGW(TAG, "未知的0x83类型帧，功能码: 0x%02X", func_code);
            }
        } else {
            ESP_LOGW(TAG, "0x83应答帧长度不足，无法获取功能码");
        }
    }
    else {
        ESP_LOGW(TAG, "未识别的帧格式: 长度=0x%02X, 类型=0x%02X", frame_len, frame_type);
    }
    
    ESP_LOGI(TAG, "");  // 空行分隔
}

/**
 * @brief UART接收数据处理（带帧缓冲和重组）
 */
void uart_DataReceive_handler(uint8_t *p, uint16_t len)
{
    // 参数校验
    if (p == NULL || len == 0) {
        ESP_LOGW(TAG, "无效数据: p=%p, len=%d", p, len);
        return;
    }
    
    ESP_LOGD(TAG, "收到 %d 字节原始数据", len);
    
    // 将新数据追加到帧缓冲区
    for (uint16_t i = 0; i < len; i++) {
        // 防止缓冲区溢出
        if (frame_buffer_count >= FRAME_BUFFER_SIZE) {
            ESP_LOGW(TAG, "帧缓冲区溢出，清空重新开始");
            frame_buffer_count = 0;
        }
        
        frame_buffer[frame_buffer_count++] = p[i];
        
        // 至少需要2字节（长度+类型）才能判断
        if (frame_buffer_count >= 2) {
            uint8_t expected_len = frame_buffer[0];  // 第一个字节是长度字段
            
            // 长度字段合法性检查
            if (expected_len != 0x14 && expected_len != 0x06) {
                // 不是合法的帧头，可能是数据错位，查找下一个可能的帧头
                ESP_LOGW(TAG, "非法长度字段 0x%02X，丢弃并重新同步", expected_len);
                frame_buffer_count = 0;
                continue;
            }
            
            // 检查是否收到完整的一帧
            if (frame_buffer_count >= expected_len) {
                // 收到完整帧，解析它
                parse_complete_frame(frame_buffer, expected_len);
                
                // 处理缓冲区中可能存在的剩余数据（粘包情况）
                uint16_t remaining = frame_buffer_count - expected_len;
                if (remaining > 0) {
                    ESP_LOGD(TAG, "缓冲区有 %d 字节剩余数据，继续处理", remaining);
                    // 将剩余数据移到缓冲区开头
                    memmove(frame_buffer, frame_buffer + expected_len, remaining);
                    frame_buffer_count = remaining;
                    // 注意：不需要回退循环，剩余数据已经在缓冲区中，
                    // 下一次接收新数据时会继续累积处理
                } else {
                    frame_buffer_count = 0;
                }
            }
        }
    }
}

static void uart_DataReceive_task(void *arg)
{
    ESP_LOGI(TAG, "UART接收任务启动");
    
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,                    // 115200
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,                  // 无校验
        .stop_bits = UART_STOP_BITS_1,                  // 1位停止位
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,          // 无硬件流控制    
        .source_clk = UART_SCLK_XTAL,                   // ESP32-C5使用晶振时钟源
    };
    uart_event_t event;
    
    // 订阅任务看门狗
    esp_task_wdt_add(NULL);

    // 配置UART驱动
    ESP_LOGI(TAG, "初始化UART%d (TX:GPIO%d, RX:GPIO%d, 波特率:%d)", 
             ECHO_UART_PORT_NUM, UART_TXD_PIN, UART_RXD_PIN, UART_BAUD_RATE);
    
    // 安装UART驱动（RX缓冲区、TX缓冲区、事件队列大小）
    esp_err_t ret = uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE, BUF_SIZE, 20, &uart1_queue, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART驱动安装失败: %d", ret);
        vTaskDelete(NULL);
        return;
    }
    
    // 配置UART参数
    ret = uart_param_config(ECHO_UART_PORT_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART参数配置失败: %d", ret);
        uart_driver_delete(ECHO_UART_PORT_NUM);
        vTaskDelete(NULL);
        return;
    }
    
    // 设置UART引脚
    ret = uart_set_pin(ECHO_UART_PORT_NUM, UART_TXD_PIN, UART_RXD_PIN, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART引脚设置失败: %d", ret);
        uart_driver_delete(ECHO_UART_PORT_NUM);
        vTaskDelete(NULL);
        return;
    }
    
    // 设置初始化标志
    uart_initialized = true;
    ESP_LOGI(TAG, "UART初始化完成");
    
    // 分配接收缓冲区
    uint8_t *dtmp = (uint8_t *) malloc(BUF_SIZE);
    if (dtmp == NULL) {
        ESP_LOGE(TAG, "分配UART接收缓冲区失败");
        uart_initialized = false;
        uart_driver_delete(ECHO_UART_PORT_NUM);  // 清理UART驱动
        vTaskDelete(NULL);
        return;
    }
    
    while (1)
    {
        // 喂狗：告诉看门狗任务还活着
        esp_task_wdt_reset();
        
        // 等待UART事件（使用超时避免看门狗超时）
        if (xQueueReceive(uart1_queue, (void *)&event, (TickType_t)pdMS_TO_TICKS(10000))) 
        {
            switch (event.type)
            {
            // UART接收数据事件
            case UART_DATA:
                // 只读取实际接收的字节数
                if (event.size > 0 && event.size <= BUF_SIZE) {
                    int len = uart_read_bytes(ECHO_UART_PORT_NUM, dtmp, event.size, pdMS_TO_TICKS(100));
                    if (len > 0) {
                        uart_DataReceive_handler(dtmp, len);
                    }
                } else {
                    ESP_LOGW(TAG, "异常的数据长度: %d", event.size);
                    uart_flush_input(ECHO_UART_PORT_NUM);
                }
                break;
            // FIFO溢出 - 清空所有缓冲区
            case UART_FIFO_OVF:
                uart_errors.fifo_overflow++;
                ESP_LOGW(TAG, "FIFO溢出 (累计: %lu)", uart_errors.fifo_overflow);
                uart_flush_input(ECHO_UART_PORT_NUM);
                xQueueReset(uart1_queue);
                // 清空帧缓冲区，防止数据损坏
                frame_buffer_count = 0;
                break;
            // 缓冲区满 - 清空所有缓冲区
            case UART_BUFFER_FULL:
                uart_errors.buffer_full++;
                ESP_LOGW(TAG, "缓冲区满 (累计: %lu)", uart_errors.buffer_full);
                uart_flush_input(ECHO_UART_PORT_NUM);
                xQueueReset(uart1_queue);
                // 清空帧缓冲区
                frame_buffer_count = 0;
                break;
            // RX中断检测
            case UART_BREAK:
                ESP_LOGD(TAG, "检测到RX中断");
                break;
            // 校验错误 - 清空帧缓冲区
            case UART_PARITY_ERR:
                uart_errors.parity_error++;
                ESP_LOGW(TAG, "校验错误 (累计: %lu)", uart_errors.parity_error);
                uart_flush_input(ECHO_UART_PORT_NUM);
                frame_buffer_count = 0;  // 清空帧缓冲区
                break;
            // 帧错误 - 清空帧缓冲区
            case UART_FRAME_ERR:
                uart_errors.frame_error++;
                ESP_LOGW(TAG, "帧错误 (累计: %lu)", uart_errors.frame_error);
                uart_flush_input(ECHO_UART_PORT_NUM);
                frame_buffer_count = 0;  // 清空帧缓冲区
                break;
            // 模式检测（未启用）
            case UART_PATTERN_DET:
                // 当前未使用模式检测功能
                break;
            // 其他事件
            default:
                ESP_LOGW(TAG, "未处理的UART事件类型: %d", event.type);
                break;
            }
        }            
    }
    // 注意：以下代码不会执行（无限循环）
    // 如果将来需要支持任务退出，需要添加退出条件
    free(dtmp);
    dtmp = NULL;
}

/**
 * @brief 检查UART是否已初始化
 */
bool uart_is_initialized(void)
{
    return uart_initialized;
}

/**
 * @brief 发送键值帧（APP >> 设备）
 * @param mac_addr 目标设备MAC地址（6字节）
 * @param key_value 键值（参考KEY_VALUE_xxx宏定义）
 * @return ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t uart_send_key_frame(const uint8_t *mac_addr, uint16_t key_value)
{
    // 参数校验
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "MAC地址为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查UART是否已初始化
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART未初始化，无法发送键值帧");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 构建键值帧
    key_frame_cmd_t frame = {
        .length = 0x0C,
        .type = FRAME_TYPE_KEY_CMD,
        .key_value = key_value,    // 结构体会自动按小端序存储
        .reserved = 0x0000
    };
    
    // 复制MAC地址
    memcpy(frame.mac_addr, mac_addr, 6);
    
    // 打印发送信息
    ESP_LOGI(TAG, "发送键值帧: MAC=%02X:%02X:%02X:%02X:%02X:%02X, 键值=0x%04X",
             mac_addr[0], mac_addr[1], mac_addr[2], 
             mac_addr[3], mac_addr[4], mac_addr[5], 
             key_value);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, &frame, sizeof(frame), ESP_LOG_INFO);
    
    // 发送数据
    uart_send_data((uint8_t*)&frame, sizeof(frame));
    
    return ESP_OK;
}

/**
 * @brief 发送指令帧（APP >> 设备）
 * @param mac_addr 目标设备MAC地址（6字节）
 * @param command 指令（参考CMD_xxx宏定义）
 * @return ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t uart_send_cmd_frame(const uint8_t *mac_addr, uint16_t command)
{
    // 参数校验
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "MAC地址为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查UART是否已初始化
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART未初始化，无法发送指令帧");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 构建指令帧
    cmd_frame_cmd_t frame = {
        .length = 0x0C,
        .type = FRAME_TYPE_CMD_CMD,
        .command = command,        // 结构体会自动按小端序存储
        .reserved = 0x0000
    };
    
    // 复制MAC地址
    memcpy(frame.mac_addr, mac_addr, 6);
    
    // 打印发送信息
    ESP_LOGI(TAG, "发送指令帧: MAC=%02X:%02X:%02X:%02X:%02X:%02X, 指令=0x%04X",
             mac_addr[0], mac_addr[1], mac_addr[2], 
             mac_addr[3], mac_addr[4], mac_addr[5], 
             command);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, &frame, sizeof(frame), ESP_LOG_INFO);
    
    // 发送数据
    uart_send_data((uint8_t*)&frame, sizeof(frame));
    
    return ESP_OK;
}

/**
 * @brief 获取寄存器名称（已在前面定义，用于读写日志）
 */
// 已在parse相关函数前面定义

/**
 * @brief 发送读寄存器帧（APP >> 设备）
 * @param mac_addr 目标设备MAC地址（6字节）
 * @param reg_addr 寄存器地址（参考REG_xxx宏定义）
 * @return ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t uart_send_read_register(const uint8_t *mac_addr, uint8_t reg_addr)
{
    // 参数校验
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "MAC地址为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查UART是否已初始化
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART未初始化，无法发送读寄存器帧");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 构建读寄存器帧
    read_register_cmd_t frame = {
        .length = 0x0C,
        .type = FRAME_TYPE_READ_CMD,
        .reg_addr = reg_addr,
        .value = 0x00,
        .max_value = 0x00,
        .min_value = 0x00
    };
    
    // 复制MAC地址
    memcpy(frame.mac_addr, mac_addr, 6);
    
    // 获取寄存器名称（用于日志）
    const char *reg_name = get_register_name(reg_addr);
    
    // 打印发送信息
    ESP_LOGI(TAG, "发送读寄存器帧: MAC=%02X:%02X:%02X:%02X:%02X:%02X, 寄存器=0x%02X (%s)",
             mac_addr[0], mac_addr[1], mac_addr[2], 
             mac_addr[3], mac_addr[4], mac_addr[5], 
             reg_addr, reg_name);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, &frame, sizeof(frame), ESP_LOG_INFO);
    
    // 发送数据
    uart_send_data((uint8_t*)&frame, sizeof(frame));
    
    return ESP_OK;
}

/**
 * @brief 发送写寄存器帧（APP >> 设备）
 * @param mac_addr 目标设备MAC地址（6字节）
 * @param reg_addr 寄存器地址（参考REG_xxx宏定义）
 * @param value 写入值
 * @return ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t uart_send_write_register(const uint8_t *mac_addr, uint8_t reg_addr, uint8_t value)
{
    // 参数校验
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "MAC地址为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查UART是否已初始化
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART未初始化，无法发送写寄存器帧");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 构建写寄存器帧
    write_register_cmd_t frame = {
        .length = 0x0C,
        .type = FRAME_TYPE_WRITE_CMD,
        .reg_addr = reg_addr,
        .value = value,
        .max_value = 0x00,
        .min_value = 0x00
    };
    
    // 复制MAC地址
    memcpy(frame.mac_addr, mac_addr, 6);
    
    // 获取寄存器名称（用于日志）
    const char *reg_name = get_register_name(reg_addr);
    
    // 打印发送信息
    ESP_LOGI(TAG, "发送写寄存器帧: MAC=%02X:%02X:%02X:%02X:%02X:%02X, 寄存器=0x%02X (%s), 值=%d",
             mac_addr[0], mac_addr[1], mac_addr[2], 
             mac_addr[3], mac_addr[4], mac_addr[5], 
             reg_addr, reg_name, value);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, &frame, sizeof(frame), ESP_LOG_INFO);
    
    // 发送数据
    uart_send_data((uint8_t*)&frame, sizeof(frame));
    
    return ESP_OK;
}

/**
 * @brief 启动UART接收任务
 */
BaseType_t start_uart_receive_task(void)
{
    if (uart_initialized) {
        ESP_LOGW(TAG, "UART任务已经启动");
        return pdPASS;
    }
    
    // 增加任务栈到3072字节（考虑到大量日志输出）
    BaseType_t ret = xTaskCreate(uart_DataReceive_task, "uart_receive", 3072, NULL, 5, NULL);
    
    if (ret == pdPASS) {
        ESP_LOGI(TAG, "UART接收任务创建成功");
    } else {
        ESP_LOGE(TAG, "UART接收任务创建失败");
    }
    
    return ret;
}

