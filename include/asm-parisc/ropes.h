#ifndef _ASM_PARISC_ROPES_H_
#define _ASM_PARISC_ROPES_H_

#ifdef CONFIG_64BIT
/* "low end" PA8800 machines use ZX1 chipset: PAT PDC and only run 64-bit */
#define ZX1_SUPPORT
#endif

#ifdef CONFIG_PROC_FS
/* depends on proc fs support. But costs CPU performance */
#undef SBA_COLLECT_STATS
#endif

/*
** The number of pdir entries to "free" before issueing
** a read to PCOM register to flush out PCOM writes.
** Interacts with allocation granularity (ie 4 or 8 entries
** allocated and free'd/purged at a time might make this
** less interesting).
*/
#define DELAYED_RESOURCE_CNT	16

#define MAX_IOC		2	/* per Ike. Pluto/Astro only have 1. */
#define ROPES_PER_IOC	8	/* per Ike half or Pluto/Astro */

struct ioc {
	void __iomem	*ioc_hpa;	/* I/O MMU base address */
	char		*res_map;	/* resource map, bit == pdir entry */
	u64		*pdir_base;	/* physical base address */
	unsigned long	ibase;		/* pdir IOV Space base - shared w/lba_pci */
	unsigned long	imask;		/* pdir IOV Space mask - shared w/lba_pci */
#ifdef ZX1_SUPPORT
	unsigned long	iovp_mask;	/* help convert IOVA to IOVP */
#endif
	unsigned long	*res_hint;	/* next avail IOVP - circular search */
	spinlock_t	res_lock;
	unsigned int	res_bitshift;	/* from the LEFT! */
	unsigned int	res_size;	/* size of resource map in bytes */
#ifdef SBA_HINT_SUPPORT
/* FIXME : DMA HINTs not used */
	unsigned long	hint_mask_pdir; /* bits used for DMA hints */
	unsigned int	hint_shift_pdir;
#endif
#if DELAYED_RESOURCE_CNT > 0
	int		saved_cnt;
	struct sba_dma_pair {
			dma_addr_t	iova;
			size_t		size;
        } saved[DELAYED_RESOURCE_CNT];
#endif

#ifdef SBA_COLLECT_STATS
#define SBA_SEARCH_SAMPLE	0x100
	unsigned long	avg_search[SBA_SEARCH_SAMPLE];
	unsigned long	avg_idx;	/* current index into avg_search */
	unsigned long	used_pages;
	unsigned long	msingle_calls;
	unsigned long	msingle_pages;
	unsigned long	msg_calls;
	unsigned long	msg_pages;
	unsigned long	usingle_calls;
	unsigned long	usingle_pages;
	unsigned long	usg_calls;
	unsigned long	usg_pages;
#endif
        /* STUFF We don't need in performance path */
	unsigned int	pdir_size;	/* in bytes, determined by IOV Space size */
};

struct sba_device {
	struct sba_device	*next;  /* list of SBA's in system */
	struct parisc_device	*dev;   /* dev found in bus walk */
	const char		*name;
	void __iomem		*sba_hpa; /* base address */
	spinlock_t		sba_lock;
	unsigned int		flags;  /* state/functionality enabled */
	unsigned int		hw_rev;  /* HW revision of chip */

	struct resource		chip_resv; /* MMIO reserved for chip */
	struct resource		iommu_resv; /* MMIO reserved for iommu */

	unsigned int		num_ioc;  /* number of on-board IOC's */
	struct ioc		ioc[MAX_IOC];
};

#define ASTRO_RUNWAY_PORT	0x582
#define IKE_MERCED_PORT		0x803
#define REO_MERCED_PORT		0x804
#define REOG_MERCED_PORT	0x805
#define PLUTO_MCKINLEY_PORT	0x880

static inline int IS_ASTRO(struct parisc_device *d) {
	return d->id.hversion == ASTRO_RUNWAY_PORT;
}

static inline int IS_IKE(struct parisc_device *d) {
	return d->id.hversion == IKE_MERCED_PORT;
}

static inline int IS_PLUTO(struct parisc_device *d) {
	return d->id.hversion == PLUTO_MCKINLEY_PORT;
}

#define SBA_IOMMU_COOKIE	0x0000badbadc0ffeeUL

/*
** lba_device: Per instance Elroy data structure
*/
struct lba_device {
	struct pci_hba_data	hba;

	spinlock_t		lba_lock;
	void			*iosapic_obj;

#ifdef CONFIG_64BIT
	void __iomem		*iop_base;	/* PA_VIEW - for IO port accessor funcs */
#endif

	int			flags;		/* state/functionality enabled */
	int			hw_rev;		/* HW revision of chip */
};

#define ELROY_HVERS		0x782
#define MERCURY_HVERS		0x783
#define QUICKSILVER_HVERS	0x784

static inline int IS_ELROY(struct parisc_device *d) {
	return (d->id.hversion == ELROY_HVERS);
}

static inline int IS_MERCURY(struct parisc_device *d) {
	return (d->id.hversion == MERCURY_HVERS);
}

static inline int IS_QUICKSILVER(struct parisc_device *d) {
	return (d->id.hversion == QUICKSILVER_HVERS);
}

/*
** I/O SAPIC init function
** Caller knows where an I/O SAPIC is. LBA has an integrated I/O SAPIC.
** Call setup as part of per instance initialization.
** (ie *not* init_module() function unless only one is present.)
** fixup_irq is to initialize PCI IRQ line support and
** virtualize pcidev->irq value. To be called by pci_fixup_bus().
*/
extern void *iosapic_register(unsigned long hpa);
extern int iosapic_fixup_irq(void *obj, struct pci_dev *pcidev);

#endif /*_ASM_PARISC_ROPES_H_*/
