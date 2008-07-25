#ifndef ___ASM_SPARC_DMA_MAPPING_H
#define ___ASM_SPARC_DMA_MAPPING_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/dma-mapping_64.h>
#else
#include <asm-sparc/dma-mapping_32.h>
#endif
#endif
