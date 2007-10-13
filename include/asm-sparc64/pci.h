#ifndef __SPARC64_PCI_H
#define __SPARC64_PCI_H

#ifdef __KERNEL__

#include <linux/dma-mapping.h>

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader.
 */
#define pcibios_assign_all_busses()	0
#define pcibios_scan_all_fns(a, b)	0

#define PCIBIOS_MIN_IO		0UL
#define PCIBIOS_MIN_MEM		0UL

#define PCI_IRQ_NONE		0xffffffff

#define PCI_CACHE_LINE_BYTES	64

static inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

/* The PCI address space does not equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(0)

static inline void *pci_alloc_consistent(struct pci_dev *pdev, size_t size,
					 dma_addr_t *dma_handle)
{
	return dma_alloc_coherent(&pdev->dev, size, dma_handle, GFP_ATOMIC);
}

static inline void pci_free_consistent(struct pci_dev *pdev, size_t size,
				       void *vaddr, dma_addr_t dma_handle)
{
	return dma_free_coherent(&pdev->dev, size, vaddr, dma_handle);
}

static inline dma_addr_t pci_map_single(struct pci_dev *pdev, void *ptr,
					size_t size, int direction)
{
	return dma_map_single(&pdev->dev, ptr, size,
			      (enum dma_data_direction) direction);
}

static inline void pci_unmap_single(struct pci_dev *pdev, dma_addr_t dma_addr,
				    size_t size, int direction)
{
	dma_unmap_single(&pdev->dev, dma_addr, size,
			 (enum dma_data_direction) direction);
}

#define pci_map_page(dev, page, off, size, dir) \
	pci_map_single(dev, (page_address(page) + (off)), size, dir)
#define pci_unmap_page(dev,addr,sz,dir) \
	pci_unmap_single(dev,addr,sz,dir)

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

static inline int pci_map_sg(struct pci_dev *pdev, struct scatterlist *sg,
			     int nents, int direction)
{
	return dma_map_sg(&pdev->dev, sg, nents,
			  (enum dma_data_direction) direction);
}

static inline void pci_unmap_sg(struct pci_dev *pdev, struct scatterlist *sg,
				int nents, int direction)
{
	dma_unmap_sg(&pdev->dev, sg, nents,
		     (enum dma_data_direction) direction);
}

static inline void pci_dma_sync_single_for_cpu(struct pci_dev *pdev,
					       dma_addr_t dma_handle,
					       size_t size, int direction)
{
	dma_sync_single_for_cpu(&pdev->dev, dma_handle, size,
				(enum dma_data_direction) direction);
}

static inline void pci_dma_sync_single_for_device(struct pci_dev *pdev,
						  dma_addr_t dma_handle,
						  size_t size, int direction)
{
	/* No flushing needed to sync cpu writes to the device.  */
}

static inline void pci_dma_sync_sg_for_cpu(struct pci_dev *pdev,
					   struct scatterlist *sg,
					   int nents, int direction)
{
	dma_sync_sg_for_cpu(&pdev->dev, sg, nents,
			    (enum dma_data_direction) direction);
}

static inline void pci_dma_sync_sg_for_device(struct pci_dev *pdev,
					      struct scatterlist *sg,
					      int nelems, int direction)
{
	/* No flushing needed to sync cpu writes to the device.  */
}

/* Return whether the given PCI device DMA address mask can
 * be supported properly.  For example, if your device can
 * only drive the low 24-bits during PCI bus mastering, then
 * you would pass 0x00ffffff as the mask to this function.
 */
extern int pci_dma_supported(struct pci_dev *hwdev, u64 mask);

/* PCI IOMMU mapping bypass support. */

/* PCI 64-bit addressing works for all slots on all controller
 * types on sparc64.  However, it requires that the device
 * can drive enough of the 64 bits.
 */
#define PCI64_REQUIRED_MASK	(~(dma64_addr_t)0)
#define PCI64_ADDR_BASE		0xfffc000000000000UL

static inline int pci_dma_mapping_error(dma_addr_t dma_addr)
{
	return dma_mapping_error(dma_addr);
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

	*strat = PCI_DMA_BURST_BOUNDARY;
	*strategy_parameter = cacheline_size;
}
#endif

/* Return the index of the PCI controller for device PDEV. */

extern int pci_domain_nr(struct pci_bus *bus);
static inline int pci_proc_domain(struct pci_bus *bus)
{
	return 1;
}

/* Platform support for /proc/bus/pci/X/Y mmap()s. */

#define HAVE_PCI_MMAP
#define HAVE_ARCH_PCI_GET_UNMAPPED_AREA
#define get_pci_unmapped_area get_fb_unmapped_area

extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_state,
			       int write_combine);

extern void
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			struct resource *res);

extern void
pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			struct pci_bus_region *region);

extern struct resource *pcibios_select_root(struct pci_dev *, struct resource *);

static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return PCI_IRQ_NONE;
}

struct device_node;
extern struct device_node *pci_device_to_OF_node(struct pci_dev *pdev);

#endif /* __KERNEL__ */

#endif /* __SPARC64_PCI_H */
