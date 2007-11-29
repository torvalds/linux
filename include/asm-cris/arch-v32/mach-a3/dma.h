#ifndef _ASM_ARCH_CRIS_DMA_H
#define _ASM_ARCH_CRIS_DMA_H

/* Defines for using and allocating dma channels. */

#define MAX_DMA_CHANNELS	12 /* 8 and 10 not used. */

enum dma_owner {
	dma_eth,
	dma_ser0,
	dma_ser1,
	dma_ser2,
	dma_ser3,
	dma_ser4,
	dma_iop,
	dma_sser,
	dma_strp,
	dma_h264,
	dma_jpeg
};

int crisv32_request_dma(unsigned int dmanr, const char *device_id,
	unsigned options, unsigned bandwidth, enum dma_owner owner);
void crisv32_free_dma(unsigned int dmanr);

/* Masks used by crisv32_request_dma options: */
#define DMA_VERBOSE_ON_ERROR 1
#define DMA_PANIC_ON_ERROR (2|DMA_VERBOSE_ON_ERROR)
#define DMA_INT_MEM 4

#endif /* _ASM_ARCH_CRIS_DMA_H */
