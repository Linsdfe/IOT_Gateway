# IoT Gateway 项目代码详解与调试指南

---

## 1. 项目概述

### 1.1 功能说明

本项目是一个运行在 i.MX6ULL ARM 开发板上的工业物联网边缘网关，实现以下核心功能：

- **传感器数据采集**：通过 I2C 总线读取 SHT30 温湿度传感器和 BH1750 光照传感器（含模拟数据模式）
- **内核驱动模块**：sht30_driver.ko / bh1750_driver.ko，提供 sysfs 标准接口
- **云平台数据上报**：通过 MQTT 协议将传感器数据上报至中国移动 OneNET 物联网平台
- **LVGL图形界面**：使用 LVGL v8.3 在 480×272 LCD 屏幕上显示仪表盘风格 UI（深色主题+动画）
- **简单Framebuffer显示**：备选方案，内置5×7点阵字体，直接操作显存
- **传感器模拟模式**：I2C故障时自动生成正弦波仿真数据，恢复后自动切回真实传感器
- **统一日志系统**：多级别、双输出（控制台+文件）、线程安全的日志记录

### 1.2 技术栈

| 类别 | 技术 | 说明 |
|------|------|------|
| 语言 | C++11 / C (内核驱动) | 兼容 ARM 交叉编译器 |
| 硬件 | i.MX6ULL + SHT30 + BH1750 | ARM Cortex-A7, I2C 传感器 |
| 通信 | I2C (i2c-dev / 内核驱动) | 用户态和内核态双模式 |
| 协议 | MQTT (libmosquitto) | 轻量级物联网消息协议 |
| 云平台 | OneNET | Token 认证 (HMAC-SHA1) |
| 显示 | LVGL v8.3 / Framebuffer | 图形界面 / 简单文本显示 |
| 加密 | OpenSSL | HMAC-SHA1 签名、Base64 编解码 |
| 构建 | CMake + arm-linux-gnueabihf | 交叉编译工具链 |
| 调试 | gdb-multiarch + gdbserver | ARM 远程调试 |

### 1.3 架构设计

```
┌─────────────────────────────────────────────────────────────────┐
│                         main.cpp                                │
│              命令行解析 · 配置构建 · 信号处理 · 生命周期           │
└────────────────────────────┬────────────────────────────────────┘
                             │ init(cfg) / start() / stop()
┌────────────────────────────▼────────────────────────────────────┐
│                    GatewaySDK (门面模式)                         │
│              统一初始化 · 启动/停止顺序 · 子系统协调              │
└───┬──────────────┬──────────────────┬──────────────────┬───────┘
    │              │                  │                  │
┌───▼───┐    ┌────▼────┐       ┌─────▼─────┐     ┌─────▼─────┐
│Sensor │    │  Data   │       │   MQTT    │     │  Display  │
│Reader │───▶│ Manager │──┬───▶│ Publisher │     │ Manager   │
│       │    │ (中枢)  │  │    │           │     │           │
│SHT30  │    │回调分发  │  │    │ OneNET    │  ┌──┤ LVGL/FB  │
│BH1750 │    │线程安全  │  │    │ Token认证  │  │  │ (二选一)   │
└───────┘    └─────────┘  │    └───────────┘  │  └───────────┘
                          │                   │
                     ┌────▼────┐              │
                     │ Logger  │              │
                     │ 日志系统 │              │
                     │ 控制台   │              │
                     │ +文件   │              │
                     └─────────┘              │
                                              │
                          ┌───────────────────┘
                          │
                    ┌─────▼──────┐
                    │ LvglDisplay│  USE_LVGL=ON 时启用
                    │ LVGL v8.3  │  仪表盘+进度条+动画
                    └────────────┘
                    ┌────────────┐
                    │DisplayMgr  │  ENABLE_DISPLAY=ON 时启用
                    │ Framebuf   │  5×7字体 简单文本
                    └────────────┘
```

**数据流**：`SensorReader` → `DataManager`（生产者-消费者）→ 回调分发 → `MqttPublisher` + `LvglDisplay/DisplayManager`

**线程模型**：

| 线程 | 职责 | 启动者 |
|------|------|--------|
| 主线程 | 信号处理、等待停止 | main() |
| 采集线程 | 定时读取传感器、触发回调 | DataManager |
| 发布线程 | 定时获取数据、MQTT 上报 | MqttPublisher |
| 显示线程 | LVGL事件循环/FB刷新 | LvglDisplay/DisplayManager |

---

## 2. 核心模块代码详解

### 2.1 目录结构

```
IOT_Gateway/
├── drivers/                          # 内核驱动模块（独立Makefile编译）
│   ├── sht30_driver.c                # SHT30内核驱动（sysfs接口）
│   ├── sht30_overlay.dts             # SHT30设备树覆盖
│   ├── bh1750_driver.c               # BH1750内核驱动（sysfs接口）
│   ├── bh1750_overlay.dts            # BH1750设备树覆盖
│   └── Makefile                      # 内核模块编译脚本
├── src/
│   ├── app/                          # 应用层模块
│   │   ├── main.cpp                  # 程序入口
│   │   ├── logger.h / logger.cpp     # 日志系统
│   │   ├── sensor_reader.h / .cpp    # 传感器读取（含模拟数据模式）
│   │   ├── data_manager.h / .cpp     # 数据管理
│   │   ├── mqtt_publisher.h / .cpp   # MQTT 发布
│   │   ├── onenet_iot.h / .cpp       # OneNET 认证
│   │   ├── lvgl_display.h / .cpp     # LVGL 图形界面显示
│   │   ├── lv_conf.h                 # LVGL 配置文件
│   │   └── display_manager.h / .cpp  # Framebuffer 显示（备选）
│   └── sdk/
│       └── gateway_sdk.h / .cpp      # 网关 SDK（门面）
├── third_party/
│   └── lvgl/                         # LVGL v8.3 图形库
├── tools/
│   ├── arm-linux-gnueabihf.cmake     # 交叉编译工具链
│   └── sysroot/                      # ARM 库文件
├── scripts/                          # 辅助脚本
└── CMakeLists.txt                    # 构建配置
```

### 2.2 logger — 日志系统

**文件**：`src/app/logger.h` / `logger.cpp`

**设计要点**：单例模式、线程安全、双通道输出、ANSI 颜色自适应

```cpp
LOG_D("Sensor", "raw_temp=%u, raw_humi=%u", raw_temp, raw_humi);
LOG_I("Main", "OneNET IoT: PID=%s DN=%s", pid, dn);
LOG_W("DataMgr", "sensor read failed");
LOG_E("MQTT", "connect to %s:%d failed: %s", host, port, err);
```

**输出格式**：
```
[2026-05-12 23:45:32] [INFO ] [Sensor] init OK (SHT30=0x44, BH1750=0x23)
```

**关键实现逻辑**：

| 机制 | 代码位置 | 说明 |
|------|----------|------|
| 级别过滤 | `log()` 入口 | `if (level < level_) return;` 避免无效格式化开销 |
| 线程安全 | `std::mutex` | `writeLog()` 前加锁，防止多线程输出交错 |
| 即时刷新 | `fflush(stdout)` | 每条日志后立即刷新，确保 VS Code 调试终端可见 |
| 行缓冲文件 | `setvbuf(fp_, nullptr, _IOLBF, 0)` | 日志文件行缓冲，程序崩溃也不丢日志 |
| 颜色自适应 | `isatty(STDOUT_FILENO)` | 检测 stdout 是否为终端，非终端不输出 ANSI 码 |

### 2.3 sensor_reader — I2C 传感器读取

**文件**：`src/app/sensor_reader.h` / `sensor_reader.cpp`

**设计要点**：Linux i2c-dev 用户空间驱动、双传感器独立文件描述符

**核心数据结构**：

```cpp
struct SensorData {
    float temperature;  // 温度（°C），SHT30 量程 -40~125°C
    float humidity;     // 湿度（%），SHT30 量程 0~100%
    float light;        // 光照（lux），BH1750 量程 1~65535 lux
    bool  valid;        // 数据有效性标志
};
```

**I2C 通信流程**：

```cpp
// 1. 打开 I2C 总线并设置从机地址
int fd = open("/dev/i2c-1", O_RDWR);
ioctl(fd, I2C_SLAVE, 0x44);  // SHT30 地址

// 2. SHT30：发送命令 → 等待 → 读取数据
uint8_t cmd[2] = {0x2C, 0x06};  // 单次测量，高重复性
write(fd, cmd, 2);
usleep(20000);                   // 等待 20ms
uint8_t buf[6];
read(fd, buf, 6);               // 读取 6 字节

// 3. 数据转换（SHT30 数据手册公式）
uint16_t raw_temp = (buf[0] << 8) | buf[1];
float temp = -45.0f + 175.0f * raw_temp / 65535.0f;
```

### 2.4 data_manager — 数据管理器

**文件**：`src/app/data_manager.h` / `data_manager.cpp`

**设计要点**：生产者-消费者模型、回调分发、分段 sleep

```cpp
void DataManager::collectLoop() {
    while (running_) {
        SensorData data;
        if (reader_ && reader_->readAll(data)) {
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                latestData_ = data;
            }
            {
                std::lock_guard<std::mutex> lock(cbMutex_);
                for (auto &cb : callbacks_) {
                    cb(data);
                }
            }
        }
        for (int i = 0; i < intervalMs_ / 100 && running_; i++) {
            usleep(100000);
        }
    }
}
```

**设计考量**：

| 决策 | 原因 |
|------|------|
| 回调在采集线程中同步调用 | 避免队列开销，回调应快速返回 |
| 分段 sleep（100ms 一段） | 确保 `stop()` 最多延迟 100ms 生效 |
| `getLatestData()` 返回快照 | 加锁拷贝，消费者无需持锁 |
| 双 mutex（data + callbacks） | 避免数据读写和回调注册互相阻塞 |

### 2.5 onenet_iot — OneNET Token 认证

**文件**：`src/app/onenet_iot.h` / `onenet_iot.cpp`

**设计要点**：HMAC-SHA1 签名、Base64 编解码、URL 编码、匿名命名空间封装

**Token 生成流程**：

```
输入：deviceKey(Base64), productId, deviceName
  │
  ├─1. 构造资源路径：res = "products/{pid}/devices/{dn}"
  ├─2. 计算过期时间：et = time(nullptr) + 86400
  ├─3. 构造待签名字符串：et + "\n" + "sha1" + "\n" + res + "\n" + "2018-10-31"
  ├─4. Base64 解码 deviceKey → rawKey
  ├─5. HMAC-SHA1(rawKey, stringToSign) → sign
  ├─6. Base64 编码 sign → sign_b64
  └─7. URL 编码后拼接：version=2018-10-31&res=...&et=...&method=sha1&sign=...
```

### 2.6 mqtt_publisher — MQTT 发布器

**文件**：`src/app/mqtt_publisher.h` / `mqtt_publisher.cpp`

**设计要点**：自动重连、条件编译（stub 模式）、Onenet 参数自动填充

**条件编译**：定义 `USE_MOSQUITTO` 宏时使用 libmosquitto，否则编译为 stub 模式（仅打印日志）。

### 2.7 lvgl_display — LVGL 图形界面显示管理器

**文件**：`src/app/lvgl_display.h` / `lvgl_display.cpp`

**设计要点**：LVGL v8.3 + Linux Framebuffer 后端、独立线程事件循环、深色主题仪表盘 UI

**初始化流程**：

```cpp
bool LvglDisplay::init(const Config &cfg)
{
    // 1. 初始化 Linux Framebuffer
    initFramebuffer();    // open(/dev/fb0) → ioctl → mmap

    // 2. 初始化 LVGL 库
    initLvgl();           // lv_init() → 创建显示驱动 → 注册flush回调

    // 3. 创建 UI 界面
    createUI();           // 深色背景 + 标题 + 三个数据卡片
}
```

**Framebuffer flush 回调**（LVGL 渲染结果 → 显存）：

```cpp
static void lvglDisplayFlushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                                lv_color_t *color_p)
{
    LvglDisplay *disp = static_cast<LvglDisplay *>(drv->user_data);
    uint8_t *fb = static_cast<uint8_t *>(disp->getFbMem());
    int line_len = disp->getFbLineLen();

    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint32_t *fb_line = reinterpret_cast<uint32_t *>(fb + y * line_len);
        for (int32_t x = area->x1; x <= area->x2; x++) {
            fb_line[x] = lv_color_to32(*color_p);
            color_p++;
        }
    }
    lv_disp_flush_ready(drv);
}
```

**UI 创建**（三个数据卡片）：

```cpp
void LvglDisplay::createUI()
{
    scr_ = lv_scr_act();
    lv_obj_set_style_bg_color(scr_, lv_color_hex(0x1A1A2E), 0);  // 深蓝背景

    // 标题
    lblTitle_ = lv_label_create(scr_);
    lv_label_set_text(lblTitle_, "IoT Gateway");
    lv_obj_set_style_text_color(lblTitle_, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_font(lblTitle_, &lv_font_montserrat_20, 0);

    // 每个卡片：panel + title_label + value_label + unit_label + bar
    create_panel(panelTemp_, lblTempVal_, lblTempUnit_, barTemp_,
                 start_x, "Temperature", 0xFF6B6B);
    create_panel(panelHumi_, lblHumiVal_, lblHumiUnit_, barHumi_,
                 start_x + panel_w + gap, "Humidity", 0x4ECDC4);
    create_panel(panelLight_, lblLightVal_, lblLightUnit_, barLight_,
                 start_x + 2 * (panel_w + gap), "Light", 0xFFD93D);
}
```

**数据更新**（带动画的进度条）：

```cpp
void LvglDisplay::updateSensorData(const SensorData &data)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", data.temperature);
    lv_label_set_text(lblTempVal_, buf);
    lv_label_set_text(lblTempUnit_, "Celsius");
    lv_bar_set_value(barTemp_,
                     (int)((data.temperature + 10) * 100 / 135), LV_ANIM_ON);
}
```

**LVGL 事件循环**（独立线程）：

```cpp
void LvglDisplay::lvglLoop()
{
    while (running_) {
        if (mgr_) {
            SensorData data = mgr_->getLatestData();
            if (data.valid) updateSensorData(data);
        }
        lv_timer_handler();
        usleep(cfg_.refreshMs * 1000);  // 默认33ms ≈ 30fps
    }
}
```

**颜色方案**：

| 元素 | 颜色 | Hex 值 |
|------|------|--------|
| 背景 | 深蓝 | `0x1A1A2E` |
| 卡片背景 | 深灰蓝 | `0x2D2D44` |
| 标题 | 青色 | `0x00D4FF` |
| 温度数值 | 白色 | `0xFFFFFF` |
| 温度标题/进度条 | 红色 | `0xFF6B6B` |
| 湿度标题/进度条 | 青绿 | `0x4ECDC4` |
| 光照标题/进度条 | 金黄 | `0xFFD93D` |

**条件编译设计**：

```cpp
// lvgl_display.h 中根据宏选择类型
#if defined(ENABLE_LVGL) && defined(USE_LVGL)
    lv_disp_drv_t dispDrv_;        // LVGL真实类型
    lv_obj_t *lblTempVal_;         // LVGL对象指针
#else
    void *lblTempVal_;             // stub类型（无LVGL时）
#endif
```

### 2.8 display_manager — Framebuffer 显示管理器（备选）

**文件**：`src/app/display_manager.h` / `display_manager.cpp`

**设计要点**：Framebuffer mmap、内置 5×7 点阵字体、200ms 全屏重绘

当编译时未启用 `USE_LVGL`，使用此简单显示方案。

### 2.9 内核驱动模块

#### drivers/sht30_driver.c（sysfs版本，推荐）

**设计要点**：I2C子系统 + sysfs属性接口 + 设备树匹配

```c
// probe函数：分配设备数据 → 初始化mutex → 创建sysfs属性组
static int sht30_probe(struct i2c_client *client,
                       const struct i2c_device_id *id)
{
    struct sht30_data *data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    data->client = client;
    mutex_init(&data->lock);
    i2c_set_clientdata(client, data);
    ret = sysfs_create_group(&client->dev.kobj, &sht30_attr_group);
    return 0;
}

// sysfs属性：temperature（毫摄氏度）、humidity（毫百分比）
static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RO(humidity);
```

#### drivers/bh1750_driver.c（sysfs版本，推荐）

```c
// sysfs属性：illuminance（毫lux）
static DEVICE_ATTR_RO(illuminance);
```

#### src/driver/ 下的字符设备版本

提供 `/dev/sht30` 和 `/dev/bh1750` 字符设备节点，通过 `cat /dev/sht30` 读取格式化数据。

### 2.10 gateway_sdk — 网关 SDK

**文件**：`src/sdk/gateway_sdk.h` / `gateway_sdk.cpp`

**设计要点**：门面模式、初始化/停止顺序、核心/非核心依赖区分、LVGL条件编译

```cpp
// 编译时根据 USE_LVGL 宏选择显示后端
#if defined(USE_LVGL)
    std::unique_ptr<LvglDisplay> display_;
#else
    std::unique_ptr<DisplayManager> display_;
#endif

// 初始化时自动选择
if (cfg_.enableDisplay) {
#if defined(USE_LVGL)
    display_.reset(new LvglDisplay());
#else
    display_.reset(new DisplayManager());
#endif
}
```

### 2.11 main.cpp — 程序入口

**文件**：`src/app/main.cpp`

**设计要点**：默认启用 OneNET + 显示、命令行参数覆盖、LVGL参数支持

```cpp
// LVGL模式自动配置额外参数
#if defined(USE_LVGL)
    cfg.display.width = 480;
    cfg.display.height = 272;
    cfg.display.refreshMs = 33;
#endif

// LVGL专用命令行参数
#if defined(USE_LVGL)
    } else if (arg == "--fb-width" && i + 1 < argc) {
        cfg.display.width = std::stoi(argv[++i]);
    } else if (arg == "--fb-height" && i + 1 < argc) {
        cfg.display.height = std::stoi(argv[++i]);
#endif
```

---

## 3. 推荐断点设置指南

### 3.1 入门级：理解程序启动流程

| # | 断点位置 | 作用 | 观察重点 |
|---|----------|------|----------|
| B1 | `main.cpp:44` `main()` 入口 | 程序起点 | `argc`、`argv` 参数 |
| B2 | `main.cpp:164` `gateway.init(cfg)` | 子系统初始化 | `cfg` 结构体各字段值 |
| B3 | `main.cpp:177` `gateway.start()` | 子系统启动 | 启动前的状态 |
| B4 | `main.cpp:172` `gateway.onData(lambda)` | 回调注册 | lambda 捕获 |

### 3.2 进阶级：理解 I2C 传感器通信

| # | 断点位置 | 作用 | 观察重点 |
|---|----------|------|----------|
| B5 | `sensor_reader.cpp` `openI2CDev()` | I2C 设备打开 | `addr` 参数、`fd` 返回值 |
| B6 | `sensor_reader.cpp` `sht30SendCmd(0x2C, 0x06)` | SHT30 命令发送 | `write()` 返回值 |
| B7 | `sensor_reader.cpp` `raw_temp = (buf[0]<<8)\|buf[1]` | SHT30 原始数据 | `buf[0..5]` 原始字节 |
| B8 | `sensor_reader.cpp` `temp = -45.0f + ...` | 温度转换 | 转换前后的值 |

### 3.3 高级级：理解 LVGL 显示流程

| # | 断点位置 | 作用 | 观察重点 |
|---|----------|------|----------|
| B9 | `lvgl_display.cpp` `initFramebuffer()` | FB初始化 | `fbWidth_`、`fbHeight_`、`fbMem_` |
| B10 | `lvgl_display.cpp` `initLvgl()` | LVGL初始化 | `dispDrv_`、`lvDisplay_` |
| B11 | `lvgl_display.cpp` `createUI()` | UI创建 | 各控件指针 |
| B12 | `lvgl_display.cpp` `updateSensorData()` | 数据更新 | `data` 值、进度条值 |
| B13 | `lvgl_display.cpp` `lvglDisplayFlushCb()` | 渲染输出 | `area` 范围、像素数据 |

### 3.4 专家级：理解内核驱动

| # | 断点位置（开发板dmesg） | 作用 | 观察重点 |
|---|----------|------|----------|
| B14 | `sht30_probe()` | 驱动探测 | `client->addr`、sysfs创建 |
| B15 | `sht30_update_values()` | I2C读取 | `raw_temp`、`raw_humi` |
| B16 | `bh1750_probe()` | 驱动探测 | `client->addr`、sysfs创建 |

### 3.5 调试技巧速查

| 场景 | GDB 命令 | 说明 |
|------|----------|------|
| 查看结构体 | `print cfg` | 展开所有字段 |
| 查看字符串 | `print cfg.mqtt.onenet.productId.c_str()` | std::string 需调用 c_str() |
| 条件断点 | `break sensor_reader.cpp:122 if raw_temp > 30000` | 仅在特定条件暂停 |
| 查看LVGL对象 | `print *lblTempVal_` | 查看LVGL控件状态 |
| 查看内核日志 | `dmesg \| tail -20` | 内核驱动调试 |
| 查看sysfs属性 | `cat /sys/bus/i2c/drivers/sht30/1-0044/temperature` | 验证内核驱动 |

---

## 4. 代码执行流程分析

### 4.1 完整启动流程

```
main()                                                    [main.cpp:44]
  │
  ├─ Logger::instance().setLevel(DEBUG)                   [main.cpp:46]
  ├─ Logger::instance().setLogFile("/tmp/iot_gateway.log")[main.cpp:47]
  │
  ├─ 构建 GatewayConfig (默认: OneNET+Display)            [main.cpp:62-86]
  ├─ 解析命令行参数覆盖默认值                              [main.cpp:88-150]
  │
  ├─ gateway.init(cfg)                                    [main.cpp:167]
  │   ├─ SensorReader::init()                             [sensor_reader.cpp]
  │   │   ├─ openI2CDev(0x44) → fd_sht30_
  │   │   └─ openI2CDev(0x23) → fd_bh1750_
  │   ├─ new DataManager()
  │   ├─ MqttPublisher::init()
  │   │   └─ OnenetIotConfig::getPassword() → HMAC-SHA1 签名
  │   └─ LvglDisplay::init() / DisplayManager::init()    [条件编译选择]
  │       ├─ initFramebuffer() → open/mmap /dev/fb0
  │       ├─ initLvgl() → lv_init + 显示驱动注册         [LVGL模式]
  │       └─ createUI() → 仪表盘布局                      [LVGL模式]
  │
  ├─ gateway.onData(callback)
  └─ gateway.start()
      ├─ DataManager::start() → 采集线程启动
      ├─ MqttPublisher::start() → 发布线程启动
      └─ LvglDisplay::start() / DisplayManager::start() → 显示线程启动
```

### 4.2 运行时数据流（每 2 秒循环）

```
[采集线程] DataManager::collectLoop()
  │
  ├─ SensorReader::readAll(data)
  │   ├─ readSHT30() → I2C通信 → 温度/湿度
  │   └─ readBH1750() → I2C通信 → 光照
  │
  ├─ latestData_ = data  (加锁)
  │
  ├─ 回调通知 ─────────────────────┬──────────────────────┐
  │                                │                      │
  │   [发布线程]                    │   [显示线程]          │
  │   MqttPublisher::publishLoop() │   LvglDisplay::      │
  │   ├─ getLatestData()           │   lvglLoop()         │
  │   ├─ buildPayload(data)        │   ├─ getLatestData() │
  │   └─ mosquitto_publish()       │   ├─ updateSensorData()
  │                                │   └─ lv_timer_handler()
  │                                │                      │
  │   [备选显示线程]                │                      │
  │                                │   DisplayManager::   │
  │                                │   displayLoop()      │
  │                                │   ├─ getLatestData() │
  │                                │   └─ updateDisplay() │
  └─ sleep(intervalMs) ───────────┴──────────────────────┘
```

### 4.3 LVGL 渲染流程

```
LvglDisplay::lvglLoop()                   [lvgl_display.cpp]
  │
  ├─ lv_tick_inc(33)                       ← 推进LVGL内部时钟（关键！）
  │                                         缺少此调用会导致UI冻结
  ├─ lastDataUpdate += 33
  ├─ if (lastDataUpdate >= 2000)           ← 每2秒更新一次数据
  │   ├─ mgr_->getLatestData() → data
  │   └─ updateSensorData(data)
  │       ├─ lv_label_set_text(lblTempVal_, "25.6")
  │       ├─ lv_bar_set_value(barTemp_, 26, LV_ANIM_ON)
  │       └─ ... (湿度、光照同理)
  │
  ├─ lv_timer_handler()                    [LVGL内部]
  │   ├─ 检查脏区域 → 触发渲染
  │   ├─ LVGL渲染引擎绘制到draw_buf
  │   └─ 调用 flush_cb → lvglDisplayFlushCb()
  │       ├─ 遍历area内像素
  │       ├─ lv_color_to32() 转换颜色格式
  │       ├─ 写入mmap后的Framebuffer内存
  │       └─ lv_disp_flush_ready() 通知LVGL完成
  │
  └─ usleep(33ms) → ~30fps
```

### 4.4 内核驱动加载流程

```
# 开发板上执行
make install                              [drivers/Makefile]
  │
  ├─ insmod sht30_driver.ko               # 加载SHT30驱动模块
  │   └─ sht30_probe()                    # I2C子系统调用
  │       ├─ devm_kzalloc() → 分配设备数据
  │       ├─ mutex_init() → 初始化互斥锁
  │       ├─ sysfs_create_group() → 创建属性文件
  │       └─ dev_info() → 打印probe成功日志
  │
  ├─ insmod bh1750_driver.ko              # 加载BH1750驱动模块
  │   └─ bh1750_probe()                   # 同上
  │
  ├─ 加载 sht30_overlay.dtbo              # 设备树覆盖
  │   └─ 内核解析 → 在i2c1总线上创建sht30@44设备
  │       └─ 触发 sht30_driver probe
  │
  └─ 加载 bh1750_overlay.dtbo             # 设备树覆盖
      └─ 内核解析 → 在i2c1总线上创建bh1750@23设备
          └─ 触发 bh1750_driver probe
```

---

## 5. 开发板文件结构

### 5.1 系统概览

开发板运行 Debian 10 (buster)，内核 4.19.35-imx6，通过 USB 虚拟网卡 (192.168.7.2) 与主机通信。

```
i.MX6ULL 开发板 (debian@npi)
├── /dev/                          设备文件
│   ├── i2c-0                     I2C-0 总线（板载：触摸屏、MPU6050）
│   ├── i2c-1                     I2C-1 总线（传感器：SHT30、BH1750）
│   └── fb0                       Framebuffer（480×272 LCD）
│
├── /sys/bus/i2c/                  内核I2C子系统
│   ├── devices/                   已注册的I2C设备
│   │   ├── 0-0014  Goodix触摸屏
│   │   ├── 0-0068  MPU6050陀螺仪
│   │   ├── 1-001a  WM8960音频
│   │   ├── 1-0039  SII902X HDMI
│   │   ├── 1-0044  SHT30（加载驱动后）
│   │   └── 1-0023  BH1750（加载驱动后）
│   └── drivers/                   内核驱动sysfs接口
│
├── /boot/uEnv.txt                 U-Boot启动配置（dtbo加载列表）
│
├── /home/debian/                  用户主目录
│   ├── iot_gateway                ★ 可执行文件（LVGL+MQTT版）
│   ├── IOT_Gateway/               源码+构建目录
│   │   ├── build/iot_gateway      编译产物
│   │   ├── drivers/               内核驱动源码
│   │   ├── src/                   应用源码
│   │   ├── third_party/lvgl/      LVGL库
│   │   └── ...
│   └── bin/                       系统工具
│
└── /tmp/iot_gateway.log           运行日志（行缓冲）
```

### 5.2 I2C 设备地址映射

```
I2C-0 (/dev/i2c-0)                   I2C-1 (/dev/i2c-1)
┌────────────────────────┐           ┌────────────────────────┐
│ 0x14 (UU) Goodix 触摸屏 │           │ 0x1a      WM8960 音频  │
│ 0x51      EEPROM       │           │ 0x23 (★)  BH1750 光照  │
│ 0x59      未知         │           │ 0x39      SII902X HDMI │
│ 0x68 (UU) MPU6050 陀螺 │           │ 0x44 (★)  SHT30 温湿度 │
└────────────────────────┘           └────────────────────────┘
  UU = 内核驱动占用                     ★ = 我们的传感器
```

### 5.3 关键路径速查

| 路径 | 说明 | 用法 |
|------|------|------|
| `~/iot_gateway` | 可执行文件 | `sudo ~/iot_gateway` |
| `/tmp/iot_gateway.log` | 运行日志 | `tail -f /tmp/iot_gateway.log` |
| `/dev/i2c-1` | I2C-1 总线 | `i2cdetect -y 1` |
| `/dev/fb0` | Framebuffer | `fbset` |
| `/boot/uEnv.txt` | 启动配置 | `cat /boot/uEnv.txt` |
| `~/IOT_Gateway/drivers/` | 内核驱动源码 | `cd ~/IOT_Gateway/drivers && make` |
| `~/IOT_Gateway/build/` | 编译目录 | `cmake .. && make` |

---

## 附录：CMakeLists.txt 构建配置说明

```cmake
# LVGL条件编译
option(USE_LVGL "Enable LVGL graphical interface" OFF)

if(USE_LVGL)
    # 编译LVGL显示管理器
    list(APPEND SOURCES src/app/lvgl_display.cpp)
    target_compile_definitions(... PRIVATE ENABLE_LVGL USE_LVGL ENABLE_DISPLAY)
    # 设置LVGL配置文件路径
    set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/src/app/lv_conf.h" CACHE FILEPATH FORCE)
    # 添加LVGL子目录
    add_subdirectory(third_party/lvgl)
    target_link_libraries(... PRIVATE lvgl)
    # 将lv_conf.h目录加入LVGL的包含路径
    target_include_directories(lvgl PUBLIC ${CMAKE_SOURCE_DIR}/src/app)
else()
    # 编译简单Framebuffer显示
    list(APPEND SOURCES src/app/display_manager.cpp)
endif()
```

**编译命令**：

```bash
# ARM 交叉编译 + LVGL
cmake -B build-debug \
    -DCMAKE_TOOLCHAIN_FILE=tools/arm-linux-gnueabihf.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DUSE_MOSQUITTO=ON \
    -DUSE_LVGL=ON

# 开发板本地编译 + LVGL
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_MOSQUITTO=ON \
    -DUSE_LVGL=ON

# 开发板本地编译 + 简单Framebuffer
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_MOSQUITTO=ON \
    -DENABLE_DISPLAY=ON

# 编译内核驱动模块（在开发板上）
cd drivers && make KDIR=/lib/modules/$(uname -r)/build
```
