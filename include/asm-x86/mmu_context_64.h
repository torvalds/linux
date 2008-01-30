#ifndef __X86_64_MMU_CONTEXT_H
#define __X86_64_MMU_CONTEXT_H

#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/pgalloc.h>
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#ifndef CONFIG_PARAVIRT
#include <asm-generic/mm_hooks.h>
#endif

/*
 * possibly do the LDT unload here?
 */
int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void destroy_context(struct mm_struct *mm);

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
#ifdef CONFIG_SMP
	if (read_pda(mmu_state) == TLBSTATE_OK) 
		write_pda(mmu_state, TLBSTATE_LAZY);
#endif
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();
	if (likely(prev != next)) {
		/* stop flush ipis for the previous mm */
		cpu_clear(cpu, prev->cpu_vm_mask);
#ifdef CONFIG_SMP
		write_pda(mmu_state, TLBSTATE_OK);
		write_pda(active_mm, next);
#endif
		cpu_set(cpu, next->cpu_vm_mask);
		load_cr3(next->pgd);

		if (unlikely(next->context.ldt != prev->context.ldt)) 
			load_LDT_nolock(&next->context);
	}
#ifdef CONFIG_SMP
	else {
		write_pda(mmu_state, TLBSTATE_OK);
		if (read_pda(active_mm) != next)
			BUG();
		if (!cpu_test_and_set(cpu, next->cpu_vm_mask)) {
			/* We were in lazy tlb mode and leave_mm disabled 
			 * tlb flush IPI delivery. We must reload CR3
			 * to make sure to use no freed page tables.
			 */
			load_cr3(next->pgd);
			load_LDT_nolock(&next->context);
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
