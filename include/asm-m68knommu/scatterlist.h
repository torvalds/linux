#ifndef _M68KNOMMU_SCATTERLIST_H
#define _M68KNOMMU_SCATTERLIST_H

struct scatterlist {
	struct page	*page;
	unsigned int	offset;
	dma_addr_t	dma_address;
	unsigned int	length;
};

#define ISA_DMA_THRESHOLD	(0xffffffff)

#endif /* !(_M68KNOMMU_SCATTERLIST_H) */
