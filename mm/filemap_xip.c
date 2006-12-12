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
#include <asm/tlbflush.h>
#include "filemap.h"

/*
 * This is a file read routine for execute in place files, and uses
 * the mapping->a_ops->get_xip_page() function for the actual low-level
 * stuff.
 *
 * Note the struct file* is not used at all.  It may be NULL.
 */
static void
do_xip_mapping_read(struct address_space *mapping,
		    struct file_ra_state *_ra,
		    struct file *filp,
		    loff_t *ppos,
		    read_descriptor_t *desc,
		    read_actor_t actor)
{
	struct inode *inode = mapping->host;
	unsigned long index, end_index, offset;
	loff_t isize;

	BUG_ON(!mapping->a_ops->get_xip_page);

	index = *ppos >> PAGE_CACHE_SHIFT;
	offset = *ppos & ~PAGE_CACHE_MASK;

	isize = i_size_read(inode);
	if (!isize)
		goto out;

	end_index = (isize - 1) >> PAGE_CACHE_SHIFT;
	for (;;) {
		struct page *page;
		unsigned long nr, ret;

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

		page = mapping->a_ops->get_xip_page(mapping,
			index*(PAGE_SIZE/512), 0);
		if (!page)
			goto no_xip_page;
		if (unlikely(IS_ERR(page))) {
			if (PTR_ERR(page) == -ENODATA) {
				/* sparse */
				page = ZERO_PAGE(0);
			} else {
				desc->error = PTR_ERR(page);
				goto out;
			}
		}

		/* If users can be writing to this page using arbitrary
		 * virtual addresses, take care about potential aliasing
		 * before reading the page on the kernel side.
		 */
		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		/*
		 * Ok, we have the page, so now we can copy it to user space...
		 *
		 * The actor routine returns how many bytes were actually used..
		 * NOTE! This may not be the same as how much of a user buffer
		 * we filled up (we may be padding etc), so we can only update
		 * "pos" here (the actor routine has to update the user buffer
		 * pointers and the remaining count).
		 */
		ret = actor(desc, page, offset, nr);
		offset += ret;
		index += offset >> PAGE_CACHE_SHIFT;
		offset &= ~PAGE_CACHE_MASK;

		if (ret == nr && desc->count)
			continue;
		goto out;

no_xip_page:
		/* Did not get the page. Report it */
		desc->error = -EIO;
		goto out;
	}

out:
	*ppos = ((loff_t) index << PAGE_CACHE_SHIFT) + offset;
	if (filp)
		file_accessed(filp);
}

ssize_t
xip_file_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	read_descriptor_t desc;

	if (!access_ok(VERIFY_WRITE, buf, len))
		return -EFAULT;

	desc.written = 0;
	desc.arg.buf = buf;
	desc.count = len;
	desc.error = 0;

	do_xip_mapping_read(filp->f_mapping, &filp->f_ra, filp,
			    ppos, &desc, file_read_actor);

	if (desc.written)
		return desc.written;
	else
		return desc.error;
}
EXPORT_SYMBOL_GPL(xip_file_read);

ssize_t
xip_file_sendfile(struct file *in_file, loff_t *ppos,
	     size_t count, read_actor_t actor, void *target)
{
	read_descriptor_t desc;

	if (!count)
		return 0;

	desc.written = 0;
	desc.count = count;
	desc.arg.data = target;
	desc.error = 0;

	do_xip_mapping_read(in_file->f_mapping, &in_file->f_ra, in_file,
			    ppos, &desc, actor);
	if (desc.written)
		return desc.written;
	return desc.error;
}
EXPORT_SYMBOL_GPL(xip_file_sendfile);

/*
 * __xip_unmap is invoked from xip_unmap and
 * xip_write
 *
 * This function walks all vmas of the address_space and unmaps the
 * ZERO_PAGE when found at pgoff. Should it go in rmap.c?
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

	spin_lock(&mapping->i_mmap_lock);
	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, pgoff, pgoff) {
		mm = vma->vm_mm;
		address = vma->vm_start +
			((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		BUG_ON(address < vma->vm_start || address >= vma->vm_end);
		page = ZERO_PAGE(address);
		pte = page_check_address(page, mm, address, &ptl);
		if (pte) {
			/* Nuke the page table entry. */
			flush_cache_page(vma, address, pte_pfn(*pte));
			pteval = ptep_clear_flush(vma, address, pte);
			page_remove_rmap(page);
			dec_mm_counter(mm, file_rss);
			BUG_ON(pte_dirty(pteval));
			pte_unmap_unlock(pte, ptl);
			page_cache_release(page);
		}
	}
	spin_unlock(&mapping->i_mmap_lock);
}

/*
 * xip_nopage() is invoked via the vma operations vector for a
 * mapped memory region to read in file data during a page fault.
 *
 * This function is derived from filemap_nopage, but used for execute in place
 */
static struct page *
xip_file_nopage(struct vm_area_struct * area,
		   unsigned long address,
		   int *type)
{
	struct file *file = area->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct page *page;
	unsigned long size, pgoff, endoff;

	pgoff = ((address - area->vm_start) >> PAGE_CACHE_SHIFT)
		+ area->vm_pgoff;
	endoff = ((area->vm_end - area->vm_start) >> PAGE_CACHE_SHIFT)
		+ area->vm_pgoff;

	size = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	if (pgoff >= size) {
		return NULL;
	}

	page = mapping->a_ops->get_xip_page(mapping, pgoff*(PAGE_SIZE/512), 0);
	if (!IS_ERR(page)) {
		goto out;
	}
	if (PTR_ERR(page) != -ENODATA)
		return NULL;

	/* sparse block */
	if ((area->vm_flags & (VM_WRITE | VM_MAYWRITE)) &&
	    (area->vm_flags & (VM_SHARED| VM_MAYSHARE)) &&
	    (!(mapping->host->i_sb->s_flags & MS_RDONLY))) {
		/* maybe shared writable, allocate new block */
		page = mapping->a_ops->get_xip_page (mapping,
			pgoff*(PAGE_SIZE/512), 1);
		if (IS_ERR(page))
			return NULL;
		/* unmap page at pgoff from all other vmas */
		__xip_unmap(mapping, pgoff);
	} else {
		/* not shared and writable, use ZERO_PAGE() */
		page = ZERO_PAGE(address);
	}

out:
	page_cache_get(page);
	return page;
}

static struct vm_operations_struct xip_file_vm_ops = {
	.nopage         = xip_file_nopage,
};

int xip_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	BUG_ON(!file->f_mapping->a_ops->get_xip_page);

	file_accessed(file);
	vma->vm_ops = &xip_file_vm_ops;
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
	struct page	*page;
	size_t		bytes;
	ssize_t		written = 0;

	BUG_ON(!mapping->a_ops->get_xip_page);

	do {
		unsigned long index;
		unsigned long offset;
		size_t copied;

		offset = (pos & (PAGE_CACHE_SIZE -1)); /* Within page */
		index = pos >> PAGE_CACHE_SHIFT;
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > count)
			bytes = count;

		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 */
		fault_in_pages_readable(buf, bytes);

		page = a_ops->get_xip_page(mapping,
					   index*(PAGE_SIZE/512), 0);
		if (IS_ERR(page) && (PTR_ERR(page) == -ENODATA)) {
			/* we allocate a new page unmap it */
			page = a_ops->get_xip_page(mapping,
						   index*(PAGE_SIZE/512), 1);
			if (!IS_ERR(page))
				/* unmap page at pgoff from all other vmas */
				__xip_unmap(mapping, index);
		}

		if (IS_ERR(page)) {
			status = PTR_ERR(page);
			break;
		}

		copied = filemap_copy_from_user(page, offset, buf, bytes);
		flush_dcache_page(page);
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
 * functionality is analog to block_truncate_page but does use get_xip_page
 * to get the page instead of page cache
 */
int
xip_truncate_page(struct address_space *mapping, loff_t from)
{
	pgoff_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned blocksize;
	unsigned length;
	struct page *page;
	void *kaddr;

	BUG_ON(!mapping->a_ops->get_xip_page);

	blocksize = 1 << mapping->host->i_blkbits;
	length = offset & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;

	length = blocksize - length;

	page = mapping->a_ops->get_xip_page(mapping,
					    index*(PAGE_SIZE/512), 0);
	if (!page)
		return -ENOMEM;
	if (unlikely(IS_ERR(page))) {
		if (PTR_ERR(page) == -ENODATA)
			/* Hole? No need to truncate */
			return 0;
		else
			return PTR_ERR(page);
	}
	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr + offset, 0, length);
	kunmap_atomic(kaddr, KM_USER0);

	flush_dcache_page(page);
	return 0;
}
EXPORT_SYMBOL_GPL(xip_truncate_page);
