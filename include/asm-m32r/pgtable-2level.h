#ifndef _ASM_M32R_PGTABLE_2LEVEL_H
#define _ASM_M32R_PGTABLE_2LEVEL_H

#ifdef __KERNEL__


/*
 * traditional M32R two-level paging structure:
 */

#define PGDIR_SHIFT	22
#define PTRS_PER_PGD	1024

/*
 * the M32R is two-level, so we don't really have any
 * PMD directory physically.
 */
#define PMD_SHIFT	22
#define PTRS_PER_PMD	1

#define PTRS_PER_PTE	1024

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
static inline int pgd_none(pgd_t pgd)	{ return 0; }
static inline int pgd_bad(pgd_t pgd)	{ return 0; }
static inline int pgd_present(pgd_t pgd)	{ return 1; }
#define pgd_clear(xp)				do { } while (0)

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) (*(pteptr) = pteval)
#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)

/*
 * (pmds are folded into pgds so this doesnt get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = pmdval)
#define set_pgd(pgdptr, pgdval) (*(pgdptr) = pgdval)

#define pgd_page_vaddr(pgd) \
((unsigned long) __va(pgd_val(pgd) & PAGE_MASK))

#ifndef CONFIG_DISCONTIGMEM
#define pgd_page(pgd)	(mem_map + ((pgd_val(pgd) >> PAGE_SHIFT) - PFN_BASE))
#endif /* !CONFIG_DISCONTIGMEM */

static inline pmd_t *pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

#define ptep_get_and_clear(mm,addr,xp)	__pte(xchg(&(xp)->pte, 0))
#define pte_same(a, b)		(pte_val(a) == pte_val(b))
#define pte_page(x)		pfn_to_page(pte_pfn(x))
#define pte_none(x)		(!pte_val(x))
#define pte_pfn(x)		(pte_val(x) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

#define PTE_FILE_MAX_BITS	29
#define pte_to_pgoff(pte)	(((pte_val(pte) >> 2) & 0xef) | (((pte_val(pte) >> 10)) << 7))
#define pgoff_to_pte(off)	((pte_t) { (((off) & 0xef) << 2) | (((off) >> 7) << 10) | _PAGE_FILE })

#endif /* __KERNEL__ */

#endif /* _ASM_M32R_PGTABLE_2LEVEL_H */
