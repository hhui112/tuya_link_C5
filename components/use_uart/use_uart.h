#ifndef USE_UART_H
#define USE_UART_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 串口配置
#define UART_PORT_NUM           (2)
#define UART_TXD_PIN           (19)
#define UART_RXD_PIN           (25)
#define UART_BAUD_RATE         (115200)

#define ECHO_UART_PORT_NUM      (2)
#define ECHO_UART_BAUD_RATE     (115200)
#define ECHO_TASK_STACK_SIZE    (1024)

// 新协议定义
#define PROTOCOL_FRAME_HEADER   (0x14)
#define PROTOCOL_DATA_SIZE      (19)
#define MAX_FRAME_SIZE          (64)

// 设备状态数据结构（0x14协议）
typedef struct __attribute__((packed)) {
    uint8_t length;         // 长度字段 (0x14)
    uint8_t type;           // 类型
    uint8_t position_mode;  // 位置&模式
    uint16_t model_id;      // 型号ID
    uint16_t motor_stroke;  // 电机总行程
    uint8_t current_pos;    // 当前位置
    uint8_t timeout;        // 设置超时
    uint16_t alarm;         // 报警提示
    uint16_t motor_current; // 电机电流
    uint16_t auto_close;    // 自动关门
    uint8_t light_timeout[3]; // 灯超时 (3字节)
    uint8_t run_count;      // 运行次数
    uint8_t maintenance;    // 保养提示
} device_status_frame_t;

// UART接收缓冲区结构
typedef union {
    device_status_frame_t frame_data;
    uint8_t raw_data[MAX_FRAME_SIZE];
} uart_rx_buffer_t;

// 全局变量声明
extern uart_rx_buffer_t g_uart_rx_buffer;

/**
 * @brief 初始化UART硬件
 */
esp_err_t uart_init(void);

/**
 * @brief 发送数据（同步，立即发送）
 * @param data 数据
 * @param len 长度
 */
void uart_send_data(const uint8_t *data, size_t len);

/**
 * @brief 从UART读取数据（非阻塞）
 * @param buffer 接收缓冲区
 * @param max_len 最大长度
 * @param timeout_ms 超时时间（毫秒）
 * @return 实际读取的字节数，-1表示错误
 */
int uart_receive_data(uint8_t *buffer, size_t max_len, uint32_t timeout_ms);

/**
 * @brief 清空接收缓冲区
 */
void uart_flush_rx_buffer(void);

/**
 * @brief 设备状态数据回调函数类型
 */
typedef void (*device_status_callback_t)(const device_status_frame_t *status);

/**
 * @brief 设置设备状态回调函数
 */
void set_device_status_callback(device_status_callback_t callback);

/**
 * @brief 解析0x14协议数据
 */
bool uart_parse(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* USE_UART_H */
