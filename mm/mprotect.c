// SPDX-License-Identifier: GPL-2.0
/*
 *  mm/mprotect.c
 *
 *  (C) Copyright 1994 Linus Torvalds
 *  (C) Copyright 2002 Christoph Hellwig
 *
 *  Address space accounting code	<alan@lxorguk.ukuu.org.uk>
 *  (C) Copyright 2002 Red Hat Inc, All Rights Reserved
 */

#include <linux/pagewalk.h>
#include <linux/hugetlb.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/security.h>
#include <linux/mempolicy.h>
#include <linux/personality.h>
#include <linux/syscalls.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>
#include <linux/perf_event.h>
#include <linux/pkeys.h>
#include <linux/ksm.h>
#include <linux/uaccess.h>
#include <linux/mm_inline.h>
#include <linux/pgtable.h>
#include <linux/sched/sysctl.h>
#include <linux/userfaultfd_k.h>
#include <linux/memory-tiers.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>

#include "internal.h"

static inline bool can_change_pte_writable(struct vm_area_struct *vma,
					   unsigned long addr, pte_t pte)
{
	struct page *page;

	VM_BUG_ON(!(vma->vm_flags & VM_WRITE) || pte_write(pte));

	if (pte_protnone(pte) || !pte_dirty(pte))
		return false;

	/* Do we need write faults for softdirty tracking? */
	if (vma_soft_dirty_enabled(vma) && !pte_soft_dirty(pte))
		return false;

	/* Do we need write faults for uffd-wp tracking? */
	if (userfaultfd_pte_wp(vma, pte))
		return false;

	if (!(vma->vm_flags & VM_SHARED)) {
		/*
		 * We can only special-case on exclusive anonymous pages,
		 * because we know that our write-fault handler similarly would
		 * map them writable without any additional checks while holding
		 * the PT lock.
		 */
		page = vm_normal_page(vma, addr, pte);
		if (!page || !PageAnon(page) || !PageAnonExclusive(page))
			return false;
	}

	return true;
}

static unsigned long change_pte_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, pmd_t *pmd, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	pte_t *pte, oldpte;
	spinlock_t *ptl;
	unsigned long pages = 0;
	int target_node = NUMA_NO_NODE;
	bool prot_numa = cp_flags & MM_CP_PROT_NUMA;
	bool uffd_wp = cp_flags & MM_CP_UFFD_WP;
	bool uffd_wp_resolve = cp_flags & MM_CP_UFFD_WP_RESOLVE;

	tlb_change_page_size(tlb, PAGE_SIZE);

	/*
	 * Can be called with only the mmap_lock for reading by
	 * prot_numa so we must check the pmd isn't constantly
	 * changing from under us from pmd_none to pmd_trans_huge
	 * and/or the other way around.
	 */
	if (pmd_trans_unstable(pmd))
		return 0;

	/*
	 * The pmd points to a regular pte so the pmd can't change
	 * from under us even if the mmap_lock is only hold for
	 * reading.
	 */
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);

	/* Get target node for single threaded private VMAs */
	if (prot_numa && !(vma->vm_flags & VM_SHARED) &&
	    atomic_read(&vma->vm_mm->mm_users) == 1)
		target_node = numa_node_id();

	flush_tlb_batched_pending(vma->vm_mm);
	arch_enter_lazy_mmu_mode();
	do {
		oldpte = *pte;
		if (pte_present(oldpte)) {
			pte_t ptent;
			bool preserve_write = prot_numa && pte_write(oldpte);

			/*
			 * Avoid trapping faults against the zero or KSM
			 * pages. See similar comment in change_huge_pmd.
			 */
			if (prot_numa) {
				struct page *page;
				int nid;
				bool toptier;

				/* Avoid TLB flush if possible */
				if (pte_protnone(oldpte))
					continue;

				page = vm_normal_page(vma, addr, oldpte);
				if (!page || is_zone_device_page(page) || PageKsm(page))
					continue;

				/* Also skip shared copy-on-write pages */
				if (is_cow_mapping(vma->vm_flags) &&
				    page_count(page) != 1)
					continue;

				/*
				 * While migration can move some dirty pages,
				 * it cannot move them all from MIGRATE_ASYNC
				 * context.
				 */
				if (page_is_file_lru(page) && PageDirty(page))
					continue;

				/*
				 * Don't mess with PTEs if page is already on the node
				 * a single-threaded process is running on.
				 */
				nid = page_to_nid(page);
				if (target_node == nid)
					continue;
				toptier = node_is_toptier(nid);

				/*
				 * Skip scanning top tier node if normal numa
				 * balancing is disabled
				 */
				if (!(sysctl_numa_balancing_mode & NUMA_BALANCING_NORMAL) &&
				    toptier)
					continue;
				if (sysctl_numa_balancing_mode & NUMA_BALANCING_MEMORY_TIERING &&
				    !toptier)
					xchg_page_access_time(page,
						jiffies_to_msecs(jiffies));
			}

			oldpte = ptep_modify_prot_start(vma, addr, pte);
			ptent = pte_modify(oldpte, newprot);
			if (preserve_write)
				ptent = pte_mk_savedwrite(ptent);

			if (uffd_wp) {
				ptent = pte_wrprotect(ptent);
				ptent = pte_mkuffd_wp(ptent);
			} else if (uffd_wp_resolve) {
				ptent = pte_clear_uffd_wp(ptent);
			}

			/*
			 * In some writable, shared mappings, we might want
			 * to catch actual write access -- see
			 * vma_wants_writenotify().
			 *
			 * In all writable, private mappings, we have to
			 * properly handle COW.
			 *
			 * In both cases, we can sometimes still change PTEs
			 * writable and avoid the write-fault handler, for
			 * example, if a PTE is already dirty and no other
			 * COW or special handling is required.
			 */
			if ((cp_flags & MM_CP_TRY_CHANGE_WRITABLE) &&
			    !pte_write(ptent) &&
			    can_change_pte_writable(vma, addr, ptent))
				ptent = pte_mkwrite(ptent);

			ptep_modify_prot_commit(vma, addr, pte, oldpte, ptent);
			if (pte_needs_flush(oldpte, ptent))
				tlb_flush_pte_range(tlb, addr, PAGE_SIZE);
			pages++;
		} else if (is_swap_pte(oldpte)) {
			swp_entry_t entry = pte_to_swp_entry(oldpte);
			pte_t newpte;

			if (is_writable_migration_entry(entry)) {
				struct page *page = pfn_swap_entry_to_page(entry);

				/*
				 * A protection check is difficult so
				 * just be safe and disable write
				 */
				if (PageAnon(page))
					entry = make_readable_exclusive_migration_entry(
							     swp_offset(entry));
				else
					entry = make_readable_migration_entry(swp_offset(entry));
				newpte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(oldpte))
					newpte = pte_swp_mksoft_dirty(newpte);
				if (pte_swp_uffd_wp(oldpte))
					newpte = pte_swp_mkuffd_wp(newpte);
			} else if (is_writable_device_private_entry(entry)) {
				/*
				 * We do not preserve soft-dirtiness. See
				 * copy_one_pte() for explanation.
				 */
				entry = make_readable_device_private_entry(
							swp_offset(entry));
				newpte = swp_entry_to_pte(entry);
				if (pte_swp_uffd_wp(oldpte))
					newpte = pte_swp_mkuffd_wp(newpte);
			} else if (is_writable_device_exclusive_entry(entry)) {
				entry = make_readable_device_exclusive_entry(
							swp_offset(entry));
				newpte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(oldpte))
					newpte = pte_swp_mksoft_dirty(newpte);
				if (pte_swp_uffd_wp(oldpte))
					newpte = pte_swp_mkuffd_wp(newpte);
			} else if (pte_marker_entry_uffd_wp(entry)) {
				/*
				 * If this is uffd-wp pte marker and we'd like
				 * to unprotect it, drop it; the next page
				 * fault will trigger without uffd trapping.
				 */
				if (uffd_wp_resolve) {
					pte_clear(vma->vm_mm, addr, pte);
					pages++;
				}
				continue;
			} else {
				newpte = oldpte;
			}

			if (uffd_wp)
				newpte = pte_swp_mkuffd_wp(newpte);
			else if (uffd_wp_resolve)
				newpte = pte_swp_clear_uffd_wp(newpte);

			if (!pte_same(oldpte, newpte)) {
				set_pte_at(vma->vm_mm, addr, pte, newpte);
				pages++;
			}
		} else {
			/* It must be an none page, or what else?.. */
			WARN_ON_ONCE(!pte_none(oldpte));
#ifdef CONFIG_PTE_MARKER_UFFD_WP
			if (unlikely(uffd_wp && !vma_is_anonymous(vma))) {
				/*
				 * For file-backed mem, we need to be able to
				 * wr-protect a none pte, because even if the
				 * pte is none, the page/swap cache could
				 * exist.  Doing that by install a marker.
				 */
				set_pte_at(vma->vm_mm, addr, pte,
					   make_pte_marker(PTE_MARKER_UFFD_WP));
				pages++;
			}
#endif
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);

	return pages;
}

/*
 * Used when setting automatic NUMA hinting protection where it is
 * critical that a numa hinting PMD is not confused with a bad PMD.
 */
static inline int pmd_none_or_clear_bad_unless_trans_huge(pmd_t *pmd)
{
	pmd_t pmdval = pmd_read_atomic(pmd);

	/* See pmd_none_or_trans_huge_or_clear_bad for info on barrier */
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	barrier();
#endif

	if (pmd_none(pmdval))
		return 1;
	if (pmd_trans_huge(pmdval))
		return 0;
	if (unlikely(pmd_bad(pmdval))) {
		pmd_clear_bad(pmd);
		return 1;
	}

	return 0;
}

/* Return true if we're uffd wr-protecting file-backed memory, or false */
static inline bool
uffd_wp_protect_file(struct vm_area_struct *vma, unsigned long cp_flags)
{
	return (cp_flags & MM_CP_UFFD_WP) && !vma_is_anonymous(vma);
}

/*
 * If wr-protecting the range for file-backed, populate pgtable for the case
 * when pgtable is empty but page cache exists.  When {pte|pmd|...}_alloc()
 * failed it means no memory, we don't have a better option but stop.
 */
#define  change_pmd_prepare(vma, pmd, cp_flags)				\
	do {								\
		if (unlikely(uffd_wp_protect_file(vma, cp_flags))) {	\
			if (WARN_ON_ONCE(pte_alloc(vma->vm_mm, pmd)))	\
				break;					\
		}							\
	} while (0)
/*
 * This is the general pud/p4d/pgd version of change_pmd_prepare(). We need to
 * have separate change_pmd_prepare() because pte_alloc() returns 0 on success,
 * while {pmd|pud|p4d}_alloc() returns the valid pointer on success.
 */
#define  change_prepare(vma, high, low, addr, cp_flags)			\
	do {								\
		if (unlikely(uffd_wp_protect_file(vma, cp_flags))) {	\
			low##_t *p = low##_alloc(vma->vm_mm, high, addr); \
			if (WARN_ON_ONCE(p == NULL))			\
				break;					\
		}							\
	} while (0)

static inline unsigned long change_pmd_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, pud_t *pud, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long pages = 0;
	unsigned long nr_huge_updates = 0;
	struct mmu_notifier_range range;

	range.start = 0;

	pmd = pmd_offset(pud, addr);
	do {
		unsigned long this_pages;

		next = pmd_addr_end(addr, end);

		change_pmd_prepare(vma, pmd, cp_flags);
		/*
		 * Automatic NUMA balancing walks the tables with mmap_lock
		 * held for read. It's possible a parallel update to occur
		 * between pmd_trans_huge() and a pmd_none_or_clear_bad()
		 * check leading to a false positive and clearing.
		 * Hence, it's necessary to atomically read the PMD value
		 * for all the checks.
		 */
		if (!is_swap_pmd(*pmd) && !pmd_devmap(*pmd) &&
		     pmd_none_or_clear_bad_unless_trans_huge(pmd))
			goto next;

		/* invoke the mmu notifier if the pmd is populated */
		if (!range.start) {
			mmu_notifier_range_init(&range,
				MMU_NOTIFY_PROTECTION_VMA, 0,
				vma, vma->vm_mm, addr, end);
			mmu_notifier_invalidate_range_start(&range);
		}

		if (is_swap_pmd(*pmd) || pmd_trans_huge(*pmd) || pmd_devmap(*pmd)) {
			if ((next - addr != HPAGE_PMD_SIZE) ||
			    uffd_wp_protect_file(vma, cp_flags)) {
				__split_huge_pmd(vma, pmd, addr, false, NULL);
				/*
				 * For file-backed, the pmd could have been
				 * cleared; make sure pmd populated if
				 * necessary, then fall-through to pte level.
				 */
				change_pmd_prepare(vma, pmd, cp_flags);
			} else {
				/*
				 * change_huge_pmd() does not defer TLB flushes,
				 * so no need to propagate the tlb argument.
				 */
				int nr_ptes = change_huge_pmd(tlb, vma, pmd,
						addr, newprot, cp_flags);

				if (nr_ptes) {
					if (nr_ptes == HPAGE_PMD_NR) {
						pages += HPAGE_PMD_NR;
						nr_huge_updates++;
					}

					/* huge pmd was handled */
					goto next;
				}
			}
			/* fall through, the trans huge pmd just split */
		}
		this_pages = change_pte_range(tlb, vma, pmd, addr, next,
					      newprot, cp_flags);
		pages += this_pages;
next:
		cond_resched();
	} while (pmd++, addr = next, addr != end);

	if (range.start)
		mmu_notifier_invalidate_range_end(&range);

	if (nr_huge_updates)
		count_vm_numa_events(NUMA_HUGE_PTE_UPDATES, nr_huge_updates);
	return pages;
}

static inline unsigned long change_pud_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, p4d_t *p4d, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	pud_t *pud;
	unsigned long next;
	unsigned long pages = 0;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		change_prepare(vma, pud, pmd, addr, cp_flags);
		if (pud_none_or_clear_bad(pud))
			continue;
		pages += change_pmd_range(tlb, vma, pud, addr, next, newprot,
					  cp_flags);
	} while (pud++, addr = next, addr != end);

	return pages;
}

static inline unsigned long change_p4d_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, pgd_t *pgd, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	p4d_t *p4d;
	unsigned long next;
	unsigned long pages = 0;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		change_prepare(vma, p4d, pud, addr, cp_flags);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		pages += change_pud_range(tlb, vma, p4d, addr, next, newprot,
					  cp_flags);
	} while (p4d++, addr = next, addr != end);

	return pages;
}

static unsigned long change_protection_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	unsigned long next;
	unsigned long pages = 0;

	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	tlb_start_vma(tlb, vma);
	do {
		next = pgd_addr_end(addr, end);
		change_prepare(vma, pgd, p4d, addr, cp_flags);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		pages += change_p4d_range(tlb, vma, pgd, addr, next, newprot,
					  cp_flags);
	} while (pgd++, addr = next, addr != end);

	tlb_end_vma(tlb, vma);

	return pages;
}

unsigned long change_protection(struct mmu_gather *tlb,
		       struct vm_area_struct *vma, unsigned long start,
		       unsigned long end, pgprot_t newprot,
		       unsigned long cp_flags)
{
	unsigned long pages;

	BUG_ON((cp_flags & MM_CP_UFFD_WP_ALL) == MM_CP_UFFD_WP_ALL);

	if (is_vm_hugetlb_page(vma))
		pages = hugetlb_change_protection(vma, start, end, newprot,
						  cp_flags);
	else
		pages = change_protection_range(tlb, vma, start, end, newprot,
						cp_flags);

	return pages;
}

static int prot_none_pte_entry(pte_t *pte, unsigned long addr,
			       unsigned long next, struct mm_walk *walk)
{
	return pfn_modify_allowed(pte_pfn(*pte), *(pgprot_t *)(walk->private)) ?
		0 : -EACCES;
}

static int prot_none_hugetlb_entry(pte_t *pte, unsigned long hmask,
				   unsigned long addr, unsigned long next,
				   struct mm_walk *walk)
{
	return pfn_modify_allowed(pte_pfn(*pte), *(pgprot_t *)(walk->private)) ?
		0 : -EACCES;
}

static int prot_none_test(unsigned long addr, unsigned long next,
			  struct mm_walk *walk)
{
	return 0;
}

static const struct mm_walk_ops prot_none_walk_ops = {
	.pte_entry		= prot_none_pte_entry,
	.hugetlb_entry		= prot_none_hugetlb_entry,
	.test_walk		= prot_none_test,
};

int
mprotect_fixup(struct mmu_gather *tlb, struct vm_area_struct *vma,
	       struct vm_area_struct **pprev, unsigned long start,
	       unsigned long end, unsigned long newflags)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long oldflags = vma->vm_flags;
	long nrpages = (end - start) >> PAGE_SHIFT;
	unsigned long charged = 0;
	bool try_change_writable;
	pgoff_t pgoff;
	int error;

	if (newflags == oldflags) {
		*pprev = vma;
		return 0;
	}

	/*
	 * Do PROT_NONE PFN permission checks here when we can still
	 * bail out without undoing a lot of state. This is a rather
	 * uncommon case, so doesn't need to be very optimized.
	 */
	if (arch_has_pfn_modify_check() &&
	    (vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) &&
	    (newflags & VM_ACCESS_FLAGS) == 0) {
		pgprot_t new_pgprot = vm_get_page_prot(newflags);

		error = walk_page_range(current->mm, start, end,
				&prot_none_walk_ops, &new_pgprot);
		if (error)
			return error;
	}

	/*
	 * If we make a private mapping writable we increase our commit;
	 * but (without finer accounting) cannot reduce our commit if we
	 * make it unwritable again. hugetlb mapping were accounted for
	 * even if read-only so there is no need to account for them here
	 */
	if (newflags & VM_WRITE) {
		/* Check space limits when area turns into data. */
		if (!may_expand_vm(mm, newflags, nrpages) &&
				may_expand_vm(mm, oldflags, nrpages))
			return -ENOMEM;
		if (!(oldflags & (VM_ACCOUNT|VM_WRITE|VM_HUGETLB|
						VM_SHARED|VM_NORESERVE))) {
			charged = nrpages;
			if (security_vm_enough_memory_mm(mm, charged))
				return -ENOMEM;
			newflags |= VM_ACCOUNT;
		}
	}

	/*
	 * First try to merge with previous and/or next vma.
	 */
	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*pprev = vma_merge(mm, *pprev, start, end, newflags,
			   vma->anon_vma, vma->vm_file, pgoff, vma_policy(vma),
			   vma->vm_userfaultfd_ctx, anon_vma_name(vma));
	if (*pprev) {
		vma = *pprev;
		VM_WARN_ON((vma->vm_flags ^ newflags) & ~VM_SOFTDIRTY);
		goto success;
	}

	*pprev = vma;

	if (start != vma->vm_start) {
		error = split_vma(mm, vma, start, 1);
		if (error)
			goto fail;
	}

	if (end != vma->vm_end) {
		error = split_vma(mm, vma, end, 0);
		if (error)
			goto fail;
	}

success:
	/*
	 * vm_flags and vm_page_prot are protected by the mmap_lock
	 * held in write mode.
	 */
	vma->vm_flags = newflags;
	/*
	 * We want to check manually if we can change individual PTEs writable
	 * if we can't do that automatically for all PTEs in a mapping. For
	 * private mappings, that's always the case when we have write
	 * permissions as we properly have to handle COW.
	 */
	if (vma->vm_flags & VM_SHARED)
		try_change_writable = vma_wants_writenotify(vma, vma->vm_page_prot);
	else
		try_change_writable = !!(vma->vm_flags & VM_WRITE);
	vma_set_page_prot(vma);

	change_protection(tlb, vma, start, end, vma->vm_page_prot,
			  try_change_writable ? MM_CP_TRY_CHANGE_WRITABLE : 0);

	/*
	 * Private VM_LOCKED VMA becoming writable: trigger COW to avoid major
	 * fault on access.
	 */
	if ((oldflags & (VM_WRITE | VM_SHARED | VM_LOCKED)) == VM_LOCKED &&
			(newflags & VM_WRITE)) {
		populate_vma_page_range(vma, start, end, NULL);
	}

	vm_stat_account(mm, oldflags, -nrpages);
	vm_stat_account(mm, newflags, nrpages);
	perf_event_mmap(vma);
	return 0;

fail:
	vm_unacct_memory(charged);
	return error;
}

/*
 * pkey==-1 when doing a legacy mprotect()
 */
static int do_mprotect_pkey(unsigned long start, size_t len,
		unsigned long prot, int pkey)
{
	unsigned long nstart, end, tmp, reqprot;
	struct vm_area_struct *vma, *prev;
	int error;
	const int grows = prot & (PROT_GROWSDOWN|PROT_GROWSUP);
	const bool rier = (current->personality & READ_IMPLIES_EXEC) &&
				(prot & PROT_READ);
	struct mmu_gather tlb;
	MA_STATE(mas, &current->mm->mm_mt, 0, 0);

	start = untagged_addr(start);

	prot &= ~(PROT_GROWSDOWN|PROT_GROWSUP);
	if (grows == (PROT_GROWSDOWN|PROT_GROWSUP)) /* can't be both */
		return -EINVAL;

	if (start & ~PAGE_MASK)
		return -EINVAL;
	if (!len)
		return 0;
	len = PAGE_ALIGN(len);
	end = start + len;
	if (end <= start)
		return -ENOMEM;
	if (!arch_validate_prot(prot, start))
		return -EINVAL;

	reqprot = prot;

	if (mmap_write_lock_killable(current->mm))
		return -EINTR;

	/*
	 * If userspace did not allocate the pkey, do not let
	 * them use it here.
	 */
	error = -EINVAL;
	if ((pkey != -1) && !mm_pkey_is_allocated(current->mm, pkey))
		goto out;

	mas_set(&mas, start);
	vma = mas_find(&mas, ULONG_MAX);
	error = -ENOMEM;
	if (!vma)
		goto out;

	if (unlikely(grows & PROT_GROWSDOWN)) {
		if (vma->vm_start >= end)
			goto out;
		start = vma->vm_start;
		error = -EINVAL;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out;
	} else {
		if (vma->vm_start > start)
			goto out;
		if (unlikely(grows & PROT_GROWSUP)) {
			end = vma->vm_end;
			error = -EINVAL;
			if (!(vma->vm_flags & VM_GROWSUP))
				goto out;
		}
	}

	if (start > vma->vm_start)
		prev = vma;
	else
		prev = mas_prev(&mas, 0);

	tlb_gather_mmu(&tlb, current->mm);
	for (nstart = start ; ; ) {
		unsigned long mask_off_old_flags;
		unsigned long newflags;
		int new_vma_pkey;

		/* Here we know that vma->vm_start <= nstart < vma->vm_end. */

		/* Does the application expect PROT_READ to imply PROT_EXEC */
		if (rier && (vma->vm_flags & VM_MAYEXEC))
			prot |= PROT_EXEC;

		/*
		 * Each mprotect() call explicitly passes r/w/x permissions.
		 * If a permission is not passed to mprotect(), it must be
		 * cleared from the VMA.
		 */
		mask_off_old_flags = VM_READ | VM_WRITE | VM_EXEC |
					VM_FLAGS_CLEAR;

		new_vma_pkey = arch_override_mprotect_pkey(vma, prot, pkey);
		newflags = calc_vm_prot_bits(prot, new_vma_pkey);
		newflags |= (vma->vm_flags & ~mask_off_old_flags);

		/* newflags >> 4 shift VM_MAY% in place of VM_% */
		if ((newflags & ~(newflags >> 4)) & VM_ACCESS_FLAGS) {
			error = -EACCES;
			break;
		}

		/* Allow architectures to sanity-check the new flags */
		if (!arch_validate_flags(newflags)) {
			error = -EINVAL;
			break;
		}

		error = security_file_mprotect(vma, reqprot, prot);
		if (error)
			break;

		tmp = vma->vm_end;
		if (tmp > end)
			tmp = end;

		if (vma->vm_ops && vma->vm_ops->mprotect) {
			error = vma->vm_ops->mprotect(vma, nstart, tmp, newflags);
			if (error)
				break;
		}

		error = mprotect_fixup(&tlb, vma, &prev, nstart, tmp, newflags);
		if (error)
			break;

		nstart = tmp;

		if (nstart < prev->vm_end)
			nstart = prev->vm_end;
		if (nstart >= end)
			break;

		vma = find_vma(current->mm, prev->vm_end);
		if (!vma || vma->vm_start != nstart) {
			error = -ENOMEM;
			break;
		}
		prot = reqprot;
	}
	tlb_finish_mmu(&tlb);
out:
	mmap_write_unlock(current->mm);
	return error;
}

SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len,
		unsigned long, prot)
{
	return do_mprotect_pkey(start, len, prot, -1);
}

#ifdef CONFIG_ARCH_HAS_PKEYS

SYSCALL_DEFINE4(pkey_mprotect, unsigned long, start, size_t, len,
		unsigned long, prot, int, pkey)
{
	return do_mprotect_pkey(start, len, prot, pkey);
}

SYSCALL_DEFINE2(pkey_alloc, unsigned long, flags, unsigned long, init_val)
{
	int pkey;
	int ret;

	/* No flags supported yet. */
	if (flags)
		return -EINVAL;
	/* check for unsupported init values */
	if (init_val & ~PKEY_ACCESS_MASK)
		return -EINVAL;

	mmap_write_lock(current->mm);
	pkey = mm_pkey_alloc(current->mm);

	ret = -ENOSPC;
	if (pkey == -1)
		goto out;

	ret = arch_set_user_pkey_access(current, pkey, init_val);
	if (ret) {
		mm_pkey_free(current->mm, pkey);
		goto out;
	}
	ret = pkey;
out:
	mmap_write_unlock(current->mm);
	return ret;
}

SYSCALL_DEFINE1(pkey_free, int, pkey)
{
	int ret;

	mmap_write_lock(current->mm);
	ret = mm_pkey_free(current->mm, pkey);
	mmap_write_unlock(current->mm);

	/*
	 * We could provide warnings or errors if any VMA still
	 * has the pkey set here.
	 */
	return ret;
}

#endif /* CONFIG_ARCH_HAS_PKEYS */
