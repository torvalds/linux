/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#ifdef __KERNEL__
#ifndef _PPC_PROM_H
#define _PPC_PROM_H

#include <linux/config.h>
#include <linux/types.h>

typedef u32 phandle;
typedef u32 ihandle;

struct address_range {
	unsigned int space;
	unsigned int address;
	unsigned int size;
};

struct interrupt_info {
	int	line;
	int	sense;		/* +ve/-ve logic, edge or level, etc. */
};

struct reg_property {
	unsigned int address;
	unsigned int size;
};

struct property {
	char	*name;
	int	length;
	unsigned char *value;
	struct property *next;
};

/*
 * Note: don't change this structure for now or you'll break BootX !
 */
struct device_node {
	char	*name;
	char	*type;
	phandle	node;
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
};

struct prom_args;
typedef void (*prom_entry)(struct prom_args *);

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
extern struct device_node *of_find_all_nodes(struct device_node *prev);
extern struct device_node *of_get_parent(const struct device_node *node);
extern struct device_node *of_get_next_child(const struct device_node *node,
					     struct device_node *prev);
extern struct device_node *of_node_get(struct device_node *node);
extern void of_node_put(struct device_node *node);

/* Other Prototypes */
extern void abort(void);
extern unsigned long prom_init(int, int, prom_entry);
extern void prom_print(const char *msg);
extern void relocate_nodes(void);
extern void finish_device_tree(void);
extern int device_is_compatible(struct device_node *device, const char *);
extern int machine_is_compatible(const char *compat);
extern unsigned char *get_property(struct device_node *node, const char *name,
				   int *lenp);
extern int prom_add_property(struct device_node* np, struct property* prop);
extern void prom_get_irq_senses(unsigned char *, int, int);
extern int prom_n_addr_cells(struct device_node* np);
extern int prom_n_size_cells(struct device_node* np);

extern struct resource*
request_OF_resource(struct device_node* node, int index, const char* name_postfix);
extern int release_OF_resource(struct device_node* node, int index);

extern void print_properties(struct device_node *node);
extern int call_rtas(const char *service, int nargs, int nret,
		     unsigned long *outputs, ...);

/*
 * PCI <-> OF matching functions
 */
struct pci_bus;
struct pci_dev;
extern int pci_device_from_OF_node(struct device_node *node,
				   u8* bus, u8* devfn);
extern struct device_node* pci_busdev_to_OF_node(struct pci_bus *, int);
extern struct device_node* pci_device_to_OF_node(struct pci_dev *);
extern void pci_create_OF_bus_map(void);

/*
 * When we call back to the Open Firmware client interface, we usually
 * have to do that before the kernel is relocated to its final location
 * (this is because we can't use OF after we have overwritten the
 * exception vectors with our exception handlers).  These macros assist
 * in performing the address calculations that we need to do to access
 * data when the kernel is running at an address that is different from
 * the address that the kernel is linked at.  The reloc_offset() function
 * returns the difference between these two addresses and the macros
 * simplify the process of adding or subtracting this offset to/from
 * pointer values.  See arch/ppc/kernel/prom.c for how these are used.
 */
extern unsigned long reloc_offset(void);
extern unsigned long add_reloc_offset(unsigned long);
extern unsigned long sub_reloc_offset(unsigned long);

#define PTRRELOC(x)	((typeof(x))add_reloc_offset((unsigned long)(x)))
#define PTRUNRELOC(x)	((typeof(x))sub_reloc_offset((unsigned long)(x)))


/*
 * OF address retreival & translation
 */


/* Translate an OF address block into a CPU physical address
 */
#define OF_BAD_ADDR	((u64)-1)
extern u64 of_translate_address(struct device_node *np, u32 *addr);

/* Extract an address from a device, returns the region size and
 * the address space flags too. The PCI version uses a BAR number
 * instead of an absolute index
 */
extern u32 *of_get_address(struct device_node *dev, int index,
			   u64 *size, unsigned int *flags);
extern u32 *of_get_pci_address(struct device_node *dev, int bar_no,
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

#ifndef CONFIG_PPC_OF
/*
 * Fallback definitions for builds where we don't have prom.c included.
 */
#define machine_is_compatible(x)		0
#define of_find_compatible_node(f, t, c)	NULL
#define get_property(p, n, l)			NULL
#endif

#endif /* _PPC_PROM_H */
#endif /* __KERNEL__ */
