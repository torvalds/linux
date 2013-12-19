/*
 * lv5207lp.h - Sanyo LV5207LP LEDs Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LV5207LP_H__
#define __LV5207LP_H__

struct device;

struct lv5207lp_platform_data {
	struct device *fbdev;
	unsigned int max_value;
	unsigned int def_value;
};

#endif
