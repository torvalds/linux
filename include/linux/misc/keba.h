/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2024, KEBA Industrial Automation Gmbh */

#ifndef _LINUX_MISC_KEBA_H
#define _LINUX_MISC_KEBA_H

#include <linux/auxiliary_bus.h>

struct i2c_board_info;

/**
 * struct keba_i2c_auxdev - KEBA I2C auxiliary device
 * @auxdev: auxiliary device object
 * @io: address range of I2C controller IO memory
 * @info_size: number of I2C devices to be probed
 * @info: I2C devices to be probed
 */
struct keba_i2c_auxdev {
	struct auxiliary_device auxdev;
	struct resource io;
	int info_size;
	struct i2c_board_info *info;
};

#endif /* _LINUX_MISC_KEBA_H */
