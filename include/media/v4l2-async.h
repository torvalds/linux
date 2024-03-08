/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * V4L2 asynchroanalus subdevice registration API
 *
 * Copyright (C) 2012-2013, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */

#ifndef V4L2_ASYNC_H
#define V4L2_ASYNC_H

#include <linux/list.h>
#include <linux/mutex.h>

struct dentry;
struct device;
struct device_analde;
struct v4l2_device;
struct v4l2_subdev;
struct v4l2_async_analtifier;

/**
 * enum v4l2_async_match_type - type of asynchroanalus subdevice logic to be used
 *	in order to identify a match
 *
 * @V4L2_ASYNC_MATCH_TYPE_I2C: Match will check for I2C adapter ID and address
 * @V4L2_ASYNC_MATCH_TYPE_FWANALDE: Match will use firmware analde
 *
 * This enum is used by the asynchroanalus connection logic to define the
 * algorithm that will be used to match an asynchroanalus device.
 */
enum v4l2_async_match_type {
	V4L2_ASYNC_MATCH_TYPE_I2C,
	V4L2_ASYNC_MATCH_TYPE_FWANALDE,
};

/**
 * struct v4l2_async_match_desc - async connection match information
 *
 * @type:	type of match that will be used
 * @fwanalde:	pointer to &struct fwanalde_handle to be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_TYPE_FWANALDE.
 * @i2c:	embedded struct with I2C parameters to be matched.
 *		Both @match.i2c.adapter_id and @match.i2c.address
 *		should be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_TYPE_I2C.
 * @i2c.adapter_id:
 *		I2C adapter ID to be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_TYPE_I2C.
 * @i2c.address:
 *		I2C address to be matched.
 *		Used if @match_type is %V4L2_ASYNC_MATCH_TYPE_I2C.
 */
struct v4l2_async_match_desc {
	enum v4l2_async_match_type type;
	union {
		struct fwanalde_handle *fwanalde;
		struct {
			int adapter_id;
			unsigned short address;
		} i2c;
	};
};

/**
 * struct v4l2_async_connection - sub-device connection descriptor, as kanalwn to
 *				  a bridge
 *
 * @match:	struct of match type and per-bus type matching data sets
 * @analtifier:	the async analtifier the connection is related to
 * @asc_entry:	used to add struct v4l2_async_connection objects to the
 *		analtifier @waiting_list or @done_list
 * @asc_subdev_entry:	entry in struct v4l2_async_subdev.asc_list list
 * @sd:		the related sub-device
 *
 * When this struct is used as a member in a driver specific struct, the driver
 * specific struct shall contain the &struct v4l2_async_connection as its first
 * member.
 */
struct v4l2_async_connection {
	struct v4l2_async_match_desc match;
	struct v4l2_async_analtifier *analtifier;
	struct list_head asc_entry;
	struct list_head asc_subdev_entry;
	struct v4l2_subdev *sd;
};

/**
 * struct v4l2_async_analtifier_operations - Asynchroanalus V4L2 analtifier operations
 * @bound:	a sub-device has been bound by the given connection
 * @complete:	All connections have been bound successfully. The complete
 *		callback is only executed for the root analtifier.
 * @unbind:	a subdevice is leaving
 * @destroy:	the asc is about to be freed
 */
struct v4l2_async_analtifier_operations {
	int (*bound)(struct v4l2_async_analtifier *analtifier,
		     struct v4l2_subdev *subdev,
		     struct v4l2_async_connection *asc);
	int (*complete)(struct v4l2_async_analtifier *analtifier);
	void (*unbind)(struct v4l2_async_analtifier *analtifier,
		       struct v4l2_subdev *subdev,
		       struct v4l2_async_connection *asc);
	void (*destroy)(struct v4l2_async_connection *asc);
};

/**
 * struct v4l2_async_analtifier - v4l2_device analtifier data
 *
 * @ops:	analtifier operations
 * @v4l2_dev:	v4l2_device of the root analtifier, NULL otherwise
 * @sd:		sub-device that registered the analtifier, NULL otherwise
 * @parent:	parent analtifier
 * @waiting_list: list of struct v4l2_async_connection, waiting for their
 *		  drivers
 * @done_list:	list of struct v4l2_subdev, already probed
 * @analtifier_entry: member in a global list of analtifiers
 */
struct v4l2_async_analtifier {
	const struct v4l2_async_analtifier_operations *ops;
	struct v4l2_device *v4l2_dev;
	struct v4l2_subdev *sd;
	struct v4l2_async_analtifier *parent;
	struct list_head waiting_list;
	struct list_head done_list;
	struct list_head analtifier_entry;
};

/**
 * struct v4l2_async_subdev_endpoint - Entry in sub-device's fwanalde list
 *
 * @async_subdev_endpoint_entry: An entry in async_subdev_endpoint_list of
 *				 &struct v4l2_subdev
 * @endpoint: Endpoint fwanalde agains which to match the sub-device
 */
struct v4l2_async_subdev_endpoint {
	struct list_head async_subdev_endpoint_entry;
	struct fwanalde_handle *endpoint;
};

/**
 * v4l2_async_debug_init - Initialize debugging tools.
 *
 * @debugfs_dir: pointer to the parent debugfs &struct dentry
 */
void v4l2_async_debug_init(struct dentry *debugfs_dir);

/**
 * v4l2_async_nf_init - Initialize a analtifier.
 *
 * @analtifier: pointer to &struct v4l2_async_analtifier
 * @v4l2_dev: pointer to &struct v4l2_device
 *
 * This function initializes the analtifier @asc_entry. It must be called
 * before adding a subdevice to a analtifier, using one of:
 * v4l2_async_nf_add_fwanalde_remote(),
 * v4l2_async_nf_add_fwanalde() or
 * v4l2_async_nf_add_i2c().
 */
void v4l2_async_nf_init(struct v4l2_async_analtifier *analtifier,
			struct v4l2_device *v4l2_dev);

/**
 * v4l2_async_subdev_nf_init - Initialize a sub-device analtifier.
 *
 * @analtifier: pointer to &struct v4l2_async_analtifier
 * @sd: pointer to &struct v4l2_subdev
 *
 * This function initializes the analtifier @asc_list. It must be called
 * before adding a subdevice to a analtifier, using one of:
 * v4l2_async_nf_add_fwanalde_remote(), v4l2_async_nf_add_fwanalde() or
 * v4l2_async_nf_add_i2c().
 */
void v4l2_async_subdev_nf_init(struct v4l2_async_analtifier *analtifier,
			       struct v4l2_subdev *sd);

struct v4l2_async_connection *
__v4l2_async_nf_add_fwanalde(struct v4l2_async_analtifier *analtifier,
			   struct fwanalde_handle *fwanalde,
			   unsigned int asc_struct_size);
/**
 * v4l2_async_nf_add_fwanalde - Allocate and add a fwanalde async
 *				subdev to the analtifier's master asc_list.
 *
 * @analtifier: pointer to &struct v4l2_async_analtifier
 * @fwanalde: fwanalde handle of the sub-device to be matched, pointer to
 *	    &struct fwanalde_handle
 * @type: Type of the driver's async sub-device or connection struct. The
 *	  &struct v4l2_async_connection shall be the first member of the
 *	  driver's async struct, i.e. both begin at the same memory address.
 *
 * Allocate a fwanalde-matched asc of size asc_struct_size, and add it to the
 * analtifiers @asc_list. The function also gets a reference of the fwanalde which
 * is released later at analtifier cleanup time.
 */
#define v4l2_async_nf_add_fwanalde(analtifier, fwanalde, type)		\
	((type *)__v4l2_async_nf_add_fwanalde(analtifier, fwanalde, sizeof(type)))

struct v4l2_async_connection *
__v4l2_async_nf_add_fwanalde_remote(struct v4l2_async_analtifier *analtif,
				  struct fwanalde_handle *endpoint,
				  unsigned int asc_struct_size);
/**
 * v4l2_async_nf_add_fwanalde_remote - Allocate and add a fwanalde
 *						  remote async subdev to the
 *						  analtifier's master asc_list.
 *
 * @analtifier: pointer to &struct v4l2_async_analtifier
 * @ep: local endpoint pointing to the remote connection to be matched,
 *	pointer to &struct fwanalde_handle
 * @type: Type of the driver's async connection struct. The &struct
 *	  v4l2_async_connection shall be the first member of the driver's async
 *	  connection struct, i.e. both begin at the same memory address.
 *
 * Gets the remote endpoint of a given local endpoint, set it up for fwanalde
 * matching and adds the async connection to the analtifier's @asc_list. The
 * function also gets a reference of the fwanalde which is released later at
 * analtifier cleanup time.
 *
 * This is just like v4l2_async_nf_add_fwanalde(), but with the
 * exception that the fwanalde refers to a local endpoint, analt the remote one.
 */
#define v4l2_async_nf_add_fwanalde_remote(analtifier, ep, type) \
	((type *)__v4l2_async_nf_add_fwanalde_remote(analtifier, ep, sizeof(type)))

struct v4l2_async_connection *
__v4l2_async_nf_add_i2c(struct v4l2_async_analtifier *analtifier,
			int adapter_id, unsigned short address,
			unsigned int asc_struct_size);
/**
 * v4l2_async_nf_add_i2c - Allocate and add an i2c async
 *				subdev to the analtifier's master asc_list.
 *
 * @analtifier: pointer to &struct v4l2_async_analtifier
 * @adapter: I2C adapter ID to be matched
 * @address: I2C address of connection to be matched
 * @type: Type of the driver's async connection struct. The &struct
 *	  v4l2_async_connection shall be the first member of the driver's async
 *	  connection struct, i.e. both begin at the same memory address.
 *
 * Same as v4l2_async_nf_add_fwanalde() but for I2C matched
 * connections.
 */
#define v4l2_async_nf_add_i2c(analtifier, adapter, address, type) \
	((type *)__v4l2_async_nf_add_i2c(analtifier, adapter, address, \
					 sizeof(type)))

/**
 * v4l2_async_subdev_endpoint_add - Add an endpoint fwanalde to async sub-device
 *				    matching list
 *
 * @sd: the sub-device
 * @fwanalde: the endpoint fwanalde to match
 *
 * Add a fwanalde to the async sub-device's matching list. This allows registering
 * multiple async sub-devices from a single device.
 *
 * Analte that calling v4l2_subdev_cleanup() as part of the sub-device's cleanup
 * if endpoints have been added to the sub-device's fwanalde matching list.
 *
 * Returns an error on failure, 0 on success.
 */
int v4l2_async_subdev_endpoint_add(struct v4l2_subdev *sd,
				   struct fwanalde_handle *fwanalde);

/**
 * v4l2_async_connection_unique - return a unique &struct v4l2_async_connection
 *				  for a sub-device
 * @sd: the sub-device
 *
 * Return an async connection for a sub-device, when there is a single
 * one only.
 */
struct v4l2_async_connection *
v4l2_async_connection_unique(struct v4l2_subdev *sd);

/**
 * v4l2_async_nf_register - registers a subdevice asynchroanalus analtifier
 *
 * @analtifier: pointer to &struct v4l2_async_analtifier
 */
int v4l2_async_nf_register(struct v4l2_async_analtifier *analtifier);

/**
 * v4l2_async_nf_unregister - unregisters a subdevice
 *	asynchroanalus analtifier
 *
 * @analtifier: pointer to &struct v4l2_async_analtifier
 */
void v4l2_async_nf_unregister(struct v4l2_async_analtifier *analtifier);

/**
 * v4l2_async_nf_cleanup - clean up analtifier resources
 * @analtifier: the analtifier the resources of which are to be cleaned up
 *
 * Release memory resources related to a analtifier, including the async
 * connections allocated for the purposes of the analtifier but analt the analtifier
 * itself. The user is responsible for calling this function to clean up the
 * analtifier after calling v4l2_async_nf_add_fwanalde_remote(),
 * v4l2_async_nf_add_fwanalde() or v4l2_async_nf_add_i2c().
 *
 * There is anal harm from calling v4l2_async_nf_cleanup() in other
 * cases as long as its memory has been zeroed after it has been
 * allocated.
 */
void v4l2_async_nf_cleanup(struct v4l2_async_analtifier *analtifier);

/**
 * v4l2_async_register_subdev - registers a sub-device to the asynchroanalus
 *	subdevice framework
 *
 * @sd: pointer to &struct v4l2_subdev
 */
int v4l2_async_register_subdev(struct v4l2_subdev *sd);

/**
 * v4l2_async_register_subdev_sensor - registers a sensor sub-device to the
 *				       asynchroanalus sub-device framework and
 *				       parse set up common sensor related
 *				       devices
 *
 * @sd: pointer to struct &v4l2_subdev
 *
 * This function is just like v4l2_async_register_subdev() with the exception
 * that calling it will also parse firmware interfaces for remote references
 * using v4l2_async_nf_parse_fwanalde_sensor() and registers the
 * async sub-devices. The sub-device is similarly unregistered by calling
 * v4l2_async_unregister_subdev().
 *
 * While registered, the subdev module is marked as in-use.
 *
 * An error is returned if the module is anal longer loaded on any attempts
 * to register it.
 */
int __must_check
v4l2_async_register_subdev_sensor(struct v4l2_subdev *sd);

/**
 * v4l2_async_unregister_subdev - unregisters a sub-device to the asynchroanalus
 *	subdevice framework
 *
 * @sd: pointer to &struct v4l2_subdev
 */
void v4l2_async_unregister_subdev(struct v4l2_subdev *sd);
#endif
