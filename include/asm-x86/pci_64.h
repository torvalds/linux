#ifndef __x8664_PCI_H
#define __x8664_PCI_H


#ifdef __KERNEL__


#ifdef CONFIG_CALGARY_IOMMU
static inline void* pci_iommu(struct pci_bus *bus)
{
	struct pci_sysdata *sd = bus->sysdata;
	return sd->iommu;
}

static inline void set_pci_iommu(struct pci_bus *bus, void *val)
{
	struct pci_sysdata *sd = bus->sysdata;
	sd->iommu = val;
}
#endif /* CONFIG_CALGARY_IOMMU */


extern int (*pci_config_read)(int seg, int bus, int dev, int fn, int reg, int len, u32 *value);
extern int (*pci_config_write)(int seg, int bus, int dev, int fn, int reg, int len, u32 value);



extern void pci_iommu_alloc(void);
extern int iommu_setup(char *opt);

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions
 *
 * On AMD64 it mostly equals, but we set it to zero if a hardware
 * IOMMU (gart) of sotware IOMMU (swiotlb) is available.
 */
#define PCI_DMA_BUS_IS_PHYS (dma_ops->is_phys)

#if defined(CONFIG_IOMMU) || defined(CONFIG_CALGARY_IOMMU)

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
/* No IOMMU */

#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)
#define pci_unmap_addr(PTR, ADDR_NAME)		(0)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	do { } while (0)
#define pci_unmap_len(PTR, LEN_NAME)		(0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	do { } while (0)

#endif

#endif /* __KERNEL__ */


#endif /* __x8664_PCI_H */
