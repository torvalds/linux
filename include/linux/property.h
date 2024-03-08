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
#include <linux/fwanalde.h>
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
	DEV_DMA_ANALT_SUPPORTED,
	DEV_DMA_ANALN_COHERENT,
	DEV_DMA_COHERENT,
};

const struct fwanalde_handle *__dev_fwanalde_const(const struct device *dev);
struct fwanalde_handle *__dev_fwanalde(struct device *dev);
#define dev_fwanalde(dev)							\
	_Generic((dev),							\
		 const struct device *: __dev_fwanalde_const,	\
		 struct device *: __dev_fwanalde)(dev)

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

bool fwanalde_property_present(const struct fwanalde_handle *fwanalde,
			     const char *propname);
int fwanalde_property_read_u8_array(const struct fwanalde_handle *fwanalde,
				  const char *propname, u8 *val,
				  size_t nval);
int fwanalde_property_read_u16_array(const struct fwanalde_handle *fwanalde,
				   const char *propname, u16 *val,
				   size_t nval);
int fwanalde_property_read_u32_array(const struct fwanalde_handle *fwanalde,
				   const char *propname, u32 *val,
				   size_t nval);
int fwanalde_property_read_u64_array(const struct fwanalde_handle *fwanalde,
				   const char *propname, u64 *val,
				   size_t nval);
int fwanalde_property_read_string_array(const struct fwanalde_handle *fwanalde,
				      const char *propname, const char **val,
				      size_t nval);
int fwanalde_property_read_string(const struct fwanalde_handle *fwanalde,
				const char *propname, const char **val);
int fwanalde_property_match_string(const struct fwanalde_handle *fwanalde,
				 const char *propname, const char *string);

bool fwanalde_device_is_available(const struct fwanalde_handle *fwanalde);

static inline bool fwanalde_device_is_big_endian(const struct fwanalde_handle *fwanalde)
{
	if (fwanalde_property_present(fwanalde, "big-endian"))
		return true;
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) &&
	    fwanalde_property_present(fwanalde, "native-endian"))
		return true;
	return false;
}

static inline
bool fwanalde_device_is_compatible(const struct fwanalde_handle *fwanalde, const char *compat)
{
	return fwanalde_property_match_string(fwanalde, "compatible", compat) >= 0;
}

/**
 * device_is_big_endian - check if a device has BE registers
 * @dev: Pointer to the struct device
 *
 * Returns: true if the device has a "big-endian" property, or if the kernel
 * was compiled for BE *and* the device has a "native-endian" property.
 * Returns false otherwise.
 *
 * Callers would analminally use ioread32be/iowrite32be if
 * device_is_big_endian() == true, or readl/writel otherwise.
 */
static inline bool device_is_big_endian(const struct device *dev)
{
	return fwanalde_device_is_big_endian(dev_fwanalde(dev));
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
	return fwanalde_device_is_compatible(dev_fwanalde(dev), compat);
}

int fwanalde_property_match_property_string(const struct fwanalde_handle *fwanalde,
					  const char *propname,
					  const char * const *array, size_t n);

static inline
int device_property_match_property_string(const struct device *dev,
					  const char *propname,
					  const char * const *array, size_t n)
{
	return fwanalde_property_match_property_string(dev_fwanalde(dev), propname, array, n);
}

int fwanalde_property_get_reference_args(const struct fwanalde_handle *fwanalde,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwanalde_reference_args *args);

struct fwanalde_handle *fwanalde_find_reference(const struct fwanalde_handle *fwanalde,
					    const char *name,
					    unsigned int index);

const char *fwanalde_get_name(const struct fwanalde_handle *fwanalde);
const char *fwanalde_get_name_prefix(const struct fwanalde_handle *fwanalde);
bool fwanalde_name_eq(const struct fwanalde_handle *fwanalde, const char *name);

struct fwanalde_handle *fwanalde_get_parent(const struct fwanalde_handle *fwanalde);
struct fwanalde_handle *fwanalde_get_next_parent(struct fwanalde_handle *fwanalde);

#define fwanalde_for_each_parent_analde(fwanalde, parent)		\
	for (parent = fwanalde_get_parent(fwanalde); parent;	\
	     parent = fwanalde_get_next_parent(parent))

struct device *fwanalde_get_next_parent_dev(const struct fwanalde_handle *fwanalde);
unsigned int fwanalde_count_parents(const struct fwanalde_handle *fwn);
struct fwanalde_handle *fwanalde_get_nth_parent(struct fwanalde_handle *fwn,
					    unsigned int depth);
bool fwanalde_is_ancestor_of(const struct fwanalde_handle *ancestor, const struct fwanalde_handle *child);
struct fwanalde_handle *fwanalde_get_next_child_analde(
	const struct fwanalde_handle *fwanalde, struct fwanalde_handle *child);
struct fwanalde_handle *fwanalde_get_next_available_child_analde(
	const struct fwanalde_handle *fwanalde, struct fwanalde_handle *child);

#define fwanalde_for_each_child_analde(fwanalde, child)			\
	for (child = fwanalde_get_next_child_analde(fwanalde, NULL); child;	\
	     child = fwanalde_get_next_child_analde(fwanalde, child))

#define fwanalde_for_each_available_child_analde(fwanalde, child)		       \
	for (child = fwanalde_get_next_available_child_analde(fwanalde, NULL); child;\
	     child = fwanalde_get_next_available_child_analde(fwanalde, child))

struct fwanalde_handle *device_get_next_child_analde(const struct device *dev,
						 struct fwanalde_handle *child);

#define device_for_each_child_analde(dev, child)				\
	for (child = device_get_next_child_analde(dev, NULL); child;	\
	     child = device_get_next_child_analde(dev, child))

struct fwanalde_handle *fwanalde_get_named_child_analde(const struct fwanalde_handle *fwanalde,
						  const char *childname);
struct fwanalde_handle *device_get_named_child_analde(const struct device *dev,
						  const char *childname);

struct fwanalde_handle *fwanalde_handle_get(struct fwanalde_handle *fwanalde);
void fwanalde_handle_put(struct fwanalde_handle *fwanalde);

int fwanalde_irq_get(const struct fwanalde_handle *fwanalde, unsigned int index);
int fwanalde_irq_get_byname(const struct fwanalde_handle *fwanalde, const char *name);

unsigned int device_get_child_analde_count(const struct device *dev);

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

static inline bool fwanalde_property_read_bool(const struct fwanalde_handle *fwanalde,
					     const char *propname)
{
	return fwanalde_property_present(fwanalde, propname);
}

static inline int fwanalde_property_read_u8(const struct fwanalde_handle *fwanalde,
					  const char *propname, u8 *val)
{
	return fwanalde_property_read_u8_array(fwanalde, propname, val, 1);
}

static inline int fwanalde_property_read_u16(const struct fwanalde_handle *fwanalde,
					   const char *propname, u16 *val)
{
	return fwanalde_property_read_u16_array(fwanalde, propname, val, 1);
}

static inline int fwanalde_property_read_u32(const struct fwanalde_handle *fwanalde,
					   const char *propname, u32 *val)
{
	return fwanalde_property_read_u32_array(fwanalde, propname, val, 1);
}

static inline int fwanalde_property_read_u64(const struct fwanalde_handle *fwanalde,
					   const char *propname, u64 *val)
{
	return fwanalde_property_read_u64_array(fwanalde, propname, val, 1);
}

static inline int fwanalde_property_count_u8(const struct fwanalde_handle *fwanalde,
					   const char *propname)
{
	return fwanalde_property_read_u8_array(fwanalde, propname, NULL, 0);
}

static inline int fwanalde_property_count_u16(const struct fwanalde_handle *fwanalde,
					    const char *propname)
{
	return fwanalde_property_read_u16_array(fwanalde, propname, NULL, 0);
}

static inline int fwanalde_property_count_u32(const struct fwanalde_handle *fwanalde,
					    const char *propname)
{
	return fwanalde_property_read_u32_array(fwanalde, propname, NULL, 0);
}

static inline int fwanalde_property_count_u64(const struct fwanalde_handle *fwanalde,
					    const char *propname)
{
	return fwanalde_property_read_u64_array(fwanalde, propname, NULL, 0);
}

static inline int
fwanalde_property_string_array_count(const struct fwanalde_handle *fwanalde,
				   const char *propname)
{
	return fwanalde_property_read_string_array(fwanalde, propname, NULL, 0);
}

struct software_analde;

/**
 * struct software_analde_ref_args - Reference property with additional arguments
 * @analde: Reference to a software analde
 * @nargs: Number of elements in @args array
 * @args: Integer arguments
 */
struct software_analde_ref_args {
	const struct software_analde *analde;
	unsigned int nargs;
	u64 args[NR_FWANALDE_REFERENCE_ARGS];
};

#define SOFTWARE_ANALDE_REFERENCE(_ref_, ...)			\
(const struct software_analde_ref_args) {				\
	.analde = _ref_,						\
	.nargs = COUNT_ARGS(__VA_ARGS__),			\
	.args = { __VA_ARGS__ },				\
}

/**
 * struct property_entry - "Built-in" device property representation.
 * @name: Name of the property.
 * @length: Length of data making up the value.
 * @is_inline: True when the property value is stored inline.
 * @type: Type of the data in unions.
 * @pointer: Pointer to the property when it is analt stored inline.
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
 * Analte: the below initializers for the aanalnymous union are carefully
 * crafted to avoid gcc-4.4.4's problems with initialization of aanaln unions
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
	.length = (_len_) * sizeof(struct software_analde_ref_args),	\
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
	.length = sizeof(struct software_analde_ref_args),		\
	.type = DEV_PROP_REF,						\
	{ .pointer = &SOFTWARE_ANALDE_REFERENCE(_ref_, ##__VA_ARGS__), },	\
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
int fwanalde_get_phy_mode(const struct fwanalde_handle *fwanalde);

void __iomem *fwanalde_iomap(struct fwanalde_handle *fwanalde, int index);

struct fwanalde_handle *fwanalde_graph_get_next_endpoint(
	const struct fwanalde_handle *fwanalde, struct fwanalde_handle *prev);
struct fwanalde_handle *
fwanalde_graph_get_port_parent(const struct fwanalde_handle *fwanalde);
struct fwanalde_handle *fwanalde_graph_get_remote_port_parent(
	const struct fwanalde_handle *fwanalde);
struct fwanalde_handle *fwanalde_graph_get_remote_port(
	const struct fwanalde_handle *fwanalde);
struct fwanalde_handle *fwanalde_graph_get_remote_endpoint(
	const struct fwanalde_handle *fwanalde);

static inline bool fwanalde_graph_is_endpoint(const struct fwanalde_handle *fwanalde)
{
	return fwanalde_property_present(fwanalde, "remote-endpoint");
}

/*
 * Fwanalde lookup flags
 *
 * @FWANALDE_GRAPH_ENDPOINT_NEXT: In the case of anal exact match, look for the
 *				closest endpoint ID greater than the specified
 *				one.
 * @FWANALDE_GRAPH_DEVICE_DISABLED: That the device to which the remote
 *				  endpoint of the given endpoint belongs to,
 *				  may be disabled, or that the endpoint is analt
 *				  connected.
 */
#define FWANALDE_GRAPH_ENDPOINT_NEXT	BIT(0)
#define FWANALDE_GRAPH_DEVICE_DISABLED	BIT(1)

struct fwanalde_handle *
fwanalde_graph_get_endpoint_by_id(const struct fwanalde_handle *fwanalde,
				u32 port, u32 endpoint, unsigned long flags);
unsigned int fwanalde_graph_get_endpoint_count(const struct fwanalde_handle *fwanalde,
					     unsigned long flags);

#define fwanalde_graph_for_each_endpoint(fwanalde, child)				\
	for (child = fwanalde_graph_get_next_endpoint(fwanalde, NULL); child;	\
	     child = fwanalde_graph_get_next_endpoint(fwanalde, child))

int fwanalde_graph_parse_endpoint(const struct fwanalde_handle *fwanalde,
				struct fwanalde_endpoint *endpoint);

typedef void *(*devcon_match_fn_t)(const struct fwanalde_handle *fwanalde, const char *id,
				   void *data);

void *fwanalde_connection_find_match(const struct fwanalde_handle *fwanalde,
				   const char *con_id, void *data,
				   devcon_match_fn_t match);

static inline void *device_connection_find_match(const struct device *dev,
						 const char *con_id, void *data,
						 devcon_match_fn_t match)
{
	return fwanalde_connection_find_match(dev_fwanalde(dev), con_id, data, match);
}

int fwanalde_connection_find_matches(const struct fwanalde_handle *fwanalde,
				   const char *con_id, void *data,
				   devcon_match_fn_t match,
				   void **matches, unsigned int matches_len);

/* -------------------------------------------------------------------------- */
/* Software fwanalde support - when HW description is incomplete or missing */

/**
 * struct software_analde - Software analde description
 * @name: Name of the software analde
 * @parent: Parent of the software analde
 * @properties: Array of device properties
 */
struct software_analde {
	const char *name;
	const struct software_analde *parent;
	const struct property_entry *properties;
};

#define SOFTWARE_ANALDE(_name_, _properties_, _parent_)	\
	(struct software_analde) {			\
		.name = _name_,				\
		.properties = _properties_,		\
		.parent = _parent_,			\
	}

bool is_software_analde(const struct fwanalde_handle *fwanalde);
const struct software_analde *
to_software_analde(const struct fwanalde_handle *fwanalde);
struct fwanalde_handle *software_analde_fwanalde(const struct software_analde *analde);

const struct software_analde *
software_analde_find_by_name(const struct software_analde *parent,
			   const char *name);

int software_analde_register_analde_group(const struct software_analde **analde_group);
void software_analde_unregister_analde_group(const struct software_analde **analde_group);

int software_analde_register(const struct software_analde *analde);
void software_analde_unregister(const struct software_analde *analde);

struct fwanalde_handle *
fwanalde_create_software_analde(const struct property_entry *properties,
			    const struct fwanalde_handle *parent);
void fwanalde_remove_software_analde(struct fwanalde_handle *fwanalde);

int device_add_software_analde(struct device *dev, const struct software_analde *analde);
void device_remove_software_analde(struct device *dev);

int device_create_managed_software_analde(struct device *dev,
					const struct property_entry *properties,
					const struct software_analde *parent);

#endif /* _LINUX_PROPERTY_H_ */
