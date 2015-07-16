/*
 * property.h - Unified device property interface.
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_PROPERTY_H_
#define _LINUX_PROPERTY_H_

#include <linux/fwnode.h>
#include <linux/types.h>

struct device;

enum dev_prop_type {
	DEV_PROP_U8,
	DEV_PROP_U16,
	DEV_PROP_U32,
	DEV_PROP_U64,
	DEV_PROP_STRING,
	DEV_PROP_MAX,
};

bool device_property_present(struct device *dev, const char *propname);
int device_property_read_u8_array(struct device *dev, const char *propname,
				  u8 *val, size_t nval);
int device_property_read_u16_array(struct device *dev, const char *propname,
				   u16 *val, size_t nval);
int device_property_read_u32_array(struct device *dev, const char *propname,
				   u32 *val, size_t nval);
int device_property_read_u64_array(struct device *dev, const char *propname,
				   u64 *val, size_t nval);
int device_property_read_string_array(struct device *dev, const char *propname,
				      const char **val, size_t nval);
int device_property_read_string(struct device *dev, const char *propname,
				const char **val);

bool fwnode_property_present(struct fwnode_handle *fwnode, const char *propname);
int fwnode_property_read_u8_array(struct fwnode_handle *fwnode,
				  const char *propname, u8 *val,
				  size_t nval);
int fwnode_property_read_u16_array(struct fwnode_handle *fwnode,
				   const char *propname, u16 *val,
				   size_t nval);
int fwnode_property_read_u32_array(struct fwnode_handle *fwnode,
				   const char *propname, u32 *val,
				   size_t nval);
int fwnode_property_read_u64_array(struct fwnode_handle *fwnode,
				   const char *propname, u64 *val,
				   size_t nval);
int fwnode_property_read_string_array(struct fwnode_handle *fwnode,
				      const char *propname, const char **val,
				      size_t nval);
int fwnode_property_read_string(struct fwnode_handle *fwnode,
				const char *propname, const char **val);

struct fwnode_handle *device_get_next_child_node(struct device *dev,
						 struct fwnode_handle *child);

#define device_for_each_child_node(dev, child) \
	for (child = device_get_next_child_node(dev, NULL); child; \
	     child = device_get_next_child_node(dev, child))

void fwnode_handle_put(struct fwnode_handle *fwnode);

unsigned int device_get_child_node_count(struct device *dev);

static inline bool device_property_read_bool(struct device *dev,
					     const char *propname)
{
	return device_property_present(dev, propname);
}

static inline int device_property_read_u8(struct device *dev,
					  const char *propname, u8 *val)
{
	return device_property_read_u8_array(dev, propname, val, 1);
}

static inline int device_property_read_u16(struct device *dev,
					   const char *propname, u16 *val)
{
	return device_property_read_u16_array(dev, propname, val, 1);
}

static inline int device_property_read_u32(struct device *dev,
					   const char *propname, u32 *val)
{
	return device_property_read_u32_array(dev, propname, val, 1);
}

static inline int device_property_read_u64(struct device *dev,
					   const char *propname, u64 *val)
{
	return device_property_read_u64_array(dev, propname, val, 1);
}

static inline bool fwnode_property_read_bool(struct fwnode_handle *fwnode,
					     const char *propname)
{
	return fwnode_property_present(fwnode, propname);
}

static inline int fwnode_property_read_u8(struct fwnode_handle *fwnode,
					  const char *propname, u8 *val)
{
	return fwnode_property_read_u8_array(fwnode, propname, val, 1);
}

static inline int fwnode_property_read_u16(struct fwnode_handle *fwnode,
					   const char *propname, u16 *val)
{
	return fwnode_property_read_u16_array(fwnode, propname, val, 1);
}

static inline int fwnode_property_read_u32(struct fwnode_handle *fwnode,
					   const char *propname, u32 *val)
{
	return fwnode_property_read_u32_array(fwnode, propname, val, 1);
}

static inline int fwnode_property_read_u64(struct fwnode_handle *fwnode,
					   const char *propname, u64 *val)
{
	return fwnode_property_read_u64_array(fwnode, propname, val, 1);
}

/**
 * struct property_entry - "Built-in" device property representation.
 * @name: Name of the property.
 * @type: Type of the property.
 * @nval: Number of items of type @type making up the value.
 * @value: Value of the property (an array of @nval items of type @type).
 */
struct property_entry {
	const char *name;
	enum dev_prop_type type;
	size_t nval;
	union {
		void *raw_data;
		u8 *u8_data;
		u16 *u16_data;
		u32 *u32_data;
		u64 *u64_data;
		const char **str;
	} value;
};

/**
 * struct property_set - Collection of "built-in" device properties.
 * @fwnode: Handle to be pointed to by the fwnode field of struct device.
 * @properties: Array of properties terminated with a null entry.
 */
struct property_set {
	struct fwnode_handle fwnode;
	struct property_entry *properties;
};

void device_add_property_set(struct device *dev, struct property_set *pset);

bool device_dma_is_coherent(struct device *dev);

#endif /* _LINUX_PROPERTY_H_ */
