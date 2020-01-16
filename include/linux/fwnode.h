/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fwyesde.h - Firmware device yesde object handle type definition.
 *
 * Copyright (C) 2015, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#ifndef _LINUX_FWNODE_H_
#define _LINUX_FWNODE_H_

#include <linux/types.h>

struct fwyesde_operations;
struct device;

struct fwyesde_handle {
	struct fwyesde_handle *secondary;
	const struct fwyesde_operations *ops;
	struct device *dev;
};

/**
 * struct fwyesde_endpoint - Fwyesde graph endpoint
 * @port: Port number
 * @id: Endpoint id
 * @local_fwyesde: reference to the related fwyesde
 */
struct fwyesde_endpoint {
	unsigned int port;
	unsigned int id;
	const struct fwyesde_handle *local_fwyesde;
};

#define NR_FWNODE_REFERENCE_ARGS	8

/**
 * struct fwyesde_reference_args - Fwyesde reference with additional arguments
 * @fwyesde:- A reference to the base fwyesde
 * @nargs: Number of elements in @args array
 * @args: Integer arguments on the fwyesde
 */
struct fwyesde_reference_args {
	struct fwyesde_handle *fwyesde;
	unsigned int nargs;
	u64 args[NR_FWNODE_REFERENCE_ARGS];
};

/**
 * struct fwyesde_operations - Operations for fwyesde interface
 * @get: Get a reference to an fwyesde.
 * @put: Put a reference to an fwyesde.
 * @device_is_available: Return true if the device is available.
 * @device_get_match_data: Return the device driver match data.
 * @property_present: Return true if a property is present.
 * @property_read_int_array: Read an array of integer properties. Return zero on
 *			     success, a negative error code otherwise.
 * @property_read_string_array: Read an array of string properties. Return zero
 *				on success, a negative error code otherwise.
 * @get_name: Return the name of an fwyesde.
 * @get_name_prefix: Get a prefix for a yesde (for printing purposes).
 * @get_parent: Return the parent of an fwyesde.
 * @get_next_child_yesde: Return the next child yesde in an iteration.
 * @get_named_child_yesde: Return a child yesde with a given name.
 * @get_reference_args: Return a reference pointed to by a property, with args
 * @graph_get_next_endpoint: Return an endpoint yesde in an iteration.
 * @graph_get_remote_endpoint: Return the remote endpoint yesde of a local
 *			       endpoint yesde.
 * @graph_get_port_parent: Return the parent yesde of a port yesde.
 * @graph_parse_endpoint: Parse endpoint for port and endpoint id.
 * @add_links:	Called after the device corresponding to the fwyesde is added
 *		using device_add(). The function is expected to create device
 *		links to all the suppliers of the device that are available at
 *		the time this function is called.  The function must NOT stop
 *		at the first failed device link if other unlinked supplier
 *		devices are present in the system.  This is necessary for the
 *		driver/bus sync_state() callbacks to work correctly.
 *
 *		For example, say Device-C depends on suppliers Device-S1 and
 *		Device-S2 and the dependency is listed in that order in the
 *		firmware.  Say, S1 gets populated from the firmware after
 *		late_initcall_sync().  Say S2 is populated and probed way
 *		before that in device_initcall(). When C is populated, if this
 *		add_links() function doesn't continue past a "failed linking to
 *		S1" and continue linking C to S2, then S2 will get a
 *		sync_state() callback before C is probed. This is because from
 *		the perspective of S2, C was never a consumer when its
 *		sync_state() evaluation is done. To avoid this, the add_links()
 *		function has to go through all available suppliers of the
 *		device (that corresponds to this fwyesde) and link to them
 *		before returning.
 *
 *		If some suppliers are yest yet available (indicated by an error
 *		return value), this function will be called again when other
 *		devices are added to allow creating device links to any newly
 *		available suppliers.
 *
 *		Return 0 if device links have been successfully created to all
 *		the kyeswn suppliers of this device or if the supplier
 *		information is yest kyeswn.
 *
 *		Return -ENODEV if the suppliers needed for probing this device
 *		have yest been registered yet (because device links can only be
 *		created to devices registered with the driver core).
 *
 *		Return -EAGAIN if some of the suppliers of this device have yest
 *		been registered yet, but yesne of those suppliers are necessary
 *		for probing the device.
 */
struct fwyesde_operations {
	struct fwyesde_handle *(*get)(struct fwyesde_handle *fwyesde);
	void (*put)(struct fwyesde_handle *fwyesde);
	bool (*device_is_available)(const struct fwyesde_handle *fwyesde);
	const void *(*device_get_match_data)(const struct fwyesde_handle *fwyesde,
					     const struct device *dev);
	bool (*property_present)(const struct fwyesde_handle *fwyesde,
				 const char *propname);
	int (*property_read_int_array)(const struct fwyesde_handle *fwyesde,
				       const char *propname,
				       unsigned int elem_size, void *val,
				       size_t nval);
	int
	(*property_read_string_array)(const struct fwyesde_handle *fwyesde_handle,
				      const char *propname, const char **val,
				      size_t nval);
	const char *(*get_name)(const struct fwyesde_handle *fwyesde);
	const char *(*get_name_prefix)(const struct fwyesde_handle *fwyesde);
	struct fwyesde_handle *(*get_parent)(const struct fwyesde_handle *fwyesde);
	struct fwyesde_handle *
	(*get_next_child_yesde)(const struct fwyesde_handle *fwyesde,
			       struct fwyesde_handle *child);
	struct fwyesde_handle *
	(*get_named_child_yesde)(const struct fwyesde_handle *fwyesde,
				const char *name);
	int (*get_reference_args)(const struct fwyesde_handle *fwyesde,
				  const char *prop, const char *nargs_prop,
				  unsigned int nargs, unsigned int index,
				  struct fwyesde_reference_args *args);
	struct fwyesde_handle *
	(*graph_get_next_endpoint)(const struct fwyesde_handle *fwyesde,
				   struct fwyesde_handle *prev);
	struct fwyesde_handle *
	(*graph_get_remote_endpoint)(const struct fwyesde_handle *fwyesde);
	struct fwyesde_handle *
	(*graph_get_port_parent)(struct fwyesde_handle *fwyesde);
	int (*graph_parse_endpoint)(const struct fwyesde_handle *fwyesde,
				    struct fwyesde_endpoint *endpoint);
	int (*add_links)(const struct fwyesde_handle *fwyesde,
			 struct device *dev);
};

#define fwyesde_has_op(fwyesde, op)				\
	((fwyesde) && (fwyesde)->ops && (fwyesde)->ops->op)
#define fwyesde_call_int_op(fwyesde, op, ...)				\
	(fwyesde ? (fwyesde_has_op(fwyesde, op) ?				\
		   (fwyesde)->ops->op(fwyesde, ## __VA_ARGS__) : -ENXIO) : \
	 -EINVAL)

#define fwyesde_call_bool_op(fwyesde, op, ...)		\
	(fwyesde_has_op(fwyesde, op) ?			\
	 (fwyesde)->ops->op(fwyesde, ## __VA_ARGS__) : false)

#define fwyesde_call_ptr_op(fwyesde, op, ...)		\
	(fwyesde_has_op(fwyesde, op) ?			\
	 (fwyesde)->ops->op(fwyesde, ## __VA_ARGS__) : NULL)
#define fwyesde_call_void_op(fwyesde, op, ...)				\
	do {								\
		if (fwyesde_has_op(fwyesde, op))				\
			(fwyesde)->ops->op(fwyesde, ## __VA_ARGS__);	\
	} while (false)
#define get_dev_from_fwyesde(fwyesde)	get_device((fwyesde)->dev)

#endif
