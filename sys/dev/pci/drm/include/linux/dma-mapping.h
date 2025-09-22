/* Public domain. */

#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#include <linux/sizes.h>
#include <linux/scatterlist.h>
#include <linux/dma-direction.h>

struct device;

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : (1ULL<<(n)) -1)

#define DMA_MAPPING_ERROR (dma_addr_t)-1

static inline int
dma_set_coherent_mask(struct device *dev, uint64_t m)
{
	return 0;
}

static inline int
dma_set_max_seg_size(struct device *dev, unsigned int sz)
{
	return 0;
}

static inline int
dma_set_mask(struct device *dev, uint64_t m)
{
	return 0;
}

static inline int
dma_set_mask_and_coherent(void *dev, uint64_t m)
{
	return 0;
}

static inline bool
dma_addressing_limited(void *dev)
{
	return false;
}

static inline dma_addr_t
dma_map_page(void *dev, struct vm_page *page, size_t offset,
    size_t size, enum dma_data_direction dir)
{
	return VM_PAGE_TO_PHYS(page);
}

static inline void
dma_unmap_page(void *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir)
{
}

static inline int
dma_mapping_error(void *dev, dma_addr_t addr)
{
	return 0;
}

void *dma_alloc_coherent(struct device *, size_t, dma_addr_t *, int);
void dma_free_coherent(struct device *, size_t, void *, dma_addr_t);

static inline void *
dmam_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dva, int gfp)
{
	return dma_alloc_coherent(dev, size, dva, gfp);
}

int	dma_get_sgtable(struct device *, struct sg_table *, void *,
	    dma_addr_t, size_t);
int	dma_map_sgtable(struct device *, struct sg_table *,
	    enum dma_data_direction, u_long);
void	dma_unmap_sgtable(struct device *, struct sg_table *,
	    enum dma_data_direction, u_long);

dma_addr_t dma_map_resource(struct device *, phys_addr_t, size_t,
    enum dma_data_direction, u_long);

#endif
