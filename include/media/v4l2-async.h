/*
 * V4L2 asynchronous subdevice registration API
 *
 * Copyright (C) 2012-2013, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef V4L2_ASYNC_H
#define V4L2_ASYNC_H

#include <linux/list.h>
#include <linux/mutex.h>

struct device;
struct v4l2_device;
struct v4l2_subdev;
struct v4l2_async_notifier;

/* A random max subdevice number, used to allocate an array on stack */
#define V4L2_MAX_SUBDEVS 128U

enum v4l2_async_bus_type {
	V4L2_ASYNC_BUS_CUSTOM,
	V4L2_ASYNC_BUS_PLATFORM,
	V4L2_ASYNC_BUS_I2C,
};

/**
 * struct v4l2_async_subdev - sub-device descriptor, as known to a bridge
 * @bus_type:	subdevice bus type to select the appropriate matching method
 * @match:	union of per-bus type matching data sets
 * @list:	used to link struct v4l2_async_subdev objects, waiting to be
 *		probed, to a notifier->waiting list
 */
struct v4l2_async_subdev {
	enum v4l2_async_bus_type bus_type;
	union {
		struct {
			const char *name;
		} platform;
		struct {
			int adapter_id;
			unsigned short address;
		} i2c;
		struct {
			bool (*match)(struct device *,
				      struct v4l2_async_subdev *);
			void *priv;
		} custom;
	} match;

	/* v4l2-async core private: not to be used by drivers */
	struct list_head list;
};

/**
 * v4l2_async_subdev_list - provided by subdevices
 * @list:	links struct v4l2_async_subdev_list objects to a global list
 *		before probing, and onto notifier->done after probing
 * @asd:	pointer to respective struct v4l2_async_subdev
 * @notifier:	pointer to managing notifier
 */
struct v4l2_async_subdev_list {
	struct list_head list;
	struct v4l2_async_subdev *asd;
	struct v4l2_async_notifier *notifier;
};

/**
 * v4l2_async_notifier - v4l2_device notifier data
 * @num_subdevs:number of subdevices
 * @subdev:	array of pointers to subdevice descriptors
 * @v4l2_dev:	pointer to struct v4l2_device
 * @waiting:	list of struct v4l2_async_subdev, waiting for their drivers
 * @done:	list of struct v4l2_async_subdev_list, already probed
 * @list:	member in a global list of notifiers
 * @bound:	a subdevice driver has successfully probed one of subdevices
 * @complete:	all subdevices have been probed successfully
 * @unbind:	a subdevice is leaving
 */
struct v4l2_async_notifier {
	unsigned int num_subdevs;
	struct v4l2_async_subdev **subdev;
	struct v4l2_device *v4l2_dev;
	struct list_head waiting;
	struct list_head done;
	struct list_head list;
	int (*bound)(struct v4l2_async_notifier *notifier,
		     struct v4l2_subdev *subdev,
		     struct v4l2_async_subdev *asd);
	int (*complete)(struct v4l2_async_notifier *notifier);
	void (*unbind)(struct v4l2_async_notifier *notifier,
		       struct v4l2_subdev *subdev,
		       struct v4l2_async_subdev *asd);
};

int v4l2_async_notifier_register(struct v4l2_device *v4l2_dev,
				 struct v4l2_async_notifier *notifier);
void v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier);
int v4l2_async_register_subdev(struct v4l2_subdev *sd);
void v4l2_async_unregister_subdev(struct v4l2_subdev *sd);
#endif
