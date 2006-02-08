/* $Id: mmu_context.h,v 1.54 2002/02/09 19:49:31 davem Exp $ */
#ifndef __SPARC64_MMU_CONTEXT_H
#define __SPARC64_MMU_CONTEXT_H

/* Derived heavily from Linus's Alpha/AXP ASN code... */

#ifndef __ASSEMBLY__

#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/spitfire.h>

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

extern spinlock_t ctx_alloc_lock;
extern unsigned long tlb_context_cache;
extern unsigned long mmu_context_bmap[];

extern void get_new_mmu_context(struct mm_struct *mm);
extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void destroy_context(struct mm_struct *mm);

extern void __tsb_context_switch(unsigned long pgd_pa, unsigned long tsb_reg,
				 unsigned long tsb_vaddr, unsigned long tsb_pte);

static inline void tsb_context_switch(struct mm_struct *mm)
{
	__tsb_context_switch(__pa(mm->pgd), mm->context.tsb_reg_val,
			     mm->context.tsb_map_vaddr,
			     mm->context.tsb_map_pte);
}

extern void tsb_grow(struct mm_struct *mm, unsigned long mm_rss, gfp_t gfp_flags);
#ifdef CONFIG_SMP
extern void smp_tsb_sync(struct mm_struct *mm);
#else
#define smp_tsb_sync(__mm) do { } while (0)
#endif

/* Set MMU context in the actual hardware. */
#define load_secondary_context(__mm) \
	__asm__ __volatile__( \
	"\n661:	stxa		%0, [%1] %2\n" \
	"	.section	.sun4v_1insn_patch, \"ax\"\n" \
	"	.word		661b\n" \
	"	stxa		%0, [%1] %3\n" \
	"	.previous\n" \
	"	flush		%%g6\n" \
	: /* No outputs */ \
	: "r" (CTX_HWBITS((__mm)->context)), \
	  "r" (SECONDARY_CONTEXT), "i" (ASI_DMMU), "i" (ASI_MMU))

extern void __flush_tlb_mm(unsigned long, unsigned long);

/* Switch the current MM context. */
static inline void switch_mm(struct mm_struct *old_mm, struct mm_struct *mm, struct task_struct *tsk)
{
	unsigned long ctx_valid;
	int cpu;

	/* Note: page_table_lock is used here to serialize switch_mm
	 * and activate_mm, and their calls to get_new_mmu_context.
	 * This use of page_table_lock is unrelated to its other uses.
	 */ 
	spin_lock(&mm->page_table_lock);
	ctx_valid = CTX_VALID(mm->context);
	if (!ctx_valid)
		get_new_mmu_context(mm);
	spin_unlock(&mm->page_table_lock);

	if (!ctx_valid || (old_mm != mm)) {
		load_secondary_context(mm);
		tsb_context_switch(mm);
	}

	/* Even if (mm == old_mm) we _must_ check
	 * the cpu_vm_mask.  If we do not we could
	 * corrupt the TLB state because of how
	 * smp_flush_tlb_{page,range,mm} on sparc64
	 * and lazy tlb switches work. -DaveM
	 */
	cpu = smp_processor_id();
	if (!ctx_valid || !cpu_isset(cpu, mm->cpu_vm_mask)) {
		cpu_set(cpu, mm->cpu_vm_mask);
		__flush_tlb_mm(CTX_HWBITS(mm->context),
			       SECONDARY_CONTEXT);
	}
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/* Activate a new MM instance for the current task. */
static inline void activate_mm(struct mm_struct *active_mm, struct mm_struct *mm)
{
	int cpu;

	/* Note: page_table_lock is used here to serialize switch_mm
	 * and activate_mm, and their calls to get_new_mmu_context.
	 * This use of page_table_lock is unrelated to its other uses.
	 */ 
	spin_lock(&mm->page_table_lock);
	if (!CTX_VALID(mm->context))
		get_new_mmu_context(mm);
	cpu = smp_processor_id();
	if (!cpu_isset(cpu, mm->cpu_vm_mask))
		cpu_set(cpu, mm->cpu_vm_mask);
	spin_unlock(&mm->page_table_lock);

	load_secondary_context(mm);
	__flush_tlb_mm(CTX_HWBITS(mm->context), SECONDARY_CONTEXT);
	tsb_context_switch(mm);
}

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_MMU_CONTEXT_H) */
