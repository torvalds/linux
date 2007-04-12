#ifndef _SPARC_PROM_H
#define _SPARC_PROM_H
#ifdef __KERNEL__


/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * Updates for PPC64 by Peter Bergner & David Engebretsen, IBM Corp.
 * Updates for SPARC32 by David S. Miller
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/proc_fs.h>
#include <asm/atomic.h>

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

struct device_node {
	const char	*name;
	const char	*type;
	phandle	node;
	char	*path_component_name;
	char	*full_name;

	struct	property *properties;
	struct  property *deadprops; /* removed properties */
	struct	device_node *parent;
	struct	device_node *child;
	struct	device_node *sibling;
	struct	device_node *next;	/* next device of same type */
	struct	device_node *allnext;	/* next in list of all nodes */
	struct  proc_dir_entry *pde;	/* this node's proc directory */
	struct  kref kref;
	unsigned long _flags;
	void	*data;
	unsigned int unique_id;
};

/* flag descriptions */
#define OF_DYNAMIC 1 /* node and properties were allocated via kmalloc */

#define OF_IS_DYNAMIC(x) test_bit(OF_DYNAMIC, &x->_flags)
#define OF_MARK_DYNAMIC(x) set_bit(OF_DYNAMIC, &x->_flags)

#define OF_BAD_ADDR	((u64)-1)

static inline void set_node_proc_entry(struct device_node *dn, struct proc_dir_entry *de)
{
	dn->pde = de;
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
extern struct device_node *of_find_node_by_path(const char *path);
extern struct device_node *of_find_node_by_phandle(phandle handle);
extern struct device_node *of_get_parent(const struct device_node *node);
extern struct device_node *of_get_next_child(const struct device_node *node,
					     struct device_node *prev);
extern struct property *of_find_property(const struct device_node *np,
					 const char *name,
					 int *lenp);
extern int of_device_is_compatible(const struct device_node *device,
				   const char *);
extern const void *of_get_property(const struct device_node *node,
				   const char *name,
				   int *lenp);
#define get_property(node,name,lenp) of_get_property(node,name,lenp)
extern int of_set_property(struct device_node *node, const char *name, void *val, int len);
extern int of_getintprop_default(struct device_node *np,
				 const char *name,
				 int def);
extern int of_n_addr_cells(struct device_node *np);
extern int of_n_size_cells(struct device_node *np);

extern void prom_build_devicetree(void);

#endif /* __KERNEL__ */
#endif /* _SPARC_PROM_H */
