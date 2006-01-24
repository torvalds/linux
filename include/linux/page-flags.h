/*
 * Macros for manipulating and testing page->flags
 */

#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

#include <linux/percpu.h>
#include <linux/cache.h>
#include <asm/pgtable.h>

/*
 * Various page->flags bits:
 *
 * PG_reserved is set for special pages, which can never be swapped out. Some
 * of them might not even exist (eg empty_bad_page)...
 *
 * The PG_private bitflag is set if page->private contains a valid value.
 *
 * During disk I/O, PG_locked is used. This bit is set before I/O and
 * reset when I/O completes. page_waitqueue(page) is a wait queue of all tasks
 * waiting for the I/O on this page to complete.
 *
 * PG_uptodate tells whether the page's contents is valid.  When a read
 * completes, the page becomes uptodate, unless a disk I/O error happened.
 *
 * For choosing which pages to swap out, inode pages carry a PG_referenced bit,
 * which is set any time the system accesses that page through the (mapping,
 * index) hash table.  This referenced bit, together with the referenced bit
 * in the page tables, is used to manipulate page->age and move the page across
 * the active, inactive_dirty and inactive_clean lists.
 *
 * Note that the referenced bit, the page->lru list_head and the active,
 * inactive_dirty and inactive_clean lists are protected by the
 * zone->lru_lock, and *NOT* by the usual PG_locked bit!
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
 */

/*
 * Don't use the *_dontuse flags.  Use the macros.  Otherwise you'll break
 * locked- and dirty-page accounting.  The top eight bits of page->flags are
 * used for page->zone, so putting flag bits there doesn't work.
 */
#define PG_locked	 	 0	/* Page is locked. Don't touch. */
#define PG_error		 1
#define PG_referenced		 2
#define PG_uptodate		 3

#define PG_dirty	 	 4
#define PG_lru			 5
#define PG_active		 6
#define PG_slab			 7	/* slab debug (Suparna wants this) */

#define PG_checked		 8	/* kill me in 2.5.<early>. */
#define PG_arch_1		 9
#define PG_reserved		10
#define PG_private		11	/* Has something at ->private */

#define PG_writeback		12	/* Page is under writeback */
#define PG_nosave		13	/* Used for system suspend/resume */
#define PG_compound		14	/* Part of a compound page */
#define PG_swapcache		15	/* Swap page: swp_entry_t in private */

#define PG_mappedtodisk		16	/* Has blocks allocated on-disk */
#define PG_reclaim		17	/* To be reclaimed asap */
#define PG_nosave_free		18	/* Free, should not be written */
#define PG_uncached		19	/* Page has been mapped as uncached */

/*
 * Global page accounting.  One instance per CPU.  Only unsigned longs are
 * allowed.
 *
 * - Fields can be modified with xxx_page_state and xxx_page_state_zone at
 * any time safely (which protects the instance from modification by
 * interrupt.
 * - The __xxx_page_state variants can be used safely when interrupts are
 * disabled.
 * - The __xxx_page_state variants can be used if the field is only
 * modified from process context, or only modified from interrupt context.
 * In this case, the field should be commented here.
 */
struct page_state {
	unsigned long nr_dirty;		/* Dirty writeable pages */
	unsigned long nr_writeback;	/* Pages under writeback */
	unsigned long nr_unstable;	/* NFS unstable pages */
	unsigned long nr_page_table_pages;/* Pages used for pagetables */
	unsigned long nr_mapped;	/* mapped into pagetables.
					 * only modified from process context */
	unsigned long nr_slab;		/* In slab */
#define GET_PAGE_STATE_LAST nr_slab

	/*
	 * The below are zeroed by get_page_state().  Use get_full_page_state()
	 * to add up all these.
	 */
	unsigned long pgpgin;		/* Disk reads */
	unsigned long pgpgout;		/* Disk writes */
	unsigned long pswpin;		/* swap reads */
	unsigned long pswpout;		/* swap writes */

	unsigned long pgalloc_high;	/* page allocations */
	unsigned long pgalloc_normal;
	unsigned long pgalloc_dma32;
	unsigned long pgalloc_dma;

	unsigned long pgfree;		/* page freeings */
	unsigned long pgactivate;	/* pages moved inactive->active */
	unsigned long pgdeactivate;	/* pages moved active->inactive */

	unsigned long pgfault;		/* faults (major+minor) */
	unsigned long pgmajfault;	/* faults (major only) */

	unsigned long pgrefill_high;	/* inspected in refill_inactive_zone */
	unsigned long pgrefill_normal;
	unsigned long pgrefill_dma32;
	unsigned long pgrefill_dma;

	unsigned long pgsteal_high;	/* total highmem pages reclaimed */
	unsigned long pgsteal_normal;
	unsigned long pgsteal_dma32;
	unsigned long pgsteal_dma;

	unsigned long pgscan_kswapd_high;/* total highmem pages scanned */
	unsigned long pgscan_kswapd_normal;
	unsigned long pgscan_kswapd_dma32;
	unsigned long pgscan_kswapd_dma;

	unsigned long pgscan_direct_high;/* total highmem pages scanned */
	unsigned long pgscan_direct_normal;
	unsigned long pgscan_direct_dma32;
	unsigned long pgscan_direct_dma;

	unsigned long pginodesteal;	/* pages reclaimed via inode freeing */
	unsigned long slabs_scanned;	/* slab objects scanned */
	unsigned long kswapd_steal;	/* pages reclaimed by kswapd */
	unsigned long kswapd_inodesteal;/* reclaimed via kswapd inode freeing */
	unsigned long pageoutrun;	/* kswapd's calls to page reclaim */
	unsigned long allocstall;	/* direct reclaim calls */

	unsigned long pgrotated;	/* pages rotated to tail of the LRU */
	unsigned long nr_bounce;	/* pages for bounce buffers */
};

extern void get_page_state(struct page_state *ret);
extern void get_page_state_node(struct page_state *ret, int node);
extern void get_full_page_state(struct page_state *ret);
extern unsigned long read_page_state_offset(unsigned long offset);
extern void mod_page_state_offset(unsigned long offset, unsigned long delta);
extern void __mod_page_state_offset(unsigned long offset, unsigned long delta);

#define read_page_state(member) \
	read_page_state_offset(offsetof(struct page_state, member))

#define mod_page_state(member, delta)	\
	mod_page_state_offset(offsetof(struct page_state, member), (delta))

#define __mod_page_state(member, delta)	\
	__mod_page_state_offset(offsetof(struct page_state, member), (delta))

#define inc_page_state(member)		mod_page_state(member, 1UL)
#define dec_page_state(member)		mod_page_state(member, 0UL - 1)
#define add_page_state(member,delta)	mod_page_state(member, (delta))
#define sub_page_state(member,delta)	mod_page_state(member, 0UL - (delta))

#define __inc_page_state(member)	__mod_page_state(member, 1UL)
#define __dec_page_state(member)	__mod_page_state(member, 0UL - 1)
#define __add_page_state(member,delta)	__mod_page_state(member, (delta))
#define __sub_page_state(member,delta)	__mod_page_state(member, 0UL - (delta))

#define page_state(member) (*__page_state(offsetof(struct page_state, member)))

#define state_zone_offset(zone, member)					\
({									\
	unsigned offset;						\
	if (is_highmem(zone))						\
		offset = offsetof(struct page_state, member##_high);	\
	else if (is_normal(zone))					\
		offset = offsetof(struct page_state, member##_normal);	\
	else if (is_dma32(zone))					\
		offset = offsetof(struct page_state, member##_dma32);	\
	else								\
		offset = offsetof(struct page_state, member##_dma);	\
	offset;								\
})

#define __mod_page_state_zone(zone, member, delta)			\
 do {									\
	__mod_page_state_offset(state_zone_offset(zone, member), (delta)); \
 } while (0)

#define mod_page_state_zone(zone, member, delta)			\
 do {									\
	mod_page_state_offset(state_zone_offset(zone, member), (delta)); \
 } while (0)

/*
 * Manipulation of page state flags
 */
#define PageLocked(page)		\
		test_bit(PG_locked, &(page)->flags)
#define SetPageLocked(page)		\
		set_bit(PG_locked, &(page)->flags)
#define TestSetPageLocked(page)		\
		test_and_set_bit(PG_locked, &(page)->flags)
#define ClearPageLocked(page)		\
		clear_bit(PG_locked, &(page)->flags)
#define TestClearPageLocked(page)	\
		test_and_clear_bit(PG_locked, &(page)->flags)

#define PageError(page)		test_bit(PG_error, &(page)->flags)
#define SetPageError(page)	set_bit(PG_error, &(page)->flags)
#define ClearPageError(page)	clear_bit(PG_error, &(page)->flags)

#define PageReferenced(page)	test_bit(PG_referenced, &(page)->flags)
#define SetPageReferenced(page)	set_bit(PG_referenced, &(page)->flags)
#define ClearPageReferenced(page)	clear_bit(PG_referenced, &(page)->flags)
#define TestClearPageReferenced(page) test_and_clear_bit(PG_referenced, &(page)->flags)

#define PageUptodate(page)	test_bit(PG_uptodate, &(page)->flags)
#ifndef SetPageUptodate
#define SetPageUptodate(page)	set_bit(PG_uptodate, &(page)->flags)
#endif
#define ClearPageUptodate(page)	clear_bit(PG_uptodate, &(page)->flags)

#define PageDirty(page)		test_bit(PG_dirty, &(page)->flags)
#define SetPageDirty(page)	set_bit(PG_dirty, &(page)->flags)
#define TestSetPageDirty(page)	test_and_set_bit(PG_dirty, &(page)->flags)
#define ClearPageDirty(page)	clear_bit(PG_dirty, &(page)->flags)
#define __ClearPageDirty(page)	__clear_bit(PG_dirty, &(page)->flags)
#define TestClearPageDirty(page) test_and_clear_bit(PG_dirty, &(page)->flags)

#define SetPageLRU(page)	set_bit(PG_lru, &(page)->flags)
#define PageLRU(page)		test_bit(PG_lru, &(page)->flags)
#define TestSetPageLRU(page)	test_and_set_bit(PG_lru, &(page)->flags)
#define TestClearPageLRU(page)	test_and_clear_bit(PG_lru, &(page)->flags)

#define PageActive(page)	test_bit(PG_active, &(page)->flags)
#define SetPageActive(page)	set_bit(PG_active, &(page)->flags)
#define ClearPageActive(page)	clear_bit(PG_active, &(page)->flags)
#define TestClearPageActive(page) test_and_clear_bit(PG_active, &(page)->flags)
#define TestSetPageActive(page) test_and_set_bit(PG_active, &(page)->flags)

#define PageSlab(page)		test_bit(PG_slab, &(page)->flags)
#define SetPageSlab(page)	set_bit(PG_slab, &(page)->flags)
#define ClearPageSlab(page)	clear_bit(PG_slab, &(page)->flags)
#define TestClearPageSlab(page)	test_and_clear_bit(PG_slab, &(page)->flags)
#define TestSetPageSlab(page)	test_and_set_bit(PG_slab, &(page)->flags)

#ifdef CONFIG_HIGHMEM
#define PageHighMem(page)	is_highmem(page_zone(page))
#else
#define PageHighMem(page)	0 /* needed to optimize away at compile time */
#endif

#define PageChecked(page)	test_bit(PG_checked, &(page)->flags)
#define SetPageChecked(page)	set_bit(PG_checked, &(page)->flags)
#define ClearPageChecked(page)	clear_bit(PG_checked, &(page)->flags)

#define PageReserved(page)	test_bit(PG_reserved, &(page)->flags)
#define SetPageReserved(page)	set_bit(PG_reserved, &(page)->flags)
#define ClearPageReserved(page)	clear_bit(PG_reserved, &(page)->flags)
#define __ClearPageReserved(page)	__clear_bit(PG_reserved, &(page)->flags)

#define SetPagePrivate(page)	set_bit(PG_private, &(page)->flags)
#define ClearPagePrivate(page)	clear_bit(PG_private, &(page)->flags)
#define PagePrivate(page)	test_bit(PG_private, &(page)->flags)
#define __SetPagePrivate(page)  __set_bit(PG_private, &(page)->flags)
#define __ClearPagePrivate(page) __clear_bit(PG_private, &(page)->flags)

#define PageWriteback(page)	test_bit(PG_writeback, &(page)->flags)
#define SetPageWriteback(page)						\
	do {								\
		if (!test_and_set_bit(PG_writeback,			\
				&(page)->flags))			\
			inc_page_state(nr_writeback);			\
	} while (0)
#define TestSetPageWriteback(page)					\
	({								\
		int ret;						\
		ret = test_and_set_bit(PG_writeback,			\
					&(page)->flags);		\
		if (!ret)						\
			inc_page_state(nr_writeback);			\
		ret;							\
	})
#define ClearPageWriteback(page)					\
	do {								\
		if (test_and_clear_bit(PG_writeback,			\
				&(page)->flags))			\
			dec_page_state(nr_writeback);			\
	} while (0)
#define TestClearPageWriteback(page)					\
	({								\
		int ret;						\
		ret = test_and_clear_bit(PG_writeback,			\
				&(page)->flags);			\
		if (ret)						\
			dec_page_state(nr_writeback);			\
		ret;							\
	})

#define PageNosave(page)	test_bit(PG_nosave, &(page)->flags)
#define SetPageNosave(page)	set_bit(PG_nosave, &(page)->flags)
#define TestSetPageNosave(page)	test_and_set_bit(PG_nosave, &(page)->flags)
#define ClearPageNosave(page)		clear_bit(PG_nosave, &(page)->flags)
#define TestClearPageNosave(page)	test_and_clear_bit(PG_nosave, &(page)->flags)

#define PageNosaveFree(page)	test_bit(PG_nosave_free, &(page)->flags)
#define SetPageNosaveFree(page)	set_bit(PG_nosave_free, &(page)->flags)
#define ClearPageNosaveFree(page)		clear_bit(PG_nosave_free, &(page)->flags)

#define PageMappedToDisk(page)	test_bit(PG_mappedtodisk, &(page)->flags)
#define SetPageMappedToDisk(page) set_bit(PG_mappedtodisk, &(page)->flags)
#define ClearPageMappedToDisk(page) clear_bit(PG_mappedtodisk, &(page)->flags)

#define PageReclaim(page)	test_bit(PG_reclaim, &(page)->flags)
#define SetPageReclaim(page)	set_bit(PG_reclaim, &(page)->flags)
#define ClearPageReclaim(page)	clear_bit(PG_reclaim, &(page)->flags)
#define TestClearPageReclaim(page) test_and_clear_bit(PG_reclaim, &(page)->flags)

#define PageCompound(page)	test_bit(PG_compound, &(page)->flags)
#define SetPageCompound(page)	set_bit(PG_compound, &(page)->flags)
#define ClearPageCompound(page)	clear_bit(PG_compound, &(page)->flags)

#ifdef CONFIG_SWAP
#define PageSwapCache(page)	test_bit(PG_swapcache, &(page)->flags)
#define SetPageSwapCache(page)	set_bit(PG_swapcache, &(page)->flags)
#define ClearPageSwapCache(page) clear_bit(PG_swapcache, &(page)->flags)
#else
#define PageSwapCache(page)	0
#endif

#define PageUncached(page)	test_bit(PG_uncached, &(page)->flags)
#define SetPageUncached(page)	set_bit(PG_uncached, &(page)->flags)
#define ClearPageUncached(page)	clear_bit(PG_uncached, &(page)->flags)

struct page;	/* forward declaration */

int test_clear_page_dirty(struct page *page);
int test_clear_page_writeback(struct page *page);
int test_set_page_writeback(struct page *page);

static inline void clear_page_dirty(struct page *page)
{
	test_clear_page_dirty(page);
}

static inline void set_page_writeback(struct page *page)
{
	test_set_page_writeback(page);
}

#endif	/* PAGE_FLAGS_H */
