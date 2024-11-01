/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Mediated device definition
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 */

#ifndef MDEV_H
#define MDEV_H

#include <linux/device.h>
#include <linux/uuid.h>

struct mdev_type;

struct mdev_device {
	struct device dev;
	guid_t uuid;
	struct list_head next;
	struct mdev_type *type;
	bool active;
};

struct mdev_type {
	/* set by the driver before calling mdev_register parent: */
	const char *sysfs_name;
	const char *pretty_name;

	/* set by the core, can be used drivers */
	struct mdev_parent *parent;

	/* internal only */
	struct kobject kobj;
	struct kobject *devices_kobj;
};

/* embedded into the struct device that the mdev devices hang off */
struct mdev_parent {
	struct device *dev;
	struct mdev_driver *mdev_driver;
	struct kset *mdev_types_kset;
	/* Synchronize device creation/removal with parent unregistration */
	struct rw_semaphore unreg_sem;
	struct mdev_type **types;
	unsigned int nr_types;
	atomic_t available_instances;
};

static inline struct mdev_device *to_mdev_device(struct device *dev)
{
	return container_of(dev, struct mdev_device, dev);
}

/**
 * struct mdev_driver - Mediated device driver
 * @device_api: string to return for the device_api sysfs
 * @max_instances: maximum number of instances supported (optional)
 * @probe: called when new device created
 * @remove: called when device removed
 * @get_available: Return the max number of instances that can be created
 * @show_description: Print a description of the mtype
 * @driver: device driver structure
 **/
struct mdev_driver {
	const char *device_api;
	unsigned int max_instances;
	int (*probe)(struct mdev_device *dev);
	void (*remove)(struct mdev_device *dev);
	unsigned int (*get_available)(struct mdev_type *mtype);
	ssize_t (*show_description)(struct mdev_type *mtype, char *buf);
	struct device_driver driver;
};

int mdev_register_parent(struct mdev_parent *parent, struct device *dev,
		struct mdev_driver *mdev_driver, struct mdev_type **types,
		unsigned int nr_types);
void mdev_unregister_parent(struct mdev_parent *parent);

int mdev_register_driver(struct mdev_driver *drv);
void mdev_unregister_driver(struct mdev_driver *drv);

static inline struct device *mdev_dev(struct mdev_device *mdev)
{
	return &mdev->dev;
}

#endif /* MDEV_H */
