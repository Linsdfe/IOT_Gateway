/**
 * @file lv_conf.h
 * @brief LVGL 配置文件 - 针对 i.MX6ULL 480x272 LCD 优化
 *
 * 将此文件复制到 lvgl 目录或添加到 include 路径中。
 * 基于 LVGL v8.3 默认配置，针对嵌入式 ARM 平台裁剪。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH          32
#define LV_COLOR_16_SWAP        0
#define LV_COLOR_SCREEN_TRANSP  0

#define LV_MEM_CUSTOM           0
#define LV_MEM_SIZE             (2 * 1024 * 1024)
#define LV_MEM_ADR              0
#define LV_MEM_BUF_MAX_NUM      16

#define LV_DISP_DEF_REFR_PERIOD 33
#define LV_INDEV_DEF_READ_PERIOD 30

#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1

#define LV_THEME_DEFAULT_DARK   0
#define LV_THEME_DEFAULT_FONT_PRIMARY     &lv_font_montserrat_14
#define LV_THEME_DEFAULT_FONT_SECONDARY   &lv_font_montserrat_12

#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BTN              1
#define LV_USE_BTNMATRIX        1
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMG              1
#define LV_USE_LABEL            1
#define LV_USE_LINE             1
#define LV_USE_ROLLER           0
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_TEXTAREA         1
#define LV_USE_TABLE            0

#define LV_USE_ANIM             1
#define LV_USE_FLEX             1
#define LV_USE_GRID             0

#define LV_USE_CHART            1
#define LV_USE_LED              1
#define LV_USE_MSGBOX           0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          1
#define LV_USE_TABVIEW          0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0
#define LV_USE_MENU             0
#define LV_USE_KEYBOARD         1

#define LV_USE_SNAPSHOT         0
#define LV_USE_SYSMON           0
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0

#define LV_BUILD_WITH_PIC       0

#endif
