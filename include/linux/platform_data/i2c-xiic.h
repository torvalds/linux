/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * i2c-xiic.h
 * Copyright (c) 2009 Intel Corporation
 */

/* Supports:
 * Xilinx IIC
 */

#ifndef _LINUX_I2C_XIIC_H
#define _LINUX_I2C_XIIC_H

/**
 * struct xiic_i2c_platform_data - Platform data of the Xilinx I2C driver
 * @num_devices:	Number of devices that shall be added when the driver
 *			is probed.
 * @devices:		The actuall devices to add.
 *
 * This purpose of this platform data struct is to be able to provide a number
 * of devices that should be added to the I2C bus. The reason is that sometimes
 * the I2C board info is not enough, a new PCI board can for instance be
 * plugged into a standard PC, and the bus number might be unknown at
 * early init time.
 */
struct xiic_i2c_platform_data {
	u8				num_devices;
	struct i2c_board_info const	*devices;
};

#endif /* _LINUX_I2C_XIIC_H */
