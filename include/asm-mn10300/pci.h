/* MN10300 PCI definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_PCI_H
#define _ASM_PCI_H

#ifdef __KERNEL__
#include <linux/mm.h>		/* for struct page */

#if 0
#define __pcbdebug(FMT, ADDR, ...) \
	printk(KERN_DEBUG "PCIBRIDGE[%08x]: "FMT"\n", \
	       (u32)(ADDR), ##__VA_ARGS__)

#define __pcidebug(FMT, BUS, DEVFN, WHERE,...)		\
do {							\
	printk(KERN_DEBUG "PCI[%02x:%02x.%x + %02x]: "FMT"\n",	\
	       (BUS)->number,					\
	       PCI_SLOT(DEVFN),					\
	       PCI_FUNC(DEVFN),					\
	       (u32)(WHERE), ##__VA_ARGS__);			\
} while (0)

#else
#define __pcbdebug(FMT, ADDR, ...)		do {} while (0)
#define __pcidebug(FMT, BUS, DEVFN, WHERE, ...)	do {} while (0)
#endif

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes or
 * architectures with incomplete PCI setup by the loader */

#ifdef CONFIG_PCI
#define pcibios_assign_all_busses()	1
extern void unit_pci_init(void);
#else
#define pcibios_assign_all_busses()	0
#endif

extern unsigned long pci_mem_start;
#define PCIBIOS_MIN_IO		0xBE000004
#define PCIBIOS_MIN_MEM		0xB8000000

void pcibios_set_master(struct pci_dev *dev);
void pcibios_penalize_isa_irq(int irq);

/* Dynamic DMA mapping stuff.
 * i386 has everything mapped statically.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <asm/io.h>

struct pci_dev;

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)


/* This is always fine. */
#define pci_dac_dma_supported(pci_dev, mask)	(0)

/* Return the index of the PCI controller for device. */
static inline int pci_controller_num(struct pci_dev *dev)
{
	return 0;
}

#define HAVE_PCI_MMAP
extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_state,
			       int write_combine);

#endif /* __KERNEL__ */

/* implement the pci_ DMA API in terms of the generic device dma_ one */
#include <asm-generic/pci-dma-compat.h>

/**
 * pcibios_resource_to_bus - convert resource to PCI bus address
 * @dev: device which owns this resource
 * @region: converted bus-centric region (start,end)
 * @res: resource to convert
 *
 * Convert a resource to a PCI device bus address or bus window.
 */
extern void pcibios_resource_to_bus(struct pci_dev *dev,
				    struct pci_bus_region *region,
				    struct resource *res);

extern void pcibios_bus_to_resource(struct pci_dev *dev,
				    struct resource *res,
				    struct pci_bus_region *region);

static inline struct resource *
pcibios_select_root(struct pci_dev *pdev, struct resource *res)
{
	struct resource *root = NULL;

	if (res->flags & IORESOURCE_IO)
		root = &ioport_resource;
	if (res->flags & IORESOURCE_MEM)
		root = &iomem_resource;

	return root;
}

#define pcibios_scan_all_fns(a, b)	0

#endif /* _ASM_PCI_H */
