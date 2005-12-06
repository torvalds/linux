#ifndef __PPC_PCI_H
#define __PPC_PCI_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <asm/scatterlist.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm-generic/pci-dma-compat.h>

struct pci_dev;

/* Values for the `which' argument to sys_pciconfig_iobase syscall.  */
#define IOBASE_BRIDGE_NUMBER	0
#define IOBASE_MEMORY		1
#define IOBASE_IO		2
#define IOBASE_ISA_IO		3
#define IOBASE_ISA_MEM		4

/*
 * Set this to 1 if you want the kernel to re-assign all PCI
 * bus numbers
 */
extern int pci_assign_all_buses;

#define pcibios_assign_all_busses()	(pci_assign_all_buses)
#define pcibios_scan_all_fns(a, b)	0

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

extern inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

extern inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

extern unsigned long pci_resource_to_bus(struct pci_dev *pdev, struct resource *res);

/*
 * The PCI bus bridge can translate addresses issued by the processor(s)
 * into a different address on the PCI bus.  On 32-bit cpus, we assume
 * this mapping is 1-1, but on 64-bit systems it often isn't.
 *
 * Obsolete ! Drivers should now use pci_resource_to_bus
 */
extern unsigned long phys_to_bus(unsigned long pa);
extern unsigned long pci_phys_to_bus(unsigned long pa, int busnr);
extern unsigned long pci_bus_to_phys(unsigned int ba, int busnr);

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     (1)

/* pci_unmap_{page,single} is a nop so... */
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)
#define pci_unmap_addr(PTR, ADDR_NAME)		(0)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	do { } while (0)
#define pci_unmap_len(PTR, LEN_NAME)		(0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	do { } while (0)

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}
#endif

/*
 * At present there are very few 32-bit PPC machines that can have
 * memory above the 4GB point, and we don't support that.
 */
#define pci_dac_dma_supported(pci_dev, mask)	(0)

/* Return the index of the PCI controller for device PDEV. */
#define pci_domain_nr(bus) ((struct pci_controller *)(bus)->sysdata)->index

/* Set the name of the bus as it appears in /proc/bus/pci */
static inline int pci_proc_domain(struct pci_bus *bus)
{
	return 0;
}

/* Map a range of PCI memory or I/O space for a device into user space */
int pci_mmap_page_range(struct pci_dev *pdev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine);

/* Tell drivers/pci/proc.c that we have pci_mmap_page_range() */
#define HAVE_PCI_MMAP	1

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

extern void pcibios_add_platform_entries(struct pci_dev *dev);

struct file;
extern pgprot_t	pci_phys_mem_access_prot(struct file *file,
					 unsigned long pfn,
					 unsigned long size,
					 pgprot_t prot);

#define HAVE_ARCH_PCI_RESOURCE_TO_USER
extern void pci_resource_to_user(const struct pci_dev *dev, int bar,
				 const struct resource *rsrc,
				 u64 *start, u64 *end);


#endif	/* __KERNEL__ */

#endif /* __PPC_PCI_H */
