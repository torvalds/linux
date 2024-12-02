/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Industrial I/O software device interface
 *
 * Copyright (c) 2016 Intel Corporation
 */

#ifndef __IIO_SW_DEVICE
#define __IIO_SW_DEVICE

#include <linux/module.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/configfs.h>

#define module_iio_sw_device_driver(__iio_sw_device_type) \
	module_driver(__iio_sw_device_type, iio_register_sw_device_type, \
		      iio_unregister_sw_device_type)

struct iio_sw_device_ops;

struct iio_sw_device_type {
	const char *name;
	struct module *owner;
	const struct iio_sw_device_ops *ops;
	struct list_head list;
	struct config_group *group;
};

struct iio_sw_device {
	struct iio_dev *device;
	struct iio_sw_device_type *device_type;
	struct config_group group;
};

struct iio_sw_device_ops {
	struct iio_sw_device* (*probe)(const char *);
	int (*remove)(struct iio_sw_device *);
};

static inline
struct iio_sw_device *to_iio_sw_device(struct config_item *item)
{
	return container_of(to_config_group(item), struct iio_sw_device,
			    group);
}

int iio_register_sw_device_type(struct iio_sw_device_type *dt);
void iio_unregister_sw_device_type(struct iio_sw_device_type *dt);

struct iio_sw_device *iio_sw_device_create(const char *, const char *);
void iio_sw_device_destroy(struct iio_sw_device *);

int iio_sw_device_type_configfs_register(struct iio_sw_device_type *dt);
void iio_sw_device_type_configfs_unregister(struct iio_sw_device_type *dt);

static inline
void iio_swd_group_init_type_name(struct iio_sw_device *d,
				  const char *name,
				  const struct config_item_type *type)
{
#if IS_ENABLED(CONFIG_CONFIGFS_FS)
	config_group_init_type_name(&d->group, name, type);
#endif
}

#endif /* __IIO_SW_DEVICE */
