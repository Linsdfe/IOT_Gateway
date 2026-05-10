#!/bin/bash
# LCD显示配置教程脚本 - 在开发板上执行

echo "========================================"
echo "  LCD显示配置教程 - i.MX6ULL开发板"
echo "========================================"

echo ""
echo "=== 第1步: 检查Framebuffer设备 ==="
echo "执行: ls -la /dev/fb*"
echo "预期输出: /dev/fb0"
echo ""
echo "如果没有fb0，检查设备树overlay:"
echo "  cat /boot/uEnv.txt | grep lcd"
echo "确保以下行未被注释:"
echo "  dtoverlay=/usr/lib/linux-image-4.19.35-imx6/imx-fire-lcd.dtbo"
echo ""

echo "=== 第2步: 获取屏幕参数 ==="
echo "执行以下命令获取分辨率和色深:"
cat << 'CMD'
fbset
CMD
echo "预期输出类似:"
echo "  mode \"480x272\""
echo "    geometry 480 272 480 272 32"
echo "    timings 111111 40 5 8 8 8 3"
echo "    rgba 8/16,8/8,8/0,8/24"
echo "endmode"
echo ""

echo "=== 第3步: 测试Framebuffer ==="
echo "清屏:"
echo "  sudo dd if=/dev/zero of=/dev/fb0"
echo ""
echo "全屏红色:"
echo "  sudo dd if=/dev/urandom bs=480 count=1088 of=/dev/fb0"
echo ""
echo "更安全的测试方式（用cat写颜色）:"
cat << 'CMD'
# 白色填充（32位色深，0xFFFFFFFF）
sudo python3 -c "
fb = open('/dev/fb0', 'wb')
pixel = b'\xff\xff\xff\xff'
for i in range(480 * 272):
    fb.write(pixel)
fb.close()
print('Screen should be white now')
"
CMD
echo ""

echo "=== 第4步: 运行带显示的网关程序 ==="
echo "执行: sudo ./iot_gateway --display"
echo ""

echo "=== 第5步: 触摸屏校准（如果需要） ==="
echo "执行: sudo ts_calibrate"
echo "然后按屏幕提示点击5个点"
echo ""

echo "========================================"
echo "  常见LCD问题排查"
echo "========================================"
echo ""
echo "1. 黑屏: 检查LCD排线是否插好，拨码开关是否正确"
echo "2. 花屏: fbset检查色深是否为32位"
echo "3. 触摸不准: 执行ts_calibrate校准"
echo "4. /dev/fb0不存在: 检查uEnv.txt中LCD overlay是否启用"
echo "5. 权限不足: sudo chmod 666 /dev/fb0"
