#ifndef _ASM_LINUX_DMA_MAPPING_H
#define _ASM_LINUX_DMA_MAPPING_H

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

#define DMA_64BIT_MASK	0xffffffffffffffffULL
#define DMA_48BIT_MASK	0x0000ffffffffffffULL
#define DMA_40BIT_MASK	0x000000ffffffffffULL
#define DMA_39BIT_MASK	0x0000007fffffffffULL
#define DMA_32BIT_MASK	0x00000000ffffffffULL
#define DMA_31BIT_MASK	0x000000007fffffffULL
#define DMA_30BIT_MASK	0x000000003fffffffULL
#define DMA_29BIT_MASK	0x000000001fffffffULL
#define DMA_28BIT_MASK	0x000000000fffffffULL
#define DMA_24BIT_MASK	0x0000000000ffffffULL

static inline int valid_dma_direction(int dma_direction)
{
	return ((dma_direction == DMA_BIDIRECTIONAL) ||
		(dma_direction == DMA_TO_DEVICE) ||
		(dma_direction == DMA_FROM_DEVICE));
}

#include <asm/dma-mapping.h>

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
