#ifndef __V850_DMA_MAPPING_H__
#define __V850_DMA_MAPPING_H__


#ifdef CONFIG_PCI
#include <asm-generic/dma-mapping.h>
#else
#include <asm-generic/dma-mapping-broken.h>
#endif

#endif /* __V850_DMA_MAPPING_H__ */
