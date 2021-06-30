/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PGTABLE_NOP4D_H
#define _PGTABLE_NOP4D_H

#ifndef __ASSEMBLY__

#define __PAGETABLE_P4D_FOLDED 1

typedef struct { pgd_t pgd; } p4d_t;

#define P4D_SHIFT		PGDIR_SHIFT
#define PTRS_PER_P4D		1
#define P4D_SIZE		(1UL << P4D_SHIFT)
#define P4D_MASK		(~(P4D_SIZE-1))

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the p4d is never bad, and a p4d always exists (as it's folded
 * into the pgd entry)
 */
static inline int pgd_none(pgd_t pgd)		{ return 0; }
static inline int pgd_bad(pgd_t pgd)		{ return 0; }
static inline int pgd_present(pgd_t pgd)	{ return 1; }
static inline void pgd_clear(pgd_t *pgd)	{ }
#define p4d_ERROR(p4d)				(pgd_ERROR((p4d).pgd))

#define pgd_populate(mm, pgd, p4d)		do { } while (0)
#define pgd_populate_safe(mm, pgd, p4d)		do { } while (0)
/*
 * (p4ds are folded into pgds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pgd(pgdptr, pgdval)	set_p4d((p4d_t *)(pgdptr), (p4d_t) { pgdval })

static inline p4d_t *p4d_offset(pgd_t *pgd, unsigned long address)
{
	return (p4d_t *)pgd;
}

#define p4d_val(x)				(pgd_val((x).pgd))
#define __p4d(x)				((p4d_t) { __pgd(x) })

#define pgd_page(pgd)				(p4d_page((p4d_t){ pgd }))
#define pgd_page_vaddr(pgd)			(p4d_page_vaddr((p4d_t){ pgd }))

/*
 * allocating and freeing a p4d is trivial: the 1-entry p4d is
 * inside the pgd, so has no extra memory associated with it.
 */
#define p4d_alloc_one(mm, address)		NULL
#define p4d_free(mm, x)				do { } while (0)
#define p4d_free_tlb(tlb, x, a)			do { } while (0)

#undef  p4d_addr_end
#define p4d_addr_end(addr, end)			(end)

#endif /* __ASSEMBLY__ */
#endif /* _PGTABLE_NOP4D_H */
