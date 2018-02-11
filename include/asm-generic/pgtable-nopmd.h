/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PGTABLE_NOPMD_H
#define _PGTABLE_NOPMD_H

#ifndef __ASSEMBLY__

#include <asm-generic/pgtable-nopud.h>

struct mm_struct;

#define __PAGETABLE_PMD_FOLDED

/*
 * Having the pmd type consist of a pud gets the size right, and allows
 * us to conceptually access the pud entry that this pmd is folded into
 * without casting.
 */
typedef struct { pud_t pud; } pmd_t;

#define PMD_SHIFT	PUD_SHIFT
#define PTRS_PER_PMD	1
#define PMD_SIZE  	(1UL << PMD_SHIFT)
#define PMD_MASK  	(~(PMD_SIZE-1))

/*
 * The "pud_xxx()" functions here are trivial for a folded two-level
 * setup: the pmd is never bad, and a pmd always exists (as it's folded
 * into the pud entry)
 */
static inline int pud_none(pud_t pud)		{ return 0; }
static inline int pud_bad(pud_t pud)		{ return 0; }
static inline int pud_present(pud_t pud)	{ return 1; }
static inline void pud_clear(pud_t *pud)	{ }
#define pmd_ERROR(pmd)				(pud_ERROR((pmd).pud))

#define pud_populate(mm, pmd, pte)		do { } while (0)

/*
 * (pmds are folded into puds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pud(pudptr, pudval)			set_pmd((pmd_t *)(pudptr), (pmd_t) { pudval })

static inline pmd_t * pmd_offset(pud_t * pud, unsigned long address)
{
	return (pmd_t *)pud;
}

#define pmd_val(x)				(pud_val((x).pud))
#define __pmd(x)				((pmd_t) { __pud(x) } )

#define pud_page(pud)				(pmd_page((pmd_t){ pud }))
#define pud_page_vaddr(pud)			(pmd_page_vaddr((pmd_t){ pud }))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pud, so has no extra memory associated with it.
 */
#define pmd_alloc_one(mm, address)		NULL
static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
}
#define __pmd_free_tlb(tlb, x, a)		do { } while (0)

#undef  pmd_addr_end
#define pmd_addr_end(addr, end)			(end)

#endif /* __ASSEMBLY__ */

#endif /* _PGTABLE_NOPMD_H */
