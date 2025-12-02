#!/bin/bash
# 编译并运行涂鸦配置文件生成工具 (Linux/Mac)

echo "========================================"
echo "涂鸦配置文件生成工具 - 编译脚本"
echo "========================================"
echo ""

# 检查是否安装了GCC
if ! command -v gcc &> /dev/null; then
    echo "错误: 未找到 GCC 编译器！"
    echo ""
    echo "请安装 GCC:"
    echo "  Ubuntu/Debian: sudo apt install gcc"
    echo "  CentOS/RHEL:   sudo yum install gcc"
    echo "  macOS:         xcode-select --install"
    echo ""
    exit 1
fi

# 切换到脚本所在目录
cd "$(dirname "$0")"

# 编译程序
echo "[1/2] 编译 generate_tuya_bin.c ..."
gcc generate_tuya_bin.c -o generate_tuya_bin -O2

if [ $? -ne 0 ]; then
    echo ""
    echo "❌ 编译失败！"
    exit 1
fi

echo "✓ 编译成功！"
echo ""

# 添加执行权限
chmod +x generate_tuya_bin

# 检查是否提供了CSV文件参数
if [ -z "$1" ]; then
    echo "[2/2] 显示使用说明..."
    echo ""
    ./generate_tuya_bin
    echo ""
    echo "提示: 使用方法: ./build_and_run.sh devices.csv [输出目录]"
    exit 0
fi

# 运行程序处理CSV文件
echo "[2/2] 处理CSV文件: $1"
echo ""
./generate_tuya_bin "$1" "$2"

echo ""

