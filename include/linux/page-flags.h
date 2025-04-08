/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Macros for manipulating and testing page->flags
 */

#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

#include <linux/types.h>
#include <linux/bug.h>
#include <linux/mmdebug.h>
#ifndef __GENERATING_BOUNDS_H
#include <linux/mm_types.h>
#include <generated/bounds.h>
#endif /* !__GENERATING_BOUNDS_H */

/*
 * Various page->flags bits:
 *
 * PG_reserved is set for special pages. The "struct page" of such a page
 * should in general not be touched (e.g. set dirty) except by its owner.
 * Pages marked as PG_reserved include:
 * - Pages part of the kernel image (including vDSO) and similar (e.g. BIOS,
 *   initrd, HW tables)
 * - Pages reserved or allocated early during boot (before the page allocator
 *   was initialized). This includes (depending on the architecture) the
 *   initial vmemmap, initial page tables, crashkernel, elfcorehdr, and much
 *   much more. Once (if ever) freed, PG_reserved is cleared and they will
 *   be given to the page allocator.
 * - Pages falling into physical memory gaps - not IORESOURCE_SYSRAM. Trying
 *   to read/write these pages might end badly. Don't touch!
 * - The zero page(s)
 * - Pages allocated in the context of kexec/kdump (loaded kernel image,
 *   control pages, vmcoreinfo)
 * - MMIO/DMA pages. Some architectures don't allow to ioremap pages that are
 *   not marked PG_reserved (as they might be in use by somebody else who does
 *   not respect the caching strategy).
 * - MCA pages on ia64
 * - Pages holding CPU notes for POWER Firmware Assisted Dump
 * - Device memory (e.g. PMEM, DAX, HMM)
 * Some PG_reserved pages will be excluded from the hibernation image.
 * PG_reserved does in general not hinder anybody from dumping or swapping
 * and is no longer required for remap_pfn_range(). ioremap might require it.
 * Consequently, PG_reserved for a page mapped into user space can indicate
 * the zero page, the vDSO, MMIO pages or device memory.
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
 * PG_swapbacked is set when a page uses swap as a backing storage.  This are
 * usually PageAnon or shmem pages but please note that even anonymous pages
 * might lose their PG_swapbacked flag when they simply can be dropped (e.g. as
 * a result of MADV_FREE).
 *
 * PG_referenced, PG_reclaim are used for page reclaim for anonymous and
 * file-backed pagecache (see mm/vmscan.c).
 *
 * PG_arch_1 is an architecture specific page state bit.  The generic code
 * guarantees that this bit is cleared for a page when it first is entered into
 * the page cache.
 *
 * PG_hwpoison indicates that a page got corrupted in hardware and contains
 * data with incorrect ECC bits that triggered a machine check. Accessing is
 * not safe since it may cause another machine check. Don't touch!
 */

/*
 * Don't use the pageflags directly.  Use the PageFoo macros.
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
	PG_writeback,		/* Page is under writeback */
	PG_referenced,
	PG_uptodate,
	PG_dirty,
	PG_lru,
	PG_head,		/* Must be in bit 6 */
	PG_waiters,		/* Page has waiters, check its waitqueue. Must be bit #7 and in the same byte as "PG_locked" */
	PG_active,
	PG_workingset,
	PG_owner_priv_1,	/* Owner use. If pagecache, fs may use */
	PG_owner_2,		/* Owner use. If pagecache, fs may use */
	PG_arch_1,
	PG_reserved,
	PG_private,		/* If pagecache, has fs-private data */
	PG_private_2,		/* If pagecache, has fs aux data */
	PG_reclaim,		/* To be reclaimed asap */
	PG_swapbacked,		/* Page is backed by RAM/swap */
	PG_unevictable,		/* Page is "unevictable"  */
	PG_dropbehind,		/* drop pages on IO completion */
#ifdef CONFIG_MMU
	PG_mlocked,		/* Page is vma mlocked */
#endif
#ifdef CONFIG_MEMORY_FAILURE
	PG_hwpoison,		/* hardware poisoned page. Don't touch */
#endif
#if defined(CONFIG_PAGE_IDLE_FLAG) && defined(CONFIG_64BIT)
	PG_young,
	PG_idle,
#endif
#ifdef CONFIG_ARCH_USES_PG_ARCH_2
	PG_arch_2,
#endif
#ifdef CONFIG_ARCH_USES_PG_ARCH_3
	PG_arch_3,
#endif
	__NR_PAGEFLAGS,

	PG_readahead = PG_reclaim,

	/* Anonymous memory (and shmem) */
	PG_swapcache = PG_owner_priv_1, /* Swap page: swp_entry_t in private */
	/* Some filesystems */
	PG_checked = PG_owner_priv_1,

	/*
	 * Depending on the way an anonymous folio can be mapped into a page
	 * table (e.g., single PMD/PUD/CONT of the head page vs. PTE-mapped
	 * THP), PG_anon_exclusive may be set only for the head page or for
	 * tail pages of an anonymous folio. For now, we only expect it to be
	 * set on tail pages for PTE-mapped THP.
	 */
	PG_anon_exclusive = PG_owner_2,

	/*
	 * Set if all buffer heads in the folio are mapped.
	 * Filesystems which do not use BHs can use it for their own purpose.
	 */
	PG_mappedtodisk = PG_owner_2,

	/* Two page bits are conscripted by FS-Cache to maintain local caching
	 * state.  These bits are set on pages belonging to the netfs's inodes
	 * when those inodes are being locally cached.
	 */
	PG_fscache = PG_private_2,	/* page backed by cache */

	/* XEN */
	/* Pinned in Xen as a read-only pagetable page. */
	PG_pinned = PG_owner_priv_1,
	/* Pinned as part of domain save (see xen_mm_pin_all()). */
	PG_savepinned = PG_dirty,
	/* Has a grant mapping of another (foreign) domain's page. */
	PG_foreign = PG_owner_priv_1,
	/* Remapped by swiotlb-xen. */
	PG_xen_remapped = PG_owner_priv_1,

	/* non-lru isolated movable page */
	PG_isolated = PG_reclaim,

	/* Only valid for buddy pages. Used to track pages that are reported */
	PG_reported = PG_uptodate,

#ifdef CONFIG_MEMORY_HOTPLUG
	/* For self-hosted memmap pages */
	PG_vmemmap_self_hosted = PG_owner_priv_1,
#endif

	/*
	 * Flags only valid for compound pages.  Stored in first tail page's
	 * flags word.  Cannot use the first 8 flags or any flag marked as
	 * PF_ANY.
	 */

	/* At least one page in this folio has the hwpoison flag set */
	PG_has_hwpoisoned = PG_active,
	PG_large_rmappable = PG_workingset, /* anon or file-backed */
	PG_partially_mapped = PG_reclaim, /* was identified to be partially mapped */
};

#define PAGEFLAGS_MASK		((1UL << NR_PAGEFLAGS) - 1)

#ifndef __GENERATING_BOUNDS_H

#ifdef CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP
DECLARE_STATIC_KEY_FALSE(hugetlb_optimize_vmemmap_key);

/*
 * Return the real head page struct iff the @page is a fake head page, otherwise
 * return the @page itself. See Documentation/mm/vmemmap_dedup.rst.
 */
static __always_inline const struct page *page_fixed_fake_head(const struct page *page)
{
	if (!static_branch_unlikely(&hugetlb_optimize_vmemmap_key))
		return page;

	/*
	 * Only addresses aligned with PAGE_SIZE of struct page may be fake head
	 * struct page. The alignment check aims to avoid access the fields (
	 * e.g. compound_head) of the @page[1]. It can avoid touch a (possibly)
	 * cold cacheline in some cases.
	 */
	if (IS_ALIGNED((unsigned long)page, PAGE_SIZE) &&
	    test_bit(PG_head, &page->flags)) {
		/*
		 * We can safely access the field of the @page[1] with PG_head
		 * because the @page is a compound page composed with at least
		 * two contiguous pages.
		 */
		unsigned long head = READ_ONCE(page[1].compound_head);

		if (likely(head & 1))
			return (const struct page *)(head - 1);
	}
	return page;
}

static __always_inline bool page_count_writable(const struct page *page, int u)
{
	if (!static_branch_unlikely(&hugetlb_optimize_vmemmap_key))
		return true;

	/*
	 * The refcount check is ordered before the fake-head check to prevent
	 * the following race:
	 *   CPU 1 (HVO)                     CPU 2 (speculative PFN walker)
	 *
	 *   page_ref_freeze()
	 *   synchronize_rcu()
	 *                                   rcu_read_lock()
	 *                                   page_is_fake_head() is false
	 *   vmemmap_remap_pte()
	 *   XXX: struct page[] becomes r/o
	 *
	 *   page_ref_unfreeze()
	 *                                   page_ref_count() is not zero
	 *
	 *                                   atomic_add_unless(&page->_refcount)
	 *                                   XXX: try to modify r/o struct page[]
	 *
	 * The refcount check also prevents modification attempts to other (r/o)
	 * tail pages that are not fake heads.
	 */
	if (atomic_read_acquire(&page->_refcount) == u)
		return false;

	return page_fixed_fake_head(page) == page;
}
#else
static inline const struct page *page_fixed_fake_head(const struct page *page)
{
	return page;
}

static inline bool page_count_writable(const struct page *page, int u)
{
	return true;
}
#endif

static __always_inline int page_is_fake_head(const struct page *page)
{
	return page_fixed_fake_head(page) != page;
}

static __always_inline unsigned long _compound_head(const struct page *page)
{
	unsigned long head = READ_ONCE(page->compound_head);

	if (unlikely(head & 1))
		return head - 1;
	return (unsigned long)page_fixed_fake_head(page);
}

#define compound_head(page)	((typeof(page))_compound_head(page))

/**
 * page_folio - Converts from page to folio.
 * @p: The page.
 *
 * Every page is part of a folio.  This function cannot be called on a
 * NULL pointer.
 *
 * Context: No reference, nor lock is required on @page.  If the caller
 * does not hold a reference, this call may race with a folio split, so
 * it should re-check the folio still contains this page after gaining
 * a reference on the folio.
 * Return: The folio which contains this page.
 */
#define page_folio(p)		(_Generic((p),				\
	const struct page *:	(const struct folio *)_compound_head(p), \
	struct page *:		(struct folio *)_compound_head(p)))

/**
 * folio_page - Return a page from a folio.
 * @folio: The folio.
 * @n: The page number to return.
 *
 * @n is relative to the start of the folio.  This function does not
 * check that the page number lies within @folio; the caller is presumed
 * to have a reference to the page.
 */
#define folio_page(folio, n)	nth_page(&(folio)->page, n)

static __always_inline int PageTail(const struct page *page)
{
	return READ_ONCE(page->compound_head) & 1 || page_is_fake_head(page);
}

static __always_inline int PageCompound(const struct page *page)
{
	return test_bit(PG_head, &page->flags) ||
	       READ_ONCE(page->compound_head) & 1;
}

#define	PAGE_POISON_PATTERN	-1l
static inline int PagePoisoned(const struct page *page)
{
	return READ_ONCE(page->flags) == PAGE_POISON_PATTERN;
}

#ifdef CONFIG_DEBUG_VM
void page_init_poison(struct page *page, size_t size);
#else
static inline void page_init_poison(struct page *page, size_t size)
{
}
#endif

static const unsigned long *const_folio_flags(const struct folio *folio,
		unsigned n)
{
	const struct page *page = &folio->page;

	VM_BUG_ON_PGFLAGS(page->compound_head & 1, page);
	VM_BUG_ON_PGFLAGS(n > 0 && !test_bit(PG_head, &page->flags), page);
	return &page[n].flags;
}

static unsigned long *folio_flags(struct folio *folio, unsigned n)
{
	struct page *page = &folio->page;

	VM_BUG_ON_PGFLAGS(page->compound_head & 1, page);
	VM_BUG_ON_PGFLAGS(n > 0 && !test_bit(PG_head, &page->flags), page);
	return &page[n].flags;
}

/*
 * Page flags policies wrt compound pages
 *
 * PF_POISONED_CHECK
 *     check if this struct page poisoned/uninitialized
 *
 * PF_ANY:
 *     the page flag is relevant for small, head and tail pages.
 *
 * PF_HEAD:
 *     for compound page all operations related to the page flag applied to
 *     head page.
 *
 * PF_NO_TAIL:
 *     modifications of the page flag must be done on small or head pages,
 *     checks can be done on tail pages too.
 *
 * PF_NO_COMPOUND:
 *     the page flag is not relevant for compound pages.
 *
 * PF_SECOND:
 *     the page flag is stored in the first tail page.
 */
#define PF_POISONED_CHECK(page) ({					\
		VM_BUG_ON_PGFLAGS(PagePoisoned(page), page);		\
		page; })
#define PF_ANY(page, enforce)	PF_POISONED_CHECK(page)
#define PF_HEAD(page, enforce)	PF_POISONED_CHECK(compound_head(page))
#define PF_NO_TAIL(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(enforce && PageTail(page), page);	\
		PF_POISONED_CHECK(compound_head(page)); })
#define PF_NO_COMPOUND(page, enforce) ({				\
		VM_BUG_ON_PGFLAGS(enforce && PageCompound(page), page);	\
		PF_POISONED_CHECK(page); })
#define PF_SECOND(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(!PageHead(page), page);		\
		PF_POISONED_CHECK(&page[1]); })

/* Which page is the flag stored in */
#define FOLIO_PF_ANY		0
#define FOLIO_PF_HEAD		0
#define FOLIO_PF_NO_TAIL	0
#define FOLIO_PF_NO_COMPOUND	0
#define FOLIO_PF_SECOND		1

#define FOLIO_HEAD_PAGE		0
#define FOLIO_SECOND_PAGE	1

/*
 * Macros to create function definitions for page flags
 */
#define FOLIO_TEST_FLAG(name, page)					\
static __always_inline bool folio_test_##name(const struct folio *folio) \
{ return test_bit(PG_##name, const_folio_flags(folio, page)); }

#define FOLIO_SET_FLAG(name, page)					\
static __always_inline void folio_set_##name(struct folio *folio)	\
{ set_bit(PG_##name, folio_flags(folio, page)); }

#define FOLIO_CLEAR_FLAG(name, page)					\
static __always_inline void folio_clear_##name(struct folio *folio)	\
{ clear_bit(PG_##name, folio_flags(folio, page)); }

#define __FOLIO_SET_FLAG(name, page)					\
static __always_inline void __folio_set_##name(struct folio *folio)	\
{ __set_bit(PG_##name, folio_flags(folio, page)); }

#define __FOLIO_CLEAR_FLAG(name, page)					\
static __always_inline void __folio_clear_##name(struct folio *folio)	\
{ __clear_bit(PG_##name, folio_flags(folio, page)); }

#define FOLIO_TEST_SET_FLAG(name, page)					\
static __always_inline bool folio_test_set_##name(struct folio *folio)	\
{ return test_and_set_bit(PG_##name, folio_flags(folio, page)); }

#define FOLIO_TEST_CLEAR_FLAG(name, page)				\
static __always_inline bool folio_test_clear_##name(struct folio *folio) \
{ return test_and_clear_bit(PG_##name, folio_flags(folio, page)); }

#define FOLIO_FLAG(name, page)						\
FOLIO_TEST_FLAG(name, page)						\
FOLIO_SET_FLAG(name, page)						\
FOLIO_CLEAR_FLAG(name, page)

#define TESTPAGEFLAG(uname, lname, policy)				\
FOLIO_TEST_FLAG(lname, FOLIO_##policy)					\
static __always_inline int Page##uname(const struct page *page)		\
{ return test_bit(PG_##lname, &policy(page, 0)->flags); }

#define SETPAGEFLAG(uname, lname, policy)				\
FOLIO_SET_FLAG(lname, FOLIO_##policy)					\
static __always_inline void SetPage##uname(struct page *page)		\
{ set_bit(PG_##lname, &policy(page, 1)->flags); }

#define CLEARPAGEFLAG(uname, lname, policy)				\
FOLIO_CLEAR_FLAG(lname, FOLIO_##policy)					\
static __always_inline void ClearPage##uname(struct page *page)		\
{ clear_bit(PG_##lname, &policy(page, 1)->flags); }

#define __SETPAGEFLAG(uname, lname, policy)				\
__FOLIO_SET_FLAG(lname, FOLIO_##policy)					\
static __always_inline void __SetPage##uname(struct page *page)		\
{ __set_bit(PG_##lname, &policy(page, 1)->flags); }

#define __CLEARPAGEFLAG(uname, lname, policy)				\
__FOLIO_CLEAR_FLAG(lname, FOLIO_##policy)				\
static __always_inline void __ClearPage##uname(struct page *page)	\
{ __clear_bit(PG_##lname, &policy(page, 1)->flags); }

#define TESTSETFLAG(uname, lname, policy)				\
FOLIO_TEST_SET_FLAG(lname, FOLIO_##policy)				\
static __always_inline int TestSetPage##uname(struct page *page)	\
{ return test_and_set_bit(PG_##lname, &policy(page, 1)->flags); }

#define TESTCLEARFLAG(uname, lname, policy)				\
FOLIO_TEST_CLEAR_FLAG(lname, FOLIO_##policy)				\
static __always_inline int TestClearPage##uname(struct page *page)	\
{ return test_and_clear_bit(PG_##lname, &policy(page, 1)->flags); }

#define PAGEFLAG(uname, lname, policy)					\
	TESTPAGEFLAG(uname, lname, policy)				\
	SETPAGEFLAG(uname, lname, policy)				\
	CLEARPAGEFLAG(uname, lname, policy)

#define __PAGEFLAG(uname, lname, policy)				\
	TESTPAGEFLAG(uname, lname, policy)				\
	__SETPAGEFLAG(uname, lname, policy)				\
	__CLEARPAGEFLAG(uname, lname, policy)

#define TESTSCFLAG(uname, lname, policy)				\
	TESTSETFLAG(uname, lname, policy)				\
	TESTCLEARFLAG(uname, lname, policy)

#define FOLIO_TEST_FLAG_FALSE(name)					\
static inline bool folio_test_##name(const struct folio *folio)		\
{ return false; }
#define FOLIO_SET_FLAG_NOOP(name)					\
static inline void folio_set_##name(struct folio *folio) { }
#define FOLIO_CLEAR_FLAG_NOOP(name)					\
static inline void folio_clear_##name(struct folio *folio) { }
#define __FOLIO_SET_FLAG_NOOP(name)					\
static inline void __folio_set_##name(struct folio *folio) { }
#define __FOLIO_CLEAR_FLAG_NOOP(name)					\
static inline void __folio_clear_##name(struct folio *folio) { }
#define FOLIO_TEST_SET_FLAG_FALSE(name)					\
static inline bool folio_test_set_##name(struct folio *folio)		\
{ return false; }
#define FOLIO_TEST_CLEAR_FLAG_FALSE(name)				\
static inline bool folio_test_clear_##name(struct folio *folio)		\
{ return false; }

#define FOLIO_FLAG_FALSE(name)						\
FOLIO_TEST_FLAG_FALSE(name)						\
FOLIO_SET_FLAG_NOOP(name)						\
FOLIO_CLEAR_FLAG_NOOP(name)

#define TESTPAGEFLAG_FALSE(uname, lname)				\
FOLIO_TEST_FLAG_FALSE(lname)						\
static inline int Page##uname(const struct page *page) { return 0; }

#define SETPAGEFLAG_NOOP(uname, lname)					\
FOLIO_SET_FLAG_NOOP(lname)						\
static inline void SetPage##uname(struct page *page) {  }

#define CLEARPAGEFLAG_NOOP(uname, lname)				\
FOLIO_CLEAR_FLAG_NOOP(lname)						\
static inline void ClearPage##uname(struct page *page) {  }

#define __CLEARPAGEFLAG_NOOP(uname, lname)				\
__FOLIO_CLEAR_FLAG_NOOP(lname)						\
static inline void __ClearPage##uname(struct page *page) {  }

#define TESTSETFLAG_FALSE(uname, lname)					\
FOLIO_TEST_SET_FLAG_FALSE(lname)					\
static inline int TestSetPage##uname(struct page *page) { return 0; }

#define TESTCLEARFLAG_FALSE(uname, lname)				\
FOLIO_TEST_CLEAR_FLAG_FALSE(lname)					\
static inline int TestClearPage##uname(struct page *page) { return 0; }

#define PAGEFLAG_FALSE(uname, lname) TESTPAGEFLAG_FALSE(uname, lname)	\
	SETPAGEFLAG_NOOP(uname, lname) CLEARPAGEFLAG_NOOP(uname, lname)

#define TESTSCFLAG_FALSE(uname, lname)					\
	TESTSETFLAG_FALSE(uname, lname) TESTCLEARFLAG_FALSE(uname, lname)

__PAGEFLAG(Locked, locked, PF_NO_TAIL)
FOLIO_FLAG(waiters, FOLIO_HEAD_PAGE)
FOLIO_FLAG(referenced, FOLIO_HEAD_PAGE)
	FOLIO_TEST_CLEAR_FLAG(referenced, FOLIO_HEAD_PAGE)
	__FOLIO_SET_FLAG(referenced, FOLIO_HEAD_PAGE)
PAGEFLAG(Dirty, dirty, PF_HEAD) TESTSCFLAG(Dirty, dirty, PF_HEAD)
	__CLEARPAGEFLAG(Dirty, dirty, PF_HEAD)
PAGEFLAG(LRU, lru, PF_HEAD) __CLEARPAGEFLAG(LRU, lru, PF_HEAD)
	TESTCLEARFLAG(LRU, lru, PF_HEAD)
FOLIO_FLAG(active, FOLIO_HEAD_PAGE)
	__FOLIO_CLEAR_FLAG(active, FOLIO_HEAD_PAGE)
	FOLIO_TEST_CLEAR_FLAG(active, FOLIO_HEAD_PAGE)
PAGEFLAG(Workingset, workingset, PF_HEAD)
	TESTCLEARFLAG(Workingset, workingset, PF_HEAD)
PAGEFLAG(Checked, checked, PF_NO_COMPOUND)	   /* Used by some filesystems */

/* Xen */
PAGEFLAG(Pinned, pinned, PF_NO_COMPOUND)
	TESTSCFLAG(Pinned, pinned, PF_NO_COMPOUND)
PAGEFLAG(SavePinned, savepinned, PF_NO_COMPOUND);
PAGEFLAG(Foreign, foreign, PF_NO_COMPOUND);
PAGEFLAG(XenRemapped, xen_remapped, PF_NO_COMPOUND)
	TESTCLEARFLAG(XenRemapped, xen_remapped, PF_NO_COMPOUND)

PAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
	__CLEARPAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
	__SETPAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
FOLIO_FLAG(swapbacked, FOLIO_HEAD_PAGE)
	__FOLIO_CLEAR_FLAG(swapbacked, FOLIO_HEAD_PAGE)
	__FOLIO_SET_FLAG(swapbacked, FOLIO_HEAD_PAGE)

/*
 * Private page markings that may be used by the filesystem that owns the page
 * for its own purposes.
 * - PG_private and PG_private_2 cause release_folio() and co to be invoked
 */
PAGEFLAG(Private, private, PF_ANY)
FOLIO_FLAG(private_2, FOLIO_HEAD_PAGE)

/* owner_2 can be set on tail pages for anon memory */
FOLIO_FLAG(owner_2, FOLIO_HEAD_PAGE)

/*
 * Only test-and-set exist for PG_writeback.  The unconditional operators are
 * risky: they bypass page accounting.
 */
TESTPAGEFLAG(Writeback, writeback, PF_NO_TAIL)
	TESTSCFLAG(Writeback, writeback, PF_NO_TAIL)
FOLIO_FLAG(mappedtodisk, FOLIO_HEAD_PAGE)

/* PG_readahead is only used for reads; PG_reclaim is only for writes */
PAGEFLAG(Reclaim, reclaim, PF_NO_TAIL)
	TESTCLEARFLAG(Reclaim, reclaim, PF_NO_TAIL)
FOLIO_FLAG(readahead, FOLIO_HEAD_PAGE)
	FOLIO_TEST_CLEAR_FLAG(readahead, FOLIO_HEAD_PAGE)

FOLIO_FLAG(dropbehind, FOLIO_HEAD_PAGE)
	FOLIO_TEST_CLEAR_FLAG(dropbehind, FOLIO_HEAD_PAGE)
	__FOLIO_SET_FLAG(dropbehind, FOLIO_HEAD_PAGE)

#ifdef CONFIG_HIGHMEM
/*
 * Must use a macro here due to header dependency issues. page_zone() is not
 * available at this point.
 */
#define PageHighMem(__p) is_highmem_idx(page_zonenum(__p))
#define folio_test_highmem(__f)	is_highmem_idx(folio_zonenum(__f))
#else
PAGEFLAG_FALSE(HighMem, highmem)
#endif

#ifdef CONFIG_SWAP
static __always_inline bool folio_test_swapcache(const struct folio *folio)
{
	return folio_test_swapbacked(folio) &&
			test_bit(PG_swapcache, const_folio_flags(folio, 0));
}

FOLIO_SET_FLAG(swapcache, FOLIO_HEAD_PAGE)
FOLIO_CLEAR_FLAG(swapcache, FOLIO_HEAD_PAGE)
#else
FOLIO_FLAG_FALSE(swapcache)
#endif

FOLIO_FLAG(unevictable, FOLIO_HEAD_PAGE)
	__FOLIO_CLEAR_FLAG(unevictable, FOLIO_HEAD_PAGE)
	FOLIO_TEST_CLEAR_FLAG(unevictable, FOLIO_HEAD_PAGE)

#ifdef CONFIG_MMU
FOLIO_FLAG(mlocked, FOLIO_HEAD_PAGE)
	__FOLIO_CLEAR_FLAG(mlocked, FOLIO_HEAD_PAGE)
	FOLIO_TEST_CLEAR_FLAG(mlocked, FOLIO_HEAD_PAGE)
	FOLIO_TEST_SET_FLAG(mlocked, FOLIO_HEAD_PAGE)
#else
FOLIO_FLAG_FALSE(mlocked)
	__FOLIO_CLEAR_FLAG_NOOP(mlocked)
	FOLIO_TEST_CLEAR_FLAG_FALSE(mlocked)
	FOLIO_TEST_SET_FLAG_FALSE(mlocked)
#endif

#ifdef CONFIG_MEMORY_FAILURE
PAGEFLAG(HWPoison, hwpoison, PF_ANY)
TESTSCFLAG(HWPoison, hwpoison, PF_ANY)
#define __PG_HWPOISON (1UL << PG_hwpoison)
#else
PAGEFLAG_FALSE(HWPoison, hwpoison)
#define __PG_HWPOISON 0
#endif

#ifdef CONFIG_PAGE_IDLE_FLAG
#ifdef CONFIG_64BIT
FOLIO_TEST_FLAG(young, FOLIO_HEAD_PAGE)
FOLIO_SET_FLAG(young, FOLIO_HEAD_PAGE)
FOLIO_TEST_CLEAR_FLAG(young, FOLIO_HEAD_PAGE)
FOLIO_FLAG(idle, FOLIO_HEAD_PAGE)
#endif
/* See page_idle.h for !64BIT workaround */
#else /* !CONFIG_PAGE_IDLE_FLAG */
FOLIO_FLAG_FALSE(young)
FOLIO_TEST_CLEAR_FLAG_FALSE(young)
FOLIO_FLAG_FALSE(idle)
#endif

/*
 * PageReported() is used to track reported free pages within the Buddy
 * allocator. We can use the non-atomic version of the test and set
 * operations as both should be shielded with the zone lock to prevent
 * any possible races on the setting or clearing of the bit.
 */
__PAGEFLAG(Reported, reported, PF_NO_COMPOUND)

#ifdef CONFIG_MEMORY_HOTPLUG
PAGEFLAG(VmemmapSelfHosted, vmemmap_self_hosted, PF_ANY)
#else
PAGEFLAG_FALSE(VmemmapSelfHosted, vmemmap_self_hosted)
#endif

/*
 * On an anonymous folio mapped into a user virtual memory area,
 * folio->mapping points to its anon_vma, not to a struct address_space;
 * with the PAGE_MAPPING_ANON bit set to distinguish it.  See rmap.h.
 *
 * On an anonymous page in a VM_MERGEABLE area, if CONFIG_KSM is enabled,
 * the PAGE_MAPPING_MOVABLE bit may be set along with the PAGE_MAPPING_ANON
 * bit; and then folio->mapping points, not to an anon_vma, but to a private
 * structure which KSM associates with that merged page.  See ksm.h.
 *
 * PAGE_MAPPING_KSM without PAGE_MAPPING_ANON is used for non-lru movable
 * page and then folio->mapping points to a struct movable_operations.
 *
 * Please note that, confusingly, "folio_mapping" refers to the inode
 * address_space which maps the folio from disk; whereas "folio_mapped"
 * refers to user virtual address space into which the folio is mapped.
 *
 * For slab pages, since slab reuses the bits in struct page to store its
 * internal states, the folio->mapping does not exist as such, nor do
 * these flags below.  So in order to avoid testing non-existent bits,
 * please make sure that folio_test_slab(folio) actually evaluates to
 * false before calling the following functions (e.g., folio_test_anon).
 * See mm/slab.h.
 */
#define PAGE_MAPPING_ANON	0x1
#define PAGE_MAPPING_MOVABLE	0x2
#define PAGE_MAPPING_KSM	(PAGE_MAPPING_ANON | PAGE_MAPPING_MOVABLE)
#define PAGE_MAPPING_FLAGS	(PAGE_MAPPING_ANON | PAGE_MAPPING_MOVABLE)

static __always_inline bool folio_mapping_flags(const struct folio *folio)
{
	return ((unsigned long)folio->mapping & PAGE_MAPPING_FLAGS) != 0;
}

static __always_inline bool PageMappingFlags(const struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) != 0;
}

static __always_inline bool folio_test_anon(const struct folio *folio)
{
	return ((unsigned long)folio->mapping & PAGE_MAPPING_ANON) != 0;
}

static __always_inline bool PageAnonNotKsm(const struct page *page)
{
	unsigned long flags = (unsigned long)page_folio(page)->mapping;

	return (flags & PAGE_MAPPING_FLAGS) == PAGE_MAPPING_ANON;
}

static __always_inline bool PageAnon(const struct page *page)
{
	return folio_test_anon(page_folio(page));
}

static __always_inline bool __folio_test_movable(const struct folio *folio)
{
	return ((unsigned long)folio->mapping & PAGE_MAPPING_FLAGS) ==
			PAGE_MAPPING_MOVABLE;
}

static __always_inline bool __PageMovable(const struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) ==
				PAGE_MAPPING_MOVABLE;
}

#ifdef CONFIG_KSM
/*
 * A KSM page is one of those write-protected "shared pages" or "merged pages"
 * which KSM maps into multiple mms, wherever identical anonymous page content
 * is found in VM_MERGEABLE vmas.  It's a PageAnon page, pointing not to any
 * anon_vma, but to that page's node of the stable tree.
 */
static __always_inline bool folio_test_ksm(const struct folio *folio)
{
	return ((unsigned long)folio->mapping & PAGE_MAPPING_FLAGS) ==
				PAGE_MAPPING_KSM;
}
#else
FOLIO_TEST_FLAG_FALSE(ksm)
#endif

u64 stable_page_flags(const struct page *page);

/**
 * folio_xor_flags_has_waiters - Change some folio flags.
 * @folio: The folio.
 * @mask: Bits set in this word will be changed.
 *
 * This must only be used for flags which are changed with the folio
 * lock held.  For example, it is unsafe to use for PG_dirty as that
 * can be set without the folio lock held.  It can also only be used
 * on flags which are in the range 0-6 as some of the implementations
 * only affect those bits.
 *
 * Return: Whether there are tasks waiting on the folio.
 */
static inline bool folio_xor_flags_has_waiters(struct folio *folio,
		unsigned long mask)
{
	return xor_unlock_is_negative_byte(mask, folio_flags(folio, 0));
}

/**
 * folio_test_uptodate - Is this folio up to date?
 * @folio: The folio.
 *
 * The uptodate flag is set on a folio when every byte in the folio is
 * at least as new as the corresponding bytes on storage.  Anonymous
 * and CoW folios are always uptodate.  If the folio is not uptodate,
 * some of the bytes in it may be; see the is_partially_uptodate()
 * address_space operation.
 */
static inline bool folio_test_uptodate(const struct folio *folio)
{
	bool ret = test_bit(PG_uptodate, const_folio_flags(folio, 0));
	/*
	 * Must ensure that the data we read out of the folio is loaded
	 * _after_ we've loaded folio->flags to check the uptodate bit.
	 * We can skip the barrier if the folio is not uptodate, because
	 * we wouldn't be reading anything from it.
	 *
	 * See folio_mark_uptodate() for the other side of the story.
	 */
	if (ret)
		smp_rmb();

	return ret;
}

static inline bool PageUptodate(const struct page *page)
{
	return folio_test_uptodate(page_folio(page));
}

static __always_inline void __folio_mark_uptodate(struct folio *folio)
{
	smp_wmb();
	__set_bit(PG_uptodate, folio_flags(folio, 0));
}

static __always_inline void folio_mark_uptodate(struct folio *folio)
{
	/*
	 * Memory barrier must be issued before setting the PG_uptodate bit,
	 * so that all previous stores issued in order to bring the folio
	 * uptodate are actually visible before folio_test_uptodate becomes true.
	 */
	smp_wmb();
	set_bit(PG_uptodate, folio_flags(folio, 0));
}

static __always_inline void __SetPageUptodate(struct page *page)
{
	__folio_mark_uptodate((struct folio *)page);
}

static __always_inline void SetPageUptodate(struct page *page)
{
	folio_mark_uptodate((struct folio *)page);
}

CLEARPAGEFLAG(Uptodate, uptodate, PF_NO_TAIL)

void __folio_start_writeback(struct folio *folio, bool keep_write);
void set_page_writeback(struct page *page);

#define folio_start_writeback(folio)			\
	__folio_start_writeback(folio, false)
#define folio_start_writeback_keepwrite(folio)	\
	__folio_start_writeback(folio, true)

static __always_inline bool folio_test_head(const struct folio *folio)
{
	return test_bit(PG_head, const_folio_flags(folio, FOLIO_PF_ANY));
}

static __always_inline int PageHead(const struct page *page)
{
	PF_POISONED_CHECK(page);
	return test_bit(PG_head, &page->flags) && !page_is_fake_head(page);
}

__SETPAGEFLAG(Head, head, PF_ANY)
__CLEARPAGEFLAG(Head, head, PF_ANY)
CLEARPAGEFLAG(Head, head, PF_ANY)

/**
 * folio_test_large() - Does this folio contain more than one page?
 * @folio: The folio to test.
 *
 * Return: True if the folio is larger than one page.
 */
static inline bool folio_test_large(const struct folio *folio)
{
	return folio_test_head(folio);
}

static __always_inline void set_compound_head(struct page *page, struct page *head)
{
	WRITE_ONCE(page->compound_head, (unsigned long)head + 1);
}

static __always_inline void clear_compound_head(struct page *page)
{
	WRITE_ONCE(page->compound_head, 0);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline void ClearPageCompound(struct page *page)
{
	BUG_ON(!PageHead(page));
	ClearPageHead(page);
}
FOLIO_FLAG(large_rmappable, FOLIO_SECOND_PAGE)
FOLIO_FLAG(partially_mapped, FOLIO_SECOND_PAGE)
#else
FOLIO_FLAG_FALSE(large_rmappable)
FOLIO_FLAG_FALSE(partially_mapped)
#endif

#define PG_head_mask ((1UL << PG_head))

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 * PageHuge() only returns true for hugetlbfs pages, but not for
 * normal or transparent huge pages.
 *
 * PageTransHuge() returns true for both transparent huge and
 * hugetlbfs pages, but not normal pages. PageTransHuge() can only be
 * called only in the core VM paths where hugetlbfs pages can't exist.
 */
static inline int PageTransHuge(const struct page *page)
{
	VM_BUG_ON_PAGE(PageTail(page), page);
	return PageHead(page);
}

/*
 * PageTransCompound returns true for both transparent huge pages
 * and hugetlbfs pages, so it should only be called when it's known
 * that hugetlbfs pages aren't involved.
 */
static inline int PageTransCompound(const struct page *page)
{
	return PageCompound(page);
}
#else
TESTPAGEFLAG_FALSE(TransHuge, transhuge)
TESTPAGEFLAG_FALSE(TransCompound, transcompound)
#endif

#if defined(CONFIG_MEMORY_FAILURE) && defined(CONFIG_TRANSPARENT_HUGEPAGE)
/*
 * PageHasHWPoisoned indicates that at least one subpage is hwpoisoned in the
 * compound page.
 *
 * This flag is set by hwpoison handler.  Cleared by THP split or free page.
 */
FOLIO_FLAG(has_hwpoisoned, FOLIO_SECOND_PAGE)
#else
FOLIO_FLAG_FALSE(has_hwpoisoned)
#endif

/*
 * For pages that do not use mapcount, page_type may be used.
 * The low 24 bits of pagetype may be used for your own purposes, as long
 * as you are careful to not affect the top 8 bits.  The low bits of
 * pagetype will be overwritten when you clear the page_type from the page.
 */
enum pagetype {
	/* 0x00-0x7f are positive numbers, ie mapcount */
	/* Reserve 0x80-0xef for mapcount overflow. */
	PGTY_buddy		= 0xf0,
	PGTY_offline		= 0xf1,
	PGTY_table		= 0xf2,
	PGTY_guard		= 0xf3,
	PGTY_hugetlb		= 0xf4,
	PGTY_slab		= 0xf5,
	PGTY_zsmalloc		= 0xf6,
	PGTY_unaccepted		= 0xf7,
	PGTY_large_kmalloc	= 0xf8,

	PGTY_mapcount_underflow = 0xff
};

static inline bool page_type_has_type(int page_type)
{
	return page_type < (PGTY_mapcount_underflow << 24);
}

/* This takes a mapcount which is one more than page->_mapcount */
static inline bool page_mapcount_is_type(unsigned int mapcount)
{
	return page_type_has_type(mapcount - 1);
}

static inline bool page_has_type(const struct page *page)
{
	return page_mapcount_is_type(data_race(page->page_type));
}

#define FOLIO_TYPE_OPS(lname, fname)					\
static __always_inline bool folio_test_##fname(const struct folio *folio) \
{									\
	return data_race(folio->page.page_type >> 24) == PGTY_##lname;	\
}									\
static __always_inline void __folio_set_##fname(struct folio *folio)	\
{									\
	if (folio_test_##fname(folio))					\
		return;							\
	VM_BUG_ON_FOLIO(data_race(folio->page.page_type) != UINT_MAX,	\
			folio);						\
	folio->page.page_type = (unsigned int)PGTY_##lname << 24;	\
}									\
static __always_inline void __folio_clear_##fname(struct folio *folio)	\
{									\
	if (folio->page.page_type == UINT_MAX)				\
		return;							\
	VM_BUG_ON_FOLIO(!folio_test_##fname(folio), folio);		\
	folio->page.page_type = UINT_MAX;				\
}

#define PAGE_TYPE_OPS(uname, lname, fname)				\
FOLIO_TYPE_OPS(lname, fname)						\
static __always_inline int Page##uname(const struct page *page)		\
{									\
	return data_race(page->page_type >> 24) == PGTY_##lname;	\
}									\
static __always_inline void __SetPage##uname(struct page *page)		\
{									\
	if (Page##uname(page))						\
		return;							\
	VM_BUG_ON_PAGE(data_race(page->page_type) != UINT_MAX, page);	\
	page->page_type = (unsigned int)PGTY_##lname << 24;		\
}									\
static __always_inline void __ClearPage##uname(struct page *page)	\
{									\
	if (page->page_type == UINT_MAX)				\
		return;							\
	VM_BUG_ON_PAGE(!Page##uname(page), page);			\
	page->page_type = UINT_MAX;					\
}

/*
 * PageBuddy() indicates that the page is free and in the buddy system
 * (see mm/page_alloc.c).
 */
PAGE_TYPE_OPS(Buddy, buddy, buddy)

/*
 * PageOffline() indicates that the page is logically offline although the
 * containing section is online. (e.g. inflated in a balloon driver or
 * not onlined when onlining the section).
 * The content of these pages is effectively stale. Such pages should not
 * be touched (read/write/dump/save) except by their owner.
 *
 * When a memory block gets onlined, all pages are initialized with a
 * refcount of 1 and PageOffline(). generic_online_page() will
 * take care of clearing PageOffline().
 *
 * If a driver wants to allow to offline unmovable PageOffline() pages without
 * putting them back to the buddy, it can do so via the memory notifier by
 * decrementing the reference count in MEM_GOING_OFFLINE and incrementing the
 * reference count in MEM_CANCEL_OFFLINE. When offlining, the PageOffline()
 * pages (now with a reference count of zero) are treated like free (unmanaged)
 * pages, allowing the containing memory block to get offlined. A driver that
 * relies on this feature is aware that re-onlining the memory block will
 * require not giving them to the buddy via generic_online_page().
 *
 * Memory offlining code will not adjust the managed page count for any
 * PageOffline() pages, treating them like they were never exposed to the
 * buddy using generic_online_page().
 *
 * There are drivers that mark a page PageOffline() and expect there won't be
 * any further access to page content. PFN walkers that read content of random
 * pages should check PageOffline() and synchronize with such drivers using
 * page_offline_freeze()/page_offline_thaw().
 */
PAGE_TYPE_OPS(Offline, offline, offline)

extern void page_offline_freeze(void);
extern void page_offline_thaw(void);
extern void page_offline_begin(void);
extern void page_offline_end(void);

/*
 * Marks pages in use as page tables.
 */
PAGE_TYPE_OPS(Table, table, pgtable)

/*
 * Marks guardpages used with debug_pagealloc.
 */
PAGE_TYPE_OPS(Guard, guard, guard)

FOLIO_TYPE_OPS(slab, slab)

/**
 * PageSlab - Determine if the page belongs to the slab allocator
 * @page: The page to test.
 *
 * Context: Any context.
 * Return: True for slab pages, false for any other kind of page.
 */
static inline bool PageSlab(const struct page *page)
{
	return folio_test_slab(page_folio(page));
}

#ifdef CONFIG_HUGETLB_PAGE
FOLIO_TYPE_OPS(hugetlb, hugetlb)
#else
FOLIO_TEST_FLAG_FALSE(hugetlb)
#endif

PAGE_TYPE_OPS(Zsmalloc, zsmalloc, zsmalloc)

/*
 * Mark pages that has to be accepted before touched for the first time.
 *
 * Serialized with zone lock.
 */
PAGE_TYPE_OPS(Unaccepted, unaccepted, unaccepted)
FOLIO_TYPE_OPS(large_kmalloc, large_kmalloc)

/**
 * PageHuge - Determine if the page belongs to hugetlbfs
 * @page: The page to test.
 *
 * Context: Any context.
 * Return: True for hugetlbfs pages, false for anon pages or pages
 * belonging to other filesystems.
 */
static inline bool PageHuge(const struct page *page)
{
	return folio_test_hugetlb(page_folio(page));
}

/*
 * Check if a page is currently marked HWPoisoned. Note that this check is
 * best effort only and inherently racy: there is no way to synchronize with
 * failing hardware.
 */
static inline bool is_page_hwpoison(const struct page *page)
{
	const struct folio *folio;

	if (PageHWPoison(page))
		return true;
	folio = page_folio(page);
	return folio_test_hugetlb(folio) && PageHWPoison(&folio->page);
}

static inline bool folio_contain_hwpoisoned_page(struct folio *folio)
{
	return folio_test_hwpoison(folio) ||
	    (folio_test_large(folio) && folio_test_has_hwpoisoned(folio));
}

bool is_free_buddy_page(const struct page *page);

PAGEFLAG(Isolated, isolated, PF_ANY);

static __always_inline int PageAnonExclusive(const struct page *page)
{
	VM_BUG_ON_PGFLAGS(!PageAnon(page), page);
	/*
	 * HugeTLB stores this information on the head page; THP keeps it per
	 * page
	 */
	if (PageHuge(page))
		page = compound_head(page);
	return test_bit(PG_anon_exclusive, &PF_ANY(page, 1)->flags);
}

static __always_inline void SetPageAnonExclusive(struct page *page)
{
	VM_BUG_ON_PGFLAGS(!PageAnonNotKsm(page), page);
	VM_BUG_ON_PGFLAGS(PageHuge(page) && !PageHead(page), page);
	set_bit(PG_anon_exclusive, &PF_ANY(page, 1)->flags);
}

static __always_inline void ClearPageAnonExclusive(struct page *page)
{
	VM_BUG_ON_PGFLAGS(!PageAnonNotKsm(page), page);
	VM_BUG_ON_PGFLAGS(PageHuge(page) && !PageHead(page), page);
	clear_bit(PG_anon_exclusive, &PF_ANY(page, 1)->flags);
}

static __always_inline void __ClearPageAnonExclusive(struct page *page)
{
	VM_BUG_ON_PGFLAGS(!PageAnon(page), page);
	VM_BUG_ON_PGFLAGS(PageHuge(page) && !PageHead(page), page);
	__clear_bit(PG_anon_exclusive, &PF_ANY(page, 1)->flags);
}

#ifdef CONFIG_MMU
#define __PG_MLOCKED		(1UL << PG_mlocked)
#else
#define __PG_MLOCKED		0
#endif

/*
 * Flags checked when a page is freed.  Pages being freed should not have
 * these flags set.  If they are, there is a problem.
 */
#define PAGE_FLAGS_CHECK_AT_FREE				\
	(1UL << PG_lru		| 1UL << PG_locked	|	\
	 1UL << PG_private	| 1UL << PG_private_2	|	\
	 1UL << PG_writeback	| 1UL << PG_reserved	|	\
	 1UL << PG_active 	|				\
	 1UL << PG_unevictable	| __PG_MLOCKED | LRU_GEN_MASK)

/*
 * Flags checked when a page is prepped for return by the page allocator.
 * Pages being prepped should not have these flags set.  If they are set,
 * there has been a kernel bug or struct page corruption.
 *
 * __PG_HWPOISON is exceptional because it needs to be kept beyond page's
 * alloc-free cycle to prevent from reusing the page.
 */
#define PAGE_FLAGS_CHECK_AT_PREP	\
	((PAGEFLAGS_MASK & ~__PG_HWPOISON) | LRU_GEN_MASK | LRU_REFS_MASK)

/*
 * Flags stored in the second page of a compound page.  They may overlap
 * the CHECK_AT_FREE flags above, so need to be cleared.
 */
#define PAGE_FLAGS_SECOND						\
	(0xffUL /* order */		| 1UL << PG_has_hwpoisoned |	\
	 1UL << PG_large_rmappable	| 1UL << PG_partially_mapped)

#define PAGE_FLAGS_PRIVATE				\
	(1UL << PG_private | 1UL << PG_private_2)
/**
 * folio_has_private - Determine if folio has private stuff
 * @folio: The folio to be checked
 *
 * Determine if a folio has private stuff, indicating that release routines
 * should be invoked upon it.
 */
static inline int folio_has_private(const struct folio *folio)
{
	return !!(folio->flags & PAGE_FLAGS_PRIVATE);
}

static inline bool folio_test_large_maybe_mapped_shared(const struct folio *folio)
{
	return test_bit(FOLIO_MM_IDS_SHARED_BITNUM, &folio->_mm_ids);
}
#undef PF_ANY
#undef PF_HEAD
#undef PF_NO_TAIL
#undef PF_NO_COMPOUND
#undef PF_SECOND
#endif /* !__GENERATING_BOUNDS_H */

#endif	/* PAGE_FLAGS_H */
