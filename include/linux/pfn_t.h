/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PFN_T_H_
#define _LINUX_PFN_T_H_
#include <linux/mm.h>

/*
 * PFN_FLAGS_MASK - mask of all the possible valid pfn_t flags
 * PFN_SG_CHAIN - pfn is a pointer to the next scatterlist entry
 * PFN_SG_LAST - pfn references a page and is the last scatterlist entry
 * PFN_DEV - pfn is not covered by system memmap by default
 * PFN_MAP - pfn has a dynamic page mapping established by a device driver
 * PFN_SPECIAL - for CONFIG_FS_DAX_LIMITED builds to allow XIP, but not
 *		 get_user_pages
 */
#define PFN_FLAGS_MASK (((u64) (~PAGE_MASK)) << (BITS_PER_LONG_LONG - PAGE_SHIFT))
#define PFN_SG_CHAIN (1ULL << (BITS_PER_LONG_LONG - 1))
#define PFN_SG_LAST (1ULL << (BITS_PER_LONG_LONG - 2))
#define PFN_DEV (1ULL << (BITS_PER_LONG_LONG - 3))
#define PFN_MAP (1ULL << (BITS_PER_LONG_LONG - 4))
#define PFN_SPECIAL (1ULL << (BITS_PER_LONG_LONG - 5))

#define PFN_FLAGS_TRACE \
	{ PFN_SPECIAL,	"SPECIAL" }, \
	{ PFN_SG_CHAIN,	"SG_CHAIN" }, \
	{ PFN_SG_LAST,	"SG_LAST" }, \
	{ PFN_DEV,	"DEV" }, \
	{ PFN_MAP,	"MAP" }

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

static inline phys_addr_t pfn_t_to_phys(pfn_t pfn)
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

#ifdef __HAVE_ARCH_PTE_DEVMAP
static inline bool pfn_t_devmap(pfn_t pfn)
{
	const u64 flags = PFN_DEV|PFN_MAP;

	return (pfn.val & flags) == flags;
}
#else
static inline bool pfn_t_devmap(pfn_t pfn)
{
	return false;
}
pte_t pte_mkdevmap(pte_t pte);
pmd_t pmd_mkdevmap(pmd_t pmd);
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && \
	defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
pud_t pud_mkdevmap(pud_t pud);
#endif
#endif /* __HAVE_ARCH_PTE_DEVMAP */

#ifdef CONFIG_ARCH_HAS_PTE_SPECIAL
static inline bool pfn_t_special(pfn_t pfn)
{
	return (pfn.val & PFN_SPECIAL) == PFN_SPECIAL;
}
#else
static inline bool pfn_t_special(pfn_t pfn)
{
	return false;
}
#endif /* CONFIG_ARCH_HAS_PTE_SPECIAL */
#endif /* _LINUX_PFN_T_H_ */
