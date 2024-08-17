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
#include <uapi/linux/mman.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>

#include "internal.h"

bool can_change_pte_writable(struct vm_area_struct *vma, unsigned long addr,
			     pte_t pte)
{
	struct page *page;

	if (WARN_ON_ONCE(!(vma->vm_flags & VM_WRITE)))
		return false;

	/* Don't touch entries that are not even readable. */
	if (pte_protnone(pte))
		return false;

	/* Do we need write faults for softdirty tracking? */
	if (pte_needs_soft_dirty_wp(vma, pte))
		return false;

	/* Do we need write faults for uffd-wp tracking? */
	if (userfaultfd_pte_wp(vma, pte))
		return false;

	if (!(vma->vm_flags & VM_SHARED)) {
		/*
		 * Writable MAP_PRIVATE mapping: We can only special-case on
		 * exclusive anonymous pages, because we know that our
		 * write-fault handler similarly would map them writable without
		 * any additional checks while holding the PT lock.
		 */
		page = vm_normal_page(vma, addr, pte);
		return page && PageAnon(page) && PageAnonExclusive(page);
	}

	VM_WARN_ON_ONCE(is_zero_pfn(pte_pfn(pte)) && pte_dirty(pte));

	/*
	 * Writable MAP_SHARED mapping: "clean" might indicate that the FS still
	 * needs a real write-fault for writenotify
	 * (see vma_wants_writenotify()). If "dirty", the assumption is that the
	 * FS was already notified and we can simply mark the PTE writable
	 * just like the write-fault handler would do.
	 */
	return pte_dirty(pte);
}

static long change_pte_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, pmd_t *pmd, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	pte_t *pte, oldpte;
	spinlock_t *ptl;
	long pages = 0;
	int target_node = NUMA_NO_NODE;
	bool prot_numa = cp_flags & MM_CP_PROT_NUMA;
	bool uffd_wp = cp_flags & MM_CP_UFFD_WP;
	bool uffd_wp_resolve = cp_flags & MM_CP_UFFD_WP_RESOLVE;

	tlb_change_page_size(tlb, PAGE_SIZE);
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (!pte)
		return -EAGAIN;

	/* Get target node for single threaded private VMAs */
	if (prot_numa && !(vma->vm_flags & VM_SHARED) &&
	    atomic_read(&vma->vm_mm->mm_users) == 1)
		target_node = numa_node_id();

	flush_tlb_batched_pending(vma->vm_mm);
	arch_enter_lazy_mmu_mode();
	do {
		oldpte = ptep_get(pte);
		if (pte_present(oldpte)) {
			pte_t ptent;

			/*
			 * Avoid trapping faults against the zero or KSM
			 * pages. See similar comment in change_huge_pmd.
			 */
			if (prot_numa) {
				struct folio *folio;
				int nid;
				bool toptier;

				/* Avoid TLB flush if possible */
				if (pte_protnone(oldpte))
					continue;

				folio = vm_normal_folio(vma, addr, oldpte);
				if (!folio || folio_is_zone_device(folio) ||
				    folio_test_ksm(folio))
					continue;

				/* Also skip shared copy-on-write pages */
				if (is_cow_mapping(vma->vm_flags) &&
				    (folio_maybe_dma_pinned(folio) ||
				     folio_likely_mapped_shared(folio)))
					continue;

				/*
				 * While migration can move some dirty pages,
				 * it cannot move them all from MIGRATE_ASYNC
				 * context.
				 */
				if (folio_is_file_lru(folio) &&
				    folio_test_dirty(folio))
					continue;

				/*
				 * Don't mess with PTEs if page is already on the node
				 * a single-threaded process is running on.
				 */
				nid = folio_nid(folio);
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
				if (folio_use_access_time(folio))
					folio_xchg_access_time(folio,
						jiffies_to_msecs(jiffies));
			}

			oldpte = ptep_modify_prot_start(vma, addr, pte);
			ptent = pte_modify(oldpte, newprot);

			if (uffd_wp)
				ptent = pte_mkuffd_wp(ptent);
			else if (uffd_wp_resolve)
				ptent = pte_clear_uffd_wp(ptent);

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
				ptent = pte_mkwrite(ptent, vma);

			ptep_modify_prot_commit(vma, addr, pte, oldpte, ptent);
			if (pte_needs_flush(oldpte, ptent))
				tlb_flush_pte_range(tlb, addr, PAGE_SIZE);
			pages++;
		} else if (is_swap_pte(oldpte)) {
			swp_entry_t entry = pte_to_swp_entry(oldpte);
			pte_t newpte;

			if (is_writable_migration_entry(entry)) {
				struct folio *folio = pfn_swap_entry_folio(entry);

				/*
				 * A protection check is difficult so
				 * just be safe and disable write
				 */
				if (folio_test_anon(folio))
					entry = make_readable_exclusive_migration_entry(
							     swp_offset(entry));
				else
					entry = make_readable_migration_entry(swp_offset(entry));
				newpte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(oldpte))
					newpte = pte_swp_mksoft_dirty(newpte);
			} else if (is_writable_device_private_entry(entry)) {
				/*
				 * We do not preserve soft-dirtiness. See
				 * copy_nonpresent_pte() for explanation.
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
			} else if (is_pte_marker_entry(entry)) {
				/*
				 * Ignore error swap entries unconditionally,
				 * because any access should sigbus anyway.
				 */
				if (is_poisoned_swp_entry(entry))
					continue;
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

			/*
			 * Nobody plays with any none ptes besides
			 * userfaultfd when applying the protections.
			 */
			if (likely(!uffd_wp))
				continue;

			if (userfaultfd_wp_use_markers(vma)) {
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
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);

	return pages;
}

/*
 * Return true if we want to split THPs into PTE mappings in change
 * protection procedure, false otherwise.
 */
static inline bool
pgtable_split_needed(struct vm_area_struct *vma, unsigned long cp_flags)
{
	/*
	 * pte markers only resides in pte level, if we need pte markers,
	 * we need to split.  For example, we cannot wr-protect a file thp
	 * (e.g. 2M shmem) because file thp is handled differently when
	 * split by erasing the pmd so far.
	 */
	return (cp_flags & MM_CP_UFFD_WP) && !vma_is_anonymous(vma);
}

/*
 * Return true if we want to populate pgtables in change protection
 * procedure, false otherwise
 */
static inline bool
pgtable_populate_needed(struct vm_area_struct *vma, unsigned long cp_flags)
{
	/* If not within ioctl(UFFDIO_WRITEPROTECT), then don't bother */
	if (!(cp_flags & MM_CP_UFFD_WP))
		return false;

	/* Populate if the userfaultfd mode requires pte markers */
	return userfaultfd_wp_use_markers(vma);
}

/*
 * Populate the pgtable underneath for whatever reason if requested.
 * When {pte|pmd|...}_alloc() failed we treat it the same way as pgtable
 * allocation failures during page faults by kicking OOM and returning
 * error.
 */
#define  change_pmd_prepare(vma, pmd, cp_flags)				\
	({								\
		long err = 0;						\
		if (unlikely(pgtable_populate_needed(vma, cp_flags))) {	\
			if (pte_alloc(vma->vm_mm, pmd))			\
				err = -ENOMEM;				\
		}							\
		err;							\
	})

/*
 * This is the general pud/p4d/pgd version of change_pmd_prepare(). We need to
 * have separate change_pmd_prepare() because pte_alloc() returns 0 on success,
 * while {pmd|pud|p4d}_alloc() returns the valid pointer on success.
 */
#define  change_prepare(vma, high, low, addr, cp_flags)			\
	  ({								\
		long err = 0;						\
		if (unlikely(pgtable_populate_needed(vma, cp_flags))) {	\
			low##_t *p = low##_alloc(vma->vm_mm, high, addr); \
			if (p == NULL)					\
				err = -ENOMEM;				\
		}							\
		err;							\
	})

static inline long change_pmd_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, pud_t *pud, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	pmd_t *pmd;
	unsigned long next;
	long pages = 0;
	unsigned long nr_huge_updates = 0;

	pmd = pmd_offset(pud, addr);
	do {
		long ret;
		pmd_t _pmd;
again:
		next = pmd_addr_end(addr, end);

		ret = change_pmd_prepare(vma, pmd, cp_flags);
		if (ret) {
			pages = ret;
			break;
		}

		if (pmd_none(*pmd))
			goto next;

		_pmd = pmdp_get_lockless(pmd);
		if (is_swap_pmd(_pmd) || pmd_trans_huge(_pmd) || pmd_devmap(_pmd)) {
			if ((next - addr != HPAGE_PMD_SIZE) ||
			    pgtable_split_needed(vma, cp_flags)) {
				__split_huge_pmd(vma, pmd, addr, false, NULL);
				/*
				 * For file-backed, the pmd could have been
				 * cleared; make sure pmd populated if
				 * necessary, then fall-through to pte level.
				 */
				ret = change_pmd_prepare(vma, pmd, cp_flags);
				if (ret) {
					pages = ret;
					break;
				}
			} else {
				ret = change_huge_pmd(tlb, vma, pmd,
						addr, newprot, cp_flags);
				if (ret) {
					if (ret == HPAGE_PMD_NR) {
						pages += HPAGE_PMD_NR;
						nr_huge_updates++;
					}

					/* huge pmd was handled */
					goto next;
				}
			}
			/* fall through, the trans huge pmd just split */
		}

		ret = change_pte_range(tlb, vma, pmd, addr, next, newprot,
				       cp_flags);
		if (ret < 0)
			goto again;
		pages += ret;
next:
		cond_resched();
	} while (pmd++, addr = next, addr != end);

	if (nr_huge_updates)
		count_vm_numa_events(NUMA_HUGE_PTE_UPDATES, nr_huge_updates);
	return pages;
}

static inline long change_pud_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, p4d_t *p4d, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	struct mmu_notifier_range range;
	pud_t *pudp, pud;
	unsigned long next;
	long pages = 0, ret;

	range.start = 0;

	pudp = pud_offset(p4d, addr);
	do {
again:
		next = pud_addr_end(addr, end);
		ret = change_prepare(vma, pudp, pmd, addr, cp_flags);
		if (ret) {
			pages = ret;
			break;
		}

		pud = READ_ONCE(*pudp);
		if (pud_none(pud))
			continue;

		if (!range.start) {
			mmu_notifier_range_init(&range,
						MMU_NOTIFY_PROTECTION_VMA, 0,
						vma->vm_mm, addr, end);
			mmu_notifier_invalidate_range_start(&range);
		}

		if (pud_leaf(pud)) {
			if ((next - addr != PUD_SIZE) ||
			    pgtable_split_needed(vma, cp_flags)) {
				__split_huge_pud(vma, pudp, addr);
				goto again;
			} else {
				ret = change_huge_pud(tlb, vma, pudp,
						      addr, newprot, cp_flags);
				if (ret == 0)
					goto again;
				/* huge pud was handled */
				if (ret == HPAGE_PUD_NR)
					pages += HPAGE_PUD_NR;
				continue;
			}
		}

		pages += change_pmd_range(tlb, vma, pudp, addr, next, newprot,
					  cp_flags);
	} while (pudp++, addr = next, addr != end);

	if (range.start)
		mmu_notifier_invalidate_range_end(&range);

	return pages;
}

static inline long change_p4d_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, pgd_t *pgd, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	p4d_t *p4d;
	unsigned long next;
	long pages = 0, ret;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		ret = change_prepare(vma, p4d, pud, addr, cp_flags);
		if (ret)
			return ret;
		if (p4d_none_or_clear_bad(p4d))
			continue;
		pages += change_pud_range(tlb, vma, p4d, addr, next, newprot,
					  cp_flags);
	} while (p4d++, addr = next, addr != end);

	return pages;
}

static long change_protection_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long addr,
		unsigned long end, pgprot_t newprot, unsigned long cp_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	unsigned long next;
	long pages = 0, ret;

	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	tlb_start_vma(tlb, vma);
	do {
		next = pgd_addr_end(addr, end);
		ret = change_prepare(vma, pgd, p4d, addr, cp_flags);
		if (ret) {
			pages = ret;
			break;
		}
		if (pgd_none_or_clear_bad(pgd))
			continue;
		pages += change_p4d_range(tlb, vma, pgd, addr, next, newprot,
					  cp_flags);
	} while (pgd++, addr = next, addr != end);

	tlb_end_vma(tlb, vma);

	return pages;
}

long change_protection(struct mmu_gather *tlb,
		       struct vm_area_struct *vma, unsigned long start,
		       unsigned long end, unsigned long cp_flags)
{
	pgprot_t newprot = vma->vm_page_prot;
	long pages;

	BUG_ON((cp_flags & MM_CP_UFFD_WP_ALL) == MM_CP_UFFD_WP_ALL);

#ifdef CONFIG_NUMA_BALANCING
	/*
	 * Ordinary protection updates (mprotect, uffd-wp, softdirty tracking)
	 * are expected to reflect their requirements via VMA flags such that
	 * vma_set_page_prot() will adjust vma->vm_page_prot accordingly.
	 */
	if (cp_flags & MM_CP_PROT_NUMA)
		newprot = PAGE_NONE;
#else
	WARN_ON_ONCE(cp_flags & MM_CP_PROT_NUMA);
#endif

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
	return pfn_modify_allowed(pte_pfn(ptep_get(pte)),
				  *(pgprot_t *)(walk->private)) ?
		0 : -EACCES;
}

static int prot_none_hugetlb_entry(pte_t *pte, unsigned long hmask,
				   unsigned long addr, unsigned long next,
				   struct mm_walk *walk)
{
	return pfn_modify_allowed(pte_pfn(ptep_get(pte)),
				  *(pgprot_t *)(walk->private)) ?
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
	.walk_lock		= PGWALK_WRLOCK,
};

int
mprotect_fixup(struct vma_iterator *vmi, struct mmu_gather *tlb,
	       struct vm_area_struct *vma, struct vm_area_struct **pprev,
	       unsigned long start, unsigned long end, unsigned long newflags)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long oldflags = vma->vm_flags;
	long nrpages = (end - start) >> PAGE_SHIFT;
	unsigned int mm_cp_flags = 0;
	unsigned long charged = 0;
	int error;

	if (!can_modify_vma(vma))
		return -EPERM;

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
	 * make it unwritable again except in the anonymous case where no
	 * anon_vma has yet to be assigned.
	 *
	 * hugetlb mapping were accounted for even if read-only so there is
	 * no need to account for them here.
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
	} else if ((oldflags & VM_ACCOUNT) && vma_is_anonymous(vma) &&
		   !vma->anon_vma) {
		newflags &= ~VM_ACCOUNT;
	}

	vma = vma_modify_flags(vmi, *pprev, vma, start, end, newflags);
	if (IS_ERR(vma)) {
		error = PTR_ERR(vma);
		goto fail;
	}

	*pprev = vma;

	/*
	 * vm_flags and vm_page_prot are protected by the mmap_lock
	 * held in write mode.
	 */
	vma_start_write(vma);
	vm_flags_reset(vma, newflags);
	if (vma_wants_manual_pte_write_upgrade(vma))
		mm_cp_flags |= MM_CP_TRY_CHANGE_WRITABLE;
	vma_set_page_prot(vma);

	change_protection(tlb, vma, start, end, mm_cp_flags);

	if ((oldflags & VM_ACCOUNT) && !(newflags & VM_ACCOUNT))
		vm_unacct_memory(nrpages);

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
	struct vma_iterator vmi;

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

	vma_iter_init(&vmi, current->mm, start);
	vma = vma_find(&vmi, end);
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

	prev = vma_prev(&vmi);
	if (start > vma->vm_start)
		prev = vma;

	tlb_gather_mmu(&tlb, current->mm);
	nstart = start;
	tmp = vma->vm_start;
	for_each_vma_range(vmi, vma, end) {
		unsigned long mask_off_old_flags;
		unsigned long newflags;
		int new_vma_pkey;

		if (vma->vm_start != tmp) {
			error = -ENOMEM;
			break;
		}

		/* Does the application expect PROT_READ to imply PROT_EXEC */
		if (rier && (vma->vm_flags & VM_MAYEXEC))
			prot |= PROT_EXEC;

		/*
		 * Each mprotect() call explicitly passes r/w/x permissions.
		 * If a permission is not passed to mprotect(), it must be
		 * cleared from the VMA.
		 */
		mask_off_old_flags = VM_ACCESS_FLAGS | VM_FLAGS_CLEAR;

		new_vma_pkey = arch_override_mprotect_pkey(vma, prot, pkey);
		newflags = calc_vm_prot_bits(prot, new_vma_pkey);
		newflags |= (vma->vm_flags & ~mask_off_old_flags);

		/* newflags >> 4 shift VM_MAY% in place of VM_% */
		if ((newflags & ~(newflags >> 4)) & VM_ACCESS_FLAGS) {
			error = -EACCES;
			break;
		}

		if (map_deny_write_exec(vma, newflags)) {
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

		error = mprotect_fixup(&vmi, &tlb, vma, &prev, nstart, tmp, newflags);
		if (error)
			break;

		tmp = vma_iter_end(&vmi);
		nstart = tmp;
		prot = reqprot;
	}
	tlb_finish_mmu(&tlb);

	if (!error && tmp < end)
		error = -ENOMEM;

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
