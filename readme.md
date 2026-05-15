# 工业级物联网边缘网关 - 完整开发指南

> 基于野火i.MX6ULL Mini开发板 + SHT30温湿度传感器 + BH1750光照传感器 + OneNET云平台

## 目录

1. [项目概述](#1-项目概述)
2. [开发环境配置](#2-开发环境配置)
3. [核心概念讲解](#3-核心概念讲解)
4. [传感器驱动开发](#4-传感器驱动开发)
5. [内核驱动模块集成](#5-内核驱动模块集成)
6. [应用层开发（C++多线程）](#6-应用层开发c多线程)
7. [MQTT通信与云平台接入](#7-mqtt通信与云平台接入)
8. [LCD显示（Framebuffer + LVGL）](#8-lcd显示framebuffer--lvgl)
9. [SDK封装](#9-sdk封装)
10. [编译与部署](#10-编译与部署)
11. [开发板文件结构](#11-开发板文件结构)
12. [常见问题FAQ](#12-常见问题faq)
13. [附录](#附录)

***

## 1. 项目概述

### 1.1 项目定位

本项目是一个**工业级物联网边缘网关**，基于i.MX6ULL开发板，实现传感器数据采集、本地显示、云端上报的完整链路。

**已完成功能：**

- ✅ SHT30温湿度传感器 + BH1750光照传感器数据采集（含模拟数据模式）
- ✅ C++多线程数据管理架构（采集线程 + 回调机制）
- ✅ MQTT协议连接OneNET云平台（Token签名认证）
- ✅ 内核驱动模块集成（sht30_driver.ko / bh1750_driver.ko，sysfs接口）
- ✅ LVGL v8.3图形界面（仪表盘+进度条+动画+深色主题）
- ✅ Framebuffer LCD显示（内置5x7字体引擎，备选方案）
- ✅ 统一SDK封装（init/start/stop/onData API，条件编译选择显示后端）
- ✅ 交叉编译 + 开发板本地编译双模式
- ✅ 传感器模拟数据模式（I2C故障时自动切换，恢复后自动切回）

**待开发功能：**

- ⏳ Buildroot从零构建定制Linux系统
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
│  │         内核驱动模块 (sht30_driver.ko / bh1750_driver.ko)│  │
│  │    sysfs接口: temperature / humidity / illuminance      │  │
│  ├────────────────────────────────────────────────────────┤  │
│  │         或 SensorReader (i2c-dev用户态)                 │  │
│  │         /dev/i2c-1, 0x44(SHT30), 0x23(BH1750)         │  │
│  └──────┬────────────────────────────────────────────────┘  │
│         │                                                    │
│  ┌──────▼────────────────────────────────────────────────┐  │
│  │              DataManager (多线程数据管理)               │  │
│  │    采集线程(2s) → mutex保护 → 回调通知 → 各消费者       │  │
│  └──┬──────────┬──────────────┬──────────────────────────┘  │
│     │          │              │                              │
│  ┌──▼───┐  ┌──▼───────┐  ┌──▼──────────────────────────┐  │
│  │ 控制台 │  │ LCD显示  │  │    MqttPublisher            │  │
│  │ 输出   │  │ LVGL/FB  │  │  OneNET/阿里云/本地MQTT     │  │
│  └──────┘  └──────────┘  └─────────────────────────────┘  │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │              GatewaySDK (统一API)                       │  │
│  │  init() → start() → stop() → onData() → readSensors() │  │
│  └────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 硬件清单

| 设备         | 型号                  | 作用     | 接口         |
| ---------- | ------------------- | ------ | ---------- |
| 开发板        | EBF6ULL S1 Mini（野火） | 主控     | -          |
| 温湿度传感器     | SHT30               | 采集温湿度  | I2C (0x44) |
| 光照传感器      | BH1750              | 采集光照强度 | I2C (0x23) |
| USB转TTL串口线 | CH340               | 调试串口   | UART       |
| 4.3寸LCD    | RGB触摸屏              | 本地显示   | RGB666     |

### 1.4 实际开发环境信息

| 项目     | 值                                        |
| ------ | ---------------------------------------- |
| 主机OS   | Ubuntu 22.04.4 LTS (x86\_64, VMware)     |
| 主机IP   | 192.168.40.130 (NAT), 192.168.7.1 (USB)  |
| 开发板OS  | Debian 10.13, 内核 4.19.35-imx6            |
| 开发板主机名 | npi                                      |
| 开发板IP  | 192.168.7.2 (USB虚拟网卡)                    |
| 交叉编译器  | Linaro GCC 7.5.0 + Ubuntu arm-gcc 11.4.0 |
| 云平台    | OneNET (中移物联网)                           |

### 1.5 项目文件结构

```
/home/lin/Desktop/IOT_Gateway/
├── CMakeLists.txt                      # CMake构建配置（支持LVGL/Framebuffer切换）
├── readme.md                           # 项目文档
├── CODE_GUIDE.md                       # 代码详解与调试指南
├── .gitignore                          # Git忽略规则
├── drivers/                            # 内核驱动模块（独立Makefile编译）
│   ├── sht30_driver.c                  # SHT30内核驱动（sysfs接口）
│   ├── sht30_overlay.dts               # SHT30设备树覆盖
│   ├── bh1750_driver.c                 # BH1750内核驱动（sysfs接口）
│   ├── bh1750_overlay.dts              # BH1750设备树覆盖
│   └── Makefile                        # 内核模块编译脚本
├── src/
│   ├── app/
│   │   ├── main.cpp                    # 主程序入口（命令行参数解析）
│   │   ├── sensor_reader.h/cpp         # 传感器读取（i2c-dev用户态+模拟模式）
│   │   ├── data_manager.h/cpp          # 多线程数据管理+回调机制
│   │   ├── mqtt_publisher.h/cpp        # MQTT客户端（支持OneNET/阿里云/本地）
│   │   ├── onenet_iot.h/cpp            # OneNET认证（Token签名）
│   │   ├── lvgl_display.h/cpp          # LVGL图形界面显示管理器
│   │   ├── lv_conf.h                   # LVGL配置文件（480x272优化）
│   │   ├── display_manager.h/cpp       # Framebuffer显示（内置5x7字体，备选）
│   │   └── logger.h/cpp                # 日志系统（控制台+文件双输出）
│   └── sdk/
│       ├── gateway_sdk.h               # SDK公共接口
│       └── gateway_sdk.cpp             # SDK实现
├── third_party/
│   └── lvgl/                           # LVGL v8.3 图形库
├── scripts/
│   ├── setup_env.sh                    # 环境配置验证脚本
│   ├── setup_mqtt.sh                   # MQTT配置教程脚本
│   ├── setup_lcd.sh                    # LCD显示配置脚本
│   └── test_sensors.sh                 # 传感器测试脚本
└── tools/
    ├── arm-linux-gnueabihf.cmake       # 交叉编译工具链配置
    └── sysroot/                        # ARM库（从开发板拷贝）
```

***

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

# 安装内核头文件（编译内核模块必需）
sudo apt install -y linux-headers-$(uname -r)
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

***

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

| 传感器    | I2C地址 | 命令        | 数据长度 | 转换公式                                  |
| ------ | ----- | --------- | ---- | ------------------------------------- |
| SHT30  | 0x44  | 0x2C 0x06 | 6字节  | T=-45+175×raw/65535, RH=100×raw/65535 |
| BH1750 | 0x23  | 0x10      | 2字节  | Lux=(high<<8\|low)/1.2                |

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
  mutex保护       │   │ LVGL/FB     │
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
- `LvglDisplay`：LVGL图形界面线程，仪表盘+进度条+动画
- `DisplayManager`：Framebuffer显示线程，5x7字体（备选方案）

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

***

## 4. 传感器驱动开发

### 4.1 两种驱动方式对比

| 方式             | 实现                 | 优点         | 缺点      |
| -------------- | ------------------ | ---------- | ------- |
| **用户态i2c-dev** | open("/dev/i2c-1") | 开发快、无需内核模块 | 性能略低    |
| **内核驱动模块**     | insmod xxx.ko      | 性能高、标准接口   | 需匹配内核版本 |

**本项目两种方式均已实现**，用户态i2c-dev为默认方式，内核驱动模块已集成。

### 4.2 SHT30温湿度传感器

#### 工作原理

SHT30通过I2C通信，单次测量流程：

1. 主设备发送测量命令 `0x2C 0x06`（高精度模式）
2. 等待20ms测量完成
3. 读取6字节数据：\[温度高, 温度低, CRC, 湿度高, 湿度低, CRC]
4. 转换公式：`T = -45 + 175 × raw / 65535`，`RH = 100 × raw / 65535`

#### 用户态读取代码（sensor\_reader.cpp核心片段）

```cpp
bool SensorReader::readSHT30(float &temp, float &humi)
{
    uint8_t cmd[2] = {0x2C, 0x06};
    if (write(fd_sht30_, cmd, 2) != 2)
        return false;

    usleep(20000);

    uint8_t buf[6];
    if (read(fd_sht30_, buf, 6) != 6)
        return false;

    uint16_t raw_temp = (buf[0] << 8) | buf[1];
    uint16_t raw_humi = (buf[3] << 8) | buf[4];
    temp = -45.0f + 175.0f * raw_temp / 65535.0f;
    humi = 100.0f * raw_humi / 65535.0f;
    return true;
}
```

### 4.3 BH1750光照传感器

#### 工作原理

1. 发送连续测量命令 `0x10`（高分辨率，约120ms）
2. 读取2字节数据：\[高8位, 低8位]
3. 转换公式：`Lux = (high << 8 | low) / 1.2`

#### 用户态读取代码

```cpp
bool SensorReader::readBH1750(float &light)
{
    uint8_t cmd = 0x10;
    if (write(fd_bh1750_, &cmd, 1) != 1)
        return false;

    usleep(180000);

    uint8_t buf[2];
    if (read(fd_bh1750_, buf, 2) != 2)
        return false;

    uint16_t raw = (buf[0] << 8) | buf[1];
    light = raw / 1.2f;
    return true;
}
```

***

## 5. 内核驱动模块集成

### 5.1 内核驱动概述

本项目提供了两种内核驱动实现：

| 文件位置 | 接口类型 | 说明 |
|---------|---------|------|
| `drivers/sht30_driver.c` | sysfs属性 | 通过 `/sys/bus/i2c/drivers/sht30/` 读取 |
| `drivers/bh1750_driver.c` | sysfs属性 | 通过 `/sys/bus/i2c/drivers/bh1750/` 读取 |
| `src/driver/sht30_driver.c` | 字符设备 | 通过 `/dev/sht30` 读取 |
| `src/driver/bh1750_driver.c` | 字符设备 | 通过 `/dev/bh1750` 读取 |

**推荐使用 `drivers/` 目录下的 sysfs 版本**，这是 Linux IIO 子系统的标准做法，更符合内核规范。

### 5.2 SHT30 内核驱动（sysfs版本）

驱动位于 `drivers/sht30_driver.c`，主要特点：

- I2C子系统注册，支持设备树匹配 `compatible = "sensirion,sht30"`
- sysfs属性接口：`temperature`（毫摄氏度）、`humidity`（毫百分比）
- mutex保护并发访问
- devm_kzalloc 自动内存管理

#### sysfs属性文件

加载驱动后，创建以下属性文件：

```
/sys/bus/i2c/drivers/sht30/1-0044/temperature   # 温度（毫摄氏度，如 25600 = 25.6°C）
/sys/bus/i2c/drivers/sht30/1-0044/humidity      # 湿度（毫百分比，如 67000 = 67.0%）
```

#### 核心代码

```c
static int sht30_probe(struct i2c_client *client,
                       const struct i2c_device_id *id)
{
    struct sht30_data *data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    data->client = client;
    mutex_init(&data->lock);
    i2c_set_clientdata(client, data);

    ret = sysfs_create_group(&client->dev.kobj, &sht30_attr_group);
    dev_info(&client->dev, "SHT30 driver probed at 0x%02x\n", client->addr);
    return 0;
}

static ssize_t temperature_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct sht30_data *data = dev_get_drvdata(dev);
    int ret = sht30_update_values(data);
    if (ret) return ret;
    return sprintf(buf, "%d\n", data->temperature);
}
static DEVICE_ATTR_RO(temperature);
```

### 5.3 BH1750 内核驱动（sysfs版本）

驱动位于 `drivers/bh1750_driver.c`，主要特点：

- I2C子系统注册，支持设备树匹配 `compatible = "rohm,bh1750"`
- sysfs属性接口：`illuminance`（毫lux）
- mutex保护并发访问

#### sysfs属性文件

```
/sys/bus/i2c/drivers/bh1750/1-0023/illuminance   # 光照（毫lux，如 14200 = 14.2 lux）
```

### 5.4 设备树覆盖（Device Tree Overlay）

每个传感器有独立的设备树覆盖文件：

#### sht30_overlay.dts

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "fsl,imx6ull-14x14-evk";
    fragment@0 {
        target = <&i2c1>;
        __overlay__ {
            status = "okay";
            sht30: sht30@44 {
                compatible = "sensirion,sht30";
                reg = <0x44>;
            };
        };
    };
};
```

#### bh1750_overlay.dts

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "fsl,imx6ull-14x14-evk";
    fragment@0 {
        target = <&i2c1>;
        __overlay__ {
            status = "okay";
            bh1750: bh1750@23 {
                compatible = "rohm,bh1750";
                reg = <0x23>;
            };
        };
    };
};
```

### 5.5 编译与加载内核模块

#### 在开发板上编译

```bash
# 1. 传输驱动源码到开发板
cd /home/lin/Desktop/IOT_Gateway
scp -r drivers/ debian@192.168.7.2:~/iot_drivers/

# 2. 在开发板上编译
ssh debian@192.168.7.2
cd ~/iot_drivers
make KDIR=/lib/modules/$(uname -r)/build

# 3. 加载设备树覆盖和驱动模块
sudo make install
```

#### 交叉编译

```bash
cd /home/lin/Desktop/IOT_Gateway/drivers
make KDIR=/path/to/kernel/source ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
```

#### 验证驱动加载

```bash
# 查看已加载模块
lsmod | grep -E "sht30|bh1750"

# 读取传感器数据
cat /sys/bus/i2c/drivers/sht30/1-0044/temperature
# 输出: 25600 (表示 25.6°C)

cat /sys/bus/i2c/drivers/sht30/1-0044/humidity
# 输出: 67000 (表示 67.0%)

cat /sys/bus/i2c/drivers/bh1750/1-0023/illuminance
# 输出: 14200 (表示 14.2 lux)

# 卸载驱动
cd ~/iot_drivers && sudo make uninstall
```

### 5.6 内核驱动 vs 用户态i2c-dev

| 对比项 | 内核驱动模块 | 用户态i2c-dev |
|-------|-----------|-------------|
| 数据接口 | sysfs属性文件 | /dev/i2c-N |
| 性能 | 高（内核空间直接操作） | 略低（系统调用开销） |
| 标准化 | 符合Linux设备模型 | 简单直接 |
| 热插拔 | 支持设备树自动加载 | 需手动配置 |
| 调试难度 | 较高（需dmesg） | 较低（可直接printf） |
| 适用场景 | 产品级部署 | 开发调试阶段 |

***

## 6. 应用层开发（C++多线程）

### 6.1 SensorReader - 传感器读取模块

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

### 6.2 DataManager - 多线程数据管理

**职责：** 管理采集线程，线程安全地共享最新数据，通过回调通知消费者。

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
            for (auto &cb : callbacks_) {
                cb(data);
            }
        }
        usleep(intervalMs_ * 1000);
    }
}
```

### 6.3 MqttPublisher - MQTT发布模块

**职责：** 将传感器数据通过MQTT协议上报到云平台。

**支持三种模式：**

| 模式     | 命令行参数                | 认证方式               |
| ------ | -------------------- | ------------------ |
| 本地MQTT | `--mqtt localhost`   | 无认证                |
| OneNET | `--onenet-pid/dn/dk` | Token签名(HMAC-SHA1) |
| 阿里云    | `--aliyun-pk/dn/ds`  | HMAC-SHA1签名        |

### 6.4 LvglDisplay - LVGL图形界面显示

**职责：** 使用LVGL v8.3图形库在LCD屏幕上显示仪表盘风格的传感器数据。

**关键设计：**

- 独立线程运行LVGL事件循环（`lv_timer_handler()`）
- Linux Framebuffer后端：mmap映射显存，flush回调写入像素
- 深色主题UI：标题 + 三个数据卡片（温度/湿度/光照）
- 每个卡片包含：标题、数值、单位、进度条
- 动画效果：进度条值变化带动画

### 6.5 DisplayManager - Framebuffer显示（备选）

**职责：** 在LCD屏幕上显示传感器数据（简单文本模式）。

**关键设计：**

- 直接操作 `/dev/fb0`，通过mmap映射帧缓冲区
- 内置5x7 ASCII字体引擎（95个可打印字符），无需依赖字体库
- 200ms刷新间隔，避免频繁写入

### 6.6 GatewaySDK - 统一API封装

**职责：** 提供简洁的API，隐藏内部实现细节。编译时自动选择显示后端。

```cpp
iot::GatewaySDK gateway;
gateway.init(cfg);
gateway.onData([](const SensorData &d) {
    printf("T=%.1f H=%.1f L=%.1f\n", d.temperature, d.humidity, d.light);
});
gateway.start();
```

***

## 7. MQTT通信与云平台接入

### 7.1 OneNET平台配置

#### 创建产品和设备

1. 登录 <https://open.iot.10086.cn>
2. 控制台 → 产品开发 → 创建产品
3. 添加物模型属性：

| 属性标识符          | 数据类型  | 读写类型 | 单位  |
| -------------- | ----- | ---- | --- |
| Temperature    | float | 只读   | °C  |
| Humidity       | float | 只读   | %   |
| LightIntensity | float | 只读   | lux |

4. 注册设备，获取：产品ID、设备名称、设备密钥

#### 运行网关连接OneNET

```bash
sudo ~/IOT_Gateway/build/iot_gateway \
    --onenet-pid XUV077XBf9 \
    --onenet-dn imx6ull_01 \
    --onenet-dk UG83cDgySktEQWZhNHdLRXQ2WHd6TGRaZUtSdG9CZTI=
```

### 7.2 本地MQTT测试

```bash
# 开发板上启动mosquitto
sudo systemctl start mosquitto

# 终端1: 订阅
mosquitto_sub -h localhost -t '/iot/gateway/#' -v

# 终端2: 运行网关
sudo ./iot_gateway --mqtt localhost
```

***

## 8. LCD显示（Framebuffer + LVGL）

### 8.1 显示方案对比

| 特性 | LVGL图形界面 | 简单Framebuffer |
|------|-----------|--------------|
| 编译选项 | `-DUSE_LVGL=ON` | `-DENABLE_DISPLAY=ON` |
| UI风格 | 仪表盘+进度条+动画 | 纯文本（5x7字体） |
| 依赖库 | LVGL v8.3（已集成） | 无外部依赖 |
| 内存占用 | ~2MB（LVGL堆） | 极低 |
| 刷新率 | ~30fps | ~5fps |
| 适用场景 | 产品级UI展示 | 快速验证/极简场景 |

### 8.2 LVGL图形界面

#### UI布局

```
┌──────────────────────────────────────────┐
│              IoT Gateway                  │  ← 标题（青色）
│                                           │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ │
│  │Temperature│ │ Humidity │ │  Light   │ │
│  │          │ │          │ │          │ │
│  │  25.6    │ │  67.0    │ │  14.2    │ │  ← 数值（白色，20号字体）
│  │ Celsius  │ │ Percent  │ │   Lux    │ │  ← 单位（灰色）
│  │ ████████ │ │ ████████ │ │ ██░░░░░░ │ │  ← 进度条（彩色动画）
│  └──────────┘ └──────────┘ └──────────┘ │
│                                           │
│  深色主题: 背景#1A1A2E 卡片#2D2D44         │
│  温度#FF6B6B 湿度#4ECDC4 光照#FFD93D       │
└──────────────────────────────────────────┘
```

#### LVGL配置文件

`src/app/lv_conf.h` 针对 i.MX6ULL 480×272 LCD 优化：

| 配置项 | 值 | 说明 |
|-------|---|------|
| LV_COLOR_DEPTH | 32 | 32位色深（ARGB8888） |
| LV_MEM_SIZE | 2MB | LVGL堆内存大小 |
| LV_FONT_MONTSERRAT_20 | 1 | 启用20号字体 |
| LV_USE_BAR | 1 | 启用进度条控件 |
| LV_USE_ANIM | 1 | 启用动画效果 |
| LV_USE_CHART | 1 | 启用图表控件（预留） |

#### LVGL显示管理器核心代码

```cpp
// Framebuffer flush回调 - 将LVGL渲染结果写入显存
static void lvglDisplayFlushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                                lv_color_t *color_p)
{
    LvglDisplay *disp = static_cast<LvglDisplay *>(drv->user_data);
    uint8_t *fb = static_cast<uint8_t *>(disp->getFbMem());
    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint32_t *fb_line = reinterpret_cast<uint32_t *>(fb + y * disp->getFbLineLen());
        for (int32_t x = area->x1; x <= area->x2; x++) {
            fb_line[x] = lv_color_to32(*color_p);
            color_p++;
        }
    }
    lv_disp_flush_ready(drv);
}

// LVGL事件循环（独立线程）
void LvglDisplay::lvglLoop()
{
    while (running_) {
        if (mgr_) {
            SensorData data = mgr_->getLatestData();
            if (data.valid) updateSensorData(data);
        }
        lv_timer_handler();
        usleep(cfg_.refreshMs * 1000);
    }
}
```

### 8.3 验证Framebuffer

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

### 8.4 启用显示

```bash
# LVGL模式（推荐）
sudo ./iot_gateway

# 简单Framebuffer模式
sudo ./iot_gateway    # 编译时使用 -DENABLE_DISPLAY=ON
```

### 8.5 触摸屏校准

```bash
sudo ts_calibrate   # 校准
sudo ts_test        # 测试
```

***

## 9. SDK封装

### 9.1 SDK API

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

### 9.2 GatewayConfig配置项

```cpp
struct GatewayConfig {
    std::string i2cDev = "/dev/i2c-1";     // I2C总线
    uint8_t sht30Addr = 0x44;               // SHT30地址
    uint8_t bh1750Addr = 0x23;              // BH1750地址
    int collectIntervalMs = 2000;            // 采集间隔(ms)

    bool enableMqtt = false;                 // 启用MQTT
    MqttPublisher::Config mqtt;              // MQTT配置

    bool enableDisplay = false;              // 启用显示
    // USE_LVGL时为 LvglDisplay::Config（含width/height/refreshMs）
    // 默认为 DisplayManager::Config
    decltype(display) display;               // 显示配置
};
```

***

## 10. 编译与部署

### 10.1 主机交叉编译（基础版，无MQTT）

```bash
cd /home/lin/Desktop/IOT_Gateway
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-linux-gnueabihf.cmake
make -j4
```

### 10.2 主机交叉编译（带LVGL显示）

```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-linux-gnueabihf.cmake -DUSE_LVGL=ON
make -j4
```

### 10.3 开发板本地编译（带MQTT + LVGL，推荐）

由于交叉编译带MQTT需要从开发板拷贝ARM库且glibc版本可能不兼容，**推荐在开发板上直接编译**：

```bash
# 1. 传输源码到开发板
cd /home/lin/Desktop/IOT_Gateway
tar czf /tmp/iot_gateway_src.tar.gz --exclude='build' --exclude='tools/sysroot' --exclude='*.tar.gz' CMakeLists.txt src/ tools/ scripts/ third_party/ drivers/
sshpass -p 'temppwd' scp /tmp/iot_gateway_src.tar.gz debian@192.168.7.2:~/

# 2. 在开发板上编译
ssh debian@192.168.7.2
cd ~ && mkdir -p IOT_Gateway && tar xzf iot_gateway_src.tar.gz -C IOT_Gateway/
cd IOT_Gateway && mkdir build && cd build

# LVGL模式
cmake .. -DUSE_MOSQUITTO=ON -DUSE_LVGL=ON
make -j4

# 或简单Framebuffer模式
cmake .. -DUSE_MOSQUITTO=ON -DENABLE_DISPLAY=ON
make -j4

# 3. 运行
sudo ./iot_gateway
```

### 10.4 编译内核驱动模块

```bash
# 在开发板上编译
cd ~/IOT_Gateway/drivers
make KDIR=/lib/modules/$(uname -r)/build

# 加载
sudo make install

# 验证
cat /sys/bus/i2c/drivers/sht30/1-0044/temperature
cat /sys/bus/i2c/drivers/bh1750/1-0023/illuminance
```

### 10.5 编译选项速查

| 选项                           | 说明                          |
| ---------------------------- | --------------------------- |
| `-DUSE_MOSQUITTO=ON`         | 启用MQTT支持（需libmosquitto-dev） |
| `-DUSE_LVGL=ON`              | 启用LVGL图形界面（推荐）             |
| `-DENABLE_DISPLAY=ON`        | 启用简单Framebuffer显示（备选）       |
| `-DCMAKE_TOOLCHAIN_FILE=...` | 交叉编译工具链                     |

> ⚠️ `-DUSE_LVGL=ON` 和 `-DENABLE_DISPLAY=ON` 互斥，优先使用 USE_LVGL。

### 10.6 运行命令速查

| 场景        | 命令                                                                            |
| --------- | ----------------------------------------------------------------------------- |
| 仅传感器采集    | `sudo ./iot_gateway --no-display --no-mqtt`                                   |
| 带LVGL显示   | `sudo ./iot_gateway`                                                          |
| 带本地MQTT   | `sudo ./iot_gateway --mqtt localhost`                                         |
| OneNET云平台 | `sudo ./iot_gateway --onenet-pid PID --onenet-dn DN --onenet-dk DK`           |
| 自定义采集间隔   | `sudo ./iot_gateway --interval 5000`                                          |
| LVGL分辨率覆盖 | `sudo ./iot_gateway --fb-width 800 --fb-height 480`                           |
| 查看帮助      | `./iot_gateway --help`                                                        |

***

## 11. 开发板文件结构

### 11.1 开发板系统概览

开发板运行 Debian 10 (buster)，内核 4.19.35-imx6，通过 USB 虚拟网卡以 192.168.7.2 与主机通信。

```
i.MX6ULL 开发板 (debian@npi)
├── 硬件接口
│   ├── /dev/i2c-0          I2C-0 总线（板载设备：触摸屏0x14、MPU6050 0x68）
│   ├── /dev/i2c-1          I2C-1 总线（传感器：SHT30 0x44、BH1750 0x23）
│   ├── /dev/fb0            Framebuffer（480×272, 32bpp, RGB666 LCD）
│   └── /dev/ttyUSB0        USB串口（调试用）
│
├── 系统设备
│   ├── /sys/bus/i2c/devices/       I2C设备树
│   │   ├── 0-0014  (Goodix 触摸屏)
│   │   ├── 0-0068  (MPU6050 陀螺仪)
│   │   ├── 1-001a  (WM8960 音频编解码)
│   │   ├── 1-0039  (SII902X HDMI发送器)
│   │   ├── 1-0044  (SHT30 温湿度，加载驱动后出现)
│   │   └── 1-0023  (BH1750 光照，加载驱动后出现)
│   ├── /sys/bus/i2c/drivers/sht30/   内核驱动sysfs接口
│   └── /sys/bus/i2c/drivers/bh1750/  内核驱动sysfs接口
│
├── 启动配置
│   └── /boot/uEnv.txt       U-Boot环境变量（含dtbo加载列表）
│
└── 用户文件
    ├── ~/iot_gateway        可执行文件（2.1MB，LVGL+MQTT版本）
    ├── ~/IOT_Gateway/       源码+构建目录
    └── /tmp/iot_gateway.log 运行日志文件
```

### 11.2 用户主目录

```
/home/debian/
├── .bash_history            命令历史
├── .bashrc / .profile       Shell配置
├── .ssh/                    SSH密钥
├── bin/                     系统工具（空）
├── iot_gateway              ★ 可执行文件（LVGL+MQTT版，2.1MB）
└── IOT_Gateway/             源码+构建目录
    ├── CMakeLists.txt       CMake构建配置
    ├── build/
    │   └── iot_gateway      编译产物（630KB，与~/iot_gateway相同）
    ├── drivers/             内核驱动模块源码
    │   ├── sht30_driver.c
    │   ├── sht30_overlay.dts
    │   ├── bh1750_driver.c
    │   ├── bh1750_overlay.dts
    │   └── Makefile
    ├── src/
    │   ├── app/             应用层源码（16个文件）
    │   └── sdk/             SDK源码
    ├── third_party/
    │   └── lvgl/            LVGL v8.3 图形库
    ├── scripts/             辅助脚本
    └── tools/               交叉编译工具链配置
```

### 11.3 关键设备文件

| 设备文件 | 权限 | 所属组 | 说明 |
|---------|------|--------|------|
| `/dev/i2c-0` | crw-rw---- | i2c | I2C-0 总线（板载设备） |
| `/dev/i2c-1` | crw-rw-rw- | i2c | I2C-1 总线（传感器） |
| `/dev/fb0` | crw-rw---- | video | Framebuffer（LCD） |

> ⚠️ 访问 `/dev/i2c-*` 和 `/dev/fb0` 需要 sudo 或加入对应用户组：
> ```bash
> sudo usermod -aG i2c debian
> sudo usermod -aG video debian
> ```

### 11.4 I2C 设备地址映射

```
I2C-0 (i2c-0)                        I2C-1 (i2c-1)
┌──────────────────────┐              ┌──────────────────────┐
│ 0x14 (UU) Goodix触摸 │              │ 0x1a      WM8960音频 │
│ 0x51      EEPROM     │              │ 0x23 (★)  BH1750光照 │
│ 0x59      未知       │              │ 0x39      SII902X HDMI│
│ 0x68 (UU) MPU6050    │              │ 0x44 (★)  SHT30温湿度│
└──────────────────────┘              └──────────────────────┘
  UU = 内核驱动占用                      ★ = 我们的传感器
```

### 11.5 运行日志

程序运行时写入 `/tmp/iot_gateway.log`（行缓冲，崩溃不丢日志）：

```bash
# 查看最新日志
tail -f /tmp/iot_gateway.log

# 查看传感器数据
grep "DataMgr" /tmp/iot_gateway.log | tail -5

# 查看错误
grep "ERROR" /tmp/iot_gateway.log
```

### 11.6 内核驱动加载后的变化

加载 `sht30_driver.ko` + `bh1750_driver.ko` 后：

```bash
# 新增的sysfs属性文件
/sys/bus/i2c/drivers/sht30/1-0044/temperature   # 毫摄氏度
/sys/bus/i2c/drivers/sht30/1-0044/humidity      # 毫百分比
/sys/bus/i2c/drivers/bh1750/1-0023/illuminance  # 毫lux

# 读取示例
cat /sys/bus/i2c/drivers/sht30/1-0044/temperature
# 输出: 21000 (表示 21.0°C)
```

> ⚠️ **注意**：加载内核驱动后，用户态 i2c-dev 访问会冲突（"Resource temporarily unavailable"）。两者二选一使用。

### 11.7 开发板常用命令

```bash
# I2C扫描
sudo i2cdetect -y 1

# 传感器快速测试
sudo i2cget -y 1 0x44 0x2C w    # SHT30

# 查看Framebuffer信息
fbset

# 查看内核日志
dmesg | tail -20

# 查看已加载的内核模块
lsmod | grep -E "sht30|bh1750"

# 运行网关
sudo ~/iot_gateway

# 停止网关
sudo pkill iot_gateway

# 查看实时日志
tail -f /tmp/iot_gateway.log
```

***

## 12. 常见问题FAQ

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

### Q6: 内核模块编译失败（找不到内核头文件）？

**A:** 需要安装与运行内核匹配的头文件：

```bash
sudo apt install -y linux-headers-$(uname -r)
# 如果apt找不到对应版本，需要从内核源码编译
make KDIR=/path/to/kernel/source
```

### Q7: LVGL编译报错找不到lv_conf.h？

**A:** 确保 CMake 使用了正确的 LV_CONF_PATH：

```bash
cmake .. -DUSE_LVGL=ON -DLV_CONF_PATH=/path/to/src/app/lv_conf.h
# 项目已自动配置，通常直接 cmake .. -DUSE_LVGL=ON 即可
```

### Q8: insmod内核模块后dmesg报错"unknown symbol"？

**A:** 内核模块与运行内核版本不匹配。必须在目标开发板上编译，或使用完全相同的内核源码交叉编译。

### Q9: 串口权限不足？

**A:** `sudo usermod -aG dialout $USER`，然后重新登录。临时方案：`sudo chmod 666 /dev/ttyUSB0`

### Q10: I2C传感器检测不到？

**A:**

1. 确认uEnv.txt中I2C overlay已启用
2. `i2cdetect -y 1` 扫描
3. 检查杜邦线接线（SDA/SCL/VCC/GND）
4. 确认传感器地址（SHT30: 0x44, BH1750: 0x23）
5. 如果 i2cdetect 显示总线为空，可能是 I2C 总线锁死（SDA被拉低），尝试重启开发板

### Q11: LVGL界面不更新数据（UI冻结）？

**A:** 必须在 LVGL 渲染循环中调用 `lv_tick_inc(ms)` 推进内部时钟。缺少此调用会导致 `lv_timer_handler()` 不触发重绘，UI 冻结。已在最新版本中修复。

### Q12: 传感器读取报 "Resource temporarily unavailable"？

**A:** I2C 总线被其他进程或内核驱动占用。可能原因：
1. 已加载 sht30_driver.ko / bh1750_driver.ko 内核模块（与用户态 i2c-dev 冲突）
2. 其他进程正在使用 /dev/i2c-1
3. I2C 总线锁死（需要重启开发板恢复）

解决：`sudo rmmod sht30_driver bh1750_driver`，或重启开发板

### Q13: 程序显示 SIMULATED MODE 但传感器已连接？

**A:** 模拟模式会在传感器连续3次读取失败后自动启用。程序每30次读取尝试恢复真实传感器。如果传感器已重新连接，等待约60秒即可自动切回。也可重启程序立即恢复。

***

## 附录

### A. 参考资料

1. [野火i.MX6ULL快速使用手册](https://doc.embedfire.com/linux/imx6/quick_start/zh/latest/index.html)
2. [OneNET MQTT接入文档](https://open.iot.10086.cn/doc/v5/fuse/detail/912)
3. [Buildroot官方文档](https://buildroot.org/downloads/manual/manual.html)
4. [LVGL官方文档](https://docs.lvgl.io/)
5. [Linux I2C子系统文档](https://www.kernel.org/doc/html/latest/i2c/index.html)
6. [Linux Device Tree Overlay文档](https://www.kernel.org/doc/html/latest/devicetree/overlay-notes.html)

### B. 开发路径

#### 第一阶段：在现有系统上完成网关功能（✅ 已完成）

| 步骤 | 任务                | 状态 |
| -- | ----------------- | -- |
| 1  | 开发板连接与基础验证        | ✅  |
| 2  | I2C接口使能与传感器验证     | ✅  |
| 3  | C++传感器读取模块开发      | ✅  |
| 4  | 多线程数据管理架构         | ✅  |
| 5  | MQTT通信（OneNET+本地） | ✅  |
| 6  | LCD Framebuffer显示 | ✅  |
| 7  | SDK封装             | ✅  |

#### 第二阶段：内核驱动与图形界面（✅ 已完成）

| 步骤 | 任务          | 说明               | 状态 |
| -- | ----------- | ---------------- | -- |
| 1  | SHT30内核驱动  | sysfs接口，设备树覆盖    | ✅  |
| 2  | BH1750内核驱动 | sysfs接口，设备树覆盖    | ✅  |
| 3  | LVGL图形界面集成 | 仪表盘+进度条+动画       | ✅  |
| 4  | 编译系统更新      | CMake条件编译LVGL/FB | ✅  |
| 5  | SDK层集成      | GatewaySDK自动选择显示 | ✅  |

#### 第三阶段：从零构建定制Linux系统（待开发）

| 步骤 | 任务          | 说明               |
| -- | ----------- | ---------------- |
| 1  | 下载Buildroot | 2023.02 LTS      |
| 2  | 配置Buildroot | i.MX6ULL目标平台     |
| 3  | 编译系统        | 内核+rootfs+U-Boot |
| 4  | 烧写SD卡       | dd或Etcher        |
| 5  | 集成驱动        | 驱动编入内核或模块        |
| 6  | 集成应用        | 网关应用加入rootfs     |
| 7  | 系统优化        | 裁剪内核、优化启动        |
| 8  | 烧写eMMC/NAND | 板载存储启动           |

### C. 进阶方向

1. **系统层面**: Buildroot/Yocto构建定制系统
2. **驱动层面**: 中断处理、DMA、设备树深入
3. **网络层面**: TLS加密、OTA升级
4. **应用层面**: LVGL图表控件、Docker容器、边缘计算
