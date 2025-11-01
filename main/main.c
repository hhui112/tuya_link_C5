/* 
* @Author: FZH
* @Date: 2025-08-21 16:09:32
* @LastEditors: FZH
* @LastEditTime: 2025-08-21 16:09:32
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "use_ble_server.h"
#include "common.h"
#include "app_main.h"

static const char *TAG = "main";

void app_main(void)
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化任务看门狗（在创建任务之前）
    ESP_ERROR_CHECK(watchdog_init());

    device_init();
    
    // 初始化并启动BLE服务器
    ESP_ERROR_CHECK(initialize_ble_server());
    
    // 启动解耦的系统服务
    ESP_ERROR_CHECK(system_services_start());

    // 启动APP主业务任务
    ESP_ERROR_CHECK(app_main_start());    
    // app_main()函数退出，main任务结束，释放栈空间
    // 所有业务逻辑由独立的任务（msg_task, iot_task, uart_receive等）处理
}
