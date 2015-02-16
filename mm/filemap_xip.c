/*
 *	linux/mm/filemap_xip.c
 *
 * Copyright (C) 2005 IBM Corporation
 * Author: Carsten Otte <cotte@de.ibm.com>
 *
 * derived from linux/mm/filemap.c - Copyright (C) Linus Torvalds
 *
 */

#include <linux/fs.h>
#include <linux/backing-dev.h>
#include <linux/pagemap.h>
#include <linux/export.h>
#include <linux/uio.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/sched.h>
#include <linux/seqlock.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

/*
 * We do use our own empty page to avoid interference with other users
 * of ZERO_PAGE(), such as /dev/zero
 */
static DEFINE_MUTEX(xip_sparse_mutex);
static seqcount_t xip_sparse_seq = SEQCNT_ZERO(xip_sparse_seq);
static struct page *__xip_sparse_page;

/* called under xip_sparse_mutex */
static struct page *xip_sparse_page(void)
{
	if (!__xip_sparse_page) {
		struct page *page = alloc_page(GFP_HIGHUSER | __GFP_ZERO);

		if (page)
			__xip_sparse_page = page;
	}
	return __xip_sparse_page;
}

/*
 * __xip_unmap is invoked from xip_unmap and xip_write
 *
 * This function walks all vmas of the address_space and unmaps the
 * __xip_sparse_page when found at pgoff.
 */
static void __xip_unmap(struct address_space * mapping, unsigned long pgoff)
{
	struct vm_area_struct *vma;
	struct page *page;
	unsigned count;
	int locked = 0;

	count = read_seqcount_begin(&xip_sparse_seq);

	page = __xip_sparse_page;
	if (!page)
		return;

retry:
	i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, pgoff, pgoff) {
		pte_t *pte, pteval;
		spinlock_t *ptl;
		struct mm_struct *mm = vma->vm_mm;
		unsigned long address = vma->vm_start +
			((pgoff - vma->vm_pgoff) << PAGE_SHIFT);

		BUG_ON(address < vma->vm_start || address >= vma->vm_end);
		pte = page_check_address(page, mm, address, &ptl, 1);
		if (pte) {
			/* Nuke the page table entry. */
			flush_cache_page(vma, address, pte_pfn(*pte));
			pteval = ptep_clear_flush(vma, address, pte);
			page_remove_rmap(page);
			dec_mm_counter(mm, MM_FILEPAGES);
			BUG_ON(pte_dirty(pteval));
			pte_unmap_unlock(pte, ptl);
			/* must invalidate_page _before_ freeing the page */
			mmu_notifier_invalidate_page(mm, address);
			page_cache_release(page);
		}
	}
	i_mmap_unlock_read(mapping);

	if (locked) {
		mutex_unlock(&xip_sparse_mutex);
	} else if (read_seqcount_retry(&xip_sparse_seq, count)) {
		mutex_lock(&xip_sparse_mutex);
		locked = 1;
		goto retry;
	}
}

/*
 * xip_fault() is invoked via the vma operations vector for a
 * mapped memory region to read in file data during a page fault.
 *
 * This function is derived from filemap_fault, but used for execute in place
 */
static int xip_file_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct file *file = vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	pgoff_t size;
	void *xip_mem;
	unsigned long xip_pfn;
	struct page *page;
	int error;

	/* XXX: are VM_FAULT_ codes OK? */
again:
	size = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	if (vmf->pgoff >= size)
		return VM_FAULT_SIGBUS;

	error = mapping->a_ops->get_xip_mem(mapping, vmf->pgoff, 0,
						&xip_mem, &xip_pfn);
	if (likely(!error))
		goto found;
	if (error != -ENODATA)
		return VM_FAULT_OOM;

	/* sparse block */
	if ((vma->vm_flags & (VM_WRITE | VM_MAYWRITE)) &&
	    (vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) &&
	    (!(mapping->host->i_sb->s_flags & MS_RDONLY))) {
		int err;

		/* maybe shared writable, allocate new block */
		mutex_lock(&xip_sparse_mutex);
		error = mapping->a_ops->get_xip_mem(mapping, vmf->pgoff, 1,
							&xip_mem, &xip_pfn);
		mutex_unlock(&xip_sparse_mutex);
		if (error)
			return VM_FAULT_SIGBUS;
		/* unmap sparse mappings at pgoff from all other vmas */
		__xip_unmap(mapping, vmf->pgoff);

found:
		/*
		 * We must recheck i_size under i_mmap_rwsem to prevent races
		 * with truncation
		 */
		i_mmap_lock_read(mapping);
		size = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >>
							PAGE_CACHE_SHIFT;
		if (unlikely(vmf->pgoff >= size)) {
			i_mmap_unlock_read(mapping);
			return VM_FAULT_SIGBUS;
		}
		err = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address,
							xip_pfn);
		i_mmap_unlock_read(mapping);
		if (err == -ENOMEM)
			return VM_FAULT_OOM;
		/*
		 * err == -EBUSY is fine, we've raced against another thread
		 * that faulted-in the same page
		 */
		if (err != -EBUSY)
			BUG_ON(err);
		return VM_FAULT_NOPAGE;
	} else {
		int err, ret = VM_FAULT_OOM;

		mutex_lock(&xip_sparse_mutex);
		write_seqcount_begin(&xip_sparse_seq);
		error = mapping->a_ops->get_xip_mem(mapping, vmf->pgoff, 0,
							&xip_mem, &xip_pfn);
		if (unlikely(!error)) {
			write_seqcount_end(&xip_sparse_seq);
			mutex_unlock(&xip_sparse_mutex);
			goto again;
		}
		if (error != -ENODATA)
			goto out;

		/*
		 * We must recheck i_size under i_mmap_rwsem to prevent races
		 * with truncation
		 */
		i_mmap_lock_read(mapping);
		size = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >>
							PAGE_CACHE_SHIFT;
		if (unlikely(vmf->pgoff >= size)) {
			ret = VM_FAULT_SIGBUS;
			goto unlock;
		}
		/* not shared and writable, use xip_sparse_page() */
		page = xip_sparse_page();
		if (!page)
			goto unlock;
		err = vm_insert_page(vma, (unsigned long)vmf->virtual_address,
							page);
		if (err == -ENOMEM)
			goto unlock;

		ret = VM_FAULT_NOPAGE;
unlock:
		i_mmap_unlock_read(mapping);
out:
		write_seqcount_end(&xip_sparse_seq);
		mutex_unlock(&xip_sparse_mutex);

		return ret;
	}
}

static const struct vm_operations_struct xip_file_vm_ops = {
	.fault	= xip_file_fault,
	.page_mkwrite	= filemap_page_mkwrite,
};

int xip_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	BUG_ON(!file->f_mapping->a_ops->get_xip_mem);

	file_accessed(file);
	vma->vm_ops = &xip_file_vm_ops;
	vma->vm_flags |= VM_MIXEDMAP;
	return 0;
}
EXPORT_SYMBOL_GPL(xip_file_mmap);

/*
 * truncate a page used for execute in place
 * functionality is analog to block_truncate_page but does use get_xip_mem
 * to get the page instead of page cache
 */
int
xip_truncate_page(struct address_space *mapping, loff_t from)
{
	pgoff_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned blocksize;
	unsigned length;
	void *xip_mem;
	unsigned long xip_pfn;
	int err;

	BUG_ON(!mapping->a_ops->get_xip_mem);

	blocksize = 1 << mapping->host->i_blkbits;
	length = offset & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;

	length = blocksize - length;

	err = mapping->a_ops->get_xip_mem(mapping, index, 0,
						&xip_mem, &xip_pfn);
	if (unlikely(err)) {
		if (err == -ENODATA)
			/* Hole? No need to truncate */
			return 0;
		else
			return err;
	}
	memset(xip_mem + offset, 0, length);
	return 0;
}
EXPORT_SYMBOL_GPL(xip_truncate_page);
