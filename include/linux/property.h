/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * property.h - Unified device property interface.
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#ifndef _LINUX_PROPERTY_H_
#define _LINUX_PROPERTY_H_

#include <linux/args.h>
#include <linux/bits.h>
#include <linux/fwnode.h>
#include <linux/stddef.h>
#include <linux/types.h>

struct device;

enum dev_prop_type {
	DEV_PROP_U8,
	DEV_PROP_U16,
	DEV_PROP_U32,
	DEV_PROP_U64,
	DEV_PROP_STRING,
	DEV_PROP_REF,
};

enum dev_dma_attr {
	DEV_DMA_NOT_SUPPORTED,
	DEV_DMA_NON_COHERENT,
	DEV_DMA_COHERENT,
};

const struct fwnode_handle *__dev_fwnode_const(const struct device *dev);
struct fwnode_handle *__dev_fwnode(struct device *dev);
#define dev_fwnode(dev)							\
	_Generic((dev),							\
		 const struct device *: __dev_fwnode_const,	\
		 struct device *: __dev_fwnode)(dev)

bool device_property_present(const struct device *dev, const char *propname);
int device_property_read_u8_array(const struct device *dev, const char *propname,
				  u8 *val, size_t nval);
int device_property_read_u16_array(const struct device *dev, const char *propname,
				   u16 *val, size_t nval);
int device_property_read_u32_array(const struct device *dev, const char *propname,
				   u32 *val, size_t nval);
int device_property_read_u64_array(const struct device *dev, const char *propname,
				   u64 *val, size_t nval);
int device_property_read_string_array(const struct device *dev, const char *propname,
				      const char **val, size_t nval);
int device_property_read_string(const struct device *dev, const char *propname,
				const char **val);
int device_property_match_string(const struct device *dev,
				 const char *propname, const char *string);

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

bool fwnode_device_is_available(const struct fwnode_handle *fwnode);

static inline
bool fwnode_device_is_compatible(const struct fwnode_handle *fwnode, const char *compat)
{
	return fwnode_property_match_string(fwnode, "compatible", compat) >= 0;
}

/**
 * device_is_compatible - match 'compatible' property of the device with a given string
 * @dev: Pointer to the struct device
 * @compat: The string to match 'compatible' property with
 *
 * Returns: true if matches, otherwise false.
 */
static inline bool device_is_compatible(const struct device *dev, const char *compat)
{
	return fwnode_device_is_compatible(dev_fwnode(dev), compat);
}

int fwnode_property_get_reference_args(const struct fwnode_handle *fwnode,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwnode_reference_args *args);

struct fwnode_handle *fwnode_find_reference(const struct fwnode_handle *fwnode,
					    const char *name,
					    unsigned int index);

const char *fwnode_get_name(const struct fwnode_handle *fwnode);
const char *fwnode_get_name_prefix(const struct fwnode_handle *fwnode);
bool fwnode_name_eq(const struct fwnode_handle *fwnode, const char *name);

struct fwnode_handle *fwnode_get_parent(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_get_next_parent(struct fwnode_handle *fwnode);

#define fwnode_for_each_parent_node(fwnode, parent)		\
	for (parent = fwnode_get_parent(fwnode); parent;	\
	     parent = fwnode_get_next_parent(parent))

struct device *fwnode_get_next_parent_dev(const struct fwnode_handle *fwnode);
unsigned int fwnode_count_parents(const struct fwnode_handle *fwn);
struct fwnode_handle *fwnode_get_nth_parent(struct fwnode_handle *fwn,
					    unsigned int depth);
bool fwnode_is_ancestor_of(const struct fwnode_handle *ancestor, const struct fwnode_handle *child);
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

struct fwnode_handle *device_get_next_child_node(const struct device *dev,
						 struct fwnode_handle *child);

#define device_for_each_child_node(dev, child)				\
	for (child = device_get_next_child_node(dev, NULL); child;	\
	     child = device_get_next_child_node(dev, child))

struct fwnode_handle *fwnode_get_named_child_node(const struct fwnode_handle *fwnode,
						  const char *childname);
struct fwnode_handle *device_get_named_child_node(const struct device *dev,
						  const char *childname);

struct fwnode_handle *fwnode_handle_get(struct fwnode_handle *fwnode);
void fwnode_handle_put(struct fwnode_handle *fwnode);

int fwnode_irq_get(const struct fwnode_handle *fwnode, unsigned int index);
int fwnode_irq_get_byname(const struct fwnode_handle *fwnode, const char *name);

unsigned int device_get_child_node_count(const struct device *dev);

static inline bool device_property_read_bool(const struct device *dev,
					     const char *propname)
{
	return device_property_present(dev, propname);
}

static inline int device_property_read_u8(const struct device *dev,
					  const char *propname, u8 *val)
{
	return device_property_read_u8_array(dev, propname, val, 1);
}

static inline int device_property_read_u16(const struct device *dev,
					   const char *propname, u16 *val)
{
	return device_property_read_u16_array(dev, propname, val, 1);
}

static inline int device_property_read_u32(const struct device *dev,
					   const char *propname, u32 *val)
{
	return device_property_read_u32_array(dev, propname, val, 1);
}

static inline int device_property_read_u64(const struct device *dev,
					   const char *propname, u64 *val)
{
	return device_property_read_u64_array(dev, propname, val, 1);
}

static inline int device_property_count_u8(const struct device *dev, const char *propname)
{
	return device_property_read_u8_array(dev, propname, NULL, 0);
}

static inline int device_property_count_u16(const struct device *dev, const char *propname)
{
	return device_property_read_u16_array(dev, propname, NULL, 0);
}

static inline int device_property_count_u32(const struct device *dev, const char *propname)
{
	return device_property_read_u32_array(dev, propname, NULL, 0);
}

static inline int device_property_count_u64(const struct device *dev, const char *propname)
{
	return device_property_read_u64_array(dev, propname, NULL, 0);
}

static inline int device_property_string_array_count(const struct device *dev,
						     const char *propname)
{
	return device_property_read_string_array(dev, propname, NULL, 0);
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

static inline int fwnode_property_count_u8(const struct fwnode_handle *fwnode,
					   const char *propname)
{
	return fwnode_property_read_u8_array(fwnode, propname, NULL, 0);
}

static inline int fwnode_property_count_u16(const struct fwnode_handle *fwnode,
					    const char *propname)
{
	return fwnode_property_read_u16_array(fwnode, propname, NULL, 0);
}

static inline int fwnode_property_count_u32(const struct fwnode_handle *fwnode,
					    const char *propname)
{
	return fwnode_property_read_u32_array(fwnode, propname, NULL, 0);
}

static inline int fwnode_property_count_u64(const struct fwnode_handle *fwnode,
					    const char *propname)
{
	return fwnode_property_read_u64_array(fwnode, propname, NULL, 0);
}

static inline int
fwnode_property_string_array_count(const struct fwnode_handle *fwnode,
				   const char *propname)
{
	return fwnode_property_read_string_array(fwnode, propname, NULL, 0);
}

struct software_node;

/**
 * struct software_node_ref_args - Reference property with additional arguments
 * @node: Reference to a software node
 * @nargs: Number of elements in @args array
 * @args: Integer arguments
 */
struct software_node_ref_args {
	const struct software_node *node;
	unsigned int nargs;
	u64 args[NR_FWNODE_REFERENCE_ARGS];
};

#define SOFTWARE_NODE_REFERENCE(_ref_, ...)			\
(const struct software_node_ref_args) {				\
	.node = _ref_,						\
	.nargs = COUNT_ARGS(__VA_ARGS__),			\
	.args = { __VA_ARGS__ },				\
}

/**
 * struct property_entry - "Built-in" device property representation.
 * @name: Name of the property.
 * @length: Length of data making up the value.
 * @is_inline: True when the property value is stored inline.
 * @type: Type of the data in unions.
 * @pointer: Pointer to the property when it is not stored inline.
 * @value: Value of the property when it is stored inline.
 */
struct property_entry {
	const char *name;
	size_t length;
	bool is_inline;
	enum dev_prop_type type;
	union {
		const void *pointer;
		union {
			u8 u8_data[sizeof(u64) / sizeof(u8)];
			u16 u16_data[sizeof(u64) / sizeof(u16)];
			u32 u32_data[sizeof(u64) / sizeof(u32)];
			u64 u64_data[sizeof(u64) / sizeof(u64)];
			const char *str[sizeof(u64) / sizeof(char *)];
		} value;
	};
};

/*
 * Note: the below initializers for the anonymous union are carefully
 * crafted to avoid gcc-4.4.4's problems with initialization of anon unions
 * and structs.
 */
#define __PROPERTY_ENTRY_ARRAY_LEN(_name_, _elem_, _Type_, _val_, _len_)		\
(struct property_entry) {								\
	.name = _name_,									\
	.length = (_len_) * sizeof_field(struct property_entry, value._elem_[0]),	\
	.type = DEV_PROP_##_Type_,							\
	{ .pointer = _val_ },								\
}

#define PROPERTY_ENTRY_U8_ARRAY_LEN(_name_, _val_, _len_)		\
	__PROPERTY_ENTRY_ARRAY_LEN(_name_, u8_data, U8, _val_, _len_)
#define PROPERTY_ENTRY_U16_ARRAY_LEN(_name_, _val_, _len_)		\
	__PROPERTY_ENTRY_ARRAY_LEN(_name_, u16_data, U16, _val_, _len_)
#define PROPERTY_ENTRY_U32_ARRAY_LEN(_name_, _val_, _len_)		\
	__PROPERTY_ENTRY_ARRAY_LEN(_name_, u32_data, U32, _val_, _len_)
#define PROPERTY_ENTRY_U64_ARRAY_LEN(_name_, _val_, _len_)		\
	__PROPERTY_ENTRY_ARRAY_LEN(_name_, u64_data, U64, _val_, _len_)
#define PROPERTY_ENTRY_STRING_ARRAY_LEN(_name_, _val_, _len_)		\
	__PROPERTY_ENTRY_ARRAY_LEN(_name_, str, STRING, _val_, _len_)

#define PROPERTY_ENTRY_REF_ARRAY_LEN(_name_, _val_, _len_)		\
(struct property_entry) {						\
	.name = _name_,							\
	.length = (_len_) * sizeof(struct software_node_ref_args),	\
	.type = DEV_PROP_REF,						\
	{ .pointer = _val_ },						\
}

#define PROPERTY_ENTRY_U8_ARRAY(_name_, _val_)				\
	PROPERTY_ENTRY_U8_ARRAY_LEN(_name_, _val_, ARRAY_SIZE(_val_))
#define PROPERTY_ENTRY_U16_ARRAY(_name_, _val_)				\
	PROPERTY_ENTRY_U16_ARRAY_LEN(_name_, _val_, ARRAY_SIZE(_val_))
#define PROPERTY_ENTRY_U32_ARRAY(_name_, _val_)				\
	PROPERTY_ENTRY_U32_ARRAY_LEN(_name_, _val_, ARRAY_SIZE(_val_))
#define PROPERTY_ENTRY_U64_ARRAY(_name_, _val_)				\
	PROPERTY_ENTRY_U64_ARRAY_LEN(_name_, _val_, ARRAY_SIZE(_val_))
#define PROPERTY_ENTRY_STRING_ARRAY(_name_, _val_)			\
	PROPERTY_ENTRY_STRING_ARRAY_LEN(_name_, _val_, ARRAY_SIZE(_val_))
#define PROPERTY_ENTRY_REF_ARRAY(_name_, _val_)				\
	PROPERTY_ENTRY_REF_ARRAY_LEN(_name_, _val_, ARRAY_SIZE(_val_))

#define __PROPERTY_ENTRY_ELEMENT(_name_, _elem_, _Type_, _val_)		\
(struct property_entry) {						\
	.name = _name_,							\
	.length = sizeof_field(struct property_entry, value._elem_[0]),	\
	.is_inline = true,						\
	.type = DEV_PROP_##_Type_,					\
	{ .value = { ._elem_[0] = _val_ } },				\
}

#define PROPERTY_ENTRY_U8(_name_, _val_)				\
	__PROPERTY_ENTRY_ELEMENT(_name_, u8_data, U8, _val_)
#define PROPERTY_ENTRY_U16(_name_, _val_)				\
	__PROPERTY_ENTRY_ELEMENT(_name_, u16_data, U16, _val_)
#define PROPERTY_ENTRY_U32(_name_, _val_)				\
	__PROPERTY_ENTRY_ELEMENT(_name_, u32_data, U32, _val_)
#define PROPERTY_ENTRY_U64(_name_, _val_)				\
	__PROPERTY_ENTRY_ELEMENT(_name_, u64_data, U64, _val_)
#define PROPERTY_ENTRY_STRING(_name_, _val_)				\
	__PROPERTY_ENTRY_ELEMENT(_name_, str, STRING, _val_)

#define PROPERTY_ENTRY_REF(_name_, _ref_, ...)				\
(struct property_entry) {						\
	.name = _name_,							\
	.length = sizeof(struct software_node_ref_args),		\
	.type = DEV_PROP_REF,						\
	{ .pointer = &SOFTWARE_NODE_REFERENCE(_ref_, ##__VA_ARGS__), },	\
}

#define PROPERTY_ENTRY_BOOL(_name_)		\
(struct property_entry) {			\
	.name = _name_,				\
	.is_inline = true,			\
}

struct property_entry *
property_entries_dup(const struct property_entry *properties);
void property_entries_free(const struct property_entry *properties);

bool device_dma_supported(const struct device *dev);
enum dev_dma_attr device_get_dma_attr(const struct device *dev);

const void *device_get_match_data(const struct device *dev);

int device_get_phy_mode(struct device *dev);
int fwnode_get_phy_mode(const struct fwnode_handle *fwnode);

void __iomem *fwnode_iomap(struct fwnode_handle *fwnode, int index);

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

static inline bool fwnode_graph_is_endpoint(const struct fwnode_handle *fwnode)
{
	return fwnode_property_present(fwnode, "remote-endpoint");
}

/*
 * Fwnode lookup flags
 *
 * @FWNODE_GRAPH_ENDPOINT_NEXT: In the case of no exact match, look for the
 *				closest endpoint ID greater than the specified
 *				one.
 * @FWNODE_GRAPH_DEVICE_DISABLED: That the device to which the remote
 *				  endpoint of the given endpoint belongs to,
 *				  may be disabled, or that the endpoint is not
 *				  connected.
 */
#define FWNODE_GRAPH_ENDPOINT_NEXT	BIT(0)
#define FWNODE_GRAPH_DEVICE_DISABLED	BIT(1)

struct fwnode_handle *
fwnode_graph_get_endpoint_by_id(const struct fwnode_handle *fwnode,
				u32 port, u32 endpoint, unsigned long flags);
unsigned int fwnode_graph_get_endpoint_count(const struct fwnode_handle *fwnode,
					     unsigned long flags);

#define fwnode_graph_for_each_endpoint(fwnode, child)				\
	for (child = fwnode_graph_get_next_endpoint(fwnode, NULL); child;	\
	     child = fwnode_graph_get_next_endpoint(fwnode, child))

int fwnode_graph_parse_endpoint(const struct fwnode_handle *fwnode,
				struct fwnode_endpoint *endpoint);

typedef void *(*devcon_match_fn_t)(const struct fwnode_handle *fwnode, const char *id,
				   void *data);

void *fwnode_connection_find_match(const struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match);

static inline void *device_connection_find_match(const struct device *dev,
						 const char *con_id, void *data,
						 devcon_match_fn_t match)
{
	return fwnode_connection_find_match(dev_fwnode(dev), con_id, data, match);
}

int fwnode_connection_find_matches(const struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match,
				   void **matches, unsigned int matches_len);

/* -------------------------------------------------------------------------- */
/* Software fwnode support - when HW description is incomplete or missing */

/**
 * struct software_node - Software node description
 * @name: Name of the software node
 * @parent: Parent of the software node
 * @properties: Array of device properties
 */
struct software_node {
	const char *name;
	const struct software_node *parent;
	const struct property_entry *properties;
};

bool is_software_node(const struct fwnode_handle *fwnode);
const struct software_node *
to_software_node(const struct fwnode_handle *fwnode);
struct fwnode_handle *software_node_fwnode(const struct software_node *node);

const struct software_node *
software_node_find_by_name(const struct software_node *parent,
			   const char *name);

int software_node_register_node_group(const struct software_node **node_group);
void software_node_unregister_node_group(const struct software_node **node_group);

int software_node_register(const struct software_node *node);
void software_node_unregister(const struct software_node *node);

struct fwnode_handle *
fwnode_create_software_node(const struct property_entry *properties,
			    const struct fwnode_handle *parent);
void fwnode_remove_software_node(struct fwnode_handle *fwnode);

int device_add_software_node(struct device *dev, const struct software_node *node);
void device_remove_software_node(struct device *dev);

int device_create_managed_software_node(struct device *dev,
					const struct property_entry *properties,
					const struct software_node *parent);

#endif /* _LINUX_PROPERTY_H_ */
