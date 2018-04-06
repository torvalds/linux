/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MIGRATE_H
#define _LINUX_MIGRATE_H

#include <linux/mm.h>
#include <linux/mempolicy.h>
#include <linux/migrate_mode.h>
#include <linux/hugetlb.h>

typedef struct page *new_page_t(struct page *page, unsigned long private,
				int **reason);
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
extern char *migrate_reason_names[MR_TYPES];

static inline struct page *new_page_nodemask(struct page *page,
				int preferred_nid, nodemask_t *nodemask)
{
	gfp_t gfp_mask = GFP_USER | __GFP_MOVABLE | __GFP_RETRY_MAYFAIL;
	unsigned int order = 0;
	struct page *new_page = NULL;

	if (PageHuge(page))
		return alloc_huge_page_nodemask(page_hstate(compound_head(page)),
				preferred_nid, nodemask);

	if (thp_migration_supported() && PageTransHuge(page)) {
		order = HPAGE_PMD_ORDER;
		gfp_mask |= GFP_TRANSHUGE;
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
		struct page *newpage, struct page *page,
		struct buffer_head *head, enum migrate_mode mode,
		int extra_count);
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
#define MIGRATE_PFN_DEVICE	(1UL << 4)
#define MIGRATE_PFN_ERROR	(1UL << 5)
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

/*
 * struct migrate_vma_ops - migrate operation callback
 *
 * @alloc_and_copy: alloc destination memory and copy source memory to it
 * @finalize_and_map: allow caller to map the successfully migrated pages
 *
 *
 * The alloc_and_copy() callback happens once all source pages have been locked,
 * unmapped and checked (checked whether pinned or not). All pages that can be
 * migrated will have an entry in the src array set with the pfn value of the
 * page and with the MIGRATE_PFN_VALID and MIGRATE_PFN_MIGRATE flag set (other
 * flags might be set but should be ignored by the callback).
 *
 * The alloc_and_copy() callback can then allocate destination memory and copy
 * source memory to it for all those entries (ie with MIGRATE_PFN_VALID and
 * MIGRATE_PFN_MIGRATE flag set). Once these are allocated and copied, the
 * callback must update each corresponding entry in the dst array with the pfn
 * value of the destination page and with the MIGRATE_PFN_VALID and
 * MIGRATE_PFN_LOCKED flags set (destination pages must have their struct pages
 * locked, via lock_page()).
 *
 * At this point the alloc_and_copy() callback is done and returns.
 *
 * Note that the callback does not have to migrate all the pages that are
 * marked with MIGRATE_PFN_MIGRATE flag in src array unless this is a migration
 * from device memory to system memory (ie the MIGRATE_PFN_DEVICE flag is also
 * set in the src array entry). If the device driver cannot migrate a device
 * page back to system memory, then it must set the corresponding dst array
 * entry to MIGRATE_PFN_ERROR. This will trigger a SIGBUS if CPU tries to
 * access any of the virtual addresses originally backed by this page. Because
 * a SIGBUS is such a severe result for the userspace process, the device
 * driver should avoid setting MIGRATE_PFN_ERROR unless it is really in an
 * unrecoverable state.
 *
 * For empty entry inside CPU page table (pte_none() or pmd_none() is true) we
 * do set MIGRATE_PFN_MIGRATE flag inside the corresponding source array thus
 * allowing device driver to allocate device memory for those unback virtual
 * address. For this the device driver simply have to allocate device memory
 * and properly set the destination entry like for regular migration. Note that
 * this can still fails and thus inside the device driver must check if the
 * migration was successful for those entry inside the finalize_and_map()
 * callback just like for regular migration.
 *
 * THE alloc_and_copy() CALLBACK MUST NOT CHANGE ANY OF THE SRC ARRAY ENTRIES
 * OR BAD THINGS WILL HAPPEN !
 *
 *
 * The finalize_and_map() callback happens after struct page migration from
 * source to destination (destination struct pages are the struct pages for the
 * memory allocated by the alloc_and_copy() callback).  Migration can fail, and
 * thus the finalize_and_map() allows the driver to inspect which pages were
 * successfully migrated, and which were not. Successfully migrated pages will
 * have the MIGRATE_PFN_MIGRATE flag set for their src array entry.
 *
 * It is safe to update device page table from within the finalize_and_map()
 * callback because both destination and source page are still locked, and the
 * mmap_sem is held in read mode (hence no one can unmap the range being
 * migrated).
 *
 * Once callback is done cleaning up things and updating its page table (if it
 * chose to do so, this is not an obligation) then it returns. At this point,
 * the HMM core will finish up the final steps, and the migration is complete.
 *
 * THE finalize_and_map() CALLBACK MUST NOT CHANGE ANY OF THE SRC OR DST ARRAY
 * ENTRIES OR BAD THINGS WILL HAPPEN !
 */
struct migrate_vma_ops {
	void (*alloc_and_copy)(struct vm_area_struct *vma,
			       const unsigned long *src,
			       unsigned long *dst,
			       unsigned long start,
			       unsigned long end,
			       void *private);
	void (*finalize_and_map)(struct vm_area_struct *vma,
				 const unsigned long *src,
				 const unsigned long *dst,
				 unsigned long start,
				 unsigned long end,
				 void *private);
};

#if defined(CONFIG_MIGRATE_VMA_HELPER)
int migrate_vma(const struct migrate_vma_ops *ops,
		struct vm_area_struct *vma,
		unsigned long start,
		unsigned long end,
		unsigned long *src,
		unsigned long *dst,
		void *private);
#else
static inline int migrate_vma(const struct migrate_vma_ops *ops,
			      struct vm_area_struct *vma,
			      unsigned long start,
			      unsigned long end,
			      unsigned long *src,
			      unsigned long *dst,
			      void *private)
{
	return -EINVAL;
}
#endif /* IS_ENABLED(CONFIG_MIGRATE_VMA_HELPER) */

#endif /* CONFIG_MIGRATION */

#endif /* _LINUX_MIGRATE_H */
