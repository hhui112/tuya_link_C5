@echo off
REM 编译并运行涂鸦配置文件生成工具

setlocal EnableDelayedExpansion

echo ========================================
echo 涂鸦配置文件生成工具 - 编译脚本
echo ========================================
echo.

REM 检查是否安装了GCC
where gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo 错误: 未找到 GCC 编译器！
    echo.
    echo 请安装 MinGW-w64 或 TDM-GCC:
    echo   下载地址: https://winlibs.com/ 或 https://jmeubank.github.io/tdm-gcc/
    echo.
    pause
    exit /b 1
)

REM 切换到脚本所在目录
cd /d "%~dp0"

REM 编译程序
echo [1/2] 编译 generate_tuya_bin.c ...
gcc generate_tuya_bin.c -o generate_tuya_bin.exe -O2

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ❌ 编译失败！
    pause
    exit /b 1
)

echo ✓ 编译成功！
echo.

REM 检查是否提供了CSV文件参数
if "%1"=="" (
    echo [2/2] 显示使用说明...
    echo.
    generate_tuya_bin.exe
    echo.
    echo 提示: 将涂鸦CSV文件拖放到此批处理文件上即可直接处理
    pause
    exit /b 0
)

REM 运行程序处理CSV文件
echo [2/2] 处理CSV文件: %1
echo.
generate_tuya_bin.exe "%~1" "%~2"

echo.
pause

