#ifndef _LINUX_OF_H
#define _LINUX_OF_H
/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh and other computers.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * Updates for PPC64 by Peter Bergner & David Engebretsen, IBM Corp.
 * Updates for SPARC64 by David S. Miller
 * Derived from PowerPC and Sparc prom.h files by Stephen Rothwell, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <linux/mod_devicetable.h>
#include <linux/spinlock.h>
#include <linux/topology.h>
#include <linux/notifier.h>
#include <linux/property.h>
#include <linux/list.h>

#include <asm/byteorder.h>
#include <asm/errno.h>

typedef u32 phandle;
typedef u32 ihandle;

struct property {
	char	*name;
	int	length;
	void	*value;
	struct property *next;
#if defined(CONFIG_OF_DYNAMIC) || defined(CONFIG_SPARC)
	unsigned long _flags;
#endif
#if defined(CONFIG_OF_PROMTREE)
	unsigned int unique_id;
#endif
#if defined(CONFIG_OF_KOBJ)
	struct bin_attribute attr;
#endif
};

#if defined(CONFIG_SPARC)
struct of_irq_controller;
#endif

struct device_node {
	const char *name;
	const char *type;
	phandle phandle;
	const char *full_name;
	struct fwnode_handle fwnode;

	struct	property *properties;
	struct	property *deadprops;	/* removed properties */
	struct	device_node *parent;
	struct	device_node *child;
	struct	device_node *sibling;
#if defined(CONFIG_OF_KOBJ)
	struct	kobject kobj;
#endif
	unsigned long _flags;
	void	*data;
#if defined(CONFIG_SPARC)
	const char *path_component_name;
	unsigned int unique_id;
	struct of_irq_controller *irq_trans;
#endif
};

#define MAX_PHANDLE_ARGS 16
struct of_phandle_args {
	struct device_node *np;
	int args_count;
	uint32_t args[MAX_PHANDLE_ARGS];
};

struct of_phandle_iterator {
	/* Common iterator information */
	const char *cells_name;
	int cell_count;
	const struct device_node *parent;

	/* List size information */
	const __be32 *list_end;
	const __be32 *phandle_end;

	/* Current position state */
	const __be32 *cur;
	uint32_t cur_count;
	phandle phandle;
	struct device_node *node;
};

struct of_reconfig_data {
	struct device_node	*dn;
	struct property		*prop;
	struct property		*old_prop;
};

/* initialize a node */
extern struct kobj_type of_node_ktype;
extern const struct fwnode_operations of_fwnode_ops;
static inline void of_node_init(struct device_node *node)
{
#if defined(CONFIG_OF_KOBJ)
	kobject_init(&node->kobj, &of_node_ktype);
#endif
	node->fwnode.ops = &of_fwnode_ops;
}

#if defined(CONFIG_OF_KOBJ)
#define of_node_kobj(n) (&(n)->kobj)
#else
#define of_node_kobj(n) NULL
#endif

#ifdef CONFIG_OF_DYNAMIC
extern struct device_node *of_node_get(struct device_node *node);
extern void of_node_put(struct device_node *node);
#else /* CONFIG_OF_DYNAMIC */
/* Dummy ref counting routines - to be implemented later */
static inline struct device_node *of_node_get(struct device_node *node)
{
	return node;
}
static inline void of_node_put(struct device_node *node) { }
#endif /* !CONFIG_OF_DYNAMIC */

/* Pointer for first entry in chain of all nodes. */
extern struct device_node *of_root;
extern struct device_node *of_chosen;
extern struct device_node *of_aliases;
extern struct device_node *of_stdout;
extern raw_spinlock_t devtree_lock;

/* flag descriptions (need to be visible even when !CONFIG_OF) */
#define OF_DYNAMIC	1 /* node and properties were allocated via kmalloc */
#define OF_DETACHED	2 /* node has been detached from the device tree */
#define OF_POPULATED	3 /* device already created for the node */
#define OF_POPULATED_BUS	4 /* of_platform_populate recursed to children of this node */

#define OF_BAD_ADDR	((u64)-1)

#ifdef CONFIG_OF
void of_core_init(void);

static inline bool is_of_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) && fwnode->ops == &of_fwnode_ops;
}

#define to_of_node(__fwnode)						\
	({								\
		typeof(__fwnode) __to_of_node_fwnode = (__fwnode);	\
									\
		is_of_node(__to_of_node_fwnode) ?			\
			container_of(__to_of_node_fwnode,		\
				     struct device_node, fwnode) :	\
			NULL;						\
	})

#define of_fwnode_handle(node)						\
	({								\
		typeof(node) __of_fwnode_handle_node = (node);		\
									\
		__of_fwnode_handle_node ?				\
			&__of_fwnode_handle_node->fwnode : NULL;	\
	})

static inline bool of_have_populated_dt(void)
{
	return of_root != NULL;
}

static inline bool of_node_is_root(const struct device_node *node)
{
	return node && (node->parent == NULL);
}

static inline int of_node_check_flag(struct device_node *n, unsigned long flag)
{
	return test_bit(flag, &n->_flags);
}

static inline int of_node_test_and_set_flag(struct device_node *n,
					    unsigned long flag)
{
	return test_and_set_bit(flag, &n->_flags);
}

static inline void of_node_set_flag(struct device_node *n, unsigned long flag)
{
	set_bit(flag, &n->_flags);
}

static inline void of_node_clear_flag(struct device_node *n, unsigned long flag)
{
	clear_bit(flag, &n->_flags);
}

#if defined(CONFIG_OF_DYNAMIC) || defined(CONFIG_SPARC)
static inline int of_property_check_flag(struct property *p, unsigned long flag)
{
	return test_bit(flag, &p->_flags);
}

static inline void of_property_set_flag(struct property *p, unsigned long flag)
{
	set_bit(flag, &p->_flags);
}

static inline void of_property_clear_flag(struct property *p, unsigned long flag)
{
	clear_bit(flag, &p->_flags);
}
#endif

extern struct device_node *__of_find_all_nodes(struct device_node *prev);
extern struct device_node *of_find_all_nodes(struct device_node *prev);

/*
 * OF address retrieval & translation
 */

/* Helper to read a big number; size is in cells (not bytes) */
static inline u64 of_read_number(const __be32 *cell, int size)
{
	u64 r = 0;
	while (size--)
		r = (r << 32) | be32_to_cpu(*(cell++));
	return r;
}

/* Like of_read_number, but we want an unsigned long result */
static inline unsigned long of_read_ulong(const __be32 *cell, int size)
{
	/* toss away upper bits if unsigned long is smaller than u64 */
	return of_read_number(cell, size);
}

#if defined(CONFIG_SPARC)
#include <asm/prom.h>
#endif

/* Default #address and #size cells.  Allow arch asm/prom.h to override */
#if !defined(OF_ROOT_NODE_ADDR_CELLS_DEFAULT)
#define OF_ROOT_NODE_ADDR_CELLS_DEFAULT 1
#define OF_ROOT_NODE_SIZE_CELLS_DEFAULT 1
#endif

#define OF_IS_DYNAMIC(x) test_bit(OF_DYNAMIC, &x->_flags)
#define OF_MARK_DYNAMIC(x) set_bit(OF_DYNAMIC, &x->_flags)

static inline const char *of_node_full_name(const struct device_node *np)
{
	return np ? np->full_name : "<no-node>";
}

#define for_each_of_allnodes_from(from, dn) \
	for (dn = __of_find_all_nodes(from); dn; dn = __of_find_all_nodes(dn))
#define for_each_of_allnodes(dn) for_each_of_allnodes_from(NULL, dn)
extern struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name);
extern struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type);
extern struct device_node *of_find_compatible_node(struct device_node *from,
	const char *type, const char *compat);
extern struct device_node *of_find_matching_node_and_match(
	struct device_node *from,
	const struct of_device_id *matches,
	const struct of_device_id **match);

extern struct device_node *of_find_node_opts_by_path(const char *path,
	const char **opts);
static inline struct device_node *of_find_node_by_path(const char *path)
{
	return of_find_node_opts_by_path(path, NULL);
}

extern struct device_node *of_find_node_by_phandle(phandle handle);
extern struct device_node *of_get_parent(const struct device_node *node);
extern struct device_node *of_get_next_parent(struct device_node *node);
extern struct device_node *of_get_next_child(const struct device_node *node,
					     struct device_node *prev);
extern struct device_node *of_get_next_available_child(
	const struct device_node *node, struct device_node *prev);

extern struct device_node *of_get_child_by_name(const struct device_node *node,
					const char *name);

/* cache lookup */
extern struct device_node *of_find_next_cache_node(const struct device_node *);
extern int of_find_last_cache_level(unsigned int cpu);
extern struct device_node *of_find_node_with_property(
	struct device_node *from, const char *prop_name);

extern struct property *of_find_property(const struct device_node *np,
					 const char *name,
					 int *lenp);
extern int of_property_count_elems_of_size(const struct device_node *np,
				const char *propname, int elem_size);
extern int of_property_read_u32_index(const struct device_node *np,
				       const char *propname,
				       u32 index, u32 *out_value);
extern int of_property_read_u64_index(const struct device_node *np,
				       const char *propname,
				       u32 index, u64 *out_value);
extern int of_property_read_variable_u8_array(const struct device_node *np,
					const char *propname, u8 *out_values,
					size_t sz_min, size_t sz_max);
extern int of_property_read_variable_u16_array(const struct device_node *np,
					const char *propname, u16 *out_values,
					size_t sz_min, size_t sz_max);
extern int of_property_read_variable_u32_array(const struct device_node *np,
					const char *propname,
					u32 *out_values,
					size_t sz_min,
					size_t sz_max);
extern int of_property_read_u64(const struct device_node *np,
				const char *propname, u64 *out_value);
extern int of_property_read_variable_u64_array(const struct device_node *np,
					const char *propname,
					u64 *out_values,
					size_t sz_min,
					size_t sz_max);

extern int of_property_read_string(const struct device_node *np,
				   const char *propname,
				   const char **out_string);
extern int of_property_match_string(const struct device_node *np,
				    const char *propname,
				    const char *string);
extern int of_property_read_string_helper(const struct device_node *np,
					      const char *propname,
					      const char **out_strs, size_t sz, int index);
extern int of_device_is_compatible(const struct device_node *device,
				   const char *);
extern int of_device_compatible_match(struct device_node *device,
				      const char *const *compat);
extern bool of_device_is_available(const struct device_node *device);
extern bool of_device_is_big_endian(const struct device_node *device);
extern const void *of_get_property(const struct device_node *node,
				const char *name,
				int *lenp);
extern struct device_node *of_get_cpu_node(int cpu, unsigned int *thread);
#define for_each_property_of_node(dn, pp) \
	for (pp = dn->properties; pp != NULL; pp = pp->next)

extern int of_n_addr_cells(struct device_node *np);
extern int of_n_size_cells(struct device_node *np);
extern const struct of_device_id *of_match_node(
	const struct of_device_id *matches, const struct device_node *node);
extern int of_modalias_node(struct device_node *node, char *modalias, int len);
extern void of_print_phandle_args(const char *msg, const struct of_phandle_args *args);
extern struct device_node *of_parse_phandle(const struct device_node *np,
					    const char *phandle_name,
					    int index);
extern int of_parse_phandle_with_args(const struct device_node *np,
	const char *list_name, const char *cells_name, int index,
	struct of_phandle_args *out_args);
extern int of_parse_phandle_with_fixed_args(const struct device_node *np,
	const char *list_name, int cells_count, int index,
	struct of_phandle_args *out_args);
extern int of_count_phandle_with_args(const struct device_node *np,
	const char *list_name, const char *cells_name);

/* phandle iterator functions */
extern int of_phandle_iterator_init(struct of_phandle_iterator *it,
				    const struct device_node *np,
				    const char *list_name,
				    const char *cells_name,
				    int cell_count);

extern int of_phandle_iterator_next(struct of_phandle_iterator *it);
extern int of_phandle_iterator_args(struct of_phandle_iterator *it,
				    uint32_t *args,
				    int size);

extern void of_alias_scan(void * (*dt_alloc)(u64 size, u64 align));
extern int of_alias_get_id(struct device_node *np, const char *stem);
extern int of_alias_get_highest_id(const char *stem);

extern int of_machine_is_compatible(const char *compat);

extern int of_add_property(struct device_node *np, struct property *prop);
extern int of_remove_property(struct device_node *np, struct property *prop);
extern int of_update_property(struct device_node *np, struct property *newprop);

/* For updating the device tree at runtime */
#define OF_RECONFIG_ATTACH_NODE		0x0001
#define OF_RECONFIG_DETACH_NODE		0x0002
#define OF_RECONFIG_ADD_PROPERTY	0x0003
#define OF_RECONFIG_REMOVE_PROPERTY	0x0004
#define OF_RECONFIG_UPDATE_PROPERTY	0x0005

extern int of_attach_node(struct device_node *);
extern int of_detach_node(struct device_node *);

#define of_match_ptr(_ptr)	(_ptr)

/**
 * of_property_read_u8_array - Find and read an array of u8 from a property.
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device node and read 8-bit value(s) from
 * it. Returns 0 on success, -EINVAL if the property does not exist,
 * -ENODATA if property does not have a value, and -EOVERFLOW if the
 * property data isn't large enough.
 *
 * dts entry of array should be like:
 *	property = /bits/ 8 <0x50 0x60 0x70>;
 *
 * The out_values is modified only if a valid u8 value can be decoded.
 */
static inline int of_property_read_u8_array(const struct device_node *np,
					    const char *propname,
					    u8 *out_values, size_t sz)
{
	int ret = of_property_read_variable_u8_array(np, propname, out_values,
						     sz, 0);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

/**
 * of_property_read_u16_array - Find and read an array of u16 from a property.
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device node and read 16-bit value(s) from
 * it. Returns 0 on success, -EINVAL if the property does not exist,
 * -ENODATA if property does not have a value, and -EOVERFLOW if the
 * property data isn't large enough.
 *
 * dts entry of array should be like:
 *	property = /bits/ 16 <0x5000 0x6000 0x7000>;
 *
 * The out_values is modified only if a valid u16 value can be decoded.
 */
static inline int of_property_read_u16_array(const struct device_node *np,
					     const char *propname,
					     u16 *out_values, size_t sz)
{
	int ret = of_property_read_variable_u16_array(np, propname, out_values,
						      sz, 0);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

/**
 * of_property_read_u32_array - Find and read an array of 32 bit integers
 * from a property.
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device node and read 32-bit value(s) from
 * it. Returns 0 on success, -EINVAL if the property does not exist,
 * -ENODATA if property does not have a value, and -EOVERFLOW if the
 * property data isn't large enough.
 *
 * The out_values is modified only if a valid u32 value can be decoded.
 */
static inline int of_property_read_u32_array(const struct device_node *np,
					     const char *propname,
					     u32 *out_values, size_t sz)
{
	int ret = of_property_read_variable_u32_array(np, propname, out_values,
						      sz, 0);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

/**
 * of_property_read_u64_array - Find and read an array of 64 bit integers
 * from a property.
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device node and read 64-bit value(s) from
 * it. Returns 0 on success, -EINVAL if the property does not exist,
 * -ENODATA if property does not have a value, and -EOVERFLOW if the
 * property data isn't large enough.
 *
 * The out_values is modified only if a valid u64 value can be decoded.
 */
static inline int of_property_read_u64_array(const struct device_node *np,
					     const char *propname,
					     u64 *out_values, size_t sz)
{
	int ret = of_property_read_variable_u64_array(np, propname, out_values,
						      sz, 0);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

/*
 * struct property *prop;
 * const __be32 *p;
 * u32 u;
 *
 * of_property_for_each_u32(np, "propname", prop, p, u)
 *         printk("U32 value: %x\n", u);
 */
const __be32 *of_prop_next_u32(struct property *prop, const __be32 *cur,
			       u32 *pu);
/*
 * struct property *prop;
 * const char *s;
 *
 * of_property_for_each_string(np, "propname", prop, s)
 *         printk("String value: %s\n", s);
 */
const char *of_prop_next_string(struct property *prop, const char *cur);

bool of_console_check(struct device_node *dn, char *name, int index);

extern int of_cpu_node_to_id(struct device_node *np);

#else /* CONFIG_OF */

static inline void of_core_init(void)
{
}

static inline bool is_of_node(const struct fwnode_handle *fwnode)
{
	return false;
}

static inline struct device_node *to_of_node(const struct fwnode_handle *fwnode)
{
	return NULL;
}

static inline const char* of_node_full_name(const struct device_node *np)
{
	return "<no-node>";
}

static inline struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name)
{
	return NULL;
}

static inline struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type)
{
	return NULL;
}

static inline struct device_node *of_find_matching_node_and_match(
	struct device_node *from,
	const struct of_device_id *matches,
	const struct of_device_id **match)
{
	return NULL;
}

static inline struct device_node *of_find_node_by_path(const char *path)
{
	return NULL;
}

static inline struct device_node *of_find_node_opts_by_path(const char *path,
	const char **opts)
{
	return NULL;
}

static inline struct device_node *of_find_node_by_phandle(phandle handle)
{
	return NULL;
}

static inline struct device_node *of_get_parent(const struct device_node *node)
{
	return NULL;
}

static inline struct device_node *of_get_next_child(
	const struct device_node *node, struct device_node *prev)
{
	return NULL;
}

static inline struct device_node *of_get_next_available_child(
	const struct device_node *node, struct device_node *prev)
{
	return NULL;
}

static inline struct device_node *of_find_node_with_property(
	struct device_node *from, const char *prop_name)
{
	return NULL;
}

#define of_fwnode_handle(node) NULL

static inline bool of_have_populated_dt(void)
{
	return false;
}

static inline struct device_node *of_get_child_by_name(
					const struct device_node *node,
					const char *name)
{
	return NULL;
}

static inline int of_device_is_compatible(const struct device_node *device,
					  const char *name)
{
	return 0;
}

static inline  int of_device_compatible_match(struct device_node *device,
					      const char *const *compat)
{
	return 0;
}

static inline bool of_device_is_available(const struct device_node *device)
{
	return false;
}

static inline bool of_device_is_big_endian(const struct device_node *device)
{
	return false;
}

static inline struct property *of_find_property(const struct device_node *np,
						const char *name,
						int *lenp)
{
	return NULL;
}

static inline struct device_node *of_find_compatible_node(
						struct device_node *from,
						const char *type,
						const char *compat)
{
	return NULL;
}

static inline int of_property_count_elems_of_size(const struct device_node *np,
			const char *propname, int elem_size)
{
	return -ENOSYS;
}

static inline int of_property_read_u8_array(const struct device_node *np,
			const char *propname, u8 *out_values, size_t sz)
{
	return -ENOSYS;
}

static inline int of_property_read_u16_array(const struct device_node *np,
			const char *propname, u16 *out_values, size_t sz)
{
	return -ENOSYS;
}

static inline int of_property_read_u32_array(const struct device_node *np,
					     const char *propname,
					     u32 *out_values, size_t sz)
{
	return -ENOSYS;
}

static inline int of_property_read_u64_array(const struct device_node *np,
					     const char *propname,
					     u64 *out_values, size_t sz)
{
	return -ENOSYS;
}

static inline int of_property_read_u32_index(const struct device_node *np,
			const char *propname, u32 index, u32 *out_value)
{
	return -ENOSYS;
}

static inline int of_property_read_u64_index(const struct device_node *np,
			const char *propname, u32 index, u64 *out_value)
{
	return -ENOSYS;
}

static inline const void *of_get_property(const struct device_node *node,
				const char *name,
				int *lenp)
{
	return NULL;
}

static inline struct device_node *of_get_cpu_node(int cpu,
					unsigned int *thread)
{
	return NULL;
}

static inline int of_n_addr_cells(struct device_node *np)
{
	return 0;

}
static inline int of_n_size_cells(struct device_node *np)
{
	return 0;
}

static inline int of_property_read_variable_u8_array(const struct device_node *np,
					const char *propname, u8 *out_values,
					size_t sz_min, size_t sz_max)
{
	return -ENOSYS;
}

static inline int of_property_read_variable_u16_array(const struct device_node *np,
					const char *propname, u16 *out_values,
					size_t sz_min, size_t sz_max)
{
	return -ENOSYS;
}

static inline int of_property_read_variable_u32_array(const struct device_node *np,
					const char *propname,
					u32 *out_values,
					size_t sz_min,
					size_t sz_max)
{
	return -ENOSYS;
}

static inline int of_property_read_u64(const struct device_node *np,
				       const char *propname, u64 *out_value)
{
	return -ENOSYS;
}

static inline int of_property_read_variable_u64_array(const struct device_node *np,
					const char *propname,
					u64 *out_values,
					size_t sz_min,
					size_t sz_max)
{
	return -ENOSYS;
}

static inline int of_property_read_string(const struct device_node *np,
					  const char *propname,
					  const char **out_string)
{
	return -ENOSYS;
}

static inline int of_property_match_string(const struct device_node *np,
					   const char *propname,
					   const char *string)
{
	return -ENOSYS;
}

static inline int of_property_read_string_helper(const struct device_node *np,
						 const char *propname,
						 const char **out_strs, size_t sz, int index)
{
	return -ENOSYS;
}

static inline struct device_node *of_parse_phandle(const struct device_node *np,
						   const char *phandle_name,
						   int index)
{
	return NULL;
}

static inline int of_parse_phandle_with_args(const struct device_node *np,
					     const char *list_name,
					     const char *cells_name,
					     int index,
					     struct of_phandle_args *out_args)
{
	return -ENOSYS;
}

static inline int of_parse_phandle_with_fixed_args(const struct device_node *np,
	const char *list_name, int cells_count, int index,
	struct of_phandle_args *out_args)
{
	return -ENOSYS;
}

static inline int of_count_phandle_with_args(struct device_node *np,
					     const char *list_name,
					     const char *cells_name)
{
	return -ENOSYS;
}

static inline int of_phandle_iterator_init(struct of_phandle_iterator *it,
					   const struct device_node *np,
					   const char *list_name,
					   const char *cells_name,
					   int cell_count)
{
	return -ENOSYS;
}

static inline int of_phandle_iterator_next(struct of_phandle_iterator *it)
{
	return -ENOSYS;
}

static inline int of_phandle_iterator_args(struct of_phandle_iterator *it,
					   uint32_t *args,
					   int size)
{
	return 0;
}

static inline int of_alias_get_id(struct device_node *np, const char *stem)
{
	return -ENOSYS;
}

static inline int of_alias_get_highest_id(const char *stem)
{
	return -ENOSYS;
}

static inline int of_machine_is_compatible(const char *compat)
{
	return 0;
}

static inline bool of_console_check(const struct device_node *dn, const char *name, int index)
{
	return false;
}

static inline const __be32 *of_prop_next_u32(struct property *prop,
		const __be32 *cur, u32 *pu)
{
	return NULL;
}

static inline const char *of_prop_next_string(struct property *prop,
		const char *cur)
{
	return NULL;
}

static inline int of_node_check_flag(struct device_node *n, unsigned long flag)
{
	return 0;
}

static inline int of_node_test_and_set_flag(struct device_node *n,
					    unsigned long flag)
{
	return 0;
}

static inline void of_node_set_flag(struct device_node *n, unsigned long flag)
{
}

static inline void of_node_clear_flag(struct device_node *n, unsigned long flag)
{
}

static inline int of_property_check_flag(struct property *p, unsigned long flag)
{
	return 0;
}

static inline void of_property_set_flag(struct property *p, unsigned long flag)
{
}

static inline void of_property_clear_flag(struct property *p, unsigned long flag)
{
}

static inline int of_cpu_node_to_id(struct device_node *np)
{
	return -ENODEV;
}

#define of_match_ptr(_ptr)	NULL
#define of_match_node(_matches, _node)	NULL
#endif /* CONFIG_OF */

/* Default string compare functions, Allow arch asm/prom.h to override */
#if !defined(of_compat_cmp)
#define of_compat_cmp(s1, s2, l)	strcasecmp((s1), (s2))
#define of_prop_cmp(s1, s2)		strcmp((s1), (s2))
#define of_node_cmp(s1, s2)		strcasecmp((s1), (s2))
#endif

#if defined(CONFIG_OF) && defined(CONFIG_NUMA)
extern int of_node_to_nid(struct device_node *np);
#else
static inline int of_node_to_nid(struct device_node *device)
{
	return NUMA_NO_NODE;
}
#endif

#ifdef CONFIG_OF_NUMA
extern int of_numa_init(void);
#else
static inline int of_numa_init(void)
{
	return -ENOSYS;
}
#endif

static inline struct device_node *of_find_matching_node(
	struct device_node *from,
	const struct of_device_id *matches)
{
	return of_find_matching_node_and_match(from, matches, NULL);
}

/**
 * of_property_count_u8_elems - Count the number of u8 elements in a property
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device node and count the number of u8 elements
 * in it. Returns number of elements on sucess, -EINVAL if the property does
 * not exist or its length does not match a multiple of u8 and -ENODATA if the
 * property does not have a value.
 */
static inline int of_property_count_u8_elems(const struct device_node *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u8));
}

/**
 * of_property_count_u16_elems - Count the number of u16 elements in a property
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device node and count the number of u16 elements
 * in it. Returns number of elements on sucess, -EINVAL if the property does
 * not exist or its length does not match a multiple of u16 and -ENODATA if the
 * property does not have a value.
 */
static inline int of_property_count_u16_elems(const struct device_node *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u16));
}

/**
 * of_property_count_u32_elems - Count the number of u32 elements in a property
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device node and count the number of u32 elements
 * in it. Returns number of elements on sucess, -EINVAL if the property does
 * not exist or its length does not match a multiple of u32 and -ENODATA if the
 * property does not have a value.
 */
static inline int of_property_count_u32_elems(const struct device_node *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u32));
}

/**
 * of_property_count_u64_elems - Count the number of u64 elements in a property
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device node and count the number of u64 elements
 * in it. Returns number of elements on sucess, -EINVAL if the property does
 * not exist or its length does not match a multiple of u64 and -ENODATA if the
 * property does not have a value.
 */
static inline int of_property_count_u64_elems(const struct device_node *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u64));
}

/**
 * of_property_read_string_array() - Read an array of strings from a multiple
 * strings property.
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_strs:	output array of string pointers.
 * @sz:		number of array elements to read.
 *
 * Search for a property in a device tree node and retrieve a list of
 * terminated string values (pointer to data, not a copy) in that property.
 *
 * If @out_strs is NULL, the number of strings in the property is returned.
 */
static inline int of_property_read_string_array(const struct device_node *np,
						const char *propname, const char **out_strs,
						size_t sz)
{
	return of_property_read_string_helper(np, propname, out_strs, sz, 0);
}

/**
 * of_property_count_strings() - Find and return the number of strings from a
 * multiple strings property.
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device tree node and retrieve the number of null
 * terminated string contain in it. Returns the number of strings on
 * success, -EINVAL if the property does not exist, -ENODATA if property
 * does not have a value, and -EILSEQ if the string is not null-terminated
 * within the length of the property data.
 */
static inline int of_property_count_strings(const struct device_node *np,
					    const char *propname)
{
	return of_property_read_string_helper(np, propname, NULL, 0, 0);
}

/**
 * of_property_read_string_index() - Find and read a string from a multiple
 * strings property.
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @index:	index of the string in the list of strings
 * @out_string:	pointer to null terminated return string, modified only if
 *		return value is 0.
 *
 * Search for a property in a device tree node and retrieve a null
 * terminated string value (pointer to data, not a copy) in the list of strings
 * contained in that property.
 * Returns 0 on success, -EINVAL if the property does not exist, -ENODATA if
 * property does not have a value, and -EILSEQ if the string is not
 * null-terminated within the length of the property data.
 *
 * The out_string pointer is modified only if a valid string can be decoded.
 */
static inline int of_property_read_string_index(const struct device_node *np,
						const char *propname,
						int index, const char **output)
{
	int rc = of_property_read_string_helper(np, propname, output, 1, index);
	return rc < 0 ? rc : 0;
}

/**
 * of_property_read_bool - Findfrom a property
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device node.
 * Returns true if the property exists false otherwise.
 */
static inline bool of_property_read_bool(const struct device_node *np,
					 const char *propname)
{
	struct property *prop = of_find_property(np, propname, NULL);

	return prop ? true : false;
}

static inline int of_property_read_u8(const struct device_node *np,
				       const char *propname,
				       u8 *out_value)
{
	return of_property_read_u8_array(np, propname, out_value, 1);
}

static inline int of_property_read_u16(const struct device_node *np,
				       const char *propname,
				       u16 *out_value)
{
	return of_property_read_u16_array(np, propname, out_value, 1);
}

static inline int of_property_read_u32(const struct device_node *np,
				       const char *propname,
				       u32 *out_value)
{
	return of_property_read_u32_array(np, propname, out_value, 1);
}

static inline int of_property_read_s32(const struct device_node *np,
				       const char *propname,
				       s32 *out_value)
{
	return of_property_read_u32(np, propname, (u32*) out_value);
}

#define of_for_each_phandle(it, err, np, ln, cn, cc)			\
	for (of_phandle_iterator_init((it), (np), (ln), (cn), (cc)),	\
	     err = of_phandle_iterator_next(it);			\
	     err == 0;							\
	     err = of_phandle_iterator_next(it))

#define of_property_for_each_u32(np, propname, prop, p, u)	\
	for (prop = of_find_property(np, propname, NULL),	\
		p = of_prop_next_u32(prop, NULL, &u);		\
		p;						\
		p = of_prop_next_u32(prop, p, &u))

#define of_property_for_each_string(np, propname, prop, s)	\
	for (prop = of_find_property(np, propname, NULL),	\
		s = of_prop_next_string(prop, NULL);		\
		s;						\
		s = of_prop_next_string(prop, s))

#define for_each_node_by_name(dn, name) \
	for (dn = of_find_node_by_name(NULL, name); dn; \
	     dn = of_find_node_by_name(dn, name))
#define for_each_node_by_type(dn, type) \
	for (dn = of_find_node_by_type(NULL, type); dn; \
	     dn = of_find_node_by_type(dn, type))
#define for_each_compatible_node(dn, type, compatible) \
	for (dn = of_find_compatible_node(NULL, type, compatible); dn; \
	     dn = of_find_compatible_node(dn, type, compatible))
#define for_each_matching_node(dn, matches) \
	for (dn = of_find_matching_node(NULL, matches); dn; \
	     dn = of_find_matching_node(dn, matches))
#define for_each_matching_node_and_match(dn, matches, match) \
	for (dn = of_find_matching_node_and_match(NULL, matches, match); \
	     dn; dn = of_find_matching_node_and_match(dn, matches, match))

#define for_each_child_of_node(parent, child) \
	for (child = of_get_next_child(parent, NULL); child != NULL; \
	     child = of_get_next_child(parent, child))
#define for_each_available_child_of_node(parent, child) \
	for (child = of_get_next_available_child(parent, NULL); child != NULL; \
	     child = of_get_next_available_child(parent, child))

#define for_each_node_with_property(dn, prop_name) \
	for (dn = of_find_node_with_property(NULL, prop_name); dn; \
	     dn = of_find_node_with_property(dn, prop_name))

static inline int of_get_child_count(const struct device_node *np)
{
	struct device_node *child;
	int num = 0;

	for_each_child_of_node(np, child)
		num++;

	return num;
}

static inline int of_get_available_child_count(const struct device_node *np)
{
	struct device_node *child;
	int num = 0;

	for_each_available_child_of_node(np, child)
		num++;

	return num;
}

#if defined(CONFIG_OF) && !defined(MODULE)
#define _OF_DECLARE(table, name, compat, fn, fn_type)			\
	static const struct of_device_id __of_table_##name		\
		__used __section(__##table##_of_table)			\
		 = { .compatible = compat,				\
		     .data = (fn == (fn_type)NULL) ? fn : fn  }
#else
#define _OF_DECLARE(table, name, compat, fn, fn_type)			\
	static const struct of_device_id __of_table_##name		\
		__attribute__((unused))					\
		 = { .compatible = compat,				\
		     .data = (fn == (fn_type)NULL) ? fn : fn }
#endif

typedef int (*of_init_fn_2)(struct device_node *, struct device_node *);
typedef int (*of_init_fn_1_ret)(struct device_node *);
typedef void (*of_init_fn_1)(struct device_node *);

#define OF_DECLARE_1(table, name, compat, fn) \
		_OF_DECLARE(table, name, compat, fn, of_init_fn_1)
#define OF_DECLARE_1_RET(table, name, compat, fn) \
		_OF_DECLARE(table, name, compat, fn, of_init_fn_1_ret)
#define OF_DECLARE_2(table, name, compat, fn) \
		_OF_DECLARE(table, name, compat, fn, of_init_fn_2)

/**
 * struct of_changeset_entry	- Holds a changeset entry
 *
 * @node:	list_head for the log list
 * @action:	notifier action
 * @np:		pointer to the device node affected
 * @prop:	pointer to the property affected
 * @old_prop:	hold a pointer to the original property
 *
 * Every modification of the device tree during a changeset
 * is held in a list of of_changeset_entry structures.
 * That way we can recover from a partial application, or we can
 * revert the changeset
 */
struct of_changeset_entry {
	struct list_head node;
	unsigned long action;
	struct device_node *np;
	struct property *prop;
	struct property *old_prop;
};

/**
 * struct of_changeset - changeset tracker structure
 *
 * @entries:	list_head for the changeset entries
 *
 * changesets are a convenient way to apply bulk changes to the
 * live tree. In case of an error, changes are rolled-back.
 * changesets live on after initial application, and if not
 * destroyed after use, they can be reverted in one single call.
 */
struct of_changeset {
	struct list_head entries;
};

enum of_reconfig_change {
	OF_RECONFIG_NO_CHANGE = 0,
	OF_RECONFIG_CHANGE_ADD,
	OF_RECONFIG_CHANGE_REMOVE,
};

#ifdef CONFIG_OF_DYNAMIC
extern int of_reconfig_notifier_register(struct notifier_block *);
extern int of_reconfig_notifier_unregister(struct notifier_block *);
extern int of_reconfig_notify(unsigned long, struct of_reconfig_data *rd);
extern int of_reconfig_get_state_change(unsigned long action,
					struct of_reconfig_data *arg);

extern void of_changeset_init(struct of_changeset *ocs);
extern void of_changeset_destroy(struct of_changeset *ocs);
extern int of_changeset_apply(struct of_changeset *ocs);
extern int of_changeset_revert(struct of_changeset *ocs);
extern int of_changeset_action(struct of_changeset *ocs,
		unsigned long action, struct device_node *np,
		struct property *prop);

static inline int of_changeset_attach_node(struct of_changeset *ocs,
		struct device_node *np)
{
	return of_changeset_action(ocs, OF_RECONFIG_ATTACH_NODE, np, NULL);
}

static inline int of_changeset_detach_node(struct of_changeset *ocs,
		struct device_node *np)
{
	return of_changeset_action(ocs, OF_RECONFIG_DETACH_NODE, np, NULL);
}

static inline int of_changeset_add_property(struct of_changeset *ocs,
		struct device_node *np, struct property *prop)
{
	return of_changeset_action(ocs, OF_RECONFIG_ADD_PROPERTY, np, prop);
}

static inline int of_changeset_remove_property(struct of_changeset *ocs,
		struct device_node *np, struct property *prop)
{
	return of_changeset_action(ocs, OF_RECONFIG_REMOVE_PROPERTY, np, prop);
}

static inline int of_changeset_update_property(struct of_changeset *ocs,
		struct device_node *np, struct property *prop)
{
	return of_changeset_action(ocs, OF_RECONFIG_UPDATE_PROPERTY, np, prop);
}
#else /* CONFIG_OF_DYNAMIC */
static inline int of_reconfig_notifier_register(struct notifier_block *nb)
{
	return -EINVAL;
}
static inline int of_reconfig_notifier_unregister(struct notifier_block *nb)
{
	return -EINVAL;
}
static inline int of_reconfig_notify(unsigned long action,
				     struct of_reconfig_data *arg)
{
	return -EINVAL;
}
static inline int of_reconfig_get_state_change(unsigned long action,
						struct of_reconfig_data *arg)
{
	return -EINVAL;
}
#endif /* CONFIG_OF_DYNAMIC */

/**
 * of_device_is_system_power_controller - Tells if system-power-controller is found for device_node
 * @np: Pointer to the given device_node
 *
 * return true if present false otherwise
 */
static inline bool of_device_is_system_power_controller(const struct device_node *np)
{
	return of_property_read_bool(np, "system-power-controller");
}

/**
 * Overlay support
 */

enum of_overlay_notify_action {
	OF_OVERLAY_PRE_APPLY = 0,
	OF_OVERLAY_POST_APPLY,
	OF_OVERLAY_PRE_REMOVE,
	OF_OVERLAY_POST_REMOVE,
};

struct of_overlay_notify_data {
	struct device_node *overlay;
	struct device_node *target;
};

#ifdef CONFIG_OF_OVERLAY

/* ID based overlays; the API for external users */
int of_overlay_apply(struct device_node *tree, int *ovcs_id);
int of_overlay_remove(int *ovcs_id);
int of_overlay_remove_all(void);

int of_overlay_notifier_register(struct notifier_block *nb);
int of_overlay_notifier_unregister(struct notifier_block *nb);

#else

static inline int of_overlay_apply(struct device_node *tree, int *ovcs_id)
{
	return -ENOTSUPP;
}

static inline int of_overlay_remove(int *ovcs_id)
{
	return -ENOTSUPP;
}

static inline int of_overlay_remove_all(void)
{
	return -ENOTSUPP;
}

static inline int of_overlay_notifier_register(struct notifier_block *nb)
{
	return 0;
}

static inline int of_overlay_notifier_unregister(struct notifier_block *nb)
{
	return 0;
}

#endif

#endif /* _LINUX_OF_H */
