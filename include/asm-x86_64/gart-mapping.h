#ifndef _X8664_GART_MAPPING_H
#define _X8664_GART_MAPPING_H 1

#include <linux/types.h>
#include <asm/types.h>

struct device;

extern void*
gart_alloc_coherent(struct device *dev, size_t size,
        dma_addr_t *dma_handle, gfp_t gfp);

extern int
gart_dma_supported(struct device *hwdev, u64 mask);

#endif /* _X8664_GART_MAPPING_H */
