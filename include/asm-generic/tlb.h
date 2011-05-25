/* include/asm-generic/tlb.h
 *
 *	Generic TLB shootdown code
 *
 * Copyright 2001 Red Hat, Inc.
 * Based on code from mm/memory.c Copyright Linus Torvalds and others.
 *
 * Copyright 2011 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_GENERIC__TLB_H
#define _ASM_GENERIC__TLB_H

#include <linux/swap.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

/*
 * For UP we don't need to worry about TLB flush
 * and page free order so much..
 */
#ifdef CONFIG_SMP
  #define tlb_fast_mode(tlb) ((tlb)->nr == ~0U)
#else
  #define tlb_fast_mode(tlb) 1
#endif

#ifdef CONFIG_HAVE_RCU_TABLE_FREE
/*
 * Semi RCU freeing of the page directories.
 *
 * This is needed by some architectures to implement software pagetable walkers.
 *
 * gup_fast() and other software pagetable walkers do a lockless page-table
 * walk and therefore needs some synchronization with the freeing of the page
 * directories. The chosen means to accomplish that is by disabling IRQs over
 * the walk.
 *
 * Architectures that use IPIs to flush TLBs will then automagically DTRT,
 * since we unlink the page, flush TLBs, free the page. Since the disabling of
 * IRQs delays the completion of the TLB flush we can never observe an already
 * freed page.
 *
 * Architectures that do not have this (PPC) need to delay the freeing by some
 * other means, this is that means.
 *
 * What we do is batch the freed directory pages (tables) and RCU free them.
 * We use the sched RCU variant, as that guarantees that IRQ/preempt disabling
 * holds off grace periods.
 *
 * However, in order to batch these pages we need to allocate storage, this
 * allocation is deep inside the MM code and can thus easily fail on memory
 * pressure. To guarantee progress we fall back to single table freeing, see
 * the implementation of tlb_remove_table_one().
 *
 */
struct mmu_table_batch {
	struct rcu_head		rcu;
	unsigned int		nr;
	void			*tables[0];
};

#define MAX_TABLE_BATCH		\
	((PAGE_SIZE - sizeof(struct mmu_table_batch)) / sizeof(void *))

extern void tlb_table_flush(struct mmu_gather *tlb);
extern void tlb_remove_table(struct mmu_gather *tlb, void *table);

#endif

/*
 * If we can't allocate a page to make a big batch of page pointers
 * to work on, then just handle a few from the on-stack structure.
 */
#define MMU_GATHER_BUNDLE	8

/* struct mmu_gather is an opaque type used by the mm code for passing around
 * any data needed by arch specific code for tlb_remove_page.
 */
struct mmu_gather {
	struct mm_struct	*mm;
#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	struct mmu_table_batch	*batch;
#endif
	unsigned int		nr;	/* set to ~0U means fast mode */
	unsigned int		max;	/* nr < max */
	unsigned int		need_flush;/* Really unmapped some ptes? */
	unsigned int		fullmm; /* non-zero means full mm flush */
	struct page		**pages;
	struct page		*local[MMU_GATHER_BUNDLE];
};

static inline void __tlb_alloc_page(struct mmu_gather *tlb)
{
	unsigned long addr = __get_free_pages(GFP_NOWAIT | __GFP_NOWARN, 0);

	if (addr) {
		tlb->pages = (void *)addr;
		tlb->max = PAGE_SIZE / sizeof(struct page *);
	}
}

/* tlb_gather_mmu
 *	Called to initialize an (on-stack) mmu_gather structure for page-table
 *	tear-down from @mm. The @fullmm argument is used when @mm is without
 *	users and we're going to destroy the full address space (exit/execve).
 */
static inline void
tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm, bool fullmm)
{
	tlb->mm = mm;

	tlb->max = ARRAY_SIZE(tlb->local);
	tlb->pages = tlb->local;

	if (num_online_cpus() > 1) {
		tlb->nr = 0;
		__tlb_alloc_page(tlb);
	} else /* Use fast mode if only one CPU is online */
		tlb->nr = ~0U;

	tlb->fullmm = fullmm;

#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	tlb->batch = NULL;
#endif
}

static inline void
tlb_flush_mmu(struct mmu_gather *tlb)
{
	if (!tlb->need_flush)
		return;
	tlb->need_flush = 0;
	tlb_flush(tlb);
#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	tlb_table_flush(tlb);
#endif
	if (!tlb_fast_mode(tlb)) {
		free_pages_and_swap_cache(tlb->pages, tlb->nr);
		tlb->nr = 0;
		/*
		 * If we are using the local on-stack array of pages for MMU
		 * gather, try allocating an off-stack array again as we have
		 * recently freed pages.
		 */
		if (tlb->pages == tlb->local)
			__tlb_alloc_page(tlb);
	}
}

/* tlb_finish_mmu
 *	Called at the end of the shootdown operation to free up any resources
 *	that were required.
 */
static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
	tlb_flush_mmu(tlb);

	/* keep the page table cache within bounds */
	check_pgt_cache();

	if (tlb->pages != tlb->local)
		free_pages((unsigned long)tlb->pages, 0);
}

/* __tlb_remove_page
 *	Must perform the equivalent to __free_pte(pte_get_and_clear(ptep)), while
 *	handling the additional races in SMP caused by other CPUs caching valid
 *	mappings in their TLBs. Returns the number of free page slots left.
 *	When out of page slots we must call tlb_flush_mmu().
 */
static inline int __tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	tlb->need_flush = 1;
	if (tlb_fast_mode(tlb)) {
		free_page_and_swap_cache(page);
		return 1; /* avoid calling tlb_flush_mmu() */
	}
	tlb->pages[tlb->nr++] = page;
	VM_BUG_ON(tlb->nr > tlb->max);

	return tlb->max - tlb->nr;
}

/* tlb_remove_page
 *	Similar to __tlb_remove_page but will call tlb_flush_mmu() itself when
 *	required.
 */
static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	if (!__tlb_remove_page(tlb, page))
		tlb_flush_mmu(tlb);
}

/**
 * tlb_remove_tlb_entry - remember a pte unmapping for later tlb invalidation.
 *
 * Record the fact that pte's were really umapped in ->need_flush, so we can
 * later optimise away the tlb invalidate.   This helps when userspace is
 * unmapping already-unmapped pages, which happens quite a lot.
 */
#define tlb_remove_tlb_entry(tlb, ptep, address)		\
	do {							\
		tlb->need_flush = 1;				\
		__tlb_remove_tlb_entry(tlb, ptep, address);	\
	} while (0)

#define pte_free_tlb(tlb, ptep, address)			\
	do {							\
		tlb->need_flush = 1;				\
		__pte_free_tlb(tlb, ptep, address);		\
	} while (0)

#ifndef __ARCH_HAS_4LEVEL_HACK
#define pud_free_tlb(tlb, pudp, address)			\
	do {							\
		tlb->need_flush = 1;				\
		__pud_free_tlb(tlb, pudp, address);		\
	} while (0)
#endif

#define pmd_free_tlb(tlb, pmdp, address)			\
	do {							\
		tlb->need_flush = 1;				\
		__pmd_free_tlb(tlb, pmdp, address);		\
	} while (0)

#define tlb_migrate_finish(mm) do {} while (0)

#endif /* _ASM_GENERIC__TLB_H */
