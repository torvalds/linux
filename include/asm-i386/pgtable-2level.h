#ifndef _I386_PGTABLE_2LEVEL_H
#define _I386_PGTABLE_2LEVEL_H

#include <asm-generic/pgtable-nopmd.h>

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, (e).pte_low)
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) (*(pteptr) = pteval)
#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)
#define set_pte_atomic(pteptr, pteval) set_pte(pteptr,pteval)
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))

#define ptep_get_and_clear(mm,addr,xp)	__pte(xchg(&(xp)->pte_low, 0))
#define pte_same(a, b)		((a).pte_low == (b).pte_low)
#define pte_page(x)		pfn_to_page(pte_pfn(x))
#define pte_none(x)		(!(x).pte_low)
#define pte_pfn(x)		((unsigned long)(((x).pte_low >> PAGE_SHIFT)))
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

/*
 * All present user pages are user-executable:
 */
static inline int pte_exec(pte_t pte)
{
	return pte_user(pte);
}

/*
 * All present pages are kernel-executable:
 */
static inline int pte_exec_kernel(pte_t pte)
{
	return 1;
}

/*
 * Bits 0, 6 and 7 are taken, split up the 29 bits of offset
 * into this range:
 */
#define PTE_FILE_MAX_BITS	29

#define pte_to_pgoff(pte) \
	((((pte).pte_low >> 1) & 0x1f ) + (((pte).pte_low >> 8) << 5 ))

#define pgoff_to_pte(off) \
	((pte_t) { (((off) & 0x1f) << 1) + (((off) >> 5) << 8) + _PAGE_FILE })

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 1) & 0x1f)
#define __swp_offset(x)			((x).val >> 8)
#define __swp_entry(type, offset)	((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { (pte).pte_low })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

void vmalloc_sync_all(void);

#endif /* _I386_PGTABLE_2LEVEL_H */
