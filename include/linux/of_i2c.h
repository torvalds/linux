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

#include <linux/i2c.h>

static inline void of_i2c_register_devices(struct i2c_adapter *adap) { };

#endif /* __LINUX_OF_I2C_H */
