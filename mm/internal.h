/* SPDX-License-Identifier: GPL-2.0-or-later */
/* internal.h: mm/ internal definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#ifndef __MM_INTERNAL_H
#define __MM_INTERNAL_H

#include <linux/fs.h>
#include <linux/khugepaged.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/mmu_notifier.h>
#include <linux/pagemap.h>
#include <linux/pagewalk.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/leafops.h>
#include <linux/tracepoint-defs.h>

/* Internal core VMA manipulation functions. */
#include "vma.h"

struct folio_batch;
struct hstate;

struct huge_bootmem_page {
	struct list_head list;
	struct hstate *hstate;
	unsigned long flags;
};

/* mm/workingset.c */
bool workingset_test_recent(void *shadow, bool file, bool *workingset,
			    bool flush);
void workingset_age_nonresident(struct lruvec *lruvec, unsigned long nr_pages);
void *workingset_eviction(struct folio *folio,
			  struct mem_cgroup *target_memcg);
void workingset_refault(struct folio *folio, void *shadow);
void workingset_activation(struct folio *folio);

/* mm/folio.c */
void lru_note_cost_unlock_irq(struct lruvec *lruvec, bool file,
		unsigned int nr_io, unsigned int nr_rotated);
void lru_note_cost_refault(struct folio *folio);
void folio_add_lru_vma(struct folio *folio, struct vm_area_struct *vma);

static inline bool folio_may_be_lru_cached(struct folio *folio)
{
	/*
	 * Holding PMD-sized folios in per-CPU LRU cache unbalances accounting.
	 * Holding small numbers of low-order mTHP folios in per-CPU LRU cache
	 * will be sensible, but nobody has implemented and tested that yet.
	 */
	return !folio_test_large(folio);
}

static inline void lru_cache_enable(void)
{
	atomic_dec(&lru_disable_count);
}

void lru_cache_disable(void);
void lru_add_drain(void);
void lru_add_drain_cpu(int cpu);
void lru_add_drain_cpu_zone(struct zone *zone);
void folio_deactivate(struct folio *folio);
void folio_mark_lazyfree(struct folio *folio);

/* mm/vmscan.c */
unsigned long zone_reclaimable_pages(struct zone *zone);
unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
				gfp_t gfp_mask, const nodemask_t *mask);
unsigned long lruvec_lru_size(struct lruvec *lruvec, enum lru_list lru,
			      int zone_idx);

#define MEMCG_RECLAIM_MAY_SWAP (1 << 1)
#define MEMCG_RECLAIM_PROACTIVE (1 << 2)
#define MIN_SWAPPINESS 0
#define MAX_SWAPPINESS 200

/* Just reclaim from anon folios in proactive memory reclaim */
#define SWAPPINESS_ANON_ONLY (MAX_SWAPPINESS + 1)

unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *memcg,
					   unsigned long nr_pages,
					   gfp_t gfp_mask,
					   unsigned int reclaim_options,
					   int *swappiness);
unsigned long mem_cgroup_shrink_node(struct mem_cgroup *memcg,
				     gfp_t gfp_mask, bool noswap,
				     pg_data_t *pgdat,
				     unsigned long *nr_scanned);

#ifdef CONFIG_NUMA
extern int sysctl_min_unmapped_ratio;
extern int sysctl_min_slab_ratio;
#endif

/*
 * Maintains state across a page table move. The operation assumes both source
 * and destination VMAs already exist and are specified by the user.
 *
 * Partial moves are permitted, but the old and new ranges must both reside
 * within a VMA.
 *
 * mmap lock must be held in write and VMA write locks must be held on any VMA
 * that is visible.
 *
 * Use the PAGETABLE_MOVE() macro to initialise this struct.
 *
 * The old_addr and new_addr fields are updated as the page table move is
 * executed.
 *
 * NOTE: The page table move is affected by reading from [old_addr, old_end),
 * and old_addr may be updated for better page table alignment, so len_in
 * represents the length of the range being copied as specified by the user.
 */
struct pagetable_move_control {
	struct vm_area_struct *old; /* Source VMA. */
	struct vm_area_struct *new; /* Destination VMA. */
	unsigned long old_addr; /* Address from which the move begins. */
	unsigned long old_end; /* Exclusive address at which old range ends. */
	unsigned long new_addr; /* Address to move page tables to. */
	unsigned long len_in; /* Bytes to remap specified by user. */

	bool need_rmap_locks; /* Do rmap locks need to be taken? */
	bool for_stack; /* Is this an early temp stack being moved? */
};

#define PAGETABLE_MOVE(name, old_, new_, old_addr_, new_addr_, len_)	\
	struct pagetable_move_control name = {				\
		.old = old_,						\
		.new = new_,						\
		.old_addr = old_addr_,					\
		.old_end = (old_addr_) + (len_),			\
		.new_addr = new_addr_,					\
		.len_in = len_,						\
	}

/*
 * The set of flags that only affect watermark checking and reclaim
 * behaviour. This is used by the MM to obey the caller constraints
 * about IO, FS and watermark checking while ignoring placement
 * hints such as HIGHMEM usage.
 */
#define GFP_RECLAIM_MASK (__GFP_RECLAIM|__GFP_HIGH|__GFP_IO|__GFP_FS|\
			__GFP_NOWARN|__GFP_RETRY_MAYFAIL|__GFP_NOFAIL|\
			__GFP_NORETRY|__GFP_MEMALLOC|__GFP_NOMEMALLOC|\
			__GFP_NOLOCKDEP)

/* The GFP flags allowed during early boot */
#define GFP_BOOT_MASK (__GFP_BITS_MASK & ~(__GFP_RECLAIM|__GFP_IO|__GFP_FS))

/* Control allocation cpuset and node placement constraints */
#define GFP_CONSTRAINT_MASK (__GFP_HARDWALL|__GFP_THISNODE)

/* Do not use these with a slab allocator */
#define GFP_SLAB_BUG_MASK (__GFP_DMA32|__GFP_HIGHMEM|~__GFP_BITS_MASK)

/*
 * Different from WARN_ON_ONCE(), no warning will be issued
 * when we specify __GFP_NOWARN.
 */
#define WARN_ON_ONCE_GFP(cond, gfp)	({				\
	static bool __section(".data..once") __warned;			\
	int __ret_warn_once = !!(cond);					\
									\
	if (unlikely(!(gfp & __GFP_NOWARN) && __ret_warn_once && !__warned)) { \
		__warned = true;					\
		WARN_ON(1);						\
	}								\
	unlikely(__ret_warn_once);					\
})

void page_writeback_init(void);

/*
 * If a 16GB hugetlb folio were mapped by PTEs of all of its 4kB pages,
 * its nr_pages_mapped would be 0x400000: choose the ENTIRELY_MAPPED bit
 * above that range, instead of 2*(PMD_SIZE/PAGE_SIZE).  Hugetlb currently
 * leaves nr_pages_mapped at 0, but avoid surprise if it participates later.
 */
#define ENTIRELY_MAPPED		0x800000
#define FOLIO_PAGES_MAPPED	(ENTIRELY_MAPPED - 1)

/*
 * Flags passed to __show_mem() and show_free_areas() to suppress output in
 * various contexts.
 */
#define SHOW_MEM_FILTER_NODES		(0x0001u)	/* disallowed nodes */

/*
 * How many individual pages have an elevated _mapcount.  Excludes
 * the folio's entire_mapcount.
 *
 * Don't use this function outside of debugging code.
 */
static inline int folio_nr_pages_mapped(const struct folio *folio)
{
	if (IS_ENABLED(CONFIG_NO_PAGE_MAPCOUNT))
		return -1;
	return atomic_read(&folio->_nr_pages_mapped) & FOLIO_PAGES_MAPPED;
}

/*
 * Retrieve the first entry of a folio based on a provided entry within the
 * folio. We cannot rely on folio->swap as there is no guarantee that it has
 * been initialized. Used for calling arch_swap_restore()
 */
static inline swp_entry_t folio_swap(swp_entry_t entry,
		const struct folio *folio)
{
	swp_entry_t swap = {
		.val = ALIGN_DOWN(entry.val, folio_nr_pages(folio)),
	};

	return swap;
}

static inline void *folio_raw_mapping(const struct folio *folio)
{
	unsigned long mapping = (unsigned long)folio->mapping;

	return (void *)(mapping & ~FOLIO_MAPPING_FLAGS);
}

/*
 * This is a file-backed mapping, and is about to be memory mapped - invoke its
 * mmap hook and safely handle error conditions. On error, VMA hooks will be
 * mutated.
 *
 * @file: File which backs the mapping.
 * @vma:  VMA which we are mapping.
 *
 * Returns: 0 if success, error otherwise.
 */
static inline int mmap_file(struct file *file, struct vm_area_struct *vma)
{
	int err = vfs_mmap(file, vma);

	if (likely(!err))
		return 0;

	/*
	 * OK, we tried to call the file hook for mmap(), but an error
	 * arose. The mapping is in an inconsistent state and we must not invoke
	 * any further hooks on it.
	 */
	vma->vm_ops = &vma_dummy_vm_ops;

	return err;
}

/*
 * If the VMA has a close hook then close it, and since closing it might leave
 * it in an inconsistent state which makes the use of any hooks suspect, clear
 * them down by installing dummy empty hooks.
 */
static inline void vma_close(struct vm_area_struct *vma)
{
	if (vma->vm_ops && vma->vm_ops->close) {
		vma->vm_ops->close(vma);

		/*
		 * The mapping is in an inconsistent state, and no further hooks
		 * may be invoked upon it.
		 */
		vma->vm_ops = &vma_dummy_vm_ops;
	}
}

/* unmap_vmas is in mm/memory.c */
void unmap_vmas(struct mmu_gather *tlb, struct unmap_desc *unmap);

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

static inline int anon_vma_trylock_write(struct anon_vma *anon_vma)
{
	return down_write_trylock(&anon_vma->root->rwsem);
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

struct anon_vma *folio_get_anon_vma(const struct folio *folio);

/* Operations which modify VMAs. */
enum vma_operation {
	VMA_OP_SPLIT,
	VMA_OP_MERGE_UNFAULTED,
	VMA_OP_REMAP,
	VMA_OP_FORK,
};

int anon_vma_clone(struct vm_area_struct *dst, struct vm_area_struct *src,
	enum vma_operation operation);
int anon_vma_fork(struct vm_area_struct *vma, struct vm_area_struct *pvma);
int  __anon_vma_prepare(struct vm_area_struct *vma);
void unlink_anon_vmas(struct vm_area_struct *vma);

static inline int anon_vma_prepare(struct vm_area_struct *vma)
{
	if (likely(vma->anon_vma))
		return 0;

	return __anon_vma_prepare(vma);
}

/* Flags for folio_pte_batch(). */
typedef int __bitwise fpb_t;

/* Compare PTEs respecting the dirty bit. */
#define FPB_RESPECT_DIRTY		((__force fpb_t)BIT(0))

/* Compare PTEs respecting the soft-dirty bit. */
#define FPB_RESPECT_SOFT_DIRTY		((__force fpb_t)BIT(1))

/* Compare PTEs respecting the writable bit. */
#define FPB_RESPECT_WRITE		((__force fpb_t)BIT(2))

/*
 * Merge PTE write bits: if any PTE in the batch is writable, modify the
 * PTE at @ptentp to be writable.
 */
#define FPB_MERGE_WRITE			((__force fpb_t)BIT(3))

/*
 * Merge PTE young and dirty bits: if any PTE in the batch is young or dirty,
 * modify the PTE at @ptentp to be young or dirty, respectively.
 */
#define FPB_MERGE_YOUNG_DIRTY		((__force fpb_t)BIT(4))

static inline pte_t __pte_batch_clear_ignored(pte_t pte, fpb_t flags)
{
	if (!(flags & FPB_RESPECT_DIRTY))
		pte = pte_mkclean(pte);
	if (likely(!(flags & FPB_RESPECT_SOFT_DIRTY)))
		pte = pte_clear_soft_dirty(pte);
	if (likely(!(flags & FPB_RESPECT_WRITE)))
		pte = pte_wrprotect(pte);
	return pte_mkold(pte);
}

/**
 * folio_pte_batch_flags - detect a PTE batch for a large folio
 * @folio: The large folio to detect a PTE batch for.
 * @vma: The VMA. Only relevant with FPB_MERGE_WRITE, otherwise can be NULL.
 * @ptep: Page table pointer for the first entry.
 * @ptentp: Pointer to a COPY of the first page table entry whose flags this
 *	    function updates based on @flags if appropriate.
 * @max_nr: The maximum number of table entries to consider.
 * @flags: Flags to modify the PTE batch semantics.
 *
 * Detect a PTE batch: consecutive (present) PTEs that map consecutive
 * pages of the same large folio in a single VMA and a single page table.
 *
 * All PTEs inside a PTE batch have the same PTE bits set, excluding the PFN,
 * the accessed bit, writable bit, dirty bit (unless FPB_RESPECT_DIRTY is set)
 * and soft-dirty bit (unless FPB_RESPECT_SOFT_DIRTY is set).
 *
 * @ptep must map any page of the folio. max_nr must be at least one and
 * must be limited by the caller so scanning cannot exceed a single VMA and
 * a single page table.
 *
 * Depending on the FPB_MERGE_* flags, the pte stored at @ptentp will
 * be updated: it's crucial that a pointer to a COPY of the first
 * page table entry, obtained through ptep_get(), is provided as @ptentp.
 *
 * This function will be inlined to optimize based on the input parameters;
 * consider using folio_pte_batch() instead if applicable.
 *
 * Return: the number of table entries in the batch.
 */
static inline unsigned int folio_pte_batch_flags(struct folio *folio,
		struct vm_area_struct *vma, pte_t *ptep, pte_t *ptentp,
		unsigned int max_nr, fpb_t flags)
{
	bool any_writable = false, any_young = false, any_dirty = false;
	pte_t expected_pte, pte = *ptentp;
	unsigned int nr, cur_nr;

	VM_WARN_ON_FOLIO(!pte_present(pte), folio);
	VM_WARN_ON_FOLIO(!folio_test_large(folio) || max_nr < 1, folio);
	VM_WARN_ON_FOLIO(page_folio(pfn_to_page(pte_pfn(pte))) != folio, folio);
	/*
	 * Ensure this is a pointer to a copy not a pointer into a page table.
	 * If this is a stack value, it won't be a valid virtual address, but
	 * that's fine because it also cannot be pointing into the page table.
	 */
	VM_WARN_ON(virt_addr_valid(ptentp) && PageTable(virt_to_page(ptentp)));

	/* Limit max_nr to the actual remaining PFNs in the folio we could batch. */
	max_nr = min_t(unsigned long, max_nr,
		       folio_pfn(folio) + folio_nr_pages(folio) - pte_pfn(pte));

	nr = pte_batch_hint(ptep, pte);
	expected_pte = __pte_batch_clear_ignored(pte_advance_pfn(pte, nr), flags);
	ptep = ptep + nr;

	while (nr < max_nr) {
		pte = ptep_get(ptep);

		if (!pte_same(__pte_batch_clear_ignored(pte, flags), expected_pte))
			break;

		if (flags & FPB_MERGE_WRITE)
			any_writable |= pte_write(pte);
		if (flags & FPB_MERGE_YOUNG_DIRTY) {
			any_young |= pte_young(pte);
			any_dirty |= pte_dirty(pte);
		}

		cur_nr = pte_batch_hint(ptep, pte);
		expected_pte = pte_advance_pfn(expected_pte, cur_nr);
		ptep += cur_nr;
		nr += cur_nr;
	}

	if (any_writable)
		*ptentp = pte_mkwrite(*ptentp, vma);
	if (any_young)
		*ptentp = pte_mkyoung(*ptentp);
	if (any_dirty)
		*ptentp = pte_mkdirty(*ptentp);

	return min(nr, max_nr);
}

unsigned int folio_pte_batch(struct folio *folio, pte_t *ptep, pte_t pte,
		unsigned int max_nr);

/**
 * pte_move_swp_offset - Move the swap entry offset field of a swap pte
 *	 forward or backward by delta
 * @pte: The initial pte state; must be a swap entry
 * @delta: The direction and the offset we are moving; forward if delta
 *	 is positive; backward if delta is negative
 *
 * Moves the swap offset, while maintaining all other fields, including
 * swap type, and any swp pte bits. The resulting pte is returned.
 */
static inline pte_t pte_move_swp_offset(pte_t pte, long delta)
{
	const softleaf_t entry = softleaf_from_pte(pte);
	pte_t new = __swp_entry_to_pte(__swp_entry(swp_type(entry),
						   (swp_offset(entry) + delta)));

	if (pte_swp_soft_dirty(pte))
		new = pte_swp_mksoft_dirty(new);
	if (pte_swp_exclusive(pte))
		new = pte_swp_mkexclusive(new);
	if (pte_swp_uffd(pte))
		new = pte_swp_mkuffd(new);

	return new;
}


/**
 * pte_next_swp_offset - Increment the swap entry offset field of a swap pte.
 * @pte: The initial pte state; must be a swap entry.
 *
 * Increments the swap offset, while maintaining all other fields, including
 * swap type, and any swp pte bits. The resulting pte is returned.
 */
static inline pte_t pte_next_swp_offset(pte_t pte)
{
	return pte_move_swp_offset(pte, 1);
}

/**
 * swap_pte_batch - detect a PTE batch for a set of contiguous swap entries
 * @start_ptep: Page table pointer for the first entry.
 * @max_nr: The maximum number of table entries to consider.
 * @pte: Page table entry for the first entry.
 *
 * Detect a batch of contiguous swap entries: consecutive (non-present) PTEs
 * containing swap entries all with consecutive offsets and targeting the same
 * swap type, all with matching swp pte bits.
 *
 * max_nr must be at least one and must be limited by the caller so scanning
 * cannot exceed a single page table.
 *
 * Return: the number of table entries in the batch.
 */
static inline int swap_pte_batch(pte_t *start_ptep, int max_nr, pte_t pte)
{
	pte_t expected_pte = pte_next_swp_offset(pte);
	const pte_t *end_ptep = start_ptep + max_nr;
	pte_t *ptep = start_ptep + 1;

	VM_WARN_ON(max_nr < 1);
	VM_WARN_ON(!softleaf_is_swap(softleaf_from_pte(pte)));

	while (ptep < end_ptep) {
		pte = ptep_get(ptep);

		if (!pte_same(pte, expected_pte))
			break;
		expected_pte = pte_next_swp_offset(expected_pte);
		ptep++;
	}

	return ptep - start_ptep;
}
#endif /* CONFIG_MMU */

void __acct_reclaim_writeback(pg_data_t *pgdat, struct folio *folio,
						int nr_throttled);
static inline void acct_reclaim_writeback(struct folio *folio)
{
	pg_data_t *pgdat = folio_pgdat(folio);
	int nr_throttled = atomic_read(&pgdat->nr_writeback_throttled);

	if (nr_throttled)
		__acct_reclaim_writeback(pgdat, folio, nr_throttled);
}

static inline void wake_throttle_isolated(pg_data_t *pgdat)
{
	wait_queue_head_t *wqh;

	wqh = &pgdat->reclaim_wait[VMSCAN_THROTTLE_ISOLATED];
	if (waitqueue_active(wqh))
		wake_up(wqh);
}

vm_fault_t __vmf_anon_prepare(struct vm_fault *vmf);
static inline vm_fault_t vmf_anon_prepare(struct vm_fault *vmf)
{
	vm_fault_t ret = __vmf_anon_prepare(vmf);

	if (unlikely(ret & VM_FAULT_RETRY))
		vma_end_read(vmf->vma);
	return ret;
}

vm_fault_t do_swap_page(struct vm_fault *vmf);
void folio_rotate_reclaimable(struct folio *folio);
bool __folio_end_writeback(struct folio *folio);
void deactivate_file_folio(struct folio *folio);
void folio_activate(struct folio *folio);

void free_pgtables(struct mmu_gather *tlb, struct unmap_desc *desc);

void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte);

/**
 * sync_with_folio_pmd_zap - sync with concurrent zapping of a folio PMD
 * @mm: The mm_struct.
 * @pmdp: Pointer to the pmd that was found to be pmd_none().
 *
 * When we find a pmd_none() while unmapping a folio without holding the PTL,
 * zap_huge_pmd() may have cleared the PMD but not yet modified the folio to
 * indicate that it's unmapped. Skipping the PMD without synchronization could
 * make folio unmapping code assume that unmapping failed.
 *
 * Wait for concurrent zapping to complete by grabbing the PTL.
 */
static inline void sync_with_folio_pmd_zap(struct mm_struct *mm, pmd_t *pmdp)
{
	spinlock_t *ptl = pmd_lock(mm, pmdp);

	spin_unlock(ptl);
}

struct zap_details;
void zap_vma_range_batched(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long addr,
		unsigned long size, struct zap_details *details);
int zap_vma_for_reaping(struct vm_area_struct *vma);
int folio_unmap_invalidate(struct address_space *mapping, struct folio *folio,
			   gfp_t gfp);

void page_cache_ra_order(struct readahead_control *, struct file_ra_state *);
void force_page_cache_ra(struct readahead_control *, unsigned long nr);
static inline void force_page_cache_readahead(struct address_space *mapping,
		struct file *file, pgoff_t index, unsigned long nr_to_read)
{
	DEFINE_READAHEAD(ractl, file, &file->f_ra, mapping, index);
	force_page_cache_ra(&ractl, nr_to_read);
}

unsigned find_lock_entries(struct address_space *mapping, pgoff_t *start,
		pgoff_t end, struct folio_batch *fbatch, pgoff_t *indices);
unsigned find_get_entries(struct address_space *mapping, pgoff_t *start,
		pgoff_t end, struct folio_batch *fbatch, pgoff_t *indices);
int truncate_inode_folio(struct address_space *mapping, struct folio *folio);
bool truncate_inode_partial_folio(struct folio *folio, loff_t start,
		loff_t end);
long mapping_evict_folio(struct address_space *mapping, struct folio *folio);
unsigned long mapping_try_invalidate(struct address_space *mapping,
		pgoff_t start, pgoff_t end, unsigned long *nr_failed);

/**
 * folio_evictable - Test whether a folio is evictable.
 * @folio: The folio to test.
 *
 * Test whether @folio is evictable -- i.e., should be placed on
 * active/inactive lists vs unevictable list.
 *
 * Reasons folio might not be evictable:
 * 1. folio's mapping marked unevictable
 * 2. One of the pages in the folio is part of an mlocked VMA
 */
static inline bool folio_evictable(struct folio *folio)
{
	bool ret;

	/* Prevent address_space of inode and swap cache from being freed */
	rcu_read_lock();
	ret = !mapping_unevictable(folio_mapping(folio)) &&
			!folio_test_mlocked(folio);
	rcu_read_unlock();
	return ret;
}

/*
 * Turn a non-refcounted page (->_refcount == 0) into refcounted with
 * a count of one.
 */
static inline void set_page_refcounted(struct page *page)
{
	VM_BUG_ON_PAGE(PageTail(page), page);
	VM_BUG_ON_PAGE(page_ref_count(page), page);
	set_page_count(page, 1);
}

static inline void set_pages_refcounted(struct page *page, unsigned long nr_pages)
{
	unsigned long pfn = page_to_pfn(page);

	for (; nr_pages--; pfn++)
		set_page_refcounted(pfn_to_page(pfn));
}

/*
 * Return true if a folio needs ->release_folio() calling upon it.
 */
static inline bool folio_needs_release(struct folio *folio)
{
	struct address_space *mapping = folio_mapping(folio);

	return folio_has_private(folio) ||
		(mapping && mapping_release_always(mapping));
}

extern unsigned long highest_memmap_pfn;

/*
 * Maximum number of reclaim retries without progress before the OOM
 * killer is consider the only way forward.
 */
#define MAX_RECLAIM_RETRIES 16

/*
 * in mm/vmscan.c:
 */
bool folio_isolate_lru(struct folio *folio);
void folio_putback_lru(struct folio *folio);
extern void reclaim_throttle(pg_data_t *pgdat, enum vmscan_throttle_state reason);
int user_proactive_reclaim(char *buf,
			   struct mem_cgroup *memcg, pg_data_t *pgdat);

/*
 * in mm/rmap.c:
 */
pmd_t *mm_find_pmd(struct mm_struct *mm, unsigned long address);

/*
 * in mm/khugepaged.c
 */
void set_recommended_min_free_kbytes(void);

/*
 * in mm/page_alloc.c
 */
#define K(x) ((x) << (PAGE_SHIFT-10))

extern char * const zone_names[MAX_NR_ZONES];

extern int min_free_kbytes;
extern int defrag_mode;

void setup_per_zone_wmarks(void);
void calculate_min_free_kbytes(void);
int __meminit init_per_zone_wmark_min(void);

extern int __isolate_free_page(struct page *page, unsigned int order);
extern void __putback_isolated_page(struct page *page, unsigned int order,
				    int mt);

/*
 * This will have no effect, other than possibly generating a warning, if the
 * caller passes in a non-large folio.
 */
static inline void folio_set_order(struct folio *folio, unsigned int order)
{
	if (WARN_ON_ONCE(!order || !folio_test_large(folio)))
		return;
	VM_WARN_ON_ONCE(order > MAX_FOLIO_ORDER);

	folio->_flags_1 = (folio->_flags_1 & ~0xffUL) | order;
#ifdef NR_PAGES_IN_LARGE_FOLIO
	folio->_nr_pages = 1U << order;
#endif
}

bool __folio_unqueue_deferred_split(struct folio *folio);
static inline bool folio_unqueue_deferred_split(struct folio *folio)
{
	if (folio_order(folio) <= 1 || !folio_test_large_rmappable(folio))
		return false;

	/*
	 * At this point, there is no one trying to add the folio to
	 * deferred_list. If folio is not in deferred_list, it's safe
	 * to check without acquiring the list_lru lock.
	 */
	if (data_race(list_empty(&folio->_deferred_list)))
		return false;

	return __folio_unqueue_deferred_split(folio);
}

static inline struct folio *page_rmappable_folio(struct page *page)
{
	struct folio *folio = (struct folio *)page;

	if (folio && folio_test_large(folio))
		folio_set_large_rmappable(folio);
	return folio;
}

static inline void prep_compound_head(struct page *page, unsigned int order)
{
	struct folio *folio = (struct folio *)page;

	folio_set_order(folio, order);
	atomic_set(&folio->_large_mapcount, -1);
	if (IS_ENABLED(CONFIG_PAGE_MAPCOUNT))
		atomic_set(&folio->_nr_pages_mapped, 0);
	if (IS_ENABLED(CONFIG_MM_ID)) {
		folio->_mm_ids = 0;
		folio->_mm_id_mapcount[0] = -1;
		folio->_mm_id_mapcount[1] = -1;
	}
	if (IS_ENABLED(CONFIG_64BIT) || order > 1) {
		atomic_set(&folio->_pincount, 0);
		atomic_set(&folio->_entire_mapcount, -1);
	}
	if (order > 1)
		INIT_LIST_HEAD(&folio->_deferred_list);
}

static inline void prep_compound_tail(struct page *tail,
		const struct page *head, unsigned int order)
{
	tail->mapping = TAIL_MAPPING;
	set_compound_head(tail, head, order);
	VM_WARN_ON_ONCE(tail->private);
}

static inline void init_compound_tail(struct page *tail,
		const struct page *head, unsigned int order, struct zone *zone)
{
	atomic_set(&tail->_mapcount, -1);
	set_page_node(tail, zone_to_nid(zone));
	set_page_zone(tail, zone_idx(zone));
	prep_compound_tail(tail, head, order);
}

#if defined CONFIG_COMPACTION || defined CONFIG_CMA

/*
 * in mm/compaction.c
 */
/*
 * compact_control is used to track pages being migrated and the free pages
 * they are being migrated to during memory compaction. The free_pfn starts
 * at the end of a zone and migrate_pfn begins at the start. Movable pages
 * are moved to the end of a zone during a compaction run and the run
 * completes when free_pfn <= migrate_pfn
 */
struct compact_control {
	struct list_head freepages[NR_PAGE_ORDERS];	/* List of free pages to migrate to */
	struct list_head migratepages;	/* List of pages being migrated */
	unsigned int nr_freepages;	/* Number of isolated free pages */
	unsigned int nr_migratepages;	/* Number of pages to migrate */
	unsigned long free_pfn;		/* isolate_freepages search base */
	/*
	 * Acts as an in/out parameter to page isolation for migration.
	 * isolate_migratepages uses it as a search base.
	 * isolate_migratepages_block will update the value to the next pfn
	 * after the last isolated one.
	 */
	unsigned long migrate_pfn;
	unsigned long fast_start_pfn;	/* a pfn to start linear scan from */
	struct zone *zone;
	unsigned long total_migrate_scanned;
	unsigned long total_free_scanned;
	unsigned short fast_search_fail;/* failures to use free list searches */
	short search_order;		/* order to start a fast search at */
	const gfp_t gfp_mask;		/* gfp mask of a direct compactor */
	int order;			/* order a direct compactor needs */
	int migratetype;		/* migratetype of direct compactor */
	const unsigned int alloc_flags;	/* alloc flags of a direct compactor */
	const int highest_zoneidx;	/* zone index of a direct compactor */
	enum migrate_mode mode;		/* Async or sync migration mode */
	bool ignore_skip_hint;		/* Scan blocks even if marked skip */
	bool no_set_skip_hint;		/* Don't mark blocks for skipping */
	bool ignore_block_suitable;	/* Scan blocks considered unsuitable */
	bool direct_compaction;		/* False from kcompactd or /proc/... */
	bool proactive_compaction;	/* kcompactd proactive compaction */
	bool whole_zone;		/* Whole zone should/has been scanned */
	bool contended;			/* Signal lock contention */
	bool finish_pageblock;		/* Scan the remainder of a pageblock. Used
					 * when there are potentially transient
					 * isolation or migration failures to
					 * ensure forward progress.
					 */
	bool alloc_contig;		/* alloc_contig_range allocation */
};

/*
 * Used in direct compaction when a page should be taken from the freelists
 * immediately when one is created during the free path.
 */
struct capture_control {
	struct compact_control *cc;
	struct page *page;
};

unsigned long
isolate_freepages_range(struct compact_control *cc,
			unsigned long start_pfn, unsigned long end_pfn);
int
isolate_migratepages_range(struct compact_control *cc,
			   unsigned long low_pfn, unsigned long end_pfn);

#endif /* CONFIG_COMPACTION || CONFIG_CMA */

struct cma;

#ifdef CONFIG_CMA
bool cma_validate_zones(struct cma *cma);
void *cma_reserve_early(struct cma *cma, unsigned long size);
#else
static inline bool cma_validate_zones(struct cma *cma)
{
	return false;
}
static inline void *cma_reserve_early(struct cma *cma, unsigned long size)
{
	return NULL;
}
#endif

/* mm/util.c */
struct anon_vma *folio_anon_vma(const struct folio *folio);

#ifdef CONFIG_MMU
void unmap_mapping_folio(struct folio *folio);
extern long populate_vma_page_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, int *locked);
extern long faultin_page_range(struct mm_struct *mm, unsigned long start,
		unsigned long end, bool write, int *locked);
bool mlock_future_ok(const struct mm_struct *mm, bool is_vma_locked,
		unsigned long bytes);

/*
 * NOTE: This function can't tell whether the folio is "fully mapped" in the
 * range.
 * "fully mapped" means all the pages of folio is associated with the page
 * table of range while this function just check whether the folio range is
 * within the range [start, end). Function caller needs to do page table
 * check if it cares about the page table association.
 *
 * Typical usage (like mlock or madvise) is:
 * Caller knows at least 1 page of folio is associated with page table of VMA
 * and the range [start, end) is intersect with the VMA range. Caller wants
 * to know whether the folio is fully associated with the range. It calls
 * this function to check whether the folio is in the range first. Then checks
 * the page table to know whether the folio is fully mapped to the range.
 */
static inline bool
folio_within_range(struct folio *folio, struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	pgoff_t pgoff, addr;
	unsigned long vma_pglen = vma_pages(vma);

	VM_WARN_ON_FOLIO(folio_test_ksm(folio), folio);
	if (start > end)
		return false;

	if (start < vma->vm_start)
		start = vma->vm_start;

	if (end > vma->vm_end)
		end = vma->vm_end;

	pgoff = folio_pgoff(folio);

	/* if folio start address is not in vma range */
	if (!in_range(pgoff, vma->vm_pgoff, vma_pglen))
		return false;

	addr = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);

	return !(addr < start || end - addr < folio_size(folio));
}

static inline bool
folio_within_vma(struct folio *folio, struct vm_area_struct *vma)
{
	return folio_within_range(folio, vma, vma->vm_start, vma->vm_end);
}

/*
 * mlock_vma_folio() and munlock_vma_folio():
 * should be called with vma's mmap_lock held for read or write,
 * under page table lock for the pte/pmd being added or removed.
 *
 * mlock is usually called at the end of folio_add_*_rmap_*(), munlock at
 * the end of folio_remove_rmap_*(); but new anon folios are managed by
 * folio_add_lru_vma() calling mlock_new_folio().
 */
void mlock_folio(struct folio *folio);
static inline void mlock_vma_folio(struct folio *folio,
				struct vm_area_struct *vma)
{
	/*
	 * The VM_SPECIAL check here serves two purposes.
	 * 1) VM_IO check prevents migration from double-counting during mlock.
	 * 2) Although mmap_region() and mlock_fixup() take care that VM_LOCKED
	 *    is never left set on a VM_SPECIAL vma, there is an interval while
	 *    file->f_op->mmap() is using vm_insert_page(s), when VM_LOCKED may
	 *    still be set while VM_SPECIAL bits are added: so ignore it then.
	 */
	if (unlikely((vma->vm_flags & (VM_LOCKED|VM_SPECIAL)) == VM_LOCKED))
		mlock_folio(folio);
}

void munlock_folio(struct folio *folio);
static inline void munlock_vma_folio(struct folio *folio,
					struct vm_area_struct *vma)
{
	/*
	 * munlock if the function is called. Ideally, we should only
	 * do munlock if any page of folio is unmapped from VMA and
	 * cause folio not fully mapped to VMA.
	 *
	 * But it's not easy to confirm that's the situation. So we
	 * always munlock the folio and page reclaim will correct it
	 * if it's wrong.
	 */
	if (unlikely(vma->vm_flags & VM_LOCKED))
		munlock_folio(folio);
}

void mlock_new_folio(struct folio *folio);
bool need_mlock_drain(int cpu);
void mlock_drain_local(void);
void mlock_drain_remote(int cpu);

extern pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma);

/**
 * vma_address - Find the virtual address a page range is mapped at
 * @vma: The vma which maps this object.
 * @pgoff: The page offset within its object.
 * @nr_pages: The number of pages to consider.
 *
 * If any page in this range is mapped by this VMA, return the first address
 * where any of these pages appear.  Otherwise, return -EFAULT.
 */
static inline unsigned long vma_address(const struct vm_area_struct *vma,
		pgoff_t pgoff, unsigned long nr_pages)
{
	unsigned long address;

	if (pgoff >= vma->vm_pgoff) {
		address = vma->vm_start +
			((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		/* Check for address beyond vma (or wrapped through 0?) */
		if (address < vma->vm_start || address >= vma->vm_end)
			address = -EFAULT;
	} else if (pgoff + nr_pages - 1 >= vma->vm_pgoff) {
		/* Test above avoids possibility of wrap to 0 on 32-bit */
		address = vma->vm_start;
	} else {
		address = -EFAULT;
	}
	return address;
}

/*
 * Then at what user virtual address will none of the range be found in vma?
 * Assumes that vma_address() already returned a good starting address.
 */
static inline unsigned long vma_address_end(struct page_vma_mapped_walk *pvmw)
{
	struct vm_area_struct *vma = pvmw->vma;
	pgoff_t pgoff;
	unsigned long address;

	/* Common case, plus ->pgoff is invalid for KSM */
	if (pvmw->nr_pages == 1)
		return pvmw->address + PAGE_SIZE;

	pgoff = pvmw->pgoff + pvmw->nr_pages;
	address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	/* Check for address beyond vma (or wrapped through 0?) */
	if (address < vma->vm_start || address > vma->vm_end)
		address = vma->vm_end;
	return address;
}

static inline struct file *maybe_unlock_mmap_for_io(struct vm_fault *vmf,
						    struct file *fpin)
{
	int flags = vmf->flags;

	if (fpin)
		return fpin;

	/*
	 * FAULT_FLAG_RETRY_NOWAIT means we don't want to wait on page locks or
	 * anything, so we only pin the file and drop the mmap_lock if only
	 * FAULT_FLAG_ALLOW_RETRY is set, while this is the first attempt.
	 */
	if (fault_flag_allow_retry_first(flags) &&
	    !(flags & FAULT_FLAG_RETRY_NOWAIT)) {
		fpin = get_file(vmf->vma->vm_file);
		release_fault_lock(vmf);
	}
	return fpin;
}

static inline bool vma_supports_mlock(const struct vm_area_struct *vma)
{
	if (vma_test_any_mask(vma, VMA_SPECIAL_FLAGS))
		return false;
	if (vma_test_single_mask(vma, VMA_DROPPABLE))
		return false;
	if (vma_is_dax(vma) || is_vm_hugetlb_page(vma))
		return false;
	return vma != get_gate_vma(current->mm);
}

#else /* !CONFIG_MMU */
static inline void unmap_mapping_folio(struct folio *folio) { }
static inline void mlock_new_folio(struct folio *folio) { }
static inline bool need_mlock_drain(int cpu) { return false; }
static inline void mlock_drain_local(void) { }
static inline void mlock_drain_remote(int cpu) { }
static inline void vunmap_range_noflush(unsigned long start, unsigned long end)
{
}
#endif /* !CONFIG_MMU */

#define NODE_RECLAIM_NOSCAN	-2
#define NODE_RECLAIM_FULL	-1
#define NODE_RECLAIM_SOME	0
#define NODE_RECLAIM_SUCCESS	1

#ifdef CONFIG_NUMA
extern int node_reclaim_mode;

extern int node_reclaim(struct pglist_data *, gfp_t, unsigned int);
extern int find_next_best_node(int node, nodemask_t *used_node_mask);
#else
#define node_reclaim_mode 0

static inline int node_reclaim(struct pglist_data *pgdat, gfp_t mask,
				unsigned int order)
{
	return NODE_RECLAIM_NOSCAN;
}
static inline int find_next_best_node(int node, nodemask_t *used_node_mask)
{
	return NUMA_NO_NODE;
}
#endif

static inline bool node_reclaim_enabled(void)
{
	/* Is any node_reclaim_mode bit set? */
	return node_reclaim_mode & (RECLAIM_ZONE|RECLAIM_WRITE|RECLAIM_UNMAP);
}

/*
 * mm/memory-failure.c
 */
#ifdef CONFIG_MEMORY_FAILURE
int unmap_poisoned_folio(struct folio *folio, unsigned long pfn, bool must_kill);
void shake_folio(struct folio *folio);
typedef int hwpoison_filter_func_t(struct page *p);
void hwpoison_filter_register(hwpoison_filter_func_t *filter);
void hwpoison_filter_unregister(void);

#define MAGIC_HWPOISON	0x48575053U	/* HWPS */
void SetPageHWPoisonTakenOff(struct page *page);
void ClearPageHWPoisonTakenOff(struct page *page);
bool take_page_off_buddy(struct page *page);
bool put_page_back_buddy(struct page *page);
struct task_struct *task_early_kill(struct task_struct *tsk, int force_early);
void add_to_kill_ksm(struct task_struct *tsk, const struct page *p,
		     struct vm_area_struct *vma, struct list_head *to_kill,
		     unsigned long ksm_addr);
unsigned long page_mapped_in_vma(const struct page *page,
		struct vm_area_struct *vma);

#else
static inline int unmap_poisoned_folio(struct folio *folio, unsigned long pfn, bool must_kill)
{
	return -EBUSY;
}
#endif

extern unsigned long  __must_check vm_mmap_pgoff(struct file *, unsigned long,
        unsigned long, unsigned long,
        unsigned long, unsigned long);

unsigned long reclaim_pages(struct list_head *folio_list);
unsigned int reclaim_clean_pages_from_list(struct zone *zone,
					    struct list_head *folio_list);

enum ttu_flags;
struct tlbflush_unmap_batch;


/*
 * only for MM internal work items which do not depend on
 * any allocations or locks which might depend on allocations
 */
extern struct workqueue_struct *mm_percpu_wq;

#ifdef CONFIG_ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH
void try_to_unmap_flush(void);
void try_to_unmap_flush_dirty(void);
void flush_tlb_batched_pending(struct mm_struct *mm);
#else
static inline void try_to_unmap_flush(void)
{
}
static inline void try_to_unmap_flush_dirty(void)
{
}
static inline void flush_tlb_batched_pending(struct mm_struct *mm)
{
}
#endif /* CONFIG_ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH */

extern const struct trace_print_flags pageflag_names[];
extern const struct trace_print_flags vmaflag_names[];
extern const struct trace_print_flags gfpflag_names[];

void setup_zone_pageset(struct zone *zone);

struct migration_target_control {
	int nid;		/* preferred node id */
	nodemask_t *nmask;
	gfp_t gfp_mask;
	enum migrate_reason reason;
};

/*
 * mm/filemap.c
 */
size_t splice_folio_into_pipe(struct pipe_inode_info *pipe,
			      struct folio *folio, loff_t fpos, size_t size);

/*
 * mm/vmalloc.c
 */
#ifdef CONFIG_MMU
void __init vmalloc_init(void);
int __must_check vmap_pages_range_noflush(unsigned long addr, unsigned long end,
	pgprot_t prot, struct page **pages, unsigned int page_shift, gfp_t gfp_mask);
unsigned int get_vm_area_page_order(struct vm_struct *vm);
#else
static inline void vmalloc_init(void)
{
}

static inline
int __must_check vmap_pages_range_noflush(unsigned long addr, unsigned long end,
	pgprot_t prot, struct page **pages, unsigned int page_shift, gfp_t gfp_mask)
{
	return -EINVAL;
}
#endif

void clear_vm_uninitialized_flag(struct vm_struct *vm);

int __must_check __vmap_pages_range_noflush(unsigned long addr,
			       unsigned long end, pgprot_t prot,
			       struct page **pages, unsigned int page_shift);

void vunmap_range_noflush(unsigned long start, unsigned long end);

void __vunmap_range_noflush(unsigned long start, unsigned long end);

static inline bool vma_is_single_threaded_private(struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_SHARED)
		return false;

	return atomic_read(&vma->vm_mm->mm_users) == 1;
}

#ifdef CONFIG_NUMA_BALANCING
bool folio_can_map_prot_numa(struct folio *folio, struct vm_area_struct *vma,
		bool is_private_single_threaded);

#else
static inline bool folio_can_map_prot_numa(struct folio *folio,
		struct vm_area_struct *vma, bool is_private_single_threaded)
{
	return false;
}
#endif

int numa_migrate_check(struct folio *folio, struct vm_fault *vmf,
		      unsigned long addr, int *flags, bool writable,
		      int *last_cpupid);

void free_zone_device_folio(struct folio *folio);
int migrate_device_coherent_folio(struct folio *folio);

struct vm_struct *__get_vm_area_node(unsigned long size,
				     unsigned long align, unsigned long shift,
				     unsigned long vm_flags, unsigned long start,
				     unsigned long end, int node, gfp_t gfp_mask,
				     const void *caller);

/*
 * mm/gup.c
 */
int __must_check try_grab_folio(struct folio *folio, int refs,
				unsigned int flags);

/*
 * mm/huge_memory.c
 */
void touch_pud(struct vm_area_struct *vma, unsigned long addr,
	       pud_t *pud, bool write);
bool touch_pmd(struct vm_area_struct *vma, unsigned long addr,
	       pmd_t *pmd, bool write);

/*
 * Parses a string with mem suffixes into its order. Useful to parse kernel
 * parameters.
 */
static inline int get_order_from_str(const char *size_str,
				     unsigned long valid_orders)
{
	unsigned long size;
	char *endptr;
	int order;

	size = memparse(size_str, &endptr);

	if (!is_power_of_2(size))
		return -EINVAL;
	order = get_order(size);
	if (BIT(order) & ~valid_orders)
		return -EINVAL;

	return order;
}

enum {
	/* mark page accessed */
	FOLL_TOUCH = 1 << 16,
	/* a retry, previous pass started an IO */
	FOLL_TRIED = 1 << 17,
	/* we are working on non-current tsk/mm */
	FOLL_REMOTE = 1 << 18,
	/* pages must be released via unpin_user_page */
	FOLL_PIN = 1 << 19,
	/* gup_fast: prevent fall-back to slow gup */
	FOLL_FAST_ONLY = 1 << 20,
	/* allow unlocking the mmap lock */
	FOLL_UNLOCKABLE = 1 << 21,
	/* VMA lookup+checks compatible with MADV_POPULATE_(READ|WRITE) */
	FOLL_MADV_POPULATE = 1 << 22,
};

#define INTERNAL_GUP_FLAGS (FOLL_TOUCH | FOLL_TRIED | FOLL_REMOTE | FOLL_PIN | \
			    FOLL_FAST_ONLY | FOLL_UNLOCKABLE | \
			    FOLL_MADV_POPULATE)

/*
 * Indicates for which pages that are write-protected in the page table,
 * whether GUP has to trigger unsharing via FAULT_FLAG_UNSHARE such that the
 * GUP pin will remain consistent with the pages mapped into the page tables
 * of the MM.
 *
 * Temporary unmapping of PageAnonExclusive() pages or clearing of
 * PageAnonExclusive() has to protect against concurrent GUP:
 * * Ordinary GUP: Using the PT lock
 * * GUP-fast and fork(): mm->write_protect_seq
 * * GUP-fast and KSM or temporary unmapping (swap, migration): see
 *    folio_try_share_anon_rmap_*()
 *
 * Must be called with the (sub)page that's actually referenced via the
 * page table entry, which might not necessarily be the head page for a
 * PTE-mapped THP.
 *
 * If the vma is NULL, we're coming from the GUP-fast path and might have
 * to fallback to the slow path just to lookup the vma.
 */
static inline bool gup_must_unshare(struct vm_area_struct *vma,
				    unsigned int flags, struct page *page)
{
	/*
	 * FOLL_WRITE is implicitly handled correctly as the page table entry
	 * has to be writable -- and if it references (part of) an anonymous
	 * folio, that part is required to be marked exclusive.
	 */
	if ((flags & (FOLL_WRITE | FOLL_PIN)) != FOLL_PIN)
		return false;
	/*
	 * Note: PageAnon(page) is stable until the page is actually getting
	 * freed.
	 */
	if (!PageAnon(page)) {
		/*
		 * We only care about R/O long-term pining: R/O short-term
		 * pinning does not have the semantics to observe successive
		 * changes through the process page tables.
		 */
		if (!(flags & FOLL_LONGTERM))
			return false;

		/* We really need the vma ... */
		if (!vma)
			return true;

		/*
		 * ... because we only care about writable private ("COW")
		 * mappings where we have to break COW early.
		 */
		return is_cow_mapping(vma->vm_flags);
	}

	/* Paired with a memory barrier in folio_try_share_anon_rmap_*(). */
	if (IS_ENABLED(CONFIG_HAVE_GUP_FAST))
		smp_rmb();

	/*
	 * Note that KSM pages cannot be exclusive, and consequently,
	 * cannot get pinned.
	 */
	return !PageAnonExclusive(page);
}


static __always_inline void vma_set_range(struct vm_area_struct *vma,
					  unsigned long start, unsigned long end,
					  pgoff_t pgoff)
{
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
}

static inline bool vma_soft_dirty_enabled(struct vm_area_struct *vma)
{
	/*
	 * NOTE: we must check this before VM_SOFTDIRTY on soft-dirty
	 * enablements, because when without soft-dirty being compiled in,
	 * VM_SOFTDIRTY is defined as 0x0, then !(vm_flags & VM_SOFTDIRTY)
	 * will be constantly true.
	 */
	if (!pgtable_supports_soft_dirty())
		return false;

	/*
	 * Soft-dirty is kind of special: its tracking is enabled when the
	 * vma flags not set.
	 */
	return !(vma->vm_flags & VM_SOFTDIRTY);
}

static inline bool pmd_needs_soft_dirty_wp(struct vm_area_struct *vma, pmd_t pmd)
{
	return vma_soft_dirty_enabled(vma) && !pmd_soft_dirty(pmd);
}

static inline bool pte_needs_soft_dirty_wp(struct vm_area_struct *vma, pte_t pte)
{
	return vma_soft_dirty_enabled(vma) && !pte_soft_dirty(pte);
}

/* shrinker related functions */
unsigned long shrink_slab(gfp_t gfp_mask, int nid, struct mem_cgroup *memcg,
			  int priority);

int shmem_add_to_page_cache(struct folio *folio,
			    struct address_space *mapping,
			    pgoff_t index, void *expected, gfp_t gfp);
int shmem_inode_acct_blocks(struct inode *inode, long pages);
bool shmem_recalc_inode(struct inode *inode, long alloced, long swapped);

#ifdef CONFIG_SHRINKER_DEBUG
static inline __printf(2, 0) int shrinker_debugfs_name_alloc(
			struct shrinker *shrinker, const char *fmt, va_list ap)
{
	shrinker->name = kvasprintf_const(GFP_KERNEL, fmt, ap);

	return shrinker->name ? 0 : -ENOMEM;
}

static inline void shrinker_debugfs_name_free(struct shrinker *shrinker)
{
	kfree_const(shrinker->name);
	shrinker->name = NULL;
}

extern int shrinker_debugfs_add(struct shrinker *shrinker);
extern struct dentry *shrinker_debugfs_detach(struct shrinker *shrinker,
					      int *debugfs_id);
extern void shrinker_debugfs_remove(struct dentry *debugfs_entry,
				    int debugfs_id);
#else /* CONFIG_SHRINKER_DEBUG */
static inline int shrinker_debugfs_add(struct shrinker *shrinker)
{
	return 0;
}
static inline int shrinker_debugfs_name_alloc(struct shrinker *shrinker,
					      const char *fmt, va_list ap)
{
	return 0;
}
static inline void shrinker_debugfs_name_free(struct shrinker *shrinker)
{
}
static inline struct dentry *shrinker_debugfs_detach(struct shrinker *shrinker,
						     int *debugfs_id)
{
	*debugfs_id = -1;
	return NULL;
}
static inline void shrinker_debugfs_remove(struct dentry *debugfs_entry,
					   int debugfs_id)
{
}
#endif /* CONFIG_SHRINKER_DEBUG */

/* Only track the nodes of mappings with shadow entries */
void workingset_update_node(struct xa_node *node);
extern struct list_lru shadow_nodes;
#define mapping_set_update(xas, mapping) do {			\
	if (!dax_mapping(mapping) && !shmem_mapping(mapping)) {	\
		xas_set_update(xas, workingset_update_node);	\
		xas_set_lru(xas, &shadow_nodes);		\
	}							\
} while (0)

/* mremap.c */
unsigned long move_page_tables(struct pagetable_move_control *pmc);

#ifdef CONFIG_UNACCEPTED_MEMORY
void accept_page(struct page *page);
#else /* CONFIG_UNACCEPTED_MEMORY */
static inline void accept_page(struct page *page)
{
}
#endif /* CONFIG_UNACCEPTED_MEMORY */

/* pagewalk.c */
int walk_page_range_mm_unsafe(struct mm_struct *mm, unsigned long start,
		unsigned long end, const struct mm_walk_ops *ops,
		void *private);
int walk_page_range_vma_unsafe(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, const struct mm_walk_ops *ops,
		void *private);
int walk_page_range_debug(struct mm_struct *mm, unsigned long start,
			  unsigned long end, const struct mm_walk_ops *ops,
			  pgd_t *pgd, void *private);

void dup_mm_exe_file(struct mm_struct *mm, struct mm_struct *oldmm);
int dup_mmap(struct mm_struct *mm, struct mm_struct *oldmm);

int remap_pfn_range_prepare(struct vm_area_desc *desc);
int remap_pfn_range_complete(struct vm_area_struct *vma,
			     struct mmap_action *action);
int simple_ioremap_prepare(struct vm_area_desc *desc);

static inline int io_remap_pfn_range_prepare(struct vm_area_desc *desc)
{
	struct mmap_action *action = &desc->action;
	const unsigned long orig_pfn = action->remap.start_pfn;
	const pgprot_t orig_pgprot = action->remap.pgprot;
	const unsigned long size = action->remap.size;
	const unsigned long pfn = io_remap_pfn_range_pfn(orig_pfn, size);
	int err;

	action->remap.start_pfn = pfn;
	action->remap.pgprot = pgprot_decrypted(orig_pgprot);
	err = remap_pfn_range_prepare(desc);
	if (err)
		return err;

	/* Remap does the actual work. */
	action->type = MMAP_REMAP_PFN;
	return 0;
}

/*
 * When we succeed an mmap action or just before we unmap a VMA on error, we
 * need to ensure any rmap lock held is released. On unmap it's required to
 * avoid a deadlock.
 */
static inline void maybe_rmap_unlock_action(struct vm_area_struct *vma,
		struct mmap_action *action)
{
	struct file *file;

	if (!action->hide_from_rmap_until_complete)
		return;

	VM_WARN_ON_ONCE(vma_is_anonymous(vma));
	file = vma->vm_file;
	i_mmap_unlock_write(file->f_mapping);
	action->hide_from_rmap_until_complete = false;
}

#ifdef CONFIG_MMU_NOTIFIER
static inline bool clear_flush_young_ptes_notify(struct vm_area_struct *vma,
		unsigned long addr, pte_t *ptep, unsigned int nr)
{
	bool young;

	young = clear_flush_young_ptes(vma, addr, ptep, nr);
	young |= mmu_notifier_clear_flush_young(vma->vm_mm, addr,
						addr + nr * PAGE_SIZE);
	return young;
}

static inline bool pmdp_clear_flush_young_notify(struct vm_area_struct *vma,
		unsigned long addr, pmd_t *pmdp)
{
	bool young;

	young = pmdp_clear_flush_young(vma, addr, pmdp);
	young |= mmu_notifier_clear_flush_young(vma->vm_mm, addr, addr + PMD_SIZE);
	return young;
}

static inline bool test_and_clear_young_ptes_notify(struct vm_area_struct *vma,
		unsigned long addr, pte_t *ptep, unsigned int nr)
{
	bool young;

	young = test_and_clear_young_ptes(vma, addr, ptep, nr);
	young |= mmu_notifier_clear_young(vma->vm_mm, addr, addr + nr * PAGE_SIZE);
	return young;
}

static inline bool pmdp_test_and_clear_young_notify(struct vm_area_struct *vma,
		unsigned long addr, pmd_t *pmdp)
{
	bool young;

	young = pmdp_test_and_clear_young(vma, addr, pmdp);
	young |= mmu_notifier_clear_young(vma->vm_mm, addr, addr + PMD_SIZE);
	return young;
}

#else /* CONFIG_MMU_NOTIFIER */

#define clear_flush_young_ptes_notify	clear_flush_young_ptes
#define pmdp_clear_flush_young_notify	pmdp_clear_flush_young
#define test_and_clear_young_ptes_notify	test_and_clear_young_ptes
#define pmdp_test_and_clear_young_notify	pmdp_test_and_clear_young

#endif /* CONFIG_MMU_NOTIFIER */

extern int sysctl_max_map_count;
static inline int get_sysctl_max_map_count(void)
{
	return READ_ONCE(sysctl_max_map_count);
}

bool may_expand_vm(struct mm_struct *mm, const vma_flags_t *vma_flags,
		   unsigned long npages);

static inline void mm_prepare_for_swap_entries(struct mm_struct *mm)
{
	if (list_empty(&mm->mmlist)) {
		spin_lock(&mmlist_lock);
		if (list_empty(&mm->mmlist))
			list_add(&mm->mmlist, &init_mm.mmlist);
		spin_unlock(&mmlist_lock);
	}
}

static inline bool can_spin_trylock(void)
{
	/*
	 * In PREEMPT_RT spin_trylock() will call raw_spin_lock() which is
	 * unsafe in NMI. If spin_trylock() is called from hard IRQ the current
	 * task may be waiting for one rt_spin_lock, but rt_spin_trylock() will
	 * mark the task as the owner of another rt_spin_lock which will
	 * confuse PI logic, so return immediately if called from hard IRQ or
	 * NMI.
	 *
	 * Note, irqs_disabled() case is ok. spin_trylock() can be called
	 * from raw_spin_lock_irqsave region.
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && (in_nmi() || in_hardirq()))
		return false;

	/* On UP, spin_trylock() always succeeds even when it is locked */
	if (!IS_ENABLED(CONFIG_SMP) && in_nmi())
		return false;

	return true;
}

#endif	/* __MM_INTERNAL_H */
