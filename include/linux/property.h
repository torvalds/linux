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

enum dev_dma_attr {
	DEV_DMA_NOT_SUPPORTED,
	DEV_DMA_NON_COHERENT,
	DEV_DMA_COHERENT,
};

struct fwnode_handle *dev_fwnode(struct device *dev);

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
int device_property_match_string(struct device *dev,
				 const char *propname, const char *string);

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
int fwnode_property_match_string(struct fwnode_handle *fwnode,
				 const char *propname, const char *string);

struct fwnode_handle *device_get_next_child_node(struct device *dev,
						 struct fwnode_handle *child);

#define device_for_each_child_node(dev, child)				\
	for (child = device_get_next_child_node(dev, NULL); child;	\
	     child = device_get_next_child_node(dev, child))

struct fwnode_handle *device_get_named_child_node(struct device *dev,
						  const char *childname);

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
 * @length: Length of data making up the value.
 * @is_array: True when the property is an array.
 * @is_string: True when property is a string.
 * @pointer: Pointer to the property (an array of items of the given type).
 * @value: Value of the property (when it is a single item of the given type).
 */
struct property_entry {
	const char *name;
	size_t length;
	bool is_array;
	bool is_string;
	union {
		union {
			void *raw_data;
			u8 *u8_data;
			u16 *u16_data;
			u32 *u32_data;
			u64 *u64_data;
			const char **str;
		} pointer;
		union {
			unsigned long long raw_data;
			u8 u8_data;
			u16 u16_data;
			u32 u32_data;
			u64 u64_data;
			const char *str;
		} value;
	};
};

/*
 * Note: the below four initializers for the anonymous union are carefully
 * crafted to avoid gcc-4.4.4's problems with initialization of anon unions
 * and structs.
 */

#define PROPERTY_ENTRY_INTEGER_ARRAY(_name_, _type_, _val_)	\
{								\
	.name = _name_,						\
	.length = ARRAY_SIZE(_val_) * sizeof(_type_),		\
	.is_array = true,					\
	.is_string = false,					\
	{ .pointer = { ._type_##_data = _val_ } },		\
}

#define PROPERTY_ENTRY_U8_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_INTEGER_ARRAY(_name_, u8, _val_)
#define PROPERTY_ENTRY_U16_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_INTEGER_ARRAY(_name_, u16, _val_)
#define PROPERTY_ENTRY_U32_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_INTEGER_ARRAY(_name_, u32, _val_)
#define PROPERTY_ENTRY_U64_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_INTEGER_ARRAY(_name_, u64, _val_)

#define PROPERTY_ENTRY_STRING_ARRAY(_name_, _val_)		\
{								\
	.name = _name_,						\
	.length = ARRAY_SIZE(_val_) * sizeof(const char *),	\
	.is_array = true,					\
	.is_string = true,					\
	{ .pointer = { .str = _val_ } },			\
}

#define PROPERTY_ENTRY_INTEGER(_name_, _type_, _val_)	\
{							\
	.name = _name_,					\
	.length = sizeof(_type_),			\
	.is_string = false,				\
	{ .value = { ._type_##_data = _val_ } },	\
}

#define PROPERTY_ENTRY_U8(_name_, _val_)		\
	PROPERTY_ENTRY_INTEGER(_name_, u8, _val_)
#define PROPERTY_ENTRY_U16(_name_, _val_)		\
	PROPERTY_ENTRY_INTEGER(_name_, u16, _val_)
#define PROPERTY_ENTRY_U32(_name_, _val_)		\
	PROPERTY_ENTRY_INTEGER(_name_, u32, _val_)
#define PROPERTY_ENTRY_U64(_name_, _val_)		\
	PROPERTY_ENTRY_INTEGER(_name_, u64, _val_)

#define PROPERTY_ENTRY_STRING(_name_, _val_)		\
{							\
	.name = _name_,					\
	.length = sizeof(_val_),			\
	.is_string = true,				\
	{ .value = { .str = _val_ } },			\
}

#define PROPERTY_ENTRY_BOOL(_name_)		\
{						\
	.name = _name_,				\
}

int device_add_properties(struct device *dev,
			  struct property_entry *properties);
void device_remove_properties(struct device *dev);

bool device_dma_supported(struct device *dev);

enum dev_dma_attr device_get_dma_attr(struct device *dev);

int device_get_phy_mode(struct device *dev);

void *device_get_mac_address(struct device *dev, char *addr, int alen);

#endif /* _LINUX_PROPERTY_H_ */
