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
#include <linux/kref.h>
#include <linux/mod_devicetable.h>
#include <linux/spinlock.h>
#include <linux/topology.h>

#include <asm/byteorder.h>
#include <asm/errno.h>

typedef u32 phandle;
typedef u32 ihandle;

struct property {
	char	*name;
	int	length;
	void	*value;
	struct property *next;
	unsigned long _flags;
	unsigned int unique_id;
};

#if defined(CONFIG_SPARC)
struct of_irq_controller;
#endif

struct device_node {
	const char *name;
	const char *type;
	phandle phandle;
	char	*full_name;

	struct	property *properties;
	struct	property *deadprops;	/* removed properties */
	struct	device_node *parent;
	struct	device_node *child;
	struct	device_node *sibling;
	struct	device_node *next;	/* next device of same type */
	struct	device_node *allnext;	/* next in list of all nodes */
	struct	proc_dir_entry *pde;	/* this node's proc directory */
	struct	kref kref;
	unsigned long _flags;
	void	*data;
#if defined(CONFIG_SPARC)
	char	*path_component_name;
	unsigned int unique_id;
	struct of_irq_controller *irq_trans;
#endif
};

#define MAX_PHANDLE_ARGS 8
struct of_phandle_args {
	struct device_node *np;
	int args_count;
	uint32_t args[MAX_PHANDLE_ARGS];
};

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

#ifdef CONFIG_OF

/* Pointer for first entry in chain of all nodes. */
extern struct device_node *allnodes;
extern struct device_node *of_chosen;
extern struct device_node *of_aliases;
extern rwlock_t devtree_lock;

static inline bool of_have_populated_dt(void)
{
	return allnodes != NULL;
}

static inline bool of_node_is_root(const struct device_node *node)
{
	return node && (node->parent == NULL);
}

static inline int of_node_check_flag(struct device_node *n, unsigned long flag)
{
	return test_bit(flag, &n->_flags);
}

static inline void of_node_set_flag(struct device_node *n, unsigned long flag)
{
	set_bit(flag, &n->_flags);
}

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

#include <asm/prom.h>

/* Default #address and #size cells.  Allow arch asm/prom.h to override */
#if !defined(OF_ROOT_NODE_ADDR_CELLS_DEFAULT)
#define OF_ROOT_NODE_ADDR_CELLS_DEFAULT 1
#define OF_ROOT_NODE_SIZE_CELLS_DEFAULT 1
#endif

/* Default string compare functions, Allow arch asm/prom.h to override */
#if !defined(of_compat_cmp)
#define of_compat_cmp(s1, s2, l)	strcasecmp((s1), (s2))
#define of_prop_cmp(s1, s2)		strcmp((s1), (s2))
#define of_node_cmp(s1, s2)		strcasecmp((s1), (s2))
#endif

/* flag descriptions */
#define OF_DYNAMIC	1 /* node and properties were allocated via kmalloc */
#define OF_DETACHED	2 /* node has been detached from the device tree */

#define OF_IS_DYNAMIC(x) test_bit(OF_DYNAMIC, &x->_flags)
#define OF_MARK_DYNAMIC(x) set_bit(OF_DYNAMIC, &x->_flags)

#define OF_BAD_ADDR	((u64)-1)

static inline const char* of_node_full_name(struct device_node *np)
{
	return np ? np->full_name : "<no-node>";
}

extern struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name);
#define for_each_node_by_name(dn, name) \
	for (dn = of_find_node_by_name(NULL, name); dn; \
	     dn = of_find_node_by_name(dn, name))
extern struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type);
#define for_each_node_by_type(dn, type) \
	for (dn = of_find_node_by_type(NULL, type); dn; \
	     dn = of_find_node_by_type(dn, type))
extern struct device_node *of_find_compatible_node(struct device_node *from,
	const char *type, const char *compat);
#define for_each_compatible_node(dn, type, compatible) \
	for (dn = of_find_compatible_node(NULL, type, compatible); dn; \
	     dn = of_find_compatible_node(dn, type, compatible))
extern struct device_node *of_find_matching_node(struct device_node *from,
	const struct of_device_id *matches);
#define for_each_matching_node(dn, matches) \
	for (dn = of_find_matching_node(NULL, matches); dn; \
	     dn = of_find_matching_node(dn, matches))
extern struct device_node *of_find_node_by_path(const char *path);
extern struct device_node *of_find_node_by_phandle(phandle handle);
extern struct device_node *of_get_parent(const struct device_node *node);
extern struct device_node *of_get_next_parent(struct device_node *node);
extern struct device_node *of_get_next_child(const struct device_node *node,
					     struct device_node *prev);
#define for_each_child_of_node(parent, child) \
	for (child = of_get_next_child(parent, NULL); child != NULL; \
	     child = of_get_next_child(parent, child))

static inline int of_get_child_count(const struct device_node *np)
{
	struct device_node *child;
	int num = 0;

	for_each_child_of_node(np, child)
		num++;

	return num;
}

extern struct device_node *of_find_node_with_property(
	struct device_node *from, const char *prop_name);
#define for_each_node_with_property(dn, prop_name) \
	for (dn = of_find_node_with_property(NULL, prop_name); dn; \
	     dn = of_find_node_with_property(dn, prop_name))

extern struct property *of_find_property(const struct device_node *np,
					 const char *name,
					 int *lenp);
extern int of_property_read_u32_array(const struct device_node *np,
				      const char *propname,
				      u32 *out_values,
				      size_t sz);
extern int of_property_read_u64(const struct device_node *np,
				const char *propname, u64 *out_value);

extern int of_property_read_string(struct device_node *np,
				   const char *propname,
				   const char **out_string);
extern int of_property_read_string_index(struct device_node *np,
					 const char *propname,
					 int index, const char **output);
extern int of_property_match_string(struct device_node *np,
				    const char *propname,
				    const char *string);
extern int of_property_count_strings(struct device_node *np,
				     const char *propname);
extern int of_device_is_compatible(const struct device_node *device,
				   const char *);
extern int of_device_is_available(const struct device_node *device);
extern const void *of_get_property(const struct device_node *node,
				const char *name,
				int *lenp);
#define for_each_property_of_node(dn, pp) \
	for (pp = dn->properties; pp != NULL; pp = pp->next)

extern int of_n_addr_cells(struct device_node *np);
extern int of_n_size_cells(struct device_node *np);
extern const struct of_device_id *of_match_node(
	const struct of_device_id *matches, const struct device_node *node);
extern int of_modalias_node(struct device_node *node, char *modalias, int len);
extern struct device_node *of_parse_phandle(struct device_node *np,
					    const char *phandle_name,
					    int index);
extern int of_parse_phandle_with_args(struct device_node *np,
	const char *list_name, const char *cells_name, int index,
	struct of_phandle_args *out_args);

extern void of_alias_scan(void * (*dt_alloc)(u64 size, u64 align));
extern int of_alias_get_id(struct device_node *np, const char *stem);

extern int of_machine_is_compatible(const char *compat);

extern int prom_add_property(struct device_node* np, struct property* prop);
extern int prom_remove_property(struct device_node *np, struct property *prop);
extern int prom_update_property(struct device_node *np,
				struct property *newprop);

#if defined(CONFIG_OF_DYNAMIC)
/* For updating the device tree at runtime */
extern void of_attach_node(struct device_node *);
extern void of_detach_node(struct device_node *);
#endif

#define of_match_ptr(_ptr)	(_ptr)

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
#define of_property_for_each_u32(np, propname, prop, p, u)	\
	for (prop = of_find_property(np, propname, NULL),	\
		p = of_prop_next_u32(prop, NULL, &u);		\
		p;						\
		p = of_prop_next_u32(prop, p, &u))

/*
 * struct property *prop;
 * const char *s;
 *
 * of_property_for_each_string(np, "propname", prop, s)
 *         printk("String value: %s\n", s);
 */
const char *of_prop_next_string(struct property *prop, const char *cur);
#define of_property_for_each_string(np, propname, prop, s)	\
	for (prop = of_find_property(np, propname, NULL),	\
		s = of_prop_next_string(prop, NULL);		\
		s;						\
		s = of_prop_next_string(prop, s))

#else /* CONFIG_OF */

static inline const char* of_node_full_name(struct device_node *np)
{
	return "<no-node>";
}

static inline bool of_have_populated_dt(void)
{
	return false;
}

#define for_each_child_of_node(parent, child) \
	while (0)

static inline int of_get_child_count(const struct device_node *np)
{
	return 0;
}

static inline int of_device_is_compatible(const struct device_node *device,
					  const char *name)
{
	return 0;
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

static inline int of_property_read_u32_array(const struct device_node *np,
					     const char *propname,
					     u32 *out_values, size_t sz)
{
	return -ENOSYS;
}

static inline int of_property_read_string(struct device_node *np,
					  const char *propname,
					  const char **out_string)
{
	return -ENOSYS;
}

static inline int of_property_read_string_index(struct device_node *np,
						const char *propname, int index,
						const char **out_string)
{
	return -ENOSYS;
}

static inline int of_property_count_strings(struct device_node *np,
					    const char *propname)
{
	return -ENOSYS;
}

static inline const void *of_get_property(const struct device_node *node,
				const char *name,
				int *lenp)
{
	return NULL;
}

static inline int of_property_read_u64(const struct device_node *np,
				       const char *propname, u64 *out_value)
{
	return -ENOSYS;
}

static inline int of_property_match_string(struct device_node *np,
					   const char *propname,
					   const char *string)
{
	return -ENOSYS;
}

static inline struct device_node *of_parse_phandle(struct device_node *np,
						   const char *phandle_name,
						   int index)
{
	return NULL;
}

static inline int of_parse_phandle_with_args(struct device_node *np,
					     const char *list_name,
					     const char *cells_name,
					     int index,
					     struct of_phandle_args *out_args)
{
	return -ENOSYS;
}

static inline int of_alias_get_id(struct device_node *np, const char *stem)
{
	return -ENOSYS;
}

static inline int of_machine_is_compatible(const char *compat)
{
	return 0;
}

#define of_match_ptr(_ptr)	NULL
#define of_match_node(_matches, _node)	NULL
#define of_property_for_each_u32(np, propname, prop, p, u) \
	while (0)
#define of_property_for_each_string(np, propname, prop, s) \
	while (0)
#endif /* CONFIG_OF */

#ifndef of_node_to_nid
static inline int of_node_to_nid(struct device_node *np)
{
	return numa_node_id();
}

#define of_node_to_nid of_node_to_nid
#endif

/**
 * of_property_read_bool - Findfrom a property
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device node.
 * Returns true if the property exist false otherwise.
 */
static inline bool of_property_read_bool(const struct device_node *np,
					 const char *propname)
{
	struct property *prop = of_find_property(np, propname, NULL);

	return prop ? true : false;
}

static inline int of_property_read_u32(const struct device_node *np,
				       const char *propname,
				       u32 *out_value)
{
	return of_property_read_u32_array(np, propname, out_value, 1);
}

#endif /* _LINUX_OF_H */
