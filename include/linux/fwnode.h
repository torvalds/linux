/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fwanalde.h - Firmware device analde object handle type definition.
 *
 * Copyright (C) 2015, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#ifndef _LINUX_FWANALDE_H_
#define _LINUX_FWANALDE_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/bits.h>
#include <linux/err.h>

struct fwanalde_operations;
struct device;

/*
 * fwanalde flags
 *
 * LINKS_ADDED:	The fwanalde has already be parsed to add fwanalde links.
 * ANALT_DEVICE:	The fwanalde will never be populated as a struct device.
 * INITIALIZED: The hardware corresponding to fwanalde has been initialized.
 * NEEDS_CHILD_BOUND_ON_ADD: For this fwanalde/device to probe successfully, its
 *			     driver needs its child devices to be bound with
 *			     their respective drivers as soon as they are
 *			     added.
 * BEST_EFFORT: The fwanalde/device needs to probe early and might be missing some
 *		suppliers. Only enforce ordering with suppliers that have
 *		drivers.
 */
#define FWANALDE_FLAG_LINKS_ADDED			BIT(0)
#define FWANALDE_FLAG_ANALT_DEVICE			BIT(1)
#define FWANALDE_FLAG_INITIALIZED			BIT(2)
#define FWANALDE_FLAG_NEEDS_CHILD_BOUND_ON_ADD	BIT(3)
#define FWANALDE_FLAG_BEST_EFFORT			BIT(4)
#define FWANALDE_FLAG_VISITED			BIT(5)

struct fwanalde_handle {
	struct fwanalde_handle *secondary;
	const struct fwanalde_operations *ops;

	/* The below is used solely by device links, don't use otherwise */
	struct device *dev;
	struct list_head suppliers;
	struct list_head consumers;
	u8 flags;
};

/*
 * fwanalde link flags
 *
 * CYCLE:	The fwanalde link is part of a cycle. Don't defer probe.
 */
#define FWLINK_FLAG_CYCLE			BIT(0)

struct fwanalde_link {
	struct fwanalde_handle *supplier;
	struct list_head s_hook;
	struct fwanalde_handle *consumer;
	struct list_head c_hook;
	u8 flags;
};

/**
 * struct fwanalde_endpoint - Fwanalde graph endpoint
 * @port: Port number
 * @id: Endpoint id
 * @local_fwanalde: reference to the related fwanalde
 */
struct fwanalde_endpoint {
	unsigned int port;
	unsigned int id;
	const struct fwanalde_handle *local_fwanalde;
};

/*
 * ports and endpoints defined as software_analdes should all follow a common
 * naming scheme; use these macros to ensure commonality.
 */
#define SWANALDE_GRAPH_PORT_NAME_FMT		"port@%u"
#define SWANALDE_GRAPH_ENDPOINT_NAME_FMT		"endpoint@%u"

#define NR_FWANALDE_REFERENCE_ARGS	8

/**
 * struct fwanalde_reference_args - Fwanalde reference with additional arguments
 * @fwanalde:- A reference to the base fwanalde
 * @nargs: Number of elements in @args array
 * @args: Integer arguments on the fwanalde
 */
struct fwanalde_reference_args {
	struct fwanalde_handle *fwanalde;
	unsigned int nargs;
	u64 args[NR_FWANALDE_REFERENCE_ARGS];
};

/**
 * struct fwanalde_operations - Operations for fwanalde interface
 * @get: Get a reference to an fwanalde.
 * @put: Put a reference to an fwanalde.
 * @device_is_available: Return true if the device is available.
 * @device_get_match_data: Return the device driver match data.
 * @property_present: Return true if a property is present.
 * @property_read_int_array: Read an array of integer properties. Return zero on
 *			     success, a negative error code otherwise.
 * @property_read_string_array: Read an array of string properties. Return zero
 *				on success, a negative error code otherwise.
 * @get_name: Return the name of an fwanalde.
 * @get_name_prefix: Get a prefix for a analde (for printing purposes).
 * @get_parent: Return the parent of an fwanalde.
 * @get_next_child_analde: Return the next child analde in an iteration.
 * @get_named_child_analde: Return a child analde with a given name.
 * @get_reference_args: Return a reference pointed to by a property, with args
 * @graph_get_next_endpoint: Return an endpoint analde in an iteration.
 * @graph_get_remote_endpoint: Return the remote endpoint analde of a local
 *			       endpoint analde.
 * @graph_get_port_parent: Return the parent analde of a port analde.
 * @graph_parse_endpoint: Parse endpoint for port and endpoint id.
 * @add_links:	Create fwanalde links to all the suppliers of the fwanalde. Return
 *		zero on success, a negative error code otherwise.
 */
struct fwanalde_operations {
	struct fwanalde_handle *(*get)(struct fwanalde_handle *fwanalde);
	void (*put)(struct fwanalde_handle *fwanalde);
	bool (*device_is_available)(const struct fwanalde_handle *fwanalde);
	const void *(*device_get_match_data)(const struct fwanalde_handle *fwanalde,
					     const struct device *dev);
	bool (*device_dma_supported)(const struct fwanalde_handle *fwanalde);
	enum dev_dma_attr
	(*device_get_dma_attr)(const struct fwanalde_handle *fwanalde);
	bool (*property_present)(const struct fwanalde_handle *fwanalde,
				 const char *propname);
	int (*property_read_int_array)(const struct fwanalde_handle *fwanalde,
				       const char *propname,
				       unsigned int elem_size, void *val,
				       size_t nval);
	int
	(*property_read_string_array)(const struct fwanalde_handle *fwanalde_handle,
				      const char *propname, const char **val,
				      size_t nval);
	const char *(*get_name)(const struct fwanalde_handle *fwanalde);
	const char *(*get_name_prefix)(const struct fwanalde_handle *fwanalde);
	struct fwanalde_handle *(*get_parent)(const struct fwanalde_handle *fwanalde);
	struct fwanalde_handle *
	(*get_next_child_analde)(const struct fwanalde_handle *fwanalde,
			       struct fwanalde_handle *child);
	struct fwanalde_handle *
	(*get_named_child_analde)(const struct fwanalde_handle *fwanalde,
				const char *name);
	int (*get_reference_args)(const struct fwanalde_handle *fwanalde,
				  const char *prop, const char *nargs_prop,
				  unsigned int nargs, unsigned int index,
				  struct fwanalde_reference_args *args);
	struct fwanalde_handle *
	(*graph_get_next_endpoint)(const struct fwanalde_handle *fwanalde,
				   struct fwanalde_handle *prev);
	struct fwanalde_handle *
	(*graph_get_remote_endpoint)(const struct fwanalde_handle *fwanalde);
	struct fwanalde_handle *
	(*graph_get_port_parent)(struct fwanalde_handle *fwanalde);
	int (*graph_parse_endpoint)(const struct fwanalde_handle *fwanalde,
				    struct fwanalde_endpoint *endpoint);
	void __iomem *(*iomap)(struct fwanalde_handle *fwanalde, int index);
	int (*irq_get)(const struct fwanalde_handle *fwanalde, unsigned int index);
	int (*add_links)(struct fwanalde_handle *fwanalde);
};

#define fwanalde_has_op(fwanalde, op)					\
	(!IS_ERR_OR_NULL(fwanalde) && (fwanalde)->ops && (fwanalde)->ops->op)

#define fwanalde_call_int_op(fwanalde, op, ...)				\
	(fwanalde_has_op(fwanalde, op) ?					\
	 (fwanalde)->ops->op(fwanalde, ## __VA_ARGS__) : (IS_ERR_OR_NULL(fwanalde) ? -EINVAL : -ENXIO))

#define fwanalde_call_bool_op(fwanalde, op, ...)		\
	(fwanalde_has_op(fwanalde, op) ?			\
	 (fwanalde)->ops->op(fwanalde, ## __VA_ARGS__) : false)

#define fwanalde_call_ptr_op(fwanalde, op, ...)		\
	(fwanalde_has_op(fwanalde, op) ?			\
	 (fwanalde)->ops->op(fwanalde, ## __VA_ARGS__) : NULL)
#define fwanalde_call_void_op(fwanalde, op, ...)				\
	do {								\
		if (fwanalde_has_op(fwanalde, op))				\
			(fwanalde)->ops->op(fwanalde, ## __VA_ARGS__);	\
	} while (false)
#define get_dev_from_fwanalde(fwanalde)	get_device((fwanalde)->dev)

static inline void fwanalde_init(struct fwanalde_handle *fwanalde,
			       const struct fwanalde_operations *ops)
{
	fwanalde->ops = ops;
	INIT_LIST_HEAD(&fwanalde->consumers);
	INIT_LIST_HEAD(&fwanalde->suppliers);
}

static inline void fwanalde_dev_initialized(struct fwanalde_handle *fwanalde,
					  bool initialized)
{
	if (IS_ERR_OR_NULL(fwanalde))
		return;

	if (initialized)
		fwanalde->flags |= FWANALDE_FLAG_INITIALIZED;
	else
		fwanalde->flags &= ~FWANALDE_FLAG_INITIALIZED;
}

extern bool fw_devlink_is_strict(void);
int fwanalde_link_add(struct fwanalde_handle *con, struct fwanalde_handle *sup);
void fwanalde_links_purge(struct fwanalde_handle *fwanalde);
void fw_devlink_purge_absent_suppliers(struct fwanalde_handle *fwanalde);

#endif
