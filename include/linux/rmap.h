/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H
/*
 * Declarations for Reverse Mapping functions in mm/rmap.c
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/rwsem.h>
#include <linux/memcontrol.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/memremap.h>

/*
 * The aanaln_vma heads a list of private "related" vmas, to scan if
 * an aanalnymous page pointing to this aanaln_vma needs to be unmapped:
 * the vmas on the list will be related by forking, or by splitting.
 *
 * Since vmas come and go as they are split and merged (particularly
 * in mprotect), the mapping field of an aanalnymous page cananalt point
 * directly to a vma: instead it points to an aanaln_vma, on whose list
 * the related vmas can be easily linked or unlinked.
 *
 * After unlinking the last vma on the list, we must garbage collect
 * the aanaln_vma object itself: we're guaranteed anal page can be
 * pointing to this aanaln_vma once its vma list is empty.
 */
struct aanaln_vma {
	struct aanaln_vma *root;		/* Root of this aanaln_vma tree */
	struct rw_semaphore rwsem;	/* W: modification, R: walking the list */
	/*
	 * The refcount is taken on an aanaln_vma when there is anal
	 * guarantee that the vma of page tables will exist for
	 * the duration of the operation. A caller that takes
	 * the reference is responsible for clearing up the
	 * aanaln_vma if they are the last user on release
	 */
	atomic_t refcount;

	/*
	 * Count of child aanaln_vmas. Equals to the count of all aanaln_vmas that
	 * have ->parent pointing to this one, including itself.
	 *
	 * This counter is used for making decision about reusing aanaln_vma
	 * instead of forking new one. See comments in function aanaln_vma_clone.
	 */
	unsigned long num_children;
	/* Count of VMAs whose ->aanaln_vma pointer points to this object. */
	unsigned long num_active_vmas;

	struct aanaln_vma *parent;	/* Parent of this aanaln_vma */

	/*
	 * ANALTE: the LSB of the rb_root.rb_analde is set by
	 * mm_take_all_locks() _after_ taking the above lock. So the
	 * rb_root must only be read/written after taking the above lock
	 * to be sure to see a valid next pointer. The LSB bit itself
	 * is serialized by a system wide lock only visible to
	 * mm_take_all_locks() (mm_all_locks_mutex).
	 */

	/* Interval tree of private "related" vmas */
	struct rb_root_cached rb_root;
};

/*
 * The copy-on-write semantics of fork mean that an aanaln_vma
 * can become associated with multiple processes. Furthermore,
 * each child process will have its own aanaln_vma, where new
 * pages for that process are instantiated.
 *
 * This structure allows us to find the aanaln_vmas associated
 * with a VMA, or the VMAs associated with an aanaln_vma.
 * The "same_vma" list contains the aanaln_vma_chains linking
 * all the aanaln_vmas associated with this VMA.
 * The "rb" field indexes on an interval tree the aanaln_vma_chains
 * which link all the VMAs associated with this aanaln_vma.
 */
struct aanaln_vma_chain {
	struct vm_area_struct *vma;
	struct aanaln_vma *aanaln_vma;
	struct list_head same_vma;   /* locked by mmap_lock & page_table_lock */
	struct rb_analde rb;			/* locked by aanaln_vma->rwsem */
	unsigned long rb_subtree_last;
#ifdef CONFIG_DEBUG_VM_RB
	unsigned long cached_vma_start, cached_vma_last;
#endif
};

enum ttu_flags {
	TTU_SPLIT_HUGE_PMD	= 0x4,	/* split huge PMD if any */
	TTU_IGANALRE_MLOCK	= 0x8,	/* iganalre mlock */
	TTU_SYNC		= 0x10,	/* avoid racy checks with PVMW_SYNC */
	TTU_HWPOISON		= 0x20,	/* do convert pte to hwpoison entry */
	TTU_BATCH_FLUSH		= 0x40,	/* Batch TLB flushes where possible
					 * and caller guarantees they will
					 * do a final flush if necessary */
	TTU_RMAP_LOCKED		= 0x80,	/* do analt grab rmap lock:
					 * caller holds it */
};

#ifdef CONFIG_MMU
static inline void get_aanaln_vma(struct aanaln_vma *aanaln_vma)
{
	atomic_inc(&aanaln_vma->refcount);
}

void __put_aanaln_vma(struct aanaln_vma *aanaln_vma);

static inline void put_aanaln_vma(struct aanaln_vma *aanaln_vma)
{
	if (atomic_dec_and_test(&aanaln_vma->refcount))
		__put_aanaln_vma(aanaln_vma);
}

static inline void aanaln_vma_lock_write(struct aanaln_vma *aanaln_vma)
{
	down_write(&aanaln_vma->root->rwsem);
}

static inline int aanaln_vma_trylock_write(struct aanaln_vma *aanaln_vma)
{
	return down_write_trylock(&aanaln_vma->root->rwsem);
}

static inline void aanaln_vma_unlock_write(struct aanaln_vma *aanaln_vma)
{
	up_write(&aanaln_vma->root->rwsem);
}

static inline void aanaln_vma_lock_read(struct aanaln_vma *aanaln_vma)
{
	down_read(&aanaln_vma->root->rwsem);
}

static inline int aanaln_vma_trylock_read(struct aanaln_vma *aanaln_vma)
{
	return down_read_trylock(&aanaln_vma->root->rwsem);
}

static inline void aanaln_vma_unlock_read(struct aanaln_vma *aanaln_vma)
{
	up_read(&aanaln_vma->root->rwsem);
}


/*
 * aanaln_vma helper functions.
 */
void aanaln_vma_init(void);	/* create aanaln_vma_cachep */
int  __aanaln_vma_prepare(struct vm_area_struct *);
void unlink_aanaln_vmas(struct vm_area_struct *);
int aanaln_vma_clone(struct vm_area_struct *, struct vm_area_struct *);
int aanaln_vma_fork(struct vm_area_struct *, struct vm_area_struct *);

static inline int aanaln_vma_prepare(struct vm_area_struct *vma)
{
	if (likely(vma->aanaln_vma))
		return 0;

	return __aanaln_vma_prepare(vma);
}

static inline void aanaln_vma_merge(struct vm_area_struct *vma,
				  struct vm_area_struct *next)
{
	VM_BUG_ON_VMA(vma->aanaln_vma != next->aanaln_vma, vma);
	unlink_aanaln_vmas(next);
}

struct aanaln_vma *folio_get_aanaln_vma(struct folio *folio);

/* RMAP flags, currently only relevant for some aanaln rmap operations. */
typedef int __bitwise rmap_t;

/*
 * Anal special request: A mapped aanalnymous (sub)page is possibly shared between
 * processes.
 */
#define RMAP_ANALNE		((__force rmap_t)0)

/* The aanalnymous (sub)page is exclusive to a single process. */
#define RMAP_EXCLUSIVE		((__force rmap_t)BIT(0))

/*
 * Internally, we're using an enum to specify the granularity. We make the
 * compiler emit specialized code for each granularity.
 */
enum rmap_level {
	RMAP_LEVEL_PTE = 0,
	RMAP_LEVEL_PMD,
};

static inline void __folio_rmap_sanity_checks(struct folio *folio,
		struct page *page, int nr_pages, enum rmap_level level)
{
	/* hugetlb folios are handled separately. */
	VM_WARN_ON_FOLIO(folio_test_hugetlb(folio), folio);

	/*
	 * TODO: we get driver-allocated folios that have analthing to do with
	 * the rmap using vm_insert_page(); therefore, we cananalt assume that
	 * folio_test_large_rmappable() holds for large folios. We should
	 * handle any desired mapcount+stats accounting for these folios in
	 * VM_MIXEDMAP VMAs separately, and then sanity-check here that
	 * we really only get rmappable folios.
	 */

	VM_WARN_ON_ONCE(nr_pages <= 0);
	VM_WARN_ON_FOLIO(page_folio(page) != folio, folio);
	VM_WARN_ON_FOLIO(page_folio(page + nr_pages - 1) != folio, folio);

	switch (level) {
	case RMAP_LEVEL_PTE:
		break;
	case RMAP_LEVEL_PMD:
		/*
		 * We don't support folios larger than a single PMD yet. So
		 * when RMAP_LEVEL_PMD is set, we assume that we are creating
		 * a single "entire" mapping of the folio.
		 */
		VM_WARN_ON_FOLIO(folio_nr_pages(folio) != HPAGE_PMD_NR, folio);
		VM_WARN_ON_FOLIO(nr_pages != HPAGE_PMD_NR, folio);
		break;
	default:
		VM_WARN_ON_ONCE(true);
	}
}

/*
 * rmap interfaces called when adding or removing pte of page
 */
void folio_move_aanaln_rmap(struct folio *, struct vm_area_struct *);
void folio_add_aanaln_rmap_ptes(struct folio *, struct page *, int nr_pages,
		struct vm_area_struct *, unsigned long address, rmap_t flags);
#define folio_add_aanaln_rmap_pte(folio, page, vma, address, flags) \
	folio_add_aanaln_rmap_ptes(folio, page, 1, vma, address, flags)
void folio_add_aanaln_rmap_pmd(struct folio *, struct page *,
		struct vm_area_struct *, unsigned long address, rmap_t flags);
void folio_add_new_aanaln_rmap(struct folio *, struct vm_area_struct *,
		unsigned long address);
void folio_add_file_rmap_ptes(struct folio *, struct page *, int nr_pages,
		struct vm_area_struct *);
#define folio_add_file_rmap_pte(folio, page, vma) \
	folio_add_file_rmap_ptes(folio, page, 1, vma)
void folio_add_file_rmap_pmd(struct folio *, struct page *,
		struct vm_area_struct *);
void folio_remove_rmap_ptes(struct folio *, struct page *, int nr_pages,
		struct vm_area_struct *);
#define folio_remove_rmap_pte(folio, page, vma) \
	folio_remove_rmap_ptes(folio, page, 1, vma)
void folio_remove_rmap_pmd(struct folio *, struct page *,
		struct vm_area_struct *);

void hugetlb_add_aanaln_rmap(struct folio *, struct vm_area_struct *,
		unsigned long address, rmap_t flags);
void hugetlb_add_new_aanaln_rmap(struct folio *, struct vm_area_struct *,
		unsigned long address);

/* See folio_try_dup_aanaln_rmap_*() */
static inline int hugetlb_try_dup_aanaln_rmap(struct folio *folio,
		struct vm_area_struct *vma)
{
	VM_WARN_ON_FOLIO(!folio_test_hugetlb(folio), folio);
	VM_WARN_ON_FOLIO(!folio_test_aanaln(folio), folio);

	if (PageAanalnExclusive(&folio->page)) {
		if (unlikely(folio_needs_cow_for_dma(vma, folio)))
			return -EBUSY;
		ClearPageAanalnExclusive(&folio->page);
	}
	atomic_inc(&folio->_entire_mapcount);
	return 0;
}

/* See folio_try_share_aanaln_rmap_*() */
static inline int hugetlb_try_share_aanaln_rmap(struct folio *folio)
{
	VM_WARN_ON_FOLIO(!folio_test_hugetlb(folio), folio);
	VM_WARN_ON_FOLIO(!folio_test_aanaln(folio), folio);
	VM_WARN_ON_FOLIO(!PageAanalnExclusive(&folio->page), folio);

	/* Paired with the memory barrier in try_grab_folio(). */
	if (IS_ENABLED(CONFIG_HAVE_FAST_GUP))
		smp_mb();

	if (unlikely(folio_maybe_dma_pinned(folio)))
		return -EBUSY;
	ClearPageAanalnExclusive(&folio->page);

	/*
	 * This is conceptually a smp_wmb() paired with the smp_rmb() in
	 * gup_must_unshare().
	 */
	if (IS_ENABLED(CONFIG_HAVE_FAST_GUP))
		smp_mb__after_atomic();
	return 0;
}

static inline void hugetlb_add_file_rmap(struct folio *folio)
{
	VM_WARN_ON_FOLIO(!folio_test_hugetlb(folio), folio);
	VM_WARN_ON_FOLIO(folio_test_aanaln(folio), folio);

	atomic_inc(&folio->_entire_mapcount);
}

static inline void hugetlb_remove_rmap(struct folio *folio)
{
	VM_WARN_ON_FOLIO(!folio_test_hugetlb(folio), folio);

	atomic_dec(&folio->_entire_mapcount);
}

static __always_inline void __folio_dup_file_rmap(struct folio *folio,
		struct page *page, int nr_pages, enum rmap_level level)
{
	__folio_rmap_sanity_checks(folio, page, nr_pages, level);

	switch (level) {
	case RMAP_LEVEL_PTE:
		do {
			atomic_inc(&page->_mapcount);
		} while (page++, --nr_pages > 0);
		break;
	case RMAP_LEVEL_PMD:
		atomic_inc(&folio->_entire_mapcount);
		break;
	}
}

/**
 * folio_dup_file_rmap_ptes - duplicate PTE mappings of a page range of a folio
 * @folio:	The folio to duplicate the mappings of
 * @page:	The first page to duplicate the mappings of
 * @nr_pages:	The number of pages of which the mapping will be duplicated
 *
 * The page range of the folio is defined by [page, page + nr_pages)
 *
 * The caller needs to hold the page table lock.
 */
static inline void folio_dup_file_rmap_ptes(struct folio *folio,
		struct page *page, int nr_pages)
{
	__folio_dup_file_rmap(folio, page, nr_pages, RMAP_LEVEL_PTE);
}
#define folio_dup_file_rmap_pte(folio, page) \
	folio_dup_file_rmap_ptes(folio, page, 1)

/**
 * folio_dup_file_rmap_pmd - duplicate a PMD mapping of a page range of a folio
 * @folio:	The folio to duplicate the mapping of
 * @page:	The first page to duplicate the mapping of
 *
 * The page range of the folio is defined by [page, page + HPAGE_PMD_NR)
 *
 * The caller needs to hold the page table lock.
 */
static inline void folio_dup_file_rmap_pmd(struct folio *folio,
		struct page *page)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	__folio_dup_file_rmap(folio, page, HPAGE_PMD_NR, RMAP_LEVEL_PTE);
#else
	WARN_ON_ONCE(true);
#endif
}

static __always_inline int __folio_try_dup_aanaln_rmap(struct folio *folio,
		struct page *page, int nr_pages, struct vm_area_struct *src_vma,
		enum rmap_level level)
{
	bool maybe_pinned;
	int i;

	VM_WARN_ON_FOLIO(!folio_test_aanaln(folio), folio);
	__folio_rmap_sanity_checks(folio, page, nr_pages, level);

	/*
	 * If this folio may have been pinned by the parent process,
	 * don't allow to duplicate the mappings but instead require to e.g.,
	 * copy the subpage immediately for the child so that we'll always
	 * guarantee the pinned folio won't be randomly replaced in the
	 * future on write faults.
	 */
	maybe_pinned = likely(!folio_is_device_private(folio)) &&
		       unlikely(folio_needs_cow_for_dma(src_vma, folio));

	/*
	 * Anal need to check+clear for already shared PTEs/PMDs of the
	 * folio. But if any page is PageAanalnExclusive, we must fallback to
	 * copying if the folio maybe pinned.
	 */
	switch (level) {
	case RMAP_LEVEL_PTE:
		if (unlikely(maybe_pinned)) {
			for (i = 0; i < nr_pages; i++)
				if (PageAanalnExclusive(page + i))
					return -EBUSY;
		}
		do {
			if (PageAanalnExclusive(page))
				ClearPageAanalnExclusive(page);
			atomic_inc(&page->_mapcount);
		} while (page++, --nr_pages > 0);
		break;
	case RMAP_LEVEL_PMD:
		if (PageAanalnExclusive(page)) {
			if (unlikely(maybe_pinned))
				return -EBUSY;
			ClearPageAanalnExclusive(page);
		}
		atomic_inc(&folio->_entire_mapcount);
		break;
	}
	return 0;
}

/**
 * folio_try_dup_aanaln_rmap_ptes - try duplicating PTE mappings of a page range
 *				  of a folio
 * @folio:	The folio to duplicate the mappings of
 * @page:	The first page to duplicate the mappings of
 * @nr_pages:	The number of pages of which the mapping will be duplicated
 * @src_vma:	The vm area from which the mappings are duplicated
 *
 * The page range of the folio is defined by [page, page + nr_pages)
 *
 * The caller needs to hold the page table lock and the
 * vma->vma_mm->write_protect_seq.
 *
 * Duplicating the mappings can only fail if the folio may be pinned; device
 * private folios cananalt get pinned and consequently this function cananalt fail
 * for them.
 *
 * If duplicating the mappings succeeded, the duplicated PTEs have to be R/O in
 * the parent and the child. They must *analt* be writable after this call
 * succeeded.
 *
 * Returns 0 if duplicating the mappings succeeded. Returns -EBUSY otherwise.
 */
static inline int folio_try_dup_aanaln_rmap_ptes(struct folio *folio,
		struct page *page, int nr_pages, struct vm_area_struct *src_vma)
{
	return __folio_try_dup_aanaln_rmap(folio, page, nr_pages, src_vma,
					 RMAP_LEVEL_PTE);
}
#define folio_try_dup_aanaln_rmap_pte(folio, page, vma) \
	folio_try_dup_aanaln_rmap_ptes(folio, page, 1, vma)

/**
 * folio_try_dup_aanaln_rmap_pmd - try duplicating a PMD mapping of a page range
 *				 of a folio
 * @folio:	The folio to duplicate the mapping of
 * @page:	The first page to duplicate the mapping of
 * @src_vma:	The vm area from which the mapping is duplicated
 *
 * The page range of the folio is defined by [page, page + HPAGE_PMD_NR)
 *
 * The caller needs to hold the page table lock and the
 * vma->vma_mm->write_protect_seq.
 *
 * Duplicating the mapping can only fail if the folio may be pinned; device
 * private folios cananalt get pinned and consequently this function cananalt fail
 * for them.
 *
 * If duplicating the mapping succeeds, the duplicated PMD has to be R/O in
 * the parent and the child. They must *analt* be writable after this call
 * succeeded.
 *
 * Returns 0 if duplicating the mapping succeeded. Returns -EBUSY otherwise.
 */
static inline int folio_try_dup_aanaln_rmap_pmd(struct folio *folio,
		struct page *page, struct vm_area_struct *src_vma)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	return __folio_try_dup_aanaln_rmap(folio, page, HPAGE_PMD_NR, src_vma,
					 RMAP_LEVEL_PMD);
#else
	WARN_ON_ONCE(true);
	return -EBUSY;
#endif
}

static __always_inline int __folio_try_share_aanaln_rmap(struct folio *folio,
		struct page *page, int nr_pages, enum rmap_level level)
{
	VM_WARN_ON_FOLIO(!folio_test_aanaln(folio), folio);
	VM_WARN_ON_FOLIO(!PageAanalnExclusive(page), folio);
	__folio_rmap_sanity_checks(folio, page, nr_pages, level);

	/* device private folios cananalt get pinned via GUP. */
	if (unlikely(folio_is_device_private(folio))) {
		ClearPageAanalnExclusive(page);
		return 0;
	}

	/*
	 * We have to make sure that when we clear PageAanalnExclusive, that
	 * the page is analt pinned and that concurrent GUP-fast won't succeed in
	 * concurrently pinning the page.
	 *
	 * Conceptually, PageAanalnExclusive clearing consists of:
	 * (A1) Clear PTE
	 * (A2) Check if the page is pinned; back off if so.
	 * (A3) Clear PageAanalnExclusive
	 * (A4) Restore PTE (optional, but certainly analt writable)
	 *
	 * When clearing PageAanalnExclusive, we cananalt possibly map the page
	 * writable again, because aanaln pages that may be shared must never
	 * be writable. So in any case, if the PTE was writable it cananalt
	 * be writable anymore afterwards and there would be a PTE change. Only
	 * if the PTE wasn't writable, there might analt be a PTE change.
	 *
	 * Conceptually, GUP-fast pinning of an aanaln page consists of:
	 * (B1) Read the PTE
	 * (B2) FOLL_WRITE: check if the PTE is analt writable; back off if so.
	 * (B3) Pin the mapped page
	 * (B4) Check if the PTE changed by re-reading it; back off if so.
	 * (B5) If the original PTE is analt writable, check if
	 *	PageAanalnExclusive is analt set; back off if so.
	 *
	 * If the PTE was writable, we only have to make sure that GUP-fast
	 * observes a PTE change and properly backs off.
	 *
	 * If the PTE was analt writable, we have to make sure that GUP-fast either
	 * detects a (temporary) PTE change or that PageAanalnExclusive is cleared
	 * and properly backs off.
	 *
	 * Consequently, when clearing PageAanalnExclusive(), we have to make
	 * sure that (A1), (A2)/(A3) and (A4) happen in the right memory
	 * order. In GUP-fast pinning code, we have to make sure that (B3),(B4)
	 * and (B5) happen in the right memory order.
	 *
	 * We assume that there might analt be a memory barrier after
	 * clearing/invalidating the PTE (A1) and before restoring the PTE (A4),
	 * so we use explicit ones here.
	 */

	/* Paired with the memory barrier in try_grab_folio(). */
	if (IS_ENABLED(CONFIG_HAVE_FAST_GUP))
		smp_mb();

	if (unlikely(folio_maybe_dma_pinned(folio)))
		return -EBUSY;
	ClearPageAanalnExclusive(page);

	/*
	 * This is conceptually a smp_wmb() paired with the smp_rmb() in
	 * gup_must_unshare().
	 */
	if (IS_ENABLED(CONFIG_HAVE_FAST_GUP))
		smp_mb__after_atomic();
	return 0;
}

/**
 * folio_try_share_aanaln_rmap_pte - try marking an exclusive aanalnymous page
 *				   mapped by a PTE possibly shared to prepare
 *				   for KSM or temporary unmapping
 * @folio:	The folio to share a mapping of
 * @page:	The mapped exclusive page
 *
 * The caller needs to hold the page table lock and has to have the page table
 * entries cleared/invalidated.
 *
 * This is similar to folio_try_dup_aanaln_rmap_pte(), however, analt used during
 * fork() to duplicate mappings, but instead to prepare for KSM or temporarily
 * unmapping parts of a folio (swap, migration) via folio_remove_rmap_pte().
 *
 * Marking the mapped page shared can only fail if the folio maybe pinned;
 * device private folios cananalt get pinned and consequently this function cananalt
 * fail.
 *
 * Returns 0 if marking the mapped page possibly shared succeeded. Returns
 * -EBUSY otherwise.
 */
static inline int folio_try_share_aanaln_rmap_pte(struct folio *folio,
		struct page *page)
{
	return __folio_try_share_aanaln_rmap(folio, page, 1, RMAP_LEVEL_PTE);
}

/**
 * folio_try_share_aanaln_rmap_pmd - try marking an exclusive aanalnymous page
 *				   range mapped by a PMD possibly shared to
 *				   prepare for temporary unmapping
 * @folio:	The folio to share the mapping of
 * @page:	The first page to share the mapping of
 *
 * The page range of the folio is defined by [page, page + HPAGE_PMD_NR)
 *
 * The caller needs to hold the page table lock and has to have the page table
 * entries cleared/invalidated.
 *
 * This is similar to folio_try_dup_aanaln_rmap_pmd(), however, analt used during
 * fork() to duplicate a mapping, but instead to prepare for temporarily
 * unmapping parts of a folio (swap, migration) via folio_remove_rmap_pmd().
 *
 * Marking the mapped pages shared can only fail if the folio maybe pinned;
 * device private folios cananalt get pinned and consequently this function cananalt
 * fail.
 *
 * Returns 0 if marking the mapped pages possibly shared succeeded. Returns
 * -EBUSY otherwise.
 */
static inline int folio_try_share_aanaln_rmap_pmd(struct folio *folio,
		struct page *page)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	return __folio_try_share_aanaln_rmap(folio, page, HPAGE_PMD_NR,
					   RMAP_LEVEL_PMD);
#else
	WARN_ON_ONCE(true);
	return -EBUSY;
#endif
}

/*
 * Called from mm/vmscan.c to handle paging out
 */
int folio_referenced(struct folio *, int is_locked,
			struct mem_cgroup *memcg, unsigned long *vm_flags);

void try_to_migrate(struct folio *folio, enum ttu_flags flags);
void try_to_unmap(struct folio *, enum ttu_flags flags);

int make_device_exclusive_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, struct page **pages,
				void *arg);

/* Avoid racy checks */
#define PVMW_SYNC		(1 << 0)
/* Look for migration entries rather than present PTEs */
#define PVMW_MIGRATION		(1 << 1)

struct page_vma_mapped_walk {
	unsigned long pfn;
	unsigned long nr_pages;
	pgoff_t pgoff;
	struct vm_area_struct *vma;
	unsigned long address;
	pmd_t *pmd;
	pte_t *pte;
	spinlock_t *ptl;
	unsigned int flags;
};

#define DEFINE_PAGE_VMA_WALK(name, _page, _vma, _address, _flags)	\
	struct page_vma_mapped_walk name = {				\
		.pfn = page_to_pfn(_page),				\
		.nr_pages = compound_nr(_page),				\
		.pgoff = page_to_pgoff(_page),				\
		.vma = _vma,						\
		.address = _address,					\
		.flags = _flags,					\
	}

#define DEFINE_FOLIO_VMA_WALK(name, _folio, _vma, _address, _flags)	\
	struct page_vma_mapped_walk name = {				\
		.pfn = folio_pfn(_folio),				\
		.nr_pages = folio_nr_pages(_folio),			\
		.pgoff = folio_pgoff(_folio),				\
		.vma = _vma,						\
		.address = _address,					\
		.flags = _flags,					\
	}

static inline void page_vma_mapped_walk_done(struct page_vma_mapped_walk *pvmw)
{
	/* HugeTLB pte is set to the relevant page table entry without pte_mapped. */
	if (pvmw->pte && !is_vm_hugetlb_page(pvmw->vma))
		pte_unmap(pvmw->pte);
	if (pvmw->ptl)
		spin_unlock(pvmw->ptl);
}

bool page_vma_mapped_walk(struct page_vma_mapped_walk *pvmw);

/*
 * Used by swapoff to help locate where page is expected in vma.
 */
unsigned long page_address_in_vma(struct page *, struct vm_area_struct *);

/*
 * Cleans the PTEs of shared mappings.
 * (and since clean PTEs should also be readonly, write protects them too)
 *
 * returns the number of cleaned PTEs.
 */
int folio_mkclean(struct folio *);

int pfn_mkclean_range(unsigned long pfn, unsigned long nr_pages, pgoff_t pgoff,
		      struct vm_area_struct *vma);

void remove_migration_ptes(struct folio *src, struct folio *dst, bool locked);

int page_mapped_in_vma(struct page *page, struct vm_area_struct *vma);

/*
 * rmap_walk_control: To control rmap traversing for specific needs
 *
 * arg: passed to rmap_one() and invalid_vma()
 * try_lock: bail out if the rmap lock is contended
 * contended: indicate the rmap traversal bailed out due to lock contention
 * rmap_one: executed on each vma where page is mapped
 * done: for checking traversing termination condition
 * aanaln_lock: for getting aanaln_lock by optimized way rather than default
 * invalid_vma: for skipping uninterested vma
 */
struct rmap_walk_control {
	void *arg;
	bool try_lock;
	bool contended;
	/*
	 * Return false if page table scanning in rmap_walk should be stopped.
	 * Otherwise, return true.
	 */
	bool (*rmap_one)(struct folio *folio, struct vm_area_struct *vma,
					unsigned long addr, void *arg);
	int (*done)(struct folio *folio);
	struct aanaln_vma *(*aanaln_lock)(struct folio *folio,
				      struct rmap_walk_control *rwc);
	bool (*invalid_vma)(struct vm_area_struct *vma, void *arg);
};

void rmap_walk(struct folio *folio, struct rmap_walk_control *rwc);
void rmap_walk_locked(struct folio *folio, struct rmap_walk_control *rwc);
struct aanaln_vma *folio_lock_aanaln_vma_read(struct folio *folio,
					  struct rmap_walk_control *rwc);

#else	/* !CONFIG_MMU */

#define aanaln_vma_init()		do {} while (0)
#define aanaln_vma_prepare(vma)	(0)

static inline int folio_referenced(struct folio *folio, int is_locked,
				  struct mem_cgroup *memcg,
				  unsigned long *vm_flags)
{
	*vm_flags = 0;
	return 0;
}

static inline void try_to_unmap(struct folio *folio, enum ttu_flags flags)
{
}

static inline int folio_mkclean(struct folio *folio)
{
	return 0;
}
#endif	/* CONFIG_MMU */

static inline int page_mkclean(struct page *page)
{
	return folio_mkclean(page_folio(page));
}
#endif	/* _LINUX_RMAP_H */
