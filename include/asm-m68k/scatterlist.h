#ifndef _M68K_SCATTERLIST_H
#define _M68K_SCATTERLIST_H

#include <linux/types.h>

struct scatterlist {
	struct page *page;
	unsigned int offset;
	unsigned int length;

	__u32 dma_address;	/* A place to hang host-specific addresses at. */
};

/* This is bogus and should go away. */
#define ISA_DMA_THRESHOLD (0x00ffffff)

#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)		((sg)->length)

#endif /* !(_M68K_SCATTERLIST_H) */
