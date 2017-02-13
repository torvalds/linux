/*
 * Mediated device definition
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MDEV_H
#define MDEV_H

/* Parent device */
struct parent_device {
	struct device		*dev;
	const struct parent_ops	*ops;

	/* internal */
	struct kref		ref;
	struct mutex		lock;
	struct list_head	next;
	struct kset		*mdev_types_kset;
	struct list_head	type_list;
};

/* Mediated device */
struct mdev_device {
	struct device		dev;
	struct parent_device	*parent;
	uuid_le			uuid;
	void			*driver_data;

	/* internal */
	struct kref		ref;
	struct list_head	next;
	struct kobject		*type_kobj;
};

/**
 * struct parent_ops - Structure to be registered for each parent device to
 * register the device to mdev module.
 *
 * @owner:		The module owner.
 * @dev_attr_groups:	Attributes of the parent device.
 * @mdev_attr_groups:	Attributes of the mediated device.
 * @supported_type_groups: Attributes to define supported types. It is mandatory
 *			to provide supported types.
 * @create:		Called to allocate basic resources in parent device's
 *			driver for a particular mediated device. It is
 *			mandatory to provide create ops.
 *			@kobj: kobject of type for which 'create' is called.
 *			@mdev: mdev_device structure on of mediated device
 *			      that is being created
 *			Returns integer: success (0) or error (< 0)
 * @remove:		Called to free resources in parent device's driver for a
 *			a mediated device. It is mandatory to provide 'remove'
 *			ops.
 *			@mdev: mdev_device device structure which is being
 *			       destroyed
 *			Returns integer: success (0) or error (< 0)
 * @open:		Open mediated device.
 *			@mdev: mediated device.
 *			Returns integer: success (0) or error (< 0)
 * @release:		release mediated device
 *			@mdev: mediated device.
 * @read:		Read emulation callback
 *			@mdev: mediated device structure
 *			@buf: read buffer
 *			@count: number of bytes to read
 *			@ppos: address.
 *			Retuns number on bytes read on success or error.
 * @write:		Write emulation callback
 *			@mdev: mediated device structure
 *			@buf: write buffer
 *			@count: number of bytes to be written
 *			@ppos: address.
 *			Retuns number on bytes written on success or error.
 * @ioctl:		IOCTL callback
 *			@mdev: mediated device structure
 *			@cmd: ioctl command
 *			@arg: arguments to ioctl
 * @mmap:		mmap callback
 *			@mdev: mediated device structure
 *			@vma: vma structure
 * Parent device that support mediated device should be registered with mdev
 * module with parent_ops structure.
 **/

struct parent_ops {
	struct module   *owner;
	const struct attribute_group **dev_attr_groups;
	const struct attribute_group **mdev_attr_groups;
	struct attribute_group **supported_type_groups;

	int     (*create)(struct kobject *kobj, struct mdev_device *mdev);
	int     (*remove)(struct mdev_device *mdev);
	int     (*open)(struct mdev_device *mdev);
	void    (*release)(struct mdev_device *mdev);
	ssize_t (*read)(struct mdev_device *mdev, char __user *buf,
			size_t count, loff_t *ppos);
	ssize_t (*write)(struct mdev_device *mdev, const char __user *buf,
			 size_t count, loff_t *ppos);
	ssize_t (*ioctl)(struct mdev_device *mdev, unsigned int cmd,
			 unsigned long arg);
	int	(*mmap)(struct mdev_device *mdev, struct vm_area_struct *vma);
};

/* interface for exporting mdev supported type attributes */
struct mdev_type_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct device *dev, char *buf);
	ssize_t (*store)(struct kobject *kobj, struct device *dev,
			 const char *buf, size_t count);
};

#define MDEV_TYPE_ATTR(_name, _mode, _show, _store)		\
struct mdev_type_attribute mdev_type_attr_##_name =		\
	__ATTR(_name, _mode, _show, _store)
#define MDEV_TYPE_ATTR_RW(_name) \
	struct mdev_type_attribute mdev_type_attr_##_name = __ATTR_RW(_name)
#define MDEV_TYPE_ATTR_RO(_name) \
	struct mdev_type_attribute mdev_type_attr_##_name = __ATTR_RO(_name)
#define MDEV_TYPE_ATTR_WO(_name) \
	struct mdev_type_attribute mdev_type_attr_##_name = __ATTR_WO(_name)

/**
 * struct mdev_driver - Mediated device driver
 * @name: driver name
 * @probe: called when new device created
 * @remove: called when device removed
 * @driver: device driver structure
 *
 **/
struct mdev_driver {
	const char *name;
	int  (*probe)(struct device *dev);
	void (*remove)(struct device *dev);
	struct device_driver driver;
};

#define to_mdev_driver(drv)	container_of(drv, struct mdev_driver, driver)
#define to_mdev_device(dev)	container_of(dev, struct mdev_device, dev)

static inline void *mdev_get_drvdata(struct mdev_device *mdev)
{
	return mdev->driver_data;
}

static inline void mdev_set_drvdata(struct mdev_device *mdev, void *data)
{
	mdev->driver_data = data;
}

extern struct bus_type mdev_bus_type;

#define dev_is_mdev(d) ((d)->bus == &mdev_bus_type)

extern int  mdev_register_device(struct device *dev,
				 const struct parent_ops *ops);
extern void mdev_unregister_device(struct device *dev);

extern int  mdev_register_driver(struct mdev_driver *drv, struct module *owner);
extern void mdev_unregister_driver(struct mdev_driver *drv);

#endif /* MDEV_H */
