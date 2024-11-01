// SPDX-License-Identifier: GPL-2.0-only
/*
 *  mm/userfaultfd.c
 *
 *  Copyright (C) 2015  Red Hat, Inc.
 */

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/userfaultfd_k.h>
#include <linux/mmu_notifier.h>
#include <linux/hugetlb.h>
#include <linux/shmem_fs.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include "internal.h"

static __always_inline
bool validate_dst_vma(struct vm_area_struct *dst_vma, unsigned long dst_end)
{
	/* Make sure that the dst range is fully within dst_vma. */
	if (dst_end > dst_vma->vm_end)
		return false;

	/*
	 * Check the vma is registered in uffd, this is required to
	 * enforce the VM_MAYWRITE check done at uffd registration
	 * time.
	 */
	if (!dst_vma->vm_userfaultfd_ctx.ctx)
		return false;

	return true;
}

static __always_inline
struct vm_area_struct *find_vma_and_prepare_anon(struct mm_struct *mm,
						 unsigned long addr)
{
	struct vm_area_struct *vma;

	mmap_assert_locked(mm);
	vma = vma_lookup(mm, addr);
	if (!vma)
		vma = ERR_PTR(-ENOENT);
	else if (!(vma->vm_flags & VM_SHARED) &&
		 unlikely(anon_vma_prepare(vma)))
		vma = ERR_PTR(-ENOMEM);

	return vma;
}

#ifdef CONFIG_PER_VMA_LOCK
/*
 * lock_vma() - Lookup and lock vma corresponding to @address.
 * @mm: mm to search vma in.
 * @address: address that the vma should contain.
 *
 * Should be called without holding mmap_lock. vma should be unlocked after use
 * with unlock_vma().
 *
 * Return: A locked vma containing @address, -ENOENT if no vma is found, or
 * -ENOMEM if anon_vma couldn't be allocated.
 */
static struct vm_area_struct *lock_vma(struct mm_struct *mm,
				       unsigned long address)
{
	struct vm_area_struct *vma;

	vma = lock_vma_under_rcu(mm, address);
	if (vma) {
		/*
		 * lock_vma_under_rcu() only checks anon_vma for private
		 * anonymous mappings. But we need to ensure it is assigned in
		 * private file-backed vmas as well.
		 */
		if (!(vma->vm_flags & VM_SHARED) && unlikely(!vma->anon_vma))
			vma_end_read(vma);
		else
			return vma;
	}

	mmap_read_lock(mm);
	vma = find_vma_and_prepare_anon(mm, address);
	if (!IS_ERR(vma)) {
		/*
		 * We cannot use vma_start_read() as it may fail due to
		 * false locked (see comment in vma_start_read()). We
		 * can avoid that by directly locking vm_lock under
		 * mmap_lock, which guarantees that nobody can lock the
		 * vma for write (vma_start_write()) under us.
		 */
		down_read(&vma->vm_lock->lock);
	}

	mmap_read_unlock(mm);
	return vma;
}

static struct vm_area_struct *uffd_mfill_lock(struct mm_struct *dst_mm,
					      unsigned long dst_start,
					      unsigned long len)
{
	struct vm_area_struct *dst_vma;

	dst_vma = lock_vma(dst_mm, dst_start);
	if (IS_ERR(dst_vma) || validate_dst_vma(dst_vma, dst_start + len))
		return dst_vma;

	vma_end_read(dst_vma);
	return ERR_PTR(-ENOENT);
}

static void uffd_mfill_unlock(struct vm_area_struct *vma)
{
	vma_end_read(vma);
}

#else

static struct vm_area_struct *uffd_mfill_lock(struct mm_struct *dst_mm,
					      unsigned long dst_start,
					      unsigned long len)
{
	struct vm_area_struct *dst_vma;

	mmap_read_lock(dst_mm);
	dst_vma = find_vma_and_prepare_anon(dst_mm, dst_start);
	if (IS_ERR(dst_vma))
		goto out_unlock;

	if (validate_dst_vma(dst_vma, dst_start + len))
		return dst_vma;

	dst_vma = ERR_PTR(-ENOENT);
out_unlock:
	mmap_read_unlock(dst_mm);
	return dst_vma;
}

static void uffd_mfill_unlock(struct vm_area_struct *vma)
{
	mmap_read_unlock(vma->vm_mm);
}
#endif

/*
 * Install PTEs, to map dst_addr (within dst_vma) to page.
 *
 * This function handles both MCOPY_ATOMIC_NORMAL and _CONTINUE for both shmem
 * and anon, and for both shared and private VMAs.
 */
int mfill_atomic_install_pte(struct mm_struct *dst_mm, pmd_t *dst_pmd,
			     struct vm_area_struct *dst_vma,
			     unsigned long dst_addr, struct page *page,
			     bool newly_allocated, bool wp_copy)
{
	int ret;
	pte_t _dst_pte, *dst_pte;
	bool writable = dst_vma->vm_flags & VM_WRITE;
	bool vm_shared = dst_vma->vm_flags & VM_SHARED;
	bool page_in_cache = page_mapping(page);
	spinlock_t *ptl;
	struct inode *inode;
	pgoff_t offset, max_off;

	_dst_pte = mk_pte(page, dst_vma->vm_page_prot);
	_dst_pte = pte_mkdirty(_dst_pte);
	if (page_in_cache && !vm_shared)
		writable = false;

	/*
	 * Always mark a PTE as write-protected when needed, regardless of
	 * VM_WRITE, which the user might change.
	 */
	if (wp_copy) {
		_dst_pte = pte_mkuffd_wp(_dst_pte);
		writable = false;
	}

	if (writable)
		_dst_pte = pte_mkwrite(_dst_pte);
	else
		/*
		 * We need this to make sure write bit removed; as mk_pte()
		 * could return a pte with write bit set.
		 */
		_dst_pte = pte_wrprotect(_dst_pte);

	dst_pte = pte_offset_map_lock(dst_mm, dst_pmd, dst_addr, &ptl);

	if (vma_is_shmem(dst_vma)) {
		/* serialize against truncate with the page table lock */
		inode = dst_vma->vm_file->f_inode;
		offset = linear_page_index(dst_vma, dst_addr);
		max_off = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
		ret = -EFAULT;
		if (unlikely(offset >= max_off))
			goto out_unlock;
	}

	ret = -EEXIST;
	/*
	 * We allow to overwrite a pte marker: consider when both MISSING|WP
	 * registered, we firstly wr-protect a none pte which has no page cache
	 * page backing it, then access the page.
	 */
	if (!pte_none_mostly(*dst_pte))
		goto out_unlock;

	if (page_in_cache) {
		/* Usually, cache pages are already added to LRU */
		if (newly_allocated)
			lru_cache_add(page);
		page_add_file_rmap(page, dst_vma, false);
	} else {
		page_add_new_anon_rmap(page, dst_vma, dst_addr);
		lru_cache_add_inactive_or_unevictable(page, dst_vma);
	}

	/*
	 * Must happen after rmap, as mm_counter() checks mapping (via
	 * PageAnon()), which is set by __page_set_anon_rmap().
	 */
	inc_mm_counter(dst_mm, mm_counter(page));

	set_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(dst_vma, dst_addr, dst_pte);
	ret = 0;
out_unlock:
	pte_unmap_unlock(dst_pte, ptl);
	return ret;
}

static int mcopy_atomic_pte(struct mm_struct *dst_mm,
			    pmd_t *dst_pmd,
			    struct vm_area_struct *dst_vma,
			    unsigned long dst_addr,
			    unsigned long src_addr,
			    struct page **pagep,
			    bool wp_copy)
{
	void *page_kaddr;
	int ret;
	struct page *page;

	if (!*pagep) {
		ret = -ENOMEM;
		page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, dst_vma, dst_addr);
		if (!page)
			goto out;

		page_kaddr = kmap_local_page(page);
		/*
		 * The read mmap_lock is held here.  Despite the
		 * mmap_lock being read recursive a deadlock is still
		 * possible if a writer has taken a lock.  For example:
		 *
		 * process A thread 1 takes read lock on own mmap_lock
		 * process A thread 2 calls mmap, blocks taking write lock
		 * process B thread 1 takes page fault, read lock on own mmap lock
		 * process B thread 2 calls mmap, blocks taking write lock
		 * process A thread 1 blocks taking read lock on process B
		 * process B thread 1 blocks taking read lock on process A
		 *
		 * Disable page faults to prevent potential deadlock
		 * and retry the copy outside the mmap_lock.
		 */
		pagefault_disable();
		ret = copy_from_user(page_kaddr,
				     (const void __user *) src_addr,
				     PAGE_SIZE);
		pagefault_enable();
		kunmap_local(page_kaddr);

		/* fallback to copy_from_user outside mmap_lock */
		if (unlikely(ret)) {
			ret = -ENOENT;
			*pagep = page;
			/* don't free the page */
			goto out;
		}

		flush_dcache_page(page);
	} else {
		page = *pagep;
		*pagep = NULL;
	}

	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * preceding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__SetPageUptodate(page);

	ret = -ENOMEM;
	if (mem_cgroup_charge(page_folio(page), dst_mm, GFP_KERNEL))
		goto out_release;

	ret = mfill_atomic_install_pte(dst_mm, dst_pmd, dst_vma, dst_addr,
				       page, true, wp_copy);
	if (ret)
		goto out_release;
out:
	return ret;
out_release:
	put_page(page);
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
	pgoff_t offset, max_off;
	struct inode *inode;

	_dst_pte = pte_mkspecial(pfn_pte(my_zero_pfn(dst_addr),
					 dst_vma->vm_page_prot));
	dst_pte = pte_offset_map_lock(dst_mm, dst_pmd, dst_addr, &ptl);
	if (dst_vma->vm_file) {
		/* the shmem MAP_PRIVATE case requires checking the i_size */
		inode = dst_vma->vm_file->f_inode;
		offset = linear_page_index(dst_vma, dst_addr);
		max_off = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
		ret = -EFAULT;
		if (unlikely(offset >= max_off))
			goto out_unlock;
	}
	ret = -EEXIST;
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

/* Handles UFFDIO_CONTINUE for all shmem VMAs (shared or private). */
static int mcontinue_atomic_pte(struct mm_struct *dst_mm,
				pmd_t *dst_pmd,
				struct vm_area_struct *dst_vma,
				unsigned long dst_addr,
				bool wp_copy)
{
	struct inode *inode = file_inode(dst_vma->vm_file);
	pgoff_t pgoff = linear_page_index(dst_vma, dst_addr);
	struct folio *folio;
	struct page *page;
	int ret;

	ret = shmem_get_folio(inode, pgoff, &folio, SGP_NOALLOC);
	/* Our caller expects us to return -EFAULT if we failed to find folio */
	if (ret == -ENOENT)
		ret = -EFAULT;
	if (ret)
		goto out;
	if (!folio) {
		ret = -EFAULT;
		goto out;
	}

	page = folio_file_page(folio, pgoff);
	if (PageHWPoison(page)) {
		ret = -EIO;
		goto out_release;
	}

	ret = mfill_atomic_install_pte(dst_mm, dst_pmd, dst_vma, dst_addr,
				       page, false, wp_copy);
	if (ret)
		goto out_release;

	folio_unlock(folio);
	ret = 0;
out:
	return ret;
out_release:
	folio_unlock(folio);
	folio_put(folio);
	goto out;
}

static pmd_t *mm_alloc_pmd(struct mm_struct *mm, unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;

	pgd = pgd_offset(mm, address);
	p4d = p4d_alloc(mm, pgd, address);
	if (!p4d)
		return NULL;
	pud = pud_alloc(mm, p4d, address);
	if (!pud)
		return NULL;
	/*
	 * Note that we didn't run this because the pmd was
	 * missing, the *pmd may be already established and in
	 * turn it may also be a trans_huge_pmd.
	 */
	return pmd_alloc(mm, pud, address);
}

#ifdef CONFIG_HUGETLB_PAGE
/*
 * __mcopy_atomic processing for HUGETLB vmas.  Note that this routine is
 * called with either vma-lock or mmap_lock held, it will release the lock
 * before returning.
 */
static __always_inline ssize_t __mcopy_atomic_hugetlb(
					      struct userfaultfd_ctx *ctx,
					      struct vm_area_struct *dst_vma,
					      unsigned long dst_start,
					      unsigned long src_start,
					      unsigned long len,
					      enum mcopy_atomic_mode mode,
					      bool wp_copy)
{
	struct mm_struct *dst_mm = dst_vma->vm_mm;
	ssize_t err;
	pte_t *dst_pte;
	unsigned long src_addr, dst_addr;
	long copied;
	struct page *page;
	unsigned long vma_hpagesize;
	pgoff_t idx;
	u32 hash;
	struct address_space *mapping;

	/*
	 * There is no default zero huge page for all huge page sizes as
	 * supported by hugetlb.  A PMD_SIZE huge pages may exist as used
	 * by THP.  Since we can not reliably insert a zero page, this
	 * feature is not supported.
	 */
	if (mode == MCOPY_ATOMIC_ZEROPAGE) {
		up_read(&ctx->map_changing_lock);
		uffd_mfill_unlock(dst_vma);
		return -EINVAL;
	}

	src_addr = src_start;
	dst_addr = dst_start;
	copied = 0;
	page = NULL;
	vma_hpagesize = vma_kernel_pagesize(dst_vma);

	/*
	 * Validate alignment based on huge page size
	 */
	err = -EINVAL;
	if (dst_start & (vma_hpagesize - 1) || len & (vma_hpagesize - 1))
		goto out_unlock;

retry:
	/*
	 * On routine entry dst_vma is set.  If we had to drop mmap_lock and
	 * retry, dst_vma will be set to NULL and we must lookup again.
	 */
	if (!dst_vma) {
		dst_vma = uffd_mfill_lock(dst_mm, dst_start, len);
		if (IS_ERR(dst_vma)) {
			err = PTR_ERR(dst_vma);
			goto out;
		}

		err = -ENOENT;
		if (!is_vm_hugetlb_page(dst_vma))
			goto out_unlock_vma;

		err = -EINVAL;
		if (vma_hpagesize != vma_kernel_pagesize(dst_vma))
			goto out_unlock_vma;

		/*
		 * If memory mappings are changing because of non-cooperative
		 * operation (e.g. mremap) running in parallel, bail out and
		 * request the user to retry later
		 */
		down_read(&ctx->map_changing_lock);
		err = -EAGAIN;
		if (atomic_read(&ctx->mmap_changing))
			goto out_unlock;
	}

	while (src_addr < src_start + len) {
		BUG_ON(dst_addr >= dst_start + len);

		/*
		 * Serialize via vma_lock and hugetlb_fault_mutex.
		 * vma_lock ensures the dst_pte remains valid even
		 * in the case of shared pmds.  fault mutex prevents
		 * races with other faulting threads.
		 */
		idx = linear_page_index(dst_vma, dst_addr);
		mapping = dst_vma->vm_file->f_mapping;
		hash = hugetlb_fault_mutex_hash(mapping, idx);
		mutex_lock(&hugetlb_fault_mutex_table[hash]);
		hugetlb_vma_lock_read(dst_vma);

		err = -ENOMEM;
		dst_pte = huge_pte_alloc(dst_mm, dst_vma, dst_addr, vma_hpagesize);
		if (!dst_pte) {
			hugetlb_vma_unlock_read(dst_vma);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			goto out_unlock;
		}

		if (mode != MCOPY_ATOMIC_CONTINUE &&
		    !huge_pte_none_mostly(huge_ptep_get(dst_pte))) {
			err = -EEXIST;
			hugetlb_vma_unlock_read(dst_vma);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			goto out_unlock;
		}

		err = hugetlb_mcopy_atomic_pte(dst_mm, dst_pte, dst_vma,
					       dst_addr, src_addr, mode, &page,
					       wp_copy);

		hugetlb_vma_unlock_read(dst_vma);
		mutex_unlock(&hugetlb_fault_mutex_table[hash]);

		cond_resched();

		if (unlikely(err == -ENOENT)) {
			up_read(&ctx->map_changing_lock);
			uffd_mfill_unlock(dst_vma);
			BUG_ON(!page);

			err = copy_huge_page_from_user(page,
						(const void __user *)src_addr,
						vma_hpagesize / PAGE_SIZE,
						true);
			if (unlikely(err)) {
				err = -EFAULT;
				goto out;
			}

			dst_vma = NULL;
			goto retry;
		} else
			BUG_ON(page);

		if (!err) {
			dst_addr += vma_hpagesize;
			src_addr += vma_hpagesize;
			copied += vma_hpagesize;

			if (fatal_signal_pending(current))
				err = -EINTR;
		}
		if (err)
			break;
	}

out_unlock:
	up_read(&ctx->map_changing_lock);
out_unlock_vma:
	uffd_mfill_unlock(dst_vma);
out:
	if (page)
		put_page(page);
	BUG_ON(copied < 0);
	BUG_ON(err > 0);
	BUG_ON(!copied && !err);
	return copied ? copied : err;
}
#else /* !CONFIG_HUGETLB_PAGE */
/* fail at build time if gcc attempts to use this */
extern ssize_t __mcopy_atomic_hugetlb(struct userfaultfd_ctx *ctx,
				      struct vm_area_struct *dst_vma,
				      unsigned long dst_start,
				      unsigned long src_start,
				      unsigned long len,
				      enum mcopy_atomic_mode mode,
				      bool wp_copy);
#endif /* CONFIG_HUGETLB_PAGE */

static __always_inline ssize_t mfill_atomic_pte(struct mm_struct *dst_mm,
						pmd_t *dst_pmd,
						struct vm_area_struct *dst_vma,
						unsigned long dst_addr,
						unsigned long src_addr,
						struct page **page,
						enum mcopy_atomic_mode mode,
						bool wp_copy)
{
	ssize_t err;

	if (mode == MCOPY_ATOMIC_CONTINUE) {
		return mcontinue_atomic_pte(dst_mm, dst_pmd, dst_vma, dst_addr,
					    wp_copy);
	}

	/*
	 * The normal page fault path for a shmem will invoke the
	 * fault, fill the hole in the file and COW it right away. The
	 * result generates plain anonymous memory. So when we are
	 * asked to fill an hole in a MAP_PRIVATE shmem mapping, we'll
	 * generate anonymous memory directly without actually filling
	 * the hole. For the MAP_PRIVATE case the robustness check
	 * only happens in the pagetable (to verify it's still none)
	 * and not in the radix tree.
	 */
	if (!(dst_vma->vm_flags & VM_SHARED)) {
		if (mode == MCOPY_ATOMIC_NORMAL)
			err = mcopy_atomic_pte(dst_mm, dst_pmd, dst_vma,
					       dst_addr, src_addr, page,
					       wp_copy);
		else
			err = mfill_zeropage_pte(dst_mm, dst_pmd,
						 dst_vma, dst_addr);
	} else {
		err = shmem_mfill_atomic_pte(dst_mm, dst_pmd, dst_vma,
					     dst_addr, src_addr,
					     mode != MCOPY_ATOMIC_NORMAL,
					     wp_copy, page);
	}

	return err;
}

static __always_inline ssize_t __mcopy_atomic(struct userfaultfd_ctx *ctx,
					      unsigned long dst_start,
					      unsigned long src_start,
					      unsigned long len,
					      enum mcopy_atomic_mode mcopy_mode,
					      __u64 mode)
{
	struct mm_struct *dst_mm = ctx->mm;
	struct vm_area_struct *dst_vma;
	ssize_t err;
	pmd_t *dst_pmd;
	unsigned long src_addr, dst_addr;
	long copied;
	struct page *page;
	bool wp_copy;

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
	/*
	 * Make sure the vma is not shared, that the dst range is
	 * both valid and fully within a single existing vma.
	 */
	dst_vma = uffd_mfill_lock(dst_mm, dst_start, len);
	if (IS_ERR(dst_vma)) {
		err = PTR_ERR(dst_vma);
		goto out;
	}

	/*
	 * If memory mappings are changing because of non-cooperative
	 * operation (e.g. mremap) running in parallel, bail out and
	 * request the user to retry later
	 */
	down_read(&ctx->map_changing_lock);
	err = -EAGAIN;
	if (atomic_read(&ctx->mmap_changing))
		goto out_unlock;

	err = -EINVAL;
	/*
	 * shmem_zero_setup is invoked in mmap for MAP_ANONYMOUS|MAP_SHARED but
	 * it will overwrite vm_ops, so vma_is_anonymous must return false.
	 */
	if (WARN_ON_ONCE(vma_is_anonymous(dst_vma) &&
	    dst_vma->vm_flags & VM_SHARED))
		goto out_unlock;

	/*
	 * validate 'mode' now that we know the dst_vma: don't allow
	 * a wrprotect copy if the userfaultfd didn't register as WP.
	 */
	wp_copy = mode & UFFDIO_COPY_MODE_WP;
	if (wp_copy && !(dst_vma->vm_flags & VM_UFFD_WP))
		goto out_unlock;

	/*
	 * If this is a HUGETLB vma, pass off to appropriate routine
	 */
	if (is_vm_hugetlb_page(dst_vma))
		return  __mcopy_atomic_hugetlb(ctx, dst_vma, dst_start,
					       src_start, len, mcopy_mode,
					       wp_copy);

	if (!vma_is_anonymous(dst_vma) && !vma_is_shmem(dst_vma))
		goto out_unlock;
	if (!vma_is_shmem(dst_vma) && mcopy_mode == MCOPY_ATOMIC_CONTINUE)
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
		    unlikely(__pte_alloc(dst_mm, dst_pmd))) {
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

		err = mfill_atomic_pte(dst_mm, dst_pmd, dst_vma, dst_addr,
				       src_addr, &page, mcopy_mode, wp_copy);
		cond_resched();

		if (unlikely(err == -ENOENT)) {
			void *page_kaddr;

			up_read(&ctx->map_changing_lock);
			uffd_mfill_unlock(dst_vma);
			BUG_ON(!page);

			page_kaddr = kmap_local_page(page);
			err = copy_from_user(page_kaddr,
					     (const void __user *) src_addr,
					     PAGE_SIZE);
			kunmap_local(page_kaddr);
			if (unlikely(err)) {
				err = -EFAULT;
				goto out;
			}
			flush_dcache_page(page);
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
	up_read(&ctx->map_changing_lock);
	uffd_mfill_unlock(dst_vma);
out:
	if (page)
		put_page(page);
	BUG_ON(copied < 0);
	BUG_ON(err > 0);
	BUG_ON(!copied && !err);
	return copied ? copied : err;
}

ssize_t mcopy_atomic(struct userfaultfd_ctx *ctx, unsigned long dst_start,
		     unsigned long src_start, unsigned long len, __u64 mode)
{
	return __mcopy_atomic(ctx, dst_start, src_start, len,
			      MCOPY_ATOMIC_NORMAL, mode);
}

ssize_t mfill_zeropage(struct userfaultfd_ctx *ctx, unsigned long start,
		       unsigned long len)
{
	return __mcopy_atomic(ctx, start, 0, len, MCOPY_ATOMIC_ZEROPAGE, 0);
}

ssize_t mcopy_continue(struct userfaultfd_ctx *ctx, unsigned long start,
		       unsigned long len)
{
	return __mcopy_atomic(ctx, start, 0, len, MCOPY_ATOMIC_CONTINUE, 0);
}

void uffd_wp_range(struct mm_struct *dst_mm, struct vm_area_struct *dst_vma,
		   unsigned long start, unsigned long len, bool enable_wp)
{
	struct mmu_gather tlb;
	pgprot_t newprot;

	if (enable_wp)
		newprot = vm_get_page_prot(dst_vma->vm_flags & ~(VM_WRITE));
	else
		newprot = vm_get_page_prot(dst_vma->vm_flags);

	tlb_gather_mmu(&tlb, dst_mm);
	change_protection(&tlb, dst_vma, start, start + len, newprot,
			  enable_wp ? MM_CP_UFFD_WP : MM_CP_UFFD_WP_RESOLVE);
	tlb_finish_mmu(&tlb);
}

int mwriteprotect_range(struct userfaultfd_ctx *ctx, unsigned long start,
			unsigned long len, bool enable_wp)
{
	struct mm_struct *dst_mm = ctx->mm;
	struct vm_area_struct *dst_vma;
	unsigned long page_mask;
	int err;

	/*
	 * Sanitize the command parameters:
	 */
	BUG_ON(start & ~PAGE_MASK);
	BUG_ON(len & ~PAGE_MASK);

	/* Does the address range wrap, or is the span zero-sized? */
	BUG_ON(start + len <= start);

	mmap_read_lock(dst_mm);

	/*
	 * If memory mappings are changing because of non-cooperative
	 * operation (e.g. mremap) running in parallel, bail out and
	 * request the user to retry later
	 */
	down_read(&ctx->map_changing_lock);
	err = -EAGAIN;
	if (atomic_read(&ctx->mmap_changing))
		goto out_unlock;

	err = -ENOENT;
	dst_vma = find_vma(dst_mm, start);

	if (!dst_vma)
		goto out_unlock;
	if (start < dst_vma->vm_start || !validate_dst_vma(dst_vma, start + len))
		goto out_unlock;
	if (!userfaultfd_wp(dst_vma))
		goto out_unlock;
	if (!vma_can_userfault(dst_vma, dst_vma->vm_flags))
		goto out_unlock;

	if (is_vm_hugetlb_page(dst_vma)) {
		err = -EINVAL;
		page_mask = vma_kernel_pagesize(dst_vma) - 1;
		if ((start & page_mask) || (len & page_mask))
			goto out_unlock;
	}

	uffd_wp_range(dst_mm, dst_vma, start, len, enable_wp);

	err = 0;
out_unlock:
	up_read(&ctx->map_changing_lock);
	mmap_read_unlock(dst_mm);
	return err;
}


void double_pt_lock(spinlock_t *ptl1,
		    spinlock_t *ptl2)
	__acquires(ptl1)
	__acquires(ptl2)
{
	spinlock_t *ptl_tmp;

	if (ptl1 > ptl2) {
		/* exchange ptl1 and ptl2 */
		ptl_tmp = ptl1;
		ptl1 = ptl2;
		ptl2 = ptl_tmp;
	}
	/* lock in virtual address order to avoid lock inversion */
	spin_lock(ptl1);
	if (ptl1 != ptl2)
		spin_lock_nested(ptl2, SINGLE_DEPTH_NESTING);
	else
		__acquire(ptl2);
}

void double_pt_unlock(spinlock_t *ptl1,
		      spinlock_t *ptl2)
	__releases(ptl1)
	__releases(ptl2)
{
	spin_unlock(ptl1);
	if (ptl1 != ptl2)
		spin_unlock(ptl2);
	else
		__release(ptl2);
}


static int move_present_pte(struct mm_struct *mm,
			    struct vm_area_struct *dst_vma,
			    struct vm_area_struct *src_vma,
			    unsigned long dst_addr, unsigned long src_addr,
			    pte_t *dst_pte, pte_t *src_pte,
			    pte_t orig_dst_pte, pte_t orig_src_pte,
			    spinlock_t *dst_ptl, spinlock_t *src_ptl,
			    struct folio *src_folio)
{
	int err = 0;

	double_pt_lock(dst_ptl, src_ptl);

	if (!pte_same(ptep_get(src_pte), orig_src_pte) ||
	    !pte_same(ptep_get(dst_pte), orig_dst_pte)) {
		err = -EAGAIN;
		goto out;
	}
	if (folio_test_large(src_folio) ||
	    folio_maybe_dma_pinned(src_folio) ||
	    !PageAnonExclusive(&src_folio->page)) {
		err = -EBUSY;
		goto out;
	}

	orig_src_pte = ptep_clear_flush(src_vma, src_addr, src_pte);
	/* Folio got pinned from under us. Put it back and fail the move. */
	if (folio_maybe_dma_pinned(src_folio)) {
		set_pte_at(mm, src_addr, src_pte, orig_src_pte);
		err = -EBUSY;
		goto out;
	}

	page_move_anon_rmap(&src_folio->page, dst_vma);
	WRITE_ONCE(src_folio->index, linear_page_index(dst_vma, dst_addr));

	orig_dst_pte = mk_pte(&src_folio->page, dst_vma->vm_page_prot);
	/* Follow mremap() behavior and treat the entry dirty after the move */
	orig_dst_pte = pte_mkwrite(pte_mkdirty(orig_dst_pte));

	set_pte_at(mm, dst_addr, dst_pte, orig_dst_pte);
out:
	double_pt_unlock(dst_ptl, src_ptl);
	return err;
}

static int move_swap_pte(struct mm_struct *mm,
			 unsigned long dst_addr, unsigned long src_addr,
			 pte_t *dst_pte, pte_t *src_pte,
			 pte_t orig_dst_pte, pte_t orig_src_pte,
			 spinlock_t *dst_ptl, spinlock_t *src_ptl)
{
	if (!pte_swp_exclusive(orig_src_pte))
		return -EBUSY;

	double_pt_lock(dst_ptl, src_ptl);

	if (!pte_same(ptep_get(src_pte), orig_src_pte) ||
	    !pte_same(ptep_get(dst_pte), orig_dst_pte)) {
		double_pt_unlock(dst_ptl, src_ptl);
		return -EAGAIN;
	}

	orig_src_pte = ptep_get_and_clear(mm, src_addr, src_pte);
	set_pte_at(mm, dst_addr, dst_pte, orig_src_pte);
	double_pt_unlock(dst_ptl, src_ptl);

	return 0;
}

static int move_zeropage_pte(struct mm_struct *mm,
			     struct vm_area_struct *dst_vma,
			     struct vm_area_struct *src_vma,
			     unsigned long dst_addr, unsigned long src_addr,
			     pte_t *dst_pte, pte_t *src_pte,
			     pte_t orig_dst_pte, pte_t orig_src_pte,
			     spinlock_t *dst_ptl, spinlock_t *src_ptl)
{
	pte_t zero_pte;

	double_pt_lock(dst_ptl, src_ptl);
	if (!pte_same(ptep_get(src_pte), orig_src_pte) ||
	    !pte_same(ptep_get(dst_pte), orig_dst_pte)) {
		double_pt_unlock(dst_ptl, src_ptl);
		return -EAGAIN;
	}

	zero_pte = pte_mkspecial(pfn_pte(my_zero_pfn(dst_addr),
					 dst_vma->vm_page_prot));
	ptep_clear_flush(src_vma, src_addr, src_pte);
	set_pte_at(mm, dst_addr, dst_pte, zero_pte);
	double_pt_unlock(dst_ptl, src_ptl);

	return 0;
}


/*
 * The mmap_lock for reading is held by the caller. Just move the page
 * from src_pmd to dst_pmd if possible, and return true if succeeded
 * in moving the page.
 */
static int move_pages_pte(struct mm_struct *mm, pmd_t *dst_pmd, pmd_t *src_pmd,
			  struct vm_area_struct *dst_vma,
			  struct vm_area_struct *src_vma,
			  unsigned long dst_addr, unsigned long src_addr,
			  __u64 mode)
{
	swp_entry_t entry;
	pte_t orig_src_pte, orig_dst_pte;
	pte_t src_folio_pte;
	spinlock_t *src_ptl, *dst_ptl;
	pte_t *src_pte = NULL;
	pte_t *dst_pte = NULL;

	struct folio *src_folio = NULL;
	struct anon_vma *src_anon_vma = NULL;
	struct mmu_notifier_range range;
	int err = 0;

	flush_cache_range(src_vma, src_addr, src_addr + PAGE_SIZE);
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, src_vma, mm,
				src_addr, src_addr + PAGE_SIZE);
	mmu_notifier_invalidate_range_start(&range);
retry:
	dst_pte = pte_offset_map(dst_pmd, dst_addr);
	dst_ptl = pte_lockptr(mm, dst_pmd);


	/* Retry if a huge pmd materialized from under us */
	if (unlikely(!dst_pte)) {
		err = -EAGAIN;
		goto out;
	}

	src_pte = pte_offset_map(src_pmd, src_addr);
	src_ptl = pte_lockptr(mm, src_pmd);

	/*
	 * We held the mmap_lock for reading so MADV_DONTNEED
	 * can zap transparent huge pages under us, or the
	 * transparent huge page fault can establish new
	 * transparent huge pages under us.
	 */
	if (unlikely(!src_pte)) {
		err = -EAGAIN;
		goto out;
	}

	/* Sanity checks before the operation */
	if (WARN_ON_ONCE(pmd_none(*dst_pmd)) ||	WARN_ON_ONCE(pmd_none(*src_pmd)) ||
	    WARN_ON_ONCE(pmd_trans_huge(*dst_pmd)) || WARN_ON_ONCE(pmd_trans_huge(*src_pmd))) {
		err = -EINVAL;
		goto out;
	}

	spin_lock(dst_ptl);
	orig_dst_pte = ptep_get(dst_pte);
	spin_unlock(dst_ptl);
	if (!pte_none(orig_dst_pte)) {
		err = -EEXIST;
		goto out;
	}

	spin_lock(src_ptl);
	orig_src_pte = ptep_get(src_pte);
	spin_unlock(src_ptl);
	if (pte_none(orig_src_pte)) {
		if (!(mode & UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES))
			err = -ENOENT;
		else /* nothing to do to move a hole */
			err = 0;
		goto out;
	}

	/* If PTE changed after we locked the folio them start over */
	if (src_folio && unlikely(!pte_same(src_folio_pte, orig_src_pte))) {
		err = -EAGAIN;
		goto out;
	}

	if (pte_present(orig_src_pte)) {
		if (is_zero_pfn(pte_pfn(orig_src_pte))) {
			err = move_zeropage_pte(mm, dst_vma, src_vma,
					       dst_addr, src_addr, dst_pte, src_pte,
					       orig_dst_pte, orig_src_pte,
					       dst_ptl, src_ptl);
			goto out;
		}

		/*
		 * Pin and lock both source folio and anon_vma. Since we are in
		 * RCU read section, we can't block, so on contention have to
		 * unmap the ptes, obtain the lock and retry.
		 */
		if (!src_folio) {
			struct folio *folio;

			/*
			 * Pin the page while holding the lock to be sure the
			 * page isn't freed under us
			 */
			spin_lock(src_ptl);
			if (!pte_same(orig_src_pte, ptep_get(src_pte))) {
				spin_unlock(src_ptl);
				err = -EAGAIN;
				goto out;
			}

			folio = vm_normal_folio(src_vma, src_addr, orig_src_pte);
			if (!folio || !PageAnonExclusive(&folio->page)) {
				spin_unlock(src_ptl);
				err = -EBUSY;
				goto out;
			}

			folio_get(folio);
			src_folio = folio;
			src_folio_pte = orig_src_pte;
			spin_unlock(src_ptl);

			if (!folio_trylock(src_folio)) {
				pte_unmap(&orig_src_pte);
				pte_unmap(&orig_dst_pte);
				src_pte = dst_pte = NULL;
				/* now we can block and wait */
				folio_lock(src_folio);
				goto retry;
			}

			if (WARN_ON_ONCE(!folio_test_anon(src_folio))) {
				err = -EBUSY;
				goto out;
			}
		}

		/* at this point we have src_folio locked */
		if (folio_test_large(src_folio)) {
			/* split_folio() can block */
			pte_unmap(&orig_src_pte);
			pte_unmap(&orig_dst_pte);
			src_pte = dst_pte = NULL;
			err = split_folio(src_folio);
			if (err)
				goto out;
			/* have to reacquire the folio after it got split */
			folio_unlock(src_folio);
			folio_put(src_folio);
			src_folio = NULL;
			goto retry;
		}

		if (!src_anon_vma) {
			/*
			 * folio_referenced walks the anon_vma chain
			 * without the folio lock. Serialize against it with
			 * the anon_vma lock, the folio lock is not enough.
			 */
			src_anon_vma = folio_get_anon_vma(src_folio);
			if (!src_anon_vma) {
				/* page was unmapped from under us */
				err = -EAGAIN;
				goto out;
			}
			if (!anon_vma_trylock_write(src_anon_vma)) {
				pte_unmap(&orig_src_pte);
				pte_unmap(&orig_dst_pte);
				src_pte = dst_pte = NULL;
				/* now we can block and wait */
				anon_vma_lock_write(src_anon_vma);
				goto retry;
			}
		}

		err = move_present_pte(mm,  dst_vma, src_vma,
				       dst_addr, src_addr, dst_pte, src_pte,
				       orig_dst_pte, orig_src_pte,
				       dst_ptl, src_ptl, src_folio);
	} else {
		entry = pte_to_swp_entry(orig_src_pte);
		if (non_swap_entry(entry)) {
			if (is_migration_entry(entry)) {
				pte_unmap(&orig_src_pte);
				pte_unmap(&orig_dst_pte);
				src_pte = dst_pte = NULL;
				migration_entry_wait(mm, src_pmd, src_addr);
				err = -EAGAIN;
			} else
				err = -EFAULT;
			goto out;
		}

		err = move_swap_pte(mm, dst_addr, src_addr,
				    dst_pte, src_pte,
				    orig_dst_pte, orig_src_pte,
				    dst_ptl, src_ptl);
	}

out:
	if (src_anon_vma) {
		anon_vma_unlock_write(src_anon_vma);
		put_anon_vma(src_anon_vma);
	}
	if (src_folio) {
		folio_unlock(src_folio);
		folio_put(src_folio);
	}
	if (dst_pte)
		pte_unmap(dst_pte);
	if (src_pte)
		pte_unmap(src_pte);
	mmu_notifier_invalidate_range_end(&range);

	return err;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline bool move_splits_huge_pmd(unsigned long dst_addr,
					unsigned long src_addr,
					unsigned long src_end)
{
	return (src_addr & ~HPAGE_PMD_MASK) || (dst_addr & ~HPAGE_PMD_MASK) ||
		src_end - src_addr < HPAGE_PMD_SIZE;
}
#else
static inline bool move_splits_huge_pmd(unsigned long dst_addr,
					unsigned long src_addr,
					unsigned long src_end)
{
	/* This is unreachable anyway, just to avoid warnings when HPAGE_PMD_SIZE==0 */
	return false;
}
#endif

static inline bool vma_move_compatible(struct vm_area_struct *vma)
{
	return !(vma->vm_flags & (VM_PFNMAP | VM_IO |  VM_HUGETLB |
				  VM_MIXEDMAP));
}

static int validate_move_areas(struct userfaultfd_ctx *ctx,
			       struct vm_area_struct *src_vma,
			       struct vm_area_struct *dst_vma)
{
	/* Only allow moving if both have the same access and protection */
	if ((src_vma->vm_flags & VM_ACCESS_FLAGS) != (dst_vma->vm_flags & VM_ACCESS_FLAGS) ||
	    pgprot_val(src_vma->vm_page_prot) != pgprot_val(dst_vma->vm_page_prot))
		return -EINVAL;

	/* Only allow moving if both are mlocked or both aren't */
	if ((src_vma->vm_flags & VM_LOCKED) != (dst_vma->vm_flags & VM_LOCKED))
		return -EINVAL;

	/*
	 * For now, we keep it simple and only move between writable VMAs.
	 * Access flags are equal, therefore cheching only the source is enough.
	 */
	if (!(src_vma->vm_flags & VM_WRITE))
		return -EINVAL;

	/* Check if vma flags indicate content which can be moved */
	if (!vma_move_compatible(src_vma) || !vma_move_compatible(dst_vma))
		return -EINVAL;

	/* Ensure dst_vma is registered in uffd we are operating on */
	if (!dst_vma->vm_userfaultfd_ctx.ctx ||
	    dst_vma->vm_userfaultfd_ctx.ctx != ctx)
		return -EINVAL;

	/* Only allow moving across anonymous vmas */
	if (!vma_is_anonymous(src_vma) || !vma_is_anonymous(dst_vma))
		return -EINVAL;

	return 0;
}

static __always_inline
int find_vmas_mm_locked(struct mm_struct *mm,
			unsigned long dst_start,
			unsigned long src_start,
			struct vm_area_struct **dst_vmap,
			struct vm_area_struct **src_vmap)
{
	struct vm_area_struct *vma;

	mmap_assert_locked(mm);
	vma = find_vma_and_prepare_anon(mm, dst_start);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	*dst_vmap = vma;
	/* Skip finding src_vma if src_start is in dst_vma */
	if (src_start >= vma->vm_start && src_start < vma->vm_end)
		goto out_success;

	vma = vma_lookup(mm, src_start);
	if (!vma)
		return -ENOENT;
out_success:
	*src_vmap = vma;
	return 0;
}

#ifdef CONFIG_PER_VMA_LOCK
static int uffd_move_lock(struct mm_struct *mm,
			  unsigned long dst_start,
			  unsigned long src_start,
			  struct vm_area_struct **dst_vmap,
			  struct vm_area_struct **src_vmap)
{
	struct vm_area_struct *vma;
	int err;

	vma = lock_vma(mm, dst_start);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	*dst_vmap = vma;
	/*
	 * Skip finding src_vma if src_start is in dst_vma. This also ensures
	 * that we don't lock the same vma twice.
	 */
	if (src_start >= vma->vm_start && src_start < vma->vm_end) {
		*src_vmap = vma;
		return 0;
	}

	/*
	 * Using lock_vma() to get src_vma can lead to following deadlock:
	 *
	 * Thread1				Thread2
	 * -------				-------
	 * vma_start_read(dst_vma)
	 *					mmap_write_lock(mm)
	 *					vma_start_write(src_vma)
	 * vma_start_read(src_vma)
	 * mmap_read_lock(mm)
	 *					vma_start_write(dst_vma)
	 */
	*src_vmap = lock_vma_under_rcu(mm, src_start);
	if (likely(*src_vmap))
		return 0;

	/* Undo any locking and retry in mmap_lock critical section */
	vma_end_read(*dst_vmap);

	mmap_read_lock(mm);
	err = find_vmas_mm_locked(mm, dst_start, src_start, dst_vmap, src_vmap);
	if (!err) {
		/*
		 * See comment in lock_vma() as to why not using
		 * vma_start_read() here.
		 */
		down_read(&(*dst_vmap)->vm_lock->lock);
		if (*dst_vmap != *src_vmap)
			down_read_nested(&(*src_vmap)->vm_lock->lock,
					 SINGLE_DEPTH_NESTING);
	}
	mmap_read_unlock(mm);
	return err;
}

static void uffd_move_unlock(struct vm_area_struct *dst_vma,
			     struct vm_area_struct *src_vma)
{
	vma_end_read(src_vma);
	if (src_vma != dst_vma)
		vma_end_read(dst_vma);
}

#else

static int uffd_move_lock(struct mm_struct *mm,
			  unsigned long dst_start,
			  unsigned long src_start,
			  struct vm_area_struct **dst_vmap,
			  struct vm_area_struct **src_vmap)
{
	int err;

	mmap_read_lock(mm);
	err = find_vmas_mm_locked(mm, dst_start, src_start, dst_vmap, src_vmap);
	if (err)
		mmap_read_unlock(mm);
	return err;
}

static void uffd_move_unlock(struct vm_area_struct *dst_vma,
			     struct vm_area_struct *src_vma)
{
	mmap_assert_locked(src_vma->vm_mm);
	mmap_read_unlock(dst_vma->vm_mm);
}
#endif

/**
 * move_pages - move arbitrary anonymous pages of an existing vma
 * @ctx: pointer to the userfaultfd context
 * @dst_start: start of the destination virtual memory range
 * @src_start: start of the source virtual memory range
 * @len: length of the virtual memory range
 * @mode: flags from uffdio_move.mode
 *
 * It will either use the mmap_lock in read mode or per-vma locks
 *
 * move_pages() remaps arbitrary anonymous pages atomically in zero
 * copy. It only works on non shared anonymous pages because those can
 * be relocated without generating non linear anon_vmas in the rmap
 * code.
 *
 * It provides a zero copy mechanism to handle userspace page faults.
 * The source vma pages should have mapcount == 1, which can be
 * enforced by using madvise(MADV_DONTFORK) on src vma.
 *
 * The thread receiving the page during the userland page fault
 * will receive the faulting page in the source vma through the network,
 * storage or any other I/O device (MADV_DONTFORK in the source vma
 * avoids move_pages() to fail with -EBUSY if the process forks before
 * move_pages() is called), then it will call move_pages() to map the
 * page in the faulting address in the destination vma.
 *
 * This userfaultfd command works purely via pagetables, so it's the
 * most efficient way to move physical non shared anonymous pages
 * across different virtual addresses. Unlike mremap()/mmap()/munmap()
 * it does not create any new vmas. The mapping in the destination
 * address is atomic.
 *
 * It only works if the vma protection bits are identical from the
 * source and destination vma.
 *
 * It can remap non shared anonymous pages within the same vma too.
 *
 * If the source virtual memory range has any unmapped holes, or if
 * the destination virtual memory range is not a whole unmapped hole,
 * move_pages() will fail respectively with -ENOENT or -EEXIST. This
 * provides a very strict behavior to avoid any chance of memory
 * corruption going unnoticed if there are userland race conditions.
 * Only one thread should resolve the userland page fault at any given
 * time for any given faulting address. This means that if two threads
 * try to both call move_pages() on the same destination address at the
 * same time, the second thread will get an explicit error from this
 * command.
 *
 * The command retval will return "len" is successful. The command
 * however can be interrupted by fatal signals or errors. If
 * interrupted it will return the number of bytes successfully
 * remapped before the interruption if any, or the negative error if
 * none. It will never return zero. Either it will return an error or
 * an amount of bytes successfully moved. If the retval reports a
 * "short" remap, the move_pages() command should be repeated by
 * userland with src+retval, dst+reval, len-retval if it wants to know
 * about the error that interrupted it.
 *
 * The UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES flag can be specified to
 * prevent -ENOENT errors to materialize if there are holes in the
 * source virtual range that is being remapped. The holes will be
 * accounted as successfully remapped in the retval of the
 * command. This is mostly useful to remap hugepage naturally aligned
 * virtual regions without knowing if there are transparent hugepage
 * in the regions or not, but preventing the risk of having to split
 * the hugepmd during the remap.
 *
 * If there's any rmap walk that is taking the anon_vma locks without
 * first obtaining the folio lock (the only current instance is
 * folio_referenced), they will have to verify if the folio->mapping
 * has changed after taking the anon_vma lock. If it changed they
 * should release the lock and retry obtaining a new anon_vma, because
 * it means the anon_vma was changed by move_pages() before the lock
 * could be obtained. This is the only additional complexity added to
 * the rmap code to provide this anonymous page remapping functionality.
 */
ssize_t move_pages(struct userfaultfd_ctx *ctx, unsigned long dst_start,
		   unsigned long src_start, unsigned long len, __u64 mode)
{
	struct mm_struct *mm = ctx->mm;
	struct vm_area_struct *src_vma, *dst_vma;
	unsigned long src_addr, dst_addr;
	pmd_t *src_pmd, *dst_pmd;
	long err = -EINVAL;
	ssize_t moved = 0;

	/* Sanitize the command parameters. */
	if (WARN_ON_ONCE(src_start & ~PAGE_MASK) ||
	    WARN_ON_ONCE(dst_start & ~PAGE_MASK) ||
	    WARN_ON_ONCE(len & ~PAGE_MASK))
		goto out;

	/* Does the address range wrap, or is the span zero-sized? */
	if (WARN_ON_ONCE(src_start + len <= src_start) ||
	    WARN_ON_ONCE(dst_start + len <= dst_start))
		goto out;

	err = uffd_move_lock(mm, dst_start, src_start, &dst_vma, &src_vma);
	if (err)
		goto out;

	/* Re-check after taking map_changing_lock */
	err = -EAGAIN;
	down_read(&ctx->map_changing_lock);
	if (likely(atomic_read(&ctx->mmap_changing)))
		goto out_unlock;
	/*
	 * Make sure the vma is not shared, that the src and dst remap
	 * ranges are both valid and fully within a single existing
	 * vma.
	 */
	err = -EINVAL;
	if (src_vma->vm_flags & VM_SHARED)
		goto out_unlock;
	if (src_start + len > src_vma->vm_end)
		goto out_unlock;

	if (dst_vma->vm_flags & VM_SHARED)
		goto out_unlock;
	if (dst_start + len > dst_vma->vm_end)
		goto out_unlock;

	err = validate_move_areas(ctx, src_vma, dst_vma);
	if (err)
		goto out_unlock;

	for (src_addr = src_start, dst_addr = dst_start;
	     src_addr < src_start + len;) {
		spinlock_t *ptl;
		pmd_t dst_pmdval;
		unsigned long step_size;

		/*
		 * Below works because anonymous area would not have a
		 * transparent huge PUD. If file-backed support is added,
		 * that case would need to be handled here.
		 */
		src_pmd = mm_find_pmd(mm, src_addr);
		if (unlikely(!src_pmd)) {
			if (!(mode & UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES)) {
				err = -ENOENT;
				break;
			}
			src_pmd = mm_alloc_pmd(mm, src_addr);
			if (unlikely(!src_pmd)) {
				err = -ENOMEM;
				break;
			}
		}
		dst_pmd = mm_alloc_pmd(mm, dst_addr);
		if (unlikely(!dst_pmd)) {
			err = -ENOMEM;
			break;
		}

		dst_pmdval = pmd_read_atomic(dst_pmd);
		/*
		 * If the dst_pmd is mapped as THP don't override it and just
		 * be strict. If dst_pmd changes into TPH after this check, the
		 * move_pages_huge_pmd() will detect the change and retry
		 * while move_pages_pte() will detect the change and fail.
		 */
		if (unlikely(pmd_trans_huge(dst_pmdval))) {
			err = -EEXIST;
			break;
		}

		ptl = pmd_trans_huge_lock(src_pmd, src_vma);
		if (ptl) {
			if (pmd_devmap(*src_pmd)) {
				spin_unlock(ptl);
				err = -ENOENT;
				break;
			}

			/* Check if we can move the pmd without splitting it. */
			if (move_splits_huge_pmd(dst_addr, src_addr, src_start + len) ||
			    !pmd_none(dst_pmdval)) {
				struct folio *folio = pfn_folio(pmd_pfn(*src_pmd));

				if (!folio || (!is_huge_zero_page(&folio->page) &&
					       !PageAnonExclusive(&folio->page))) {
					spin_unlock(ptl);
					err = -EBUSY;
					break;
				}

				spin_unlock(ptl);
				split_huge_pmd(src_vma, src_pmd, src_addr);
				/* The folio will be split by move_pages_pte() */
				continue;
			}

			err = move_pages_huge_pmd(mm, dst_pmd, src_pmd,
						  dst_pmdval, dst_vma, src_vma,
						  dst_addr, src_addr);
			step_size = HPAGE_PMD_SIZE;
		} else {
			if (pmd_none(*src_pmd)) {
				if (!(mode & UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES)) {
					err = -ENOENT;
					break;
				}
				if (unlikely(__pte_alloc(mm, src_pmd))) {
					err = -ENOMEM;
					break;
				}
			}

			if (unlikely(pte_alloc(mm, dst_pmd))) {
				err = -ENOMEM;
				break;
			}

			err = move_pages_pte(mm, dst_pmd, src_pmd,
					     dst_vma, src_vma,
					     dst_addr, src_addr, mode);
			step_size = PAGE_SIZE;
		}

		cond_resched();

		if (fatal_signal_pending(current)) {
			/* Do not override an error */
			if (!err || err == -EAGAIN)
				err = -EINTR;
			break;
		}

		if (err) {
			if (err == -EAGAIN)
				continue;
			break;
		}

		/* Proceed to the next page */
		dst_addr += step_size;
		src_addr += step_size;
		moved += step_size;
	}

out_unlock:
	up_read(&ctx->map_changing_lock);
	uffd_move_unlock(dst_vma, src_vma);
out:
	VM_WARN_ON(moved < 0);
	VM_WARN_ON(err > 0);
	VM_WARN_ON(!moved && !err);
	return moved ? moved : err;
}
