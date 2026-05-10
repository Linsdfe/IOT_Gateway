#!/bin/bash
# MQTT配置教程脚本 - 在开发板上执行

echo "========================================"
echo "  MQTT配置教程 - i.MX6ULL开发板"
echo "========================================"

echo ""
echo "=== 第1步: 安装mosquitto ==="
echo "执行: sudo apt update && sudo apt install -y mosquitto mosquitto-clients libmosquitto-dev"
echo ""

echo "=== 第2步: 配置本地MQTT Broker ==="
echo "执行以下命令配置mosquitto:"
cat << 'CONF'
sudo tee /etc/mosquitto/conf.d/gateway.conf > /dev/null << 'EOF'
listener 1883 0.0.0.0
allow_anonymous true
max_connections -1
EOF
CONF
echo ""

echo "=== 第3步: 启动mosquitto服务 ==="
echo "执行: sudo systemctl restart mosquitto && sudo systemctl enable mosquitto"
echo ""

echo "=== 第4步: 测试本地MQTT ==="
echo "终端1 - 订阅主题:"
echo "  mosquitto_sub -h localhost -t '/iot/gateway/#' -v"
echo ""
echo "终端2 - 发布测试消息:"
echo "  mosquitto_pub -h localhost -t '/iot/gateway/test' -m 'hello from i.MX6ULL'"
echo ""

echo "=== 第5步: 运行带MQTT的网关程序 ==="
echo "执行: sudo ./iot_gateway --mqtt localhost --mqtt-port 1883"
echo ""

echo "========================================"
echo "  阿里云IoT配置（进阶）"
echo "========================================"
echo ""
echo "1. 登录阿里云IoT: https://iot.console.aliyun.com"
echo "2. 创建产品 -> 选择'自定义品类' -> 节点类型'直连设备'"
echo "3. 添加物模型属性:"
echo "   - Temperature (Float, °C)"
echo "   - Humidity (Float, %)"
echo "   - LightIntensity (Float, lux)"
echo "4. 注册设备，获取三元组: ProductKey, DeviceName, DeviceSecret"
echo "5. MQTT连接参数:"
echo "   Host: \${ProductKey}.iot-as-mqtt.\${regionId}.aliyuncs.com"
echo "   Port: 1883"
echo "   ClientId: \${DeviceName}|securemode=3,signmethod=hmacsha1|"
echo "   Username: \${DeviceName}&\${ProductKey}"
echo "   Password: HMAC-SHA1(DeviceSecret, content)"
