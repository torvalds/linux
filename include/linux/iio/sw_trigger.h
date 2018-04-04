/*
 * Industrial I/O software trigger interface
 *
 * Copyright (c) 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef __IIO_SW_TRIGGER
#define __IIO_SW_TRIGGER

#include <linux/module.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/configfs.h>

#define module_iio_sw_trigger_driver(__iio_sw_trigger_type) \
	module_driver(__iio_sw_trigger_type, iio_register_sw_trigger_type, \
		      iio_unregister_sw_trigger_type)

struct iio_sw_trigger_ops;

struct iio_sw_trigger_type {
	const char *name;
	struct module *owner;
	const struct iio_sw_trigger_ops *ops;
	struct list_head list;
	struct config_group *group;
};

struct iio_sw_trigger {
	struct iio_trigger *trigger;
	struct iio_sw_trigger_type *trigger_type;
	struct config_group group;
};

struct iio_sw_trigger_ops {
	struct iio_sw_trigger* (*probe)(const char *);
	int (*remove)(struct iio_sw_trigger *);
};

static inline
struct iio_sw_trigger *to_iio_sw_trigger(struct config_item *item)
{
	return container_of(to_config_group(item), struct iio_sw_trigger,
			    group);
}

int iio_register_sw_trigger_type(struct iio_sw_trigger_type *tt);
void iio_unregister_sw_trigger_type(struct iio_sw_trigger_type *tt);

struct iio_sw_trigger *iio_sw_trigger_create(const char *, const char *);
void iio_sw_trigger_destroy(struct iio_sw_trigger *);

int iio_sw_trigger_type_configfs_register(struct iio_sw_trigger_type *tt);
void iio_sw_trigger_type_configfs_unregister(struct iio_sw_trigger_type *tt);

static inline
void iio_swt_group_init_type_name(struct iio_sw_trigger *t,
				  const char *name,
				  const struct config_item_type *type)
{
#if IS_ENABLED(CONFIG_CONFIGFS_FS)
	config_group_init_type_name(&t->group, name, type);
#endif
}

#endif /* __IIO_SW_TRIGGER */
