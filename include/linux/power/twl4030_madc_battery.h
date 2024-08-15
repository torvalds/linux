/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Dumb driver for LiIon batteries using TWL4030 madc.
 *
 * Copyright 2013 Golden Delicious Computers
 * Nikolaus Schaller <hns@goldelico.com>
 */

#ifndef __TWL4030_MADC_BATTERY_H
#define __TWL4030_MADC_BATTERY_H

/*
 * Usually we can assume 100% @ 4.15V and 0% @ 3.3V but curves differ for
 * charging and discharging!
 */

struct twl4030_madc_bat_calibration {
	short voltage;	/* in mV - specify -1 for end of list */
	short level;	/* in percent (0 .. 100%) */
};

struct twl4030_madc_bat_platform_data {
	unsigned int capacity;	/* total capacity in uAh */
	struct twl4030_madc_bat_calibration *charging;
	int charging_size;
	struct twl4030_madc_bat_calibration *discharging;
	int discharging_size;
};

#endif
