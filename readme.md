# 工业级物联网边缘网关

> 基于野火 i.MX6ULL Mini 开发板 + SHT30 温湿度传感器 + BH1750 光照传感器 + OneNET 云平台

## 目录

1. [项目介绍](#1-项目介绍)
2. [快速上手](#2-快速上手)
3. [编译与部署](#3-编译与部署)
4. [使用教程](#4-使用教程)
5. [常见问题](#5-常见问题)

> 📖 项目架构原理和开发指南请参阅 [ARCHITECTURE.md](ARCHITECTURE.md)

***

## 1. 项目介绍

### 1.1 项目定位

本项目是一个**工业级物联网边缘网关**，基于 i.MX6ULL 开发板，实现传感器数据采集、本地显示、云端上报的完整链路。

**核心特性：**

- ✅ **动态插件系统** — 运行时加载 .so 传感器驱动，热替换无需重新编译主程序
- ✅ **四种驱动模式** — 模拟数据 / 用户空间I2C / 标准内核驱动(IIO/HWMON) / 自定义内核驱动
- ✅ **自动驱动管理** — i2c 和 custom 插件自动释放/恢复内核驱动绑定，无需手动操作
- ✅ **MQTT 云平台** — OneNET Token 签名认证，物模型数据上报
- ✅ **边缘计算** — 滑动窗口滤波、统计分析、带滞回阈值告警、变化检测、资源监控
- ✅ **LCD 显示** — LVGL 仪表盘 + Framebuffer 文本双方案
- ✅ **统一 SDK** — init/start/stop/onData/hotSwap 简洁 API

### 1.2 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    OneNET云平台                              │
└──────────────────────────┬──────────────────────────────────┘
                           │ MQTT (Token认证)
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                  工业级物联网边缘网关                          │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            动态插件系统 (dlopen/dlsym)                │   │
│  │                                                       │   │
│  │  ┌─────────────────┐  ┌─────────────────┐            │   │
│  │  │ libsht30_bh1750  │  │ libsimulated_   │            │   │
│  │  │ _i2c_plugin.so   │  │ plugin.so       │  ...更多   │   │
│  │  └────────┬────────┘  └────────┬────────┘            │   │
│  │     ┌─────▼────────────────────▼──────┐               │   │
│  │     │      PluginLoader (加载器)      │               │   │
│  │     └─────────────┬──────────────────┘               │   │
│  └───────────────────┼──────────────────────────────────┘   │
│                       │                                      │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │              SensorReader (插件桥接)                    │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │                                      │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │              DataManager (多线程数据管理)               │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │                                      │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │              EdgeCompute (边缘计算)                     │  │
│  └──┬──────────┬──────────────┬──────────────────────────┘  │
│     │          │              │                              │
│  ┌──▼───┐  ┌──▼───────┐  ┌──▼──────────────────────────┐  │
│  │ 控制台 │  │ LCD显示  │  │    MqttPublisher            │  │
│  └──────┘  └──────────┘  └─────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 硬件清单

| 设备 | 型号 | 作用 | 接口 |
|------|------|------|------|
| 开发板 | EBF6ULL S1 Mini（野火） | 主控 | - |
| 温湿度传感器 | SHT30 | 采集温湿度 | I2C (0x44) |
| 光照传感器 | BH1750 | 采集光照强度 | I2C (0x23) |
| USB转TTL串口线 | CH340 | 调试串口 | UART |
| 4.3寸LCD | RGB触摸屏 | 本地显示 | RGB666 |

### 1.4 开发环境

| 项目 | 值 |
|------|-----|
| 主机OS | Ubuntu 22.04 LTS (x86_64) |
| 开发板OS | Debian 10, 内核 4.19.35-imx6 |
| 开发板IP | 192.168.7.2 (USB-OTG) |
| 交叉编译器 | arm-linux-gnueabihf-gcc 7.5.0 |
| 云平台 | OneNET (中移物联网) |

### 1.5 项目文件结构

```
IOT_Gateway/
├── CMakeLists.txt                      # CMake构建配置
├── readme.md                           # 使用教程（本文件）
├── ARCHITECTURE.md                     # 架构原理与开发指南
├── drivers/                            # 内核驱动模块
│   ├── sht30_driver.c                  # SHT30内核驱动（sysfs接口）
│   ├── bh1750_driver.c                 # BH1750内核驱动（sysfs接口）
│   └── Makefile                        # 内核模块编译脚本
├── src/
│   ├── app/
│   │   ├── main.cpp                    # 主程序入口
│   │   ├── sensor_reader.h/cpp         # 传感器读取（插件桥接）
│   │   ├── data_manager.h/cpp          # 多线程数据管理
│   │   ├── edge_compute.h/cpp          # 边缘计算模块（滤波/统计/告警）
│   │   ├── edge_compute_test.cpp       # 边缘计算单元测试（34项）
│   │   ├── onenet_test.cpp             # OneNET认证单元测试（35项）
│   │   ├── integration_test.cpp        # 模块集成测试（28项）
│   │   ├── system_test.cpp             # 系统级测试（24项）
│   │   ├── mqtt_publisher.h/cpp        # MQTT客户端
│   │   ├── onenet_iot.h/cpp            # OneNET认证
│   │   ├── lvgl_display.h/cpp          # LVGL图形界面
│   │   └── display_manager.h/cpp       # Framebuffer显示
│   ├── plugin/                         # 动态插件系统
│   │   ├── sensor_plugin.h             # 插件C接口规范
│   │   ├── plugin_loader.h/cpp         # 插件加载器
│   │   ├── sht30_bh1750_i2c_plugin.cpp # 用户空间I2C插件
│   │   ├── sht30_bh1750_kernel_plugin.cpp # 标准内核驱动插件
│   │   ├── sht30_bh1750_custom_plugin.cpp # 自定义内核驱动插件
│   │   └── simulated_plugin.cpp        # 模拟数据插件
│   └── sdk/
│       ├── gateway_sdk.h               # SDK公共接口
│       └── gateway_sdk.cpp             # SDK实现
├── third_party/lvgl/                   # LVGL v8.3 图形库
├── scripts/
│   └── build.sh                        # 统一编译脚本
└── tools/
    └── arm-linux-gnueabihf.cmake       # 交叉编译工具链
```

***

## 2. 快速上手

### 2.1 前提条件

| 项目 | 要求 |
|------|------|
| 开发板 | 野火 i.MX6ULL Mini，通过 USB-OTG 连接主机（192.168.7.2） |
| 主机 | Ubuntu 22.04，已安装交叉编译器和 sshpass |
| 传感器 | SHT30（0x44）+ BH1750（0x23）接在 I2C-1 总线 |

### 2.2 一键编译部署

```bash
cd /home/lin/Desktop/IOT_Gateway
bash scripts/build.sh all
```

### 2.3 列出可用插件

```bash
sshpass -p temppwd ssh debian@192.168.7.2 "sudo ~/iot_gateway --list-plugins"
```

预期输出：

```
Available plugins:
  sht30_bh1750_kernel        Linux kernel drivers (sht3x IIO/HWMON + bh1750 IIO + custom sysfs fallback)  (api=1)
  sht30_bh1750_custom        Custom kernel drivers (sht30_driver.ko + bh1750_driver.ko via custom sysfs)  (api=1)
  simulated                  Simulated sensor data (sine wave, no hardware required)  (api=1)
  sht30_bh1750_i2c           User-space I2C protocol (no kernel driver required, direct /dev/i2c-N access)  (api=1)
```

### 2.4 运行网关（四种驱动模式）

#### 模式1：模拟数据（无需硬件）

```bash
sshpass -p temppwd ssh debian@192.168.7.2 \
  "sudo ~/iot_gateway --plugin simulated --no-display --no-mqtt --interval 2000"
```

#### 模式2：用户空间I2C（自动释放内核驱动）

```bash
sshpass -p temppwd ssh debian@192.168.7.2 \
  "sudo ~/iot_gateway --plugin sht30_bh1750_i2c --no-display --no-mqtt --interval 2000"
```

> 💡 i2c 插件会自动检测并释放内核驱动占用的 I2C 设备，退出时自动恢复标准驱动绑定。

#### 模式3：标准内核驱动（推荐，IIO/HWMON接口）

```bash
# 加载标准内核驱动
sshpass -p temppwd ssh debian@192.168.7.2 \
  "sudo insmod ~/crc8.ko 2>/dev/null; sudo insmod ~/sht3x.ko 2>/dev/null; sudo insmod ~/bh1750.ko 2>/dev/null; \
   sudo sh -c 'echo sht3x 0x44 > /sys/bus/i2c/devices/i2c-1/new_device'; \
   sudo sh -c 'echo bh1750 0x23 > /sys/bus/i2c/devices/i2c-1/new_device'"

# 运行网关
sshpass -p temppwd ssh debian@192.168.7.2 \
  "sudo ~/iot_gateway --plugin sht30_bh1750_kernel --no-display --no-mqtt --interval 2000"
```

> ⚠️ 若标准模块因 MODVERSIONS CRC 不匹配无法加载，改用自定义驱动（模式4），kernel 插件会自动发现自定义驱动的 sysfs 接口。

#### 模式4：自定义内核驱动（自动切换）

```bash
# 加载自定义内核驱动模块
sshpass -p temppwd ssh debian@192.168.7.2 \
  "sudo insmod ~/sht30_driver.ko 2>/dev/null; sudo insmod ~/bh1750_driver.ko 2>/dev/null"

# 运行网关（custom 插件会自动释放标准驱动并注册到自定义驱动）
sshpass -p temppwd ssh debian@192.168.7.2 \
  "sudo ~/iot_gateway --plugin sht30_bh1750_custom --no-display --no-mqtt --interval 2000"
```

> 💡 custom 插件会自动解绑标准驱动、注册 I2C 设备到自定义驱动，退出时自动恢复标准驱动绑定。

### 2.5 四种驱动模式对比

| 对比项 | simulated | i2c | kernel | custom |
|--------|-----------|-----|--------|--------|
| 需要硬件 | ❌ | ✅ | ✅ | ✅ |
| 需要内核模块 | ❌ | ❌ | ✅ 标准或自定义 | ✅ 自定义 |
| 自动驱动管理 | N/A | ✅ 释放/恢复 | ✅ 自动注册 | ✅ 解绑/注册/恢复 |
| 适用场景 | 调试/演示 | 快速开发 | 标准化部署 | 定制化需求 |

### 2.6 连接 OneNET 云平台

```bash
sshpass -p temppwd ssh debian@192.168.7.2 \
  "sudo ~/iot_gateway --plugin sht30_bh1750_kernel \
   --onenet-pid <YOUR_PRODUCT_ID> \
   --onenet-dn <YOUR_DEVICE_NAME> \
   --onenet-dk <YOUR_DEVICE_KEY>"
```

### 2.7 常用命令速查

| 场景 | 命令 |
|------|------|
| 查看帮助 | `./iot_gateway --help` |
| 列出插件 | `sudo ./iot_gateway --list-plugins` |
| 仅传感器采集 | `sudo ./iot_gateway --plugin <NAME> --no-display --no-mqtt` |
| 自定义采集间隔 | `sudo ./iot_gateway --plugin <NAME> --interval 5000` |
| 扫描I2C设备 | `sudo i2cdetect -y 1` |

> 📖 更多命令详见 [4. 使用教程](#4-使用教程)

***

## 3. 编译与部署

### 3.1 使用统一编译脚本（推荐）

```bash
cd /home/lin/Desktop/IOT_Gateway

# 一键编译+部署+验证
bash scripts/build.sh all

# 分步执行
bash scripts/build.sh cross          # 交叉编译
bash scripts/build.sh deploy         # 部署到开发板
bash scripts/build.sh verify         # 验证
bash scripts/build.sh onboard        # 开发板本地编译
```

### 3.2 手动编译

#### 交叉编译（带MQTT + LVGL显示）

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-linux-gnueabihf.cmake \
         -DUSE_MOSQUITTO=ON -DUSE_LVGL=ON
make -j4
```

#### 开发板本地编译

```bash
# 传输源码到开发板
cd /home/lin/Desktop/IOT_Gateway
tar czf /tmp/iot_gateway_src.tar.gz --exclude='build' --exclude='tools/sysroot' .
sshpass -p temppwd scp /tmp/iot_gateway_src.tar.gz debian@192.168.7.2:~/

# 在开发板上编译
sshpass -p temppwd ssh debian@192.168.7.2
cd ~ && mkdir -p IOT_Gateway && tar xzf iot_gateway_src.tar.gz -C IOT_Gateway/
cd IOT_Gateway && mkdir build && cd build
cmake .. -DUSE_MOSQUITTO=ON -DUSE_LVGL=ON
make -j4
```

### 3.3 编译选项

| 选项 | 说明 |
|------|------|
| `-DUSE_MOSQUITTO=ON` | 启用MQTT支持（需libmosquitto-dev） |
| `-DUSE_LVGL=ON` | 启用LVGL图形界面（仪表盘+进度条+动画，推荐） |
| `-DENABLE_DISPLAY=ON` | 启用简单Framebuffer显示（5x7字体纯文本，备选） |
| `-DCMAKE_TOOLCHAIN_FILE=...` | 交叉编译工具链 |

**显示方案选择：**

| 方案 | 编译选项 | 效果 | 适用场景 |
|------|---------|------|---------|
| LVGL图形界面 | `-DUSE_LVGL=ON` | 仪表盘+进度条+动画，深色主题，~30fps | 产品级UI展示 |
| Framebuffer文本 | `-DENABLE_DISPLAY=ON` | 纯文本5x7字体，~5fps，无外部依赖 | 快速验证/极简场景 |
| 无显示 | 不加显示选项 | 仅控制台输出 | 无LCD或远程运行 |

> 💡 两个显示选项互斥，只能选一个。默认不启用显示。运行时可通过 `--no-display` 参数关闭显示。

**编译示例：**

```bash
# 推荐：LVGL图形界面 + MQTT
cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-linux-gnueabihf.cmake \
         -DUSE_MOSQUITTO=ON -DUSE_LVGL=ON

# 备选：Framebuffer文本 + MQTT
cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-linux-gnueabihf.cmake \
         -DUSE_MOSQUITTO=ON -DENABLE_DISPLAY=ON

# 仅传感器采集（无显示无MQTT，最精简）
cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-linux-gnueabihf.cmake
```

### 3.4 运行测试

项目提供 4 套测试，覆盖单元测试、集成测试和系统测试：

```bash
cd /home/lin/Desktop/IOT_Gateway

# x86 本地测试（无需交叉编译）
cmake -B build_x86 -S .
cmake --build build_x86 -j$(nproc)

# 运行全部测试
./build_x86/edge_compute_test     # 边缘计算单元测试（34项）
./build_x86/onenet_test           # OneNET认证单元测试（35项）
./build_x86/integration_test      # 模块集成测试（28项）
./build_x86/system_test           # 系统级测试（24项）
```

| 测试套件 | 类型 | 数量 | 覆盖内容 |
|---------|------|------|---------|
| `edge_compute_test` | 单元测试 | 34 | 滑动滤波、窗口统计、滞回告警、变化检测、环形缓冲、资源监控 |
| `onenet_test` | 单元测试 | 35 | Token生成、Base64编解码、JSON载荷、MQTT参数、边界条件 |
| `integration_test` | 集成测试 | 28 | 回调链、数据流、插件加载/卸载、告警生命周期、环形缓冲持久化 |
| `system_test` | 系统测试 | 24 | SDK生命周期、多次启停循环、回调注册、插件列表、配置变体、异常处理 |

ARM 交叉编译测试（产物不可在 x86 运行）：

```bash
cmake -B build_arm -DCMAKE_TOOLCHAIN_FILE=tools/arm-linux-gnueabihf.cmake -S .
cmake --build build_arm -j$(nproc)
file build_arm/*_test                          # 验证 ARM 架构
```

***

## 4. 使用教程

> 本章汇总所有开发板上的操作指令，可直接复制执行。以下命令均在开发板上运行，或通过 SSH 远程执行。

### 4.1 开发板连接

```bash
# SSH连接（从主机）
sshpass -p temppwd ssh debian@192.168.7.2

# 或使用root
sshpass -p root ssh root@192.168.7.2

# 串口连接
sudo picocom -b 115200 /dev/ttyUSB0
```

### 4.2 内核驱动管理

```bash
# --- 标准内核驱动 ---
# 加载
sudo insmod ~/crc8.ko
sudo insmod ~/sht3x.ko
sudo insmod ~/bh1750.ko

# 注册I2C设备
sudo sh -c 'echo "sht3x 0x44" > /sys/bus/i2c/devices/i2c-1/new_device'
sudo sh -c 'echo "bh1750 0x23" > /sys/bus/i2c/devices/i2c-1/new_device'

# 验证
cat /sys/class/hwmon/hwmon1/temp1_input       # 温度（毫摄氏度）
cat /sys/class/hwmon/hwmon1/humidity1_input    # 湿度（毫百分比）
cat /sys/bus/iio/devices/iio:device1/in_illuminance_raw  # 光照原始值
cat /sys/bus/iio/devices/iio:device1/in_illuminance_scale # 光照比例

# 卸载
sudo sh -c 'echo "0x44" > /sys/bus/i2c/devices/i2c-1/delete_device'
sudo sh -c 'echo "0x23" > /sys/bus/i2c/devices/i2c-1/delete_device'
sudo rmmod bh1750
sudo rmmod sht3x
sudo rmmod crc8

# --- 自定义内核驱动 ---
# 加载
sudo insmod ~/sht30_driver.ko
sudo insmod ~/bh1750_driver.ko

# 注册I2C设备
sudo sh -c 'echo "sht30 0x44" > /sys/bus/i2c/devices/i2c-1/new_device'
sudo sh -c 'echo "bh1750_custom 0x23" > /sys/bus/i2c/devices/i2c-1/new_device'

# 验证
cat /sys/bus/i2c/drivers/sht30/1-0044/temperature    # 温度（毫摄氏度）
cat /sys/bus/i2c/drivers/sht30/1-0044/humidity        # 湿度（毫百分比）
cat /sys/bus/i2c/drivers/bh1750_custom/1-0023/illuminance  # 光照（厘lux，0.01lux）

# 卸载
sudo sh -c 'echo "0x44" > /sys/bus/i2c/devices/i2c-1/delete_device'
sudo sh -c 'echo "0x23" > /sys/bus/i2c/devices/i2c-1/delete_device'
sudo rmmod bh1750_driver
sudo rmmod sht30_driver
```

### 4.3 网关运行

```bash
# 查看帮助
~/iot_gateway --help

# 列出可用插件
sudo ~/iot_gateway --list-plugins

# --- 四种驱动模式 ---

# 模式1: 模拟数据（无需硬件）
sudo ~/iot_gateway --plugin simulated --no-display --no-mqtt

# 模式2: 用户空间I2C（自动释放内核驱动）
sudo ~/iot_gateway --plugin sht30_bh1750_i2c --no-display --no-mqtt

# 模式3: 标准内核驱动（IIO/HWMON接口，推荐）
sudo ~/iot_gateway --plugin sht30_bh1750_kernel --no-display --no-mqtt

# 模式4: 自定义内核驱动（自动切换到自定义驱动sysfs接口）
sudo ~/iot_gateway --plugin sht30_bh1750_custom --no-display --no-mqtt

# --- 带完整功能运行 ---

# 传感器采集 + LCD显示 + OneNET云平台
sudo ~/iot_gateway --plugin sht30_bh1750_kernel \
  --onenet-pid <YOUR_PRODUCT_ID> \
  --onenet-dn <YOUR_DEVICE_NAME> \
  --onenet-dk <YOUR_DEVICE_KEY>

# 传感器采集 + LCD显示 + 本地MQTT
sudo ~/iot_gateway --plugin sht30_bh1750_kernel --mqtt localhost

# 自定义采集间隔（5秒）
sudo ~/iot_gateway --plugin sht30_bh1750_kernel --no-display --no-mqtt --interval 5000

# 自定义I2C配置
sudo ~/iot_gateway --plugin sht30_bh1750_i2c \
  --plugin-config "i2c=/dev/i2c-1;sht30=0x44;bh1750=0x23"

# 自定义模拟数据基准值
sudo ~/iot_gateway --plugin simulated \
  --plugin-config "temp=30;humi=70;light=500"

# 调整日志级别
sudo ~/iot_gateway --plugin sht30_bh1750_kernel --log-level debug --no-display --no-mqtt
```

### 4.4 驱动模式切换

> 💡 i2c 和 custom 插件会自动处理驱动绑定切换，无需手动操作。以下仅为手动切换参考。

从标准内核驱动切换到自定义内核驱动：

```bash
# 自动方式（推荐）：直接运行 custom 插件
sudo ~/iot_gateway --plugin sht30_bh1750_custom --no-display --no-mqtt

# 手动方式（调试用）：
sudo sh -c 'echo "1-0044" > /sys/bus/i2c/drivers/sht3x/unbind'
sudo sh -c 'echo "1-0023" > /sys/bus/i2c/drivers/bh1750/unbind'
sudo sh -c 'echo "0x44" > /sys/bus/i2c/devices/i2c-1/delete_device'
sudo sh -c 'echo "0x23" > /sys/bus/i2c/devices/i2c-1/delete_device'
sudo sh -c 'echo "sht30 0x44" > /sys/bus/i2c/devices/i2c-1/new_device'
sudo sh -c 'echo "bh1750_custom 0x23" > /sys/bus/i2c/devices/i2c-1/new_device'
sudo ~/iot_gateway --plugin sht30_bh1750_custom --no-display --no-mqtt
```

从任意模式切换到用户空间I2C：

```bash
# 自动方式（推荐）：直接运行 i2c 插件
sudo ~/iot_gateway --plugin sht30_bh1750_i2c --no-display --no-mqtt

# 手动方式（调试用）：
sudo sh -c 'echo "0x44" > /sys/bus/i2c/devices/i2c-1/delete_device'
sudo sh -c 'echo "0x23" > /sys/bus/i2c/devices/i2c-1/delete_device'
sudo ~/iot_gateway --plugin sht30_bh1750_i2c --no-display --no-mqtt
```

### 4.5 诊断与调试

```bash
# I2C总线扫描
sudo i2cdetect -y 1

# 查看已加载内核模块
lsmod | grep -E 'sht3x|bh1750|crc8'

# 查看I2C设备列表
ls /sys/bus/i2c/devices/

# 查看HWMON设备
cat /sys/class/hwmon/hwmon1/name    # 应显示 sht3x

# 查看IIO设备
cat /sys/bus/iio/devices/iio:device1/name  # 应显示 bh1750

# 查看自定义驱动sysfs
ls /sys/bus/i2c/drivers/sht30/
ls /sys/bus/i2c/drivers/bh1750_custom/

# 查看运行日志
tail -f /tmp/iot_gateway.log

# 查看内核日志（驱动调试）
dmesg | tail -20
dmesg | grep -E 'sht3x|bh1750|sht30'

# 检查可执行文件架构
file ~/iot_gateway

# 检查插件依赖
ldd /usr/lib/iot/plugins/libsht30_bh1750_kernel_plugin.so
```

### 4.6 网络配置

```bash
# 配置DNS
echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf

# 设置默认网关（通过主机上网）
sudo route add default gw 192.168.7.1

# 主机端开启NAT转发
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -s 192.168.7.0/30 -j MASQUERADE

# 测试网络连通性
ping -c 3 8.8.8.8
ping -c 3 mqtts.heclouds.com
```

### 4.7 开发板文件结构

```
i.MX6ULL 开发板 (debian@npi)
├── ~/iot_gateway                ★ 可执行文件
├── ~/sht30_driver.ko            ★ 自研SHT30内核驱动模块
├── ~/bh1750_driver.ko           ★ 自研BH1750内核驱动模块
├── ~/sht3x.ko                   ★ Linux标准SHT3x HWMON驱动模块
├── ~/bh1750.ko                  ★ Linux标准BH1750 IIO驱动模块
├── ~/crc8.ko                    ★ CRC8库模块（SHT3x依赖）
├── /usr/lib/iot/plugins/        ★ 插件目录
│   ├── libsht30_bh1750_i2c_plugin.so
│   ├── libsht30_bh1750_kernel_plugin.so
│   ├── libsht30_bh1750_custom_plugin.so
│   └── libsimulated_plugin.so
└── /tmp/iot_gateway.log         运行日志
```

***

## 5. 常见问题

### Q1: i2cdetect命令找不到？

**A:** 开发板需要安装i2c-tools：`sudo apt install -y i2c-tools`

### Q2: OneNET设备在线但属性值显示undefined？

**A:** 数据格式错误。OneNET要求每个属性值用`{"value": xxx}`包裹。

### Q3: 交叉编译带MQTT时链接失败？

**A:** 推荐在开发板上直接编译，或使用 `--allow-shlib-undefined` 链接选项（已自动配置）。

### Q4: 插件加载失败（dlopen failed）？

**A:** 常见原因：
1. .so 文件路径不正确（使用绝对路径）
2. .so 文件架构不匹配（x86 .so 不能在 ARM 上运行）
3. .so 文件缺少依赖库（使用 `ldd` 检查）
4. .so 未导出 `sensor_plugin_get` 函数

### Q5: 插件 API 版本不匹配？

**A:** 主程序和插件必须使用相同版本的 `sensor_plugin.h` 编译。重新编译插件即可。

### Q6: 如何热替换插件？

**A:** 通过 GatewaySDK 的 `hotSwapPlugin()` 方法，或替换 .so 文件后重启程序。

### Q7: 开发板无法访问外网？

**A:**

```bash
echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf
sudo route add default gw 192.168.7.1
# 主机端开启NAT:
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -s 192.168.7.0/30 -j MASQUERADE
```

### Q8: 程序显示 SIMULATED MODE 但传感器已连接？

**A:** 模拟模式会在传感器连续3次读取失败后自动启用。程序每30次读取尝试恢复真实传感器。检查内核驱动是否正确加载和I2C设备是否注册。

### Q9: 内核模块加载失败 "disagrees about version of symbol"？

**A:** 运行中内核启用了 `CONFIG_MODVERSIONS=y`，编译的模块CRC校验值与内核不匹配。**推荐方案**：改用自定义内核驱动（sht30_driver.ko + bh1750_driver.ko），kernel 插件会自动发现自定义驱动的 sysfs 接口。

### Q10: 切换驱动模式后I2C总线卡死？

**A:** i2c 和 custom 插件会自动处理驱动绑定切换。如果手动操作，必须先解绑旧驱动再注册新驱动。如果总线已卡死，重启开发板恢复。

### Q11: 自定义驱动的光照数据是标准驱动的十分之一？

**A:** 自定义驱动的 `illuminance` sysfs 属性单位是厘lux（0.01 lux），不是毫lux。插件已修复此问题（除以100而非1000）。如果使用旧版插件，请更新到最新版本。
