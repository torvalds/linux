/*
 * fs/dax.c - Direct Access filesystem code
 * Copyright (c) 2013-2014 Intel Corporation
 * Author: Matthew Wilcox <matthew.r.wilcox@intel.com>
 * Author: Ross Zwisler <ross.zwisler@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/atomic.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/memcontrol.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/vmstat.h>

int dax_clear_blocks(struct inode *inode, sector_t block, long size)
{
	struct block_device *bdev = inode->i_sb->s_bdev;
	sector_t sector = block << (inode->i_blkbits - 9);

	might_sleep();
	do {
		void *addr;
		unsigned long pfn;
		long count;

		count = bdev_direct_access(bdev, sector, &addr, &pfn, size);
		if (count < 0)
			return count;
		BUG_ON(size < count);
		while (count > 0) {
			unsigned pgsz = PAGE_SIZE - offset_in_page(addr);
			if (pgsz > count)
				pgsz = count;
			if (pgsz < PAGE_SIZE)
				memset(addr, 0, pgsz);
			else
				clear_page(addr);
			addr += pgsz;
			size -= pgsz;
			count -= pgsz;
			BUG_ON(pgsz & 511);
			sector += pgsz / 512;
			cond_resched();
		}
	} while (size);

	return 0;
}
EXPORT_SYMBOL_GPL(dax_clear_blocks);

static long dax_get_addr(struct buffer_head *bh, void **addr, unsigned blkbits)
{
	unsigned long pfn;
	sector_t sector = bh->b_blocknr << (blkbits - 9);
	return bdev_direct_access(bh->b_bdev, sector, addr, &pfn, bh->b_size);
}

static void dax_new_buf(void *addr, unsigned size, unsigned first, loff_t pos,
			loff_t end)
{
	loff_t final = end - pos + first; /* The final byte of the buffer */

	if (first > 0)
		memset(addr, 0, first);
	if (final < size)
		memset(addr + final, 0, size - final);
}

static bool buffer_written(struct buffer_head *bh)
{
	return buffer_mapped(bh) && !buffer_unwritten(bh);
}

/*
 * When ext4 encounters a hole, it returns without modifying the buffer_head
 * which means that we can't trust b_size.  To cope with this, we set b_state
 * to 0 before calling get_block and, if any bit is set, we know we can trust
 * b_size.  Unfortunate, really, since ext4 knows precisely how long a hole is
 * and would save us time calling get_block repeatedly.
 */
static bool buffer_size_valid(struct buffer_head *bh)
{
	return bh->b_state != 0;
}

static ssize_t dax_io(struct inode *inode, struct iov_iter *iter,
		      loff_t start, loff_t end, get_block_t get_block,
		      struct buffer_head *bh)
{
	ssize_t retval = 0;
	loff_t pos = start;
	loff_t max = start;
	loff_t bh_max = start;
	void *addr;
	bool hole = false;

	if (iov_iter_rw(iter) != WRITE)
		end = min(end, i_size_read(inode));

	while (pos < end) {
		unsigned len;
		if (pos == max) {
			unsigned blkbits = inode->i_blkbits;
			sector_t block = pos >> blkbits;
			unsigned first = pos - (block << blkbits);
			long size;

			if (pos == bh_max) {
				bh->b_size = PAGE_ALIGN(end - pos);
				bh->b_state = 0;
				retval = get_block(inode, block, bh,
						   iov_iter_rw(iter) == WRITE);
				if (retval)
					break;
				if (!buffer_size_valid(bh))
					bh->b_size = 1 << blkbits;
				bh_max = pos - first + bh->b_size;
			} else {
				unsigned done = bh->b_size -
						(bh_max - (pos - first));
				bh->b_blocknr += done >> blkbits;
				bh->b_size -= done;
			}

			hole = iov_iter_rw(iter) != WRITE && !buffer_written(bh);
			if (hole) {
				addr = NULL;
				size = bh->b_size - first;
			} else {
				retval = dax_get_addr(bh, &addr, blkbits);
				if (retval < 0)
					break;
				if (buffer_unwritten(bh) || buffer_new(bh))
					dax_new_buf(addr, retval, first, pos,
									end);
				addr += first;
				size = retval - first;
			}
			max = min(pos + size, end);
		}

		if (iov_iter_rw(iter) == WRITE)
			len = copy_from_iter_nocache(addr, max - pos, iter);
		else if (!hole)
			len = copy_to_iter(addr, max - pos, iter);
		else
			len = iov_iter_zero(max - pos, iter);

		if (!len)
			break;

		pos += len;
		addr += len;
	}

	return (pos == start) ? retval : pos - start;
}

/**
 * dax_do_io - Perform I/O to a DAX file
 * @iocb: The control block for this I/O
 * @inode: The file which the I/O is directed at
 * @iter: The addresses to do I/O from or to
 * @pos: The file offset where the I/O starts
 * @get_block: The filesystem method used to translate file offsets to blocks
 * @end_io: A filesystem callback for I/O completion
 * @flags: See below
 *
 * This function uses the same locking scheme as do_blockdev_direct_IO:
 * If @flags has DIO_LOCKING set, we assume that the i_mutex is held by the
 * caller for writes.  For reads, we take and release the i_mutex ourselves.
 * If DIO_LOCKING is not set, the filesystem takes care of its own locking.
 * As with do_blockdev_direct_IO(), we increment i_dio_count while the I/O
 * is in progress.
 */
ssize_t dax_do_io(struct kiocb *iocb, struct inode *inode,
		  struct iov_iter *iter, loff_t pos, get_block_t get_block,
		  dio_iodone_t end_io, int flags)
{
	struct buffer_head bh;
	ssize_t retval = -EINVAL;
	loff_t end = pos + iov_iter_count(iter);

	memset(&bh, 0, sizeof(bh));

	if ((flags & DIO_LOCKING) && iov_iter_rw(iter) == READ) {
		struct address_space *mapping = inode->i_mapping;
		mutex_lock(&inode->i_mutex);
		retval = filemap_write_and_wait_range(mapping, pos, end - 1);
		if (retval) {
			mutex_unlock(&inode->i_mutex);
			goto out;
		}
	}

	/* Protects against truncate */
	if (!(flags & DIO_SKIP_DIO_COUNT))
		inode_dio_begin(inode);

	retval = dax_io(inode, iter, pos, end, get_block, &bh);

	if ((flags & DIO_LOCKING) && iov_iter_rw(iter) == READ)
		mutex_unlock(&inode->i_mutex);

	if ((retval > 0) && end_io)
		end_io(iocb, pos, retval, bh.b_private);

	if (!(flags & DIO_SKIP_DIO_COUNT))
		inode_dio_end(inode);
 out:
	return retval;
}
EXPORT_SYMBOL_GPL(dax_do_io);

/*
 * The user has performed a load from a hole in the file.  Allocating
 * a new page in the file would cause excessive storage usage for
 * workloads with sparse files.  We allocate a page cache page instead.
 * We'll kick it out of the page cache if it's ever written to,
 * otherwise it will simply fall out of the page cache under memory
 * pressure without ever having been dirtied.
 */
static int dax_load_hole(struct address_space *mapping, struct page *page,
							struct vm_fault *vmf)
{
	unsigned long size;
	struct inode *inode = mapping->host;
	if (!page)
		page = find_or_create_page(mapping, vmf->pgoff,
						GFP_KERNEL | __GFP_ZERO);
	if (!page)
		return VM_FAULT_OOM;
	/* Recheck i_size under page lock to avoid truncate race */
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (vmf->pgoff >= size) {
		unlock_page(page);
		page_cache_release(page);
		return VM_FAULT_SIGBUS;
	}

	vmf->page = page;
	return VM_FAULT_LOCKED;
}

static int copy_user_bh(struct page *to, struct buffer_head *bh,
			unsigned blkbits, unsigned long vaddr)
{
	void *vfrom, *vto;
	if (dax_get_addr(bh, &vfrom, blkbits) < 0)
		return -EIO;
	vto = kmap_atomic(to);
	copy_user_page(vto, vfrom, vaddr, to);
	kunmap_atomic(vto);
	return 0;
}

static int dax_insert_mapping(struct inode *inode, struct buffer_head *bh,
			struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct address_space *mapping = inode->i_mapping;
	sector_t sector = bh->b_blocknr << (inode->i_blkbits - 9);
	unsigned long vaddr = (unsigned long)vmf->virtual_address;
	void *addr;
	unsigned long pfn;
	pgoff_t size;
	int error;

	i_mmap_lock_read(mapping);

	/*
	 * Check truncate didn't happen while we were allocating a block.
	 * If it did, this block may or may not be still allocated to the
	 * file.  We can't tell the filesystem to free it because we can't
	 * take i_mutex here.  In the worst case, the file still has blocks
	 * allocated past the end of the file.
	 */
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (unlikely(vmf->pgoff >= size)) {
		error = -EIO;
		goto out;
	}

	error = bdev_direct_access(bh->b_bdev, sector, &addr, &pfn, bh->b_size);
	if (error < 0)
		goto out;
	if (error < PAGE_SIZE) {
		error = -EIO;
		goto out;
	}

	if (buffer_unwritten(bh) || buffer_new(bh))
		clear_page(addr);

	error = vm_insert_mixed(vma, vaddr, pfn);

 out:
	i_mmap_unlock_read(mapping);

	return error;
}

/**
 * __dax_fault - handle a page fault on a DAX file
 * @vma: The virtual memory area where the fault occurred
 * @vmf: The description of the fault
 * @get_block: The filesystem method used to translate file offsets to blocks
 * @complete_unwritten: The filesystem method used to convert unwritten blocks
 *	to written so the data written to them is exposed. This is required for
 *	required by write faults for filesystems that will return unwritten
 *	extent mappings from @get_block, but it is optional for reads as
 *	dax_insert_mapping() will always zero unwritten blocks. If the fs does
 *	not support unwritten extents, the it should pass NULL.
 *
 * When a page fault occurs, filesystems may call this helper in their
 * fault handler for DAX files. __dax_fault() assumes the caller has done all
 * the necessary locking for the page fault to proceed successfully.
 */
int __dax_fault(struct vm_area_struct *vma, struct vm_fault *vmf,
			get_block_t get_block, dax_iodone_t complete_unwritten)
{
	struct file *file = vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct page *page;
	struct buffer_head bh;
	unsigned long vaddr = (unsigned long)vmf->virtual_address;
	unsigned blkbits = inode->i_blkbits;
	sector_t block;
	pgoff_t size;
	int error;
	int major = 0;

	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (vmf->pgoff >= size)
		return VM_FAULT_SIGBUS;

	memset(&bh, 0, sizeof(bh));
	block = (sector_t)vmf->pgoff << (PAGE_SHIFT - blkbits);
	bh.b_size = PAGE_SIZE;

 repeat:
	page = find_get_page(mapping, vmf->pgoff);
	if (page) {
		if (!lock_page_or_retry(page, vma->vm_mm, vmf->flags)) {
			page_cache_release(page);
			return VM_FAULT_RETRY;
		}
		if (unlikely(page->mapping != mapping)) {
			unlock_page(page);
			page_cache_release(page);
			goto repeat;
		}
		size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
		if (unlikely(vmf->pgoff >= size)) {
			/*
			 * We have a struct page covering a hole in the file
			 * from a read fault and we've raced with a truncate
			 */
			error = -EIO;
			goto unlock_page;
		}
	}

	error = get_block(inode, block, &bh, 0);
	if (!error && (bh.b_size < PAGE_SIZE))
		error = -EIO;		/* fs corruption? */
	if (error)
		goto unlock_page;

	if (!buffer_mapped(&bh) && !buffer_unwritten(&bh) && !vmf->cow_page) {
		if (vmf->flags & FAULT_FLAG_WRITE) {
			error = get_block(inode, block, &bh, 1);
			count_vm_event(PGMAJFAULT);
			mem_cgroup_count_vm_event(vma->vm_mm, PGMAJFAULT);
			major = VM_FAULT_MAJOR;
			if (!error && (bh.b_size < PAGE_SIZE))
				error = -EIO;
			if (error)
				goto unlock_page;
		} else {
			return dax_load_hole(mapping, page, vmf);
		}
	}

	if (vmf->cow_page) {
		struct page *new_page = vmf->cow_page;
		if (buffer_written(&bh))
			error = copy_user_bh(new_page, &bh, blkbits, vaddr);
		else
			clear_user_highpage(new_page, vaddr);
		if (error)
			goto unlock_page;
		vmf->page = page;
		if (!page) {
			i_mmap_lock_read(mapping);
			/* Check we didn't race with truncate */
			size = (i_size_read(inode) + PAGE_SIZE - 1) >>
								PAGE_SHIFT;
			if (vmf->pgoff >= size) {
				i_mmap_unlock_read(mapping);
				error = -EIO;
				goto out;
			}
		}
		return VM_FAULT_LOCKED;
	}

	/* Check we didn't race with a read fault installing a new page */
	if (!page && major)
		page = find_lock_page(mapping, vmf->pgoff);

	if (page) {
		unmap_mapping_range(mapping, vmf->pgoff << PAGE_SHIFT,
							PAGE_CACHE_SIZE, 0);
		delete_from_page_cache(page);
		unlock_page(page);
		page_cache_release(page);
	}

	/*
	 * If we successfully insert the new mapping over an unwritten extent,
	 * we need to ensure we convert the unwritten extent. If there is an
	 * error inserting the mapping, the filesystem needs to leave it as
	 * unwritten to prevent exposure of the stale underlying data to
	 * userspace, but we still need to call the completion function so
	 * the private resources on the mapping buffer can be released. We
	 * indicate what the callback should do via the uptodate variable, same
	 * as for normal BH based IO completions.
	 */
	error = dax_insert_mapping(inode, &bh, vma, vmf);
	if (buffer_unwritten(&bh)) {
		if (complete_unwritten)
			complete_unwritten(&bh, !error);
		else
			WARN_ON_ONCE(!(vmf->flags & FAULT_FLAG_WRITE));
	}

 out:
	if (error == -ENOMEM)
		return VM_FAULT_OOM | major;
	/* -EBUSY is fine, somebody else faulted on the same PTE */
	if ((error < 0) && (error != -EBUSY))
		return VM_FAULT_SIGBUS | major;
	return VM_FAULT_NOPAGE | major;

 unlock_page:
	if (page) {
		unlock_page(page);
		page_cache_release(page);
	}
	goto out;
}
EXPORT_SYMBOL(__dax_fault);

/**
 * dax_fault - handle a page fault on a DAX file
 * @vma: The virtual memory area where the fault occurred
 * @vmf: The description of the fault
 * @get_block: The filesystem method used to translate file offsets to blocks
 *
 * When a page fault occurs, filesystems may call this helper in their
 * fault handler for DAX files.
 */
int dax_fault(struct vm_area_struct *vma, struct vm_fault *vmf,
	      get_block_t get_block, dax_iodone_t complete_unwritten)
{
	int result;
	struct super_block *sb = file_inode(vma->vm_file)->i_sb;

	if (vmf->flags & FAULT_FLAG_WRITE) {
		sb_start_pagefault(sb);
		file_update_time(vma->vm_file);
	}
	result = __dax_fault(vma, vmf, get_block, complete_unwritten);
	if (vmf->flags & FAULT_FLAG_WRITE)
		sb_end_pagefault(sb);

	return result;
}
EXPORT_SYMBOL_GPL(dax_fault);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 * The 'colour' (ie low bits) within a PMD of a page offset.  This comes up
 * more often than one might expect in the below function.
 */
#define PG_PMD_COLOUR	((PMD_SIZE >> PAGE_SHIFT) - 1)

int __dax_pmd_fault(struct vm_area_struct *vma, unsigned long address,
		pmd_t *pmd, unsigned int flags, get_block_t get_block,
		dax_iodone_t complete_unwritten)
{
	struct file *file = vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct buffer_head bh;
	unsigned blkbits = inode->i_blkbits;
	unsigned long pmd_addr = address & PMD_MASK;
	bool write = flags & FAULT_FLAG_WRITE;
	long length;
	void *kaddr;
	pgoff_t size, pgoff;
	sector_t block, sector;
	unsigned long pfn;
	int result = 0;

	/* Fall back to PTEs if we're going to COW */
	if (write && !(vma->vm_flags & VM_SHARED))
		return VM_FAULT_FALLBACK;
	/* If the PMD would extend outside the VMA */
	if (pmd_addr < vma->vm_start)
		return VM_FAULT_FALLBACK;
	if ((pmd_addr + PMD_SIZE) > vma->vm_end)
		return VM_FAULT_FALLBACK;

	pgoff = ((pmd_addr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (pgoff >= size)
		return VM_FAULT_SIGBUS;
	/* If the PMD would cover blocks out of the file */
	if ((pgoff | PG_PMD_COLOUR) >= size)
		return VM_FAULT_FALLBACK;

	memset(&bh, 0, sizeof(bh));
	block = (sector_t)pgoff << (PAGE_SHIFT - blkbits);

	bh.b_size = PMD_SIZE;
	length = get_block(inode, block, &bh, write);
	if (length)
		return VM_FAULT_SIGBUS;
	i_mmap_lock_read(mapping);

	/*
	 * If the filesystem isn't willing to tell us the length of a hole,
	 * just fall back to PTEs.  Calling get_block 512 times in a loop
	 * would be silly.
	 */
	if (!buffer_size_valid(&bh) || bh.b_size < PMD_SIZE)
		goto fallback;

	/* Guard against a race with truncate */
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (pgoff >= size) {
		result = VM_FAULT_SIGBUS;
		goto out;
	}
	if ((pgoff | PG_PMD_COLOUR) >= size)
		goto fallback;

	if (is_huge_zero_pmd(*pmd))
		unmap_mapping_range(mapping, pgoff << PAGE_SHIFT, PMD_SIZE, 0);

	if (!write && !buffer_mapped(&bh) && buffer_uptodate(&bh)) {
		bool set;
		spinlock_t *ptl;
		struct mm_struct *mm = vma->vm_mm;
		struct page *zero_page = get_huge_zero_page();
		if (unlikely(!zero_page))
			goto fallback;

		ptl = pmd_lock(mm, pmd);
		set = set_huge_zero_page(NULL, mm, vma, pmd_addr, pmd,
								zero_page);
		spin_unlock(ptl);
		result = VM_FAULT_NOPAGE;
	} else {
		sector = bh.b_blocknr << (blkbits - 9);
		length = bdev_direct_access(bh.b_bdev, sector, &kaddr, &pfn,
						bh.b_size);
		if (length < 0) {
			result = VM_FAULT_SIGBUS;
			goto out;
		}
		if ((length < PMD_SIZE) || (pfn & PG_PMD_COLOUR))
			goto fallback;

		if (buffer_unwritten(&bh) || buffer_new(&bh)) {
			int i;
			for (i = 0; i < PTRS_PER_PMD; i++)
				clear_page(kaddr + i * PAGE_SIZE);
			count_vm_event(PGMAJFAULT);
			mem_cgroup_count_vm_event(vma->vm_mm, PGMAJFAULT);
			result |= VM_FAULT_MAJOR;
		}

		result |= vmf_insert_pfn_pmd(vma, address, pmd, pfn, write);
	}

 out:
	i_mmap_unlock_read(mapping);

	if (buffer_unwritten(&bh))
		complete_unwritten(&bh, !(result & VM_FAULT_ERROR));

	return result;

 fallback:
	count_vm_event(THP_FAULT_FALLBACK);
	result = VM_FAULT_FALLBACK;
	goto out;
}
EXPORT_SYMBOL_GPL(__dax_pmd_fault);

/**
 * dax_pmd_fault - handle a PMD fault on a DAX file
 * @vma: The virtual memory area where the fault occurred
 * @vmf: The description of the fault
 * @get_block: The filesystem method used to translate file offsets to blocks
 *
 * When a page fault occurs, filesystems may call this helper in their
 * pmd_fault handler for DAX files.
 */
int dax_pmd_fault(struct vm_area_struct *vma, unsigned long address,
			pmd_t *pmd, unsigned int flags, get_block_t get_block,
			dax_iodone_t complete_unwritten)
{
	int result;
	struct super_block *sb = file_inode(vma->vm_file)->i_sb;

	if (flags & FAULT_FLAG_WRITE) {
		sb_start_pagefault(sb);
		file_update_time(vma->vm_file);
	}
	result = __dax_pmd_fault(vma, address, pmd, flags, get_block,
				complete_unwritten);
	if (flags & FAULT_FLAG_WRITE)
		sb_end_pagefault(sb);

	return result;
}
EXPORT_SYMBOL_GPL(dax_pmd_fault);
#endif /* CONFIG_TRANSPARENT_HUGEPAGES */

/**
 * dax_pfn_mkwrite - handle first write to DAX page
 * @vma: The virtual memory area where the fault occurred
 * @vmf: The description of the fault
 *
 */
int dax_pfn_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct super_block *sb = file_inode(vma->vm_file)->i_sb;

	sb_start_pagefault(sb);
	file_update_time(vma->vm_file);
	sb_end_pagefault(sb);
	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL_GPL(dax_pfn_mkwrite);

/**
 * dax_zero_page_range - zero a range within a page of a DAX file
 * @inode: The file being truncated
 * @from: The file offset that is being truncated to
 * @length: The number of bytes to zero
 * @get_block: The filesystem method used to translate file offsets to blocks
 *
 * This function can be called by a filesystem when it is zeroing part of a
 * page in a DAX file.  This is intended for hole-punch operations.  If
 * you are truncating a file, the helper function dax_truncate_page() may be
 * more convenient.
 *
 * We work in terms of PAGE_CACHE_SIZE here for commonality with
 * block_truncate_page(), but we could go down to PAGE_SIZE if the filesystem
 * took care of disposing of the unnecessary blocks.  Even if the filesystem
 * block size is smaller than PAGE_SIZE, we have to zero the rest of the page
 * since the file might be mmapped.
 */
int dax_zero_page_range(struct inode *inode, loff_t from, unsigned length,
							get_block_t get_block)
{
	struct buffer_head bh;
	pgoff_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	int err;

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;
	BUG_ON((offset + length) > PAGE_CACHE_SIZE);

	memset(&bh, 0, sizeof(bh));
	bh.b_size = PAGE_CACHE_SIZE;
	err = get_block(inode, index, &bh, 0);
	if (err < 0)
		return err;
	if (buffer_written(&bh)) {
		void *addr;
		err = dax_get_addr(&bh, &addr, inode->i_blkbits);
		if (err < 0)
			return err;
		memset(addr + offset, 0, length);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dax_zero_page_range);

/**
 * dax_truncate_page - handle a partial page being truncated in a DAX file
 * @inode: The file being truncated
 * @from: The file offset that is being truncated to
 * @get_block: The filesystem method used to translate file offsets to blocks
 *
 * Similar to block_truncate_page(), this function can be called by a
 * filesystem when it is truncating a DAX file to handle the partial page.
 *
 * We work in terms of PAGE_CACHE_SIZE here for commonality with
 * block_truncate_page(), but we could go down to PAGE_SIZE if the filesystem
 * took care of disposing of the unnecessary blocks.  Even if the filesystem
 * block size is smaller than PAGE_SIZE, we have to zero the rest of the page
 * since the file might be mmapped.
 */
int dax_truncate_page(struct inode *inode, loff_t from, get_block_t get_block)
{
	unsigned length = PAGE_CACHE_ALIGN(from) - from;
	return dax_zero_page_range(inode, from, length, get_block);
}
EXPORT_SYMBOL_GPL(dax_truncate_page);
