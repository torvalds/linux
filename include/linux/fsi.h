/* FSI device & driver interfaces
 *
 * Copyright (C) IBM Corporation 2016
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef LINUX_FSI_H
#define LINUX_FSI_H

#include <linux/device.h>

struct fsi_device {
	struct device		dev;
	u8			engine_type;
	u8			version;
};

struct fsi_device_id {
	u8	engine_type;
	u8	version;
};

#define FSI_VERSION_ANY		0

#define FSI_DEVICE(t) \
	.engine_type = (t), .version = FSI_VERSION_ANY,

#define FSI_DEVICE_VERSIONED(t, v) \
	.engine_type = (t), .version = (v),


struct fsi_driver {
	struct device_driver		drv;
	const struct fsi_device_id	*id_table;
};

#define to_fsi_dev(devp) container_of(devp, struct fsi_device, dev)
#define to_fsi_drv(drvp) container_of(drvp, struct fsi_driver, drv)

extern struct bus_type fsi_bus_type;

#endif /* LINUX_FSI_H */
