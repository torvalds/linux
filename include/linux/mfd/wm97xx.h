/*
 * wm97xx client interface
 *
 * Copyright (C) 2017 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_MFD_WM97XX_H
#define __LINUX_MFD_WM97XX_H

struct regmap;
struct wm97xx_batt_pdata;
struct snd_ac97;

struct wm97xx_platform_data {
	struct snd_ac97 *ac97;
	struct regmap *regmap;
	struct wm97xx_batt_pdata *batt_pdata;
};

#endif
