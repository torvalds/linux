/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Kionix, Inc.
 * Written by Chris Hudson <chudson@kionix.com>
 */

#ifndef __KXTJ9_H__
#define __KXTJ9_H__

#define KXTJ9_I2C_ADDR		0x0F

struct kxtj9_platform_data {
	unsigned int min_interval;	/* minimum poll interval (in milli-seconds) */
	unsigned int init_interval;	/* initial poll interval (in milli-seconds) */

	/*
	 * By default, x is axis 0, y is axis 1, z is axis 2; these can be
	 * changed to account for sensor orientation within the host device.
	 */
	u8 axis_map_x;
	u8 axis_map_y;
	u8 axis_map_z;

	/*
	 * Each axis can be negated to account for sensor orientation within
	 * the host device.
	 */
	bool negate_x;
	bool negate_y;
	bool negate_z;

	/* CTRL_REG1: set resolution, g-range, data ready enable */
	/* Output resolution: 8-bit valid or 12-bit valid */
	#define RES_8BIT		0
	#define RES_12BIT		(1 << 6)
	u8 res_12bit;
	/* Output g-range: +/-2g, 4g, or 8g */
	#define KXTJ9_G_2G		0
	#define KXTJ9_G_4G		(1 << 3)
	#define KXTJ9_G_8G		(1 << 4)
	u8 g_range;

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);
};
#endif  /* __KXTJ9_H__ */
