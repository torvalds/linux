/* $Id: pbm.h,v 1.27 2001/08/12 13:18:23 davem Exp $
 * pbm.h: UltraSparc PCI controller software state.
 *
 * Copyright (C) 1997, 1998, 1999 David S. Miller (davem@redhat.com)
 */

#ifndef __SPARC64_PBM_H
#define __SPARC64_PBM_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/msi.h>

#include <asm/io.h>
#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/iommu.h>

/* The abstraction used here is that there are PCI controllers,
 * each with one (Sabre) or two (PSYCHO/SCHIZO) PCI bus modules
 * underneath.  Each PCI bus module uses an IOMMU (shared by both
 * PBMs of a controller, or per-PBM), and if a streaming buffer
 * is present, each PCI bus module has it's own. (ie. the IOMMU
 * might be shared between PBMs, the STC is never shared)
 * Furthermore, each PCI bus module controls it's own autonomous
 * PCI bus.
 */

struct pci_controller_info;

/* This contains the software state necessary to drive a PCI
 * controller's IOMMU.
 */
struct pci_iommu_arena {
	unsigned long	*map;
	unsigned int	hint;
	unsigned int	limit;
};

struct pci_iommu {
	/* This protects the controller's IOMMU and all
	 * streaming buffers underneath.
	 */
	spinlock_t	lock;

	struct pci_iommu_arena arena;

	/* IOMMU page table, a linear array of ioptes. */
	iopte_t		*page_table;		/* The page table itself. */

	/* Base PCI memory space address where IOMMU mappings
	 * begin.
	 */
	u32		page_table_map_base;

	/* IOMMU Controller Registers */
	unsigned long	iommu_control;		/* IOMMU control register */
	unsigned long	iommu_tsbbase;		/* IOMMU page table base register */
	unsigned long	iommu_flush;		/* IOMMU page flush register */
	unsigned long	iommu_ctxflush;		/* IOMMU context flush register */

	/* This is a register in the PCI controller, which if
	 * read will have no side-effects but will guarantee
	 * completion of all previous writes into IOMMU/STC.
	 */
	unsigned long	write_complete_reg;

	/* In order to deal with some buggy third-party PCI bridges that
	 * do wrong prefetching, we never mark valid mappings as invalid.
	 * Instead we point them at this dummy page.
	 */
	unsigned long	dummy_page;
	unsigned long	dummy_page_pa;

	/* CTX allocation. */
	unsigned long ctx_lowest_free;
	unsigned long ctx_bitmap[IOMMU_NUM_CTXS / (sizeof(unsigned long) * 8)];

	/* Here a PCI controller driver describes the areas of
	 * PCI memory space where DMA to/from physical memory
	 * are addressed.  Drivers interrogate the PCI layer
	 * if their device has addressing limitations.  They
	 * do so via pci_dma_supported, and pass in a mask of
	 * DMA address bits their device can actually drive.
	 *
	 * The test for being usable is:
	 * 	(device_mask & dma_addr_mask) == dma_addr_mask
	 */
	u32 dma_addr_mask;
};

extern void pci_iommu_table_init(struct pci_iommu *iommu, int tsbsize, u32 dma_offset, u32 dma_addr_mask);

/* This describes a PCI bus module's streaming buffer. */
struct pci_strbuf {
	int		strbuf_enabled;		/* Present and using it? */

	/* Streaming Buffer Control Registers */
	unsigned long	strbuf_control;		/* STC control register */
	unsigned long	strbuf_pflush;		/* STC page flush register */
	unsigned long	strbuf_fsync;		/* STC flush synchronization reg */
	unsigned long	strbuf_ctxflush;	/* STC context flush register */
	unsigned long	strbuf_ctxmatch_base;	/* STC context flush match reg */
	unsigned long	strbuf_flushflag_pa;	/* Physical address of flush flag */
	volatile unsigned long *strbuf_flushflag; /* The flush flag itself */

	/* And this is the actual flush flag area.
	 * We allocate extra because the chips require
	 * a 64-byte aligned area.
	 */
	volatile unsigned long	__flushflag_buf[(64 + (64 - 1)) / sizeof(long)];
};

#define PCI_STC_FLUSHFLAG_INIT(STC) \
	(*((STC)->strbuf_flushflag) = 0UL)
#define PCI_STC_FLUSHFLAG_SET(STC) \
	(*((STC)->strbuf_flushflag) != 0UL)

/* There can be quite a few ranges and interrupt maps on a PCI
 * segment.  Thus...
 */
#define PROM_PCIRNG_MAX		64
#define PROM_PCIIMAP_MAX	64

struct pci_pbm_info {
	/* PCI controller we sit under. */
	struct pci_controller_info	*parent;

	/* Physical address base of controller registers. */
	unsigned long			controller_regs;

	/* Physical address base of PBM registers. */
	unsigned long			pbm_regs;

	/* Physical address of DMA sync register, if any.  */
	unsigned long			sync_reg;

	/* Opaque 32-bit system bus Port ID. */
	u32				portid;

	/* Opaque 32-bit handle used for hypervisor calls.  */
	u32				devhandle;

	/* Chipset version information. */
	int				chip_type;
#define PBM_CHIP_TYPE_SABRE		1
#define PBM_CHIP_TYPE_PSYCHO		2
#define PBM_CHIP_TYPE_SCHIZO		3
#define PBM_CHIP_TYPE_SCHIZO_PLUS	4
#define PBM_CHIP_TYPE_TOMATILLO		5
	int				chip_version;
	int				chip_revision;

	/* Name used for top-level resources. */
	char				*name;

	/* OBP specific information. */
	struct device_node		*prom_node;
	u64				ino_bitmap;

	/* PBM I/O and Memory space resources. */
	struct resource			io_space;
	struct resource			mem_space;

	/* Base of PCI Config space, can be per-PBM or shared. */
	unsigned long			config_space;

	/* State of 66MHz capabilities on this PBM. */
	int				is_66mhz_capable;
	int				all_devs_66mhz;

#ifdef CONFIG_PCI_MSI
	/* MSI info.  */
	u32				msiq_num;
	u32				msiq_ent_count;
	u32				msiq_first;
	u32				msiq_first_devino;
	u32				msi_num;
	u32				msi_first;
	u32				msi_data_mask;
	u32				msix_data_width;
	u64				msi32_start;
	u64				msi64_start;
	u32				msi32_len;
	u32				msi64_len;
	void				*msi_queues;
	unsigned long			*msi_bitmap;
#endif /* !(CONFIG_PCI_MSI) */

	/* This PBM's streaming buffer. */
	struct pci_strbuf		stc;

	/* IOMMU state, potentially shared by both PBM segments. */
	struct pci_iommu		*iommu;

	/* Now things for the actual PCI bus probes. */
	unsigned int			pci_first_busno;
	unsigned int			pci_last_busno;
	struct pci_bus			*pci_bus;
};

struct pci_controller_info {
	/* List of all PCI controllers. */
	struct pci_controller_info	*next;

	/* Each controller gets a unique index, used mostly for
	 * error logging purposes.
	 */
	int				index;

	/* The PCI bus modules controlled by us. */
	struct pci_pbm_info		pbm_A;
	struct pci_pbm_info		pbm_B;

	/* Operations which are controller specific. */
	void (*scan_bus)(struct pci_controller_info *);

#ifdef CONFIG_PCI_MSI
	int (*setup_msi_irq)(unsigned int *virt_irq_p, struct pci_dev *pdev,
			     struct msi_desc *entry);
	void (*teardown_msi_irq)(unsigned int virt_irq, struct pci_dev *pdev);
#endif

	/* Now things for the actual PCI bus probes. */
	struct pci_ops			*pci_ops;
	unsigned int			pci_first_busno;
	unsigned int			pci_last_busno;
};

#endif /* !(__SPARC64_PBM_H) */
