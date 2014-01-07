/*
 * si570.h - Configuration for si570 misc driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License only).
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_SI570_H
#define __LINUX_SI570_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/i2c.h>

struct si570_platform_data {
	u64 factory_fout;		/* Factory default output frequency */
	unsigned long initial_fout;	/* Requested initial frequency */
};

int get_frequency_si570(struct device *dev, unsigned long *freq);
int set_frequency_si570(struct device *dev, unsigned long freq);
int reset_si570(struct device *dev, int id);
struct i2c_client *get_i2c_client_si570(void);

#endif /* __LINUX_SI570_H */
