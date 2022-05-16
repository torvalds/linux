/* SPDX-License-Identifier: GPL-2.0-or-later */
/* include/asm-generic/tlb.h
 *
 *	Generic TLB shootdown code
 *
 * Copyright 2001 Red Hat, Inc.
 * Based on code from mm/memory.c Copyright Linus Torvalds and others.
 *
 * Copyright 2011 Red Hat, Inc., Peter Zijlstra
 */
#ifndef _ASM_GENERIC__TLB_H
#define _ASM_GENERIC__TLB_H

#include <linux/mmu_notifier.h>
#include <linux/swap.h>
#include <linux/hugetlb_inline.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

/*
 * Blindly accessing user memory from NMI context can be dangerous
 * if we're in the middle of switching the current user task or switching
 * the loaded mm.
 */
#ifndef nmi_uaccess_okay
# define nmi_uaccess_okay() true
#endif

#ifdef CONFIG_MMU

/*
 * Generic MMU-gather implementation.
 *
 * The mmu_gather data structure is used by the mm code to implement the
 * correct and efficient ordering of freeing pages and TLB invalidations.
 *
 * This correct ordering is:
 *
 *  1) unhook page
 *  2) TLB invalidate page
 *  3) free page
 *
 * That is, we must never free a page before we have ensured there are no live
 * translations left to it. Otherwise it might be possible to observe (or
 * worse, change) the page content after it has been reused.
 *
 * The mmu_gather API consists of:
 *
 *  - tlb_gather_mmu() / tlb_gather_mmu_fullmm() / tlb_finish_mmu()
 *
 *    start and finish a mmu_gather
 *
 *    Finish in particular will issue a (final) TLB invalidate and free
 *    all (remaining) queued pages.
 *
 *  - tlb_start_vma() / tlb_end_vma(); marks the start / end of a VMA
 *
 *    Defaults to flushing at tlb_end_vma() to reset the range; helps when
 *    there's large holes between the VMAs.
 *
 *  - tlb_remove_table()
 *
 *    tlb_remove_table() is the basic primitive to free page-table directories
 *    (__p*_free_tlb()).  In it's most primitive form it is an alias for
 *    tlb_remove_page() below, for when page directories are pages and have no
 *    additional constraints.
 *
 *    See also MMU_GATHER_TABLE_FREE and MMU_GATHER_RCU_TABLE_FREE.
 *
 *  - tlb_remove_page() / __tlb_remove_page()
 *  - tlb_remove_page_size() / __tlb_remove_page_size()
 *
 *    __tlb_remove_page_size() is the basic primitive that queues a page for
 *    freeing. __tlb_remove_page() assumes PAGE_SIZE. Both will return a
 *    boolean indicating if the queue is (now) full and a call to
 *    tlb_flush_mmu() is required.
 *
 *    tlb_remove_page() and tlb_remove_page_size() imply the call to
 *    tlb_flush_mmu() when required and has no return value.
 *
 *  - tlb_change_page_size()
 *
 *    call before __tlb_remove_page*() to set the current page-size; implies a
 *    possible tlb_flush_mmu() call.
 *
 *  - tlb_flush_mmu() / tlb_flush_mmu_tlbonly()
 *
 *    tlb_flush_mmu_tlbonly() - does the TLB invalidate (and resets
 *                              related state, like the range)
 *
 *    tlb_flush_mmu() - in addition to the above TLB invalidate, also frees
 *			whatever pages are still batched.
 *
 *  - mmu_gather::fullmm
 *
 *    A flag set by tlb_gather_mmu_fullmm() to indicate we're going to free
 *    the entire mm; this allows a number of optimizations.
 *
 *    - We can ignore tlb_{start,end}_vma(); because we don't
 *      care about ranges. Everything will be shot down.
 *
 *    - (RISC) architectures that use ASIDs can cycle to a new ASID
 *      and delay the invalidation until ASID space runs out.
 *
 *  - mmu_gather::need_flush_all
 *
 *    A flag that can be set by the arch code if it wants to force
 *    flush the entire TLB irrespective of the range. For instance
 *    x86-PAE needs this when changing top-level entries.
 *
 * And allows the architecture to provide and implement tlb_flush():
 *
 * tlb_flush() may, in addition to the above mentioned mmu_gather fields, make
 * use of:
 *
 *  - mmu_gather::start / mmu_gather::end
 *
 *    which provides the range that needs to be flushed to cover the pages to
 *    be freed.
 *
 *  - mmu_gather::freed_tables
 *
 *    set when we freed page table pages
 *
 *  - tlb_get_unmap_shift() / tlb_get_unmap_size()
 *
 *    returns the smallest TLB entry size unmapped in this range.
 *
 * If an architecture does not provide tlb_flush() a default implementation
 * based on flush_tlb_range() will be used, unless MMU_GATHER_NO_RANGE is
 * specified, in which case we'll default to flush_tlb_mm().
 *
 * Additionally there are a few opt-in features:
 *
 *  MMU_GATHER_PAGE_SIZE
 *
 *  This ensures we call tlb_flush() every time tlb_change_page_size() actually
 *  changes the size and provides mmu_gather::page_size to tlb_flush().
 *
 *  This might be useful if your architecture has size specific TLB
 *  invalidation instructions.
 *
 *  MMU_GATHER_TABLE_FREE
 *
 *  This provides tlb_remove_table(), to be used instead of tlb_remove_page()
 *  for page directores (__p*_free_tlb()).
 *
 *  Useful if your architecture has non-page page directories.
 *
 *  When used, an architecture is expected to provide __tlb_remove_table()
 *  which does the actual freeing of these pages.
 *
 *  MMU_GATHER_RCU_TABLE_FREE
 *
 *  Like MMU_GATHER_TABLE_FREE, and adds semi-RCU semantics to the free (see
 *  comment below).
 *
 *  Useful if your architecture doesn't use IPIs for remote TLB invalidates
 *  and therefore doesn't naturally serialize with software page-table walkers.
 *
 *  MMU_GATHER_NO_RANGE
 *
 *  Use this if your architecture lacks an efficient flush_tlb_range().
 *
 *  MMU_GATHER_NO_GATHER
 *
 *  If the option is set the mmu_gather will not track individual pages for
 *  delayed page free anymore. A platform that enables the option needs to
 *  provide its own implementation of the __tlb_remove_page_size() function to
 *  free pages.
 *
 *  This is useful if your architecture already flushes TLB entries in the
 *  various ptep_get_and_clear() functions.
 */

#ifdef CONFIG_MMU_GATHER_TABLE_FREE

struct mmu_table_batch {
#ifdef CONFIG_MMU_GATHER_RCU_TABLE_FREE
	struct rcu_head		rcu;
#endif
	unsigned int		nr;
	void			*tables[0];
};

#define MAX_TABLE_BATCH		\
	((PAGE_SIZE - sizeof(struct mmu_table_batch)) / sizeof(void *))

extern void tlb_remove_table(struct mmu_gather *tlb, void *table);

#else /* !CONFIG_MMU_GATHER_HAVE_TABLE_FREE */

/*
 * Without MMU_GATHER_TABLE_FREE the architecture is assumed to have page based
 * page directories and we can use the normal page batching to free them.
 */
#define tlb_remove_table(tlb, page) tlb_remove_page((tlb), (page))

#endif /* CONFIG_MMU_GATHER_TABLE_FREE */

#ifdef CONFIG_MMU_GATHER_RCU_TABLE_FREE
/*
 * This allows an architecture that does not use the linux page-tables for
 * hardware to skip the TLBI when freeing page tables.
 */
#ifndef tlb_needs_table_invalidate
#define tlb_needs_table_invalidate() (true)
#endif

#else

#ifdef tlb_needs_table_invalidate
#error tlb_needs_table_invalidate() requires MMU_GATHER_RCU_TABLE_FREE
#endif

#endif /* CONFIG_MMU_GATHER_RCU_TABLE_FREE */


#ifndef CONFIG_MMU_GATHER_NO_GATHER
/*
 * If we can't allocate a page to make a big batch of page pointers
 * to work on, then just handle a few from the on-stack structure.
 */
#define MMU_GATHER_BUNDLE	8

struct mmu_gather_batch {
	struct mmu_gather_batch	*next;
	unsigned int		nr;
	unsigned int		max;
	struct page		*pages[0];
};

#define MAX_GATHER_BATCH	\
	((PAGE_SIZE - sizeof(struct mmu_gather_batch)) / sizeof(void *))

/*
 * Limit the maximum number of mmu_gather batches to reduce a risk of soft
 * lockups for non-preemptible kernels on huge machines when a lot of memory
 * is zapped during unmapping.
 * 10K pages freed at once should be safe even without a preemption point.
 */
#define MAX_GATHER_BATCH_COUNT	(10000UL/MAX_GATHER_BATCH)

extern bool __tlb_remove_page_size(struct mmu_gather *tlb, struct page *page,
				   int page_size);
#endif

/*
 * struct mmu_gather is an opaque type used by the mm code for passing around
 * any data needed by arch specific code for tlb_remove_page.
 */
struct mmu_gather {
	struct mm_struct	*mm;

#ifdef CONFIG_MMU_GATHER_TABLE_FREE
	struct mmu_table_batch	*batch;
#endif

	unsigned long		start;
	unsigned long		end;
	/*
	 * we are in the middle of an operation to clear
	 * a full mm and can make some optimizations
	 */
	unsigned int		fullmm : 1;

	/*
	 * we have performed an operation which
	 * requires a complete flush of the tlb
	 */
	unsigned int		need_flush_all : 1;

	/*
	 * we have removed page directories
	 */
	unsigned int		freed_tables : 1;

	/*
	 * at which levels have we cleared entries?
	 */
	unsigned int		cleared_ptes : 1;
	unsigned int		cleared_pmds : 1;
	unsigned int		cleared_puds : 1;
	unsigned int		cleared_p4ds : 1;

	/*
	 * tracks VM_EXEC | VM_HUGETLB in tlb_start_vma
	 */
	unsigned int		vma_exec : 1;
	unsigned int		vma_huge : 1;

	unsigned int		batch_count;

#ifndef CONFIG_MMU_GATHER_NO_GATHER
	struct mmu_gather_batch *active;
	struct mmu_gather_batch	local;
	struct page		*__pages[MMU_GATHER_BUNDLE];

#ifdef CONFIG_MMU_GATHER_PAGE_SIZE
	unsigned int page_size;
#endif
#endif
};

void tlb_flush_mmu(struct mmu_gather *tlb);

static inline void __tlb_adjust_range(struct mmu_gather *tlb,
				      unsigned long address,
				      unsigned int range_size)
{
	tlb->start = min(tlb->start, address);
	tlb->end = max(tlb->end, address + range_size);
}

static inline void __tlb_reset_range(struct mmu_gather *tlb)
{
	if (tlb->fullmm) {
		tlb->start = tlb->end = ~0;
	} else {
		tlb->start = TASK_SIZE;
		tlb->end = 0;
	}
	tlb->freed_tables = 0;
	tlb->cleared_ptes = 0;
	tlb->cleared_pmds = 0;
	tlb->cleared_puds = 0;
	tlb->cleared_p4ds = 0;
	/*
	 * Do not reset mmu_gather::vma_* fields here, we do not
	 * call into tlb_start_vma() again to set them if there is an
	 * intermediate flush.
	 */
}

#ifdef CONFIG_MMU_GATHER_NO_RANGE

#if defined(tlb_flush) || defined(tlb_start_vma) || defined(tlb_end_vma)
#error MMU_GATHER_NO_RANGE relies on default tlb_flush(), tlb_start_vma() and tlb_end_vma()
#endif

/*
 * When an architecture does not have efficient means of range flushing TLBs
 * there is no point in doing intermediate flushes on tlb_end_vma() to keep the
 * range small. We equally don't have to worry about page granularity or other
 * things.
 *
 * All we need to do is issue a full flush for any !0 range.
 */
static inline void tlb_flush(struct mmu_gather *tlb)
{
	if (tlb->end)
		flush_tlb_mm(tlb->mm);
}

static inline void
tlb_update_vma_flags(struct mmu_gather *tlb, struct vm_area_struct *vma) { }

#define tlb_end_vma tlb_end_vma
static inline void tlb_end_vma(struct mmu_gather *tlb, struct vm_area_struct *vma) { }

#else /* CONFIG_MMU_GATHER_NO_RANGE */

#ifndef tlb_flush

#if defined(tlb_start_vma) || defined(tlb_end_vma)
#error Default tlb_flush() relies on default tlb_start_vma() and tlb_end_vma()
#endif

/*
 * When an architecture does not provide its own tlb_flush() implementation
 * but does have a reasonably efficient flush_vma_range() implementation
 * use that.
 */
static inline void tlb_flush(struct mmu_gather *tlb)
{
	if (tlb->fullmm || tlb->need_flush_all) {
		flush_tlb_mm(tlb->mm);
	} else if (tlb->end) {
		struct vm_area_struct vma = {
			.vm_mm = tlb->mm,
			.vm_flags = (tlb->vma_exec ? VM_EXEC    : 0) |
				    (tlb->vma_huge ? VM_HUGETLB : 0),
		};

		flush_tlb_range(&vma, tlb->start, tlb->end);
	}
}

static inline void
tlb_update_vma_flags(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	/*
	 * flush_tlb_range() implementations that look at VM_HUGETLB (tile,
	 * mips-4k) flush only large pages.
	 *
	 * flush_tlb_range() implementations that flush I-TLB also flush D-TLB
	 * (tile, xtensa, arm), so it's ok to just add VM_EXEC to an existing
	 * range.
	 *
	 * We rely on tlb_end_vma() to issue a flush, such that when we reset
	 * these values the batch is empty.
	 */
	tlb->vma_huge = is_vm_hugetlb_page(vma);
	tlb->vma_exec = !!(vma->vm_flags & VM_EXEC);
}

#else

static inline void
tlb_update_vma_flags(struct mmu_gather *tlb, struct vm_area_struct *vma) { }

#endif

#endif /* CONFIG_MMU_GATHER_NO_RANGE */

static inline void tlb_flush_mmu_tlbonly(struct mmu_gather *tlb)
{
	/*
	 * Anything calling __tlb_adjust_range() also sets at least one of
	 * these bits.
	 */
	if (!(tlb->freed_tables || tlb->cleared_ptes || tlb->cleared_pmds ||
	      tlb->cleared_puds || tlb->cleared_p4ds))
		return;

	tlb_flush(tlb);
	mmu_notifier_invalidate_range(tlb->mm, tlb->start, tlb->end);
	__tlb_reset_range(tlb);
}

static inline void tlb_remove_page_size(struct mmu_gather *tlb,
					struct page *page, int page_size)
{
	if (__tlb_remove_page_size(tlb, page, page_size))
		tlb_flush_mmu(tlb);
}

static inline bool __tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	return __tlb_remove_page_size(tlb, page, PAGE_SIZE);
}

/* tlb_remove_page
 *	Similar to __tlb_remove_page but will call tlb_flush_mmu() itself when
 *	required.
 */
static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	return tlb_remove_page_size(tlb, page, PAGE_SIZE);
}

static inline void tlb_change_page_size(struct mmu_gather *tlb,
						     unsigned int page_size)
{
#ifdef CONFIG_MMU_GATHER_PAGE_SIZE
	if (tlb->page_size && tlb->page_size != page_size) {
		if (!tlb->fullmm && !tlb->need_flush_all)
			tlb_flush_mmu(tlb);
	}

	tlb->page_size = page_size;
#endif
}

static inline unsigned long tlb_get_unmap_shift(struct mmu_gather *tlb)
{
	if (tlb->cleared_ptes)
		return PAGE_SHIFT;
	if (tlb->cleared_pmds)
		return PMD_SHIFT;
	if (tlb->cleared_puds)
		return PUD_SHIFT;
	if (tlb->cleared_p4ds)
		return P4D_SHIFT;

	return PAGE_SHIFT;
}

static inline unsigned long tlb_get_unmap_size(struct mmu_gather *tlb)
{
	return 1UL << tlb_get_unmap_shift(tlb);
}

/*
 * In the case of tlb vma handling, we can optimise these away in the
 * case where we're doing a full MM flush.  When we're doing a munmap,
 * the vmas are adjusted to only cover the region to be torn down.
 */
#ifndef tlb_start_vma
static inline void tlb_start_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (tlb->fullmm)
		return;

	tlb_update_vma_flags(tlb, vma);
	flush_cache_range(vma, vma->vm_start, vma->vm_end);
}
#endif

#ifndef tlb_end_vma
static inline void tlb_end_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (tlb->fullmm)
		return;

	/*
	 * Do a TLB flush and reset the range at VMA boundaries; this avoids
	 * the ranges growing with the unused space between consecutive VMAs,
	 * but also the mmu_gather::vma_* flags from tlb_start_vma() rely on
	 * this.
	 */
	tlb_flush_mmu_tlbonly(tlb);
}
#endif

/*
 * tlb_flush_{pte|pmd|pud|p4d}_range() adjust the tlb->start and tlb->end,
 * and set corresponding cleared_*.
 */
static inline void tlb_flush_pte_range(struct mmu_gather *tlb,
				     unsigned long address, unsigned long size)
{
	__tlb_adjust_range(tlb, address, size);
	tlb->cleared_ptes = 1;
}

static inline void tlb_flush_pmd_range(struct mmu_gather *tlb,
				     unsigned long address, unsigned long size)
{
	__tlb_adjust_range(tlb, address, size);
	tlb->cleared_pmds = 1;
}

static inline void tlb_flush_pud_range(struct mmu_gather *tlb,
				     unsigned long address, unsigned long size)
{
	__tlb_adjust_range(tlb, address, size);
	tlb->cleared_puds = 1;
}

static inline void tlb_flush_p4d_range(struct mmu_gather *tlb,
				     unsigned long address, unsigned long size)
{
	__tlb_adjust_range(tlb, address, size);
	tlb->cleared_p4ds = 1;
}

#ifndef __tlb_remove_tlb_entry
#define __tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)
#endif

/**
 * tlb_remove_tlb_entry - remember a pte unmapping for later tlb invalidation.
 *
 * Record the fact that pte's were really unmapped by updating the range,
 * so we can later optimise away the tlb invalidate.   This helps when
 * userspace is unmapping already-unmapped pages, which happens quite a lot.
 */
#define tlb_remove_tlb_entry(tlb, ptep, address)		\
	do {							\
		tlb_flush_pte_range(tlb, address, PAGE_SIZE);	\
		__tlb_remove_tlb_entry(tlb, ptep, address);	\
	} while (0)

#define tlb_remove_huge_tlb_entry(h, tlb, ptep, address)	\
	do {							\
		unsigned long _sz = huge_page_size(h);		\
		if (_sz >= P4D_SIZE)				\
			tlb_flush_p4d_range(tlb, address, _sz);	\
		else if (_sz >= PUD_SIZE)			\
			tlb_flush_pud_range(tlb, address, _sz);	\
		else if (_sz >= PMD_SIZE)			\
			tlb_flush_pmd_range(tlb, address, _sz);	\
		else						\
			tlb_flush_pte_range(tlb, address, _sz);	\
		__tlb_remove_tlb_entry(tlb, ptep, address);	\
	} while (0)

/**
 * tlb_remove_pmd_tlb_entry - remember a pmd mapping for later tlb invalidation
 * This is a nop so far, because only x86 needs it.
 */
#ifndef __tlb_remove_pmd_tlb_entry
#define __tlb_remove_pmd_tlb_entry(tlb, pmdp, address) do {} while (0)
#endif

#define tlb_remove_pmd_tlb_entry(tlb, pmdp, address)			\
	do {								\
		tlb_flush_pmd_range(tlb, address, HPAGE_PMD_SIZE);	\
		__tlb_remove_pmd_tlb_entry(tlb, pmdp, address);		\
	} while (0)

/**
 * tlb_remove_pud_tlb_entry - remember a pud mapping for later tlb
 * invalidation. This is a nop so far, because only x86 needs it.
 */
#ifndef __tlb_remove_pud_tlb_entry
#define __tlb_remove_pud_tlb_entry(tlb, pudp, address) do {} while (0)
#endif

#define tlb_remove_pud_tlb_entry(tlb, pudp, address)			\
	do {								\
		tlb_flush_pud_range(tlb, address, HPAGE_PUD_SIZE);	\
		__tlb_remove_pud_tlb_entry(tlb, pudp, address);		\
	} while (0)

/*
 * For things like page tables caches (ie caching addresses "inside" the
 * page tables, like x86 does), for legacy reasons, flushing an
 * individual page had better flush the page table caches behind it. This
 * is definitely how x86 works, for example. And if you have an
 * architected non-legacy page table cache (which I'm not aware of
 * anybody actually doing), you're going to have some architecturally
 * explicit flushing for that, likely *separate* from a regular TLB entry
 * flush, and thus you'd need more than just some range expansion..
 *
 * So if we ever find an architecture
 * that would want something that odd, I think it is up to that
 * architecture to do its own odd thing, not cause pain for others
 * http://lkml.kernel.org/r/CA+55aFzBggoXtNXQeng5d_mRoDnaMBE5Y+URs+PHR67nUpMtaw@mail.gmail.com
 *
 * For now w.r.t page table cache, mark the range_size as PAGE_SIZE
 */

#ifndef pte_free_tlb
#define pte_free_tlb(tlb, ptep, address)			\
	do {							\
		tlb_flush_pmd_range(tlb, address, PAGE_SIZE);	\
		tlb->freed_tables = 1;				\
		__pte_free_tlb(tlb, ptep, address);		\
	} while (0)
#endif

#ifndef pmd_free_tlb
#define pmd_free_tlb(tlb, pmdp, address)			\
	do {							\
		tlb_flush_pud_range(tlb, address, PAGE_SIZE);	\
		tlb->freed_tables = 1;				\
		__pmd_free_tlb(tlb, pmdp, address);		\
	} while (0)
#endif

#ifndef pud_free_tlb
#define pud_free_tlb(tlb, pudp, address)			\
	do {							\
		tlb_flush_p4d_range(tlb, address, PAGE_SIZE);	\
		tlb->freed_tables = 1;				\
		__pud_free_tlb(tlb, pudp, address);		\
	} while (0)
#endif

#ifndef p4d_free_tlb
#define p4d_free_tlb(tlb, pudp, address)			\
	do {							\
		__tlb_adjust_range(tlb, address, PAGE_SIZE);	\
		tlb->freed_tables = 1;				\
		__p4d_free_tlb(tlb, pudp, address);		\
	} while (0)
#endif

#endif /* CONFIG_MMU */

#endif /* _ASM_GENERIC__TLB_H */
