/*
 * include/linux/mfd/wm8994/core.h -- Core interface for WM8994
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __MFD_WM8994_CORE_H__
#define __MFD_WM8994_CORE_H__

<<<<<<< HEAD
#include <linux/interrupt.h>

enum wm8994_type {
	WM8994 = 0,
	WM8958 = 1,
};

=======
>>>>>>> parent of 15f7fab... temp revert rk change
struct regulator_dev;
struct regulator_bulk_data;

#define WM8994_NUM_GPIO_REGS 11
#define WM8994_NUM_LDO_REGS 2

struct wm8994 {
	struct mutex io_lock;

	enum wm8994_type type;

	struct device *dev;
	int (*read_dev)(struct wm8994 *wm8994, unsigned short reg,
			int bytes, void *dest);
	int (*write_dev)(struct wm8994 *wm8994, unsigned short reg,
			 int bytes, const void *src);

	void *control_data;

	int gpio_base;

	/* Used over suspend/resume */
	bool suspended;
	u16 ldo_regs[WM8994_NUM_LDO_REGS];
	u16 gpio_regs[WM8994_NUM_GPIO_REGS];

	struct regulator_dev *dbvdd;
	int num_supplies;
	struct regulator_bulk_data *supplies;
};

/* Device I/O API */
int wm8994_reg_read(struct wm8994 *wm8994, unsigned short reg);
int wm8994_reg_write(struct wm8994 *wm8994, unsigned short reg,
		 unsigned short val);
int wm8994_set_bits(struct wm8994 *wm8994, unsigned short reg,
		    unsigned short mask, unsigned short val);
int wm8994_bulk_read(struct wm8994 *wm8994, unsigned short reg,
		     int count, u16 *buf);
int wm8994_bulk_write(struct wm8994 *wm8994, unsigned short reg,
		     int count, const u16 *buf);

#endif
