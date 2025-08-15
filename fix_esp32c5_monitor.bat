@echo off
chcp 65001
cls
echo ================================================================
echo           ESP32-C5 Monitor 问题诊断工具
echo ================================================================
echo.

echo 📋 当前状态检查：
echo ----------------------------------------------------------------
echo  ✅ 固件已成功烧录
echo  ❓ Monitor 无输出 - 开始诊断...
echo.

echo 🔄 步骤1: 复位ESP32-C5
echo ----------------------------------------------------------------
echo 📌 重要：ESP32-C5可能仍处于下载模式
echo.
echo 请按照以下操作：
echo  1️⃣  按一下开发板上的 RESET 按钮
echo  2️⃣  观察开发板指示灯变化
echo  3️⃣  等待 3-5 秒让系统完全启动
echo  4️⃣  按任意键继续诊断...
echo ----------------------------------------------------------------
pause >nul
echo ✅ 复位操作完成
echo.

echo 🔍 步骤2: 检查串口状态
echo ----------------------------------------------------------------
powershell -Command "Get-WmiObject -Class Win32_SerialPort | Where-Object {$_.DeviceID -eq 'COM4'} | Select-Object DeviceID, Name, Status" 2>nul
if %errorlevel% neq 0 (
    echo ❌ 无法检查COM4状态
) else (
    echo ✅ COM4 状态检查完成
)
echo.

echo 🛑 步骤3: 清理串口占用
echo ----------------------------------------------------------------
taskkill /f /im "putty.exe" 2>nul
taskkill /f /im "arduino.exe" 2>nul
taskkill /f /im "monitor.exe" 2>nul
taskkill /f /im "minicom.exe" 2>nul
echo ✅ 串口清理完成
echo.

echo 📡 步骤4: 尝试不同波特率的Monitor
echo ================================================================
echo.

echo 🚀 方法1: 默认波特率 115200
echo ----------------------------------------------------------------
echo 正在启动 monitor (115200)，观察是否有输出...
echo 如果有输出，按 Ctrl+] 退出monitor
echo 如果10秒内无输出，将自动继续下一个方法
echo.
timeout /t 2 /nobreak >nul
start /B /wait cmd /c "echo 开始监控... && timeout /t 10 /nobreak >nul && taskkill /f /im python.exe 2>nul" 
idf.py -p COM4 -b 115200 monitor

echo.
echo ❓ 方法1是否有输出？ (y/n)
set /p "response1=请输入 y 或 n: "
if /i "%response1%"=="y" (
    echo ✅ 找到输出！问题已解决
    goto :success
)

echo 🚀 方法2: 高波特率 460800
echo ----------------------------------------------------------------
echo 正在启动 monitor (460800)...
timeout /t 2 /nobreak >nul
idf.py -p COM4 -b 460800 monitor

echo.
echo ❓ 方法2是否有输出？ (y/n)
set /p "response2=请输入 y 或 n: "
if /i "%response2%"=="y" (
    echo ✅ 找到输出！问题已解决
    goto :success
)

echo 🚀 方法3: Bootloader波特率 74880
echo ----------------------------------------------------------------
echo 正在启动 monitor (74880) - ESP32 bootloader默认波特率...
timeout /t 2 /nobreak >nul
idf.py -p COM4 -b 74880 monitor

echo.
echo ❓ 方法3是否有输出？ (y/n)
set /p "response3=请输入 y 或 n: "
if /i "%response3%"=="y" (
    echo ✅ 找到输出！问题已解决
    goto :success
)

echo 🚀 方法4: 使用第三方工具验证
echo ----------------------------------------------------------------
echo 如果您安装了PuTTY，可以尝试：
echo  - 打开PuTTY
echo  - 连接类型：Serial
echo  - 串口：COM4
echo  - 波特率：115200
echo  - 数据位：8，停止位：1，校验：无
echo.
echo 按任意键继续检查其他可能原因...
pause >nul

echo 🔧 步骤5: 高级诊断
echo ================================================================
echo.

echo 🔍 检查项目配置
echo ----------------------------------------------------------------
echo 1. 检查目标芯片配置：
idf.py show-target 2>nul
echo.
echo 2. 检查日志级别配置
echo   建议运行：idf.py menuconfig
echo   导航到：Component config -> Log output -> Default log verbosity
echo   设置为：Info 或 Debug
echo.

echo 💡 可能的问题和解决方案：
echo ================================================================
echo.
echo 🔧 问题1: 应用程序启动失败
echo    解决：检查代码是否有编译错误或运行时崩溃
echo.
echo 🔧 问题2: 日志级别过低
echo    解决：修改 menuconfig 中的日志级别设置
echo.
echo 🔧 问题3: ESP32-C5仍在下载模式
echo    解决：确保按过 RESET 按钮，多试几次
echo.
echo 🔧 问题4: 硬件问题
echo    解决：检查USB线缆、驱动程序、开发板电源
echo.
echo 🔧 问题5: 代码问题
echo    解决：添加更多调试输出，检查main函数是否正常执行
echo.

echo 📝 建议的下一步：
echo ================================================================
echo  1️⃣  重新编译并烧录固件：idf.py build flash
echo  2️⃣  检查代码中是否有printf输出
echo  3️⃣  使用其他串口工具验证（PuTTY等）
echo  4️⃣  检查开发板硬件文档和原理图
echo  5️⃣  尝试简单的"Hello World"程序
echo.
goto :end

:success
echo.
echo 🎉 Monitor问题已解决！
echo ================================================================
echo  ✅ ESP32-C5 串口输出正常
echo  📝 记住使用正确的波特率
echo  🔍 如果输出不完整，检查日志级别设置
echo.

:end
echo 📖 详细故障排除指南：ESP32C5_Monitor问题解决指南.md
echo 按任意键退出...
pause >nul 