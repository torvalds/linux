/* SPDX-License-Identifier: GPL-2.0-or-later */
/* internal.h: mm/ internal definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#ifndef __MM_INTERNAL_H
#define __MM_INTERNAL_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/tracepoint-defs.h>

struct folio_batch;

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
	static bool __section(".data.once") __warned;			\
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
 */
static inline int folio_nr_pages_mapped(struct folio *folio)
{
	return atomic_read(&folio->_nr_pages_mapped) & FOLIO_PAGES_MAPPED;
}

static inline void *folio_raw_mapping(struct folio *folio)
{
	unsigned long mapping = (unsigned long)folio->mapping;

	return (void *)(mapping & ~PAGE_MAPPING_FLAGS);
}

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

vm_fault_t do_swap_page(struct vm_fault *vmf);
void folio_rotate_reclaimable(struct folio *folio);
bool __folio_end_writeback(struct folio *folio);
void deactivate_file_folio(struct folio *folio);
void folio_activate(struct folio *folio);

void free_pgtables(struct mmu_gather *tlb, struct ma_state *mas,
		   struct vm_area_struct *start_vma, unsigned long floor,
		   unsigned long ceiling, bool mm_wr_locked);
void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte);

struct zap_details;
void unmap_page_range(struct mmu_gather *tlb,
			     struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end,
			     struct zap_details *details);

void page_cache_ra_order(struct readahead_control *, struct file_ra_state *,
		unsigned int order);
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
void filemap_free_folio(struct address_space *mapping, struct folio *folio);
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
bool isolate_lru_page(struct page *page);
bool folio_isolate_lru(struct folio *folio);
void putback_lru_page(struct page *page);
void folio_putback_lru(struct folio *folio);
extern void reclaim_throttle(pg_data_t *pgdat, enum vmscan_throttle_state reason);

/*
 * in mm/rmap.c:
 */
pmd_t *mm_find_pmd(struct mm_struct *mm, unsigned long address);

/*
 * in mm/page_alloc.c
 */
#define K(x) ((x) << (PAGE_SHIFT-10))

extern char * const zone_names[MAX_NR_ZONES];

/* perform sanity checks on struct pages being allocated or freed */
DECLARE_STATIC_KEY_MAYBE(CONFIG_DEBUG_VM, check_pages_enabled);

extern int min_free_kbytes;

void setup_per_zone_wmarks(void);
void calculate_min_free_kbytes(void);
int __meminit init_per_zone_wmark_min(void);
void page_alloc_sysctl_init(void);

/*
 * Structure for holding the mostly immutable allocation parameters passed
 * between functions involved in allocations, including the alloc_pages*
 * family of functions.
 *
 * nodemask, migratetype and highest_zoneidx are initialized only once in
 * __alloc_pages() and then never change.
 *
 * zonelist, preferred_zone and highest_zoneidx are set first in
 * __alloc_pages() for the fast path, and might be later changed
 * in __alloc_pages_slowpath(). All other functions pass the whole structure
 * by a const pointer.
 */
struct alloc_context {
	struct zonelist *zonelist;
	nodemask_t *nodemask;
	struct zoneref *preferred_zoneref;
	int migratetype;

	/*
	 * highest_zoneidx represents highest usable zone index of
	 * the allocation request. Due to the nature of the zone,
	 * memory on lower zone than the highest_zoneidx will be
	 * protected by lowmem_reserve[highest_zoneidx].
	 *
	 * highest_zoneidx is also used by reclaim/compaction to limit
	 * the target zone since higher zone than this index cannot be
	 * usable for this allocation request.
	 */
	enum zone_type highest_zoneidx;
	bool spread_dirty_pages;
};

/*
 * This function returns the order of a free page in the buddy system. In
 * general, page_zone(page)->lock must be held by the caller to prevent the
 * page from being allocated in parallel and returning garbage as the order.
 * If a caller does not hold page_zone(page)->lock, it must guarantee that the
 * page cannot be allocated or merged in parallel. Alternatively, it must
 * handle invalid values gracefully, and use buddy_order_unsafe() below.
 */
static inline unsigned int buddy_order(struct page *page)
{
	/* PageBuddy() must be checked by the caller */
	return page_private(page);
}

/*
 * Like buddy_order(), but for callers who cannot afford to hold the zone lock.
 * PageBuddy() should be checked first by the caller to minimize race window,
 * and invalid values must be handled gracefully.
 *
 * READ_ONCE is used so that if the caller assigns the result into a local
 * variable and e.g. tests it for valid range before using, the compiler cannot
 * decide to remove the variable and inline the page_private(page) multiple
 * times, potentially observing different values in the tests and the actual
 * use of the result.
 */
#define buddy_order_unsafe(page)	READ_ONCE(page_private(page))

/*
 * This function checks whether a page is free && is the buddy
 * we can coalesce a page and its buddy if
 * (a) the buddy is not in a hole (check before calling!) &&
 * (b) the buddy is in the buddy system &&
 * (c) a page and its buddy have the same order &&
 * (d) a page and its buddy are in the same zone.
 *
 * For recording whether a page is in the buddy system, we set PageBuddy.
 * Setting, clearing, and testing PageBuddy is serialized by zone->lock.
 *
 * For recording page's order, we use page_private(page).
 */
static inline bool page_is_buddy(struct page *page, struct page *buddy,
				 unsigned int order)
{
	if (!page_is_guard(buddy) && !PageBuddy(buddy))
		return false;

	if (buddy_order(buddy) != order)
		return false;

	/*
	 * zone check is done late to avoid uselessly calculating
	 * zone/node ids for pages that could never merge.
	 */
	if (page_zone_id(page) != page_zone_id(buddy))
		return false;

	VM_BUG_ON_PAGE(page_count(buddy) != 0, buddy);

	return true;
}

/*
 * Locate the struct page for both the matching buddy in our
 * pair (buddy1) and the combined O(n+1) page they form (page).
 *
 * 1) Any buddy B1 will have an order O twin B2 which satisfies
 * the following equation:
 *     B2 = B1 ^ (1 << O)
 * For example, if the starting buddy (buddy2) is #8 its order
 * 1 buddy is #10:
 *     B2 = 8 ^ (1 << 1) = 8 ^ 2 = 10
 *
 * 2) Any buddy B will have an order O+1 parent P which
 * satisfies the following equation:
 *     P = B & ~(1 << O)
 *
 * Assumption: *_mem_map is contiguous at least up to MAX_ORDER
 */
static inline unsigned long
__find_buddy_pfn(unsigned long page_pfn, unsigned int order)
{
	return page_pfn ^ (1 << order);
}

/*
 * Find the buddy of @page and validate it.
 * @page: The input page
 * @pfn: The pfn of the page, it saves a call to page_to_pfn() when the
 *       function is used in the performance-critical __free_one_page().
 * @order: The order of the page
 * @buddy_pfn: The output pointer to the buddy pfn, it also saves a call to
 *             page_to_pfn().
 *
 * The found buddy can be a non PageBuddy, out of @page's zone, or its order is
 * not the same as @page. The validation is necessary before use it.
 *
 * Return: the found buddy page or NULL if not found.
 */
static inline struct page *find_buddy_page_pfn(struct page *page,
			unsigned long pfn, unsigned int order, unsigned long *buddy_pfn)
{
	unsigned long __buddy_pfn = __find_buddy_pfn(pfn, order);
	struct page *buddy;

	buddy = page + (__buddy_pfn - pfn);
	if (buddy_pfn)
		*buddy_pfn = __buddy_pfn;

	if (page_is_buddy(page, buddy, order))
		return buddy;
	return NULL;
}

extern struct page *__pageblock_pfn_to_page(unsigned long start_pfn,
				unsigned long end_pfn, struct zone *zone);

static inline struct page *pageblock_pfn_to_page(unsigned long start_pfn,
				unsigned long end_pfn, struct zone *zone)
{
	if (zone->contiguous)
		return pfn_to_page(start_pfn);

	return __pageblock_pfn_to_page(start_pfn, end_pfn, zone);
}

void set_zone_contiguous(struct zone *zone);

static inline void clear_zone_contiguous(struct zone *zone)
{
	zone->contiguous = false;
}

extern int __isolate_free_page(struct page *page, unsigned int order);
extern void __putback_isolated_page(struct page *page, unsigned int order,
				    int mt);
extern void memblock_free_pages(struct page *page, unsigned long pfn,
					unsigned int order);
extern void __free_pages_core(struct page *page, unsigned int order);

/*
 * This will have no effect, other than possibly generating a warning, if the
 * caller passes in a non-large folio.
 */
static inline void folio_set_order(struct folio *folio, unsigned int order)
{
	if (WARN_ON_ONCE(!order || !folio_test_large(folio)))
		return;

	folio->_flags_1 = (folio->_flags_1 & ~0xffUL) | order;
#ifdef CONFIG_64BIT
	folio->_folio_nr_pages = 1U << order;
#endif
}

void folio_undo_large_rmappable(struct folio *folio);

static inline struct folio *page_rmappable_folio(struct page *page)
{
	struct folio *folio = (struct folio *)page;

	if (folio && folio_order(folio) > 1)
		folio_prep_large_rmappable(folio);
	return folio;
}

static inline void prep_compound_head(struct page *page, unsigned int order)
{
	struct folio *folio = (struct folio *)page;

	folio_set_order(folio, order);
	atomic_set(&folio->_entire_mapcount, -1);
	atomic_set(&folio->_nr_pages_mapped, 0);
	atomic_set(&folio->_pincount, 0);
}

static inline void prep_compound_tail(struct page *head, int tail_idx)
{
	struct page *p = head + tail_idx;

	p->mapping = TAIL_MAPPING;
	set_compound_head(p, head);
	set_page_private(p, 0);
}

extern void prep_compound_page(struct page *page, unsigned int order);

extern void post_alloc_hook(struct page *page, unsigned int order,
					gfp_t gfp_flags);
extern int user_min_free_kbytes;

extern void free_unref_page(struct page *page, unsigned int order);
extern void free_unref_page_list(struct list_head *list);

extern void zone_pcp_reset(struct zone *zone);
extern void zone_pcp_disable(struct zone *zone);
extern void zone_pcp_enable(struct zone *zone);
extern void zone_pcp_init(struct zone *zone);

extern void *memmap_alloc(phys_addr_t size, phys_addr_t align,
			  phys_addr_t min_addr,
			  int nid, bool exact_nid);

void memmap_init_range(unsigned long, int, unsigned long, unsigned long,
		unsigned long, enum meminit_context, struct vmem_altmap *, int);


int split_free_page(struct page *free_page,
			unsigned int order, unsigned long split_pfn_offset);

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
	struct list_head freepages;	/* List of free pages to migrate to */
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

int __alloc_contig_migrate_range(struct compact_control *cc,
					unsigned long start, unsigned long end);

/* Free whole pageblock and set its migration type to MIGRATE_CMA. */
void init_cma_reserved_pageblock(struct page *page);

#endif /* CONFIG_COMPACTION || CONFIG_CMA */

int find_suitable_fallback(struct free_area *area, unsigned int order,
			int migratetype, bool only_stealable, bool *can_steal);

static inline bool free_area_empty(struct free_area *area, int migratetype)
{
	return list_empty(&area->free_list[migratetype]);
}

/*
 * These three helpers classifies VMAs for virtual memory accounting.
 */

/*
 * Executable code area - executable, not writable, not stack
 */
static inline bool is_exec_mapping(vm_flags_t flags)
{
	return (flags & (VM_EXEC | VM_WRITE | VM_STACK)) == VM_EXEC;
}

/*
 * Stack area (including shadow stacks)
 *
 * VM_GROWSUP / VM_GROWSDOWN VMAs are always private anonymous:
 * do_mmap() forbids all other combinations.
 */
static inline bool is_stack_mapping(vm_flags_t flags)
{
	return ((flags & VM_STACK) == VM_STACK) || (flags & VM_SHADOW_STACK);
}

/*
 * Data area - private, writable, not stack
 */
static inline bool is_data_mapping(vm_flags_t flags)
{
	return (flags & (VM_WRITE | VM_SHARED | VM_STACK)) == VM_WRITE;
}

/* mm/util.c */
struct anon_vma *folio_anon_vma(struct folio *folio);

#ifdef CONFIG_MMU
void unmap_mapping_folio(struct folio *folio);
extern long populate_vma_page_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, int *locked);
extern long faultin_vma_page_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end,
				   bool write, int *locked);
extern bool mlock_future_ok(struct mm_struct *mm, unsigned long flags,
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

/*
 * Return the start of user virtual address at the specific offset within
 * a vma.
 */
static inline unsigned long
vma_pgoff_address(pgoff_t pgoff, unsigned long nr_pages,
		  struct vm_area_struct *vma)
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
 * Return the start of user virtual address of a page within a vma.
 * Returns -EFAULT if all of the page is outside the range of vma.
 * If page is a compound head, the entire compound page is considered.
 */
static inline unsigned long
vma_address(struct page *page, struct vm_area_struct *vma)
{
	VM_BUG_ON_PAGE(PageKsm(page), page);	/* KSM page->index unusable */
	return vma_pgoff_address(page_to_pgoff(page), compound_nr(page), vma);
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

/* Memory initialisation debug and verification */
#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
DECLARE_STATIC_KEY_TRUE(deferred_pages);

bool __init deferred_grow_zone(struct zone *zone, unsigned int order);
#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

enum mminit_level {
	MMINIT_WARNING,
	MMINIT_VERIFY,
	MMINIT_TRACE
};

#ifdef CONFIG_DEBUG_MEMORY_INIT

extern int mminit_loglevel;

#define mminit_dprintk(level, prefix, fmt, arg...) \
do { \
	if (level < mminit_loglevel) { \
		if (level <= MMINIT_WARNING) \
			pr_warn("mminit::" prefix " " fmt, ##arg);	\
		else \
			printk(KERN_DEBUG "mminit::" prefix " " fmt, ##arg); \
	} \
} while (0)

extern void mminit_verify_pageflags_layout(void);
extern void mminit_verify_zonelist(void);
#else

static inline void mminit_dprintk(enum mminit_level level,
				const char *prefix, const char *fmt, ...)
{
}

static inline void mminit_verify_pageflags_layout(void)
{
}

static inline void mminit_verify_zonelist(void)
{
}
#endif /* CONFIG_DEBUG_MEMORY_INIT */

#define NODE_RECLAIM_NOSCAN	-2
#define NODE_RECLAIM_FULL	-1
#define NODE_RECLAIM_SOME	0
#define NODE_RECLAIM_SUCCESS	1

#ifdef CONFIG_NUMA
extern int node_reclaim(struct pglist_data *, gfp_t, unsigned int);
extern int find_next_best_node(int node, nodemask_t *used_node_mask);
#else
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

/*
 * mm/memory-failure.c
 */
extern int hwpoison_filter(struct page *p);

extern u32 hwpoison_filter_dev_major;
extern u32 hwpoison_filter_dev_minor;
extern u64 hwpoison_filter_flags_mask;
extern u64 hwpoison_filter_flags_value;
extern u64 hwpoison_filter_memcg;
extern u32 hwpoison_filter_enable;

extern unsigned long  __must_check vm_mmap_pgoff(struct file *, unsigned long,
        unsigned long, unsigned long,
        unsigned long, unsigned long);

extern void set_pageblock_order(void);
unsigned long reclaim_pages(struct list_head *folio_list);
unsigned int reclaim_clean_pages_from_list(struct zone *zone,
					    struct list_head *folio_list);
/* The ALLOC_WMARK bits are used as an index to zone->watermark */
#define ALLOC_WMARK_MIN		WMARK_MIN
#define ALLOC_WMARK_LOW		WMARK_LOW
#define ALLOC_WMARK_HIGH	WMARK_HIGH
#define ALLOC_NO_WATERMARKS	0x04 /* don't check watermarks at all */

/* Mask to get the watermark bits */
#define ALLOC_WMARK_MASK	(ALLOC_NO_WATERMARKS-1)

/*
 * Only MMU archs have async oom victim reclaim - aka oom_reaper so we
 * cannot assume a reduced access to memory reserves is sufficient for
 * !MMU
 */
#ifdef CONFIG_MMU
#define ALLOC_OOM		0x08
#else
#define ALLOC_OOM		ALLOC_NO_WATERMARKS
#endif

#define ALLOC_NON_BLOCK		 0x10 /* Caller cannot block. Allow access
				       * to 25% of the min watermark or
				       * 62.5% if __GFP_HIGH is set.
				       */
#define ALLOC_MIN_RESERVE	 0x20 /* __GFP_HIGH set. Allow access to 50%
				       * of the min watermark.
				       */
#define ALLOC_CPUSET		 0x40 /* check for correct cpuset */
#define ALLOC_CMA		 0x80 /* allow allocations from CMA areas */
#ifdef CONFIG_ZONE_DMA32
#define ALLOC_NOFRAGMENT	0x100 /* avoid mixing pageblock types */
#else
#define ALLOC_NOFRAGMENT	  0x0
#endif
#define ALLOC_HIGHATOMIC	0x200 /* Allows access to MIGRATE_HIGHATOMIC */
#define ALLOC_KSWAPD		0x800 /* allow waking of kswapd, __GFP_KSWAPD_RECLAIM set */

/* Flags that allow allocations below the min watermark. */
#define ALLOC_RESERVES (ALLOC_NON_BLOCK|ALLOC_MIN_RESERVE|ALLOC_HIGHATOMIC|ALLOC_OOM)

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
extern const struct trace_print_flags pagetype_names[];
extern const struct trace_print_flags vmaflag_names[];
extern const struct trace_print_flags gfpflag_names[];

static inline bool is_migrate_highatomic(enum migratetype migratetype)
{
	return migratetype == MIGRATE_HIGHATOMIC;
}

static inline bool is_migrate_highatomic_page(struct page *page)
{
	return get_pageblock_migratetype(page) == MIGRATE_HIGHATOMIC;
}

void setup_zone_pageset(struct zone *zone);

struct migration_target_control {
	int nid;		/* preferred node id */
	nodemask_t *nmask;
	gfp_t gfp_mask;
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
                pgprot_t prot, struct page **pages, unsigned int page_shift);
#else
static inline void vmalloc_init(void)
{
}

static inline
int __must_check vmap_pages_range_noflush(unsigned long addr, unsigned long end,
                pgprot_t prot, struct page **pages, unsigned int page_shift)
{
	return -EINVAL;
}
#endif

int __must_check __vmap_pages_range_noflush(unsigned long addr,
			       unsigned long end, pgprot_t prot,
			       struct page **pages, unsigned int page_shift);

void vunmap_range_noflush(unsigned long start, unsigned long end);

void __vunmap_range_noflush(unsigned long start, unsigned long end);

int numa_migrate_prep(struct folio *folio, struct vm_area_struct *vma,
		      unsigned long addr, int page_nid, int *flags);

void free_zone_device_page(struct page *page);
int migrate_device_coherent_page(struct page *page);

/*
 * mm/gup.c
 */
struct folio *try_grab_folio(struct page *page, int refs, unsigned int flags);
int __must_check try_grab_page(struct page *page, unsigned int flags);

/*
 * mm/huge_memory.c
 */
struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
				   unsigned long addr, pmd_t *pmd,
				   unsigned int flags);

/*
 * mm/mmap.c
 */
struct vm_area_struct *vma_merge_extend(struct vma_iterator *vmi,
					struct vm_area_struct *vma,
					unsigned long delta);

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
};

#define INTERNAL_GUP_FLAGS (FOLL_TOUCH | FOLL_TRIED | FOLL_REMOTE | FOLL_PIN | \
			    FOLL_FAST_ONLY | FOLL_UNLOCKABLE)

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
	if (IS_ENABLED(CONFIG_HAVE_FAST_GUP))
		smp_rmb();

	/*
	 * During GUP-fast we might not get called on the head page for a
	 * hugetlb page that is mapped using cont-PTE, because GUP-fast does
	 * not work with the abstracted hugetlb PTEs that always point at the
	 * head page. For hugetlb, PageAnonExclusive only applies on the head
	 * page (as it cannot be partially COW-shared), so lookup the head page.
	 */
	if (unlikely(!PageHead(page) && PageHuge(page)))
		page = compound_head(page);

	/*
	 * Note that PageKsm() pages cannot be exclusive, and consequently,
	 * cannot get pinned.
	 */
	return !PageAnonExclusive(page);
}

extern bool mirrored_kernelcore;
extern bool memblock_has_mirror(void);

static inline bool vma_soft_dirty_enabled(struct vm_area_struct *vma)
{
	/*
	 * NOTE: we must check this before VM_SOFTDIRTY on soft-dirty
	 * enablements, because when without soft-dirty being compiled in,
	 * VM_SOFTDIRTY is defined as 0x0, then !(vm_flags & VM_SOFTDIRTY)
	 * will be constantly true.
	 */
	if (!IS_ENABLED(CONFIG_MEM_SOFT_DIRTY))
		return false;

	/*
	 * Soft-dirty is kind of special: its tracking is enabled when the
	 * vma flags not set.
	 */
	return !(vma->vm_flags & VM_SOFTDIRTY);
}

static inline void vma_iter_config(struct vma_iterator *vmi,
		unsigned long index, unsigned long last)
{
	__mas_set_range(&vmi->mas, index, last - 1);
}

/*
 * VMA Iterator functions shared between nommu and mmap
 */
static inline int vma_iter_prealloc(struct vma_iterator *vmi,
		struct vm_area_struct *vma)
{
	return mas_preallocate(&vmi->mas, vma, GFP_KERNEL);
}

static inline void vma_iter_clear(struct vma_iterator *vmi)
{
	mas_store_prealloc(&vmi->mas, NULL);
}

static inline struct vm_area_struct *vma_iter_load(struct vma_iterator *vmi)
{
	return mas_walk(&vmi->mas);
}

/* Store a VMA with preallocated memory */
static inline void vma_iter_store(struct vma_iterator *vmi,
				  struct vm_area_struct *vma)
{

#if defined(CONFIG_DEBUG_VM_MAPLE_TREE)
	if (MAS_WARN_ON(&vmi->mas, vmi->mas.status != ma_start &&
			vmi->mas.index > vma->vm_start)) {
		pr_warn("%lx > %lx\n store vma %lx-%lx\n into slot %lx-%lx\n",
			vmi->mas.index, vma->vm_start, vma->vm_start,
			vma->vm_end, vmi->mas.index, vmi->mas.last);
	}
	if (MAS_WARN_ON(&vmi->mas, vmi->mas.status != ma_start &&
			vmi->mas.last <  vma->vm_start)) {
		pr_warn("%lx < %lx\nstore vma %lx-%lx\ninto slot %lx-%lx\n",
		       vmi->mas.last, vma->vm_start, vma->vm_start, vma->vm_end,
		       vmi->mas.index, vmi->mas.last);
	}
#endif

	if (vmi->mas.status != ma_start &&
	    ((vmi->mas.index > vma->vm_start) || (vmi->mas.last < vma->vm_start)))
		vma_iter_invalidate(vmi);

	__mas_set_range(&vmi->mas, vma->vm_start, vma->vm_end - 1);
	mas_store_prealloc(&vmi->mas, vma);
}

static inline int vma_iter_store_gfp(struct vma_iterator *vmi,
			struct vm_area_struct *vma, gfp_t gfp)
{
	if (vmi->mas.status != ma_start &&
	    ((vmi->mas.index > vma->vm_start) || (vmi->mas.last < vma->vm_start)))
		vma_iter_invalidate(vmi);

	__mas_set_range(&vmi->mas, vma->vm_start, vma->vm_end - 1);
	mas_store_gfp(&vmi->mas, vma, gfp);
	if (unlikely(mas_is_err(&vmi->mas)))
		return -ENOMEM;

	return 0;
}

/*
 * VMA lock generalization
 */
struct vma_prepare {
	struct vm_area_struct *vma;
	struct vm_area_struct *adj_next;
	struct file *file;
	struct address_space *mapping;
	struct anon_vma *anon_vma;
	struct vm_area_struct *insert;
	struct vm_area_struct *remove;
	struct vm_area_struct *remove2;
};

void __meminit __init_single_page(struct page *page, unsigned long pfn,
				unsigned long zone, int nid);

/* shrinker related functions */
unsigned long shrink_slab(gfp_t gfp_mask, int nid, struct mem_cgroup *memcg,
			  int priority);

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

#endif	/* __MM_INTERNAL_H */
