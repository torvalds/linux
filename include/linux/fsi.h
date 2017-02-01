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
	struct device dev;
};

struct fsi_driver {
	struct device_driver drv;
};

#define to_fsi_dev(devp) container_of(devp, struct fsi_device, dev)
#define to_fsi_drv(drvp) container_of(drvp, struct fsi_driver, drv)

extern struct bus_type fsi_bus_type;

#endif /* LINUX_FSI_H */
