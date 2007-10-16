/* $Id: scatterlist.h,v 1.11 2001/12/17 07:05:15 davem Exp $ */
#ifndef _SPARC64_SCATTERLIST_H
#define _SPARC64_SCATTERLIST_H

#include <asm/page.h>
#include <asm/types.h>

struct scatterlist {
	struct page	*page;
	unsigned int	offset;

	unsigned int	length;

	dma_addr_t	dma_address;
	__u32		dma_length;
};

#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)     	((sg)->dma_length)

#define ISA_DMA_THRESHOLD	(~0UL)

#define ARCH_HAS_SG_CHAIN

#endif /* !(_SPARC64_SCATTERLIST_H) */
