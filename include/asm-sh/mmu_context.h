/*
 * Copyright (C) 1999 Niibe Yutaka
 * Copyright (C) 2003 Paul Mundt
 *
 * ASID handling idea taken from MIPS implementation.
 */
#ifndef __ASM_SH_MMU_CONTEXT_H
#define __ASM_SH_MMU_CONTEXT_H
#ifdef __KERNEL__

#include <asm/cpu/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/*
 * The MMU "context" consists of two things:
 *    (a) TLB cache version (or round, cycle whatever expression you like)
 *    (b) ASID (Address Space IDentifier)
 */

/*
 * Cache of MMU context last used.
 */
extern unsigned long mmu_context_cache;

#define MMU_CONTEXT_ASID_MASK		0x000000ff
#define MMU_CONTEXT_VERSION_MASK	0xffffff00
#define MMU_CONTEXT_FIRST_VERSION	0x00000100
#define NO_CONTEXT			0

/* ASID is 8-bit value, so it can't be 0x100 */
#define MMU_NO_ASID			0x100

/*
 * Virtual Page Number mask
 */
#define MMU_VPN_MASK	0xfffff000

#ifdef CONFIG_MMU
/*
 * Get MMU context if needed.
 */
static inline void get_mmu_context(struct mm_struct *mm)
{
	unsigned long mc = mmu_context_cache;

	/* Check if we have old version of context. */
	if (((mm->context.id ^ mc) & MMU_CONTEXT_VERSION_MASK) == 0)
		/* It's up to date, do nothing */
		return;

	/* It's old, we need to get new context with new version. */
	mc = ++mmu_context_cache;
	if (!(mc & MMU_CONTEXT_ASID_MASK)) {
		/*
		 * We exhaust ASID of this version.
		 * Flush all TLB and start new cycle.
		 */
		flush_tlb_all();

		/*
		 * Fix version; Note that we avoid version #0
		 * to distingush NO_CONTEXT.
		 */
		if (!mc)
			mmu_context_cache = mc = MMU_CONTEXT_FIRST_VERSION;
	}
	mm->context.id = mc;
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
static inline int init_new_context(struct task_struct *tsk,
				       struct mm_struct *mm)
{
	mm->context.id = NO_CONTEXT;
	return 0;
}

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	/* Do nothing */
}

static inline void set_asid(unsigned long asid)
{
	unsigned long __dummy;

	__asm__ __volatile__ ("mov.l	%2, %0\n\t"
			      "and	%3, %0\n\t"
			      "or	%1, %0\n\t"
			      "mov.l	%0, %2"
			      : "=&r" (__dummy)
			      : "r" (asid), "m" (__m(MMU_PTEH)),
			        "r" (0xffffff00));
}

static inline unsigned long get_asid(void)
{
	unsigned long asid;

	__asm__ __volatile__ ("mov.l	%1, %0"
			      : "=r" (asid)
			      : "m" (__m(MMU_PTEH)));
	asid &= MMU_CONTEXT_ASID_MASK;
	return asid;
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_context(struct mm_struct *mm)
{
	get_mmu_context(mm);
	set_asid(mm->context.id & MMU_CONTEXT_ASID_MASK);
}

/* MMU_TTB is used for optimizing the fault handling. */
static inline void set_TTB(pgd_t *pgd)
{
	ctrl_outl((unsigned long)pgd, MMU_TTB);
}

static inline pgd_t *get_TTB(void)
{
	return (pgd_t *)ctrl_inl(MMU_TTB);
}

static inline void switch_mm(struct mm_struct *prev,
			     struct mm_struct *next,
			     struct task_struct *tsk)
{
	if (likely(prev != next)) {
		set_TTB(next->pgd);
		activate_context(next);
	}
}

#define deactivate_mm(tsk,mm)	do { } while (0)

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL)

static inline void
enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}
#else /* !CONFIG_MMU */
#define get_mmu_context(mm)		do { } while (0)
#define init_new_context(tsk,mm)	(0)
#define destroy_context(mm)		do { } while (0)
#define set_asid(asid)			do { } while (0)
#define get_asid()			(0)
#define activate_context(mm)		do { } while (0)
#define switch_mm(prev,next,tsk)	do { } while (0)
#define deactivate_mm(tsk,mm)		do { } while (0)
#define activate_mm(prev,next)		do { } while (0)
#define enter_lazy_tlb(mm,tsk)		do { } while (0)
#endif /* CONFIG_MMU */

#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4)
/*
 * If this processor has an MMU, we need methods to turn it off/on ..
 * paging_init() will also have to be updated for the processor in
 * question.
 */
static inline void enable_mmu(void)
{
	/* Enable MMU */
	ctrl_outl(MMU_CONTROL_INIT, MMUCR);
	ctrl_barrier();

	if (mmu_context_cache == NO_CONTEXT)
		mmu_context_cache = MMU_CONTEXT_FIRST_VERSION;

	set_asid(mmu_context_cache & MMU_CONTEXT_ASID_MASK);
}

static inline void disable_mmu(void)
{
	unsigned long cr;

	cr = ctrl_inl(MMUCR);
	cr &= ~MMU_CONTROL_INIT;
	ctrl_outl(cr, MMUCR);

	ctrl_barrier();
}
#else
/*
 * MMU control handlers for processors lacking memory
 * management hardware.
 */
#define enable_mmu()	do { BUG(); } while (0)
#define disable_mmu()	do { BUG(); } while (0)
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_SH_MMU_CONTEXT_H */
