#ifndef _ASM_GENERIC_PGTABLE_H
#define _ASM_GENERIC_PGTABLE_H

#ifndef __ASSEMBLY__

#ifndef __HAVE_ARCH_PTEP_ESTABLISH
/*
 * Establish a new mapping:
 *  - flush the old one
 *  - update the page tables
 *  - inform the TLB about the new one
 *
 * We hold the mm semaphore for reading, and the pte lock.
 *
 * Note: the old pte is known to not be writable, so we don't need to
 * worry about dirty bits etc getting lost.
 */
#define ptep_establish(__vma, __address, __ptep, __entry)		\
do {				  					\
	set_pte_at((__vma)->vm_mm, (__address), __ptep, __entry);	\
	flush_tlb_page(__vma, __address);				\
} while (0)
#endif

#ifndef __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
/*
 * Largely same as above, but only sets the access flags (dirty,
 * accessed, and writable). Furthermore, we know it always gets set
 * to a "more permissive" setting, which allows most architectures
 * to optimize this.
 */
#define ptep_set_access_flags(__vma, __address, __ptep, __entry, __dirty) \
do {				  					  \
	set_pte_at((__vma)->vm_mm, (__address), __ptep, __entry);	  \
	flush_tlb_page(__vma, __address);				  \
} while (0)
#endif

#ifndef __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young(__vma, __address, __ptep)		\
({									\
	pte_t __pte = *(__ptep);					\
	int r = 1;							\
	if (!pte_young(__pte))						\
		r = 0;							\
	else								\
		set_pte_at((__vma)->vm_mm, (__address),			\
			   (__ptep), pte_mkold(__pte));			\
	r;								\
})
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
#define ptep_clear_flush_young(__vma, __address, __ptep)		\
({									\
	int __young;							\
	__young = ptep_test_and_clear_young(__vma, __address, __ptep);	\
	if (__young)							\
		flush_tlb_page(__vma, __address);			\
	__young;							\
})
#endif

#ifndef __HAVE_ARCH_PTEP_TEST_AND_CLEAR_DIRTY
#define ptep_test_and_clear_dirty(__vma, __address, __ptep)		\
({									\
	pte_t __pte = *__ptep;						\
	int r = 1;							\
	if (!pte_dirty(__pte))						\
		r = 0;							\
	else								\
		set_pte_at((__vma)->vm_mm, (__address), (__ptep),	\
			   pte_mkclean(__pte));				\
	r;								\
})
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_DIRTY_FLUSH
#define ptep_clear_flush_dirty(__vma, __address, __ptep)		\
({									\
	int __dirty;							\
	__dirty = ptep_test_and_clear_dirty(__vma, __address, __ptep);	\
	if (__dirty)							\
		flush_tlb_page(__vma, __address);			\
	__dirty;							\
})
#endif

#ifndef __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define ptep_get_and_clear(__mm, __address, __ptep)			\
({									\
	pte_t __pte = *(__ptep);					\
	pte_clear((__mm), (__address), (__ptep));			\
	__pte;								\
})
#endif

#ifndef __HAVE_ARCH_PTEP_GET_AND_CLEAR_FULL
#define ptep_get_and_clear_full(__mm, __address, __ptep, __full)	\
({									\
	pte_t __pte;							\
	__pte = ptep_get_and_clear((__mm), (__address), (__ptep));	\
	__pte;								\
})
#endif

/*
 * Some architectures may be able to avoid expensive synchronization
 * primitives when modifications are made to PTE's which are already
 * not present, or in the process of an address space destruction.
 */
#ifndef __HAVE_ARCH_PTE_CLEAR_NOT_PRESENT_FULL
#define pte_clear_not_present_full(__mm, __address, __ptep, __full)	\
do {									\
	pte_clear((__mm), (__address), (__ptep));			\
} while (0)
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_FLUSH
#define ptep_clear_flush(__vma, __address, __ptep)			\
({									\
	pte_t __pte;							\
	__pte = ptep_get_and_clear((__vma)->vm_mm, __address, __ptep);	\
	flush_tlb_page(__vma, __address);				\
	__pte;								\
})
#endif

#ifndef __HAVE_ARCH_PTEP_SET_WRPROTECT
struct mm_struct;
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long address, pte_t *ptep)
{
	pte_t old_pte = *ptep;
	set_pte_at(mm, address, ptep, pte_wrprotect(old_pte));
}
#endif

#ifndef __HAVE_ARCH_PTE_SAME
#define pte_same(A,B)	(pte_val(A) == pte_val(B))
#endif

#ifndef __HAVE_ARCH_PAGE_TEST_DIRTY
#define page_test_dirty(page)		(0)
#endif

#ifndef __HAVE_ARCH_PAGE_CLEAR_DIRTY
#define page_clear_dirty(page)		do { } while (0)
#endif

#ifndef __HAVE_ARCH_PAGE_TEST_DIRTY
#define pte_maybe_dirty(pte)		pte_dirty(pte)
#else
#define pte_maybe_dirty(pte)		(1)
#endif

#ifndef __HAVE_ARCH_PAGE_TEST_AND_CLEAR_YOUNG
#define page_test_and_clear_young(page) (0)
#endif

#ifndef __HAVE_ARCH_PGD_OFFSET_GATE
#define pgd_offset_gate(mm, addr)	pgd_offset(mm, addr)
#endif

#ifndef __HAVE_ARCH_LAZY_MMU_PROT_UPDATE
#define lazy_mmu_prot_update(pte)	do { } while (0)
#endif

#ifndef __HAVE_ARCH_MOVE_PTE
#define move_pte(pte, prot, old_addr, new_addr)	(pte)
#endif

/*
 * A facility to provide lazy MMU batching.  This allows PTE updates and
 * page invalidations to be delayed until a call to leave lazy MMU mode
 * is issued.  Some architectures may benefit from doing this, and it is
 * beneficial for both shadow and direct mode hypervisors, which may batch
 * the PTE updates which happen during this window.  Note that using this
 * interface requires that read hazards be removed from the code.  A read
 * hazard could result in the direct mode hypervisor case, since the actual
 * write to the page tables may not yet have taken place, so reads though
 * a raw PTE pointer after it has been modified are not guaranteed to be
 * up to date.  This mode can only be entered and left under the protection of
 * the page table locks for all page tables which may be modified.  In the UP
 * case, this is required so that preemption is disabled, and in the SMP case,
 * it must synchronize the delayed page table writes properly on other CPUs.
 */
#ifndef __HAVE_ARCH_ENTER_LAZY_MMU_MODE
#define arch_enter_lazy_mmu_mode()	do {} while (0)
#define arch_leave_lazy_mmu_mode()	do {} while (0)
#define arch_flush_lazy_mmu_mode()	do {} while (0)
#endif

/*
 * A facility to provide batching of the reload of page tables with the
 * actual context switch code for paravirtualized guests.  By convention,
 * only one of the lazy modes (CPU, MMU) should be active at any given
 * time, entry should never be nested, and entry and exits should always
 * be paired.  This is for sanity of maintaining and reasoning about the
 * kernel code.
 */
#ifndef __HAVE_ARCH_ENTER_LAZY_CPU_MODE
#define arch_enter_lazy_cpu_mode()	do {} while (0)
#define arch_leave_lazy_cpu_mode()	do {} while (0)
#define arch_flush_lazy_cpu_mode()	do {} while (0)
#endif

/*
 * When walking page tables, get the address of the next boundary,
 * or the end address of the range if that comes earlier.  Although no
 * vma end wraps to 0, rounded up __boundary may wrap to 0 throughout.
 */

#define pgd_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PGDIR_SIZE) & PGDIR_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})

#ifndef pud_addr_end
#define pud_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PUD_SIZE) & PUD_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})
#endif

#ifndef pmd_addr_end
#define pmd_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PMD_SIZE) & PMD_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})
#endif

/*
 * When walking page tables, we usually want to skip any p?d_none entries;
 * and any p?d_bad entries - reporting the error before resetting to none.
 * Do the tests inline, but report and clear the bad entry in mm/memory.c.
 */
void pgd_clear_bad(pgd_t *);
void pud_clear_bad(pud_t *);
void pmd_clear_bad(pmd_t *);

static inline int pgd_none_or_clear_bad(pgd_t *pgd)
{
	if (pgd_none(*pgd))
		return 1;
	if (unlikely(pgd_bad(*pgd))) {
		pgd_clear_bad(pgd);
		return 1;
	}
	return 0;
}

static inline int pud_none_or_clear_bad(pud_t *pud)
{
	if (pud_none(*pud))
		return 1;
	if (unlikely(pud_bad(*pud))) {
		pud_clear_bad(pud);
		return 1;
	}
	return 0;
}

static inline int pmd_none_or_clear_bad(pmd_t *pmd)
{
	if (pmd_none(*pmd))
		return 1;
	if (unlikely(pmd_bad(*pmd))) {
		pmd_clear_bad(pmd);
		return 1;
	}
	return 0;
}
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_GENERIC_PGTABLE_H */
