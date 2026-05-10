#!/bin/bash

I2C_BUS=1
SHT30_ADDR=0x44
BH1750_ADDR=0x23

echo "========================================"
echo "  传感器检测与验证脚本"
echo "========================================"

echo ""
echo "[1/4] 检查I2C总线..."
if ls /dev/i2c-${I2C_BUS} >/dev/null 2>&1; then
    echo "  ✓ I2C总线 /dev/i2c-${I2C_BUS} 存在"
else
    echo "  ✗ I2C总线 /dev/i2c-${I2C_BUS} 不存在"
    echo "    请检查设备树插件是否已启用（编辑 /boot/uEnv.txt）"
    exit 1
fi

echo ""
echo "[2/4] 扫描I2C设备..."
i2cdetect -y ${I2C_BUS}

echo ""
echo "[3/4] 检查SHT30温湿度传感器 (0x${SHT30_ADDR})..."
if i2cdetect -y ${I2C_BUS} | grep -q "44"; then
    echo "  ✓ SHT30传感器已检测到 (地址: 0x44)"

    if [ -e /dev/sht30 ]; then
        echo "  ✓ 设备节点 /dev/sht30 存在"
        echo "  读取温湿度数据:"
        cat /dev/sht30
    else
        echo "  ✗ 设备节点 /dev/sht30 不存在，尝试使用i2c-tools直接读取..."
        i2cset -y ${I2C_BUS} ${SHT30_ADDR} 0x2C 0x06
        sleep 0.1
        i2cget -y ${I2C_BUS} ${SHT30_ADDR} 0x00 w
    fi
else
    echo "  ✗ SHT30传感器未检测到，请检查接线"
fi

echo ""
echo "[4/4] 检查BH1750光照传感器 (0x${BH1750_ADDR})..."
if i2cdetect -y ${I2C_BUS} | grep -q "23"; then
    echo "  ✓ BH1750传感器已检测到 (地址: 0x23)"

    if [ -e /dev/bh1750 ]; then
        echo "  ✓ 设备节点 /dev/bh1750 存在"
        echo "  读取光照数据:"
        cat /dev/bh1750
    else
        echo "  ✗ 设备节点 /dev/bh1750 不存在，尝试使用i2c-tools直接读取..."
        i2cset -y ${I2C_BUS} ${BH1750_ADDR} 0x10
        sleep 0.2
        i2cget -y ${I2C_BUS} ${BH1750_ADDR} 0x00 w
    fi
else
    echo "  ✗ BH1750传感器未检测到，请检查接线"
fi

echo ""
echo "========================================"
echo "  传感器检测完成"
echo "========================================"
