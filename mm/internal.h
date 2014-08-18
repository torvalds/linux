/* internal.h: mm/ internal definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef __MM_INTERNAL_H
#define __MM_INTERNAL_H

#include <linux/fs.h>
#include <linux/mm.h>

void free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *start_vma,
		unsigned long floor, unsigned long ceiling);

static inline void set_page_count(struct page *page, int v)
{
	atomic_set(&page->_count, v);
}

extern int __do_page_cache_readahead(struct address_space *mapping,
		struct file *filp, pgoff_t offset, unsigned long nr_to_read,
		unsigned long lookahead_size);

/*
 * Submit IO for the read-ahead request in file_ra_state.
 */
static inline unsigned long ra_submit(struct file_ra_state *ra,
		struct address_space *mapping, struct file *filp)
{
	return __do_page_cache_readahead(mapping, filp,
					ra->start, ra->size, ra->async_size);
}

/*
 * Turn a non-refcounted page (->_count == 0) into refcounted with
 * a count of one.
 */
static inline void set_page_refcounted(struct page *page)
{
	VM_BUG_ON_PAGE(PageTail(page), page);
	VM_BUG_ON_PAGE(atomic_read(&page->_count), page);
	set_page_count(page, 1);
}

static inline void __get_page_tail_foll(struct page *page,
					bool get_page_head)
{
	/*
	 * If we're getting a tail page, the elevated page->_count is
	 * required only in the head page and we will elevate the head
	 * page->_count and tail page->_mapcount.
	 *
	 * We elevate page_tail->_mapcount for tail pages to force
	 * page_tail->_count to be zero at all times to avoid getting
	 * false positives from get_page_unless_zero() with
	 * speculative page access (like in
	 * page_cache_get_speculative()) on tail pages.
	 */
	VM_BUG_ON_PAGE(atomic_read(&page->first_page->_count) <= 0, page);
	if (get_page_head)
		atomic_inc(&page->first_page->_count);
	get_huge_page_tail(page);
}

/*
 * This is meant to be called as the FOLL_GET operation of
 * follow_page() and it must be called while holding the proper PT
 * lock while the pte (or pmd_trans_huge) is still mapping the page.
 */
static inline void get_page_foll(struct page *page)
{
	if (unlikely(PageTail(page)))
		/*
		 * This is safe only because
		 * __split_huge_page_refcount() can't run under
		 * get_page_foll() because we hold the proper PT lock.
		 */
		__get_page_tail_foll(page, true);
	else {
		/*
		 * Getting a normal page or the head of a compound page
		 * requires to already have an elevated page->_count.
		 */
		VM_BUG_ON_PAGE(atomic_read(&page->_count) <= 0, page);
		atomic_inc(&page->_count);
	}
}

extern unsigned long highest_memmap_pfn;

/*
 * in mm/vmscan.c:
 */
extern int isolate_lru_page(struct page *page);
extern void putback_lru_page(struct page *page);
extern bool zone_reclaimable(struct zone *zone);

/*
 * in mm/rmap.c:
 */
extern pmd_t *mm_find_pmd(struct mm_struct *mm, unsigned long address);

/*
 * in mm/page_alloc.c
 */
extern void __free_pages_bootmem(struct page *page, unsigned int order);
extern void prep_compound_page(struct page *page, unsigned long order);
#ifdef CONFIG_MEMORY_FAILURE
extern bool is_free_buddy_page(struct page *page);
#endif
extern int user_min_free_kbytes;

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
	unsigned long nr_freepages;	/* Number of isolated free pages */
	unsigned long nr_migratepages;	/* Number of pages to migrate */
	unsigned long free_pfn;		/* isolate_freepages search base */
	unsigned long migrate_pfn;	/* isolate_migratepages search base */
	enum migrate_mode mode;		/* Async or sync migration mode */
	bool ignore_skip_hint;		/* Scan blocks even if marked skip */
	bool finished_update_free;	/* True when the zone cached pfns are
					 * no longer being updated
					 */
	bool finished_update_migrate;

	int order;			/* order a direct compactor needs */
	int migratetype;		/* MOVABLE, RECLAIMABLE etc */
	struct zone *zone;
	bool contended;			/* True if a lock was contended, or
					 * need_resched() true during async
					 * compaction
					 */
};

unsigned long
isolate_freepages_range(struct compact_control *cc,
			unsigned long start_pfn, unsigned long end_pfn);
unsigned long
isolate_migratepages_range(struct zone *zone, struct compact_control *cc,
	unsigned long low_pfn, unsigned long end_pfn, bool unevictable);

#endif

/*
 * This function returns the order of a free page in the buddy system. In
 * general, page_zone(page)->lock must be held by the caller to prevent the
 * page from being allocated in parallel and returning garbage as the order.
 * If a caller does not hold page_zone(page)->lock, it must guarantee that the
 * page cannot be allocated or merged in parallel.
 */
static inline unsigned long page_order(struct page *page)
{
	/* PageBuddy() must be checked by the caller */
	return page_private(page);
}

static inline bool is_cow_mapping(vm_flags_t flags)
{
	return (flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
}

/* mm/util.c */
void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		struct vm_area_struct *prev, struct rb_node *rb_parent);

#ifdef CONFIG_MMU
extern long __mlock_vma_pages_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, int *nonblocking);
extern void munlock_vma_pages_range(struct vm_area_struct *vma,
			unsigned long start, unsigned long end);
static inline void munlock_vma_pages_all(struct vm_area_struct *vma)
{
	munlock_vma_pages_range(vma, vma->vm_start, vma->vm_end);
}

/*
 * must be called with vma's mmap_sem held for read or write, and page locked.
 */
extern void mlock_vma_page(struct page *page);
extern unsigned int munlock_vma_page(struct page *page);

/*
 * Clear the page's PageMlocked().  This can be useful in a situation where
 * we want to unconditionally remove a page from the pagecache -- e.g.,
 * on truncation or freeing.
 *
 * It is legal to call this function for any page, mlocked or not.
 * If called for a page that is still mapped by mlocked vmas, all we do
 * is revert to lazy LRU behaviour -- semantics are not broken.
 */
extern void clear_page_mlock(struct page *page);

/*
 * mlock_migrate_page - called only from migrate_page_copy() to
 * migrate the Mlocked page flag; update statistics.
 */
static inline void mlock_migrate_page(struct page *newpage, struct page *page)
{
	if (TestClearPageMlocked(page)) {
		unsigned long flags;
		int nr_pages = hpage_nr_pages(page);

		local_irq_save(flags);
		__mod_zone_page_state(page_zone(page), NR_MLOCK, -nr_pages);
		SetPageMlocked(newpage);
		__mod_zone_page_state(page_zone(newpage), NR_MLOCK, nr_pages);
		local_irq_restore(flags);
	}
}

extern pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
extern unsigned long vma_address(struct page *page,
				 struct vm_area_struct *vma);
#endif
#else /* !CONFIG_MMU */
static inline void clear_page_mlock(struct page *page) { }
static inline void mlock_vma_page(struct page *page) { }
static inline void mlock_migrate_page(struct page *new, struct page *old) { }

#endif /* !CONFIG_MMU */

/*
 * Return the mem_map entry representing the 'offset' subpage within
 * the maximally aligned gigantic page 'base'.  Handle any discontiguity
 * in the mem_map at MAX_ORDER_NR_PAGES boundaries.
 */
static inline struct page *mem_map_offset(struct page *base, int offset)
{
	if (unlikely(offset >= MAX_ORDER_NR_PAGES))
		return nth_page(base, offset);
	return base + offset;
}

/*
 * Iterator over all subpages within the maximally aligned gigantic
 * page 'base'.  Handle any discontiguity in the mem_map.
 */
static inline struct page *mem_map_next(struct page *iter,
						struct page *base, int offset)
{
	if (unlikely((offset & (MAX_ORDER_NR_PAGES - 1)) == 0)) {
		unsigned long pfn = page_to_pfn(base) + offset;
		if (!pfn_valid(pfn))
			return NULL;
		return pfn_to_page(pfn);
	}
	return iter + 1;
}

/*
 * FLATMEM and DISCONTIGMEM configurations use alloc_bootmem_node,
 * so all functions starting at paging_init should be marked __init
 * in those cases. SPARSEMEM, however, allows for memory hotplug,
 * and alloc_bootmem_node is not used.
 */
#ifdef CONFIG_SPARSEMEM
#define __paginginit __meminit
#else
#define __paginginit __init
#endif

/* Memory initialisation debug and verification */
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
		printk(level <= MMINIT_WARNING ? KERN_WARNING : KERN_DEBUG); \
		printk(KERN_CONT "mminit::" prefix " " fmt, ##arg); \
	} \
} while (0)

extern void mminit_verify_pageflags_layout(void);
extern void mminit_verify_page_links(struct page *page,
		enum zone_type zone, unsigned long nid, unsigned long pfn);
extern void mminit_verify_zonelist(void);

#else

static inline void mminit_dprintk(enum mminit_level level,
				const char *prefix, const char *fmt, ...)
{
}

static inline void mminit_verify_pageflags_layout(void)
{
}

static inline void mminit_verify_page_links(struct page *page,
		enum zone_type zone, unsigned long nid, unsigned long pfn)
{
}

static inline void mminit_verify_zonelist(void)
{
}
#endif /* CONFIG_DEBUG_MEMORY_INIT */

/* mminit_validate_memmodel_limits is independent of CONFIG_DEBUG_MEMORY_INIT */
#if defined(CONFIG_SPARSEMEM)
extern void mminit_validate_memmodel_limits(unsigned long *start_pfn,
				unsigned long *end_pfn);
#else
static inline void mminit_validate_memmodel_limits(unsigned long *start_pfn,
				unsigned long *end_pfn)
{
}
#endif /* CONFIG_SPARSEMEM */

#define ZONE_RECLAIM_NOSCAN	-2
#define ZONE_RECLAIM_FULL	-1
#define ZONE_RECLAIM_SOME	0
#define ZONE_RECLAIM_SUCCESS	1

extern int hwpoison_filter(struct page *p);

extern u32 hwpoison_filter_dev_major;
extern u32 hwpoison_filter_dev_minor;
extern u64 hwpoison_filter_flags_mask;
extern u64 hwpoison_filter_flags_value;
extern u64 hwpoison_filter_memcg;
extern u32 hwpoison_filter_enable;

extern unsigned long vm_mmap_pgoff(struct file *, unsigned long,
        unsigned long, unsigned long,
        unsigned long, unsigned long);

extern void set_pageblock_order(void);
unsigned long reclaim_clean_pages_from_list(struct zone *zone,
					    struct list_head *page_list);
/* The ALLOC_WMARK bits are used as an index to zone->watermark */
#define ALLOC_WMARK_MIN		WMARK_MIN
#define ALLOC_WMARK_LOW		WMARK_LOW
#define ALLOC_WMARK_HIGH	WMARK_HIGH
#define ALLOC_NO_WATERMARKS	0x04 /* don't check watermarks at all */

/* Mask to get the watermark bits */
#define ALLOC_WMARK_MASK	(ALLOC_NO_WATERMARKS-1)

#define ALLOC_HARDER		0x10 /* try to alloc harder */
#define ALLOC_HIGH		0x20 /* __GFP_HIGH set */
#define ALLOC_CPUSET		0x40 /* check for correct cpuset */
#define ALLOC_CMA		0x80 /* allow allocations from CMA areas */
#define ALLOC_FAIR		0x100 /* fair zone allocation */

#endif	/* __MM_INTERNAL_H */
