#ifndef __ASM_SH64_PGALLOC_H
#define __ASM_SH64_PGALLOC_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/pgalloc.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 * Copyright (C) 2003, 2004  Richard Curnow
 *
 */

#include <linux/threads.h>
#include <linux/mm.h>

#define pgd_quicklist (current_cpu_data.pgd_quick)
#define pmd_quicklist (current_cpu_data.pmd_quick)
#define pte_quicklist (current_cpu_data.pte_quick)
#define pgtable_cache_size (current_cpu_data.pgtable_cache_sz)

static inline void pgd_init(unsigned long page)
{
	unsigned long *pgd = (unsigned long *)page;
	extern pte_t empty_bad_pte_table[PTRS_PER_PTE];
	int i;

	for (i = 0; i < USER_PTRS_PER_PGD; i++)
		pgd[i] = (unsigned long)empty_bad_pte_table;
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static inline pgd_t *get_pgd_slow(void)
{
	unsigned int pgd_size = (USER_PTRS_PER_PGD * sizeof(pgd_t));
	pgd_t *ret = kmalloc(pgd_size, GFP_KERNEL);
	return ret;
}

static inline pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if ((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	} else
		ret = (unsigned long *)get_pgd_slow();

	if (ret) {
		memset(ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
	}
	return (pgd_t *)ret;
}

static inline void free_pgd_fast(pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

static inline void free_pgd_slow(pgd_t *pgd)
{
	kfree((void *)pgd);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);
extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long address_preadjusted);

static inline pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

static inline void free_pte_fast(pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

static inline void pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					   unsigned long address)
{
	pte_t *pte;

	pte = (pte_t *)__get_free_page(GFP_KERNEL | __GFP_REPEAT|__GFP_ZERO);

	return pte;
}

static inline struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;

	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);

	return pte;
}

#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#if defined(CONFIG_SH64_PGTABLE_2_LEVEL)

#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)			do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()
#define __pte_free_tlb(tlb,pte)		tlb_remove_page((tlb),(pte))
#define __pmd_free_tlb(tlb,pmd)		do { } while (0)

#elif defined(CONFIG_SH64_PGTABLE_3_LEVEL)

static __inline__ pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pmd_t *pmd;
	pmd = (pmd_t *) __get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
	return pmd;
}

static __inline__ void pmd_free(pmd_t *pmd)
{
	free_page((unsigned long) pmd);
}

#define pgd_populate(mm, pgd, pmd) pgd_set(pgd, pmd)
#define __pmd_free_tlb(tlb,pmd)		pmd_free(pmd)

#else
#error "No defined page table size"
#endif

#define check_pgt_cache()		do { } while (0)
#define pgd_free(pgd)		free_pgd_slow(pgd)
#define pgd_alloc(mm)		get_pgd_fast()

extern int do_check_pgt_cache(int, int);

#define pmd_populate_kernel(mm, pmd, pte) \
	set_pmd(pmd, __pmd(_PAGE_TABLE + (unsigned long) (pte)))

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE + (unsigned long) page_address (pte)));
}

#endif /* __ASM_SH64_PGALLOC_H */
