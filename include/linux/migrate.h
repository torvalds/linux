/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MIGRATE_H
#define _LINUX_MIGRATE_H

#include <linux/mm.h>
#include <linux/mempolicy.h>
#include <linux/migrate_mode.h>
#include <linux/hugetlb.h>

typedef struct page *new_page_t(struct page *page, unsigned long private);
typedef void free_page_t(struct page *page, unsigned long private);

/*
 * Return values from addresss_space_operations.migratepage():
 * - negative errno on page migration failure;
 * - zero on page migration success;
 */
#define MIGRATEPAGE_SUCCESS		0

enum migrate_reason {
	MR_COMPACTION,
	MR_MEMORY_FAILURE,
	MR_MEMORY_HOTPLUG,
	MR_SYSCALL,		/* also applies to cpusets */
	MR_MEMPOLICY_MBIND,
	MR_NUMA_MISPLACED,
	MR_CONTIG_RANGE,
	MR_TYPES
};

/* In mm/debug.c; also keep sync with include/trace/events/migrate.h */
extern const char *migrate_reason_names[MR_TYPES];

static inline struct page *new_page_nodemask(struct page *page,
				int preferred_nid, nodemask_t *nodemask)
{
	gfp_t gfp_mask = GFP_USER | __GFP_MOVABLE | __GFP_RETRY_MAYFAIL;
	unsigned int order = 0;
	struct page *new_page = NULL;

	if (PageHuge(page))
		return alloc_huge_page_nodemask(page_hstate(compound_head(page)),
				preferred_nid, nodemask);

	if (PageTransHuge(page)) {
		gfp_mask |= GFP_TRANSHUGE;
		order = HPAGE_PMD_ORDER;
	}

	if (PageHighMem(page) || (zone_idx(page_zone(page)) == ZONE_MOVABLE))
		gfp_mask |= __GFP_HIGHMEM;

	new_page = __alloc_pages_nodemask(gfp_mask, order,
				preferred_nid, nodemask);

	if (new_page && PageTransHuge(new_page))
		prep_transhuge_page(new_page);

	return new_page;
}

#ifdef CONFIG_MIGRATION

extern void putback_movable_pages(struct list_head *l);
extern int migrate_page(struct address_space *mapping,
			struct page *newpage, struct page *page,
			enum migrate_mode mode);
extern int migrate_pages(struct list_head *l, new_page_t new, free_page_t free,
		unsigned long private, enum migrate_mode mode, int reason);
extern int isolate_movable_page(struct page *page, isolate_mode_t mode);
extern void putback_movable_page(struct page *page);

extern int migrate_prep(void);
extern int migrate_prep_local(void);
extern void migrate_page_states(struct page *newpage, struct page *page);
extern void migrate_page_copy(struct page *newpage, struct page *page);
extern int migrate_huge_page_move_mapping(struct address_space *mapping,
				  struct page *newpage, struct page *page);
extern int migrate_page_move_mapping(struct address_space *mapping,
		struct page *newpage, struct page *page, int extra_count);
#else

static inline void putback_movable_pages(struct list_head *l) {}
static inline int migrate_pages(struct list_head *l, new_page_t new,
		free_page_t free, unsigned long private, enum migrate_mode mode,
		int reason)
	{ return -ENOSYS; }
static inline int isolate_movable_page(struct page *page, isolate_mode_t mode)
	{ return -EBUSY; }

static inline int migrate_prep(void) { return -ENOSYS; }
static inline int migrate_prep_local(void) { return -ENOSYS; }

static inline void migrate_page_states(struct page *newpage, struct page *page)
{
}

static inline void migrate_page_copy(struct page *newpage,
				     struct page *page) {}

static inline int migrate_huge_page_move_mapping(struct address_space *mapping,
				  struct page *newpage, struct page *page)
{
	return -ENOSYS;
}

#endif /* CONFIG_MIGRATION */

#ifdef CONFIG_COMPACTION
extern int PageMovable(struct page *page);
extern void __SetPageMovable(struct page *page, struct address_space *mapping);
extern void __ClearPageMovable(struct page *page);
#else
static inline int PageMovable(struct page *page) { return 0; };
static inline void __SetPageMovable(struct page *page,
				struct address_space *mapping)
{
}
static inline void __ClearPageMovable(struct page *page)
{
}
#endif

#ifdef CONFIG_NUMA_BALANCING
extern bool pmd_trans_migrating(pmd_t pmd);
extern int migrate_misplaced_page(struct page *page,
				  struct vm_area_struct *vma, int node);
#else
static inline bool pmd_trans_migrating(pmd_t pmd)
{
	return false;
}
static inline int migrate_misplaced_page(struct page *page,
					 struct vm_area_struct *vma, int node)
{
	return -EAGAIN; /* can't migrate now */
}
#endif /* CONFIG_NUMA_BALANCING */

#if defined(CONFIG_NUMA_BALANCING) && defined(CONFIG_TRANSPARENT_HUGEPAGE)
extern int migrate_misplaced_transhuge_page(struct mm_struct *mm,
			struct vm_area_struct *vma,
			pmd_t *pmd, pmd_t entry,
			unsigned long address,
			struct page *page, int node);
#else
static inline int migrate_misplaced_transhuge_page(struct mm_struct *mm,
			struct vm_area_struct *vma,
			pmd_t *pmd, pmd_t entry,
			unsigned long address,
			struct page *page, int node)
{
	return -EAGAIN;
}
#endif /* CONFIG_NUMA_BALANCING && CONFIG_TRANSPARENT_HUGEPAGE*/


#ifdef CONFIG_MIGRATION

/*
 * Watch out for PAE architecture, which has an unsigned long, and might not
 * have enough bits to store all physical address and flags. So far we have
 * enough room for all our flags.
 */
#define MIGRATE_PFN_VALID	(1UL << 0)
#define MIGRATE_PFN_MIGRATE	(1UL << 1)
#define MIGRATE_PFN_LOCKED	(1UL << 2)
#define MIGRATE_PFN_WRITE	(1UL << 3)
#define MIGRATE_PFN_SHIFT	6

static inline struct page *migrate_pfn_to_page(unsigned long mpfn)
{
	if (!(mpfn & MIGRATE_PFN_VALID))
		return NULL;
	return pfn_to_page(mpfn >> MIGRATE_PFN_SHIFT);
}

static inline unsigned long migrate_pfn(unsigned long pfn)
{
	return (pfn << MIGRATE_PFN_SHIFT) | MIGRATE_PFN_VALID;
}

struct migrate_vma {
	struct vm_area_struct	*vma;
	/*
	 * Both src and dst array must be big enough for
	 * (end - start) >> PAGE_SHIFT entries.
	 *
	 * The src array must not be modified by the caller after
	 * migrate_vma_setup(), and must not change the dst array after
	 * migrate_vma_pages() returns.
	 */
	unsigned long		*dst;
	unsigned long		*src;
	unsigned long		cpages;
	unsigned long		npages;
	unsigned long		start;
	unsigned long		end;

	/*
	 * Set to the owner value also stored in page->pgmap->owner for
	 * migrating out of device private memory.  If set only device
	 * private pages with this owner are migrated.  If not set
	 * device private pages are not migrated at all.
	 */
	void			*src_owner;
};

int migrate_vma_setup(struct migrate_vma *args);
void migrate_vma_pages(struct migrate_vma *migrate);
void migrate_vma_finalize(struct migrate_vma *migrate);

#endif /* CONFIG_MIGRATION */

#endif /* _LINUX_MIGRATE_H */
