#ifdef __KERNEL__
#ifndef _PPC_SCATTERLIST_H
#define _PPC_SCATTERLIST_H

#include <asm/dma.h>

struct scatterlist {
	struct page	*page;
	unsigned int	offset;
	dma_addr_t	dma_address;
	unsigned int	length;
};

/*
 * These macros should be used after a pci_map_sg call has been done
 * to get bus addresses of each of the SG entries and their lengths.
 * You should only work with the number of sg entries pci_map_sg
 * returns, or alternatively stop on the first sg_dma_len(sg) which
 * is 0.
 */
#define sg_dma_address(sg)      ((sg)->dma_address)
#define sg_dma_len(sg)          ((sg)->length)

#endif /* !(_PPC_SCATTERLIST_H) */
#endif /* __KERNEL__ */
