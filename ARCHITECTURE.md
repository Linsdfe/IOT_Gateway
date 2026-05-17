# 工业级物联网边缘网关 - 架构与原理

> 本文档详细说明项目的技术架构、核心原理和开发指南。使用教程请参阅 [readme.md](readme.md)。

## 目录

1. [开发环境配置](#1-开发环境配置)
2. [核心概念讲解](#2-核心概念讲解)
3. [动态插件系统](#3-动态插件系统)
4. [传感器驱动开发](#4-传感器驱动开发)
5. [内核驱动模块集成](#5-内核驱动模块集成)
6. [应用层开发（C++多线程）](#6-应用层开发c多线程)
7. [MQTT通信与云平台接入](#7-mqtt通信与云平台接入)
8. [LCD显示（Framebuffer + LVGL）](#8-lcd显示framebuffer--lvgl)
9. [SDK封装](#9-sdk封装)
10. [附录](#附录)

***

## 1. 开发环境配置

### 1.1 主机端环境配置

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

### 1.2 开发板端环境配置

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
```

***

## 2. 核心概念讲解

### 2.1 I2C通信原理

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
| BH1750 | 0x23  | 0x20      | 2字节  | Lux=(high<<8\|low)/1.2                |

### 2.2 Linux I2C用户态编程（i2c-dev）

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

### 2.3 多线程数据管理架构

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

- `SensorReader`：通过 PluginLoader 动态加载 .so 插件，提供 `readAll()` 接口
- `DataManager`：管理采集线程，mutex保护共享数据，回调通知消费者
- `MqttPublisher`：独立发布线程，自动重连，支持OneNET/阿里云/本地MQTT
- `LvglDisplay`：LVGL图形界面线程，仪表盘+进度条+动画
- `DisplayManager`：Framebuffer显示线程，5x7字体（备选方案）

### 2.4 MQTT协议与云平台认证

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

## 3. 动态插件系统

### 3.1 设计理念

传统嵌入式项目中，传感器驱动硬编码在主程序中。更换传感器需要修改源码、重新编译、重新部署整个程序。动态插件系统解决了这个问题：

| 传统方式 | 动态插件方式 |
|---------|-----------|
| 驱动硬编码在主程序 | 驱动编译为独立 .so 文件 |
| 更换传感器需重新编译 | 替换 .so 文件即可切换驱动 |
| 所有驱动占用主程序内存 | 按需加载，节省内存 |
| 无法运行时切换 | 支持热替换，无需重启 |

### 3.2 架构设计

```
┌─────────────────────────────────────────────────────────┐
│                    主程序 (iot_gateway)                   │
│                                                          │
│  SensorReader ──→ PluginLoader ──→ dlopen/dlsym/dlclose │
│       │                                  │               │
│       │         ┌────────────────────────┘               │
│       │         │                                        │
│       │    ┌────▼─────────────────────────────────┐      │
│       │    │         SensorPlugin C 接口           │      │
│       │    │  api_version / name / description     │      │
│       │    │  init() / deinit() / read()           │      │
│       │    │  is_simulated()                       │      │
│       │    └────┬─────────────────────────────────┘      │
│       │         │                                        │
│  ┌────▼─────────▼──────────────────────────────────┐     │
│  │              .so 插件文件                         │     │
│  │                                                   │     │
│  │  libsht30_bh1750_i2c_plugin.so     (I2C硬件)     │     │
│  │  libsht30_bh1750_kernel_plugin.so  (内核驱动)    │     │
│  │  libsht30_bh1750_custom_plugin.so  (自定义驱动)  │     │
│  │  libsimulated_plugin.so             (模拟数据)   │     │
│  └──────────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────┘
```

### 3.3 插件接口规范

每个 .so 插件必须遵循 `sensor_plugin.h` 中定义的纯 C 接口：

```c
typedef struct {
    float temperature;  // 温度（°C）
    float humidity;     // 湿度（%）
    float light;        // 光照（lux）
    bool  valid;        // 数据有效性
} PluginSensorData;

typedef struct {
    int api_version;           // API版本号（必须等于 SENSOR_PLUGIN_API_VERSION）
    const char *name;          // 插件名称
    const char *description;   // 插件描述
    bool (*init)(const char *config);    // 初始化
    void (*deinit)(void);                // 反初始化
    bool (*read)(PluginSensorData *data);// 读取传感器数据
    bool (*is_simulated)(void);          // 是否模拟模式
} SensorPlugin;

extern "C" const SensorPlugin *sensor_plugin_get(void);
```

### 3.4 插件加载流程

```
主程序启动
  │
  ├─ 1. 解析命令行参数 (--plugin / --plugin-dir / --plugin-config)
  │
  ├─ 2. SensorReader::init()
  │     ├─ 如果指定了 --plugin：直接加载该 .so
  │     ├─ 否则扫描 --plugin-dir：加载第一个可用插件
  │     └─ 如果没有找到插件：使用内置模拟模式
  │
  ├─ 3. PluginLoader::load(path)
  │     ├─ dlopen(path, RTLD_NOW)  → 打开 .so 文件
  │     ├─ dlsym("sensor_plugin_get")  → 查找入口函数
  │     ├─ sensor_plugin_get()  → 获取 SensorPlugin 接口
  │     └─ 校验 api_version 是否匹配
  │
  ├─ 4. PluginLoader::init(config)
  │     └─ plugin->init(config)  → 调用插件初始化
  │
  └─ 5. 采集循环中调用 PluginLoader::read(data)
        └─ plugin->read(data)  → 通过插件读取数据
```

### 3.5 热替换机制

运行时替换传感器驱动，无需重启主程序：

```
当前运行: libsht30_bh1750_i2c_plugin.so (I2C硬件驱动)
  │
  ├─ 1. 调用 hotSwapPlugin(newPath, config)
  │
  ├─ 2. 旧插件 deinit()  → 释放I2C资源
  ├─ 3. dlclose()        → 卸载旧 .so
  │
  ├─ 4. dlopen(newPath)  → 加载新 .so
  ├─ 5. 新插件 init()    → 初始化新驱动
  │
  └─ 6. 后续 readAll() 自动使用新插件
```

### 3.6 开发自定义插件

只需 3 步即可开发新插件：

#### 步骤 1：实现插件接口

```cpp
#include "sensor_plugin.h"
#include <cmath>

static bool myInit(const char *config) { return true; }
static void myDeinit(void) {}
static bool myRead(PluginSensorData *data) {
    data->temperature = 25.0f;
    data->humidity = 50.0f;
    data->light = 100.0f;
    data->valid = true;
    return true;
}
static bool myIsSimulated(void) { return false; }

static const SensorPlugin my_plugin = {
    SENSOR_PLUGIN_API_VERSION,
    "my_sensor",
    "My custom sensor driver",
    myInit, myDeinit, myRead, myIsSimulated
};

extern "C" const SensorPlugin *sensor_plugin_get(void) {
    return &my_plugin;
}
```

#### 步骤 2：编译为 .so

```bash
g++ -shared -fPIC -o libmy_sensor_plugin.so my_sensor_plugin.cpp
```

#### 步骤 3：部署到开发板

```bash
scp libmy_sensor_plugin.so debian@192.168.7.2:/usr/lib/iot/plugins/
```

### 3.7 内置插件说明

#### 插件1: sht30_bh1750_i2c（用户空间I2C）

- **驱动方式**：用户态直接操作 `/dev/i2c-1`，通过 `ioctl(I2C_SLAVE)` + `read/write` 发送 I2C 命令
- **I2C 协议层**：应用层（用户空间代码实现完整 I2C 通信协议）
- **自动驱动管理**：init 时自动检测并释放内核驱动占用的 I2C 设备，deinit 时自动恢复标准驱动绑定
- **配置**：`"i2c=/dev/i2c-1;sht30=0x44;bh1750=0x23"`
- **需要内核模块**：❌ 不需要
- **模拟后备**：I2C 连续 3 次读取失败后自动切换模拟数据，每 30 次尝试恢复

#### 插件2: sht30_bh1750_kernel（标准内核驱动）

- **驱动方式**：读取 Linux 内核驱动提供的 sysfs 属性，自动发现接口类型
- **I2C 协议层**：内核层（Linux IIO/HWMON 驱动或自定义内核驱动）
- **接口发现优先级**：
  - SHT3x: IIO → HWMON → 自定义 sysfs（sht30_driver）
  - BH1750: IIO → 自定义 sysfs（bh1750_driver）
- **读取路径**（按优先级自动发现）：
  - SHT3x IIO: `in_temp_input` → 毫摄氏度, `in_humidityrelative_input` → 毫百分比
  - SHT3x HWMON: `temp1_input` → 毫摄氏度, `humidity1_input` → 毫百分比
  - SHT3x 自定义sysfs: `temperature` → 毫摄氏度, `humidity` → 毫百分比
  - BH1750 IIO: `in_illuminance_raw` × `in_illuminance_scale` → lux
  - BH1750 自定义sysfs: `illuminance` → 厘lux（0.01 lux）
- **需要内核模块**：✅ 需要（标准内核驱动或自定义内核驱动，二选一）
- **模拟后备**：驱动不可用或连续 3 次读取失败后自动切换模拟数据

#### 插件3: sht30_bh1750_custom（自定义内核驱动）

- **驱动方式**：读取自行开发的内核驱动模块的 sysfs 属性
- **I2C 协议层**：内核层（自定义内核驱动模块）
- **读取路径**：
  - `/sys/bus/i2c/drivers/sht30/1-0044/temperature` → 毫摄氏度
  - `/sys/bus/i2c/drivers/sht30/1-0044/humidity` → 毫百分比
  - `/sys/bus/i2c/drivers/bh1750_custom/1-0023/illuminance` → 厘lux（0.01 lux）
- **自动驱动管理**：init 时自动解绑标准驱动、注册 I2C 设备到自定义驱动，deinit 时自动恢复标准驱动绑定
- **需要内核模块**：✅ 需要自定义内核驱动模块（sht30_driver.ko + bh1750_driver.ko）

#### 插件4: simulated（模拟数据）

- **驱动方式**：数学函数生成正弦波仿真数据
- **配置**：`"temp=25;humi=60;light=200"`（设置基准值）
- **需要内核模块**：❌ 不需要

> 📖 四种驱动模式对比表请参阅 [readme.md 2.5节](readme.md#25-四种驱动模式对比)

***

## 4. 传感器驱动开发

### 4.1 SHT30温湿度传感器

#### 工作原理

SHT30通过I2C通信，单次测量流程：

1. 主设备发送测量命令 `0x2C 0x06`（高精度模式）
2. 等待20ms测量完成
3. 读取6字节数据：\[温度高, 温度低, CRC, 湿度高, 湿度低, CRC]
4. 转换公式：`T = -45 + 175 × raw / 65535`，`RH = 100 × raw / 65535`

### 4.2 BH1750光照传感器

#### 工作原理

1. 发送单次测量命令 `0x20`（高分辨率，约120ms）
2. 读取2字节数据：\[高8位, 低8位]
3. 转换公式：`Lux = (high << 8 | low) / 1.2`

> ⚠️ BH1750 支持连续测量（0x10）和单次测量（0x20）两种模式。本项目使用**单次测量模式 0x20**，避免连续模式下读取到不完整的测量周期数据导致光照值异常跳变。

***

## 5. 内核驱动模块集成

### 5.1 内核驱动概述

本项目提供了两种内核驱动实现：

| 文件位置 | 接口类型 | 说明 |
|---------|---------|------|
| `drivers/sht30_driver.c` | sysfs属性 | 通过 `/sys/bus/i2c/drivers/sht30/` 读取 |
| `drivers/bh1750_driver.c` | sysfs属性 | 通过 `/sys/bus/i2c/drivers/bh1750_custom/` 读取 |

### 5.2 编译与加载内核模块

#### 编译自研内核驱动

```bash
# 在开发板上编译
cd ~/IOT_Gateway/drivers
make KDIR=/lib/modules/$(uname -r)/build

# 加载驱动
sudo insmod sht30_driver.ko
sudo insmod bh1750_driver.ko

# 注册I2C设备
echo "sht30 0x44" | sudo tee /sys/bus/i2c/devices/i2c-1/new_device
echo "bh1750_custom 0x23" | sudo tee /sys/bus/i2c/devices/i2c-1/new_device

# 验证
cat /sys/bus/i2c/drivers/sht30/1-0044/temperature
cat /sys/bus/i2c/drivers/sht30/1-0044/humidity
cat /sys/bus/i2c/drivers/bh1750_custom/1-0023/illuminance
```

#### 编译标准内核驱动

```bash
# 在开发板上编译（需要内核源码和正确的Module.symvers）
cd /usr/src/linux-source-4.19

# ⚠️ 重要：先编译vmlinux生成正确的CRC值（MODVERSIONS必需）
sudo make vmlinux -j4

# 确保内核配置正确
sudo scripts/config --module CONFIG_SENSORS_SHT3X
sudo scripts/config --module CONFIG_BH1750
sudo scripts/config --module CONFIG_CRC8
sudo make olddefconfig

# 编译模块
sudo make M=lib modules
sudo make M=drivers/hwmon modules
sudo make M=drivers/iio/light modules

# 复制到主目录
cp lib/crc8.ko drivers/hwmon/sht3x.ko drivers/iio/light/bh1750.ko ~/
```

> ⚠️ **MODVERSIONS注意事项**：如果加载内核模块时出现 "disagrees about version of symbol" 错误，说明运行中内核启用了 `CONFIG_MODVERSIONS=y`。推荐改用自定义内核驱动。

### 5.3 内核驱动 vs 动态插件 vs 用户态i2c-dev

| 对比项 | 内核驱动模块 | 动态插件 .so | 用户态i2c-dev |
|-------|-----------|-------------|-------------|
| 数据接口 | sysfs属性文件 | SensorPlugin C接口 | /dev/i2c-N |
| 热替换 | 不支持 | ✅ 支持 | 不支持 |
| 性能 | 高 | 中 | 略低 |
| 标准化 | 符合Linux设备模型 | 项目自定义接口 | 简单直接 |
| 适用场景 | 产品级部署 | 灵活部署/多传感器 | 开发调试阶段 |

***

## 6. 应用层开发（C++多线程）

### 6.1 SensorReader - 传感器读取模块（插件桥接）

**职责：** 通过 PluginLoader 动态加载 .so 插件，提供统一的传感器数据读取接口。

**三级后备策略：**
1. 指定插件路径 → 直接加载
2. 扫描插件目录 → 加载第一个可用插件
3. 无可用插件 → 使用内置模拟数据

**关键方法：**

| 方法 | 说明 |
|------|------|
| `init(pluginDir, pluginName, config)` | 加载并初始化插件 |
| `readAll(data)` | 调用插件的 `read()` 读取传感器数据 |
| `getPluginName()` | 获取当前插件名称 |
| `hotSwapPlugin(path, config)` | 运行时热替换插件 |

### 6.2 DataManager - 多线程数据管理

**职责：** 管理采集线程，线程安全地共享最新数据，通过回调通知消费者。

**线程模型：**

```
DataManager::start()
  └─ 创建采集线程 collectThread()
       ├─ 循环: sensorReader_->readAll()
       ├─ mutex_lock(&mutex_)
       ├─ latestData_ = data
       ├─ mutex_unlock(&mutex_)
       └─ 调用所有回调: callback(data)
```

**关键设计：**
- 使用 `std::mutex` 保护 `latestData_`，避免数据竞争
- 回调机制：消费者通过 `addCallback()` 注册回调，数据更新时自动通知
- 采集间隔通过 `--interval` 参数控制，默认 2000ms

### 6.3 MqttPublisher - MQTT发布模块

**职责：** 将传感器数据通过MQTT协议上报到云平台。

**支持的目标：**

| 目标 | 连接参数 | 数据格式 |
|------|---------|---------|
| OneNET | `--onenet-pid/dn/dk` | OneNET物模型JSON |
| 本地MQTT | `--mqtt localhost` | 简单JSON |

**OneNET认证流程：**
1. 根据 PID/DN/DK 计算 Token（HMAC-SHA1签名）
2. 连接 `mqtts.heclouds.com:1883`，ClientID=DN，Username=PID，Password=Token
3. 发布到 `$sys/{pid}/{dn}/thing/property/post`，订阅 `$sys/{pid}/{dn}/thing/property/post/reply`

**自动重连：** 连接断开后每5秒自动重试

### 6.4 GatewaySDK - 统一API封装

**职责：** 提供简洁的API，隐藏内部实现细节。支持插件热替换。

```cpp
iot::GatewaySDK gateway;
gateway.init(cfg);
gateway.onData([](const SensorData &d) {
    printf("T=%.1f H=%.1f L=%.1f\n", d.temperature, d.humidity, d.light);
});
gateway.start();

// 运行时热替换插件
gateway.hotSwapPlugin("/usr/lib/iot/plugins/libsimulated_plugin.so", "");
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

***

## 9. SDK封装

### 9.1 SDK API

```cpp
namespace iot {

class GatewaySDK {
public:
    bool init(const GatewayConfig &cfg);
    bool start();
    void stop();
    SensorData getLatestData();
    void onData(SensorDataCallback cb);
    bool readSensors(float &t, float &h, float &l);
    bool sendToCloud(const std::string &json);
    bool isRunning() const;
    bool hotSwapPlugin(const std::string &newPath, const std::string &config);
    std::string getPluginName() const;
    std::vector<PluginInfo> listPlugins();
};

}
```

### 9.2 GatewayConfig配置项

```cpp
struct GatewayConfig {
    std::string i2cDev = "/dev/i2c-1";
    uint8_t sht30Addr = 0x44;
    uint8_t bh1750Addr = 0x23;
    int collectIntervalMs = 2000;

    std::string pluginDir = "/usr/lib/iot/plugins";
    std::string pluginPath;
    std::string pluginConfig;

    bool enableMqtt = false;
    MqttPublisher::Config mqtt;

    bool enableDisplay = false;
};
```

***

## 附录

### A. 参考资料

1. [野火i.MX6ULL快速使用手册](https://doc.embedfire.com/linux/imx6/quick_start/zh/latest/index.html)
2. [OneNET MQTT接入文档](https://open.iot.10086.cn/doc/v5/fuse/detail/912)
3. [LVGL官方文档](https://docs.lvgl.io/)
4. [Linux I2C子系统文档](https://www.kernel.org/doc/html/latest/i2c/index.html)
5. [Linux dlopen/dlsym手册](https://man7.org/linux/man-pages/man3/dlopen.3.html)

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

| 步骤 | 任务          | 状态 |
| -- | ----------- | -- |
| 1  | SHT30内核驱动  | ✅  |
| 2  | BH1750内核驱动 | ✅  |
| 3  | LVGL图形界面集成 | ✅  |
| 4  | 编译系统更新      | ✅  |
| 5  | SDK层集成      | ✅  |

#### 第2.5阶段：动态插件系统（✅ 已完成）

| 步骤 | 任务                    | 状态 |
| -- | --------------------- | -- |
| 1  | 定义插件 C 接口规范           | ✅  |
| 2  | 实现 PluginLoader (dlopen) | ✅  |
| 3  | SHT30+BH1750 插件化      | ✅  |
| 4  | 模拟数据插件                | ✅  |
| 5  | SensorReader 重构       | ✅  |
| 6  | SDK/主程序集成             | ✅  |
| 7  | CMake 构建系统更新          | ✅  |
| 8  | 部署脚本更新                | ✅  |

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

1. **插件层面**: MQTT远程热替换、插件配置持久化、插件依赖管理
2. **系统层面**: Buildroot/Yocto构建定制系统
3. **驱动层面**: 中断处理、DMA、设备树深入
4. **网络层面**: TLS加密、OTA升级
5. **应用层面**: LVGL图表控件、Docker容器、边缘计算
