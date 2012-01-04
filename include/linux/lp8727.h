/*
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

struct lp8727_chg_param {
	/* end of charge level setting */
	enum lp8727_eoc_level eoc_level;
	/* charging current */
	enum lp8727_ichg ichg;
};

struct lp8727_platform_data {
	u8 (*get_batt_present)(void);
	u16 (*get_batt_level)(void);
	u8 (*get_batt_capacity)(void);
	u8 (*get_batt_temp)(void);
	struct lp8727_chg_param ac;
	struct lp8727_chg_param usb;
};

#endif
