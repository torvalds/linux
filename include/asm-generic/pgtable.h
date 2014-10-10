#ifndef _ASM_GENERIC_PGTABLE_H
#define _ASM_GENERIC_PGTABLE_H

#ifndef __ASSEMBLY__
#ifdef CONFIG_MMU

#include <linux/mm_types.h>
#include <linux/bug.h>

/*
 * On almost all architectures and configurations, 0 can be used as the
 * upper ceiling to free_pgtables(): on many architectures it has the same
 * effect as using TASK_SIZE.  However, there is one configuration which
 * must impose a more careful limit, to avoid freeing kernel pgtables.
 */
#ifndef USER_PGTABLES_CEILING
#define USER_PGTABLES_CEILING	0UL
#endif

#ifndef __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
extern int ptep_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pte_t *ptep,
				 pte_t entry, int dirty);
#endif

#ifndef __HAVE_ARCH_PMDP_SET_ACCESS_FLAGS
extern int pmdp_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pmd_t *pmdp,
				 pmd_t entry, int dirty);
#endif

#ifndef __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pte_t *ptep)
{
	pte_t pte = *ptep;
	int r = 1;
	if (!pte_young(pte))
		r = 0;
	else
		set_pte_at(vma->vm_mm, address, ptep, pte_mkold(pte));
	return r;
}
#endif

#ifndef __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline int pmdp_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;
	int r = 1;
	if (!pmd_young(pmd))
		r = 0;
	else
		set_pmd_at(vma->vm_mm, address, pmdp, pmd_mkold(pmd));
	return r;
}
#else /* CONFIG_TRANSPARENT_HUGEPAGE */
static inline int pmdp_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pmd_t *pmdp)
{
	BUG();
	return 0;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
int ptep_clear_flush_young(struct vm_area_struct *vma,
			   unsigned long address, pte_t *ptep);
#endif

#ifndef __HAVE_ARCH_PMDP_CLEAR_YOUNG_FLUSH
int pmdp_clear_flush_young(struct vm_area_struct *vma,
			   unsigned long address, pmd_t *pmdp);
#endif

#ifndef __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long address,
				       pte_t *ptep)
{
	pte_t pte = *ptep;
	pte_clear(mm, address, ptep);
	return pte;
}
#endif

#ifndef __HAVE_ARCH_PMDP_GET_AND_CLEAR
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline pmd_t pmdp_get_and_clear(struct mm_struct *mm,
				       unsigned long address,
				       pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;
	pmd_clear(pmdp);
	return pmd;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
#endif

#ifndef __HAVE_ARCH_PTEP_GET_AND_CLEAR_FULL
static inline pte_t ptep_get_and_clear_full(struct mm_struct *mm,
					    unsigned long address, pte_t *ptep,
					    int full)
{
	pte_t pte;
	pte = ptep_get_and_clear(mm, address, ptep);
	return pte;
}
#endif

/*
 * Some architectures may be able to avoid expensive synchronization
 * primitives when modifications are made to PTE's which are already
 * not present, or in the process of an address space destruction.
 */
#ifndef __HAVE_ARCH_PTE_CLEAR_NOT_PRESENT_FULL
static inline void pte_clear_not_present_full(struct mm_struct *mm,
					      unsigned long address,
					      pte_t *ptep,
					      int full)
{
	pte_clear(mm, address, ptep);
}
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_FLUSH
extern pte_t ptep_clear_flush(struct vm_area_struct *vma,
			      unsigned long address,
			      pte_t *ptep);
#endif

#ifndef __HAVE_ARCH_PMDP_CLEAR_FLUSH
extern pmd_t pmdp_clear_flush(struct vm_area_struct *vma,
			      unsigned long address,
			      pmd_t *pmdp);
#endif

#ifndef __HAVE_ARCH_PTEP_SET_WRPROTECT
struct mm_struct;
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long address, pte_t *ptep)
{
	pte_t old_pte = *ptep;
	set_pte_at(mm, address, ptep, pte_wrprotect(old_pte));
}
#endif

#ifndef __HAVE_ARCH_PMDP_SET_WRPROTECT
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline void pmdp_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pmd_t *pmdp)
{
	pmd_t old_pmd = *pmdp;
	set_pmd_at(mm, address, pmdp, pmd_wrprotect(old_pmd));
}
#else /* CONFIG_TRANSPARENT_HUGEPAGE */
static inline void pmdp_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pmd_t *pmdp)
{
	BUG();
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
#endif

#ifndef __HAVE_ARCH_PMDP_SPLITTING_FLUSH
extern void pmdp_splitting_flush(struct vm_area_struct *vma,
				 unsigned long address, pmd_t *pmdp);
#endif

#ifndef __HAVE_ARCH_PGTABLE_DEPOSIT
extern void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				       pgtable_t pgtable);
#endif

#ifndef __HAVE_ARCH_PGTABLE_WITHDRAW
extern pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp);
#endif

#ifndef __HAVE_ARCH_PMDP_INVALIDATE
extern void pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
			    pmd_t *pmdp);
#endif

#ifndef __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t pte_a, pte_t pte_b)
{
	return pte_val(pte_a) == pte_val(pte_b);
}
#endif

#ifndef __HAVE_ARCH_PTE_UNUSED
/*
 * Some architectures provide facilities to virtualization guests
 * so that they can flag allocated pages as unused. This allows the
 * host to transparently reclaim unused pages. This function returns
 * whether the pte's page is unused.
 */
static inline int pte_unused(pte_t pte)
{
	return 0;
}
#endif

#ifndef __HAVE_ARCH_PMD_SAME
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline int pmd_same(pmd_t pmd_a, pmd_t pmd_b)
{
	return pmd_val(pmd_a) == pmd_val(pmd_b);
}
#else /* CONFIG_TRANSPARENT_HUGEPAGE */
static inline int pmd_same(pmd_t pmd_a, pmd_t pmd_b)
{
	BUG();
	return 0;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
#endif

#ifndef __HAVE_ARCH_PGD_OFFSET_GATE
#define pgd_offset_gate(mm, addr)	pgd_offset(mm, addr)
#endif

#ifndef __HAVE_ARCH_MOVE_PTE
#define move_pte(pte, prot, old_addr, new_addr)	(pte)
#endif

#ifndef pte_accessible
# define pte_accessible(mm, pte)	((void)(pte), 1)
#endif

#ifndef pte_present_nonuma
#define pte_present_nonuma(pte) pte_present(pte)
#endif

#ifndef flush_tlb_fix_spurious_fault
#define flush_tlb_fix_spurious_fault(vma, address) flush_tlb_page(vma, address)
#endif

#ifndef pgprot_noncached
#define pgprot_noncached(prot)	(prot)
#endif

#ifndef pgprot_writecombine
#define pgprot_writecombine pgprot_noncached
#endif

#ifndef pgprot_device
#define pgprot_device pgprot_noncached
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

static inline pte_t __ptep_modify_prot_start(struct mm_struct *mm,
					     unsigned long addr,
					     pte_t *ptep)
{
	/*
	 * Get the current pte state, but zero it out to make it
	 * non-present, preventing the hardware from asynchronously
	 * updating it.
	 */
	return ptep_get_and_clear(mm, addr, ptep);
}

static inline void __ptep_modify_prot_commit(struct mm_struct *mm,
					     unsigned long addr,
					     pte_t *ptep, pte_t pte)
{
	/*
	 * The pte is non-present, so there's no hardware state to
	 * preserve.
	 */
	set_pte_at(mm, addr, ptep, pte);
}

#ifndef __HAVE_ARCH_PTEP_MODIFY_PROT_TRANSACTION
/*
 * Start a pte protection read-modify-write transaction, which
 * protects against asynchronous hardware modifications to the pte.
 * The intention is not to prevent the hardware from making pte
 * updates, but to prevent any updates it may make from being lost.
 *
 * This does not protect against other software modifications of the
 * pte; the appropriate pte lock must be held over the transation.
 *
 * Note that this interface is intended to be batchable, meaning that
 * ptep_modify_prot_commit may not actually update the pte, but merely
 * queue the update to be done at some later time.  The update must be
 * actually committed before the pte lock is released, however.
 */
static inline pte_t ptep_modify_prot_start(struct mm_struct *mm,
					   unsigned long addr,
					   pte_t *ptep)
{
	return __ptep_modify_prot_start(mm, addr, ptep);
}

/*
 * Commit an update to a pte, leaving any hardware-controlled bits in
 * the PTE unmodified.
 */
static inline void ptep_modify_prot_commit(struct mm_struct *mm,
					   unsigned long addr,
					   pte_t *ptep, pte_t pte)
{
	__ptep_modify_prot_commit(mm, addr, ptep, pte);
}
#endif /* __HAVE_ARCH_PTEP_MODIFY_PROT_TRANSACTION */
#endif /* CONFIG_MMU */

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
 * A facility to provide batching of the reload of page tables and
 * other process state with the actual context switch code for
 * paravirtualized guests.  By convention, only one of the batched
 * update (lazy) modes (CPU, MMU) should be active at any given time,
 * entry should never be nested, and entry and exits should always be
 * paired.  This is for sanity of maintaining and reasoning about the
 * kernel code.  In this case, the exit (end of the context switch) is
 * in architecture-specific code, and so doesn't need a generic
 * definition.
 */
#ifndef __HAVE_ARCH_START_CONTEXT_SWITCH
#define arch_start_context_switch(prev)	do {} while (0)
#endif

#ifndef CONFIG_HAVE_ARCH_SOFT_DIRTY
static inline int pte_soft_dirty(pte_t pte)
{
	return 0;
}

static inline int pmd_soft_dirty(pmd_t pmd)
{
	return 0;
}

static inline pte_t pte_mksoft_dirty(pte_t pte)
{
	return pte;
}

static inline pmd_t pmd_mksoft_dirty(pmd_t pmd)
{
	return pmd;
}

static inline pte_t pte_swp_mksoft_dirty(pte_t pte)
{
	return pte;
}

static inline int pte_swp_soft_dirty(pte_t pte)
{
	return 0;
}

static inline pte_t pte_swp_clear_soft_dirty(pte_t pte)
{
	return pte;
}

static inline pte_t pte_file_clear_soft_dirty(pte_t pte)
{
       return pte;
}

static inline pte_t pte_file_mksoft_dirty(pte_t pte)
{
       return pte;
}

static inline int pte_file_soft_dirty(pte_t pte)
{
       return 0;
}
#endif

#ifndef __HAVE_PFNMAP_TRACKING
/*
 * Interfaces that can be used by architecture code to keep track of
 * memory type of pfn mappings specified by the remap_pfn_range,
 * vm_insert_pfn.
 */

/*
 * track_pfn_remap is called when a _new_ pfn mapping is being established
 * by remap_pfn_range() for physical range indicated by pfn and size.
 */
static inline int track_pfn_remap(struct vm_area_struct *vma, pgprot_t *prot,
				  unsigned long pfn, unsigned long addr,
				  unsigned long size)
{
	return 0;
}

/*
 * track_pfn_insert is called when a _new_ single pfn is established
 * by vm_insert_pfn().
 */
static inline int track_pfn_insert(struct vm_area_struct *vma, pgprot_t *prot,
				   unsigned long pfn)
{
	return 0;
}

/*
 * track_pfn_copy is called when vma that is covering the pfnmap gets
 * copied through copy_page_range().
 */
static inline int track_pfn_copy(struct vm_area_struct *vma)
{
	return 0;
}

/*
 * untrack_pfn_vma is called while unmapping a pfnmap for a region.
 * untrack can be called for a specific region indicated by pfn and size or
 * can be for the entire vma (in which case pfn, size are zero).
 */
static inline void untrack_pfn(struct vm_area_struct *vma,
			       unsigned long pfn, unsigned long size)
{
}
#else
extern int track_pfn_remap(struct vm_area_struct *vma, pgprot_t *prot,
			   unsigned long pfn, unsigned long addr,
			   unsigned long size);
extern int track_pfn_insert(struct vm_area_struct *vma, pgprot_t *prot,
			    unsigned long pfn);
extern int track_pfn_copy(struct vm_area_struct *vma);
extern void untrack_pfn(struct vm_area_struct *vma, unsigned long pfn,
			unsigned long size);
#endif

#ifdef __HAVE_COLOR_ZERO_PAGE
static inline int is_zero_pfn(unsigned long pfn)
{
	extern unsigned long zero_pfn;
	unsigned long offset_from_zero_pfn = pfn - zero_pfn;
	return offset_from_zero_pfn <= (zero_page_mask >> PAGE_SHIFT);
}

#define my_zero_pfn(addr)	page_to_pfn(ZERO_PAGE(addr))

#else
static inline int is_zero_pfn(unsigned long pfn)
{
	extern unsigned long zero_pfn;
	return pfn == zero_pfn;
}

static inline unsigned long my_zero_pfn(unsigned long addr)
{
	extern unsigned long zero_pfn;
	return zero_pfn;
}
#endif

#ifdef CONFIG_MMU

#ifndef CONFIG_TRANSPARENT_HUGEPAGE
static inline int pmd_trans_huge(pmd_t pmd)
{
	return 0;
}
static inline int pmd_trans_splitting(pmd_t pmd)
{
	return 0;
}
#ifndef __HAVE_ARCH_PMD_WRITE
static inline int pmd_write(pmd_t pmd)
{
	BUG();
	return 0;
}
#endif /* __HAVE_ARCH_PMD_WRITE */
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#ifndef pmd_read_atomic
static inline pmd_t pmd_read_atomic(pmd_t *pmdp)
{
	/*
	 * Depend on compiler for an atomic pmd read. NOTE: this is
	 * only going to work, if the pmdval_t isn't larger than
	 * an unsigned long.
	 */
	return *pmdp;
}
#endif

#ifndef pmd_move_must_withdraw
static inline int pmd_move_must_withdraw(spinlock_t *new_pmd_ptl,
					 spinlock_t *old_pmd_ptl)
{
	/*
	 * With split pmd lock we also need to move preallocated
	 * PTE page table if new_pmd is on different PMD page table.
	 */
	return new_pmd_ptl != old_pmd_ptl;
}
#endif

/*
 * This function is meant to be used by sites walking pagetables with
 * the mmap_sem hold in read mode to protect against MADV_DONTNEED and
 * transhuge page faults. MADV_DONTNEED can convert a transhuge pmd
 * into a null pmd and the transhuge page fault can convert a null pmd
 * into an hugepmd or into a regular pmd (if the hugepage allocation
 * fails). While holding the mmap_sem in read mode the pmd becomes
 * stable and stops changing under us only if it's not null and not a
 * transhuge pmd. When those races occurs and this function makes a
 * difference vs the standard pmd_none_or_clear_bad, the result is
 * undefined so behaving like if the pmd was none is safe (because it
 * can return none anyway). The compiler level barrier() is critically
 * important to compute the two checks atomically on the same pmdval.
 *
 * For 32bit kernels with a 64bit large pmd_t this automatically takes
 * care of reading the pmd atomically to avoid SMP race conditions
 * against pmd_populate() when the mmap_sem is hold for reading by the
 * caller (a special atomic read not done by "gcc" as in the generic
 * version above, is also needed when THP is disabled because the page
 * fault can populate the pmd from under us).
 */
static inline int pmd_none_or_trans_huge_or_clear_bad(pmd_t *pmd)
{
	pmd_t pmdval = pmd_read_atomic(pmd);
	/*
	 * The barrier will stabilize the pmdval in a register or on
	 * the stack so that it will stop changing under the code.
	 *
	 * When CONFIG_TRANSPARENT_HUGEPAGE=y on x86 32bit PAE,
	 * pmd_read_atomic is allowed to return a not atomic pmdval
	 * (for example pointing to an hugepage that has never been
	 * mapped in the pmd). The below checks will only care about
	 * the low part of the pmd with 32bit PAE x86 anyway, with the
	 * exception of pmd_none(). So the important thing is that if
	 * the low part of the pmd is found null, the high part will
	 * be also null or the pmd_none() check below would be
	 * confused.
	 */
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	barrier();
#endif
	if (pmd_none(pmdval) || pmd_trans_huge(pmdval))
		return 1;
	if (unlikely(pmd_bad(pmdval))) {
		pmd_clear_bad(pmd);
		return 1;
	}
	return 0;
}

/*
 * This is a noop if Transparent Hugepage Support is not built into
 * the kernel. Otherwise it is equivalent to
 * pmd_none_or_trans_huge_or_clear_bad(), and shall only be called in
 * places that already verified the pmd is not none and they want to
 * walk ptes while holding the mmap sem in read mode (write mode don't
 * need this). If THP is not enabled, the pmd can't go away under the
 * code even if MADV_DONTNEED runs, but if THP is enabled we need to
 * run a pmd_trans_unstable before walking the ptes after
 * split_huge_page_pmd returns (because it may have run when the pmd
 * become null, but then a page fault can map in a THP and not a
 * regular page).
 */
static inline int pmd_trans_unstable(pmd_t *pmd)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	return pmd_none_or_trans_huge_or_clear_bad(pmd);
#else
	return 0;
#endif
}

#ifdef CONFIG_NUMA_BALANCING
/*
 * _PAGE_NUMA distinguishes between an unmapped page table entry, an entry that
 * is protected for PROT_NONE and a NUMA hinting fault entry. If the
 * architecture defines __PAGE_PROTNONE then it should take that into account
 * but those that do not can rely on the fact that the NUMA hinting scanner
 * skips inaccessible VMAs.
 *
 * pte/pmd_present() returns true if pte/pmd_numa returns true. Page
 * fault triggers on those regions if pte/pmd_numa returns true
 * (because _PAGE_PRESENT is not set).
 */
#ifndef pte_numa
static inline int pte_numa(pte_t pte)
{
	return ptenuma_flags(pte) == _PAGE_NUMA;
}
#endif

#ifndef pmd_numa
static inline int pmd_numa(pmd_t pmd)
{
	return pmdnuma_flags(pmd) == _PAGE_NUMA;
}
#endif

/*
 * pte/pmd_mknuma sets the _PAGE_ACCESSED bitflag automatically
 * because they're called by the NUMA hinting minor page fault. If we
 * wouldn't set the _PAGE_ACCESSED bitflag here, the TLB miss handler
 * would be forced to set it later while filling the TLB after we
 * return to userland. That would trigger a second write to memory
 * that we optimize away by setting _PAGE_ACCESSED here.
 */
#ifndef pte_mknonnuma
static inline pte_t pte_mknonnuma(pte_t pte)
{
	pteval_t val = pte_val(pte);

	val &= ~_PAGE_NUMA;
	val |= (_PAGE_PRESENT|_PAGE_ACCESSED);
	return __pte(val);
}
#endif

#ifndef pmd_mknonnuma
static inline pmd_t pmd_mknonnuma(pmd_t pmd)
{
	pmdval_t val = pmd_val(pmd);

	val &= ~_PAGE_NUMA;
	val |= (_PAGE_PRESENT|_PAGE_ACCESSED);

	return __pmd(val);
}
#endif

#ifndef pte_mknuma
static inline pte_t pte_mknuma(pte_t pte)
{
	pteval_t val = pte_val(pte);

	VM_BUG_ON(!(val & _PAGE_PRESENT));

	val &= ~_PAGE_PRESENT;
	val |= _PAGE_NUMA;

	return __pte(val);
}
#endif

#ifndef ptep_set_numa
static inline void ptep_set_numa(struct mm_struct *mm, unsigned long addr,
				 pte_t *ptep)
{
	pte_t ptent = *ptep;

	ptent = pte_mknuma(ptent);
	set_pte_at(mm, addr, ptep, ptent);
	return;
}
#endif

#ifndef pmd_mknuma
static inline pmd_t pmd_mknuma(pmd_t pmd)
{
	pmdval_t val = pmd_val(pmd);

	val &= ~_PAGE_PRESENT;
	val |= _PAGE_NUMA;

	return __pmd(val);
}
#endif

#ifndef pmdp_set_numa
static inline void pmdp_set_numa(struct mm_struct *mm, unsigned long addr,
				 pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;

	pmd = pmd_mknuma(pmd);
	set_pmd_at(mm, addr, pmdp, pmd);
	return;
}
#endif
#else
static inline int pmd_numa(pmd_t pmd)
{
	return 0;
}

static inline int pte_numa(pte_t pte)
{
	return 0;
}

static inline pte_t pte_mknonnuma(pte_t pte)
{
	return pte;
}

static inline pmd_t pmd_mknonnuma(pmd_t pmd)
{
	return pmd;
}

static inline pte_t pte_mknuma(pte_t pte)
{
	return pte;
}

static inline void ptep_set_numa(struct mm_struct *mm, unsigned long addr,
				 pte_t *ptep)
{
	return;
}


static inline pmd_t pmd_mknuma(pmd_t pmd)
{
	return pmd;
}

static inline void pmdp_set_numa(struct mm_struct *mm, unsigned long addr,
				 pmd_t *pmdp)
{
	return ;
}
#endif /* CONFIG_NUMA_BALANCING */

#endif /* CONFIG_MMU */

#endif /* !__ASSEMBLY__ */

#ifndef io_remap_pfn_range
#define io_remap_pfn_range remap_pfn_range
#endif

#endif /* _ASM_GENERIC_PGTABLE_H */
