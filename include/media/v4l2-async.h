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
struct device_node;
struct v4l2_device;
struct v4l2_subdev;
struct v4l2_async_notifier;

/* A random max subdevice number, used to allocate an array on stack */
#define V4L2_MAX_SUBDEVS 128U

/**
 * enum v4l2_async_match_type - type of asynchronous subdevice logic to be used
 *	in order to identify a match
 *
 * @V4L2_ASYNC_MATCH_CUSTOM: Match will use the logic provided by &struct
 * 	v4l2_async_subdev.match ops
 * @V4L2_ASYNC_MATCH_DEVNAME: Match will use the device name
 * @V4L2_ASYNC_MATCH_I2C: Match will check for I2C adapter ID and address
 * @V4L2_ASYNC_MATCH_OF: Match will use OF node
 *
 * This enum is used by the asyncrhronous sub-device logic to define the
 * algorithm that will be used to match an asynchronous device.
 */
enum v4l2_async_match_type {
	V4L2_ASYNC_MATCH_CUSTOM,
	V4L2_ASYNC_MATCH_DEVNAME,
	V4L2_ASYNC_MATCH_I2C,
	V4L2_ASYNC_MATCH_OF,
};

/**
 * struct v4l2_async_subdev - sub-device descriptor, as known to a bridge
 *
 * @match_type:	type of match that will be used
 * @match:	union of per-bus type matching data sets
 * @list:	used to link struct v4l2_async_subdev objects, waiting to be
 *		probed, to a notifier->waiting list
 */
struct v4l2_async_subdev {
	enum v4l2_async_match_type match_type;
	union {
		struct {
			const struct device_node *node;
		} of;
		struct {
			const char *name;
		} device_name;
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
 * struct v4l2_async_notifier - v4l2_device notifier data
 *
 * @num_subdevs: number of subdevices
 * @subdevs:	array of pointers to subdevice descriptors
 * @v4l2_dev:	pointer to struct v4l2_device
 * @waiting:	list of struct v4l2_async_subdev, waiting for their drivers
 * @done:	list of struct v4l2_subdev, already probed
 * @list:	member in a global list of notifiers
 * @bound:	a subdevice driver has successfully probed one of subdevices
 * @complete:	all subdevices have been probed successfully
 * @unbind:	a subdevice is leaving
 */
struct v4l2_async_notifier {
	unsigned int num_subdevs;
	struct v4l2_async_subdev **subdevs;
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

/**
 * v4l2_async_notifier_register - registers a subdevice asynchronous notifier
 *
 * @v4l2_dev: pointer to &struct v4l2_device
 * @notifier: pointer to &struct v4l2_async_notifier
 */
int v4l2_async_notifier_register(struct v4l2_device *v4l2_dev,
				 struct v4l2_async_notifier *notifier);

/**
 * v4l2_async_notifier_unregister - unregisters a subdevice asynchronous notifier
 *
 * @notifier: pointer to &struct v4l2_async_notifier
 */
void v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier);

/**
 * v4l2_async_register_subdev - registers a sub-device to the asynchronous
 * 	subdevice framework
 *
 * @sd: pointer to &struct v4l2_subdev
 */
int v4l2_async_register_subdev(struct v4l2_subdev *sd);

/**
 * v4l2_async_unregister_subdev - unregisters a sub-device to the asynchronous
 * 	subdevice framework
 *
 * @sd: pointer to &struct v4l2_subdev
 */
void v4l2_async_unregister_subdev(struct v4l2_subdev *sd);
#endif
