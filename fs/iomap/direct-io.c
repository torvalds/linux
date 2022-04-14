// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (c) 2016-2021 Christoph Hellwig.
 */
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/backing-dev.h>
#include <linux/uio.h>
#include <linux/task_io_accounting_ops.h>
#include "trace.h"

#include "../internal.h"

/*
 * Private flags for iomap_dio, must not overlap with the public ones in
 * iomap.h:
 */
#define IOMAP_DIO_WRITE_FUA	(1 << 28)
#define IOMAP_DIO_NEED_SYNC	(1 << 29)
#define IOMAP_DIO_WRITE		(1 << 30)
#define IOMAP_DIO_DIRTY		(1 << 31)

struct iomap_dio {
	struct kiocb		*iocb;
	const struct iomap_dio_ops *dops;
	loff_t			i_size;
	loff_t			size;
	atomic_t		ref;
	unsigned		flags;
	int			error;
	size_t			done_before;
	bool			wait_for_completion;

	union {
		/* used during submission and for synchronous completion: */
		struct {
			struct iov_iter		*iter;
			struct task_struct	*waiter;
			struct request_queue	*last_queue;
			blk_qc_t		cookie;
		} submit;

		/* used for aio completion: */
		struct {
			struct work_struct	work;
		} aio;
	};
};

int iomap_dio_iopoll(struct kiocb *kiocb, bool spin)
{
	struct request_queue *q = READ_ONCE(kiocb->private);

	if (!q)
		return 0;
	return blk_poll(q, READ_ONCE(kiocb->ki_cookie), spin);
}
EXPORT_SYMBOL_GPL(iomap_dio_iopoll);

static void iomap_dio_submit_bio(const struct iomap_iter *iter,
		struct iomap_dio *dio, struct bio *bio, loff_t pos)
{
	atomic_inc(&dio->ref);

	if (dio->iocb->ki_flags & IOCB_HIPRI)
		bio_set_polled(bio, dio->iocb);

	dio->submit.last_queue = bdev_get_queue(iter->iomap.bdev);
	if (dio->dops && dio->dops->submit_io)
		dio->submit.cookie = dio->dops->submit_io(iter, bio, pos);
	else
		dio->submit.cookie = submit_bio(bio);
}

ssize_t iomap_dio_complete(struct iomap_dio *dio)
{
	const struct iomap_dio_ops *dops = dio->dops;
	struct kiocb *iocb = dio->iocb;
	struct inode *inode = file_inode(iocb->ki_filp);
	loff_t offset = iocb->ki_pos;
	ssize_t ret = dio->error;

	if (dops && dops->end_io)
		ret = dops->end_io(iocb, dio->size, ret, dio->flags);

	if (likely(!ret)) {
		ret = dio->size;
		/* check for short read */
		if (offset + ret > dio->i_size &&
		    !(dio->flags & IOMAP_DIO_WRITE))
			ret = dio->i_size - offset;
		iocb->ki_pos += ret;
	}

	/*
	 * Try again to invalidate clean pages which might have been cached by
	 * non-direct readahead, or faulted in by get_user_pages() if the source
	 * of the write was an mmap'ed region of the file we're writing.  Either
	 * one is a pretty crazy thing to do, so we don't support it 100%.  If
	 * this invalidation fails, tough, the write still worked...
	 *
	 * And this page cache invalidation has to be after ->end_io(), as some
	 * filesystems convert unwritten extents to real allocations in
	 * ->end_io() when necessary, otherwise a racing buffer read would cache
	 * zeros from unwritten extents.
	 */
	if (!dio->error && dio->size &&
	    (dio->flags & IOMAP_DIO_WRITE) && inode->i_mapping->nrpages) {
		int err;
		err = invalidate_inode_pages2_range(inode->i_mapping,
				offset >> PAGE_SHIFT,
				(offset + dio->size - 1) >> PAGE_SHIFT);
		if (err)
			dio_warn_stale_pagecache(iocb->ki_filp);
	}

	inode_dio_end(file_inode(iocb->ki_filp));
	/*
	 * If this is a DSYNC write, make sure we push it to stable storage now
	 * that we've written data.
	 */
	if (ret > 0 && (dio->flags & IOMAP_DIO_NEED_SYNC))
		ret = generic_write_sync(iocb, ret);

	if (ret > 0)
		ret += dio->done_before;

	kfree(dio);

	return ret;
}
EXPORT_SYMBOL_GPL(iomap_dio_complete);

static void iomap_dio_complete_work(struct work_struct *work)
{
	struct iomap_dio *dio = container_of(work, struct iomap_dio, aio.work);
	struct kiocb *iocb = dio->iocb;

	iocb->ki_complete(iocb, iomap_dio_complete(dio), 0);
}

/*
 * Set an error in the dio if none is set yet.  We have to use cmpxchg
 * as the submission context and the completion context(s) can race to
 * update the error.
 */
static inline void iomap_dio_set_error(struct iomap_dio *dio, int ret)
{
	cmpxchg(&dio->error, 0, ret);
}

static void iomap_dio_bio_end_io(struct bio *bio)
{
	struct iomap_dio *dio = bio->bi_private;
	bool should_dirty = (dio->flags & IOMAP_DIO_DIRTY);

	if (bio->bi_status)
		iomap_dio_set_error(dio, blk_status_to_errno(bio->bi_status));

	if (atomic_dec_and_test(&dio->ref)) {
		if (dio->wait_for_completion) {
			struct task_struct *waiter = dio->submit.waiter;
			WRITE_ONCE(dio->submit.waiter, NULL);
			blk_wake_io_task(waiter);
		} else if (dio->flags & IOMAP_DIO_WRITE) {
			struct inode *inode = file_inode(dio->iocb->ki_filp);

			INIT_WORK(&dio->aio.work, iomap_dio_complete_work);
			queue_work(inode->i_sb->s_dio_done_wq, &dio->aio.work);
		} else {
			iomap_dio_complete_work(&dio->aio.work);
		}
	}

	if (should_dirty) {
		bio_check_pages_dirty(bio);
	} else {
		bio_release_pages(bio, false);
		bio_put(bio);
	}
}

static void iomap_dio_zero(const struct iomap_iter *iter, struct iomap_dio *dio,
		loff_t pos, unsigned len)
{
	struct page *page = ZERO_PAGE(0);
	int flags = REQ_SYNC | REQ_IDLE;
	struct bio *bio;

	bio = bio_alloc(GFP_KERNEL, 1);
	bio_set_dev(bio, iter->iomap.bdev);
	bio->bi_iter.bi_sector = iomap_sector(&iter->iomap, pos);
	bio->bi_private = dio;
	bio->bi_end_io = iomap_dio_bio_end_io;

	get_page(page);
	__bio_add_page(bio, page, len, 0);
	bio_set_op_attrs(bio, REQ_OP_WRITE, flags);
	iomap_dio_submit_bio(iter, dio, bio, pos);
}

/*
 * Figure out the bio's operation flags from the dio request, the
 * mapping, and whether or not we want FUA.  Note that we can end up
 * clearing the WRITE_FUA flag in the dio request.
 */
static inline unsigned int iomap_dio_bio_opflags(struct iomap_dio *dio,
		const struct iomap *iomap, bool use_fua)
{
	unsigned int opflags = REQ_SYNC | REQ_IDLE;

	if (!(dio->flags & IOMAP_DIO_WRITE)) {
		WARN_ON_ONCE(iomap->flags & IOMAP_F_ZONE_APPEND);
		return REQ_OP_READ;
	}

	if (iomap->flags & IOMAP_F_ZONE_APPEND)
		opflags |= REQ_OP_ZONE_APPEND;
	else
		opflags |= REQ_OP_WRITE;

	if (use_fua)
		opflags |= REQ_FUA;
	else
		dio->flags &= ~IOMAP_DIO_WRITE_FUA;

	return opflags;
}

static loff_t iomap_dio_bio_iter(const struct iomap_iter *iter,
		struct iomap_dio *dio)
{
	const struct iomap *iomap = &iter->iomap;
	struct inode *inode = iter->inode;
	unsigned int blkbits = blksize_bits(bdev_logical_block_size(iomap->bdev));
	unsigned int fs_block_size = i_blocksize(inode), pad;
	unsigned int align = iov_iter_alignment(dio->submit.iter);
	loff_t length = iomap_length(iter);
	loff_t pos = iter->pos;
	unsigned int bio_opf;
	struct bio *bio;
	bool need_zeroout = false;
	bool use_fua = false;
	int nr_pages, ret = 0;
	size_t copied = 0;
	size_t orig_count;

	if ((pos | length | align) & ((1 << blkbits) - 1))
		return -EINVAL;

	if (iomap->type == IOMAP_UNWRITTEN) {
		dio->flags |= IOMAP_DIO_UNWRITTEN;
		need_zeroout = true;
	}

	if (iomap->flags & IOMAP_F_SHARED)
		dio->flags |= IOMAP_DIO_COW;

	if (iomap->flags & IOMAP_F_NEW) {
		need_zeroout = true;
	} else if (iomap->type == IOMAP_MAPPED) {
		/*
		 * Use a FUA write if we need datasync semantics, this is a pure
		 * data IO that doesn't require any metadata updates (including
		 * after IO completion such as unwritten extent conversion) and
		 * the underlying device supports FUA. This allows us to avoid
		 * cache flushes on IO completion.
		 */
		if (!(iomap->flags & (IOMAP_F_SHARED|IOMAP_F_DIRTY)) &&
		    (dio->flags & IOMAP_DIO_WRITE_FUA) &&
		    blk_queue_fua(bdev_get_queue(iomap->bdev)))
			use_fua = true;
	}

	/*
	 * Save the original count and trim the iter to just the extent we
	 * are operating on right now.  The iter will be re-expanded once
	 * we are done.
	 */
	orig_count = iov_iter_count(dio->submit.iter);
	iov_iter_truncate(dio->submit.iter, length);

	if (!iov_iter_count(dio->submit.iter))
		goto out;

	if (need_zeroout) {
		/* zero out from the start of the block to the write offset */
		pad = pos & (fs_block_size - 1);
		if (pad)
			iomap_dio_zero(iter, dio, pos - pad, pad);
	}

	/*
	 * Set the operation flags early so that bio_iov_iter_get_pages
	 * can set up the page vector appropriately for a ZONE_APPEND
	 * operation.
	 */
	bio_opf = iomap_dio_bio_opflags(dio, iomap, use_fua);

	nr_pages = bio_iov_vecs_to_alloc(dio->submit.iter, BIO_MAX_VECS);
	do {
		size_t n;
		if (dio->error) {
			iov_iter_revert(dio->submit.iter, copied);
			copied = ret = 0;
			goto out;
		}

		bio = bio_alloc(GFP_KERNEL, nr_pages);
		bio_set_dev(bio, iomap->bdev);
		bio->bi_iter.bi_sector = iomap_sector(iomap, pos);
		bio->bi_write_hint = dio->iocb->ki_hint;
		bio->bi_ioprio = dio->iocb->ki_ioprio;
		bio->bi_private = dio;
		bio->bi_end_io = iomap_dio_bio_end_io;
		bio->bi_opf = bio_opf;

		ret = bio_iov_iter_get_pages(bio, dio->submit.iter);
		if (unlikely(ret)) {
			/*
			 * We have to stop part way through an IO. We must fall
			 * through to the sub-block tail zeroing here, otherwise
			 * this short IO may expose stale data in the tail of
			 * the block we haven't written data to.
			 */
			bio_put(bio);
			goto zero_tail;
		}

		n = bio->bi_iter.bi_size;
		if (dio->flags & IOMAP_DIO_WRITE) {
			task_io_account_write(n);
		} else {
			if (dio->flags & IOMAP_DIO_DIRTY)
				bio_set_pages_dirty(bio);
		}

		dio->size += n;
		copied += n;

		nr_pages = bio_iov_vecs_to_alloc(dio->submit.iter,
						 BIO_MAX_VECS);
		iomap_dio_submit_bio(iter, dio, bio, pos);
		pos += n;
	} while (nr_pages);

	/*
	 * We need to zeroout the tail of a sub-block write if the extent type
	 * requires zeroing or the write extends beyond EOF. If we don't zero
	 * the block tail in the latter case, we can expose stale data via mmap
	 * reads of the EOF block.
	 */
zero_tail:
	if (need_zeroout ||
	    ((dio->flags & IOMAP_DIO_WRITE) && pos >= i_size_read(inode))) {
		/* zero out from the end of the write to the end of the block */
		pad = pos & (fs_block_size - 1);
		if (pad)
			iomap_dio_zero(iter, dio, pos, fs_block_size - pad);
	}
out:
	/* Undo iter limitation to current extent */
	iov_iter_reexpand(dio->submit.iter, orig_count - copied);
	if (copied)
		return copied;
	return ret;
}

static loff_t iomap_dio_hole_iter(const struct iomap_iter *iter,
		struct iomap_dio *dio)
{
	loff_t length = iov_iter_zero(iomap_length(iter), dio->submit.iter);

	dio->size += length;
	if (!length)
		return -EFAULT;
	return length;
}

static loff_t iomap_dio_inline_iter(const struct iomap_iter *iomi,
		struct iomap_dio *dio)
{
	const struct iomap *iomap = &iomi->iomap;
	struct iov_iter *iter = dio->submit.iter;
	void *inline_data = iomap_inline_data(iomap, iomi->pos);
	loff_t length = iomap_length(iomi);
	loff_t pos = iomi->pos;
	size_t copied;

	if (WARN_ON_ONCE(!iomap_inline_data_valid(iomap)))
		return -EIO;

	if (dio->flags & IOMAP_DIO_WRITE) {
		loff_t size = iomi->inode->i_size;

		if (pos > size)
			memset(iomap_inline_data(iomap, size), 0, pos - size);
		copied = copy_from_iter(inline_data, length, iter);
		if (copied) {
			if (pos + copied > size)
				i_size_write(iomi->inode, pos + copied);
			mark_inode_dirty(iomi->inode);
		}
	} else {
		copied = copy_to_iter(inline_data, length, iter);
	}
	dio->size += copied;
	if (!copied)
		return -EFAULT;
	return copied;
}

static loff_t iomap_dio_iter(const struct iomap_iter *iter,
		struct iomap_dio *dio)
{
	switch (iter->iomap.type) {
	case IOMAP_HOLE:
		if (WARN_ON_ONCE(dio->flags & IOMAP_DIO_WRITE))
			return -EIO;
		return iomap_dio_hole_iter(iter, dio);
	case IOMAP_UNWRITTEN:
		if (!(dio->flags & IOMAP_DIO_WRITE))
			return iomap_dio_hole_iter(iter, dio);
		return iomap_dio_bio_iter(iter, dio);
	case IOMAP_MAPPED:
		return iomap_dio_bio_iter(iter, dio);
	case IOMAP_INLINE:
		return iomap_dio_inline_iter(iter, dio);
	case IOMAP_DELALLOC:
		/*
		 * DIO is not serialised against mmap() access at all, and so
		 * if the page_mkwrite occurs between the writeback and the
		 * iomap_iter() call in the DIO path, then it will see the
		 * DELALLOC block that the page-mkwrite allocated.
		 */
		pr_warn_ratelimited("Direct I/O collision with buffered writes! File: %pD4 Comm: %.20s\n",
				    dio->iocb->ki_filp, current->comm);
		return -EIO;
	default:
		WARN_ON_ONCE(1);
		return -EIO;
	}
}

/*
 * iomap_dio_rw() always completes O_[D]SYNC writes regardless of whether the IO
 * is being issued as AIO or not.  This allows us to optimise pure data writes
 * to use REQ_FUA rather than requiring generic_write_sync() to issue a
 * REQ_FLUSH post write. This is slightly tricky because a single request here
 * can be mapped into multiple disjoint IOs and only a subset of the IOs issued
 * may be pure data writes. In that case, we still need to do a full data sync
 * completion.
 *
 * When page faults are disabled and @dio_flags includes IOMAP_DIO_PARTIAL,
 * __iomap_dio_rw can return a partial result if it encounters a non-resident
 * page in @iter after preparing a transfer.  In that case, the non-resident
 * pages can be faulted in and the request resumed with @done_before set to the
 * number of bytes previously transferred.  The request will then complete with
 * the correct total number of bytes transferred; this is essential for
 * completing partial requests asynchronously.
 *
 * Returns -ENOTBLK In case of a page invalidation invalidation failure for
 * writes.  The callers needs to fall back to buffered I/O in this case.
 */
struct iomap_dio *
__iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops, const struct iomap_dio_ops *dops,
		unsigned int dio_flags, size_t done_before)
{
	struct address_space *mapping = iocb->ki_filp->f_mapping;
	struct inode *inode = file_inode(iocb->ki_filp);
	struct iomap_iter iomi = {
		.inode		= inode,
		.pos		= iocb->ki_pos,
		.len		= iov_iter_count(iter),
		.flags		= IOMAP_DIRECT,
	};
	loff_t end = iomi.pos + iomi.len - 1, ret = 0;
	bool wait_for_completion =
		is_sync_kiocb(iocb) || (dio_flags & IOMAP_DIO_FORCE_WAIT);
	struct blk_plug plug;
	struct iomap_dio *dio;

	if (!iomi.len)
		return NULL;

	dio = kmalloc(sizeof(*dio), GFP_KERNEL);
	if (!dio)
		return ERR_PTR(-ENOMEM);

	dio->iocb = iocb;
	atomic_set(&dio->ref, 1);
	dio->size = 0;
	dio->i_size = i_size_read(inode);
	dio->dops = dops;
	dio->error = 0;
	dio->flags = 0;
	dio->done_before = done_before;

	dio->submit.iter = iter;
	dio->submit.waiter = current;
	dio->submit.cookie = BLK_QC_T_NONE;
	dio->submit.last_queue = NULL;

	if (iov_iter_rw(iter) == READ) {
		if (iomi.pos >= dio->i_size)
			goto out_free_dio;

		if (iocb->ki_flags & IOCB_NOWAIT) {
			if (filemap_range_needs_writeback(mapping, iomi.pos,
					end)) {
				ret = -EAGAIN;
				goto out_free_dio;
			}
			iomi.flags |= IOMAP_NOWAIT;
		}

		if (iter_is_iovec(iter))
			dio->flags |= IOMAP_DIO_DIRTY;
	} else {
		iomi.flags |= IOMAP_WRITE;
		dio->flags |= IOMAP_DIO_WRITE;

		if (iocb->ki_flags & IOCB_NOWAIT) {
			if (filemap_range_has_page(mapping, iomi.pos, end)) {
				ret = -EAGAIN;
				goto out_free_dio;
			}
			iomi.flags |= IOMAP_NOWAIT;
		}

		/* for data sync or sync, we need sync completion processing */
		if (iocb->ki_flags & IOCB_DSYNC)
			dio->flags |= IOMAP_DIO_NEED_SYNC;

		/*
		 * For datasync only writes, we optimistically try using FUA for
		 * this IO.  Any non-FUA write that occurs will clear this flag,
		 * hence we know before completion whether a cache flush is
		 * necessary.
		 */
		if ((iocb->ki_flags & (IOCB_DSYNC | IOCB_SYNC)) == IOCB_DSYNC)
			dio->flags |= IOMAP_DIO_WRITE_FUA;
	}

	if (dio_flags & IOMAP_DIO_OVERWRITE_ONLY) {
		ret = -EAGAIN;
		if (iomi.pos >= dio->i_size ||
		    iomi.pos + iomi.len > dio->i_size)
			goto out_free_dio;
		iomi.flags |= IOMAP_OVERWRITE_ONLY;
	}

	ret = filemap_write_and_wait_range(mapping, iomi.pos, end);
	if (ret)
		goto out_free_dio;

	if (iov_iter_rw(iter) == WRITE) {
		/*
		 * Try to invalidate cache pages for the range we are writing.
		 * If this invalidation fails, let the caller fall back to
		 * buffered I/O.
		 */
		if (invalidate_inode_pages2_range(mapping,
				iomi.pos >> PAGE_SHIFT, end >> PAGE_SHIFT)) {
			trace_iomap_dio_invalidate_fail(inode, iomi.pos,
							iomi.len);
			ret = -ENOTBLK;
			goto out_free_dio;
		}

		if (!wait_for_completion && !inode->i_sb->s_dio_done_wq) {
			ret = sb_init_dio_done_wq(inode->i_sb);
			if (ret < 0)
				goto out_free_dio;
		}
	}

	inode_dio_begin(inode);

	blk_start_plug(&plug);
	while ((ret = iomap_iter(&iomi, ops)) > 0)
		iomi.processed = iomap_dio_iter(&iomi, dio);
	blk_finish_plug(&plug);

	/*
	 * We only report that we've read data up to i_size.
	 * Revert iter to a state corresponding to that as some callers (such
	 * as the splice code) rely on it.
	 */
	if (iov_iter_rw(iter) == READ && iomi.pos >= dio->i_size)
		iov_iter_revert(iter, iomi.pos - dio->i_size);

	if (ret == -EFAULT && dio->size && (dio_flags & IOMAP_DIO_PARTIAL)) {
		if (!(iocb->ki_flags & IOCB_NOWAIT))
			wait_for_completion = true;
		ret = 0;
	}

	/* magic error code to fall back to buffered I/O */
	if (ret == -ENOTBLK) {
		wait_for_completion = true;
		ret = 0;
	}
	if (ret < 0)
		iomap_dio_set_error(dio, ret);

	/*
	 * If all the writes we issued were FUA, we don't need to flush the
	 * cache on IO completion. Clear the sync flag for this case.
	 */
	if (dio->flags & IOMAP_DIO_WRITE_FUA)
		dio->flags &= ~IOMAP_DIO_NEED_SYNC;

	WRITE_ONCE(iocb->ki_cookie, dio->submit.cookie);
	WRITE_ONCE(iocb->private, dio->submit.last_queue);

	/*
	 * We are about to drop our additional submission reference, which
	 * might be the last reference to the dio.  There are three different
	 * ways we can progress here:
	 *
	 *  (a) If this is the last reference we will always complete and free
	 *	the dio ourselves.
	 *  (b) If this is not the last reference, and we serve an asynchronous
	 *	iocb, we must never touch the dio after the decrement, the
	 *	I/O completion handler will complete and free it.
	 *  (c) If this is not the last reference, but we serve a synchronous
	 *	iocb, the I/O completion handler will wake us up on the drop
	 *	of the final reference, and we will complete and free it here
	 *	after we got woken by the I/O completion handler.
	 */
	dio->wait_for_completion = wait_for_completion;
	if (!atomic_dec_and_test(&dio->ref)) {
		if (!wait_for_completion)
			return ERR_PTR(-EIOCBQUEUED);

		for (;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (!READ_ONCE(dio->submit.waiter))
				break;

			if (!(iocb->ki_flags & IOCB_HIPRI) ||
			    !dio->submit.last_queue ||
			    !blk_poll(dio->submit.last_queue,
					 dio->submit.cookie, true))
				blk_io_schedule();
		}
		__set_current_state(TASK_RUNNING);
	}

	return dio;

out_free_dio:
	kfree(dio);
	if (ret)
		return ERR_PTR(ret);
	return NULL;
}
EXPORT_SYMBOL_GPL(__iomap_dio_rw);

ssize_t
iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops, const struct iomap_dio_ops *dops,
		unsigned int dio_flags, size_t done_before)
{
	struct iomap_dio *dio;

	dio = __iomap_dio_rw(iocb, iter, ops, dops, dio_flags, done_before);
	if (IS_ERR_OR_NULL(dio))
		return PTR_ERR_OR_ZERO(dio);
	return iomap_dio_complete(dio);
}
EXPORT_SYMBOL_GPL(iomap_dio_rw);
