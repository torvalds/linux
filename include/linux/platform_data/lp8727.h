/*
 * LP8727 Micro/Mini USB IC with integrated charger
 *
 *			Copyright (C) 2011 Texas Instruments
 *			Copyright (C) 2011 National Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LP8727_H
#define _LP8727_H

enum lp8727_eoc_level {
	EOC_5P,
	EOC_10P,
	EOC_16P,
	EOC_20P,
	EOC_25P,
	EOC_33P,
	EOC_50P,
};

enum lp8727_ichg {
	ICHG_90mA,
	ICHG_100mA,
	ICHG_400mA,
	ICHG_450mA,
	ICHG_500mA,
	ICHG_600mA,
	ICHG_700mA,
	ICHG_800mA,
	ICHG_900mA,
	ICHG_1000mA,
};

/**
 * struct lp8727_chg_param
 * @eoc_level : end of charge level setting
 * @ichg : charging current
 */
struct lp8727_chg_param {
	enum lp8727_eoc_level eoc_level;
	enum lp8727_ichg ichg;
};

/**
 * struct lp8727_platform_data
 * @get_batt_present : check battery status - exists or not
 * @get_batt_level : get battery voltage (mV)
 * @get_batt_capacity : get battery capacity (%)
 * @get_batt_temp : get battery temperature
 * @ac                : charging parameters for AC type charger
 * @usb               : charging parameters for USB type charger
 * @debounce_msec     : interrupt debounce time
 */
struct lp8727_platform_data {
	u8 (*get_batt_present)(void);
	u16 (*get_batt_level)(void);
	u8 (*get_batt_capacity)(void);
	u8 (*get_batt_temp)(void);
	struct lp8727_chg_param *ac;
	struct lp8727_chg_param *usb;
	unsigned int debounce_msec;
};

#endif
