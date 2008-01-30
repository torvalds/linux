#ifndef _I386_PGTABLE_3LEVEL_H
#define _I386_PGTABLE_3LEVEL_H

/*
 * Intel Physical Address Extension (PAE) Mode - three-level page
 * tables on PPro+ CPUs.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %p(%08lx%08lx).\n", __FILE__, __LINE__, &(e), (e).pte_high, (e).pte_low)
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %p(%016Lx).\n", __FILE__, __LINE__, &(e), pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %p(%016Lx).\n", __FILE__, __LINE__, &(e), pgd_val(e))


static inline int pud_none(pud_t pud)
{
	return pud_val(pud) == 0;
}
static inline int pud_bad(pud_t pud)
{
	return (pud_val(pud) & ~(PTE_MASK | _KERNPG_TABLE | _PAGE_USER)) != 0;
}
static inline int pud_present(pud_t pud)
{
	return pud_val(pud) & _PAGE_PRESENT;
}

/* Rules for using set_pte: the pte being assigned *must* be
 * either not present or in a state where the hardware will
 * not attempt to update the pte.  In places where this is
 * not possible, use pte_get_and_clear to obtain the old pte
 * value and then use set_pte to update it.  -ben
 */
static inline void native_set_pte(pte_t *ptep, pte_t pte)
{
	ptep->pte_high = pte.pte_high;
	smp_wmb();
	ptep->pte_low = pte.pte_low;
}

/*
 * Since this is only called on user PTEs, and the page fault handler
 * must handle the already racy situation of simultaneous page faults,
 * we are justified in merely clearing the PTE present bit, followed
 * by a set.  The ordering here is important.
 */
static inline void native_set_pte_present(struct mm_struct *mm, unsigned long addr,
					  pte_t *ptep, pte_t pte)
{
	ptep->pte_low = 0;
	smp_wmb();
	ptep->pte_high = pte.pte_high;
	smp_wmb();
	ptep->pte_low = pte.pte_low;
}

static inline void native_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	set_64bit((unsigned long long *)(ptep),native_pte_val(pte));
}
static inline void native_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	set_64bit((unsigned long long *)(pmdp),native_pmd_val(pmd));
}
static inline void native_set_pud(pud_t *pudp, pud_t pud)
{
	set_64bit((unsigned long long *)(pudp),native_pud_val(pud));
}

/*
 * For PTEs and PDEs, we must clear the P-bit first when clearing a page table
 * entry, so clear the bottom half first and enforce ordering with a compiler
 * barrier.
 */
static inline void native_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	ptep->pte_low = 0;
	smp_wmb();
	ptep->pte_high = 0;
}

static inline void native_pmd_clear(pmd_t *pmd)
{
	u32 *tmp = (u32 *)pmd;
	*tmp = 0;
	smp_wmb();
	*(tmp + 1) = 0;
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));

	/*
	 * In principle we need to do a cr3 reload here to make sure
	 * the processor recognizes the changed pgd.  In practice, all
	 * the places where pud_clear() gets called are followed by
	 * full tlb flushes anyway, so we can defer the cost here.
	 *
	 * Specifically:
	 *
	 * mm/memory.c:free_pmd_range() - immediately after the
	 * pud_clear() it does a pmd_free_tlb().  We change the
	 * mmu_gather structure to do a full tlb flush (which has the
	 * effect of reloading cr3) when the pagetable free is
	 * complete.
	 *
	 * arch/x86/mm/hugetlbpage.c:huge_pmd_unshare() - the call to
	 * this is followed by a flush_tlb_range, which on x86 does a
	 * full tlb flush.
	 */
}

#define pud_page(pud) \
((struct page *) __va(pud_val(pud) & PAGE_MASK))

#define pud_page_vaddr(pud) \
((unsigned long) __va(pud_val(pud) & PAGE_MASK))


/* Find an entry in the second-level page table.. */
#define pmd_offset(pud, address) ((pmd_t *) pud_page(*(pud)) + \
			pmd_index(address))

#ifdef CONFIG_SMP
static inline pte_t native_ptep_get_and_clear(pte_t *ptep)
{
	pte_t res;

	/* xchg acts as a barrier before the setting of the high bits */
	res.pte_low = xchg(&ptep->pte_low, 0);
	res.pte_high = ptep->pte_high;
	ptep->pte_high = 0;

	return res;
}
#else
#define native_ptep_get_and_clear(xp) native_local_ptep_get_and_clear(xp)
#endif

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t a, pte_t b)
{
	return a.pte_low == b.pte_low && a.pte_high == b.pte_high;
}

#define pte_page(x)	pfn_to_page(pte_pfn(x))

static inline int pte_none(pte_t pte)
{
	return !pte.pte_low && !pte.pte_high;
}

static inline unsigned long pte_pfn(pte_t pte)
{
	return (pte_val(pte) & ~_PAGE_NX) >> PAGE_SHIFT;
}

/*
 * Bits 0, 6 and 7 are taken in the low part of the pte,
 * put the 32 bits of offset into the high part.
 */
#define pte_to_pgoff(pte) ((pte).pte_high)
#define pgoff_to_pte(off) ((pte_t) { { .pte_low = _PAGE_FILE, .pte_high = (off) } })
#define PTE_FILE_MAX_BITS       32

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val) & 0x1f)
#define __swp_offset(x)			((x).val >> 5)
#define __swp_entry(type, offset)	((swp_entry_t){(type) | (offset) << 5})
#define __pte_to_swp_entry(pte)		((swp_entry_t){ (pte).pte_high })
#define __swp_entry_to_pte(x)		((pte_t){ { .pte_high = (x).val } })

#endif /* _I386_PGTABLE_3LEVEL_H */
