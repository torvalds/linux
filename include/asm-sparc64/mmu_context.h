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

/* Initialize a new mmu context.  This is invoked when a new
 * address space instance (unique or shared) is instantiated.
 * This just needs to set mm->context to an invalid context.
 */
#define init_new_context(__tsk, __mm)	\
	(((__mm)->context.sparc64_ctx_val = 0UL), 0)

/* Destroy a dead context.  This occurs when mmput drops the
 * mm_users count to zero, the mmaps have been released, and
 * all the page tables have been flushed.  Our job is to destroy
 * any remaining processor-specific state, and in the sparc64
 * case this just means freeing up the mmu context ID held by
 * this task if valid.
 */
#define destroy_context(__mm)					\
do {	spin_lock(&ctx_alloc_lock);				\
	if (CTX_VALID((__mm)->context)) {			\
		unsigned long nr = CTX_NRBITS((__mm)->context);	\
		mmu_context_bmap[nr>>6] &= ~(1UL << (nr & 63));	\
	}							\
	spin_unlock(&ctx_alloc_lock);				\
} while(0)

/* Reload the two core values used by TLB miss handler
 * processing on sparc64.  They are:
 * 1) The physical address of mm->pgd, when full page
 *    table walks are necessary, this is where the
 *    search begins.
 * 2) A "PGD cache".  For 32-bit tasks only pgd[0] is
 *    ever used since that maps the entire low 4GB
 *    completely.  To speed up TLB miss processing we
 *    make this value available to the handlers.  This
 *    decreases the amount of memory traffic incurred.
 */
#define reload_tlbmiss_state(__tsk, __mm) \
do { \
	register unsigned long paddr asm("o5"); \
	register unsigned long pgd_cache asm("o4"); \
	paddr = __pa((__mm)->pgd); \
	pgd_cache = 0UL; \
	if ((__tsk)->thread_info->flags & _TIF_32BIT) \
		pgd_cache = get_pgd_cache((__mm)->pgd); \
	__asm__ __volatile__("wrpr	%%g0, 0x494, %%pstate\n\t" \
			     "mov	%3, %%g4\n\t" \
			     "mov	%0, %%g7\n\t" \
			     "stxa	%1, [%%g4] %2\n\t" \
			     "membar	#Sync\n\t" \
			     "wrpr	%%g0, 0x096, %%pstate" \
			     : /* no outputs */ \
			     : "r" (paddr), "r" (pgd_cache),\
			       "i" (ASI_DMMU), "i" (TSB_REG)); \
} while(0)

/* Set MMU context in the actual hardware. */
#define load_secondary_context(__mm) \
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t" \
			     "flush	%%g6" \
			     : /* No outputs */ \
			     : "r" (CTX_HWBITS((__mm)->context)), \
			       "r" (SECONDARY_CONTEXT), "i" (ASI_DMMU))

extern void __flush_tlb_mm(unsigned long, unsigned long);

/* Switch the current MM context. */
static inline void switch_mm(struct mm_struct *old_mm, struct mm_struct *mm, struct task_struct *tsk)
{
	unsigned long ctx_valid;

	spin_lock(&mm->page_table_lock);
	if (CTX_VALID(mm->context))
		ctx_valid = 1;
        else
		ctx_valid = 0;

	if (!ctx_valid || (old_mm != mm)) {
		if (!ctx_valid)
			get_new_mmu_context(mm);

		load_secondary_context(mm);
		reload_tlbmiss_state(tsk, mm);
	}

	{
		int cpu = smp_processor_id();

		/* Even if (mm == old_mm) we _must_ check
		 * the cpu_vm_mask.  If we do not we could
		 * corrupt the TLB state because of how
		 * smp_flush_tlb_{page,range,mm} on sparc64
		 * and lazy tlb switches work. -DaveM
		 */
		if (!ctx_valid || !cpu_isset(cpu, mm->cpu_vm_mask)) {
			cpu_set(cpu, mm->cpu_vm_mask);
			__flush_tlb_mm(CTX_HWBITS(mm->context),
				       SECONDARY_CONTEXT);
		}
	}
	spin_unlock(&mm->page_table_lock);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/* Activate a new MM instance for the current task. */
static inline void activate_mm(struct mm_struct *active_mm, struct mm_struct *mm)
{
	int cpu;

	spin_lock(&mm->page_table_lock);
	if (!CTX_VALID(mm->context))
		get_new_mmu_context(mm);
	cpu = smp_processor_id();
	if (!cpu_isset(cpu, mm->cpu_vm_mask))
		cpu_set(cpu, mm->cpu_vm_mask);
	spin_unlock(&mm->page_table_lock);

	load_secondary_context(mm);
	__flush_tlb_mm(CTX_HWBITS(mm->context), SECONDARY_CONTEXT);
	reload_tlbmiss_state(current, mm);
}

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_MMU_CONTEXT_H) */
