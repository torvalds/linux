// SPDX-License-Identifier: GPL-2.0
/*
 * High memory handling common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * 64-bit physical space. With current x86 CPUs this
 * means up to 64 Gigabytes physical RAM.
 *
 * Rewrote high memory support to move the page cache into
 * high memory. Implemented permanent (schedulable) kmaps
 * based on Linus' idea.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#include <linux/mm.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <linux/kgdb.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>

/*
 * Virtual_count is not a pure "count".
 *  0 means that it is not mapped, and has not been mapped
 *    since a TLB flush - it is usable.
 *  1 means that there are no users, but it has been mapped
 *    since the last TLB flush - so we can't use it.
 *  n means that there are (n-1) current users of it.
 */
#ifdef CONFIG_HIGHMEM

/*
 * Architecture with aliasing data cache may define the following family of
 * helper functions in its asm/highmem.h to control cache color of virtual
 * addresses where physical memory pages are mapped by kmap.
 */
#ifndef get_pkmap_color

/*
 * Determine color of virtual address where the page should be mapped.
 */
static inline unsigned int get_pkmap_color(struct page *page)
{
	return 0;
}
#define get_pkmap_color get_pkmap_color

/*
 * Get next index for mapping inside PKMAP region for page with given color.
 */
static inline unsigned int get_next_pkmap_nr(unsigned int color)
{
	static unsigned int last_pkmap_nr;

	last_pkmap_nr = (last_pkmap_nr + 1) & LAST_PKMAP_MASK;
	return last_pkmap_nr;
}

/*
 * Determine if page index inside PKMAP region (pkmap_nr) of given color
 * has wrapped around PKMAP region end. When this happens an attempt to
 * flush all unused PKMAP slots is made.
 */
static inline int no_more_pkmaps(unsigned int pkmap_nr, unsigned int color)
{
	return pkmap_nr == 0;
}

/*
 * Get the number of PKMAP entries of the given color. If no free slot is
 * found after checking that many entries, kmap will sleep waiting for
 * someone to call kunmap and free PKMAP slot.
 */
static inline int get_pkmap_entries_count(unsigned int color)
{
	return LAST_PKMAP;
}

/*
 * Get head of a wait queue for PKMAP entries of the given color.
 * Wait queues for different mapping colors should be independent to avoid
 * unnecessary wakeups caused by freeing of slots of other colors.
 */
static inline wait_queue_head_t *get_pkmap_wait_queue_head(unsigned int color)
{
	static DECLARE_WAIT_QUEUE_HEAD(pkmap_map_wait);

	return &pkmap_map_wait;
}
#endif

atomic_long_t _totalhigh_pages __read_mostly;
EXPORT_SYMBOL(_totalhigh_pages);

unsigned int __nr_free_highpages (void)
{
	struct zone *zone;
	unsigned int pages = 0;

	for_each_populated_zone(zone) {
		if (is_highmem(zone))
			pages += zone_page_state(zone, NR_FREE_PAGES);
	}

	return pages;
}

static int pkmap_count[LAST_PKMAP];
static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(kmap_lock);

pte_t * pkmap_page_table;

/*
 * Most architectures have no use for kmap_high_get(), so let's abstract
 * the disabling of IRQ out of the locking in that case to save on a
 * potential useless overhead.
 */
#ifdef ARCH_NEEDS_KMAP_HIGH_GET
#define lock_kmap()             spin_lock_irq(&kmap_lock)
#define unlock_kmap()           spin_unlock_irq(&kmap_lock)
#define lock_kmap_any(flags)    spin_lock_irqsave(&kmap_lock, flags)
#define unlock_kmap_any(flags)  spin_unlock_irqrestore(&kmap_lock, flags)
#else
#define lock_kmap()             spin_lock(&kmap_lock)
#define unlock_kmap()           spin_unlock(&kmap_lock)
#define lock_kmap_any(flags)    \
		do { spin_lock(&kmap_lock); (void)(flags); } while (0)
#define unlock_kmap_any(flags)  \
		do { spin_unlock(&kmap_lock); (void)(flags); } while (0)
#endif

struct page *__kmap_to_page(void *vaddr)
{
	unsigned long addr = (unsigned long)vaddr;

	if (addr >= PKMAP_ADDR(0) && addr < PKMAP_ADDR(LAST_PKMAP)) {
		int i = PKMAP_NR(addr);
		return pte_page(pkmap_page_table[i]);
	}

	return virt_to_page(addr);
}
EXPORT_SYMBOL(__kmap_to_page);

static void flush_all_zero_pkmaps(void)
{
	int i;
	int need_flush = 0;

	flush_cache_kmaps();

	for (i = 0; i < LAST_PKMAP; i++) {
		struct page *page;

		/*
		 * zero means we don't have anything to do,
		 * >1 means that it is still in use. Only
		 * a count of 1 means that it is free but
		 * needs to be unmapped
		 */
		if (pkmap_count[i] != 1)
			continue;
		pkmap_count[i] = 0;

		/* sanity check */
		BUG_ON(pte_none(pkmap_page_table[i]));

		/*
		 * Don't need an atomic fetch-and-clear op here;
		 * no-one has the page mapped, and cannot get at
		 * its virtual address (and hence PTE) without first
		 * getting the kmap_lock (which is held here).
		 * So no dangers, even with speculative execution.
		 */
		page = pte_page(pkmap_page_table[i]);
		pte_clear(&init_mm, PKMAP_ADDR(i), &pkmap_page_table[i]);

		set_page_address(page, NULL);
		need_flush = 1;
	}
	if (need_flush)
		flush_tlb_kernel_range(PKMAP_ADDR(0), PKMAP_ADDR(LAST_PKMAP));
}

void __kmap_flush_unused(void)
{
	lock_kmap();
	flush_all_zero_pkmaps();
	unlock_kmap();
}

static inline unsigned long map_new_virtual(struct page *page)
{
	unsigned long vaddr;
	int count;
	unsigned int last_pkmap_nr;
	unsigned int color = get_pkmap_color(page);

start:
	count = get_pkmap_entries_count(color);
	/* Find an empty entry */
	for (;;) {
		last_pkmap_nr = get_next_pkmap_nr(color);
		if (no_more_pkmaps(last_pkmap_nr, color)) {
			flush_all_zero_pkmaps();
			count = get_pkmap_entries_count(color);
		}
		if (!pkmap_count[last_pkmap_nr])
			break;	/* Found a usable entry */
		if (--count)
			continue;

		/*
		 * Sleep for somebody else to unmap their entries
		 */
		{
			DECLARE_WAITQUEUE(wait, current);
			wait_queue_head_t *pkmap_map_wait =
				get_pkmap_wait_queue_head(color);

			__set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(pkmap_map_wait, &wait);
			unlock_kmap();
			schedule();
			remove_wait_queue(pkmap_map_wait, &wait);
			lock_kmap();

			/* Somebody else might have mapped it while we slept */
			if (page_address(page))
				return (unsigned long)page_address(page);

			/* Re-start */
			goto start;
		}
	}
	vaddr = PKMAP_ADDR(last_pkmap_nr);
	set_pte_at(&init_mm, vaddr,
		   &(pkmap_page_table[last_pkmap_nr]), mk_pte(page, kmap_prot));

	pkmap_count[last_pkmap_nr] = 1;
	set_page_address(page, (void *)vaddr);

	return vaddr;
}

/**
 * kmap_high - map a highmem page into memory
 * @page: &struct page to map
 *
 * Returns the page's virtual memory address.
 *
 * We cannot call this from interrupts, as it may block.
 */
void *kmap_high(struct page *page)
{
	unsigned long vaddr;

	/*
	 * For highmem pages, we can't trust "virtual" until
	 * after we have the lock.
	 */
	lock_kmap();
	vaddr = (unsigned long)page_address(page);
	if (!vaddr)
		vaddr = map_new_virtual(page);
	pkmap_count[PKMAP_NR(vaddr)]++;
	BUG_ON(pkmap_count[PKMAP_NR(vaddr)] < 2);
	unlock_kmap();
	return (void*) vaddr;
}

EXPORT_SYMBOL(kmap_high);

#ifdef ARCH_NEEDS_KMAP_HIGH_GET
/**
 * kmap_high_get - pin a highmem page into memory
 * @page: &struct page to pin
 *
 * Returns the page's current virtual memory address, or NULL if no mapping
 * exists.  If and only if a non null address is returned then a
 * matching call to kunmap_high() is necessary.
 *
 * This can be called from any context.
 */
void *kmap_high_get(struct page *page)
{
	unsigned long vaddr, flags;

	lock_kmap_any(flags);
	vaddr = (unsigned long)page_address(page);
	if (vaddr) {
		BUG_ON(pkmap_count[PKMAP_NR(vaddr)] < 1);
		pkmap_count[PKMAP_NR(vaddr)]++;
	}
	unlock_kmap_any(flags);
	return (void*) vaddr;
}
#endif

/**
 * kunmap_high - unmap a highmem page into memory
 * @page: &struct page to unmap
 *
 * If ARCH_NEEDS_KMAP_HIGH_GET is not defined then this may be called
 * only from user context.
 */
void kunmap_high(struct page *page)
{
	unsigned long vaddr;
	unsigned long nr;
	unsigned long flags;
	int need_wakeup;
	unsigned int color = get_pkmap_color(page);
	wait_queue_head_t *pkmap_map_wait;

	lock_kmap_any(flags);
	vaddr = (unsigned long)page_address(page);
	BUG_ON(!vaddr);
	nr = PKMAP_NR(vaddr);

	/*
	 * A count must never go down to zero
	 * without a TLB flush!
	 */
	need_wakeup = 0;
	switch (--pkmap_count[nr]) {
	case 0:
		BUG();
	case 1:
		/*
		 * Avoid an unnecessary wake_up() function call.
		 * The common case is pkmap_count[] == 1, but
		 * no waiters.
		 * The tasks queued in the wait-queue are guarded
		 * by both the lock in the wait-queue-head and by
		 * the kmap_lock.  As the kmap_lock is held here,
		 * no need for the wait-queue-head's lock.  Simply
		 * test if the queue is empty.
		 */
		pkmap_map_wait = get_pkmap_wait_queue_head(color);
		need_wakeup = waitqueue_active(pkmap_map_wait);
	}
	unlock_kmap_any(flags);

	/* do wake-up, if needed, race-free outside of the spin lock */
	if (need_wakeup)
		wake_up(pkmap_map_wait);
}
EXPORT_SYMBOL(kunmap_high);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void zero_user_segments(struct page *page, unsigned start1, unsigned end1,
		unsigned start2, unsigned end2)
{
	unsigned int i;

	BUG_ON(end1 > page_size(page) || end2 > page_size(page));

	for (i = 0; i < compound_nr(page); i++) {
		void *kaddr = NULL;

		if (start1 < PAGE_SIZE || start2 < PAGE_SIZE)
			kaddr = kmap_atomic(page + i);

		if (start1 >= PAGE_SIZE) {
			start1 -= PAGE_SIZE;
			end1 -= PAGE_SIZE;
		} else {
			unsigned this_end = min_t(unsigned, end1, PAGE_SIZE);

			if (end1 > start1)
				memset(kaddr + start1, 0, this_end - start1);
			end1 -= this_end;
			start1 = 0;
		}

		if (start2 >= PAGE_SIZE) {
			start2 -= PAGE_SIZE;
			end2 -= PAGE_SIZE;
		} else {
			unsigned this_end = min_t(unsigned, end2, PAGE_SIZE);

			if (end2 > start2)
				memset(kaddr + start2, 0, this_end - start2);
			end2 -= this_end;
			start2 = 0;
		}

		if (kaddr) {
			kunmap_atomic(kaddr);
			flush_dcache_page(page + i);
		}

		if (!end1 && !end2)
			break;
	}

	BUG_ON((start1 | start2 | end1 | end2) != 0);
}
EXPORT_SYMBOL(zero_user_segments);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
#endif /* CONFIG_HIGHMEM */

#ifdef CONFIG_KMAP_LOCAL

#include <asm/kmap_size.h>

/*
 * With DEBUG_KMAP_LOCAL the stack depth is doubled and every second
 * slot is unused which acts as a guard page
 */
#ifdef CONFIG_DEBUG_KMAP_LOCAL
# define KM_INCR	2
#else
# define KM_INCR	1
#endif

static inline int kmap_local_idx_push(void)
{
	WARN_ON_ONCE(in_irq() && !irqs_disabled());
	current->kmap_ctrl.idx += KM_INCR;
	BUG_ON(current->kmap_ctrl.idx >= KM_MAX_IDX);
	return current->kmap_ctrl.idx - 1;
}

static inline int kmap_local_idx(void)
{
	return current->kmap_ctrl.idx - 1;
}

static inline void kmap_local_idx_pop(void)
{
	current->kmap_ctrl.idx -= KM_INCR;
	BUG_ON(current->kmap_ctrl.idx < 0);
}

#ifndef arch_kmap_local_post_map
# define arch_kmap_local_post_map(vaddr, pteval)	do { } while (0)
#endif

#ifndef arch_kmap_local_pre_unmap
# define arch_kmap_local_pre_unmap(vaddr)		do { } while (0)
#endif

#ifndef arch_kmap_local_post_unmap
# define arch_kmap_local_post_unmap(vaddr)		do { } while (0)
#endif

#ifndef arch_kmap_local_map_idx
#define arch_kmap_local_map_idx(idx, pfn)	kmap_local_calc_idx(idx)
#endif

#ifndef arch_kmap_local_unmap_idx
#define arch_kmap_local_unmap_idx(idx, vaddr)	kmap_local_calc_idx(idx)
#endif

#ifndef arch_kmap_local_high_get
static inline void *arch_kmap_local_high_get(struct page *page)
{
	return NULL;
}
#endif

#ifndef arch_kmap_local_set_pte
#define arch_kmap_local_set_pte(mm, vaddr, ptep, ptev)	\
	set_pte_at(mm, vaddr, ptep, ptev)
#endif

/* Unmap a local mapping which was obtained by kmap_high_get() */
static inline bool kmap_high_unmap_local(unsigned long vaddr)
{
#ifdef ARCH_NEEDS_KMAP_HIGH_GET
	if (vaddr >= PKMAP_ADDR(0) && vaddr < PKMAP_ADDR(LAST_PKMAP)) {
		kunmap_high(pte_page(pkmap_page_table[PKMAP_NR(vaddr)]));
		return true;
	}
#endif
	return false;
}

static inline int kmap_local_calc_idx(int idx)
{
	return idx + KM_MAX_IDX * smp_processor_id();
}

static pte_t *__kmap_pte;

static pte_t *kmap_get_pte(void)
{
	if (!__kmap_pte)
		__kmap_pte = virt_to_kpte(__fix_to_virt(FIX_KMAP_BEGIN));
	return __kmap_pte;
}

void *__kmap_local_pfn_prot(unsigned long pfn, pgprot_t prot)
{
	pte_t pteval, *kmap_pte = kmap_get_pte();
	unsigned long vaddr;
	int idx;

	/*
	 * Disable migration so resulting virtual address is stable
	 * accross preemption.
	 */
	migrate_disable();
	preempt_disable();
	idx = arch_kmap_local_map_idx(kmap_local_idx_push(), pfn);
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	BUG_ON(!pte_none(*(kmap_pte - idx)));
	pteval = pfn_pte(pfn, prot);
	arch_kmap_local_set_pte(&init_mm, vaddr, kmap_pte - idx, pteval);
	arch_kmap_local_post_map(vaddr, pteval);
	current->kmap_ctrl.pteval[kmap_local_idx()] = pteval;
	preempt_enable();

	return (void *)vaddr;
}
EXPORT_SYMBOL_GPL(__kmap_local_pfn_prot);

void *__kmap_local_page_prot(struct page *page, pgprot_t prot)
{
	void *kmap;

	/*
	 * To broaden the usage of the actual kmap_local() machinery always map
	 * pages when debugging is enabled and the architecture has no problems
	 * with alias mappings.
	 */
	if (!IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL_FORCE_MAP) && !PageHighMem(page))
		return page_address(page);

	/* Try kmap_high_get() if architecture has it enabled */
	kmap = arch_kmap_local_high_get(page);
	if (kmap)
		return kmap;

	return __kmap_local_pfn_prot(page_to_pfn(page), prot);
}
EXPORT_SYMBOL(__kmap_local_page_prot);

void kunmap_local_indexed(void *vaddr)
{
	unsigned long addr = (unsigned long) vaddr & PAGE_MASK;
	pte_t *kmap_pte = kmap_get_pte();
	int idx;

	if (addr < __fix_to_virt(FIX_KMAP_END) ||
	    addr > __fix_to_virt(FIX_KMAP_BEGIN)) {
		if (IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL_FORCE_MAP)) {
			/* This _should_ never happen! See above. */
			WARN_ON_ONCE(1);
			return;
		}
		/*
		 * Handle mappings which were obtained by kmap_high_get()
		 * first as the virtual address of such mappings is below
		 * PAGE_OFFSET. Warn for all other addresses which are in
		 * the user space part of the virtual address space.
		 */
		if (!kmap_high_unmap_local(addr))
			WARN_ON_ONCE(addr < PAGE_OFFSET);
		return;
	}

	preempt_disable();
	idx = arch_kmap_local_unmap_idx(kmap_local_idx(), addr);
	WARN_ON_ONCE(addr != __fix_to_virt(FIX_KMAP_BEGIN + idx));

	arch_kmap_local_pre_unmap(addr);
	pte_clear(&init_mm, addr, kmap_pte - idx);
	arch_kmap_local_post_unmap(addr);
	current->kmap_ctrl.pteval[kmap_local_idx()] = __pte(0);
	kmap_local_idx_pop();
	preempt_enable();
	migrate_enable();
}
EXPORT_SYMBOL(kunmap_local_indexed);

/*
 * Invoked before switch_to(). This is safe even when during or after
 * clearing the maps an interrupt which needs a kmap_local happens because
 * the task::kmap_ctrl.idx is not modified by the unmapping code so a
 * nested kmap_local will use the next unused index and restore the index
 * on unmap. The already cleared kmaps of the outgoing task are irrelevant
 * because the interrupt context does not know about them. The same applies
 * when scheduling back in for an interrupt which happens before the
 * restore is complete.
 */
void __kmap_local_sched_out(void)
{
	struct task_struct *tsk = current;
	pte_t *kmap_pte = kmap_get_pte();
	int i;

	/* Clear kmaps */
	for (i = 0; i < tsk->kmap_ctrl.idx; i++) {
		pte_t pteval = tsk->kmap_ctrl.pteval[i];
		unsigned long addr;
		int idx;

		/* With debug all even slots are unmapped and act as guard */
		if (IS_ENABLED(CONFIG_DEBUG_HIGHMEM) && !(i & 0x01)) {
			WARN_ON_ONCE(!pte_none(pteval));
			continue;
		}
		if (WARN_ON_ONCE(pte_none(pteval)))
			continue;

		/*
		 * This is a horrible hack for XTENSA to calculate the
		 * coloured PTE index. Uses the PFN encoded into the pteval
		 * and the map index calculation because the actual mapped
		 * virtual address is not stored in task::kmap_ctrl.
		 * For any sane architecture this is optimized out.
		 */
		idx = arch_kmap_local_map_idx(i, pte_pfn(pteval));

		addr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
		arch_kmap_local_pre_unmap(addr);
		pte_clear(&init_mm, addr, kmap_pte - idx);
		arch_kmap_local_post_unmap(addr);
	}
}

void __kmap_local_sched_in(void)
{
	struct task_struct *tsk = current;
	pte_t *kmap_pte = kmap_get_pte();
	int i;

	/* Restore kmaps */
	for (i = 0; i < tsk->kmap_ctrl.idx; i++) {
		pte_t pteval = tsk->kmap_ctrl.pteval[i];
		unsigned long addr;
		int idx;

		/* With debug all even slots are unmapped and act as guard */
		if (IS_ENABLED(CONFIG_DEBUG_HIGHMEM) && !(i & 0x01)) {
			WARN_ON_ONCE(!pte_none(pteval));
			continue;
		}
		if (WARN_ON_ONCE(pte_none(pteval)))
			continue;

		/* See comment in __kmap_local_sched_out() */
		idx = arch_kmap_local_map_idx(i, pte_pfn(pteval));
		addr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
		set_pte_at(&init_mm, addr, kmap_pte - idx, pteval);
		arch_kmap_local_post_map(addr, pteval);
	}
}

void kmap_local_fork(struct task_struct *tsk)
{
	if (WARN_ON_ONCE(tsk->kmap_ctrl.idx))
		memset(&tsk->kmap_ctrl, 0, sizeof(tsk->kmap_ctrl));
}

#endif

#if defined(HASHED_PAGE_VIRTUAL)

#define PA_HASH_ORDER	7

/*
 * Describes one page->virtual association
 */
struct page_address_map {
	struct page *page;
	void *virtual;
	struct list_head list;
};

static struct page_address_map page_address_maps[LAST_PKMAP];

/*
 * Hash table bucket
 */
static struct page_address_slot {
	struct list_head lh;			/* List of page_address_maps */
	spinlock_t lock;			/* Protect this bucket's list */
} ____cacheline_aligned_in_smp page_address_htable[1<<PA_HASH_ORDER];

static struct page_address_slot *page_slot(const struct page *page)
{
	return &page_address_htable[hash_ptr(page, PA_HASH_ORDER)];
}

/**
 * page_address - get the mapped virtual address of a page
 * @page: &struct page to get the virtual address of
 *
 * Returns the page's virtual address.
 */
void *page_address(const struct page *page)
{
	unsigned long flags;
	void *ret;
	struct page_address_slot *pas;

	if (!PageHighMem(page))
		return lowmem_page_address(page);

	pas = page_slot(page);
	ret = NULL;
	spin_lock_irqsave(&pas->lock, flags);
	if (!list_empty(&pas->lh)) {
		struct page_address_map *pam;

		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				ret = pam->virtual;
				goto done;
			}
		}
	}
done:
	spin_unlock_irqrestore(&pas->lock, flags);
	return ret;
}

EXPORT_SYMBOL(page_address);

/**
 * set_page_address - set a page's virtual address
 * @page: &struct page to set
 * @virtual: virtual address to use
 */
void set_page_address(struct page *page, void *virtual)
{
	unsigned long flags;
	struct page_address_slot *pas;
	struct page_address_map *pam;

	BUG_ON(!PageHighMem(page));

	pas = page_slot(page);
	if (virtual) {		/* Add */
		pam = &page_address_maps[PKMAP_NR((unsigned long)virtual)];
		pam->page = page;
		pam->virtual = virtual;

		spin_lock_irqsave(&pas->lock, flags);
		list_add_tail(&pam->list, &pas->lh);
		spin_unlock_irqrestore(&pas->lock, flags);
	} else {		/* Remove */
		spin_lock_irqsave(&pas->lock, flags);
		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				list_del(&pam->list);
				spin_unlock_irqrestore(&pas->lock, flags);
				goto done;
			}
		}
		spin_unlock_irqrestore(&pas->lock, flags);
	}
done:
	return;
}

void __init page_address_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(page_address_htable); i++) {
		INIT_LIST_HEAD(&page_address_htable[i].lh);
		spin_lock_init(&page_address_htable[i].lock);
	}
}

#endif	/* defined(HASHED_PAGE_VIRTUAL) */
