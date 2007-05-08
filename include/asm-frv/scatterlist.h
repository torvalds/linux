#ifndef _ASM_SCATTERLIST_H
#define _ASM_SCATTERLIST_H

#include <asm/types.h>

/*
 * Drivers must set either ->address or (preferred) ->page and ->offset
 * to indicate where data must be transferred to/from.
 *
 * Using ->page is recommended since it handles highmem data as well as
 * low mem. ->address is restricted to data which has a virtual mapping, and
 * it will go away in the future. Updating to ->page can be automated very
 * easily -- something like
 *
 * sg->address = some_ptr;
 *
 * can be rewritten as
 *
 * sg->page = virt_to_page(some_ptr);
 * sg->offset = (unsigned long) some_ptr & ~PAGE_MASK;
 *
 * and that's it. There's no excuse for not highmem enabling YOUR driver. /jens
 */
struct scatterlist {
	struct page	*page;		/* Location for highmem page, if any */
	unsigned int	offset;		/* for highmem, page offset */

	dma_addr_t	dma_address;
	unsigned int	length;
};

#define ISA_DMA_THRESHOLD (0xffffffffUL)

#endif /* !_ASM_SCATTERLIST_H */
