#ifndef __ASM_SH_PCI_H
#define __ASM_SH_PCI_H

#ifdef __KERNEL__

#include <linux/dma-mapping.h>

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

#define pcibios_assign_all_busses()	1
#define pcibios_scan_all_fns(a, b)	0

/*
 * A board can define one or more PCI channels that represent built-in (or
 * external) PCI controllers.
 */
struct pci_channel {
	struct pci_ops *pci_ops;
	struct resource *io_resource;
	struct resource *mem_resource;
	int first_devfn;
	int last_devfn;
};

/*
 * Each board initializes this array and terminates it with a NULL entry.
 */
extern struct pci_channel board_pci_channels[];

#define PCIBIOS_MIN_IO		board_pci_channels->io_resource->start
#define PCIBIOS_MIN_MEM		board_pci_channels->mem_resource->start

/*
 * I/O routine helpers
 */
#if defined(CONFIG_CPU_SUBTYPE_SH7780) || defined(CONFIG_CPU_SUBTYPE_SH7785)
#define PCI_IO_AREA		0xFE400000
#define PCI_IO_SIZE		0x00400000
#else
#define PCI_IO_AREA		0xFE240000
#define PCI_IO_SIZE		0X00040000
#endif

#define PCI_MEM_SIZE		0x01000000

#define SH4_PCIIOBR_MASK	0xFFFC0000
#define pci_ioaddr(addr)	(PCI_IO_AREA + (addr & ~SH4_PCIIOBR_MASK))

#if defined(CONFIG_PCI)
#define is_pci_ioaddr(port)		\
	(((port) >= PCIBIOS_MIN_IO) &&	\
	 ((port) < (PCIBIOS_MIN_IO + PCI_IO_SIZE)))
#define is_pci_memaddr(port)		\
	(((port) >= PCIBIOS_MIN_MEM) &&	\
	 ((port) < (PCIBIOS_MIN_MEM + PCI_MEM_SIZE)))
#else
#define is_pci_ioaddr(port)	(0)
#define is_pci_memaddr(port)	(0)
#endif

struct pci_dev;

extern void pcibios_set_master(struct pci_dev *dev);

static inline void pcibios_penalize_isa_irq(int irq, int active)
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

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}
#endif

/* Board-specific fixup routines. */
void pcibios_fixup(void);
int pcibios_init_platform(void);
int pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin);

#ifdef CONFIG_PCI_AUTO
int pciauto_assign_resources(int busno, struct pci_channel *hose);
#endif

static inline void pcibios_add_platform_entries(struct pci_dev *dev)
{
}

#endif /* __KERNEL__ */

/* generic pci stuff */
#include <asm-generic/pci.h>

/* generic DMA-mapping stuff */
#include <asm-generic/pci-dma-compat.h>

#endif /* __ASM_SH_PCI_H */

