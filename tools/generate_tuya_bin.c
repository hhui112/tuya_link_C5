/**
 * @file generate_tuya_bin.c
 * @brief 涂鸦配置文件生成工具 - 从CSV生成bin文件
 * 
 * 编译方法：
 *   Windows: gcc generate_tuya_bin.c -o generate_tuya_bin.exe
 *   Linux:   gcc generate_tuya_bin.c -o generate_tuya_bin
 * 
 * 使用方法：
 *   generate_tuya_bin devices.csv               # 生成多个bin文件
 *   generate_tuya_bin devices.csv output_dir    # 指定输出目录
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// 与ESP32中的结构体保持一致
typedef struct
{
    char product_id[32];      // 产品ID
    char device_id[32];       // 设备ID  
    char device_secret[32];   // 设备密钥
} tuya_config_t;              // 总大小：96字节

#define MAX_LINE_LENGTH 1024
#define MAX_FIELD_LENGTH 256

/**
 * @brief 去除字符串两端的引号和空格
 */
void trim_quotes(char *str) {
    if (str == NULL || strlen(str) == 0) return;
    
    // 去除前导空格
    while (*str == ' ' || *str == '\t') str++;
    
    // 去除引号
    if (*str == '"') str++;
    
    // 去除尾部引号和空格
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == '"' || str[len-1] == ' ' || 
                       str[len-1] == '\t' || str[len-1] == '\r' || 
                       str[len-1] == '\n')) {
        str[len-1] = '\0';
        len--;
    }
}

/**
 * @brief 解析CSV的一行，提取被引号包围的字段
 * @param line CSV行
 * @param fields 输出的字段数组
 * @param max_fields 最大字段数
 * @return 实际解析的字段数
 */
int parse_csv_line(char *line, char fields[][MAX_FIELD_LENGTH], int max_fields) {
    int field_count = 0;
    char *p = line;
    char *field_start;
    bool in_quotes = false;
    int field_pos = 0;
    
    while (*p && field_count < max_fields) {
        if (*p == '"') {
            in_quotes = !in_quotes;
            p++;
            continue;
        }
        
        if (*p == ',' && !in_quotes) {
            // 字段结束
            fields[field_count][field_pos] = '\0';
            trim_quotes(fields[field_count]);
            field_count++;
            field_pos = 0;
            p++;
            continue;
        }
        
        fields[field_count][field_pos++] = *p;
        if (field_pos >= MAX_FIELD_LENGTH - 1) {
            fprintf(stderr, "警告: 字段过长被截断\n");
            field_pos = MAX_FIELD_LENGTH - 1;
        }
        p++;
    }
    
    // 处理最后一个字段
    if (field_pos > 0 || field_count > 0) {
        fields[field_count][field_pos] = '\0';
        trim_quotes(fields[field_count]);
        field_count++;
    }
    
    return field_count;
}

/**
 * @brief 生成涂鸦配置bin文件
 */
bool generate_bin_file(const char *product_id, const char *device_id, 
                       const char *device_secret, const char *filename) {
    tuya_config_t config;
    
    // 检查参数长度
    if (strlen(product_id) >= sizeof(config.product_id)) {
        fprintf(stderr, "错误: product_id 过长 (最多 %zu 字符): %s\n", 
                sizeof(config.product_id) - 1, product_id);
        return false;
    }
    if (strlen(device_id) >= sizeof(config.device_id)) {
        fprintf(stderr, "错误: device_id 过长 (最多 %zu 字符): %s\n", 
                sizeof(config.device_id) - 1, device_id);
        return false;
    }
    if (strlen(device_secret) >= sizeof(config.device_secret)) {
        fprintf(stderr, "错误: device_secret 过长 (最多 %zu 字符): %s\n", 
                sizeof(config.device_secret) - 1, device_secret);
        return false;
    }
    
    // 初始化为0
    memset(&config, 0, sizeof(tuya_config_t));
    
    // 复制数据
    strncpy(config.product_id, product_id, sizeof(config.product_id) - 1);
    strncpy(config.device_id, device_id, sizeof(config.device_id) - 1);
    strncpy(config.device_secret, device_secret, sizeof(config.device_secret) - 1);
    
    // 写入文件
    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        fprintf(stderr, "错误: 无法创建文件 %s\n", filename);
        return false;
    }
    
    size_t written = fwrite(&config, 1, sizeof(tuya_config_t), fp);
    fclose(fp);
    
    if (written != sizeof(tuya_config_t)) {
        fprintf(stderr, "错误: 写入文件失败\n");
        return false;
    }
    
    printf("✓ 生成: %s\n", filename);
    printf("  ProductID:     %s\n", product_id);
    printf("  DeviceID:      %s\n", device_id);
    printf("  DeviceSecret:  %s\n", device_secret);
    printf("  文件大小:      %zu 字节\n\n", sizeof(tuya_config_t));
    
    return true;
}

/**
 * @brief 处理CSV文件
 */
int process_csv_file(const char *csv_filename, const char *output_dir) {
    FILE *fp = fopen(csv_filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "错误: 无法打开文件 %s\n", csv_filename);
        return -1;
    }
    
    char line[MAX_LINE_LENGTH];
    char fields[10][MAX_FIELD_LENGTH];
    int line_number = 0;
    int success_count = 0;
    bool is_first_line = true;
    
    printf("========================================\n");
    printf("涂鸦配置文件生成工具\n");
    printf("========================================\n\n");
    printf("读取文件: %s\n", csv_filename);
    printf("输出目录: %s\n\n", output_dir);
    
    while (fgets(line, sizeof(line), fp)) {
        line_number++;
        
        // 跳过空行
        if (strlen(line) <= 1) continue;
        
        // 解析CSV行
        int field_count = parse_csv_line(line, fields, 10);
        
        if (field_count < 4) {
            if (!is_first_line) {
                fprintf(stderr, "警告: 第 %d 行字段不足，跳过\n", line_number);
            }
            is_first_line = false;
            continue;
        }
        
        // 跳过标题行（检测中文或英文标题）
        if (is_first_line) {
            is_first_line = false;
            if (strstr(fields[0], "ID") || strstr(fields[0], "注册")) {
                printf("跳过标题行\n\n");
                continue;
            }
        }
        
        // CSV格式：
        // 0: 注册ID
        // 1: ProductID
        // 2: DeviceId
        // 3: DeviceSecret
        // 4: BindCode
        // 5: 备注
        // 6: 二维码链接
        
        const char *product_id = fields[1];
        const char *device_id = fields[2];
        const char *device_secret = fields[3];
        
        // 检查必要字段是否为空
        if (strlen(product_id) == 0 || strlen(device_id) == 0 || strlen(device_secret) == 0) {
            fprintf(stderr, "警告: 第 %d 行数据不完整，跳过\n", line_number);
            continue;
        }
        
        // 生成输出文件名
        char output_filename[512];
        if (output_dir && strlen(output_dir) > 0) {
            snprintf(output_filename, sizeof(output_filename), 
                    "%s/tuya_config_%s.bin", output_dir, device_id);
        } else {
            snprintf(output_filename, sizeof(output_filename), 
                    "tuya_config_%s.bin", device_id);
        }
        
        // 生成bin文件
        if (generate_bin_file(product_id, device_id, device_secret, output_filename)) {
            success_count++;
        }
    }
    
    fclose(fp);
    
    printf("========================================\n");
    printf("处理完成！\n");
    printf("总行数: %d\n", line_number);
    printf("成功生成: %d 个文件\n", success_count);
    printf("========================================\n\n");
    
    if (success_count > 0) {
        printf("烧录方法：\n");
        printf("  2MB Flash: esptool.py --chip esp32c5 --port COM3 write_flash 0x620000 tuya_config_xxx.bin\n");
        printf("  4MB Flash: esptool.py --chip esp32c5 --port COM3 write_flash 0x3A0000 tuya_config_xxx.bin\n");
    }
    
    return success_count;
}

/**
 * @brief 打印使用说明
 */
void print_usage(const char *program_name) {
    printf("========================================\n");
    printf("涂鸦配置文件生成工具\n");
    printf("========================================\n\n");
    printf("使用方法:\n");
    printf("  %s <csv文件> [输出目录]\n\n", program_name);
    printf("参数说明:\n");
    printf("  csv文件    : 涂鸦平台下载的设备凭证CSV文件\n");
    printf("  输出目录   : bin文件输出目录 (可选，默认当前目录)\n\n");
    printf("CSV文件格式:\n");
    printf("  \"注册ID\",\"ProductID\",\"DeviceId\",\"DeviceSecret\",\"BindCode\",\"备注\",\"二维码链接\"\n");
    printf("  \"xxx\",\"2esrxjypqu3jbysa\",\"26020eed9491bbe94fmzgz\",\"37JPY3dUmXGe1pM0\",\"xxx\",\"\",\"https://...\"\n\n");
    printf("示例:\n");
    printf("  %s devices.csv\n", program_name);
    printf("  %s devices.csv ./output\n", program_name);
    printf("  %s devices.csv C:\\\\firmware\n\n", program_name);
    printf("输出:\n");
    printf("  生成文件: tuya_config_<DeviceId>.bin (96字节)\n");
    printf("  每个设备生成一个独立的bin文件\n\n");
    printf("编译方法:\n");
    printf("  Windows: gcc generate_tuya_bin.c -o generate_tuya_bin.exe\n");
    printf("  Linux:   gcc generate_tuya_bin.c -o generate_tuya_bin\n");
    printf("========================================\n");
}

int main(int argc, char *argv[]) {
    // 设置UTF-8支持（Windows）
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif
    
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *csv_file = argv[1];
    const char *output_dir = (argc >= 3) ? argv[2] : ".";
    
    int result = process_csv_file(csv_file, output_dir);
    
    if (result < 0) {
        return 1;
    }
    
    return 0;
}

