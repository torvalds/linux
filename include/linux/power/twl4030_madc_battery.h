/*
 * Dumb driver for LiIon batteries using TWL4030 madc.
 *
 * Copyright 2013 Golden Delicious Computers
 * Nikolaus Schaller <hns@goldelico.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
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
