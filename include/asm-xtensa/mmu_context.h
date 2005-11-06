/*
 * include/asm-xtensa/mmu_context.h
 *
 * Switch an MMU context.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_MMU_CONTEXT_H
#define _XTENSA_MMU_CONTEXT_H

#include <linux/config.h>
#include <linux/stringify.h>

#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

/*
 * Linux was ported to Xtensa assuming all auto-refill ways in set 0
 * had the same properties (a very likely assumption).  Multiple sets
 * of auto-refill ways will still work properly, but not as optimally
 * as the Xtensa designer may have assumed.
 *
 * We make this case a hard #error, killing the kernel build, to alert
 * the developer to this condition (which is more likely an error).
 * You super-duper clever developers can change it to a warning or
 * remove it altogether if you think you know what you're doing.  :)
 */

#if (XCHAL_HAVE_TLBS != 1)
# error "Linux must have an MMU!"
#endif

#if ((XCHAL_ITLB_ARF_WAYS == 0) || (XCHAL_DTLB_ARF_WAYS == 0))
# error "MMU must have auto-refill ways"
#endif

#if ((XCHAL_ITLB_ARF_SETS != 1) || (XCHAL_DTLB_ARF_SETS != 1))
# error Linux may not use all auto-refill ways as efficiently as you think
#endif

#if (XCHAL_MMU_MAX_PTE_PAGE_SIZE != XCHAL_MMU_MIN_PTE_PAGE_SIZE)
# error Only one page size allowed!
#endif

extern unsigned long asid_cache;
extern pgd_t *current_pgd;

/*
 * Define the number of entries per auto-refill way in set 0 of both I and D
 * TLBs.  We deal only with set 0 here (an assumption further explained in
 * assertions.h).  Also, define the total number of ARF entries in both TLBs.
 */

#define ITLB_ENTRIES_PER_ARF_WAY  (XCHAL_ITLB_SET(XCHAL_ITLB_ARF_SET0,ENTRIES))
#define DTLB_ENTRIES_PER_ARF_WAY  (XCHAL_DTLB_SET(XCHAL_DTLB_ARF_SET0,ENTRIES))

#define ITLB_ENTRIES							\
 	(ITLB_ENTRIES_PER_ARF_WAY * (XCHAL_ITLB_SET(XCHAL_ITLB_ARF_SET0,WAYS)))
#define DTLB_ENTRIES							\
	(DTLB_ENTRIES_PER_ARF_WAY * (XCHAL_DTLB_SET(XCHAL_DTLB_ARF_SET0,WAYS)))


/*
 * SMALLEST_NTLB_ENTRIES is the smaller of ITLB_ENTRIES and DTLB_ENTRIES.
 * In practice, they are probably equal.  This macro simplifies function
 * flush_tlb_range().
 */

#if (DTLB_ENTRIES < ITLB_ENTRIES)
# define SMALLEST_NTLB_ENTRIES  DTLB_ENTRIES
#else
# define SMALLEST_NTLB_ENTRIES  ITLB_ENTRIES
#endif


/*
 * asid_cache tracks only the ASID[USER_RING] field of the RASID special
 * register, which is the current user-task asid allocation value.
 * mm->context has the same meaning.  When it comes time to write the
 * asid_cache or mm->context values to the RASID special register, we first
 * shift the value left by 8, then insert the value.
 * ASID[0] always contains the kernel's asid value, and we reserve three
 * other asid values that we never assign to user tasks.
 */

#define ASID_INC	0x1
#define ASID_MASK	((1 << XCHAL_MMU_ASID_BITS) - 1)

/*
 * XCHAL_MMU_ASID_INVALID is a configurable Xtensa processor constant
 * indicating invalid address space.  XCHAL_MMU_ASID_KERNEL is a configurable
 * Xtensa processor constant indicating the kernel address space.  They can
 * be arbitrary values.
 *
 * We identify three more unique, reserved ASID values to use in the unused
 * ring positions.  No other user process will be assigned these reserved
 * ASID values.
 *
 * For example, given that
 *
 *	XCHAL_MMU_ASID_INVALID == 0
 *	XCHAL_MMU_ASID_KERNEL  == 1
 *
 * the following maze of #if statements would generate
 *
 *	ASID_RESERVED_1        == 2
 *	ASID_RESERVED_2        == 3
 *	ASID_RESERVED_3        == 4
 *	ASID_FIRST_NONRESERVED == 5
 */

#if (XCHAL_MMU_ASID_INVALID != XCHAL_MMU_ASID_KERNEL + 1)
# define ASID_RESERVED_1         ((XCHAL_MMU_ASID_KERNEL + 1) & ASID_MASK)
#else
# define ASID_RESERVED_1         ((XCHAL_MMU_ASID_KERNEL + 2) & ASID_MASK)
#endif

#if (XCHAL_MMU_ASID_INVALID != ASID_RESERVED_1 + 1)
# define ASID_RESERVED_2         ((ASID_RESERVED_1 + 1) & ASID_MASK)
#else
# define ASID_RESERVED_2         ((ASID_RESERVED_1 + 2) & ASID_MASK)
#endif

#if (XCHAL_MMU_ASID_INVALID != ASID_RESERVED_2 + 1)
# define ASID_RESERVED_3         ((ASID_RESERVED_2 + 1) & ASID_MASK)
#else
# define ASID_RESERVED_3         ((ASID_RESERVED_2 + 2) & ASID_MASK)
#endif

#if (XCHAL_MMU_ASID_INVALID != ASID_RESERVED_3 + 1)
# define ASID_FIRST_NONRESERVED  ((ASID_RESERVED_3 + 1) & ASID_MASK)
#else
# define ASID_FIRST_NONRESERVED  ((ASID_RESERVED_3 + 2) & ASID_MASK)
#endif

#define ASID_ALL_RESERVED ( ((ASID_RESERVED_1) << 24) + \
                            ((ASID_RESERVED_2) << 16) + \
                            ((ASID_RESERVED_3) <<  8) + \
                            ((XCHAL_MMU_ASID_KERNEL))   )


/*
 * NO_CONTEXT is the invalid ASID value that we don't ever assign to
 * any user or kernel context.  NO_CONTEXT is a better mnemonic than
 * XCHAL_MMU_ASID_INVALID, so we use it in code instead.
 */

#define NO_CONTEXT   XCHAL_MMU_ASID_INVALID

#if (KERNEL_RING != 0)
# error The KERNEL_RING really should be zero.
#endif

#if (USER_RING >= XCHAL_MMU_RINGS)
# error USER_RING cannot be greater than the highest numbered ring.
#endif

#if (USER_RING == KERNEL_RING)
# error The user and kernel rings really should not be equal.
#endif

#if (USER_RING == 1)
#define ASID_INSERT(x) ( ((ASID_RESERVED_1)   << 24) + \
                         ((ASID_RESERVED_2)   << 16) + \
                         (((x) & (ASID_MASK)) <<  8) + \
                         ((XCHAL_MMU_ASID_KERNEL))   )

#elif (USER_RING == 2)
#define ASID_INSERT(x) ( ((ASID_RESERVED_1)   << 24) + \
                         (((x) & (ASID_MASK)) << 16) + \
                         ((ASID_RESERVED_2)   <<  8) + \
                         ((XCHAL_MMU_ASID_KERNEL))   )

#elif (USER_RING == 3)
#define ASID_INSERT(x) ( (((x) & (ASID_MASK)) << 24) + \
			 ((ASID_RESERVED_1)   << 16) + \
                         ((ASID_RESERVED_2)   <<  8) + \
                         ((XCHAL_MMU_ASID_KERNEL))   )

#else
#error Goofy value for USER_RING

#endif /* USER_RING == 1 */


/*
 *  All unused by hardware upper bits will be considered
 *  as a software asid extension.
 */

#define ASID_VERSION_MASK  ((unsigned long)~(ASID_MASK|(ASID_MASK-1)))
#define ASID_FIRST_VERSION						\
	((unsigned long)(~ASID_VERSION_MASK) + 1 + ASID_FIRST_NONRESERVED)

static inline void set_rasid_register (unsigned long val)
{
	__asm__ __volatile__ (" wsr %0, "__stringify(RASID)"\n\t"
			      " isync\n" : : "a" (val));
}

static inline unsigned long get_rasid_register (void)
{
	unsigned long tmp;
	__asm__ __volatile__ (" rsr %0, "__stringify(RASID)"\n\t" : "=a" (tmp));
	return tmp;
}


#if ((XCHAL_MMU_ASID_INVALID == 0) && (XCHAL_MMU_ASID_KERNEL == 1))

static inline void
get_new_mmu_context(struct mm_struct *mm, unsigned long asid)
{
	extern void flush_tlb_all(void);
	if (! ((asid += ASID_INC) & ASID_MASK) ) {
		flush_tlb_all(); /* start new asid cycle */
		if (!asid)      /* fix version if needed */
			asid = ASID_FIRST_VERSION - ASID_FIRST_NONRESERVED;
		asid += ASID_FIRST_NONRESERVED;
	}
	mm->context = asid_cache = asid;
}

#else
#warning ASID_{INVALID,KERNEL} values impose non-optimal get_new_mmu_context implementation

/* XCHAL_MMU_ASID_INVALID == 0 and XCHAL_MMU_ASID_KERNEL ==1 are
   really the best, but if you insist... */

static inline int validate_asid (unsigned long asid)
{
	switch (asid) {
	case XCHAL_MMU_ASID_INVALID:
	case XCHAL_MMU_ASID_KERNEL:
	case ASID_RESERVED_1:
	case ASID_RESERVED_2:
	case ASID_RESERVED_3:
		return 0; /* can't use these values as ASIDs */
	}
	return 1; /* valid */
}

static inline void
get_new_mmu_context(struct mm_struct *mm, unsigned long asid)
{
	extern void flush_tlb_all(void);
	while (1) {
		asid += ASID_INC;
		if ( ! (asid & ASID_MASK) ) {
			flush_tlb_all(); /* start new asid cycle */
			if (!asid)      /* fix version if needed */
				asid = ASID_FIRST_VERSION - ASID_FIRST_NONRESERVED;
			asid += ASID_FIRST_NONRESERVED;
			break; /* no need to validate here */
		}
		if (validate_asid (asid & ASID_MASK))
			break;
	}
	mm->context = asid_cache = asid;
}

#endif


/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */

static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context = NO_CONTEXT;
	return 0;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
                             struct task_struct *tsk)
{
	unsigned long asid = asid_cache;

	/* Check if our ASID is of an older version and thus invalid */

	if ((next->context ^ asid) & ASID_VERSION_MASK)
		get_new_mmu_context(next, asid);

	set_rasid_register (ASID_INSERT(next->context));
	invalidate_page_directory();
}

#define deactivate_mm(tsk, mm)	do { } while(0)

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	/* Nothing to do. */
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void
activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	/* Unconditionally get a new ASID.  */

	get_new_mmu_context(next, asid_cache);
	set_rasid_register (ASID_INSERT(next->context));
	invalidate_page_directory();
}


static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
	/* Nothing to do. */

}

#endif /* _XTENSA_MMU_CONTEXT_H */
