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
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <asm/irq.h>
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
	/* version 17 fields below */
	u32	dt_struct_size;		/* size of the DT structure block */
};



typedef u32 phandle;
typedef u32 ihandle;

struct property {
	char	*name;
	int	length;
	void	*value;
	struct property *next;
};

struct device_node {
	const char *name;
	const char *type;
	phandle	node;
	phandle linux_phandle;
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


/* New style node lookup */
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
extern struct device_node *of_find_all_nodes(struct device_node *prev);
extern struct device_node *of_get_parent(const struct device_node *node);
extern struct device_node *of_get_next_child(const struct device_node *node,
					     struct device_node *prev);
extern struct property *of_find_property(const struct device_node *np,
					 const char *name,
					 int *lenp);
extern struct device_node *of_node_get(struct device_node *node);
extern void of_node_put(struct device_node *node);

/* For scanning the flat device-tree at boot time */
extern int __init of_scan_flat_dt(int (*it)(unsigned long node,
					    const char *uname, int depth,
					    void *data),
				  void *data);
extern void* __init of_get_flat_dt_prop(unsigned long node, const char *name,
					unsigned long *size);
extern int __init of_flat_dt_is_compatible(unsigned long node, const char *name);
extern unsigned long __init of_get_flat_dt_root(void);

/* For updating the device tree at runtime */
extern void of_attach_node(struct device_node *);
extern void of_detach_node(const struct device_node *);

/* Other Prototypes */
extern void finish_device_tree(void);
extern void unflatten_device_tree(void);
extern void early_init_devtree(void *);
extern int of_device_is_compatible(const struct device_node *device,
				const char *);
#define device_is_compatible(d, c)	of_device_is_compatible((d), (c))
extern int machine_is_compatible(const char *compat);
extern const void *of_get_property(const struct device_node *node,
				const char *name,
				int *lenp);
#define get_property(a, b, c)	of_get_property((a), (b), (c))
extern void print_properties(struct device_node *node);
extern int of_n_addr_cells(struct device_node* np);
extern int of_n_size_cells(struct device_node* np);
extern int prom_n_intr_cells(struct device_node* np);
extern void prom_get_irq_senses(unsigned char *senses, int off, int max);
extern int prom_add_property(struct device_node* np, struct property* prop);
extern int prom_remove_property(struct device_node *np, struct property *prop);
extern int prom_update_property(struct device_node *np,
				struct property *newprop,
				struct property *oldprop);

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


/*
 * OF address retreival & translation
 */


/* Helper to read a big number; size is in cells (not bytes) */
static inline u64 of_read_number(const u32 *cell, int size)
{
	u64 r = 0;
	while (size--)
		r = (r << 32) | *(cell++);
	return r;
}

/* Like of_read_number, but we want an unsigned long result */
#ifdef CONFIG_PPC32
static inline unsigned long of_read_ulong(const u32 *cell, int size)
{
	return cell[size-1];
}
#else
#define of_read_ulong(cell, size)	of_read_number(cell, size)
#endif

/* Translate an OF address block into a CPU physical address
 */
#define OF_BAD_ADDR	((u64)-1)
extern u64 of_translate_address(struct device_node *np, const u32 *addr);

/* Extract an address from a device, returns the region size and
 * the address space flags too. The PCI version uses a BAR number
 * instead of an absolute index
 */
extern const u32 *of_get_address(struct device_node *dev, int index,
			   u64 *size, unsigned int *flags);
extern const u32 *of_get_pci_address(struct device_node *dev, int bar_no,
			       u64 *size, unsigned int *flags);

/* Get an address as a resource. Note that if your address is
 * a PIO address, the conversion will fail if the physical address
 * can't be internally converted to an IO token with
 * pci_address_to_pio(), that is because it's either called to early
 * or it can't be matched to any host bridge IO space
 */
extern int of_address_to_resource(struct device_node *dev, int index,
				  struct resource *r);
extern int of_pci_address_to_resource(struct device_node *dev, int bar,
				      struct resource *r);

/* Parse the ibm,dma-window property of an OF node into the busno, phys and
 * size parameters.
 */
void of_parse_dma_window(struct device_node *dn, const void *dma_window_prop,
		unsigned long *busno, unsigned long *phys, unsigned long *size);

extern void kdump_move_device_tree(void);

/* CPU OF node matching */
struct device_node *of_get_cpu_node(int cpu, unsigned int *thread);

/* Get the MAC address */
extern const void *of_get_mac_address(struct device_node *np);

/*
 * OF interrupt mapping
 */

/* This structure is returned when an interrupt is mapped. The controller
 * field needs to be put() after use
 */

#define OF_MAX_IRQ_SPEC		 4 /* We handle specifiers of at most 4 cells */

struct of_irq {
	struct device_node *controller;	/* Interrupt controller node */
	u32 size;			/* Specifier size */
	u32 specifier[OF_MAX_IRQ_SPEC];	/* Specifier copy */
};

/**
 * of_irq_map_init - Initialize the irq remapper
 * @flags:	flags defining workarounds to enable
 *
 * Some machines have bugs in the device-tree which require certain workarounds
 * to be applied. Call this before any interrupt mapping attempts to enable
 * those workarounds.
 */
#define OF_IMAP_OLDWORLD_MAC	0x00000001
#define OF_IMAP_NO_PHANDLE	0x00000002

extern void of_irq_map_init(unsigned int flags);

/**
 * of_irq_map_raw - Low level interrupt tree parsing
 * @parent:	the device interrupt parent
 * @intspec:	interrupt specifier ("interrupts" property of the device)
 * @ointsize:   size of the passed in interrupt specifier
 * @addr:	address specifier (start of "reg" property of the device)
 * @out_irq:	structure of_irq filled by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent.
 *
 */

extern int of_irq_map_raw(struct device_node *parent, const u32 *intspec,
			  u32 ointsize, const u32 *addr,
			  struct of_irq *out_irq);


/**
 * of_irq_map_one - Resolve an interrupt for a device
 * @device:	the device whose interrupt is to be resolved
 * @index:     	index of the interrupt to resolve
 * @out_irq:	structure of_irq filled by this function
 *
 * This function resolves an interrupt, walking the tree, for a given
 * device-tree node. It's the high level pendant to of_irq_map_raw().
 * It also implements the workarounds for OldWolrd Macs.
 */
extern int of_irq_map_one(struct device_node *device, int index,
			  struct of_irq *out_irq);

/**
 * of_irq_map_pci - Resolve the interrupt for a PCI device
 * @pdev:	the device whose interrupt is to be resolved
 * @out_irq:	structure of_irq filled by this function
 *
 * This function resolves the PCI interrupt for a given PCI device. If a
 * device-node exists for a given pci_dev, it will use normal OF tree
 * walking. If not, it will implement standard swizzling and walk up the
 * PCI tree until an device-node is found, at which point it will finish
 * resolving using the OF tree walking.
 */
struct pci_dev;
extern int of_irq_map_pci(struct pci_dev *pdev, struct of_irq *out_irq);

extern int of_irq_to_resource(struct device_node *dev, int index,
			struct resource *r);

/**
 * of_iomap - Maps the memory mapped IO for a given device_node
 * @device:	the device whose io range will be mapped
 * @index:	index of the io range
 *
 * Returns a pointer to the mapped memory
 */
extern void __iomem *of_iomap(struct device_node *device, int index);

#endif /* __KERNEL__ */
#endif /* _POWERPC_PROM_H */
