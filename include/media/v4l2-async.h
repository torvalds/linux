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
 * @V4L2_ASYNC_MATCH_FWNODE: Match will use firmware node
 *
 * This enum is used by the asyncrhronous sub-device logic to define the
 * algorithm that will be used to match an asynchronous device.
 */
enum v4l2_async_match_type {
	V4L2_ASYNC_MATCH_CUSTOM,
	V4L2_ASYNC_MATCH_DEVNAME,
	V4L2_ASYNC_MATCH_I2C,
	V4L2_ASYNC_MATCH_FWNODE,
};

/**
 * struct v4l2_async_subdev - sub-device descriptor, as known to a bridge
 *
 * @match_type:	type of match that will be used
 * @match:	union of per-bus type matching data sets
 * @list:	used to link struct v4l2_async_subdev objects, waiting to be
 *		probed, to a notifier->waiting list
 *
 * When this struct is used as a member in a driver specific struct,
 * the driver specific struct shall contain the &struct
 * v4l2_async_subdev as its first member.
 */
struct v4l2_async_subdev {
	enum v4l2_async_match_type match_type;
	union {
		struct {
			struct fwnode_handle *fwnode;
		} fwnode;
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
 * struct v4l2_async_notifier_operations - Asynchronous V4L2 notifier operations
 * @bound:	a subdevice driver has successfully probed one of the subdevices
 * @complete:	all subdevices have been probed successfully
 * @unbind:	a subdevice is leaving
 */
struct v4l2_async_notifier_operations {
	int (*bound)(struct v4l2_async_notifier *notifier,
		     struct v4l2_subdev *subdev,
		     struct v4l2_async_subdev *asd);
	int (*complete)(struct v4l2_async_notifier *notifier);
	void (*unbind)(struct v4l2_async_notifier *notifier,
		       struct v4l2_subdev *subdev,
		       struct v4l2_async_subdev *asd);
};

/**
 * struct v4l2_async_notifier - v4l2_device notifier data
 *
 * @ops:	notifier operations
 * @num_subdevs: number of subdevices used in the subdevs array
 * @max_subdevs: number of subdevices allocated in the subdevs array
 * @subdevs:	array of pointers to subdevice descriptors
 * @v4l2_dev:	v4l2_device of the root notifier, NULL otherwise
 * @sd:		sub-device that registered the notifier, NULL otherwise
 * @parent:	parent notifier
 * @waiting:	list of struct v4l2_async_subdev, waiting for their drivers
 * @done:	list of struct v4l2_subdev, already probed
 * @list:	member in a global list of notifiers
 */
struct v4l2_async_notifier {
	const struct v4l2_async_notifier_operations *ops;
	unsigned int num_subdevs;
	unsigned int max_subdevs;
	struct v4l2_async_subdev **subdevs;
	struct v4l2_device *v4l2_dev;
	struct v4l2_subdev *sd;
	struct v4l2_async_notifier *parent;
	struct list_head waiting;
	struct list_head done;
	struct list_head list;
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
 * v4l2_async_subdev_notifier_register - registers a subdevice asynchronous
 *					 notifier for a sub-device
 *
 * @sd: pointer to &struct v4l2_subdev
 * @notifier: pointer to &struct v4l2_async_notifier
 */
int v4l2_async_subdev_notifier_register(struct v4l2_subdev *sd,
					struct v4l2_async_notifier *notifier);

/**
 * v4l2_async_notifier_unregister - unregisters a subdevice asynchronous notifier
 *
 * @notifier: pointer to &struct v4l2_async_notifier
 */
void v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier);

/**
 * v4l2_async_notifier_cleanup - clean up notifier resources
 * @notifier: the notifier the resources of which are to be cleaned up
 *
 * Release memory resources related to a notifier, including the async
 * sub-devices allocated for the purposes of the notifier but not the notifier
 * itself. The user is responsible for calling this function to clean up the
 * notifier after calling @v4l2_async_notifier_parse_fwnode_endpoints or
 * @v4l2_fwnode_reference_parse_sensor_common.
 *
 * There is no harm from calling v4l2_async_notifier_cleanup in other
 * cases as long as its memory has been zeroed after it has been
 * allocated.
 */
void v4l2_async_notifier_cleanup(struct v4l2_async_notifier *notifier);

/**
 * v4l2_async_register_subdev - registers a sub-device to the asynchronous
 * 	subdevice framework
 *
 * @sd: pointer to &struct v4l2_subdev
 */
int v4l2_async_register_subdev(struct v4l2_subdev *sd);

/**
 * v4l2_async_register_subdev_sensor_common - registers a sensor sub-device to
 *					      the asynchronous sub-device
 *					      framework and parse set up common
 *					      sensor related devices
 *
 * @sd: pointer to struct &v4l2_subdev
 *
 * This function is just like v4l2_async_register_subdev() with the exception
 * that calling it will also parse firmware interfaces for remote references
 * using v4l2_async_notifier_parse_fwnode_sensor_common() and registers the
 * async sub-devices. The sub-device is similarly unregistered by calling
 * v4l2_async_unregister_subdev().
 *
 * While registered, the subdev module is marked as in-use.
 *
 * An error is returned if the module is no longer loaded on any attempts
 * to register it.
 */
int __must_check v4l2_async_register_subdev_sensor_common(
	struct v4l2_subdev *sd);

/**
 * v4l2_async_unregister_subdev - unregisters a sub-device to the asynchronous
 * 	subdevice framework
 *
 * @sd: pointer to &struct v4l2_subdev
 */
void v4l2_async_unregister_subdev(struct v4l2_subdev *sd);

#endif
