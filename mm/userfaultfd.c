/*
 *  mm/userfaultfd.c
 *
 *  Copyright (C) 2015  Red Hat, Inc.
 *
 *  This work is licensed under the terms of the GNU GPL, version 2. See
 *  the COPYING file in the top-level directory.
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/userfaultfd_k.h>
#include <linux/mmu_notifier.h>
#include <asm/tlbflush.h>
#include "internal.h"

static int mcopy_atomic_pte(struct mm_struct *dst_mm,
			    pmd_t *dst_pmd,
			    struct vm_area_struct *dst_vma,
			    unsigned long dst_addr,
			    unsigned long src_addr,
			    struct page **pagep)
{
	struct mem_cgroup *memcg;
	pte_t _dst_pte, *dst_pte;
	spinlock_t *ptl;
	void *page_kaddr;
	int ret;
	struct page *page;

	if (!*pagep) {
		ret = -ENOMEM;
		page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, dst_vma, dst_addr);
		if (!page)
			goto out;

		page_kaddr = kmap_atomic(page);
		ret = copy_from_user(page_kaddr,
				     (const void __user *) src_addr,
				     PAGE_SIZE);
		kunmap_atomic(page_kaddr);

		/* fallback to copy_from_user outside mmap_sem */
		if (unlikely(ret)) {
			ret = -EFAULT;
			*pagep = page;
			/* don't free the page */
			goto out;
		}
	} else {
		page = *pagep;
		*pagep = NULL;
	}

	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * preceeding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__SetPageUptodate(page);

	ret = -ENOMEM;
	if (mem_cgroup_try_charge(page, dst_mm, GFP_KERNEL, &memcg, false))
		goto out_release;

	_dst_pte = mk_pte(page, dst_vma->vm_page_prot);
	if (dst_vma->vm_flags & VM_WRITE)
		_dst_pte = pte_mkwrite(pte_mkdirty(_dst_pte));

	ret = -EEXIST;
	dst_pte = pte_offset_map_lock(dst_mm, dst_pmd, dst_addr, &ptl);
	if (!pte_none(*dst_pte))
		goto out_release_uncharge_unlock;

	inc_mm_counter(dst_mm, MM_ANONPAGES);
	page_add_new_anon_rmap(page, dst_vma, dst_addr, false);
	mem_cgroup_commit_charge(page, memcg, false, false);
	lru_cache_add_active_or_unevictable(page, dst_vma);

	set_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(dst_vma, dst_addr, dst_pte);

	pte_unmap_unlock(dst_pte, ptl);
	ret = 0;
out:
	return ret;
out_release_uncharge_unlock:
	pte_unmap_unlock(dst_pte, ptl);
	mem_cgroup_cancel_charge(page, memcg, false);
out_release:
	page_cache_release(page);
	goto out;
}

static int mfill_zeropage_pte(struct mm_struct *dst_mm,
			      pmd_t *dst_pmd,
			      struct vm_area_struct *dst_vma,
			      unsigned long dst_addr)
{
	pte_t _dst_pte, *dst_pte;
	spinlock_t *ptl;
	int ret;

	_dst_pte = pte_mkspecial(pfn_pte(my_zero_pfn(dst_addr),
					 dst_vma->vm_page_prot));
	ret = -EEXIST;
	dst_pte = pte_offset_map_lock(dst_mm, dst_pmd, dst_addr, &ptl);
	if (!pte_none(*dst_pte))
		goto out_unlock;
	set_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte);
	/* No need to invalidate - it was non-present before */
	update_mmu_cache(dst_vma, dst_addr, dst_pte);
	ret = 0;
out_unlock:
	pte_unmap_unlock(dst_pte, ptl);
	return ret;
}

static pmd_t *mm_alloc_pmd(struct mm_struct *mm, unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, address);
	pud = pud_alloc(mm, pgd, address);
	if (pud)
		/*
		 * Note that we didn't run this because the pmd was
		 * missing, the *pmd may be already established and in
		 * turn it may also be a trans_huge_pmd.
		 */
		pmd = pmd_alloc(mm, pud, address);
	return pmd;
}

static __always_inline ssize_t __mcopy_atomic(struct mm_struct *dst_mm,
					      unsigned long dst_start,
					      unsigned long src_start,
					      unsigned long len,
					      bool zeropage)
{
	struct vm_area_struct *dst_vma;
	ssize_t err;
	pmd_t *dst_pmd;
	unsigned long src_addr, dst_addr;
	long copied;
	struct page *page;

	/*
	 * Sanitize the command parameters:
	 */
	BUG_ON(dst_start & ~PAGE_MASK);
	BUG_ON(len & ~PAGE_MASK);

	/* Does the address range wrap, or is the span zero-sized? */
	BUG_ON(src_start + len <= src_start);
	BUG_ON(dst_start + len <= dst_start);

	src_addr = src_start;
	dst_addr = dst_start;
	copied = 0;
	page = NULL;
retry:
	down_read(&dst_mm->mmap_sem);

	/*
	 * Make sure the vma is not shared, that the dst range is
	 * both valid and fully within a single existing vma.
	 */
	err = -EINVAL;
	dst_vma = find_vma(dst_mm, dst_start);
	if (!dst_vma || (dst_vma->vm_flags & VM_SHARED))
		goto out_unlock;
	if (dst_start < dst_vma->vm_start ||
	    dst_start + len > dst_vma->vm_end)
		goto out_unlock;

	/*
	 * Be strict and only allow __mcopy_atomic on userfaultfd
	 * registered ranges to prevent userland errors going
	 * unnoticed. As far as the VM consistency is concerned, it
	 * would be perfectly safe to remove this check, but there's
	 * no useful usage for __mcopy_atomic ouside of userfaultfd
	 * registered ranges. This is after all why these are ioctls
	 * belonging to the userfaultfd and not syscalls.
	 */
	if (!dst_vma->vm_userfaultfd_ctx.ctx)
		goto out_unlock;

	/*
	 * FIXME: only allow copying on anonymous vmas, tmpfs should
	 * be added.
	 */
	if (dst_vma->vm_ops)
		goto out_unlock;

	/*
	 * Ensure the dst_vma has a anon_vma or this page
	 * would get a NULL anon_vma when moved in the
	 * dst_vma.
	 */
	err = -ENOMEM;
	if (unlikely(anon_vma_prepare(dst_vma)))
		goto out_unlock;

	while (src_addr < src_start + len) {
		pmd_t dst_pmdval;

		BUG_ON(dst_addr >= dst_start + len);

		dst_pmd = mm_alloc_pmd(dst_mm, dst_addr);
		if (unlikely(!dst_pmd)) {
			err = -ENOMEM;
			break;
		}

		dst_pmdval = pmd_read_atomic(dst_pmd);
		/*
		 * If the dst_pmd is mapped as THP don't
		 * override it and just be strict.
		 */
		if (unlikely(pmd_trans_huge(dst_pmdval))) {
			err = -EEXIST;
			break;
		}
		if (unlikely(pmd_none(dst_pmdval)) &&
		    unlikely(__pte_alloc(dst_mm, dst_pmd, dst_addr))) {
			err = -ENOMEM;
			break;
		}
		/* If an huge pmd materialized from under us fail */
		if (unlikely(pmd_trans_huge(*dst_pmd))) {
			err = -EFAULT;
			break;
		}

		BUG_ON(pmd_none(*dst_pmd));
		BUG_ON(pmd_trans_huge(*dst_pmd));

		if (!zeropage)
			err = mcopy_atomic_pte(dst_mm, dst_pmd, dst_vma,
					       dst_addr, src_addr, &page);
		else
			err = mfill_zeropage_pte(dst_mm, dst_pmd, dst_vma,
						 dst_addr);

		cond_resched();

		if (unlikely(err == -EFAULT)) {
			void *page_kaddr;

			up_read(&dst_mm->mmap_sem);
			BUG_ON(!page);

			page_kaddr = kmap(page);
			err = copy_from_user(page_kaddr,
					     (const void __user *) src_addr,
					     PAGE_SIZE);
			kunmap(page);
			if (unlikely(err)) {
				err = -EFAULT;
				goto out;
			}
			goto retry;
		} else
			BUG_ON(page);

		if (!err) {
			dst_addr += PAGE_SIZE;
			src_addr += PAGE_SIZE;
			copied += PAGE_SIZE;

			if (fatal_signal_pending(current))
				err = -EINTR;
		}
		if (err)
			break;
	}

out_unlock:
	up_read(&dst_mm->mmap_sem);
out:
	if (page)
		page_cache_release(page);
	BUG_ON(copied < 0);
	BUG_ON(err > 0);
	BUG_ON(!copied && !err);
	return copied ? copied : err;
}

ssize_t mcopy_atomic(struct mm_struct *dst_mm, unsigned long dst_start,
		     unsigned long src_start, unsigned long len)
{
	return __mcopy_atomic(dst_mm, dst_start, src_start, len, false);
}

ssize_t mfill_zeropage(struct mm_struct *dst_mm, unsigned long start,
		       unsigned long len)
{
	return __mcopy_atomic(dst_mm, start, 0, len, true);
}
