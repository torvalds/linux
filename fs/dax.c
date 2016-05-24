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
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/memcontrol.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pagevec.h>
#include <linux/pmem.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/vmstat.h>
#include <linux/pfn_t.h>
#include <linux/sizes.h>

#define RADIX_DAX_MASK	0xf
#define RADIX_DAX_SHIFT	4
#define RADIX_DAX_PTE  (0x4 | RADIX_TREE_EXCEPTIONAL_ENTRY)
#define RADIX_DAX_PMD  (0x8 | RADIX_TREE_EXCEPTIONAL_ENTRY)
#define RADIX_DAX_TYPE(entry) ((unsigned long)entry & RADIX_DAX_MASK)
#define RADIX_DAX_SECTOR(entry) (((unsigned long)entry >> RADIX_DAX_SHIFT))
#define RADIX_DAX_ENTRY(sector, pmd) ((void *)((unsigned long)sector << \
		RADIX_DAX_SHIFT | (pmd ? RADIX_DAX_PMD : RADIX_DAX_PTE)))

static long dax_map_atomic(struct block_device *bdev, struct blk_dax_ctl *dax)
{
	struct request_queue *q = bdev->bd_queue;
	long rc = -EIO;

	dax->addr = (void __pmem *) ERR_PTR(-EIO);
	if (blk_queue_enter(q, true) != 0)
		return rc;

	rc = bdev_direct_access(bdev, dax);
	if (rc < 0) {
		dax->addr = (void __pmem *) ERR_PTR(rc);
		blk_queue_exit(q);
		return rc;
	}
	return rc;
}

static void dax_unmap_atomic(struct block_device *bdev,
		const struct blk_dax_ctl *dax)
{
	if (IS_ERR(dax->addr))
		return;
	blk_queue_exit(bdev->bd_queue);
}

struct page *read_dax_sector(struct block_device *bdev, sector_t n)
{
	struct page *page = alloc_pages(GFP_KERNEL, 0);
	struct blk_dax_ctl dax = {
		.size = PAGE_SIZE,
		.sector = n & ~((((int) PAGE_SIZE) / 512) - 1),
	};
	long rc;

	if (!page)
		return ERR_PTR(-ENOMEM);

	rc = dax_map_atomic(bdev, &dax);
	if (rc < 0)
		return ERR_PTR(rc);
	memcpy_from_pmem(page_address(page), dax.addr, PAGE_SIZE);
	dax_unmap_atomic(bdev, &dax);
	return page;
}

/*
 * dax_clear_sectors() is called from within transaction context from XFS,
 * and hence this means the stack from this point must follow GFP_NOFS
 * semantics for all operations.
 */
int dax_clear_sectors(struct block_device *bdev, sector_t _sector, long _size)
{
	struct blk_dax_ctl dax = {
		.sector = _sector,
		.size = _size,
	};

	might_sleep();
	do {
		long count, sz;

		count = dax_map_atomic(bdev, &dax);
		if (count < 0)
			return count;
		sz = min_t(long, count, SZ_128K);
		clear_pmem(dax.addr, sz);
		dax.size -= sz;
		dax.sector += sz / 512;
		dax_unmap_atomic(bdev, &dax);
		cond_resched();
	} while (dax.size);

	wmb_pmem();
	return 0;
}
EXPORT_SYMBOL_GPL(dax_clear_sectors);

/* the clear_pmem() calls are ordered by a wmb_pmem() in the caller */
static void dax_new_buf(void __pmem *addr, unsigned size, unsigned first,
		loff_t pos, loff_t end)
{
	loff_t final = end - pos + first; /* The final byte of the buffer */

	if (first > 0)
		clear_pmem(addr, first);
	if (final < size)
		clear_pmem(addr + final, size - final);
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


static sector_t to_sector(const struct buffer_head *bh,
		const struct inode *inode)
{
	sector_t sector = bh->b_blocknr << (inode->i_blkbits - 9);

	return sector;
}

static ssize_t dax_io(struct inode *inode, struct iov_iter *iter,
		      loff_t start, loff_t end, get_block_t get_block,
		      struct buffer_head *bh)
{
	loff_t pos = start, max = start, bh_max = start;
	bool hole = false, need_wmb = false;
	struct block_device *bdev = NULL;
	int rw = iov_iter_rw(iter), rc;
	long map_len = 0;
	struct blk_dax_ctl dax = {
		.addr = (void __pmem *) ERR_PTR(-EIO),
	};

	if (rw == READ)
		end = min(end, i_size_read(inode));

	while (pos < end) {
		size_t len;
		if (pos == max) {
			unsigned blkbits = inode->i_blkbits;
			long page = pos >> PAGE_SHIFT;
			sector_t block = page << (PAGE_SHIFT - blkbits);
			unsigned first = pos - (block << blkbits);
			long size;

			if (pos == bh_max) {
				bh->b_size = PAGE_ALIGN(end - pos);
				bh->b_state = 0;
				rc = get_block(inode, block, bh, rw == WRITE);
				if (rc)
					break;
				if (!buffer_size_valid(bh))
					bh->b_size = 1 << blkbits;
				bh_max = pos - first + bh->b_size;
				bdev = bh->b_bdev;
			} else {
				unsigned done = bh->b_size -
						(bh_max - (pos - first));
				bh->b_blocknr += done >> blkbits;
				bh->b_size -= done;
			}

			hole = rw == READ && !buffer_written(bh);
			if (hole) {
				size = bh->b_size - first;
			} else {
				dax_unmap_atomic(bdev, &dax);
				dax.sector = to_sector(bh, inode);
				dax.size = bh->b_size;
				map_len = dax_map_atomic(bdev, &dax);
				if (map_len < 0) {
					rc = map_len;
					break;
				}
				if (buffer_unwritten(bh) || buffer_new(bh)) {
					dax_new_buf(dax.addr, map_len, first,
							pos, end);
					need_wmb = true;
				}
				dax.addr += first;
				size = map_len - first;
			}
			max = min(pos + size, end);
		}

		if (iov_iter_rw(iter) == WRITE) {
			len = copy_from_iter_pmem(dax.addr, max - pos, iter);
			need_wmb = true;
		} else if (!hole)
			len = copy_to_iter((void __force *) dax.addr, max - pos,
					iter);
		else
			len = iov_iter_zero(max - pos, iter);

		if (!len) {
			rc = -EFAULT;
			break;
		}

		pos += len;
		if (!IS_ERR(dax.addr))
			dax.addr += len;
	}

	if (need_wmb)
		wmb_pmem();
	dax_unmap_atomic(bdev, &dax);

	return (pos == start) ? rc : pos - start;
}

/**
 * dax_do_io - Perform I/O to a DAX file
 * @iocb: The control block for this I/O
 * @inode: The file which the I/O is directed at
 * @iter: The addresses to do I/O from or to
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
		  struct iov_iter *iter, get_block_t get_block,
		  dio_iodone_t end_io, int flags)
{
	struct buffer_head bh;
	ssize_t retval = -EINVAL;
	loff_t pos = iocb->ki_pos;
	loff_t end = pos + iov_iter_count(iter);

	memset(&bh, 0, sizeof(bh));
	bh.b_bdev = inode->i_sb->s_bdev;

	if ((flags & DIO_LOCKING) && iov_iter_rw(iter) == READ) {
		struct address_space *mapping = inode->i_mapping;
		inode_lock(inode);
		retval = filemap_write_and_wait_range(mapping, pos, end - 1);
		if (retval) {
			inode_unlock(inode);
			goto out;
		}
	}

	/* Protects against truncate */
	if (!(flags & DIO_SKIP_DIO_COUNT))
		inode_dio_begin(inode);

	retval = dax_io(inode, iter, pos, end, get_block, &bh);

	if ((flags & DIO_LOCKING) && iov_iter_rw(iter) == READ)
		inode_unlock(inode);

	if (end_io) {
		int err;

		err = end_io(iocb, pos, retval, bh.b_private);
		if (err)
			retval = err;
	}

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
		put_page(page);
		return VM_FAULT_SIGBUS;
	}

	vmf->page = page;
	return VM_FAULT_LOCKED;
}

static int copy_user_bh(struct page *to, struct inode *inode,
		struct buffer_head *bh, unsigned long vaddr)
{
	struct blk_dax_ctl dax = {
		.sector = to_sector(bh, inode),
		.size = bh->b_size,
	};
	struct block_device *bdev = bh->b_bdev;
	void *vto;

	if (dax_map_atomic(bdev, &dax) < 0)
		return PTR_ERR(dax.addr);
	vto = kmap_atomic(to);
	copy_user_page(vto, (void __force *)dax.addr, vaddr, to);
	kunmap_atomic(vto);
	dax_unmap_atomic(bdev, &dax);
	return 0;
}

#define NO_SECTOR -1
#define DAX_PMD_INDEX(page_index) (page_index & (PMD_MASK >> PAGE_SHIFT))

static int dax_radix_entry(struct address_space *mapping, pgoff_t index,
		sector_t sector, bool pmd_entry, bool dirty)
{
	struct radix_tree_root *page_tree = &mapping->page_tree;
	pgoff_t pmd_index = DAX_PMD_INDEX(index);
	int type, error = 0;
	void *entry;

	WARN_ON_ONCE(pmd_entry && !dirty);
	if (dirty)
		__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);

	spin_lock_irq(&mapping->tree_lock);

	entry = radix_tree_lookup(page_tree, pmd_index);
	if (entry && RADIX_DAX_TYPE(entry) == RADIX_DAX_PMD) {
		index = pmd_index;
		goto dirty;
	}

	entry = radix_tree_lookup(page_tree, index);
	if (entry) {
		type = RADIX_DAX_TYPE(entry);
		if (WARN_ON_ONCE(type != RADIX_DAX_PTE &&
					type != RADIX_DAX_PMD)) {
			error = -EIO;
			goto unlock;
		}

		if (!pmd_entry || type == RADIX_DAX_PMD)
			goto dirty;

		/*
		 * We only insert dirty PMD entries into the radix tree.  This
		 * means we don't need to worry about removing a dirty PTE
		 * entry and inserting a clean PMD entry, thus reducing the
		 * range we would flush with a follow-up fsync/msync call.
		 */
		radix_tree_delete(&mapping->page_tree, index);
		mapping->nrexceptional--;
	}

	if (sector == NO_SECTOR) {
		/*
		 * This can happen during correct operation if our pfn_mkwrite
		 * fault raced against a hole punch operation.  If this
		 * happens the pte that was hole punched will have been
		 * unmapped and the radix tree entry will have been removed by
		 * the time we are called, but the call will still happen.  We
		 * will return all the way up to wp_pfn_shared(), where the
		 * pte_same() check will fail, eventually causing page fault
		 * to be retried by the CPU.
		 */
		goto unlock;
	}

	error = radix_tree_insert(page_tree, index,
			RADIX_DAX_ENTRY(sector, pmd_entry));
	if (error)
		goto unlock;

	mapping->nrexceptional++;
 dirty:
	if (dirty)
		radix_tree_tag_set(page_tree, index, PAGECACHE_TAG_DIRTY);
 unlock:
	spin_unlock_irq(&mapping->tree_lock);
	return error;
}

static int dax_writeback_one(struct block_device *bdev,
		struct address_space *mapping, pgoff_t index, void *entry)
{
	struct radix_tree_root *page_tree = &mapping->page_tree;
	int type = RADIX_DAX_TYPE(entry);
	struct radix_tree_node *node;
	struct blk_dax_ctl dax;
	void **slot;
	int ret = 0;

	spin_lock_irq(&mapping->tree_lock);
	/*
	 * Regular page slots are stabilized by the page lock even
	 * without the tree itself locked.  These unlocked entries
	 * need verification under the tree lock.
	 */
	if (!__radix_tree_lookup(page_tree, index, &node, &slot))
		goto unlock;
	if (*slot != entry)
		goto unlock;

	/* another fsync thread may have already written back this entry */
	if (!radix_tree_tag_get(page_tree, index, PAGECACHE_TAG_TOWRITE))
		goto unlock;

	if (WARN_ON_ONCE(type != RADIX_DAX_PTE && type != RADIX_DAX_PMD)) {
		ret = -EIO;
		goto unlock;
	}

	dax.sector = RADIX_DAX_SECTOR(entry);
	dax.size = (type == RADIX_DAX_PMD ? PMD_SIZE : PAGE_SIZE);
	spin_unlock_irq(&mapping->tree_lock);

	/*
	 * We cannot hold tree_lock while calling dax_map_atomic() because it
	 * eventually calls cond_resched().
	 */
	ret = dax_map_atomic(bdev, &dax);
	if (ret < 0)
		return ret;

	if (WARN_ON_ONCE(ret < dax.size)) {
		ret = -EIO;
		goto unmap;
	}

	wb_cache_pmem(dax.addr, dax.size);

	spin_lock_irq(&mapping->tree_lock);
	radix_tree_tag_clear(page_tree, index, PAGECACHE_TAG_TOWRITE);
	spin_unlock_irq(&mapping->tree_lock);
 unmap:
	dax_unmap_atomic(bdev, &dax);
	return ret;

 unlock:
	spin_unlock_irq(&mapping->tree_lock);
	return ret;
}

/*
 * Flush the mapping to the persistent domain within the byte range of [start,
 * end]. This is required by data integrity operations to ensure file data is
 * on persistent storage prior to completion of the operation.
 */
int dax_writeback_mapping_range(struct address_space *mapping,
		struct block_device *bdev, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	pgoff_t start_index, end_index, pmd_index;
	pgoff_t indices[PAGEVEC_SIZE];
	struct pagevec pvec;
	bool done = false;
	int i, ret = 0;
	void *entry;

	if (WARN_ON_ONCE(inode->i_blkbits != PAGE_SHIFT))
		return -EIO;

	if (!mapping->nrexceptional || wbc->sync_mode != WB_SYNC_ALL)
		return 0;

	start_index = wbc->range_start >> PAGE_SHIFT;
	end_index = wbc->range_end >> PAGE_SHIFT;
	pmd_index = DAX_PMD_INDEX(start_index);

	rcu_read_lock();
	entry = radix_tree_lookup(&mapping->page_tree, pmd_index);
	rcu_read_unlock();

	/* see if the start of our range is covered by a PMD entry */
	if (entry && RADIX_DAX_TYPE(entry) == RADIX_DAX_PMD)
		start_index = pmd_index;

	tag_pages_for_writeback(mapping, start_index, end_index);

	pagevec_init(&pvec, 0);
	while (!done) {
		pvec.nr = find_get_entries_tag(mapping, start_index,
				PAGECACHE_TAG_TOWRITE, PAGEVEC_SIZE,
				pvec.pages, indices);

		if (pvec.nr == 0)
			break;

		for (i = 0; i < pvec.nr; i++) {
			if (indices[i] > end_index) {
				done = true;
				break;
			}

			ret = dax_writeback_one(bdev, mapping, indices[i],
					pvec.pages[i]);
			if (ret < 0)
				return ret;
		}
	}
	wmb_pmem();
	return 0;
}
EXPORT_SYMBOL_GPL(dax_writeback_mapping_range);

static int dax_insert_mapping(struct inode *inode, struct buffer_head *bh,
			struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long vaddr = (unsigned long)vmf->virtual_address;
	struct address_space *mapping = inode->i_mapping;
	struct block_device *bdev = bh->b_bdev;
	struct blk_dax_ctl dax = {
		.sector = to_sector(bh, inode),
		.size = bh->b_size,
	};
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

	if (dax_map_atomic(bdev, &dax) < 0) {
		error = PTR_ERR(dax.addr);
		goto out;
	}

	if (buffer_unwritten(bh) || buffer_new(bh)) {
		clear_pmem(dax.addr, PAGE_SIZE);
		wmb_pmem();
	}
	dax_unmap_atomic(bdev, &dax);

	error = dax_radix_entry(mapping, vmf->pgoff, dax.sector, false,
			vmf->flags & FAULT_FLAG_WRITE);
	if (error)
		goto out;

	error = vm_insert_mixed(vma, vaddr, dax.pfn);

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
	bh.b_bdev = inode->i_sb->s_bdev;
	bh.b_size = PAGE_SIZE;

 repeat:
	page = find_get_page(mapping, vmf->pgoff);
	if (page) {
		if (!lock_page_or_retry(page, vma->vm_mm, vmf->flags)) {
			put_page(page);
			return VM_FAULT_RETRY;
		}
		if (unlikely(page->mapping != mapping)) {
			unlock_page(page);
			put_page(page);
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

	if (!buffer_mapped(&bh) && !vmf->cow_page) {
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
			error = copy_user_bh(new_page, inode, &bh, vaddr);
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
							PAGE_SIZE, 0);
		delete_from_page_cache(page);
		unlock_page(page);
		put_page(page);
		page = NULL;
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
		put_page(page);
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

static void __dax_dbg(struct buffer_head *bh, unsigned long address,
		const char *reason, const char *fn)
{
	if (bh) {
		char bname[BDEVNAME_SIZE];
		bdevname(bh->b_bdev, bname);
		pr_debug("%s: %s addr: %lx dev %s state %lx start %lld "
			"length %zd fallback: %s\n", fn, current->comm,
			address, bname, bh->b_state, (u64)bh->b_blocknr,
			bh->b_size, reason);
	} else {
		pr_debug("%s: %s addr: %lx fallback: %s\n", fn,
			current->comm, address, reason);
	}
}

#define dax_pmd_dbg(bh, address, reason)	__dax_dbg(bh, address, reason, "dax_pmd")

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
	struct block_device *bdev;
	pgoff_t size, pgoff;
	sector_t block;
	int error, result = 0;
	bool alloc = false;

	/* dax pmd mappings require pfn_t_devmap() */
	if (!IS_ENABLED(CONFIG_FS_DAX_PMD))
		return VM_FAULT_FALLBACK;

	/* Fall back to PTEs if we're going to COW */
	if (write && !(vma->vm_flags & VM_SHARED)) {
		split_huge_pmd(vma, pmd, address);
		dax_pmd_dbg(NULL, address, "cow write");
		return VM_FAULT_FALLBACK;
	}
	/* If the PMD would extend outside the VMA */
	if (pmd_addr < vma->vm_start) {
		dax_pmd_dbg(NULL, address, "vma start unaligned");
		return VM_FAULT_FALLBACK;
	}
	if ((pmd_addr + PMD_SIZE) > vma->vm_end) {
		dax_pmd_dbg(NULL, address, "vma end unaligned");
		return VM_FAULT_FALLBACK;
	}

	pgoff = linear_page_index(vma, pmd_addr);
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (pgoff >= size)
		return VM_FAULT_SIGBUS;
	/* If the PMD would cover blocks out of the file */
	if ((pgoff | PG_PMD_COLOUR) >= size) {
		dax_pmd_dbg(NULL, address,
				"offset + huge page size > file size");
		return VM_FAULT_FALLBACK;
	}

	memset(&bh, 0, sizeof(bh));
	bh.b_bdev = inode->i_sb->s_bdev;
	block = (sector_t)pgoff << (PAGE_SHIFT - blkbits);

	bh.b_size = PMD_SIZE;

	if (get_block(inode, block, &bh, 0) != 0)
		return VM_FAULT_SIGBUS;

	if (!buffer_mapped(&bh) && write) {
		if (get_block(inode, block, &bh, 1) != 0)
			return VM_FAULT_SIGBUS;
		alloc = true;
	}

	bdev = bh.b_bdev;

	/*
	 * If the filesystem isn't willing to tell us the length of a hole,
	 * just fall back to PTEs.  Calling get_block 512 times in a loop
	 * would be silly.
	 */
	if (!buffer_size_valid(&bh) || bh.b_size < PMD_SIZE) {
		dax_pmd_dbg(&bh, address, "allocated block too small");
		return VM_FAULT_FALLBACK;
	}

	/*
	 * If we allocated new storage, make sure no process has any
	 * zero pages covering this hole
	 */
	if (alloc) {
		loff_t lstart = pgoff << PAGE_SHIFT;
		loff_t lend = lstart + PMD_SIZE - 1; /* inclusive */

		truncate_pagecache_range(inode, lstart, lend);
	}

	i_mmap_lock_read(mapping);

	/*
	 * If a truncate happened while we were allocating blocks, we may
	 * leave blocks allocated to the file that are beyond EOF.  We can't
	 * take i_mutex here, so just leave them hanging; they'll be freed
	 * when the file is deleted.
	 */
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (pgoff >= size) {
		result = VM_FAULT_SIGBUS;
		goto out;
	}
	if ((pgoff | PG_PMD_COLOUR) >= size) {
		dax_pmd_dbg(&bh, address,
				"offset + huge page size > file size");
		goto fallback;
	}

	if (!write && !buffer_mapped(&bh) && buffer_uptodate(&bh)) {
		spinlock_t *ptl;
		pmd_t entry;
		struct page *zero_page = get_huge_zero_page();

		if (unlikely(!zero_page)) {
			dax_pmd_dbg(&bh, address, "no zero page");
			goto fallback;
		}

		ptl = pmd_lock(vma->vm_mm, pmd);
		if (!pmd_none(*pmd)) {
			spin_unlock(ptl);
			dax_pmd_dbg(&bh, address, "pmd already present");
			goto fallback;
		}

		dev_dbg(part_to_dev(bdev->bd_part),
				"%s: %s addr: %lx pfn: <zero> sect: %llx\n",
				__func__, current->comm, address,
				(unsigned long long) to_sector(&bh, inode));

		entry = mk_pmd(zero_page, vma->vm_page_prot);
		entry = pmd_mkhuge(entry);
		set_pmd_at(vma->vm_mm, pmd_addr, pmd, entry);
		result = VM_FAULT_NOPAGE;
		spin_unlock(ptl);
	} else {
		struct blk_dax_ctl dax = {
			.sector = to_sector(&bh, inode),
			.size = PMD_SIZE,
		};
		long length = dax_map_atomic(bdev, &dax);

		if (length < 0) {
			result = VM_FAULT_SIGBUS;
			goto out;
		}
		if (length < PMD_SIZE) {
			dax_pmd_dbg(&bh, address, "dax-length too small");
			dax_unmap_atomic(bdev, &dax);
			goto fallback;
		}
		if (pfn_t_to_pfn(dax.pfn) & PG_PMD_COLOUR) {
			dax_pmd_dbg(&bh, address, "pfn unaligned");
			dax_unmap_atomic(bdev, &dax);
			goto fallback;
		}

		if (!pfn_t_devmap(dax.pfn)) {
			dax_unmap_atomic(bdev, &dax);
			dax_pmd_dbg(&bh, address, "pfn not in memmap");
			goto fallback;
		}

		if (buffer_unwritten(&bh) || buffer_new(&bh)) {
			clear_pmem(dax.addr, PMD_SIZE);
			wmb_pmem();
			count_vm_event(PGMAJFAULT);
			mem_cgroup_count_vm_event(vma->vm_mm, PGMAJFAULT);
			result |= VM_FAULT_MAJOR;
		}
		dax_unmap_atomic(bdev, &dax);

		/*
		 * For PTE faults we insert a radix tree entry for reads, and
		 * leave it clean.  Then on the first write we dirty the radix
		 * tree entry via the dax_pfn_mkwrite() path.  This sequence
		 * allows the dax_pfn_mkwrite() call to be simpler and avoid a
		 * call into get_block() to translate the pgoff to a sector in
		 * order to be able to create a new radix tree entry.
		 *
		 * The PMD path doesn't have an equivalent to
		 * dax_pfn_mkwrite(), though, so for a read followed by a
		 * write we traverse all the way through __dax_pmd_fault()
		 * twice.  This means we can just skip inserting a radix tree
		 * entry completely on the initial read and just wait until
		 * the write to insert a dirty entry.
		 */
		if (write) {
			error = dax_radix_entry(mapping, pgoff, dax.sector,
					true, true);
			if (error) {
				dax_pmd_dbg(&bh, address,
						"PMD radix insertion failed");
				goto fallback;
			}
		}

		dev_dbg(part_to_dev(bdev->bd_part),
				"%s: %s addr: %lx pfn: %lx sect: %llx\n",
				__func__, current->comm, address,
				pfn_t_to_pfn(dax.pfn),
				(unsigned long long) dax.sector);
		result |= vmf_insert_pfn_pmd(vma, address, pmd,
				dax.pfn, write);
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
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/**
 * dax_pfn_mkwrite - handle first write to DAX page
 * @vma: The virtual memory area where the fault occurred
 * @vmf: The description of the fault
 */
int dax_pfn_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct file *file = vma->vm_file;
	int error;

	/*
	 * We pass NO_SECTOR to dax_radix_entry() because we expect that a
	 * RADIX_DAX_PTE entry already exists in the radix tree from a
	 * previous call to __dax_fault().  We just want to look up that PTE
	 * entry using vmf->pgoff and make sure the dirty tag is set.  This
	 * saves us from having to make a call to get_block() here to look
	 * up the sector.
	 */
	error = dax_radix_entry(file->f_mapping, vmf->pgoff, NO_SECTOR, false,
			true);

	if (error == -ENOMEM)
		return VM_FAULT_OOM;
	if (error)
		return VM_FAULT_SIGBUS;
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
 * We work in terms of PAGE_SIZE here for commonality with
 * block_truncate_page(), but we could go down to PAGE_SIZE if the filesystem
 * took care of disposing of the unnecessary blocks.  Even if the filesystem
 * block size is smaller than PAGE_SIZE, we have to zero the rest of the page
 * since the file might be mmapped.
 */
int dax_zero_page_range(struct inode *inode, loff_t from, unsigned length,
							get_block_t get_block)
{
	struct buffer_head bh;
	pgoff_t index = from >> PAGE_SHIFT;
	unsigned offset = from & (PAGE_SIZE-1);
	int err;

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;
	BUG_ON((offset + length) > PAGE_SIZE);

	memset(&bh, 0, sizeof(bh));
	bh.b_bdev = inode->i_sb->s_bdev;
	bh.b_size = PAGE_SIZE;
	err = get_block(inode, index, &bh, 0);
	if (err < 0)
		return err;
	if (buffer_written(&bh)) {
		struct block_device *bdev = bh.b_bdev;
		struct blk_dax_ctl dax = {
			.sector = to_sector(&bh, inode),
			.size = PAGE_SIZE,
		};

		if (dax_map_atomic(bdev, &dax) < 0)
			return PTR_ERR(dax.addr);
		clear_pmem(dax.addr + offset, length);
		wmb_pmem();
		dax_unmap_atomic(bdev, &dax);
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
 * We work in terms of PAGE_SIZE here for commonality with
 * block_truncate_page(), but we could go down to PAGE_SIZE if the filesystem
 * took care of disposing of the unnecessary blocks.  Even if the filesystem
 * block size is smaller than PAGE_SIZE, we have to zero the rest of the page
 * since the file might be mmapped.
 */
int dax_truncate_page(struct inode *inode, loff_t from, get_block_t get_block)
{
	unsigned length = PAGE_ALIGN(from) - from;
	return dax_zero_page_range(inode, from, length, get_block);
}
EXPORT_SYMBOL_GPL(dax_truncate_page);
