#ifndef _I386_PGTABLE_2LEVEL_H
#define _I386_PGTABLE_2LEVEL_H

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, (e).pte_low)
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void native_set_pte(pte_t *ptep , pte_t pte)
{
	*ptep = pte;
}

static inline void native_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

static inline void native_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	native_set_pte(ptep, pte);
}

static inline void native_set_pte_present(struct mm_struct *mm,
					  unsigned long addr,
					  pte_t *ptep, pte_t pte)
{
	native_set_pte(ptep, pte);
}

static inline void native_pmd_clear(pmd_t *pmdp)
{
	native_set_pmd(pmdp, __pmd(0));
}

static inline void native_pte_clear(struct mm_struct *mm,
				    unsigned long addr, pte_t *xp)
{
	*xp = native_make_pte(0);
}

#ifdef CONFIG_SMP
static inline pte_t native_ptep_get_and_clear(pte_t *xp)
{
	return __pte(xchg(&xp->pte_low, 0));
}
#else
#define native_ptep_get_and_clear(xp) native_local_ptep_get_and_clear(xp)
#endif

#define pte_none(x)		(!(x).pte_low)

/*
 * Bits 0, 6 and 7 are taken, split up the 29 bits of offset
 * into this range:
 */
#define PTE_FILE_MAX_BITS	29

#define pte_to_pgoff(pte)						\
	((((pte).pte_low >> 1) & 0x1f) + (((pte).pte_low >> 8) << 5))

#define pgoff_to_pte(off)						\
	((pte_t) { .pte_low = (((off) & 0x1f) << 1) +			\
			(((off) >> 5) << 8) + _PAGE_FILE })

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 1) & 0x1f)
#define __swp_offset(x)			((x).val >> 8)
#define __swp_entry(type, offset)				\
	((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { (pte).pte_low })
#define __swp_entry_to_pte(x)		((pte_t) { .pte = (x).val })

#endif /* _I386_PGTABLE_2LEVEL_H */
