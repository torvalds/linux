/*
 * This file is part of the APDS990x sensor driver.
 * Chip is combined proximity and ambient light sensor.
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __APDS990X_H__
#define __APDS990X_H__


#define APDS_IRLED_CURR_12mA	0x3
#define APDS_IRLED_CURR_25mA	0x2
#define APDS_IRLED_CURR_50mA	0x1
#define APDS_IRLED_CURR_100mA	0x0

/**
 * struct apds990x_chip_factors - defines effect of the cover window
 * @ga: Total glass attenuation
 * @cf1: clear channel factor 1 for raw to lux conversion
 * @irf1: IR channel factor 1 for raw to lux conversion
 * @cf2: clear channel factor 2 for raw to lux conversion
 * @irf2: IR channel factor 2 for raw to lux conversion
 * @df: device factor for conversion formulas
 *
 * Structure for tuning ALS calculation to match with environment.
 * Values depend on the material above the sensor and the sensor
 * itself. If the GA is zero, driver will use uncovered sensor default values
 * format: decimal value * APDS_PARAM_SCALE except df which is plain integer.
 */
#define APDS_PARAM_SCALE 4096
struct apds990x_chip_factors {
	int ga;
	int cf1;
	int irf1;
	int cf2;
	int irf2;
	int df;
};

/**
 * struct apds990x_platform_data - platform data for apsd990x.c driver
 * @cf: chip factor data
 * @pddrive: IR-led driving current
 * @ppcount: number of IR pulses used for proximity estimation
 * @setup_resources: interrupt line setup call back function
 * @release_resources: interrupt line release call back function
 *
 * Proximity detection result depends heavily on correct ppcount, pdrive
 * and cover window.
 *
 */

struct apds990x_platform_data {
	struct apds990x_chip_factors cf;
	u8     pdrive;
	u8     ppcount;
	int    (*setup_resources)(void);
	int    (*release_resources)(void);
};

#endif
