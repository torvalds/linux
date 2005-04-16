#ifndef __ASM_SH64_PCI_H
#define __ASM_SH64_PCI_H

#ifdef __KERNEL__

#include <linux/dma-mapping.h>

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

#define pcibios_assign_all_busses()     1

/*
 * These are currently the correct values for the STM overdrive board
 * We need some way of setting this on a board specific way, it will
 * not be the same on other boards I think
 */
#if defined(CONFIG_CPU_SUBTYPE_SH5_101) || defined(CONFIG_CPU_SUBTYPE_SH5_103)
#define PCIBIOS_MIN_IO          0x2000
#define PCIBIOS_MIN_MEM         0x40000000
#endif

extern void pcibios_set_master(struct pci_dev *dev);

/*
 * Set penalize isa irq function
 */
static inline void pcibios_penalize_isa_irq(int irq)
{
	/* We don't do dynamic PCI IRQ allocation */
}

/* Dynamic DMA mapping stuff.
 * SuperH has everything mapped statically like x86.
 */

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

/* pci_unmap_{single,page} being a nop depends upon the
 * configuration.
 */
#ifdef CONFIG_SH_PCIDMA_NONCOHERENT
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
#else
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)
#define pci_unmap_addr(PTR, ADDR_NAME)		(0)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	do { } while (0)
#define pci_unmap_len(PTR, LEN_NAME)		(0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	do { } while (0)
#endif

/* Not supporting more than 32-bit PCI bus addresses now, but
 * must satisfy references to this function.  Change if needed.
 */
#define pci_dac_dma_supported(pci_dev, mask) (0)

/* These macros should be used after a pci_map_sg call has been done
 * to get bus addresses of each of the SG entries and their lengths.
 * You should only work with the number of sg entries pci_map_sg
 * returns, or alternatively stop on the first sg_dma_len(sg) which
 * is 0.
 */
#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)		((sg)->length)

/* Board-specific fixup routines. */
extern void pcibios_fixup(void);
extern void pcibios_fixup_irqs(void);

#ifdef CONFIG_PCI_AUTO
extern int pciauto_assign_resources(int busno, struct pci_channel *hose);
#endif

static inline void pcibios_add_platform_entries(struct pci_dev *dev)
{
}

#endif /* __KERNEL__ */

/* generic pci stuff */
#include <asm-generic/pci.h>

/* generic DMA-mapping stuff */
#include <asm-generic/pci-dma-compat.h>

#endif /* __ASM_SH64_PCI_H */

