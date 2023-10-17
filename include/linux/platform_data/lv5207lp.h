/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * lv5207lp.h - Sanyo LV5207LP LEDs Driver
 */
#ifndef __LV5207LP_H__
#define __LV5207LP_H__

struct device;

struct lv5207lp_platform_data {
	struct device *dev;
	unsigned int max_value;
	unsigned int def_value;
};

#endif
