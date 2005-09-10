#ifndef __PPC64_PCI_H
#define __PPC64_PCI_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>

#include <asm/machdep.h>
#include <asm/scatterlist.h>
#include <asm/io.h>
#include <asm/prom.h>

#include <asm-generic/pci-dma-compat.h>

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

struct pci_dev;

#ifdef CONFIG_PPC_ISERIES
#define pcibios_scan_all_fns(a, b)	0
#else
extern int pcibios_scan_all_fns(struct pci_bus *bus, int devfn);
#endif

static inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

#define HAVE_ARCH_PCI_GET_LEGACY_IDE_IRQ
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	if (ppc_md.pci_get_legacy_ide_irq)
		return ppc_md.pci_get_legacy_ide_irq(dev, channel);
	return channel ? 15 : 14;
}

#define HAVE_ARCH_PCI_MWI 1
static inline int pcibios_prep_mwi(struct pci_dev *dev)
{
	/*
	 * We would like to avoid touching the cacheline size or MWI bit
	 * but we cant do that with the current pcibios_prep_mwi 
	 * interface. pSeries firmware sets the cacheline size (which is not
	 * the cpu cacheline size in all cases) and hardware treats MWI 
	 * the same as memory write. So we dont touch the cacheline size
	 * here and allow the generic code to set the MWI bit.
	 */
	return 0;
}

extern unsigned int pcibios_assign_all_busses(void);

extern struct dma_mapping_ops pci_dma_ops;

/* For DAC DMA, we currently don't support it by default, but
 * we let the platform override this
 */
static inline int pci_dac_dma_supported(struct pci_dev *hwdev,u64 mask)
{
	if (pci_dma_ops.dac_dma_supported)
		return pci_dma_ops.dac_dma_supported(&hwdev->dev, mask);
	return 0;
}

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	unsigned long cacheline_size;
	u8 byte;

	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &byte);
	if (byte == 0)
		cacheline_size = 1024;
	else
		cacheline_size = (int) byte * 4;

	*strat = PCI_DMA_BURST_MULTIPLE;
	*strategy_parameter = cacheline_size;
}
#endif

extern int pci_domain_nr(struct pci_bus *bus);

/* Decide whether to display the domain number in /proc */
extern int pci_proc_domain(struct pci_bus *bus);

struct vm_area_struct;
/* Map a range of PCI memory or I/O space for a device into user space */
int pci_mmap_page_range(struct pci_dev *pdev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine);

/* Tell drivers/pci/proc.c that we have pci_mmap_page_range() */
#define HAVE_PCI_MMAP	1

/* pci_unmap_{single,page} is not a nop, thus... */
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)	\
	dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)		\
	__u32 LEN_NAME;
#define pci_unmap_addr(PTR, ADDR_NAME)			\
	((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)		\
	(((PTR)->ADDR_NAME) = (VAL))
#define pci_unmap_len(PTR, LEN_NAME)			\
	((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)		\
	(((PTR)->LEN_NAME) = (VAL))

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(0)
	
extern void
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			struct resource *res);

extern void
pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
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

extern int
unmap_bus_range(struct pci_bus *bus);

extern int
remap_bus_range(struct pci_bus *bus);

extern void
pcibios_fixup_device_resources(struct pci_dev *dev, struct pci_bus *bus);

extern struct pci_controller *init_phb_dynamic(struct device_node *dn);

extern int pci_read_irq_line(struct pci_dev *dev);

extern void pcibios_add_platform_entries(struct pci_dev *dev);

struct file;
extern pgprot_t	pci_phys_mem_access_prot(struct file *file,
					 unsigned long offset,
					 unsigned long size,
					 pgprot_t prot);

#ifdef CONFIG_PPC_MULTIPLATFORM
#define HAVE_ARCH_PCI_RESOURCE_TO_USER
extern void pci_resource_to_user(const struct pci_dev *dev, int bar,
				 const struct resource *rsrc,
				 u64 *start, u64 *end);
#endif /* CONFIG_PPC_MULTIPLATFORM */


#endif	/* __KERNEL__ */

#endif /* __PPC64_PCI_H */
