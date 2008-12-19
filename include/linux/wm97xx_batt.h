#ifndef _LINUX_WM97XX_BAT_H
#define _LINUX_WM97XX_BAT_H

#include <linux/wm97xx.h>

struct wm97xx_batt_info {
	int	batt_aux;
	int	temp_aux;
	int	charge_gpio;
	int	min_voltage;
	int	max_voltage;
	int	batt_div;
	int	batt_mult;
	int	temp_div;
	int	temp_mult;
	int	batt_tech;
	char	*batt_name;
};

#ifdef CONFIG_BATTERY_WM97XX
void __init wm97xx_bat_set_pdata(struct wm97xx_batt_info *data);
#else
static inline void wm97xx_bat_set_pdata(struct wm97xx_batt_info *data) {}
#endif

#endif
