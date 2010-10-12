#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H
/*
 * Declarations for Reverse Mapping functions in mm/rmap.c
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/memcontrol.h>

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
	spinlock_t lock;	/* Serialize access to vma list */
	struct anon_vma *root;	/* Root of this anon_vma tree */
#if defined(CONFIG_KSM) || defined(CONFIG_MIGRATION)

	/*
	 * The external_refcount is taken by either KSM or page migration
	 * to take a reference to an anon_vma when there is no
	 * guarantee that the vma of page tables will exist for
	 * the duration of the operation. A caller that takes
	 * the reference is responsible for clearing up the
	 * anon_vma if they are the last user on release
	 */
	atomic_t external_refcount;
#endif
	/*
	 * NOTE: the LSB of the head.next is set by
	 * mm_take_all_locks() _after_ taking the above lock. So the
	 * head must only be read/written after taking the above lock
	 * to be sure to see a valid next pointer. The LSB bit itself
	 * is serialized by a system wide lock only visible to
	 * mm_take_all_locks() (mm_all_locks_mutex).
	 */
	struct list_head head;	/* Chain of private "related" vmas */
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
 * The "same_anon_vma" list contains the anon_vma_chains
 * which link all the VMAs associated with this anon_vma.
 */
struct anon_vma_chain {
	struct vm_area_struct *vma;
	struct anon_vma *anon_vma;
	struct list_head same_vma;   /* locked by mmap_sem & page_table_lock */
	struct list_head same_anon_vma;	/* locked by anon_vma->lock */
};

#ifdef CONFIG_MMU
#if defined(CONFIG_KSM) || defined(CONFIG_MIGRATION)
static inline void anonvma_external_refcount_init(struct anon_vma *anon_vma)
{
	atomic_set(&anon_vma->external_refcount, 0);
}

static inline int anonvma_external_refcount(struct anon_vma *anon_vma)
{
	return atomic_read(&anon_vma->external_refcount);
}

static inline void get_anon_vma(struct anon_vma *anon_vma)
{
	atomic_inc(&anon_vma->external_refcount);
}

void drop_anon_vma(struct anon_vma *);
#else
static inline void anonvma_external_refcount_init(struct anon_vma *anon_vma)
{
}

static inline int anonvma_external_refcount(struct anon_vma *anon_vma)
{
	return 0;
}

static inline void get_anon_vma(struct anon_vma *anon_vma)
{
}

static inline void drop_anon_vma(struct anon_vma *anon_vma)
{
}
#endif /* CONFIG_KSM */

static inline struct anon_vma *page_anon_vma(struct page *page)
{
	if (((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) !=
					    PAGE_MAPPING_ANON)
		return NULL;
	return page_rmapping(page);
}

static inline void vma_lock_anon_vma(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = vma->anon_vma;
	if (anon_vma)
		spin_lock(&anon_vma->root->lock);
}

static inline void vma_unlock_anon_vma(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = vma->anon_vma;
	if (anon_vma)
		spin_unlock(&anon_vma->root->lock);
}

static inline void anon_vma_lock(struct anon_vma *anon_vma)
{
	spin_lock(&anon_vma->root->lock);
}

static inline void anon_vma_unlock(struct anon_vma *anon_vma)
{
	spin_unlock(&anon_vma->root->lock);
}

/*
 * anon_vma helper functions.
 */
void anon_vma_init(void);	/* create anon_vma_cachep */
int  anon_vma_prepare(struct vm_area_struct *);
void unlink_anon_vmas(struct vm_area_struct *);
int anon_vma_clone(struct vm_area_struct *, struct vm_area_struct *);
int anon_vma_fork(struct vm_area_struct *, struct vm_area_struct *);
void __anon_vma_link(struct vm_area_struct *);
void anon_vma_free(struct anon_vma *);

static inline void anon_vma_merge(struct vm_area_struct *vma,
				  struct vm_area_struct *next)
{
	VM_BUG_ON(vma->anon_vma != next->anon_vma);
	unlink_anon_vmas(next);
}

/*
 * rmap interfaces called when adding or removing pte of page
 */
void page_move_anon_rmap(struct page *, struct vm_area_struct *, unsigned long);
void page_add_anon_rmap(struct page *, struct vm_area_struct *, unsigned long);
void do_page_add_anon_rmap(struct page *, struct vm_area_struct *,
			   unsigned long, int);
void page_add_new_anon_rmap(struct page *, struct vm_area_struct *, unsigned long);
void page_add_file_rmap(struct page *);
void page_remove_rmap(struct page *);

void hugepage_add_anon_rmap(struct page *, struct vm_area_struct *,
			    unsigned long);
void hugepage_add_new_anon_rmap(struct page *, struct vm_area_struct *,
				unsigned long);

static inline void page_dup_rmap(struct page *page)
{
	atomic_inc(&page->_mapcount);
}

/*
 * Called from mm/vmscan.c to handle paging out
 */
int page_referenced(struct page *, int is_locked,
			struct mem_cgroup *cnt, unsigned long *vm_flags);
int page_referenced_one(struct page *, struct vm_area_struct *,
	unsigned long address, unsigned int *mapcount, unsigned long *vm_flags);

enum ttu_flags {
	TTU_UNMAP = 0,			/* unmap mode */
	TTU_MIGRATION = 1,		/* migration mode */
	TTU_MUNLOCK = 2,		/* munlock mode */
	TTU_ACTION_MASK = 0xff,

	TTU_IGNORE_MLOCK = (1 << 8),	/* ignore mlock */
	TTU_IGNORE_ACCESS = (1 << 9),	/* don't age */
	TTU_IGNORE_HWPOISON = (1 << 10),/* corrupted page is recoverable */
};
#define TTU_ACTION(x) ((x) & TTU_ACTION_MASK)

int try_to_unmap(struct page *, enum ttu_flags flags);
int try_to_unmap_one(struct page *, struct vm_area_struct *,
			unsigned long address, enum ttu_flags flags);

/*
 * Called from mm/filemap_xip.c to unmap empty zero page
 */
pte_t *page_check_address(struct page *, struct mm_struct *,
				unsigned long, spinlock_t **, int);

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
int try_to_munlock(struct page *);

/*
 * Called by memory-failure.c to kill processes.
 */
struct anon_vma *page_lock_anon_vma(struct page *page);
void page_unlock_anon_vma(struct anon_vma *anon_vma);
int page_mapped_in_vma(struct page *page, struct vm_area_struct *vma);

/*
 * Called by migrate.c to remove migration ptes, but might be used more later.
 */
int rmap_walk(struct page *page, int (*rmap_one)(struct page *,
		struct vm_area_struct *, unsigned long, void *), void *arg);

#else	/* !CONFIG_MMU */

#define anon_vma_init()		do {} while (0)
#define anon_vma_prepare(vma)	(0)
#define anon_vma_link(vma)	do {} while (0)

static inline int page_referenced(struct page *page, int is_locked,
				  struct mem_cgroup *cnt,
				  unsigned long *vm_flags)
{
	*vm_flags = 0;
	return 0;
}

#define try_to_unmap(page, refs) SWAP_FAIL

static inline int page_mkclean(struct page *page)
{
	return 0;
}


#endif	/* CONFIG_MMU */

/*
 * Return values of try_to_unmap
 */
#define SWAP_SUCCESS	0
#define SWAP_AGAIN	1
#define SWAP_FAIL	2
#define SWAP_MLOCK	3

#endif	/* _LINUX_RMAP_H */
