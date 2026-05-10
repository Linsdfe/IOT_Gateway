# 工业级物联网边缘网关 - 完整开发指南

> 基于野火i.MX6ULL Mini开发板 + SHT30温湿度传感器 + BH1750光照传感器 + OneNET云平台

## 目录
1. [项目概述](#1-项目概述)
2. [开发环境配置](#2-开发环境配置)
3. [核心概念讲解](#3-核心概念讲解)
4. [传感器驱动开发](#4-传感器驱动开发)
5. [应用层开发（C++多线程）](#5-应用层开发c多线程)
6. [MQTT通信与云平台接入](#6-mqtt通信与云平台接入)
7. [LCD显示](#7-lcd显示)
8. [SDK封装](#8-sdk封装)
9. [编译与部署](#9-编译与部署)
10. [常见问题FAQ](#10-常见问题faq)
11. [附录](#附录)

---

## 1. 项目概述

### 1.1 项目定位

本项目是一个**工业级物联网边缘网关**，基于i.MX6ULL开发板，实现传感器数据采集、本地显示、云端上报的完整链路。

**已完成功能（第一阶段）：**
- ✅ SHT30温湿度传感器 + BH1750光照传感器数据采集
- ✅ C++多线程数据管理架构（采集线程 + 回调机制）
- ✅ MQTT协议连接OneNET云平台（Token签名认证）
- ✅ Framebuffer LCD显示（内置5x7字体引擎）
- ✅ 统一SDK封装（init/start/stop/onData API）
- ✅ 交叉编译 + 开发板本地编译双模式

**待开发功能（第二阶段）：**
- ⏳ Buildroot从零构建定制Linux系统
- ⏳ 内核驱动模块集成（sht30_driver.ko / bh1750_driver.ko）
- ⏳ LVGL图形界面替代简单Framebuffer
- ⏳ 系统烧录到eMMC/NAND

### 1.2 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    OneNET云平台                              │
│              (数据存储/可视化/规则引擎)                        │
└──────────────────────────┬──────────────────────────────────┘
                           │ MQTT (Token认证)
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                  工业级物联网边缘网关                          │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐                         │
│  │ 温湿度传感器   │  │  光照传感器   │                         │
│  │ SHT30 (I2C)  │  │ BH1750 (I2C) │                         │
│  └──────┬───────┘  └──────┬───────┘                         │
│         │    I2C总线1      │                                  │
│  ┌──────▼─────────────────▼──────────────────────────────┐  │
│  │              SensorReader (i2c-dev用户态)              │  │
│  │         /dev/i2c-1, 0x44(SHT30), 0x23(BH1750)        │  │
│  └──────┬────────────────────────────────────────────────┘  │
│         │                                                    │
│  ┌──────▼────────────────────────────────────────────────┐  │
│  │              DataManager (多线程数据管理)               │  │
│  │    采集线程(2s) → mutex保护 → 回调通知 → 各消费者       │  │
│  └──┬──────────┬──────────────┬──────────────────────────┘  │
│     │          │              │                              │
│  ┌──▼───┐  ┌──▼───────┐  ┌──▼──────────────────────────┐  │
│  │ 控制台 │  │ LCD显示  │  │    MqttPublisher            │  │
│  │ 输出   │  │ Framebuf │  │  OneNET/阿里云/本地MQTT     │  │
│  └──────┘  └──────────┘  └─────────────────────────────┘  │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │              GatewaySDK (统一API)                       │  │
│  │  init() → start() → stop() → onData() → readSensors() │  │
│  └────────────────────────────────────────────────────────┘  │
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

### 1.4 实际开发环境信息

| 项目 | 值 |
|------|------|
| 主机OS | Ubuntu 22.04.4 LTS (x86_64, VMware) |
| 主机IP | 192.168.40.130 (NAT), 192.168.7.1 (USB) |
| 开发板OS | Debian 10.13, 内核 4.19.35-imx6 |
| 开发板主机名 | npi |
| 开发板IP | 192.168.7.2 (USB虚拟网卡) |
| 交叉编译器 | Linaro GCC 7.5.0 + Ubuntu arm-gcc 11.4.0 |
| 云平台 | OneNET (中移物联网) |

### 1.5 项目文件结构

```
/home/lin/Desktop/IOT_Gateway/
├── CMakeLists.txt                      # CMake构建配置
├── readme.md                           # 项目文档
├── src/
│   ├── app/
│   │   ├── main.cpp                    # 主程序入口（命令行参数解析）
│   │   ├── sensor_reader.h/cpp         # 传感器读取（i2c-dev用户态）
│   │   ├── data_manager.h/cpp          # 多线程数据管理+回调机制
│   │   ├── mqtt_publisher.h/cpp        # MQTT客户端（支持OneNET/阿里云/本地）
│   │   ├── aliyun_iot.h/cpp            # 阿里云IoT认证（HMAC-SHA1签名）
│   │   ├── onenet_iot.h/cpp            # OneNET认证（Token签名）
│   │   ├── display_manager.h/cpp       # Framebuffer显示（内置5x7字体）
│   │   └── iot_gateway.py              # Python版网关（快速验证用）
│   ├── driver/
│   │   ├── sht30_driver.c              # SHT30内核驱动模块
│   │   ├── bh1750_driver.c             # BH1750内核驱动模块
│   │   ├── imx-fire-i2c1-sensors.dtso  # 传感器设备树覆盖
│   │   └── Makefile                    # 驱动编译脚本
│   └── sdk/
│       ├── gateway_sdk.h               # SDK公共接口
│       └── gateway_sdk.cpp             # SDK实现
├── scripts/
│   ├── setup_env.sh                    # 环境配置验证脚本
│   ├── setup_mqtt.sh                   # MQTT配置教程脚本
│   ├── setup_lcd.sh                    # LCD显示配置脚本
│   └── test_sensors.sh                 # 传感器测试脚本
└── tools/
    ├── arm-linux-gnueabihf.cmake       # 交叉编译工具链配置
    └── sysroot/                        # ARM库（从开发板拷贝）
```

---

## 2. 开发环境配置

### 2.1 主机端环境配置

#### 安装编译依赖

```bash
sudo apt install -y \
    build-essential git wget cpio unzip rsync bc \
    libncurses5-dev libncursesw5-dev flex bison libssl-dev \
    device-tree-compiler python3 python3-pip \
    file swig libpython3-dev \
    picocom minicom \
    gcc-arm-linux-gnueabihf \
    tftpd-hpa tftp-hpa \
    nfs-kernel-server \
    openssh-server \
    i2c-tools \
    libmosquitto-dev mosquitto mosquitto-clients \
    cmake pkg-config \
    autoconf automake libtool \
    sshpass
```

#### 安装Linaro交叉编译器

```bash
cd /tmp
wget https://releases.linaro.org/components/toolchain/binaries/latest-7/arm-linux-gnueabihf/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf.tar.xz
sudo mkdir -p /opt/toolchains
sudo tar xf gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf.tar.xz -C /opt/toolchains/
echo 'export PATH=/opt/toolchains/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
arm-linux-gnueabihf-gcc --version
```

#### 配置TFTP服务

```bash
sudo mkdir -p /tftpboot && sudo chmod 777 /tftpboot
sudo tee /etc/default/tftpd-hpa > /dev/null << 'EOF'
TFTP_USERNAME="tftp"
TFTP_DIRECTORY="/tftpboot"
TFTP_ADDRESS="0.0.0.0:69"
TFTP_OPTIONS="-l -c -s"
EOF
sudo systemctl restart tftpd-hpa && sudo systemctl enable tftpd-hpa
```

#### 配置NFS服务

```bash
sudo mkdir -p /nfs/rootfs && sudo chmod 777 /nfs/rootfs
sudo bash -c 'cat > /etc/exports << EOF
/nfs/rootfs *(rw,sync,no_subtree_check,no_root_squash)
EOF'
sudo exportfs -ra
sudo systemctl restart nfs-kernel-server && sudo systemctl enable nfs-kernel-server
```

#### 配置串口权限

```bash
sudo usermod -aG dialout $USER
# 重新登录后生效
# 连接开发板: sudo picocom -b 115200 /dev/ttyUSB0
# 退出: Ctrl+A 然后按 X
```

### 2.2 开发板端环境配置

#### 连接开发板

```bash
# 主机端通过picocom连接
sudo picocom -b 115200 /dev/ttyUSB0

# 开发板登录
# 用户名: debian  密码: temppwd
# 或: root / root
```

#### 开发板安装必要软件

```bash
# 安装i2c-tools和mosquitto
sudo apt update
sudo apt install -y i2c-tools mosquitto mosquitto-clients libmosquitto-dev

# 安装编译工具（用于开发板本地编译）
sudo apt install -y build-essential cmake libssl-dev
```

#### 配置开发板网络

```bash
# 方式1: 以太网DHCP
sudo dhclient eth0

# 方式2: USB虚拟网卡（默认已有192.168.7.2）
# 主机端可通过192.168.7.2访问开发板

# 设置DNS（访问外网必须）
echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf

# 设置默认网关（如果需要通过主机上网）
sudo route add default gw 192.168.7.1
```

#### 验证I2C和传感器

```bash
# 查看I2C总线
ls /dev/i2c-*
# 输出: /dev/i2c-0  /dev/i2c-1

# 扫描I2C设备
i2cdetect -y 1
# 预期: 0x44(SHT30) 和 0x23(BH1750) 出现

# 快速验证传感器（使用开发板自带的Python脚本）
python3 read_all.py
# 输出: 温度:25.6℃  湿度:67.0%  光照:14.2lux
```

---

## 3. 核心概念讲解

### 3.1 I2C通信原理

I2C（Inter-Integrated Circuit）是一种两线制串行总线协议：

- **SDA**（数据线）：双向数据传输
- **SCL**（时钟线）：由主设备控制时钟同步

```
主设备(开发板)          从设备(传感器)
    │                      │
    ├──── 起始信号 ────────┤
    ├──── 设备地址+写 ─────┤  (0x44或0x23)
    ├──── 寄存器/命令 ─────┤
    ├──── 重复起始信号 ────┤
    ├──── 设备地址+读 ─────┤
    ├──── 读取数据 ────────┤
    ├──── 停止信号 ────────┤
```

**本项目使用的I2C设备：**

| 传感器 | I2C地址 | 命令 | 数据长度 | 转换公式 |
|--------|---------|------|----------|----------|
| SHT30 | 0x44 | 0x2C 0x06 | 6字节 | T=-45+175×raw/65535, RH=100×raw/65535 |
| BH1750 | 0x23 | 0x10 | 2字节 | Lux=(high<<8\|low)/1.2 |

### 3.2 Linux I2C用户态编程（i2c-dev）

Linux内核提供了`i2c-dev`接口，允许用户态程序直接操作I2C总线，无需编写内核驱动：

```c
// 打开I2C总线
int fd = open("/dev/i2c-1", O_RDWR);

// 设置从机地址
ioctl(fd, I2C_SLAVE, 0x44);

// 发送命令
uint8_t cmd[2] = {0x2C, 0x06};
write(fd, cmd, 2);

// 读取数据
uint8_t buf[6];
read(fd, buf, 6);

close(fd);
```

**优点：** 开发快速、调试方便、无需内核模块
**缺点：** 每次操作需要open/ioctl系统调用，性能略低于内核驱动

### 3.3 多线程数据管理架构

本项目采用**生产者-消费者**模式：

```
采集线程(Producer)          消费者(Consumers)
┌──────────────┐      ┌──────────────┐
│ SensorReader │      │   控制台输出  │
│   readAll()  │──┬──→│   printf     │
│   (2秒间隔)   │  │   └──────────────┘
└──────┬───────┘  │   ┌──────────────┐
       │          ├──→│  LCD显示     │
  mutex保护       │   │  Framebuffer │
  latestData_     │   └──────────────┘
       │          │   ┌──────────────┐
       │          └──→│  MQTT发布    │
       │              │  OneNET/本地 │
       │              └──────────────┘
```

关键设计：
- `SensorReader`：封装I2C操作，提供`readAll()`接口
- `DataManager`：管理采集线程，mutex保护共享数据，回调通知消费者
- `MqttPublisher`：独立发布线程，自动重连，支持OneNET/阿里云/本地MQTT
- `DisplayManager`：Framebuffer显示线程，200ms刷新

### 3.4 MQTT协议与云平台认证

#### MQTT发布/订阅模式

```
网关 ──发布──>  ┌──────────┐  <──订阅── 手机APP
               │  MQTT    │
               │  Broker  │  <──订阅── Web页面
               └──────────┘
```

#### OneNET Token认证机制

OneNET使用基于HMAC-SHA1的Token认证，流程如下：

```
1. 签名内容 = "过期时间\n签名方法\n资源路径\n版本号"
   例: "1735689600\nsha1\nproducts/XUV077XBf9/devices/imx6ull_01\n2018-10-31"

2. 密钥 = Base64解码(设备密钥)
   例: Base64Decode("UG83cDgySktEQWZhNHdLRXQ2WHd6TGRaZUtSdG9CZTI=")

3. 签名 = Base64Encode(HMAC-SHA1(密钥, 签名内容))

4. Token = "version=2018-10-31&res=URL编码(资源路径)&et=过期时间&method=sha1&sign=URL编码(签名)"
```

#### OneNET物模型数据格式

属性上报必须使用`{"value": xxx}`包裹每个属性值：

```json
{
  "id": "1715689600",
  "version": "1.0",
  "params": {
    "Temperature": {"value": 25.6},
    "Humidity": {"value": 67.0},
    "LightIntensity": {"value": 14.2}
  }
}
```

> ⚠️ 如果直接写 `"Temperature": 25.6`（不包裹value），OneNET会显示undefined！

---

## 4. 传感器驱动开发

### 4.1 两种驱动方式对比

| 方式 | 实现 | 优点 | 缺点 |
|------|------|------|------|
| **用户态i2c-dev** | open("/dev/i2c-1") | 开发快、无需内核模块 | 性能略低 |
| **内核驱动模块** | insmod xxx.ko | 性能高、标准接口 | 需匹配内核版本 |

**本项目当前使用用户态i2c-dev方式**（第一阶段），内核驱动已编写但暂未加载。

### 4.2 SHT30温湿度传感器

#### 工作原理

SHT30通过I2C通信，单次测量流程：
1. 主设备发送测量命令 `0x2C 0x06`（高精度模式）
2. 等待20ms测量完成
3. 读取6字节数据：[温度高, 温度低, CRC, 湿度高, 湿度低, CRC]
4. 转换公式：`T = -45 + 175 × raw / 65535`，`RH = 100 × raw / 65535`

#### 用户态读取代码（sensor_reader.cpp核心片段）

```cpp
bool SensorReader::readSHT30(float &temp, float &humi)
{
    // 发送测量命令
    uint8_t cmd[2] = {0x2C, 0x06};
    if (write(fd_sht30_, cmd, 2) != 2)
        return false;

    usleep(20000);  // 等待20ms

    // 读取6字节原始数据
    uint8_t buf[6];
    if (read(fd_sht30_, buf, 6) != 6)
        return false;

    // 转换为实际温湿度
    uint16_t raw_temp = (buf[0] << 8) | buf[1];
    uint16_t raw_humi = (buf[3] << 8) | buf[4];
    temp = -45.0f + 175.0f * raw_temp / 65535.0f;
    humi = 100.0f * raw_humi / 65535.0f;
    return true;
}
```

#### 内核驱动（sht30_driver.c）

内核驱动创建 `/dev/sht30` 字符设备节点，支持 `cat /dev/sht30` 读取。源码位于 `src/driver/sht30_driver.c`，主要特点：
- I2C子系统注册，支持设备树匹配 `compatible = "sensirion,sht30"`
- 字符设备框架：alloc_chrdev_region → cdev_init → cdev_add → device_create
- 读取格式：`25.50,67.00\n`（温度,湿度）

### 4.3 BH1750光照传感器

#### 工作原理

1. 发送连续测量命令 `0x10`（高分辨率，约120ms）
2. 读取2字节数据：[高8位, 低8位]
3. 转换公式：`Lux = (high << 8 | low) / 1.2`

#### 用户态读取代码

```cpp
bool SensorReader::readBH1750(float &light)
{
    uint8_t cmd = 0x10;
    if (write(fd_bh1750_, &cmd, 1) != 1)
        return false;

    usleep(180000);  // 等待180ms

    uint8_t buf[2];
    if (read(fd_bh1750_, buf, 2) != 2)
        return false;

    uint16_t raw = (buf[0] << 8) | buf[1];
    light = raw / 1.2f;
    return true;
}
```

### 4.4 设备树配置

野火EBF6ULL使用设备树插件（Device Tree Overlay）机制，通过 `/boot/uEnv.txt` 启用I2C：

```bash
# 查看当前启用的overlay
cat /boot/uEnv.txt

# I2C1已启用的行：
dtoverlay=/usr/lib/linux-image-4.19.35-imx6/imx-fire-i2c1.dtbo
```

自定义传感器设备树覆盖文件位于 `src/driver/imx-fire-i2c1-sensors.dtso`：

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "fsl,imx6ull-14x14-evk", "fsl,imx6ull";
    fragment@0 {
        target = <&i2c1>;
        __overlay__ {
            status = "okay";
            sht30@44 {
                compatible = "sensirion,sht30";
                reg = <0x44>;
            };
            bh1750@23 {
                compatible = "rohm,bh1750";
                reg = <0x23>;
            };
        };
    };
};
```

---

## 5. 应用层开发（C++多线程）

### 5.1 SensorReader - 传感器读取模块

**职责：** 封装I2C操作，提供统一的传感器数据读取接口。

**关键设计：**
- 构造时指定I2C设备路径和传感器地址
- `init()` 打开两个文件描述符（SHT30和BH1750各一个）
- `readAll()` 依次读取两个传感器，返回 `SensorData` 结构体

```cpp
struct SensorData {
    float temperature;  // 温度 °C
    float humidity;     // 湿度 %
    float light;        // 光照 lux
    bool valid;         // 数据是否有效
};
```

### 5.2 DataManager - 多线程数据管理

**职责：** 管理采集线程，线程安全地共享最新数据，通过回调通知消费者。

**关键设计：**
- `start()` 启动采集线程，按指定间隔循环调用 `reader->readAll()`
- `getLatestData()` 使用mutex保护，任何线程可安全读取最新数据
- `registerCallback()` 注册回调函数，数据更新时自动通知

```cpp
void DataManager::collectLoop()
{
    while (running_) {
        SensorData data;
        if (reader_ && reader_->readAll(data)) {
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                latestData_ = data;
            }
            // 通知所有回调
            for (auto &cb : callbacks_) {
                cb(data);
            }
        }
        usleep(intervalMs_ * 1000);
    }
}
```

### 5.3 MqttPublisher - MQTT发布模块

**职责：** 将传感器数据通过MQTT协议上报到云平台。

**支持三种模式：**

| 模式 | 命令行参数 | 认证方式 |
|------|-----------|----------|
| 本地MQTT | `--mqtt localhost` | 无认证 |
| OneNET | `--onenet-pid/dn/dk` | Token签名(HMAC-SHA1) |
| 阿里云 | `--aliyun-pk/dn/ds` | HMAC-SHA1签名 |

**关键设计：**
- 独立发布线程，与采集线程解耦
- 自动重连机制：连接失败后10秒重试
- 编译时可选：`-DUSE_MOSQUITTO=ON` 启用真实MQTT，否则为stub模式

### 5.4 DisplayManager - Framebuffer显示

**职责：** 在LCD屏幕上显示传感器数据。

**关键设计：**
- 直接操作 `/dev/fb0`，通过mmap映射帧缓冲区
- 内置5x7 ASCII字体引擎（95个可打印字符），无需依赖字体库
- 200ms刷新间隔，避免频繁写入

```cpp
void DisplayManager::updateDisplay(const SensorData &data)
{
    fillRect(0, 0, fbWidth_, fbHeight_, 0x1A1A2E);  // 深蓝背景
    drawText(10, 10, "IoT Gateway", 0x00D4FF);        // 标题
    char buf[64];
    snprintf(buf, sizeof(buf), "Temp: %.1f C", data.temperature);
    drawText(10, 40, buf, 0xFFFFFF);                   // 温度
    snprintf(buf, sizeof(buf), "Humi: %.1f %%", data.humidity);
    drawText(10, 60, buf, 0x00FF88);                   // 湿度
    snprintf(buf, sizeof(buf), "Light: %.1f lux", data.light);
    drawText(10, 80, buf, 0xFFAA00);                   // 光照
}
```

### 5.5 GatewaySDK - 统一API封装

**职责：** 提供简洁的API，隐藏内部实现细节。

```cpp
// 使用示例
iot::GatewayConfig cfg;
cfg.i2cDev = "/dev/i2c-1";
cfg.enableMqtt = true;
cfg.mqtt.onenet.productId = "XUV077XBf9";
cfg.mqtt.onenet.deviceName = "imx6ull_01";
cfg.mqtt.onenet.deviceKey = "UG83cDg...";

iot::GatewaySDK gateway;
gateway.init(cfg);
gateway.onData([](const SensorData &d) {
    printf("T=%.1f H=%.1f L=%.1f\n", d.temperature, d.humidity, d.light);
});
gateway.start();
```

---

## 6. MQTT通信与云平台接入

### 6.1 OneNET平台配置

#### 创建产品和设备

1. 登录 https://open.iot.10086.cn
2. 控制台 → 产品开发 → 创建产品
3. 添加物模型属性：

| 属性标识符 | 数据类型 | 读写类型 | 单位 |
|-----------|----------|---------|------|
| Temperature | float | 只读 | °C |
| Humidity | float | 只读 | % |
| LightIntensity | float | 只读 | lux |

4. 注册设备，获取：产品ID、设备名称、设备密钥

#### 运行网关连接OneNET

```bash
sudo ~/IOT_Gateway/build/iot_gateway \
    --onenet-pid XUV077XBf9 \
    --onenet-dn imx6ull_01 \
    --onenet-dk UG83cDgySktEQWZhNHdLRXQ2WHd6TGRaZUtSdG9CZTI=
```

#### OneNET Token认证原理

OneNET Token由以下部分组成：

```
Token = version=2018-10-31
      & res=URL编码(products/PID/devices/DN)
      & et=过期时间戳
      & method=sha1
      & sign=URL编码(Base64Encode(HMAC-SHA1(Base64Decode(DeviceKey), StringToSign)))

StringToSign = "et\nmethod\nres\nversion"
```

代码实现在 `src/app/onenet_iot.cpp` 的 `generateToken()` 函数中。

#### OneNET数据格式要求

**正确格式**（属性值必须用value包裹）：
```json
{"id":"123","version":"1.0","params":{"Temperature":{"value":25.6},"Humidity":{"value":67.0}}}
```

**错误格式**（直接写数值会导致OneNET显示undefined）：
```json
{"id":"123","version":"1.0","params":{"Temperature":25.6,"Humidity":67.0}}
```

### 6.2 本地MQTT测试

```bash
# 开发板上启动mosquitto
sudo systemctl start mosquitto

# 终端1: 订阅
mosquitto_sub -h localhost -t '/iot/gateway/#' -v

# 终端2: 运行网关
sudo ./iot_gateway --mqtt localhost

# 终端1应收到: /iot/gateway/sensor/data {"id":"...","params":{...}}
```

### 6.3 阿里云IoT配置（备选）

```bash
sudo ./iot_gateway \
    --aliyun-pk a1XXXXXX \
    --aliyun-dn MyDevice \
    --aliyun-ds XXXXXXXX \
    --aliyun-region cn-shanghai
```

---

## 7. LCD显示

### 7.1 验证Framebuffer

```bash
# 检查fb0
ls -la /dev/fb0
fbset

# 测试白色填充
sudo python3 -c "
fb = open('/dev/fb0', 'wb')
pixel = b'\xff\xff\xff\xff'
for i in range(480 * 272):
    fb.write(pixel)
fb.close()
print('Screen should be WHITE')
"
```

### 7.2 启用显示

```bash
sudo chmod 666 /dev/fb0
sudo ./iot_gateway --display
```

### 7.3 触摸屏校准

```bash
sudo ts_calibrate   # 校准
sudo ts_test        # 测试
```

---

## 8. SDK封装

### 8.1 SDK API

```cpp
namespace iot {

class GatewaySDK {
public:
    bool init(const GatewayConfig &cfg);     // 初始化
    bool start();                             // 启动所有线程
    void stop();                              // 停止
    SensorData getLatestData();               // 获取最新数据
    void onData(SensorDataCallback cb);       // 注册回调
    bool readSensors(float &t, float &h, float &l);  // 手动读取
    bool sendToCloud(const std::string &json);        // 发送到云端
    bool isRunning() const;                   // 运行状态
};

}
```

### 8.2 GatewayConfig配置项

```cpp
struct GatewayConfig {
    std::string i2cDev = "/dev/i2c-1";     // I2C总线
    uint8_t sht30Addr = 0x44;               // SHT30地址
    uint8_t bh1750Addr = 0x23;              // BH1750地址
    int collectIntervalMs = 2000;            // 采集间隔(ms)

    bool enableMqtt = false;                 // 启用MQTT
    MqttPublisher::Config mqtt;              // MQTT配置

    bool enableDisplay = false;              // 启用显示
    DisplayManager::Config display;          // 显示配置
};
```

---

## 9. 编译与部署

### 9.1 主机交叉编译（基础版，无MQTT）

```bash
cd /home/lin/Desktop/IOT_Gateway
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-linux-gnueabihf.cmake
make -j4
```

### 9.2 主机交叉编译（带显示）

```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-linux-gnueabihf.cmake -DENABLE_DISPLAY=ON
make -j4
```

### 9.3 开发板本地编译（带MQTT，推荐）

由于交叉编译带MQTT需要从开发板拷贝ARM库且glibc版本可能不兼容，**推荐在开发板上直接编译**：

```bash
# 1. 传输源码到开发板
cd /home/lin/Desktop/IOT_Gateway
tar czf /tmp/iot_gateway_src.tar.gz --exclude='build' --exclude='tools/sysroot' --exclude='*.tar.gz' CMakeLists.txt src/ tools/ scripts/
sshpass -p 'temppwd' scp /tmp/iot_gateway_src.tar.gz debian@192.168.7.2:~/

# 2. 在开发板上编译
ssh debian@192.168.7.2
cd ~ && mkdir -p IOT_Gateway && tar xzf iot_gateway_src.tar.gz -C IOT_Gateway/
cd IOT_Gateway && mkdir build && cd build
cmake .. -DUSE_MOSQUITTO=ON -DENABLE_DISPLAY=ON
make -j4

# 3. 运行
sudo ./iot_gateway --onenet-pid XUV077XBf9 --onenet-dn imx6ull_01 --onenet-dk UG83cDg...
```

### 9.4 编译选项速查

| 选项 | 说明 |
|------|------|
| `-DUSE_MOSQUITTO=ON` | 启用MQTT支持（需libmosquitto-dev） |
| `-DENABLE_DISPLAY=ON` | 启用Framebuffer显示 |
| `-DCMAKE_TOOLCHAIN_FILE=...` | 交叉编译工具链 |

### 9.5 运行命令速查

| 场景 | 命令 |
|------|------|
| 仅传感器采集 | `sudo ./iot_gateway` |
| 带LCD显示 | `sudo ./iot_gateway --display` |
| 带本地MQTT | `sudo ./iot_gateway --mqtt localhost` |
| OneNET云平台 | `sudo ./iot_gateway --onenet-pid PID --onenet-dn DN --onenet-dk DK` |
| 完整功能 | `sudo ./iot_gateway --display --onenet-pid PID --onenet-dn DN --onenet-dk DK` |
| 自定义采集间隔 | `sudo ./iot_gateway --interval 5000` |
| 查看帮助 | `./iot_gateway --help` |

---

## 10. 常见问题FAQ

### Q1: i2cdetect命令找不到？

**A:** 开发板需要安装i2c-tools：`sudo apt install -y i2c-tools`

### Q2: OneNET设备在线但属性值显示undefined？

**A:** 数据格式错误。OneNET要求每个属性值用`{"value": xxx}`包裹：
```json
// 错误: "Temperature":25.6
// 正确: "Temperature":{"value":25.6}
```

### Q3: 交叉编译带MQTT时链接失败（glibc版本不兼容）？

**A:** Linaro 7.5工具链的glibc版本(2.28)可能低于开发板库的要求。解决方案：
1. 在开发板上直接编译（推荐）
2. 使用更新版本的交叉编译工具链

### Q4: 开发板无法访问外网（ping不通OneNET）？

**A:**
```bash
# 设置DNS
echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf
# 设置默认网关（通过USB网络共享主机网络）
sudo route add default gw 192.168.7.1
# 主机端开启IP转发
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -s 192.168.7.0/30 -j MASQUERADE
```

### Q5: /dev/fb0不存在？

**A:** 检查uEnv.txt中LCD overlay是否启用：
```bash
cat /boot/uEnv.txt | grep lcd
# 确保这行未被注释:
# dtoverlay=/usr/lib/linux-image-4.19.35-imx6/imx-fire-lcd.dtbo
```

### Q6: 串口权限不足？

**A:** `sudo usermod -aG dialout $USER`，然后重新登录。临时方案：`sudo chmod 666 /dev/ttyUSB0`

### Q7: SCP传输文件权限被拒？

**A:** 开发板上用sudo拷贝到home目录：
```bash
sudo cp /tmp/file.tar.gz ~/ && sudo chown debian:debian ~/file.tar.gz
```

### Q8: MQTT连接OneNET失败？

**A:**
1. 检查DNS：`ping mqtts.heclouds.com`
2. 检查设备密钥是否完整（含=号）
3. 检查系统时间：`date`（Token依赖时间戳）
4. 同步时间：`sudo ntpdate ntp.aliyun.com`

### Q9: Ubuntu软件源下载慢？

**A:** 更换国内镜像：
```bash
sudo sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list
sudo apt update
```

### Q10: I2C传感器检测不到？

**A:**
1. 确认uEnv.txt中I2C overlay已启用
2. `i2cdetect -y 1` 扫描
3. 检查杜邦线接线（SDA/SCL/VCC/GND）
4. 确认传感器地址（SHT30: 0x44, BH1750: 0x23）

---

## 附录

### A. 参考资料

1. [野火i.MX6ULL快速使用手册](https://doc.embedfire.com/linux/imx6/quick_start/zh/latest/index.html)
2. [OneNET MQTT接入文档](https://open.iot.10086.cn/doc/v5/fuse/detail/912)
3. [Buildroot官方文档](https://buildroot.org/downloads/manual/manual.html)
4. [LVGL官方文档](https://docs.lvgl.io/)
5. [Linux I2C子系统文档](https://www.kernel.org/doc/html/latest/i2c/index.html)

### B. 两阶段开发路径

#### 第一阶段：在现有系统上完成网关功能（✅ 已完成）

| 步骤 | 任务 | 状态 |
|------|------|------|
| 1 | 开发板连接与基础验证 | ✅ |
| 2 | I2C接口使能与传感器验证 | ✅ |
| 3 | C++传感器读取模块开发 | ✅ |
| 4 | 多线程数据管理架构 | ✅ |
| 5 | MQTT通信（OneNET+本地） | ✅ |
| 6 | LCD Framebuffer显示 | ✅ |
| 7 | SDK封装 | ✅ |

#### 第二阶段：从零构建定制Linux系统（待开发）

| 步骤 | 任务 | 说明 |
|------|------|------|
| 1 | 下载Buildroot | 2023.02 LTS |
| 2 | 配置Buildroot | i.MX6ULL目标平台 |
| 3 | 编译系统 | 内核+rootfs+U-Boot |
| 4 | 烧写SD卡 | dd或Etcher |
| 5 | 集成驱动 | 驱动编入内核或模块 |
| 6 | 集成应用 | 网关应用加入rootfs |
| 7 | 系统优化 | 裁剪内核、优化启动 |
| 8 | 烧写eMMC/NAND | 板载存储启动 |

### C. 进阶方向

1. **系统层面**: Buildroot/Yocto构建定制系统
2. **驱动层面**: 中断处理、DMA、设备树深入
3. **网络层面**: TLS加密、OTA升级
4. **应用层面**: LVGL图形界面、Docker容器、边缘计算
