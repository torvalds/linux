/*
 * fwnode.h - Firmware device node object handle type definition.
 *
 * Copyright (C) 2015, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_FWNODE_H_
#define _LINUX_FWNODE_H_

#include <linux/types.h>

enum fwnode_type {
	FWNODE_INVALID = 0,
	FWNODE_OF,
	FWNODE_ACPI,
	FWNODE_ACPI_DATA,
	FWNODE_ACPI_STATIC,
	FWNODE_PDATA,
	FWNODE_IRQCHIP
};

struct fwnode_operations;

struct fwnode_handle {
	enum fwnode_type type;
	struct fwnode_handle *secondary;
	const struct fwnode_operations *ops;
};

/**
 * struct fwnode_endpoint - Fwnode graph endpoint
 * @port: Port number
 * @id: Endpoint id
 * @local_fwnode: reference to the related fwnode
 */
struct fwnode_endpoint {
	unsigned int port;
	unsigned int id;
	const struct fwnode_handle *local_fwnode;
};

/**
 * struct fwnode_operations - Operations for fwnode interface
 * @get: Get a reference to an fwnode.
 * @put: Put a reference to an fwnode.
 * @property_present: Return true if a property is present.
 * @property_read_integer_array: Read an array of integer properties. Return
 *				 zero on success, a negative error code
 *				 otherwise.
 * @property_read_string_array: Read an array of string properties. Return zero
 *				on success, a negative error code otherwise.
 * @get_parent: Return the parent of an fwnode.
 * @get_next_child_node: Return the next child node in an iteration.
 * @get_named_child_node: Return a child node with a given name.
 * @graph_get_next_endpoint: Return an endpoint node in an iteration.
 * @graph_get_remote_endpoint: Return the remote endpoint node of a local
 *			       endpoint node.
 * @graph_get_port_parent: Return the parent node of a port node.
 * @graph_parse_endpoint: Parse endpoint for port and endpoint id.
 */
struct fwnode_operations {
	void (*get)(struct fwnode_handle *fwnode);
	void (*put)(struct fwnode_handle *fwnode);
	bool (*device_is_available)(struct fwnode_handle *fwnode);
	bool (*property_present)(struct fwnode_handle *fwnode,
				 const char *propname);
	int (*property_read_int_array)(struct fwnode_handle *fwnode,
				       const char *propname,
				       unsigned int elem_size, void *val,
				       size_t nval);
	int (*property_read_string_array)(struct fwnode_handle *fwnode_handle,
					  const char *propname,
					  const char **val, size_t nval);
	struct fwnode_handle *(*get_parent)(struct fwnode_handle *fwnode);
	struct fwnode_handle *
	(*get_next_child_node)(struct fwnode_handle *fwnode,
			       struct fwnode_handle *child);
	struct fwnode_handle *
	(*get_named_child_node)(struct fwnode_handle *fwnode, const char *name);
	struct fwnode_handle *
	(*graph_get_next_endpoint)(struct fwnode_handle *fwnode,
				   struct fwnode_handle *prev);
	struct fwnode_handle *
	(*graph_get_remote_endpoint)(struct fwnode_handle *fwnode);
	struct fwnode_handle *
	(*graph_get_port_parent)(struct fwnode_handle *fwnode);
	int (*graph_parse_endpoint)(struct fwnode_handle *fwnode,
				    struct fwnode_endpoint *endpoint);
};

#define fwnode_has_op(fwnode, op)				\
	((fwnode) && (fwnode)->ops && (fwnode)->ops->op)
#define fwnode_call_int_op(fwnode, op, ...)				\
	(fwnode ? (fwnode_has_op(fwnode, op) ?				\
		   (fwnode)->ops->op(fwnode, ## __VA_ARGS__) : -ENXIO) : \
	 -EINVAL)
#define fwnode_call_bool_op(fwnode, op, ...)				\
	(fwnode ? (fwnode_has_op(fwnode, op) ?				\
		   (fwnode)->ops->op(fwnode, ## __VA_ARGS__) : false) : \
	 false)
#define fwnode_call_ptr_op(fwnode, op, ...)		\
	(fwnode_has_op(fwnode, op) ?			\
	 (fwnode)->ops->op(fwnode, ## __VA_ARGS__) : NULL)
#define fwnode_call_void_op(fwnode, op, ...)				\
	do {								\
		if (fwnode_has_op(fwnode, op))				\
			(fwnode)->ops->op(fwnode, ## __VA_ARGS__);	\
	} while (false)

#endif
