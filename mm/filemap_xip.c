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
				page = virt_to_page(empty_zero_page);
			} else {
				desc->error = PTR_ERR(page);
				goto out;
			}
		} else
			BUG_ON(!PageUptodate(page));

		/* If users can be writing to this page using arbitrary
		 * virtual addresses, take care about potential aliasing
		 * before reading the page on the kernel side.
		 */
		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
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

/*
 * This is the "read()" routine for all filesystems
 * that uses the get_xip_page address space operation.
 */
static ssize_t
__xip_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t *ppos)
{
	struct file *filp = iocb->ki_filp;
	ssize_t retval;
	unsigned long seg;
	size_t count;

	count = 0;
	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *iv = &iov[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		count += iv->iov_len;
		if (unlikely((ssize_t)(count|iv->iov_len) < 0))
			return -EINVAL;
		if (access_ok(VERIFY_WRITE, iv->iov_base, iv->iov_len))
			continue;
		if (seg == 0)
			return -EFAULT;
		nr_segs = seg;
		count -= iv->iov_len;	/* This segment is no good */
		break;
	}

	retval = 0;
	if (count) {
		for (seg = 0; seg < nr_segs; seg++) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.arg.buf = iov[seg].iov_base;
			desc.count = iov[seg].iov_len;
			if (desc.count == 0)
				continue;
			desc.error = 0;
			do_xip_mapping_read(filp->f_mapping, &filp->f_ra, filp,
					    ppos, &desc, file_read_actor);
			retval += desc.written;
			if (!retval) {
				retval = desc.error;
				break;
			}
		}
	}
	return retval;
}

ssize_t
xip_file_aio_read(struct kiocb *iocb, char __user *buf, size_t count,
		  loff_t pos)
{
	struct iovec local_iov = { .iov_base = buf, .iov_len = count };

	BUG_ON(iocb->ki_pos != pos);
	return __xip_file_aio_read(iocb, &local_iov, 1, &iocb->ki_pos);
}
EXPORT_SYMBOL_GPL(xip_file_aio_read);

ssize_t
xip_file_readv(struct file *filp, const struct iovec *iov,
	       unsigned long nr_segs, loff_t *ppos)
{
	struct kiocb kiocb;

	init_sync_kiocb(&kiocb, filp);
	return __xip_file_aio_read(&kiocb, iov, nr_segs, ppos);
}
EXPORT_SYMBOL_GPL(xip_file_readv);

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
 * empty_zero_page when found at pgoff. Should it go in rmap.c?
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

	spin_lock(&mapping->i_mmap_lock);
	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, pgoff, pgoff) {
		mm = vma->vm_mm;
		address = vma->vm_start +
			((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		BUG_ON(address < vma->vm_start || address >= vma->vm_end);
		/*
		 * We need the page_table_lock to protect us from page faults,
		 * munmap, fork, etc...
		 */
		pte = page_check_address(virt_to_page(empty_zero_page), mm,
					 address);
		if (!IS_ERR(pte)) {
			/* Nuke the page table entry. */
			flush_cache_page(vma, address, pte_pfn(pte));
			pteval = ptep_clear_flush(vma, address, pte);
			BUG_ON(pte_dirty(pteval));
			pte_unmap(pte);
			spin_unlock(&mm->page_table_lock);
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
		BUG_ON(!PageUptodate(page));
		return page;
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
		BUG_ON(!PageUptodate(page));
		/* unmap page at pgoff from all other vmas */
		__xip_unmap(mapping, pgoff);
	} else {
		/* not shared and writable, use empty_zero_page */
		page = virt_to_page(empty_zero_page);
	}

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
do_xip_file_write(struct kiocb *iocb, const struct iovec *iov,
		  unsigned long nr_segs, loff_t pos, loff_t *ppos,
		  size_t count)
{
	struct file *file = iocb->ki_filp;
	struct address_space * mapping = file->f_mapping;
	struct address_space_operations *a_ops = mapping->a_ops;
	struct inode 	*inode = mapping->host;
	long		status = 0;
	struct page	*page;
	size_t		bytes;
	const struct iovec *cur_iov = iov; /* current iovec */
	size_t		iov_base = 0;	   /* offset in the current iovec */
	char __user	*buf;
	ssize_t		written = 0;

	BUG_ON(!mapping->a_ops->get_xip_page);

	buf = iov->iov_base;
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

		BUG_ON(!PageUptodate(page));

		if (likely(nr_segs == 1))
			copied = filemap_copy_from_user(page, offset,
							buf, bytes);
		else
			copied = filemap_copy_from_user_iovec(page, offset,
						cur_iov, iov_base, bytes);
		flush_dcache_page(page);
		if (likely(copied > 0)) {
			status = copied;

			if (status >= 0) {
				written += status;
				count -= status;
				pos += status;
				buf += status;
				if (unlikely(nr_segs > 1))
					filemap_set_next_iovec(&cur_iov,
							&iov_base, status);
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
	 * cannot change under us because we hold i_sem.
	 */
	if (pos > inode->i_size) {
		i_size_write(inode, pos);
		mark_inode_dirty(inode);
	}

	return written ? written : status;
}

static ssize_t
xip_file_aio_write_nolock(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t *ppos)
{
	struct file *file = iocb->ki_filp;
	struct address_space * mapping = file->f_mapping;
	size_t ocount;		/* original count */
	size_t count;		/* after file limit checks */
	struct inode 	*inode = mapping->host;
	unsigned long	seg;
	loff_t		pos;
	ssize_t		written;
	ssize_t		err;

	ocount = 0;
	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *iv = &iov[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		ocount += iv->iov_len;
		if (unlikely((ssize_t)(ocount|iv->iov_len) < 0))
			return -EINVAL;
		if (access_ok(VERIFY_READ, iv->iov_base, iv->iov_len))
			continue;
		if (seg == 0)
			return -EFAULT;
		nr_segs = seg;
		ocount -= iv->iov_len;	/* This segment is no good */
		break;
	}

	count = ocount;
	pos = *ppos;

	vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE);

	written = 0;

	err = generic_write_checks(file, &pos, &count, S_ISBLK(inode->i_mode));
	if (err)
		goto out;

	if (count == 0)
		goto out;

	err = remove_suid(file->f_dentry);
	if (err)
		goto out;

	inode_update_time(inode, 1);

	/* use execute in place to copy directly to disk */
	written = do_xip_file_write (iocb, iov,
				  nr_segs, pos, ppos, count);
 out:
	return written ? written : err;
}

static ssize_t
__xip_file_write_nolock(struct file *file, const struct iovec *iov,
			unsigned long nr_segs, loff_t *ppos)
{
	struct kiocb kiocb;

	init_sync_kiocb(&kiocb, file);
	return xip_file_aio_write_nolock(&kiocb, iov, nr_segs, ppos);
}

ssize_t
xip_file_aio_write(struct kiocb *iocb, const char __user *buf,
		       size_t count, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t ret;
	struct iovec local_iov = { .iov_base = (void __user *)buf,
				   .iov_len = count };

	BUG_ON(iocb->ki_pos != pos);

	down(&inode->i_sem);
	ret = xip_file_aio_write_nolock(iocb, &local_iov, 1, &iocb->ki_pos);
	up(&inode->i_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(xip_file_aio_write);

ssize_t xip_file_writev(struct file *file, const struct iovec *iov,
			unsigned long nr_segs, loff_t *ppos)
{
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t ret;

	down(&inode->i_sem);
	ret = __xip_file_write_nolock(file, iov, nr_segs, ppos);
	up(&inode->i_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(xip_file_writev);

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
	int err;

	BUG_ON(!mapping->a_ops->get_xip_page);

	blocksize = 1 << mapping->host->i_blkbits;
	length = offset & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;

	length = blocksize - length;

	page = mapping->a_ops->get_xip_page(mapping,
					    index*(PAGE_SIZE/512), 0);
	err = -ENOMEM;
	if (!page)
		goto out;
	if (unlikely(IS_ERR(page))) {
		if (PTR_ERR(page) == -ENODATA) {
			/* Hole? No need to truncate */
			return 0;
		} else {
			err = PTR_ERR(page);
			goto out;
		}
	} else
		BUG_ON(!PageUptodate(page));
	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr + offset, 0, length);
	kunmap_atomic(kaddr, KM_USER0);

	flush_dcache_page(page);
	err = 0;
out:
	return err;
}
EXPORT_SYMBOL_GPL(xip_truncate_page);
