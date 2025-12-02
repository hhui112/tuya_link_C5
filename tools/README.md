# 涂鸦配置文件生成工具使用说明

## 📝 工具说明

本工具用于读取涂鸦平台下载的设备凭证CSV文件，批量生成可烧录到ESP32-C5的配置bin文件。

---

## 🔧 工具特点

- ✅ **批量处理**：一次性处理多个设备的配置
- ✅ **直接读取CSV**：无需手动复制粘贴参数
- ✅ **C语言实现**：跨平台，执行速度快
- ✅ **GCC编译**：使用标准C编译器，无需Python环境
- ✅ **自动命名**：根据DeviceId自动生成文件名

---

## 📋 文件结构

```
tools/
├── generate_tuya_bin.c      # C源代码
├── build_and_run.bat         # Windows编译运行脚本
├── devices_example.csv       # CSV格式示例
└── README.md                 # 本说明文件
```

---

## 🛠️ 环境准备

### Windows系统

需要安装GCC编译器（MinGW）：

**方法1：使用WinLibs（推荐）**
1. 下载：https://winlibs.com/
2. 选择：UCRT runtime（推荐最新版本）
3. 解压到目录，如：`C:\mingw64`
4. 添加到环境变量：
   - 打开"系统属性" → "环境变量"
   - 在"Path"中添加：`C:\mingw64\bin`
5. 验证安装：打开CMD输入 `gcc --version`

**方法2：使用TDM-GCC**
1. 下载：https://jmeubank.github.io/tdm-gcc/
2. 运行安装程序，选择安装路径
3. 自动配置环境变量
4. 验证安装：打开CMD输入 `gcc --version`

### Linux系统

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install gcc

# CentOS/RHEL
sudo yum install gcc

# 验证安装
gcc --version
```

---

## 📥 CSV文件格式

从涂鸦平台下载的CSV文件格式：

```csv
"注册ID","ProductID","DeviceId","DeviceSecret","BindCode","备注","二维码链接"
"zdwGghNAJkRbXYdcUDDb","2esrxjypqu3jbysa","26020eed9491bbe94fmzgz","37JPY3dUmXGe1pM0","q5YJnvcz","","https://m.smart321.com/AY3UQeqm7rPNyrEr"
```

**字段说明：**
- **ProductID**：产品ID（最多31字符）
- **DeviceId**：设备ID（最多31字符）
- **DeviceSecret**：设备密钥（最多31字符）

其他字段（注册ID、BindCode、备注、二维码）不会被使用。

---

## 🚀 使用方法

### Windows快速使用

#### 方法1：拖放操作（最简单）

1. 双击 `build_and_run.bat` 进行编译
2. 将涂鸦下载的CSV文件拖放到 `build_and_run.bat` 上
3. 自动生成bin文件

#### 方法2：命令行操作

```batch
# 1. 编译程序
cd tools
build_and_run.bat

# 2. 处理CSV文件（生成到当前目录）
generate_tuya_bin.exe devices.csv

# 3. 处理CSV文件（指定输出目录）
generate_tuya_bin.exe devices.csv ./output

# 4. 完整路径示例
generate_tuya_bin.exe C:\Downloads\tuya_devices.csv D:\firmware
```

#### 方法3：手动编译运行

```batch
# 编译
gcc generate_tuya_bin.c -o generate_tuya_bin.exe -O2

# 运行
generate_tuya_bin.exe devices.csv
```

### Linux使用

```bash
# 1. 进入工具目录
cd tools

# 2. 编译
gcc generate_tuya_bin.c -o generate_tuya_bin -O2

# 3. 运行
./generate_tuya_bin devices.csv

# 4. 指定输出目录
./generate_tuya_bin devices.csv ./output
```

---

## 📤 输出结果

### 生成的文件

程序会为CSV中的每个设备生成一个独立的bin文件：

```
tuya_config_26020eed9491bbe94fmzgz.bin  (96字节)
tuya_config_26020eed9491bbe94fmzga.bin  (96字节)
tuya_config_26020eed9491bbe94fmzgb.bin  (96字节)
...
```

**文件命名规则：** `tuya_config_<DeviceId>.bin`

### 控制台输出示例

```
========================================
涂鸦配置文件生成工具
========================================

读取文件: devices.csv
输出目录: .

跳过标题行

✓ 生成: tuya_config_26020eed9491bbe94fmzgz.bin
  ProductID:     2esrxjypqu3jbysa
  DeviceID:      26020eed9491bbe94fmzgz
  DeviceSecret:  37JPY3dUmXGe1pM0
  文件大小:      96 字节

✓ 生成: tuya_config_26020eed9491bbe94fmzga.bin
  ProductID:     2esrxjypqu3jbysa
  DeviceID:      26020eed9491bbe94fmzga
  DeviceSecret:  37JPY3dUmXGe1pM1
  文件大小:      96 字节

========================================
处理完成！
总行数: 3
成功生成: 2 个文件
========================================

烧录方法：
  2MB Flash: esptool.py --chip esp32c5 --port COM3 write_flash 0x620000 tuya_config_xxx.bin
  4MB Flash: esptool.py --chip esp32c5 --port COM3 write_flash 0x3A0000 tuya_config_xxx.bin
```

---

## 🔥 烧录到ESP32

### 确定烧录地址

根据使用的Flash大小选择对应地址：

| Flash大小 | 烧录地址 | 分区表文件 |
|----------|---------|-----------|
| 2MB | `0x620000` | my_partitions.csv |
| 4MB | `0x3A0000` | my_partitions_4mb.csv |

### 烧录方法

#### 方法1：使用esptool.py

```bash
# 安装esptool（首次使用）
pip install esptool

# 2MB Flash烧录
esptool.py --chip esp32c5 --port COM3 write_flash 0x620000 tuya_config_26020eed9491bbe94fmzgz.bin

# 4MB Flash烧录
esptool.py --chip esp32c5 --port COM3 write_flash 0x3A0000 tuya_config_26020eed9491bbe94fmzgz.bin

# Linux系统（串口号通常是/dev/ttyUSB0）
esptool.py --chip esp32c5 --port /dev/ttyUSB0 write_flash 0x620000 tuya_config_xxx.bin
```

#### 方法2：使用Flash Download Tool

1. 打开 `flash_download_tool_3.9.9_R2.exe`
2. 选择芯片：ESP32-C5
3. 添加bin文件：
   - 点击 `...` 浏览选择生成的bin文件
   - 输入地址：`0x620000`（2MB）或 `0x3A0000`（4MB）
   - 勾选该文件
4. 设置COM口和波特率（通常921600）
5. 点击 START 开始烧录

---

## ✅ 验证烧录结果

### 1. 启用代码读取功能

代码已经修改好，无需额外操作（已在 `components/common/common.c` 中启用）。

### 2. 查看串口日志

烧录配置文件后重启设备，应该看到：

```
从 storage 分区读取涂鸦配置成功
PRODUCT_ID:2esrxjypqu3jbysa, DEVICE_ID:26020eed9491bbe94fmzgz
```

如果显示：
```
未找到 storage 分区，使用默认配置
```
或
```
storage 分区数据无效，使用默认配置
```

说明：
- 烧录地址不正确
- 分区表配置不匹配
- 设备Flash大小与分区表不符

---

## 🔍 常见问题

### 1. 编译错误："gcc: command not found"

**原因：** 未安装GCC或未添加到环境变量

**解决：**
- 检查是否已安装GCC：`gcc --version`
- 重新安装MinGW并配置环境变量
- 重启命令行窗口

### 2. CSV文件乱码或解析失败

**原因：** CSV文件编码不是UTF-8

**解决：**
- 用记事本打开CSV，另存为UTF-8编码
- 或用Excel打开后重新保存

### 3. 生成的bin文件大小不是96字节

**原因：** 编译器结构体对齐设置问题

**解决：**
- 检查编译命令是否正确
- 查看 `sizeof(tuya_config_t)` 的输出

### 4. 烧录后设备仍使用默认配置

**原因：** 
- 烧录地址错误
- Flash大小与分区表不匹配

**解决：**
- 确认使用的分区表文件（sdkconfig中查看）
- 确认Flash芯片实际大小
- 使用正确的烧录地址

### 5. DeviceId或ProductId过长

**原因：** 超过31字符限制

**解决：**
- 检查涂鸦平台数据是否正确
- 如需支持更长ID，修改 `tuya_config_t` 结构体中的数组大小

---

## 📊 二进制文件格式

**文件结构（96字节）：**

```
偏移量      大小      内容
0x00       32字节    product_id（ASCII字符串 + \0 + 填充0）
0x20       32字节    device_id（ASCII字符串 + \0 + 填充0）
0x40       32字节    device_secret（ASCII字符串 + \0 + 填充0）
```

**示例（十六进制）：**
```
0000: 32 65 73 72 78 6A 79 70 71 75 33 6A 62 79 73 61  2esrxjypqu3jbysa
0010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0020: 32 36 30 32 30 65 65 64 39 34 39 31 62 62 65 39  26020eed9491bbe9
0030: 34 66 6D 7A 67 7A 00 00 00 00 00 00 00 00 00 00  4fmzgz..........
0040: 33 37 4A 50 59 33 64 55 6D 58 47 65 31 70 4D 30  37JPY3dUmXGe1pM0
0050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

---

## 📦 批量生产流程

### 推荐工作流程

1. **从涂鸦平台批量下载设备凭证**
   - 导出CSV文件（包含所有设备）

2. **批量生成bin文件**
   ```batch
   generate_tuya_bin.exe tuya_devices.csv ./firmware
   ```

3. **建立设备与bin文件的对应关系**
   - 文件名包含DeviceId，便于识别
   - 可以打印标签贴在设备上

4. **烧录**
   - 方案A：使用esptool.py编写自动化脚本
   - 方案B：使用Flash Download Tool逐个烧录

### 自动化烧录脚本示例（Windows）

```batch
@echo off
REM 批量烧录脚本示例

set FLASH_ADDR=0x620000
set COM_PORT=COM3

for %%f in (tuya_config_*.bin) do (
    echo 烧录: %%f
    esptool.py --chip esp32c5 --port %COM_PORT% write_flash %FLASH_ADDR% %%f
    echo 完成，请更换下一个设备...
    pause
)
```

---

## 📞 技术支持

如遇到问题，请检查：

1. ✅ GCC是否正确安装
2. ✅ CSV文件格式是否正确
3. ✅ bin文件大小是否为96字节
4. ✅ 烧录地址是否正确
5. ✅ Flash大小与分区表是否匹配

---

**最后更新：** 2025-11-28  
**工具版本：** v1.0  
**兼容固件：** tuya_link_C5 v1.0.3+

