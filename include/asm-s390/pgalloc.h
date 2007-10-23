/*
 *  include/asm-s390/pgalloc.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/pgalloc.h"
 *    Copyright (C) 1994  Linus Torvalds
 */

#ifndef _S390_PGALLOC_H
#define _S390_PGALLOC_H

#include <linux/threads.h>
#include <linux/gfp.h>
#include <linux/mm.h>

#define check_pgt_cache()	do {} while (0)

unsigned long *crst_table_alloc(struct mm_struct *, int);
void crst_table_free(unsigned long *);

unsigned long *page_table_alloc(int);
void page_table_free(unsigned long *);

static inline void clear_table(unsigned long *s, unsigned long val, size_t n)
{
	*s = val;
	n = (n / 256) - 1;
	asm volatile(
#ifdef CONFIG_64BIT
		"	mvc	8(248,%0),0(%0)\n"
#else
		"	mvc	4(252,%0),0(%0)\n"
#endif
		"0:	mvc	256(256,%0),0(%0)\n"
		"	la	%0,256(%0)\n"
		"	brct	%1,0b\n"
		: "+a" (s), "+d" (n));
}

static inline void crst_table_init(unsigned long *crst, unsigned long entry)
{
	clear_table(crst, entry, sizeof(unsigned long)*2048);
	crst = get_shadow_table(crst);
	if (crst)
		clear_table(crst, entry, sizeof(unsigned long)*2048);
}

#ifndef __s390x__

static inline unsigned long pgd_entry_type(struct mm_struct *mm)
{
	return _SEGMENT_ENTRY_EMPTY;
}

#define pud_alloc_one(mm,address)		({ BUG(); ((pud_t *)2); })
#define pud_free(x)				do { } while (0)

#define pmd_alloc_one(mm,address)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)				do { } while (0)

#define pgd_populate(mm, pgd, pud)		BUG()
#define pgd_populate_kernel(mm, pgd, pud)	BUG()

#define pud_populate(mm, pud, pmd)		BUG()
#define pud_populate_kernel(mm, pud, pmd)	BUG()

#else /* __s390x__ */

static inline unsigned long pgd_entry_type(struct mm_struct *mm)
{
	return _REGION3_ENTRY_EMPTY;
}

#define pud_alloc_one(mm,address)		({ BUG(); ((pud_t *)2); })
#define pud_free(x)				do { } while (0)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long vmaddr)
{
	unsigned long *crst = crst_table_alloc(mm, s390_noexec);
	if (crst)
		crst_table_init(crst, _SEGMENT_ENTRY_EMPTY);
	return (pmd_t *) crst;
}
#define pmd_free(pmd) crst_table_free((unsigned long *) pmd)

#define pgd_populate(mm, pgd, pud)		BUG()
#define pgd_populate_kernel(mm, pgd, pud)	BUG()

static inline void pud_populate_kernel(struct mm_struct *mm,
				       pud_t *pud, pmd_t *pmd)
{
	pud_val(*pud) = _REGION3_ENTRY | __pa(pmd);
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	pud_t *shadow_pud = get_shadow_table(pud);
	pmd_t *shadow_pmd = get_shadow_table(pmd);

	if (shadow_pud && shadow_pmd)
		pud_populate_kernel(mm, shadow_pud, shadow_pmd);
	pud_populate_kernel(mm, pud, pmd);
}

#endif /* __s390x__ */

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	unsigned long *crst = crst_table_alloc(mm, s390_noexec);
	if (crst)
		crst_table_init(crst, pgd_entry_type(mm));
	return (pgd_t *) crst;
}
#define pgd_free(pgd) crst_table_free((unsigned long *) pgd)

static inline void 
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
#ifndef __s390x__
	pmd_val(pmd[0]) = _SEGMENT_ENTRY + __pa(pte);
	pmd_val(pmd[1]) = _SEGMENT_ENTRY + __pa(pte+256);
	pmd_val(pmd[2]) = _SEGMENT_ENTRY + __pa(pte+512);
	pmd_val(pmd[3]) = _SEGMENT_ENTRY + __pa(pte+768);
#else /* __s390x__ */
	pmd_val(*pmd) = _SEGMENT_ENTRY + __pa(pte);
	pmd_val1(*pmd) = _SEGMENT_ENTRY + __pa(pte+256);
#endif /* __s390x__ */
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *page)
{
	pte_t *pte = (pte_t *)page_to_phys(page);
	pmd_t *shadow_pmd = get_shadow_table(pmd);
	pte_t *shadow_pte = get_shadow_pte(pte);

	pmd_populate_kernel(mm, pmd, pte);
	if (shadow_pmd && shadow_pte)
		pmd_populate_kernel(mm, shadow_pmd, shadow_pte);
}

/*
 * page table entry allocation/free routines.
 */
#define pte_alloc_one_kernel(mm, vmaddr) \
	((pte_t *) page_table_alloc(s390_noexec))
#define pte_alloc_one(mm, vmaddr) \
	virt_to_page(page_table_alloc(s390_noexec))

#define pte_free_kernel(pte) \
	page_table_free((unsigned long *) pte)
#define pte_free(pte) \
	page_table_free((unsigned long *) page_to_phys((struct page *) pte))

#endif /* _S390_PGALLOC_H */
