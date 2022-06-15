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

struct mdev_type;

struct mdev_device {
	struct device dev;
	guid_t uuid;
	struct list_head next;
	struct mdev_type *type;
	bool active;
};

static inline struct mdev_device *to_mdev_device(struct device *dev)
{
	return container_of(dev, struct mdev_device, dev);
}

unsigned int mdev_get_type_group_id(struct mdev_device *mdev);
unsigned int mtype_get_type_group_id(struct mdev_type *mtype);
struct device *mtype_get_parent_dev(struct mdev_type *mtype);

/* interface for exporting mdev supported type attributes */
struct mdev_type_attribute {
	struct attribute attr;
	ssize_t (*show)(struct mdev_type *mtype,
			struct mdev_type_attribute *attr, char *buf);
	ssize_t (*store)(struct mdev_type *mtype,
			 struct mdev_type_attribute *attr, const char *buf,
			 size_t count);
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
 * @probe: called when new device created
 * @remove: called when device removed
 * @supported_type_groups: Attributes to define supported types. It is mandatory
 *			to provide supported types.
 * @driver: device driver structure
 *
 **/
struct mdev_driver {
	int (*probe)(struct mdev_device *dev);
	void (*remove)(struct mdev_device *dev);
	struct attribute_group **supported_type_groups;
	struct device_driver driver;
};

static inline const guid_t *mdev_uuid(struct mdev_device *mdev)
{
	return &mdev->uuid;
}

extern struct bus_type mdev_bus_type;

int mdev_register_device(struct device *dev, struct mdev_driver *mdev_driver);
void mdev_unregister_device(struct device *dev);

int mdev_register_driver(struct mdev_driver *drv);
void mdev_unregister_driver(struct mdev_driver *drv);

struct device *mdev_parent_dev(struct mdev_device *mdev);
static inline struct device *mdev_dev(struct mdev_device *mdev)
{
	return &mdev->dev;
}
static inline struct mdev_device *mdev_from_dev(struct device *dev)
{
	return dev->bus == &mdev_bus_type ? to_mdev_device(dev) : NULL;
}

#endif /* MDEV_H */
