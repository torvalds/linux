/*
 * This file is part of the ROHM BH1770GLC / OSRAM SFH7770 sensor driver.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __BH1770_H__
#define __BH1770_H__

/**
 * struct bh1770_platform_data - platform data for bh1770glc driver
 * @led_def_curr: IR led driving current.
 * @glass_attenuation: Attenuation factor for covering window.
 * @setup_resources: Call back for interrupt line setup function
 * @release_resources: Call back for interrupte line release function
 *
 * Example of glass attenuation: 16384 * 385 / 100 means attenuation factor
 * of 3.85. i.e. light_above_sensor = light_above_cover_window / 3.85
 */

struct bh1770_platform_data {
#define BH1770_LED_5mA	0
#define BH1770_LED_10mA	1
#define BH1770_LED_20mA	2
#define BH1770_LED_50mA	3
#define BH1770_LED_100mA 4
#define BH1770_LED_150mA 5
#define BH1770_LED_200mA 6
	__u8 led_def_curr;
#define BH1770_NEUTRAL_GA 16384 /* 16384 / 16384 = 1 */
	__u32 glass_attenuation;
	int (*setup_resources)(void);
	int (*release_resources)(void);
};
#endif
