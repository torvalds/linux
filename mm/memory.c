/*
 *  linux/mm/memory.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

/*
 * 05.04.94  -  Multi-page memory management added for v1.1.
 *              Idea by Alex Bligh (alex@cconcepts.co.uk)
 *
 * 16.07.99  -  Support of BIGMEM added by Gerhard Wichert, Siemens AG
 *		(Gerhard.Wichert@pdb.siemens.de)
 *
 * Aug/Sep 2004 Changed to four level page tables (Andi Kleen)
 */

#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/task.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/memremap.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/export.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/pfn_t.h>
#include <linux/writeback.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/swapops.h>
#include <linux/elf.h>
#include <linux/gfp.h>
#include <linux/migrate.h>
#include <linux/string.h>
#include <linux/dma-debug.h>
#include <linux/debugfs.h>
#include <linux/userfaultfd_k.h>
#include <linux/dax.h>
#include <linux/oom.h>

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include "internal.h"

#if defined(LAST_CPUPID_NOT_IN_PAGE_FLAGS) && !defined(CONFIG_COMPILE_TEST)
#warning Unfortunate NUMA and NUMA Balancing config, growing page-frame for last_cpupid.
#endif

#ifndef CONFIG_NEED_MULTIPLE_NODES
/* use the per-pgdat data instead for discontigmem - mbligh */
unsigned long max_mapnr;
EXPORT_SYMBOL(max_mapnr);

struct page *mem_map;
EXPORT_SYMBOL(mem_map);
#endif

/*
 * A number of key systems in x86 including ioremap() rely on the assumption
 * that high_memory defines the upper bound on direct map memory, then end
 * of ZONE_NORMAL.  Under CONFIG_DISCONTIG this means that max_low_pfn and
 * highstart_pfn must be the same; there must be no gap between ZONE_NORMAL
 * and ZONE_HIGHMEM.
 */
void *high_memory;
EXPORT_SYMBOL(high_memory);

/*
 * Randomize the address space (stacks, mmaps, brk, etc.).
 *
 * ( When CONFIG_COMPAT_BRK=y we exclude brk from randomization,
 *   as ancient (libc5 based) binaries can segfault. )
 */
int randomize_va_space __read_mostly =
#ifdef CONFIG_COMPAT_BRK
					1;
#else
					2;
#endif

static int __init disable_randmaps(char *s)
{
	randomize_va_space = 0;
	return 1;
}
__setup("norandmaps", disable_randmaps);

unsigned long zero_pfn __read_mostly;
EXPORT_SYMBOL(zero_pfn);

unsigned long highest_memmap_pfn __read_mostly;

/*
 * CONFIG_MMU architectures set up ZERO_PAGE in their paging_init()
 */
static int __init init_zero_pfn(void)
{
	zero_pfn = page_to_pfn(ZERO_PAGE(0));
	return 0;
}
core_initcall(init_zero_pfn);


#if defined(SPLIT_RSS_COUNTING)

void sync_mm_rss(struct mm_struct *mm)
{
	int i;

	for (i = 0; i < NR_MM_COUNTERS; i++) {
		if (current->rss_stat.count[i]) {
			add_mm_counter(mm, i, current->rss_stat.count[i]);
			current->rss_stat.count[i] = 0;
		}
	}
	current->rss_stat.events = 0;
}

static void add_mm_counter_fast(struct mm_struct *mm, int member, int val)
{
	struct task_struct *task = current;

	if (likely(task->mm == mm))
		task->rss_stat.count[member] += val;
	else
		add_mm_counter(mm, member, val);
}
#define inc_mm_counter_fast(mm, member) add_mm_counter_fast(mm, member, 1)
#define dec_mm_counter_fast(mm, member) add_mm_counter_fast(mm, member, -1)

/* sync counter once per 64 page faults */
#define TASK_RSS_EVENTS_THRESH	(64)
static void check_sync_rss_stat(struct task_struct *task)
{
	if (unlikely(task != current))
		return;
	if (unlikely(task->rss_stat.events++ > TASK_RSS_EVENTS_THRESH))
		sync_mm_rss(task->mm);
}
#else /* SPLIT_RSS_COUNTING */

#define inc_mm_counter_fast(mm, member) inc_mm_counter(mm, member)
#define dec_mm_counter_fast(mm, member) dec_mm_counter(mm, member)

static void check_sync_rss_stat(struct task_struct *task)
{
}

#endif /* SPLIT_RSS_COUNTING */

#ifdef HAVE_GENERIC_MMU_GATHER

static bool tlb_next_batch(struct mmu_gather *tlb)
{
	struct mmu_gather_batch *batch;

	batch = tlb->active;
	if (batch->next) {
		tlb->active = batch->next;
		return true;
	}

	if (tlb->batch_count == MAX_GATHER_BATCH_COUNT)
		return false;

	batch = (void *)__get_free_pages(GFP_NOWAIT | __GFP_NOWARN, 0);
	if (!batch)
		return false;

	tlb->batch_count++;
	batch->next = NULL;
	batch->nr   = 0;
	batch->max  = MAX_GATHER_BATCH;

	tlb->active->next = batch;
	tlb->active = batch;

	return true;
}

void arch_tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
				unsigned long start, unsigned long end)
{
	tlb->mm = mm;

	/* Is it from 0 to ~0? */
	tlb->fullmm     = !(start | (end+1));
	tlb->need_flush_all = 0;
	tlb->local.next = NULL;
	tlb->local.nr   = 0;
	tlb->local.max  = ARRAY_SIZE(tlb->__pages);
	tlb->active     = &tlb->local;
	tlb->batch_count = 0;

#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	tlb->batch = NULL;
#endif
	tlb->page_size = 0;

	__tlb_reset_range(tlb);
}

static void tlb_flush_mmu_tlbonly(struct mmu_gather *tlb)
{
	if (!tlb->end)
		return;

	tlb_flush(tlb);
	mmu_notifier_invalidate_range(tlb->mm, tlb->start, tlb->end);
#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	tlb_table_flush(tlb);
#endif
	__tlb_reset_range(tlb);
}

static void tlb_flush_mmu_free(struct mmu_gather *tlb)
{
	struct mmu_gather_batch *batch;

	for (batch = &tlb->local; batch && batch->nr; batch = batch->next) {
		free_pages_and_swap_cache(batch->pages, batch->nr);
		batch->nr = 0;
	}
	tlb->active = &tlb->local;
}

void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush_mmu_tlbonly(tlb);
	tlb_flush_mmu_free(tlb);
}

/* tlb_finish_mmu
 *	Called at the end of the shootdown operation to free up any resources
 *	that were required.
 */
void arch_tlb_finish_mmu(struct mmu_gather *tlb,
		unsigned long start, unsigned long end, bool force)
{
	struct mmu_gather_batch *batch, *next;

	if (force)
		__tlb_adjust_range(tlb, start, end - start);

	tlb_flush_mmu(tlb);

	/* keep the page table cache within bounds */
	check_pgt_cache();

	for (batch = tlb->local.next; batch; batch = next) {
		next = batch->next;
		free_pages((unsigned long)batch, 0);
	}
	tlb->local.next = NULL;
}

/* __tlb_remove_page
 *	Must perform the equivalent to __free_pte(pte_get_and_clear(ptep)), while
 *	handling the additional races in SMP caused by other CPUs caching valid
 *	mappings in their TLBs. Returns the number of free page slots left.
 *	When out of page slots we must call tlb_flush_mmu().
 *returns true if the caller should flush.
 */
bool __tlb_remove_page_size(struct mmu_gather *tlb, struct page *page, int page_size)
{
	struct mmu_gather_batch *batch;

	VM_BUG_ON(!tlb->end);
	VM_WARN_ON(tlb->page_size != page_size);

	batch = tlb->active;
	/*
	 * Add the page and check if we are full. If so
	 * force a flush.
	 */
	batch->pages[batch->nr++] = page;
	if (batch->nr == batch->max) {
		if (!tlb_next_batch(tlb))
			return true;
		batch = tlb->active;
	}
	VM_BUG_ON_PAGE(batch->nr > batch->max, page);

	return false;
}

#endif /* HAVE_GENERIC_MMU_GATHER */

#ifdef CONFIG_HAVE_RCU_TABLE_FREE

/*
 * See the comment near struct mmu_table_batch.
 */

static void tlb_remove_table_smp_sync(void *arg)
{
	/* Simply deliver the interrupt */
}

static void tlb_remove_table_one(void *table)
{
	/*
	 * This isn't an RCU grace period and hence the page-tables cannot be
	 * assumed to be actually RCU-freed.
	 *
	 * It is however sufficient for software page-table walkers that rely on
	 * IRQ disabling. See the comment near struct mmu_table_batch.
	 */
	smp_call_function(tlb_remove_table_smp_sync, NULL, 1);
	__tlb_remove_table(table);
}

static void tlb_remove_table_rcu(struct rcu_head *head)
{
	struct mmu_table_batch *batch;
	int i;

	batch = container_of(head, struct mmu_table_batch, rcu);

	for (i = 0; i < batch->nr; i++)
		__tlb_remove_table(batch->tables[i]);

	free_page((unsigned long)batch);
}

void tlb_table_flush(struct mmu_gather *tlb)
{
	struct mmu_table_batch **batch = &tlb->batch;

	if (*batch) {
		call_rcu_sched(&(*batch)->rcu, tlb_remove_table_rcu);
		*batch = NULL;
	}
}

void tlb_remove_table(struct mmu_gather *tlb, void *table)
{
	struct mmu_table_batch **batch = &tlb->batch;

	/*
	 * When there's less then two users of this mm there cannot be a
	 * concurrent page-table walk.
	 */
	if (atomic_read(&tlb->mm->mm_users) < 2) {
		__tlb_remove_table(table);
		return;
	}

	if (*batch == NULL) {
		*batch = (struct mmu_table_batch *)__get_free_page(GFP_NOWAIT | __GFP_NOWARN);
		if (*batch == NULL) {
			tlb_remove_table_one(table);
			return;
		}
		(*batch)->nr = 0;
	}
	(*batch)->tables[(*batch)->nr++] = table;
	if ((*batch)->nr == MAX_TABLE_BATCH)
		tlb_table_flush(tlb);
}

#endif /* CONFIG_HAVE_RCU_TABLE_FREE */

/**
 * tlb_gather_mmu - initialize an mmu_gather structure for page-table tear-down
 * @tlb: the mmu_gather structure to initialize
 * @mm: the mm_struct of the target address space
 * @start: start of the region that will be removed from the page-table
 * @end: end of the region that will be removed from the page-table
 *
 * Called to initialize an (on-stack) mmu_gather structure for page-table
 * tear-down from @mm. The @start and @end are set to 0 and -1
 * respectively when @mm is without users and we're going to destroy
 * the full address space (exit/execve).
 */
void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
			unsigned long start, unsigned long end)
{
	arch_tlb_gather_mmu(tlb, mm, start, end);
	inc_tlb_flush_pending(tlb->mm);
}

void tlb_finish_mmu(struct mmu_gather *tlb,
		unsigned long start, unsigned long end)
{
	/*
	 * If there are parallel threads are doing PTE changes on same range
	 * under non-exclusive lock(e.g., mmap_sem read-side) but defer TLB
	 * flush by batching, a thread has stable TLB entry can fail to flush
	 * the TLB by observing pte_none|!pte_dirty, for example so flush TLB
	 * forcefully if we detect parallel PTE batching threads.
	 */
	bool force = mm_tlb_flush_nested(tlb->mm);

	arch_tlb_finish_mmu(tlb, start, end, force);
	dec_tlb_flush_pending(tlb->mm);
}

/*
 * Note: this doesn't free the actual pages themselves. That
 * has been handled earlier when unmapping all the memory regions.
 */
static void free_pte_range(struct mmu_gather *tlb, pmd_t *pmd,
			   unsigned long addr)
{
	pgtable_t token = pmd_pgtable(*pmd);
	pmd_clear(pmd);
	pte_free_tlb(tlb, token, addr);
	mm_dec_nr_ptes(tlb->mm);
}

static inline void free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;

	start = addr;
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		free_pte_range(tlb, pmd, addr);
	} while (pmd++, addr = next, addr != end);

	start &= PUD_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PUD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pmd = pmd_offset(pud, start);
	pud_clear(pud);
	pmd_free_tlb(tlb, pmd, start);
	mm_dec_nr_pmds(tlb->mm);
}

static inline void free_pud_range(struct mmu_gather *tlb, p4d_t *p4d,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		free_pmd_range(tlb, pud, addr, next, floor, ceiling);
	} while (pud++, addr = next, addr != end);

	start &= P4D_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= P4D_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pud = pud_offset(p4d, start);
	p4d_clear(p4d);
	pud_free_tlb(tlb, pud, start);
	mm_dec_nr_puds(tlb->mm);
}

static inline void free_p4d_range(struct mmu_gather *tlb, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	p4d_t *p4d;
	unsigned long next;
	unsigned long start;

	start = addr;
	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		free_pud_range(tlb, p4d, addr, next, floor, ceiling);
	} while (p4d++, addr = next, addr != end);

	start &= PGDIR_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PGDIR_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	p4d = p4d_offset(pgd, start);
	pgd_clear(pgd);
	p4d_free_tlb(tlb, p4d, start);
}

/*
 * This function frees user-level page tables of a process.
 */
void free_pgd_range(struct mmu_gather *tlb,
			unsigned long addr, unsigned long end,
			unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;

	/*
	 * The next few lines have given us lots of grief...
	 *
	 * Why are we testing PMD* at this top level?  Because often
	 * there will be no work to do at all, and we'd prefer not to
	 * go all the way down to the bottom just to discover that.
	 *
	 * Why all these "- 1"s?  Because 0 represents both the bottom
	 * of the address space and the top of it (using -1 for the
	 * top wouldn't help much: the masks would do the wrong thing).
	 * The rule is that addr 0 and floor 0 refer to the bottom of
	 * the address space, but end 0 and ceiling 0 refer to the top
	 * Comparisons need to use "end - 1" and "ceiling - 1" (though
	 * that end 0 case should be mythical).
	 *
	 * Wherever addr is brought up or ceiling brought down, we must
	 * be careful to reject "the opposite 0" before it confuses the
	 * subsequent tests.  But what about where end is brought down
	 * by PMD_SIZE below? no, end can't go down to 0 there.
	 *
	 * Whereas we round start (addr) and ceiling down, by different
	 * masks at different levels, in order to test whether a table
	 * now has no other vmas using it, so can be freed, we don't
	 * bother to round floor or end up - the tests don't need that.
	 */

	addr &= PMD_MASK;
	if (addr < floor) {
		addr += PMD_SIZE;
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= PMD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= PMD_SIZE;
	if (addr > end - 1)
		return;
	/*
	 * We add page table cache pages with PAGE_SIZE,
	 * (see pte_free_tlb()), flush the tlb if we need
	 */
	tlb_remove_check_page_size_change(tlb, PAGE_SIZE);
	pgd = pgd_offset(tlb->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		free_p4d_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

void free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *vma,
		unsigned long floor, unsigned long ceiling)
{
	while (vma) {
		struct vm_area_struct *next = vma->vm_next;
		unsigned long addr = vma->vm_start;

		/*
		 * Hide vma from rmap and truncate_pagecache before freeing
		 * pgtables
		 */
		unlink_anon_vmas(vma);
		unlink_file_vma(vma);

		if (is_vm_hugetlb_page(vma)) {
			hugetlb_free_pgd_range(tlb, addr, vma->vm_end,
				floor, next ? next->vm_start : ceiling);
		} else {
			/*
			 * Optimization: gather nearby vmas into one call down
			 */
			while (next && next->vm_start <= vma->vm_end + PMD_SIZE
			       && !is_vm_hugetlb_page(next)) {
				vma = next;
				next = vma->vm_next;
				unlink_anon_vmas(vma);
				unlink_file_vma(vma);
			}
			free_pgd_range(tlb, addr, vma->vm_end,
				floor, next ? next->vm_start : ceiling);
		}
		vma = next;
	}
}

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd, unsigned long address)
{
	spinlock_t *ptl;
	pgtable_t new = pte_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	/*
	 * Ensure all pte setup (eg. pte page lock and page clearing) are
	 * visible before the pte is made visible to other CPUs by being
	 * put into page tables.
	 *
	 * The other side of the story is the pointer chasing in the page
	 * table walking code (when walking the page table without locking;
	 * ie. most of the time). Fortunately, these data accesses consist
	 * of a chain of data-dependent loads, meaning most CPUs (alpha
	 * being the notable exception) will already guarantee loads are
	 * seen in-order. See the alpha page table accessors for the
	 * smp_read_barrier_depends() barriers in page table walking code.
	 */
	smp_wmb(); /* Could be smp_wmb__xxx(before|after)_spin_lock */

	ptl = pmd_lock(mm, pmd);
	if (likely(pmd_none(*pmd))) {	/* Has another populated it ? */
		mm_inc_nr_ptes(mm);
		pmd_populate(mm, pmd, new);
		new = NULL;
	}
	spin_unlock(ptl);
	if (new)
		pte_free(mm, new);
	return 0;
}

int __pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	pte_t *new = pte_alloc_one_kernel(&init_mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&init_mm.page_table_lock);
	if (likely(pmd_none(*pmd))) {	/* Has another populated it ? */
		pmd_populate_kernel(&init_mm, pmd, new);
		new = NULL;
	}
	spin_unlock(&init_mm.page_table_lock);
	if (new)
		pte_free_kernel(&init_mm, new);
	return 0;
}

static inline void init_rss_vec(int *rss)
{
	memset(rss, 0, sizeof(int) * NR_MM_COUNTERS);
}

static inline void add_mm_rss_vec(struct mm_struct *mm, int *rss)
{
	int i;

	if (current->mm == mm)
		sync_mm_rss(mm);
	for (i = 0; i < NR_MM_COUNTERS; i++)
		if (rss[i])
			add_mm_counter(mm, i, rss[i]);
}

/*
 * This function is called to print an error when a bad pte
 * is found. For example, we might have a PFN-mapped pte in
 * a region that doesn't allow it.
 *
 * The calling function must still handle the error.
 */
static void print_bad_pte(struct vm_area_struct *vma, unsigned long addr,
			  pte_t pte, struct page *page)
{
	pgd_t *pgd = pgd_offset(vma->vm_mm, addr);
	p4d_t *p4d = p4d_offset(pgd, addr);
	pud_t *pud = pud_offset(p4d, addr);
	pmd_t *pmd = pmd_offset(pud, addr);
	struct address_space *mapping;
	pgoff_t index;
	static unsigned long resume;
	static unsigned long nr_shown;
	static unsigned long nr_unshown;

	/*
	 * Allow a burst of 60 reports, then keep quiet for that minute;
	 * or allow a steady drip of one report per second.
	 */
	if (nr_shown == 60) {
		if (time_before(jiffies, resume)) {
			nr_unshown++;
			return;
		}
		if (nr_unshown) {
			pr_alert("BUG: Bad page map: %lu messages suppressed\n",
				 nr_unshown);
			nr_unshown = 0;
		}
		nr_shown = 0;
	}
	if (nr_shown++ == 0)
		resume = jiffies + 60 * HZ;

	mapping = vma->vm_file ? vma->vm_file->f_mapping : NULL;
	index = linear_page_index(vma, addr);

	pr_alert("BUG: Bad page map in process %s  pte:%08llx pmd:%08llx\n",
		 current->comm,
		 (long long)pte_val(pte), (long long)pmd_val(*pmd));
	if (page)
		dump_page(page, "bad pte");
	pr_alert("addr:%p vm_flags:%08lx anon_vma:%p mapping:%p index:%lx\n",
		 (void *)addr, vma->vm_flags, vma->anon_vma, mapping, index);
	pr_alert("file:%pD fault:%pf mmap:%pf readpage:%pf\n",
		 vma->vm_file,
		 vma->vm_ops ? vma->vm_ops->fault : NULL,
		 vma->vm_file ? vma->vm_file->f_op->mmap : NULL,
		 mapping ? mapping->a_ops->readpage : NULL);
	dump_stack();
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
}

/*
 * vm_normal_page -- This function gets the "struct page" associated with a pte.
 *
 * "Special" mappings do not wish to be associated with a "struct page" (either
 * it doesn't exist, or it exists but they don't want to touch it). In this
 * case, NULL is returned here. "Normal" mappings do have a struct page.
 *
 * There are 2 broad cases. Firstly, an architecture may define a pte_special()
 * pte bit, in which case this function is trivial. Secondly, an architecture
 * may not have a spare pte bit, which requires a more complicated scheme,
 * described below.
 *
 * A raw VM_PFNMAP mapping (ie. one that is not COWed) is always considered a
 * special mapping (even if there are underlying and valid "struct pages").
 * COWed pages of a VM_PFNMAP are always normal.
 *
 * The way we recognize COWed pages within VM_PFNMAP mappings is through the
 * rules set up by "remap_pfn_range()": the vma will have the VM_PFNMAP bit
 * set, and the vm_pgoff will point to the first PFN mapped: thus every special
 * mapping will always honor the rule
 *
 *	pfn_of_page == vma->vm_pgoff + ((addr - vma->vm_start) >> PAGE_SHIFT)
 *
 * And for normal mappings this is false.
 *
 * This restricts such mappings to be a linear translation from virtual address
 * to pfn. To get around this restriction, we allow arbitrary mappings so long
 * as the vma is not a COW mapping; in that case, we know that all ptes are
 * special (because none can have been COWed).
 *
 *
 * In order to support COW of arbitrary special mappings, we have VM_MIXEDMAP.
 *
 * VM_MIXEDMAP mappings can likewise contain memory with or without "struct
 * page" backing, however the difference is that _all_ pages with a struct
 * page (that is, those where pfn_valid is true) are refcounted and considered
 * normal pages by the VM. The disadvantage is that pages are refcounted
 * (which can be slower and simply not an option for some PFNMAP users). The
 * advantage is that we don't have to follow the strict linearity rule of
 * PFNMAP mappings in order to support COWable mappings.
 *
 */
#ifdef __HAVE_ARCH_PTE_SPECIAL
# define HAVE_PTE_SPECIAL 1
#else
# define HAVE_PTE_SPECIAL 0
#endif
struct page *_vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			     pte_t pte, bool with_public_device)
{
	unsigned long pfn = pte_pfn(pte);

	if (HAVE_PTE_SPECIAL) {
		if (likely(!pte_special(pte)))
			goto check_pfn;
		if (vma->vm_ops && vma->vm_ops->find_special_page)
			return vma->vm_ops->find_special_page(vma, addr);
		if (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))
			return NULL;
		if (is_zero_pfn(pfn))
			return NULL;

		/*
		 * Device public pages are special pages (they are ZONE_DEVICE
		 * pages but different from persistent memory). They behave
		 * allmost like normal pages. The difference is that they are
		 * not on the lru and thus should never be involve with any-
		 * thing that involve lru manipulation (mlock, numa balancing,
		 * ...).
		 *
		 * This is why we still want to return NULL for such page from
		 * vm_normal_page() so that we do not have to special case all
		 * call site of vm_normal_page().
		 */
		if (likely(pfn <= highest_memmap_pfn)) {
			struct page *page = pfn_to_page(pfn);

			if (is_device_public_page(page)) {
				if (with_public_device)
					return page;
				return NULL;
			}
		}
		print_bad_pte(vma, addr, pte, NULL);
		return NULL;
	}

	/* !HAVE_PTE_SPECIAL case follows: */

	if (unlikely(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP))) {
		if (vma->vm_flags & VM_MIXEDMAP) {
			if (!pfn_valid(pfn))
				return NULL;
			goto out;
		} else {
			unsigned long off;
			off = (addr - vma->vm_start) >> PAGE_SHIFT;
			if (pfn == vma->vm_pgoff + off)
				return NULL;
			if (!is_cow_mapping(vma->vm_flags))
				return NULL;
		}
	}

	if (is_zero_pfn(pfn))
		return NULL;
check_pfn:
	if (unlikely(pfn > highest_memmap_pfn)) {
		print_bad_pte(vma, addr, pte, NULL);
		return NULL;
	}

	/*
	 * NOTE! We still have PageReserved() pages in the page tables.
	 * eg. VDSO mappings can cause them to exist.
	 */
out:
	return pfn_to_page(pfn);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
struct page *vm_normal_page_pmd(struct vm_area_struct *vma, unsigned long addr,
				pmd_t pmd)
{
	unsigned long pfn = pmd_pfn(pmd);

	/*
	 * There is no pmd_special() but there may be special pmds, e.g.
	 * in a direct-access (dax) mapping, so let's just replicate the
	 * !HAVE_PTE_SPECIAL case from vm_normal_page() here.
	 */
	if (unlikely(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP))) {
		if (vma->vm_flags & VM_MIXEDMAP) {
			if (!pfn_valid(pfn))
				return NULL;
			goto out;
		} else {
			unsigned long off;
			off = (addr - vma->vm_start) >> PAGE_SHIFT;
			if (pfn == vma->vm_pgoff + off)
				return NULL;
			if (!is_cow_mapping(vma->vm_flags))
				return NULL;
		}
	}

	if (is_zero_pfn(pfn))
		return NULL;
	if (unlikely(pfn > highest_memmap_pfn))
		return NULL;

	/*
	 * NOTE! We still have PageReserved() pages in the page tables.
	 * eg. VDSO mappings can cause them to exist.
	 */
out:
	return pfn_to_page(pfn);
}
#endif

/*
 * copy one vm_area from one task to the other. Assumes the page tables
 * already present in the new task to be cleared in the whole range
 * covered by this vma.
 */

static inline unsigned long
copy_one_pte(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pte_t *dst_pte, pte_t *src_pte, struct vm_area_struct *vma,
		unsigned long addr, int *rss)
{
	unsigned long vm_flags = vma->vm_flags;
	pte_t pte = *src_pte;
	struct page *page;

	/* pte contains position in swap or file, so copy. */
	if (unlikely(!pte_present(pte))) {
		swp_entry_t entry = pte_to_swp_entry(pte);

		if (likely(!non_swap_entry(entry))) {
			if (swap_duplicate(entry) < 0)
				return entry.val;

			/* make sure dst_mm is on swapoff's mmlist. */
			if (unlikely(list_empty(&dst_mm->mmlist))) {
				spin_lock(&mmlist_lock);
				if (list_empty(&dst_mm->mmlist))
					list_add(&dst_mm->mmlist,
							&src_mm->mmlist);
				spin_unlock(&mmlist_lock);
			}
			rss[MM_SWAPENTS]++;
		} else if (is_migration_entry(entry)) {
			page = migration_entry_to_page(entry);

			rss[mm_counter(page)]++;

			if (is_write_migration_entry(entry) &&
					is_cow_mapping(vm_flags)) {
				/*
				 * COW mappings require pages in both
				 * parent and child to be set to read.
				 */
				make_migration_entry_read(&entry);
				pte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(*src_pte))
					pte = pte_swp_mksoft_dirty(pte);
				set_pte_at(src_mm, addr, src_pte, pte);
			}
		} else if (is_device_private_entry(entry)) {
			page = device_private_entry_to_page(entry);

			/*
			 * Update rss count even for unaddressable pages, as
			 * they should treated just like normal pages in this
			 * respect.
			 *
			 * We will likely want to have some new rss counters
			 * for unaddressable pages, at some point. But for now
			 * keep things as they are.
			 */
			get_page(page);
			rss[mm_counter(page)]++;
			page_dup_rmap(page, false);

			/*
			 * We do not preserve soft-dirty information, because so
			 * far, checkpoint/restore is the only feature that
			 * requires that. And checkpoint/restore does not work
			 * when a device driver is involved (you cannot easily
			 * save and restore device driver state).
			 */
			if (is_write_device_private_entry(entry) &&
			    is_cow_mapping(vm_flags)) {
				make_device_private_entry_read(&entry);
				pte = swp_entry_to_pte(entry);
				set_pte_at(src_mm, addr, src_pte, pte);
			}
		}
		goto out_set_pte;
	}

	/*
	 * If it's a COW mapping, write protect it both
	 * in the parent and the child
	 */
	if (is_cow_mapping(vm_flags)) {
		ptep_set_wrprotect(src_mm, addr, src_pte);
		pte = pte_wrprotect(pte);
	}

	/*
	 * If it's a shared mapping, mark it clean in
	 * the child
	 */
	if (vm_flags & VM_SHARED)
		pte = pte_mkclean(pte);
	pte = pte_mkold(pte);

	page = vm_normal_page(vma, addr, pte);
	if (page) {
		get_page(page);
		page_dup_rmap(page, false);
		rss[mm_counter(page)]++;
	} else if (pte_devmap(pte)) {
		page = pte_page(pte);

		/*
		 * Cache coherent device memory behave like regular page and
		 * not like persistent memory page. For more informations see
		 * MEMORY_DEVICE_CACHE_COHERENT in memory_hotplug.h
		 */
		if (is_device_public_page(page)) {
			get_page(page);
			page_dup_rmap(page, false);
			rss[mm_counter(page)]++;
		}
	}

out_set_pte:
	set_pte_at(dst_mm, addr, dst_pte, pte);
	return 0;
}

static int copy_pte_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		   pmd_t *dst_pmd, pmd_t *src_pmd, struct vm_area_struct *vma,
		   unsigned long addr, unsigned long end)
{
	pte_t *orig_src_pte, *orig_dst_pte;
	pte_t *src_pte, *dst_pte;
	spinlock_t *src_ptl, *dst_ptl;
	int progress = 0;
	int rss[NR_MM_COUNTERS];
	swp_entry_t entry = (swp_entry_t){0};

again:
	init_rss_vec(rss);

	dst_pte = pte_alloc_map_lock(dst_mm, dst_pmd, addr, &dst_ptl);
	if (!dst_pte)
		return -ENOMEM;
	src_pte = pte_offset_map(src_pmd, addr);
	src_ptl = pte_lockptr(src_mm, src_pmd);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
	orig_src_pte = src_pte;
	orig_dst_pte = dst_pte;
	arch_enter_lazy_mmu_mode();

	do {
		/*
		 * We are holding two locks at this point - either of them
		 * could generate latencies in another task on another CPU.
		 */
		if (progress >= 32) {
			progress = 0;
			if (need_resched() ||
			    spin_needbreak(src_ptl) || spin_needbreak(dst_ptl))
				break;
		}
		if (pte_none(*src_pte)) {
			progress++;
			continue;
		}
		entry.val = copy_one_pte(dst_mm, src_mm, dst_pte, src_pte,
							vma, addr, rss);
		if (entry.val)
			break;
		progress += 8;
	} while (dst_pte++, src_pte++, addr += PAGE_SIZE, addr != end);

	arch_leave_lazy_mmu_mode();
	spin_unlock(src_ptl);
	pte_unmap(orig_src_pte);
	add_mm_rss_vec(dst_mm, rss);
	pte_unmap_unlock(orig_dst_pte, dst_ptl);
	cond_resched();

	if (entry.val) {
		if (add_swap_count_continuation(entry, GFP_KERNEL) < 0)
			return -ENOMEM;
		progress = 0;
	}
	if (addr != end)
		goto again;
	return 0;
}

static inline int copy_pmd_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pud_t *dst_pud, pud_t *src_pud, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pmd_t *src_pmd, *dst_pmd;
	unsigned long next;

	dst_pmd = pmd_alloc(dst_mm, dst_pud, addr);
	if (!dst_pmd)
		return -ENOMEM;
	src_pmd = pmd_offset(src_pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (is_swap_pmd(*src_pmd) || pmd_trans_huge(*src_pmd)
			|| pmd_devmap(*src_pmd)) {
			int err;
			VM_BUG_ON_VMA(next-addr != HPAGE_PMD_SIZE, vma);
			err = copy_huge_pmd(dst_mm, src_mm,
					    dst_pmd, src_pmd, addr, vma);
			if (err == -ENOMEM)
				return -ENOMEM;
			if (!err)
				continue;
			/* fall through */
		}
		if (pmd_none_or_clear_bad(src_pmd))
			continue;
		if (copy_pte_range(dst_mm, src_mm, dst_pmd, src_pmd,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pmd++, src_pmd++, addr = next, addr != end);
	return 0;
}

static inline int copy_pud_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		p4d_t *dst_p4d, p4d_t *src_p4d, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pud_t *src_pud, *dst_pud;
	unsigned long next;

	dst_pud = pud_alloc(dst_mm, dst_p4d, addr);
	if (!dst_pud)
		return -ENOMEM;
	src_pud = pud_offset(src_p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_trans_huge(*src_pud) || pud_devmap(*src_pud)) {
			int err;

			VM_BUG_ON_VMA(next-addr != HPAGE_PUD_SIZE, vma);
			err = copy_huge_pud(dst_mm, src_mm,
					    dst_pud, src_pud, addr, vma);
			if (err == -ENOMEM)
				return -ENOMEM;
			if (!err)
				continue;
			/* fall through */
		}
		if (pud_none_or_clear_bad(src_pud))
			continue;
		if (copy_pmd_range(dst_mm, src_mm, dst_pud, src_pud,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pud++, src_pud++, addr = next, addr != end);
	return 0;
}

static inline int copy_p4d_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pgd_t *dst_pgd, pgd_t *src_pgd, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	p4d_t *src_p4d, *dst_p4d;
	unsigned long next;

	dst_p4d = p4d_alloc(dst_mm, dst_pgd, addr);
	if (!dst_p4d)
		return -ENOMEM;
	src_p4d = p4d_offset(src_pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(src_p4d))
			continue;
		if (copy_pud_range(dst_mm, src_mm, dst_p4d, src_p4d,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_p4d++, src_p4d++, addr = next, addr != end);
	return 0;
}

int copy_page_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		struct vm_area_struct *vma)
{
	pgd_t *src_pgd, *dst_pgd;
	unsigned long next;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */
	bool is_cow;
	int ret;

	/*
	 * Don't copy ptes where a page fault will fill them correctly.
	 * Fork becomes much lighter when there are big shared or private
	 * readonly mappings. The tradeoff is that copy_page_range is more
	 * efficient than faulting.
	 */
	if (!(vma->vm_flags & (VM_HUGETLB | VM_PFNMAP | VM_MIXEDMAP)) &&
			!vma->anon_vma)
		return 0;

	if (is_vm_hugetlb_page(vma))
		return copy_hugetlb_page_range(dst_mm, src_mm, vma);

	if (unlikely(vma->vm_flags & VM_PFNMAP)) {
		/*
		 * We do not free on error cases below as remove_vma
		 * gets called on error from higher level routine
		 */
		ret = track_pfn_copy(vma);
		if (ret)
			return ret;
	}

	/*
	 * We need to invalidate the secondary MMU mappings only when
	 * there could be a permission downgrade on the ptes of the
	 * parent mm. And a permission downgrade will only happen if
	 * is_cow_mapping() returns true.
	 */
	is_cow = is_cow_mapping(vma->vm_flags);
	mmun_start = addr;
	mmun_end   = end;
	if (is_cow)
		mmu_notifier_invalidate_range_start(src_mm, mmun_start,
						    mmun_end);

	ret = 0;
	dst_pgd = pgd_offset(dst_mm, addr);
	src_pgd = pgd_offset(src_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(src_pgd))
			continue;
		if (unlikely(copy_p4d_range(dst_mm, src_mm, dst_pgd, src_pgd,
					    vma, addr, next))) {
			ret = -ENOMEM;
			break;
		}
	} while (dst_pgd++, src_pgd++, addr = next, addr != end);

	if (is_cow)
		mmu_notifier_invalidate_range_end(src_mm, mmun_start, mmun_end);
	return ret;
}

static unsigned long zap_pte_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	struct mm_struct *mm = tlb->mm;
	int force_flush = 0;
	int rss[NR_MM_COUNTERS];
	spinlock_t *ptl;
	pte_t *start_pte;
	pte_t *pte;
	swp_entry_t entry;

	tlb_remove_check_page_size_change(tlb, PAGE_SIZE);
again:
	init_rss_vec(rss);
	start_pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	pte = start_pte;
	flush_tlb_batched_pending(mm);
	arch_enter_lazy_mmu_mode();
	do {
		pte_t ptent = *pte;
		if (pte_none(ptent))
			continue;

		if (pte_present(ptent)) {
			struct page *page;

			page = _vm_normal_page(vma, addr, ptent, true);
			if (unlikely(details) && page) {
				/*
				 * unmap_shared_mapping_pages() wants to
				 * invalidate cache without truncating:
				 * unmap shared but keep private pages.
				 */
				if (details->check_mapping &&
				    details->check_mapping != page_rmapping(page))
					continue;
			}
			ptent = ptep_get_and_clear_full(mm, addr, pte,
							tlb->fullmm);
			tlb_remove_tlb_entry(tlb, pte, addr);
			if (unlikely(!page))
				continue;

			if (!PageAnon(page)) {
				if (pte_dirty(ptent)) {
					force_flush = 1;
					set_page_dirty(page);
				}
				if (pte_young(ptent) &&
				    likely(!(vma->vm_flags & VM_SEQ_READ)))
					mark_page_accessed(page);
			}
			rss[mm_counter(page)]--;
			page_remove_rmap(page, false);
			if (unlikely(page_mapcount(page) < 0))
				print_bad_pte(vma, addr, ptent, page);
			if (unlikely(__tlb_remove_page(tlb, page))) {
				force_flush = 1;
				addr += PAGE_SIZE;
				break;
			}
			continue;
		}

		entry = pte_to_swp_entry(ptent);
		if (non_swap_entry(entry) && is_device_private_entry(entry)) {
			struct page *page = device_private_entry_to_page(entry);

			if (unlikely(details && details->check_mapping)) {
				/*
				 * unmap_shared_mapping_pages() wants to
				 * invalidate cache without truncating:
				 * unmap shared but keep private pages.
				 */
				if (details->check_mapping !=
				    page_rmapping(page))
					continue;
			}

			pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
			rss[mm_counter(page)]--;
			page_remove_rmap(page, false);
			put_page(page);
			continue;
		}

		/* If details->check_mapping, we leave swap entries. */
		if (unlikely(details))
			continue;

		entry = pte_to_swp_entry(ptent);
		if (!non_swap_entry(entry))
			rss[MM_SWAPENTS]--;
		else if (is_migration_entry(entry)) {
			struct page *page;

			page = migration_entry_to_page(entry);
			rss[mm_counter(page)]--;
		}
		if (unlikely(!free_swap_and_cache(entry)))
			print_bad_pte(vma, addr, ptent, NULL);
		pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
	} while (pte++, addr += PAGE_SIZE, addr != end);

	add_mm_rss_vec(mm, rss);
	arch_leave_lazy_mmu_mode();

	/* Do the actual TLB flush before dropping ptl */
	if (force_flush)
		tlb_flush_mmu_tlbonly(tlb);
	pte_unmap_unlock(start_pte, ptl);

	/*
	 * If we forced a TLB flush (either due to running out of
	 * batch buffers or because we needed to flush dirty TLB
	 * entries before releasing the ptl), free the batched
	 * memory too. Restart if we didn't do everything.
	 */
	if (force_flush) {
		force_flush = 0;
		tlb_flush_mmu_free(tlb);
		if (addr != end)
			goto again;
	}

	return addr;
}

static inline unsigned long zap_pmd_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pud_t *pud,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (is_swap_pmd(*pmd) || pmd_trans_huge(*pmd) || pmd_devmap(*pmd)) {
			if (next - addr != HPAGE_PMD_SIZE) {
				VM_BUG_ON_VMA(vma_is_anonymous(vma) &&
				    !rwsem_is_locked(&tlb->mm->mmap_sem), vma);
				__split_huge_pmd(vma, pmd, addr, false, NULL);
			} else if (zap_huge_pmd(tlb, vma, pmd, addr))
				goto next;
			/* fall through */
		}
		/*
		 * Here there can be other concurrent MADV_DONTNEED or
		 * trans huge page faults running, and if the pmd is
		 * none or trans huge it can change under us. This is
		 * because MADV_DONTNEED holds the mmap_sem in read
		 * mode.
		 */
		if (pmd_none_or_trans_huge_or_clear_bad(pmd))
			goto next;
		next = zap_pte_range(tlb, vma, pmd, addr, next, details);
next:
		cond_resched();
	} while (pmd++, addr = next, addr != end);

	return addr;
}

static inline unsigned long zap_pud_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, p4d_t *p4d,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_trans_huge(*pud) || pud_devmap(*pud)) {
			if (next - addr != HPAGE_PUD_SIZE) {
				VM_BUG_ON_VMA(!rwsem_is_locked(&tlb->mm->mmap_sem), vma);
				split_huge_pud(vma, pud, addr);
			} else if (zap_huge_pud(tlb, vma, pud, addr))
				goto next;
			/* fall through */
		}
		if (pud_none_or_clear_bad(pud))
			continue;
		next = zap_pmd_range(tlb, vma, pud, addr, next, details);
next:
		cond_resched();
	} while (pud++, addr = next, addr != end);

	return addr;
}

static inline unsigned long zap_p4d_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	p4d_t *p4d;
	unsigned long next;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		next = zap_pud_range(tlb, vma, p4d, addr, next, details);
	} while (p4d++, addr = next, addr != end);

	return addr;
}

void unmap_page_range(struct mmu_gather *tlb,
			     struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end,
			     struct zap_details *details)
{
	pgd_t *pgd;
	unsigned long next;

	BUG_ON(addr >= end);
	tlb_start_vma(tlb, vma);
	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		next = zap_p4d_range(tlb, vma, pgd, addr, next, details);
	} while (pgd++, addr = next, addr != end);
	tlb_end_vma(tlb, vma);
}


static void unmap_single_vma(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr,
		struct zap_details *details)
{
	unsigned long start = max(vma->vm_start, start_addr);
	unsigned long end;

	if (start >= vma->vm_end)
		return;
	end = min(vma->vm_end, end_addr);
	if (end <= vma->vm_start)
		return;

	if (vma->vm_file)
		uprobe_munmap(vma, start, end);

	if (unlikely(vma->vm_flags & VM_PFNMAP))
		untrack_pfn(vma, 0, 0);

	if (start != end) {
		if (unlikely(is_vm_hugetlb_page(vma))) {
			/*
			 * It is undesirable to test vma->vm_file as it
			 * should be non-null for valid hugetlb area.
			 * However, vm_file will be NULL in the error
			 * cleanup path of mmap_region. When
			 * hugetlbfs ->mmap method fails,
			 * mmap_region() nullifies vma->vm_file
			 * before calling this function to clean up.
			 * Since no pte has actually been setup, it is
			 * safe to do nothing in this case.
			 */
			if (vma->vm_file) {
				i_mmap_lock_write(vma->vm_file->f_mapping);
				__unmap_hugepage_range_final(tlb, vma, start, end, NULL);
				i_mmap_unlock_write(vma->vm_file->f_mapping);
			}
		} else
			unmap_page_range(tlb, vma, start, end, details);
	}
}

/**
 * unmap_vmas - unmap a range of memory covered by a list of vma's
 * @tlb: address of the caller's struct mmu_gather
 * @vma: the starting vma
 * @start_addr: virtual address at which to start unmapping
 * @end_addr: virtual address at which to end unmapping
 *
 * Unmap all pages in the vma list.
 *
 * Only addresses between `start' and `end' will be unmapped.
 *
 * The VMA list must be sorted in ascending virtual address order.
 *
 * unmap_vmas() assumes that the caller will flush the whole unmapped address
 * range after unmap_vmas() returns.  So the only responsibility here is to
 * ensure that any thus-far unmapped pages are flushed before unmap_vmas()
 * drops the lock and schedules.
 */
void unmap_vmas(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr)
{
	struct mm_struct *mm = vma->vm_mm;

	mmu_notifier_invalidate_range_start(mm, start_addr, end_addr);
	for ( ; vma && vma->vm_start < end_addr; vma = vma->vm_next)
		unmap_single_vma(tlb, vma, start_addr, end_addr, NULL);
	mmu_notifier_invalidate_range_end(mm, start_addr, end_addr);
}

/**
 * zap_page_range - remove user pages in a given range
 * @vma: vm_area_struct holding the applicable pages
 * @start: starting address of pages to zap
 * @size: number of bytes to zap
 *
 * Caller must protect the VMA list
 */
void zap_page_range(struct vm_area_struct *vma, unsigned long start,
		unsigned long size)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather tlb;
	unsigned long end = start + size;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, start, end);
	update_hiwater_rss(mm);
	mmu_notifier_invalidate_range_start(mm, start, end);
	for ( ; vma && vma->vm_start < end; vma = vma->vm_next) {
		unmap_single_vma(&tlb, vma, start, end, NULL);

		/*
		 * zap_page_range does not specify whether mmap_sem should be
		 * held for read or write. That allows parallel zap_page_range
		 * operations to unmap a PTE and defer a flush meaning that
		 * this call observes pte_none and fails to flush the TLB.
		 * Rather than adding a complex API, ensure that no stale
		 * TLB entries exist when this call returns.
		 */
		flush_tlb_range(vma, start, end);
	}

	mmu_notifier_invalidate_range_end(mm, start, end);
	tlb_finish_mmu(&tlb, start, end);
}

/**
 * zap_page_range_single - remove user pages in a given range
 * @vma: vm_area_struct holding the applicable pages
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 * @details: details of shared cache invalidation
 *
 * The range must fit into one VMA.
 */
static void zap_page_range_single(struct vm_area_struct *vma, unsigned long address,
		unsigned long size, struct zap_details *details)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather tlb;
	unsigned long end = address + size;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, address, end);
	update_hiwater_rss(mm);
	mmu_notifier_invalidate_range_start(mm, address, end);
	unmap_single_vma(&tlb, vma, address, end, details);
	mmu_notifier_invalidate_range_end(mm, address, end);
	tlb_finish_mmu(&tlb, address, end);
}

/**
 * zap_vma_ptes - remove ptes mapping the vma
 * @vma: vm_area_struct holding ptes to be zapped
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 *
 * This function only unmaps ptes assigned to VM_PFNMAP vmas.
 *
 * The entire address range must be fully contained within the vma.
 *
 * Returns 0 if successful.
 */
int zap_vma_ptes(struct vm_area_struct *vma, unsigned long address,
		unsigned long size)
{
	if (address < vma->vm_start || address + size > vma->vm_end ||
	    		!(vma->vm_flags & VM_PFNMAP))
		return -1;
	zap_page_range_single(vma, address, size, NULL);
	return 0;
}
EXPORT_SYMBOL_GPL(zap_vma_ptes);

pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			spinlock_t **ptl)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;
	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return NULL;
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;

	VM_BUG_ON(pmd_trans_huge(*pmd));
	return pte_alloc_map_lock(mm, pmd, addr, ptl);
}

/*
 * This is the old fallback for page remapping.
 *
 * For historical reasons, it only allows reserved pages. Only
 * old drivers should use this, and they needed to mark their
 * pages reserved for the old functions anyway.
 */
static int insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page, pgprot_t prot)
{
	struct mm_struct *mm = vma->vm_mm;
	int retval;
	pte_t *pte;
	spinlock_t *ptl;

	retval = -EINVAL;
	if (PageAnon(page))
		goto out;
	retval = -ENOMEM;
	flush_dcache_page(page);
	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto out;
	retval = -EBUSY;
	if (!pte_none(*pte))
		goto out_unlock;

	/* Ok, finally just insert the thing.. */
	get_page(page);
	inc_mm_counter_fast(mm, mm_counter_file(page));
	page_add_file_rmap(page, false);
	set_pte_at(mm, addr, pte, mk_pte(page, prot));

	retval = 0;
	pte_unmap_unlock(pte, ptl);
	return retval;
out_unlock:
	pte_unmap_unlock(pte, ptl);
out:
	return retval;
}

/**
 * vm_insert_page - insert single page into user vma
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @page: source kernel page
 *
 * This allows drivers to insert individual pages they've allocated
 * into a user vma.
 *
 * The page has to be a nice clean _individual_ kernel allocation.
 * If you allocate a compound page, you need to have marked it as
 * such (__GFP_COMP), or manually just split the page up yourself
 * (see split_page()).
 *
 * NOTE! Traditionally this was done with "remap_pfn_range()" which
 * took an arbitrary page protection parameter. This doesn't allow
 * that. Your vma protection will have to be set up correctly, which
 * means that if you want a shared writable mapping, you'd better
 * ask for a shared writable mapping!
 *
 * The page does not need to be reserved.
 *
 * Usually this function is called from f_op->mmap() handler
 * under mm->mmap_sem write-lock, so it can change vma->vm_flags.
 * Caller must set VM_MIXEDMAP on vma if it wants to call this
 * function from other places, for example from page-fault handler.
 */
int vm_insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page)
{
	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;
	if (!page_count(page))
		return -EINVAL;
	if (!(vma->vm_flags & VM_MIXEDMAP)) {
		BUG_ON(down_read_trylock(&vma->vm_mm->mmap_sem));
		BUG_ON(vma->vm_flags & VM_PFNMAP);
		vma->vm_flags |= VM_MIXEDMAP;
	}
	return insert_page(vma, addr, page, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_insert_page);

static int insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn, pgprot_t prot, bool mkwrite)
{
	struct mm_struct *mm = vma->vm_mm;
	int retval;
	pte_t *pte, entry;
	spinlock_t *ptl;

	retval = -ENOMEM;
	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto out;
	retval = -EBUSY;
	if (!pte_none(*pte)) {
		if (mkwrite) {
			/*
			 * For read faults on private mappings the PFN passed
			 * in may not match the PFN we have mapped if the
			 * mapped PFN is a writeable COW page.  In the mkwrite
			 * case we are creating a writable PTE for a shared
			 * mapping and we expect the PFNs to match.
			 */
			if (WARN_ON_ONCE(pte_pfn(*pte) != pfn_t_to_pfn(pfn)))
				goto out_unlock;
			entry = *pte;
			goto out_mkwrite;
		} else
			goto out_unlock;
	}

	/* Ok, finally just insert the thing.. */
	if (pfn_t_devmap(pfn))
		entry = pte_mkdevmap(pfn_t_pte(pfn, prot));
	else
		entry = pte_mkspecial(pfn_t_pte(pfn, prot));

out_mkwrite:
	if (mkwrite) {
		entry = pte_mkyoung(entry);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	}

	set_pte_at(mm, addr, pte, entry);
	update_mmu_cache(vma, addr, pte); /* XXX: why not for insert_page? */

	retval = 0;
out_unlock:
	pte_unmap_unlock(pte, ptl);
out:
	return retval;
}

/**
 * vm_insert_pfn - insert single pfn into user vma
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @pfn: source kernel pfn
 *
 * Similar to vm_insert_page, this allows drivers to insert individual pages
 * they've allocated into a user vma. Same comments apply.
 *
 * This function should only be called from a vm_ops->fault handler, and
 * in that case the handler should return NULL.
 *
 * vma cannot be a COW mapping.
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 */
int vm_insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn)
{
	return vm_insert_pfn_prot(vma, addr, pfn, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_insert_pfn);

/**
 * vm_insert_pfn_prot - insert single pfn into user vma with specified pgprot
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @pfn: source kernel pfn
 * @pgprot: pgprot flags for the inserted page
 *
 * This is exactly like vm_insert_pfn, except that it allows drivers to
 * to override pgprot on a per-page basis.
 *
 * This only makes sense for IO mappings, and it makes no sense for
 * cow mappings.  In general, using multiple vmas is preferable;
 * vm_insert_pfn_prot should only be used if using multiple VMAs is
 * impractical.
 */
int vm_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn, pgprot_t pgprot)
{
	int ret;
	/*
	 * Technically, architectures with pte_special can avoid all these
	 * restrictions (same for remap_pfn_range).  However we would like
	 * consistency in testing and feature parity among all, so we should
	 * try to keep these invariants in place for everybody.
	 */
	BUG_ON(!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)));
	BUG_ON((vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) ==
						(VM_PFNMAP|VM_MIXEDMAP));
	BUG_ON((vma->vm_flags & VM_PFNMAP) && is_cow_mapping(vma->vm_flags));
	BUG_ON((vma->vm_flags & VM_MIXEDMAP) && pfn_valid(pfn));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;

	track_pfn_insert(vma, &pgprot, __pfn_to_pfn_t(pfn, PFN_DEV));

	ret = insert_pfn(vma, addr, __pfn_to_pfn_t(pfn, PFN_DEV), pgprot,
			false);

	return ret;
}
EXPORT_SYMBOL(vm_insert_pfn_prot);

static bool vm_mixed_ok(struct vm_area_struct *vma, pfn_t pfn)
{
	/* these checks mirror the abort conditions in vm_normal_page */
	if (vma->vm_flags & VM_MIXEDMAP)
		return true;
	if (pfn_t_devmap(pfn))
		return true;
	if (pfn_t_special(pfn))
		return true;
	if (is_zero_pfn(pfn_t_to_pfn(pfn)))
		return true;
	return false;
}

static int __vm_insert_mixed(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn, bool mkwrite)
{
	pgprot_t pgprot = vma->vm_page_prot;

	BUG_ON(!vm_mixed_ok(vma, pfn));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;

	track_pfn_insert(vma, &pgprot, pfn);

	/*
	 * If we don't have pte special, then we have to use the pfn_valid()
	 * based VM_MIXEDMAP scheme (see vm_normal_page), and thus we *must*
	 * refcount the page if pfn_valid is true (hence insert_page rather
	 * than insert_pfn).  If a zero_pfn were inserted into a VM_MIXEDMAP
	 * without pte special, it would there be refcounted as a normal page.
	 */
	if (!HAVE_PTE_SPECIAL && !pfn_t_devmap(pfn) && pfn_t_valid(pfn)) {
		struct page *page;

		/*
		 * At this point we are committed to insert_page()
		 * regardless of whether the caller specified flags that
		 * result in pfn_t_has_page() == false.
		 */
		page = pfn_to_page(pfn_t_to_pfn(pfn));
		return insert_page(vma, addr, page, pgprot);
	}
	return insert_pfn(vma, addr, pfn, pgprot, mkwrite);
}

int vm_insert_mixed(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn)
{
	return __vm_insert_mixed(vma, addr, pfn, false);

}
EXPORT_SYMBOL(vm_insert_mixed);

int vm_insert_mixed_mkwrite(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn)
{
	return __vm_insert_mixed(vma, addr, pfn, true);
}
EXPORT_SYMBOL(vm_insert_mixed_mkwrite);

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 */
static int remap_pte_range(struct mm_struct *mm, pmd_t *pmd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pte_t *pte;
	spinlock_t *ptl;

	pte = pte_alloc_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return -ENOMEM;
	arch_enter_lazy_mmu_mode();
	do {
		BUG_ON(!pte_none(*pte));
		set_pte_at(mm, addr, pte, pte_mkspecial(pfn_pte(pfn, prot)));
		pfn++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);
	return 0;
}

static inline int remap_pmd_range(struct mm_struct *mm, pud_t *pud,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pmd_t *pmd;
	unsigned long next;

	pfn -= addr >> PAGE_SHIFT;
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	VM_BUG_ON(pmd_trans_huge(*pmd));
	do {
		next = pmd_addr_end(addr, end);
		if (remap_pte_range(mm, pmd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot))
			return -ENOMEM;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int remap_pud_range(struct mm_struct *mm, p4d_t *p4d,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pud_t *pud;
	unsigned long next;

	pfn -= addr >> PAGE_SHIFT;
	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		if (remap_pmd_range(mm, pud, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot))
			return -ENOMEM;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static inline int remap_p4d_range(struct mm_struct *mm, pgd_t *pgd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	p4d_t *p4d;
	unsigned long next;

	pfn -= addr >> PAGE_SHIFT;
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return -ENOMEM;
	do {
		next = p4d_addr_end(addr, end);
		if (remap_pud_range(mm, p4d, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot))
			return -ENOMEM;
	} while (p4d++, addr = next, addr != end);
	return 0;
}

/**
 * remap_pfn_range - remap kernel memory to userspace
 * @vma: user vma to map to
 * @addr: target user address to start at
 * @pfn: physical address of kernel memory
 * @size: size of map area
 * @prot: page protection flags for this mapping
 *
 *  Note: this is only safe if the mm semaphore is held when called.
 */
int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
		    unsigned long pfn, unsigned long size, pgprot_t prot)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long end = addr + PAGE_ALIGN(size);
	struct mm_struct *mm = vma->vm_mm;
	unsigned long remap_pfn = pfn;
	int err;

	/*
	 * Physically remapped pages are special. Tell the
	 * rest of the world about it:
	 *   VM_IO tells people not to look at these pages
	 *	(accesses can have side effects).
	 *   VM_PFNMAP tells the core MM that the base pages are just
	 *	raw PFN mappings, and do not have a "struct page" associated
	 *	with them.
	 *   VM_DONTEXPAND
	 *      Disable vma merging and expanding with mremap().
	 *   VM_DONTDUMP
	 *      Omit vma from core dump, even when VM_IO turned off.
	 *
	 * There's a horrible special case to handle copy-on-write
	 * behaviour that some programs depend on. We mark the "original"
	 * un-COW'ed pages by matching them up with "vma->vm_pgoff".
	 * See vm_normal_page() for details.
	 */
	if (is_cow_mapping(vma->vm_flags)) {
		if (addr != vma->vm_start || end != vma->vm_end)
			return -EINVAL;
		vma->vm_pgoff = pfn;
	}

	err = track_pfn_remap(vma, &prot, remap_pfn, addr, PAGE_ALIGN(size));
	if (err)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;

	BUG_ON(addr >= end);
	pfn -= addr >> PAGE_SHIFT;
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		err = remap_p4d_range(mm, pgd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	if (err)
		untrack_pfn(vma, remap_pfn, PAGE_ALIGN(size));

	return err;
}
EXPORT_SYMBOL(remap_pfn_range);

/**
 * vm_iomap_memory - remap memory to userspace
 * @vma: user vma to map to
 * @start: start of area
 * @len: size of area
 *
 * This is a simplified io_remap_pfn_range() for common driver use. The
 * driver just needs to give us the physical memory range to be mapped,
 * we'll figure out the rest from the vma information.
 *
 * NOTE! Some drivers might want to tweak vma->vm_page_prot first to get
 * whatever write-combining details or similar.
 */
int vm_iomap_memory(struct vm_area_struct *vma, phys_addr_t start, unsigned long len)
{
	unsigned long vm_len, pfn, pages;

	/* Check that the physical memory area passed in looks valid */
	if (start + len < start)
		return -EINVAL;
	/*
	 * You *really* shouldn't map things that aren't page-aligned,
	 * but we've historically allowed it because IO memory might
	 * just have smaller alignment.
	 */
	len += start & ~PAGE_MASK;
	pfn = start >> PAGE_SHIFT;
	pages = (len + ~PAGE_MASK) >> PAGE_SHIFT;
	if (pfn + pages < pfn)
		return -EINVAL;

	/* We start the mapping 'vm_pgoff' pages into the area */
	if (vma->vm_pgoff > pages)
		return -EINVAL;
	pfn += vma->vm_pgoff;
	pages -= vma->vm_pgoff;

	/* Can we fit all of the mapping? */
	vm_len = vma->vm_end - vma->vm_start;
	if (vm_len >> PAGE_SHIFT > pages)
		return -EINVAL;

	/* Ok, let it rip */
	return io_remap_pfn_range(vma, vma->vm_start, pfn, vm_len, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_iomap_memory);

static int apply_to_pte_range(struct mm_struct *mm, pmd_t *pmd,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pte_t *pte;
	int err;
	pgtable_t token;
	spinlock_t *uninitialized_var(ptl);

	pte = (mm == &init_mm) ?
		pte_alloc_kernel(pmd, addr) :
		pte_alloc_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return -ENOMEM;

	BUG_ON(pmd_huge(*pmd));

	arch_enter_lazy_mmu_mode();

	token = pmd_pgtable(*pmd);

	do {
		err = fn(pte++, token, addr, data);
		if (err)
			break;
	} while (addr += PAGE_SIZE, addr != end);

	arch_leave_lazy_mmu_mode();

	if (mm != &init_mm)
		pte_unmap_unlock(pte-1, ptl);
	return err;
}

static int apply_to_pmd_range(struct mm_struct *mm, pud_t *pud,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pmd_t *pmd;
	unsigned long next;
	int err;

	BUG_ON(pud_huge(*pud));

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);
		err = apply_to_pte_range(mm, pmd, addr, next, fn, data);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);
	return err;
}

static int apply_to_pud_range(struct mm_struct *mm, p4d_t *p4d,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pud_t *pud;
	unsigned long next;
	int err;

	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		err = apply_to_pmd_range(mm, pud, addr, next, fn, data);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);
	return err;
}

static int apply_to_p4d_range(struct mm_struct *mm, pgd_t *pgd,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	p4d_t *p4d;
	unsigned long next;
	int err;

	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return -ENOMEM;
	do {
		next = p4d_addr_end(addr, end);
		err = apply_to_pud_range(mm, p4d, addr, next, fn, data);
		if (err)
			break;
	} while (p4d++, addr = next, addr != end);
	return err;
}

/*
 * Scan a region of virtual memory, filling in page tables as necessary
 * and calling a provided function on each leaf page table.
 */
int apply_to_page_range(struct mm_struct *mm, unsigned long addr,
			unsigned long size, pte_fn_t fn, void *data)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long end = addr + size;
	int err;

	if (WARN_ON(addr >= end))
		return -EINVAL;

	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		err = apply_to_p4d_range(mm, pgd, addr, next, fn, data);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	return err;
}
EXPORT_SYMBOL_GPL(apply_to_page_range);

/*
 * handle_pte_fault chooses page fault handler according to an entry which was
 * read non-atomically.  Before making any commitment, on those architectures
 * or configurations (e.g. i386 with PAE) which might give a mix of unmatched
 * parts, do_swap_page must check under lock before unmapping the pte and
 * proceeding (but do_wp_page is only called after already making such a check;
 * and do_anonymous_page can safely check later on).
 */
static inline int pte_unmap_same(struct mm_struct *mm, pmd_t *pmd,
				pte_t *page_table, pte_t orig_pte)
{
	int same = 1;
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
	if (sizeof(pte_t) > sizeof(unsigned long)) {
		spinlock_t *ptl = pte_lockptr(mm, pmd);
		spin_lock(ptl);
		same = pte_same(*page_table, orig_pte);
		spin_unlock(ptl);
	}
#endif
	pte_unmap(page_table);
	return same;
}

static inline void cow_user_page(struct page *dst, struct page *src, unsigned long va, struct vm_area_struct *vma)
{
	debug_dma_assert_idle(src);

	/*
	 * If the source page was a PFN mapping, we don't have
	 * a "struct page" for it. We do a best-effort copy by
	 * just copying from the original user address. If that
	 * fails, we just zero-fill it. Live with it.
	 */
	if (unlikely(!src)) {
		void *kaddr = kmap_atomic(dst);
		void __user *uaddr = (void __user *)(va & PAGE_MASK);

		/*
		 * This really shouldn't fail, because the page is there
		 * in the page tables. But it might just be unreadable,
		 * in which case we just give up and fill the result with
		 * zeroes.
		 */
		if (__copy_from_user_inatomic(kaddr, uaddr, PAGE_SIZE))
			clear_page(kaddr);
		kunmap_atomic(kaddr);
		flush_dcache_page(dst);
	} else
		copy_user_highpage(dst, src, va, vma);
}

static gfp_t __get_fault_gfp_mask(struct vm_area_struct *vma)
{
	struct file *vm_file = vma->vm_file;

	if (vm_file)
		return mapping_gfp_mask(vm_file->f_mapping) | __GFP_FS | __GFP_IO;

	/*
	 * Special mappings (e.g. VDSO) do not have any file so fake
	 * a default GFP_KERNEL for them.
	 */
	return GFP_KERNEL;
}

/*
 * Notify the address space that the page is about to become writable so that
 * it can prohibit this or wait for the page to get into an appropriate state.
 *
 * We do this without the lock held, so that it can sleep if it needs to.
 */
static int do_page_mkwrite(struct vm_fault *vmf)
{
	int ret;
	struct page *page = vmf->page;
	unsigned int old_flags = vmf->flags;

	vmf->flags = FAULT_FLAG_WRITE|FAULT_FLAG_MKWRITE;

	ret = vmf->vma->vm_ops->page_mkwrite(vmf);
	/* Restore original flags so that caller is not surprised */
	vmf->flags = old_flags;
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))
		return ret;
	if (unlikely(!(ret & VM_FAULT_LOCKED))) {
		lock_page(page);
		if (!page->mapping) {
			unlock_page(page);
			return 0; /* retry */
		}
		ret |= VM_FAULT_LOCKED;
	} else
		VM_BUG_ON_PAGE(!PageLocked(page), page);
	return ret;
}

/*
 * Handle dirtying of a page in shared file mapping on a write fault.
 *
 * The function expects the page to be locked and unlocks it.
 */
static void fault_dirty_shared_page(struct vm_area_struct *vma,
				    struct page *page)
{
	struct address_space *mapping;
	bool dirtied;
	bool page_mkwrite = vma->vm_ops && vma->vm_ops->page_mkwrite;

	dirtied = set_page_dirty(page);
	VM_BUG_ON_PAGE(PageAnon(page), page);
	/*
	 * Take a local copy of the address_space - page.mapping may be zeroed
	 * by truncate after unlock_page().   The address_space itself remains
	 * pinned by vma->vm_file's reference.  We rely on unlock_page()'s
	 * release semantics to prevent the compiler from undoing this copying.
	 */
	mapping = page_rmapping(page);
	unlock_page(page);

	if ((dirtied || page_mkwrite) && mapping) {
		/*
		 * Some device drivers do not set page.mapping
		 * but still dirty their pages
		 */
		balance_dirty_pages_ratelimited(mapping);
	}

	if (!page_mkwrite)
		file_update_time(vma->vm_file);
}

/*
 * Handle write page faults for pages that can be reused in the current vma
 *
 * This can happen either due to the mapping being with the VM_SHARED flag,
 * or due to us being the last reference standing to the page. In either
 * case, all we need to do here is to mark the page as writable and update
 * any related book-keeping.
 */
static inline void wp_page_reuse(struct vm_fault *vmf)
	__releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = vmf->page;
	pte_t entry;
	/*
	 * Clear the pages cpupid information as the existing
	 * information potentially belongs to a now completely
	 * unrelated process.
	 */
	if (page)
		page_cpupid_xchg_last(page, (1 << LAST_CPUPID_SHIFT) - 1);

	flush_cache_page(vma, vmf->address, pte_pfn(vmf->orig_pte));
	entry = pte_mkyoung(vmf->orig_pte);
	entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	if (ptep_set_access_flags(vma, vmf->address, vmf->pte, entry, 1))
		update_mmu_cache(vma, vmf->address, vmf->pte);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
}

/*
 * Handle the case of a page which we actually need to copy to a new page.
 *
 * Called with mmap_sem locked and the old page referenced, but
 * without the ptl held.
 *
 * High level logic flow:
 *
 * - Allocate a page, copy the content of the old page to the new one.
 * - Handle book keeping and accounting - cgroups, mmu-notifiers, etc.
 * - Take the PTL. If the pte changed, bail out and release the allocated page
 * - If the pte is still the way we remember it, update the page table and all
 *   relevant references. This includes dropping the reference the page-table
 *   held to the old page, as well as updating the rmap.
 * - In any case, unlock the PTL and drop the reference we took to the old page.
 */
static int wp_page_copy(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *mm = vma->vm_mm;
	struct page *old_page = vmf->page;
	struct page *new_page = NULL;
	pte_t entry;
	int page_copied = 0;
	const unsigned long mmun_start = vmf->address & PAGE_MASK;
	const unsigned long mmun_end = mmun_start + PAGE_SIZE;
	struct mem_cgroup *memcg;

	if (unlikely(anon_vma_prepare(vma)))
		goto oom;

	if (is_zero_pfn(pte_pfn(vmf->orig_pte))) {
		new_page = alloc_zeroed_user_highpage_movable(vma,
							      vmf->address);
		if (!new_page)
			goto oom;
	} else {
		new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma,
				vmf->address);
		if (!new_page)
			goto oom;
		cow_user_page(new_page, old_page, vmf->address, vma);
	}

	if (mem_cgroup_try_charge(new_page, mm, GFP_KERNEL, &memcg, false))
		goto oom_free_new;

	__SetPageUptodate(new_page);

	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	/*
	 * Re-check the pte - we dropped the lock
	 */
	vmf->pte = pte_offset_map_lock(mm, vmf->pmd, vmf->address, &vmf->ptl);
	if (likely(pte_same(*vmf->pte, vmf->orig_pte))) {
		if (old_page) {
			if (!PageAnon(old_page)) {
				dec_mm_counter_fast(mm,
						mm_counter_file(old_page));
				inc_mm_counter_fast(mm, MM_ANONPAGES);
			}
		} else {
			inc_mm_counter_fast(mm, MM_ANONPAGES);
		}
		flush_cache_page(vma, vmf->address, pte_pfn(vmf->orig_pte));
		entry = mk_pte(new_page, vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		/*
		 * Clear the pte entry and flush it first, before updating the
		 * pte with the new entry. This will avoid a race condition
		 * seen in the presence of one thread doing SMC and another
		 * thread doing COW.
		 */
		ptep_clear_flush_notify(vma, vmf->address, vmf->pte);
		page_add_new_anon_rmap(new_page, vma, vmf->address, false);
		mem_cgroup_commit_charge(new_page, memcg, false, false);
		lru_cache_add_active_or_unevictable(new_page, vma);
		/*
		 * We call the notify macro here because, when using secondary
		 * mmu page tables (such as kvm shadow page tables), we want the
		 * new page to be mapped directly into the secondary page table.
		 */
		set_pte_at_notify(mm, vmf->address, vmf->pte, entry);
		update_mmu_cache(vma, vmf->address, vmf->pte);
		if (old_page) {
			/*
			 * Only after switching the pte to the new page may
			 * we remove the mapcount here. Otherwise another
			 * process may come and find the rmap count decremented
			 * before the pte is switched to the new page, and
			 * "reuse" the old page writing into it while our pte
			 * here still points into it and can be read by other
			 * threads.
			 *
			 * The critical issue is to order this
			 * page_remove_rmap with the ptp_clear_flush above.
			 * Those stores are ordered by (if nothing else,)
			 * the barrier present in the atomic_add_negative
			 * in page_remove_rmap.
			 *
			 * Then the TLB flush in ptep_clear_flush ensures that
			 * no process can access the old page before the
			 * decremented mapcount is visible. And the old page
			 * cannot be reused until after the decremented
			 * mapcount is visible. So transitively, TLBs to
			 * old page will be flushed before it can be reused.
			 */
			page_remove_rmap(old_page, false);
		}

		/* Free the old page.. */
		new_page = old_page;
		page_copied = 1;
	} else {
		mem_cgroup_cancel_charge(new_page, memcg, false);
	}

	if (new_page)
		put_page(new_page);

	pte_unmap_unlock(vmf->pte, vmf->ptl);
	/*
	 * No need to double call mmu_notifier->invalidate_range() callback as
	 * the above ptep_clear_flush_notify() did already call it.
	 */
	mmu_notifier_invalidate_range_only_end(mm, mmun_start, mmun_end);
	if (old_page) {
		/*
		 * Don't let another task, with possibly unlocked vma,
		 * keep the mlocked page.
		 */
		if (page_copied && (vma->vm_flags & VM_LOCKED)) {
			lock_page(old_page);	/* LRU manipulation */
			if (PageMlocked(old_page))
				munlock_vma_page(old_page);
			unlock_page(old_page);
		}
		put_page(old_page);
	}
	return page_copied ? VM_FAULT_WRITE : 0;
oom_free_new:
	put_page(new_page);
oom:
	if (old_page)
		put_page(old_page);
	return VM_FAULT_OOM;
}

/**
 * finish_mkwrite_fault - finish page fault for a shared mapping, making PTE
 *			  writeable once the page is prepared
 *
 * @vmf: structure describing the fault
 *
 * This function handles all that is needed to finish a write page fault in a
 * shared mapping due to PTE being read-only once the mapped page is prepared.
 * It handles locking of PTE and modifying it. The function returns
 * VM_FAULT_WRITE on success, 0 when PTE got changed before we acquired PTE
 * lock.
 *
 * The function expects the page to be locked or other protection against
 * concurrent faults / writeback (such as DAX radix tree locks).
 */
int finish_mkwrite_fault(struct vm_fault *vmf)
{
	WARN_ON_ONCE(!(vmf->vma->vm_flags & VM_SHARED));
	vmf->pte = pte_offset_map_lock(vmf->vma->vm_mm, vmf->pmd, vmf->address,
				       &vmf->ptl);
	/*
	 * We might have raced with another page fault while we released the
	 * pte_offset_map_lock.
	 */
	if (!pte_same(*vmf->pte, vmf->orig_pte)) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return VM_FAULT_NOPAGE;
	}
	wp_page_reuse(vmf);
	return 0;
}

/*
 * Handle write page faults for VM_MIXEDMAP or VM_PFNMAP for a VM_SHARED
 * mapping
 */
static int wp_pfn_shared(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;

	if (vma->vm_ops && vma->vm_ops->pfn_mkwrite) {
		int ret;

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		vmf->flags |= FAULT_FLAG_MKWRITE;
		ret = vma->vm_ops->pfn_mkwrite(vmf);
		if (ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE))
			return ret;
		return finish_mkwrite_fault(vmf);
	}
	wp_page_reuse(vmf);
	return VM_FAULT_WRITE;
}

static int wp_page_shared(struct vm_fault *vmf)
	__releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;

	get_page(vmf->page);

	if (vma->vm_ops && vma->vm_ops->page_mkwrite) {
		int tmp;

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		tmp = do_page_mkwrite(vmf);
		if (unlikely(!tmp || (tmp &
				      (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
			put_page(vmf->page);
			return tmp;
		}
		tmp = finish_mkwrite_fault(vmf);
		if (unlikely(tmp & (VM_FAULT_ERROR | VM_FAULT_NOPAGE))) {
			unlock_page(vmf->page);
			put_page(vmf->page);
			return tmp;
		}
	} else {
		wp_page_reuse(vmf);
		lock_page(vmf->page);
	}
	fault_dirty_shared_page(vma, vmf->page);
	put_page(vmf->page);

	return VM_FAULT_WRITE;
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * Note that this routine assumes that the protection checks have been
 * done by the caller (the low-level page fault routine in most cases).
 * Thus we can safely just mark it writable once we've done any necessary
 * COW.
 *
 * We also mark the page dirty at this point even though the page will
 * change only once the write actually happens. This avoids a few races,
 * and potentially makes it more efficient.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), with pte both mapped and locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_wp_page(struct vm_fault *vmf)
	__releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;

	vmf->page = vm_normal_page(vma, vmf->address, vmf->orig_pte);
	if (!vmf->page) {
		/*
		 * VM_MIXEDMAP !pfn_valid() case, or VM_SOFTDIRTY clear on a
		 * VM_PFNMAP VMA.
		 *
		 * We should not cow pages in a shared writeable mapping.
		 * Just mark the pages writable and/or call ops->pfn_mkwrite.
		 */
		if ((vma->vm_flags & (VM_WRITE|VM_SHARED)) ==
				     (VM_WRITE|VM_SHARED))
			return wp_pfn_shared(vmf);

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return wp_page_copy(vmf);
	}

	/*
	 * Take out anonymous pages first, anonymous shared vmas are
	 * not dirty accountable.
	 */
	if (PageAnon(vmf->page) && !PageKsm(vmf->page)) {
		int total_map_swapcount;
		if (!trylock_page(vmf->page)) {
			get_page(vmf->page);
			pte_unmap_unlock(vmf->pte, vmf->ptl);
			lock_page(vmf->page);
			vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
					vmf->address, &vmf->ptl);
			if (!pte_same(*vmf->pte, vmf->orig_pte)) {
				unlock_page(vmf->page);
				pte_unmap_unlock(vmf->pte, vmf->ptl);
				put_page(vmf->page);
				return 0;
			}
			put_page(vmf->page);
		}
		if (reuse_swap_page(vmf->page, &total_map_swapcount)) {
			if (total_map_swapcount == 1) {
				/*
				 * The page is all ours. Move it to
				 * our anon_vma so the rmap code will
				 * not search our parent or siblings.
				 * Protected against the rmap code by
				 * the page lock.
				 */
				page_move_anon_rmap(vmf->page, vma);
			}
			unlock_page(vmf->page);
			wp_page_reuse(vmf);
			return VM_FAULT_WRITE;
		}
		unlock_page(vmf->page);
	} else if (unlikely((vma->vm_flags & (VM_WRITE|VM_SHARED)) ==
					(VM_WRITE|VM_SHARED))) {
		return wp_page_shared(vmf);
	}

	/*
	 * Ok, we need to copy. Oh, well..
	 */
	get_page(vmf->page);

	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return wp_page_copy(vmf);
}

static void unmap_mapping_range_vma(struct vm_area_struct *vma,
		unsigned long start_addr, unsigned long end_addr,
		struct zap_details *details)
{
	zap_page_range_single(vma, start_addr, end_addr - start_addr, details);
}

static inline void unmap_mapping_range_tree(struct rb_root_cached *root,
					    struct zap_details *details)
{
	struct vm_area_struct *vma;
	pgoff_t vba, vea, zba, zea;

	vma_interval_tree_foreach(vma, root,
			details->first_index, details->last_index) {

		vba = vma->vm_pgoff;
		vea = vba + vma_pages(vma) - 1;
		zba = details->first_index;
		if (zba < vba)
			zba = vba;
		zea = details->last_index;
		if (zea > vea)
			zea = vea;

		unmap_mapping_range_vma(vma,
			((zba - vba) << PAGE_SHIFT) + vma->vm_start,
			((zea - vba + 1) << PAGE_SHIFT) + vma->vm_start,
				details);
	}
}

/**
 * unmap_mapping_pages() - Unmap pages from processes.
 * @mapping: The address space containing pages to be unmapped.
 * @start: Index of first page to be unmapped.
 * @nr: Number of pages to be unmapped.  0 to unmap to end of file.
 * @even_cows: Whether to unmap even private COWed pages.
 *
 * Unmap the pages in this address space from any userspace process which
 * has them mmaped.  Generally, you want to remove COWed pages as well when
 * a file is being truncated, but not when invalidating pages from the page
 * cache.
 */
void unmap_mapping_pages(struct address_space *mapping, pgoff_t start,
		pgoff_t nr, bool even_cows)
{
	struct zap_details details = { };

	details.check_mapping = even_cows ? NULL : mapping;
	details.first_index = start;
	details.last_index = start + nr - 1;
	if (details.last_index < details.first_index)
		details.last_index = ULONG_MAX;

	i_mmap_lock_write(mapping);
	if (unlikely(!RB_EMPTY_ROOT(&mapping->i_mmap.rb_root)))
		unmap_mapping_range_tree(&mapping->i_mmap, &details);
	i_mmap_unlock_write(mapping);
}

/**
 * unmap_mapping_range - unmap the portion of all mmaps in the specified
 * address_space corresponding to the specified byte range in the underlying
 * file.
 *
 * @mapping: the address space containing mmaps to be unmapped.
 * @holebegin: byte in first page to unmap, relative to the start of
 * the underlying file.  This will be rounded down to a PAGE_SIZE
 * boundary.  Note that this is different from truncate_pagecache(), which
 * must keep the partial page.  In contrast, we must get rid of
 * partial pages.
 * @holelen: size of prospective hole in bytes.  This will be rounded
 * up to a PAGE_SIZE boundary.  A holelen of zero truncates to the
 * end of the file.
 * @even_cows: 1 when truncating a file, unmap even private COWed pages;
 * but 0 when invalidating pagecache, don't throw away private data.
 */
void unmap_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen, int even_cows)
{
	pgoff_t hba = holebegin >> PAGE_SHIFT;
	pgoff_t hlen = (holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/* Check for overflow. */
	if (sizeof(holelen) > sizeof(hlen)) {
		long long holeend =
			(holebegin + holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;
		if (holeend & ~(long long)ULONG_MAX)
			hlen = ULONG_MAX - hba + 1;
	}

	unmap_mapping_pages(mapping, hba, hlen, even_cows);
}
EXPORT_SYMBOL(unmap_mapping_range);

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with pte unmapped and unlocked.
 *
 * We return with the mmap_sem locked or unlocked in the same cases
 * as does filemap_fault().
 */
int do_swap_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = NULL, *swapcache;
	struct mem_cgroup *memcg;
	swp_entry_t entry;
	pte_t pte;
	int locked;
	int exclusive = 0;
	int ret = 0;

	if (!pte_unmap_same(vma->vm_mm, vmf->pmd, vmf->pte, vmf->orig_pte))
		goto out;

	entry = pte_to_swp_entry(vmf->orig_pte);
	if (unlikely(non_swap_entry(entry))) {
		if (is_migration_entry(entry)) {
			migration_entry_wait(vma->vm_mm, vmf->pmd,
					     vmf->address);
		} else if (is_device_private_entry(entry)) {
			/*
			 * For un-addressable device memory we call the pgmap
			 * fault handler callback. The callback must migrate
			 * the page back to some CPU accessible page.
			 */
			ret = device_private_entry_fault(vma, vmf->address, entry,
						 vmf->flags, vmf->pmd);
		} else if (is_hwpoison_entry(entry)) {
			ret = VM_FAULT_HWPOISON;
		} else {
			print_bad_pte(vma, vmf->address, vmf->orig_pte, NULL);
			ret = VM_FAULT_SIGBUS;
		}
		goto out;
	}


	delayacct_set_flag(DELAYACCT_PF_SWAPIN);
	page = lookup_swap_cache(entry, vma, vmf->address);
	swapcache = page;

	if (!page) {
		struct swap_info_struct *si = swp_swap_info(entry);

		if (si->flags & SWP_SYNCHRONOUS_IO &&
				__swap_count(si, entry) == 1) {
			/* skip swapcache */
			page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma,
							vmf->address);
			if (page) {
				__SetPageLocked(page);
				__SetPageSwapBacked(page);
				set_page_private(page, entry.val);
				lru_cache_add_anon(page);
				swap_readpage(page, true);
			}
		} else {
			page = swapin_readahead(entry, GFP_HIGHUSER_MOVABLE,
						vmf);
			swapcache = page;
		}

		if (!page) {
			/*
			 * Back out if somebody else faulted in this pte
			 * while we released the pte lock.
			 */
			vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
					vmf->address, &vmf->ptl);
			if (likely(pte_same(*vmf->pte, vmf->orig_pte)))
				ret = VM_FAULT_OOM;
			delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
			goto unlock;
		}

		/* Had to read the page from swap area: Major fault */
		ret = VM_FAULT_MAJOR;
		count_vm_event(PGMAJFAULT);
		count_memcg_event_mm(vma->vm_mm, PGMAJFAULT);
	} else if (PageHWPoison(page)) {
		/*
		 * hwpoisoned dirty swapcache pages are kept for killing
		 * owner processes (which may be unknown at hwpoison time)
		 */
		ret = VM_FAULT_HWPOISON;
		delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
		goto out_release;
	}

	locked = lock_page_or_retry(page, vma->vm_mm, vmf->flags);

	delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
	if (!locked) {
		ret |= VM_FAULT_RETRY;
		goto out_release;
	}

	/*
	 * Make sure try_to_free_swap or reuse_swap_page or swapoff did not
	 * release the swapcache from under us.  The page pin, and pte_same
	 * test below, are not enough to exclude that.  Even if it is still
	 * swapcache, we need to check that the page's swap has not changed.
	 */
	if (unlikely((!PageSwapCache(page) ||
			page_private(page) != entry.val)) && swapcache)
		goto out_page;

	page = ksm_might_need_to_copy(page, vma, vmf->address);
	if (unlikely(!page)) {
		ret = VM_FAULT_OOM;
		page = swapcache;
		goto out_page;
	}

	if (mem_cgroup_try_charge(page, vma->vm_mm, GFP_KERNEL,
				&memcg, false)) {
		ret = VM_FAULT_OOM;
		goto out_page;
	}

	/*
	 * Back out if somebody else already faulted in this pte.
	 */
	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
			&vmf->ptl);
	if (unlikely(!pte_same(*vmf->pte, vmf->orig_pte)))
		goto out_nomap;

	if (unlikely(!PageUptodate(page))) {
		ret = VM_FAULT_SIGBUS;
		goto out_nomap;
	}

	/*
	 * The page isn't present yet, go ahead with the fault.
	 *
	 * Be careful about the sequence of operations here.
	 * To get its accounting right, reuse_swap_page() must be called
	 * while the page is counted on swap but not yet in mapcount i.e.
	 * before page_add_anon_rmap() and swap_free(); try_to_free_swap()
	 * must be called after the swap_free(), or it will never succeed.
	 */

	inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES);
	dec_mm_counter_fast(vma->vm_mm, MM_SWAPENTS);
	pte = mk_pte(page, vma->vm_page_prot);
	if ((vmf->flags & FAULT_FLAG_WRITE) && reuse_swap_page(page, NULL)) {
		pte = maybe_mkwrite(pte_mkdirty(pte), vma);
		vmf->flags &= ~FAULT_FLAG_WRITE;
		ret |= VM_FAULT_WRITE;
		exclusive = RMAP_EXCLUSIVE;
	}
	flush_icache_page(vma, page);
	if (pte_swp_soft_dirty(vmf->orig_pte))
		pte = pte_mksoft_dirty(pte);
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, pte);
	arch_do_swap_page(vma->vm_mm, vma, vmf->address, pte, vmf->orig_pte);
	vmf->orig_pte = pte;

	/* ksm created a completely new copy */
	if (unlikely(page != swapcache && swapcache)) {
		page_add_new_anon_rmap(page, vma, vmf->address, false);
		mem_cgroup_commit_charge(page, memcg, false, false);
		lru_cache_add_active_or_unevictable(page, vma);
	} else {
		do_page_add_anon_rmap(page, vma, vmf->address, exclusive);
		mem_cgroup_commit_charge(page, memcg, true, false);
		activate_page(page);
	}

	swap_free(entry);
	if (mem_cgroup_swap_full(page) ||
	    (vma->vm_flags & VM_LOCKED) || PageMlocked(page))
		try_to_free_swap(page);
	unlock_page(page);
	if (page != swapcache && swapcache) {
		/*
		 * Hold the lock to avoid the swap entry to be reused
		 * until we take the PT lock for the pte_same() check
		 * (to avoid false positives from pte_same). For
		 * further safety release the lock after the swap_free
		 * so that the swap count won't change under a
		 * parallel locked swapcache.
		 */
		unlock_page(swapcache);
		put_page(swapcache);
	}

	if (vmf->flags & FAULT_FLAG_WRITE) {
		ret |= do_wp_page(vmf);
		if (ret & VM_FAULT_ERROR)
			ret &= VM_FAULT_ERROR;
		goto out;
	}

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, vmf->address, vmf->pte);
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
out:
	return ret;
out_nomap:
	mem_cgroup_cancel_charge(page, memcg, false);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
out_page:
	unlock_page(page);
out_release:
	put_page(page);
	if (page != swapcache && swapcache) {
		unlock_page(swapcache);
		put_page(swapcache);
	}
	return ret;
}

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_anonymous_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mem_cgroup *memcg;
	struct page *page;
	int ret = 0;
	pte_t entry;

	/* File mapping without ->vm_ops ? */
	if (vma->vm_flags & VM_SHARED)
		return VM_FAULT_SIGBUS;

	/*
	 * Use pte_alloc() instead of pte_alloc_map().  We can't run
	 * pte_offset_map() on pmds where a huge pmd might be created
	 * from a different thread.
	 *
	 * pte_alloc_map() is safe to use under down_write(mmap_sem) or when
	 * parallel threads are excluded by other means.
	 *
	 * Here we only have down_read(mmap_sem).
	 */
	if (pte_alloc(vma->vm_mm, vmf->pmd, vmf->address))
		return VM_FAULT_OOM;

	/* See the comment in pte_alloc_one_map() */
	if (unlikely(pmd_trans_unstable(vmf->pmd)))
		return 0;

	/* Use the zero-page for reads */
	if (!(vmf->flags & FAULT_FLAG_WRITE) &&
			!mm_forbids_zeropage(vma->vm_mm)) {
		entry = pte_mkspecial(pfn_pte(my_zero_pfn(vmf->address),
						vma->vm_page_prot));
		vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
				vmf->address, &vmf->ptl);
		if (!pte_none(*vmf->pte))
			goto unlock;
		ret = check_stable_address_space(vma->vm_mm);
		if (ret)
			goto unlock;
		/* Deliver the page fault to userland, check inside PT lock */
		if (userfaultfd_missing(vma)) {
			pte_unmap_unlock(vmf->pte, vmf->ptl);
			return handle_userfault(vmf, VM_UFFD_MISSING);
		}
		goto setpte;
	}

	/* Allocate our own private page. */
	if (unlikely(anon_vma_prepare(vma)))
		goto oom;
	page = alloc_zeroed_user_highpage_movable(vma, vmf->address);
	if (!page)
		goto oom;

	if (mem_cgroup_try_charge(page, vma->vm_mm, GFP_KERNEL, &memcg, false))
		goto oom_free_page;

	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * preceeding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__SetPageUptodate(page);

	entry = mk_pte(page, vma->vm_page_prot);
	if (vma->vm_flags & VM_WRITE)
		entry = pte_mkwrite(pte_mkdirty(entry));

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
			&vmf->ptl);
	if (!pte_none(*vmf->pte))
		goto release;

	ret = check_stable_address_space(vma->vm_mm);
	if (ret)
		goto release;

	/* Deliver the page fault to userland, check inside PT lock */
	if (userfaultfd_missing(vma)) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		mem_cgroup_cancel_charge(page, memcg, false);
		put_page(page);
		return handle_userfault(vmf, VM_UFFD_MISSING);
	}

	inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES);
	page_add_new_anon_rmap(page, vma, vmf->address, false);
	mem_cgroup_commit_charge(page, memcg, false, false);
	lru_cache_add_active_or_unevictable(page, vma);
setpte:
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, vmf->address, vmf->pte);
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
release:
	mem_cgroup_cancel_charge(page, memcg, false);
	put_page(page);
	goto unlock;
oom_free_page:
	put_page(page);
oom:
	return VM_FAULT_OOM;
}

/*
 * The mmap_sem must have been held on entry, and may have been
 * released depending on flags and vma->vm_ops->fault() return value.
 * See filemap_fault() and __lock_page_retry().
 */
static int __do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	int ret;

	ret = vma->vm_ops->fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY |
			    VM_FAULT_DONE_COW)))
		return ret;

	if (unlikely(PageHWPoison(vmf->page))) {
		if (ret & VM_FAULT_LOCKED)
			unlock_page(vmf->page);
		put_page(vmf->page);
		vmf->page = NULL;
		return VM_FAULT_HWPOISON;
	}

	if (unlikely(!(ret & VM_FAULT_LOCKED)))
		lock_page(vmf->page);
	else
		VM_BUG_ON_PAGE(!PageLocked(vmf->page), vmf->page);

	return ret;
}

/*
 * The ordering of these checks is important for pmds with _PAGE_DEVMAP set.
 * If we check pmd_trans_unstable() first we will trip the bad_pmd() check
 * inside of pmd_none_or_trans_huge_or_clear_bad(). This will end up correctly
 * returning 1 but not before it spams dmesg with the pmd_clear_bad() output.
 */
static int pmd_devmap_trans_unstable(pmd_t *pmd)
{
	return pmd_devmap(*pmd) || pmd_trans_unstable(pmd);
}

static int pte_alloc_one_map(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;

	if (!pmd_none(*vmf->pmd))
		goto map_pte;
	if (vmf->prealloc_pte) {
		vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
		if (unlikely(!pmd_none(*vmf->pmd))) {
			spin_unlock(vmf->ptl);
			goto map_pte;
		}

		mm_inc_nr_ptes(vma->vm_mm);
		pmd_populate(vma->vm_mm, vmf->pmd, vmf->prealloc_pte);
		spin_unlock(vmf->ptl);
		vmf->prealloc_pte = NULL;
	} else if (unlikely(pte_alloc(vma->vm_mm, vmf->pmd, vmf->address))) {
		return VM_FAULT_OOM;
	}
map_pte:
	/*
	 * If a huge pmd materialized under us just retry later.  Use
	 * pmd_trans_unstable() via pmd_devmap_trans_unstable() instead of
	 * pmd_trans_huge() to ensure the pmd didn't become pmd_trans_huge
	 * under us and then back to pmd_none, as a result of MADV_DONTNEED
	 * running immediately after a huge pmd fault in a different thread of
	 * this mm, in turn leading to a misleading pmd_trans_huge() retval.
	 * All we have to ensure is that it is a regular pmd that we can walk
	 * with pte_offset_map() and we can do that through an atomic read in
	 * C, which is what pmd_trans_unstable() provides.
	 */
	if (pmd_devmap_trans_unstable(vmf->pmd))
		return VM_FAULT_NOPAGE;

	/*
	 * At this point we know that our vmf->pmd points to a page of ptes
	 * and it cannot become pmd_none(), pmd_devmap() or pmd_trans_huge()
	 * for the duration of the fault.  If a racing MADV_DONTNEED runs and
	 * we zap the ptes pointed to by our vmf->pmd, the vmf->ptl will still
	 * be valid and we will re-check to make sure the vmf->pte isn't
	 * pte_none() under vmf->ptl protection when we return to
	 * alloc_set_pte().
	 */
	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
			&vmf->ptl);
	return 0;
}

#ifdef CONFIG_TRANSPARENT_HUGE_PAGECACHE

#define HPAGE_CACHE_INDEX_MASK (HPAGE_PMD_NR - 1)
static inline bool transhuge_vma_suitable(struct vm_area_struct *vma,
		unsigned long haddr)
{
	if (((vma->vm_start >> PAGE_SHIFT) & HPAGE_CACHE_INDEX_MASK) !=
			(vma->vm_pgoff & HPAGE_CACHE_INDEX_MASK))
		return false;
	if (haddr < vma->vm_start || haddr + HPAGE_PMD_SIZE > vma->vm_end)
		return false;
	return true;
}

static void deposit_prealloc_pte(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;

	pgtable_trans_huge_deposit(vma->vm_mm, vmf->pmd, vmf->prealloc_pte);
	/*
	 * We are going to consume the prealloc table,
	 * count that as nr_ptes.
	 */
	mm_inc_nr_ptes(vma->vm_mm);
	vmf->prealloc_pte = NULL;
}

static int do_set_pmd(struct vm_fault *vmf, struct page *page)
{
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	pmd_t entry;
	int i, ret;

	if (!transhuge_vma_suitable(vma, haddr))
		return VM_FAULT_FALLBACK;

	ret = VM_FAULT_FALLBACK;
	page = compound_head(page);

	/*
	 * Archs like ppc64 need additonal space to store information
	 * related to pte entry. Use the preallocated table for that.
	 */
	if (arch_needs_pgtable_deposit() && !vmf->prealloc_pte) {
		vmf->prealloc_pte = pte_alloc_one(vma->vm_mm, vmf->address);
		if (!vmf->prealloc_pte)
			return VM_FAULT_OOM;
		smp_wmb(); /* See comment in __pte_alloc() */
	}

	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_none(*vmf->pmd)))
		goto out;

	for (i = 0; i < HPAGE_PMD_NR; i++)
		flush_icache_page(vma, page + i);

	entry = mk_huge_pmd(page, vma->vm_page_prot);
	if (write)
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);

	add_mm_counter(vma->vm_mm, MM_FILEPAGES, HPAGE_PMD_NR);
	page_add_file_rmap(page, true);
	/*
	 * deposit and withdraw with pmd lock held
	 */
	if (arch_needs_pgtable_deposit())
		deposit_prealloc_pte(vmf);

	set_pmd_at(vma->vm_mm, haddr, vmf->pmd, entry);

	update_mmu_cache_pmd(vma, haddr, vmf->pmd);

	/* fault is handled */
	ret = 0;
	count_vm_event(THP_FILE_MAPPED);
out:
	spin_unlock(vmf->ptl);
	return ret;
}
#else
static int do_set_pmd(struct vm_fault *vmf, struct page *page)
{
	BUILD_BUG();
	return 0;
}
#endif

/**
 * alloc_set_pte - setup new PTE entry for given page and add reverse page
 * mapping. If needed, the fucntion allocates page table or use pre-allocated.
 *
 * @vmf: fault environment
 * @memcg: memcg to charge page (only for private mappings)
 * @page: page to map
 *
 * Caller must take care of unlocking vmf->ptl, if vmf->pte is non-NULL on
 * return.
 *
 * Target users are page handler itself and implementations of
 * vm_ops->map_pages.
 */
int alloc_set_pte(struct vm_fault *vmf, struct mem_cgroup *memcg,
		struct page *page)
{
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	pte_t entry;
	int ret;

	if (pmd_none(*vmf->pmd) && PageTransCompound(page) &&
			IS_ENABLED(CONFIG_TRANSPARENT_HUGE_PAGECACHE)) {
		/* THP on COW? */
		VM_BUG_ON_PAGE(memcg, page);

		ret = do_set_pmd(vmf, page);
		if (ret != VM_FAULT_FALLBACK)
			return ret;
	}

	if (!vmf->pte) {
		ret = pte_alloc_one_map(vmf);
		if (ret)
			return ret;
	}

	/* Re-check under ptl */
	if (unlikely(!pte_none(*vmf->pte)))
		return VM_FAULT_NOPAGE;

	flush_icache_page(vma, page);
	entry = mk_pte(page, vma->vm_page_prot);
	if (write)
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	/* copy-on-write page */
	if (write && !(vma->vm_flags & VM_SHARED)) {
		inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES);
		page_add_new_anon_rmap(page, vma, vmf->address, false);
		mem_cgroup_commit_charge(page, memcg, false, false);
		lru_cache_add_active_or_unevictable(page, vma);
	} else {
		inc_mm_counter_fast(vma->vm_mm, mm_counter_file(page));
		page_add_file_rmap(page, false);
	}
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);

	/* no need to invalidate: a not-present page won't be cached */
	update_mmu_cache(vma, vmf->address, vmf->pte);

	return 0;
}


/**
 * finish_fault - finish page fault once we have prepared the page to fault
 *
 * @vmf: structure describing the fault
 *
 * This function handles all that is needed to finish a page fault once the
 * page to fault in is prepared. It handles locking of PTEs, inserts PTE for
 * given page, adds reverse page mapping, handles memcg charges and LRU
 * addition. The function returns 0 on success, VM_FAULT_ code in case of
 * error.
 *
 * The function expects the page to be locked and on success it consumes a
 * reference of a page being mapped (for the PTE which maps it).
 */
int finish_fault(struct vm_fault *vmf)
{
	struct page *page;
	int ret = 0;

	/* Did we COW the page? */
	if ((vmf->flags & FAULT_FLAG_WRITE) &&
	    !(vmf->vma->vm_flags & VM_SHARED))
		page = vmf->cow_page;
	else
		page = vmf->page;

	/*
	 * check even for read faults because we might have lost our CoWed
	 * page
	 */
	if (!(vmf->vma->vm_flags & VM_SHARED))
		ret = check_stable_address_space(vmf->vma->vm_mm);
	if (!ret)
		ret = alloc_set_pte(vmf, vmf->memcg, page);
	if (vmf->pte)
		pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
}

static unsigned long fault_around_bytes __read_mostly =
	rounddown_pow_of_two(65536);

#ifdef CONFIG_DEBUG_FS
static int fault_around_bytes_get(void *data, u64 *val)
{
	*val = fault_around_bytes;
	return 0;
}

/*
 * fault_around_bytes must be rounded down to the nearest page order as it's
 * what do_fault_around() expects to see.
 */
static int fault_around_bytes_set(void *data, u64 val)
{
	if (val / PAGE_SIZE > PTRS_PER_PTE)
		return -EINVAL;
	if (val > PAGE_SIZE)
		fault_around_bytes = rounddown_pow_of_two(val);
	else
		fault_around_bytes = PAGE_SIZE; /* rounddown_pow_of_two(0) is undefined */
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fault_around_bytes_fops,
		fault_around_bytes_get, fault_around_bytes_set, "%llu\n");

static int __init fault_around_debugfs(void)
{
	void *ret;

	ret = debugfs_create_file_unsafe("fault_around_bytes", 0644, NULL, NULL,
			&fault_around_bytes_fops);
	if (!ret)
		pr_warn("Failed to create fault_around_bytes in debugfs");
	return 0;
}
late_initcall(fault_around_debugfs);
#endif

/*
 * do_fault_around() tries to map few pages around the fault address. The hope
 * is that the pages will be needed soon and this will lower the number of
 * faults to handle.
 *
 * It uses vm_ops->map_pages() to map the pages, which skips the page if it's
 * not ready to be mapped: not up-to-date, locked, etc.
 *
 * This function is called with the page table lock taken. In the split ptlock
 * case the page table lock only protects only those entries which belong to
 * the page table corresponding to the fault address.
 *
 * This function doesn't cross the VMA boundaries, in order to call map_pages()
 * only once.
 *
 * fault_around_bytes defines how many bytes we'll try to map.
 * do_fault_around() expects it to be set to a power of two less than or equal
 * to PTRS_PER_PTE.
 *
 * The virtual address of the area that we map is naturally aligned to
 * fault_around_bytes rounded down to the machine page size
 * (and therefore to page order).  This way it's easier to guarantee
 * that we don't cross page table boundaries.
 */
static int do_fault_around(struct vm_fault *vmf)
{
	unsigned long address = vmf->address, nr_pages, mask;
	pgoff_t start_pgoff = vmf->pgoff;
	pgoff_t end_pgoff;
	int off, ret = 0;

	nr_pages = READ_ONCE(fault_around_bytes) >> PAGE_SHIFT;
	mask = ~(nr_pages * PAGE_SIZE - 1) & PAGE_MASK;

	vmf->address = max(address & mask, vmf->vma->vm_start);
	off = ((address - vmf->address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	start_pgoff -= off;

	/*
	 *  end_pgoff is either the end of the page table, the end of
	 *  the vma or nr_pages from start_pgoff, depending what is nearest.
	 */
	end_pgoff = start_pgoff -
		((vmf->address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)) +
		PTRS_PER_PTE - 1;
	end_pgoff = min3(end_pgoff, vma_pages(vmf->vma) + vmf->vma->vm_pgoff - 1,
			start_pgoff + nr_pages - 1);

	if (pmd_none(*vmf->pmd)) {
		vmf->prealloc_pte = pte_alloc_one(vmf->vma->vm_mm,
						  vmf->address);
		if (!vmf->prealloc_pte)
			goto out;
		smp_wmb(); /* See comment in __pte_alloc() */
	}

	vmf->vma->vm_ops->map_pages(vmf, start_pgoff, end_pgoff);

	/* Huge page is mapped? Page fault is solved */
	if (pmd_trans_huge(*vmf->pmd)) {
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	/* ->map_pages() haven't done anything useful. Cold page cache? */
	if (!vmf->pte)
		goto out;

	/* check if the page fault is solved */
	vmf->pte -= (vmf->address >> PAGE_SHIFT) - (address >> PAGE_SHIFT);
	if (!pte_none(*vmf->pte))
		ret = VM_FAULT_NOPAGE;
	pte_unmap_unlock(vmf->pte, vmf->ptl);
out:
	vmf->address = address;
	vmf->pte = NULL;
	return ret;
}

static int do_read_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	int ret = 0;

	/*
	 * Let's call ->map_pages() first and use ->fault() as fallback
	 * if page by the offset is not ready to be mapped (cold cache or
	 * something).
	 */
	if (vma->vm_ops->map_pages && fault_around_bytes >> PAGE_SHIFT > 1) {
		ret = do_fault_around(vmf);
		if (ret)
			return ret;
	}

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	ret |= finish_fault(vmf);
	unlock_page(vmf->page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		put_page(vmf->page);
	return ret;
}

static int do_cow_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	int ret;

	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;

	vmf->cow_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vmf->address);
	if (!vmf->cow_page)
		return VM_FAULT_OOM;

	if (mem_cgroup_try_charge(vmf->cow_page, vma->vm_mm, GFP_KERNEL,
				&vmf->memcg, false)) {
		put_page(vmf->cow_page);
		return VM_FAULT_OOM;
	}

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	if (ret & VM_FAULT_DONE_COW)
		return ret;

	copy_user_highpage(vmf->cow_page, vmf->page, vmf->address, vma);
	__SetPageUptodate(vmf->cow_page);

	ret |= finish_fault(vmf);
	unlock_page(vmf->page);
	put_page(vmf->page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	return ret;
uncharge_out:
	mem_cgroup_cancel_charge(vmf->cow_page, vmf->memcg, false);
	put_page(vmf->cow_page);
	return ret;
}

static int do_shared_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	int ret, tmp;

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	/*
	 * Check if the backing address space wants to know that the page is
	 * about to become writable
	 */
	if (vma->vm_ops->page_mkwrite) {
		unlock_page(vmf->page);
		tmp = do_page_mkwrite(vmf);
		if (unlikely(!tmp ||
				(tmp & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
			put_page(vmf->page);
			return tmp;
		}
	}

	ret |= finish_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE |
					VM_FAULT_RETRY))) {
		unlock_page(vmf->page);
		put_page(vmf->page);
		return ret;
	}

	fault_dirty_shared_page(vma, vmf->page);
	return ret;
}

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults).
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
static int do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	int ret;

	/* The VMA was not fully populated on mmap() or missing VM_DONTEXPAND */
	if (!vma->vm_ops->fault)
		ret = VM_FAULT_SIGBUS;
	else if (!(vmf->flags & FAULT_FLAG_WRITE))
		ret = do_read_fault(vmf);
	else if (!(vma->vm_flags & VM_SHARED))
		ret = do_cow_fault(vmf);
	else
		ret = do_shared_fault(vmf);

	/* preallocated pagetable is unused: free it */
	if (vmf->prealloc_pte) {
		pte_free(vma->vm_mm, vmf->prealloc_pte);
		vmf->prealloc_pte = NULL;
	}
	return ret;
}

static int numa_migrate_prep(struct page *page, struct vm_area_struct *vma,
				unsigned long addr, int page_nid,
				int *flags)
{
	get_page(page);

	count_vm_numa_event(NUMA_HINT_FAULTS);
	if (page_nid == numa_node_id()) {
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);
		*flags |= TNF_FAULT_LOCAL;
	}

	return mpol_misplaced(page, vma, addr);
}

static int do_numa_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = NULL;
	int page_nid = -1;
	int last_cpupid;
	int target_nid;
	bool migrated = false;
	pte_t pte;
	bool was_writable = pte_savedwrite(vmf->orig_pte);
	int flags = 0;

	/*
	 * The "pte" at this point cannot be used safely without
	 * validation through pte_unmap_same(). It's of NUMA type but
	 * the pfn may be screwed if the read is non atomic.
	 */
	vmf->ptl = pte_lockptr(vma->vm_mm, vmf->pmd);
	spin_lock(vmf->ptl);
	if (unlikely(!pte_same(*vmf->pte, vmf->orig_pte))) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		goto out;
	}

	/*
	 * Make it present again, Depending on how arch implementes non
	 * accessible ptes, some can allow access by kernel mode.
	 */
	pte = ptep_modify_prot_start(vma->vm_mm, vmf->address, vmf->pte);
	pte = pte_modify(pte, vma->vm_page_prot);
	pte = pte_mkyoung(pte);
	if (was_writable)
		pte = pte_mkwrite(pte);
	ptep_modify_prot_commit(vma->vm_mm, vmf->address, vmf->pte, pte);
	update_mmu_cache(vma, vmf->address, vmf->pte);

	page = vm_normal_page(vma, vmf->address, pte);
	if (!page) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return 0;
	}

	/* TODO: handle PTE-mapped THP */
	if (PageCompound(page)) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return 0;
	}

	/*
	 * Avoid grouping on RO pages in general. RO pages shouldn't hurt as
	 * much anyway since they can be in shared cache state. This misses
	 * the case where a mapping is writable but the process never writes
	 * to it but pte_write gets cleared during protection updates and
	 * pte_dirty has unpredictable behaviour between PTE scan updates,
	 * background writeback, dirty balancing and application behaviour.
	 */
	if (!pte_write(pte))
		flags |= TNF_NO_GROUP;

	/*
	 * Flag if the page is shared between multiple address spaces. This
	 * is later used when determining whether to group tasks together
	 */
	if (page_mapcount(page) > 1 && (vma->vm_flags & VM_SHARED))
		flags |= TNF_SHARED;

	last_cpupid = page_cpupid_last(page);
	page_nid = page_to_nid(page);
	target_nid = numa_migrate_prep(page, vma, vmf->address, page_nid,
			&flags);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	if (target_nid == -1) {
		put_page(page);
		goto out;
	}

	/* Migrate to the requested node */
	migrated = migrate_misplaced_page(page, vma, target_nid);
	if (migrated) {
		page_nid = target_nid;
		flags |= TNF_MIGRATED;
	} else
		flags |= TNF_MIGRATE_FAIL;

out:
	if (page_nid != -1)
		task_numa_fault(last_cpupid, page_nid, 1, flags);
	return 0;
}

static inline int create_huge_pmd(struct vm_fault *vmf)
{
	if (vma_is_anonymous(vmf->vma))
		return do_huge_pmd_anonymous_page(vmf);
	if (vmf->vma->vm_ops->huge_fault)
		return vmf->vma->vm_ops->huge_fault(vmf, PE_SIZE_PMD);
	return VM_FAULT_FALLBACK;
}

/* `inline' is required to avoid gcc 4.1.2 build error */
static inline int wp_huge_pmd(struct vm_fault *vmf, pmd_t orig_pmd)
{
	if (vma_is_anonymous(vmf->vma))
		return do_huge_pmd_wp_page(vmf, orig_pmd);
	if (vmf->vma->vm_ops->huge_fault)
		return vmf->vma->vm_ops->huge_fault(vmf, PE_SIZE_PMD);

	/* COW handled on pte level: split pmd */
	VM_BUG_ON_VMA(vmf->vma->vm_flags & VM_SHARED, vmf->vma);
	__split_huge_pmd(vmf->vma, vmf->pmd, vmf->address, false, NULL);

	return VM_FAULT_FALLBACK;
}

static inline bool vma_is_accessible(struct vm_area_struct *vma)
{
	return vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE);
}

static int create_huge_pud(struct vm_fault *vmf)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/* No support for anonymous transparent PUD pages yet */
	if (vma_is_anonymous(vmf->vma))
		return VM_FAULT_FALLBACK;
	if (vmf->vma->vm_ops->huge_fault)
		return vmf->vma->vm_ops->huge_fault(vmf, PE_SIZE_PUD);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
	return VM_FAULT_FALLBACK;
}

static int wp_huge_pud(struct vm_fault *vmf, pud_t orig_pud)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/* No support for anonymous transparent PUD pages yet */
	if (vma_is_anonymous(vmf->vma))
		return VM_FAULT_FALLBACK;
	if (vmf->vma->vm_ops->huge_fault)
		return vmf->vma->vm_ops->huge_fault(vmf, PE_SIZE_PUD);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
	return VM_FAULT_FALLBACK;
}

/*
 * These routines also need to handle stuff like marking pages dirty
 * and/or accessed for architectures that don't do it in hardware (most
 * RISC architectures).  The early dirtying is also good on the i386.
 *
 * There is also a hook called "update_mmu_cache()" that architectures
 * with external mmu caches can use to update those (ie the Sparc or
 * PowerPC hashed page tables that act as extended TLBs).
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes, but allow
 * concurrent faults).
 *
 * The mmap_sem may have been released depending on flags and our return value.
 * See filemap_fault() and __lock_page_or_retry().
 */
static int handle_pte_fault(struct vm_fault *vmf)
{
	pte_t entry;

	if (unlikely(pmd_none(*vmf->pmd))) {
		/*
		 * Leave __pte_alloc() until later: because vm_ops->fault may
		 * want to allocate huge page, and if we expose page table
		 * for an instant, it will be difficult to retract from
		 * concurrent faults and from rmap lookups.
		 */
		vmf->pte = NULL;
	} else {
		/* See comment in pte_alloc_one_map() */
		if (pmd_devmap_trans_unstable(vmf->pmd))
			return 0;
		/*
		 * A regular pmd is established and it can't morph into a huge
		 * pmd from under us anymore at this point because we hold the
		 * mmap_sem read mode and khugepaged takes it in write mode.
		 * So now it's safe to run pte_offset_map().
		 */
		vmf->pte = pte_offset_map(vmf->pmd, vmf->address);
		vmf->orig_pte = *vmf->pte;

		/*
		 * some architectures can have larger ptes than wordsize,
		 * e.g.ppc44x-defconfig has CONFIG_PTE_64BIT=y and
		 * CONFIG_32BIT=y, so READ_ONCE cannot guarantee atomic
		 * accesses.  The code below just needs a consistent view
		 * for the ifs and we later double check anyway with the
		 * ptl lock held. So here a barrier will do.
		 */
		barrier();
		if (pte_none(vmf->orig_pte)) {
			pte_unmap(vmf->pte);
			vmf->pte = NULL;
		}
	}

	if (!vmf->pte) {
		if (vma_is_anonymous(vmf->vma))
			return do_anonymous_page(vmf);
		else
			return do_fault(vmf);
	}

	if (!pte_present(vmf->orig_pte))
		return do_swap_page(vmf);

	if (pte_protnone(vmf->orig_pte) && vma_is_accessible(vmf->vma))
		return do_numa_page(vmf);

	vmf->ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd);
	spin_lock(vmf->ptl);
	entry = vmf->orig_pte;
	if (unlikely(!pte_same(*vmf->pte, entry)))
		goto unlock;
	if (vmf->flags & FAULT_FLAG_WRITE) {
		if (!pte_write(entry))
			return do_wp_page(vmf);
		entry = pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	if (ptep_set_access_flags(vmf->vma, vmf->address, vmf->pte, entry,
				vmf->flags & FAULT_FLAG_WRITE)) {
		update_mmu_cache(vmf->vma, vmf->address, vmf->pte);
	} else {
		/*
		 * This is needed only for protection faults but the arch code
		 * is not yet telling us if this is a protection fault or not.
		 * This still avoids useless tlb flushes for .text page faults
		 * with threads.
		 */
		if (vmf->flags & FAULT_FLAG_WRITE)
			flush_tlb_fix_spurious_fault(vmf->vma, vmf->address);
	}
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return 0;
}

/*
 * By the time we get here, we already hold the mm semaphore
 *
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
static int __handle_mm_fault(struct vm_area_struct *vma, unsigned long address,
		unsigned int flags)
{
	struct vm_fault vmf = {
		.vma = vma,
		.address = address & PAGE_MASK,
		.flags = flags,
		.pgoff = linear_page_index(vma, address),
		.gfp_mask = __get_fault_gfp_mask(vma),
	};
	unsigned int dirty = flags & FAULT_FLAG_WRITE;
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	p4d_t *p4d;
	int ret;

	pgd = pgd_offset(mm, address);
	p4d = p4d_alloc(mm, pgd, address);
	if (!p4d)
		return VM_FAULT_OOM;

	vmf.pud = pud_alloc(mm, p4d, address);
	if (!vmf.pud)
		return VM_FAULT_OOM;
	if (pud_none(*vmf.pud) && transparent_hugepage_enabled(vma)) {
		ret = create_huge_pud(&vmf);
		if (!(ret & VM_FAULT_FALLBACK))
			return ret;
	} else {
		pud_t orig_pud = *vmf.pud;

		barrier();
		if (pud_trans_huge(orig_pud) || pud_devmap(orig_pud)) {

			/* NUMA case for anonymous PUDs would go here */

			if (dirty && !pud_write(orig_pud)) {
				ret = wp_huge_pud(&vmf, orig_pud);
				if (!(ret & VM_FAULT_FALLBACK))
					return ret;
			} else {
				huge_pud_set_accessed(&vmf, orig_pud);
				return 0;
			}
		}
	}

	vmf.pmd = pmd_alloc(mm, vmf.pud, address);
	if (!vmf.pmd)
		return VM_FAULT_OOM;
	if (pmd_none(*vmf.pmd) && transparent_hugepage_enabled(vma)) {
		ret = create_huge_pmd(&vmf);
		if (!(ret & VM_FAULT_FALLBACK))
			return ret;
	} else {
		pmd_t orig_pmd = *vmf.pmd;

		barrier();
		if (unlikely(is_swap_pmd(orig_pmd))) {
			VM_BUG_ON(thp_migration_supported() &&
					  !is_pmd_migration_entry(orig_pmd));
			if (is_pmd_migration_entry(orig_pmd))
				pmd_migration_entry_wait(mm, vmf.pmd);
			return 0;
		}
		if (pmd_trans_huge(orig_pmd) || pmd_devmap(orig_pmd)) {
			if (pmd_protnone(orig_pmd) && vma_is_accessible(vma))
				return do_huge_pmd_numa_page(&vmf, orig_pmd);

			if (dirty && !pmd_write(orig_pmd)) {
				ret = wp_huge_pmd(&vmf, orig_pmd);
				if (!(ret & VM_FAULT_FALLBACK))
					return ret;
			} else {
				huge_pmd_set_accessed(&vmf, orig_pmd);
				return 0;
			}
		}
	}

	return handle_pte_fault(&vmf);
}

/*
 * By the time we get here, we already hold the mm semaphore
 *
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
int handle_mm_fault(struct vm_area_struct *vma, unsigned long address,
		unsigned int flags)
{
	int ret;

	__set_current_state(TASK_RUNNING);

	count_vm_event(PGFAULT);
	count_memcg_event_mm(vma->vm_mm, PGFAULT);

	/* do counter updates before entering really critical section. */
	check_sync_rss_stat(current);

	if (!arch_vma_access_permitted(vma, flags & FAULT_FLAG_WRITE,
					    flags & FAULT_FLAG_INSTRUCTION,
					    flags & FAULT_FLAG_REMOTE))
		return VM_FAULT_SIGSEGV;

	/*
	 * Enable the memcg OOM handling for faults triggered in user
	 * space.  Kernel faults are handled more gracefully.
	 */
	if (flags & FAULT_FLAG_USER)
		mem_cgroup_oom_enable();

	if (unlikely(is_vm_hugetlb_page(vma)))
		ret = hugetlb_fault(vma->vm_mm, vma, address, flags);
	else
		ret = __handle_mm_fault(vma, address, flags);

	if (flags & FAULT_FLAG_USER) {
		mem_cgroup_oom_disable();
		/*
		 * The task may have entered a memcg OOM situation but
		 * if the allocation error was handled gracefully (no
		 * VM_FAULT_OOM), there is no need to kill anything.
		 * Just clean up the OOM state peacefully.
		 */
		if (task_in_memcg_oom(current) && !(ret & VM_FAULT_OOM))
			mem_cgroup_oom_synchronize(false);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(handle_mm_fault);

#ifndef __PAGETABLE_P4D_FOLDED
/*
 * Allocate p4d page table.
 * We've already handled the fast-path in-line.
 */
int __p4d_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	p4d_t *new = p4d_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&mm->page_table_lock);
	if (pgd_present(*pgd))		/* Another has populated it */
		p4d_free(mm, new);
	else
		pgd_populate(mm, pgd, new);
	spin_unlock(&mm->page_table_lock);
	return 0;
}
#endif /* __PAGETABLE_P4D_FOLDED */

#ifndef __PAGETABLE_PUD_FOLDED
/*
 * Allocate page upper directory.
 * We've already handled the fast-path in-line.
 */
int __pud_alloc(struct mm_struct *mm, p4d_t *p4d, unsigned long address)
{
	pud_t *new = pud_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&mm->page_table_lock);
#ifndef __ARCH_HAS_5LEVEL_HACK
	if (!p4d_present(*p4d)) {
		mm_inc_nr_puds(mm);
		p4d_populate(mm, p4d, new);
	} else	/* Another has populated it */
		pud_free(mm, new);
#else
	if (!pgd_present(*p4d)) {
		mm_inc_nr_puds(mm);
		pgd_populate(mm, p4d, new);
	} else	/* Another has populated it */
		pud_free(mm, new);
#endif /* __ARCH_HAS_5LEVEL_HACK */
	spin_unlock(&mm->page_table_lock);
	return 0;
}
#endif /* __PAGETABLE_PUD_FOLDED */

#ifndef __PAGETABLE_PMD_FOLDED
/*
 * Allocate page middle directory.
 * We've already handled the fast-path in-line.
 */
int __pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	spinlock_t *ptl;
	pmd_t *new = pmd_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	ptl = pud_lock(mm, pud);
#ifndef __ARCH_HAS_4LEVEL_HACK
	if (!pud_present(*pud)) {
		mm_inc_nr_pmds(mm);
		pud_populate(mm, pud, new);
	} else	/* Another has populated it */
		pmd_free(mm, new);
#else
	if (!pgd_present(*pud)) {
		mm_inc_nr_pmds(mm);
		pgd_populate(mm, pud, new);
	} else /* Another has populated it */
		pmd_free(mm, new);
#endif /* __ARCH_HAS_4LEVEL_HACK */
	spin_unlock(ptl);
	return 0;
}
#endif /* __PAGETABLE_PMD_FOLDED */

static int __follow_pte_pmd(struct mm_struct *mm, unsigned long address,
			    unsigned long *start, unsigned long *end,
			    pte_t **ptepp, pmd_t **pmdpp, spinlock_t **ptlp)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		goto out;

	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d)))
		goto out;

	pud = pud_offset(p4d, address);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		goto out;

	pmd = pmd_offset(pud, address);
	VM_BUG_ON(pmd_trans_huge(*pmd));

	if (pmd_huge(*pmd)) {
		if (!pmdpp)
			goto out;

		if (start && end) {
			*start = address & PMD_MASK;
			*end = *start + PMD_SIZE;
			mmu_notifier_invalidate_range_start(mm, *start, *end);
		}
		*ptlp = pmd_lock(mm, pmd);
		if (pmd_huge(*pmd)) {
			*pmdpp = pmd;
			return 0;
		}
		spin_unlock(*ptlp);
		if (start && end)
			mmu_notifier_invalidate_range_end(mm, *start, *end);
	}

	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		goto out;

	if (start && end) {
		*start = address & PAGE_MASK;
		*end = *start + PAGE_SIZE;
		mmu_notifier_invalidate_range_start(mm, *start, *end);
	}
	ptep = pte_offset_map_lock(mm, pmd, address, ptlp);
	if (!pte_present(*ptep))
		goto unlock;
	*ptepp = ptep;
	return 0;
unlock:
	pte_unmap_unlock(ptep, *ptlp);
	if (start && end)
		mmu_notifier_invalidate_range_end(mm, *start, *end);
out:
	return -EINVAL;
}

static inline int follow_pte(struct mm_struct *mm, unsigned long address,
			     pte_t **ptepp, spinlock_t **ptlp)
{
	int res;

	/* (void) is needed to make gcc happy */
	(void) __cond_lock(*ptlp,
			   !(res = __follow_pte_pmd(mm, address, NULL, NULL,
						    ptepp, NULL, ptlp)));
	return res;
}

int follow_pte_pmd(struct mm_struct *mm, unsigned long address,
			     unsigned long *start, unsigned long *end,
			     pte_t **ptepp, pmd_t **pmdpp, spinlock_t **ptlp)
{
	int res;

	/* (void) is needed to make gcc happy */
	(void) __cond_lock(*ptlp,
			   !(res = __follow_pte_pmd(mm, address, start, end,
						    ptepp, pmdpp, ptlp)));
	return res;
}
EXPORT_SYMBOL(follow_pte_pmd);

/**
 * follow_pfn - look up PFN at a user virtual address
 * @vma: memory mapping
 * @address: user virtual address
 * @pfn: location to store found PFN
 *
 * Only IO mappings and raw PFN mappings are allowed.
 *
 * Returns zero and the pfn at @pfn on success, -ve otherwise.
 */
int follow_pfn(struct vm_area_struct *vma, unsigned long address,
	unsigned long *pfn)
{
	int ret = -EINVAL;
	spinlock_t *ptl;
	pte_t *ptep;

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		return ret;

	ret = follow_pte(vma->vm_mm, address, &ptep, &ptl);
	if (ret)
		return ret;
	*pfn = pte_pfn(*ptep);
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(follow_pfn);

#ifdef CONFIG_HAVE_IOREMAP_PROT
int follow_phys(struct vm_area_struct *vma,
		unsigned long address, unsigned int flags,
		unsigned long *prot, resource_size_t *phys)
{
	int ret = -EINVAL;
	pte_t *ptep, pte;
	spinlock_t *ptl;

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		goto out;

	if (follow_pte(vma->vm_mm, address, &ptep, &ptl))
		goto out;
	pte = *ptep;

	if ((flags & FOLL_WRITE) && !pte_write(pte))
		goto unlock;

	*prot = pgprot_val(pte_pgprot(pte));
	*phys = (resource_size_t)pte_pfn(pte) << PAGE_SHIFT;

	ret = 0;
unlock:
	pte_unmap_unlock(ptep, ptl);
out:
	return ret;
}

int generic_access_phys(struct vm_area_struct *vma, unsigned long addr,
			void *buf, int len, int write)
{
	resource_size_t phys_addr;
	unsigned long prot = 0;
	void __iomem *maddr;
	int offset = addr & (PAGE_SIZE-1);

	if (follow_phys(vma, addr, write, &prot, &phys_addr))
		return -EINVAL;

	maddr = ioremap_prot(phys_addr, PAGE_ALIGN(len + offset), prot);
	if (write)
		memcpy_toio(maddr + offset, buf, len);
	else
		memcpy_fromio(buf, maddr + offset, len);
	iounmap(maddr);

	return len;
}
EXPORT_SYMBOL_GPL(generic_access_phys);
#endif

/*
 * Access another process' address space as given in mm.  If non-NULL, use the
 * given task for page fault accounting.
 */
int __access_remote_vm(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long addr, void *buf, int len, unsigned int gup_flags)
{
	struct vm_area_struct *vma;
	void *old_buf = buf;
	int write = gup_flags & FOLL_WRITE;

	down_read(&mm->mmap_sem);
	/* ignore errors, just check how much was successfully transferred */
	while (len) {
		int bytes, ret, offset;
		void *maddr;
		struct page *page = NULL;

		ret = get_user_pages_remote(tsk, mm, addr, 1,
				gup_flags, &page, &vma, NULL);
		if (ret <= 0) {
#ifndef CONFIG_HAVE_IOREMAP_PROT
			break;
#else
			/*
			 * Check if this is a VM_IO | VM_PFNMAP VMA, which
			 * we can access using slightly different code.
			 */
			vma = find_vma(mm, addr);
			if (!vma || vma->vm_start > addr)
				break;
			if (vma->vm_ops && vma->vm_ops->access)
				ret = vma->vm_ops->access(vma, addr, buf,
							  len, write);
			if (ret <= 0)
				break;
			bytes = ret;
#endif
		} else {
			bytes = len;
			offset = addr & (PAGE_SIZE-1);
			if (bytes > PAGE_SIZE-offset)
				bytes = PAGE_SIZE-offset;

			maddr = kmap(page);
			if (write) {
				copy_to_user_page(vma, page, addr,
						  maddr + offset, buf, bytes);
				set_page_dirty_lock(page);
			} else {
				copy_from_user_page(vma, page, addr,
						    buf, maddr + offset, bytes);
			}
			kunmap(page);
			put_page(page);
		}
		len -= bytes;
		buf += bytes;
		addr += bytes;
	}
	up_read(&mm->mmap_sem);

	return buf - old_buf;
}

/**
 * access_remote_vm - access another process' address space
 * @mm:		the mm_struct of the target address space
 * @addr:	start address to access
 * @buf:	source or destination buffer
 * @len:	number of bytes to transfer
 * @gup_flags:	flags modifying lookup behaviour
 *
 * The caller must hold a reference on @mm.
 */
int access_remote_vm(struct mm_struct *mm, unsigned long addr,
		void *buf, int len, unsigned int gup_flags)
{
	return __access_remote_vm(NULL, mm, addr, buf, len, gup_flags);
}

/*
 * Access another process' address space.
 * Source/target buffer must be kernel space,
 * Do not walk the page table directly, use get_user_pages
 */
int access_process_vm(struct task_struct *tsk, unsigned long addr,
		void *buf, int len, unsigned int gup_flags)
{
	struct mm_struct *mm;
	int ret;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

	ret = __access_remote_vm(tsk, mm, addr, buf, len, gup_flags);

	mmput(mm);

	return ret;
}
EXPORT_SYMBOL_GPL(access_process_vm);

/*
 * Print the name of a VMA.
 */
void print_vma_addr(char *prefix, unsigned long ip)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	/*
	 * we might be running from an atomic context so we cannot sleep
	 */
	if (!down_read_trylock(&mm->mmap_sem))
		return;

	vma = find_vma(mm, ip);
	if (vma && vma->vm_file) {
		struct file *f = vma->vm_file;
		char *buf = (char *)__get_free_page(GFP_NOWAIT);
		if (buf) {
			char *p;

			p = file_path(f, buf, PAGE_SIZE);
			if (IS_ERR(p))
				p = "?";
			printk("%s%s[%lx+%lx]", prefix, kbasename(p),
					vma->vm_start,
					vma->vm_end - vma->vm_start);
			free_page((unsigned long)buf);
		}
	}
	up_read(&mm->mmap_sem);
}

#if defined(CONFIG_PROVE_LOCKING) || defined(CONFIG_DEBUG_ATOMIC_SLEEP)
void __might_fault(const char *file, int line)
{
	/*
	 * Some code (nfs/sunrpc) uses socket ops on kernel memory while
	 * holding the mmap_sem, this is safe because kernel memory doesn't
	 * get paged out, therefore we'll never actually fault, and the
	 * below annotations will generate false positives.
	 */
	if (uaccess_kernel())
		return;
	if (pagefault_disabled())
		return;
	__might_sleep(file, line, 0);
#if defined(CONFIG_DEBUG_ATOMIC_SLEEP)
	if (current->mm)
		might_lock_read(&current->mm->mmap_sem);
#endif
}
EXPORT_SYMBOL(__might_fault);
#endif

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_HUGETLBFS)
static void clear_gigantic_page(struct page *page,
				unsigned long addr,
				unsigned int pages_per_huge_page)
{
	int i;
	struct page *p = page;

	might_sleep();
	for (i = 0; i < pages_per_huge_page;
	     i++, p = mem_map_next(p, page, i)) {
		cond_resched();
		clear_user_highpage(p, addr + i * PAGE_SIZE);
	}
}
void clear_huge_page(struct page *page,
		     unsigned long addr_hint, unsigned int pages_per_huge_page)
{
	int i, n, base, l;
	unsigned long addr = addr_hint &
		~(((unsigned long)pages_per_huge_page << PAGE_SHIFT) - 1);

	if (unlikely(pages_per_huge_page > MAX_ORDER_NR_PAGES)) {
		clear_gigantic_page(page, addr, pages_per_huge_page);
		return;
	}

	/* Clear sub-page to access last to keep its cache lines hot */
	might_sleep();
	n = (addr_hint - addr) / PAGE_SIZE;
	if (2 * n <= pages_per_huge_page) {
		/* If sub-page to access in first half of huge page */
		base = 0;
		l = n;
		/* Clear sub-pages at the end of huge page */
		for (i = pages_per_huge_page - 1; i >= 2 * n; i--) {
			cond_resched();
			clear_user_highpage(page + i, addr + i * PAGE_SIZE);
		}
	} else {
		/* If sub-page to access in second half of huge page */
		base = pages_per_huge_page - 2 * (pages_per_huge_page - n);
		l = pages_per_huge_page - n;
		/* Clear sub-pages at the begin of huge page */
		for (i = 0; i < base; i++) {
			cond_resched();
			clear_user_highpage(page + i, addr + i * PAGE_SIZE);
		}
	}
	/*
	 * Clear remaining sub-pages in left-right-left-right pattern
	 * towards the sub-page to access
	 */
	for (i = 0; i < l; i++) {
		int left_idx = base + i;
		int right_idx = base + 2 * l - 1 - i;

		cond_resched();
		clear_user_highpage(page + left_idx,
				    addr + left_idx * PAGE_SIZE);
		cond_resched();
		clear_user_highpage(page + right_idx,
				    addr + right_idx * PAGE_SIZE);
	}
}

static void copy_user_gigantic_page(struct page *dst, struct page *src,
				    unsigned long addr,
				    struct vm_area_struct *vma,
				    unsigned int pages_per_huge_page)
{
	int i;
	struct page *dst_base = dst;
	struct page *src_base = src;

	for (i = 0; i < pages_per_huge_page; ) {
		cond_resched();
		copy_user_highpage(dst, src, addr + i*PAGE_SIZE, vma);

		i++;
		dst = mem_map_next(dst, dst_base, i);
		src = mem_map_next(src, src_base, i);
	}
}

void copy_user_huge_page(struct page *dst, struct page *src,
			 unsigned long addr, struct vm_area_struct *vma,
			 unsigned int pages_per_huge_page)
{
	int i;

	if (unlikely(pages_per_huge_page > MAX_ORDER_NR_PAGES)) {
		copy_user_gigantic_page(dst, src, addr, vma,
					pages_per_huge_page);
		return;
	}

	might_sleep();
	for (i = 0; i < pages_per_huge_page; i++) {
		cond_resched();
		copy_user_highpage(dst + i, src + i, addr + i*PAGE_SIZE, vma);
	}
}

long copy_huge_page_from_user(struct page *dst_page,
				const void __user *usr_src,
				unsigned int pages_per_huge_page,
				bool allow_pagefault)
{
	void *src = (void *)usr_src;
	void *page_kaddr;
	unsigned long i, rc = 0;
	unsigned long ret_val = pages_per_huge_page * PAGE_SIZE;

	for (i = 0; i < pages_per_huge_page; i++) {
		if (allow_pagefault)
			page_kaddr = kmap(dst_page + i);
		else
			page_kaddr = kmap_atomic(dst_page + i);
		rc = copy_from_user(page_kaddr,
				(const void __user *)(src + i * PAGE_SIZE),
				PAGE_SIZE);
		if (allow_pagefault)
			kunmap(dst_page + i);
		else
			kunmap_atomic(page_kaddr);

		ret_val -= (PAGE_SIZE - rc);
		if (rc)
			break;

		cond_resched();
	}
	return ret_val;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE || CONFIG_HUGETLBFS */

#if USE_SPLIT_PTE_PTLOCKS && ALLOC_SPLIT_PTLOCKS

static struct kmem_cache *page_ptl_cachep;

void __init ptlock_cache_init(void)
{
	page_ptl_cachep = kmem_cache_create("page->ptl", sizeof(spinlock_t), 0,
			SLAB_PANIC, NULL);
}

bool ptlock_alloc(struct page *page)
{
	spinlock_t *ptl;

	ptl = kmem_cache_alloc(page_ptl_cachep, GFP_KERNEL);
	if (!ptl)
		return false;
	page->ptl = ptl;
	return true;
}

void ptlock_free(struct page *page)
{
	kmem_cache_free(page_ptl_cachep, page->ptl);
}
#endif
