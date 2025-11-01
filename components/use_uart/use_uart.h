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
#define UART_PORT_NUM          (1)      // UART1端口号
#define UART_TXD_PIN           (23)     // GPIO23作为TX（J3排针序号5）
#define UART_RXD_PIN           (24)     // GPIO24作为RX（J3排针序号4）
#define UART_BAUD_RATE         (115200) // 波特率

// 内部使用的别名（保持兼容性）
#define ECHO_UART_PORT_NUM      UART_PORT_NUM
#define ECHO_UART_BAUD_RATE     UART_BAUD_RATE

// 协议定义
#define MAX_FRAME_SIZE          (64)

// 下行数据类型定义
#define FRAME_TYPE_KEY_CMD      0x02  // 键值帧下行
#define FRAME_TYPE_READ_CMD     0x03  // 读寄存器下行
#define FRAME_TYPE_WRITE_CMD    0x04  // 写寄存器下行
#define FRAME_TYPE_CMD_CMD      0x05  // 指令帧下行

// 上行数据类型定义
#define FRAME_TYPE_KEY_RESP     0x82  // 键值设备应答
#define FRAME_TYPE_CMD_RESP     0x85  // 指令设备应答
#define FRAME_TYPE_READ_RESP    0x83  // 读设备应答
#define FRAME_TYPE_WRITE_RESP   0x84  // 写设备应答

// 键值定义
#define KEY_VALUE_SETTING       0x0001  // 设置键
#define KEY_VALUE_OPEN          0x0002  // 开门键
#define KEY_VALUE_PAIRING       0x0004  // 对码键
#define KEY_VALUE_CLOSE         0x0008  // 关门键
#define KEY_VALUE_REMOTE1       0x1000  // 遥控键1
#define KEY_VALUE_REMOTE2       0x2000  // 遥控键2
#define KEY_VALUE_VENT          0x4000  // 通风键
#define KEY_VALUE_CHILD_LOCK    0x8000  // 儿童锁键

// 指令定义
#define CMD_OPEN_DOOR           0x0001  // 控制门体开门
#define CMD_CLOSE_DOOR          0x0002  // 控制门体关门
#define CMD_STOP                0x0004  // 控制门体停止
#define CMD_SET_UPPER_LIMIT     0x0006  // 上限位设置
#define CMD_SET_LOWER_LIMIT     0x0007  // 下限位设置
#define CMD_STROKE_COMPLETE     0x0008  // 行程设置完成
#define CMD_PAIRING_MODE        0x0009  // 一键进入对码模式
#define CMD_FACTORY_RESET       0x000A  // 恢复出厂设置
#define CMD_CLEAR_RUN_COUNT     0x000B  // 清运行次数

// 寄存器地址定义
#define REG_COLLISION_LEVEL     0x10  // 遇阻反弹档位（1~9）
#define REG_CHILD_LOCK          0x11  // 儿童锁使能开关（0/1）
#define REG_INFRARED_PROTECT    0x12  // 红外保护使能开关（0/1）
#define REG_AUTO_CLOSE_MIN      0x13  // 自动关门分钟数（0~9）
#define REG_OPEN_FORCE          0x14  // 开门力度档位（1~9）
#define REG_CLOSE_SPEED         0x15  // 关门速度档位（5~9或0）
#define REG_INSTALL_DIR         0x16  // 安装方向（0正转/1反转）
#define REG_ELECTRIC_LOCK       0x17  // 电子锁使能（0/1）
#define REG_COURTYARD_MODE      0x18  // 庭院功能模式（0/1/2）
#define REG_REMOTE_LEARN        0x19  // 遥控器学习使能（0/1）
#define REG_STOP_TERMINAL       0x1A  // STOP端子（0常开/1常闭）
#define REG_VENT_POSITION       0x1B  // 通风位置（0~9）
#define REG_FOLLOW_FUNC         0x1C  // 跟随功能（0~9）
#define REG_MAINTENANCE_LEVEL   0x1D  // 保养等级（0~9）
#define REG_READ_ALL_FE         0xFE  // 读取所有参数-格式2（四元组格式）
#define REG_READ_ALL            0xFF  // 读取所有参数-格式1（固定格式）

// 1. 键值&指令应答（0x14字节长度）
typedef struct __attribute__((packed)) {
    uint8_t length;              // 长度：0x14
    uint8_t type;                // 类型：0x82/0x85
    uint8_t position_mode;       // 位置&模式（Bit7:位置有效性, Bit0-6:工作模式）
    uint8_t model_id;            // 型号ID（0x10~0x14）
    uint16_t motor_stroke;       // 电机总行程（大端序）
    uint16_t current_pos;        // 当前位置（大端序）
    uint8_t timeout;             // 设置超时（秒）
    uint8_t alarm;               // 报警提示（1~13）
    uint16_t motor_current;      // 电机电流（大端序）
    uint16_t auto_close;         // 自动关门时间（秒，大端序）
    uint16_t light_timeout;      // 灯超时（秒，大端序）
    uint8_t run_count_high;      // 运行次数高字节
    uint16_t run_count_low;      // 运行次数低2字节（大端序）
    uint8_t maintenance;         // 保养提示（0/1/2）
} device_status_frame_t;

// 2. 单个寄存器读写应答（0x06字节长度）
typedef struct __attribute__((packed)) {
    uint8_t length;              // 长度：0x06
    uint8_t type;                // 类型：0x83（读）/0x84（写）
    uint8_t reg_addr;            // 寄存器地址（0x10~0x1d）
    uint8_t value;               // 当前值
    uint8_t max_value;           // 上限值
    uint8_t min_value;           // 下限值
} register_resp_frame_t;

// 3. 读所有寄存器应答-格式1（0x14字节长度，功能码0xFF）
typedef struct __attribute__((packed)) {
    uint8_t length;              // 长度：0x14
    uint8_t type;                // 类型：0x83
    uint8_t func_code;           // 功能码：0xFF
    uint8_t reserved;            // 预留：0x00
    uint8_t collision_level;     // 遇阻反弹档位（1~9）
    uint8_t child_lock;          // 儿童锁（0/1）
    uint8_t infrared_protect;    // 红外保护（0/1）
    uint8_t auto_close_min;      // 自动关门分钟数（0~9）
    uint8_t open_force;          // 开门力度（1~9）
    uint8_t close_speed;         // 关门速度（5~9或0）
    uint8_t electric_lock;       // 电子锁（0/1）
    uint8_t install_dir;         // 安装方向（0/1）
    uint8_t courtyard_mode;      // 庭院功能（0/1/2）
    uint8_t remote_learn;        // 遥控器学习（0/1）
    uint8_t stop_terminal;       // STOP端子（0/1）
    uint8_t vent_position;       // 通风位置（0~9）
    uint8_t follow_func;         // 跟随功能（0~9）
    uint8_t maintenance_level;   // 保养等级（0~9）
    uint8_t maintenance_tip;     // 保养提示（0/1）
    uint8_t reserved2;           // 预留：0x00
} all_registers_frame_t;

// 4. 读所有寄存器应答-格式2（0x14字节长度，功能码0xFE）
// 每组寄存器数据：寄存器地址(1) + 值(1) + 上限(1) + 下限(1) = 4字节
typedef struct __attribute__((packed)) {
    uint8_t reg_addr;            // 寄存器地址
    uint8_t value;               // 当前值
    uint8_t max_value;           // 上限值
    uint8_t min_value;           // 下限值
} register_group_t;

typedef struct __attribute__((packed)) {
    uint8_t length;              // 长度：0x14 (20字节)
    uint8_t type;                // 类型：0x83
    uint8_t func_code;           // 功能码：0xFE
    uint8_t reserved;            // 预留：0x00
    register_group_t regs[4];    // 4组寄存器数据 (16字节)
} all_registers_fe_frame_t;

// 5. 键值帧下行数据（0x0C字节长度）
typedef struct __attribute__((packed)) {
    uint8_t length;              // 长度：0x0C
    uint8_t type;                // 类型：0x02
    uint8_t mac_addr[6];         // MAC地址（6字节）
    uint16_t key_value;          // 键值（2字节，小端序）
    uint16_t reserved;           // 预留（0x0000）
} key_frame_cmd_t;

// 6. 指令帧下行数据（0x0C字节长度）
typedef struct __attribute__((packed)) {
    uint8_t length;              // 长度：0x0C
    uint8_t type;                // 类型：0x05
    uint8_t mac_addr[6];         // MAC地址（6字节）
    uint16_t command;            // 指令（2字节，小端序）
    uint16_t reserved;           // 预留（0x0000）
} cmd_frame_cmd_t;

// 7. 读寄存器下行数据（0x0C字节长度）
typedef struct __attribute__((packed)) {
    uint8_t length;              // 长度：0x0C
    uint8_t type;                // 类型：0x03
    uint8_t mac_addr[6];         // MAC地址（6字节）
    uint8_t reg_addr;            // 寄存器地址（1字节）
    uint8_t value;               // 值（固定0x00）
    uint8_t max_value;           // 上限值（固定0x00）
    uint8_t min_value;           // 下限值（固定0x00）
} read_register_cmd_t;

// 8. 写寄存器下行数据（0x0C字节长度）
typedef struct __attribute__((packed)) {
    uint8_t length;              // 长度：0x0C
    uint8_t type;                // 类型：0x04
    uint8_t mac_addr[6];         // MAC地址（6字节）
    uint8_t reg_addr;            // 寄存器地址（1字节）
    uint8_t value;               // 写入值（1字节）
    uint8_t max_value;           // 上限值（固定0x00）
    uint8_t min_value;           // 下限值（固定0x00）
} write_register_cmd_t;

// 通用接收缓冲区（仅用于类型定义，实际缓冲由帧重组逻辑处理）
typedef union {
    device_status_frame_t status;
    register_resp_frame_t reg_resp;
    all_registers_frame_t all_regs;
    all_registers_fe_frame_t all_regs_fe;
    uint8_t raw_data[MAX_FRAME_SIZE];
} uart_rx_buffer_t;

/**
 * @brief 发送数据（同步，立即发送）
 * @param data 数据指针
 * @param len 数据长度
 */
void uart_send_data(const uint8_t *data, size_t len);

/**
 * @brief 检查UART是否已初始化
 * @return true: 已初始化, false: 未初始化
 */
bool uart_is_initialized(void);

/**
 * @brief 启动UART接收任务
 * @return pdPASS: 成功, pdFAIL: 失败
 */
BaseType_t start_uart_receive_task(void);

/**
 * @brief 发送键值帧（APP >> 设备）
 * @param mac_addr 目标设备MAC地址（6字节）
 * @param key_value 键值（参考KEY_VALUE_xxx宏定义）
 * @return ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t uart_send_key_frame(const uint8_t *mac_addr, uint16_t key_value);

/**
 * @brief 发送指令帧（APP >> 设备）
 * @param mac_addr 目标设备MAC地址（6字节）
 * @param command 指令（参考CMD_xxx宏定义）
 * @return ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t uart_send_cmd_frame(const uint8_t *mac_addr, uint16_t command);

/**
 * @brief 发送读寄存器帧（APP >> 设备）
 * @param mac_addr 目标设备MAC地址（6字节）
 * @param reg_addr 寄存器地址（参考REG_xxx宏定义）
 * @return ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t uart_send_read_register(const uint8_t *mac_addr, uint8_t reg_addr);

/**
 * @brief 发送写寄存器帧（APP >> 设备）
 * @param mac_addr 目标设备MAC地址（6字节）
 * @param reg_addr 寄存器地址（参考REG_xxx宏定义）
 * @param value 写入值
 * @return ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t uart_send_write_register(const uint8_t *mac_addr, uint8_t reg_addr, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* USE_UART_H */
