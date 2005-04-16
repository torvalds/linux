#ifndef __X86_64_MMU_CONTEXT_H
#define __X86_64_MMU_CONTEXT_H

#include <linux/config.h>
#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/pgalloc.h>
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

/*
 * possibly do the LDT unload here?
 */
int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void destroy_context(struct mm_struct *mm);

#ifdef CONFIG_SMP

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
	if (read_pda(mmu_state) == TLBSTATE_OK) 
		write_pda(mmu_state, TLBSTATE_LAZY);
}
#else
static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}
#endif

static inline void load_cr3(pgd_t *pgd)
{
	asm volatile("movq %0,%%cr3" :: "r" (__pa(pgd)) : "memory");
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();
	if (likely(prev != next)) {
		/* stop flush ipis for the previous mm */
		clear_bit(cpu, &prev->cpu_vm_mask);
#ifdef CONFIG_SMP
		write_pda(mmu_state, TLBSTATE_OK);
		write_pda(active_mm, next);
#endif
		set_bit(cpu, &next->cpu_vm_mask);
		load_cr3(next->pgd);

		if (unlikely(next->context.ldt != prev->context.ldt)) 
			load_LDT_nolock(&next->context, cpu);
	}
#ifdef CONFIG_SMP
	else {
		write_pda(mmu_state, TLBSTATE_OK);
		if (read_pda(active_mm) != next)
			out_of_line_bug();
		if(!test_and_set_bit(cpu, &next->cpu_vm_mask)) {
			/* We were in lazy tlb mode and leave_mm disabled 
			 * tlb flush IPI delivery. We must reload CR3
			 * to make sure to use no freed page tables.
			 */
			load_cr3(next->pgd);
			load_LDT_nolock(&next->context, cpu);
		}
	}
#endif
}

#define deactivate_mm(tsk,mm)	do { \
	load_gs_index(0); \
	asm volatile("movl %0,%%fs"::"r"(0));  \
} while(0)

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL)


#endif
