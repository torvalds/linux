#ifdef __KERNEL__
#ifndef _ASM_PCI_BRIDGE_H
#define _ASM_PCI_BRIDGE_H

#include <linux/pci.h>

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
	volatile unsigned char __iomem *cfg_data;

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

struct device_node *fetch_dev_dn(struct pci_dev *dev);

/* Get a device_node from a pci_dev.  This code must be fast except in the case
 * where the sysdata is incorrect and needs to be fixed up (hopefully just once)
 */
static inline struct device_node *pci_device_to_OF_node(struct pci_dev *dev)
{
	struct device_node *dn = dev->sysdata;

	if (dn->devfn == dev->devfn && dn->busno == dev->bus->number)
		return dn;	/* fast path.  sysdata is good */
	else
		return fetch_dev_dn(dev);
}

static inline struct device_node *pci_bus_to_OF_node(struct pci_bus *bus)
{
	if (bus->self)
		return pci_device_to_OF_node(bus->self);
	else
		return bus->sysdata; /* Must be root bus (PHB) */
}

extern void pci_process_bridge_OF_ranges(struct pci_controller *hose,
					 struct device_node *dev);

extern int pcibios_remove_root_bus(struct pci_controller *phb);

extern void phbs_remap_io(void);

static inline struct pci_controller *pci_bus_to_host(struct pci_bus *bus)
{
	struct device_node *busdn = bus->sysdata;

	BUG_ON(busdn == NULL);
	return busdn->phb;
}

#endif
#endif /* __KERNEL__ */
