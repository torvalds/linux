/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MEMORY_MODEL_H
#define __ASM_MEMORY_MODEL_H

#include <linux/pfn.h>

#ifndef __ASSEMBLY__

/*
 * supports 3 memory models.
 */
#if defined(CONFIG_FLATMEM)

#ifndef ARCH_PFN_OFFSET
#define ARCH_PFN_OFFSET		(0UL)
#endif

#define __pfn_to_page(pfn)	(mem_map + ((pfn) - ARCH_PFN_OFFSET))
#define __page_to_pfn(page)	((unsigned long)((page) - mem_map) + \
				 ARCH_PFN_OFFSET)

/* avoid <linux/mm.h> include hell */
extern unsigned long max_mapnr;

#ifndef pfn_valid
static inline int pfn_valid(unsigned long pfn)
{
	unsigned long pfn_offset = ARCH_PFN_OFFSET;

	return pfn >= pfn_offset && (pfn - pfn_offset) < max_mapnr;
}
#define pfn_valid pfn_valid
#endif

#elif defined(CONFIG_SPARSEMEM_VMEMMAP)

/* memmap is virtually contiguous.  */
#define __pfn_to_page(pfn)	(vmemmap + (pfn))
#define __page_to_pfn(page)	(unsigned long)((page) - vmemmap)

#elif defined(CONFIG_SPARSEMEM)
/*
 * Note: section's mem_map is encoded to reflect its start_pfn.
 * section[i].section_mem_map == mem_map's address - start_pfn;
 */
#define __page_to_pfn(pg)					\
({	const struct page *__pg = (pg);				\
	int __sec = page_to_section(__pg);			\
	(unsigned long)(__pg - __section_mem_map_addr(__nr_to_section(__sec)));	\
})

#define __pfn_to_page(pfn)				\
({	unsigned long __pfn = (pfn);			\
	struct mem_section *__sec = __pfn_to_section(__pfn);	\
	__section_mem_map_addr(__sec) + __pfn;		\
})
#endif /* CONFIG_FLATMEM/SPARSEMEM */

/*
 * Convert a physical address to a Page Frame Number and back
 */
#define	__phys_to_pfn(paddr)	PHYS_PFN(paddr)
#define	__pfn_to_phys(pfn)	PFN_PHYS(pfn)

#define page_to_pfn __page_to_pfn
#define pfn_to_page __pfn_to_page

#ifdef CONFIG_DEBUG_VIRTUAL
#define page_to_phys(page)						\
({									\
	unsigned long __pfn = page_to_pfn(page);			\
									\
	WARN_ON_ONCE(!pfn_valid(__pfn));				\
	PFN_PHYS(__pfn);						\
})
#else
#define page_to_phys(page)	PFN_PHYS(page_to_pfn(page))
#endif /* CONFIG_DEBUG_VIRTUAL */
#define phys_to_page(phys)	pfn_to_page(PHYS_PFN(phys))

#endif /* __ASSEMBLY__ */

#endif
