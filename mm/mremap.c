// SPDX-License-Identifier: GPL-2.0
/*
 *	mm/mremap.c
 *
 *	(C) Copyright 1996 Linus Torvalds
 *
 *	Address space accounting code	<alan@lxorguk.ukuu.org.uk>
 *	(C) Copyright 2002 Red Hat Inc, All Rights Reserved
 */

#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/hugetlb.h>
#include <linux/shm.h>
#include <linux/ksm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/swapops.h>
#include <linux/highmem.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/mmu_notifier.h>
#include <linux/uaccess.h>
#include <linux/userfaultfd_k.h>
#include <linux/mempolicy.h>

#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/pgalloc.h>

#include "internal.h"

/* Classify the kind of remap operation being performed. */
enum mremap_type {
	MREMAP_INVALID,		/* Initial state. */
	MREMAP_NO_RESIZE,	/* old_len == new_len, if not moved, do nothing. */
	MREMAP_SHRINK,		/* old_len > new_len. */
	MREMAP_EXPAND,		/* old_len < new_len. */
};

/*
 * Describes a VMA mremap() operation and is threaded throughout it.
 *
 * Any of the fields may be mutated by the operation, however these values will
 * always accurately reflect the remap (for instance, we may adjust lengths and
 * delta to account for hugetlb alignment).
 */
struct vma_remap_struct {
	/* User-provided state. */
	unsigned long addr;	/* User-specified address from which we remap. */
	unsigned long old_len;	/* Length of range being remapped. */
	unsigned long new_len;	/* Desired new length of mapping. */
	const unsigned long flags; /* user-specified MREMAP_* flags. */
	unsigned long new_addr;	/* Optionally, desired new address. */

	/* uffd state. */
	struct vm_userfaultfd_ctx *uf;
	struct list_head *uf_unmap_early;
	struct list_head *uf_unmap;

	/* VMA state, determined in do_mremap(). */
	struct vm_area_struct *vma;

	/* Internal state, determined in do_mremap(). */
	unsigned long delta;		/* Absolute delta of old_len,new_len. */
	bool mlocked;			/* Was the VMA mlock()'d? */
	enum mremap_type remap_type;	/* expand, shrink, etc. */
	bool mmap_locked;		/* Is mm currently write-locked? */
	unsigned long charged;		/* If VM_ACCOUNT, # pages to account. */
};

static pud_t *get_old_pud(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;

	pgd = pgd_offset(mm, addr);
	if (pgd_none_or_clear_bad(pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (p4d_none_or_clear_bad(p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (pud_none_or_clear_bad(pud))
		return NULL;

	return pud;
}

static pmd_t *get_old_pmd(struct mm_struct *mm, unsigned long addr)
{
	pud_t *pud;
	pmd_t *pmd;

	pud = get_old_pud(mm, addr);
	if (!pud)
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return NULL;

	return pmd;
}

static pud_t *alloc_new_pud(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;

	return pud_alloc(mm, p4d, addr);
}

static pmd_t *alloc_new_pmd(struct mm_struct *mm, unsigned long addr)
{
	pud_t *pud;
	pmd_t *pmd;

	pud = alloc_new_pud(mm, addr);
	if (!pud)
		return NULL;

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;

	VM_BUG_ON(pmd_trans_huge(*pmd));

	return pmd;
}

static void take_rmap_locks(struct vm_area_struct *vma)
{
	if (vma->vm_file)
		i_mmap_lock_write(vma->vm_file->f_mapping);
	if (vma->anon_vma)
		anon_vma_lock_write(vma->anon_vma);
}

static void drop_rmap_locks(struct vm_area_struct *vma)
{
	if (vma->anon_vma)
		anon_vma_unlock_write(vma->anon_vma);
	if (vma->vm_file)
		i_mmap_unlock_write(vma->vm_file->f_mapping);
}

static pte_t move_soft_dirty_pte(pte_t pte)
{
	/*
	 * Set soft dirty bit so we can notice
	 * in userspace the ptes were moved.
	 */
#ifdef CONFIG_MEM_SOFT_DIRTY
	if (pte_present(pte))
		pte = pte_mksoft_dirty(pte);
	else if (is_swap_pte(pte))
		pte = pte_swp_mksoft_dirty(pte);
#endif
	return pte;
}

static int mremap_folio_pte_batch(struct vm_area_struct *vma, unsigned long addr,
		pte_t *ptep, pte_t pte, int max_nr)
{
	struct folio *folio;

	if (max_nr == 1)
		return 1;

	folio = vm_normal_folio(vma, addr, pte);
	if (!folio || !folio_test_large(folio))
		return 1;

	return folio_pte_batch(folio, ptep, pte, max_nr);
}

static int move_ptes(struct pagetable_move_control *pmc,
		unsigned long extent, pmd_t *old_pmd, pmd_t *new_pmd)
{
	struct vm_area_struct *vma = pmc->old;
	bool need_clear_uffd_wp = vma_has_uffd_without_event_remap(vma);
	struct mm_struct *mm = vma->vm_mm;
	pte_t *old_ptep, *new_ptep;
	pte_t old_pte, pte;
	pmd_t dummy_pmdval;
	spinlock_t *old_ptl, *new_ptl;
	bool force_flush = false;
	unsigned long old_addr = pmc->old_addr;
	unsigned long new_addr = pmc->new_addr;
	unsigned long old_end = old_addr + extent;
	unsigned long len = old_end - old_addr;
	int max_nr_ptes;
	int nr_ptes;
	int err = 0;

	/*
	 * When need_rmap_locks is true, we take the i_mmap_rwsem and anon_vma
	 * locks to ensure that rmap will always observe either the old or the
	 * new ptes. This is the easiest way to avoid races with
	 * truncate_pagecache(), page migration, etc...
	 *
	 * When need_rmap_locks is false, we use other ways to avoid
	 * such races:
	 *
	 * - During exec() shift_arg_pages(), we use a specially tagged vma
	 *   which rmap call sites look for using vma_is_temporary_stack().
	 *
	 * - During mremap(), new_vma is often known to be placed after vma
	 *   in rmap traversal order. This ensures rmap will always observe
	 *   either the old pte, or the new pte, or both (the page table locks
	 *   serialize access to individual ptes, but only rmap traversal
	 *   order guarantees that we won't miss both the old and new ptes).
	 */
	if (pmc->need_rmap_locks)
		take_rmap_locks(vma);

	/*
	 * We don't have to worry about the ordering of src and dst
	 * pte locks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptep = pte_offset_map_lock(mm, old_pmd, old_addr, &old_ptl);
	if (!old_ptep) {
		err = -EAGAIN;
		goto out;
	}
	/*
	 * Now new_pte is none, so hpage_collapse_scan_file() path can not find
	 * this by traversing file->f_mapping, so there is no concurrency with
	 * retract_page_tables(). In addition, we already hold the exclusive
	 * mmap_lock, so this new_pte page is stable, so there is no need to get
	 * pmdval and do pmd_same() check.
	 */
	new_ptep = pte_offset_map_rw_nolock(mm, new_pmd, new_addr, &dummy_pmdval,
					   &new_ptl);
	if (!new_ptep) {
		pte_unmap_unlock(old_ptep, old_ptl);
		err = -EAGAIN;
		goto out;
	}
	if (new_ptl != old_ptl)
		spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);
	flush_tlb_batched_pending(vma->vm_mm);
	arch_enter_lazy_mmu_mode();

	for (; old_addr < old_end; old_ptep += nr_ptes, old_addr += nr_ptes * PAGE_SIZE,
		new_ptep += nr_ptes, new_addr += nr_ptes * PAGE_SIZE) {
		VM_WARN_ON_ONCE(!pte_none(*new_ptep));

		nr_ptes = 1;
		max_nr_ptes = (old_end - old_addr) >> PAGE_SHIFT;
		old_pte = ptep_get(old_ptep);
		if (pte_none(old_pte))
			continue;

		/*
		 * If we are remapping a valid PTE, make sure
		 * to flush TLB before we drop the PTL for the
		 * PTE.
		 *
		 * NOTE! Both old and new PTL matter: the old one
		 * for racing with folio_mkclean(), the new one to
		 * make sure the physical page stays valid until
		 * the TLB entry for the old mapping has been
		 * flushed.
		 */
		if (pte_present(old_pte)) {
			nr_ptes = mremap_folio_pte_batch(vma, old_addr, old_ptep,
							 old_pte, max_nr_ptes);
			force_flush = true;
		}
		pte = get_and_clear_full_ptes(mm, old_addr, old_ptep, nr_ptes, 0);
		pte = move_pte(pte, old_addr, new_addr);
		pte = move_soft_dirty_pte(pte);

		if (need_clear_uffd_wp && pte_marker_uffd_wp(pte))
			pte_clear(mm, new_addr, new_ptep);
		else {
			if (need_clear_uffd_wp) {
				if (pte_present(pte))
					pte = pte_clear_uffd_wp(pte);
				else if (is_swap_pte(pte))
					pte = pte_swp_clear_uffd_wp(pte);
			}
			set_ptes(mm, new_addr, new_ptep, pte, nr_ptes);
		}
	}

	arch_leave_lazy_mmu_mode();
	if (force_flush)
		flush_tlb_range(vma, old_end - len, old_end);
	if (new_ptl != old_ptl)
		spin_unlock(new_ptl);
	pte_unmap(new_ptep - 1);
	pte_unmap_unlock(old_ptep - 1, old_ptl);
out:
	if (pmc->need_rmap_locks)
		drop_rmap_locks(vma);
	return err;
}

#ifndef arch_supports_page_table_move
#define arch_supports_page_table_move arch_supports_page_table_move
static inline bool arch_supports_page_table_move(void)
{
	return IS_ENABLED(CONFIG_HAVE_MOVE_PMD) ||
		IS_ENABLED(CONFIG_HAVE_MOVE_PUD);
}
#endif

#ifdef CONFIG_HAVE_MOVE_PMD
static bool move_normal_pmd(struct pagetable_move_control *pmc,
			pmd_t *old_pmd, pmd_t *new_pmd)
{
	spinlock_t *old_ptl, *new_ptl;
	struct vm_area_struct *vma = pmc->old;
	struct mm_struct *mm = vma->vm_mm;
	bool res = false;
	pmd_t pmd;

	if (!arch_supports_page_table_move())
		return false;
	/*
	 * The destination pmd shouldn't be established, free_pgtables()
	 * should have released it.
	 *
	 * However, there's a case during execve() where we use mremap
	 * to move the initial stack, and in that case the target area
	 * may overlap the source area (always moving down).
	 *
	 * If everything is PMD-aligned, that works fine, as moving
	 * each pmd down will clear the source pmd. But if we first
	 * have a few 4kB-only pages that get moved down, and then
	 * hit the "now the rest is PMD-aligned, let's do everything
	 * one pmd at a time", we will still have the old (now empty
	 * of any 4kB pages, but still there) PMD in the page table
	 * tree.
	 *
	 * Warn on it once - because we really should try to figure
	 * out how to do this better - but then say "I won't move
	 * this pmd".
	 *
	 * One alternative might be to just unmap the target pmd at
	 * this point, and verify that it really is empty. We'll see.
	 */
	if (WARN_ON_ONCE(!pmd_none(*new_pmd)))
		return false;

	/* If this pmd belongs to a uffd vma with remap events disabled, we need
	 * to ensure that the uffd-wp state is cleared from all pgtables. This
	 * means recursing into lower page tables in move_page_tables(), and we
	 * can reuse the existing code if we simply treat the entry as "not
	 * moved".
	 */
	if (vma_has_uffd_without_event_remap(vma))
		return false;

	/*
	 * We don't have to worry about the ordering of src and dst
	 * ptlocks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptl = pmd_lock(mm, old_pmd);
	new_ptl = pmd_lockptr(mm, new_pmd);
	if (new_ptl != old_ptl)
		spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);

	pmd = *old_pmd;

	/* Racing with collapse? */
	if (unlikely(!pmd_present(pmd) || pmd_leaf(pmd)))
		goto out_unlock;
	/* Clear the pmd */
	pmd_clear(old_pmd);
	res = true;

	VM_BUG_ON(!pmd_none(*new_pmd));

	pmd_populate(mm, new_pmd, pmd_pgtable(pmd));
	flush_tlb_range(vma, pmc->old_addr, pmc->old_addr + PMD_SIZE);
out_unlock:
	if (new_ptl != old_ptl)
		spin_unlock(new_ptl);
	spin_unlock(old_ptl);

	return res;
}
#else
static inline bool move_normal_pmd(struct pagetable_move_control *pmc,
		pmd_t *old_pmd, pmd_t *new_pmd)
{
	return false;
}
#endif

#if CONFIG_PGTABLE_LEVELS > 2 && defined(CONFIG_HAVE_MOVE_PUD)
static bool move_normal_pud(struct pagetable_move_control *pmc,
		pud_t *old_pud, pud_t *new_pud)
{
	spinlock_t *old_ptl, *new_ptl;
	struct vm_area_struct *vma = pmc->old;
	struct mm_struct *mm = vma->vm_mm;
	pud_t pud;

	if (!arch_supports_page_table_move())
		return false;
	/*
	 * The destination pud shouldn't be established, free_pgtables()
	 * should have released it.
	 */
	if (WARN_ON_ONCE(!pud_none(*new_pud)))
		return false;

	/* If this pud belongs to a uffd vma with remap events disabled, we need
	 * to ensure that the uffd-wp state is cleared from all pgtables. This
	 * means recursing into lower page tables in move_page_tables(), and we
	 * can reuse the existing code if we simply treat the entry as "not
	 * moved".
	 */
	if (vma_has_uffd_without_event_remap(vma))
		return false;

	/*
	 * We don't have to worry about the ordering of src and dst
	 * ptlocks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptl = pud_lock(mm, old_pud);
	new_ptl = pud_lockptr(mm, new_pud);
	if (new_ptl != old_ptl)
		spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);

	/* Clear the pud */
	pud = *old_pud;
	pud_clear(old_pud);

	VM_BUG_ON(!pud_none(*new_pud));

	pud_populate(mm, new_pud, pud_pgtable(pud));
	flush_tlb_range(vma, pmc->old_addr, pmc->old_addr + PUD_SIZE);
	if (new_ptl != old_ptl)
		spin_unlock(new_ptl);
	spin_unlock(old_ptl);

	return true;
}
#else
static inline bool move_normal_pud(struct pagetable_move_control *pmc,
		pud_t *old_pud, pud_t *new_pud)
{
	return false;
}
#endif

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
static bool move_huge_pud(struct pagetable_move_control *pmc,
		pud_t *old_pud, pud_t *new_pud)
{
	spinlock_t *old_ptl, *new_ptl;
	struct vm_area_struct *vma = pmc->old;
	struct mm_struct *mm = vma->vm_mm;
	pud_t pud;

	/*
	 * The destination pud shouldn't be established, free_pgtables()
	 * should have released it.
	 */
	if (WARN_ON_ONCE(!pud_none(*new_pud)))
		return false;

	/*
	 * We don't have to worry about the ordering of src and dst
	 * ptlocks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptl = pud_lock(mm, old_pud);
	new_ptl = pud_lockptr(mm, new_pud);
	if (new_ptl != old_ptl)
		spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);

	/* Clear the pud */
	pud = *old_pud;
	pud_clear(old_pud);

	VM_BUG_ON(!pud_none(*new_pud));

	/* Set the new pud */
	/* mark soft_ditry when we add pud level soft dirty support */
	set_pud_at(mm, pmc->new_addr, new_pud, pud);
	flush_pud_tlb_range(vma, pmc->old_addr, pmc->old_addr + HPAGE_PUD_SIZE);
	if (new_ptl != old_ptl)
		spin_unlock(new_ptl);
	spin_unlock(old_ptl);

	return true;
}
#else
static bool move_huge_pud(struct pagetable_move_control *pmc,
		pud_t *old_pud, pud_t *new_pud)

{
	WARN_ON_ONCE(1);
	return false;

}
#endif

enum pgt_entry {
	NORMAL_PMD,
	HPAGE_PMD,
	NORMAL_PUD,
	HPAGE_PUD,
};

/*
 * Returns an extent of the corresponding size for the pgt_entry specified if
 * valid. Else returns a smaller extent bounded by the end of the source and
 * destination pgt_entry.
 */
static __always_inline unsigned long get_extent(enum pgt_entry entry,
						struct pagetable_move_control *pmc)
{
	unsigned long next, extent, mask, size;
	unsigned long old_addr = pmc->old_addr;
	unsigned long old_end = pmc->old_end;
	unsigned long new_addr = pmc->new_addr;

	switch (entry) {
	case HPAGE_PMD:
	case NORMAL_PMD:
		mask = PMD_MASK;
		size = PMD_SIZE;
		break;
	case HPAGE_PUD:
	case NORMAL_PUD:
		mask = PUD_MASK;
		size = PUD_SIZE;
		break;
	default:
		BUILD_BUG();
		break;
	}

	next = (old_addr + size) & mask;
	/* even if next overflowed, extent below will be ok */
	extent = next - old_addr;
	if (extent > old_end - old_addr)
		extent = old_end - old_addr;
	next = (new_addr + size) & mask;
	if (extent > next - new_addr)
		extent = next - new_addr;
	return extent;
}

/*
 * Should move_pgt_entry() acquire the rmap locks? This is either expressed in
 * the PMC, or overridden in the case of normal, larger page tables.
 */
static bool should_take_rmap_locks(struct pagetable_move_control *pmc,
				   enum pgt_entry entry)
{
	switch (entry) {
	case NORMAL_PMD:
	case NORMAL_PUD:
		return true;
	default:
		return pmc->need_rmap_locks;
	}
}

/*
 * Attempts to speedup the move by moving entry at the level corresponding to
 * pgt_entry. Returns true if the move was successful, else false.
 */
static bool move_pgt_entry(struct pagetable_move_control *pmc,
			   enum pgt_entry entry, void *old_entry, void *new_entry)
{
	bool moved = false;
	bool need_rmap_locks = should_take_rmap_locks(pmc, entry);

	/* See comment in move_ptes() */
	if (need_rmap_locks)
		take_rmap_locks(pmc->old);

	switch (entry) {
	case NORMAL_PMD:
		moved = move_normal_pmd(pmc, old_entry, new_entry);
		break;
	case NORMAL_PUD:
		moved = move_normal_pud(pmc, old_entry, new_entry);
		break;
	case HPAGE_PMD:
		moved = IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
			move_huge_pmd(pmc->old, pmc->old_addr, pmc->new_addr, old_entry,
				      new_entry);
		break;
	case HPAGE_PUD:
		moved = IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
			move_huge_pud(pmc, old_entry, new_entry);
		break;

	default:
		WARN_ON_ONCE(1);
		break;
	}

	if (need_rmap_locks)
		drop_rmap_locks(pmc->old);

	return moved;
}

/*
 * A helper to check if aligning down is OK. The aligned address should fall
 * on *no mapping*. For the stack moving down, that's a special move within
 * the VMA that is created to span the source and destination of the move,
 * so we make an exception for it.
 */
static bool can_align_down(struct pagetable_move_control *pmc,
			   struct vm_area_struct *vma, unsigned long addr_to_align,
			   unsigned long mask)
{
	unsigned long addr_masked = addr_to_align & mask;

	/*
	 * If @addr_to_align of either source or destination is not the beginning
	 * of the corresponding VMA, we can't align down or we will destroy part
	 * of the current mapping.
	 */
	if (!pmc->for_stack && vma->vm_start != addr_to_align)
		return false;

	/* In the stack case we explicitly permit in-VMA alignment. */
	if (pmc->for_stack && addr_masked >= vma->vm_start)
		return true;

	/*
	 * Make sure the realignment doesn't cause the address to fall on an
	 * existing mapping.
	 */
	return find_vma_intersection(vma->vm_mm, addr_masked, vma->vm_start) == NULL;
}

/*
 * Determine if are in fact able to realign for efficiency to a higher page
 * table boundary.
 */
static bool can_realign_addr(struct pagetable_move_control *pmc,
			     unsigned long pagetable_mask)
{
	unsigned long align_mask = ~pagetable_mask;
	unsigned long old_align = pmc->old_addr & align_mask;
	unsigned long new_align = pmc->new_addr & align_mask;
	unsigned long pagetable_size = align_mask + 1;
	unsigned long old_align_next = pagetable_size - old_align;

	/*
	 * We don't want to have to go hunting for VMAs from the end of the old
	 * VMA to the next page table boundary, also we want to make sure the
	 * operation is wortwhile.
	 *
	 * So ensure that we only perform this realignment if the end of the
	 * range being copied reaches or crosses the page table boundary.
	 *
	 * boundary                        boundary
	 *    .<- old_align ->                .
	 *    .              |----------------.-----------|
	 *    .              |          vma   .           |
	 *    .              |----------------.-----------|
	 *    .              <----------------.----------->
	 *    .                          len_in
	 *    <------------------------------->
	 *    .         pagetable_size        .
	 *    .              <---------------->
	 *    .                old_align_next .
	 */
	if (pmc->len_in < old_align_next)
		return false;

	/* Skip if the addresses are already aligned. */
	if (old_align == 0)
		return false;

	/* Only realign if the new and old addresses are mutually aligned. */
	if (old_align != new_align)
		return false;

	/* Ensure realignment doesn't cause overlap with existing mappings. */
	if (!can_align_down(pmc, pmc->old, pmc->old_addr, pagetable_mask) ||
	    !can_align_down(pmc, pmc->new, pmc->new_addr, pagetable_mask))
		return false;

	return true;
}

/*
 * Opportunistically realign to specified boundary for faster copy.
 *
 * Consider an mremap() of a VMA with page table boundaries as below, and no
 * preceding VMAs from the lower page table boundary to the start of the VMA,
 * with the end of the range reaching or crossing the page table boundary.
 *
 *   boundary                        boundary
 *      .              |----------------.-----------|
 *      .              |          vma   .           |
 *      .              |----------------.-----------|
 *      .         pmc->old_addr         .      pmc->old_end
 *      .              <---------------------------->
 *      .                  move these page tables
 *
 * If we proceed with moving page tables in this scenario, we will have a lot of
 * work to do traversing old page tables and establishing new ones in the
 * destination across multiple lower level page tables.
 *
 * The idea here is simply to align pmc->old_addr, pmc->new_addr down to the
 * page table boundary, so we can simply copy a single page table entry for the
 * aligned portion of the VMA instead:
 *
 *   boundary                        boundary
 *      .              |----------------.-----------|
 *      .              |          vma   .           |
 *      .              |----------------.-----------|
 * pmc->old_addr                        .      pmc->old_end
 *      <------------------------------------------->
 *      .           move these page tables
 */
static void try_realign_addr(struct pagetable_move_control *pmc,
			     unsigned long pagetable_mask)
{

	if (!can_realign_addr(pmc, pagetable_mask))
		return;

	/*
	 * Simply align to page table boundaries. Note that we do NOT update the
	 * pmc->old_end value, and since the move_page_tables() operation spans
	 * from [old_addr, old_end) (offsetting new_addr as it is performed),
	 * this simply changes the start of the copy, not the end.
	 */
	pmc->old_addr &= pagetable_mask;
	pmc->new_addr &= pagetable_mask;
}

/* Is the page table move operation done? */
static bool pmc_done(struct pagetable_move_control *pmc)
{
	return pmc->old_addr >= pmc->old_end;
}

/* Advance to the next page table, offset by extent bytes. */
static void pmc_next(struct pagetable_move_control *pmc, unsigned long extent)
{
	pmc->old_addr += extent;
	pmc->new_addr += extent;
}

/*
 * Determine how many bytes in the specified input range have had their page
 * tables moved so far.
 */
static unsigned long pmc_progress(struct pagetable_move_control *pmc)
{
	unsigned long orig_old_addr = pmc->old_end - pmc->len_in;
	unsigned long old_addr = pmc->old_addr;

	/*
	 * Prevent negative return values when {old,new}_addr was realigned but
	 * we broke out of the loop in move_page_tables() for the first PMD
	 * itself.
	 */
	return old_addr < orig_old_addr ? 0 : old_addr - orig_old_addr;
}

unsigned long move_page_tables(struct pagetable_move_control *pmc)
{
	unsigned long extent;
	struct mmu_notifier_range range;
	pmd_t *old_pmd, *new_pmd;
	pud_t *old_pud, *new_pud;
	struct mm_struct *mm = pmc->old->vm_mm;

	if (!pmc->len_in)
		return 0;

	if (is_vm_hugetlb_page(pmc->old))
		return move_hugetlb_page_tables(pmc->old, pmc->new, pmc->old_addr,
						pmc->new_addr, pmc->len_in);

	/*
	 * If possible, realign addresses to PMD boundary for faster copy.
	 * Only realign if the mremap copying hits a PMD boundary.
	 */
	try_realign_addr(pmc, PMD_MASK);

	flush_cache_range(pmc->old, pmc->old_addr, pmc->old_end);
	mmu_notifier_range_init(&range, MMU_NOTIFY_UNMAP, 0, mm,
				pmc->old_addr, pmc->old_end);
	mmu_notifier_invalidate_range_start(&range);

	for (; !pmc_done(pmc); pmc_next(pmc, extent)) {
		cond_resched();
		/*
		 * If extent is PUD-sized try to speed up the move by moving at the
		 * PUD level if possible.
		 */
		extent = get_extent(NORMAL_PUD, pmc);

		old_pud = get_old_pud(mm, pmc->old_addr);
		if (!old_pud)
			continue;
		new_pud = alloc_new_pud(mm, pmc->new_addr);
		if (!new_pud)
			break;
		if (pud_trans_huge(*old_pud)) {
			if (extent == HPAGE_PUD_SIZE) {
				move_pgt_entry(pmc, HPAGE_PUD, old_pud, new_pud);
				/* We ignore and continue on error? */
				continue;
			}
		} else if (IS_ENABLED(CONFIG_HAVE_MOVE_PUD) && extent == PUD_SIZE) {
			if (move_pgt_entry(pmc, NORMAL_PUD, old_pud, new_pud))
				continue;
		}

		extent = get_extent(NORMAL_PMD, pmc);
		old_pmd = get_old_pmd(mm, pmc->old_addr);
		if (!old_pmd)
			continue;
		new_pmd = alloc_new_pmd(mm, pmc->new_addr);
		if (!new_pmd)
			break;
again:
		if (is_swap_pmd(*old_pmd) || pmd_trans_huge(*old_pmd)) {
			if (extent == HPAGE_PMD_SIZE &&
			    move_pgt_entry(pmc, HPAGE_PMD, old_pmd, new_pmd))
				continue;
			split_huge_pmd(pmc->old, old_pmd, pmc->old_addr);
		} else if (IS_ENABLED(CONFIG_HAVE_MOVE_PMD) &&
			   extent == PMD_SIZE) {
			/*
			 * If the extent is PMD-sized, try to speed the move by
			 * moving at the PMD level if possible.
			 */
			if (move_pgt_entry(pmc, NORMAL_PMD, old_pmd, new_pmd))
				continue;
		}
		if (pmd_none(*old_pmd))
			continue;
		if (pte_alloc(pmc->new->vm_mm, new_pmd))
			break;
		if (move_ptes(pmc, extent, old_pmd, new_pmd) < 0)
			goto again;
	}

	mmu_notifier_invalidate_range_end(&range);

	return pmc_progress(pmc);
}

/* Set vrm->delta to the difference in VMA size specified by user. */
static void vrm_set_delta(struct vma_remap_struct *vrm)
{
	vrm->delta = abs_diff(vrm->old_len, vrm->new_len);
}

/* Determine what kind of remap this is - shrink, expand or no resize at all. */
static enum mremap_type vrm_remap_type(struct vma_remap_struct *vrm)
{
	if (vrm->delta == 0)
		return MREMAP_NO_RESIZE;

	if (vrm->old_len > vrm->new_len)
		return MREMAP_SHRINK;

	return MREMAP_EXPAND;
}

/*
 * When moving a VMA to vrm->new_adr, does this result in the new and old VMAs
 * overlapping?
 */
static bool vrm_overlaps(struct vma_remap_struct *vrm)
{
	unsigned long start_old = vrm->addr;
	unsigned long start_new = vrm->new_addr;
	unsigned long end_old = vrm->addr + vrm->old_len;
	unsigned long end_new = vrm->new_addr + vrm->new_len;

	/*
	 * start_old    end_old
	 *     |-----------|
	 *     |           |
	 *     |-----------|
	 *             |-------------|
	 *             |             |
	 *             |-------------|
	 *         start_new      end_new
	 */
	if (end_old > start_new && end_new > start_old)
		return true;

	return false;
}

/*
 * Will a new address definitely be assigned? This either if the user specifies
 * it via MREMAP_FIXED, or if MREMAP_DONTUNMAP is used, indicating we will
 * always detemrine a target address.
 */
static bool vrm_implies_new_addr(struct vma_remap_struct *vrm)
{
	return vrm->flags & (MREMAP_FIXED | MREMAP_DONTUNMAP);
}

/*
 * Find an unmapped area for the requested vrm->new_addr.
 *
 * If MREMAP_FIXED then this is equivalent to a MAP_FIXED mmap() call. If only
 * MREMAP_DONTUNMAP is set, then this is equivalent to providing a hint to
 * mmap(), otherwise this is equivalent to mmap() specifying a NULL address.
 *
 * Returns 0 on success (with vrm->new_addr updated), or an error code upon
 * failure.
 */
static unsigned long vrm_set_new_addr(struct vma_remap_struct *vrm)
{
	struct vm_area_struct *vma = vrm->vma;
	unsigned long map_flags = 0;
	/* Page Offset _into_ the VMA. */
	pgoff_t internal_pgoff = (vrm->addr - vma->vm_start) >> PAGE_SHIFT;
	pgoff_t pgoff = vma->vm_pgoff + internal_pgoff;
	unsigned long new_addr = vrm_implies_new_addr(vrm) ? vrm->new_addr : 0;
	unsigned long res;

	if (vrm->flags & MREMAP_FIXED)
		map_flags |= MAP_FIXED;
	if (vma->vm_flags & VM_MAYSHARE)
		map_flags |= MAP_SHARED;

	res = get_unmapped_area(vma->vm_file, new_addr, vrm->new_len, pgoff,
				map_flags);
	if (IS_ERR_VALUE(res))
		return res;

	vrm->new_addr = res;
	return 0;
}

/*
 * Keep track of pages which have been added to the memory mapping. If the VMA
 * is accounted, also check to see if there is sufficient memory.
 *
 * Returns true on success, false if insufficient memory to charge.
 */
static bool vrm_calc_charge(struct vma_remap_struct *vrm)
{
	unsigned long charged;

	if (!(vrm->vma->vm_flags & VM_ACCOUNT))
		return true;

	/*
	 * If we don't unmap the old mapping, then we account the entirety of
	 * the length of the new one. Otherwise it's just the delta in size.
	 */
	if (vrm->flags & MREMAP_DONTUNMAP)
		charged = vrm->new_len >> PAGE_SHIFT;
	else
		charged = vrm->delta >> PAGE_SHIFT;


	/* This accounts 'charged' pages of memory. */
	if (security_vm_enough_memory_mm(current->mm, charged))
		return false;

	vrm->charged = charged;
	return true;
}

/*
 * an error has occurred so we will not be using vrm->charged memory. Unaccount
 * this memory if the VMA is accounted.
 */
static void vrm_uncharge(struct vma_remap_struct *vrm)
{
	if (!(vrm->vma->vm_flags & VM_ACCOUNT))
		return;

	vm_unacct_memory(vrm->charged);
	vrm->charged = 0;
}

/*
 * Update mm exec_vm, stack_vm, data_vm, and locked_vm fields as needed to
 * account for 'bytes' memory used, and if locked, indicate this in the VRM so
 * we can handle this correctly later.
 */
static void vrm_stat_account(struct vma_remap_struct *vrm,
			     unsigned long bytes)
{
	unsigned long pages = bytes >> PAGE_SHIFT;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = vrm->vma;

	vm_stat_account(mm, vma->vm_flags, pages);
	if (vma->vm_flags & VM_LOCKED) {
		mm->locked_vm += pages;
		vrm->mlocked = true;
	}
}

/*
 * Perform checks before attempting to write a VMA prior to it being
 * moved.
 */
static unsigned long prep_move_vma(struct vma_remap_struct *vrm)
{
	unsigned long err = 0;
	struct vm_area_struct *vma = vrm->vma;
	unsigned long old_addr = vrm->addr;
	unsigned long old_len = vrm->old_len;
	vm_flags_t dummy = vma->vm_flags;

	/*
	 * We'd prefer to avoid failure later on in do_munmap:
	 * which may split one vma into three before unmapping.
	 */
	if (current->mm->map_count >= sysctl_max_map_count - 3)
		return -ENOMEM;

	if (vma->vm_ops && vma->vm_ops->may_split) {
		if (vma->vm_start != old_addr)
			err = vma->vm_ops->may_split(vma, old_addr);
		if (!err && vma->vm_end != old_addr + old_len)
			err = vma->vm_ops->may_split(vma, old_addr + old_len);
		if (err)
			return err;
	}

	/*
	 * Advise KSM to break any KSM pages in the area to be moved:
	 * it would be confusing if they were to turn up at the new
	 * location, where they happen to coincide with different KSM
	 * pages recently unmapped.  But leave vma->vm_flags as it was,
	 * so KSM can come around to merge on vma and new_vma afterwards.
	 */
	err = ksm_madvise(vma, old_addr, old_addr + old_len,
			  MADV_UNMERGEABLE, &dummy);
	if (err)
		return err;

	return 0;
}

/*
 * Unmap source VMA for VMA move, turning it from a copy to a move, being
 * careful to ensure we do not underflow memory account while doing so if an
 * accountable move.
 *
 * This is best effort, if we fail to unmap then we simply try to correct
 * accounting and exit.
 */
static void unmap_source_vma(struct vma_remap_struct *vrm)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr = vrm->addr;
	unsigned long len = vrm->old_len;
	struct vm_area_struct *vma = vrm->vma;
	VMA_ITERATOR(vmi, mm, addr);
	int err;
	unsigned long vm_start;
	unsigned long vm_end;
	/*
	 * It might seem odd that we check for MREMAP_DONTUNMAP here, given this
	 * function implies that we unmap the original VMA, which seems
	 * contradictory.
	 *
	 * However, this occurs when this operation was attempted and an error
	 * arose, in which case we _do_ wish to unmap the _new_ VMA, which means
	 * we actually _do_ want it be unaccounted.
	 */
	bool accountable_move = (vma->vm_flags & VM_ACCOUNT) &&
		!(vrm->flags & MREMAP_DONTUNMAP);

	/*
	 * So we perform a trick here to prevent incorrect accounting. Any merge
	 * or new VMA allocation performed in copy_vma() does not adjust
	 * accounting, it is expected that callers handle this.
	 *
	 * And indeed we already have, accounting appropriately in the case of
	 * both in vrm_charge().
	 *
	 * However, when we unmap the existing VMA (to effect the move), this
	 * code will, if the VMA has VM_ACCOUNT set, attempt to unaccount
	 * removed pages.
	 *
	 * To avoid this we temporarily clear this flag, reinstating on any
	 * portions of the original VMA that remain.
	 */
	if (accountable_move) {
		vm_flags_clear(vma, VM_ACCOUNT);
		/* We are about to split vma, so store the start/end. */
		vm_start = vma->vm_start;
		vm_end = vma->vm_end;
	}

	err = do_vmi_munmap(&vmi, mm, addr, len, vrm->uf_unmap, /* unlock= */false);
	vrm->vma = NULL; /* Invalidated. */
	if (err) {
		/* OOM: unable to split vma, just get accounts right */
		vm_acct_memory(len >> PAGE_SHIFT);
		return;
	}

	/*
	 * If we mremap() from a VMA like this:
	 *
	 *    addr  end
	 *     |     |
	 *     v     v
	 * |-------------|
	 * |             |
	 * |-------------|
	 *
	 * Having cleared VM_ACCOUNT from the whole VMA, after we unmap above
	 * we'll end up with:
	 *
	 *    addr  end
	 *     |     |
	 *     v     v
	 * |---|     |---|
	 * | A |     | B |
	 * |---|     |---|
	 *
	 * The VMI is still pointing at addr, so vma_prev() will give us A, and
	 * a subsequent or lone vma_next() will give as B.
	 *
	 * do_vmi_munmap() will have restored the VMI back to addr.
	 */
	if (accountable_move) {
		unsigned long end = addr + len;

		if (vm_start < addr) {
			struct vm_area_struct *prev = vma_prev(&vmi);

			vm_flags_set(prev, VM_ACCOUNT); /* Acquires VMA lock. */
		}

		if (vm_end > end) {
			struct vm_area_struct *next = vma_next(&vmi);

			vm_flags_set(next, VM_ACCOUNT); /* Acquires VMA lock. */
		}
	}
}

/*
 * Copy vrm->vma over to vrm->new_addr possibly adjusting size as part of the
 * process. Additionally handle an error occurring on moving of page tables,
 * where we reset vrm state to cause unmapping of the new VMA.
 *
 * Outputs the newly installed VMA to new_vma_ptr. Returns 0 on success or an
 * error code.
 */
static int copy_vma_and_data(struct vma_remap_struct *vrm,
			     struct vm_area_struct **new_vma_ptr)
{
	unsigned long internal_offset = vrm->addr - vrm->vma->vm_start;
	unsigned long internal_pgoff = internal_offset >> PAGE_SHIFT;
	unsigned long new_pgoff = vrm->vma->vm_pgoff + internal_pgoff;
	unsigned long moved_len;
	struct vm_area_struct *vma = vrm->vma;
	struct vm_area_struct *new_vma;
	int err = 0;
	PAGETABLE_MOVE(pmc, NULL, NULL, vrm->addr, vrm->new_addr, vrm->old_len);

	new_vma = copy_vma(&vma, vrm->new_addr, vrm->new_len, new_pgoff,
			   &pmc.need_rmap_locks);
	if (!new_vma) {
		vrm_uncharge(vrm);
		*new_vma_ptr = NULL;
		return -ENOMEM;
	}
	vrm->vma = vma;
	pmc.old = vma;
	pmc.new = new_vma;

	moved_len = move_page_tables(&pmc);
	if (moved_len < vrm->old_len)
		err = -ENOMEM;
	else if (vma->vm_ops && vma->vm_ops->mremap)
		err = vma->vm_ops->mremap(new_vma);

	if (unlikely(err)) {
		PAGETABLE_MOVE(pmc_revert, new_vma, vma, vrm->new_addr,
			       vrm->addr, moved_len);

		/*
		 * On error, move entries back from new area to old,
		 * which will succeed since page tables still there,
		 * and then proceed to unmap new area instead of old.
		 */
		pmc_revert.need_rmap_locks = true;
		move_page_tables(&pmc_revert);

		vrm->vma = new_vma;
		vrm->old_len = vrm->new_len;
		vrm->addr = vrm->new_addr;
	} else {
		mremap_userfaultfd_prep(new_vma, vrm->uf);
	}

	fixup_hugetlb_reservations(vma);

	*new_vma_ptr = new_vma;
	return err;
}

/*
 * Perform final tasks for MADV_DONTUNMAP operation, clearing mlock() and
 * account flags on remaining VMA by convention (it cannot be mlock()'d any
 * longer, as pages in range are no longer mapped), and removing anon_vma_chain
 * links from it (if the entire VMA was copied over).
 */
static void dontunmap_complete(struct vma_remap_struct *vrm,
			       struct vm_area_struct *new_vma)
{
	unsigned long start = vrm->addr;
	unsigned long end = vrm->addr + vrm->old_len;
	unsigned long old_start = vrm->vma->vm_start;
	unsigned long old_end = vrm->vma->vm_end;

	/*
	 * We always clear VM_LOCKED[ONFAULT] | VM_ACCOUNT on the old
	 * vma.
	 */
	vm_flags_clear(vrm->vma, VM_LOCKED_MASK | VM_ACCOUNT);

	/*
	 * anon_vma links of the old vma is no longer needed after its page
	 * table has been moved.
	 */
	if (new_vma != vrm->vma && start == old_start && end == old_end)
		unlink_anon_vmas(vrm->vma);

	/* Because we won't unmap we don't need to touch locked_vm. */
}

static unsigned long move_vma(struct vma_remap_struct *vrm)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *new_vma;
	unsigned long hiwater_vm;
	int err;

	err = prep_move_vma(vrm);
	if (err)
		return err;

	/*
	 * If accounted, determine the number of bytes the operation will
	 * charge.
	 */
	if (!vrm_calc_charge(vrm))
		return -ENOMEM;

	/* We don't want racing faults. */
	vma_start_write(vrm->vma);

	/* Perform copy step. */
	err = copy_vma_and_data(vrm, &new_vma);
	/*
	 * If we established the copied-to VMA, we attempt to recover from the
	 * error by setting the destination VMA to the source VMA and unmapping
	 * it below.
	 */
	if (err && !new_vma)
		return err;

	/*
	 * If we failed to move page tables we still do total_vm increment
	 * since do_munmap() will decrement it by old_len == new_len.
	 *
	 * Since total_vm is about to be raised artificially high for a
	 * moment, we need to restore high watermark afterwards: if stats
	 * are taken meanwhile, total_vm and hiwater_vm appear too high.
	 * If this were a serious issue, we'd add a flag to do_munmap().
	 */
	hiwater_vm = mm->hiwater_vm;

	vrm_stat_account(vrm, vrm->new_len);
	if (unlikely(!err && (vrm->flags & MREMAP_DONTUNMAP)))
		dontunmap_complete(vrm, new_vma);
	else
		unmap_source_vma(vrm);

	mm->hiwater_vm = hiwater_vm;

	return err ? (unsigned long)err : vrm->new_addr;
}

/*
 * remap_is_valid() - Ensure the VMA can be moved or resized to the new length,
 * at the given address.
 *
 * Return 0 on success, error otherwise.
 */
static int remap_is_valid(struct vma_remap_struct *vrm)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = vrm->vma;
	unsigned long addr = vrm->addr;
	unsigned long old_len = vrm->old_len;
	unsigned long new_len = vrm->new_len;
	unsigned long pgoff;

	/*
	 * !old_len is a special case where an attempt is made to 'duplicate'
	 * a mapping.  This makes no sense for private mappings as it will
	 * instead create a fresh/new mapping unrelated to the original.  This
	 * is contrary to the basic idea of mremap which creates new mappings
	 * based on the original.  There are no known use cases for this
	 * behavior.  As a result, fail such attempts.
	 */
	if (!old_len && !(vma->vm_flags & (VM_SHARED | VM_MAYSHARE))) {
		pr_warn_once("%s (%d): attempted to duplicate a private mapping with mremap.  This is not supported.\n",
			     current->comm, current->pid);
		return -EINVAL;
	}

	if ((vrm->flags & MREMAP_DONTUNMAP) &&
			(vma->vm_flags & (VM_DONTEXPAND | VM_PFNMAP)))
		return -EINVAL;

	/*
	 * We permit crossing of boundaries for the range being unmapped due to
	 * a shrink.
	 */
	if (vrm->remap_type == MREMAP_SHRINK)
		old_len = new_len;

	/* We can't remap across vm area boundaries */
	if (old_len > vma->vm_end - addr)
		return -EFAULT;

	if (new_len == old_len)
		return 0;

	/* Need to be careful about a growing mapping */
	pgoff = (addr - vma->vm_start) >> PAGE_SHIFT;
	pgoff += vma->vm_pgoff;
	if (pgoff + (new_len >> PAGE_SHIFT) < pgoff)
		return -EINVAL;

	if (vma->vm_flags & (VM_DONTEXPAND | VM_PFNMAP))
		return -EFAULT;

	if (!mlock_future_ok(mm, vma->vm_flags, vrm->delta))
		return -EAGAIN;

	if (!may_expand_vm(mm, vma->vm_flags, vrm->delta >> PAGE_SHIFT))
		return -ENOMEM;

	return 0;
}

/*
 * The user has requested that the VMA be shrunk (i.e., old_len > new_len), so
 * execute this, optionally dropping the mmap lock when we do so.
 *
 * In both cases this invalidates the VMA, however if we don't drop the lock,
 * then load the correct VMA into vrm->vma afterwards.
 */
static unsigned long shrink_vma(struct vma_remap_struct *vrm,
				bool drop_lock)
{
	struct mm_struct *mm = current->mm;
	unsigned long unmap_start = vrm->addr + vrm->new_len;
	unsigned long unmap_bytes = vrm->delta;
	unsigned long res;
	VMA_ITERATOR(vmi, mm, unmap_start);

	VM_BUG_ON(vrm->remap_type != MREMAP_SHRINK);

	res = do_vmi_munmap(&vmi, mm, unmap_start, unmap_bytes,
			    vrm->uf_unmap, drop_lock);
	vrm->vma = NULL; /* Invalidated. */
	if (res)
		return res;

	/*
	 * If we've not dropped the lock, then we should reload the VMA to
	 * replace the invalidated VMA with the one that may have now been
	 * split.
	 */
	if (drop_lock) {
		vrm->mmap_locked = false;
	} else {
		vrm->vma = vma_lookup(mm, vrm->addr);
		if (!vrm->vma)
			return -EFAULT;
	}

	return 0;
}

/*
 * mremap_to() - remap a vma to a new location.
 * Returns: The new address of the vma or an error.
 */
static unsigned long mremap_to(struct vma_remap_struct *vrm)
{
	struct mm_struct *mm = current->mm;
	unsigned long err;

	if (vrm->flags & MREMAP_FIXED) {
		/*
		 * In mremap_to().
		 * VMA is moved to dst address, and munmap dst first.
		 * do_munmap will check if dst is sealed.
		 */
		err = do_munmap(mm, vrm->new_addr, vrm->new_len,
				vrm->uf_unmap_early);
		vrm->vma = NULL; /* Invalidated. */
		if (err)
			return err;

		/*
		 * If we remap a portion of a VMA elsewhere in the same VMA,
		 * this can invalidate the old VMA. Reset.
		 */
		vrm->vma = vma_lookup(mm, vrm->addr);
		if (!vrm->vma)
			return -EFAULT;
	}

	if (vrm->remap_type == MREMAP_SHRINK) {
		err = shrink_vma(vrm, /* drop_lock= */false);
		if (err)
			return err;

		/* Set up for the move now shrink has been executed. */
		vrm->old_len = vrm->new_len;
	}

	/* MREMAP_DONTUNMAP expands by old_len since old_len == new_len */
	if (vrm->flags & MREMAP_DONTUNMAP) {
		vm_flags_t vm_flags = vrm->vma->vm_flags;
		unsigned long pages = vrm->old_len >> PAGE_SHIFT;

		if (!may_expand_vm(mm, vm_flags, pages))
			return -ENOMEM;
	}

	err = vrm_set_new_addr(vrm);
	if (err)
		return err;

	return move_vma(vrm);
}

static int vma_expandable(struct vm_area_struct *vma, unsigned long delta)
{
	unsigned long end = vma->vm_end + delta;

	if (end < vma->vm_end) /* overflow */
		return 0;
	if (find_vma_intersection(vma->vm_mm, vma->vm_end, end))
		return 0;
	if (get_unmapped_area(NULL, vma->vm_start, end - vma->vm_start,
			      0, MAP_FIXED) & ~PAGE_MASK)
		return 0;
	return 1;
}

/* Determine whether we are actually able to execute an in-place expansion. */
static bool vrm_can_expand_in_place(struct vma_remap_struct *vrm)
{
	/* Number of bytes from vrm->addr to end of VMA. */
	unsigned long suffix_bytes = vrm->vma->vm_end - vrm->addr;

	/* If end of range aligns to end of VMA, we can just expand in-place. */
	if (suffix_bytes != vrm->old_len)
		return false;

	/* Check whether this is feasible. */
	if (!vma_expandable(vrm->vma, vrm->delta))
		return false;

	return true;
}

/*
 * Are the parameters passed to mremap() valid? If so return 0, otherwise return
 * error.
 */
static unsigned long check_mremap_params(struct vma_remap_struct *vrm)

{
	unsigned long addr = vrm->addr;
	unsigned long flags = vrm->flags;

	/* Ensure no unexpected flag values. */
	if (flags & ~(MREMAP_FIXED | MREMAP_MAYMOVE | MREMAP_DONTUNMAP))
		return -EINVAL;

	/* Start address must be page-aligned. */
	if (offset_in_page(addr))
		return -EINVAL;

	/*
	 * We allow a zero old-len as a special case
	 * for DOS-emu "duplicate shm area" thing. But
	 * a zero new-len is nonsensical.
	 */
	if (!vrm->new_len)
		return -EINVAL;

	/* Is the new length or address silly? */
	if (vrm->new_len > TASK_SIZE ||
	    vrm->new_addr > TASK_SIZE - vrm->new_len)
		return -EINVAL;

	/* Remainder of checks are for cases with specific new_addr. */
	if (!vrm_implies_new_addr(vrm))
		return 0;

	/* The new address must be page-aligned. */
	if (offset_in_page(vrm->new_addr))
		return -EINVAL;

	/* A fixed address implies a move. */
	if (!(flags & MREMAP_MAYMOVE))
		return -EINVAL;

	/* MREMAP_DONTUNMAP does not allow resizing in the process. */
	if (flags & MREMAP_DONTUNMAP && vrm->old_len != vrm->new_len)
		return -EINVAL;

	/* Target VMA must not overlap source VMA. */
	if (vrm_overlaps(vrm))
		return -EINVAL;

	/*
	 * move_vma() need us to stay 4 maps below the threshold, otherwise
	 * it will bail out at the very beginning.
	 * That is a problem if we have already unmaped the regions here
	 * (new_addr, and old_addr), because userspace will not know the
	 * state of the vma's after it gets -ENOMEM.
	 * So, to avoid such scenario we can pre-compute if the whole
	 * operation has high chances to success map-wise.
	 * Worst-scenario case is when both vma's (new_addr and old_addr) get
	 * split in 3 before unmapping it.
	 * That means 2 more maps (1 for each) to the ones we already hold.
	 * Check whether current map count plus 2 still leads us to 4 maps below
	 * the threshold, otherwise return -ENOMEM here to be more safe.
	 */
	if ((current->mm->map_count + 2) >= sysctl_max_map_count - 3)
		return -ENOMEM;

	return 0;
}

/*
 * We know we can expand the VMA in-place by delta pages, so do so.
 *
 * If we discover the VMA is locked, update mm_struct statistics accordingly and
 * indicate so to the caller.
 */
static unsigned long expand_vma_in_place(struct vma_remap_struct *vrm)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = vrm->vma;
	VMA_ITERATOR(vmi, mm, vma->vm_end);

	if (!vrm_calc_charge(vrm))
		return -ENOMEM;

	/*
	 * Function vma_merge_extend() is called on the
	 * extension we are adding to the already existing vma,
	 * vma_merge_extend() will merge this extension with the
	 * already existing vma (expand operation itself) and
	 * possibly also with the next vma if it becomes
	 * adjacent to the expanded vma and otherwise
	 * compatible.
	 */
	vma = vma_merge_extend(&vmi, vma, vrm->delta);
	if (!vma) {
		vrm_uncharge(vrm);
		return -ENOMEM;
	}
	vrm->vma = vma;

	vrm_stat_account(vrm, vrm->delta);

	return 0;
}

static bool align_hugetlb(struct vma_remap_struct *vrm)
{
	struct hstate *h __maybe_unused = hstate_vma(vrm->vma);

	vrm->old_len = ALIGN(vrm->old_len, huge_page_size(h));
	vrm->new_len = ALIGN(vrm->new_len, huge_page_size(h));

	/* addrs must be huge page aligned */
	if (vrm->addr & ~huge_page_mask(h))
		return false;
	if (vrm->new_addr & ~huge_page_mask(h))
		return false;

	/*
	 * Don't allow remap expansion, because the underlying hugetlb
	 * reservation is not yet capable to handle split reservation.
	 */
	if (vrm->new_len > vrm->old_len)
		return false;

	return true;
}

/*
 * We are mremap()'ing without specifying a fixed address to move to, but are
 * requesting that the VMA's size be increased.
 *
 * Try to do so in-place, if this fails, then move the VMA to a new location to
 * action the change.
 */
static unsigned long expand_vma(struct vma_remap_struct *vrm)
{
	unsigned long err;

	/*
	 * [addr, old_len) spans precisely to the end of the VMA, so try to
	 * expand it in-place.
	 */
	if (vrm_can_expand_in_place(vrm)) {
		err = expand_vma_in_place(vrm);
		if (err)
			return err;

		/* OK we're done! */
		return vrm->addr;
	}

	/*
	 * We weren't able to just expand or shrink the area,
	 * we need to create a new one and move it.
	 */

	/* We're not allowed to move the VMA, so error out. */
	if (!(vrm->flags & MREMAP_MAYMOVE))
		return -ENOMEM;

	/* Find a new location to move the VMA to. */
	err = vrm_set_new_addr(vrm);
	if (err)
		return err;

	return move_vma(vrm);
}

/*
 * Attempt to resize the VMA in-place, if we cannot, then move the VMA to the
 * first available address to perform the operation.
 */
static unsigned long mremap_at(struct vma_remap_struct *vrm)
{
	unsigned long res;

	switch (vrm->remap_type) {
	case MREMAP_INVALID:
		break;
	case MREMAP_NO_RESIZE:
		/* NO-OP CASE - resizing to the same size. */
		return vrm->addr;
	case MREMAP_SHRINK:
		/*
		 * SHRINK CASE. Can always be done in-place.
		 *
		 * Simply unmap the shrunken portion of the VMA. This does all
		 * the needed commit accounting, and we indicate that the mmap
		 * lock should be dropped.
		 */
		res = shrink_vma(vrm, /* drop_lock= */true);
		if (res)
			return res;

		return vrm->addr;
	case MREMAP_EXPAND:
		return expand_vma(vrm);
	}

	/* Should not be possible. */
	WARN_ON_ONCE(1);
	return -EINVAL;
}

/*
 * Will this operation result in the VMA being expanded or moved and thus need
 * to map a new portion of virtual address space?
 */
static bool vrm_will_map_new(struct vma_remap_struct *vrm)
{
	if (vrm->remap_type == MREMAP_EXPAND)
		return true;

	if (vrm_implies_new_addr(vrm))
		return true;

	return false;
}

static int check_prep_vma(struct vma_remap_struct *vrm)
{
	struct vm_area_struct *vma = vrm->vma;

	if (!vma)
		return -EFAULT;

	/* If mseal()'d, mremap() is prohibited. */
	if (!can_modify_vma(vma))
		return -EPERM;

	/* Align to hugetlb page size, if required. */
	if (is_vm_hugetlb_page(vma) && !align_hugetlb(vrm))
		return -EINVAL;

	vrm_set_delta(vrm);
	vrm->remap_type = vrm_remap_type(vrm);
	/* For convenience, we set new_addr even if VMA won't move. */
	if (!vrm_implies_new_addr(vrm))
		vrm->new_addr = vrm->addr;

	if (vrm_will_map_new(vrm))
		return remap_is_valid(vrm);

	return 0;
}

static void notify_uffd(struct vma_remap_struct *vrm, bool failed)
{
	struct mm_struct *mm = current->mm;

	/* Regardless of success/failure, we always notify of any unmaps. */
	userfaultfd_unmap_complete(mm, vrm->uf_unmap_early);
	if (failed)
		mremap_userfaultfd_fail(vrm->uf);
	else
		mremap_userfaultfd_complete(vrm->uf, vrm->addr,
			vrm->new_addr, vrm->old_len);
	userfaultfd_unmap_complete(mm, vrm->uf_unmap);
}

static unsigned long do_mremap(struct vma_remap_struct *vrm)
{
	struct mm_struct *mm = current->mm;
	unsigned long res;
	bool failed;

	vrm->old_len = PAGE_ALIGN(vrm->old_len);
	vrm->new_len = PAGE_ALIGN(vrm->new_len);

	res = check_mremap_params(vrm);
	if (res)
		return res;

	if (mmap_write_lock_killable(mm))
		return -EINTR;
	vrm->mmap_locked = true;

	vrm->vma = vma_lookup(current->mm, vrm->addr);
	res = check_prep_vma(vrm);
	if (res)
		goto out;

	/* Actually execute mremap. */
	res = vrm_implies_new_addr(vrm) ? mremap_to(vrm) : mremap_at(vrm);

out:
	failed = IS_ERR_VALUE(res);

	if (vrm->mmap_locked)
		mmap_write_unlock(mm);

	if (!failed && vrm->mlocked && vrm->new_len > vrm->old_len)
		mm_populate(vrm->new_addr + vrm->old_len, vrm->delta);

	notify_uffd(vrm, failed);
	return res;
}

/*
 * Expand (or shrink) an existing mapping, potentially moving it at the
 * same time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 *
 * MREMAP_FIXED option added 5-Dec-1999 by Benjamin LaHaise
 * This option implies MREMAP_MAYMOVE.
 */
SYSCALL_DEFINE5(mremap, unsigned long, addr, unsigned long, old_len,
		unsigned long, new_len, unsigned long, flags,
		unsigned long, new_addr)
{
	struct vm_userfaultfd_ctx uf = NULL_VM_UFFD_CTX;
	LIST_HEAD(uf_unmap_early);
	LIST_HEAD(uf_unmap);
	/*
	 * There is a deliberate asymmetry here: we strip the pointer tag
	 * from the old address but leave the new address alone. This is
	 * for consistency with mmap(), where we prevent the creation of
	 * aliasing mappings in userspace by leaving the tag bits of the
	 * mapping address intact. A non-zero tag will cause the subsequent
	 * range checks to reject the address as invalid.
	 *
	 * See Documentation/arch/arm64/tagged-address-abi.rst for more
	 * information.
	 */
	struct vma_remap_struct vrm = {
		.addr = untagged_addr(addr),
		.old_len = old_len,
		.new_len = new_len,
		.flags = flags,
		.new_addr = new_addr,

		.uf = &uf,
		.uf_unmap_early = &uf_unmap_early,
		.uf_unmap = &uf_unmap,

		.remap_type = MREMAP_INVALID, /* We set later. */
	};

	return do_mremap(&vrm);
}
