#ifndef _M68K_DMA_MAPPING_H
#define _M68K_DMA_MAPPING_H

#include <linux/config.h>

#ifdef CONFIG_PCI
#include <asm-generic/dma-mapping.h>
#else
#include <asm-generic/dma-mapping-broken.h>
#endif

#endif  /* _M68K_DMA_MAPPING_H */
