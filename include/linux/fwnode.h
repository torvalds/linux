/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fwnode.h - Firmware device node object handle type definition.
 *
 * This header file provides low-level data types and definitions for firmware
 * and device property providers. The respective API header files supplied by
 * them should contain all of the requisite data types and definitions for end
 * users, so including it directly should not be necessary.
 *
 * Copyright (C) 2015, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#ifndef _LINUX_FWNODE_H_
#define _LINUX_FWNODE_H_

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/types.h>

enum dev_dma_attr {
	DEV_DMA_NOT_SUPPORTED,
	DEV_DMA_NON_COHERENT,
	DEV_DMA_COHERENT,
};

struct fwnode_operations;
struct device;

/*
 * fwnode flags
 *
 * LINKS_ADDED:	The fwnode has already be parsed to add fwnode links.
 * NOT_DEVICE:	The fwnode will never be populated as a struct device.
 * INITIALIZED: The hardware corresponding to fwnode has been initialized.
 * NEEDS_CHILD_BOUND_ON_ADD: For this fwnode/device to probe successfully, its
 *			     driver needs its child devices to be bound with
 *			     their respective drivers as soon as they are
 *			     added.
 * BEST_EFFORT: The fwnode/device needs to probe early and might be missing some
 *		suppliers. Only enforce ordering with suppliers that have
 *		drivers.
 */
#define FWNODE_FLAG_LINKS_ADDED			BIT(0)
#define FWNODE_FLAG_NOT_DEVICE			BIT(1)
#define FWNODE_FLAG_INITIALIZED			BIT(2)
#define FWNODE_FLAG_NEEDS_CHILD_BOUND_ON_ADD	BIT(3)
#define FWNODE_FLAG_BEST_EFFORT			BIT(4)
#define FWNODE_FLAG_VISITED			BIT(5)

struct fwnode_handle {
	struct fwnode_handle *secondary;
	const struct fwnode_operations *ops;

	/* The below is used solely by device links, don't use otherwise */
	struct device *dev;
	struct list_head suppliers;
	struct list_head consumers;
	u8 flags;
};

/*
 * fwnode link flags
 *
 * CYCLE:	The fwnode link is part of a cycle. Don't defer probe.
 * IGNORE:	Completely ignore this link, even during cycle detection.
 */
#define FWLINK_FLAG_CYCLE			BIT(0)
#define FWLINK_FLAG_IGNORE			BIT(1)

struct fwnode_link {
	struct fwnode_handle *supplier;
	struct list_head s_hook;
	struct fwnode_handle *consumer;
	struct list_head c_hook;
	u8 flags;
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

/*
 * ports and endpoints defined as software_nodes should all follow a common
 * naming scheme; use these macros to ensure commonality.
 */
#define SWNODE_GRAPH_PORT_NAME_FMT		"port@%u"
#define SWNODE_GRAPH_ENDPOINT_NAME_FMT		"endpoint@%u"

#define NR_FWNODE_REFERENCE_ARGS	16

/**
 * struct fwnode_reference_args - Fwnode reference with additional arguments
 * @fwnode:- A reference to the base fwnode
 * @nargs: Number of elements in @args array
 * @args: Integer arguments on the fwnode
 */
struct fwnode_reference_args {
	struct fwnode_handle *fwnode;
	unsigned int nargs;
	u64 args[NR_FWNODE_REFERENCE_ARGS];
};

/**
 * struct fwnode_operations - Operations for fwnode interface
 * @get: Get a reference to an fwnode.
 * @put: Put a reference to an fwnode.
 * @device_is_available: Return true if the device is available.
 * @device_get_match_data: Return the device driver match data.
 * @property_present: Return true if a property is present.
 * @property_read_bool: Return a boolean property value.
 * @property_read_int_array: Read an array of integer properties. Return zero on
 *			     success, a negative error code otherwise.
 * @property_read_string_array: Read an array of string properties. Return zero
 *				on success, a negative error code otherwise.
 * @get_name: Return the name of an fwnode.
 * @get_name_prefix: Get a prefix for a node (for printing purposes).
 * @get_parent: Return the parent of an fwnode.
 * @get_next_child_node: Return the next child node in an iteration.
 * @get_named_child_node: Return a child node with a given name.
 * @get_reference_args: Return a reference pointed to by a property, with args
 * @graph_get_next_endpoint: Return an endpoint node in an iteration.
 * @graph_get_remote_endpoint: Return the remote endpoint node of a local
 *			       endpoint node.
 * @graph_get_port_parent: Return the parent node of a port node.
 * @graph_parse_endpoint: Parse endpoint for port and endpoint id.
 * @add_links:	Create fwnode links to all the suppliers of the fwnode. Return
 *		zero on success, a negative error code otherwise.
 */
struct fwnode_operations {
	struct fwnode_handle *(*get)(struct fwnode_handle *fwnode);
	void (*put)(struct fwnode_handle *fwnode);
	bool (*device_is_available)(const struct fwnode_handle *fwnode);
	const void *(*device_get_match_data)(const struct fwnode_handle *fwnode,
					     const struct device *dev);
	bool (*device_dma_supported)(const struct fwnode_handle *fwnode);
	enum dev_dma_attr
	(*device_get_dma_attr)(const struct fwnode_handle *fwnode);
	bool (*property_present)(const struct fwnode_handle *fwnode,
				 const char *propname);
	bool (*property_read_bool)(const struct fwnode_handle *fwnode,
				   const char *propname);
	int (*property_read_int_array)(const struct fwnode_handle *fwnode,
				       const char *propname,
				       unsigned int elem_size, void *val,
				       size_t nval);
	int
	(*property_read_string_array)(const struct fwnode_handle *fwnode_handle,
				      const char *propname, const char **val,
				      size_t nval);
	const char *(*get_name)(const struct fwnode_handle *fwnode);
	const char *(*get_name_prefix)(const struct fwnode_handle *fwnode);
	struct fwnode_handle *(*get_parent)(const struct fwnode_handle *fwnode);
	struct fwnode_handle *
	(*get_next_child_node)(const struct fwnode_handle *fwnode,
			       struct fwnode_handle *child);
	struct fwnode_handle *
	(*get_named_child_node)(const struct fwnode_handle *fwnode,
				const char *name);
	int (*get_reference_args)(const struct fwnode_handle *fwnode,
				  const char *prop, const char *nargs_prop,
				  unsigned int nargs, unsigned int index,
				  struct fwnode_reference_args *args);
	struct fwnode_handle *
	(*graph_get_next_endpoint)(const struct fwnode_handle *fwnode,
				   struct fwnode_handle *prev);
	struct fwnode_handle *
	(*graph_get_remote_endpoint)(const struct fwnode_handle *fwnode);
	struct fwnode_handle *
	(*graph_get_port_parent)(struct fwnode_handle *fwnode);
	int (*graph_parse_endpoint)(const struct fwnode_handle *fwnode,
				    struct fwnode_endpoint *endpoint);
	void __iomem *(*iomap)(struct fwnode_handle *fwnode, int index);
	int (*irq_get)(const struct fwnode_handle *fwnode, unsigned int index);
	int (*add_links)(struct fwnode_handle *fwnode);
};

#define fwnode_has_op(fwnode, op)					\
	(!IS_ERR_OR_NULL(fwnode) && (fwnode)->ops && (fwnode)->ops->op)

#define fwnode_call_int_op(fwnode, op, ...)				\
	(fwnode_has_op(fwnode, op) ?					\
	 (fwnode)->ops->op(fwnode, ## __VA_ARGS__) : (IS_ERR_OR_NULL(fwnode) ? -EINVAL : -ENXIO))

#define fwnode_call_bool_op(fwnode, op, ...)		\
	(fwnode_has_op(fwnode, op) ?			\
	 (fwnode)->ops->op(fwnode, ## __VA_ARGS__) : false)

#define fwnode_call_ptr_op(fwnode, op, ...)		\
	(fwnode_has_op(fwnode, op) ?			\
	 (fwnode)->ops->op(fwnode, ## __VA_ARGS__) : NULL)
#define fwnode_call_void_op(fwnode, op, ...)				\
	do {								\
		if (fwnode_has_op(fwnode, op))				\
			(fwnode)->ops->op(fwnode, ## __VA_ARGS__);	\
	} while (false)

static inline void fwnode_init(struct fwnode_handle *fwnode,
			       const struct fwnode_operations *ops)
{
	fwnode->ops = ops;
	INIT_LIST_HEAD(&fwnode->consumers);
	INIT_LIST_HEAD(&fwnode->suppliers);
}

static inline void fwnode_dev_initialized(struct fwnode_handle *fwnode,
					  bool initialized)
{
	if (IS_ERR_OR_NULL(fwnode))
		return;

	if (initialized)
		fwnode->flags |= FWNODE_FLAG_INITIALIZED;
	else
		fwnode->flags &= ~FWNODE_FLAG_INITIALIZED;
}

int fwnode_link_add(struct fwnode_handle *con, struct fwnode_handle *sup,
		    u8 flags);
void fwnode_links_purge(struct fwnode_handle *fwnode);
void fw_devlink_purge_absent_suppliers(struct fwnode_handle *fwnode);
bool fw_devlink_is_strict(void);

#endif
