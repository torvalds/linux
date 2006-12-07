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
 * 		Idea by Alex Bligh (alex@cconcepts.co.uk)
 *
 * 16.07.99  -  Support of BIGMEM added by Gerhard Wichert, Siemens AG
 *		(Gerhard.Wichert@pdb.siemens.de)
 *
 * Aug/Sep 2004 Changed to four level page tables (Andi Kleen)
 */

#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/module.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/writeback.h>

#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <linux/swapops.h>
#include <linux/elf.h>

#ifndef CONFIG_NEED_MULTIPLE_NODES
/* use the per-pgdat data instead for discontigmem - mbligh */
unsigned long max_mapnr;
struct page *mem_map;

EXPORT_SYMBOL(max_mapnr);
EXPORT_SYMBOL(mem_map);
#endif

unsigned long num_physpages;
/*
 * A number of key systems in x86 including ioremap() rely on the assumption
 * that high_memory defines the upper bound on direct map memory, then end
 * of ZONE_NORMAL.  Under CONFIG_DISCONTIG this means that max_low_pfn and
 * highstart_pfn must be the same; there must be no gap between ZONE_NORMAL
 * and ZONE_HIGHMEM.
 */
void * high_memory;
unsigned long vmalloc_earlyreserve;

EXPORT_SYMBOL(num_physpages);
EXPORT_SYMBOL(high_memory);
EXPORT_SYMBOL(vmalloc_earlyreserve);

int randomize_va_space __read_mostly = 1;

static int __init disable_randmaps(char *s)
{
	randomize_va_space = 0;
	return 1;
}
__setup("norandmaps", disable_randmaps);


/*
 * If a p?d_bad entry is found while walking page tables, report
 * the error, before resetting entry to p?d_none.  Usually (but
 * very seldom) called out from the p?d_none_or_clear_bad macros.
 */

void pgd_clear_bad(pgd_t *pgd)
{
	pgd_ERROR(*pgd);
	pgd_clear(pgd);
}

void pud_clear_bad(pud_t *pud)
{
	pud_ERROR(*pud);
	pud_clear(pud);
}

void pmd_clear_bad(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
}

/*
 * Note: this doesn't free the actual pages themselves. That
 * has been handled earlier when unmapping all the memory regions.
 */
static void free_pte_range(struct mmu_gather *tlb, pmd_t *pmd)
{
	struct page *page = pmd_page(*pmd);
	pmd_clear(pmd);
	pte_lock_deinit(page);
	pte_free_tlb(tlb, page);
	dec_zone_page_state(page, NR_PAGETABLE);
	tlb->mm->nr_ptes--;
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
		free_pte_range(tlb, pmd);
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
	pmd_free_tlb(tlb, pmd);
}

static inline void free_pud_range(struct mmu_gather *tlb, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		free_pmd_range(tlb, pud, addr, next, floor, ceiling);
	} while (pud++, addr = next, addr != end);

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

	pud = pud_offset(pgd, start);
	pgd_clear(pgd);
	pud_free_tlb(tlb, pud);
}

/*
 * This function frees user-level page tables of a process.
 *
 * Must be called with pagetable lock held.
 */
void free_pgd_range(struct mmu_gather **tlb,
			unsigned long addr, unsigned long end,
			unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long start;

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

	start = addr;
	pgd = pgd_offset((*tlb)->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		free_pud_range(*tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);

	if (!(*tlb)->fullmm)
		flush_tlb_pgtables((*tlb)->mm, start, end);
}

void free_pgtables(struct mmu_gather **tlb, struct vm_area_struct *vma,
		unsigned long floor, unsigned long ceiling)
{
	while (vma) {
		struct vm_area_struct *next = vma->vm_next;
		unsigned long addr = vma->vm_start;

		/*
		 * Hide vma from rmap and vmtruncate before freeing pgtables
		 */
		anon_vma_unlink(vma);
		unlink_file_vma(vma);

		if (is_vm_hugetlb_page(vma)) {
			hugetlb_free_pgd_range(tlb, addr, vma->vm_end,
				floor, next? next->vm_start: ceiling);
		} else {
			/*
			 * Optimization: gather nearby vmas into one call down
			 */
			while (next && next->vm_start <= vma->vm_end + PMD_SIZE
			       && !is_vm_hugetlb_page(next)) {
				vma = next;
				next = vma->vm_next;
				anon_vma_unlink(vma);
				unlink_file_vma(vma);
			}
			free_pgd_range(tlb, addr, vma->vm_end,
				floor, next? next->vm_start: ceiling);
		}
		vma = next;
	}
}

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd, unsigned long address)
{
	struct page *new = pte_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	pte_lock_init(new);
	spin_lock(&mm->page_table_lock);
	if (pmd_present(*pmd)) {	/* Another has populated it */
		pte_lock_deinit(new);
		pte_free(new);
	} else {
		mm->nr_ptes++;
		inc_zone_page_state(new, NR_PAGETABLE);
		pmd_populate(mm, pmd, new);
	}
	spin_unlock(&mm->page_table_lock);
	return 0;
}

int __pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	pte_t *new = pte_alloc_one_kernel(&init_mm, address);
	if (!new)
		return -ENOMEM;

	spin_lock(&init_mm.page_table_lock);
	if (pmd_present(*pmd))		/* Another has populated it */
		pte_free_kernel(new);
	else
		pmd_populate_kernel(&init_mm, pmd, new);
	spin_unlock(&init_mm.page_table_lock);
	return 0;
}

static inline void add_mm_rss(struct mm_struct *mm, int file_rss, int anon_rss)
{
	if (file_rss)
		add_mm_counter(mm, file_rss, file_rss);
	if (anon_rss)
		add_mm_counter(mm, anon_rss, anon_rss);
}

/*
 * This function is called to print an error when a bad pte
 * is found. For example, we might have a PFN-mapped pte in
 * a region that doesn't allow it.
 *
 * The calling function must still handle the error.
 */
void print_bad_pte(struct vm_area_struct *vma, pte_t pte, unsigned long vaddr)
{
	printk(KERN_ERR "Bad pte = %08llx, process = %s, "
			"vm_flags = %lx, vaddr = %lx\n",
		(long long)pte_val(pte),
		(vma->vm_mm == current->mm ? current->comm : "???"),
		vma->vm_flags, vaddr);
	dump_stack();
}

static inline int is_cow_mapping(unsigned int flags)
{
	return (flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
}

/*
 * This function gets the "struct page" associated with a pte.
 *
 * NOTE! Some mappings do not have "struct pages". A raw PFN mapping
 * will have each page table entry just pointing to a raw page frame
 * number, and as far as the VM layer is concerned, those do not have
 * pages associated with them - even if the PFN might point to memory
 * that otherwise is perfectly fine and has a "struct page".
 *
 * The way we recognize those mappings is through the rules set up
 * by "remap_pfn_range()": the vma will have the VM_PFNMAP bit set,
 * and the vm_pgoff will point to the first PFN mapped: thus every
 * page that is a raw mapping will always honor the rule
 *
 *	pfn_of_page == vma->vm_pgoff + ((addr - vma->vm_start) >> PAGE_SHIFT)
 *
 * and if that isn't true, the page has been COW'ed (in which case it
 * _does_ have a "struct page" associated with it even if it is in a
 * VM_PFNMAP range).
 */
struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);

	if (unlikely(vma->vm_flags & VM_PFNMAP)) {
		unsigned long off = (addr - vma->vm_start) >> PAGE_SHIFT;
		if (pfn == vma->vm_pgoff + off)
			return NULL;
		if (!is_cow_mapping(vma->vm_flags))
			return NULL;
	}

	/*
	 * Add some anal sanity checks for now. Eventually,
	 * we should just do "return pfn_to_page(pfn)", but
	 * in the meantime we check that we get a valid pfn,
	 * and that the resulting page looks ok.
	 */
	if (unlikely(!pfn_valid(pfn))) {
		print_bad_pte(vma, pte, addr);
		return NULL;
	}

	/*
	 * NOTE! We still have PageReserved() pages in the page 
	 * tables. 
	 *
	 * The PAGE_ZERO() pages and various VDSO mappings can
	 * cause them to exist.
	 */
	return pfn_to_page(pfn);
}

/*
 * copy one vm_area from one task to the other. Assumes the page tables
 * already present in the new task to be cleared in the whole range
 * covered by this vma.
 */

static inline void
copy_one_pte(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pte_t *dst_pte, pte_t *src_pte, struct vm_area_struct *vma,
		unsigned long addr, int *rss)
{
	unsigned long vm_flags = vma->vm_flags;
	pte_t pte = *src_pte;
	struct page *page;

	/* pte contains position in swap or file, so copy. */
	if (unlikely(!pte_present(pte))) {
		if (!pte_file(pte)) {
			swp_entry_t entry = pte_to_swp_entry(pte);

			swap_duplicate(entry);
			/* make sure dst_mm is on swapoff's mmlist. */
			if (unlikely(list_empty(&dst_mm->mmlist))) {
				spin_lock(&mmlist_lock);
				if (list_empty(&dst_mm->mmlist))
					list_add(&dst_mm->mmlist,
						 &src_mm->mmlist);
				spin_unlock(&mmlist_lock);
			}
			if (is_write_migration_entry(entry) &&
					is_cow_mapping(vm_flags)) {
				/*
				 * COW mappings require pages in both parent
				 * and child to be set to read.
				 */
				make_migration_entry_read(&entry);
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
		page_dup_rmap(page);
		rss[!!PageAnon(page)]++;
	}

out_set_pte:
	set_pte_at(dst_mm, addr, dst_pte, pte);
}

static int copy_pte_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pmd_t *dst_pmd, pmd_t *src_pmd, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pte_t *src_pte, *dst_pte;
	spinlock_t *src_ptl, *dst_ptl;
	int progress = 0;
	int rss[2];

again:
	rss[1] = rss[0] = 0;
	dst_pte = pte_alloc_map_lock(dst_mm, dst_pmd, addr, &dst_ptl);
	if (!dst_pte)
		return -ENOMEM;
	src_pte = pte_offset_map_nested(src_pmd, addr);
	src_ptl = pte_lockptr(src_mm, src_pmd);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
	arch_enter_lazy_mmu_mode();

	do {
		/*
		 * We are holding two locks at this point - either of them
		 * could generate latencies in another task on another CPU.
		 */
		if (progress >= 32) {
			progress = 0;
			if (need_resched() ||
			    need_lockbreak(src_ptl) ||
			    need_lockbreak(dst_ptl))
				break;
		}
		if (pte_none(*src_pte)) {
			progress++;
			continue;
		}
		copy_one_pte(dst_mm, src_mm, dst_pte, src_pte, vma, addr, rss);
		progress += 8;
	} while (dst_pte++, src_pte++, addr += PAGE_SIZE, addr != end);

	arch_leave_lazy_mmu_mode();
	spin_unlock(src_ptl);
	pte_unmap_nested(src_pte - 1);
	add_mm_rss(dst_mm, rss[0], rss[1]);
	pte_unmap_unlock(dst_pte - 1, dst_ptl);
	cond_resched();
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
		if (pmd_none_or_clear_bad(src_pmd))
			continue;
		if (copy_pte_range(dst_mm, src_mm, dst_pmd, src_pmd,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pmd++, src_pmd++, addr = next, addr != end);
	return 0;
}

static inline int copy_pud_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pgd_t *dst_pgd, pgd_t *src_pgd, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pud_t *src_pud, *dst_pud;
	unsigned long next;

	dst_pud = pud_alloc(dst_mm, dst_pgd, addr);
	if (!dst_pud)
		return -ENOMEM;
	src_pud = pud_offset(src_pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(src_pud))
			continue;
		if (copy_pmd_range(dst_mm, src_mm, dst_pud, src_pud,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pud++, src_pud++, addr = next, addr != end);
	return 0;
}

int copy_page_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		struct vm_area_struct *vma)
{
	pgd_t *src_pgd, *dst_pgd;
	unsigned long next;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;

	/*
	 * Don't copy ptes where a page fault will fill them correctly.
	 * Fork becomes much lighter when there are big shared or private
	 * readonly mappings. The tradeoff is that copy_page_range is more
	 * efficient than faulting.
	 */
	if (!(vma->vm_flags & (VM_HUGETLB|VM_NONLINEAR|VM_PFNMAP|VM_INSERTPAGE))) {
		if (!vma->anon_vma)
			return 0;
	}

	if (is_vm_hugetlb_page(vma))
		return copy_hugetlb_page_range(dst_mm, src_mm, vma);

	dst_pgd = pgd_offset(dst_mm, addr);
	src_pgd = pgd_offset(src_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(src_pgd))
			continue;
		if (copy_pud_range(dst_mm, src_mm, dst_pgd, src_pgd,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pgd++, src_pgd++, addr = next, addr != end);
	return 0;
}

static unsigned long zap_pte_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end,
				long *zap_work, struct zap_details *details)
{
	struct mm_struct *mm = tlb->mm;
	pte_t *pte;
	spinlock_t *ptl;
	int file_rss = 0;
	int anon_rss = 0;

	pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	arch_enter_lazy_mmu_mode();
	do {
		pte_t ptent = *pte;
		if (pte_none(ptent)) {
			(*zap_work)--;
			continue;
		}

		(*zap_work) -= PAGE_SIZE;

		if (pte_present(ptent)) {
			struct page *page;

			page = vm_normal_page(vma, addr, ptent);
			if (unlikely(details) && page) {
				/*
				 * unmap_shared_mapping_pages() wants to
				 * invalidate cache without truncating:
				 * unmap shared but keep private pages.
				 */
				if (details->check_mapping &&
				    details->check_mapping != page->mapping)
					continue;
				/*
				 * Each page->index must be checked when
				 * invalidating or truncating nonlinear.
				 */
				if (details->nonlinear_vma &&
				    (page->index < details->first_index ||
				     page->index > details->last_index))
					continue;
			}
			ptent = ptep_get_and_clear_full(mm, addr, pte,
							tlb->fullmm);
			tlb_remove_tlb_entry(tlb, pte, addr);
			if (unlikely(!page))
				continue;
			if (unlikely(details) && details->nonlinear_vma
			    && linear_page_index(details->nonlinear_vma,
						addr) != page->index)
				set_pte_at(mm, addr, pte,
					   pgoff_to_pte(page->index));
			if (PageAnon(page))
				anon_rss--;
			else {
				if (pte_dirty(ptent))
					set_page_dirty(page);
				if (pte_young(ptent))
					mark_page_accessed(page);
				file_rss--;
			}
			page_remove_rmap(page);
			tlb_remove_page(tlb, page);
			continue;
		}
		/*
		 * If details->check_mapping, we leave swap entries;
		 * if details->nonlinear_vma, we leave file entries.
		 */
		if (unlikely(details))
			continue;
		if (!pte_file(ptent))
			free_swap_and_cache(pte_to_swp_entry(ptent));
		pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
	} while (pte++, addr += PAGE_SIZE, (addr != end && *zap_work > 0));

	add_mm_rss(mm, file_rss, anon_rss);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);

	return addr;
}

static inline unsigned long zap_pmd_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pud_t *pud,
				unsigned long addr, unsigned long end,
				long *zap_work, struct zap_details *details)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd)) {
			(*zap_work)--;
			continue;
		}
		next = zap_pte_range(tlb, vma, pmd, addr, next,
						zap_work, details);
	} while (pmd++, addr = next, (addr != end && *zap_work > 0));

	return addr;
}

static inline unsigned long zap_pud_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				long *zap_work, struct zap_details *details)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud)) {
			(*zap_work)--;
			continue;
		}
		next = zap_pmd_range(tlb, vma, pud, addr, next,
						zap_work, details);
	} while (pud++, addr = next, (addr != end && *zap_work > 0));

	return addr;
}

static unsigned long unmap_page_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma,
				unsigned long addr, unsigned long end,
				long *zap_work, struct zap_details *details)
{
	pgd_t *pgd;
	unsigned long next;

	if (details && !details->check_mapping && !details->nonlinear_vma)
		details = NULL;

	BUG_ON(addr >= end);
	tlb_start_vma(tlb, vma);
	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd)) {
			(*zap_work)--;
			continue;
		}
		next = zap_pud_range(tlb, vma, pgd, addr, next,
						zap_work, details);
	} while (pgd++, addr = next, (addr != end && *zap_work > 0));
	tlb_end_vma(tlb, vma);

	return addr;
}

#ifdef CONFIG_PREEMPT
# define ZAP_BLOCK_SIZE	(8 * PAGE_SIZE)
#else
/* No preempt: go for improved straight-line efficiency */
# define ZAP_BLOCK_SIZE	(1024 * PAGE_SIZE)
#endif

/**
 * unmap_vmas - unmap a range of memory covered by a list of vma's
 * @tlbp: address of the caller's struct mmu_gather
 * @vma: the starting vma
 * @start_addr: virtual address at which to start unmapping
 * @end_addr: virtual address at which to end unmapping
 * @nr_accounted: Place number of unmapped pages in vm-accountable vma's here
 * @details: details of nonlinear truncation or shared cache invalidation
 *
 * Returns the end address of the unmapping (restart addr if interrupted).
 *
 * Unmap all pages in the vma list.
 *
 * We aim to not hold locks for too long (for scheduling latency reasons).
 * So zap pages in ZAP_BLOCK_SIZE bytecounts.  This means we need to
 * return the ending mmu_gather to the caller.
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
unsigned long unmap_vmas(struct mmu_gather **tlbp,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr, unsigned long *nr_accounted,
		struct zap_details *details)
{
	long zap_work = ZAP_BLOCK_SIZE;
	unsigned long tlb_start = 0;	/* For tlb_finish_mmu */
	int tlb_start_valid = 0;
	unsigned long start = start_addr;
	spinlock_t *i_mmap_lock = details? details->i_mmap_lock: NULL;
	int fullmm = (*tlbp)->fullmm;

	for ( ; vma && vma->vm_start < end_addr; vma = vma->vm_next) {
		unsigned long end;

		start = max(vma->vm_start, start_addr);
		if (start >= vma->vm_end)
			continue;
		end = min(vma->vm_end, end_addr);
		if (end <= vma->vm_start)
			continue;

		if (vma->vm_flags & VM_ACCOUNT)
			*nr_accounted += (end - start) >> PAGE_SHIFT;

		while (start != end) {
			if (!tlb_start_valid) {
				tlb_start = start;
				tlb_start_valid = 1;
			}

			if (unlikely(is_vm_hugetlb_page(vma))) {
				unmap_hugepage_range(vma, start, end);
				zap_work -= (end - start) /
						(HPAGE_SIZE / PAGE_SIZE);
				start = end;
			} else
				start = unmap_page_range(*tlbp, vma,
						start, end, &zap_work, details);

			if (zap_work > 0) {
				BUG_ON(start != end);
				break;
			}

			tlb_finish_mmu(*tlbp, tlb_start, start);

			if (need_resched() ||
				(i_mmap_lock && need_lockbreak(i_mmap_lock))) {
				if (i_mmap_lock) {
					*tlbp = NULL;
					goto out;
				}
				cond_resched();
			}

			*tlbp = tlb_gather_mmu(vma->vm_mm, fullmm);
			tlb_start_valid = 0;
			zap_work = ZAP_BLOCK_SIZE;
		}
	}
out:
	return start;	/* which is now the end (or restart) address */
}

/**
 * zap_page_range - remove user pages in a given range
 * @vma: vm_area_struct holding the applicable pages
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 * @details: details of nonlinear truncation or shared cache invalidation
 */
unsigned long zap_page_range(struct vm_area_struct *vma, unsigned long address,
		unsigned long size, struct zap_details *details)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather *tlb;
	unsigned long end = address + size;
	unsigned long nr_accounted = 0;

	lru_add_drain();
	tlb = tlb_gather_mmu(mm, 0);
	update_hiwater_rss(mm);
	end = unmap_vmas(&tlb, vma, address, end, &nr_accounted, details);
	if (tlb)
		tlb_finish_mmu(tlb, address, end);
	return end;
}

/*
 * Do a quick page-table lookup for a single page.
 */
struct page *follow_page(struct vm_area_struct *vma, unsigned long address,
			unsigned int flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;
	spinlock_t *ptl;
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	page = follow_huge_addr(mm, address, flags & FOLL_WRITE);
	if (!IS_ERR(page)) {
		BUG_ON(flags & FOLL_GET);
		goto out;
	}

	page = NULL;
	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		goto no_page_table;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		goto no_page_table;
	
	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		goto no_page_table;

	if (pmd_huge(*pmd)) {
		BUG_ON(flags & FOLL_GET);
		page = follow_huge_pmd(mm, address, pmd, flags & FOLL_WRITE);
		goto out;
	}

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (!ptep)
		goto out;

	pte = *ptep;
	if (!pte_present(pte))
		goto unlock;
	if ((flags & FOLL_WRITE) && !pte_write(pte))
		goto unlock;
	page = vm_normal_page(vma, address, pte);
	if (unlikely(!page))
		goto unlock;

	if (flags & FOLL_GET)
		get_page(page);
	if (flags & FOLL_TOUCH) {
		if ((flags & FOLL_WRITE) &&
		    !pte_dirty(pte) && !PageDirty(page))
			set_page_dirty(page);
		mark_page_accessed(page);
	}
unlock:
	pte_unmap_unlock(ptep, ptl);
out:
	return page;

no_page_table:
	/*
	 * When core dumping an enormous anonymous area that nobody
	 * has touched so far, we don't want to allocate page tables.
	 */
	if (flags & FOLL_ANON) {
		page = ZERO_PAGE(address);
		if (flags & FOLL_GET)
			get_page(page);
		BUG_ON(flags & FOLL_WRITE);
	}
	return page;
}

int get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long start, int len, int write, int force,
		struct page **pages, struct vm_area_struct **vmas)
{
	int i;
	unsigned int vm_flags;

	/* 
	 * Require read or write permissions.
	 * If 'force' is set, we only require the "MAY" flags.
	 */
	vm_flags  = write ? (VM_WRITE | VM_MAYWRITE) : (VM_READ | VM_MAYREAD);
	vm_flags &= force ? (VM_MAYREAD | VM_MAYWRITE) : (VM_READ | VM_WRITE);
	i = 0;

	do {
		struct vm_area_struct *vma;
		unsigned int foll_flags;

		vma = find_extend_vma(mm, start);
		if (!vma && in_gate_area(tsk, start)) {
			unsigned long pg = start & PAGE_MASK;
			struct vm_area_struct *gate_vma = get_gate_vma(tsk);
			pgd_t *pgd;
			pud_t *pud;
			pmd_t *pmd;
			pte_t *pte;
			if (write) /* user gate pages are read-only */
				return i ? : -EFAULT;
			if (pg > TASK_SIZE)
				pgd = pgd_offset_k(pg);
			else
				pgd = pgd_offset_gate(mm, pg);
			BUG_ON(pgd_none(*pgd));
			pud = pud_offset(pgd, pg);
			BUG_ON(pud_none(*pud));
			pmd = pmd_offset(pud, pg);
			if (pmd_none(*pmd))
				return i ? : -EFAULT;
			pte = pte_offset_map(pmd, pg);
			if (pte_none(*pte)) {
				pte_unmap(pte);
				return i ? : -EFAULT;
			}
			if (pages) {
				struct page *page = vm_normal_page(gate_vma, start, *pte);
				pages[i] = page;
				if (page)
					get_page(page);
			}
			pte_unmap(pte);
			if (vmas)
				vmas[i] = gate_vma;
			i++;
			start += PAGE_SIZE;
			len--;
			continue;
		}

		if (!vma || (vma->vm_flags & (VM_IO | VM_PFNMAP))
				|| !(vm_flags & vma->vm_flags))
			return i ? : -EFAULT;

		if (is_vm_hugetlb_page(vma)) {
			i = follow_hugetlb_page(mm, vma, pages, vmas,
						&start, &len, i);
			continue;
		}

		foll_flags = FOLL_TOUCH;
		if (pages)
			foll_flags |= FOLL_GET;
		if (!write && !(vma->vm_flags & VM_LOCKED) &&
		    (!vma->vm_ops || !vma->vm_ops->nopage))
			foll_flags |= FOLL_ANON;

		do {
			struct page *page;

			if (write)
				foll_flags |= FOLL_WRITE;

			cond_resched();
			while (!(page = follow_page(vma, start, foll_flags))) {
				int ret;
				ret = __handle_mm_fault(mm, vma, start,
						foll_flags & FOLL_WRITE);
				/*
				 * The VM_FAULT_WRITE bit tells us that do_wp_page has
				 * broken COW when necessary, even if maybe_mkwrite
				 * decided not to set pte_write. We can thus safely do
				 * subsequent page lookups as if they were reads.
				 */
				if (ret & VM_FAULT_WRITE)
					foll_flags &= ~FOLL_WRITE;
				
				switch (ret & ~VM_FAULT_WRITE) {
				case VM_FAULT_MINOR:
					tsk->min_flt++;
					break;
				case VM_FAULT_MAJOR:
					tsk->maj_flt++;
					break;
				case VM_FAULT_SIGBUS:
					return i ? i : -EFAULT;
				case VM_FAULT_OOM:
					return i ? i : -ENOMEM;
				default:
					BUG();
				}
				cond_resched();
			}
			if (pages) {
				pages[i] = page;

				flush_anon_page(page, start);
				flush_dcache_page(page);
			}
			if (vmas)
				vmas[i] = vma;
			i++;
			start += PAGE_SIZE;
			len--;
		} while (len && start < vma->vm_end);
	} while (len);
	return i;
}
EXPORT_SYMBOL(get_user_pages);

static int zeromap_pte_range(struct mm_struct *mm, pmd_t *pmd,
			unsigned long addr, unsigned long end, pgprot_t prot)
{
	pte_t *pte;
	spinlock_t *ptl;

	pte = pte_alloc_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return -ENOMEM;
	arch_enter_lazy_mmu_mode();
	do {
		struct page *page = ZERO_PAGE(addr);
		pte_t zero_pte = pte_wrprotect(mk_pte(page, prot));
		page_cache_get(page);
		page_add_file_rmap(page);
		inc_mm_counter(mm, file_rss);
		BUG_ON(!pte_none(*pte));
		set_pte_at(mm, addr, pte, zero_pte);
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);
	return 0;
}

static inline int zeromap_pmd_range(struct mm_struct *mm, pud_t *pud,
			unsigned long addr, unsigned long end, pgprot_t prot)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);
		if (zeromap_pte_range(mm, pmd, addr, next, prot))
			return -ENOMEM;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int zeromap_pud_range(struct mm_struct *mm, pgd_t *pgd,
			unsigned long addr, unsigned long end, pgprot_t prot)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_alloc(mm, pgd, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		if (zeromap_pmd_range(mm, pud, addr, next, prot))
			return -ENOMEM;
	} while (pud++, addr = next, addr != end);
	return 0;
}

int zeromap_page_range(struct vm_area_struct *vma,
			unsigned long addr, unsigned long size, pgprot_t prot)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long end = addr + size;
	struct mm_struct *mm = vma->vm_mm;
	int err;

	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		err = zeromap_pud_range(mm, pgd, addr, next, prot);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);
	return err;
}

pte_t * fastcall get_locked_pte(struct mm_struct *mm, unsigned long addr, spinlock_t **ptl)
{
	pgd_t * pgd = pgd_offset(mm, addr);
	pud_t * pud = pud_alloc(mm, pgd, addr);
	if (pud) {
		pmd_t * pmd = pmd_alloc(mm, pud, addr);
		if (pmd)
			return pte_alloc_map_lock(mm, pmd, addr, ptl);
	}
	return NULL;
}

/*
 * This is the old fallback for page remapping.
 *
 * For historical reasons, it only allows reserved pages. Only
 * old drivers should use this, and they needed to mark their
 * pages reserved for the old functions anyway.
 */
static int insert_page(struct mm_struct *mm, unsigned long addr, struct page *page, pgprot_t prot)
{
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
	inc_mm_counter(mm, file_rss);
	page_add_file_rmap(page);
	set_pte_at(mm, addr, pte, mk_pte(page, prot));

	retval = 0;
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
 */
int vm_insert_page(struct vm_area_struct *vma, unsigned long addr, struct page *page)
{
	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;
	if (!page_count(page))
		return -EINVAL;
	vma->vm_flags |= VM_INSERTPAGE;
	return insert_page(vma->vm_mm, addr, page, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_insert_page);

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
		set_pte_at(mm, addr, pte, pfn_pte(pfn, prot));
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
	do {
		next = pmd_addr_end(addr, end);
		if (remap_pte_range(mm, pmd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot))
			return -ENOMEM;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int remap_pud_range(struct mm_struct *mm, pgd_t *pgd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pud_t *pud;
	unsigned long next;

	pfn -= addr >> PAGE_SHIFT;
	pud = pud_alloc(mm, pgd, addr);
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
	int err;

	/*
	 * Physically remapped pages are special. Tell the
	 * rest of the world about it:
	 *   VM_IO tells people not to look at these pages
	 *	(accesses can have side effects).
	 *   VM_RESERVED is specified all over the place, because
	 *	in 2.4 it kept swapout's vma scan off this vma; but
	 *	in 2.6 the LRU scan won't even find its pages, so this
	 *	flag means no more than count its pages in reserved_vm,
	 * 	and omit it from core dump, even when VM_IO turned off.
	 *   VM_PFNMAP tells the core MM that the base pages are just
	 *	raw PFN mappings, and do not have a "struct page" associated
	 *	with them.
	 *
	 * There's a horrible special case to handle copy-on-write
	 * behaviour that some programs depend on. We mark the "original"
	 * un-COW'ed pages by matching them up with "vma->vm_pgoff".
	 */
	if (is_cow_mapping(vma->vm_flags)) {
		if (addr != vma->vm_start || end != vma->vm_end)
			return -EINVAL;
		vma->vm_pgoff = pfn;
	}

	vma->vm_flags |= VM_IO | VM_RESERVED | VM_PFNMAP;

	BUG_ON(addr >= end);
	pfn -= addr >> PAGE_SHIFT;
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		err = remap_pud_range(mm, pgd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);
	return err;
}
EXPORT_SYMBOL(remap_pfn_range);

/*
 * handle_pte_fault chooses page fault handler according to an entry
 * which was read non-atomically.  Before making any commitment, on
 * those architectures or configurations (e.g. i386 with PAE) which
 * might give a mix of unmatched parts, do_swap_page and do_file_page
 * must check under lock before unmapping the pte and proceeding
 * (but do_wp_page is only called after already making such a check;
 * and do_anonymous_page and do_no_page can safely check later on).
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

/*
 * Do pte_mkwrite, but only if the vma says VM_WRITE.  We do this when
 * servicing faults for write access.  In the normal case, do always want
 * pte_mkwrite.  But get_user_pages can cause write faults for mappings
 * that do not have writing enabled, when used by access_process_vm.
 */
static inline pte_t maybe_mkwrite(pte_t pte, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pte = pte_mkwrite(pte);
	return pte;
}

static inline void cow_user_page(struct page *dst, struct page *src, unsigned long va)
{
	/*
	 * If the source page was a PFN mapping, we don't have
	 * a "struct page" for it. We do a best-effort copy by
	 * just copying from the original user address. If that
	 * fails, we just zero-fill it. Live with it.
	 */
	if (unlikely(!src)) {
		void *kaddr = kmap_atomic(dst, KM_USER0);
		void __user *uaddr = (void __user *)(va & PAGE_MASK);

		/*
		 * This really shouldn't fail, because the page is there
		 * in the page tables. But it might just be unreadable,
		 * in which case we just give up and fill the result with
		 * zeroes.
		 */
		if (__copy_from_user_inatomic(kaddr, uaddr, PAGE_SIZE))
			memset(kaddr, 0, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		flush_dcache_page(dst);
		return;
		
	}
	copy_user_highpage(dst, src, va);
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
static int do_wp_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		spinlock_t *ptl, pte_t orig_pte)
{
	struct page *old_page, *new_page;
	pte_t entry;
	int reuse = 0, ret = VM_FAULT_MINOR;
	struct page *dirty_page = NULL;

	old_page = vm_normal_page(vma, address, orig_pte);
	if (!old_page)
		goto gotten;

	/*
	 * Take out anonymous pages first, anonymous shared vmas are
	 * not dirty accountable.
	 */
	if (PageAnon(old_page)) {
		if (!TestSetPageLocked(old_page)) {
			reuse = can_share_swap_page(old_page);
			unlock_page(old_page);
		}
	} else if (unlikely((vma->vm_flags & (VM_WRITE|VM_SHARED)) ==
					(VM_WRITE|VM_SHARED))) {
		/*
		 * Only catch write-faults on shared writable pages,
		 * read-only shared pages can get COWed by
		 * get_user_pages(.write=1, .force=1).
		 */
		if (vma->vm_ops && vma->vm_ops->page_mkwrite) {
			/*
			 * Notify the address space that the page is about to
			 * become writable so that it can prohibit this or wait
			 * for the page to get into an appropriate state.
			 *
			 * We do this without the lock held, so that it can
			 * sleep if it needs to.
			 */
			page_cache_get(old_page);
			pte_unmap_unlock(page_table, ptl);

			if (vma->vm_ops->page_mkwrite(vma, old_page) < 0)
				goto unwritable_page;

			page_cache_release(old_page);

			/*
			 * Since we dropped the lock we need to revalidate
			 * the PTE as someone else may have changed it.  If
			 * they did, we just return, as we can count on the
			 * MMU to tell us if they didn't also make it writable.
			 */
			page_table = pte_offset_map_lock(mm, pmd, address,
							 &ptl);
			if (!pte_same(*page_table, orig_pte))
				goto unlock;
		}
		dirty_page = old_page;
		get_page(dirty_page);
		reuse = 1;
	}

	if (reuse) {
		flush_cache_page(vma, address, pte_pfn(orig_pte));
		entry = pte_mkyoung(orig_pte);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		ptep_set_access_flags(vma, address, page_table, entry, 1);
		update_mmu_cache(vma, address, entry);
		lazy_mmu_prot_update(entry);
		ret |= VM_FAULT_WRITE;
		goto unlock;
	}

	/*
	 * Ok, we need to copy. Oh, well..
	 */
	page_cache_get(old_page);
gotten:
	pte_unmap_unlock(page_table, ptl);

	if (unlikely(anon_vma_prepare(vma)))
		goto oom;
	if (old_page == ZERO_PAGE(address)) {
		new_page = alloc_zeroed_user_highpage(vma, address);
		if (!new_page)
			goto oom;
	} else {
		new_page = alloc_page_vma(GFP_HIGHUSER, vma, address);
		if (!new_page)
			goto oom;
		cow_user_page(new_page, old_page, address);
	}

	/*
	 * Re-check the pte - we dropped the lock
	 */
	page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (likely(pte_same(*page_table, orig_pte))) {
		if (old_page) {
			page_remove_rmap(old_page);
			if (!PageAnon(old_page)) {
				dec_mm_counter(mm, file_rss);
				inc_mm_counter(mm, anon_rss);
			}
		} else
			inc_mm_counter(mm, anon_rss);
		flush_cache_page(vma, address, pte_pfn(orig_pte));
		entry = mk_pte(new_page, vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		lazy_mmu_prot_update(entry);
		/*
		 * Clear the pte entry and flush it first, before updating the
		 * pte with the new entry. This will avoid a race condition
		 * seen in the presence of one thread doing SMC and another
		 * thread doing COW.
		 */
		ptep_clear_flush(vma, address, page_table);
		set_pte_at(mm, address, page_table, entry);
		update_mmu_cache(vma, address, entry);
		lru_cache_add_active(new_page);
		page_add_new_anon_rmap(new_page, vma, address);

		/* Free the old page.. */
		new_page = old_page;
		ret |= VM_FAULT_WRITE;
	}
	if (new_page)
		page_cache_release(new_page);
	if (old_page)
		page_cache_release(old_page);
unlock:
	pte_unmap_unlock(page_table, ptl);
	if (dirty_page) {
		set_page_dirty_balance(dirty_page);
		put_page(dirty_page);
	}
	return ret;
oom:
	if (old_page)
		page_cache_release(old_page);
	return VM_FAULT_OOM;

unwritable_page:
	page_cache_release(old_page);
	return VM_FAULT_SIGBUS;
}

/*
 * Helper functions for unmap_mapping_range().
 *
 * __ Notes on dropping i_mmap_lock to reduce latency while unmapping __
 *
 * We have to restart searching the prio_tree whenever we drop the lock,
 * since the iterator is only valid while the lock is held, and anyway
 * a later vma might be split and reinserted earlier while lock dropped.
 *
 * The list of nonlinear vmas could be handled more efficiently, using
 * a placeholder, but handle it in the same way until a need is shown.
 * It is important to search the prio_tree before nonlinear list: a vma
 * may become nonlinear and be shifted from prio_tree to nonlinear list
 * while the lock is dropped; but never shifted from list to prio_tree.
 *
 * In order to make forward progress despite restarting the search,
 * vm_truncate_count is used to mark a vma as now dealt with, so we can
 * quickly skip it next time around.  Since the prio_tree search only
 * shows us those vmas affected by unmapping the range in question, we
 * can't efficiently keep all vmas in step with mapping->truncate_count:
 * so instead reset them all whenever it wraps back to 0 (then go to 1).
 * mapping->truncate_count and vma->vm_truncate_count are protected by
 * i_mmap_lock.
 *
 * In order to make forward progress despite repeatedly restarting some
 * large vma, note the restart_addr from unmap_vmas when it breaks out:
 * and restart from that address when we reach that vma again.  It might
 * have been split or merged, shrunk or extended, but never shifted: so
 * restart_addr remains valid so long as it remains in the vma's range.
 * unmap_mapping_range forces truncate_count to leap over page-aligned
 * values so we can save vma's restart_addr in its truncate_count field.
 */
#define is_restart_addr(truncate_count) (!((truncate_count) & ~PAGE_MASK))

static void reset_vma_truncate_counts(struct address_space *mapping)
{
	struct vm_area_struct *vma;
	struct prio_tree_iter iter;

	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, 0, ULONG_MAX)
		vma->vm_truncate_count = 0;
	list_for_each_entry(vma, &mapping->i_mmap_nonlinear, shared.vm_set.list)
		vma->vm_truncate_count = 0;
}

static int unmap_mapping_range_vma(struct vm_area_struct *vma,
		unsigned long start_addr, unsigned long end_addr,
		struct zap_details *details)
{
	unsigned long restart_addr;
	int need_break;

again:
	restart_addr = vma->vm_truncate_count;
	if (is_restart_addr(restart_addr) && start_addr < restart_addr) {
		start_addr = restart_addr;
		if (start_addr >= end_addr) {
			/* Top of vma has been split off since last time */
			vma->vm_truncate_count = details->truncate_count;
			return 0;
		}
	}

	restart_addr = zap_page_range(vma, start_addr,
					end_addr - start_addr, details);
	need_break = need_resched() ||
			need_lockbreak(details->i_mmap_lock);

	if (restart_addr >= end_addr) {
		/* We have now completed this vma: mark it so */
		vma->vm_truncate_count = details->truncate_count;
		if (!need_break)
			return 0;
	} else {
		/* Note restart_addr in vma's truncate_count field */
		vma->vm_truncate_count = restart_addr;
		if (!need_break)
			goto again;
	}

	spin_unlock(details->i_mmap_lock);
	cond_resched();
	spin_lock(details->i_mmap_lock);
	return -EINTR;
}

static inline void unmap_mapping_range_tree(struct prio_tree_root *root,
					    struct zap_details *details)
{
	struct vm_area_struct *vma;
	struct prio_tree_iter iter;
	pgoff_t vba, vea, zba, zea;

restart:
	vma_prio_tree_foreach(vma, &iter, root,
			details->first_index, details->last_index) {
		/* Skip quickly over those we have already dealt with */
		if (vma->vm_truncate_count == details->truncate_count)
			continue;

		vba = vma->vm_pgoff;
		vea = vba + ((vma->vm_end - vma->vm_start) >> PAGE_SHIFT) - 1;
		/* Assume for now that PAGE_CACHE_SHIFT == PAGE_SHIFT */
		zba = details->first_index;
		if (zba < vba)
			zba = vba;
		zea = details->last_index;
		if (zea > vea)
			zea = vea;

		if (unmap_mapping_range_vma(vma,
			((zba - vba) << PAGE_SHIFT) + vma->vm_start,
			((zea - vba + 1) << PAGE_SHIFT) + vma->vm_start,
				details) < 0)
			goto restart;
	}
}

static inline void unmap_mapping_range_list(struct list_head *head,
					    struct zap_details *details)
{
	struct vm_area_struct *vma;

	/*
	 * In nonlinear VMAs there is no correspondence between virtual address
	 * offset and file offset.  So we must perform an exhaustive search
	 * across *all* the pages in each nonlinear VMA, not just the pages
	 * whose virtual address lies outside the file truncation point.
	 */
restart:
	list_for_each_entry(vma, head, shared.vm_set.list) {
		/* Skip quickly over those we have already dealt with */
		if (vma->vm_truncate_count == details->truncate_count)
			continue;
		details->nonlinear_vma = vma;
		if (unmap_mapping_range_vma(vma, vma->vm_start,
					vma->vm_end, details) < 0)
			goto restart;
	}
}

/**
 * unmap_mapping_range - unmap the portion of all mmaps
 * in the specified address_space corresponding to the specified
 * page range in the underlying file.
 * @mapping: the address space containing mmaps to be unmapped.
 * @holebegin: byte in first page to unmap, relative to the start of
 * the underlying file.  This will be rounded down to a PAGE_SIZE
 * boundary.  Note that this is different from vmtruncate(), which
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
	struct zap_details details;
	pgoff_t hba = holebegin >> PAGE_SHIFT;
	pgoff_t hlen = (holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/* Check for overflow. */
	if (sizeof(holelen) > sizeof(hlen)) {
		long long holeend =
			(holebegin + holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;
		if (holeend & ~(long long)ULONG_MAX)
			hlen = ULONG_MAX - hba + 1;
	}

	details.check_mapping = even_cows? NULL: mapping;
	details.nonlinear_vma = NULL;
	details.first_index = hba;
	details.last_index = hba + hlen - 1;
	if (details.last_index < details.first_index)
		details.last_index = ULONG_MAX;
	details.i_mmap_lock = &mapping->i_mmap_lock;

	spin_lock(&mapping->i_mmap_lock);

	/* serialize i_size write against truncate_count write */
	smp_wmb();
	/* Protect against page faults, and endless unmapping loops */
	mapping->truncate_count++;
	/*
	 * For archs where spin_lock has inclusive semantics like ia64
	 * this smp_mb() will prevent to read pagetable contents
	 * before the truncate_count increment is visible to
	 * other cpus.
	 */
	smp_mb();
	if (unlikely(is_restart_addr(mapping->truncate_count))) {
		if (mapping->truncate_count == 0)
			reset_vma_truncate_counts(mapping);
		mapping->truncate_count++;
	}
	details.truncate_count = mapping->truncate_count;

	if (unlikely(!prio_tree_empty(&mapping->i_mmap)))
		unmap_mapping_range_tree(&mapping->i_mmap, &details);
	if (unlikely(!list_empty(&mapping->i_mmap_nonlinear)))
		unmap_mapping_range_list(&mapping->i_mmap_nonlinear, &details);
	spin_unlock(&mapping->i_mmap_lock);
}
EXPORT_SYMBOL(unmap_mapping_range);

/**
 * vmtruncate - unmap mappings "freed" by truncate() syscall
 * @inode: inode of the file used
 * @offset: file offset to start truncating
 *
 * NOTE! We have to be ready to update the memory sharing
 * between the file and the memory map for a potential last
 * incomplete page.  Ugly, but necessary.
 */
int vmtruncate(struct inode * inode, loff_t offset)
{
	struct address_space *mapping = inode->i_mapping;
	unsigned long limit;

	if (inode->i_size < offset)
		goto do_expand;
	/*
	 * truncation of in-use swapfiles is disallowed - it would cause
	 * subsequent swapout to scribble on the now-freed blocks.
	 */
	if (IS_SWAPFILE(inode))
		goto out_busy;
	i_size_write(inode, offset);
	unmap_mapping_range(mapping, offset + PAGE_SIZE - 1, 0, 1);
	truncate_inode_pages(mapping, offset);
	goto out_truncate;

do_expand:
	limit = current->signal->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit != RLIM_INFINITY && offset > limit)
		goto out_sig;
	if (offset > inode->i_sb->s_maxbytes)
		goto out_big;
	i_size_write(inode, offset);

out_truncate:
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	return 0;
out_sig:
	send_sig(SIGXFSZ, current, 0);
out_big:
	return -EFBIG;
out_busy:
	return -ETXTBSY;
}
EXPORT_SYMBOL(vmtruncate);

int vmtruncate_range(struct inode *inode, loff_t offset, loff_t end)
{
	struct address_space *mapping = inode->i_mapping;

	/*
	 * If the underlying filesystem is not going to provide
	 * a way to truncate a range of blocks (punch a hole) -
	 * we should return failure right now.
	 */
	if (!inode->i_op || !inode->i_op->truncate_range)
		return -ENOSYS;

	mutex_lock(&inode->i_mutex);
	down_write(&inode->i_alloc_sem);
	unmap_mapping_range(mapping, offset, (end - offset), 1);
	truncate_inode_pages_range(mapping, offset, end);
	inode->i_op->truncate_range(inode, offset, end);
	up_write(&inode->i_alloc_sem);
	mutex_unlock(&inode->i_mutex);

	return 0;
}
EXPORT_UNUSED_SYMBOL(vmtruncate_range);  /*  June 2006  */

/**
 * swapin_readahead - swap in pages in hope we need them soon
 * @entry: swap entry of this memory
 * @addr: address to start
 * @vma: user vma this addresses belong to
 *
 * Primitive swap readahead code. We simply read an aligned block of
 * (1 << page_cluster) entries in the swap area. This method is chosen
 * because it doesn't cost us any seek time.  We also make sure to queue
 * the 'original' request together with the readahead ones...
 *
 * This has been extended to use the NUMA policies from the mm triggering
 * the readahead.
 *
 * Caller must hold down_read on the vma->vm_mm if vma is not NULL.
 */
void swapin_readahead(swp_entry_t entry, unsigned long addr,struct vm_area_struct *vma)
{
#ifdef CONFIG_NUMA
	struct vm_area_struct *next_vma = vma ? vma->vm_next : NULL;
#endif
	int i, num;
	struct page *new_page;
	unsigned long offset;

	/*
	 * Get the number of handles we should do readahead io to.
	 */
	num = valid_swaphandles(entry, &offset);
	for (i = 0; i < num; offset++, i++) {
		/* Ok, do the async read-ahead now */
		new_page = read_swap_cache_async(swp_entry(swp_type(entry),
							   offset), vma, addr);
		if (!new_page)
			break;
		page_cache_release(new_page);
#ifdef CONFIG_NUMA
		/*
		 * Find the next applicable VMA for the NUMA policy.
		 */
		addr += PAGE_SIZE;
		if (addr == 0)
			vma = NULL;
		if (vma) {
			if (addr >= vma->vm_end) {
				vma = next_vma;
				next_vma = vma ? vma->vm_next : NULL;
			}
			if (vma && addr < vma->vm_start)
				vma = NULL;
		} else {
			if (next_vma && addr >= next_vma->vm_start) {
				vma = next_vma;
				next_vma = vma->vm_next;
			}
		}
#endif
	}
	lru_add_drain();	/* Push any new pages onto the LRU now */
}

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_swap_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		int write_access, pte_t orig_pte)
{
	spinlock_t *ptl;
	struct page *page;
	swp_entry_t entry;
	pte_t pte;
	int ret = VM_FAULT_MINOR;

	if (!pte_unmap_same(mm, pmd, page_table, orig_pte))
		goto out;

	entry = pte_to_swp_entry(orig_pte);
	if (is_migration_entry(entry)) {
		migration_entry_wait(mm, pmd, address);
		goto out;
	}
	delayacct_set_flag(DELAYACCT_PF_SWAPIN);
	page = lookup_swap_cache(entry);
	if (!page) {
		grab_swap_token(); /* Contend for token _before_ read-in */
 		swapin_readahead(entry, address, vma);
 		page = read_swap_cache_async(entry, vma, address);
		if (!page) {
			/*
			 * Back out if somebody else faulted in this pte
			 * while we released the pte lock.
			 */
			page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
			if (likely(pte_same(*page_table, orig_pte)))
				ret = VM_FAULT_OOM;
			delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
			goto unlock;
		}

		/* Had to read the page from swap area: Major fault */
		ret = VM_FAULT_MAJOR;
		count_vm_event(PGMAJFAULT);
	}

	delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
	mark_page_accessed(page);
	lock_page(page);

	/*
	 * Back out if somebody else already faulted in this pte.
	 */
	page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (unlikely(!pte_same(*page_table, orig_pte)))
		goto out_nomap;

	if (unlikely(!PageUptodate(page))) {
		ret = VM_FAULT_SIGBUS;
		goto out_nomap;
	}

	/* The page isn't present yet, go ahead with the fault. */

	inc_mm_counter(mm, anon_rss);
	pte = mk_pte(page, vma->vm_page_prot);
	if (write_access && can_share_swap_page(page)) {
		pte = maybe_mkwrite(pte_mkdirty(pte), vma);
		write_access = 0;
	}

	flush_icache_page(vma, page);
	set_pte_at(mm, address, page_table, pte);
	page_add_anon_rmap(page, vma, address);

	swap_free(entry);
	if (vm_swap_full())
		remove_exclusive_swap_page(page);
	unlock_page(page);

	if (write_access) {
		if (do_wp_page(mm, vma, address,
				page_table, pmd, ptl, pte) == VM_FAULT_OOM)
			ret = VM_FAULT_OOM;
		goto out;
	}

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, address, pte);
	lazy_mmu_prot_update(pte);
unlock:
	pte_unmap_unlock(page_table, ptl);
out:
	return ret;
out_nomap:
	pte_unmap_unlock(page_table, ptl);
	unlock_page(page);
	page_cache_release(page);
	return ret;
}

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_anonymous_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		int write_access)
{
	struct page *page;
	spinlock_t *ptl;
	pte_t entry;

	if (write_access) {
		/* Allocate our own private page. */
		pte_unmap(page_table);

		if (unlikely(anon_vma_prepare(vma)))
			goto oom;
		page = alloc_zeroed_user_highpage(vma, address);
		if (!page)
			goto oom;

		entry = mk_pte(page, vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);

		page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
		if (!pte_none(*page_table))
			goto release;
		inc_mm_counter(mm, anon_rss);
		lru_cache_add_active(page);
		page_add_new_anon_rmap(page, vma, address);
	} else {
		/* Map the ZERO_PAGE - vm_page_prot is readonly */
		page = ZERO_PAGE(address);
		page_cache_get(page);
		entry = mk_pte(page, vma->vm_page_prot);

		ptl = pte_lockptr(mm, pmd);
		spin_lock(ptl);
		if (!pte_none(*page_table))
			goto release;
		inc_mm_counter(mm, file_rss);
		page_add_file_rmap(page);
	}

	set_pte_at(mm, address, page_table, entry);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, address, entry);
	lazy_mmu_prot_update(entry);
unlock:
	pte_unmap_unlock(page_table, ptl);
	return VM_FAULT_MINOR;
release:
	page_cache_release(page);
	goto unlock;
oom:
	return VM_FAULT_OOM;
}

/*
 * do_no_page() tries to create a new page mapping. It aggressively
 * tries to share with existing pages, but makes a separate copy if
 * the "write_access" parameter is true in order to avoid the next
 * page fault.
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_no_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		int write_access)
{
	spinlock_t *ptl;
	struct page *new_page;
	struct address_space *mapping = NULL;
	pte_t entry;
	unsigned int sequence = 0;
	int ret = VM_FAULT_MINOR;
	int anon = 0;
	struct page *dirty_page = NULL;

	pte_unmap(page_table);
	BUG_ON(vma->vm_flags & VM_PFNMAP);

	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;
		sequence = mapping->truncate_count;
		smp_rmb(); /* serializes i_size against truncate_count */
	}
retry:
	new_page = vma->vm_ops->nopage(vma, address & PAGE_MASK, &ret);
	/*
	 * No smp_rmb is needed here as long as there's a full
	 * spin_lock/unlock sequence inside the ->nopage callback
	 * (for the pagecache lookup) that acts as an implicit
	 * smp_mb() and prevents the i_size read to happen
	 * after the next truncate_count read.
	 */

	/* no page was available -- either SIGBUS, OOM or REFAULT */
	if (unlikely(new_page == NOPAGE_SIGBUS))
		return VM_FAULT_SIGBUS;
	else if (unlikely(new_page == NOPAGE_OOM))
		return VM_FAULT_OOM;
	else if (unlikely(new_page == NOPAGE_REFAULT))
		return VM_FAULT_MINOR;

	/*
	 * Should we do an early C-O-W break?
	 */
	if (write_access) {
		if (!(vma->vm_flags & VM_SHARED)) {
			struct page *page;

			if (unlikely(anon_vma_prepare(vma)))
				goto oom;
			page = alloc_page_vma(GFP_HIGHUSER, vma, address);
			if (!page)
				goto oom;
			copy_user_highpage(page, new_page, address);
			page_cache_release(new_page);
			new_page = page;
			anon = 1;

		} else {
			/* if the page will be shareable, see if the backing
			 * address space wants to know that the page is about
			 * to become writable */
			if (vma->vm_ops->page_mkwrite &&
			    vma->vm_ops->page_mkwrite(vma, new_page) < 0
			    ) {
				page_cache_release(new_page);
				return VM_FAULT_SIGBUS;
			}
		}
	}

	page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
	/*
	 * For a file-backed vma, someone could have truncated or otherwise
	 * invalidated this page.  If unmap_mapping_range got called,
	 * retry getting the page.
	 */
	if (mapping && unlikely(sequence != mapping->truncate_count)) {
		pte_unmap_unlock(page_table, ptl);
		page_cache_release(new_page);
		cond_resched();
		sequence = mapping->truncate_count;
		smp_rmb();
		goto retry;
	}

	/*
	 * This silly early PAGE_DIRTY setting removes a race
	 * due to the bad i386 page protection. But it's valid
	 * for other architectures too.
	 *
	 * Note that if write_access is true, we either now have
	 * an exclusive copy of the page, or this is a shared mapping,
	 * so we can make it writable and dirty to avoid having to
	 * handle that later.
	 */
	/* Only go through if we didn't race with anybody else... */
	if (pte_none(*page_table)) {
		flush_icache_page(vma, new_page);
		entry = mk_pte(new_page, vma->vm_page_prot);
		if (write_access)
			entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		set_pte_at(mm, address, page_table, entry);
		if (anon) {
			inc_mm_counter(mm, anon_rss);
			lru_cache_add_active(new_page);
			page_add_new_anon_rmap(new_page, vma, address);
		} else {
			inc_mm_counter(mm, file_rss);
			page_add_file_rmap(new_page);
			if (write_access) {
				dirty_page = new_page;
				get_page(dirty_page);
			}
		}
	} else {
		/* One of our sibling threads was faster, back out. */
		page_cache_release(new_page);
		goto unlock;
	}

	/* no need to invalidate: a not-present page shouldn't be cached */
	update_mmu_cache(vma, address, entry);
	lazy_mmu_prot_update(entry);
unlock:
	pte_unmap_unlock(page_table, ptl);
	if (dirty_page) {
		set_page_dirty_balance(dirty_page);
		put_page(dirty_page);
	}
	return ret;
oom:
	page_cache_release(new_page);
	return VM_FAULT_OOM;
}

/*
 * do_no_pfn() tries to create a new page mapping for a page without
 * a struct_page backing it
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 *
 * It is expected that the ->nopfn handler always returns the same pfn
 * for a given virtual mapping.
 *
 * Mark this `noinline' to prevent it from bloating the main pagefault code.
 */
static noinline int do_no_pfn(struct mm_struct *mm, struct vm_area_struct *vma,
		     unsigned long address, pte_t *page_table, pmd_t *pmd,
		     int write_access)
{
	spinlock_t *ptl;
	pte_t entry;
	unsigned long pfn;
	int ret = VM_FAULT_MINOR;

	pte_unmap(page_table);
	BUG_ON(!(vma->vm_flags & VM_PFNMAP));
	BUG_ON(is_cow_mapping(vma->vm_flags));

	pfn = vma->vm_ops->nopfn(vma, address & PAGE_MASK);
	if (pfn == NOPFN_OOM)
		return VM_FAULT_OOM;
	if (pfn == NOPFN_SIGBUS)
		return VM_FAULT_SIGBUS;

	page_table = pte_offset_map_lock(mm, pmd, address, &ptl);

	/* Only go through if we didn't race with anybody else... */
	if (pte_none(*page_table)) {
		entry = pfn_pte(pfn, vma->vm_page_prot);
		if (write_access)
			entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		set_pte_at(mm, address, page_table, entry);
	}
	pte_unmap_unlock(page_table, ptl);
	return ret;
}

/*
 * Fault of a previously existing named mapping. Repopulate the pte
 * from the encoded file_pte if possible. This enables swappable
 * nonlinear vmas.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_file_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		int write_access, pte_t orig_pte)
{
	pgoff_t pgoff;
	int err;

	if (!pte_unmap_same(mm, pmd, page_table, orig_pte))
		return VM_FAULT_MINOR;

	if (unlikely(!(vma->vm_flags & VM_NONLINEAR))) {
		/*
		 * Page table corrupted: show pte and kill process.
		 */
		print_bad_pte(vma, orig_pte, address);
		return VM_FAULT_OOM;
	}
	/* We can then assume vm->vm_ops && vma->vm_ops->populate */

	pgoff = pte_to_pgoff(orig_pte);
	err = vma->vm_ops->populate(vma, address & PAGE_MASK, PAGE_SIZE,
					vma->vm_page_prot, pgoff, 0);
	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err)
		return VM_FAULT_SIGBUS;
	return VM_FAULT_MAJOR;
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
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static inline int handle_pte_fault(struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long address,
		pte_t *pte, pmd_t *pmd, int write_access)
{
	pte_t entry;
	pte_t old_entry;
	spinlock_t *ptl;

	old_entry = entry = *pte;
	if (!pte_present(entry)) {
		if (pte_none(entry)) {
			if (vma->vm_ops) {
				if (vma->vm_ops->nopage)
					return do_no_page(mm, vma, address,
							  pte, pmd,
							  write_access);
				if (unlikely(vma->vm_ops->nopfn))
					return do_no_pfn(mm, vma, address, pte,
							 pmd, write_access);
			}
			return do_anonymous_page(mm, vma, address,
						 pte, pmd, write_access);
		}
		if (pte_file(entry))
			return do_file_page(mm, vma, address,
					pte, pmd, write_access, entry);
		return do_swap_page(mm, vma, address,
					pte, pmd, write_access, entry);
	}

	ptl = pte_lockptr(mm, pmd);
	spin_lock(ptl);
	if (unlikely(!pte_same(*pte, entry)))
		goto unlock;
	if (write_access) {
		if (!pte_write(entry))
			return do_wp_page(mm, vma, address,
					pte, pmd, ptl, entry);
		entry = pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	if (!pte_same(old_entry, entry)) {
		ptep_set_access_flags(vma, address, pte, entry, write_access);
		update_mmu_cache(vma, address, entry);
		lazy_mmu_prot_update(entry);
	} else {
		/*
		 * This is needed only for protection faults but the arch code
		 * is not yet telling us if this is a protection fault or not.
		 * This still avoids useless tlb flushes for .text page faults
		 * with threads.
		 */
		if (write_access)
			flush_tlb_page(vma, address);
	}
unlock:
	pte_unmap_unlock(pte, ptl);
	return VM_FAULT_MINOR;
}

/*
 * By the time we get here, we already hold the mm semaphore
 */
int __handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, int write_access)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	__set_current_state(TASK_RUNNING);

	count_vm_event(PGFAULT);

	if (unlikely(is_vm_hugetlb_page(vma)))
		return hugetlb_fault(mm, vma, address, write_access);

	pgd = pgd_offset(mm, address);
	pud = pud_alloc(mm, pgd, address);
	if (!pud)
		return VM_FAULT_OOM;
	pmd = pmd_alloc(mm, pud, address);
	if (!pmd)
		return VM_FAULT_OOM;
	pte = pte_alloc_map(mm, pmd, address);
	if (!pte)
		return VM_FAULT_OOM;

	return handle_pte_fault(mm, vma, address, pte, pmd, write_access);
}

EXPORT_SYMBOL_GPL(__handle_mm_fault);

#ifndef __PAGETABLE_PUD_FOLDED
/*
 * Allocate page upper directory.
 * We've already handled the fast-path in-line.
 */
int __pud_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	pud_t *new = pud_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	spin_lock(&mm->page_table_lock);
	if (pgd_present(*pgd))		/* Another has populated it */
		pud_free(new);
	else
		pgd_populate(mm, pgd, new);
	spin_unlock(&mm->page_table_lock);
	return 0;
}
#else
/* Workaround for gcc 2.96 */
int __pud_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
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
	pmd_t *new = pmd_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	spin_lock(&mm->page_table_lock);
#ifndef __ARCH_HAS_4LEVEL_HACK
	if (pud_present(*pud))		/* Another has populated it */
		pmd_free(new);
	else
		pud_populate(mm, pud, new);
#else
	if (pgd_present(*pud))		/* Another has populated it */
		pmd_free(new);
	else
		pgd_populate(mm, pud, new);
#endif /* __ARCH_HAS_4LEVEL_HACK */
	spin_unlock(&mm->page_table_lock);
	return 0;
}
#else
/* Workaround for gcc 2.96 */
int __pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	return 0;
}
#endif /* __PAGETABLE_PMD_FOLDED */

int make_pages_present(unsigned long addr, unsigned long end)
{
	int ret, len, write;
	struct vm_area_struct * vma;

	vma = find_vma(current->mm, addr);
	if (!vma)
		return -1;
	write = (vma->vm_flags & VM_WRITE) != 0;
	BUG_ON(addr >= end);
	BUG_ON(end > vma->vm_end);
	len = (end+PAGE_SIZE-1)/PAGE_SIZE-addr/PAGE_SIZE;
	ret = get_user_pages(current, current->mm, addr,
			len, write, 0, NULL, NULL);
	if (ret < 0)
		return ret;
	return ret == len ? 0 : -1;
}

/* 
 * Map a vmalloc()-space virtual address to the physical page.
 */
struct page * vmalloc_to_page(void * vmalloc_addr)
{
	unsigned long addr = (unsigned long) vmalloc_addr;
	struct page *page = NULL;
	pgd_t *pgd = pgd_offset_k(addr);
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;
  
	if (!pgd_none(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (!pud_none(*pud)) {
			pmd = pmd_offset(pud, addr);
			if (!pmd_none(*pmd)) {
				ptep = pte_offset_map(pmd, addr);
				pte = *ptep;
				if (pte_present(pte))
					page = pte_page(pte);
				pte_unmap(ptep);
			}
		}
	}
	return page;
}

EXPORT_SYMBOL(vmalloc_to_page);

/*
 * Map a vmalloc()-space virtual address to the physical page frame number.
 */
unsigned long vmalloc_to_pfn(void * vmalloc_addr)
{
	return page_to_pfn(vmalloc_to_page(vmalloc_addr));
}

EXPORT_SYMBOL(vmalloc_to_pfn);

#if !defined(__HAVE_ARCH_GATE_AREA)

#if defined(AT_SYSINFO_EHDR)
static struct vm_area_struct gate_vma;

static int __init gate_vma_init(void)
{
	gate_vma.vm_mm = NULL;
	gate_vma.vm_start = FIXADDR_USER_START;
	gate_vma.vm_end = FIXADDR_USER_END;
	gate_vma.vm_page_prot = PAGE_READONLY;
	gate_vma.vm_flags = 0;
	return 0;
}
__initcall(gate_vma_init);
#endif

struct vm_area_struct *get_gate_vma(struct task_struct *tsk)
{
#ifdef AT_SYSINFO_EHDR
	return &gate_vma;
#else
	return NULL;
#endif
}

int in_gate_area_no_task(unsigned long addr)
{
#ifdef AT_SYSINFO_EHDR
	if ((addr >= FIXADDR_USER_START) && (addr < FIXADDR_USER_END))
		return 1;
#endif
	return 0;
}

#endif	/* __HAVE_ARCH_GATE_AREA */

/*
 * Access another process' address space.
 * Source/target buffer must be kernel space,
 * Do not walk the page table directly, use get_user_pages
 */
int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct page *page;
	void *old_buf = buf;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

	down_read(&mm->mmap_sem);
	/* ignore errors, just check how much was sucessfully transfered */
	while (len) {
		int bytes, ret, offset;
		void *maddr;

		ret = get_user_pages(tsk, mm, addr, 1,
				write, 1, &page, &vma);
		if (ret <= 0)
			break;

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
		page_cache_release(page);
		len -= bytes;
		buf += bytes;
		addr += bytes;
	}
	up_read(&mm->mmap_sem);
	mmput(mm);

	return buf - old_buf;
}
