#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 32
#define LV_USE_OS LV_OS_NONE
#include <stdlib.h>
#include <stdio.h>
#define LV_MEM_SIZE (1024 * 1024U)
#define LV_ASSERT_HANDLER do { fprintf(stderr,"LVGL assertion: %s:%d\n",__FILE__,__LINE__); fflush(stderr); abort(); } while(0);
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_USE_LOG 0
#define LV_USE_SDL 0
#endif
