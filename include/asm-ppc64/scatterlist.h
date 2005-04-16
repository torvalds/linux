#ifndef _PPC64_SCATTERLIST_H
#define _PPC64_SCATTERLIST_H

/*
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <asm/dma.h>

struct scatterlist {
	struct page *page;
	unsigned int offset;
	unsigned int length;

	/* For TCE support */
	u32 dma_address;
	u32 dma_length;
};

#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)		((sg)->dma_length)

#define ISA_DMA_THRESHOLD	(~0UL)

#endif /* !(_PPC64_SCATTERLIST_H) */
