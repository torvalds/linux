#ifndef _DRV_DISPLAY_COMMON_H_
#define _DRV_DISPLAY_COMMON_H_

#if CONFIG_CHIP_ID == 1120

#include <linux/drv_display_sun3i.h>

#elif CONFIG_CHIP_ID == 1123

#include <linux/drv_display_sun4i.h>

#elif CONFIG_CHIP_ID == 1125

#include <linux/drv_display_sun5i.h>

#else

#error "no chip id defined"

#endif



#endif



