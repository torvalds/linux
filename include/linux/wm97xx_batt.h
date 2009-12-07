#ifndef _LINUX_WM97XX_BAT_H
#define _LINUX_WM97XX_BAT_H

#include <linux/wm97xx.h>

#warning This file will be removed soon, use wm97xx.h instead!

#define wm97xx_batt_info wm97xx_batt_pdata

#ifdef CONFIG_BATTERY_WM97XX
void wm97xx_bat_set_pdata(struct wm97xx_batt_info *data);
#else
static inline void wm97xx_bat_set_pdata(struct wm97xx_batt_info *data) {}
#endif

#endif
