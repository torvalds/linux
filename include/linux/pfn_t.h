/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PFN_T_H_
#define _LINUX_PFN_T_H_
#include <linux/mm.h>

/*
 * PFN_FLAGS_MASK - mask of all the possible valid pfn_t flags
 */
#define PFN_FLAGS_MASK (((u64) (~PAGE_MASK)) << (BITS_PER_LONG_LONG - PAGE_SHIFT))

#define PFN_FLAGS_TRACE \
	{ }

static inline pfn_t __pfn_to_pfn_t(unsigned long pfn, u64 flags)
{
	pfn_t pfn_t = { .val = pfn | (flags & PFN_FLAGS_MASK), };

	return pfn_t;
}

/* a default pfn to pfn_t conversion assumes that @pfn is pfn_valid() */
static inline pfn_t pfn_to_pfn_t(unsigned long pfn)
{
	return __pfn_to_pfn_t(pfn, 0);
}

static inline pfn_t phys_to_pfn_t(phys_addr_t addr, u64 flags)
{
	return __pfn_to_pfn_t(addr >> PAGE_SHIFT, flags);
}

static inline bool pfn_t_has_page(pfn_t pfn)
{
	return true;
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

static inline phys_addr_t pfn_t_to_phys(pfn_t pfn)
{
	return PFN_PHYS(pfn_t_to_pfn(pfn));
}

static inline pfn_t page_to_pfn_t(struct page *page)
{
	return pfn_to_pfn_t(page_to_pfn(page));
}

static inline int pfn_t_valid(pfn_t pfn)
{
	return pfn_valid(pfn_t_to_pfn(pfn));
}

#ifdef CONFIG_MMU
static inline pte_t pfn_t_pte(pfn_t pfn, pgprot_t pgprot)
{
	return pfn_pte(pfn_t_to_pfn(pfn), pgprot);
}
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline pmd_t pfn_t_pmd(pfn_t pfn, pgprot_t pgprot)
{
	return pfn_pmd(pfn_t_to_pfn(pfn), pgprot);
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static inline pud_t pfn_t_pud(pfn_t pfn, pgprot_t pgprot)
{
	return pfn_pud(pfn_t_to_pfn(pfn), pgprot);
}
#endif
#endif

#endif /* _LINUX_PFN_T_H_ */
