/*
 * Macros for manipulating and testing page->flags
 */

#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

#include <linux/types.h>
#ifndef __GENERATING_BOUNDS_H
#include <linux/mm_types.h>
#include <linux/bounds.h>
#endif /* !__GENERATING_BOUNDS_H */

/*
 * Various page->flags bits:
 *
 * PG_reserved is set for special pages, which can never be swapped out. Some
 * of them might not even exist (eg empty_bad_page)...
 *
 * The PG_private bitflag is set on pagecache pages if they contain filesystem
 * specific data (which is normally at page->private). It can be used by
 * private allocations for its own usage.
 *
 * During initiation of disk I/O, PG_locked is set. This bit is set before I/O
 * and cleared when writeback _starts_ or when read _completes_. PG_writeback
 * is set before writeback starts and cleared when it finishes.
 *
 * PG_locked also pins a page in pagecache, and blocks truncation of the file
 * while it is held.
 *
 * page_waitqueue(page) is a wait queue of all tasks waiting for the page
 * to become unlocked.
 *
 * PG_uptodate tells whether the page's contents is valid.  When a read
 * completes, the page becomes uptodate, unless a disk I/O error happened.
 *
 * PG_referenced, PG_reclaim are used for page reclaim for anonymous and
 * file-backed pagecache (see mm/vmscan.c).
 *
 * PG_error is set to indicate that an I/O error occurred on this page.
 *
 * PG_arch_1 is an architecture specific page state bit.  The generic code
 * guarantees that this bit is cleared for a page when it first is entered into
 * the page cache.
 *
 * PG_highmem pages are not permanently mapped into the kernel virtual address
 * space, they need to be kmapped separately for doing IO on the pages.  The
 * struct page (these bits with information) are always mapped into kernel
 * address space...
 *
 * PG_buddy is set to indicate that the page is free and in the buddy system
 * (see mm/page_alloc.c).
 *
 */

/*
 * Don't use the *_dontuse flags.  Use the macros.  Otherwise you'll break
 * locked- and dirty-page accounting.
 *
 * The page flags field is split into two parts, the main flags area
 * which extends from the low bits upwards, and the fields area which
 * extends from the high bits downwards.
 *
 *  | FIELD | ... | FLAGS |
 *  N-1           ^       0
 *               (NR_PAGEFLAGS)
 *
 * The fields area is reserved for fields mapping zone, node (for NUMA) and
 * SPARSEMEM section (for variants of SPARSEMEM that require section ids like
 * SPARSEMEM_EXTREME with !SPARSEMEM_VMEMMAP).
 */
enum pageflags {
	PG_locked,		/* Page is locked. Don't touch. */
	PG_error,
	PG_referenced,
	PG_uptodate,
	PG_dirty,
	PG_lru,
	PG_active,
	PG_slab,
	PG_owner_priv_1,	/* Owner use. If pagecache, fs may use*/
	PG_arch_1,
	PG_reserved,
	PG_private,		/* If pagecache, has fs-private data */
	PG_writeback,		/* Page is under writeback */
	PG_compound,		/* A compound page */
	PG_swapcache,		/* Swap page: swp_entry_t in private */
	PG_mappedtodisk,	/* Has blocks allocated on-disk */
	PG_reclaim,		/* To be reclaimed asap */
	PG_buddy,		/* Page is free, on buddy lists */
#ifdef CONFIG_IA64_UNCACHED_ALLOCATOR
	PG_uncached,		/* Page has been mapped as uncached */
#endif
	__NR_PAGEFLAGS
};

#ifndef __GENERATING_BOUNDS_H

/*
 * Macros to create function definitions for page flags
 */
#define TESTPAGEFLAG(uname, lname)					\
static inline int Page##uname(struct page *page) 			\
			{ return test_bit(PG_##lname, &page->flags); }

#define SETPAGEFLAG(uname, lname)					\
static inline void SetPage##uname(struct page *page)			\
			{ set_bit(PG_##lname, &page->flags); }

#define CLEARPAGEFLAG(uname, lname)					\
static inline void ClearPage##uname(struct page *page)			\
			{ clear_bit(PG_##lname, &page->flags); }

#define __SETPAGEFLAG(uname, lname)					\
static inline void __SetPage##uname(struct page *page)			\
			{ __set_bit(PG_##lname, &page->flags); }

#define __CLEARPAGEFLAG(uname, lname)					\
static inline void __ClearPage##uname(struct page *page)		\
			{ __clear_bit(PG_##lname, &page->flags); }

#define TESTSETFLAG(uname, lname)					\
static inline int TestSetPage##uname(struct page *page)			\
		{ return test_and_set_bit(PG_##lname, &page->flags); }

#define TESTCLEARFLAG(uname, lname)					\
static inline int TestClearPage##uname(struct page *page)		\
		{ return test_and_clear_bit(PG_##lname, &page->flags); }


#define PAGEFLAG(uname, lname) TESTPAGEFLAG(uname, lname)		\
	SETPAGEFLAG(uname, lname) CLEARPAGEFLAG(uname, lname)

#define __PAGEFLAG(uname, lname) TESTPAGEFLAG(uname, lname)		\
	__SETPAGEFLAG(uname, lname)  __CLEARPAGEFLAG(uname, lname)

#define TESTSCFLAG(uname, lname)					\
	TESTSETFLAG(uname, lname) TESTCLEARFLAG(uname, lname)

struct page;	/* forward declaration */

PAGEFLAG(Locked, locked) TESTSCFLAG(Locked, locked)
PAGEFLAG(Error, error)
PAGEFLAG(Referenced, referenced) TESTCLEARFLAG(Referenced, referenced)
PAGEFLAG(Dirty, dirty) TESTSCFLAG(Dirty, dirty) __CLEARPAGEFLAG(Dirty, dirty)
PAGEFLAG(LRU, lru) __CLEARPAGEFLAG(LRU, lru)
PAGEFLAG(Active, active) __CLEARPAGEFLAG(Active, active)
__PAGEFLAG(Slab, slab)
PAGEFLAG(Checked, owner_priv_1)		/* Used by some filesystems */
PAGEFLAG(Pinned, owner_priv_1) TESTSCFLAG(Pinned, owner_priv_1) /* Xen */
PAGEFLAG(Reserved, reserved) __CLEARPAGEFLAG(Reserved, reserved)
PAGEFLAG(Private, private) __CLEARPAGEFLAG(Private, private)
	__SETPAGEFLAG(Private, private)

/*
 * Only test-and-set exist for PG_writeback.  The unconditional operators are
 * risky: they bypass page accounting.
 */
TESTPAGEFLAG(Writeback, writeback) TESTSCFLAG(Writeback, writeback)
__PAGEFLAG(Buddy, buddy)
PAGEFLAG(MappedToDisk, mappedtodisk)

/* PG_readahead is only used for file reads; PG_reclaim is only for writes */
PAGEFLAG(Reclaim, reclaim) TESTCLEARFLAG(Reclaim, reclaim)
PAGEFLAG(Readahead, reclaim)		/* Reminder to do async read-ahead */

#ifdef CONFIG_HIGHMEM
/*
 * Must use a macro here due to header dependency issues. page_zone() is not
 * available at this point.
 */
#define PageHighMem(__p) is_highmem(page_zone(__p))
#else
static inline int PageHighMem(struct page *page)
{
	return 0;
}
#endif

#ifdef CONFIG_SWAP
PAGEFLAG(SwapCache, swapcache)
#else
static inline int PageSwapCache(struct page *page)
{
	return 0;
}
#endif

#ifdef CONFIG_IA64_UNCACHED_ALLOCATOR
PAGEFLAG(Uncached, uncached)
#else
static inline int PageUncached(struct page *)
{
	return 0;
}
#endif

static inline int PageUptodate(struct page *page)
{
	int ret = test_bit(PG_uptodate, &(page)->flags);

	/*
	 * Must ensure that the data we read out of the page is loaded
	 * _after_ we've loaded page->flags to check for PageUptodate.
	 * We can skip the barrier if the page is not uptodate, because
	 * we wouldn't be reading anything from it.
	 *
	 * See SetPageUptodate() for the other side of the story.
	 */
	if (ret)
		smp_rmb();

	return ret;
}

static inline void __SetPageUptodate(struct page *page)
{
	smp_wmb();
	__set_bit(PG_uptodate, &(page)->flags);
#ifdef CONFIG_S390
	page_clear_dirty(page);
#endif
}

static inline void SetPageUptodate(struct page *page)
{
#ifdef CONFIG_S390
	if (!test_and_set_bit(PG_uptodate, &page->flags))
		page_clear_dirty(page);
#else
	/*
	 * Memory barrier must be issued before setting the PG_uptodate bit,
	 * so that all previous stores issued in order to bring the page
	 * uptodate are actually visible before PageUptodate becomes true.
	 *
	 * s390 doesn't need an explicit smp_wmb here because the test and
	 * set bit already provides full barriers.
	 */
	smp_wmb();
	set_bit(PG_uptodate, &(page)->flags);
#endif
}

CLEARPAGEFLAG(Uptodate, uptodate)

extern void cancel_dirty_page(struct page *page, unsigned int account_size);

int test_clear_page_writeback(struct page *page);
int test_set_page_writeback(struct page *page);

static inline void set_page_writeback(struct page *page)
{
	test_set_page_writeback(page);
}

TESTPAGEFLAG(Compound, compound)
__PAGEFLAG(Head, compound)

/*
 * PG_reclaim is used in combination with PG_compound to mark the
 * head and tail of a compound page. This saves one page flag
 * but makes it impossible to use compound pages for the page cache.
 * The PG_reclaim bit would have to be used for reclaim or readahead
 * if compound pages enter the page cache.
 *
 * PG_compound & PG_reclaim	=> Tail page
 * PG_compound & ~PG_reclaim	=> Head page
 */
#define PG_head_tail_mask ((1L << PG_compound) | (1L << PG_reclaim))

static inline int PageTail(struct page *page)
{
	return ((page->flags & PG_head_tail_mask) == PG_head_tail_mask);
}

static inline void __SetPageTail(struct page *page)
{
	page->flags |= PG_head_tail_mask;
}

static inline void __ClearPageTail(struct page *page)
{
	page->flags &= ~PG_head_tail_mask;
}

#endif /* !__GENERATING_BOUNDS_H */
#endif	/* PAGE_FLAGS_H */
