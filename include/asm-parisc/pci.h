#ifndef __ASM_PARISC_PCI_H
#define __ASM_PARISC_PCI_H

#include <linux/config.h>
#include <asm/scatterlist.h>



/*
** HP PCI platforms generally support multiple bus adapters.
**    (workstations 1-~4, servers 2-~32)
**
** Newer platforms number the busses across PCI bus adapters *sparsely*.
** E.g. 0, 8, 16, ...
**
** Under a PCI bus, most HP platforms support PPBs up to two or three
** levels deep. See "Bit3" product line. 
*/
#define PCI_MAX_BUSSES	256

/*
** pci_hba_data (aka H2P_OBJECT in HP/UX)
**
** This is the "common" or "base" data structure which HBA drivers
** (eg Dino or LBA) are required to place at the top of their own
** platform_data structure.  I've heard this called "C inheritance" too.
**
** Data needed by pcibios layer belongs here.
*/
struct pci_hba_data {
	void __iomem   *base_addr;	/* aka Host Physical Address */
	const struct parisc_device *dev; /* device from PA bus walk */
	struct pci_bus *hba_bus;	/* primary PCI bus below HBA */
	int		hba_num;	/* I/O port space access "key" */
	struct resource bus_num;	/* PCI bus numbers */
	struct resource io_space;	/* PIOP */
	struct resource lmmio_space;	/* bus addresses < 4Gb */
	struct resource elmmio_space;	/* additional bus addresses < 4Gb */
	struct resource gmmio_space;	/* bus addresses > 4Gb */

	/* NOTE: Dino code assumes it can use *all* of the lmmio_space,
	 * elmmio_space and gmmio_space as a contiguous array of
	 * resources.  This #define represents the array size */
	#define DINO_MAX_LMMIO_RESOURCES	3

	unsigned long   lmmio_space_offset;  /* CPU view - PCI view */
	void *          iommu;          /* IOMMU this device is under */
	/* REVISIT - spinlock to protect resources? */

	#define HBA_NAME_SIZE 16
	char io_name[HBA_NAME_SIZE];
	char lmmio_name[HBA_NAME_SIZE];
	char elmmio_name[HBA_NAME_SIZE];
	char gmmio_name[HBA_NAME_SIZE];
};

#define HBA_DATA(d)		((struct pci_hba_data *) (d))

/* 
** We support 2^16 I/O ports per HBA.  These are set up in the form
** 0xbbxxxx, where bb is the bus number and xxxx is the I/O port
** space address.
*/
#define HBA_PORT_SPACE_BITS	16

#define HBA_PORT_BASE(h)	((h) << HBA_PORT_SPACE_BITS)
#define HBA_PORT_SPACE_SIZE	(1UL << HBA_PORT_SPACE_BITS)

#define PCI_PORT_HBA(a)		((a) >> HBA_PORT_SPACE_BITS)
#define PCI_PORT_ADDR(a)	((a) & (HBA_PORT_SPACE_SIZE - 1))

#ifdef CONFIG_64BIT
#define PCI_F_EXTEND		0xffffffff00000000UL
#define PCI_IS_LMMIO(hba,a)	pci_is_lmmio(hba,a)

/* We need to know if an address is LMMMIO or GMMIO.
 * LMMIO requires mangling and GMMIO we must use as-is.
 */
static __inline__  int pci_is_lmmio(struct pci_hba_data *hba, unsigned long a)
{
	return(((a) & PCI_F_EXTEND) == PCI_F_EXTEND);
}

/*
** Convert between PCI (IO_VIEW) addresses and processor (PA_VIEW) addresses.
** See pcibios.c for more conversions used by Generic PCI code.
**
** Platform characteristics/firmware guarantee that
**	(1) PA_VIEW - IO_VIEW = lmmio_offset for both LMMIO and ELMMIO
**	(2) PA_VIEW == IO_VIEW for GMMIO
*/
#define PCI_BUS_ADDR(hba,a)	(PCI_IS_LMMIO(hba,a)	\
		?  ((a) - hba->lmmio_space_offset)	/* mangle LMMIO */ \
		: (a))					/* GMMIO */
#define PCI_HOST_ADDR(hba,a)	(((a) & PCI_F_EXTEND) == 0 \
		? (a) + hba->lmmio_space_offset \
		: (a))

#else	/* !CONFIG_64BIT */

#define PCI_BUS_ADDR(hba,a)	(a)
#define PCI_HOST_ADDR(hba,a)	(a)
#define PCI_F_EXTEND		0UL
#define PCI_IS_LMMIO(hba,a)	(1)	/* 32-bit doesn't support GMMIO */

#endif /* !CONFIG_64BIT */

/*
** KLUGE: linux/pci.h include asm/pci.h BEFORE declaring struct pci_bus
** (This eliminates some of the warnings).
*/
struct pci_bus;
struct pci_dev;

/*
 * If the PCI device's view of memory is the same as the CPU's view of memory,
 * PCI_DMA_BUS_IS_PHYS is true.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#ifdef CONFIG_PA20
/* All PA-2.0 machines have an IOMMU. */
#define PCI_DMA_BUS_IS_PHYS	0
#define parisc_has_iommu()	do { } while (0)
#else

#if defined(CONFIG_IOMMU_CCIO) || defined(CONFIG_IOMMU_SBA)
extern int parisc_bus_is_phys; 	/* in arch/parisc/kernel/setup.c */
#define PCI_DMA_BUS_IS_PHYS	parisc_bus_is_phys
#define parisc_has_iommu()	do { parisc_bus_is_phys = 0; } while (0)
#else
#define PCI_DMA_BUS_IS_PHYS	1
#define parisc_has_iommu()	do { } while (0)
#endif

#endif	/* !CONFIG_PA20 */


/*
** Most PCI devices (eg Tulip, NCR720) also export the same registers
** to both MMIO and I/O port space.  Due to poor performance of I/O Port
** access under HP PCI bus adapters, strongly reccomend use of MMIO
** address space.
**
** While I'm at it more PA programming notes:
**
** 1) MMIO stores (writes) are posted operations. This means the processor
**    gets an "ACK" before the write actually gets to the device. A read
**    to the same device (or typically the bus adapter above it) will
**    force in-flight write transaction(s) out to the targeted device
**    before the read can complete.
**
** 2) The Programmed I/O (PIO) data may not always be strongly ordered with
**    respect to DMA on all platforms. Ie PIO data can reach the processor
**    before in-flight DMA reaches memory. Since most SMP PA platforms
**    are I/O coherent, it generally doesn't matter...but sometimes
**    it does.
**
** I've helped device driver writers debug both types of problems.
*/
struct pci_port_ops {
	  u8 (*inb)  (struct pci_hba_data *hba, u16 port);
	 u16 (*inw)  (struct pci_hba_data *hba, u16 port);
	 u32 (*inl)  (struct pci_hba_data *hba, u16 port);
	void (*outb) (struct pci_hba_data *hba, u16 port,  u8 data);
	void (*outw) (struct pci_hba_data *hba, u16 port, u16 data);
	void (*outl) (struct pci_hba_data *hba, u16 port, u32 data);
};


struct pci_bios_ops {
	void (*init)(void);
	void (*fixup_bus)(struct pci_bus *bus);
};

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

/*
** Stuff declared in arch/parisc/kernel/pci.c
*/
extern struct pci_port_ops *pci_port;
extern struct pci_bios_ops *pci_bios;
extern int pci_post_reset_delay;	/* delay after de-asserting #RESET */
extern int pci_hba_count;
extern struct pci_hba_data *parisc_pci_hba[];

#ifdef CONFIG_PCI
extern void pcibios_register_hba(struct pci_hba_data *);
extern void pcibios_set_master(struct pci_dev *);
#else
extern inline void pcibios_register_hba(struct pci_hba_data *x)
{
}
#endif

/*
 * pcibios_assign_all_busses() is used in drivers/pci/pci.c:pci_do_scan_bus()
 *   0 == check if bridge is numbered before re-numbering.
 *   1 == pci_do_scan_bus() should automatically number all PCI-PCI bridges.
 *
 *   We *should* set this to zero for "legacy" platforms and one
 *   for PAT platforms.
 *
 *   But legacy platforms also need to renumber the busses below a Host
 *   Bus controller.  Adding a 4-port Tulip card on the first PCI root
 *   bus of a C200 resulted in the secondary bus being numbered as 1.
 *   The second PCI host bus controller's root bus had already been
 *   assigned bus number 1 by firmware and sysfs complained.
 *
 *   Firmware isn't doing anything wrong here since each controller
 *   is its own PCI domain.  It's simpler and easier for us to renumber
 *   the busses rather than treat each Dino as a separate PCI domain.
 *   Eventually, we may want to introduce PCI domains for Superdome or
 *   rp7420/8420 boxes and then revisit this issue.
 */
#define pcibios_assign_all_busses()     (1)
#define pcibios_scan_all_fns(a, b)	(0)

#define PCIBIOS_MIN_IO          0x10
#define PCIBIOS_MIN_MEM         0x1000 /* NBPG - but pci/setup-res.c dies */

/* Don't support DAC yet. */
#define pci_dac_dma_supported(pci_dev, mask)   (0)

/* export the pci_ DMA API in terms of the dma_ one */
#include <asm-generic/pci-dma-compat.h>

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

static inline void pcibios_add_platform_entries(struct pci_dev *dev)
{
}

#endif /* __ASM_PARISC_PCI_H */
