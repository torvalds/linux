#ifndef _M68KNOMMU_SCATTERLIST_H
#define _M68KNOMMU_SCATTERLIST_H

#include <linux/mm.h>

struct scatterlist {
	struct page	*page;
	unsigned int	offset;
	dma_addr_t	dma_address;
	unsigned int	length;
};

#define sg_address(sg)		(page_address((sg)->page) + (sg)->offset)
#define sg_dma_address(sg)      ((sg)->dma_address)
#define sg_dma_len(sg)          ((sg)->length)

#define ISA_DMA_THRESHOLD	(0xffffffff)

#endif /* !(_M68KNOMMU_SCATTERLIST_H) */
