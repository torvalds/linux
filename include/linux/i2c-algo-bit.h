/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * i2c-algo-bit.h: i2c driver algorithms for bit-shift adapters
 *
 *   Copyright (C) 1995-99 Simon G. Vogl
 * With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and even
 * Frodo Looijaard <frodol@dds.nl>
 */

#ifndef _LINUX_I2C_ALGO_BIT_H
#define _LINUX_I2C_ALGO_BIT_H

#include <linux/i2c.h>

/* --- Defines for bit-adapters ---------------------------------------	*/
/*
 * This struct contains the hw-dependent functions of bit-style adapters to
 * manipulate the line states, and to init any hw-specific features. This is
 * only used if you have more than one hw-type of adapter running.
 */
struct i2c_algo_bit_data {
	void *data;		/* private data for lowlevel routines */
	void (*setsda) (void *data, int state);
	void (*setscl) (void *data, int state);
	int  (*getsda) (void *data);
	int  (*getscl) (void *data);
	int  (*pre_xfer)  (struct i2c_adapter *);
	void (*post_xfer) (struct i2c_adapter *);

	/* local settings */
	int udelay;		/* half clock cycle time in us,
				   minimum 2 us for fast-mode I2C,
				   minimum 5 us for standard-mode I2C and SMBus,
				   maximum 50 us for SMBus */
	int timeout;		/* in jiffies */
};

int i2c_bit_add_bus(struct i2c_adapter *);
int i2c_bit_add_numbered_bus(struct i2c_adapter *);
extern const struct i2c_algorithm i2c_bit_algo;

#endif /* _LINUX_I2C_ALGO_BIT_H */
