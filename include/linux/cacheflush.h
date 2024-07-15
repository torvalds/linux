/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CACHEFLUSH_H
#define _LINUX_CACHEFLUSH_H

#include <asm/cacheflush.h>

struct folio;

#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
#ifndef flush_dcache_folio
void flush_dcache_folio(struct folio *folio);
#endif
#else
static inline void flush_dcache_folio(struct folio *folio)
{
}
#define flush_dcache_folio flush_dcache_folio
#endif /* ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE */

#ifndef flush_icache_pages
static inline void flush_icache_pages(struct vm_area_struct *vma,
				     struct page *page, unsigned int nr)
{
}
#endif

#define flush_icache_page(vma, page)	flush_icache_pages(vma, page, 1)

#endif /* _LINUX_CACHEFLUSH_H */
