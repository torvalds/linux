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

#include <linux/bits.h>
#include <linux/fwyesde.h>
#include <linux/types.h>

struct device;

enum dev_prop_type {
	DEV_PROP_U8,
	DEV_PROP_U16,
	DEV_PROP_U32,
	DEV_PROP_U64,
	DEV_PROP_STRING,
};

enum dev_dma_attr {
	DEV_DMA_NOT_SUPPORTED,
	DEV_DMA_NON_COHERENT,
	DEV_DMA_COHERENT,
};

struct fwyesde_handle *dev_fwyesde(struct device *dev);

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

bool fwyesde_device_is_available(const struct fwyesde_handle *fwyesde);
bool fwyesde_property_present(const struct fwyesde_handle *fwyesde,
			     const char *propname);
int fwyesde_property_read_u8_array(const struct fwyesde_handle *fwyesde,
				  const char *propname, u8 *val,
				  size_t nval);
int fwyesde_property_read_u16_array(const struct fwyesde_handle *fwyesde,
				   const char *propname, u16 *val,
				   size_t nval);
int fwyesde_property_read_u32_array(const struct fwyesde_handle *fwyesde,
				   const char *propname, u32 *val,
				   size_t nval);
int fwyesde_property_read_u64_array(const struct fwyesde_handle *fwyesde,
				   const char *propname, u64 *val,
				   size_t nval);
int fwyesde_property_read_string_array(const struct fwyesde_handle *fwyesde,
				      const char *propname, const char **val,
				      size_t nval);
int fwyesde_property_read_string(const struct fwyesde_handle *fwyesde,
				const char *propname, const char **val);
int fwyesde_property_match_string(const struct fwyesde_handle *fwyesde,
				 const char *propname, const char *string);
int fwyesde_property_get_reference_args(const struct fwyesde_handle *fwyesde,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwyesde_reference_args *args);

struct fwyesde_handle *fwyesde_find_reference(const struct fwyesde_handle *fwyesde,
					    const char *name,
					    unsigned int index);

const char *fwyesde_get_name(const struct fwyesde_handle *fwyesde);
const char *fwyesde_get_name_prefix(const struct fwyesde_handle *fwyesde);
struct fwyesde_handle *fwyesde_get_parent(const struct fwyesde_handle *fwyesde);
struct fwyesde_handle *fwyesde_get_next_parent(
	struct fwyesde_handle *fwyesde);
unsigned int fwyesde_count_parents(const struct fwyesde_handle *fwn);
struct fwyesde_handle *fwyesde_get_nth_parent(struct fwyesde_handle *fwn,
					    unsigned int depth);
struct fwyesde_handle *fwyesde_get_next_child_yesde(
	const struct fwyesde_handle *fwyesde, struct fwyesde_handle *child);
struct fwyesde_handle *fwyesde_get_next_available_child_yesde(
	const struct fwyesde_handle *fwyesde, struct fwyesde_handle *child);

#define fwyesde_for_each_child_yesde(fwyesde, child)			\
	for (child = fwyesde_get_next_child_yesde(fwyesde, NULL); child;	\
	     child = fwyesde_get_next_child_yesde(fwyesde, child))

#define fwyesde_for_each_available_child_yesde(fwyesde, child)		       \
	for (child = fwyesde_get_next_available_child_yesde(fwyesde, NULL); child;\
	     child = fwyesde_get_next_available_child_yesde(fwyesde, child))

struct fwyesde_handle *device_get_next_child_yesde(
	struct device *dev, struct fwyesde_handle *child);

#define device_for_each_child_yesde(dev, child)				\
	for (child = device_get_next_child_yesde(dev, NULL); child;	\
	     child = device_get_next_child_yesde(dev, child))

struct fwyesde_handle *fwyesde_get_named_child_yesde(
	const struct fwyesde_handle *fwyesde, const char *childname);
struct fwyesde_handle *device_get_named_child_yesde(struct device *dev,
						  const char *childname);

struct fwyesde_handle *fwyesde_handle_get(struct fwyesde_handle *fwyesde);
void fwyesde_handle_put(struct fwyesde_handle *fwyesde);

int fwyesde_irq_get(struct fwyesde_handle *fwyesde, unsigned int index);

unsigned int device_get_child_yesde_count(struct device *dev);

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

static inline int device_property_count_u8(struct device *dev, const char *propname)
{
	return device_property_read_u8_array(dev, propname, NULL, 0);
}

static inline int device_property_count_u16(struct device *dev, const char *propname)
{
	return device_property_read_u16_array(dev, propname, NULL, 0);
}

static inline int device_property_count_u32(struct device *dev, const char *propname)
{
	return device_property_read_u32_array(dev, propname, NULL, 0);
}

static inline int device_property_count_u64(struct device *dev, const char *propname)
{
	return device_property_read_u64_array(dev, propname, NULL, 0);
}

static inline bool fwyesde_property_read_bool(const struct fwyesde_handle *fwyesde,
					     const char *propname)
{
	return fwyesde_property_present(fwyesde, propname);
}

static inline int fwyesde_property_read_u8(const struct fwyesde_handle *fwyesde,
					  const char *propname, u8 *val)
{
	return fwyesde_property_read_u8_array(fwyesde, propname, val, 1);
}

static inline int fwyesde_property_read_u16(const struct fwyesde_handle *fwyesde,
					   const char *propname, u16 *val)
{
	return fwyesde_property_read_u16_array(fwyesde, propname, val, 1);
}

static inline int fwyesde_property_read_u32(const struct fwyesde_handle *fwyesde,
					   const char *propname, u32 *val)
{
	return fwyesde_property_read_u32_array(fwyesde, propname, val, 1);
}

static inline int fwyesde_property_read_u64(const struct fwyesde_handle *fwyesde,
					   const char *propname, u64 *val)
{
	return fwyesde_property_read_u64_array(fwyesde, propname, val, 1);
}

static inline int fwyesde_property_count_u8(const struct fwyesde_handle *fwyesde,
					   const char *propname)
{
	return fwyesde_property_read_u8_array(fwyesde, propname, NULL, 0);
}

static inline int fwyesde_property_count_u16(const struct fwyesde_handle *fwyesde,
					    const char *propname)
{
	return fwyesde_property_read_u16_array(fwyesde, propname, NULL, 0);
}

static inline int fwyesde_property_count_u32(const struct fwyesde_handle *fwyesde,
					    const char *propname)
{
	return fwyesde_property_read_u32_array(fwyesde, propname, NULL, 0);
}

static inline int fwyesde_property_count_u64(const struct fwyesde_handle *fwyesde,
					    const char *propname)
{
	return fwyesde_property_read_u64_array(fwyesde, propname, NULL, 0);
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
		const void *pointer;
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
 * Note: the below initializers for the ayesnymous union are carefully
 * crafted to avoid gcc-4.4.4's problems with initialization of ayesn unions
 * and structs.
 */

#define __PROPERTY_ENTRY_ELEMENT_SIZE(_elem_)				\
	sizeof(((struct property_entry *)NULL)->value._elem_)

#define __PROPERTY_ENTRY_ARRAY_LEN(_name_, _elem_, _Type_, _val_, _len_)\
(struct property_entry) {						\
	.name = _name_,							\
	.length = (_len_) * __PROPERTY_ENTRY_ELEMENT_SIZE(_elem_),	\
	.is_array = true,						\
	.type = DEV_PROP_##_Type_,					\
	{ .pointer = _val_ },						\
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

#define __PROPERTY_ENTRY_ELEMENT(_name_, _elem_, _Type_, _val_)		\
(struct property_entry) {						\
	.name = _name_,							\
	.length = __PROPERTY_ENTRY_ELEMENT_SIZE(_elem_),		\
	.type = DEV_PROP_##_Type_,					\
	{ .value = { ._elem_ = _val_ } },				\
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

int fwyesde_get_phy_mode(struct fwyesde_handle *fwyesde);
void *fwyesde_get_mac_address(struct fwyesde_handle *fwyesde,
			     char *addr, int alen);
struct fwyesde_handle *fwyesde_graph_get_next_endpoint(
	const struct fwyesde_handle *fwyesde, struct fwyesde_handle *prev);
struct fwyesde_handle *
fwyesde_graph_get_port_parent(const struct fwyesde_handle *fwyesde);
struct fwyesde_handle *fwyesde_graph_get_remote_port_parent(
	const struct fwyesde_handle *fwyesde);
struct fwyesde_handle *fwyesde_graph_get_remote_port(
	const struct fwyesde_handle *fwyesde);
struct fwyesde_handle *fwyesde_graph_get_remote_endpoint(
	const struct fwyesde_handle *fwyesde);
struct fwyesde_handle *
fwyesde_graph_get_remote_yesde(const struct fwyesde_handle *fwyesde, u32 port,
			     u32 endpoint);

/*
 * Fwyesde lookup flags
 *
 * @FWNODE_GRAPH_ENDPOINT_NEXT: In the case of yes exact match, look for the
 *				closest endpoint ID greater than the specified
 *				one.
 * @FWNODE_GRAPH_DEVICE_DISABLED: That the device to which the remote
 *				  endpoint of the given endpoint belongs to,
 *				  may be disabled.
 */
#define FWNODE_GRAPH_ENDPOINT_NEXT	BIT(0)
#define FWNODE_GRAPH_DEVICE_DISABLED	BIT(1)

struct fwyesde_handle *
fwyesde_graph_get_endpoint_by_id(const struct fwyesde_handle *fwyesde,
				u32 port, u32 endpoint, unsigned long flags);

#define fwyesde_graph_for_each_endpoint(fwyesde, child)			\
	for (child = NULL;						\
	     (child = fwyesde_graph_get_next_endpoint(fwyesde, child)); )

int fwyesde_graph_parse_endpoint(const struct fwyesde_handle *fwyesde,
				struct fwyesde_endpoint *endpoint);

/* -------------------------------------------------------------------------- */
/* Software fwyesde support - when HW description is incomplete or missing */

struct software_yesde;

/**
 * struct software_yesde_ref_args - Reference with additional arguments
 * @yesde: Reference to a software yesde
 * @nargs: Number of elements in @args array
 * @args: Integer arguments
 */
struct software_yesde_ref_args {
	const struct software_yesde *yesde;
	unsigned int nargs;
	u64 args[NR_FWNODE_REFERENCE_ARGS];
};

/**
 * struct software_yesde_reference - Named software yesde reference property
 * @name: Name of the property
 * @nrefs: Number of elements in @refs array
 * @refs: Array of references with optional arguments
 */
struct software_yesde_reference {
	const char *name;
	unsigned int nrefs;
	const struct software_yesde_ref_args *refs;
};

/**
 * struct software_yesde - Software yesde description
 * @name: Name of the software yesde
 * @parent: Parent of the software yesde
 * @properties: Array of device properties
 * @references: Array of software yesde reference properties
 */
struct software_yesde {
	const char *name;
	const struct software_yesde *parent;
	const struct property_entry *properties;
	const struct software_yesde_reference *references;
};

bool is_software_yesde(const struct fwyesde_handle *fwyesde);
const struct software_yesde *
to_software_yesde(const struct fwyesde_handle *fwyesde);
struct fwyesde_handle *software_yesde_fwyesde(const struct software_yesde *yesde);

const struct software_yesde *
software_yesde_find_by_name(const struct software_yesde *parent,
			   const char *name);

int software_yesde_register_yesdes(const struct software_yesde *yesdes);
void software_yesde_unregister_yesdes(const struct software_yesde *yesdes);

int software_yesde_register(const struct software_yesde *yesde);

int software_yesde_yestify(struct device *dev, unsigned long action);

struct fwyesde_handle *
fwyesde_create_software_yesde(const struct property_entry *properties,
			    const struct fwyesde_handle *parent);
void fwyesde_remove_software_yesde(struct fwyesde_handle *fwyesde);

#endif /* _LINUX_PROPERTY_H_ */
