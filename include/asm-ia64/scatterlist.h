#ifndef _ASM_IA64_SCATTERLIST_H
#define _ASM_IA64_SCATTERLIST_H

/*
 * Modified 1998-1999, 2001-2002, 2004
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */

struct scatterlist {
	struct page *page;
	unsigned int offset;
	unsigned int length;	/* buffer length */

	dma_addr_t dma_address;
	unsigned int dma_length;
};

/*
 * It used to be that ISA_DMA_THRESHOLD had something to do with the
 * DMA-limits of ISA-devices.  Nowadays, its only remaining use (apart
 * from the aha1542.c driver, which isn't 64-bit clean anyhow) is to
 * tell the block-layer (via BLK_BOUNCE_ISA) what the max. physical
 * address of a page is that is allocated with GFP_DMA.  On IA-64,
 * that's 4GB - 1.
 */
#define ISA_DMA_THRESHOLD	0xffffffff

#endif /* _ASM_IA64_SCATTERLIST_H */
