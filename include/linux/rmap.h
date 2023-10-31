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
 * The anon_vma heads a list of private "related" vmas, to scan if
 * an anonymous page pointing to this anon_vma needs to be unmapped:
 * the vmas on the list will be related by forking, or by splitting.
 *
 * Since vmas come and go as they are split and merged (particularly
 * in mprotect), the mapping field of an anonymous page cannot point
 * directly to a vma: instead it points to an anon_vma, on whose list
 * the related vmas can be easily linked or unlinked.
 *
 * After unlinking the last vma on the list, we must garbage collect
 * the anon_vma object itself: we're guaranteed no page can be
 * pointing to this anon_vma once its vma list is empty.
 */
struct anon_vma {
	struct anon_vma *root;		/* Root of this anon_vma tree */
	struct rw_semaphore rwsem;	/* W: modification, R: walking the list */
	/*
	 * The refcount is taken on an anon_vma when there is no
	 * guarantee that the vma of page tables will exist for
	 * the duration of the operation. A caller that takes
	 * the reference is responsible for clearing up the
	 * anon_vma if they are the last user on release
	 */
	atomic_t refcount;

	/*
	 * Count of child anon_vmas. Equals to the count of all anon_vmas that
	 * have ->parent pointing to this one, including itself.
	 *
	 * This counter is used for making decision about reusing anon_vma
	 * instead of forking new one. See comments in function anon_vma_clone.
	 */
	unsigned long num_children;
	/* Count of VMAs whose ->anon_vma pointer points to this object. */
	unsigned long num_active_vmas;

	struct anon_vma *parent;	/* Parent of this anon_vma */

	/*
	 * NOTE: the LSB of the rb_root.rb_node is set by
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
 * The copy-on-write semantics of fork mean that an anon_vma
 * can become associated with multiple processes. Furthermore,
 * each child process will have its own anon_vma, where new
 * pages for that process are instantiated.
 *
 * This structure allows us to find the anon_vmas associated
 * with a VMA, or the VMAs associated with an anon_vma.
 * The "same_vma" list contains the anon_vma_chains linking
 * all the anon_vmas associated with this VMA.
 * The "rb" field indexes on an interval tree the anon_vma_chains
 * which link all the VMAs associated with this anon_vma.
 */
struct anon_vma_chain {
	struct vm_area_struct *vma;
	struct anon_vma *anon_vma;
	struct list_head same_vma;   /* locked by mmap_lock & page_table_lock */
	struct rb_node rb;			/* locked by anon_vma->rwsem */
	unsigned long rb_subtree_last;
#ifdef CONFIG_DEBUG_VM_RB
	unsigned long cached_vma_start, cached_vma_last;
#endif
};

enum ttu_flags {
	TTU_SPLIT_HUGE_PMD	= 0x4,	/* split huge PMD if any */
	TTU_IGNORE_MLOCK	= 0x8,	/* ignore mlock */
	TTU_SYNC		= 0x10,	/* avoid racy checks with PVMW_SYNC */
	TTU_HWPOISON		= 0x20,	/* do convert pte to hwpoison entry */
	TTU_BATCH_FLUSH		= 0x40,	/* Batch TLB flushes where possible
					 * and caller guarantees they will
					 * do a final flush if necessary */
	TTU_RMAP_LOCKED		= 0x80,	/* do not grab rmap lock:
					 * caller holds it */
};

#ifdef CONFIG_MMU
static inline void get_anon_vma(struct anon_vma *anon_vma)
{
	atomic_inc(&anon_vma->refcount);
}

void __put_anon_vma(struct anon_vma *anon_vma);

static inline void put_anon_vma(struct anon_vma *anon_vma)
{
	if (atomic_dec_and_test(&anon_vma->refcount))
		__put_anon_vma(anon_vma);
}

static inline void anon_vma_lock_write(struct anon_vma *anon_vma)
{
	down_write(&anon_vma->root->rwsem);
}

static inline void anon_vma_unlock_write(struct anon_vma *anon_vma)
{
	up_write(&anon_vma->root->rwsem);
}

static inline void anon_vma_lock_read(struct anon_vma *anon_vma)
{
	down_read(&anon_vma->root->rwsem);
}

static inline int anon_vma_trylock_read(struct anon_vma *anon_vma)
{
	return down_read_trylock(&anon_vma->root->rwsem);
}

static inline void anon_vma_unlock_read(struct anon_vma *anon_vma)
{
	up_read(&anon_vma->root->rwsem);
}


/*
 * anon_vma helper functions.
 */
void anon_vma_init(void);	/* create anon_vma_cachep */
int  __anon_vma_prepare(struct vm_area_struct *);
void unlink_anon_vmas(struct vm_area_struct *);
int anon_vma_clone(struct vm_area_struct *, struct vm_area_struct *);
int anon_vma_fork(struct vm_area_struct *, struct vm_area_struct *);

static inline int anon_vma_prepare(struct vm_area_struct *vma)
{
	if (likely(vma->anon_vma))
		return 0;

	return __anon_vma_prepare(vma);
}

static inline void anon_vma_merge(struct vm_area_struct *vma,
				  struct vm_area_struct *next)
{
	VM_BUG_ON_VMA(vma->anon_vma != next->anon_vma, vma);
	unlink_anon_vmas(next);
}

struct anon_vma *folio_get_anon_vma(struct folio *folio);

/* RMAP flags, currently only relevant for some anon rmap operations. */
typedef int __bitwise rmap_t;

/*
 * No special request: if the page is a subpage of a compound page, it is
 * mapped via a PTE. The mapped (sub)page is possibly shared between processes.
 */
#define RMAP_NONE		((__force rmap_t)0)

/* The (sub)page is exclusive to a single process. */
#define RMAP_EXCLUSIVE		((__force rmap_t)BIT(0))

/*
 * The compound page is not mapped via PTEs, but instead via a single PMD and
 * should be accounted accordingly.
 */
#define RMAP_COMPOUND		((__force rmap_t)BIT(1))

/*
 * rmap interfaces called when adding or removing pte of page
 */
void page_move_anon_rmap(struct page *, struct vm_area_struct *);
void page_add_anon_rmap(struct page *, struct vm_area_struct *,
		unsigned long address, rmap_t flags);
void page_add_new_anon_rmap(struct page *, struct vm_area_struct *,
		unsigned long address);
void folio_add_new_anon_rmap(struct folio *, struct vm_area_struct *,
		unsigned long address);
void page_add_file_rmap(struct page *, struct vm_area_struct *,
		bool compound);
void folio_add_file_rmap_range(struct folio *, struct page *, unsigned int nr,
		struct vm_area_struct *, bool compound);
void page_remove_rmap(struct page *, struct vm_area_struct *,
		bool compound);

void hugepage_add_anon_rmap(struct page *, struct vm_area_struct *,
		unsigned long address, rmap_t flags);
void hugepage_add_new_anon_rmap(struct folio *, struct vm_area_struct *,
		unsigned long address);

static inline void __page_dup_rmap(struct page *page, bool compound)
{
	if (compound) {
		struct folio *folio = (struct folio *)page;

		VM_BUG_ON_PAGE(compound && !PageHead(page), page);
		atomic_inc(&folio->_entire_mapcount);
	} else {
		atomic_inc(&page->_mapcount);
	}
}

static inline void page_dup_file_rmap(struct page *page, bool compound)
{
	__page_dup_rmap(page, compound);
}

/**
 * page_try_dup_anon_rmap - try duplicating a mapping of an already mapped
 *			    anonymous page
 * @page: the page to duplicate the mapping for
 * @compound: the page is mapped as compound or as a small page
 * @vma: the source vma
 *
 * The caller needs to hold the PT lock and the vma->vma_mm->write_protect_seq.
 *
 * Duplicating the mapping can only fail if the page may be pinned; device
 * private pages cannot get pinned and consequently this function cannot fail.
 *
 * If duplicating the mapping succeeds, the page has to be mapped R/O into
 * the parent and the child. It must *not* get mapped writable after this call.
 *
 * Returns 0 if duplicating the mapping succeeded. Returns -EBUSY otherwise.
 */
static inline int page_try_dup_anon_rmap(struct page *page, bool compound,
					 struct vm_area_struct *vma)
{
	VM_BUG_ON_PAGE(!PageAnon(page), page);

	/*
	 * No need to check+clear for already shared pages, including KSM
	 * pages.
	 */
	if (!PageAnonExclusive(page))
		goto dup;

	/*
	 * If this page may have been pinned by the parent process,
	 * don't allow to duplicate the mapping but instead require to e.g.,
	 * copy the page immediately for the child so that we'll always
	 * guarantee the pinned page won't be randomly replaced in the
	 * future on write faults.
	 */
	if (likely(!is_device_private_page(page) &&
	    unlikely(page_needs_cow_for_dma(vma, page))))
		return -EBUSY;

	ClearPageAnonExclusive(page);
	/*
	 * It's okay to share the anon page between both processes, mapping
	 * the page R/O into both processes.
	 */
dup:
	__page_dup_rmap(page, compound);
	return 0;
}

/**
 * page_try_share_anon_rmap - try marking an exclusive anonymous page possibly
 *			      shared to prepare for KSM or temporary unmapping
 * @page: the exclusive anonymous page to try marking possibly shared
 *
 * The caller needs to hold the PT lock and has to have the page table entry
 * cleared/invalidated.
 *
 * This is similar to page_try_dup_anon_rmap(), however, not used during fork()
 * to duplicate a mapping, but instead to prepare for KSM or temporarily
 * unmapping a page (swap, migration) via page_remove_rmap().
 *
 * Marking the page shared can only fail if the page may be pinned; device
 * private pages cannot get pinned and consequently this function cannot fail.
 *
 * Returns 0 if marking the page possibly shared succeeded. Returns -EBUSY
 * otherwise.
 */
static inline int page_try_share_anon_rmap(struct page *page)
{
	VM_BUG_ON_PAGE(!PageAnon(page) || !PageAnonExclusive(page), page);

	/* device private pages cannot get pinned via GUP. */
	if (unlikely(is_device_private_page(page))) {
		ClearPageAnonExclusive(page);
		return 0;
	}

	/*
	 * We have to make sure that when we clear PageAnonExclusive, that
	 * the page is not pinned and that concurrent GUP-fast won't succeed in
	 * concurrently pinning the page.
	 *
	 * Conceptually, PageAnonExclusive clearing consists of:
	 * (A1) Clear PTE
	 * (A2) Check if the page is pinned; back off if so.
	 * (A3) Clear PageAnonExclusive
	 * (A4) Restore PTE (optional, but certainly not writable)
	 *
	 * When clearing PageAnonExclusive, we cannot possibly map the page
	 * writable again, because anon pages that may be shared must never
	 * be writable. So in any case, if the PTE was writable it cannot
	 * be writable anymore afterwards and there would be a PTE change. Only
	 * if the PTE wasn't writable, there might not be a PTE change.
	 *
	 * Conceptually, GUP-fast pinning of an anon page consists of:
	 * (B1) Read the PTE
	 * (B2) FOLL_WRITE: check if the PTE is not writable; back off if so.
	 * (B3) Pin the mapped page
	 * (B4) Check if the PTE changed by re-reading it; back off if so.
	 * (B5) If the original PTE is not writable, check if
	 *	PageAnonExclusive is not set; back off if so.
	 *
	 * If the PTE was writable, we only have to make sure that GUP-fast
	 * observes a PTE change and properly backs off.
	 *
	 * If the PTE was not writable, we have to make sure that GUP-fast either
	 * detects a (temporary) PTE change or that PageAnonExclusive is cleared
	 * and properly backs off.
	 *
	 * Consequently, when clearing PageAnonExclusive(), we have to make
	 * sure that (A1), (A2)/(A3) and (A4) happen in the right memory
	 * order. In GUP-fast pinning code, we have to make sure that (B3),(B4)
	 * and (B5) happen in the right memory order.
	 *
	 * We assume that there might not be a memory barrier after
	 * clearing/invalidating the PTE (A1) and before restoring the PTE (A4),
	 * so we use explicit ones here.
	 */

	/* Paired with the memory barrier in try_grab_folio(). */
	if (IS_ENABLED(CONFIG_HAVE_FAST_GUP))
		smp_mb();

	if (unlikely(page_maybe_dma_pinned(page)))
		return -EBUSY;
	ClearPageAnonExclusive(page);

	/*
	 * This is conceptually a smp_wmb() paired with the smp_rmb() in
	 * gup_must_unshare().
	 */
	if (IS_ENABLED(CONFIG_HAVE_FAST_GUP))
		smp_mb__after_atomic();
	return 0;
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
 * anon_lock: for getting anon_lock by optimized way rather than default
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
	struct anon_vma *(*anon_lock)(struct folio *folio,
				      struct rmap_walk_control *rwc);
	bool (*invalid_vma)(struct vm_area_struct *vma, void *arg);
};

void rmap_walk(struct folio *folio, struct rmap_walk_control *rwc);
void rmap_walk_locked(struct folio *folio, struct rmap_walk_control *rwc);
struct anon_vma *folio_lock_anon_vma_read(struct folio *folio,
					  struct rmap_walk_control *rwc);

#else	/* !CONFIG_MMU */

#define anon_vma_init()		do {} while (0)
#define anon_vma_prepare(vma)	(0)

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
