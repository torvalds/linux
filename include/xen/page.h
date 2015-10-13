#ifndef _XEN_PAGE_H
#define _XEN_PAGE_H

#include <asm/xen/page.h>

static inline unsigned long xen_page_to_gfn(struct page *page)
{
	return pfn_to_gfn(page_to_pfn(page));
}

struct xen_memory_region {
	unsigned long start_pfn;
	unsigned long n_pfns;
};

#define XEN_EXTRA_MEM_MAX_REGIONS 128 /* == E820MAX */

extern __initdata
struct xen_memory_region xen_extra_mem[XEN_EXTRA_MEM_MAX_REGIONS];

extern unsigned long xen_released_pages;

#endif	/* _XEN_PAGE_H */
