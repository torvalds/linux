/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * V4L2 asynchroyesus subdevice registration API
 *
 * Copyright (C) 2012-2013, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */

#ifndef V4L2_ASYNC_H
#define V4L2_ASYNC_H

#include <linux/list.h>
#include <linux/mutex.h>

struct device;
struct device_yesde;
struct v4l2_device;
struct v4l2_subdev;
struct v4l2_async_yestifier;

/**
 * enum v4l2_async_match_type - type of asynchroyesus subdevice logic to be used
 *	in order to identify a match
 *
 * @V4L2_ASYNC_MATCH_CUSTOM: Match will use the logic provided by &struct
 *	v4l2_async_subdev.match ops
 * @V4L2_ASYNC_MATCH_DEVNAME: Match will use the device name
 * @V4L2_ASYNC_MATCH_I2C: Match will check for I2C adapter ID and address
 * @V4L2_ASYNC_MATCH_FWNODE: Match will use firmware yesde
 *
 * This enum is used by the asyncrhroyesus sub-device logic to define the
 * algorithm that will be used to match an asynchroyesus device.
 */
enum v4l2_async_match_type {
	V4L2_ASYNC_MATCH_CUSTOM,
	V4L2_ASYNC_MATCH_DEVNAME,
	V4L2_ASYNC_MATCH_I2C,
	V4L2_ASYNC_MATCH_FWNODE,
};

/**
 * struct v4l2_async_subdev - sub-device descriptor, as kyeswn to a bridge
 *
 * @match_type:	type of match that will be used
 * @match:	union of per-bus type matching data sets
 * @match.fwyesde:
 *		pointer to &struct fwyesde_handle to be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_FWNODE.
 * @match.device_name:
 *		string containing the device name to be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_DEVNAME.
 * @match.i2c:	embedded struct with I2C parameters to be matched.
 *		Both @match.i2c.adapter_id and @match.i2c.address
 *		should be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_I2C.
 * @match.i2c.adapter_id:
 *		I2C adapter ID to be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_I2C.
 * @match.i2c.address:
 *		I2C address to be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_I2C.
 * @match.custom:
 *		Driver-specific match criteria.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_CUSTOM.
 * @match.custom.match:
 *		Driver-specific match function to be used if
 *		%V4L2_ASYNC_MATCH_CUSTOM.
 * @match.custom.priv:
 *		Driver-specific private struct with match parameters
 *		to be used if %V4L2_ASYNC_MATCH_CUSTOM.
 * @asd_list:	used to add struct v4l2_async_subdev objects to the
 *		master yestifier @asd_list
 * @list:	used to link struct v4l2_async_subdev objects, waiting to be
 *		probed, to a yestifier->waiting list
 *
 * When this struct is used as a member in a driver specific struct,
 * the driver specific struct shall contain the &struct
 * v4l2_async_subdev as its first member.
 */
struct v4l2_async_subdev {
	enum v4l2_async_match_type match_type;
	union {
		struct fwyesde_handle *fwyesde;
		const char *device_name;
		struct {
			int adapter_id;
			unsigned short address;
		} i2c;
		struct {
			bool (*match)(struct device *dev,
				      struct v4l2_async_subdev *sd);
			void *priv;
		} custom;
	} match;

	/* v4l2-async core private: yest to be used by drivers */
	struct list_head list;
	struct list_head asd_list;
};

/**
 * struct v4l2_async_yestifier_operations - Asynchroyesus V4L2 yestifier operations
 * @bound:	a subdevice driver has successfully probed one of the subdevices
 * @complete:	All subdevices have been probed successfully. The complete
 *		callback is only executed for the root yestifier.
 * @unbind:	a subdevice is leaving
 */
struct v4l2_async_yestifier_operations {
	int (*bound)(struct v4l2_async_yestifier *yestifier,
		     struct v4l2_subdev *subdev,
		     struct v4l2_async_subdev *asd);
	int (*complete)(struct v4l2_async_yestifier *yestifier);
	void (*unbind)(struct v4l2_async_yestifier *yestifier,
		       struct v4l2_subdev *subdev,
		       struct v4l2_async_subdev *asd);
};

/**
 * struct v4l2_async_yestifier - v4l2_device yestifier data
 *
 * @ops:	yestifier operations
 * @v4l2_dev:	v4l2_device of the root yestifier, NULL otherwise
 * @sd:		sub-device that registered the yestifier, NULL otherwise
 * @parent:	parent yestifier
 * @asd_list:	master list of struct v4l2_async_subdev
 * @waiting:	list of struct v4l2_async_subdev, waiting for their drivers
 * @done:	list of struct v4l2_subdev, already probed
 * @list:	member in a global list of yestifiers
 */
struct v4l2_async_yestifier {
	const struct v4l2_async_yestifier_operations *ops;
	struct v4l2_device *v4l2_dev;
	struct v4l2_subdev *sd;
	struct v4l2_async_yestifier *parent;
	struct list_head asd_list;
	struct list_head waiting;
	struct list_head done;
	struct list_head list;
};

/**
 * v4l2_async_yestifier_init - Initialize a yestifier.
 *
 * @yestifier: pointer to &struct v4l2_async_yestifier
 *
 * This function initializes the yestifier @asd_list. It must be called
 * before the first call to @v4l2_async_yestifier_add_subdev.
 */
void v4l2_async_yestifier_init(struct v4l2_async_yestifier *yestifier);

/**
 * v4l2_async_yestifier_add_subdev - Add an async subdev to the
 *				yestifier's master asd list.
 *
 * @yestifier: pointer to &struct v4l2_async_yestifier
 * @asd: pointer to &struct v4l2_async_subdev
 *
 * Call this function before registering a yestifier to link the
 * provided asd to the yestifiers master @asd_list.
 */
int v4l2_async_yestifier_add_subdev(struct v4l2_async_yestifier *yestifier,
				   struct v4l2_async_subdev *asd);

/**
 * v4l2_async_yestifier_add_fwyesde_subdev - Allocate and add a fwyesde async
 *				subdev to the yestifier's master asd_list.
 *
 * @yestifier: pointer to &struct v4l2_async_yestifier
 * @fwyesde: fwyesde handle of the sub-device to be matched
 * @asd_struct_size: size of the driver's async sub-device struct, including
 *		     sizeof(struct v4l2_async_subdev). The &struct
 *		     v4l2_async_subdev shall be the first member of
 *		     the driver's async sub-device struct, i.e. both
 *		     begin at the same memory address.
 *
 * Allocate a fwyesde-matched asd of size asd_struct_size, and add it to the
 * yestifiers @asd_list. The function also gets a reference of the fwyesde which
 * is released later at yestifier cleanup time.
 */
struct v4l2_async_subdev *
v4l2_async_yestifier_add_fwyesde_subdev(struct v4l2_async_yestifier *yestifier,
				      struct fwyesde_handle *fwyesde,
				      unsigned int asd_struct_size);

/**
 * v4l2_async_yestifier_add_fwyesde_remote_subdev - Allocate and add a fwyesde
 *						  remote async subdev to the
 *						  yestifier's master asd_list.
 *
 * @yestif: pointer to &struct v4l2_async_yestifier
 * @endpoint: local endpoint pointing to the remote sub-device to be matched
 * @asd: Async sub-device struct allocated by the caller. The &struct
 *	 v4l2_async_subdev shall be the first member of the driver's async
 *	 sub-device struct, i.e. both begin at the same memory address.
 *
 * Gets the remote endpoint of a given local endpoint, set it up for fwyesde
 * matching and adds the async sub-device to the yestifier's @asd_list. The
 * function also gets a reference of the fwyesde which is released later at
 * yestifier cleanup time.
 *
 * This is just like @v4l2_async_yestifier_add_fwyesde_subdev, but with the
 * exception that the fwyesde refers to a local endpoint, yest the remote one, and
 * the function relies on the caller to allocate the async sub-device struct.
 */
int
v4l2_async_yestifier_add_fwyesde_remote_subdev(struct v4l2_async_yestifier *yestif,
					     struct fwyesde_handle *endpoint,
					     struct v4l2_async_subdev *asd);

/**
 * v4l2_async_yestifier_add_i2c_subdev - Allocate and add an i2c async
 *				subdev to the yestifier's master asd_list.
 *
 * @yestifier: pointer to &struct v4l2_async_yestifier
 * @adapter_id: I2C adapter ID to be matched
 * @address: I2C address of sub-device to be matched
 * @asd_struct_size: size of the driver's async sub-device struct, including
 *		     sizeof(struct v4l2_async_subdev). The &struct
 *		     v4l2_async_subdev shall be the first member of
 *		     the driver's async sub-device struct, i.e. both
 *		     begin at the same memory address.
 *
 * Same as above but for I2C matched sub-devices.
 */
struct v4l2_async_subdev *
v4l2_async_yestifier_add_i2c_subdev(struct v4l2_async_yestifier *yestifier,
				   int adapter_id, unsigned short address,
				   unsigned int asd_struct_size);

/**
 * v4l2_async_yestifier_add_devname_subdev - Allocate and add a device-name
 *				async subdev to the yestifier's master asd_list.
 *
 * @yestifier: pointer to &struct v4l2_async_yestifier
 * @device_name: device name string to be matched
 * @asd_struct_size: size of the driver's async sub-device struct, including
 *		     sizeof(struct v4l2_async_subdev). The &struct
 *		     v4l2_async_subdev shall be the first member of
 *		     the driver's async sub-device struct, i.e. both
 *		     begin at the same memory address.
 *
 * Same as above but for device-name matched sub-devices.
 */
struct v4l2_async_subdev *
v4l2_async_yestifier_add_devname_subdev(struct v4l2_async_yestifier *yestifier,
				       const char *device_name,
				       unsigned int asd_struct_size);

/**
 * v4l2_async_yestifier_register - registers a subdevice asynchroyesus yestifier
 *
 * @v4l2_dev: pointer to &struct v4l2_device
 * @yestifier: pointer to &struct v4l2_async_yestifier
 */
int v4l2_async_yestifier_register(struct v4l2_device *v4l2_dev,
				 struct v4l2_async_yestifier *yestifier);

/**
 * v4l2_async_subdev_yestifier_register - registers a subdevice asynchroyesus
 *					 yestifier for a sub-device
 *
 * @sd: pointer to &struct v4l2_subdev
 * @yestifier: pointer to &struct v4l2_async_yestifier
 */
int v4l2_async_subdev_yestifier_register(struct v4l2_subdev *sd,
					struct v4l2_async_yestifier *yestifier);

/**
 * v4l2_async_yestifier_unregister - unregisters a subdevice
 *	asynchroyesus yestifier
 *
 * @yestifier: pointer to &struct v4l2_async_yestifier
 */
void v4l2_async_yestifier_unregister(struct v4l2_async_yestifier *yestifier);

/**
 * v4l2_async_yestifier_cleanup - clean up yestifier resources
 * @yestifier: the yestifier the resources of which are to be cleaned up
 *
 * Release memory resources related to a yestifier, including the async
 * sub-devices allocated for the purposes of the yestifier but yest the yestifier
 * itself. The user is responsible for calling this function to clean up the
 * yestifier after calling
 * @v4l2_async_yestifier_add_subdev,
 * @v4l2_async_yestifier_parse_fwyesde_endpoints or
 * @v4l2_fwyesde_reference_parse_sensor_common.
 *
 * There is yes harm from calling v4l2_async_yestifier_cleanup in other
 * cases as long as its memory has been zeroed after it has been
 * allocated.
 */
void v4l2_async_yestifier_cleanup(struct v4l2_async_yestifier *yestifier);

/**
 * v4l2_async_register_subdev - registers a sub-device to the asynchroyesus
 *	subdevice framework
 *
 * @sd: pointer to &struct v4l2_subdev
 */
int v4l2_async_register_subdev(struct v4l2_subdev *sd);

/**
 * v4l2_async_register_subdev_sensor_common - registers a sensor sub-device to
 *					      the asynchroyesus sub-device
 *					      framework and parse set up common
 *					      sensor related devices
 *
 * @sd: pointer to struct &v4l2_subdev
 *
 * This function is just like v4l2_async_register_subdev() with the exception
 * that calling it will also parse firmware interfaces for remote references
 * using v4l2_async_yestifier_parse_fwyesde_sensor_common() and registers the
 * async sub-devices. The sub-device is similarly unregistered by calling
 * v4l2_async_unregister_subdev().
 *
 * While registered, the subdev module is marked as in-use.
 *
 * An error is returned if the module is yes longer loaded on any attempts
 * to register it.
 */
int __must_check
v4l2_async_register_subdev_sensor_common(struct v4l2_subdev *sd);

/**
 * v4l2_async_unregister_subdev - unregisters a sub-device to the asynchroyesus
 *	subdevice framework
 *
 * @sd: pointer to &struct v4l2_subdev
 */
void v4l2_async_unregister_subdev(struct v4l2_subdev *sd);
#endif
