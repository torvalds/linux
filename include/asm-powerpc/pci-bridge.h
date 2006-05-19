#ifndef _ASM_POWERPC_PCI_BRIDGE_H
#define _ASM_POWERPC_PCI_BRIDGE_H
#ifdef __KERNEL__

#ifndef CONFIG_PPC64
#include <asm-ppc/pci-bridge.h>
#else

#include <linux/pci.h>
#include <linux/list.h>

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * Structure of a PCI controller (host bridge)
 */
struct pci_controller {
	struct pci_bus *bus;
	char is_dynamic;
	void *arch_data;
	struct list_head list_node;

	int first_busno;
	int last_busno;

	void __iomem *io_base_virt;
	unsigned long io_base_phys;

	/* Some machines have a non 1:1 mapping of
	 * the PCI memory space in the CPU bus space
	 */
	unsigned long pci_mem_offset;
	unsigned long pci_io_size;

	struct pci_ops *ops;
	volatile unsigned int __iomem *cfg_addr;
	volatile void __iomem *cfg_data;

	/* Currently, we limit ourselves to 1 IO range and 3 mem
	 * ranges since the common pci_bus structure can't handle more
	 */
	struct resource io_resource;
	struct resource mem_resources[3];
	int global_number;		
	int local_number;		
	unsigned long buid;
	unsigned long dma_window_base_cur;
	unsigned long dma_window_size;
};

/*
 * PCI stuff, for nodes representing PCI devices, pointed to
 * by device_node->data.
 */
struct pci_controller;
struct iommu_table;

struct pci_dn {
	int	busno;			/* pci bus number */
	int	bussubno;		/* pci subordinate bus number */
	int	devfn;			/* pci device and function number */
	int	class_code;		/* pci device class */

#ifdef CONFIG_PPC_PSERIES
	int	eeh_mode;		/* See eeh.h for possible EEH_MODEs */
	int	eeh_config_addr;
	int	eeh_pe_config_addr; /* new-style partition endpoint address */
	int 	eeh_check_count;	/* # times driver ignored error */
	int 	eeh_freeze_count;	/* # times this device froze up. */
#endif
	int	pci_ext_config_space;	/* for pci devices */
	struct  pci_controller *phb;	/* for pci devices */
	struct	iommu_table *iommu_table;	/* for phb's or bridges */
	struct	pci_dev *pcidev;	/* back-pointer to the pci device */
	struct	device_node *node;	/* back-pointer to the device_node */
#ifdef CONFIG_PPC_ISERIES
	int	Flags;			/* Possible flags(disable/bist)*/
	u8	LogicalSlot;		/* Hv Slot Index for Tces */
#endif
	u32	config_space[16];	/* saved PCI config space */
};

/* Get the pointer to a device_node's pci_dn */
#define PCI_DN(dn)	((struct pci_dn *) (dn)->data)

struct device_node *fetch_dev_dn(struct pci_dev *dev);

/* Get a device_node from a pci_dev.  This code must be fast except
 * in the case where the sysdata is incorrect and needs to be fixed
 * up (this will only happen once).
 * In this case the sysdata will have been inherited from a PCI host
 * bridge or a PCI-PCI bridge further up the tree, so it will point
 * to a valid struct pci_dn, just not the one we want.
 */
static inline struct device_node *pci_device_to_OF_node(struct pci_dev *dev)
{
	struct device_node *dn = dev->sysdata;
	struct pci_dn *pdn = dn->data;

	if (pdn && pdn->devfn == dev->devfn && pdn->busno == dev->bus->number)
		return dn;	/* fast path.  sysdata is good */
	return fetch_dev_dn(dev);
}

static inline int pci_device_from_OF_node(struct device_node *np,
					  u8 *bus, u8 *devfn)
{
	if (!PCI_DN(np))
		return -ENODEV;
	*bus = PCI_DN(np)->busno;
	*devfn = PCI_DN(np)->devfn;
	return 0;
}

static inline struct device_node *pci_bus_to_OF_node(struct pci_bus *bus)
{
	if (bus->self)
		return pci_device_to_OF_node(bus->self);
	else
		return bus->sysdata; /* Must be root bus (PHB) */
}

/** Find the bus corresponding to the indicated device node */
struct pci_bus * pcibios_find_pci_bus(struct device_node *dn);

extern void pci_process_bridge_OF_ranges(struct pci_controller *hose,
					 struct device_node *dev, int primary);

/** Remove all of the PCI devices under this bus */
void pcibios_remove_pci_devices(struct pci_bus *bus);

/** Discover new pci devices under this bus, and add them */
void pcibios_add_pci_devices(struct pci_bus * bus);
void pcibios_fixup_new_pci_devices(struct pci_bus *bus, int fix_bus);

extern int pcibios_remove_root_bus(struct pci_controller *phb);

static inline struct pci_controller *pci_bus_to_host(struct pci_bus *bus)
{
	struct device_node *busdn = bus->sysdata;

	BUG_ON(busdn == NULL);
	return PCI_DN(busdn)->phb;
}

extern struct pci_controller*
pci_find_hose_for_OF_device(struct device_node* node);

extern struct pci_controller *
pcibios_alloc_controller(struct device_node *dev);
extern void pcibios_free_controller(struct pci_controller *phb);

#ifdef CONFIG_PCI
extern unsigned long pci_address_to_pio(phys_addr_t address);
#else
static inline unsigned long pci_address_to_pio(phys_addr_t address)
{
	return (unsigned long)-1;
}
#endif

/* Return values for ppc_md.pci_probe_mode function */
#define PCI_PROBE_NONE		-1	/* Don't look at this bus at all */
#define PCI_PROBE_NORMAL	0	/* Do normal PCI probing */
#define PCI_PROBE_DEVTREE	1	/* Instantiate from device tree */

#endif /* CONFIG_PPC64 */
#endif /* __KERNEL__ */
#endif
