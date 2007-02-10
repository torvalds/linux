/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * Copyright (C) 2003 Ladislav Michl <ladis@linux-mips.org>
 */

#ifndef I2C_ALGO_SGI_H
#define I2C_ALGO_SGI_H 1

#include <linux/i2c.h>

struct i2c_algo_sgi_data {
	void *data;	/* private data for lowlevel routines */
	unsigned (*getctrl)(void *data);
	void (*setctrl)(void *data, unsigned val);
	unsigned (*rdata)(void *data);
	void (*wdata)(void *data, unsigned val);

	int xfer_timeout;
	int ack_timeout;
};

int i2c_sgi_add_bus(struct i2c_adapter *);

#endif /* I2C_ALGO_SGI_H */
