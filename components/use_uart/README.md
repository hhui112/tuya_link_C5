# UART 串口通信模块

## 📝 模块概述

本模块实现了完整的串口通信协议，用于ESP32-C5与门体控制设备之间的通信。

**协议实现完成度：100%** ✅

---

## 🎯 功能特性

### 下行数据发送（APP >> 设备）
1. **键值帧** - 模拟8种按键操作
2. **指令帧** - 9种直接控制指令
3. **读寄存器** - 读取14个配置参数 + 2种读所有格式
4. **写寄存器** - 写入14个配置参数

### 上行数据解析（设备 >> APP）
1. **设备状态应答** - 20字节完整状态信息
2. **寄存器应答** - 单个寄存器读写应答
3. **所有寄存器应答** - 两种格式（0xFF/0xFE）

### 辅助功能
- ✅ 自动帧分发和解析
- ✅ 粘包/分包处理
- ✅ 错误处理和统计
- ✅ 详细日志输出
- ✅ 参数名称映射

---

## 🚀 快速开始

### 1. 初始化UART

```c
#include "use_uart.h"

void app_main(void)
{
    // 启动UART接收任务
    start_uart_receive_task();
    
    // 等待初始化完成
    while (!uart_is_initialized()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### 2. 发送指令

```c
// 目标设备MAC地址
uint8_t device_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

// 发送开门指令
uart_send_cmd_frame(device_mac, CMD_OPEN_DOOR);

// 发送开门键值
uart_send_key_frame(device_mac, KEY_VALUE_OPEN);

// 读取儿童锁状态
uart_send_read_register(device_mac, REG_CHILD_LOCK);

// 设置开门力度为7档
uart_send_write_register(device_mac, REG_OPEN_FORCE, 7);
```

### 3. 自动接收解析

设备应答会自动接收、解析并输出日志：

```
I (12345) uart: ═══ 指令应答 (0x85) ═══
I (12345) uart: 位置有效: 是, 工作模式: 0
I (12345) uart: 当前位置: 1250, 电机总行程: 2500
...
```

---

## 📚 API 函数

### 发送函数

| 函数 | 功能 | 参数 |
|------|------|------|
| `uart_send_key_frame()` | 发送键值帧 | MAC地址, 键值 |
| `uart_send_cmd_frame()` | 发送指令帧 | MAC地址, 指令 |
| `uart_send_read_register()` | 读取寄存器 | MAC地址, 寄存器地址 |
| `uart_send_write_register()` | 写入寄存器 | MAC地址, 寄存器地址, 值 |

### 辅助函数

| 函数 | 功能 |
|------|------|
| `uart_is_initialized()` | 检查UART是否初始化 |
| `start_uart_receive_task()` | 启动UART接收任务 |

---

## 🔧 宏定义速查

### 键值（8个）
```c
KEY_VALUE_SETTING       0x0001  // 设置键
KEY_VALUE_OPEN          0x0002  // 开门键
KEY_VALUE_PAIRING       0x0004  // 对码键
KEY_VALUE_CLOSE         0x0008  // 关门键
KEY_VALUE_REMOTE1       0x1000  // 遥控键1
KEY_VALUE_REMOTE2       0x2000  // 遥控键2
KEY_VALUE_VENT          0x4000  // 通风键
KEY_VALUE_CHILD_LOCK    0x8000  // 儿童锁键
```

### 指令（9个）
```c
CMD_OPEN_DOOR           0x0001  // 控制门体开门
CMD_CLOSE_DOOR          0x0002  // 控制门体关门
CMD_STOP                0x0004  // 控制门体停止
CMD_SET_UPPER_LIMIT     0x0006  // 上限位设置
CMD_SET_LOWER_LIMIT     0x0007  // 下限位设置
CMD_STROKE_COMPLETE     0x0008  // 行程设置完成
CMD_PAIRING_MODE        0x0009  // 一键进入对码模式
CMD_FACTORY_RESET       0x000A  // 恢复出厂设置
CMD_CLEAR_RUN_COUNT     0x000B  // 清运行次数
```

### 寄存器（14个 + 2个特殊）
```c
REG_COLLISION_LEVEL     0x10  // 遇阻反弹档位（1~9）
REG_CHILD_LOCK          0x11  // 儿童锁使能（0/1）
REG_INFRARED_PROTECT    0x12  // 红外保护使能（0/1）
REG_AUTO_CLOSE_MIN      0x13  // 自动关门分钟数（0~9）
REG_OPEN_FORCE          0x14  // 开门力度档位（1~9）
REG_CLOSE_SPEED         0x15  // 关门速度档位（5~9或0）
REG_INSTALL_DIR         0x16  // 安装方向（0/1）
REG_ELECTRIC_LOCK       0x17  // 电子锁使能（0/1）
REG_COURTYARD_MODE      0x18  // 庭院功能模式（0/1/2）
REG_REMOTE_LEARN        0x19  // 遥控器学习使能（0/1）
REG_STOP_TERMINAL       0x1A  // STOP端子（0/1）
REG_VENT_POSITION       0x1B  // 通风位置（0~9）
REG_FOLLOW_FUNC         0x1C  // 跟随功能（0~9）
REG_MAINTENANCE_LEVEL   0x1D  // 保养等级（0~9）
REG_READ_ALL_FE         0xFE  // 读所有参数-格式2
REG_READ_ALL            0xFF  // 读所有参数-格式1
```

---

## 📖 详细文档

| 文档 | 说明 |
|------|------|
| `协议实现完整性检查.md` | 完整的协议实现检查报告（推荐阅读） |
| `读所有寄存器使用说明.md` | 读所有寄存器专项说明 |
| `use_uart.h` | 完整的API定义和数据结构 |
| `use_uart.c` | 实现代码（671行） |

---

## 💡 使用示例

### 示例1：基本门控制

```c
uint8_t device_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

// 开门
uart_send_cmd_frame(device_mac, CMD_OPEN_DOOR);
vTaskDelay(pdMS_TO_TICKS(5000));

// 停止
uart_send_cmd_frame(device_mac, CMD_STOP);
vTaskDelay(pdMS_TO_TICKS(1000));

// 关门
uart_send_cmd_frame(device_mac, CMD_CLOSE_DOOR);
```

### 示例2：配置设备参数

```c
uint8_t device_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

// 启用儿童锁
uart_send_write_register(device_mac, REG_CHILD_LOCK, 1);
vTaskDelay(pdMS_TO_TICKS(200));

// 设置开门力度为7档
uart_send_write_register(device_mac, REG_OPEN_FORCE, 7);
vTaskDelay(pdMS_TO_TICKS(200));

// 设置自动关门为3分钟
uart_send_write_register(device_mac, REG_AUTO_CLOSE_MIN, 3);
vTaskDelay(pdMS_TO_TICKS(200));

// 读取所有参数确认
uart_send_read_register(device_mac, REG_READ_ALL);
```

### 示例3：查询设备状态

```c
uint8_t device_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

// 读取单个参数
uart_send_read_register(device_mac, REG_CHILD_LOCK);

// 读取所有参数（格式1）
uart_send_read_register(device_mac, REG_READ_ALL);

// 读取所有参数（格式2）
uart_send_read_register(device_mac, REG_READ_ALL_FE);
```

---

## ⚙️ 配置说明

### UART配置（use_uart.h）

```c
#define UART_PORT_NUM          (1)      // UART1端口号
#define UART_TXD_PIN           (23)     // GPIO23作为TX
#define UART_RXD_PIN           (24)     // GPIO24作为RX
#define UART_BAUD_RATE         (115200) // 波特率
```

### 缓冲区配置

```c
#define BUF_SIZE               (1024)   // UART缓冲区大小
#define FRAME_BUFFER_SIZE      (256)    // 帧缓冲区大小
```

---

## 🔍 调试说明

### 查看发送日志

```
I (12345) uart: 发送指令帧: MAC=AA:BB:CC:DD:EE:FF, 指令=0x0001
I (12345) uart: 0x3ffb1234   0c 05 aa bb cc dd ee ff  01 00 00 00
I (12346) uart: UART1已发送 12/12 字节
```

### 查看接收解析日志

```
I (12400) uart: 收到完整帧 20字节 (长度:0x14, 类型:0x85)
I (12400) uart: 0x3ffb1234   14 85 80 12 09 c4 04 e2  0a 00 03 20 ...
I (12401) uart: ═══ 指令应答 (0x85) ═══
I (12401) uart: 位置有效: 是, 工作模式: 0
I (12401) uart: 型号ID: 0x12, 电机总行程: 2500
I (12401) uart: 当前位置: 1250, 设置超时: 10秒
```

---

## ⚠️ 注意事项

1. **UART初始化**
   - 发送前确保UART已初始化：`uart_is_initialized()`

2. **MAC地址**
   - 需要先获取目标设备的MAC地址

3. **发送间隔**
   - 建议连续发送间隔 200-500ms

4. **参数范围**
   - 写入寄存器前检查参数是否在有效范围内

5. **错误处理**
   - 检查API函数返回值：`ESP_OK` 表示成功

---

## 📊 实现统计

| 项目 | 数量 | 状态 |
|------|------|------|
| 下行帧类型 | 4种 | ✅ 100% |
| 上行帧类型 | 4种 | ✅ 100% |
| 键值定义 | 8个 | ✅ 100% |
| 指令定义 | 9个 | ✅ 100% |
| 寄存器定义 | 16个 | ✅ 100% |
| API函数 | 4个 | ✅ 100% |
| 解析函数 | 4个 | ✅ 100% |
| 代码行数 | 671行 | ✅ 完成 |

---

## 🎓 文件结构

```
use_uart/
├── use_uart.h                    # API头文件（252行）
├── use_uart.c                    # 实现代码（671行）
├── CMakeLists.txt                # 编译配置
├── README.md                     # 本文档
├── 协议实现完整性检查.md        # 完整性检查报告
└── 读所有寄存器使用说明.md      # 专项说明
```

---

## 🐛 故障排除

### 1. 发送失败
```c
esp_err_t ret = uart_send_cmd_frame(device_mac, CMD_OPEN_DOOR);
if (ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "UART未初始化");
} else if (ret == ESP_ERR_INVALID_ARG) {
    ESP_LOGE(TAG, "MAC地址为空");
}
```

### 2. 没有收到应答
- 检查MAC地址是否正确
- 检查设备是否上电
- 检查UART接线（TX↔RX交叉连接）
- 查看错误统计：FIFO溢出、缓冲区满等

### 3. 数据解析错误
- 查看完整十六进制数据
- 检查帧长度和类型
- 确认设备返回的协议版本

---

## 📄 许可证

本模块为项目内部使用。

---

## 👥 维护者

ESP32-C5 门控项目组

---

## 📅 更新记录

- **v1.0** (2025) - 初始版本，完整实现协议
  - ✅ 所有下行数据封装
  - ✅ 所有上行数据解析
  - ✅ 完善的错误处理
  - ✅ 详细的日志输出

