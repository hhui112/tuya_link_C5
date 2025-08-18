#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// IOT设备状态结构体
typedef struct {
    char device_status[32];  // 设备状态，如"open", "close"
    int32_t test_value;      // 测试数值
} iot_device_state_t;

// 全局状态变量声明
extern iot_device_state_t g_iot_state;

/**
 * @brief 初始化全局状态
 */
void common_init(void);

/**
 * @brief 获取当前IOT设备状态
 * 
 * @param device_status 输出缓冲区，存储设备状态字符串
 * @param status_size 缓冲区大小
 * @param test_value 输出参数，存储测试数值
 */
void get_current_iot_state(char* device_status, size_t status_size, int32_t* test_value);

/**
 * @brief 更新设备状态
 * 
 * @param device_status 新的设备状态字符串
 */
void set_device_status(const char* device_status);

/**
 * @brief 更新测试数值
 * 
 * @param test_value 新的测试数值
 */
void set_test_value(int32_t test_value);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */
