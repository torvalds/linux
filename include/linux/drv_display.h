#ifndef _DRV_DISPLAY_COMMON_H_
#define _DRV_DISPLAY_COMMON_H_

#if defined(CONFIG_ARCH_SUN3I)
#include <linux/drv_display_sun3i.h>
#elif defined(CONFIG_ARCH_SUN4I)
#include <linux/drv_display_sun4i.h>
#elif defined(CONFIG_ARCH_SUN5I)
#include <linux/drv_display_sun5i.h>
#else
#error "no chip id defined"
#endif

#endif
