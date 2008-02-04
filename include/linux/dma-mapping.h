#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#include <linux/device.h>
#include <linux/err.h>

/* These definitions mirror those in pci.h, so they can be used
 * interchangeably with their PCI_ counterparts */
enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))

/*
 * NOTE: do not use the below macros in new code and do not add new definitions
 * here.
 *
 * Instead, just open-code DMA_BIT_MASK(n) within your driver
 */
#define DMA_64BIT_MASK	DMA_BIT_MASK(64)
#define DMA_48BIT_MASK	DMA_BIT_MASK(48)
#define DMA_47BIT_MASK	DMA_BIT_MASK(47)
#define DMA_40BIT_MASK	DMA_BIT_MASK(40)
#define DMA_39BIT_MASK	DMA_BIT_MASK(39)
#define DMA_35BIT_MASK	DMA_BIT_MASK(35)
#define DMA_32BIT_MASK	DMA_BIT_MASK(32)
#define DMA_31BIT_MASK	DMA_BIT_MASK(31)
#define DMA_30BIT_MASK	DMA_BIT_MASK(30)
#define DMA_29BIT_MASK	DMA_BIT_MASK(29)
#define DMA_28BIT_MASK	DMA_BIT_MASK(28)
#define DMA_24BIT_MASK	DMA_BIT_MASK(24)

#define DMA_MASK_NONE	0x0ULL

static inline int valid_dma_direction(int dma_direction)
{
	return ((dma_direction == DMA_BIDIRECTIONAL) ||
		(dma_direction == DMA_TO_DEVICE) ||
		(dma_direction == DMA_FROM_DEVICE));
}

static inline int is_device_dma_capable(struct device *dev)
{
	return dev->dma_mask != NULL && *dev->dma_mask != DMA_MASK_NONE;
}

#ifdef CONFIG_HAS_DMA
#include <asm/dma-mapping.h>
#else
#include <asm-generic/dma-mapping-broken.h>
#endif

/* Backwards compat, remove in 2.7.x */
#define dma_sync_single		dma_sync_single_for_cpu
#define dma_sync_sg		dma_sync_sg_for_cpu

extern u64 dma_get_required_mask(struct device *dev);

/* flags for the coherent memory api */
#define	DMA_MEMORY_MAP			0x01
#define DMA_MEMORY_IO			0x02
#define DMA_MEMORY_INCLUDES_CHILDREN	0x04
#define DMA_MEMORY_EXCLUSIVE		0x08

#ifndef ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY
static inline int
dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
			    dma_addr_t device_addr, size_t size, int flags)
{
	return 0;
}

static inline void
dma_release_declared_memory(struct device *dev)
{
}

static inline void *
dma_mark_declared_memory_occupied(struct device *dev,
				  dma_addr_t device_addr, size_t size)
{
	return ERR_PTR(-EBUSY);
}
#endif

/*
 * Managed DMA API
 */
extern void *dmam_alloc_coherent(struct device *dev, size_t size,
				 dma_addr_t *dma_handle, gfp_t gfp);
extern void dmam_free_coherent(struct device *dev, size_t size, void *vaddr,
			       dma_addr_t dma_handle);
extern void *dmam_alloc_noncoherent(struct device *dev, size_t size,
				    dma_addr_t *dma_handle, gfp_t gfp);
extern void dmam_free_noncoherent(struct device *dev, size_t size, void *vaddr,
				  dma_addr_t dma_handle);
#ifdef ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY
extern int dmam_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
					dma_addr_t device_addr, size_t size,
					int flags);
extern void dmam_release_declared_memory(struct device *dev);
#else /* ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY */
static inline int dmam_declare_coherent_memory(struct device *dev,
				dma_addr_t bus_addr, dma_addr_t device_addr,
				size_t size, gfp_t gfp)
{
	return 0;
}

static inline void dmam_release_declared_memory(struct device *dev)
{
}
#endif /* ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY */

#endif
