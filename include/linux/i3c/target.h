/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022, Intel Corporation */

#ifndef I3C_TARGET_H
#define I3C_TARGET_H

#include <linux/device.h>
#include <linux/i3c/device.h>

struct i3c_master_controller;

struct i3c_target_ops {
	int (*bus_init)(struct i3c_master_controller *master);
	void (*bus_cleanup)(struct i3c_master_controller *master);
	int (*priv_xfers)(struct i3c_dev_desc *dev, struct i3c_priv_xfer *xfers, int nxfers);
	int (*generate_ibi)(struct i3c_dev_desc *dev, const u8 *data, int len);
};

int i3c_target_register(struct i3c_master_controller *master, struct device *parent,
			const struct i3c_target_ops *ops);
int i3c_target_unregister(struct i3c_master_controller *master);

#endif
