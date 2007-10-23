/* $Id: scatterlist.h,v 1.8 2001/12/17 07:05:15 davem Exp $ */
#ifndef _SPARC_SCATTERLIST_H
#define _SPARC_SCATTERLIST_H

#include <linux/types.h>

struct scatterlist {
#ifdef CONFIG_DEBUG_SG
	unsigned long sg_magic;
#endif
	unsigned long page_link;
	unsigned int offset;

	unsigned int length;

	__u32 dvma_address; /* A place to hang host-specific addresses at. */
	__u32 dvma_length;
};

#define sg_dma_address(sg) ((sg)->dvma_address)
#define sg_dma_len(sg)     ((sg)->dvma_length)

#define ISA_DMA_THRESHOLD (~0UL)

#define ARCH_HAS_SG_CHAIN

#endif /* !(_SPARC_SCATTERLIST_H) */
