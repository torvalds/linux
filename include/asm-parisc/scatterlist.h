#ifndef _ASM_PARISC_SCATTERLIST_H
#define _ASM_PARISC_SCATTERLIST_H

#include <asm/page.h>
#include <asm/types.h>

struct scatterlist {
#ifdef CONFIG_DEBUG_SG
	unsigned long sg_magic;
#endif
	unsigned long page_link;
	unsigned int offset;

	unsigned int length;

	/* an IOVA can be 64-bits on some PA-Risc platforms. */
	dma_addr_t iova;	/* I/O Virtual Address */
	__u32      iova_length; /* bytes mapped */
};

#define sg_virt_addr(sg) ((unsigned long)(page_address(sg->page) + sg->offset))
#define sg_dma_address(sg) ((sg)->iova)
#define sg_dma_len(sg)     ((sg)->iova_length)

#define ISA_DMA_THRESHOLD (~0UL)

#endif /* _ASM_PARISC_SCATTERLIST_H */
