#ifndef __ASM_CRIS_SCATTERLIST_H
#define __ASM_CRIS_SCATTERLIST_H

struct scatterlist {
	char *  address;    /* Location data is to be transferred to */
	unsigned int length;

	/* The following is i386 highmem junk - not used by us */
	struct page * page; /* Location for highmem page, if any */
	unsigned int offset;/* for highmem, page offset */

};

#define sg_dma_address(sg)	((sg)->address)
#define sg_dma_len(sg)		((sg)->length)
/* i386 junk */

#define ISA_DMA_THRESHOLD (0x1fffffff)

#endif /* !(__ASM_CRIS_SCATTERLIST_H) */
