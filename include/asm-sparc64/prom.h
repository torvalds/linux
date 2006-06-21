#ifndef _SPARC64_PROM_H
#define _SPARC64_PROM_H
#ifdef __KERNEL__


/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * Updates for PPC64 by Peter Bergner & David Engebretsen, IBM Corp.
 * Updates for SPARC64 by David S. Miller
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

struct interrupt_info {
	int	line;
	int	sense;		/* +ve/-ve logic, edge or level, etc. */
};

struct property {
	char	*name;
	int	length;
	void	*value;
	struct property *next;
};

struct device_node {
	char	*name;
	char	*type;
	phandle	node;
	phandle linux_phandle;
	int	n_intrs;
	struct	interrupt_info *intrs;
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
};

static inline void set_node_proc_entry(struct device_node *dn, struct proc_dir_entry *de)
{
	dn->pde = de;
}

extern struct device_node *of_find_node_by_path(const char *path);
extern struct device_node *of_get_parent(const struct device_node *node);
extern struct device_node *of_get_next_child(const struct device_node *node,
					     struct device_node *prev);
extern struct property *of_find_property(struct device_node *np,
					 const char *name,
					 int *lenp);

extern void prom_build_devicetree(void);

#endif /* __KERNEL__ */
#endif /* _SPARC64_PROM_H */
