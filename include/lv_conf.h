/**
 * LVGL v8.x config — tuned for CO5300 466x466 AMOLED (Waveshare ESP32-S3-Touch-AMOLED-1.75).
 * Sourced from socquique/capsule-radar, adapted for oui-spy.
 */
#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 0
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

#define LV_MEM_CUSTOM   1
// Route LVGL allocations through the system heap so OPI PSRAM can absorb large lists.
#define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
#define LV_MEM_CUSTOM_ALLOC(size)            heap_caps_malloc((size), MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE(ptr)              heap_caps_free(ptr)
#define LV_MEM_CUSTOM_REALLOC(ptr,new_size)  heap_caps_realloc((ptr),(new_size),MALLOC_CAP_8BIT)
#define LV_MEM_BUF_MAX_NUM 16
#define LV_MEMCPY_MEMSET_STD 0

#define LV_DISP_DEF_REFR_PERIOD  16
#define LV_INDEV_DEF_READ_PERIOD 20

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE   "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DPI_DEF 130

#define LV_DRAW_COMPLEX     1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#define LV_DISP_ROT_MAX_BUF (10 * 1024)

#define LV_USE_LOG   1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_ARC      1
#define LV_USE_LABEL    1
#define LV_USE_SPINNER  1
#define LV_USE_LIST     1
#define LV_USE_TILEVIEW 1

#endif
#endif
