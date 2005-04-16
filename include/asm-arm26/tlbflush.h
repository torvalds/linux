#ifndef __ASMARM_TLBFLUSH_H
#define __ASMARM_TLBFLUSH_H

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 */

#define flush_tlb_all()				memc_update_all()
#define flush_tlb_mm(mm)			memc_update_mm(mm)
#define flush_tlb_page(vma, vmaddr)		do { printk("flush_tlb_page\n");} while (0)  // IS THIS RIGHT?
#define flush_tlb_range(vma,start,end)		\
		do { memc_update_mm(vma->vm_mm); (void)(start); (void)(end); } while (0)
#define flush_tlb_pgtables(mm,start,end)        do { printk("flush_tlb_pgtables\n");} while (0)
#define flush_tlb_kernel_range(s,e)             do { printk("flush_tlb_range\n");} while (0)

/*
 * The following handle the weird MEMC chip
 */
static inline void memc_update_all(void)
{
	struct task_struct *p;
	cpu_memc_update_all(init_mm.pgd);
	for_each_process(p) {
		if (!p->mm)
			continue;
		cpu_memc_update_all(p->mm->pgd);
	}
	processor._set_pgd(current->active_mm->pgd);
}

static inline void memc_update_mm(struct mm_struct *mm)
{
	cpu_memc_update_all(mm->pgd);

	if (mm == current->active_mm)
		processor._set_pgd(mm->pgd);
}

static inline void
memc_clear(struct mm_struct *mm, struct page *page)
{
	cpu_memc_update_entry(mm->pgd, (unsigned long) page_address(page), 0);

	if (mm == current->active_mm)
		processor._set_pgd(mm->pgd);
}

static inline void
memc_update_addr(struct mm_struct *mm, pte_t pte, unsigned long vaddr)
{
	cpu_memc_update_entry(mm->pgd, pte_val(pte), vaddr);

	if (mm == current->active_mm)
		processor._set_pgd(mm->pgd);
}

static inline void
update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	struct mm_struct *mm = vma->vm_mm;
printk("update_mmu_cache\n");
	memc_update_addr(mm, pte, addr);
}

#endif
