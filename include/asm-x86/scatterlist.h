#ifndef _ASM_X86_SCATTERLIST_H
#define _ASM_X86_SCATTERLIST_H

#include <asm/types.h>

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

#define ARCH_HAS_SG_CHAIN
#define ISA_DMA_THRESHOLD (0x00ffffff)

/*
 * These macros should be used after a pci_map_sg call has been done
 * to get bus addresses of each of the SG entries and their lengths.
 * You should only work with the number of sg entries pci_map_sg
 * returns.
 */
#define sg_dma_address(sg)	((sg)->dma_address)
#ifdef CONFIG_X86_32
# define sg_dma_len(sg)		((sg)->length)
#else
# define sg_dma_len(sg)		((sg)->dma_length)
#endif

#endif
