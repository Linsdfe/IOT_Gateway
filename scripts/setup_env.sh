#!/bin/bash

set -e

echo "========================================"
echo "  i.MX6ULL IoT Gateway 开发环境配置脚本"
echo "========================================"

echo ""
echo "[1/6] 配置交叉编译器环境变量..."
TOOLCHAIN_PATH="/opt/toolchains/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf"
if [ -d "$TOOLCHAIN_PATH" ]; then
    grep -q "gcc-linaro-7.5.0" ~/.bashrc || echo "export PATH=${TOOLCHAIN_PATH}/bin:\$PATH" >> ~/.bashrc
    export PATH=${TOOLCHAIN_PATH}/bin:$PATH
    echo "  ✓ Linaro交叉编译器已配置"
    arm-linux-gnueabihf-gcc --version | head -1
else
    echo "  ✗ Linaro交叉编译器未安装，请先安装"
fi

echo ""
echo "[2/6] 配置TFTP服务..."
if systemctl is-active --quiet tftpd-hpa; then
    echo "  ✓ TFTP服务已运行 (/tftpboot)"
else
    echo "  ✗ TFTP服务未运行"
fi

echo ""
echo "[3/6] 配置NFS服务..."
if systemctl is-active --quiet nfs-kernel-server; then
    echo "  ✓ NFS服务已运行 (/nfs/rootfs)"
else
    echo "  ✗ NFS服务未运行"
fi

echo ""
echo "[4/6] 配置SSH服务..."
if systemctl is-active --quiet ssh; then
    echo "  ✓ SSH服务已运行"
else
    echo "  ✗ SSH服务未运行"
fi

echo ""
echo "[5/6] 检查串口权限..."
if groups | grep -q dialout; then
    echo "  ✓ 当前用户已在dialout组中（可访问串口设备）"
else
    echo "  ! 当前用户不在dialout组，执行: sudo usermod -aG dialout \$USER"
fi

echo ""
echo "[6/6] 检查USB转串口设备..."
if ls /dev/ttyUSB* >/dev/null 2>&1; then
    echo "  ✓ 检测到USB转串口设备:"
    ls -la /dev/ttyUSB*
else
    echo "  ! 未检测到USB转串口设备（请连接开发板后重试）"
fi

echo ""
echo "========================================"
echo "  环境配置检查完成"
echo "========================================"
echo ""
echo "常用命令："
echo "  串口连接: sudo picocom -b 115200 /dev/ttyUSB0"
echo "  TFTP目录: /tftpboot"
echo "  NFS目录:  /nfs/rootfs"
echo "  交叉编译: arm-linux-gnueabihf-gcc"
echo ""
