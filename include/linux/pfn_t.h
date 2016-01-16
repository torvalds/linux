#ifndef _LINUX_PFN_T_H_
#define _LINUX_PFN_T_H_
#include <linux/mm.h>

/*
 * PFN_FLAGS_MASK - mask of all the possible valid pfn_t flags
 * PFN_SG_CHAIN - pfn is a pointer to the next scatterlist entry
 * PFN_SG_LAST - pfn references a page and is the last scatterlist entry
 * PFN_DEV - pfn is not covered by system memmap by default
 * PFN_MAP - pfn has a dynamic page mapping established by a device driver
 */
#define PFN_FLAGS_MASK (((unsigned long) ~PAGE_MASK) \
		<< (BITS_PER_LONG - PAGE_SHIFT))
#define PFN_SG_CHAIN (1UL << (BITS_PER_LONG - 1))
#define PFN_SG_LAST (1UL << (BITS_PER_LONG - 2))
#define PFN_DEV (1UL << (BITS_PER_LONG - 3))
#define PFN_MAP (1UL << (BITS_PER_LONG - 4))

static inline pfn_t __pfn_to_pfn_t(unsigned long pfn, unsigned long flags)
{
	pfn_t pfn_t = { .val = pfn | (flags & PFN_FLAGS_MASK), };

	return pfn_t;
}

/* a default pfn to pfn_t conversion assumes that @pfn is pfn_valid() */
static inline pfn_t pfn_to_pfn_t(unsigned long pfn)
{
	return __pfn_to_pfn_t(pfn, 0);
}

extern pfn_t phys_to_pfn_t(dma_addr_t addr, unsigned long flags);

static inline bool pfn_t_has_page(pfn_t pfn)
{
	return (pfn.val & PFN_MAP) == PFN_MAP || (pfn.val & PFN_DEV) == 0;
}

static inline unsigned long pfn_t_to_pfn(pfn_t pfn)
{
	return pfn.val & ~PFN_FLAGS_MASK;
}

static inline struct page *pfn_t_to_page(pfn_t pfn)
{
	if (pfn_t_has_page(pfn))
		return pfn_to_page(pfn_t_to_pfn(pfn));
	return NULL;
}

static inline dma_addr_t pfn_t_to_phys(pfn_t pfn)
{
	return PFN_PHYS(pfn_t_to_pfn(pfn));
}

static inline void *pfn_t_to_virt(pfn_t pfn)
{
	if (pfn_t_has_page(pfn))
		return __va(pfn_t_to_phys(pfn));
	return NULL;
}

static inline pfn_t page_to_pfn_t(struct page *page)
{
	return pfn_to_pfn_t(page_to_pfn(page));
}
#endif /* _LINUX_PFN_T_H_ */
