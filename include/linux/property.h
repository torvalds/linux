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

bool fwnode_device_is_available(const struct fwnode_handle *fwnode);
bool fwnode_property_present(const struct fwnode_handle *fwnode,
			     const char *propname);
int fwnode_property_read_u8_array(const struct fwnode_handle *fwnode,
				  const char *propname, u8 *val,
				  size_t nval);
int fwnode_property_read_u16_array(const struct fwnode_handle *fwnode,
				   const char *propname, u16 *val,
				   size_t nval);
int fwnode_property_read_u32_array(const struct fwnode_handle *fwnode,
				   const char *propname, u32 *val,
				   size_t nval);
int fwnode_property_read_u64_array(const struct fwnode_handle *fwnode,
				   const char *propname, u64 *val,
				   size_t nval);
int fwnode_property_read_string_array(const struct fwnode_handle *fwnode,
				      const char *propname, const char **val,
				      size_t nval);
int fwnode_property_read_string(const struct fwnode_handle *fwnode,
				const char *propname, const char **val);
int fwnode_property_match_string(const struct fwnode_handle *fwnode,
				 const char *propname, const char *string);
int fwnode_property_get_reference_args(const struct fwnode_handle *fwnode,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwnode_reference_args *args);

struct fwnode_handle *fwnode_get_parent(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_get_next_parent(
	struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_get_next_child_node(
	const struct fwnode_handle *fwnode, struct fwnode_handle *child);
struct fwnode_handle *fwnode_get_next_available_child_node(
	const struct fwnode_handle *fwnode, struct fwnode_handle *child);

#define fwnode_for_each_child_node(fwnode, child)			\
	for (child = fwnode_get_next_child_node(fwnode, NULL); child;	\
	     child = fwnode_get_next_child_node(fwnode, child))

#define fwnode_for_each_available_child_node(fwnode, child)		       \
	for (child = fwnode_get_next_available_child_node(fwnode, NULL); child;\
	     child = fwnode_get_next_available_child_node(fwnode, child))

struct fwnode_handle *device_get_next_child_node(
	struct device *dev, struct fwnode_handle *child);

#define device_for_each_child_node(dev, child)				\
	for (child = device_get_next_child_node(dev, NULL); child;	\
	     child = device_get_next_child_node(dev, child))

struct fwnode_handle *fwnode_get_named_child_node(
	const struct fwnode_handle *fwnode, const char *childname);
struct fwnode_handle *device_get_named_child_node(struct device *dev,
						  const char *childname);

struct fwnode_handle *fwnode_handle_get(struct fwnode_handle *fwnode);
void fwnode_handle_put(struct fwnode_handle *fwnode);

int fwnode_irq_get(struct fwnode_handle *fwnode, unsigned int index);

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

static inline bool fwnode_property_read_bool(const struct fwnode_handle *fwnode,
					     const char *propname)
{
	return fwnode_property_present(fwnode, propname);
}

static inline int fwnode_property_read_u8(const struct fwnode_handle *fwnode,
					  const char *propname, u8 *val)
{
	return fwnode_property_read_u8_array(fwnode, propname, val, 1);
}

static inline int fwnode_property_read_u16(const struct fwnode_handle *fwnode,
					   const char *propname, u16 *val)
{
	return fwnode_property_read_u16_array(fwnode, propname, val, 1);
}

static inline int fwnode_property_read_u32(const struct fwnode_handle *fwnode,
					   const char *propname, u32 *val)
{
	return fwnode_property_read_u32_array(fwnode, propname, val, 1);
}

static inline int fwnode_property_read_u64(const struct fwnode_handle *fwnode,
					   const char *propname, u64 *val)
{
	return fwnode_property_read_u64_array(fwnode, propname, val, 1);
}

/**
 * struct property_entry - "Built-in" device property representation.
 * @name: Name of the property.
 * @length: Length of data making up the value.
 * @is_array: True when the property is an array.
 * @type: Type of the data in unions.
 * @pointer: Pointer to the property (an array of items of the given type).
 * @value: Value of the property (when it is a single item of the given type).
 */
struct property_entry {
	const char *name;
	size_t length;
	bool is_array;
	enum dev_prop_type type;
	union {
		union {
			const u8 *u8_data;
			const u16 *u16_data;
			const u32 *u32_data;
			const u64 *u64_data;
			const char * const *str;
		} pointer;
		union {
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

#define PROPERTY_ENTRY_INTEGER_ARRAY(_name_, _type_, _Type_, _val_)	\
(struct property_entry) {						\
	.name = _name_,							\
	.length = ARRAY_SIZE(_val_) * sizeof(_type_),			\
	.is_array = true,						\
	.type = DEV_PROP_##_Type_,					\
	{ .pointer = { ._type_##_data = _val_ } },			\
}

#define PROPERTY_ENTRY_U8_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_INTEGER_ARRAY(_name_, u8, U8, _val_)
#define PROPERTY_ENTRY_U16_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_INTEGER_ARRAY(_name_, u16, U16, _val_)
#define PROPERTY_ENTRY_U32_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_INTEGER_ARRAY(_name_, u32, U32, _val_)
#define PROPERTY_ENTRY_U64_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_INTEGER_ARRAY(_name_, u64, U64, _val_)

#define PROPERTY_ENTRY_STRING_ARRAY(_name_, _val_)		\
(struct property_entry) {					\
	.name = _name_,						\
	.length = ARRAY_SIZE(_val_) * sizeof(const char *),	\
	.is_array = true,					\
	.type = DEV_PROP_STRING,				\
	{ .pointer = { .str = _val_ } },			\
}

#define PROPERTY_ENTRY_INTEGER(_name_, _type_, _Type_, _val_)	\
(struct property_entry) {					\
	.name = _name_,						\
	.length = sizeof(_type_),				\
	.type = DEV_PROP_##_Type_,				\
	{ .value = { ._type_##_data = _val_ } },		\
}

#define PROPERTY_ENTRY_U8(_name_, _val_)		\
	PROPERTY_ENTRY_INTEGER(_name_, u8, U8, _val_)
#define PROPERTY_ENTRY_U16(_name_, _val_)		\
	PROPERTY_ENTRY_INTEGER(_name_, u16, U16, _val_)
#define PROPERTY_ENTRY_U32(_name_, _val_)		\
	PROPERTY_ENTRY_INTEGER(_name_, u32, U32, _val_)
#define PROPERTY_ENTRY_U64(_name_, _val_)		\
	PROPERTY_ENTRY_INTEGER(_name_, u64, U64, _val_)

#define PROPERTY_ENTRY_STRING(_name_, _val_)		\
(struct property_entry) {				\
	.name = _name_,					\
	.length = sizeof(const char *),			\
	.type = DEV_PROP_STRING,			\
	{ .value = { .str = _val_ } },			\
}

#define PROPERTY_ENTRY_BOOL(_name_)		\
(struct property_entry) {			\
	.name = _name_,				\
}

struct property_entry *
property_entries_dup(const struct property_entry *properties);

void property_entries_free(const struct property_entry *properties);

int device_add_properties(struct device *dev,
			  const struct property_entry *properties);
void device_remove_properties(struct device *dev);

bool device_dma_supported(struct device *dev);

enum dev_dma_attr device_get_dma_attr(struct device *dev);

const void *device_get_match_data(struct device *dev);

int device_get_phy_mode(struct device *dev);

void *device_get_mac_address(struct device *dev, char *addr, int alen);

int fwnode_get_phy_mode(struct fwnode_handle *fwnode);
void *fwnode_get_mac_address(struct fwnode_handle *fwnode,
			     char *addr, int alen);
struct fwnode_handle *fwnode_graph_get_next_endpoint(
	const struct fwnode_handle *fwnode, struct fwnode_handle *prev);
struct fwnode_handle *
fwnode_graph_get_port_parent(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_remote_port_parent(
	const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_remote_port(
	const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_remote_endpoint(
	const struct fwnode_handle *fwnode);
struct fwnode_handle *
fwnode_graph_get_remote_node(const struct fwnode_handle *fwnode, u32 port,
			     u32 endpoint);

#define fwnode_graph_for_each_endpoint(fwnode, child)			\
	for (child = NULL;						\
	     (child = fwnode_graph_get_next_endpoint(fwnode, child)); )

int fwnode_graph_parse_endpoint(const struct fwnode_handle *fwnode,
				struct fwnode_endpoint *endpoint);

/* -------------------------------------------------------------------------- */
/* Software fwnode support - when HW description is incomplete or missing */

bool is_software_node(const struct fwnode_handle *fwnode);

int software_node_notify(struct device *dev, unsigned long action);

struct fwnode_handle *
fwnode_create_software_node(const struct property_entry *properties,
			    const struct fwnode_handle *parent);
void fwnode_remove_software_node(struct fwnode_handle *fwnode);

#endif /* _LINUX_PROPERTY_H_ */
