#ifdef __KERNEL__
#ifndef __PPC_MMU_CONTEXT_H
#define __PPC_MMU_CONTEXT_H

#include <linux/bitops.h>

#include <asm/atomic.h>
#include <asm/mmu.h>
#include <asm/cputable.h>
#include <asm-generic/mm_hooks.h>

/*
 * On 32-bit PowerPC 6xx/7xx/7xxx CPUs, we use a set of 16 VSIDs
 * (virtual segment identifiers) for each context.  Although the
 * hardware supports 24-bit VSIDs, and thus >1 million contexts,
 * we only use 32,768 of them.  That is ample, since there can be
 * at most around 30,000 tasks in the system anyway, and it means
 * that we can use a bitmap to indicate which contexts are in use.
 * Using a bitmap means that we entirely avoid all of the problems
 * that we used to have when the context number overflowed,
 * particularly on SMP systems.
 *  -- paulus.
 */

/*
 * This function defines the mapping from contexts to VSIDs (virtual
 * segment IDs).  We use a skew on both the context and the high 4 bits
 * of the 32-bit virtual address (the "effective segment ID") in order
 * to spread out the entries in the MMU hash table.  Note, if this
 * function is changed then arch/ppc/mm/hashtable.S will have to be
 * changed to correspond.
 */
#define CTX_TO_VSID(ctx, va)	(((ctx) * (897 * 16) + ((va) >> 28) * 0x111) \
				 & 0xffffff)

/*
   The MPC8xx has only 16 contexts.  We rotate through them on each
   task switch.  A better way would be to keep track of tasks that
   own contexts, and implement an LRU usage.  That way very active
   tasks don't always have to pay the TLB reload overhead.  The
   kernel pages are mapped shared, so the kernel can run on behalf
   of any task that makes a kernel entry.  Shared does not mean they
   are not protected, just that the ASID comparison is not performed.
        -- Dan

   The IBM4xx has 256 contexts, so we can just rotate through these
   as a way of "switching" contexts.  If the TID of the TLB is zero,
   the PID/TID comparison is disabled, so we can use a TID of zero
   to represent all kernel pages as shared among all contexts.
   	-- Dan
 */

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

#ifdef CONFIG_8xx
#define NO_CONTEXT      	16
#define LAST_CONTEXT    	15
#define FIRST_CONTEXT    	0

#elif defined(CONFIG_4xx)
#define NO_CONTEXT      	256
#define LAST_CONTEXT    	255
#define FIRST_CONTEXT    	1

#elif defined(CONFIG_E200) || defined(CONFIG_E500)
#define NO_CONTEXT      	256
#define LAST_CONTEXT    	255
#define FIRST_CONTEXT    	1

#else

/* PPC 6xx, 7xx CPUs */
#define NO_CONTEXT      	((unsigned long) -1)
#define LAST_CONTEXT    	32767
#define FIRST_CONTEXT    	1
#endif

/*
 * Set the current MMU context.
 * On 32-bit PowerPCs (other than the 8xx embedded chips), this is done by
 * loading up the segment registers for the user part of the address space.
 *
 * Since the PGD is immediately available, it is much faster to simply
 * pass this along as a second parameter, which is required for 8xx and
 * can be used for debugging on all processors (if you happen to have
 * an Abatron).
 */
extern void set_context(unsigned long contextid, pgd_t *pgd);

/*
 * Bitmap of contexts in use.
 * The size of this bitmap is LAST_CONTEXT + 1 bits.
 */
extern unsigned long context_map[];

/*
 * This caches the next context number that we expect to be free.
 * Its use is an optimization only, we can't rely on this context
 * number to be free, but it usually will be.
 */
extern unsigned long next_mmu_context;

/*
 * If we don't have sufficient contexts to give one to every task
 * that could be in the system, we need to be able to steal contexts.
 * These variables support that.
 */
#if LAST_CONTEXT < 30000
#define FEW_CONTEXTS	1
extern atomic_t nr_free_contexts;
extern struct mm_struct *context_mm[LAST_CONTEXT+1];
extern void steal_context(void);
#endif

/*
 * Get a new mmu context for the address space described by `mm'.
 */
static inline void get_mmu_context(struct mm_struct *mm)
{
	unsigned long ctx;

	if (mm->context.id != NO_CONTEXT)
		return;
#ifdef FEW_CONTEXTS
	while (atomic_dec_if_positive(&nr_free_contexts) < 0)
		steal_context();
#endif
	ctx = next_mmu_context;
	while (test_and_set_bit(ctx, context_map)) {
		ctx = find_next_zero_bit(context_map, LAST_CONTEXT+1, ctx);
		if (ctx > LAST_CONTEXT)
			ctx = 0;
	}
	next_mmu_context = (ctx + 1) & LAST_CONTEXT;
	mm->context.id = ctx;
#ifdef FEW_CONTEXTS
	context_mm[ctx] = mm;
#endif
}

/*
 * Set up the context for a new address space.
 */
static inline int init_new_context(struct task_struct *t, struct mm_struct *mm)
{
	mm->context.id = NO_CONTEXT;
	mm->context.vdso_base = 0;
	return 0;
}

/*
 * We're finished using the context for an address space.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	preempt_disable();
	if (mm->context.id != NO_CONTEXT) {
		clear_bit(mm->context.id, context_map);
		mm->context.id = NO_CONTEXT;
#ifdef FEW_CONTEXTS
		atomic_inc(&nr_free_contexts);
#endif
	}
	preempt_enable();
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
	asm volatile ("dssall;\n"
#ifndef CONFIG_POWER4
	 "sync;\n" /* G4 needs a sync here, G5 apparently not */
#endif
	 : : );
#endif /* CONFIG_ALTIVEC */

	tsk->thread.pgdir = next->pgd;

	/* No need to flush userspace segments if the mm doesnt change */
	if (prev == next)
		return;

	/* Setup new userspace context */
	get_mmu_context(next);
	set_context(next->context.id, next->pgd);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
#define activate_mm(active_mm, mm)   switch_mm(active_mm, mm, current)

extern void mmu_context_init(void);

#endif /* __PPC_MMU_CONTEXT_H */
#endif /* __KERNEL__ */
