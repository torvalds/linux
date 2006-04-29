#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

/*
 * Copyright 1995 Linus Torvalds
 */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <asm/uaccess.h>
#include <linux/gfp.h>

/*
 * Bits in mapping->flags.  The lower __GFP_BITS_SHIFT bits are the page
 * allocation mode flags.
 */
#define	AS_EIO		(__GFP_BITS_SHIFT + 0)	/* IO error on async write */
#define AS_ENOSPC	(__GFP_BITS_SHIFT + 1)	/* ENOSPC on async write */

static inline gfp_t mapping_gfp_mask(struct address_space * mapping)
{
	return (__force gfp_t)mapping->flags & __GFP_BITS_MASK;
}

/*
 * This is non-atomic.  Only to be used before the mapping is activated.
 * Probably needs a barrier...
 */
static inline void mapping_set_gfp_mask(struct address_space *m, gfp_t mask)
{
	m->flags = (m->flags & ~(__force unsigned long)__GFP_BITS_MASK) |
				(__force unsigned long)mask;
}

/*
 * The page cache can done in larger chunks than
 * one page, because it allows for more efficient
 * throughput (it can then be mapped into user
 * space in smaller chunks for same flexibility).
 *
 * Or rather, it _will_ be done in larger chunks.
 */
#define PAGE_CACHE_SHIFT	PAGE_SHIFT
#define PAGE_CACHE_SIZE		PAGE_SIZE
#define PAGE_CACHE_MASK		PAGE_MASK
#define PAGE_CACHE_ALIGN(addr)	(((addr)+PAGE_CACHE_SIZE-1)&PAGE_CACHE_MASK)

#define page_cache_get(page)		get_page(page)
#define page_cache_release(page)	put_page(page)
void release_pages(struct page **pages, int nr, int cold);

#ifdef CONFIG_NUMA
extern struct page *page_cache_alloc(struct address_space *x);
extern struct page *page_cache_alloc_cold(struct address_space *x);
#else
static inline struct page *page_cache_alloc(struct address_space *x)
{
	return alloc_pages(mapping_gfp_mask(x), 0);
}

static inline struct page *page_cache_alloc_cold(struct address_space *x)
{
	return alloc_pages(mapping_gfp_mask(x)|__GFP_COLD, 0);
}
#endif

typedef int filler_t(void *, struct page *);

extern struct page * find_get_page(struct address_space *mapping,
				unsigned long index);
extern struct page * find_lock_page(struct address_space *mapping,
				unsigned long index);
extern __deprecated_for_modules struct page * find_trylock_page(
			struct address_space *mapping, unsigned long index);
extern struct page * find_or_create_page(struct address_space *mapping,
				unsigned long index, gfp_t gfp_mask);
unsigned find_get_pages(struct address_space *mapping, pgoff_t start,
			unsigned int nr_pages, struct page **pages);
unsigned find_get_pages_contig(struct address_space *mapping, pgoff_t start,
			       unsigned int nr_pages, struct page **pages);
unsigned find_get_pages_tag(struct address_space *mapping, pgoff_t *index,
			int tag, unsigned int nr_pages, struct page **pages);

/*
 * Returns locked page at given index in given cache, creating it if needed.
 */
static inline struct page *grab_cache_page(struct address_space *mapping, unsigned long index)
{
	return find_or_create_page(mapping, index, mapping_gfp_mask(mapping));
}

extern struct page * grab_cache_page_nowait(struct address_space *mapping,
				unsigned long index);
extern struct page * read_cache_page(struct address_space *mapping,
				unsigned long index, filler_t *filler,
				void *data);
extern int read_cache_pages(struct address_space *mapping,
		struct list_head *pages, filler_t *filler, void *data);

int add_to_page_cache(struct page *page, struct address_space *mapping,
				unsigned long index, gfp_t gfp_mask);
int add_to_page_cache_lru(struct page *page, struct address_space *mapping,
				unsigned long index, gfp_t gfp_mask);
extern void remove_from_page_cache(struct page *page);
extern void __remove_from_page_cache(struct page *page);

extern atomic_t nr_pagecache;

#ifdef CONFIG_SMP

#define PAGECACHE_ACCT_THRESHOLD        max(16, NR_CPUS * 2)
DECLARE_PER_CPU(long, nr_pagecache_local);

/*
 * pagecache_acct implements approximate accounting for pagecache.
 * vm_enough_memory() do not need high accuracy. Writers will keep
 * an offset in their per-cpu arena and will spill that into the
 * global count whenever the absolute value of the local count
 * exceeds the counter's threshold.
 *
 * MUST be protected from preemption.
 * current protection is mapping->page_lock.
 */
static inline void pagecache_acct(int count)
{
	long *local;

	local = &__get_cpu_var(nr_pagecache_local);
	*local += count;
	if (*local > PAGECACHE_ACCT_THRESHOLD || *local < -PAGECACHE_ACCT_THRESHOLD) {
		atomic_add(*local, &nr_pagecache);
		*local = 0;
	}
}

#else

static inline void pagecache_acct(int count)
{
	atomic_add(count, &nr_pagecache);
}
#endif

static inline unsigned long get_page_cache_size(void)
{
	int ret = atomic_read(&nr_pagecache);
	if (unlikely(ret < 0))
		ret = 0;
	return ret;
}

/*
 * Return byte-offset into filesystem object for page.
 */
static inline loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_CACHE_SHIFT;
}

static inline pgoff_t linear_page_index(struct vm_area_struct *vma,
					unsigned long address)
{
	pgoff_t pgoff = (address - vma->vm_start) >> PAGE_SHIFT;
	pgoff += vma->vm_pgoff;
	return pgoff >> (PAGE_CACHE_SHIFT - PAGE_SHIFT);
}

extern void FASTCALL(__lock_page(struct page *page));
extern void FASTCALL(unlock_page(struct page *page));

static inline void lock_page(struct page *page)
{
	might_sleep();
	if (TestSetPageLocked(page))
		__lock_page(page);
}
	
/*
 * This is exported only for wait_on_page_locked/wait_on_page_writeback.
 * Never use this directly!
 */
extern void FASTCALL(wait_on_page_bit(struct page *page, int bit_nr));

/* 
 * Wait for a page to be unlocked.
 *
 * This must be called with the caller "holding" the page,
 * ie with increased "page->count" so that the page won't
 * go away during the wait..
 */
static inline void wait_on_page_locked(struct page *page)
{
	if (PageLocked(page))
		wait_on_page_bit(page, PG_locked);
}

/* 
 * Wait for a page to complete writeback
 */
static inline void wait_on_page_writeback(struct page *page)
{
	if (PageWriteback(page))
		wait_on_page_bit(page, PG_writeback);
}

extern void end_page_writeback(struct page *page);

/*
 * Fault a userspace page into pagetables.  Return non-zero on a fault.
 *
 * This assumes that two userspace pages are always sufficient.  That's
 * not true if PAGE_CACHE_SIZE > PAGE_SIZE.
 */
static inline int fault_in_pages_writeable(char __user *uaddr, int size)
{
	int ret;

	/*
	 * Writing zeroes into userspace here is OK, because we know that if
	 * the zero gets there, we'll be overwriting it.
	 */
	ret = __put_user(0, uaddr);
	if (ret == 0) {
		char __user *end = uaddr + size - 1;

		/*
		 * If the page was already mapped, this will get a cache miss
		 * for sure, so try to avoid doing it.
		 */
		if (((unsigned long)uaddr & PAGE_MASK) !=
				((unsigned long)end & PAGE_MASK))
		 	ret = __put_user(0, end);
	}
	return ret;
}

static inline void fault_in_pages_readable(const char __user *uaddr, int size)
{
	volatile char c;
	int ret;

	ret = __get_user(c, uaddr);
	if (ret == 0) {
		const char __user *end = uaddr + size - 1;

		if (((unsigned long)uaddr & PAGE_MASK) !=
				((unsigned long)end & PAGE_MASK))
		 	__get_user(c, end);
	}
}

#endif /* _LINUX_PAGEMAP_H */
