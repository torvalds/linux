#ifndef __ASM_SH64_MMU_CONTEXT_H
#define __ASM_SH64_MMU_CONTEXT_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/mmu_context.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 *
 * ASID handling idea taken from MIPS implementation.
 *
 */

#ifndef __ASSEMBLY__

/*
 * Cache of MMU context last used.
 *
 * The MMU "context" consists of two things:
 *   (a) TLB cache version (or cycle, top 24 bits of mmu_context_cache)
 *   (b) ASID (Address Space IDentifier, bottom 8 bits of mmu_context_cache)
 */
extern unsigned long mmu_context_cache;

#include <linux/config.h>
#include <asm/page.h>


/* Current mm's pgd */
extern pgd_t *mmu_pdtp_cache;

#define SR_ASID_MASK		0xffffffffff00ffffULL
#define SR_ASID_SHIFT		16

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

extern __inline__ void
get_new_mmu_context(struct mm_struct *mm)
{
	extern void flush_tlb_all(void);
	extern void flush_cache_all(void);

	unsigned long mc = ++mmu_context_cache;

	if (!(mc & MMU_CONTEXT_ASID_MASK)) {
		/* We exhaust ASID of this version.
		   Flush all TLB and start new cycle. */
		flush_tlb_all();
		/* We have to flush all caches as ASIDs are
                   used in cache */
		flush_cache_all();
		/* Fix version if needed.
		   Note that we avoid version #0/asid #0 to distingush NO_CONTEXT. */
		if (!mc)
			mmu_context_cache = mc = MMU_CONTEXT_FIRST_VERSION;
	}
	mm->context = mc;
}

/*
 * Get MMU context if needed.
 */
static __inline__ void
get_mmu_context(struct mm_struct *mm)
{
	if (mm) {
		unsigned long mc = mmu_context_cache;
		/* Check if we have old version of context.
		   If it's old, we need to get new context with new version. */
		if ((mm->context ^ mc) & MMU_CONTEXT_VERSION_MASK)
			get_new_mmu_context(mm);
	}
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
static inline int init_new_context(struct task_struct *tsk,
					struct mm_struct *mm)
{
	mm->context = NO_CONTEXT;

	return 0;
}

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	extern void flush_tlb_mm(struct mm_struct *mm);

	/* Well, at least free TLB entries */
	flush_tlb_mm(mm);
}

#endif	/* __ASSEMBLY__ */

/* Common defines */
#define TLB_STEP	0x00000010
#define TLB_PTEH	0x00000000
#define TLB_PTEL	0x00000008

/* PTEH defines */
#define PTEH_ASID_SHIFT	2
#define PTEH_VALID	0x0000000000000001
#define PTEH_SHARED	0x0000000000000002
#define PTEH_MATCH_ASID	0x00000000000003ff

#ifndef __ASSEMBLY__
/* This has to be a common function because the next location to fill
 * information is shared. */
extern void __do_tlb_refill(unsigned long address, unsigned long long is_text_not_data, pte_t *pte);

/* Profiling counter. */
#ifdef CONFIG_SH64_PROC_TLB
extern unsigned long long calls_to_do_fast_page_fault;
#endif

static inline unsigned long get_asid(void)
{
	unsigned long long sr;

	asm volatile ("getcon   " __SR ", %0\n\t"
		      : "=r" (sr));

	sr = (sr >> SR_ASID_SHIFT) & MMU_CONTEXT_ASID_MASK;
	return (unsigned long) sr;
}

/* Set ASID into SR */
static inline void set_asid(unsigned long asid)
{
	unsigned long long sr, pc;

	asm volatile ("getcon	" __SR ", %0" : "=r" (sr));

	sr = (sr & SR_ASID_MASK) | (asid << SR_ASID_SHIFT);

	/*
	 * It is possible that this function may be inlined and so to avoid
	 * the assembler reporting duplicate symbols we make use of the gas trick
	 * of generating symbols using numerics and forward reference.
	 */
	asm volatile ("movi	1, %1\n\t"
		      "shlli	%1, 28, %1\n\t"
		      "or	%0, %1, %1\n\t"
		      "putcon	%1, " __SR "\n\t"
		      "putcon	%0, " __SSR "\n\t"
		      "movi	1f, %1\n\t"
		      "ori	%1, 1 , %1\n\t"
		      "putcon	%1, " __SPC "\n\t"
		      "rte\n"
		      "1:\n\t"
		      : "=r" (sr), "=r" (pc) : "0" (sr));
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static __inline__ void activate_context(struct mm_struct *mm)
{
	get_mmu_context(mm);
	set_asid(mm->context & MMU_CONTEXT_ASID_MASK);
}


static __inline__ void switch_mm(struct mm_struct *prev,
				 struct mm_struct *next,
				 struct task_struct *tsk)
{
	if (prev != next) {
		mmu_pdtp_cache = next->pgd;
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

#endif	/* __ASSEMBLY__ */

#endif /* __ASM_SH64_MMU_CONTEXT_H */
