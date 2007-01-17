#ifndef _M68KNOMMU_DMA_MAPPING_H
#define _M68KNOMMU_DMA_MAPPING_H

#ifdef CONFIG_PCI
#include <asm-generic/dma-mapping.h>
#else
#include <asm-generic/dma-mapping-broken.h>
#endif

#endif  /* _M68KNOMMU_DMA_MAPPING_H */
