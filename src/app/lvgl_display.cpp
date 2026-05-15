/**
 * @file lvgl_display.cpp
 * @brief LVGL 图形界面显示管理器实现
 *
 * 使用 LVGL v8.3 + Linux Framebuffer 驱动。
 * UI 布局：顶部标题 + 三个数据卡片（温度/湿度/光照）。
 *
 * 关键设计：
 * - flush 回调：LVGL 渲染完成后调用，将 draw_buf 中的像素写入 Framebuffer
 * - lv_tick_inc()：必须在每次循环调用，推进 LVGL 内部时钟
 *   缺少此调用会导致 lv_timer_handler() 不触发重绘，UI 冻结
 * - 数据更新节流：每2秒从 DataManager 获取数据并更新 UI，
 *   避免每帧都读取（传感器数据本身2秒更新一次）
 */

#include "lvgl_display.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <cstring>
#include <cstdio>
#include <ctime>

#if defined(ENABLE_LVGL) && defined(USE_LVGL)

static void lvglDisplayFlushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                                lv_color_t *color_p)
{
    LvglDisplay *disp = static_cast<LvglDisplay *>(drv->user_data);
    if (!disp || !disp->getFbMem()) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t w = area->x2 - area->x1 + 1;
    int fb_w = disp->getFbWidth();
    int fb_h = disp->getFbHeight();
    int line_len = disp->getFbLineLen();
    uint8_t *fb = static_cast<uint8_t *>(disp->getFbMem());

    for (int32_t y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= fb_h) {
            color_p += w;
            continue;
        }
        uint32_t *fb_line = reinterpret_cast<uint32_t *>(fb + y * line_len);
        for (int32_t x = area->x1; x <= area->x2; x++) {
            if (x >= 0 && x < fb_w) {
                fb_line[x] = lv_color_to32(*color_p);
            }
            color_p++;
        }
    }

    lv_disp_flush_ready(drv);
}

#endif

LvglDisplay::LvglDisplay()
    : mgr_(nullptr)
    , running_(false)
    , fbFd_(-1)
    , fbMem_(nullptr)
    , fbWidth_(0)
    , fbHeight_(0)
    , fbBpp_(0)
    , fbLineLen_(0)
#if defined(ENABLE_LVGL) && defined(USE_LVGL)
    , lvDisplay_(nullptr)
    , scr_(nullptr)
    , panelTemp_(nullptr)
    , panelHumi_(nullptr)
    , panelLight_(nullptr)
    , lblTempVal_(nullptr)
    , lblHumiVal_(nullptr)
    , lblLightVal_(nullptr)
    , lblTempUnit_(nullptr)
    , lblHumiUnit_(nullptr)
    , lblLightUnit_(nullptr)
    , lblTitle_(nullptr)
    , barTemp_(nullptr)
    , barHumi_(nullptr)
    , barLight_(nullptr)
#else
    , lvDisplay_(nullptr)
    , scr_(nullptr)
    , panelTemp_(nullptr)
    , panelHumi_(nullptr)
    , panelLight_(nullptr)
    , lblTempVal_(nullptr)
    , lblHumiVal_(nullptr)
    , lblLightVal_(nullptr)
    , lblTempUnit_(nullptr)
    , lblHumiUnit_(nullptr)
    , lblLightUnit_(nullptr)
    , lblTitle_(nullptr)
    , barTemp_(nullptr)
    , barHumi_(nullptr)
    , barLight_(nullptr)
#endif
{
}

LvglDisplay::~LvglDisplay()
{
    stop();
}

bool LvglDisplay::init(const Config &cfg)
{
    cfg_ = cfg;
    if (!cfg_.enabled) {
        LOG_I("LVGL", "disabled, skip init");
        return true;
    }

    if (!initFramebuffer()) {
        LOG_E("LVGL", "framebuffer init failed");
        return false;
    }

#if defined(ENABLE_LVGL) && defined(USE_LVGL)
    if (!initLvgl()) {
        LOG_E("LVGL", "lvgl init failed");
        return false;
    }

    createUI();

    LOG_I("LVGL", "init OK (%dx%d, line_len=%d)", fbWidth_, fbHeight_, fbLineLen_);
    return true;
#else
    LOG_W("LVGL", "compiled without LVGL support, using stub");
    return true;
#endif
}

bool LvglDisplay::start(DataManager *mgr)
{
    if (!cfg_.enabled) return true;
    mgr_ = mgr;
    running_ = true;
    lvglThread_ = std::thread(&LvglDisplay::lvglLoop, this);
    LOG_I("LVGL", "started");
    return true;
}

void LvglDisplay::stop()
{
    running_ = false;
    if (lvglThread_.joinable())
        lvglThread_.join();
    if (fbMem_) {
        munmap(fbMem_, fbWidth_ * fbHeight_ * fbBpp_ / 8);
        fbMem_ = nullptr;
    }
    if (fbFd_ >= 0) {
        close(fbFd_);
        fbFd_ = -1;
    }
    LOG_I("LVGL", "stopped");
}

bool LvglDisplay::initFramebuffer()
{
    fbFd_ = open(cfg_.fbDevice.c_str(), O_RDWR);
    if (fbFd_ < 0) {
        LOG_E("LVGL", "open %s failed", cfg_.fbDevice.c_str());
        return false;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fbFd_, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        LOG_E("LVGL", "get vinfo failed");
        close(fbFd_);
        fbFd_ = -1;
        return false;
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(fbFd_, FBIOGET_FSCREENINFO, &finfo) < 0) {
        LOG_E("LVGL", "get finfo failed");
        close(fbFd_);
        fbFd_ = -1;
        return false;
    }

    fbWidth_ = vinfo.xres;
    fbHeight_ = vinfo.yres;
    fbBpp_ = vinfo.bits_per_pixel;
    fbLineLen_ = finfo.line_length;

    LOG_I("LVGL", "fb info: %dx%d %dbpp line_len=%d (calc=%d)",
          fbWidth_, fbHeight_, fbBpp_, fbLineLen_,
          fbWidth_ * fbBpp_ / 8);

    size_t screensize = fbLineLen_ * fbHeight_;
    fbMem_ = mmap(nullptr, screensize, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fbFd_, 0);
    if (fbMem_ == MAP_FAILED) {
        LOG_E("LVGL", "mmap failed");
        close(fbFd_);
        fbFd_ = -1;
        fbMem_ = nullptr;
        return false;
    }

    LOG_I("LVGL", "framebuffer OK (%dx%d, %dbpp)", fbWidth_, fbHeight_, fbBpp_);
    return true;
}

#if defined(ENABLE_LVGL) && defined(USE_LVGL)

bool LvglDisplay::initLvgl()
{
    lv_init();

    static lv_color_t buf1[480 * 50];
    lv_disp_draw_buf_init(&drawBuf_, buf1, nullptr, 480 * 50);

    lv_disp_drv_init(&dispDrv_);
    dispDrv_.hor_res = fbWidth_;
    dispDrv_.ver_res = fbHeight_;
    dispDrv_.flush_cb = lvglDisplayFlushCb;
    dispDrv_.draw_buf = &drawBuf_;
    dispDrv_.user_data = this;
    lvDisplay_ = lv_disp_drv_register(&dispDrv_);

    return lvDisplay_ != nullptr;
}

void LvglDisplay::createUI()
{
    scr_ = lv_scr_act();
    lv_obj_set_style_bg_color(scr_, lv_color_hex(0x1A1A2E), 0);

    lblTitle_ = lv_label_create(scr_);
    lv_label_set_text(lblTitle_, "IoT Gateway");
    lv_obj_set_style_text_color(lblTitle_, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_font(lblTitle_, &lv_font_montserrat_20, 0);
    lv_obj_align(lblTitle_, LV_ALIGN_TOP_MID, 0, 10);

    int panel_w = 140;
    int panel_h = 180;
    int gap = 15;
    int start_x = (fbWidth_ - 3 * panel_w - 2 * gap) / 2;
    int start_y = 50;

    auto create_panel = [&](lv_obj_t *&panel, lv_obj_t *&lbl_val,
                            lv_obj_t *&lbl_unit, lv_obj_t *&bar,
                            int x, const char *title,
                            uint32_t title_color) {
        panel = lv_obj_create(scr_);
        lv_obj_set_size(panel, panel_w, panel_h);
        lv_obj_align(panel, LV_ALIGN_TOP_LEFT, x, start_y);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0x2D2D44), 0);
        lv_obj_set_style_border_color(panel, lv_color_hex(0x3D3D5C), 0);
        lv_obj_set_style_radius(panel, 10, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_t = lv_label_create(panel);
        lv_label_set_text(lbl_t, title);
        lv_obj_set_style_text_color(lbl_t, lv_color_hex(title_color), 0);
        lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_t, LV_ALIGN_TOP_MID, 0, 8);

        lbl_val = lv_label_create(panel);
        lv_label_set_text(lbl_val, "--.-");
        lv_obj_set_style_text_color(lbl_val, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_20, 0);
        lv_obj_align(lbl_val, LV_ALIGN_CENTER, 0, -10);

        lbl_unit = lv_label_create(panel);
        lv_label_set_text(lbl_unit, "");
        lv_obj_set_style_text_color(lbl_unit, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_unit, LV_ALIGN_CENTER, 0, 20);

        bar = lv_bar_create(panel);
        lv_obj_set_width(bar, panel_w - 30);
        lv_obj_set_height(bar, 8);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -12);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x3D3D5C), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(title_color), LV_PART_INDICATOR);
    };

    create_panel(panelTemp_, lblTempVal_, lblTempUnit_, barTemp_,
                 start_x, "Temperature", 0xFF6B6B);
    create_panel(panelHumi_, lblHumiVal_, lblHumiUnit_, barHumi_,
                 start_x + panel_w + gap, "Humidity", 0x4ECDC4);
    create_panel(panelLight_, lblLightVal_, lblLightUnit_, barLight_,
                 start_x + 2 * (panel_w + gap), "Light", 0xFFD93D);
}

void LvglDisplay::updateSensorData(const SensorData &data)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%.1f", data.temperature);
    lv_label_set_text(lblTempVal_, buf);
    lv_label_set_text(lblTempUnit_, "Celsius");
    lv_bar_set_value(barTemp_,
                     (int)((data.temperature + 10) * 100 / 135), LV_ANIM_ON);

    snprintf(buf, sizeof(buf), "%.1f", data.humidity);
    lv_label_set_text(lblHumiVal_, buf);
    lv_label_set_text(lblHumiUnit_, "Percent");
    lv_bar_set_value(barHumi_, (int)data.humidity, LV_ANIM_ON);

    snprintf(buf, sizeof(buf), "%.1f", data.light);
    lv_label_set_text(lblLightVal_, buf);
    lv_label_set_text(lblLightUnit_, "Lux");
    int light_pct = data.light > 1000 ? 100 : (int)(data.light * 100 / 1000);
    lv_bar_set_value(barLight_, light_pct, LV_ANIM_ON);
}

#else

bool LvglDisplay::initLvgl() { return false; }
void LvglDisplay::createUI() {}
void LvglDisplay::updateSensorData(const SensorData &) {}

#endif

void LvglDisplay::lvglLoop()
{
#if defined(ENABLE_LVGL) && defined(USE_LVGL)
    int lastDataUpdate = 0;

    while (running_) {
        lv_tick_inc(cfg_.refreshMs);

        lastDataUpdate += cfg_.refreshMs;
        if (lastDataUpdate >= 2000 && mgr_) {
            SensorData data = mgr_->getLatestData();
            if (data.valid) {
                updateSensorData(data);
            }
            lastDataUpdate = 0;
        }

        lv_timer_handler();
        usleep(cfg_.refreshMs * 1000);
    }
#else
    while (running_) {
        usleep(100000);
    }
#endif
}
