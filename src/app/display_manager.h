/**
 * @file display_manager.h
 * @brief Framebuffer 显示管理器 - LCD 屏幕传感器数据实时显示
 *
 * 通过 Linux Framebuffer (fb) 接口直接操作显存，在 LCD 屏幕上
 * 绘制传感器数据。使用内置 5×7 点阵字体，无需外部字体库依赖。
 *
 * 显示布局（480×272 屏幕）：
 * ┌────────────────────────────┐
 * │ IoT Gateway                │  标题（青色）
 * │ Temp: 25.6 C               │  温度（白色）
 * │ Humi: 67.0 %               │  湿度（绿色）
 * │ Light: 14.2 lux            │  光照（橙色）
 * └────────────────────────────┘
 *
 * 刷新策略：200ms 间隔全屏重绘，简单可靠。
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "data_manager.h"
#include <string>
#include <thread>
#include <cstdint>

/**
 * @brief Framebuffer 显示管理器
 *
 * 使用 mmap 映射 Framebuffer 设备到用户空间，直接写入像素数据。
 * 支持 32bpp 色深（ARGB8888 格式）。
 */
class DisplayManager {
public:
    /**
     * @brief 显示配置结构
     */
    struct Config {
        bool enabled = false;          ///< 是否启用显示
        std::string fbDevice;          ///< Framebuffer 设备路径，如 "/dev/fb0"
    };

    DisplayManager();
    ~DisplayManager();

    /**
     * @brief 初始化 Framebuffer
     * @param cfg 显示配置
     * @return true 初始化成功
     *
     * 流程：打开 fb 设备 → 获取屏幕参数 → mmap 映射显存
     */
    bool init(const Config &cfg);

    /**
     * @brief 启动显示刷新线程
     * @param mgr DataManager 指针，用于获取最新传感器数据
     * @return true 启动成功
     */
    bool start(DataManager *mgr);

    /** @brief 停止显示线程并释放 Framebuffer 资源 */
    void stop();

private:
    /**
     * @brief 填充矩形区域
     * @param x     起始 X 坐标
     * @param y     起始 Y 坐标
     * @param w     宽度
     * @param h     高度
     * @param color 填充颜色（32位 ARGB）
     */
    void fillRect(int x, int y, int w, int h, uint32_t color);

    /**
     * @brief 绘制文本字符串
     * @param x     起始 X 坐标
     * @param y     起始 Y 坐标
     * @param text  要绘制的文本（仅支持 ASCII 32~127）
     * @param color 文字颜色（32位 ARGB）
     *
     * 使用内置 5×7 点阵字体，每个字符占 6×8 像素（含 1 像素间距）。
     */
    void drawText(int x, int y, const char *text, uint32_t color);

    /**
     * @brief 更新屏幕显示内容
     * @param data 最新传感器数据
     *
     * 先清屏（深蓝色背景），再绘制标题和三个传感器数值。
     */
    void updateDisplay(const SensorData &data);

    /** @brief 显示刷新线程主循环 */
    void displayLoop();

    DataManager *mgr_;              ///< 数据管理器指针
    volatile bool running_;         ///< 线程运行标志
    Config cfg_;                    ///< 显示配置
    int fbFd_;                      ///< Framebuffer 文件描述符
    void *fbMem_;                   ///< mmap 映射的显存地址
    int fbWidth_;                   ///< 屏幕宽度（像素）
    int fbHeight_;                  ///< 屏幕高度（像素）
    int fbBpp_;                     ///< 色深（位/像素）
    int fbLineLen_;                 ///< 行字节长度
    std::thread displayThread_;     ///< 显示刷新线程
};

#endif
