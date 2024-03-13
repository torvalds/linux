/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Definitions for container bus type.
 *
 * Copyright (C) 2013, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#ifndef _LINUX_CONTAINER_H
#define _LINUX_CONTAINER_H

#include <linux/device.h>

/* drivers/base/power/container.c */
extern const struct bus_type container_subsys;

struct container_dev {
	struct device dev;
	int (*offline)(struct container_dev *cdev);
};

static inline struct container_dev *to_container_dev(struct device *dev)
{
	return container_of(dev, struct container_dev, dev);
}

#endif /* _LINUX_CONTAINER_H */
