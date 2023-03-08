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
struct vm_area_struct *find_dst_vma(struct mm_struct *dst_mm,
				    unsigned long dst_start,
				    unsigned long len)
{
	/*
	 * Make sure that the dst range is both valid and fully within a
	 * single existing vma.
	 */
	struct vm_area_struct *dst_vma;

	dst_vma = find_vma(dst_mm, dst_start);
	if (!dst_vma)
		return NULL;

	if (dst_start < dst_vma->vm_start ||
	    dst_start + len > dst_vma->vm_end)
		return NULL;

	/*
	 * Check the vma is registered in uffd, this is required to
	 * enforce the VM_MAYWRITE check done at uffd registration
	 * time.
	 */
	if (!dst_vma->vm_userfaultfd_ctx.ctx)
		return NULL;

	return dst_vma;
}

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
	struct folio *folio;
	struct inode *inode;
	pgoff_t offset, max_off;

	_dst_pte = mk_pte(page, dst_vma->vm_page_prot);
	_dst_pte = pte_mkdirty(_dst_pte);
	if (page_in_cache && !vm_shared)
		writable = false;
	if (writable)
		_dst_pte = pte_mkwrite(_dst_pte);
	if (wp_copy)
		_dst_pte = pte_mkuffd_wp(_dst_pte);

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

	folio = page_folio(page);
	if (page_in_cache) {
		/* Usually, cache pages are already added to LRU */
		if (newly_allocated)
			folio_add_lru(folio);
		page_add_file_rmap(page, dst_vma, false);
	} else {
		page_add_new_anon_rmap(page, dst_vma, dst_addr);
		folio_add_lru_vma(folio, dst_vma);
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
 * called with mmap_lock held, it will release mmap_lock before returning.
 */
static __always_inline ssize_t __mcopy_atomic_hugetlb(struct mm_struct *dst_mm,
					      struct vm_area_struct *dst_vma,
					      unsigned long dst_start,
					      unsigned long src_start,
					      unsigned long len,
					      enum mcopy_atomic_mode mode,
					      bool wp_copy)
{
	int vm_shared = dst_vma->vm_flags & VM_SHARED;
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
		mmap_read_unlock(dst_mm);
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
		err = -ENOENT;
		dst_vma = find_dst_vma(dst_mm, dst_start, len);
		if (!dst_vma || !is_vm_hugetlb_page(dst_vma))
			goto out_unlock;

		err = -EINVAL;
		if (vma_hpagesize != vma_kernel_pagesize(dst_vma))
			goto out_unlock;

		vm_shared = dst_vma->vm_flags & VM_SHARED;
	}

	/*
	 * If not shared, ensure the dst_vma has a anon_vma.
	 */
	err = -ENOMEM;
	if (!vm_shared) {
		if (unlikely(anon_vma_prepare(dst_vma)))
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
			mmap_read_unlock(dst_mm);
			BUG_ON(!page);

			err = copy_huge_page_from_user(page,
						(const void __user *)src_addr,
						vma_hpagesize / PAGE_SIZE,
						true);
			if (unlikely(err)) {
				err = -EFAULT;
				goto out;
			}
			mmap_read_lock(dst_mm);

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
	mmap_read_unlock(dst_mm);
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
extern ssize_t __mcopy_atomic_hugetlb(struct mm_struct *dst_mm,
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

static __always_inline ssize_t __mcopy_atomic(struct mm_struct *dst_mm,
					      unsigned long dst_start,
					      unsigned long src_start,
					      unsigned long len,
					      enum mcopy_atomic_mode mcopy_mode,
					      atomic_t *mmap_changing,
					      __u64 mode)
{
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
	mmap_read_lock(dst_mm);

	/*
	 * If memory mappings are changing because of non-cooperative
	 * operation (e.g. mremap) running in parallel, bail out and
	 * request the user to retry later
	 */
	err = -EAGAIN;
	if (mmap_changing && atomic_read(mmap_changing))
		goto out_unlock;

	/*
	 * Make sure the vma is not shared, that the dst range is
	 * both valid and fully within a single existing vma.
	 */
	err = -ENOENT;
	dst_vma = find_dst_vma(dst_mm, dst_start, len);
	if (!dst_vma)
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
		return  __mcopy_atomic_hugetlb(dst_mm, dst_vma, dst_start,
					       src_start, len, mcopy_mode,
					       wp_copy);

	if (!vma_is_anonymous(dst_vma) && !vma_is_shmem(dst_vma))
		goto out_unlock;
	if (!vma_is_shmem(dst_vma) && mcopy_mode == MCOPY_ATOMIC_CONTINUE)
		goto out_unlock;

	/*
	 * Ensure the dst_vma has a anon_vma or this page
	 * would get a NULL anon_vma when moved in the
	 * dst_vma.
	 */
	err = -ENOMEM;
	if (!(dst_vma->vm_flags & VM_SHARED) &&
	    unlikely(anon_vma_prepare(dst_vma)))
		goto out_unlock;

	while (src_addr < src_start + len) {
		pmd_t dst_pmdval;

		BUG_ON(dst_addr >= dst_start + len);

		dst_pmd = mm_alloc_pmd(dst_mm, dst_addr);
		if (unlikely(!dst_pmd)) {
			err = -ENOMEM;
			break;
		}

		dst_pmdval = pmdp_get_lockless(dst_pmd);
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

			mmap_read_unlock(dst_mm);
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
	mmap_read_unlock(dst_mm);
out:
	if (page)
		put_page(page);
	BUG_ON(copied < 0);
	BUG_ON(err > 0);
	BUG_ON(!copied && !err);
	return copied ? copied : err;
}

ssize_t mcopy_atomic(struct mm_struct *dst_mm, unsigned long dst_start,
		     unsigned long src_start, unsigned long len,
		     atomic_t *mmap_changing, __u64 mode)
{
	return __mcopy_atomic(dst_mm, dst_start, src_start, len,
			      MCOPY_ATOMIC_NORMAL, mmap_changing, mode);
}

ssize_t mfill_zeropage(struct mm_struct *dst_mm, unsigned long start,
		       unsigned long len, atomic_t *mmap_changing)
{
	return __mcopy_atomic(dst_mm, start, 0, len, MCOPY_ATOMIC_ZEROPAGE,
			      mmap_changing, 0);
}

ssize_t mcopy_continue(struct mm_struct *dst_mm, unsigned long start,
		       unsigned long len, atomic_t *mmap_changing)
{
	return __mcopy_atomic(dst_mm, start, 0, len, MCOPY_ATOMIC_CONTINUE,
			      mmap_changing, 0);
}

long uffd_wp_range(struct mm_struct *dst_mm, struct vm_area_struct *dst_vma,
		   unsigned long start, unsigned long len, bool enable_wp)
{
	unsigned int mm_cp_flags;
	struct mmu_gather tlb;
	long ret;

	if (enable_wp)
		mm_cp_flags = MM_CP_UFFD_WP;
	else
		mm_cp_flags = MM_CP_UFFD_WP_RESOLVE;

	/*
	 * vma->vm_page_prot already reflects that uffd-wp is enabled for this
	 * VMA (see userfaultfd_set_vm_flags()) and that all PTEs are supposed
	 * to be write-protected as default whenever protection changes.
	 * Try upgrading write permissions manually.
	 */
	if (!enable_wp && vma_wants_manual_pte_write_upgrade(dst_vma))
		mm_cp_flags |= MM_CP_TRY_CHANGE_WRITABLE;
	tlb_gather_mmu(&tlb, dst_mm);
	ret = change_protection(&tlb, dst_vma, start, start + len, mm_cp_flags);
	tlb_finish_mmu(&tlb);

	return ret;
}

int mwriteprotect_range(struct mm_struct *dst_mm, unsigned long start,
			unsigned long len, bool enable_wp,
			atomic_t *mmap_changing)
{
	struct vm_area_struct *dst_vma;
	unsigned long page_mask;
	long err;

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
	err = -EAGAIN;
	if (mmap_changing && atomic_read(mmap_changing))
		goto out_unlock;

	err = -ENOENT;
	dst_vma = find_dst_vma(dst_mm, start, len);

	if (!dst_vma)
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

	err = uffd_wp_range(dst_mm, dst_vma, start, len, enable_wp);

	/* Return 0 on success, <0 on failures */
	if (err > 0)
		err = 0;

out_unlock:
	mmap_read_unlock(dst_mm);
	return err;
}
