// SPDX-License-Identifier: GPL-2.0
/*
 *  mm/pgtable-generic.c
 *
 *  Generic pgtable methods declared in linux/pgtable.h
 *
 *  Copyright (C) 2010  Linus Torvalds
 */

#include <linux/pagemap.h>
#include <linux/hugetlb.h>
#include <linux/pgtable.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mm_inline.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

/*
 * If a p?d_bad entry is found while walking page tables, report
 * the error, before resetting entry to p?d_none.  Usually (but
 * very seldom) called out from the p?d_none_or_clear_bad macros.
 */

void pgd_clear_bad(pgd_t *pgd)
{
	pgd_ERROR(*pgd);
	pgd_clear(pgd);
}

#ifndef __PAGETABLE_P4D_FOLDED
void p4d_clear_bad(p4d_t *p4d)
{
	p4d_ERROR(*p4d);
	p4d_clear(p4d);
}
#endif

#ifndef __PAGETABLE_PUD_FOLDED
void pud_clear_bad(pud_t *pud)
{
	pud_ERROR(*pud);
	pud_clear(pud);
}
#endif

/*
 * Note that the pmd variant below can't be stub'ed out just as for p4d/pud
 * above. pmd folding is special and typically pmd_* macros refer to upper
 * level even when folded
 */
void pmd_clear_bad(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
}

#ifndef __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
/*
 * Only sets the access flags (dirty, accessed), as well as write
 * permission. Furthermore, we know it always gets set to a "more
 * permissive" setting, which allows most architectures to optimize
 * this. We return whether the PTE actually changed, which in turn
 * instructs the caller to do things like update__mmu_cache.  This
 * used to be done in the caller, but sparc needs minor faults to
 * force that call on sun4c so we changed this macro slightly
 */
int ptep_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pte_t *ptep,
			  pte_t entry, int dirty)
{
	int changed = !pte_same(ptep_get(ptep), entry);
	if (changed) {
		set_pte_at(vma->vm_mm, address, ptep, entry);
		flush_tlb_fix_spurious_fault(vma, address, ptep);
	}
	return changed;
}
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
int ptep_clear_flush_young(struct vm_area_struct *vma,
			   unsigned long address, pte_t *ptep)
{
	int young;
	young = ptep_test_and_clear_young(vma, address, ptep);
	if (young)
		flush_tlb_page(vma, address);
	return young;
}
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_FLUSH
pte_t ptep_clear_flush(struct vm_area_struct *vma, unsigned long address,
		       pte_t *ptep)
{
	struct mm_struct *mm = (vma)->vm_mm;
	pte_t pte;
	pte = ptep_get_and_clear(mm, address, ptep);
	if (pte_accessible(mm, pte))
		flush_tlb_page(vma, address);
	return pte;
}
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

#ifndef __HAVE_ARCH_PMDP_SET_ACCESS_FLAGS
int pmdp_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pmd_t *pmdp,
			  pmd_t entry, int dirty)
{
	int changed = !pmd_same(*pmdp, entry);
	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	if (changed) {
		set_pmd_at(vma->vm_mm, address, pmdp, entry);
		flush_pmd_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
	}
	return changed;
}
#endif

#ifndef __HAVE_ARCH_PMDP_CLEAR_YOUNG_FLUSH
int pmdp_clear_flush_young(struct vm_area_struct *vma,
			   unsigned long address, pmd_t *pmdp)
{
	int young;
	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	young = pmdp_test_and_clear_young(vma, address, pmdp);
	if (young)
		flush_pmd_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
	return young;
}
#endif

#ifndef __HAVE_ARCH_PMDP_HUGE_CLEAR_FLUSH
pmd_t pmdp_huge_clear_flush(struct vm_area_struct *vma, unsigned long address,
			    pmd_t *pmdp)
{
	pmd_t pmd;
	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	VM_BUG_ON(pmd_present(*pmdp) && !pmd_trans_huge(*pmdp) &&
			   !pmd_devmap(*pmdp));
	pmd = pmdp_huge_get_and_clear(vma->vm_mm, address, pmdp);
	flush_pmd_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
	return pmd;
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
pud_t pudp_huge_clear_flush(struct vm_area_struct *vma, unsigned long address,
			    pud_t *pudp)
{
	pud_t pud;

	VM_BUG_ON(address & ~HPAGE_PUD_MASK);
	VM_BUG_ON(!pud_trans_huge(*pudp) && !pud_devmap(*pudp));
	pud = pudp_huge_get_and_clear(vma->vm_mm, address, pudp);
	flush_pud_tlb_range(vma, address, address + HPAGE_PUD_SIZE);
	return pud;
}
#endif
#endif

#ifndef __HAVE_ARCH_PGTABLE_DEPOSIT
void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				pgtable_t pgtable)
{
	assert_spin_locked(pmd_lockptr(mm, pmdp));

	/* FIFO */
	if (!pmd_huge_pte(mm, pmdp))
		INIT_LIST_HEAD(&pgtable->lru);
	else
		list_add(&pgtable->lru, &pmd_huge_pte(mm, pmdp)->lru);
	pmd_huge_pte(mm, pmdp) = pgtable;
}
#endif

#ifndef __HAVE_ARCH_PGTABLE_WITHDRAW
/* no "address" argument so destroys page coloring of some arch */
pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp)
{
	pgtable_t pgtable;

	assert_spin_locked(pmd_lockptr(mm, pmdp));

	/* FIFO */
	pgtable = pmd_huge_pte(mm, pmdp);
	pmd_huge_pte(mm, pmdp) = list_first_entry_or_null(&pgtable->lru,
							  struct page, lru);
	if (pmd_huge_pte(mm, pmdp))
		list_del(&pgtable->lru);
	return pgtable;
}
#endif

#ifndef __HAVE_ARCH_PMDP_INVALIDATE
pmd_t pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
		     pmd_t *pmdp)
{
	VM_WARN_ON_ONCE(!pmd_present(*pmdp));
	pmd_t old = pmdp_establish(vma, address, pmdp, pmd_mkinvalid(*pmdp));
	flush_pmd_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
	return old;
}
#endif

#ifndef __HAVE_ARCH_PMDP_INVALIDATE_AD
pmd_t pmdp_invalidate_ad(struct vm_area_struct *vma, unsigned long address,
			 pmd_t *pmdp)
{
	VM_WARN_ON_ONCE(!pmd_present(*pmdp));
	return pmdp_invalidate(vma, address, pmdp);
}
#endif

#ifndef pmdp_collapse_flush
pmd_t pmdp_collapse_flush(struct vm_area_struct *vma, unsigned long address,
			  pmd_t *pmdp)
{
	/*
	 * pmd and hugepage pte format are same. So we could
	 * use the same function.
	 */
	pmd_t pmd;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	VM_BUG_ON(pmd_trans_huge(*pmdp));
	pmd = pmdp_huge_get_and_clear(vma->vm_mm, address, pmdp);

	/* collapse entails shooting down ptes not pmd */
	flush_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
	return pmd;
}
#endif

/* arch define pte_free_defer in asm/pgalloc.h for its own implementation */
#ifndef pte_free_defer
static void pte_free_now(struct rcu_head *head)
{
	struct page *page;

	page = container_of(head, struct page, rcu_head);
	pte_free(NULL /* mm not passed and not used */, (pgtable_t)page);
}

void pte_free_defer(struct mm_struct *mm, pgtable_t pgtable)
{
	struct page *page;

	page = pgtable;
	call_rcu(&page->rcu_head, pte_free_now);
}
#endif /* pte_free_defer */
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#if defined(CONFIG_GUP_GET_PXX_LOW_HIGH) && \
	(defined(CONFIG_SMP) || defined(CONFIG_PREEMPT_RCU))
/*
 * See the comment above ptep_get_lockless() in include/linux/pgtable.h:
 * the barriers in pmdp_get_lockless() cannot guarantee that the value in
 * pmd_high actually belongs with the value in pmd_low; but holding interrupts
 * off blocks the TLB flush between present updates, which guarantees that a
 * successful __pte_offset_map() points to a page from matched halves.
 */
static unsigned long pmdp_get_lockless_start(void)
{
	unsigned long irqflags;

	local_irq_save(irqflags);
	return irqflags;
}
static void pmdp_get_lockless_end(unsigned long irqflags)
{
	local_irq_restore(irqflags);
}
#else
static unsigned long pmdp_get_lockless_start(void) { return 0; }
static void pmdp_get_lockless_end(unsigned long irqflags) { }
#endif

pte_t *___pte_offset_map(pmd_t *pmd, unsigned long addr, pmd_t *pmdvalp)
{
	unsigned long irqflags;
	pmd_t pmdval;

	rcu_read_lock();
	irqflags = pmdp_get_lockless_start();
	pmdval = pmdp_get_lockless(pmd);
	pmdp_get_lockless_end(irqflags);

	if (pmdvalp)
		*pmdvalp = pmdval;
	if (unlikely(pmd_none(pmdval) || is_pmd_migration_entry(pmdval)))
		goto nomap;
	if (unlikely(pmd_trans_huge(pmdval) || pmd_devmap(pmdval)))
		goto nomap;
	if (unlikely(pmd_bad(pmdval))) {
		pmd_clear_bad(pmd);
		goto nomap;
	}
	return __pte_map(&pmdval, addr);
nomap:
	rcu_read_unlock();
	return NULL;
}

pte_t *pte_offset_map_ro_nolock(struct mm_struct *mm, pmd_t *pmd,
				unsigned long addr, spinlock_t **ptlp)
{
	pmd_t pmdval;
	pte_t *pte;

	pte = __pte_offset_map(pmd, addr, &pmdval);
	if (likely(pte))
		*ptlp = pte_lockptr(mm, &pmdval);
	return pte;
}

pte_t *pte_offset_map_rw_nolock(struct mm_struct *mm, pmd_t *pmd,
				unsigned long addr, pmd_t *pmdvalp,
				spinlock_t **ptlp)
{
	pte_t *pte;

	VM_WARN_ON_ONCE(!pmdvalp);
	pte = __pte_offset_map(pmd, addr, pmdvalp);
	if (likely(pte))
		*ptlp = pte_lockptr(mm, pmdvalp);
	return pte;
}

/*
 * pte_offset_map_lock(mm, pmd, addr, ptlp), and its internal implementation
 * __pte_offset_map_lock() below, is usually called with the pmd pointer for
 * addr, reached by walking down the mm's pgd, p4d, pud for addr: either while
 * holding mmap_lock or vma lock for read or for write; or in truncate or rmap
 * context, while holding file's i_mmap_lock or anon_vma lock for read (or for
 * write). In a few cases, it may be used with pmd pointing to a pmd_t already
 * copied to or constructed on the stack.
 *
 * When successful, it returns the pte pointer for addr, with its page table
 * kmapped if necessary (when CONFIG_HIGHPTE), and locked against concurrent
 * modification by software, with a pointer to that spinlock in ptlp (in some
 * configs mm->page_table_lock, in SPLIT_PTLOCK configs a spinlock in table's
 * struct page).  pte_unmap_unlock(pte, ptl) to unlock and unmap afterwards.
 *
 * But it is unsuccessful, returning NULL with *ptlp unchanged, if there is no
 * page table at *pmd: if, for example, the page table has just been removed,
 * or replaced by the huge pmd of a THP.  (When successful, *pmd is rechecked
 * after acquiring the ptlock, and retried internally if it changed: so that a
 * page table can be safely removed or replaced by THP while holding its lock.)
 *
 * pte_offset_map(pmd, addr), and its internal helper __pte_offset_map() above,
 * just returns the pte pointer for addr, its page table kmapped if necessary;
 * or NULL if there is no page table at *pmd.  It does not attempt to lock the
 * page table, so cannot normally be used when the page table is to be updated,
 * or when entries read must be stable.  But it does take rcu_read_lock(): so
 * that even when page table is racily removed, it remains a valid though empty
 * and disconnected table.  Until pte_unmap(pte) unmaps and rcu_read_unlock()s
 * afterwards.
 *
 * pte_offset_map_ro_nolock(mm, pmd, addr, ptlp), above, is like pte_offset_map();
 * but when successful, it also outputs a pointer to the spinlock in ptlp - as
 * pte_offset_map_lock() does, but in this case without locking it.  This helps
 * the caller to avoid a later pte_lockptr(mm, *pmd), which might by that time
 * act on a changed *pmd: pte_offset_map_ro_nolock() provides the correct spinlock
 * pointer for the page table that it returns. Even after grabbing the spinlock,
 * we might be looking either at a page table that is still mapped or one that
 * was unmapped and is about to get freed. But for R/O access this is sufficient.
 * So it is only applicable for read-only cases where any modification operations
 * to the page table are not allowed even if the corresponding spinlock is held
 * afterwards.
 *
 * pte_offset_map_rw_nolock(mm, pmd, addr, pmdvalp, ptlp), above, is like
 * pte_offset_map_ro_nolock(); but when successful, it also outputs the pdmval.
 * It is applicable for may-write cases where any modification operations to the
 * page table may happen after the corresponding spinlock is held afterwards.
 * But the users should make sure the page table is stable like checking pte_same()
 * or checking pmd_same() by using the output pmdval before performing the write
 * operations.
 *
 * Note: "RO" / "RW" expresses the intended semantics, not that the *kmap* will
 * be read-only/read-write protected.
 *
 * Note that free_pgtables(), used after unmapping detached vmas, or when
 * exiting the whole mm, does not take page table lock before freeing a page
 * table, and may not use RCU at all: "outsiders" like khugepaged should avoid
 * pte_offset_map() and co once the vma is detached from mm or mm_users is zero.
 */
pte_t *__pte_offset_map_lock(struct mm_struct *mm, pmd_t *pmd,
			     unsigned long addr, spinlock_t **ptlp)
{
	spinlock_t *ptl;
	pmd_t pmdval;
	pte_t *pte;
again:
	pte = __pte_offset_map(pmd, addr, &pmdval);
	if (unlikely(!pte))
		return pte;
	ptl = pte_lockptr(mm, &pmdval);
	spin_lock(ptl);
	if (likely(pmd_same(pmdval, pmdp_get_lockless(pmd)))) {
		*ptlp = ptl;
		return pte;
	}
	pte_unmap_unlock(pte, ptl);
	goto again;
}
