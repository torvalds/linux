#ifndef AGP_H
#define AGP_H 1

#include <asm/cacheflush.h>

/* 
 * Functions to keep the agpgart mappings coherent.
 * The GART gives the CPU a physical alias of memory. The alias is
 * mapped uncacheable. Make sure there are no conflicting mappings
 * with different cachability attributes for the same page.
 */

int map_page_into_agp(struct page *page);
int unmap_page_from_agp(struct page *page);
#define flush_agp_mappings() global_flush_tlb()

/* Could use CLFLUSH here if the cpu supports it. But then it would
   need to be called for each cacheline of the whole page so it may not be 
   worth it. Would need a page for it. */
#define flush_agp_cache() asm volatile("wbinvd":::"memory")

/* Convert a physical address to an address suitable for the GART. */
#define phys_to_gart(x) (x)
#define gart_to_phys(x) (x)

/* GATT allocation. Returns/accepts GATT kernel virtual address. */
#define alloc_gatt_pages(order)		\
	((char *)__get_free_pages(GFP_KERNEL, (order)))
#define free_gatt_pages(table, order)	\
	free_pages((unsigned long)(table), (order))

#endif
