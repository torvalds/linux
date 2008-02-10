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
 * Updates for SPARC by David S. Miller
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <asm/atomic.h>

#define OF_ROOT_NODE_ADDR_CELLS_DEFAULT	2
#define OF_ROOT_NODE_SIZE_CELLS_DEFAULT	1

#define of_compat_cmp(s1, s2, l)	strncmp((s1), (s2), (l))
#define of_prop_cmp(s1, s2)		strcasecmp((s1), (s2))
#define of_node_cmp(s1, s2)		strcmp((s1), (s2))

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

struct of_irq_controller;
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

	struct of_irq_controller *irq_trans;
};

struct of_irq_controller {
	unsigned int	(*irq_build)(struct device_node *, unsigned int, void *);
	void		*data;
};

#define OF_IS_DYNAMIC(x) test_bit(OF_DYNAMIC, &x->_flags)
#define OF_MARK_DYNAMIC(x) set_bit(OF_DYNAMIC, &x->_flags)

extern struct device_node *of_find_node_by_cpuid(int cpuid);
extern int of_set_property(struct device_node *node, const char *name, void *val, int len);
extern int of_getintprop_default(struct device_node *np,
				 const char *name,
				 int def);
extern int of_find_in_proplist(const char *list, const char *match, int len);

extern void prom_build_devicetree(void);

/* Dummy ref counting routines - to be implemented later */
static inline struct device_node *of_node_get(struct device_node *node)
{
	return node;
}
static inline void of_node_put(struct device_node *node)
{
}

/*
 * NB:  This is here while we transition from using asm/prom.h
 * to linux/of.h
 */
#include <linux/of.h>

extern struct device_node *of_console_device;
extern char *of_console_path;
extern char *of_console_options;

#endif /* __KERNEL__ */
#endif /* _SPARC_PROM_H */
