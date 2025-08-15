@echo off
chcp 65001
cls
echo ================================================================
echo           ESP32-C5 烧录问题自动修复工具
echo ================================================================
echo.

echo 🔍 步骤1: 检查串口状态...
echo ----------------------------------------------------------------
powershell -Command "Get-WmiObject -Class Win32_SerialPort | Select-Object DeviceID, Name, Status" 2>nul
if %errorlevel% neq 0 (
    echo ❌ 无法检查串口状态，可能权限不足
) else (
    echo ✅ 串口状态检查完成
)
echo.

echo 🛑 步骤2: 关闭可能占用串口的程序...
echo ----------------------------------------------------------------
echo 正在尝试关闭可能占用串口的进程...
taskkill /f /im "monitor.exe" 2>nul
taskkill /f /im "putty.exe" 2>nul
taskkill /f /im "arduino.exe" 2>nul
taskkill /f /im "idf_monitor.exe" 2>nul
taskkill /f /im "minicom.exe" 2>nul
echo ✅ 进程清理完成
echo.

echo ⏳ 步骤3: 等待串口释放...
echo ----------------------------------------------------------------
timeout /t 3 /nobreak >nul
echo ✅ 等待完成
echo.

echo 🔧 步骤4: 尝试不同的烧录方式...
echo ================================================================
echo.

echo 📋 请按照以下步骤操作ESP32-C5开发板：
echo ----------------------------------------------------------------
echo  1️⃣  按住 BOOT 按钮（不要松开）
echo  2️⃣  短按一下 RESET 按钮，然后立即松开
echo  3️⃣  继续按住 BOOT 按钮 2-3 秒
echo  4️⃣  松开 BOOT 按钮
echo  5️⃣  按任意键继续烧录...
echo ----------------------------------------------------------------
pause >nul
echo.

echo 🚀 开始烧录 - 方法1: 低波特率烧录
echo ----------------------------------------------------------------
idf.py -p COM4 -b 115200 flash
if %errorlevel% equ 0 (
    echo ✅ 烧录成功！
    goto :success
)
echo ❌ 方法1失败，尝试方法2...
echo.

echo 🚀 开始烧录 - 方法2: 使用no-stub选项
echo ----------------------------------------------------------------
echo 📋 请重新进入下载模式（重复上述按钮操作），然后按任意键继续...
pause >nul
idf.py -p COM4 --no-stub flash
if %errorlevel% equ 0 (
    echo ✅ 烧录成功！
    goto :success
)
echo ❌ 方法2失败，尝试方法3...
echo.

echo 🚀 开始烧录 - 方法3: 指定芯片类型
echo ----------------------------------------------------------------
echo 📋 请重新进入下载模式（重复上述按钮操作），然后按任意键继续...
pause >nul
idf.py -p COM4 --chip esp32c5 -b 115200 flash
if %errorlevel% equ 0 (
    echo ✅ 烧录成功！
    goto :success
)
echo ❌ 方法3失败，尝试方法4...
echo.

echo 🚀 开始烧录 - 方法4: 使用esptool直接烧录
echo ----------------------------------------------------------------
echo 📋 请重新进入下载模式（重复上述按钮操作），然后按任意键继续...
pause >nul
if exist "build\bootloader\bootloader.bin" (
    python -m esptool --chip esp32c5 --port COM4 --baud 115200 write_flash 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\wifi_station.bin
    if %errorlevel% equ 0 (
        echo ✅ 烧录成功！
        goto :success
    )
)
echo ❌ 所有自动方法都失败了
echo.

echo 💡 手动解决建议：
echo ================================================================
echo  🔧 1. 检查USB线缆是否为数据线（不是充电线）
echo  🔧 2. 尝试更换USB端口
echo  🔧 3. 检查设备管理器中的驱动程序
echo  🔧 4. 尝试使用图形化烧录工具
echo  🔧 5. 联系开发板供应商或查看硬件文档
echo.
echo 📖 详细故障排除指南：ESP32C5_串口问题解决指南.md
goto :end

:success
echo.
echo 🎉 烧录成功完成！
echo ================================================================
echo  ✅ 固件已成功烧录到ESP32-C5
echo  🔍 您可以使用以下命令查看输出：
echo     idf.py -p COM4 monitor
echo.

:end
echo 按任意键退出...
pause >nul 