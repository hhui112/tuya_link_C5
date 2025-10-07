#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "use_uart.h"

static const char *TAG = "uart";
static QueueHandle_t uart2_queue;

#define BUF_SIZE (1024)


// 全局变量定义
uart_rx_buffer_t g_uart_rx_buffer;
static device_status_callback_t g_status_callback = NULL;



/**
 * @brief 发送数据（同步，立即发送）
 */
void uart_send_data(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) return;
    
    uart_write_bytes(UART_PORT_NUM, data, len);
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    ESP_LOGD(TAG, "UART发送: ");
    for (int i = 0; i < len; i++) {printf("%02X ", data[i]);}
}

/**
 * @brief 从UART读取数据（非阻塞）
 */
int uart_receive_data(uint8_t *buffer, size_t max_len, uint32_t timeout_ms)
{
    if (buffer == NULL || max_len == 0) return -1;
    
    // 调用ESP-IDF的底层uart函数
    int len = uart_read_bytes(UART_PORT_NUM, (void*)buffer, max_len, 
                             pdMS_TO_TICKS(timeout_ms));
    
    if (len > 0) {
        ESP_LOGD(TAG, "UART接收: %d bytes", len);
    }
    
    return len;
}

/**
 * @brief 清空接收缓冲区
 */
void uart_flush_rx_buffer(void)
{
    uart_flush_input(UART_PORT_NUM);
    ESP_LOGD(TAG, "UART接收缓冲区已清空");
}


bool uart_parse(const uint8_t *data, size_t len)
{
    // 数据安全检查
    if (data == NULL) {
        ESP_LOGE(TAG, "uart_parse: 数据指针为空");
        return false;
    }
    
    if (len < sizeof(device_status_frame_t)) {
        ESP_LOGE(TAG, "uart_parse: 数据长度不足，期望%d字节，实际%d字节", 
                 sizeof(device_status_frame_t), len);
        return false;
    }
    
    // 安全地拷贝数据到全局缓冲区
    memcpy(&g_uart_rx_buffer.frame_data, data, sizeof(device_status_frame_t));
    
    // 调用回调函数
    if (g_status_callback != NULL) {
        g_status_callback(&g_uart_rx_buffer.frame_data);
    }
    
    return true;
}


void uart_DataReceive_handler(uint8_t *p, uint16_t len)
{
    // 简化处理：检查帧头和长度
    if (len == sizeof(device_status_frame_t) && p[0] == 0x14) {
        uart_parse(p, len);
    }
}

void uart_DataReceive_handler_(uint8_t *p,uint16_t len)   // 旧的串口数据接收处理函数
{
    static uint32_t count = 0;
    uint8_t checkSum = 0,checkSum1 = 0;
    memcpy(g_uart_rx_buffer.raw_data+count,p,len);
	count += len;  
    //printf("count: %d len: %d g_Sync_RX.Syncdata.length =%x \r\n",count,len,g_Sync_RX.Syncdata.length);
    if(g_uart_rx_buffer.frame_data.length != 0x14) // 20
    {           
        count = 0;
    }
    if( (count != 0) && (count>=g_uart_rx_buffer.frame_data.length))
	{	
        count = 0;	
        uart_parse(g_uart_rx_buffer.raw_data, count);
	}
}

static void uart_DataReceive_task(void *arg)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,                    // 115200
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,                  // 无校验
        .stop_bits = UART_STOP_BITS_1,                  // 1位停止位
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,          // 无硬件流控制    
        .source_clk = UART_SCLK_DEFAULT,                // 默认时钟
    };
    uart_event_t event;

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE, BUF_SIZE, 20, &uart2_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, UART_TXD_PIN, UART_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    // Configure a temporary buffer for the incoming data
    uint8_t *dtmp = (uint8_t *) malloc(BUF_SIZE);
    if (dtmp == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for UART receive buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        //Waiting for UART event.
        if (xQueueReceive(uart2_queue, (void *)&event, (TickType_t)portMAX_DELAY)) 
        {
            // ESP_LOGI("UART", "UART event received: %d", event.type);
            memset(dtmp, 0, BUF_SIZE);
            //ESP_LOGI(TAG, "uart[%d] event:", ECHO_UART_PORT_NUM);
            switch (event.type)
            {
            //Event of UART receving data
            case UART_DATA:
               //  ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                uart_read_bytes(ECHO_UART_PORT_NUM, dtmp, event.size, portMAX_DELAY);
                uart_DataReceive_handler(dtmp, event.size);   // 一次性收完一帧
               // ESP_LOGI(TAG, "[DATA EVT]:");
                //uart_write_bytes(ECHO_UART_PORT_NUM, (const char*) dtmp, event.size);
                break;
            //Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                ESP_LOGI(TAG, "hw fifo overflow");
                // If fifo overflow happened, you should consider adding flow control for your application.
                // The ISR has already reset the rx FIFO,
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(ECHO_UART_PORT_NUM);
                xQueueReset(uart2_queue);
                break;
            //Event of UART ring buffer full
            case UART_BUFFER_FULL:
                ESP_LOGI(TAG, "ring buffer full");
                // If buffer full happened, you should consider increasing your buffer size
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(ECHO_UART_PORT_NUM);
                xQueueReset(uart2_queue);
                break;
            //Event of UART RX break detected
            case UART_BREAK:
                ESP_LOGI(TAG, "uart rx break");
                break;
            //Event of UART parity check error
            case UART_PARITY_ERR:
                ESP_LOGI(TAG, "uart parity error");
                uart_flush_input(ECHO_UART_PORT_NUM);  // 清空当前错误帧
                break;
            //Event of UART frame error
            case UART_FRAME_ERR:
                ESP_LOGI(TAG, "uart frame error");
                break;
            //UART_PATTERN_DET
            case UART_PATTERN_DET:
                ESP_LOGI(TAG, "uart pattern detected");
                break;
            //Others
            default:
                ESP_LOGI(TAG, "uart event type: %d", event.type);
                break;
            }
        }            
    }
    free(dtmp);
    dtmp = NULL;
}

/**
 * @brief 设置设备状态回调函数
 */
void set_device_status_callback(device_status_callback_t callback)
{
    g_status_callback = callback;
}



/**
 * @brief 启动UART接收任务
 */
void start_uart_receive_task(void)
{
    xTaskCreate(uart_DataReceive_task, "uart_receive", 2048, NULL, 5, NULL);
}

