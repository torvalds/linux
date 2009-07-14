#ifndef __ASM_GENERIC_SCATTERLIST_H
#define __ASM_GENERIC_SCATTERLIST_H

#include <linux/types.h>

struct scatterlist {
#ifdef CONFIG_DEBUG_SG
	unsigned long	sg_magic;
#endif
	unsigned long	page_link;
	unsigned int	offset;
	unsigned int	length;
	dma_addr_t	dma_address;
	unsigned int	dma_length;
};

/*
 * These macros should be used after a dma_map_sg call has been done
 * to get bus addresses of each of the SG entries and their lengths.
 * You should only work with the number of sg entries pci_map_sg
 * returns, or alternatively stop on the first sg_dma_len(sg) which
 * is 0.
 */
#define sg_dma_address(sg)	((sg)->dma_address)
#ifndef sg_dma_len
/*
 * Normally, you have an iommu on 64 bit machines, but not on 32 bit
 * machines. Architectures that are differnt should override this.
 */
#if __BITS_PER_LONG == 64
#define sg_dma_len(sg)		((sg)->dma_length)
#else
#define sg_dma_len(sg)		((sg)->length)
#endif /* 64 bit */
#endif /* sg_dma_len */

#ifndef ISA_DMA_THRESHOLD
#define ISA_DMA_THRESHOLD	(~0UL)
#endif

#define ARCH_HAS_SG_CHAIN

#endif /* __ASM_GENERIC_SCATTERLIST_H */
