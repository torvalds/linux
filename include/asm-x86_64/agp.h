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

#endif
