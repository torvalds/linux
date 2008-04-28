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
#include <linux/pagemap.h>
#include <linux/module.h>
#include <linux/uio.h>
#include <linux/rmap.h>
#include <linux/sched.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

/*
 * We do use our own empty page to avoid interference with other users
 * of ZERO_PAGE(), such as /dev/zero
 */
static struct page *__xip_sparse_page;

static struct page *xip_sparse_page(void)
{
	if (!__xip_sparse_page) {
		struct page *page = alloc_page(GFP_HIGHUSER | __GFP_ZERO);

		if (page) {
			static DEFINE_SPINLOCK(xip_alloc_lock);
			spin_lock(&xip_alloc_lock);
			if (!__xip_sparse_page)
				__xip_sparse_page = page;
			else
				__free_page(page);
			spin_unlock(&xip_alloc_lock);
		}
	}
	return __xip_sparse_page;
}

/*
 * This is a file read routine for execute in place files, and uses
 * the mapping->a_ops->get_xip_mem() function for the actual low-level
 * stuff.
 *
 * Note the struct file* is not used at all.  It may be NULL.
 */
static ssize_t
do_xip_mapping_read(struct address_space *mapping,
		    struct file_ra_state *_ra,
		    struct file *filp,
		    char __user *buf,
		    size_t len,
		    loff_t *ppos)
{
	struct inode *inode = mapping->host;
	pgoff_t index, end_index;
	unsigned long offset;
	loff_t isize, pos;
	size_t copied = 0, error = 0;

	BUG_ON(!mapping->a_ops->get_xip_mem);

	pos = *ppos;
	index = pos >> PAGE_CACHE_SHIFT;
	offset = pos & ~PAGE_CACHE_MASK;

	isize = i_size_read(inode);
	if (!isize)
		goto out;

	end_index = (isize - 1) >> PAGE_CACHE_SHIFT;
	do {
		unsigned long nr, left;
		void *xip_mem;
		unsigned long xip_pfn;
		int zero = 0;

		/* nr is the maximum number of bytes to copy from this page */
		nr = PAGE_CACHE_SIZE;
		if (index >= end_index) {
			if (index > end_index)
				goto out;
			nr = ((isize - 1) & ~PAGE_CACHE_MASK) + 1;
			if (nr <= offset) {
				goto out;
			}
		}
		nr = nr - offset;
		if (nr > len)
			nr = len;

		error = mapping->a_ops->get_xip_mem(mapping, index, 0,
							&xip_mem, &xip_pfn);
		if (unlikely(error)) {
			if (error == -ENODATA) {
				/* sparse */
				zero = 1;
			} else
				goto out;
		}

		/* If users can be writing to this page using arbitrary
		 * virtual addresses, take care about potential aliasing
		 * before reading the page on the kernel side.
		 */
		if (mapping_writably_mapped(mapping))
			/* address based flush */ ;

		/*
		 * Ok, we have the mem, so now we can copy it to user space...
		 *
		 * The actor routine returns how many bytes were actually used..
		 * NOTE! This may not be the same as how much of a user buffer
		 * we filled up (we may be padding etc), so we can only update
		 * "pos" here (the actor routine has to update the user buffer
		 * pointers and the remaining count).
		 */
		if (!zero)
			left = __copy_to_user(buf+copied, xip_mem+offset, nr);
		else
			left = __clear_user(buf + copied, nr);

		if (left) {
			error = -EFAULT;
			goto out;
		}

		copied += (nr - left);
		offset += (nr - left);
		index += offset >> PAGE_CACHE_SHIFT;
		offset &= ~PAGE_CACHE_MASK;
	} while (copied < len);

out:
	*ppos = pos + copied;
	if (filp)
		file_accessed(filp);

	return (copied ? copied : error);
}

ssize_t
xip_file_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	if (!access_ok(VERIFY_WRITE, buf, len))
		return -EFAULT;

	return do_xip_mapping_read(filp->f_mapping, &filp->f_ra, filp,
			    buf, len, ppos);
}
EXPORT_SYMBOL_GPL(xip_file_read);

/*
 * __xip_unmap is invoked from xip_unmap and
 * xip_write
 *
 * This function walks all vmas of the address_space and unmaps the
 * __xip_sparse_page when found at pgoff.
 */
static void
__xip_unmap (struct address_space * mapping,
		     unsigned long pgoff)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	struct prio_tree_iter iter;
	unsigned long address;
	pte_t *pte;
	pte_t pteval;
	spinlock_t *ptl;
	struct page *page;

	page = __xip_sparse_page;
	if (!page)
		return;

	spin_lock(&mapping->i_mmap_lock);
	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, pgoff, pgoff) {
		mm = vma->vm_mm;
		address = vma->vm_start +
			((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		BUG_ON(address < vma->vm_start || address >= vma->vm_end);
		pte = page_check_address(page, mm, address, &ptl);
		if (pte) {
			/* Nuke the page table entry. */
			flush_cache_page(vma, address, pte_pfn(*pte));
			pteval = ptep_clear_flush(vma, address, pte);
			page_remove_rmap(page, vma);
			dec_mm_counter(mm, file_rss);
			BUG_ON(pte_dirty(pteval));
			pte_unmap_unlock(pte, ptl);
			page_cache_release(page);
		}
	}
	spin_unlock(&mapping->i_mmap_lock);
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
		error = mapping->a_ops->get_xip_mem(mapping, vmf->pgoff, 1,
							&xip_mem, &xip_pfn);
		if (error)
			return VM_FAULT_SIGBUS;
		/* unmap sparse mappings at pgoff from all other vmas */
		__xip_unmap(mapping, vmf->pgoff);

found:
		err = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address,
							xip_pfn);
		if (err == -ENOMEM)
			return VM_FAULT_OOM;
		BUG_ON(err);
		return VM_FAULT_NOPAGE;
	} else {
		/* not shared and writable, use xip_sparse_page() */
		page = xip_sparse_page();
		if (!page)
			return VM_FAULT_OOM;

		page_cache_get(page);
		vmf->page = page;
		return 0;
	}
}

static struct vm_operations_struct xip_file_vm_ops = {
	.fault	= xip_file_fault,
};

int xip_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	BUG_ON(!file->f_mapping->a_ops->get_xip_mem);

	file_accessed(file);
	vma->vm_ops = &xip_file_vm_ops;
	vma->vm_flags |= VM_CAN_NONLINEAR | VM_MIXEDMAP;
	return 0;
}
EXPORT_SYMBOL_GPL(xip_file_mmap);

static ssize_t
__xip_file_write(struct file *filp, const char __user *buf,
		  size_t count, loff_t pos, loff_t *ppos)
{
	struct address_space * mapping = filp->f_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	struct inode 	*inode = mapping->host;
	long		status = 0;
	size_t		bytes;
	ssize_t		written = 0;

	BUG_ON(!mapping->a_ops->get_xip_mem);

	do {
		unsigned long index;
		unsigned long offset;
		size_t copied;
		void *xip_mem;
		unsigned long xip_pfn;

		offset = (pos & (PAGE_CACHE_SIZE -1)); /* Within page */
		index = pos >> PAGE_CACHE_SHIFT;
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > count)
			bytes = count;

		status = a_ops->get_xip_mem(mapping, index, 0,
						&xip_mem, &xip_pfn);
		if (status == -ENODATA) {
			/* we allocate a new page unmap it */
			status = a_ops->get_xip_mem(mapping, index, 1,
							&xip_mem, &xip_pfn);
			if (!status)
				/* unmap page at pgoff from all other vmas */
				__xip_unmap(mapping, index);
		}

		if (status)
			break;

		copied = bytes -
			__copy_from_user_nocache(xip_mem + offset, buf, bytes);

		if (likely(copied > 0)) {
			status = copied;

			if (status >= 0) {
				written += status;
				count -= status;
				pos += status;
				buf += status;
			}
		}
		if (unlikely(copied != bytes))
			if (status >= 0)
				status = -EFAULT;
		if (status < 0)
			break;
	} while (count);
	*ppos = pos;
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold i_mutex.
	 */
	if (pos > inode->i_size) {
		i_size_write(inode, pos);
		mark_inode_dirty(inode);
	}

	return written ? written : status;
}

ssize_t
xip_file_write(struct file *filp, const char __user *buf, size_t len,
	       loff_t *ppos)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	size_t count;
	loff_t pos;
	ssize_t ret;

	mutex_lock(&inode->i_mutex);

	if (!access_ok(VERIFY_READ, buf, len)) {
		ret=-EFAULT;
		goto out_up;
	}

	pos = *ppos;
	count = len;

	vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE);

	/* We can write back this queue in page reclaim */
	current->backing_dev_info = mapping->backing_dev_info;

	ret = generic_write_checks(filp, &pos, &count, S_ISBLK(inode->i_mode));
	if (ret)
		goto out_backing;
	if (count == 0)
		goto out_backing;

	ret = remove_suid(filp->f_path.dentry);
	if (ret)
		goto out_backing;

	file_update_time(filp);

	ret = __xip_file_write (filp, buf, count, pos, ppos);

 out_backing:
	current->backing_dev_info = NULL;
 out_up:
	mutex_unlock(&inode->i_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(xip_file_write);

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
