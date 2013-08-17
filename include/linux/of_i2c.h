/*
 * Generic I2C API implementation for PowerPC.
 *
 * Copyright (c) 2008 Jochen Friedrich <jochen@scram.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_OF_I2C_H
#define __LINUX_OF_I2C_H

#if defined(CONFIG_OF_I2C) || defined(CONFIG_OF_I2C_MODULE)
#include <linux/i2c.h>

extern void of_i2c_register_devices(struct i2c_adapter *adap);

/* must call put_device() when done with returned i2c_client device */
extern struct i2c_client *of_find_i2c_device_by_node(struct device_node *node);

#else
static inline void of_i2c_register_devices(struct i2c_adapter *adap)
{
	return;
}
#endif /* CONFIG_OF_I2C */

#endif /* __LINUX_OF_I2C_H */
