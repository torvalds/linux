#ifndef _POWERPC_PROM_H
#define _POWERPC_PROM_H
#ifdef __KERNEL__

/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * Updates for PPC64 by Peter Bergner & David Engebretsen, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <asm/atomic.h>

/* Definitions used by the flattened device tree */
#define OF_DT_HEADER		0xd00dfeed	/* marker */
#define OF_DT_BEGIN_NODE	0x1		/* Start of node, full name */
#define OF_DT_END_NODE		0x2		/* End node */
#define OF_DT_PROP		0x3		/* Property: name off, size,
						 * content */
#define OF_DT_NOP		0x4		/* nop */
#define OF_DT_END		0x9

#define OF_DT_VERSION		0x10

/*
 * This is what gets passed to the kernel by prom_init or kexec
 *
 * The dt struct contains the device tree structure, full pathes and
 * property contents. The dt strings contain a separate block with just
 * the strings for the property names, and is fully page aligned and
 * self contained in a page, so that it can be kept around by the kernel,
 * each property name appears only once in this page (cheap compression)
 *
 * the mem_rsvmap contains a map of reserved ranges of physical memory,
 * passing it here instead of in the device-tree itself greatly simplifies
 * the job of everybody. It's just a list of u64 pairs (base/size) that
 * ends when size is 0
 */
struct boot_param_header
{
	u32	magic;			/* magic word OF_DT_HEADER */
	u32	totalsize;		/* total size of DT block */
	u32	off_dt_struct;		/* offset to structure */
	u32	off_dt_strings;		/* offset to strings */
	u32	off_mem_rsvmap;		/* offset to memory reserve map */
	u32	version;		/* format version */
	u32	last_comp_version;	/* last compatible version */
	/* version 2 fields below */
	u32	boot_cpuid_phys;	/* Physical CPU id we're booting on */
	/* version 3 fields below */
	u32	dt_strings_size;	/* size of the DT strings block */
};



typedef u32 phandle;
typedef u32 ihandle;

struct address_range {
	unsigned long space;
	unsigned long address;
	unsigned long size;
};

struct interrupt_info {
	int	line;
	int	sense;		/* +ve/-ve logic, edge or level, etc. */
};

struct pci_address {
	u32 a_hi;
	u32 a_mid;
	u32 a_lo;
};

struct isa_address {
	u32 a_hi;
	u32 a_lo;
};

struct isa_range {
	struct isa_address isa_addr;
	struct pci_address pci_addr;
	unsigned int size;
};

struct reg_property {
	unsigned long address;
	unsigned long size;
};

struct reg_property32 {
	unsigned int address;
	unsigned int size;
};

struct reg_property64 {
	u64 address;
	u64 size;
};

struct property {
	char	*name;
	int	length;
	unsigned char *value;
	struct property *next;
};

struct device_node {
	char	*name;
	char	*type;
	phandle	node;
	phandle linux_phandle;
	int	n_addrs;
	struct	address_range *addrs;
	int	n_intrs;
	struct	interrupt_info *intrs;
	char	*full_name;

	struct	property *properties;
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

extern struct device_node *of_chosen;

/* flag descriptions */
#define OF_DYNAMIC 1 /* node and properties were allocated via kmalloc */

#define OF_IS_DYNAMIC(x) test_bit(OF_DYNAMIC, &x->_flags)
#define OF_MARK_DYNAMIC(x) set_bit(OF_DYNAMIC, &x->_flags)

#define HAVE_ARCH_DEVTREE_FIXUPS

static inline void set_node_proc_entry(struct device_node *dn, struct proc_dir_entry *de)
{
	dn->pde = de;
}


/* OBSOLETE: Old style node lookup */
extern struct device_node *find_devices(const char *name);
extern struct device_node *find_type_devices(const char *type);
extern struct device_node *find_path_device(const char *path);
extern struct device_node *find_compatible_devices(const char *type,
						   const char *compat);
extern struct device_node *find_all_nodes(void);

/* New style node lookup */
extern struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name);
extern struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type);
extern struct device_node *of_find_compatible_node(struct device_node *from,
	const char *type, const char *compat);
extern struct device_node *of_find_node_by_path(const char *path);
extern struct device_node *of_find_node_by_phandle(phandle handle);
extern struct device_node *of_find_all_nodes(struct device_node *prev);
extern struct device_node *of_get_parent(const struct device_node *node);
extern struct device_node *of_get_next_child(const struct device_node *node,
					     struct device_node *prev);
extern struct device_node *of_node_get(struct device_node *node);
extern void of_node_put(struct device_node *node);

/* For scanning the flat device-tree at boot time */
int __init of_scan_flat_dt(int (*it)(unsigned long node,
				     const char *uname, int depth,
				     void *data),
			   void *data);
void* __init of_get_flat_dt_prop(unsigned long node, const char *name,
				 unsigned long *size);

/* For updating the device tree at runtime */
extern void of_attach_node(struct device_node *);
extern void of_detach_node(const struct device_node *);

/* Other Prototypes */
extern void finish_device_tree(void);
extern void unflatten_device_tree(void);
extern void early_init_devtree(void *);
extern int device_is_compatible(struct device_node *device, const char *);
extern int machine_is_compatible(const char *compat);
extern unsigned char *get_property(struct device_node *node, const char *name,
				   int *lenp);
extern void print_properties(struct device_node *node);
extern int prom_n_addr_cells(struct device_node* np);
extern int prom_n_size_cells(struct device_node* np);
extern int prom_n_intr_cells(struct device_node* np);
extern void prom_get_irq_senses(unsigned char *senses, int off, int max);
extern int prom_add_property(struct device_node* np, struct property* prop);

#ifdef CONFIG_PPC32
/*
 * PCI <-> OF matching functions
 * (XXX should these be here?)
 */
struct pci_bus;
struct pci_dev;
extern int pci_device_from_OF_node(struct device_node *node,
				   u8* bus, u8* devfn);
extern struct device_node* pci_busdev_to_OF_node(struct pci_bus *, int);
extern struct device_node* pci_device_to_OF_node(struct pci_dev *);
extern void pci_create_OF_bus_map(void);
#endif

extern struct resource *request_OF_resource(struct device_node* node,
				int index, const char* name_postfix);
extern int release_OF_resource(struct device_node* node, int index);

#endif /* __KERNEL__ */
#endif /* _POWERPC_PROM_H */
