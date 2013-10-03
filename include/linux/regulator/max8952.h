/*
 * max8952.h - Voltage regulation for the Maxim 8952
 *
 *  Copyright (C) 2010 Samsung Electrnoics
 *  MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef REGULATOR_MAX8952
#define REGULATOR_MAX8952

#include <linux/regulator/machine.h>

enum {
	MAX8952_DVS_MODE0,
	MAX8952_DVS_MODE1,
	MAX8952_DVS_MODE2,
	MAX8952_DVS_MODE3,
};

enum {
	MAX8952_DVS_770mV = 0,
	MAX8952_DVS_780mV,
	MAX8952_DVS_790mV,
	MAX8952_DVS_800mV,
	MAX8952_DVS_810mV,
	MAX8952_DVS_820mV,
	MAX8952_DVS_830mV,
	MAX8952_DVS_840mV,
	MAX8952_DVS_850mV,
	MAX8952_DVS_860mV,
	MAX8952_DVS_870mV,
	MAX8952_DVS_880mV,
	MAX8952_DVS_890mV,
	MAX8952_DVS_900mV,
	MAX8952_DVS_910mV,
	MAX8952_DVS_920mV,
	MAX8952_DVS_930mV,
	MAX8952_DVS_940mV,
	MAX8952_DVS_950mV,
	MAX8952_DVS_960mV,
	MAX8952_DVS_970mV,
	MAX8952_DVS_980mV,
	MAX8952_DVS_990mV,
	MAX8952_DVS_1000mV,
	MAX8952_DVS_1010mV,
	MAX8952_DVS_1020mV,
	MAX8952_DVS_1030mV,
	MAX8952_DVS_1040mV,
	MAX8952_DVS_1050mV,
	MAX8952_DVS_1060mV,
	MAX8952_DVS_1070mV,
	MAX8952_DVS_1080mV,
	MAX8952_DVS_1090mV,
	MAX8952_DVS_1100mV,
	MAX8952_DVS_1110mV,
	MAX8952_DVS_1120mV,
	MAX8952_DVS_1130mV,
	MAX8952_DVS_1140mV,
	MAX8952_DVS_1150mV,
	MAX8952_DVS_1160mV,
	MAX8952_DVS_1170mV,
	MAX8952_DVS_1180mV,
	MAX8952_DVS_1190mV,
	MAX8952_DVS_1200mV,
	MAX8952_DVS_1210mV,
	MAX8952_DVS_1220mV,
	MAX8952_DVS_1230mV,
	MAX8952_DVS_1240mV,
	MAX8952_DVS_1250mV,
	MAX8952_DVS_1260mV,
	MAX8952_DVS_1270mV,
	MAX8952_DVS_1280mV,
	MAX8952_DVS_1290mV,
	MAX8952_DVS_1300mV,
	MAX8952_DVS_1310mV,
	MAX8952_DVS_1320mV,
	MAX8952_DVS_1330mV,
	MAX8952_DVS_1340mV,
	MAX8952_DVS_1350mV,
	MAX8952_DVS_1360mV,
	MAX8952_DVS_1370mV,
	MAX8952_DVS_1380mV,
	MAX8952_DVS_1390mV,
	MAX8952_DVS_1400mV,
};

enum {
	MAX8952_SYNC_FREQ_26MHZ, /* Default */
	MAX8952_SYNC_FREQ_13MHZ,
	MAX8952_SYNC_FREQ_19_2MHZ,
};

enum {
	MAX8952_RAMP_32mV_us = 0, /* Default */
	MAX8952_RAMP_16mV_us,
	MAX8952_RAMP_8mV_us,
	MAX8952_RAMP_4mV_us,
	MAX8952_RAMP_2mV_us,
	MAX8952_RAMP_1mV_us,
	MAX8952_RAMP_0_5mV_us,
	MAX8952_RAMP_0_25mV_us,
};

#define MAX8952_NUM_DVS_MODE	4

struct max8952_platform_data {
	int gpio_vid0;
	int gpio_vid1;
	int gpio_en;

	u32 default_mode;
	u32 dvs_mode[MAX8952_NUM_DVS_MODE]; /* MAX8952_DVS_MODEx_XXXXmV */

	u32 sync_freq;
	u32 ramp_speed;

	struct regulator_init_data *reg_data;
};


#endif /* REGULATOR_MAX8952 */
