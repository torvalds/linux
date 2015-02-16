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
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/uio.h>

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

static ssize_t dax_io(int rw, struct inode *inode, struct iov_iter *iter,
			loff_t start, loff_t end, get_block_t get_block,
			struct buffer_head *bh)
{
	ssize_t retval = 0;
	loff_t pos = start;
	loff_t max = start;
	loff_t bh_max = start;
	void *addr;
	bool hole = false;

	if (rw != WRITE)
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
								rw == WRITE);
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

			hole = (rw != WRITE) && !buffer_written(bh);
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

		if (rw == WRITE)
			len = copy_from_iter(addr, max - pos, iter);
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
 * @rw: READ to read or WRITE to write
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
ssize_t dax_do_io(int rw, struct kiocb *iocb, struct inode *inode,
			struct iov_iter *iter, loff_t pos,
			get_block_t get_block, dio_iodone_t end_io, int flags)
{
	struct buffer_head bh;
	ssize_t retval = -EINVAL;
	loff_t end = pos + iov_iter_count(iter);

	memset(&bh, 0, sizeof(bh));

	if ((flags & DIO_LOCKING) && (rw == READ)) {
		struct address_space *mapping = inode->i_mapping;
		mutex_lock(&inode->i_mutex);
		retval = filemap_write_and_wait_range(mapping, pos, end - 1);
		if (retval) {
			mutex_unlock(&inode->i_mutex);
			goto out;
		}
	}

	/* Protects against truncate */
	atomic_inc(&inode->i_dio_count);

	retval = dax_io(rw, inode, iter, pos, end, get_block, &bh);

	if ((flags & DIO_LOCKING) && (rw == READ))
		mutex_unlock(&inode->i_mutex);

	if ((retval > 0) && end_io)
		end_io(iocb, pos, retval, bh.b_private);

	inode_dio_done(inode);
 out:
	return retval;
}
EXPORT_SYMBOL_GPL(dax_do_io);
