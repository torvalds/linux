// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/mm_inline.h>
#include <linux/sched/mm.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/task.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/memremap.h>
#include <linux/kmsan.h>
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
#include <linux/memory-tiers.h>
#include <linux/debugfs.h>
#include <linux/userfaultfd_k.h>
#include <linux/dax.h>
#include <linux/oom.h>
#include <linux/numa.h>
#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/vmalloc.h>
#include <linux/sched/sysctl.h>

#include <trace/events/kmem.h>

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

#include "pgalloc-track.h"
#include "internal.h"
#include "swap.h"

#if defined(LAST_CPUPID_NOT_IN_PAGE_FLAGS) && !defined(CONFIG_COMPILE_TEST)
#warning Unfortunate NUMA and NUMA Balancing config, growing page-frame for last_cpupid.
#endif

#ifndef CONFIG_NUMA
unsigned long max_mapnr;
EXPORT_SYMBOL(max_mapnr);

struct page *mem_map;
EXPORT_SYMBOL(mem_map);
#endif

static vm_fault_t do_fault(struct vm_fault *vmf);
static vm_fault_t do_anonymous_page(struct vm_fault *vmf);
static bool vmf_pte_changed(struct vm_fault *vmf);

/*
 * Return true if the original pte was a uffd-wp pte marker (so the pte was
 * wr-protected).
 */
static __always_inline bool vmf_orig_pte_uffd_wp(struct vm_fault *vmf)
{
	if (!userfaultfd_wp(vmf->vma))
		return false;
	if (!(vmf->flags & FAULT_FLAG_ORIG_PTE_VALID))
		return false;

	return pte_marker_uffd_wp(vmf->orig_pte);
}

/*
 * A number of key systems in x86 including ioremap() rely on the assumption
 * that high_memory defines the upper bound on direct map memory, then end
 * of ZONE_NORMAL.
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

#ifndef arch_wants_old_prefaulted_pte
static inline bool arch_wants_old_prefaulted_pte(void)
{
	/*
	 * Transitioning a PTE from 'old' to 'young' can be expensive on
	 * some architectures, even if it's performed in hardware. By
	 * default, "false" means prefaulted entries will be 'young'.
	 */
	return false;
}
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
early_initcall(init_zero_pfn);

void mm_trace_rss_stat(struct mm_struct *mm, int member)
{
	trace_rss_stat(mm, member);
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
	tlb_change_page_size(tlb, PAGE_SIZE);
	pgd = pgd_offset(tlb->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		free_p4d_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

void free_pgtables(struct mmu_gather *tlb, struct ma_state *mas,
		   struct vm_area_struct *vma, unsigned long floor,
		   unsigned long ceiling, bool mm_wr_locked)
{
	struct unlink_vma_file_batch vb;

	do {
		unsigned long addr = vma->vm_start;
		struct vm_area_struct *next;

		/*
		 * Note: USER_PGTABLES_CEILING may be passed as ceiling and may
		 * be 0.  This will underflow and is okay.
		 */
		next = mas_find(mas, ceiling - 1);
		if (unlikely(xa_is_zero(next)))
			next = NULL;

		/*
		 * Hide vma from rmap and truncate_pagecache before freeing
		 * pgtables
		 */
		if (mm_wr_locked)
			vma_start_write(vma);
		unlink_anon_vmas(vma);

		if (is_vm_hugetlb_page(vma)) {
			unlink_file_vma(vma);
			hugetlb_free_pgd_range(tlb, addr, vma->vm_end,
				floor, next ? next->vm_start : ceiling);
		} else {
			unlink_file_vma_batch_init(&vb);
			unlink_file_vma_batch_add(&vb, vma);

			/*
			 * Optimization: gather nearby vmas into one call down
			 */
			while (next && next->vm_start <= vma->vm_end + PMD_SIZE
			       && !is_vm_hugetlb_page(next)) {
				vma = next;
				next = mas_find(mas, ceiling - 1);
				if (unlikely(xa_is_zero(next)))
					next = NULL;
				if (mm_wr_locked)
					vma_start_write(vma);
				unlink_anon_vmas(vma);
				unlink_file_vma_batch_add(&vb, vma);
			}
			unlink_file_vma_batch_final(&vb);
			free_pgd_range(tlb, addr, vma->vm_end,
				floor, next ? next->vm_start : ceiling);
		}
		vma = next;
	} while (vma);
}

void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte)
{
	spinlock_t *ptl = pmd_lock(mm, pmd);

	if (likely(pmd_none(*pmd))) {	/* Has another populated it ? */
		mm_inc_nr_ptes(mm);
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
		 * smp_rmb() barriers in page table walking code.
		 */
		smp_wmb(); /* Could be smp_wmb__xxx(before|after)_spin_lock */
		pmd_populate(mm, pmd, *pte);
		*pte = NULL;
	}
	spin_unlock(ptl);
}

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd)
{
	pgtable_t new = pte_alloc_one(mm);
	if (!new)
		return -ENOMEM;

	pmd_install(mm, pmd, &new);
	if (new)
		pte_free(mm, new);
	return 0;
}

int __pte_alloc_kernel(pmd_t *pmd)
{
	pte_t *new = pte_alloc_one_kernel(&init_mm);
	if (!new)
		return -ENOMEM;

	spin_lock(&init_mm.page_table_lock);
	if (likely(pmd_none(*pmd))) {	/* Has another populated it ? */
		smp_wmb(); /* See comment in pmd_install() */
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
	pr_alert("addr:%px vm_flags:%08lx anon_vma:%px mapping:%px index:%lx\n",
		 (void *)addr, vma->vm_flags, vma->anon_vma, mapping, index);
	pr_alert("file:%pD fault:%ps mmap:%ps read_folio:%ps\n",
		 vma->vm_file,
		 vma->vm_ops ? vma->vm_ops->fault : NULL,
		 vma->vm_file ? vma->vm_file->f_op->mmap : NULL,
		 mapping ? mapping->a_ops->read_folio : NULL);
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
 * normal pages by the VM. The only exception are zeropages, which are
 * *never* refcounted.
 *
 * The disadvantage is that pages are refcounted (which can be slower and
 * simply not an option for some PFNMAP users). The advantage is that we
 * don't have to follow the strict linearity rule of PFNMAP mappings in
 * order to support COWable mappings.
 *
 */
struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			    pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);

	if (IS_ENABLED(CONFIG_ARCH_HAS_PTE_SPECIAL)) {
		if (likely(!pte_special(pte)))
			goto check_pfn;
		if (vma->vm_ops && vma->vm_ops->find_special_page)
			return vma->vm_ops->find_special_page(vma, addr);
		if (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))
			return NULL;
		if (is_zero_pfn(pfn))
			return NULL;
		if (pte_devmap(pte))
		/*
		 * NOTE: New users of ZONE_DEVICE will not set pte_devmap()
		 * and will have refcounts incremented on their struct pages
		 * when they are inserted into PTEs, thus they are safe to
		 * return here. Legacy ZONE_DEVICE pages that set pte_devmap()
		 * do not have refcounts. Example of legacy ZONE_DEVICE is
		 * MEMORY_DEVICE_FS_DAX type in pmem or virtio_fs drivers.
		 */
			return NULL;

		print_bad_pte(vma, addr, pte, NULL);
		return NULL;
	}

	/* !CONFIG_ARCH_HAS_PTE_SPECIAL case follows: */

	if (unlikely(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP))) {
		if (vma->vm_flags & VM_MIXEDMAP) {
			if (!pfn_valid(pfn))
				return NULL;
			if (is_zero_pfn(pfn))
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
	VM_WARN_ON_ONCE(is_zero_pfn(pfn));
	return pfn_to_page(pfn);
}

struct folio *vm_normal_folio(struct vm_area_struct *vma, unsigned long addr,
			    pte_t pte)
{
	struct page *page = vm_normal_page(vma, addr, pte);

	if (page)
		return page_folio(page);
	return NULL;
}

#ifdef CONFIG_PGTABLE_HAS_HUGE_LEAVES
struct page *vm_normal_page_pmd(struct vm_area_struct *vma, unsigned long addr,
				pmd_t pmd)
{
	unsigned long pfn = pmd_pfn(pmd);

	/* Currently it's only used for huge pfnmaps */
	if (unlikely(pmd_special(pmd)))
		return NULL;

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

	if (pmd_devmap(pmd))
		return NULL;
	if (is_huge_zero_pmd(pmd))
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

struct folio *vm_normal_folio_pmd(struct vm_area_struct *vma,
				  unsigned long addr, pmd_t pmd)
{
	struct page *page = vm_normal_page_pmd(vma, addr, pmd);

	if (page)
		return page_folio(page);
	return NULL;
}
#endif

static void restore_exclusive_pte(struct vm_area_struct *vma,
				  struct page *page, unsigned long address,
				  pte_t *ptep)
{
	struct folio *folio = page_folio(page);
	pte_t orig_pte;
	pte_t pte;
	swp_entry_t entry;

	orig_pte = ptep_get(ptep);
	pte = pte_mkold(mk_pte(page, READ_ONCE(vma->vm_page_prot)));
	if (pte_swp_soft_dirty(orig_pte))
		pte = pte_mksoft_dirty(pte);

	entry = pte_to_swp_entry(orig_pte);
	if (pte_swp_uffd_wp(orig_pte))
		pte = pte_mkuffd_wp(pte);
	else if (is_writable_device_exclusive_entry(entry))
		pte = maybe_mkwrite(pte_mkdirty(pte), vma);

	VM_BUG_ON_FOLIO(pte_write(pte) && (!folio_test_anon(folio) &&
					   PageAnonExclusive(page)), folio);

	/*
	 * No need to take a page reference as one was already
	 * created when the swap entry was made.
	 */
	if (folio_test_anon(folio))
		folio_add_anon_rmap_pte(folio, page, vma, address, RMAP_NONE);
	else
		/*
		 * Currently device exclusive access only supports anonymous
		 * memory so the entry shouldn't point to a filebacked page.
		 */
		WARN_ON_ONCE(1);

	set_pte_at(vma->vm_mm, address, ptep, pte);

	/*
	 * No need to invalidate - it was non-present before. However
	 * secondary CPUs may have mappings that need invalidating.
	 */
	update_mmu_cache(vma, address, ptep);
}

/*
 * Tries to restore an exclusive pte if the page lock can be acquired without
 * sleeping.
 */
static int
try_restore_exclusive_pte(pte_t *src_pte, struct vm_area_struct *vma,
			unsigned long addr)
{
	swp_entry_t entry = pte_to_swp_entry(ptep_get(src_pte));
	struct page *page = pfn_swap_entry_to_page(entry);

	if (trylock_page(page)) {
		restore_exclusive_pte(vma, page, addr, src_pte);
		unlock_page(page);
		return 0;
	}

	return -EBUSY;
}

/*
 * copy one vm_area from one task to the other. Assumes the page tables
 * already present in the new task to be cleared in the whole range
 * covered by this vma.
 */

static unsigned long
copy_nonpresent_pte(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pte_t *dst_pte, pte_t *src_pte, struct vm_area_struct *dst_vma,
		struct vm_area_struct *src_vma, unsigned long addr, int *rss)
{
	unsigned long vm_flags = dst_vma->vm_flags;
	pte_t orig_pte = ptep_get(src_pte);
	pte_t pte = orig_pte;
	struct folio *folio;
	struct page *page;
	swp_entry_t entry = pte_to_swp_entry(orig_pte);

	if (likely(!non_swap_entry(entry))) {
		if (swap_duplicate(entry) < 0)
			return -EIO;

		/* make sure dst_mm is on swapoff's mmlist. */
		if (unlikely(list_empty(&dst_mm->mmlist))) {
			spin_lock(&mmlist_lock);
			if (list_empty(&dst_mm->mmlist))
				list_add(&dst_mm->mmlist,
						&src_mm->mmlist);
			spin_unlock(&mmlist_lock);
		}
		/* Mark the swap entry as shared. */
		if (pte_swp_exclusive(orig_pte)) {
			pte = pte_swp_clear_exclusive(orig_pte);
			set_pte_at(src_mm, addr, src_pte, pte);
		}
		rss[MM_SWAPENTS]++;
	} else if (is_migration_entry(entry)) {
		folio = pfn_swap_entry_folio(entry);

		rss[mm_counter(folio)]++;

		if (!is_readable_migration_entry(entry) &&
				is_cow_mapping(vm_flags)) {
			/*
			 * COW mappings require pages in both parent and child
			 * to be set to read. A previously exclusive entry is
			 * now shared.
			 */
			entry = make_readable_migration_entry(
							swp_offset(entry));
			pte = swp_entry_to_pte(entry);
			if (pte_swp_soft_dirty(orig_pte))
				pte = pte_swp_mksoft_dirty(pte);
			if (pte_swp_uffd_wp(orig_pte))
				pte = pte_swp_mkuffd_wp(pte);
			set_pte_at(src_mm, addr, src_pte, pte);
		}
	} else if (is_device_private_entry(entry)) {
		page = pfn_swap_entry_to_page(entry);
		folio = page_folio(page);

		/*
		 * Update rss count even for unaddressable pages, as
		 * they should treated just like normal pages in this
		 * respect.
		 *
		 * We will likely want to have some new rss counters
		 * for unaddressable pages, at some point. But for now
		 * keep things as they are.
		 */
		folio_get(folio);
		rss[mm_counter(folio)]++;
		/* Cannot fail as these pages cannot get pinned. */
		folio_try_dup_anon_rmap_pte(folio, page, src_vma);

		/*
		 * We do not preserve soft-dirty information, because so
		 * far, checkpoint/restore is the only feature that
		 * requires that. And checkpoint/restore does not work
		 * when a device driver is involved (you cannot easily
		 * save and restore device driver state).
		 */
		if (is_writable_device_private_entry(entry) &&
		    is_cow_mapping(vm_flags)) {
			entry = make_readable_device_private_entry(
							swp_offset(entry));
			pte = swp_entry_to_pte(entry);
			if (pte_swp_uffd_wp(orig_pte))
				pte = pte_swp_mkuffd_wp(pte);
			set_pte_at(src_mm, addr, src_pte, pte);
		}
	} else if (is_device_exclusive_entry(entry)) {
		/*
		 * Make device exclusive entries present by restoring the
		 * original entry then copying as for a present pte. Device
		 * exclusive entries currently only support private writable
		 * (ie. COW) mappings.
		 */
		VM_BUG_ON(!is_cow_mapping(src_vma->vm_flags));
		if (try_restore_exclusive_pte(src_pte, src_vma, addr))
			return -EBUSY;
		return -ENOENT;
	} else if (is_pte_marker_entry(entry)) {
		pte_marker marker = copy_pte_marker(entry, dst_vma);

		if (marker)
			set_pte_at(dst_mm, addr, dst_pte,
				   make_pte_marker(marker));
		return 0;
	}
	if (!userfaultfd_wp(dst_vma))
		pte = pte_swp_clear_uffd_wp(pte);
	set_pte_at(dst_mm, addr, dst_pte, pte);
	return 0;
}

/*
 * Copy a present and normal page.
 *
 * NOTE! The usual case is that this isn't required;
 * instead, the caller can just increase the page refcount
 * and re-use the pte the traditional way.
 *
 * And if we need a pre-allocated page but don't yet have
 * one, return a negative error to let the preallocation
 * code know so that it can do so outside the page table
 * lock.
 */
static inline int
copy_present_page(struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma,
		  pte_t *dst_pte, pte_t *src_pte, unsigned long addr, int *rss,
		  struct folio **prealloc, struct page *page)
{
	struct folio *new_folio;
	pte_t pte;

	new_folio = *prealloc;
	if (!new_folio)
		return -EAGAIN;

	/*
	 * We have a prealloc page, all good!  Take it
	 * over and copy the page & arm it.
	 */

	if (copy_mc_user_highpage(&new_folio->page, page, addr, src_vma))
		return -EHWPOISON;

	*prealloc = NULL;
	__folio_mark_uptodate(new_folio);
	folio_add_new_anon_rmap(new_folio, dst_vma, addr, RMAP_EXCLUSIVE);
	folio_add_lru_vma(new_folio, dst_vma);
	rss[MM_ANONPAGES]++;

	/* All done, just insert the new page copy in the child */
	pte = mk_pte(&new_folio->page, dst_vma->vm_page_prot);
	pte = maybe_mkwrite(pte_mkdirty(pte), dst_vma);
	if (userfaultfd_pte_wp(dst_vma, ptep_get(src_pte)))
		/* Uffd-wp needs to be delivered to dest pte as well */
		pte = pte_mkuffd_wp(pte);
	set_pte_at(dst_vma->vm_mm, addr, dst_pte, pte);
	return 0;
}

static __always_inline void __copy_present_ptes(struct vm_area_struct *dst_vma,
		struct vm_area_struct *src_vma, pte_t *dst_pte, pte_t *src_pte,
		pte_t pte, unsigned long addr, int nr)
{
	struct mm_struct *src_mm = src_vma->vm_mm;

	/* If it's a COW mapping, write protect it both processes. */
	if (is_cow_mapping(src_vma->vm_flags) && pte_write(pte)) {
		wrprotect_ptes(src_mm, addr, src_pte, nr);
		pte = pte_wrprotect(pte);
	}

	/* If it's a shared mapping, mark it clean in the child. */
	if (src_vma->vm_flags & VM_SHARED)
		pte = pte_mkclean(pte);
	pte = pte_mkold(pte);

	if (!userfaultfd_wp(dst_vma))
		pte = pte_clear_uffd_wp(pte);

	set_ptes(dst_vma->vm_mm, addr, dst_pte, pte, nr);
}

/*
 * Copy one present PTE, trying to batch-process subsequent PTEs that map
 * consecutive pages of the same folio by copying them as well.
 *
 * Returns -EAGAIN if one preallocated page is required to copy the next PTE.
 * Otherwise, returns the number of copied PTEs (at least 1).
 */
static inline int
copy_present_ptes(struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma,
		 pte_t *dst_pte, pte_t *src_pte, pte_t pte, unsigned long addr,
		 int max_nr, int *rss, struct folio **prealloc)
{
	struct page *page;
	struct folio *folio;
	bool any_writable;
	fpb_t flags = 0;
	int err, nr;

	page = vm_normal_page(src_vma, addr, pte);
	if (unlikely(!page))
		goto copy_pte;

	folio = page_folio(page);

	/*
	 * If we likely have to copy, just don't bother with batching. Make
	 * sure that the common "small folio" case is as fast as possible
	 * by keeping the batching logic separate.
	 */
	if (unlikely(!*prealloc && folio_test_large(folio) && max_nr != 1)) {
		if (src_vma->vm_flags & VM_SHARED)
			flags |= FPB_IGNORE_DIRTY;
		if (!vma_soft_dirty_enabled(src_vma))
			flags |= FPB_IGNORE_SOFT_DIRTY;

		nr = folio_pte_batch(folio, addr, src_pte, pte, max_nr, flags,
				     &any_writable, NULL, NULL);
		folio_ref_add(folio, nr);
		if (folio_test_anon(folio)) {
			if (unlikely(folio_try_dup_anon_rmap_ptes(folio, page,
								  nr, src_vma))) {
				folio_ref_sub(folio, nr);
				return -EAGAIN;
			}
			rss[MM_ANONPAGES] += nr;
			VM_WARN_ON_FOLIO(PageAnonExclusive(page), folio);
		} else {
			folio_dup_file_rmap_ptes(folio, page, nr);
			rss[mm_counter_file(folio)] += nr;
		}
		if (any_writable)
			pte = pte_mkwrite(pte, src_vma);
		__copy_present_ptes(dst_vma, src_vma, dst_pte, src_pte, pte,
				    addr, nr);
		return nr;
	}

	folio_get(folio);
	if (folio_test_anon(folio)) {
		/*
		 * If this page may have been pinned by the parent process,
		 * copy the page immediately for the child so that we'll always
		 * guarantee the pinned page won't be randomly replaced in the
		 * future.
		 */
		if (unlikely(folio_try_dup_anon_rmap_pte(folio, page, src_vma))) {
			/* Page may be pinned, we have to copy. */
			folio_put(folio);
			err = copy_present_page(dst_vma, src_vma, dst_pte, src_pte,
						addr, rss, prealloc, page);
			return err ? err : 1;
		}
		rss[MM_ANONPAGES]++;
		VM_WARN_ON_FOLIO(PageAnonExclusive(page), folio);
	} else {
		folio_dup_file_rmap_pte(folio, page);
		rss[mm_counter_file(folio)]++;
	}

copy_pte:
	__copy_present_ptes(dst_vma, src_vma, dst_pte, src_pte, pte, addr, 1);
	return 1;
}

static inline struct folio *folio_prealloc(struct mm_struct *src_mm,
		struct vm_area_struct *vma, unsigned long addr, bool need_zero)
{
	struct folio *new_folio;

	if (need_zero)
		new_folio = vma_alloc_zeroed_movable_folio(vma, addr);
	else
		new_folio = vma_alloc_folio(GFP_HIGHUSER_MOVABLE, 0, vma, addr);

	if (!new_folio)
		return NULL;

	if (mem_cgroup_charge(new_folio, src_mm, GFP_KERNEL)) {
		folio_put(new_folio);
		return NULL;
	}
	folio_throttle_swaprate(new_folio, GFP_KERNEL);

	return new_folio;
}

static int
copy_pte_range(struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma,
	       pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
	       unsigned long end)
{
	struct mm_struct *dst_mm = dst_vma->vm_mm;
	struct mm_struct *src_mm = src_vma->vm_mm;
	pte_t *orig_src_pte, *orig_dst_pte;
	pte_t *src_pte, *dst_pte;
	pmd_t dummy_pmdval;
	pte_t ptent;
	spinlock_t *src_ptl, *dst_ptl;
	int progress, max_nr, ret = 0;
	int rss[NR_MM_COUNTERS];
	swp_entry_t entry = (swp_entry_t){0};
	struct folio *prealloc = NULL;
	int nr;

again:
	progress = 0;
	init_rss_vec(rss);

	/*
	 * copy_pmd_range()'s prior pmd_none_or_clear_bad(src_pmd), and the
	 * error handling here, assume that exclusive mmap_lock on dst and src
	 * protects anon from unexpected THP transitions; with shmem and file
	 * protected by mmap_lock-less collapse skipping areas with anon_vma
	 * (whereas vma_needs_copy() skips areas without anon_vma).  A rework
	 * can remove such assumptions later, but this is good enough for now.
	 */
	dst_pte = pte_alloc_map_lock(dst_mm, dst_pmd, addr, &dst_ptl);
	if (!dst_pte) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * We already hold the exclusive mmap_lock, the copy_pte_range() and
	 * retract_page_tables() are using vma->anon_vma to be exclusive, so
	 * the PTE page is stable, and there is no need to get pmdval and do
	 * pmd_same() check.
	 */
	src_pte = pte_offset_map_rw_nolock(src_mm, src_pmd, addr, &dummy_pmdval,
					   &src_ptl);
	if (!src_pte) {
		pte_unmap_unlock(dst_pte, dst_ptl);
		/* ret == 0 */
		goto out;
	}
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
	orig_src_pte = src_pte;
	orig_dst_pte = dst_pte;
	arch_enter_lazy_mmu_mode();

	do {
		nr = 1;

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
		ptent = ptep_get(src_pte);
		if (pte_none(ptent)) {
			progress++;
			continue;
		}
		if (unlikely(!pte_present(ptent))) {
			ret = copy_nonpresent_pte(dst_mm, src_mm,
						  dst_pte, src_pte,
						  dst_vma, src_vma,
						  addr, rss);
			if (ret == -EIO) {
				entry = pte_to_swp_entry(ptep_get(src_pte));
				break;
			} else if (ret == -EBUSY) {
				break;
			} else if (!ret) {
				progress += 8;
				continue;
			}
			ptent = ptep_get(src_pte);
			VM_WARN_ON_ONCE(!pte_present(ptent));

			/*
			 * Device exclusive entry restored, continue by copying
			 * the now present pte.
			 */
			WARN_ON_ONCE(ret != -ENOENT);
		}
		/* copy_present_ptes() will clear `*prealloc' if consumed */
		max_nr = (end - addr) / PAGE_SIZE;
		ret = copy_present_ptes(dst_vma, src_vma, dst_pte, src_pte,
					ptent, addr, max_nr, rss, &prealloc);
		/*
		 * If we need a pre-allocated page for this pte, drop the
		 * locks, allocate, and try again.
		 * If copy failed due to hwpoison in source page, break out.
		 */
		if (unlikely(ret == -EAGAIN || ret == -EHWPOISON))
			break;
		if (unlikely(prealloc)) {
			/*
			 * pre-alloc page cannot be reused by next time so as
			 * to strictly follow mempolicy (e.g., alloc_page_vma()
			 * will allocate page according to address).  This
			 * could only happen if one pinned pte changed.
			 */
			folio_put(prealloc);
			prealloc = NULL;
		}
		nr = ret;
		progress += 8 * nr;
	} while (dst_pte += nr, src_pte += nr, addr += PAGE_SIZE * nr,
		 addr != end);

	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(orig_src_pte, src_ptl);
	add_mm_rss_vec(dst_mm, rss);
	pte_unmap_unlock(orig_dst_pte, dst_ptl);
	cond_resched();

	if (ret == -EIO) {
		VM_WARN_ON_ONCE(!entry.val);
		if (add_swap_count_continuation(entry, GFP_KERNEL) < 0) {
			ret = -ENOMEM;
			goto out;
		}
		entry.val = 0;
	} else if (ret == -EBUSY || unlikely(ret == -EHWPOISON)) {
		goto out;
	} else if (ret ==  -EAGAIN) {
		prealloc = folio_prealloc(src_mm, src_vma, addr, false);
		if (!prealloc)
			return -ENOMEM;
	} else if (ret < 0) {
		VM_WARN_ON_ONCE(1);
	}

	/* We've captured and resolved the error. Reset, try again. */
	ret = 0;

	if (addr != end)
		goto again;
out:
	if (unlikely(prealloc))
		folio_put(prealloc);
	return ret;
}

static inline int
copy_pmd_range(struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma,
	       pud_t *dst_pud, pud_t *src_pud, unsigned long addr,
	       unsigned long end)
{
	struct mm_struct *dst_mm = dst_vma->vm_mm;
	struct mm_struct *src_mm = src_vma->vm_mm;
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
			VM_BUG_ON_VMA(next-addr != HPAGE_PMD_SIZE, src_vma);
			err = copy_huge_pmd(dst_mm, src_mm, dst_pmd, src_pmd,
					    addr, dst_vma, src_vma);
			if (err == -ENOMEM)
				return -ENOMEM;
			if (!err)
				continue;
			/* fall through */
		}
		if (pmd_none_or_clear_bad(src_pmd))
			continue;
		if (copy_pte_range(dst_vma, src_vma, dst_pmd, src_pmd,
				   addr, next))
			return -ENOMEM;
	} while (dst_pmd++, src_pmd++, addr = next, addr != end);
	return 0;
}

static inline int
copy_pud_range(struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma,
	       p4d_t *dst_p4d, p4d_t *src_p4d, unsigned long addr,
	       unsigned long end)
{
	struct mm_struct *dst_mm = dst_vma->vm_mm;
	struct mm_struct *src_mm = src_vma->vm_mm;
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

			VM_BUG_ON_VMA(next-addr != HPAGE_PUD_SIZE, src_vma);
			err = copy_huge_pud(dst_mm, src_mm,
					    dst_pud, src_pud, addr, src_vma);
			if (err == -ENOMEM)
				return -ENOMEM;
			if (!err)
				continue;
			/* fall through */
		}
		if (pud_none_or_clear_bad(src_pud))
			continue;
		if (copy_pmd_range(dst_vma, src_vma, dst_pud, src_pud,
				   addr, next))
			return -ENOMEM;
	} while (dst_pud++, src_pud++, addr = next, addr != end);
	return 0;
}

static inline int
copy_p4d_range(struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma,
	       pgd_t *dst_pgd, pgd_t *src_pgd, unsigned long addr,
	       unsigned long end)
{
	struct mm_struct *dst_mm = dst_vma->vm_mm;
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
		if (copy_pud_range(dst_vma, src_vma, dst_p4d, src_p4d,
				   addr, next))
			return -ENOMEM;
	} while (dst_p4d++, src_p4d++, addr = next, addr != end);
	return 0;
}

/*
 * Return true if the vma needs to copy the pgtable during this fork().  Return
 * false when we can speed up fork() by allowing lazy page faults later until
 * when the child accesses the memory range.
 */
static bool
vma_needs_copy(struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma)
{
	/*
	 * Always copy pgtables when dst_vma has uffd-wp enabled even if it's
	 * file-backed (e.g. shmem). Because when uffd-wp is enabled, pgtable
	 * contains uffd-wp protection information, that's something we can't
	 * retrieve from page cache, and skip copying will lose those info.
	 */
	if (userfaultfd_wp(dst_vma))
		return true;

	if (src_vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))
		return true;

	if (src_vma->anon_vma)
		return true;

	/*
	 * Don't copy ptes where a page fault will fill them correctly.  Fork
	 * becomes much lighter when there are big shared or private readonly
	 * mappings. The tradeoff is that copy_page_range is more efficient
	 * than faulting.
	 */
	return false;
}

int
copy_page_range(struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma)
{
	pgd_t *src_pgd, *dst_pgd;
	unsigned long next;
	unsigned long addr = src_vma->vm_start;
	unsigned long end = src_vma->vm_end;
	struct mm_struct *dst_mm = dst_vma->vm_mm;
	struct mm_struct *src_mm = src_vma->vm_mm;
	struct mmu_notifier_range range;
	bool is_cow;
	int ret;

	if (!vma_needs_copy(dst_vma, src_vma))
		return 0;

	if (is_vm_hugetlb_page(src_vma))
		return copy_hugetlb_page_range(dst_mm, src_mm, dst_vma, src_vma);

	if (unlikely(src_vma->vm_flags & VM_PFNMAP)) {
		/*
		 * We do not free on error cases below as remove_vma
		 * gets called on error from higher level routine
		 */
		ret = track_pfn_copy(src_vma);
		if (ret)
			return ret;
	}

	/*
	 * We need to invalidate the secondary MMU mappings only when
	 * there could be a permission downgrade on the ptes of the
	 * parent mm. And a permission downgrade will only happen if
	 * is_cow_mapping() returns true.
	 */
	is_cow = is_cow_mapping(src_vma->vm_flags);

	if (is_cow) {
		mmu_notifier_range_init(&range, MMU_NOTIFY_PROTECTION_PAGE,
					0, src_mm, addr, end);
		mmu_notifier_invalidate_range_start(&range);
		/*
		 * Disabling preemption is not needed for the write side, as
		 * the read side doesn't spin, but goes to the mmap_lock.
		 *
		 * Use the raw variant of the seqcount_t write API to avoid
		 * lockdep complaining about preemptibility.
		 */
		vma_assert_write_locked(src_vma);
		raw_write_seqcount_begin(&src_mm->write_protect_seq);
	}

	ret = 0;
	dst_pgd = pgd_offset(dst_mm, addr);
	src_pgd = pgd_offset(src_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(src_pgd))
			continue;
		if (unlikely(copy_p4d_range(dst_vma, src_vma, dst_pgd, src_pgd,
					    addr, next))) {
			untrack_pfn_clear(dst_vma);
			ret = -ENOMEM;
			break;
		}
	} while (dst_pgd++, src_pgd++, addr = next, addr != end);

	if (is_cow) {
		raw_write_seqcount_end(&src_mm->write_protect_seq);
		mmu_notifier_invalidate_range_end(&range);
	}
	return ret;
}

/* Whether we should zap all COWed (private) pages too */
static inline bool should_zap_cows(struct zap_details *details)
{
	/* By default, zap all pages */
	if (!details || details->reclaim_pt)
		return true;

	/* Or, we zap COWed pages only if the caller wants to */
	return details->even_cows;
}

/* Decides whether we should zap this folio with the folio pointer specified */
static inline bool should_zap_folio(struct zap_details *details,
				    struct folio *folio)
{
	/* If we can make a decision without *folio.. */
	if (should_zap_cows(details))
		return true;

	/* Otherwise we should only zap non-anon folios */
	return !folio_test_anon(folio);
}

static inline bool zap_drop_markers(struct zap_details *details)
{
	if (!details)
		return false;

	return details->zap_flags & ZAP_FLAG_DROP_MARKER;
}

/*
 * This function makes sure that we'll replace the none pte with an uffd-wp
 * swap special pte marker when necessary. Must be with the pgtable lock held.
 *
 * Returns true if uffd-wp ptes was installed, false otherwise.
 */
static inline bool
zap_install_uffd_wp_if_needed(struct vm_area_struct *vma,
			      unsigned long addr, pte_t *pte, int nr,
			      struct zap_details *details, pte_t pteval)
{
	bool was_installed = false;

#ifdef CONFIG_PTE_MARKER_UFFD_WP
	/* Zap on anonymous always means dropping everything */
	if (vma_is_anonymous(vma))
		return false;

	if (zap_drop_markers(details))
		return false;

	for (;;) {
		/* the PFN in the PTE is irrelevant. */
		if (pte_install_uffd_wp_if_needed(vma, addr, pte, pteval))
			was_installed = true;
		if (--nr == 0)
			break;
		pte++;
		addr += PAGE_SIZE;
	}
#endif
	return was_installed;
}

static __always_inline void zap_present_folio_ptes(struct mmu_gather *tlb,
		struct vm_area_struct *vma, struct folio *folio,
		struct page *page, pte_t *pte, pte_t ptent, unsigned int nr,
		unsigned long addr, struct zap_details *details, int *rss,
		bool *force_flush, bool *force_break, bool *any_skipped)
{
	struct mm_struct *mm = tlb->mm;
	bool delay_rmap = false;

	if (!folio_test_anon(folio)) {
		ptent = get_and_clear_full_ptes(mm, addr, pte, nr, tlb->fullmm);
		if (pte_dirty(ptent)) {
			folio_mark_dirty(folio);
			if (tlb_delay_rmap(tlb)) {
				delay_rmap = true;
				*force_flush = true;
			}
		}
		if (pte_young(ptent) && likely(vma_has_recency(vma)))
			folio_mark_accessed(folio);
		rss[mm_counter(folio)] -= nr;
	} else {
		/* We don't need up-to-date accessed/dirty bits. */
		clear_full_ptes(mm, addr, pte, nr, tlb->fullmm);
		rss[MM_ANONPAGES] -= nr;
	}
	/* Checking a single PTE in a batch is sufficient. */
	arch_check_zapped_pte(vma, ptent);
	tlb_remove_tlb_entries(tlb, pte, nr, addr);
	if (unlikely(userfaultfd_pte_wp(vma, ptent)))
		*any_skipped = zap_install_uffd_wp_if_needed(vma, addr, pte,
							     nr, details, ptent);

	if (!delay_rmap) {
		folio_remove_rmap_ptes(folio, page, nr, vma);

		if (unlikely(folio_mapcount(folio) < 0))
			print_bad_pte(vma, addr, ptent, page);
	}
	if (unlikely(__tlb_remove_folio_pages(tlb, page, nr, delay_rmap))) {
		*force_flush = true;
		*force_break = true;
	}
}

/*
 * Zap or skip at least one present PTE, trying to batch-process subsequent
 * PTEs that map consecutive pages of the same folio.
 *
 * Returns the number of processed (skipped or zapped) PTEs (at least 1).
 */
static inline int zap_present_ptes(struct mmu_gather *tlb,
		struct vm_area_struct *vma, pte_t *pte, pte_t ptent,
		unsigned int max_nr, unsigned long addr,
		struct zap_details *details, int *rss, bool *force_flush,
		bool *force_break, bool *any_skipped)
{
	const fpb_t fpb_flags = FPB_IGNORE_DIRTY | FPB_IGNORE_SOFT_DIRTY;
	struct mm_struct *mm = tlb->mm;
	struct folio *folio;
	struct page *page;
	int nr;

	page = vm_normal_page(vma, addr, ptent);
	if (!page) {
		/* We don't need up-to-date accessed/dirty bits. */
		ptep_get_and_clear_full(mm, addr, pte, tlb->fullmm);
		arch_check_zapped_pte(vma, ptent);
		tlb_remove_tlb_entry(tlb, pte, addr);
		if (userfaultfd_pte_wp(vma, ptent))
			*any_skipped = zap_install_uffd_wp_if_needed(vma, addr,
						pte, 1, details, ptent);
		ksm_might_unmap_zero_page(mm, ptent);
		return 1;
	}

	folio = page_folio(page);
	if (unlikely(!should_zap_folio(details, folio))) {
		*any_skipped = true;
		return 1;
	}

	/*
	 * Make sure that the common "small folio" case is as fast as possible
	 * by keeping the batching logic separate.
	 */
	if (unlikely(folio_test_large(folio) && max_nr != 1)) {
		nr = folio_pte_batch(folio, addr, pte, ptent, max_nr, fpb_flags,
				     NULL, NULL, NULL);

		zap_present_folio_ptes(tlb, vma, folio, page, pte, ptent, nr,
				       addr, details, rss, force_flush,
				       force_break, any_skipped);
		return nr;
	}
	zap_present_folio_ptes(tlb, vma, folio, page, pte, ptent, 1, addr,
			       details, rss, force_flush, force_break, any_skipped);
	return 1;
}

static inline int zap_nonpresent_ptes(struct mmu_gather *tlb,
		struct vm_area_struct *vma, pte_t *pte, pte_t ptent,
		unsigned int max_nr, unsigned long addr,
		struct zap_details *details, int *rss, bool *any_skipped)
{
	swp_entry_t entry;
	int nr = 1;

	*any_skipped = true;
	entry = pte_to_swp_entry(ptent);
	if (is_device_private_entry(entry) ||
		is_device_exclusive_entry(entry)) {
		struct page *page = pfn_swap_entry_to_page(entry);
		struct folio *folio = page_folio(page);

		if (unlikely(!should_zap_folio(details, folio)))
			return 1;
		/*
		 * Both device private/exclusive mappings should only
		 * work with anonymous page so far, so we don't need to
		 * consider uffd-wp bit when zap. For more information,
		 * see zap_install_uffd_wp_if_needed().
		 */
		WARN_ON_ONCE(!vma_is_anonymous(vma));
		rss[mm_counter(folio)]--;
		if (is_device_private_entry(entry))
			folio_remove_rmap_pte(folio, page, vma);
		folio_put(folio);
	} else if (!non_swap_entry(entry)) {
		/* Genuine swap entries, hence a private anon pages */
		if (!should_zap_cows(details))
			return 1;

		nr = swap_pte_batch(pte, max_nr, ptent);
		rss[MM_SWAPENTS] -= nr;
		free_swap_and_cache_nr(entry, nr);
	} else if (is_migration_entry(entry)) {
		struct folio *folio = pfn_swap_entry_folio(entry);

		if (!should_zap_folio(details, folio))
			return 1;
		rss[mm_counter(folio)]--;
	} else if (pte_marker_entry_uffd_wp(entry)) {
		/*
		 * For anon: always drop the marker; for file: only
		 * drop the marker if explicitly requested.
		 */
		if (!vma_is_anonymous(vma) && !zap_drop_markers(details))
			return 1;
	} else if (is_guard_swp_entry(entry)) {
		/*
		 * Ordinary zapping should not remove guard PTE
		 * markers. Only do so if we should remove PTE markers
		 * in general.
		 */
		if (!zap_drop_markers(details))
			return 1;
	} else if (is_hwpoison_entry(entry) || is_poisoned_swp_entry(entry)) {
		if (!should_zap_cows(details))
			return 1;
	} else {
		/* We should have covered all the swap entry types */
		pr_alert("unrecognized swap entry 0x%lx\n", entry.val);
		WARN_ON_ONCE(1);
	}
	clear_not_present_full_ptes(vma->vm_mm, addr, pte, nr, tlb->fullmm);
	*any_skipped = zap_install_uffd_wp_if_needed(vma, addr, pte, nr, details, ptent);

	return nr;
}

static inline int do_zap_pte_range(struct mmu_gather *tlb,
				   struct vm_area_struct *vma, pte_t *pte,
				   unsigned long addr, unsigned long end,
				   struct zap_details *details, int *rss,
				   bool *force_flush, bool *force_break,
				   bool *any_skipped)
{
	pte_t ptent = ptep_get(pte);
	int max_nr = (end - addr) / PAGE_SIZE;
	int nr = 0;

	/* Skip all consecutive none ptes */
	if (pte_none(ptent)) {
		for (nr = 1; nr < max_nr; nr++) {
			ptent = ptep_get(pte + nr);
			if (!pte_none(ptent))
				break;
		}
		max_nr -= nr;
		if (!max_nr)
			return nr;
		pte += nr;
		addr += nr * PAGE_SIZE;
	}

	if (pte_present(ptent))
		nr += zap_present_ptes(tlb, vma, pte, ptent, max_nr, addr,
				       details, rss, force_flush, force_break,
				       any_skipped);
	else
		nr += zap_nonpresent_ptes(tlb, vma, pte, ptent, max_nr, addr,
					  details, rss, any_skipped);

	return nr;
}

static unsigned long zap_pte_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	bool force_flush = false, force_break = false;
	struct mm_struct *mm = tlb->mm;
	int rss[NR_MM_COUNTERS];
	spinlock_t *ptl;
	pte_t *start_pte;
	pte_t *pte;
	pmd_t pmdval;
	unsigned long start = addr;
	bool can_reclaim_pt = reclaim_pt_is_enabled(start, end, details);
	bool direct_reclaim = true;
	int nr;

retry:
	tlb_change_page_size(tlb, PAGE_SIZE);
	init_rss_vec(rss);
	start_pte = pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return addr;

	flush_tlb_batched_pending(mm);
	arch_enter_lazy_mmu_mode();
	do {
		bool any_skipped = false;

		if (need_resched()) {
			direct_reclaim = false;
			break;
		}

		nr = do_zap_pte_range(tlb, vma, pte, addr, end, details, rss,
				      &force_flush, &force_break, &any_skipped);
		if (any_skipped)
			can_reclaim_pt = false;
		if (unlikely(force_break)) {
			addr += nr * PAGE_SIZE;
			direct_reclaim = false;
			break;
		}
	} while (pte += nr, addr += PAGE_SIZE * nr, addr != end);

	/*
	 * Fast path: try to hold the pmd lock and unmap the PTE page.
	 *
	 * If the pte lock was released midway (retry case), or if the attempt
	 * to hold the pmd lock failed, then we need to recheck all pte entries
	 * to ensure they are still none, thereby preventing the pte entries
	 * from being repopulated by another thread.
	 */
	if (can_reclaim_pt && direct_reclaim && addr == end)
		direct_reclaim = try_get_and_clear_pmd(mm, pmd, &pmdval);

	add_mm_rss_vec(mm, rss);
	arch_leave_lazy_mmu_mode();

	/* Do the actual TLB flush before dropping ptl */
	if (force_flush) {
		tlb_flush_mmu_tlbonly(tlb);
		tlb_flush_rmaps(tlb, vma);
	}
	pte_unmap_unlock(start_pte, ptl);

	/*
	 * If we forced a TLB flush (either due to running out of
	 * batch buffers or because we needed to flush dirty TLB
	 * entries before releasing the ptl), free the batched
	 * memory too. Come back again if we didn't do everything.
	 */
	if (force_flush)
		tlb_flush_mmu(tlb);

	if (addr != end) {
		cond_resched();
		force_flush = false;
		force_break = false;
		goto retry;
	}

	if (can_reclaim_pt) {
		if (direct_reclaim)
			free_pte(mm, start, tlb, pmdval);
		else
			try_to_free_pte(mm, pmd, start, tlb);
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
			if (next - addr != HPAGE_PMD_SIZE)
				__split_huge_pmd(vma, pmd, addr, false, NULL);
			else if (zap_huge_pmd(tlb, vma, pmd, addr)) {
				addr = next;
				continue;
			}
			/* fall through */
		} else if (details && details->single_folio &&
			   folio_test_pmd_mappable(details->single_folio) &&
			   next - addr == HPAGE_PMD_SIZE && pmd_none(*pmd)) {
			spinlock_t *ptl = pmd_lock(tlb->mm, pmd);
			/*
			 * Take and drop THP pmd lock so that we cannot return
			 * prematurely, while zap_huge_pmd() has cleared *pmd,
			 * but not yet decremented compound_mapcount().
			 */
			spin_unlock(ptl);
		}
		if (pmd_none(*pmd)) {
			addr = next;
			continue;
		}
		addr = zap_pte_range(tlb, vma, pmd, addr, next, details);
		if (addr != next)
			pmd--;
	} while (pmd++, cond_resched(), addr != end);

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
				mmap_assert_locked(tlb->mm);
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
		struct zap_details *details, bool mm_wr_locked)
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
		untrack_pfn(vma, 0, 0, mm_wr_locked);

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
				zap_flags_t zap_flags = details ?
				    details->zap_flags : 0;
				__unmap_hugepage_range(tlb, vma, start, end,
							     NULL, zap_flags);
			}
		} else
			unmap_page_range(tlb, vma, start, end, details);
	}
}

/**
 * unmap_vmas - unmap a range of memory covered by a list of vma's
 * @tlb: address of the caller's struct mmu_gather
 * @mas: the maple state
 * @vma: the starting vma
 * @start_addr: virtual address at which to start unmapping
 * @end_addr: virtual address at which to end unmapping
 * @tree_end: The maximum index to check
 * @mm_wr_locked: lock flag
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
void unmap_vmas(struct mmu_gather *tlb, struct ma_state *mas,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr, unsigned long tree_end,
		bool mm_wr_locked)
{
	struct mmu_notifier_range range;
	struct zap_details details = {
		.zap_flags = ZAP_FLAG_DROP_MARKER | ZAP_FLAG_UNMAP,
		/* Careful - we need to zap private pages too! */
		.even_cows = true,
	};

	mmu_notifier_range_init(&range, MMU_NOTIFY_UNMAP, 0, vma->vm_mm,
				start_addr, end_addr);
	mmu_notifier_invalidate_range_start(&range);
	do {
		unsigned long start = start_addr;
		unsigned long end = end_addr;
		hugetlb_zap_begin(vma, &start, &end);
		unmap_single_vma(tlb, vma, start, end, &details,
				 mm_wr_locked);
		hugetlb_zap_end(vma, &details);
		vma = mas_find(mas, tree_end - 1);
	} while (vma && likely(!xa_is_zero(vma)));
	mmu_notifier_invalidate_range_end(&range);
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
void zap_page_range_single(struct vm_area_struct *vma, unsigned long address,
		unsigned long size, struct zap_details *details)
{
	const unsigned long end = address + size;
	struct mmu_notifier_range range;
	struct mmu_gather tlb;

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma->vm_mm,
				address, end);
	hugetlb_zap_begin(vma, &range.start, &range.end);
	tlb_gather_mmu(&tlb, vma->vm_mm);
	update_hiwater_rss(vma->vm_mm);
	mmu_notifier_invalidate_range_start(&range);
	/*
	 * unmap 'address-end' not 'range.start-range.end' as range
	 * could have been expanded for hugetlb pmd sharing.
	 */
	unmap_single_vma(&tlb, vma, address, end, details, false);
	mmu_notifier_invalidate_range_end(&range);
	tlb_finish_mmu(&tlb);
	hugetlb_zap_end(vma, details);
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
 */
void zap_vma_ptes(struct vm_area_struct *vma, unsigned long address,
		unsigned long size)
{
	if (!range_in_vma(vma, address, address + size) ||
	    		!(vma->vm_flags & VM_PFNMAP))
		return;

	zap_page_range_single(vma, address, size, NULL);
}
EXPORT_SYMBOL_GPL(zap_vma_ptes);

static pmd_t *walk_to_pmd(struct mm_struct *mm, unsigned long addr)
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
	return pmd;
}

pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			spinlock_t **ptl)
{
	pmd_t *pmd = walk_to_pmd(mm, addr);

	if (!pmd)
		return NULL;
	return pte_alloc_map_lock(mm, pmd, addr, ptl);
}

static bool vm_mixed_zeropage_allowed(struct vm_area_struct *vma)
{
	VM_WARN_ON_ONCE(vma->vm_flags & VM_PFNMAP);
	/*
	 * Whoever wants to forbid the zeropage after some zeropages
	 * might already have been mapped has to scan the page tables and
	 * bail out on any zeropages. Zeropages in COW mappings can
	 * be unshared using FAULT_FLAG_UNSHARE faults.
	 */
	if (mm_forbids_zeropage(vma->vm_mm))
		return false;
	/* zeropages in COW mappings are common and unproblematic. */
	if (is_cow_mapping(vma->vm_flags))
		return true;
	/* Mappings that do not allow for writable PTEs are unproblematic. */
	if (!(vma->vm_flags & (VM_WRITE | VM_MAYWRITE)))
		return true;
	/*
	 * Why not allow any VMA that has vm_ops->pfn_mkwrite? GUP could
	 * find the shared zeropage and longterm-pin it, which would
	 * be problematic as soon as the zeropage gets replaced by a different
	 * page due to vma->vm_ops->pfn_mkwrite, because what's mapped would
	 * now differ to what GUP looked up. FSDAX is incompatible to
	 * FOLL_LONGTERM and VM_IO is incompatible to GUP completely (see
	 * check_vma_flags).
	 */
	return vma->vm_ops && vma->vm_ops->pfn_mkwrite &&
	       (vma_is_fsdax(vma) || vma->vm_flags & VM_IO);
}

static int validate_page_before_insert(struct vm_area_struct *vma,
				       struct page *page)
{
	struct folio *folio = page_folio(page);

	if (!folio_ref_count(folio))
		return -EINVAL;
	if (unlikely(is_zero_folio(folio))) {
		if (!vm_mixed_zeropage_allowed(vma))
			return -EINVAL;
		return 0;
	}
	if (folio_test_anon(folio) || folio_test_slab(folio) ||
	    page_has_type(page))
		return -EINVAL;
	flush_dcache_folio(folio);
	return 0;
}

static int insert_page_into_pte_locked(struct vm_area_struct *vma, pte_t *pte,
			unsigned long addr, struct page *page, pgprot_t prot)
{
	struct folio *folio = page_folio(page);
	pte_t pteval;

	if (!pte_none(ptep_get(pte)))
		return -EBUSY;
	/* Ok, finally just insert the thing.. */
	pteval = mk_pte(page, prot);
	if (unlikely(is_zero_folio(folio))) {
		pteval = pte_mkspecial(pteval);
	} else {
		folio_get(folio);
		inc_mm_counter(vma->vm_mm, mm_counter_file(folio));
		folio_add_file_rmap_pte(folio, page, vma);
	}
	set_pte_at(vma->vm_mm, addr, pte, pteval);
	return 0;
}

static int insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page, pgprot_t prot)
{
	int retval;
	pte_t *pte;
	spinlock_t *ptl;

	retval = validate_page_before_insert(vma, page);
	if (retval)
		goto out;
	retval = -ENOMEM;
	pte = get_locked_pte(vma->vm_mm, addr, &ptl);
	if (!pte)
		goto out;
	retval = insert_page_into_pte_locked(vma, pte, addr, page, prot);
	pte_unmap_unlock(pte, ptl);
out:
	return retval;
}

static int insert_page_in_batch_locked(struct vm_area_struct *vma, pte_t *pte,
			unsigned long addr, struct page *page, pgprot_t prot)
{
	int err;

	err = validate_page_before_insert(vma, page);
	if (err)
		return err;
	return insert_page_into_pte_locked(vma, pte, addr, page, prot);
}

/* insert_pages() amortizes the cost of spinlock operations
 * when inserting pages in a loop.
 */
static int insert_pages(struct vm_area_struct *vma, unsigned long addr,
			struct page **pages, unsigned long *num, pgprot_t prot)
{
	pmd_t *pmd = NULL;
	pte_t *start_pte, *pte;
	spinlock_t *pte_lock;
	struct mm_struct *const mm = vma->vm_mm;
	unsigned long curr_page_idx = 0;
	unsigned long remaining_pages_total = *num;
	unsigned long pages_to_write_in_pmd;
	int ret;
more:
	ret = -EFAULT;
	pmd = walk_to_pmd(mm, addr);
	if (!pmd)
		goto out;

	pages_to_write_in_pmd = min_t(unsigned long,
		remaining_pages_total, PTRS_PER_PTE - pte_index(addr));

	/* Allocate the PTE if necessary; takes PMD lock once only. */
	ret = -ENOMEM;
	if (pte_alloc(mm, pmd))
		goto out;

	while (pages_to_write_in_pmd) {
		int pte_idx = 0;
		const int batch_size = min_t(int, pages_to_write_in_pmd, 8);

		start_pte = pte_offset_map_lock(mm, pmd, addr, &pte_lock);
		if (!start_pte) {
			ret = -EFAULT;
			goto out;
		}
		for (pte = start_pte; pte_idx < batch_size; ++pte, ++pte_idx) {
			int err = insert_page_in_batch_locked(vma, pte,
				addr, pages[curr_page_idx], prot);
			if (unlikely(err)) {
				pte_unmap_unlock(start_pte, pte_lock);
				ret = err;
				remaining_pages_total -= pte_idx;
				goto out;
			}
			addr += PAGE_SIZE;
			++curr_page_idx;
		}
		pte_unmap_unlock(start_pte, pte_lock);
		pages_to_write_in_pmd -= batch_size;
		remaining_pages_total -= batch_size;
	}
	if (remaining_pages_total)
		goto more;
	ret = 0;
out:
	*num = remaining_pages_total;
	return ret;
}

/**
 * vm_insert_pages - insert multiple pages into user vma, batching the pmd lock.
 * @vma: user vma to map to
 * @addr: target start user address of these pages
 * @pages: source kernel pages
 * @num: in: number of pages to map. out: number of pages that were *not*
 * mapped. (0 means all pages were successfully mapped).
 *
 * Preferred over vm_insert_page() when inserting multiple pages.
 *
 * In case of error, we may have mapped a subset of the provided
 * pages. It is the caller's responsibility to account for this case.
 *
 * The same restrictions apply as in vm_insert_page().
 */
int vm_insert_pages(struct vm_area_struct *vma, unsigned long addr,
			struct page **pages, unsigned long *num)
{
	const unsigned long end_addr = addr + (*num * PAGE_SIZE) - 1;

	if (addr < vma->vm_start || end_addr >= vma->vm_end)
		return -EFAULT;
	if (!(vma->vm_flags & VM_MIXEDMAP)) {
		BUG_ON(mmap_read_trylock(vma->vm_mm));
		BUG_ON(vma->vm_flags & VM_PFNMAP);
		vm_flags_set(vma, VM_MIXEDMAP);
	}
	/* Defer page refcount checking till we're about to map that page. */
	return insert_pages(vma, addr, pages, num, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_insert_pages);

/**
 * vm_insert_page - insert single page into user vma
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @page: source kernel page
 *
 * This allows drivers to insert individual pages they've allocated
 * into a user vma. The zeropage is supported in some VMAs,
 * see vm_mixed_zeropage_allowed().
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
 * under mm->mmap_lock write-lock, so it can change vma->vm_flags.
 * Caller must set VM_MIXEDMAP on vma if it wants to call this
 * function from other places, for example from page-fault handler.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int vm_insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page)
{
	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;
	if (!(vma->vm_flags & VM_MIXEDMAP)) {
		BUG_ON(mmap_read_trylock(vma->vm_mm));
		BUG_ON(vma->vm_flags & VM_PFNMAP);
		vm_flags_set(vma, VM_MIXEDMAP);
	}
	return insert_page(vma, addr, page, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_insert_page);

/*
 * __vm_map_pages - maps range of kernel pages into user vma
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 * @offset: user's requested vm_pgoff
 *
 * This allows drivers to map range of kernel pages into a user vma.
 * The zeropage is supported in some VMAs, see
 * vm_mixed_zeropage_allowed().
 *
 * Return: 0 on success and error code otherwise.
 */
static int __vm_map_pages(struct vm_area_struct *vma, struct page **pages,
				unsigned long num, unsigned long offset)
{
	unsigned long count = vma_pages(vma);
	unsigned long uaddr = vma->vm_start;
	int ret, i;

	/* Fail if the user requested offset is beyond the end of the object */
	if (offset >= num)
		return -ENXIO;

	/* Fail if the user requested size exceeds available object size */
	if (count > num - offset)
		return -ENXIO;

	for (i = 0; i < count; i++) {
		ret = vm_insert_page(vma, uaddr, pages[offset + i]);
		if (ret < 0)
			return ret;
		uaddr += PAGE_SIZE;
	}

	return 0;
}

/**
 * vm_map_pages - maps range of kernel pages starts with non zero offset
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 *
 * Maps an object consisting of @num pages, catering for the user's
 * requested vm_pgoff
 *
 * If we fail to insert any page into the vma, the function will return
 * immediately leaving any previously inserted pages present.  Callers
 * from the mmap handler may immediately return the error as their caller
 * will destroy the vma, removing any successfully inserted pages. Other
 * callers should make their own arrangements for calling unmap_region().
 *
 * Context: Process context. Called by mmap handlers.
 * Return: 0 on success and error code otherwise.
 */
int vm_map_pages(struct vm_area_struct *vma, struct page **pages,
				unsigned long num)
{
	return __vm_map_pages(vma, pages, num, vma->vm_pgoff);
}
EXPORT_SYMBOL(vm_map_pages);

/**
 * vm_map_pages_zero - map range of kernel pages starts with zero offset
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 *
 * Similar to vm_map_pages(), except that it explicitly sets the offset
 * to 0. This function is intended for the drivers that did not consider
 * vm_pgoff.
 *
 * Context: Process context. Called by mmap handlers.
 * Return: 0 on success and error code otherwise.
 */
int vm_map_pages_zero(struct vm_area_struct *vma, struct page **pages,
				unsigned long num)
{
	return __vm_map_pages(vma, pages, num, 0);
}
EXPORT_SYMBOL(vm_map_pages_zero);

static vm_fault_t insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn, pgprot_t prot, bool mkwrite)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t *pte, entry;
	spinlock_t *ptl;

	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		return VM_FAULT_OOM;
	entry = ptep_get(pte);
	if (!pte_none(entry)) {
		if (mkwrite) {
			/*
			 * For read faults on private mappings the PFN passed
			 * in may not match the PFN we have mapped if the
			 * mapped PFN is a writeable COW page.  In the mkwrite
			 * case we are creating a writable PTE for a shared
			 * mapping and we expect the PFNs to match. If they
			 * don't match, we are likely racing with block
			 * allocation and mapping invalidation so just skip the
			 * update.
			 */
			if (pte_pfn(entry) != pfn_t_to_pfn(pfn)) {
				WARN_ON_ONCE(!is_zero_pfn(pte_pfn(entry)));
				goto out_unlock;
			}
			entry = pte_mkyoung(entry);
			entry = maybe_mkwrite(pte_mkdirty(entry), vma);
			if (ptep_set_access_flags(vma, addr, pte, entry, 1))
				update_mmu_cache(vma, addr, pte);
		}
		goto out_unlock;
	}

	/* Ok, finally just insert the thing.. */
	if (pfn_t_devmap(pfn))
		entry = pte_mkdevmap(pfn_t_pte(pfn, prot));
	else
		entry = pte_mkspecial(pfn_t_pte(pfn, prot));

	if (mkwrite) {
		entry = pte_mkyoung(entry);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	}

	set_pte_at(mm, addr, pte, entry);
	update_mmu_cache(vma, addr, pte); /* XXX: why not for insert_page? */

out_unlock:
	pte_unmap_unlock(pte, ptl);
	return VM_FAULT_NOPAGE;
}

/**
 * vmf_insert_pfn_prot - insert single pfn into user vma with specified pgprot
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @pfn: source kernel pfn
 * @pgprot: pgprot flags for the inserted page
 *
 * This is exactly like vmf_insert_pfn(), except that it allows drivers
 * to override pgprot on a per-page basis.
 *
 * This only makes sense for IO mappings, and it makes no sense for
 * COW mappings.  In general, using multiple vmas is preferable;
 * vmf_insert_pfn_prot should only be used if using multiple VMAs is
 * impractical.
 *
 * pgprot typically only differs from @vma->vm_page_prot when drivers set
 * caching- and encryption bits different than those of @vma->vm_page_prot,
 * because the caching- or encryption mode may not be known at mmap() time.
 *
 * This is ok as long as @vma->vm_page_prot is not used by the core vm
 * to set caching and encryption bits for those vmas (except for COW pages).
 * This is ensured by core vm only modifying these page table entries using
 * functions that don't touch caching- or encryption bits, using pte_modify()
 * if needed. (See for example mprotect()).
 *
 * Also when new page-table entries are created, this is only done using the
 * fault() callback, and never using the value of vma->vm_page_prot,
 * except for page-table entries that point to anonymous pages as the result
 * of COW.
 *
 * Context: Process context.  May allocate using %GFP_KERNEL.
 * Return: vm_fault_t value.
 */
vm_fault_t vmf_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn, pgprot_t pgprot)
{
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
		return VM_FAULT_SIGBUS;

	if (!pfn_modify_allowed(pfn, pgprot))
		return VM_FAULT_SIGBUS;

	track_pfn_insert(vma, &pgprot, __pfn_to_pfn_t(pfn, PFN_DEV));

	return insert_pfn(vma, addr, __pfn_to_pfn_t(pfn, PFN_DEV), pgprot,
			false);
}
EXPORT_SYMBOL(vmf_insert_pfn_prot);

/**
 * vmf_insert_pfn - insert single pfn into user vma
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @pfn: source kernel pfn
 *
 * Similar to vm_insert_page, this allows drivers to insert individual pages
 * they've allocated into a user vma. Same comments apply.
 *
 * This function should only be called from a vm_ops->fault handler, and
 * in that case the handler should return the result of this function.
 *
 * vma cannot be a COW mapping.
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 *
 * Context: Process context.  May allocate using %GFP_KERNEL.
 * Return: vm_fault_t value.
 */
vm_fault_t vmf_insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn)
{
	return vmf_insert_pfn_prot(vma, addr, pfn, vma->vm_page_prot);
}
EXPORT_SYMBOL(vmf_insert_pfn);

static bool vm_mixed_ok(struct vm_area_struct *vma, pfn_t pfn, bool mkwrite)
{
	if (unlikely(is_zero_pfn(pfn_t_to_pfn(pfn))) &&
	    (mkwrite || !vm_mixed_zeropage_allowed(vma)))
		return false;
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

static vm_fault_t __vm_insert_mixed(struct vm_area_struct *vma,
		unsigned long addr, pfn_t pfn, bool mkwrite)
{
	pgprot_t pgprot = vma->vm_page_prot;
	int err;

	if (!vm_mixed_ok(vma, pfn, mkwrite))
		return VM_FAULT_SIGBUS;

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;

	track_pfn_insert(vma, &pgprot, pfn);

	if (!pfn_modify_allowed(pfn_t_to_pfn(pfn), pgprot))
		return VM_FAULT_SIGBUS;

	/*
	 * If we don't have pte special, then we have to use the pfn_valid()
	 * based VM_MIXEDMAP scheme (see vm_normal_page), and thus we *must*
	 * refcount the page if pfn_valid is true (hence insert_page rather
	 * than insert_pfn).  If a zero_pfn were inserted into a VM_MIXEDMAP
	 * without pte special, it would there be refcounted as a normal page.
	 */
	if (!IS_ENABLED(CONFIG_ARCH_HAS_PTE_SPECIAL) &&
	    !pfn_t_devmap(pfn) && pfn_t_valid(pfn)) {
		struct page *page;

		/*
		 * At this point we are committed to insert_page()
		 * regardless of whether the caller specified flags that
		 * result in pfn_t_has_page() == false.
		 */
		page = pfn_to_page(pfn_t_to_pfn(pfn));
		err = insert_page(vma, addr, page, pgprot);
	} else {
		return insert_pfn(vma, addr, pfn, pgprot, mkwrite);
	}

	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err < 0 && err != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

vm_fault_t vmf_insert_mixed(struct vm_area_struct *vma, unsigned long addr,
		pfn_t pfn)
{
	return __vm_insert_mixed(vma, addr, pfn, false);
}
EXPORT_SYMBOL(vmf_insert_mixed);

/*
 *  If the insertion of PTE failed because someone else already added a
 *  different entry in the mean time, we treat that as success as we assume
 *  the same entry was actually inserted.
 */
vm_fault_t vmf_insert_mixed_mkwrite(struct vm_area_struct *vma,
		unsigned long addr, pfn_t pfn)
{
	return __vm_insert_mixed(vma, addr, pfn, true);
}

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 */
static int remap_pte_range(struct mm_struct *mm, pmd_t *pmd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pte_t *pte, *mapped_pte;
	spinlock_t *ptl;
	int err = 0;

	mapped_pte = pte = pte_alloc_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return -ENOMEM;
	arch_enter_lazy_mmu_mode();
	do {
		BUG_ON(!pte_none(ptep_get(pte)));
		if (!pfn_modify_allowed(pfn, prot)) {
			err = -EACCES;
			break;
		}
		set_pte_at(mm, addr, pte, pte_mkspecial(pfn_pte(pfn, prot)));
		pfn++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(mapped_pte, ptl);
	return err;
}

static inline int remap_pmd_range(struct mm_struct *mm, pud_t *pud,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pmd_t *pmd;
	unsigned long next;
	int err;

	pfn -= addr >> PAGE_SHIFT;
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	VM_BUG_ON(pmd_trans_huge(*pmd));
	do {
		next = pmd_addr_end(addr, end);
		err = remap_pte_range(mm, pmd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int remap_pud_range(struct mm_struct *mm, p4d_t *p4d,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pud_t *pud;
	unsigned long next;
	int err;

	pfn -= addr >> PAGE_SHIFT;
	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		err = remap_pmd_range(mm, pud, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static inline int remap_p4d_range(struct mm_struct *mm, pgd_t *pgd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	p4d_t *p4d;
	unsigned long next;
	int err;

	pfn -= addr >> PAGE_SHIFT;
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return -ENOMEM;
	do {
		next = p4d_addr_end(addr, end);
		err = remap_pud_range(mm, p4d, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (p4d++, addr = next, addr != end);
	return 0;
}

static int remap_pfn_range_internal(struct vm_area_struct *vma, unsigned long addr,
		unsigned long pfn, unsigned long size, pgprot_t prot)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long end = addr + PAGE_ALIGN(size);
	struct mm_struct *mm = vma->vm_mm;
	int err;

	if (WARN_ON_ONCE(!PAGE_ALIGNED(addr)))
		return -EINVAL;

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

	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);

	BUG_ON(addr >= end);
	pfn -= addr >> PAGE_SHIFT;
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		err = remap_p4d_range(mm, pgd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (pgd++, addr = next, addr != end);

	return 0;
}

/*
 * Variant of remap_pfn_range that does not call track_pfn_remap.  The caller
 * must have pre-validated the caching bits of the pgprot_t.
 */
int remap_pfn_range_notrack(struct vm_area_struct *vma, unsigned long addr,
		unsigned long pfn, unsigned long size, pgprot_t prot)
{
	int error = remap_pfn_range_internal(vma, addr, pfn, size, prot);

	if (!error)
		return 0;

	/*
	 * A partial pfn range mapping is dangerous: it does not
	 * maintain page reference counts, and callers may free
	 * pages due to the error. So zap it early.
	 */
	zap_page_range_single(vma, addr, size, NULL);
	return error;
}

/**
 * remap_pfn_range - remap kernel memory to userspace
 * @vma: user vma to map to
 * @addr: target page aligned user address to start at
 * @pfn: page frame number of kernel physical memory address
 * @size: size of mapping area
 * @prot: page protection flags for this mapping
 *
 * Note: this is only safe if the mm semaphore is held when called.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
		    unsigned long pfn, unsigned long size, pgprot_t prot)
{
	int err;

	err = track_pfn_remap(vma, &prot, pfn, addr, PAGE_ALIGN(size));
	if (err)
		return -EINVAL;

	err = remap_pfn_range_notrack(vma, addr, pfn, size, prot);
	if (err)
		untrack_pfn(vma, pfn, PAGE_ALIGN(size), true);
	return err;
}
EXPORT_SYMBOL(remap_pfn_range);

/**
 * vm_iomap_memory - remap memory to userspace
 * @vma: user vma to map to
 * @start: start of the physical memory to be mapped
 * @len: size of area
 *
 * This is a simplified io_remap_pfn_range() for common driver use. The
 * driver just needs to give us the physical memory range to be mapped,
 * we'll figure out the rest from the vma information.
 *
 * NOTE! Some drivers might want to tweak vma->vm_page_prot first to get
 * whatever write-combining details or similar.
 *
 * Return: %0 on success, negative error code otherwise.
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
				     pte_fn_t fn, void *data, bool create,
				     pgtbl_mod_mask *mask)
{
	pte_t *pte, *mapped_pte;
	int err = 0;
	spinlock_t *ptl;

	if (create) {
		mapped_pte = pte = (mm == &init_mm) ?
			pte_alloc_kernel_track(pmd, addr, mask) :
			pte_alloc_map_lock(mm, pmd, addr, &ptl);
		if (!pte)
			return -ENOMEM;
	} else {
		mapped_pte = pte = (mm == &init_mm) ?
			pte_offset_kernel(pmd, addr) :
			pte_offset_map_lock(mm, pmd, addr, &ptl);
		if (!pte)
			return -EINVAL;
	}

	arch_enter_lazy_mmu_mode();

	if (fn) {
		do {
			if (create || !pte_none(ptep_get(pte))) {
				err = fn(pte++, addr, data);
				if (err)
					break;
			}
		} while (addr += PAGE_SIZE, addr != end);
	}
	*mask |= PGTBL_PTE_MODIFIED;

	arch_leave_lazy_mmu_mode();

	if (mm != &init_mm)
		pte_unmap_unlock(mapped_pte, ptl);
	return err;
}

static int apply_to_pmd_range(struct mm_struct *mm, pud_t *pud,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data, bool create,
				     pgtbl_mod_mask *mask)
{
	pmd_t *pmd;
	unsigned long next;
	int err = 0;

	BUG_ON(pud_leaf(*pud));

	if (create) {
		pmd = pmd_alloc_track(mm, pud, addr, mask);
		if (!pmd)
			return -ENOMEM;
	} else {
		pmd = pmd_offset(pud, addr);
	}
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd) && !create)
			continue;
		if (WARN_ON_ONCE(pmd_leaf(*pmd)))
			return -EINVAL;
		if (!pmd_none(*pmd) && WARN_ON_ONCE(pmd_bad(*pmd))) {
			if (!create)
				continue;
			pmd_clear_bad(pmd);
		}
		err = apply_to_pte_range(mm, pmd, addr, next,
					 fn, data, create, mask);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);

	return err;
}

static int apply_to_pud_range(struct mm_struct *mm, p4d_t *p4d,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data, bool create,
				     pgtbl_mod_mask *mask)
{
	pud_t *pud;
	unsigned long next;
	int err = 0;

	if (create) {
		pud = pud_alloc_track(mm, p4d, addr, mask);
		if (!pud)
			return -ENOMEM;
	} else {
		pud = pud_offset(p4d, addr);
	}
	do {
		next = pud_addr_end(addr, end);
		if (pud_none(*pud) && !create)
			continue;
		if (WARN_ON_ONCE(pud_leaf(*pud)))
			return -EINVAL;
		if (!pud_none(*pud) && WARN_ON_ONCE(pud_bad(*pud))) {
			if (!create)
				continue;
			pud_clear_bad(pud);
		}
		err = apply_to_pmd_range(mm, pud, addr, next,
					 fn, data, create, mask);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);

	return err;
}

static int apply_to_p4d_range(struct mm_struct *mm, pgd_t *pgd,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data, bool create,
				     pgtbl_mod_mask *mask)
{
	p4d_t *p4d;
	unsigned long next;
	int err = 0;

	if (create) {
		p4d = p4d_alloc_track(mm, pgd, addr, mask);
		if (!p4d)
			return -ENOMEM;
	} else {
		p4d = p4d_offset(pgd, addr);
	}
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none(*p4d) && !create)
			continue;
		if (WARN_ON_ONCE(p4d_leaf(*p4d)))
			return -EINVAL;
		if (!p4d_none(*p4d) && WARN_ON_ONCE(p4d_bad(*p4d))) {
			if (!create)
				continue;
			p4d_clear_bad(p4d);
		}
		err = apply_to_pud_range(mm, p4d, addr, next,
					 fn, data, create, mask);
		if (err)
			break;
	} while (p4d++, addr = next, addr != end);

	return err;
}

static int __apply_to_page_range(struct mm_struct *mm, unsigned long addr,
				 unsigned long size, pte_fn_t fn,
				 void *data, bool create)
{
	pgd_t *pgd;
	unsigned long start = addr, next;
	unsigned long end = addr + size;
	pgtbl_mod_mask mask = 0;
	int err = 0;

	if (WARN_ON(addr >= end))
		return -EINVAL;

	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none(*pgd) && !create)
			continue;
		if (WARN_ON_ONCE(pgd_leaf(*pgd))) {
			err = -EINVAL;
			break;
		}
		if (!pgd_none(*pgd) && WARN_ON_ONCE(pgd_bad(*pgd))) {
			if (!create)
				continue;
			pgd_clear_bad(pgd);
		}
		err = apply_to_p4d_range(mm, pgd, addr, next,
					 fn, data, create, &mask);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	if (mask & ARCH_PAGE_TABLE_SYNC_MASK)
		arch_sync_kernel_mappings(start, start + size);

	return err;
}

/*
 * Scan a region of virtual memory, filling in page tables as necessary
 * and calling a provided function on each leaf page table.
 */
int apply_to_page_range(struct mm_struct *mm, unsigned long addr,
			unsigned long size, pte_fn_t fn, void *data)
{
	return __apply_to_page_range(mm, addr, size, fn, data, true);
}
EXPORT_SYMBOL_GPL(apply_to_page_range);

/*
 * Scan a region of virtual memory, calling a provided function on
 * each leaf page table where it exists.
 *
 * Unlike apply_to_page_range, this does _not_ fill in page tables
 * where they are absent.
 */
int apply_to_existing_page_range(struct mm_struct *mm, unsigned long addr,
				 unsigned long size, pte_fn_t fn, void *data)
{
	return __apply_to_page_range(mm, addr, size, fn, data, false);
}

/*
 * handle_pte_fault chooses page fault handler according to an entry which was
 * read non-atomically.  Before making any commitment, on those architectures
 * or configurations (e.g. i386 with PAE) which might give a mix of unmatched
 * parts, do_swap_page must check under lock before unmapping the pte and
 * proceeding (but do_wp_page is only called after already making such a check;
 * and do_anonymous_page can safely check later on).
 */
static inline int pte_unmap_same(struct vm_fault *vmf)
{
	int same = 1;
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPTION)
	if (sizeof(pte_t) > sizeof(unsigned long)) {
		spin_lock(vmf->ptl);
		same = pte_same(ptep_get(vmf->pte), vmf->orig_pte);
		spin_unlock(vmf->ptl);
	}
#endif
	pte_unmap(vmf->pte);
	vmf->pte = NULL;
	return same;
}

/*
 * Return:
 *	0:		copied succeeded
 *	-EHWPOISON:	copy failed due to hwpoison in source page
 *	-EAGAIN:	copied failed (some other reason)
 */
static inline int __wp_page_copy_user(struct page *dst, struct page *src,
				      struct vm_fault *vmf)
{
	int ret;
	void *kaddr;
	void __user *uaddr;
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr = vmf->address;

	if (likely(src)) {
		if (copy_mc_user_highpage(dst, src, addr, vma))
			return -EHWPOISON;
		return 0;
	}

	/*
	 * If the source page was a PFN mapping, we don't have
	 * a "struct page" for it. We do a best-effort copy by
	 * just copying from the original user address. If that
	 * fails, we just zero-fill it. Live with it.
	 */
	kaddr = kmap_local_page(dst);
	pagefault_disable();
	uaddr = (void __user *)(addr & PAGE_MASK);

	/*
	 * On architectures with software "accessed" bits, we would
	 * take a double page fault, so mark it accessed here.
	 */
	vmf->pte = NULL;
	if (!arch_has_hw_pte_young() && !pte_young(vmf->orig_pte)) {
		pte_t entry;

		vmf->pte = pte_offset_map_lock(mm, vmf->pmd, addr, &vmf->ptl);
		if (unlikely(!vmf->pte || !pte_same(ptep_get(vmf->pte), vmf->orig_pte))) {
			/*
			 * Other thread has already handled the fault
			 * and update local tlb only
			 */
			if (vmf->pte)
				update_mmu_tlb(vma, addr, vmf->pte);
			ret = -EAGAIN;
			goto pte_unlock;
		}

		entry = pte_mkyoung(vmf->orig_pte);
		if (ptep_set_access_flags(vma, addr, vmf->pte, entry, 0))
			update_mmu_cache_range(vmf, vma, addr, vmf->pte, 1);
	}

	/*
	 * This really shouldn't fail, because the page is there
	 * in the page tables. But it might just be unreadable,
	 * in which case we just give up and fill the result with
	 * zeroes.
	 */
	if (__copy_from_user_inatomic(kaddr, uaddr, PAGE_SIZE)) {
		if (vmf->pte)
			goto warn;

		/* Re-validate under PTL if the page is still mapped */
		vmf->pte = pte_offset_map_lock(mm, vmf->pmd, addr, &vmf->ptl);
		if (unlikely(!vmf->pte || !pte_same(ptep_get(vmf->pte), vmf->orig_pte))) {
			/* The PTE changed under us, update local tlb */
			if (vmf->pte)
				update_mmu_tlb(vma, addr, vmf->pte);
			ret = -EAGAIN;
			goto pte_unlock;
		}

		/*
		 * The same page can be mapped back since last copy attempt.
		 * Try to copy again under PTL.
		 */
		if (__copy_from_user_inatomic(kaddr, uaddr, PAGE_SIZE)) {
			/*
			 * Give a warn in case there can be some obscure
			 * use-case
			 */
warn:
			WARN_ON_ONCE(1);
			clear_page(kaddr);
		}
	}

	ret = 0;

pte_unlock:
	if (vmf->pte)
		pte_unmap_unlock(vmf->pte, vmf->ptl);
	pagefault_enable();
	kunmap_local(kaddr);
	flush_dcache_page(dst);

	return ret;
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
static vm_fault_t do_page_mkwrite(struct vm_fault *vmf, struct folio *folio)
{
	vm_fault_t ret;
	unsigned int old_flags = vmf->flags;

	vmf->flags = FAULT_FLAG_WRITE|FAULT_FLAG_MKWRITE;

	if (vmf->vma->vm_file &&
	    IS_SWAPFILE(vmf->vma->vm_file->f_mapping->host))
		return VM_FAULT_SIGBUS;

	ret = vmf->vma->vm_ops->page_mkwrite(vmf);
	/* Restore original flags so that caller is not surprised */
	vmf->flags = old_flags;
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))
		return ret;
	if (unlikely(!(ret & VM_FAULT_LOCKED))) {
		folio_lock(folio);
		if (!folio->mapping) {
			folio_unlock(folio);
			return 0; /* retry */
		}
		ret |= VM_FAULT_LOCKED;
	} else
		VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	return ret;
}

/*
 * Handle dirtying of a page in shared file mapping on a write fault.
 *
 * The function expects the page to be locked and unlocks it.
 */
static vm_fault_t fault_dirty_shared_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct address_space *mapping;
	struct folio *folio = page_folio(vmf->page);
	bool dirtied;
	bool page_mkwrite = vma->vm_ops && vma->vm_ops->page_mkwrite;

	dirtied = folio_mark_dirty(folio);
	VM_BUG_ON_FOLIO(folio_test_anon(folio), folio);
	/*
	 * Take a local copy of the address_space - folio.mapping may be zeroed
	 * by truncate after folio_unlock().   The address_space itself remains
	 * pinned by vma->vm_file's reference.  We rely on folio_unlock()'s
	 * release semantics to prevent the compiler from undoing this copying.
	 */
	mapping = folio_raw_mapping(folio);
	folio_unlock(folio);

	if (!page_mkwrite)
		file_update_time(vma->vm_file);

	/*
	 * Throttle page dirtying rate down to writeback speed.
	 *
	 * mapping may be NULL here because some device drivers do not
	 * set page.mapping but still dirty their pages
	 *
	 * Drop the mmap_lock before waiting on IO, if we can. The file
	 * is pinning the mapping, as per above.
	 */
	if ((dirtied || page_mkwrite) && mapping) {
		struct file *fpin;

		fpin = maybe_unlock_mmap_for_io(vmf, NULL);
		balance_dirty_pages_ratelimited(mapping);
		if (fpin) {
			fput(fpin);
			return VM_FAULT_COMPLETED;
		}
	}

	return 0;
}

/*
 * Handle write page faults for pages that can be reused in the current vma
 *
 * This can happen either due to the mapping being with the VM_SHARED flag,
 * or due to us being the last reference standing to the page. In either
 * case, all we need to do here is to mark the page as writable and update
 * any related book-keeping.
 */
static inline void wp_page_reuse(struct vm_fault *vmf, struct folio *folio)
	__releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;
	pte_t entry;

	VM_BUG_ON(!(vmf->flags & FAULT_FLAG_WRITE));
	VM_WARN_ON(is_zero_pfn(pte_pfn(vmf->orig_pte)));

	if (folio) {
		VM_BUG_ON(folio_test_anon(folio) &&
			  !PageAnonExclusive(vmf->page));
		/*
		 * Clear the folio's cpupid information as the existing
		 * information potentially belongs to a now completely
		 * unrelated process.
		 */
		folio_xchg_last_cpupid(folio, (1 << LAST_CPUPID_SHIFT) - 1);
	}

	flush_cache_page(vma, vmf->address, pte_pfn(vmf->orig_pte));
	entry = pte_mkyoung(vmf->orig_pte);
	entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	if (ptep_set_access_flags(vma, vmf->address, vmf->pte, entry, 1))
		update_mmu_cache_range(vmf, vma, vmf->address, vmf->pte, 1);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	count_vm_event(PGREUSE);
}

/*
 * We could add a bitflag somewhere, but for now, we know that all
 * vm_ops that have a ->map_pages have been audited and don't need
 * the mmap_lock to be held.
 */
static inline vm_fault_t vmf_can_call_fault(const struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;

	if (vma->vm_ops->map_pages || !(vmf->flags & FAULT_FLAG_VMA_LOCK))
		return 0;
	vma_end_read(vma);
	return VM_FAULT_RETRY;
}

/**
 * __vmf_anon_prepare - Prepare to handle an anonymous fault.
 * @vmf: The vm_fault descriptor passed from the fault handler.
 *
 * When preparing to insert an anonymous page into a VMA from a
 * fault handler, call this function rather than anon_vma_prepare().
 * If this vma does not already have an associated anon_vma and we are
 * only protected by the per-VMA lock, the caller must retry with the
 * mmap_lock held.  __anon_vma_prepare() will look at adjacent VMAs to
 * determine if this VMA can share its anon_vma, and that's not safe to
 * do with only the per-VMA lock held for this VMA.
 *
 * Return: 0 if fault handling can proceed.  Any other value should be
 * returned to the caller.
 */
vm_fault_t __vmf_anon_prepare(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret = 0;

	if (likely(vma->anon_vma))
		return 0;
	if (vmf->flags & FAULT_FLAG_VMA_LOCK) {
		if (!mmap_read_trylock(vma->vm_mm))
			return VM_FAULT_RETRY;
	}
	if (__anon_vma_prepare(vma))
		ret = VM_FAULT_OOM;
	if (vmf->flags & FAULT_FLAG_VMA_LOCK)
		mmap_read_unlock(vma->vm_mm);
	return ret;
}

/*
 * Handle the case of a page which we actually need to copy to a new page,
 * either due to COW or unsharing.
 *
 * Called with mmap_lock locked and the old page referenced, but
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
static vm_fault_t wp_page_copy(struct vm_fault *vmf)
{
	const bool unshare = vmf->flags & FAULT_FLAG_UNSHARE;
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *mm = vma->vm_mm;
	struct folio *old_folio = NULL;
	struct folio *new_folio = NULL;
	pte_t entry;
	int page_copied = 0;
	struct mmu_notifier_range range;
	vm_fault_t ret;
	bool pfn_is_zero;

	delayacct_wpcopy_start();

	if (vmf->page)
		old_folio = page_folio(vmf->page);
	ret = vmf_anon_prepare(vmf);
	if (unlikely(ret))
		goto out;

	pfn_is_zero = is_zero_pfn(pte_pfn(vmf->orig_pte));
	new_folio = folio_prealloc(mm, vma, vmf->address, pfn_is_zero);
	if (!new_folio)
		goto oom;

	if (!pfn_is_zero) {
		int err;

		err = __wp_page_copy_user(&new_folio->page, vmf->page, vmf);
		if (err) {
			/*
			 * COW failed, if the fault was solved by other,
			 * it's fine. If not, userspace would re-fault on
			 * the same address and we will handle the fault
			 * from the second attempt.
			 * The -EHWPOISON case will not be retried.
			 */
			folio_put(new_folio);
			if (old_folio)
				folio_put(old_folio);

			delayacct_wpcopy_end();
			return err == -EHWPOISON ? VM_FAULT_HWPOISON : 0;
		}
		kmsan_copy_page_meta(&new_folio->page, vmf->page);
	}

	__folio_mark_uptodate(new_folio);

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm,
				vmf->address & PAGE_MASK,
				(vmf->address & PAGE_MASK) + PAGE_SIZE);
	mmu_notifier_invalidate_range_start(&range);

	/*
	 * Re-check the pte - we dropped the lock
	 */
	vmf->pte = pte_offset_map_lock(mm, vmf->pmd, vmf->address, &vmf->ptl);
	if (likely(vmf->pte && pte_same(ptep_get(vmf->pte), vmf->orig_pte))) {
		if (old_folio) {
			if (!folio_test_anon(old_folio)) {
				dec_mm_counter(mm, mm_counter_file(old_folio));
				inc_mm_counter(mm, MM_ANONPAGES);
			}
		} else {
			ksm_might_unmap_zero_page(mm, vmf->orig_pte);
			inc_mm_counter(mm, MM_ANONPAGES);
		}
		flush_cache_page(vma, vmf->address, pte_pfn(vmf->orig_pte));
		entry = mk_pte(&new_folio->page, vma->vm_page_prot);
		entry = pte_sw_mkyoung(entry);
		if (unlikely(unshare)) {
			if (pte_soft_dirty(vmf->orig_pte))
				entry = pte_mksoft_dirty(entry);
			if (pte_uffd_wp(vmf->orig_pte))
				entry = pte_mkuffd_wp(entry);
		} else {
			entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		}

		/*
		 * Clear the pte entry and flush it first, before updating the
		 * pte with the new entry, to keep TLBs on different CPUs in
		 * sync. This code used to set the new PTE then flush TLBs, but
		 * that left a window where the new PTE could be loaded into
		 * some TLBs while the old PTE remains in others.
		 */
		ptep_clear_flush(vma, vmf->address, vmf->pte);
		folio_add_new_anon_rmap(new_folio, vma, vmf->address, RMAP_EXCLUSIVE);
		folio_add_lru_vma(new_folio, vma);
		BUG_ON(unshare && pte_write(entry));
		set_pte_at(mm, vmf->address, vmf->pte, entry);
		update_mmu_cache_range(vmf, vma, vmf->address, vmf->pte, 1);
		if (old_folio) {
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
			 * folio_remove_rmap_pte() with the ptp_clear_flush
			 * above. Those stores are ordered by (if nothing else,)
			 * the barrier present in the atomic_add_negative
			 * in folio_remove_rmap_pte();
			 *
			 * Then the TLB flush in ptep_clear_flush ensures that
			 * no process can access the old page before the
			 * decremented mapcount is visible. And the old page
			 * cannot be reused until after the decremented
			 * mapcount is visible. So transitively, TLBs to
			 * old page will be flushed before it can be reused.
			 */
			folio_remove_rmap_pte(old_folio, vmf->page, vma);
		}

		/* Free the old page.. */
		new_folio = old_folio;
		page_copied = 1;
		pte_unmap_unlock(vmf->pte, vmf->ptl);
	} else if (vmf->pte) {
		update_mmu_tlb(vma, vmf->address, vmf->pte);
		pte_unmap_unlock(vmf->pte, vmf->ptl);
	}

	mmu_notifier_invalidate_range_end(&range);

	if (new_folio)
		folio_put(new_folio);
	if (old_folio) {
		if (page_copied)
			free_swap_cache(old_folio);
		folio_put(old_folio);
	}

	delayacct_wpcopy_end();
	return 0;
oom:
	ret = VM_FAULT_OOM;
out:
	if (old_folio)
		folio_put(old_folio);

	delayacct_wpcopy_end();
	return ret;
}

/**
 * finish_mkwrite_fault - finish page fault for a shared mapping, making PTE
 *			  writeable once the page is prepared
 *
 * @vmf: structure describing the fault
 * @folio: the folio of vmf->page
 *
 * This function handles all that is needed to finish a write page fault in a
 * shared mapping due to PTE being read-only once the mapped page is prepared.
 * It handles locking of PTE and modifying it.
 *
 * The function expects the page to be locked or other protection against
 * concurrent faults / writeback (such as DAX radix tree locks).
 *
 * Return: %0 on success, %VM_FAULT_NOPAGE when PTE got changed before
 * we acquired PTE lock.
 */
static vm_fault_t finish_mkwrite_fault(struct vm_fault *vmf, struct folio *folio)
{
	WARN_ON_ONCE(!(vmf->vma->vm_flags & VM_SHARED));
	vmf->pte = pte_offset_map_lock(vmf->vma->vm_mm, vmf->pmd, vmf->address,
				       &vmf->ptl);
	if (!vmf->pte)
		return VM_FAULT_NOPAGE;
	/*
	 * We might have raced with another page fault while we released the
	 * pte_offset_map_lock.
	 */
	if (!pte_same(ptep_get(vmf->pte), vmf->orig_pte)) {
		update_mmu_tlb(vmf->vma, vmf->address, vmf->pte);
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return VM_FAULT_NOPAGE;
	}
	wp_page_reuse(vmf, folio);
	return 0;
}

/*
 * Handle write page faults for VM_MIXEDMAP or VM_PFNMAP for a VM_SHARED
 * mapping
 */
static vm_fault_t wp_pfn_shared(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;

	if (vma->vm_ops && vma->vm_ops->pfn_mkwrite) {
		vm_fault_t ret;

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		ret = vmf_can_call_fault(vmf);
		if (ret)
			return ret;

		vmf->flags |= FAULT_FLAG_MKWRITE;
		ret = vma->vm_ops->pfn_mkwrite(vmf);
		if (ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE))
			return ret;
		return finish_mkwrite_fault(vmf, NULL);
	}
	wp_page_reuse(vmf, NULL);
	return 0;
}

static vm_fault_t wp_page_shared(struct vm_fault *vmf, struct folio *folio)
	__releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret = 0;

	folio_get(folio);

	if (vma->vm_ops && vma->vm_ops->page_mkwrite) {
		vm_fault_t tmp;

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		tmp = vmf_can_call_fault(vmf);
		if (tmp) {
			folio_put(folio);
			return tmp;
		}

		tmp = do_page_mkwrite(vmf, folio);
		if (unlikely(!tmp || (tmp &
				      (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
			folio_put(folio);
			return tmp;
		}
		tmp = finish_mkwrite_fault(vmf, folio);
		if (unlikely(tmp & (VM_FAULT_ERROR | VM_FAULT_NOPAGE))) {
			folio_unlock(folio);
			folio_put(folio);
			return tmp;
		}
	} else {
		wp_page_reuse(vmf, folio);
		folio_lock(folio);
	}
	ret |= fault_dirty_shared_page(vmf);
	folio_put(folio);

	return ret;
}

static bool wp_can_reuse_anon_folio(struct folio *folio,
				    struct vm_area_struct *vma)
{
	/*
	 * We could currently only reuse a subpage of a large folio if no
	 * other subpages of the large folios are still mapped. However,
	 * let's just consistently not reuse subpages even if we could
	 * reuse in that scenario, and give back a large folio a bit
	 * sooner.
	 */
	if (folio_test_large(folio))
		return false;

	/*
	 * We have to verify under folio lock: these early checks are
	 * just an optimization to avoid locking the folio and freeing
	 * the swapcache if there is little hope that we can reuse.
	 *
	 * KSM doesn't necessarily raise the folio refcount.
	 */
	if (folio_test_ksm(folio) || folio_ref_count(folio) > 3)
		return false;
	if (!folio_test_lru(folio))
		/*
		 * We cannot easily detect+handle references from
		 * remote LRU caches or references to LRU folios.
		 */
		lru_add_drain();
	if (folio_ref_count(folio) > 1 + folio_test_swapcache(folio))
		return false;
	if (!folio_trylock(folio))
		return false;
	if (folio_test_swapcache(folio))
		folio_free_swap(folio);
	if (folio_test_ksm(folio) || folio_ref_count(folio) != 1) {
		folio_unlock(folio);
		return false;
	}
	/*
	 * Ok, we've got the only folio reference from our mapping
	 * and the folio is locked, it's dark out, and we're wearing
	 * sunglasses. Hit it.
	 */
	folio_move_anon_rmap(folio, vma);
	folio_unlock(folio);
	return true;
}

/*
 * This routine handles present pages, when
 * * users try to write to a shared page (FAULT_FLAG_WRITE)
 * * GUP wants to take a R/O pin on a possibly shared anonymous page
 *   (FAULT_FLAG_UNSHARE)
 *
 * It is done by copying the page to a new address and decrementing the
 * shared-page counter for the old page.
 *
 * Note that this routine assumes that the protection checks have been
 * done by the caller (the low-level page fault routine in most cases).
 * Thus, with FAULT_FLAG_WRITE, we can safely just mark it writable once we've
 * done any necessary COW.
 *
 * In case of FAULT_FLAG_WRITE, we also mark the page dirty at this point even
 * though the page will change only once the write actually happens. This
 * avoids a few races, and potentially makes it more efficient.
 *
 * We enter with non-exclusive mmap_lock (to exclude vma changes,
 * but allow concurrent faults), with pte both mapped and locked.
 * We return with mmap_lock still held, but pte unmapped and unlocked.
 */
static vm_fault_t do_wp_page(struct vm_fault *vmf)
	__releases(vmf->ptl)
{
	const bool unshare = vmf->flags & FAULT_FLAG_UNSHARE;
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio = NULL;
	pte_t pte;

	if (likely(!unshare)) {
		if (userfaultfd_pte_wp(vma, ptep_get(vmf->pte))) {
			if (!userfaultfd_wp_async(vma)) {
				pte_unmap_unlock(vmf->pte, vmf->ptl);
				return handle_userfault(vmf, VM_UFFD_WP);
			}

			/*
			 * Nothing needed (cache flush, TLB invalidations,
			 * etc.) because we're only removing the uffd-wp bit,
			 * which is completely invisible to the user.
			 */
			pte = pte_clear_uffd_wp(ptep_get(vmf->pte));

			set_pte_at(vma->vm_mm, vmf->address, vmf->pte, pte);
			/*
			 * Update this to be prepared for following up CoW
			 * handling
			 */
			vmf->orig_pte = pte;
		}

		/*
		 * Userfaultfd write-protect can defer flushes. Ensure the TLB
		 * is flushed in this case before copying.
		 */
		if (unlikely(userfaultfd_wp(vmf->vma) &&
			     mm_tlb_flush_pending(vmf->vma->vm_mm)))
			flush_tlb_page(vmf->vma, vmf->address);
	}

	vmf->page = vm_normal_page(vma, vmf->address, vmf->orig_pte);

	if (vmf->page)
		folio = page_folio(vmf->page);

	/*
	 * Shared mapping: we are guaranteed to have VM_WRITE and
	 * FAULT_FLAG_WRITE set at this point.
	 */
	if (vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) {
		/*
		 * VM_MIXEDMAP !pfn_valid() case, or VM_SOFTDIRTY clear on a
		 * VM_PFNMAP VMA.
		 *
		 * We should not cow pages in a shared writeable mapping.
		 * Just mark the pages writable and/or call ops->pfn_mkwrite.
		 */
		if (!vmf->page)
			return wp_pfn_shared(vmf);
		return wp_page_shared(vmf, folio);
	}

	/*
	 * Private mapping: create an exclusive anonymous page copy if reuse
	 * is impossible. We might miss VM_WRITE for FOLL_FORCE handling.
	 *
	 * If we encounter a page that is marked exclusive, we must reuse
	 * the page without further checks.
	 */
	if (folio && folio_test_anon(folio) &&
	    (PageAnonExclusive(vmf->page) || wp_can_reuse_anon_folio(folio, vma))) {
		if (!PageAnonExclusive(vmf->page))
			SetPageAnonExclusive(vmf->page);
		if (unlikely(unshare)) {
			pte_unmap_unlock(vmf->pte, vmf->ptl);
			return 0;
		}
		wp_page_reuse(vmf, folio);
		return 0;
	}
	/*
	 * Ok, we need to copy. Oh, well..
	 */
	if (folio)
		folio_get(folio);

	pte_unmap_unlock(vmf->pte, vmf->ptl);
#ifdef CONFIG_KSM
	if (folio && folio_test_ksm(folio))
		count_vm_event(COW_KSM);
#endif
	return wp_page_copy(vmf);
}

static void unmap_mapping_range_vma(struct vm_area_struct *vma,
		unsigned long start_addr, unsigned long end_addr,
		struct zap_details *details)
{
	zap_page_range_single(vma, start_addr, end_addr - start_addr, details);
}

static inline void unmap_mapping_range_tree(struct rb_root_cached *root,
					    pgoff_t first_index,
					    pgoff_t last_index,
					    struct zap_details *details)
{
	struct vm_area_struct *vma;
	pgoff_t vba, vea, zba, zea;

	vma_interval_tree_foreach(vma, root, first_index, last_index) {
		vba = vma->vm_pgoff;
		vea = vba + vma_pages(vma) - 1;
		zba = max(first_index, vba);
		zea = min(last_index, vea);

		unmap_mapping_range_vma(vma,
			((zba - vba) << PAGE_SHIFT) + vma->vm_start,
			((zea - vba + 1) << PAGE_SHIFT) + vma->vm_start,
				details);
	}
}

/**
 * unmap_mapping_folio() - Unmap single folio from processes.
 * @folio: The locked folio to be unmapped.
 *
 * Unmap this folio from any userspace process which still has it mmaped.
 * Typically, for efficiency, the range of nearby pages has already been
 * unmapped by unmap_mapping_pages() or unmap_mapping_range().  But once
 * truncation or invalidation holds the lock on a folio, it may find that
 * the page has been remapped again: and then uses unmap_mapping_folio()
 * to unmap it finally.
 */
void unmap_mapping_folio(struct folio *folio)
{
	struct address_space *mapping = folio->mapping;
	struct zap_details details = { };
	pgoff_t	first_index;
	pgoff_t	last_index;

	VM_BUG_ON(!folio_test_locked(folio));

	first_index = folio->index;
	last_index = folio_next_index(folio) - 1;

	details.even_cows = false;
	details.single_folio = folio;
	details.zap_flags = ZAP_FLAG_DROP_MARKER;

	i_mmap_lock_read(mapping);
	if (unlikely(!RB_EMPTY_ROOT(&mapping->i_mmap.rb_root)))
		unmap_mapping_range_tree(&mapping->i_mmap, first_index,
					 last_index, &details);
	i_mmap_unlock_read(mapping);
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
	pgoff_t	first_index = start;
	pgoff_t	last_index = start + nr - 1;

	details.even_cows = even_cows;
	if (last_index < first_index)
		last_index = ULONG_MAX;

	i_mmap_lock_read(mapping);
	if (unlikely(!RB_EMPTY_ROOT(&mapping->i_mmap.rb_root)))
		unmap_mapping_range_tree(&mapping->i_mmap, first_index,
					 last_index, &details);
	i_mmap_unlock_read(mapping);
}
EXPORT_SYMBOL_GPL(unmap_mapping_pages);

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
	pgoff_t hba = (pgoff_t)(holebegin) >> PAGE_SHIFT;
	pgoff_t hlen = ((pgoff_t)(holelen) + PAGE_SIZE - 1) >> PAGE_SHIFT;

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
 * Restore a potential device exclusive pte to a working pte entry
 */
static vm_fault_t remove_device_exclusive_entry(struct vm_fault *vmf)
{
	struct folio *folio = page_folio(vmf->page);
	struct vm_area_struct *vma = vmf->vma;
	struct mmu_notifier_range range;
	vm_fault_t ret;

	/*
	 * We need a reference to lock the folio because we don't hold
	 * the PTL so a racing thread can remove the device-exclusive
	 * entry and unmap it. If the folio is free the entry must
	 * have been removed already. If it happens to have already
	 * been re-allocated after being freed all we do is lock and
	 * unlock it.
	 */
	if (!folio_try_get(folio))
		return 0;

	ret = folio_lock_or_retry(folio, vmf);
	if (ret) {
		folio_put(folio);
		return ret;
	}
	mmu_notifier_range_init_owner(&range, MMU_NOTIFY_EXCLUSIVE, 0,
				vma->vm_mm, vmf->address & PAGE_MASK,
				(vmf->address & PAGE_MASK) + PAGE_SIZE, NULL);
	mmu_notifier_invalidate_range_start(&range);

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
				&vmf->ptl);
	if (likely(vmf->pte && pte_same(ptep_get(vmf->pte), vmf->orig_pte)))
		restore_exclusive_pte(vma, vmf->page, vmf->address, vmf->pte);

	if (vmf->pte)
		pte_unmap_unlock(vmf->pte, vmf->ptl);
	folio_unlock(folio);
	folio_put(folio);

	mmu_notifier_invalidate_range_end(&range);
	return 0;
}

static inline bool should_try_to_free_swap(struct folio *folio,
					   struct vm_area_struct *vma,
					   unsigned int fault_flags)
{
	if (!folio_test_swapcache(folio))
		return false;
	if (mem_cgroup_swap_full(folio) || (vma->vm_flags & VM_LOCKED) ||
	    folio_test_mlocked(folio))
		return true;
	/*
	 * If we want to map a page that's in the swapcache writable, we
	 * have to detect via the refcount if we're really the exclusive
	 * user. Try freeing the swapcache to get rid of the swapcache
	 * reference only in case it's likely that we'll be the exlusive user.
	 */
	return (fault_flags & FAULT_FLAG_WRITE) && !folio_test_ksm(folio) &&
		folio_ref_count(folio) == (1 + folio_nr_pages(folio));
}

static vm_fault_t pte_marker_clear(struct vm_fault *vmf)
{
	vmf->pte = pte_offset_map_lock(vmf->vma->vm_mm, vmf->pmd,
				       vmf->address, &vmf->ptl);
	if (!vmf->pte)
		return 0;
	/*
	 * Be careful so that we will only recover a special uffd-wp pte into a
	 * none pte.  Otherwise it means the pte could have changed, so retry.
	 *
	 * This should also cover the case where e.g. the pte changed
	 * quickly from a PTE_MARKER_UFFD_WP into PTE_MARKER_POISONED.
	 * So is_pte_marker() check is not enough to safely drop the pte.
	 */
	if (pte_same(vmf->orig_pte, ptep_get(vmf->pte)))
		pte_clear(vmf->vma->vm_mm, vmf->address, vmf->pte);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return 0;
}

static vm_fault_t do_pte_missing(struct vm_fault *vmf)
{
	if (vma_is_anonymous(vmf->vma))
		return do_anonymous_page(vmf);
	else
		return do_fault(vmf);
}

/*
 * This is actually a page-missing access, but with uffd-wp special pte
 * installed.  It means this pte was wr-protected before being unmapped.
 */
static vm_fault_t pte_marker_handle_uffd_wp(struct vm_fault *vmf)
{
	/*
	 * Just in case there're leftover special ptes even after the region
	 * got unregistered - we can simply clear them.
	 */
	if (unlikely(!userfaultfd_wp(vmf->vma)))
		return pte_marker_clear(vmf);

	return do_pte_missing(vmf);
}

static vm_fault_t handle_pte_marker(struct vm_fault *vmf)
{
	swp_entry_t entry = pte_to_swp_entry(vmf->orig_pte);
	unsigned long marker = pte_marker_get(entry);

	/*
	 * PTE markers should never be empty.  If anything weird happened,
	 * the best thing to do is to kill the process along with its mm.
	 */
	if (WARN_ON_ONCE(!marker))
		return VM_FAULT_SIGBUS;

	/* Higher priority than uffd-wp when data corrupted */
	if (marker & PTE_MARKER_POISONED)
		return VM_FAULT_HWPOISON;

	/* Hitting a guard page is always a fatal condition. */
	if (marker & PTE_MARKER_GUARD)
		return VM_FAULT_SIGSEGV;

	if (pte_marker_entry_uffd_wp(entry))
		return pte_marker_handle_uffd_wp(vmf);

	/* This is an unknown pte marker */
	return VM_FAULT_SIGBUS;
}

static struct folio *__alloc_swap_folio(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio;
	swp_entry_t entry;

	folio = vma_alloc_folio(GFP_HIGHUSER_MOVABLE, 0, vma, vmf->address);
	if (!folio)
		return NULL;

	entry = pte_to_swp_entry(vmf->orig_pte);
	if (mem_cgroup_swapin_charge_folio(folio, vma->vm_mm,
					   GFP_KERNEL, entry)) {
		folio_put(folio);
		return NULL;
	}

	return folio;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline int non_swapcache_batch(swp_entry_t entry, int max_nr)
{
	struct swap_info_struct *si = swp_swap_info(entry);
	pgoff_t offset = swp_offset(entry);
	int i;

	/*
	 * While allocating a large folio and doing swap_read_folio, which is
	 * the case the being faulted pte doesn't have swapcache. We need to
	 * ensure all PTEs have no cache as well, otherwise, we might go to
	 * swap devices while the content is in swapcache.
	 */
	for (i = 0; i < max_nr; i++) {
		if ((si->swap_map[offset + i] & SWAP_HAS_CACHE))
			return i;
	}

	return i;
}

/*
 * Check if the PTEs within a range are contiguous swap entries
 * and have consistent swapcache, zeromap.
 */
static bool can_swapin_thp(struct vm_fault *vmf, pte_t *ptep, int nr_pages)
{
	unsigned long addr;
	swp_entry_t entry;
	int idx;
	pte_t pte;

	addr = ALIGN_DOWN(vmf->address, nr_pages * PAGE_SIZE);
	idx = (vmf->address - addr) / PAGE_SIZE;
	pte = ptep_get(ptep);

	if (!pte_same(pte, pte_move_swp_offset(vmf->orig_pte, -idx)))
		return false;
	entry = pte_to_swp_entry(pte);
	if (swap_pte_batch(ptep, nr_pages, pte) != nr_pages)
		return false;

	/*
	 * swap_read_folio() can't handle the case a large folio is hybridly
	 * from different backends. And they are likely corner cases. Similar
	 * things might be added once zswap support large folios.
	 */
	if (unlikely(swap_zeromap_batch(entry, nr_pages, NULL) != nr_pages))
		return false;
	if (unlikely(non_swapcache_batch(entry, nr_pages) != nr_pages))
		return false;

	return true;
}

static inline unsigned long thp_swap_suitable_orders(pgoff_t swp_offset,
						     unsigned long addr,
						     unsigned long orders)
{
	int order, nr;

	order = highest_order(orders);

	/*
	 * To swap in a THP with nr pages, we require that its first swap_offset
	 * is aligned with that number, as it was when the THP was swapped out.
	 * This helps filter out most invalid entries.
	 */
	while (orders) {
		nr = 1 << order;
		if ((addr >> PAGE_SHIFT) % nr == swp_offset % nr)
			break;
		order = next_order(&orders, order);
	}

	return orders;
}

static struct folio *alloc_swap_folio(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	unsigned long orders;
	struct folio *folio;
	unsigned long addr;
	swp_entry_t entry;
	spinlock_t *ptl;
	pte_t *pte;
	gfp_t gfp;
	int order;

	/*
	 * If uffd is active for the vma we need per-page fault fidelity to
	 * maintain the uffd semantics.
	 */
	if (unlikely(userfaultfd_armed(vma)))
		goto fallback;

	/*
	 * A large swapped out folio could be partially or fully in zswap. We
	 * lack handling for such cases, so fallback to swapping in order-0
	 * folio.
	 */
	if (!zswap_never_enabled())
		goto fallback;

	entry = pte_to_swp_entry(vmf->orig_pte);
	/*
	 * Get a list of all the (large) orders below PMD_ORDER that are enabled
	 * and suitable for swapping THP.
	 */
	orders = thp_vma_allowable_orders(vma, vma->vm_flags,
			TVA_IN_PF | TVA_ENFORCE_SYSFS, BIT(PMD_ORDER) - 1);
	orders = thp_vma_suitable_orders(vma, vmf->address, orders);
	orders = thp_swap_suitable_orders(swp_offset(entry),
					  vmf->address, orders);

	if (!orders)
		goto fallback;

	pte = pte_offset_map_lock(vmf->vma->vm_mm, vmf->pmd,
				  vmf->address & PMD_MASK, &ptl);
	if (unlikely(!pte))
		goto fallback;

	/*
	 * For do_swap_page, find the highest order where the aligned range is
	 * completely swap entries with contiguous swap offsets.
	 */
	order = highest_order(orders);
	while (orders) {
		addr = ALIGN_DOWN(vmf->address, PAGE_SIZE << order);
		if (can_swapin_thp(vmf, pte + pte_index(addr), 1 << order))
			break;
		order = next_order(&orders, order);
	}

	pte_unmap_unlock(pte, ptl);

	/* Try allocating the highest of the remaining orders. */
	gfp = vma_thp_gfp_mask(vma);
	while (orders) {
		addr = ALIGN_DOWN(vmf->address, PAGE_SIZE << order);
		folio = vma_alloc_folio(gfp, order, vma, addr);
		if (folio) {
			if (!mem_cgroup_swapin_charge_folio(folio, vma->vm_mm,
							    gfp, entry))
				return folio;
			count_mthp_stat(order, MTHP_STAT_SWPIN_FALLBACK_CHARGE);
			folio_put(folio);
		}
		count_mthp_stat(order, MTHP_STAT_SWPIN_FALLBACK);
		order = next_order(&orders, order);
	}

fallback:
	return __alloc_swap_folio(vmf);
}
#else /* !CONFIG_TRANSPARENT_HUGEPAGE */
static struct folio *alloc_swap_folio(struct vm_fault *vmf)
{
	return __alloc_swap_folio(vmf);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static DECLARE_WAIT_QUEUE_HEAD(swapcache_wq);

/*
 * We enter with non-exclusive mmap_lock (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with pte unmapped and unlocked.
 *
 * We return with the mmap_lock locked or unlocked in the same cases
 * as does filemap_fault().
 */
vm_fault_t do_swap_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct folio *swapcache, *folio = NULL;
	DECLARE_WAITQUEUE(wait, current);
	struct page *page;
	struct swap_info_struct *si = NULL;
	rmap_t rmap_flags = RMAP_NONE;
	bool need_clear_cache = false;
	bool exclusive = false;
	swp_entry_t entry;
	pte_t pte;
	vm_fault_t ret = 0;
	void *shadow = NULL;
	int nr_pages;
	unsigned long page_idx;
	unsigned long address;
	pte_t *ptep;

	if (!pte_unmap_same(vmf))
		goto out;

	entry = pte_to_swp_entry(vmf->orig_pte);
	if (unlikely(non_swap_entry(entry))) {
		if (is_migration_entry(entry)) {
			migration_entry_wait(vma->vm_mm, vmf->pmd,
					     vmf->address);
		} else if (is_device_exclusive_entry(entry)) {
			vmf->page = pfn_swap_entry_to_page(entry);
			ret = remove_device_exclusive_entry(vmf);
		} else if (is_device_private_entry(entry)) {
			if (vmf->flags & FAULT_FLAG_VMA_LOCK) {
				/*
				 * migrate_to_ram is not yet ready to operate
				 * under VMA lock.
				 */
				vma_end_read(vma);
				ret = VM_FAULT_RETRY;
				goto out;
			}

			vmf->page = pfn_swap_entry_to_page(entry);
			vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
					vmf->address, &vmf->ptl);
			if (unlikely(!vmf->pte ||
				     !pte_same(ptep_get(vmf->pte),
							vmf->orig_pte)))
				goto unlock;

			/*
			 * Get a page reference while we know the page can't be
			 * freed.
			 */
			get_page(vmf->page);
			pte_unmap_unlock(vmf->pte, vmf->ptl);
			ret = vmf->page->pgmap->ops->migrate_to_ram(vmf);
			put_page(vmf->page);
		} else if (is_hwpoison_entry(entry)) {
			ret = VM_FAULT_HWPOISON;
		} else if (is_pte_marker_entry(entry)) {
			ret = handle_pte_marker(vmf);
		} else {
			print_bad_pte(vma, vmf->address, vmf->orig_pte, NULL);
			ret = VM_FAULT_SIGBUS;
		}
		goto out;
	}

	/* Prevent swapoff from happening to us. */
	si = get_swap_device(entry);
	if (unlikely(!si))
		goto out;

	folio = swap_cache_get_folio(entry, vma, vmf->address);
	if (folio)
		page = folio_file_page(folio, swp_offset(entry));
	swapcache = folio;

	if (!folio) {
		if (data_race(si->flags & SWP_SYNCHRONOUS_IO) &&
		    __swap_count(entry) == 1) {
			/* skip swapcache */
			folio = alloc_swap_folio(vmf);
			if (folio) {
				__folio_set_locked(folio);
				__folio_set_swapbacked(folio);

				nr_pages = folio_nr_pages(folio);
				if (folio_test_large(folio))
					entry.val = ALIGN_DOWN(entry.val, nr_pages);
				/*
				 * Prevent parallel swapin from proceeding with
				 * the cache flag. Otherwise, another thread
				 * may finish swapin first, free the entry, and
				 * swapout reusing the same entry. It's
				 * undetectable as pte_same() returns true due
				 * to entry reuse.
				 */
				if (swapcache_prepare(entry, nr_pages)) {
					/*
					 * Relax a bit to prevent rapid
					 * repeated page faults.
					 */
					add_wait_queue(&swapcache_wq, &wait);
					schedule_timeout_uninterruptible(1);
					remove_wait_queue(&swapcache_wq, &wait);
					goto out_page;
				}
				need_clear_cache = true;

				mem_cgroup_swapin_uncharge_swap(entry, nr_pages);

				shadow = get_shadow_from_swap_cache(entry);
				if (shadow)
					workingset_refault(folio, shadow);

				folio_add_lru(folio);

				/* To provide entry to swap_read_folio() */
				folio->swap = entry;
				swap_read_folio(folio, NULL);
				folio->private = NULL;
			}
		} else {
			folio = swapin_readahead(entry, GFP_HIGHUSER_MOVABLE,
						vmf);
			swapcache = folio;
		}

		if (!folio) {
			/*
			 * Back out if somebody else faulted in this pte
			 * while we released the pte lock.
			 */
			vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
					vmf->address, &vmf->ptl);
			if (likely(vmf->pte &&
				   pte_same(ptep_get(vmf->pte), vmf->orig_pte)))
				ret = VM_FAULT_OOM;
			goto unlock;
		}

		/* Had to read the page from swap area: Major fault */
		ret = VM_FAULT_MAJOR;
		count_vm_event(PGMAJFAULT);
		count_memcg_event_mm(vma->vm_mm, PGMAJFAULT);
		page = folio_file_page(folio, swp_offset(entry));
	} else if (PageHWPoison(page)) {
		/*
		 * hwpoisoned dirty swapcache pages are kept for killing
		 * owner processes (which may be unknown at hwpoison time)
		 */
		ret = VM_FAULT_HWPOISON;
		goto out_release;
	}

	ret |= folio_lock_or_retry(folio, vmf);
	if (ret & VM_FAULT_RETRY)
		goto out_release;

	if (swapcache) {
		/*
		 * Make sure folio_free_swap() or swapoff did not release the
		 * swapcache from under us.  The page pin, and pte_same test
		 * below, are not enough to exclude that.  Even if it is still
		 * swapcache, we need to check that the page's swap has not
		 * changed.
		 */
		if (unlikely(!folio_test_swapcache(folio) ||
			     page_swap_entry(page).val != entry.val))
			goto out_page;

		/*
		 * KSM sometimes has to copy on read faults, for example, if
		 * page->index of !PageKSM() pages would be nonlinear inside the
		 * anon VMA -- PageKSM() is lost on actual swapout.
		 */
		folio = ksm_might_need_to_copy(folio, vma, vmf->address);
		if (unlikely(!folio)) {
			ret = VM_FAULT_OOM;
			folio = swapcache;
			goto out_page;
		} else if (unlikely(folio == ERR_PTR(-EHWPOISON))) {
			ret = VM_FAULT_HWPOISON;
			folio = swapcache;
			goto out_page;
		}
		if (folio != swapcache)
			page = folio_page(folio, 0);

		/*
		 * If we want to map a page that's in the swapcache writable, we
		 * have to detect via the refcount if we're really the exclusive
		 * owner. Try removing the extra reference from the local LRU
		 * caches if required.
		 */
		if ((vmf->flags & FAULT_FLAG_WRITE) && folio == swapcache &&
		    !folio_test_ksm(folio) && !folio_test_lru(folio))
			lru_add_drain();
	}

	folio_throttle_swaprate(folio, GFP_KERNEL);

	/*
	 * Back out if somebody else already faulted in this pte.
	 */
	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
			&vmf->ptl);
	if (unlikely(!vmf->pte || !pte_same(ptep_get(vmf->pte), vmf->orig_pte)))
		goto out_nomap;

	if (unlikely(!folio_test_uptodate(folio))) {
		ret = VM_FAULT_SIGBUS;
		goto out_nomap;
	}

	/* allocated large folios for SWP_SYNCHRONOUS_IO */
	if (folio_test_large(folio) && !folio_test_swapcache(folio)) {
		unsigned long nr = folio_nr_pages(folio);
		unsigned long folio_start = ALIGN_DOWN(vmf->address, nr * PAGE_SIZE);
		unsigned long idx = (vmf->address - folio_start) / PAGE_SIZE;
		pte_t *folio_ptep = vmf->pte - idx;
		pte_t folio_pte = ptep_get(folio_ptep);

		if (!pte_same(folio_pte, pte_move_swp_offset(vmf->orig_pte, -idx)) ||
		    swap_pte_batch(folio_ptep, nr, folio_pte) != nr)
			goto out_nomap;

		page_idx = idx;
		address = folio_start;
		ptep = folio_ptep;
		goto check_folio;
	}

	nr_pages = 1;
	page_idx = 0;
	address = vmf->address;
	ptep = vmf->pte;
	if (folio_test_large(folio) && folio_test_swapcache(folio)) {
		int nr = folio_nr_pages(folio);
		unsigned long idx = folio_page_idx(folio, page);
		unsigned long folio_start = address - idx * PAGE_SIZE;
		unsigned long folio_end = folio_start + nr * PAGE_SIZE;
		pte_t *folio_ptep;
		pte_t folio_pte;

		if (unlikely(folio_start < max(address & PMD_MASK, vma->vm_start)))
			goto check_folio;
		if (unlikely(folio_end > pmd_addr_end(address, vma->vm_end)))
			goto check_folio;

		folio_ptep = vmf->pte - idx;
		folio_pte = ptep_get(folio_ptep);
		if (!pte_same(folio_pte, pte_move_swp_offset(vmf->orig_pte, -idx)) ||
		    swap_pte_batch(folio_ptep, nr, folio_pte) != nr)
			goto check_folio;

		page_idx = idx;
		address = folio_start;
		ptep = folio_ptep;
		nr_pages = nr;
		entry = folio->swap;
		page = &folio->page;
	}

check_folio:
	/*
	 * PG_anon_exclusive reuses PG_mappedtodisk for anon pages. A swap pte
	 * must never point at an anonymous page in the swapcache that is
	 * PG_anon_exclusive. Sanity check that this holds and especially, that
	 * no filesystem set PG_mappedtodisk on a page in the swapcache. Sanity
	 * check after taking the PT lock and making sure that nobody
	 * concurrently faulted in this page and set PG_anon_exclusive.
	 */
	BUG_ON(!folio_test_anon(folio) && folio_test_mappedtodisk(folio));
	BUG_ON(folio_test_anon(folio) && PageAnonExclusive(page));

	/*
	 * Check under PT lock (to protect against concurrent fork() sharing
	 * the swap entry concurrently) for certainly exclusive pages.
	 */
	if (!folio_test_ksm(folio)) {
		exclusive = pte_swp_exclusive(vmf->orig_pte);
		if (folio != swapcache) {
			/*
			 * We have a fresh page that is not exposed to the
			 * swapcache -> certainly exclusive.
			 */
			exclusive = true;
		} else if (exclusive && folio_test_writeback(folio) &&
			  data_race(si->flags & SWP_STABLE_WRITES)) {
			/*
			 * This is tricky: not all swap backends support
			 * concurrent page modifications while under writeback.
			 *
			 * So if we stumble over such a page in the swapcache
			 * we must not set the page exclusive, otherwise we can
			 * map it writable without further checks and modify it
			 * while still under writeback.
			 *
			 * For these problematic swap backends, simply drop the
			 * exclusive marker: this is perfectly fine as we start
			 * writeback only if we fully unmapped the page and
			 * there are no unexpected references on the page after
			 * unmapping succeeded. After fully unmapped, no
			 * further GUP references (FOLL_GET and FOLL_PIN) can
			 * appear, so dropping the exclusive marker and mapping
			 * it only R/O is fine.
			 */
			exclusive = false;
		}
	}

	/*
	 * Some architectures may have to restore extra metadata to the page
	 * when reading from swap. This metadata may be indexed by swap entry
	 * so this must be called before swap_free().
	 */
	arch_swap_restore(folio_swap(entry, folio), folio);

	/*
	 * Remove the swap entry and conditionally try to free up the swapcache.
	 * We're already holding a reference on the page but haven't mapped it
	 * yet.
	 */
	swap_free_nr(entry, nr_pages);
	if (should_try_to_free_swap(folio, vma, vmf->flags))
		folio_free_swap(folio);

	add_mm_counter(vma->vm_mm, MM_ANONPAGES, nr_pages);
	add_mm_counter(vma->vm_mm, MM_SWAPENTS, -nr_pages);
	pte = mk_pte(page, vma->vm_page_prot);
	if (pte_swp_soft_dirty(vmf->orig_pte))
		pte = pte_mksoft_dirty(pte);
	if (pte_swp_uffd_wp(vmf->orig_pte))
		pte = pte_mkuffd_wp(pte);

	/*
	 * Same logic as in do_wp_page(); however, optimize for pages that are
	 * certainly not shared either because we just allocated them without
	 * exposing them to the swapcache or because the swap entry indicates
	 * exclusivity.
	 */
	if (!folio_test_ksm(folio) &&
	    (exclusive || folio_ref_count(folio) == 1)) {
		if ((vma->vm_flags & VM_WRITE) && !userfaultfd_pte_wp(vma, pte) &&
		    !pte_needs_soft_dirty_wp(vma, pte)) {
			pte = pte_mkwrite(pte, vma);
			if (vmf->flags & FAULT_FLAG_WRITE) {
				pte = pte_mkdirty(pte);
				vmf->flags &= ~FAULT_FLAG_WRITE;
			}
		}
		rmap_flags |= RMAP_EXCLUSIVE;
	}
	folio_ref_add(folio, nr_pages - 1);
	flush_icache_pages(vma, page, nr_pages);
	vmf->orig_pte = pte_advance_pfn(pte, page_idx);

	/* ksm created a completely new copy */
	if (unlikely(folio != swapcache && swapcache)) {
		folio_add_new_anon_rmap(folio, vma, address, RMAP_EXCLUSIVE);
		folio_add_lru_vma(folio, vma);
	} else if (!folio_test_anon(folio)) {
		/*
		 * We currently only expect small !anon folios which are either
		 * fully exclusive or fully shared, or new allocated large
		 * folios which are fully exclusive. If we ever get large
		 * folios within swapcache here, we have to be careful.
		 */
		VM_WARN_ON_ONCE(folio_test_large(folio) && folio_test_swapcache(folio));
		VM_WARN_ON_FOLIO(!folio_test_locked(folio), folio);
		folio_add_new_anon_rmap(folio, vma, address, rmap_flags);
	} else {
		folio_add_anon_rmap_ptes(folio, page, nr_pages, vma, address,
					rmap_flags);
	}

	VM_BUG_ON(!folio_test_anon(folio) ||
			(pte_write(pte) && !PageAnonExclusive(page)));
	set_ptes(vma->vm_mm, address, ptep, pte, nr_pages);
	arch_do_swap_page_nr(vma->vm_mm, vma, address,
			pte, pte, nr_pages);

	folio_unlock(folio);
	if (folio != swapcache && swapcache) {
		/*
		 * Hold the lock to avoid the swap entry to be reused
		 * until we take the PT lock for the pte_same() check
		 * (to avoid false positives from pte_same). For
		 * further safety release the lock after the swap_free
		 * so that the swap count won't change under a
		 * parallel locked swapcache.
		 */
		folio_unlock(swapcache);
		folio_put(swapcache);
	}

	if (vmf->flags & FAULT_FLAG_WRITE) {
		ret |= do_wp_page(vmf);
		if (ret & VM_FAULT_ERROR)
			ret &= VM_FAULT_ERROR;
		goto out;
	}

	/* No need to invalidate - it was non-present before */
	update_mmu_cache_range(vmf, vma, address, ptep, nr_pages);
unlock:
	if (vmf->pte)
		pte_unmap_unlock(vmf->pte, vmf->ptl);
out:
	/* Clear the swap cache pin for direct swapin after PTL unlock */
	if (need_clear_cache) {
		swapcache_clear(si, entry, nr_pages);
		if (waitqueue_active(&swapcache_wq))
			wake_up(&swapcache_wq);
	}
	if (si)
		put_swap_device(si);
	return ret;
out_nomap:
	if (vmf->pte)
		pte_unmap_unlock(vmf->pte, vmf->ptl);
out_page:
	folio_unlock(folio);
out_release:
	folio_put(folio);
	if (folio != swapcache && swapcache) {
		folio_unlock(swapcache);
		folio_put(swapcache);
	}
	if (need_clear_cache) {
		swapcache_clear(si, entry, nr_pages);
		if (waitqueue_active(&swapcache_wq))
			wake_up(&swapcache_wq);
	}
	if (si)
		put_swap_device(si);
	return ret;
}

static bool pte_range_none(pte_t *pte, int nr_pages)
{
	int i;

	for (i = 0; i < nr_pages; i++) {
		if (!pte_none(ptep_get_lockless(pte + i)))
			return false;
	}

	return true;
}

static struct folio *alloc_anon_folio(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	unsigned long orders;
	struct folio *folio;
	unsigned long addr;
	pte_t *pte;
	gfp_t gfp;
	int order;

	/*
	 * If uffd is active for the vma we need per-page fault fidelity to
	 * maintain the uffd semantics.
	 */
	if (unlikely(userfaultfd_armed(vma)))
		goto fallback;

	/*
	 * Get a list of all the (large) orders below PMD_ORDER that are enabled
	 * for this vma. Then filter out the orders that can't be allocated over
	 * the faulting address and still be fully contained in the vma.
	 */
	orders = thp_vma_allowable_orders(vma, vma->vm_flags,
			TVA_IN_PF | TVA_ENFORCE_SYSFS, BIT(PMD_ORDER) - 1);
	orders = thp_vma_suitable_orders(vma, vmf->address, orders);

	if (!orders)
		goto fallback;

	pte = pte_offset_map(vmf->pmd, vmf->address & PMD_MASK);
	if (!pte)
		return ERR_PTR(-EAGAIN);

	/*
	 * Find the highest order where the aligned range is completely
	 * pte_none(). Note that all remaining orders will be completely
	 * pte_none().
	 */
	order = highest_order(orders);
	while (orders) {
		addr = ALIGN_DOWN(vmf->address, PAGE_SIZE << order);
		if (pte_range_none(pte + pte_index(addr), 1 << order))
			break;
		order = next_order(&orders, order);
	}

	pte_unmap(pte);

	if (!orders)
		goto fallback;

	/* Try allocating the highest of the remaining orders. */
	gfp = vma_thp_gfp_mask(vma);
	while (orders) {
		addr = ALIGN_DOWN(vmf->address, PAGE_SIZE << order);
		folio = vma_alloc_folio(gfp, order, vma, addr);
		if (folio) {
			if (mem_cgroup_charge(folio, vma->vm_mm, gfp)) {
				count_mthp_stat(order, MTHP_STAT_ANON_FAULT_FALLBACK_CHARGE);
				folio_put(folio);
				goto next;
			}
			folio_throttle_swaprate(folio, gfp);
			/*
			 * When a folio is not zeroed during allocation
			 * (__GFP_ZERO not used) or user folios require special
			 * handling, folio_zero_user() is used to make sure
			 * that the page corresponding to the faulting address
			 * will be hot in the cache after zeroing.
			 */
			if (user_alloc_needs_zeroing())
				folio_zero_user(folio, vmf->address);
			return folio;
		}
next:
		count_mthp_stat(order, MTHP_STAT_ANON_FAULT_FALLBACK);
		order = next_order(&orders, order);
	}

fallback:
#endif
	return folio_prealloc(vma->vm_mm, vma, vmf->address, true);
}

/*
 * We enter with non-exclusive mmap_lock (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_lock still held, but pte unmapped and unlocked.
 */
static vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	unsigned long addr = vmf->address;
	struct folio *folio;
	vm_fault_t ret = 0;
	int nr_pages = 1;
	pte_t entry;

	/* File mapping without ->vm_ops ? */
	if (vma->vm_flags & VM_SHARED)
		return VM_FAULT_SIGBUS;

	/*
	 * Use pte_alloc() instead of pte_alloc_map(), so that OOM can
	 * be distinguished from a transient failure of pte_offset_map().
	 */
	if (pte_alloc(vma->vm_mm, vmf->pmd))
		return VM_FAULT_OOM;

	/* Use the zero-page for reads */
	if (!(vmf->flags & FAULT_FLAG_WRITE) &&
			!mm_forbids_zeropage(vma->vm_mm)) {
		entry = pte_mkspecial(pfn_pte(my_zero_pfn(vmf->address),
						vma->vm_page_prot));
		vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
				vmf->address, &vmf->ptl);
		if (!vmf->pte)
			goto unlock;
		if (vmf_pte_changed(vmf)) {
			update_mmu_tlb(vma, vmf->address, vmf->pte);
			goto unlock;
		}
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
	ret = vmf_anon_prepare(vmf);
	if (ret)
		return ret;
	/* Returns NULL on OOM or ERR_PTR(-EAGAIN) if we must retry the fault */
	folio = alloc_anon_folio(vmf);
	if (IS_ERR(folio))
		return 0;
	if (!folio)
		goto oom;

	nr_pages = folio_nr_pages(folio);
	addr = ALIGN_DOWN(vmf->address, nr_pages * PAGE_SIZE);

	/*
	 * The memory barrier inside __folio_mark_uptodate makes sure that
	 * preceding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__folio_mark_uptodate(folio);

	entry = mk_pte(&folio->page, vma->vm_page_prot);
	entry = pte_sw_mkyoung(entry);
	if (vma->vm_flags & VM_WRITE)
		entry = pte_mkwrite(pte_mkdirty(entry), vma);

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, addr, &vmf->ptl);
	if (!vmf->pte)
		goto release;
	if (nr_pages == 1 && vmf_pte_changed(vmf)) {
		update_mmu_tlb(vma, addr, vmf->pte);
		goto release;
	} else if (nr_pages > 1 && !pte_range_none(vmf->pte, nr_pages)) {
		update_mmu_tlb_range(vma, addr, vmf->pte, nr_pages);
		goto release;
	}

	ret = check_stable_address_space(vma->vm_mm);
	if (ret)
		goto release;

	/* Deliver the page fault to userland, check inside PT lock */
	if (userfaultfd_missing(vma)) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		folio_put(folio);
		return handle_userfault(vmf, VM_UFFD_MISSING);
	}

	folio_ref_add(folio, nr_pages - 1);
	add_mm_counter(vma->vm_mm, MM_ANONPAGES, nr_pages);
	count_mthp_stat(folio_order(folio), MTHP_STAT_ANON_FAULT_ALLOC);
	folio_add_new_anon_rmap(folio, vma, addr, RMAP_EXCLUSIVE);
	folio_add_lru_vma(folio, vma);
setpte:
	if (vmf_orig_pte_uffd_wp(vmf))
		entry = pte_mkuffd_wp(entry);
	set_ptes(vma->vm_mm, addr, vmf->pte, entry, nr_pages);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache_range(vmf, vma, addr, vmf->pte, nr_pages);
unlock:
	if (vmf->pte)
		pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
release:
	folio_put(folio);
	goto unlock;
oom:
	return VM_FAULT_OOM;
}

/*
 * The mmap_lock must have been held on entry, and may have been
 * released depending on flags and vma->vm_ops->fault() return value.
 * See filemap_fault() and __lock_page_retry().
 */
static vm_fault_t __do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio;
	vm_fault_t ret;

	/*
	 * Preallocate pte before we take page_lock because this might lead to
	 * deadlocks for memcg reclaim which waits for pages under writeback:
	 *				lock_page(A)
	 *				SetPageWriteback(A)
	 *				unlock_page(A)
	 * lock_page(B)
	 *				lock_page(B)
	 * pte_alloc_one
	 *   shrink_folio_list
	 *     wait_on_page_writeback(A)
	 *				SetPageWriteback(B)
	 *				unlock_page(B)
	 *				# flush A, B to clear the writeback
	 */
	if (pmd_none(*vmf->pmd) && !vmf->prealloc_pte) {
		vmf->prealloc_pte = pte_alloc_one(vma->vm_mm);
		if (!vmf->prealloc_pte)
			return VM_FAULT_OOM;
	}

	ret = vma->vm_ops->fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY |
			    VM_FAULT_DONE_COW)))
		return ret;

	folio = page_folio(vmf->page);
	if (unlikely(PageHWPoison(vmf->page))) {
		vm_fault_t poisonret = VM_FAULT_HWPOISON;
		if (ret & VM_FAULT_LOCKED) {
			if (page_mapped(vmf->page))
				unmap_mapping_folio(folio);
			/* Retry if a clean folio was removed from the cache. */
			if (mapping_evict_folio(folio->mapping, folio))
				poisonret = VM_FAULT_NOPAGE;
			folio_unlock(folio);
		}
		folio_put(folio);
		vmf->page = NULL;
		return poisonret;
	}

	if (unlikely(!(ret & VM_FAULT_LOCKED)))
		folio_lock(folio);
	else
		VM_BUG_ON_PAGE(!folio_test_locked(folio), vmf->page);

	return ret;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
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

vm_fault_t do_set_pmd(struct vm_fault *vmf, struct page *page)
{
	struct folio *folio = page_folio(page);
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	pmd_t entry;
	vm_fault_t ret = VM_FAULT_FALLBACK;

	/*
	 * It is too late to allocate a small folio, we already have a large
	 * folio in the pagecache: especially s390 KVM cannot tolerate any
	 * PMD mappings, but PTE-mapped THP are fine. So let's simply refuse any
	 * PMD mappings if THPs are disabled.
	 */
	if (thp_disabled_by_hw() || vma_thp_disabled(vma, vma->vm_flags))
		return ret;

	if (!thp_vma_suitable_order(vma, haddr, PMD_ORDER))
		return ret;

	if (folio_order(folio) != HPAGE_PMD_ORDER)
		return ret;
	page = &folio->page;

	/*
	 * Just backoff if any subpage of a THP is corrupted otherwise
	 * the corrupted page may mapped by PMD silently to escape the
	 * check.  This kind of THP just can be PTE mapped.  Access to
	 * the corrupted subpage should trigger SIGBUS as expected.
	 */
	if (unlikely(folio_test_has_hwpoisoned(folio)))
		return ret;

	/*
	 * Archs like ppc64 need additional space to store information
	 * related to pte entry. Use the preallocated table for that.
	 */
	if (arch_needs_pgtable_deposit() && !vmf->prealloc_pte) {
		vmf->prealloc_pte = pte_alloc_one(vma->vm_mm);
		if (!vmf->prealloc_pte)
			return VM_FAULT_OOM;
	}

	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_none(*vmf->pmd)))
		goto out;

	flush_icache_pages(vma, page, HPAGE_PMD_NR);

	entry = mk_huge_pmd(page, vma->vm_page_prot);
	if (write)
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);

	add_mm_counter(vma->vm_mm, mm_counter_file(folio), HPAGE_PMD_NR);
	folio_add_file_rmap_pmd(folio, page, vma);

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
vm_fault_t do_set_pmd(struct vm_fault *vmf, struct page *page)
{
	return VM_FAULT_FALLBACK;
}
#endif

/**
 * set_pte_range - Set a range of PTEs to point to pages in a folio.
 * @vmf: Fault decription.
 * @folio: The folio that contains @page.
 * @page: The first page to create a PTE for.
 * @nr: The number of PTEs to create.
 * @addr: The first address to create a PTE for.
 */
void set_pte_range(struct vm_fault *vmf, struct folio *folio,
		struct page *page, unsigned int nr, unsigned long addr)
{
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	bool prefault = !in_range(vmf->address, addr, nr * PAGE_SIZE);
	pte_t entry;

	flush_icache_pages(vma, page, nr);
	entry = mk_pte(page, vma->vm_page_prot);

	if (prefault && arch_wants_old_prefaulted_pte())
		entry = pte_mkold(entry);
	else
		entry = pte_sw_mkyoung(entry);

	if (write)
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	if (unlikely(vmf_orig_pte_uffd_wp(vmf)))
		entry = pte_mkuffd_wp(entry);
	/* copy-on-write page */
	if (write && !(vma->vm_flags & VM_SHARED)) {
		VM_BUG_ON_FOLIO(nr != 1, folio);
		folio_add_new_anon_rmap(folio, vma, addr, RMAP_EXCLUSIVE);
		folio_add_lru_vma(folio, vma);
	} else {
		folio_add_file_rmap_ptes(folio, page, nr, vma);
	}
	set_ptes(vma->vm_mm, addr, vmf->pte, entry, nr);

	/* no need to invalidate: a not-present page won't be cached */
	update_mmu_cache_range(vmf, vma, addr, vmf->pte, nr);
}

static bool vmf_pte_changed(struct vm_fault *vmf)
{
	if (vmf->flags & FAULT_FLAG_ORIG_PTE_VALID)
		return !pte_same(ptep_get(vmf->pte), vmf->orig_pte);

	return !pte_none(ptep_get(vmf->pte));
}

/**
 * finish_fault - finish page fault once we have prepared the page to fault
 *
 * @vmf: structure describing the fault
 *
 * This function handles all that is needed to finish a page fault once the
 * page to fault in is prepared. It handles locking of PTEs, inserts PTE for
 * given page, adds reverse page mapping, handles memcg charges and LRU
 * addition.
 *
 * The function expects the page to be locked and on success it consumes a
 * reference of a page being mapped (for the PTE which maps it).
 *
 * Return: %0 on success, %VM_FAULT_ code in case of error.
 */
vm_fault_t finish_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page;
	struct folio *folio;
	vm_fault_t ret;
	bool is_cow = (vmf->flags & FAULT_FLAG_WRITE) &&
		      !(vma->vm_flags & VM_SHARED);
	int type, nr_pages;
	unsigned long addr;
	bool needs_fallback = false;

fallback:
	addr = vmf->address;

	/* Did we COW the page? */
	if (is_cow)
		page = vmf->cow_page;
	else
		page = vmf->page;

	/*
	 * check even for read faults because we might have lost our CoWed
	 * page
	 */
	if (!(vma->vm_flags & VM_SHARED)) {
		ret = check_stable_address_space(vma->vm_mm);
		if (ret)
			return ret;
	}

	if (pmd_none(*vmf->pmd)) {
		if (PageTransCompound(page)) {
			ret = do_set_pmd(vmf, page);
			if (ret != VM_FAULT_FALLBACK)
				return ret;
		}

		if (vmf->prealloc_pte)
			pmd_install(vma->vm_mm, vmf->pmd, &vmf->prealloc_pte);
		else if (unlikely(pte_alloc(vma->vm_mm, vmf->pmd)))
			return VM_FAULT_OOM;
	}

	folio = page_folio(page);
	nr_pages = folio_nr_pages(folio);

	/*
	 * Using per-page fault to maintain the uffd semantics, and same
	 * approach also applies to non-anonymous-shmem faults to avoid
	 * inflating the RSS of the process.
	 */
	if (!vma_is_anon_shmem(vma) || unlikely(userfaultfd_armed(vma)) ||
	    unlikely(needs_fallback)) {
		nr_pages = 1;
	} else if (nr_pages > 1) {
		pgoff_t idx = folio_page_idx(folio, page);
		/* The page offset of vmf->address within the VMA. */
		pgoff_t vma_off = vmf->pgoff - vmf->vma->vm_pgoff;
		/* The index of the entry in the pagetable for fault page. */
		pgoff_t pte_off = pte_index(vmf->address);

		/*
		 * Fallback to per-page fault in case the folio size in page
		 * cache beyond the VMA limits and PMD pagetable limits.
		 */
		if (unlikely(vma_off < idx ||
			    vma_off + (nr_pages - idx) > vma_pages(vma) ||
			    pte_off < idx ||
			    pte_off + (nr_pages - idx)  > PTRS_PER_PTE)) {
			nr_pages = 1;
		} else {
			/* Now we can set mappings for the whole large folio. */
			addr = vmf->address - idx * PAGE_SIZE;
			page = &folio->page;
		}
	}

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
				       addr, &vmf->ptl);
	if (!vmf->pte)
		return VM_FAULT_NOPAGE;

	/* Re-check under ptl */
	if (nr_pages == 1 && unlikely(vmf_pte_changed(vmf))) {
		update_mmu_tlb(vma, addr, vmf->pte);
		ret = VM_FAULT_NOPAGE;
		goto unlock;
	} else if (nr_pages > 1 && !pte_range_none(vmf->pte, nr_pages)) {
		needs_fallback = true;
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		goto fallback;
	}

	folio_ref_add(folio, nr_pages - 1);
	set_pte_range(vmf, folio, page, nr_pages, addr);
	type = is_cow ? MM_ANONPAGES : mm_counter_file(folio);
	add_mm_counter(vma->vm_mm, type, nr_pages);
	ret = 0;

unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
}

static unsigned long fault_around_pages __read_mostly =
	65536 >> PAGE_SHIFT;

#ifdef CONFIG_DEBUG_FS
static int fault_around_bytes_get(void *data, u64 *val)
{
	*val = fault_around_pages << PAGE_SHIFT;
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

	/*
	 * The minimum value is 1 page, however this results in no fault-around
	 * at all. See should_fault_around().
	 */
	val = max(val, PAGE_SIZE);
	fault_around_pages = rounddown_pow_of_two(val) >> PAGE_SHIFT;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fault_around_bytes_fops,
		fault_around_bytes_get, fault_around_bytes_set, "%llu\n");

static int __init fault_around_debugfs(void)
{
	debugfs_create_file_unsafe("fault_around_bytes", 0644, NULL, NULL,
				   &fault_around_bytes_fops);
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
 * This function doesn't cross VMA or page table boundaries, in order to call
 * map_pages() and acquire a PTE lock only once.
 *
 * fault_around_pages defines how many pages we'll try to map.
 * do_fault_around() expects it to be set to a power of two less than or equal
 * to PTRS_PER_PTE.
 *
 * The virtual address of the area that we map is naturally aligned to
 * fault_around_pages * PAGE_SIZE rounded down to the machine page size
 * (and therefore to page order).  This way it's easier to guarantee
 * that we don't cross page table boundaries.
 */
static vm_fault_t do_fault_around(struct vm_fault *vmf)
{
	pgoff_t nr_pages = READ_ONCE(fault_around_pages);
	pgoff_t pte_off = pte_index(vmf->address);
	/* The page offset of vmf->address within the VMA. */
	pgoff_t vma_off = vmf->pgoff - vmf->vma->vm_pgoff;
	pgoff_t from_pte, to_pte;
	vm_fault_t ret;

	/* The PTE offset of the start address, clamped to the VMA. */
	from_pte = max(ALIGN_DOWN(pte_off, nr_pages),
		       pte_off - min(pte_off, vma_off));

	/* The PTE offset of the end address, clamped to the VMA and PTE. */
	to_pte = min3(from_pte + nr_pages, (pgoff_t)PTRS_PER_PTE,
		      pte_off + vma_pages(vmf->vma) - vma_off) - 1;

	if (pmd_none(*vmf->pmd)) {
		vmf->prealloc_pte = pte_alloc_one(vmf->vma->vm_mm);
		if (!vmf->prealloc_pte)
			return VM_FAULT_OOM;
	}

	rcu_read_lock();
	ret = vmf->vma->vm_ops->map_pages(vmf,
			vmf->pgoff + from_pte - pte_off,
			vmf->pgoff + to_pte - pte_off);
	rcu_read_unlock();

	return ret;
}

/* Return true if we should do read fault-around, false otherwise */
static inline bool should_fault_around(struct vm_fault *vmf)
{
	/* No ->map_pages?  No way to fault around... */
	if (!vmf->vma->vm_ops->map_pages)
		return false;

	if (uffd_disable_fault_around(vmf->vma))
		return false;

	/* A single page implies no faulting 'around' at all. */
	return fault_around_pages > 1;
}

static vm_fault_t do_read_fault(struct vm_fault *vmf)
{
	vm_fault_t ret = 0;
	struct folio *folio;

	/*
	 * Let's call ->map_pages() first and use ->fault() as fallback
	 * if page by the offset is not ready to be mapped (cold cache or
	 * something).
	 */
	if (should_fault_around(vmf)) {
		ret = do_fault_around(vmf);
		if (ret)
			return ret;
	}

	ret = vmf_can_call_fault(vmf);
	if (ret)
		return ret;

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	ret |= finish_fault(vmf);
	folio = page_folio(vmf->page);
	folio_unlock(folio);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		folio_put(folio);
	return ret;
}

static vm_fault_t do_cow_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio;
	vm_fault_t ret;

	ret = vmf_can_call_fault(vmf);
	if (!ret)
		ret = vmf_anon_prepare(vmf);
	if (ret)
		return ret;

	folio = folio_prealloc(vma->vm_mm, vma, vmf->address, false);
	if (!folio)
		return VM_FAULT_OOM;

	vmf->cow_page = &folio->page;

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	if (ret & VM_FAULT_DONE_COW)
		return ret;

	if (copy_mc_user_highpage(vmf->cow_page, vmf->page, vmf->address, vma)) {
		ret = VM_FAULT_HWPOISON;
		goto unlock;
	}
	__folio_mark_uptodate(folio);

	ret |= finish_fault(vmf);
unlock:
	unlock_page(vmf->page);
	put_page(vmf->page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	return ret;
uncharge_out:
	folio_put(folio);
	return ret;
}

static vm_fault_t do_shared_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret, tmp;
	struct folio *folio;

	ret = vmf_can_call_fault(vmf);
	if (ret)
		return ret;

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	folio = page_folio(vmf->page);

	/*
	 * Check if the backing address space wants to know that the page is
	 * about to become writable
	 */
	if (vma->vm_ops->page_mkwrite) {
		folio_unlock(folio);
		tmp = do_page_mkwrite(vmf, folio);
		if (unlikely(!tmp ||
				(tmp & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
			folio_put(folio);
			return tmp;
		}
	}

	ret |= finish_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE |
					VM_FAULT_RETRY))) {
		folio_unlock(folio);
		folio_put(folio);
		return ret;
	}

	ret |= fault_dirty_shared_page(vmf);
	return ret;
}

/*
 * We enter with non-exclusive mmap_lock (to exclude vma changes,
 * but allow concurrent faults).
 * The mmap_lock may have been released depending on flags and our
 * return value.  See filemap_fault() and __folio_lock_or_retry().
 * If mmap_lock is released, vma may become invalid (for example
 * by other thread calling munmap()).
 */
static vm_fault_t do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *vm_mm = vma->vm_mm;
	vm_fault_t ret;

	/*
	 * The VMA was not fully populated on mmap() or missing VM_DONTEXPAND
	 */
	if (!vma->vm_ops->fault) {
		vmf->pte = pte_offset_map_lock(vmf->vma->vm_mm, vmf->pmd,
					       vmf->address, &vmf->ptl);
		if (unlikely(!vmf->pte))
			ret = VM_FAULT_SIGBUS;
		else {
			/*
			 * Make sure this is not a temporary clearing of pte
			 * by holding ptl and checking again. A R/M/W update
			 * of pte involves: take ptl, clearing the pte so that
			 * we don't have concurrent modification by hardware
			 * followed by an update.
			 */
			if (unlikely(pte_none(ptep_get(vmf->pte))))
				ret = VM_FAULT_SIGBUS;
			else
				ret = VM_FAULT_NOPAGE;

			pte_unmap_unlock(vmf->pte, vmf->ptl);
		}
	} else if (!(vmf->flags & FAULT_FLAG_WRITE))
		ret = do_read_fault(vmf);
	else if (!(vma->vm_flags & VM_SHARED))
		ret = do_cow_fault(vmf);
	else
		ret = do_shared_fault(vmf);

	/* preallocated pagetable is unused: free it */
	if (vmf->prealloc_pte) {
		pte_free(vm_mm, vmf->prealloc_pte);
		vmf->prealloc_pte = NULL;
	}
	return ret;
}

int numa_migrate_check(struct folio *folio, struct vm_fault *vmf,
		      unsigned long addr, int *flags,
		      bool writable, int *last_cpupid)
{
	struct vm_area_struct *vma = vmf->vma;

	/*
	 * Avoid grouping on RO pages in general. RO pages shouldn't hurt as
	 * much anyway since they can be in shared cache state. This misses
	 * the case where a mapping is writable but the process never writes
	 * to it but pte_write gets cleared during protection updates and
	 * pte_dirty has unpredictable behaviour between PTE scan updates,
	 * background writeback, dirty balancing and application behaviour.
	 */
	if (!writable)
		*flags |= TNF_NO_GROUP;

	/*
	 * Flag if the folio is shared between multiple address spaces. This
	 * is later used when determining whether to group tasks together
	 */
	if (folio_likely_mapped_shared(folio) && (vma->vm_flags & VM_SHARED))
		*flags |= TNF_SHARED;
	/*
	 * For memory tiering mode, cpupid of slow memory page is used
	 * to record page access time.  So use default value.
	 */
	if (folio_use_access_time(folio))
		*last_cpupid = (-1 & LAST_CPUPID_MASK);
	else
		*last_cpupid = folio_last_cpupid(folio);

	/* Record the current PID acceesing VMA */
	vma_set_access_pid_bit(vma);

	count_vm_numa_event(NUMA_HINT_FAULTS);
#ifdef CONFIG_NUMA_BALANCING
	count_memcg_folio_events(folio, NUMA_HINT_FAULTS, 1);
#endif
	if (folio_nid(folio) == numa_node_id()) {
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);
		*flags |= TNF_FAULT_LOCAL;
	}

	return mpol_misplaced(folio, vmf, addr);
}

static void numa_rebuild_single_mapping(struct vm_fault *vmf, struct vm_area_struct *vma,
					unsigned long fault_addr, pte_t *fault_pte,
					bool writable)
{
	pte_t pte, old_pte;

	old_pte = ptep_modify_prot_start(vma, fault_addr, fault_pte);
	pte = pte_modify(old_pte, vma->vm_page_prot);
	pte = pte_mkyoung(pte);
	if (writable)
		pte = pte_mkwrite(pte, vma);
	ptep_modify_prot_commit(vma, fault_addr, fault_pte, old_pte, pte);
	update_mmu_cache_range(vmf, vma, fault_addr, fault_pte, 1);
}

static void numa_rebuild_large_mapping(struct vm_fault *vmf, struct vm_area_struct *vma,
				       struct folio *folio, pte_t fault_pte,
				       bool ignore_writable, bool pte_write_upgrade)
{
	int nr = pte_pfn(fault_pte) - folio_pfn(folio);
	unsigned long start, end, addr = vmf->address;
	unsigned long addr_start = addr - (nr << PAGE_SHIFT);
	unsigned long pt_start = ALIGN_DOWN(addr, PMD_SIZE);
	pte_t *start_ptep;

	/* Stay within the VMA and within the page table. */
	start = max3(addr_start, pt_start, vma->vm_start);
	end = min3(addr_start + folio_size(folio), pt_start + PMD_SIZE,
		   vma->vm_end);
	start_ptep = vmf->pte - ((addr - start) >> PAGE_SHIFT);

	/* Restore all PTEs' mapping of the large folio */
	for (addr = start; addr != end; start_ptep++, addr += PAGE_SIZE) {
		pte_t ptent = ptep_get(start_ptep);
		bool writable = false;

		if (!pte_present(ptent) || !pte_protnone(ptent))
			continue;

		if (pfn_folio(pte_pfn(ptent)) != folio)
			continue;

		if (!ignore_writable) {
			ptent = pte_modify(ptent, vma->vm_page_prot);
			writable = pte_write(ptent);
			if (!writable && pte_write_upgrade &&
			    can_change_pte_writable(vma, addr, ptent))
				writable = true;
		}

		numa_rebuild_single_mapping(vmf, vma, addr, start_ptep, writable);
	}
}

static vm_fault_t do_numa_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio = NULL;
	int nid = NUMA_NO_NODE;
	bool writable = false, ignore_writable = false;
	bool pte_write_upgrade = vma_wants_manual_pte_write_upgrade(vma);
	int last_cpupid;
	int target_nid;
	pte_t pte, old_pte;
	int flags = 0, nr_pages;

	/*
	 * The pte cannot be used safely until we verify, while holding the page
	 * table lock, that its contents have not changed during fault handling.
	 */
	spin_lock(vmf->ptl);
	/* Read the live PTE from the page tables: */
	old_pte = ptep_get(vmf->pte);

	if (unlikely(!pte_same(old_pte, vmf->orig_pte))) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return 0;
	}

	pte = pte_modify(old_pte, vma->vm_page_prot);

	/*
	 * Detect now whether the PTE could be writable; this information
	 * is only valid while holding the PT lock.
	 */
	writable = pte_write(pte);
	if (!writable && pte_write_upgrade &&
	    can_change_pte_writable(vma, vmf->address, pte))
		writable = true;

	folio = vm_normal_folio(vma, vmf->address, pte);
	if (!folio || folio_is_zone_device(folio))
		goto out_map;

	nid = folio_nid(folio);
	nr_pages = folio_nr_pages(folio);

	target_nid = numa_migrate_check(folio, vmf, vmf->address, &flags,
					writable, &last_cpupid);
	if (target_nid == NUMA_NO_NODE)
		goto out_map;
	if (migrate_misplaced_folio_prepare(folio, vma, target_nid)) {
		flags |= TNF_MIGRATE_FAIL;
		goto out_map;
	}
	/* The folio is isolated and isolation code holds a folio reference. */
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	writable = false;
	ignore_writable = true;

	/* Migrate to the requested node */
	if (!migrate_misplaced_folio(folio, target_nid)) {
		nid = target_nid;
		flags |= TNF_MIGRATED;
		task_numa_fault(last_cpupid, nid, nr_pages, flags);
		return 0;
	}

	flags |= TNF_MIGRATE_FAIL;
	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
				       vmf->address, &vmf->ptl);
	if (unlikely(!vmf->pte))
		return 0;
	if (unlikely(!pte_same(ptep_get(vmf->pte), vmf->orig_pte))) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return 0;
	}
out_map:
	/*
	 * Make it present again, depending on how arch implements
	 * non-accessible ptes, some can allow access by kernel mode.
	 */
	if (folio && folio_test_large(folio))
		numa_rebuild_large_mapping(vmf, vma, folio, pte, ignore_writable,
					   pte_write_upgrade);
	else
		numa_rebuild_single_mapping(vmf, vma, vmf->address, vmf->pte,
					    writable);
	pte_unmap_unlock(vmf->pte, vmf->ptl);

	if (nid != NUMA_NO_NODE)
		task_numa_fault(last_cpupid, nid, nr_pages, flags);
	return 0;
}

static inline vm_fault_t create_huge_pmd(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	if (vma_is_anonymous(vma))
		return do_huge_pmd_anonymous_page(vmf);
	if (vma->vm_ops->huge_fault)
		return vma->vm_ops->huge_fault(vmf, PMD_ORDER);
	return VM_FAULT_FALLBACK;
}

/* `inline' is required to avoid gcc 4.1.2 build error */
static inline vm_fault_t wp_huge_pmd(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	const bool unshare = vmf->flags & FAULT_FLAG_UNSHARE;
	vm_fault_t ret;

	if (vma_is_anonymous(vma)) {
		if (likely(!unshare) &&
		    userfaultfd_huge_pmd_wp(vma, vmf->orig_pmd)) {
			if (userfaultfd_wp_async(vmf->vma))
				goto split;
			return handle_userfault(vmf, VM_UFFD_WP);
		}
		return do_huge_pmd_wp_page(vmf);
	}

	if (vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) {
		if (vma->vm_ops->huge_fault) {
			ret = vma->vm_ops->huge_fault(vmf, PMD_ORDER);
			if (!(ret & VM_FAULT_FALLBACK))
				return ret;
		}
	}

split:
	/* COW or write-notify handled on pte level: split pmd. */
	__split_huge_pmd(vma, vmf->pmd, vmf->address, false, NULL);

	return VM_FAULT_FALLBACK;
}

static vm_fault_t create_huge_pud(struct vm_fault *vmf)
{
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) &&			\
	defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
	struct vm_area_struct *vma = vmf->vma;
	/* No support for anonymous transparent PUD pages yet */
	if (vma_is_anonymous(vma))
		return VM_FAULT_FALLBACK;
	if (vma->vm_ops->huge_fault)
		return vma->vm_ops->huge_fault(vmf, PUD_ORDER);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
	return VM_FAULT_FALLBACK;
}

static vm_fault_t wp_huge_pud(struct vm_fault *vmf, pud_t orig_pud)
{
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) &&			\
	defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret;

	/* No support for anonymous transparent PUD pages yet */
	if (vma_is_anonymous(vma))
		goto split;
	if (vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) {
		if (vma->vm_ops->huge_fault) {
			ret = vma->vm_ops->huge_fault(vmf, PUD_ORDER);
			if (!(ret & VM_FAULT_FALLBACK))
				return ret;
		}
	}
split:
	/* COW or write-notify not handled on PUD level: split pud.*/
	__split_huge_pud(vma, vmf->pud, vmf->address);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE && CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
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
 * We enter with non-exclusive mmap_lock (to exclude vma changes, but allow
 * concurrent faults).
 *
 * The mmap_lock may have been released depending on flags and our return value.
 * See filemap_fault() and __folio_lock_or_retry().
 */
static vm_fault_t handle_pte_fault(struct vm_fault *vmf)
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
		vmf->flags &= ~FAULT_FLAG_ORIG_PTE_VALID;
	} else {
		pmd_t dummy_pmdval;

		/*
		 * A regular pmd is established and it can't morph into a huge
		 * pmd by anon khugepaged, since that takes mmap_lock in write
		 * mode; but shmem or file collapse to THP could still morph
		 * it into a huge pmd: just retry later if so.
		 *
		 * Use the maywrite version to indicate that vmf->pte may be
		 * modified, but since we will use pte_same() to detect the
		 * change of the !pte_none() entry, there is no need to recheck
		 * the pmdval. Here we chooes to pass a dummy variable instead
		 * of NULL, which helps new user think about why this place is
		 * special.
		 */
		vmf->pte = pte_offset_map_rw_nolock(vmf->vma->vm_mm, vmf->pmd,
						    vmf->address, &dummy_pmdval,
						    &vmf->ptl);
		if (unlikely(!vmf->pte))
			return 0;
		vmf->orig_pte = ptep_get_lockless(vmf->pte);
		vmf->flags |= FAULT_FLAG_ORIG_PTE_VALID;

		if (pte_none(vmf->orig_pte)) {
			pte_unmap(vmf->pte);
			vmf->pte = NULL;
		}
	}

	if (!vmf->pte)
		return do_pte_missing(vmf);

	if (!pte_present(vmf->orig_pte))
		return do_swap_page(vmf);

	if (pte_protnone(vmf->orig_pte) && vma_is_accessible(vmf->vma))
		return do_numa_page(vmf);

	spin_lock(vmf->ptl);
	entry = vmf->orig_pte;
	if (unlikely(!pte_same(ptep_get(vmf->pte), entry))) {
		update_mmu_tlb(vmf->vma, vmf->address, vmf->pte);
		goto unlock;
	}
	if (vmf->flags & (FAULT_FLAG_WRITE|FAULT_FLAG_UNSHARE)) {
		if (!pte_write(entry))
			return do_wp_page(vmf);
		else if (likely(vmf->flags & FAULT_FLAG_WRITE))
			entry = pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	if (ptep_set_access_flags(vmf->vma, vmf->address, vmf->pte, entry,
				vmf->flags & FAULT_FLAG_WRITE)) {
		update_mmu_cache_range(vmf, vmf->vma, vmf->address,
				vmf->pte, 1);
	} else {
		/* Skip spurious TLB flush for retried page fault */
		if (vmf->flags & FAULT_FLAG_TRIED)
			goto unlock;
		/*
		 * This is needed only for protection faults but the arch code
		 * is not yet telling us if this is a protection fault or not.
		 * This still avoids useless tlb flushes for .text page faults
		 * with threads.
		 */
		if (vmf->flags & FAULT_FLAG_WRITE)
			flush_tlb_fix_spurious_fault(vmf->vma, vmf->address,
						     vmf->pte);
	}
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return 0;
}

/*
 * On entry, we hold either the VMA lock or the mmap_lock
 * (FAULT_FLAG_VMA_LOCK tells you which).  If VM_FAULT_RETRY is set in
 * the result, the mmap_lock is not held on exit.  See filemap_fault()
 * and __folio_lock_or_retry().
 */
static vm_fault_t __handle_mm_fault(struct vm_area_struct *vma,
		unsigned long address, unsigned int flags)
{
	struct vm_fault vmf = {
		.vma = vma,
		.address = address & PAGE_MASK,
		.real_address = address,
		.flags = flags,
		.pgoff = linear_page_index(vma, address),
		.gfp_mask = __get_fault_gfp_mask(vma),
	};
	struct mm_struct *mm = vma->vm_mm;
	unsigned long vm_flags = vma->vm_flags;
	pgd_t *pgd;
	p4d_t *p4d;
	vm_fault_t ret;

	pgd = pgd_offset(mm, address);
	p4d = p4d_alloc(mm, pgd, address);
	if (!p4d)
		return VM_FAULT_OOM;

	vmf.pud = pud_alloc(mm, p4d, address);
	if (!vmf.pud)
		return VM_FAULT_OOM;
retry_pud:
	if (pud_none(*vmf.pud) &&
	    thp_vma_allowable_order(vma, vm_flags,
				TVA_IN_PF | TVA_ENFORCE_SYSFS, PUD_ORDER)) {
		ret = create_huge_pud(&vmf);
		if (!(ret & VM_FAULT_FALLBACK))
			return ret;
	} else {
		pud_t orig_pud = *vmf.pud;

		barrier();
		if (pud_trans_huge(orig_pud) || pud_devmap(orig_pud)) {

			/*
			 * TODO once we support anonymous PUDs: NUMA case and
			 * FAULT_FLAG_UNSHARE handling.
			 */
			if ((flags & FAULT_FLAG_WRITE) && !pud_write(orig_pud)) {
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

	/* Huge pud page fault raced with pmd_alloc? */
	if (pud_trans_unstable(vmf.pud))
		goto retry_pud;

	if (pmd_none(*vmf.pmd) &&
	    thp_vma_allowable_order(vma, vm_flags,
				TVA_IN_PF | TVA_ENFORCE_SYSFS, PMD_ORDER)) {
		ret = create_huge_pmd(&vmf);
		if (!(ret & VM_FAULT_FALLBACK))
			return ret;
	} else {
		vmf.orig_pmd = pmdp_get_lockless(vmf.pmd);

		if (unlikely(is_swap_pmd(vmf.orig_pmd))) {
			VM_BUG_ON(thp_migration_supported() &&
					  !is_pmd_migration_entry(vmf.orig_pmd));
			if (is_pmd_migration_entry(vmf.orig_pmd))
				pmd_migration_entry_wait(mm, vmf.pmd);
			return 0;
		}
		if (pmd_trans_huge(vmf.orig_pmd) || pmd_devmap(vmf.orig_pmd)) {
			if (pmd_protnone(vmf.orig_pmd) && vma_is_accessible(vma))
				return do_huge_pmd_numa_page(&vmf);

			if ((flags & (FAULT_FLAG_WRITE|FAULT_FLAG_UNSHARE)) &&
			    !pmd_write(vmf.orig_pmd)) {
				ret = wp_huge_pmd(&vmf);
				if (!(ret & VM_FAULT_FALLBACK))
					return ret;
			} else {
				huge_pmd_set_accessed(&vmf);
				return 0;
			}
		}
	}

	return handle_pte_fault(&vmf);
}

/**
 * mm_account_fault - Do page fault accounting
 * @mm: mm from which memcg should be extracted. It can be NULL.
 * @regs: the pt_regs struct pointer.  When set to NULL, will skip accounting
 *        of perf event counters, but we'll still do the per-task accounting to
 *        the task who triggered this page fault.
 * @address: the faulted address.
 * @flags: the fault flags.
 * @ret: the fault retcode.
 *
 * This will take care of most of the page fault accounting.  Meanwhile, it
 * will also include the PERF_COUNT_SW_PAGE_FAULTS_[MAJ|MIN] perf counter
 * updates.  However, note that the handling of PERF_COUNT_SW_PAGE_FAULTS should
 * still be in per-arch page fault handlers at the entry of page fault.
 */
static inline void mm_account_fault(struct mm_struct *mm, struct pt_regs *regs,
				    unsigned long address, unsigned int flags,
				    vm_fault_t ret)
{
	bool major;

	/* Incomplete faults will be accounted upon completion. */
	if (ret & VM_FAULT_RETRY)
		return;

	/*
	 * To preserve the behavior of older kernels, PGFAULT counters record
	 * both successful and failed faults, as opposed to perf counters,
	 * which ignore failed cases.
	 */
	count_vm_event(PGFAULT);
	count_memcg_event_mm(mm, PGFAULT);

	/*
	 * Do not account for unsuccessful faults (e.g. when the address wasn't
	 * valid).  That includes arch_vma_access_permitted() failing before
	 * reaching here. So this is not a "this many hardware page faults"
	 * counter.  We should use the hw profiling for that.
	 */
	if (ret & VM_FAULT_ERROR)
		return;

	/*
	 * We define the fault as a major fault when the final successful fault
	 * is VM_FAULT_MAJOR, or if it retried (which implies that we couldn't
	 * handle it immediately previously).
	 */
	major = (ret & VM_FAULT_MAJOR) || (flags & FAULT_FLAG_TRIED);

	if (major)
		current->maj_flt++;
	else
		current->min_flt++;

	/*
	 * If the fault is done for GUP, regs will be NULL.  We only do the
	 * accounting for the per thread fault counters who triggered the
	 * fault, and we skip the perf event updates.
	 */
	if (!regs)
		return;

	if (major)
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1, regs, address);
	else
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1, regs, address);
}

#ifdef CONFIG_LRU_GEN
static void lru_gen_enter_fault(struct vm_area_struct *vma)
{
	/* the LRU algorithm only applies to accesses with recency */
	current->in_lru_fault = vma_has_recency(vma);
}

static void lru_gen_exit_fault(void)
{
	current->in_lru_fault = false;
}
#else
static void lru_gen_enter_fault(struct vm_area_struct *vma)
{
}

static void lru_gen_exit_fault(void)
{
}
#endif /* CONFIG_LRU_GEN */

static vm_fault_t sanitize_fault_flags(struct vm_area_struct *vma,
				       unsigned int *flags)
{
	if (unlikely(*flags & FAULT_FLAG_UNSHARE)) {
		if (WARN_ON_ONCE(*flags & FAULT_FLAG_WRITE))
			return VM_FAULT_SIGSEGV;
		/*
		 * FAULT_FLAG_UNSHARE only applies to COW mappings. Let's
		 * just treat it like an ordinary read-fault otherwise.
		 */
		if (!is_cow_mapping(vma->vm_flags))
			*flags &= ~FAULT_FLAG_UNSHARE;
	} else if (*flags & FAULT_FLAG_WRITE) {
		/* Write faults on read-only mappings are impossible ... */
		if (WARN_ON_ONCE(!(vma->vm_flags & VM_MAYWRITE)))
			return VM_FAULT_SIGSEGV;
		/* ... and FOLL_FORCE only applies to COW mappings. */
		if (WARN_ON_ONCE(!(vma->vm_flags & VM_WRITE) &&
				 !is_cow_mapping(vma->vm_flags)))
			return VM_FAULT_SIGSEGV;
	}
#ifdef CONFIG_PER_VMA_LOCK
	/*
	 * Per-VMA locks can't be used with FAULT_FLAG_RETRY_NOWAIT because of
	 * the assumption that lock is dropped on VM_FAULT_RETRY.
	 */
	if (WARN_ON_ONCE((*flags &
			(FAULT_FLAG_VMA_LOCK | FAULT_FLAG_RETRY_NOWAIT)) ==
			(FAULT_FLAG_VMA_LOCK | FAULT_FLAG_RETRY_NOWAIT)))
		return VM_FAULT_SIGSEGV;
#endif

	return 0;
}

/*
 * By the time we get here, we already hold either the VMA lock or the
 * mmap_lock (FAULT_FLAG_VMA_LOCK tells you which).
 *
 * The mmap_lock may have been released depending on flags and our
 * return value.  See filemap_fault() and __folio_lock_or_retry().
 */
vm_fault_t handle_mm_fault(struct vm_area_struct *vma, unsigned long address,
			   unsigned int flags, struct pt_regs *regs)
{
	/* If the fault handler drops the mmap_lock, vma may be freed */
	struct mm_struct *mm = vma->vm_mm;
	vm_fault_t ret;
	bool is_droppable;

	__set_current_state(TASK_RUNNING);

	ret = sanitize_fault_flags(vma, &flags);
	if (ret)
		goto out;

	if (!arch_vma_access_permitted(vma, flags & FAULT_FLAG_WRITE,
					    flags & FAULT_FLAG_INSTRUCTION,
					    flags & FAULT_FLAG_REMOTE)) {
		ret = VM_FAULT_SIGSEGV;
		goto out;
	}

	is_droppable = !!(vma->vm_flags & VM_DROPPABLE);

	/*
	 * Enable the memcg OOM handling for faults triggered in user
	 * space.  Kernel faults are handled more gracefully.
	 */
	if (flags & FAULT_FLAG_USER)
		mem_cgroup_enter_user_fault();

	lru_gen_enter_fault(vma);

	if (unlikely(is_vm_hugetlb_page(vma)))
		ret = hugetlb_fault(vma->vm_mm, vma, address, flags);
	else
		ret = __handle_mm_fault(vma, address, flags);

	/*
	 * Warning: It is no longer safe to dereference vma-> after this point,
	 * because mmap_lock might have been dropped by __handle_mm_fault(), so
	 * vma might be destroyed from underneath us.
	 */

	lru_gen_exit_fault();

	/* If the mapping is droppable, then errors due to OOM aren't fatal. */
	if (is_droppable)
		ret &= ~VM_FAULT_OOM;

	if (flags & FAULT_FLAG_USER) {
		mem_cgroup_exit_user_fault();
		/*
		 * The task may have entered a memcg OOM situation but
		 * if the allocation error was handled gracefully (no
		 * VM_FAULT_OOM), there is no need to kill anything.
		 * Just clean up the OOM state peacefully.
		 */
		if (task_in_memcg_oom(current) && !(ret & VM_FAULT_OOM))
			mem_cgroup_oom_synchronize(false);
	}
out:
	mm_account_fault(mm, regs, address, flags, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(handle_mm_fault);

#ifdef CONFIG_LOCK_MM_AND_FIND_VMA
#include <linux/extable.h>

static inline bool get_mmap_lock_carefully(struct mm_struct *mm, struct pt_regs *regs)
{
	if (likely(mmap_read_trylock(mm)))
		return true;

	if (regs && !user_mode(regs)) {
		unsigned long ip = exception_ip(regs);
		if (!search_exception_tables(ip))
			return false;
	}

	return !mmap_read_lock_killable(mm);
}

static inline bool mmap_upgrade_trylock(struct mm_struct *mm)
{
	/*
	 * We don't have this operation yet.
	 *
	 * It should be easy enough to do: it's basically a
	 *    atomic_long_try_cmpxchg_acquire()
	 * from RWSEM_READER_BIAS -> RWSEM_WRITER_LOCKED, but
	 * it also needs the proper lockdep magic etc.
	 */
	return false;
}

static inline bool upgrade_mmap_lock_carefully(struct mm_struct *mm, struct pt_regs *regs)
{
	mmap_read_unlock(mm);
	if (regs && !user_mode(regs)) {
		unsigned long ip = exception_ip(regs);
		if (!search_exception_tables(ip))
			return false;
	}
	return !mmap_write_lock_killable(mm);
}

/*
 * Helper for page fault handling.
 *
 * This is kind of equivalent to "mmap_read_lock()" followed
 * by "find_extend_vma()", except it's a lot more careful about
 * the locking (and will drop the lock on failure).
 *
 * For example, if we have a kernel bug that causes a page
 * fault, we don't want to just use mmap_read_lock() to get
 * the mm lock, because that would deadlock if the bug were
 * to happen while we're holding the mm lock for writing.
 *
 * So this checks the exception tables on kernel faults in
 * order to only do this all for instructions that are actually
 * expected to fault.
 *
 * We can also actually take the mm lock for writing if we
 * need to extend the vma, which helps the VM layer a lot.
 */
struct vm_area_struct *lock_mm_and_find_vma(struct mm_struct *mm,
			unsigned long addr, struct pt_regs *regs)
{
	struct vm_area_struct *vma;

	if (!get_mmap_lock_carefully(mm, regs))
		return NULL;

	vma = find_vma(mm, addr);
	if (likely(vma && (vma->vm_start <= addr)))
		return vma;

	/*
	 * Well, dang. We might still be successful, but only
	 * if we can extend a vma to do so.
	 */
	if (!vma || !(vma->vm_flags & VM_GROWSDOWN)) {
		mmap_read_unlock(mm);
		return NULL;
	}

	/*
	 * We can try to upgrade the mmap lock atomically,
	 * in which case we can continue to use the vma
	 * we already looked up.
	 *
	 * Otherwise we'll have to drop the mmap lock and
	 * re-take it, and also look up the vma again,
	 * re-checking it.
	 */
	if (!mmap_upgrade_trylock(mm)) {
		if (!upgrade_mmap_lock_carefully(mm, regs))
			return NULL;

		vma = find_vma(mm, addr);
		if (!vma)
			goto fail;
		if (vma->vm_start <= addr)
			goto success;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto fail;
	}

	if (expand_stack_locked(vma, addr))
		goto fail;

success:
	mmap_write_downgrade(mm);
	return vma;

fail:
	mmap_write_unlock(mm);
	return NULL;
}
#endif

#ifdef CONFIG_PER_VMA_LOCK
/*
 * Lookup and lock a VMA under RCU protection. Returned VMA is guaranteed to be
 * stable and not isolated. If the VMA is not found or is being modified the
 * function returns NULL.
 */
struct vm_area_struct *lock_vma_under_rcu(struct mm_struct *mm,
					  unsigned long address)
{
	MA_STATE(mas, &mm->mm_mt, address, address);
	struct vm_area_struct *vma;

	rcu_read_lock();
retry:
	vma = mas_walk(&mas);
	if (!vma)
		goto inval;

	if (!vma_start_read(vma))
		goto inval;

	/* Check if the VMA got isolated after we found it */
	if (vma->detached) {
		vma_end_read(vma);
		count_vm_vma_lock_event(VMA_LOCK_MISS);
		/* The area was replaced with another one */
		goto retry;
	}
	/*
	 * At this point, we have a stable reference to a VMA: The VMA is
	 * locked and we know it hasn't already been isolated.
	 * From here on, we can access the VMA without worrying about which
	 * fields are accessible for RCU readers.
	 */

	/* Check since vm_start/vm_end might change before we lock the VMA */
	if (unlikely(address < vma->vm_start || address >= vma->vm_end))
		goto inval_end_read;

	rcu_read_unlock();
	return vma;

inval_end_read:
	vma_end_read(vma);
inval:
	rcu_read_unlock();
	count_vm_vma_lock_event(VMA_LOCK_ABORT);
	return NULL;
}
#endif /* CONFIG_PER_VMA_LOCK */

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

	spin_lock(&mm->page_table_lock);
	if (pgd_present(*pgd)) {	/* Another has populated it */
		p4d_free(mm, new);
	} else {
		smp_wmb(); /* See comment in pmd_install() */
		pgd_populate(mm, pgd, new);
	}
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

	spin_lock(&mm->page_table_lock);
	if (!p4d_present(*p4d)) {
		mm_inc_nr_puds(mm);
		smp_wmb(); /* See comment in pmd_install() */
		p4d_populate(mm, p4d, new);
	} else	/* Another has populated it */
		pud_free(mm, new);
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

	ptl = pud_lock(mm, pud);
	if (!pud_present(*pud)) {
		mm_inc_nr_pmds(mm);
		smp_wmb(); /* See comment in pmd_install() */
		pud_populate(mm, pud, new);
	} else {	/* Another has populated it */
		pmd_free(mm, new);
	}
	spin_unlock(ptl);
	return 0;
}
#endif /* __PAGETABLE_PMD_FOLDED */

static inline void pfnmap_args_setup(struct follow_pfnmap_args *args,
				     spinlock_t *lock, pte_t *ptep,
				     pgprot_t pgprot, unsigned long pfn_base,
				     unsigned long addr_mask, bool writable,
				     bool special)
{
	args->lock = lock;
	args->ptep = ptep;
	args->pfn = pfn_base + ((args->address & ~addr_mask) >> PAGE_SHIFT);
	args->pgprot = pgprot;
	args->writable = writable;
	args->special = special;
}

static inline void pfnmap_lockdep_assert(struct vm_area_struct *vma)
{
#ifdef CONFIG_LOCKDEP
	struct file *file = vma->vm_file;
	struct address_space *mapping = file ? file->f_mapping : NULL;

	if (mapping)
		lockdep_assert(lockdep_is_held(&mapping->i_mmap_rwsem) ||
			       lockdep_is_held(&vma->vm_mm->mmap_lock));
	else
		lockdep_assert(lockdep_is_held(&vma->vm_mm->mmap_lock));
#endif
}

/**
 * follow_pfnmap_start() - Look up a pfn mapping at a user virtual address
 * @args: Pointer to struct @follow_pfnmap_args
 *
 * The caller needs to setup args->vma and args->address to point to the
 * virtual address as the target of such lookup.  On a successful return,
 * the results will be put into other output fields.
 *
 * After the caller finished using the fields, the caller must invoke
 * another follow_pfnmap_end() to proper releases the locks and resources
 * of such look up request.
 *
 * During the start() and end() calls, the results in @args will be valid
 * as proper locks will be held.  After the end() is called, all the fields
 * in @follow_pfnmap_args will be invalid to be further accessed.  Further
 * use of such information after end() may require proper synchronizations
 * by the caller with page table updates, otherwise it can create a
 * security bug.
 *
 * If the PTE maps a refcounted page, callers are responsible to protect
 * against invalidation with MMU notifiers; otherwise access to the PFN at
 * a later point in time can trigger use-after-free.
 *
 * Only IO mappings and raw PFN mappings are allowed.  The mmap semaphore
 * should be taken for read, and the mmap semaphore cannot be released
 * before the end() is invoked.
 *
 * This function must not be used to modify PTE content.
 *
 * Return: zero on success, negative otherwise.
 */
int follow_pfnmap_start(struct follow_pfnmap_args *args)
{
	struct vm_area_struct *vma = args->vma;
	unsigned long address = args->address;
	struct mm_struct *mm = vma->vm_mm;
	spinlock_t *lock;
	pgd_t *pgdp;
	p4d_t *p4dp, p4d;
	pud_t *pudp, pud;
	pmd_t *pmdp, pmd;
	pte_t *ptep, pte;

	pfnmap_lockdep_assert(vma);

	if (unlikely(address < vma->vm_start || address >= vma->vm_end))
		goto out;

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		goto out;
retry:
	pgdp = pgd_offset(mm, address);
	if (pgd_none(*pgdp) || unlikely(pgd_bad(*pgdp)))
		goto out;

	p4dp = p4d_offset(pgdp, address);
	p4d = READ_ONCE(*p4dp);
	if (p4d_none(p4d) || unlikely(p4d_bad(p4d)))
		goto out;

	pudp = pud_offset(p4dp, address);
	pud = READ_ONCE(*pudp);
	if (pud_none(pud))
		goto out;
	if (pud_leaf(pud)) {
		lock = pud_lock(mm, pudp);
		if (!unlikely(pud_leaf(pud))) {
			spin_unlock(lock);
			goto retry;
		}
		pfnmap_args_setup(args, lock, NULL, pud_pgprot(pud),
				  pud_pfn(pud), PUD_MASK, pud_write(pud),
				  pud_special(pud));
		return 0;
	}

	pmdp = pmd_offset(pudp, address);
	pmd = pmdp_get_lockless(pmdp);
	if (pmd_leaf(pmd)) {
		lock = pmd_lock(mm, pmdp);
		if (!unlikely(pmd_leaf(pmd))) {
			spin_unlock(lock);
			goto retry;
		}
		pfnmap_args_setup(args, lock, NULL, pmd_pgprot(pmd),
				  pmd_pfn(pmd), PMD_MASK, pmd_write(pmd),
				  pmd_special(pmd));
		return 0;
	}

	ptep = pte_offset_map_lock(mm, pmdp, address, &lock);
	if (!ptep)
		goto out;
	pte = ptep_get(ptep);
	if (!pte_present(pte))
		goto unlock;
	pfnmap_args_setup(args, lock, ptep, pte_pgprot(pte),
			  pte_pfn(pte), PAGE_MASK, pte_write(pte),
			  pte_special(pte));
	return 0;
unlock:
	pte_unmap_unlock(ptep, lock);
out:
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(follow_pfnmap_start);

/**
 * follow_pfnmap_end(): End a follow_pfnmap_start() process
 * @args: Pointer to struct @follow_pfnmap_args
 *
 * Must be used in pair of follow_pfnmap_start().  See the start() function
 * above for more information.
 */
void follow_pfnmap_end(struct follow_pfnmap_args *args)
{
	if (args->lock)
		spin_unlock(args->lock);
	if (args->ptep)
		pte_unmap(args->ptep);
}
EXPORT_SYMBOL_GPL(follow_pfnmap_end);

#ifdef CONFIG_HAVE_IOREMAP_PROT
/**
 * generic_access_phys - generic implementation for iomem mmap access
 * @vma: the vma to access
 * @addr: userspace address, not relative offset within @vma
 * @buf: buffer to read/write
 * @len: length of transfer
 * @write: set to FOLL_WRITE when writing, otherwise reading
 *
 * This is a generic implementation for &vm_operations_struct.access for an
 * iomem mapping. This callback is used by access_process_vm() when the @vma is
 * not page based.
 */
int generic_access_phys(struct vm_area_struct *vma, unsigned long addr,
			void *buf, int len, int write)
{
	resource_size_t phys_addr;
	unsigned long prot = 0;
	void __iomem *maddr;
	int offset = offset_in_page(addr);
	int ret = -EINVAL;
	bool writable;
	struct follow_pfnmap_args args = { .vma = vma, .address = addr };

retry:
	if (follow_pfnmap_start(&args))
		return -EINVAL;
	prot = pgprot_val(args.pgprot);
	phys_addr = (resource_size_t)args.pfn << PAGE_SHIFT;
	writable = args.writable;
	follow_pfnmap_end(&args);

	if ((write & FOLL_WRITE) && !writable)
		return -EINVAL;

	maddr = ioremap_prot(phys_addr, PAGE_ALIGN(len + offset), prot);
	if (!maddr)
		return -ENOMEM;

	if (follow_pfnmap_start(&args))
		goto out_unmap;

	if ((prot != pgprot_val(args.pgprot)) ||
	    (phys_addr != (args.pfn << PAGE_SHIFT)) ||
	    (writable != args.writable)) {
		follow_pfnmap_end(&args);
		iounmap(maddr);
		goto retry;
	}

	if (write)
		memcpy_toio(maddr + offset, buf, len);
	else
		memcpy_fromio(buf, maddr + offset, len);
	ret = len;
	follow_pfnmap_end(&args);
out_unmap:
	iounmap(maddr);

	return ret;
}
EXPORT_SYMBOL_GPL(generic_access_phys);
#endif

/*
 * Access another process' address space as given in mm.
 */
static int __access_remote_vm(struct mm_struct *mm, unsigned long addr,
			      void *buf, int len, unsigned int gup_flags)
{
	void *old_buf = buf;
	int write = gup_flags & FOLL_WRITE;

	if (mmap_read_lock_killable(mm))
		return 0;

	/* Untag the address before looking up the VMA */
	addr = untagged_addr_remote(mm, addr);

	/* Avoid triggering the temporary warning in __get_user_pages */
	if (!vma_lookup(mm, addr) && !expand_stack(mm, addr))
		return 0;

	/* ignore errors, just check how much was successfully transferred */
	while (len) {
		int bytes, offset;
		void *maddr;
		struct vm_area_struct *vma = NULL;
		struct page *page = get_user_page_vma_remote(mm, addr,
							     gup_flags, &vma);

		if (IS_ERR(page)) {
			/* We might need to expand the stack to access it */
			vma = vma_lookup(mm, addr);
			if (!vma) {
				vma = expand_stack(mm, addr);

				/* mmap_lock was dropped on failure */
				if (!vma)
					return buf - old_buf;

				/* Try again if stack expansion worked */
				continue;
			}

			/*
			 * Check if this is a VM_IO | VM_PFNMAP VMA, which
			 * we can access using slightly different code.
			 */
			bytes = 0;
#ifdef CONFIG_HAVE_IOREMAP_PROT
			if (vma->vm_ops && vma->vm_ops->access)
				bytes = vma->vm_ops->access(vma, addr, buf,
							    len, write);
#endif
			if (bytes <= 0)
				break;
		} else {
			bytes = len;
			offset = addr & (PAGE_SIZE-1);
			if (bytes > PAGE_SIZE-offset)
				bytes = PAGE_SIZE-offset;

			maddr = kmap_local_page(page);
			if (write) {
				copy_to_user_page(vma, page, addr,
						  maddr + offset, buf, bytes);
				set_page_dirty_lock(page);
			} else {
				copy_from_user_page(vma, page, addr,
						    buf, maddr + offset, bytes);
			}
			unmap_and_put_page(page, maddr);
		}
		len -= bytes;
		buf += bytes;
		addr += bytes;
	}
	mmap_read_unlock(mm);

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
 *
 * Return: number of bytes copied from source to destination.
 */
int access_remote_vm(struct mm_struct *mm, unsigned long addr,
		void *buf, int len, unsigned int gup_flags)
{
	return __access_remote_vm(mm, addr, buf, len, gup_flags);
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

	ret = __access_remote_vm(mm, addr, buf, len, gup_flags);

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
	if (!mmap_read_trylock(mm))
		return;

	vma = vma_lookup(mm, ip);
	if (vma && vma->vm_file) {
		struct file *f = vma->vm_file;
		ip -= vma->vm_start;
		ip += vma->vm_pgoff << PAGE_SHIFT;
		printk("%s%pD[%lx,%lx+%lx]", prefix, f, ip,
				vma->vm_start,
				vma->vm_end - vma->vm_start);
	}
	mmap_read_unlock(mm);
}

#if defined(CONFIG_PROVE_LOCKING) || defined(CONFIG_DEBUG_ATOMIC_SLEEP)
void __might_fault(const char *file, int line)
{
	if (pagefault_disabled())
		return;
	__might_sleep(file, line);
#if defined(CONFIG_DEBUG_ATOMIC_SLEEP)
	if (current->mm)
		might_lock_read(&current->mm->mmap_lock);
#endif
}
EXPORT_SYMBOL(__might_fault);
#endif

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_HUGETLBFS)
/*
 * Process all subpages of the specified huge page with the specified
 * operation.  The target subpage will be processed last to keep its
 * cache lines hot.
 */
static inline int process_huge_page(
	unsigned long addr_hint, unsigned int nr_pages,
	int (*process_subpage)(unsigned long addr, int idx, void *arg),
	void *arg)
{
	int i, n, base, l, ret;
	unsigned long addr = addr_hint &
		~(((unsigned long)nr_pages << PAGE_SHIFT) - 1);

	/* Process target subpage last to keep its cache lines hot */
	might_sleep();
	n = (addr_hint - addr) / PAGE_SIZE;
	if (2 * n <= nr_pages) {
		/* If target subpage in first half of huge page */
		base = 0;
		l = n;
		/* Process subpages at the end of huge page */
		for (i = nr_pages - 1; i >= 2 * n; i--) {
			cond_resched();
			ret = process_subpage(addr + i * PAGE_SIZE, i, arg);
			if (ret)
				return ret;
		}
	} else {
		/* If target subpage in second half of huge page */
		base = nr_pages - 2 * (nr_pages - n);
		l = nr_pages - n;
		/* Process subpages at the begin of huge page */
		for (i = 0; i < base; i++) {
			cond_resched();
			ret = process_subpage(addr + i * PAGE_SIZE, i, arg);
			if (ret)
				return ret;
		}
	}
	/*
	 * Process remaining subpages in left-right-left-right pattern
	 * towards the target subpage
	 */
	for (i = 0; i < l; i++) {
		int left_idx = base + i;
		int right_idx = base + 2 * l - 1 - i;

		cond_resched();
		ret = process_subpage(addr + left_idx * PAGE_SIZE, left_idx, arg);
		if (ret)
			return ret;
		cond_resched();
		ret = process_subpage(addr + right_idx * PAGE_SIZE, right_idx, arg);
		if (ret)
			return ret;
	}
	return 0;
}

static void clear_gigantic_page(struct folio *folio, unsigned long addr_hint,
				unsigned int nr_pages)
{
	unsigned long addr = ALIGN_DOWN(addr_hint, folio_size(folio));
	int i;

	might_sleep();
	for (i = 0; i < nr_pages; i++) {
		cond_resched();
		clear_user_highpage(folio_page(folio, i), addr + i * PAGE_SIZE);
	}
}

static int clear_subpage(unsigned long addr, int idx, void *arg)
{
	struct folio *folio = arg;

	clear_user_highpage(folio_page(folio, idx), addr);
	return 0;
}

/**
 * folio_zero_user - Zero a folio which will be mapped to userspace.
 * @folio: The folio to zero.
 * @addr_hint: The address will be accessed or the base address if uncelar.
 */
void folio_zero_user(struct folio *folio, unsigned long addr_hint)
{
	unsigned int nr_pages = folio_nr_pages(folio);

	if (unlikely(nr_pages > MAX_ORDER_NR_PAGES))
		clear_gigantic_page(folio, addr_hint, nr_pages);
	else
		process_huge_page(addr_hint, nr_pages, clear_subpage, folio);
}

static int copy_user_gigantic_page(struct folio *dst, struct folio *src,
				   unsigned long addr_hint,
				   struct vm_area_struct *vma,
				   unsigned int nr_pages)
{
	unsigned long addr = ALIGN_DOWN(addr_hint, folio_size(dst));
	struct page *dst_page;
	struct page *src_page;
	int i;

	for (i = 0; i < nr_pages; i++) {
		dst_page = folio_page(dst, i);
		src_page = folio_page(src, i);

		cond_resched();
		if (copy_mc_user_highpage(dst_page, src_page,
					  addr + i*PAGE_SIZE, vma))
			return -EHWPOISON;
	}
	return 0;
}

struct copy_subpage_arg {
	struct folio *dst;
	struct folio *src;
	struct vm_area_struct *vma;
};

static int copy_subpage(unsigned long addr, int idx, void *arg)
{
	struct copy_subpage_arg *copy_arg = arg;
	struct page *dst = folio_page(copy_arg->dst, idx);
	struct page *src = folio_page(copy_arg->src, idx);

	if (copy_mc_user_highpage(dst, src, addr, copy_arg->vma))
		return -EHWPOISON;
	return 0;
}

int copy_user_large_folio(struct folio *dst, struct folio *src,
			  unsigned long addr_hint, struct vm_area_struct *vma)
{
	unsigned int nr_pages = folio_nr_pages(dst);
	struct copy_subpage_arg arg = {
		.dst = dst,
		.src = src,
		.vma = vma,
	};

	if (unlikely(nr_pages > MAX_ORDER_NR_PAGES))
		return copy_user_gigantic_page(dst, src, addr_hint, vma, nr_pages);

	return process_huge_page(addr_hint, nr_pages, copy_subpage, &arg);
}

long copy_folio_from_user(struct folio *dst_folio,
			   const void __user *usr_src,
			   bool allow_pagefault)
{
	void *kaddr;
	unsigned long i, rc = 0;
	unsigned int nr_pages = folio_nr_pages(dst_folio);
	unsigned long ret_val = nr_pages * PAGE_SIZE;
	struct page *subpage;

	for (i = 0; i < nr_pages; i++) {
		subpage = folio_page(dst_folio, i);
		kaddr = kmap_local_page(subpage);
		if (!allow_pagefault)
			pagefault_disable();
		rc = copy_from_user(kaddr, usr_src + i * PAGE_SIZE, PAGE_SIZE);
		if (!allow_pagefault)
			pagefault_enable();
		kunmap_local(kaddr);

		ret_val -= (PAGE_SIZE - rc);
		if (rc)
			break;

		flush_dcache_page(subpage);

		cond_resched();
	}
	return ret_val;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE || CONFIG_HUGETLBFS */

#if defined(CONFIG_SPLIT_PTE_PTLOCKS) && ALLOC_SPLIT_PTLOCKS

static struct kmem_cache *page_ptl_cachep;

void __init ptlock_cache_init(void)
{
	page_ptl_cachep = kmem_cache_create("page->ptl", sizeof(spinlock_t), 0,
			SLAB_PANIC, NULL);
}

bool ptlock_alloc(struct ptdesc *ptdesc)
{
	spinlock_t *ptl;

	ptl = kmem_cache_alloc(page_ptl_cachep, GFP_KERNEL);
	if (!ptl)
		return false;
	ptdesc->ptl = ptl;
	return true;
}

void ptlock_free(struct ptdesc *ptdesc)
{
	if (ptdesc->ptl)
		kmem_cache_free(page_ptl_cachep, ptdesc->ptl);
}
#endif

void vma_pgtable_walk_begin(struct vm_area_struct *vma)
{
	if (is_vm_hugetlb_page(vma))
		hugetlb_vma_lock_read(vma);
}

void vma_pgtable_walk_end(struct vm_area_struct *vma)
{
	if (is_vm_hugetlb_page(vma))
		hugetlb_vma_unlock_read(vma);
}
