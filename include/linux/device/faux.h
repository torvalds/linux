/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (c) 2025 The Linux Foundation
 *
 * A "simple" faux bus that allows devices to be created and added
 * automatically to it.  This is to be used whenever you need to create a
 * device that is not associated with any "real" system resources, and do
 * not want to have to deal with a bus/driver binding logic.  It is
 * intended to be very simple, with only a create and a destroy function
 * available.
 */
#ifndef _FAUX_DEVICE_H_
#define _FAUX_DEVICE_H_

#include <linux/container_of.h>
#include <linux/device.h>

/**
 * struct faux_device - a "faux" device
 * @dev:	internal struct device of the object
 *
 * A simple faux device that can be created/destroyed.  To be used when a
 * driver only needs to have a device to "hang" something off.  This can be
 * used for downloading firmware or other basic tasks.  Use this instead of
 * a struct platform_device if the device has no resources assigned to
 * it at all.
 */
struct faux_device {
	struct device dev;
};
#define to_faux_device(x) container_of_const((x), struct faux_device, dev)

/**
 * struct faux_device_ops - a set of callbacks for a struct faux_device
 * @probe:	called when a faux device is probed by the driver core
 *		before the device is fully bound to the internal faux bus
 *		code.  If probe succeeds, return 0, otherwise return a
 *		negative error number to stop the probe sequence from
 *		succeeding.
 * @remove:	called when a faux device is removed from the system
 *
 * Both @probe and @remove are optional, if not needed, set to NULL.
 */
struct faux_device_ops {
	int (*probe)(struct faux_device *faux_dev);
	void (*remove)(struct faux_device *faux_dev);
};

struct faux_device *faux_device_create(const char *name,
				       struct device *parent,
				       const struct faux_device_ops *faux_ops);
struct faux_device *faux_device_create_with_groups(const char *name,
						   struct device *parent,
						   const struct faux_device_ops *faux_ops,
						   const struct attribute_group **groups);
void faux_device_destroy(struct faux_device *faux_dev);

static inline void *faux_device_get_drvdata(const struct faux_device *faux_dev)
{
	return dev_get_drvdata(&faux_dev->dev);
}

static inline void faux_device_set_drvdata(struct faux_device *faux_dev, void *data)
{
	dev_set_drvdata(&faux_dev->dev, data);
}

#endif /* _FAUX_DEVICE_H_ */
