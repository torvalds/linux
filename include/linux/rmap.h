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

/*
 * The ayesn_vma heads a list of private "related" vmas, to scan if
 * an ayesnymous page pointing to this ayesn_vma needs to be unmapped:
 * the vmas on the list will be related by forking, or by splitting.
 *
 * Since vmas come and go as they are split and merged (particularly
 * in mprotect), the mapping field of an ayesnymous page canyest point
 * directly to a vma: instead it points to an ayesn_vma, on whose list
 * the related vmas can be easily linked or unlinked.
 *
 * After unlinking the last vma on the list, we must garbage collect
 * the ayesn_vma object itself: we're guaranteed yes page can be
 * pointing to this ayesn_vma once its vma list is empty.
 */
struct ayesn_vma {
	struct ayesn_vma *root;		/* Root of this ayesn_vma tree */
	struct rw_semaphore rwsem;	/* W: modification, R: walking the list */
	/*
	 * The refcount is taken on an ayesn_vma when there is yes
	 * guarantee that the vma of page tables will exist for
	 * the duration of the operation. A caller that takes
	 * the reference is responsible for clearing up the
	 * ayesn_vma if they are the last user on release
	 */
	atomic_t refcount;

	/*
	 * Count of child ayesn_vmas and VMAs which points to this ayesn_vma.
	 *
	 * This counter is used for making decision about reusing ayesn_vma
	 * instead of forking new one. See comments in function ayesn_vma_clone.
	 */
	unsigned degree;

	struct ayesn_vma *parent;	/* Parent of this ayesn_vma */

	/*
	 * NOTE: the LSB of the rb_root.rb_yesde is set by
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
 * The copy-on-write semantics of fork mean that an ayesn_vma
 * can become associated with multiple processes. Furthermore,
 * each child process will have its own ayesn_vma, where new
 * pages for that process are instantiated.
 *
 * This structure allows us to find the ayesn_vmas associated
 * with a VMA, or the VMAs associated with an ayesn_vma.
 * The "same_vma" list contains the ayesn_vma_chains linking
 * all the ayesn_vmas associated with this VMA.
 * The "rb" field indexes on an interval tree the ayesn_vma_chains
 * which link all the VMAs associated with this ayesn_vma.
 */
struct ayesn_vma_chain {
	struct vm_area_struct *vma;
	struct ayesn_vma *ayesn_vma;
	struct list_head same_vma;   /* locked by mmap_sem & page_table_lock */
	struct rb_yesde rb;			/* locked by ayesn_vma->rwsem */
	unsigned long rb_subtree_last;
#ifdef CONFIG_DEBUG_VM_RB
	unsigned long cached_vma_start, cached_vma_last;
#endif
};

enum ttu_flags {
	TTU_MIGRATION		= 0x1,	/* migration mode */
	TTU_MUNLOCK		= 0x2,	/* munlock mode */

	TTU_SPLIT_HUGE_PMD	= 0x4,	/* split huge PMD if any */
	TTU_IGNORE_MLOCK	= 0x8,	/* igyesre mlock */
	TTU_IGNORE_ACCESS	= 0x10,	/* don't age */
	TTU_IGNORE_HWPOISON	= 0x20,	/* corrupted page is recoverable */
	TTU_BATCH_FLUSH		= 0x40,	/* Batch TLB flushes where possible
					 * and caller guarantees they will
					 * do a final flush if necessary */
	TTU_RMAP_LOCKED		= 0x80,	/* do yest grab rmap lock:
					 * caller holds it */
	TTU_SPLIT_FREEZE	= 0x100,		/* freeze pte under splitting thp */
};

#ifdef CONFIG_MMU
static inline void get_ayesn_vma(struct ayesn_vma *ayesn_vma)
{
	atomic_inc(&ayesn_vma->refcount);
}

void __put_ayesn_vma(struct ayesn_vma *ayesn_vma);

static inline void put_ayesn_vma(struct ayesn_vma *ayesn_vma)
{
	if (atomic_dec_and_test(&ayesn_vma->refcount))
		__put_ayesn_vma(ayesn_vma);
}

static inline void ayesn_vma_lock_write(struct ayesn_vma *ayesn_vma)
{
	down_write(&ayesn_vma->root->rwsem);
}

static inline void ayesn_vma_unlock_write(struct ayesn_vma *ayesn_vma)
{
	up_write(&ayesn_vma->root->rwsem);
}

static inline void ayesn_vma_lock_read(struct ayesn_vma *ayesn_vma)
{
	down_read(&ayesn_vma->root->rwsem);
}

static inline void ayesn_vma_unlock_read(struct ayesn_vma *ayesn_vma)
{
	up_read(&ayesn_vma->root->rwsem);
}


/*
 * ayesn_vma helper functions.
 */
void ayesn_vma_init(void);	/* create ayesn_vma_cachep */
int  __ayesn_vma_prepare(struct vm_area_struct *);
void unlink_ayesn_vmas(struct vm_area_struct *);
int ayesn_vma_clone(struct vm_area_struct *, struct vm_area_struct *);
int ayesn_vma_fork(struct vm_area_struct *, struct vm_area_struct *);

static inline int ayesn_vma_prepare(struct vm_area_struct *vma)
{
	if (likely(vma->ayesn_vma))
		return 0;

	return __ayesn_vma_prepare(vma);
}

static inline void ayesn_vma_merge(struct vm_area_struct *vma,
				  struct vm_area_struct *next)
{
	VM_BUG_ON_VMA(vma->ayesn_vma != next->ayesn_vma, vma);
	unlink_ayesn_vmas(next);
}

struct ayesn_vma *page_get_ayesn_vma(struct page *page);

/* bitflags for do_page_add_ayesn_rmap() */
#define RMAP_EXCLUSIVE 0x01
#define RMAP_COMPOUND 0x02

/*
 * rmap interfaces called when adding or removing pte of page
 */
void page_move_ayesn_rmap(struct page *, struct vm_area_struct *);
void page_add_ayesn_rmap(struct page *, struct vm_area_struct *,
		unsigned long, bool);
void do_page_add_ayesn_rmap(struct page *, struct vm_area_struct *,
			   unsigned long, int);
void page_add_new_ayesn_rmap(struct page *, struct vm_area_struct *,
		unsigned long, bool);
void page_add_file_rmap(struct page *, bool);
void page_remove_rmap(struct page *, bool);

void hugepage_add_ayesn_rmap(struct page *, struct vm_area_struct *,
			    unsigned long);
void hugepage_add_new_ayesn_rmap(struct page *, struct vm_area_struct *,
				unsigned long);

static inline void page_dup_rmap(struct page *page, bool compound)
{
	atomic_inc(compound ? compound_mapcount_ptr(page) : &page->_mapcount);
}

/*
 * Called from mm/vmscan.c to handle paging out
 */
int page_referenced(struct page *, int is_locked,
			struct mem_cgroup *memcg, unsigned long *vm_flags);

bool try_to_unmap(struct page *, enum ttu_flags flags);

/* Avoid racy checks */
#define PVMW_SYNC		(1 << 0)
/* Look for migarion entries rather than present PTEs */
#define PVMW_MIGRATION		(1 << 1)

struct page_vma_mapped_walk {
	struct page *page;
	struct vm_area_struct *vma;
	unsigned long address;
	pmd_t *pmd;
	pte_t *pte;
	spinlock_t *ptl;
	unsigned int flags;
};

static inline void page_vma_mapped_walk_done(struct page_vma_mapped_walk *pvmw)
{
	if (pvmw->pte)
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
int page_mkclean(struct page *);

/*
 * called in munlock()/munmap() path to check for other vmas holding
 * the page mlocked.
 */
void try_to_munlock(struct page *);

void remove_migration_ptes(struct page *old, struct page *new, bool locked);

/*
 * Called by memory-failure.c to kill processes.
 */
struct ayesn_vma *page_lock_ayesn_vma_read(struct page *page);
void page_unlock_ayesn_vma_read(struct ayesn_vma *ayesn_vma);
int page_mapped_in_vma(struct page *page, struct vm_area_struct *vma);

/*
 * rmap_walk_control: To control rmap traversing for specific needs
 *
 * arg: passed to rmap_one() and invalid_vma()
 * rmap_one: executed on each vma where page is mapped
 * done: for checking traversing termination condition
 * ayesn_lock: for getting ayesn_lock by optimized way rather than default
 * invalid_vma: for skipping uninterested vma
 */
struct rmap_walk_control {
	void *arg;
	/*
	 * Return false if page table scanning in rmap_walk should be stopped.
	 * Otherwise, return true.
	 */
	bool (*rmap_one)(struct page *page, struct vm_area_struct *vma,
					unsigned long addr, void *arg);
	int (*done)(struct page *page);
	struct ayesn_vma *(*ayesn_lock)(struct page *page);
	bool (*invalid_vma)(struct vm_area_struct *vma, void *arg);
};

void rmap_walk(struct page *page, struct rmap_walk_control *rwc);
void rmap_walk_locked(struct page *page, struct rmap_walk_control *rwc);

#else	/* !CONFIG_MMU */

#define ayesn_vma_init()		do {} while (0)
#define ayesn_vma_prepare(vma)	(0)
#define ayesn_vma_link(vma)	do {} while (0)

static inline int page_referenced(struct page *page, int is_locked,
				  struct mem_cgroup *memcg,
				  unsigned long *vm_flags)
{
	*vm_flags = 0;
	return 0;
}

#define try_to_unmap(page, refs) false

static inline int page_mkclean(struct page *page)
{
	return 0;
}


#endif	/* CONFIG_MMU */

#endif	/* _LINUX_RMAP_H */
