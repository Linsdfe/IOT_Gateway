/**
 * @file lvgl_display.h
 * @brief LVGL 图形界面显示管理器 - 替代简单 Framebuffer
 *
 * 使用 LVGL v8.3 图形库，通过 Linux Framebuffer 后端驱动 LCD 屏幕。
 * 提供比简单 Framebuffer 更丰富的 UI：
 * - 仪表盘风格温度/湿度/光照显示
 * - 实时数据进度条（带动画效果）
 * - 深色主题（背景#1A1A2E，卡片#2D2D44）
 *
 * LVGL 渲染流程：
 * 1. lvglLoop() 线程每33ms调用 lv_tick_inc() + lv_timer_handler()
 * 2. LVGL 检测脏区域，渲染到 draw_buf
 * 3. 调用 lvglDisplayFlushCb() 将像素写入 Framebuffer
 * 4. LCD 控制器自动扫描 Framebuffer 显示到屏幕
 *
 * 条件编译：
 * - USE_LVGL 宏启用时使用 LVGL 真实类型
 * - 否则使用 void* stub 类型（编译通过但不运行）
 */

#ifndef LVGL_DISPLAY_H
#define LVGL_DISPLAY_H

#include "data_manager.h"
#include <string>
#include <thread>
#include <cstdint>
#include <atomic>

#if defined(ENABLE_LVGL) && defined(USE_LVGL)
#include "lvgl.h"
#endif

/**
 * @brief LVGL 图形界面显示管理器
 *
 * 管理 LVGL 初始化、UI 创建、数据更新和渲染循环。
 * 独立线程运行，通过 DataManager 获取最新传感器数据。
 */
class LvglDisplay {
public:
    /**
     * @brief 显示配置结构
     */
    struct Config {
        bool enabled = false;        ///< 是否启用 LVGL 显示
        std::string fbDevice;        ///< Framebuffer 设备路径，如 "/dev/fb0"
        int width = 480;             ///< 屏幕宽度（像素），用于 LVGL 显示驱动
        int height = 272;            ///< 屏幕高度（像素），用于 LVGL 显示驱动
        int refreshMs = 33;          ///< 刷新间隔（毫秒），33ms ≈ 30fps
    };

    LvglDisplay();
    ~LvglDisplay();

    /**
     * @brief 初始化 Framebuffer 和 LVGL
     * @param cfg 显示配置
     * @return true 初始化成功
     *
     * 流程：initFramebuffer() → initLvgl() → createUI()
     */
    bool init(const Config &cfg);

    /**
     * @brief 启动 LVGL 渲染线程
     * @param mgr DataManager 指针，用于获取最新传感器数据
     * @return true 启动成功
     */
    bool start(DataManager *mgr);

    /** @brief 停止渲染线程并释放资源 */
    void stop();

    /** @brief 获取 Framebuffer mmap 地址（flush 回调使用） */
    void *getFbMem() const { return fbMem_; }

    /** @brief 获取屏幕宽度（flush 回调使用） */
    int getFbWidth() const { return fbWidth_; }

    /** @brief 获取屏幕高度（flush 回调使用） */
    int getFbHeight() const { return fbHeight_; }

    /** @brief 获取 Framebuffer 行字节长度（flush 回调使用） */
    int getFbLineLen() const { return fbLineLen_; }

private:
    /** @brief LVGL 渲染线程主循环 */
    void lvglLoop();

    /** @brief 初始化 LVGL 库和显示驱动 */
    bool initLvgl();

    /** @brief 初始化 Linux Framebuffer（open/ioctl/mmap） */
    bool initFramebuffer();

    /** @brief 创建 UI 界面（标题 + 三个数据卡片） */
    void createUI();

    /**
     * @brief 更新传感器数据到 UI 控件
     * @param data 最新传感器数据
     *
     * 更新标签文本和进度条值（带动画）。
     * 温度进度条范围：-10°C~125°C → 0~100%
     * 湿度进度条范围：0~100% → 0~100
     * 光照进度条范围：0~1000 lux → 0~100
     */
    void updateSensorData(const SensorData &data);

    Config cfg_;                    ///< 显示配置
    DataManager *mgr_;              ///< 数据管理器指针
    std::atomic<bool> running_;     ///< 线程运行标志（原子变量）
    std::thread lvglThread_;        ///< LVGL 渲染线程

    int fbFd_;                      ///< Framebuffer 文件描述符
    void *fbMem_;                   ///< mmap 映射的显存地址
    int fbWidth_;                   ///< 屏幕宽度（像素）
    int fbHeight_;                  ///< 屏幕高度（像素）
    int fbBpp_;                     ///< 色深（位/像素）
    int fbLineLen_;                 ///< 行字节长度（从 finfo.line_length 获取）

#if defined(ENABLE_LVGL) && defined(USE_LVGL)
    lv_disp_drv_t dispDrv_;        ///< LVGL 显示驱动
    lv_disp_draw_buf_t drawBuf_;   ///< LVGL 绘图缓冲区
    lv_disp_t *lvDisplay_;         ///< LVGL 显示对象

    lv_obj_t *scr_;                ///< 当前屏幕对象
    lv_obj_t *panelTemp_;          ///< 温度卡片面板
    lv_obj_t *panelHumi_;          ///< 湿度卡片面板
    lv_obj_t *panelLight_;         ///< 光照卡片面板
    lv_obj_t *lblTempVal_;         ///< 温度数值标签
    lv_obj_t *lblHumiVal_;         ///< 湿度数值标签
    lv_obj_t *lblLightVal_;        ///< 光照数值标签
    lv_obj_t *lblTempUnit_;        ///< 温度单位标签
    lv_obj_t *lblHumiUnit_;        ///< 湿度单位标签
    lv_obj_t *lblLightUnit_;       ///< 光照单位标签
    lv_obj_t *lblTitle_;           ///< 标题标签
    lv_obj_t *barTemp_;            ///< 温度进度条
    lv_obj_t *barHumi_;            ///< 湿度进度条
    lv_obj_t *barLight_;           ///< 光照进度条
#else
    void *lvDisplay_;
    void *scr_;
    void *panelTemp_;
    void *panelHumi_;
    void *panelLight_;
    void *lblTempVal_;
    void *lblHumiVal_;
    void *lblLightVal_;
    void *lblTempUnit_;
    void *lblHumiUnit_;
    void *lblLightUnit_;
    void *lblTitle_;
    void *barTemp_;
    void *barHumi_;
    void *barLight_;
#endif
};

#endif
