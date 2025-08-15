# use_wifi 组件

## 概述

`use_wifi` 是一个ESP-IDF组件，提供了简单易用的WiFi连接和涂鸦IoT平台MQTT通信功能。该组件封装了WiFi连接管理、MQTT客户端配置和消息处理等复杂操作，让您可以专注于业务逻辑的开发。

## 功能特性

- ✅ WiFi自动连接和重连机制
- ✅ 涂鸦IoT平台MQTT连接
- ✅ 状态回调机制
- ✅ 消息接收回调
- ✅ 简单的数据发布API
- ✅ 心跳和在线状态管理
- ✅ 错误处理和状态监控

## 目录结构

```
components/use_wifi/
├── include/
│   └── use_wifi.h          # 头文件，包含所有公共API
├── use_wifi.c              # 实现文件
├── CMakeLists.txt          # 组件构建配置
└── README.md              # 本文档
```

## 快速开始

### 1. 包含头文件

```c
#include "use_wifi.h"
```

### 2. 配置WiFi和MQTT参数

```c
// WiFi配置
wifi_config_info_t wifi_config = {
    .ssid = "your_wifi_ssid",
    .password = "your_wifi_password",
    .max_retry = 5
};

// 涂鸦MQTT配置
tuya_mqtt_config_t mqtt_config = {
    .host = "m1.tuyacn.com",
    .port = 1883,
    .device_id = "your_device_id",
    .device_secret = "your_device_secret",
    .product_id = "your_product_id"
};
```

### 3. 设置回调函数（可选）

```c
// MQTT消息接收回调
void mqtt_message_callback(const char* topic, int topic_len, const char* data, int data_len) {
    printf("收到消息: %.*s\n", data_len, data);
}

// WiFi状态变化回调
void wifi_status_callback(wifi_status_t status) {
    printf("WiFi状态: %d\n", status);
}

// MQTT状态变化回调
void mqtt_status_callback(mqtt_status_t status) {
    printf("MQTT状态: %d\n", status);
}

// 设置回调函数
use_mqtt_set_message_callback(mqtt_message_callback);
use_wifi_set_status_callback(wifi_status_callback);
use_mqtt_set_status_callback(mqtt_status_callback);
```

### 4. 初始化和启动

```c
// 初始化组件
ESP_ERROR_CHECK(use_wifi_init(&wifi_config, &mqtt_config));

// 开始连接
ESP_ERROR_CHECK(use_wifi_start());

// 等待连接成功
esp_err_t result = use_wifi_wait_connected(0); // 0表示永久等待
if (result == ESP_OK) {
    printf("连接成功！\n");
}
```

### 5. 发送数据

```c
// 发送自定义数据
char data[] = "{\"properties\":{\"temperature\":25.6}}";
tuya_mqtt_publish_data(data);

// 发送心跳
tuya_mqtt_send_heartbeat();

// 报告在线状态
tuya_mqtt_report_online(true);
```

## API 参考

### 数据类型

#### wifi_status_t
WiFi连接状态枚举：
- `WIFI_STATUS_DISCONNECTED`: 断开连接
- `WIFI_STATUS_CONNECTING`: 正在连接
- `WIFI_STATUS_CONNECTED`: 已连接
- `WIFI_STATUS_FAILED`: 连接失败

#### mqtt_status_t
MQTT连接状态枚举：
- `MQTT_STATUS_DISCONNECTED`: 断开连接
- `MQTT_STATUS_CONNECTING`: 正在连接
- `MQTT_STATUS_CONNECTED`: 已连接
- `MQTT_STATUS_FAILED`: 连接失败

#### wifi_config_info_t
WiFi配置结构体：
```c
typedef struct {
    char ssid[32];          // WiFi SSID
    char password[64];      // WiFi密码
    int max_retry;          // 最大重试次数
} wifi_config_info_t;
```

#### tuya_mqtt_config_t
涂鸦MQTT配置结构体：
```c
typedef struct {
    char host[64];          // MQTT服务器地址
    int port;               // MQTT端口
    char device_id[64];     // 设备ID
    char device_secret[128]; // 设备密钥
    char product_id[32];    // 产品ID
} tuya_mqtt_config_t;
```

### 主要函数

#### 初始化和控制
- `esp_err_t use_wifi_init(const wifi_config_info_t* wifi_config, const tuya_mqtt_config_t* mqtt_config)`
- `esp_err_t use_wifi_start(void)`
- `esp_err_t use_wifi_stop(void)`
- `esp_err_t use_wifi_wait_connected(uint32_t timeout_ms)`

#### 状态查询
- `wifi_status_t use_wifi_get_status(void)`
- `mqtt_status_t use_mqtt_get_status(void)`

#### 数据发送
- `esp_err_t tuya_mqtt_publish_data(const char* data)`
- `esp_err_t tuya_mqtt_report_online(bool online)`
- `esp_err_t tuya_mqtt_send_heartbeat(void)`

#### 回调设置
- `void use_mqtt_set_message_callback(mqtt_message_callback_t callback)`
- `void use_wifi_set_status_callback(wifi_status_callback_t callback)`
- `void use_mqtt_set_status_callback(mqtt_status_callback_t callback)`

## 涂鸦IoT平台集成

### MQTT主题

组件自动处理以下MQTT主题：

**数据上报主题**：
```
tylink/{device_id}/thing/property/report
```

**命令接收主题**：
```
tylink/{device_id}/thing/property/set
```

### 数据格式

**上报数据格式**：
```json
{
  "properties": {
    "temperature": 25.6,
    "humidity": 60.2,
    "online": true,
    "timestamp": 1234567890
  }
}
```

**接收命令格式**：
```json
{
  "properties": {
    "switch": true,
    "brightness": 80
  }
}
```

## 依赖组件

该组件依赖以下ESP-IDF组件：
- `esp_wifi`
- `nvs_flash`
- `mqtt`
- `lwip`
- `esp_netif`
- `esp_event`
- `esp_system`
- `freertos`

## 使用注意事项

1. **初始化顺序**：必须先调用 `use_wifi_init()` 再调用 `use_wifi_start()`
2. **NVS初始化**：使用前需要先初始化NVS flash
3. **回调函数**：回调函数在WiFi任务上下文中执行，避免长时间阻塞操作
4. **内存管理**：组件内部管理所有动态内存，无需手动释放
5. **线程安全**：API函数是线程安全的，可以在不同任务中调用

## 示例代码

完整的使用示例请参考 `main/main.c` 文件。

## 故障排除

### 常见问题

1. **WiFi连接失败**
   - 检查SSID和密码是否正确
   - 确认WiFi信号强度
   - 检查WiFi认证模式

2. **MQTT连接失败**
   - 验证设备ID和密钥
   - 检查网络连接
   - 确认服务器地址和端口

3. **编译错误**
   - 确保所有依赖组件已正确配置
   - 检查ESP-IDF版本兼容性

### 调试日志

组件使用ESP-IDF日志系统，可以通过以下方式启用详细日志：

```c
esp_log_level_set("use_wifi", ESP_LOG_DEBUG);
esp_log_level_set("MQTT_TUYA", ESP_LOG_DEBUG);
```

## 版本历史

- v1.0.0: 初始版本，支持基本的WiFi和MQTT功能 