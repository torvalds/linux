// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (c) 2016-2025 Christoph Hellwig.
 */
#include <linux/fscrypt.h>
#include <linux/pagemap.h>
#include <linux/iomap.h>
#include <linux/task_io_accounting_ops.h>
#include "internal.h"
#include "trace.h"

#include "../internal.h"

/*
 * Private flags for iomap_dio, must not overlap with the public ones in
 * iomap.h:
 */
#define IOMAP_DIO_NO_INVALIDATE	(1U << 25)
#define IOMAP_DIO_CALLER_COMP	(1U << 26)
#define IOMAP_DIO_INLINE_COMP	(1U << 27)
#define IOMAP_DIO_WRITE_THROUGH	(1U << 28)
#define IOMAP_DIO_NEED_SYNC	(1U << 29)
#define IOMAP_DIO_WRITE		(1U << 30)
#define IOMAP_DIO_DIRTY		(1U << 31)

/*
 * Used for sub block zeroing in iomap_dio_zero()
 */
#define IOMAP_ZERO_PAGE_SIZE (SZ_64K)
#define IOMAP_ZERO_PAGE_ORDER (get_order(IOMAP_ZERO_PAGE_SIZE))
static struct page *zero_page;

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
		} submit;

		/* used for aio completion: */
		struct {
			struct work_struct	work;
		} aio;
	};
};

static struct bio *iomap_dio_alloc_bio(const struct iomap_iter *iter,
		struct iomap_dio *dio, unsigned short nr_vecs, blk_opf_t opf)
{
	if (dio->dops && dio->dops->bio_set)
		return bio_alloc_bioset(iter->iomap.bdev, nr_vecs, opf,
					GFP_KERNEL, dio->dops->bio_set);
	return bio_alloc(iter->iomap.bdev, nr_vecs, opf, GFP_KERNEL);
}

static void iomap_dio_submit_bio(const struct iomap_iter *iter,
		struct iomap_dio *dio, struct bio *bio, loff_t pos)
{
	struct kiocb *iocb = dio->iocb;

	atomic_inc(&dio->ref);

	/* Sync dio can't be polled reliably */
	if ((iocb->ki_flags & IOCB_HIPRI) && !is_sync_kiocb(iocb)) {
		bio_set_polled(bio, iocb);
		WRITE_ONCE(iocb->private, bio);
	}

	if (dio->dops && dio->dops->submit_io) {
		dio->dops->submit_io(iter, bio, pos);
	} else {
		WARN_ON_ONCE(iter->iomap.flags & IOMAP_F_ANON_WRITE);
		submit_bio(bio);
	}
}

ssize_t iomap_dio_complete(struct iomap_dio *dio)
{
	const struct iomap_dio_ops *dops = dio->dops;
	struct kiocb *iocb = dio->iocb;
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
	if (!dio->error && dio->size && (dio->flags & IOMAP_DIO_WRITE) &&
	    !(dio->flags & IOMAP_DIO_NO_INVALIDATE))
		kiocb_invalidate_post_direct_write(iocb, dio->size);

	inode_dio_end(file_inode(iocb->ki_filp));

	if (ret > 0) {
		iocb->ki_pos += ret;

		/*
		 * If this is a DSYNC write, make sure we push it to stable
		 * storage now that we've written data.
		 */
		if (dio->flags & IOMAP_DIO_NEED_SYNC)
			ret = generic_write_sync(iocb, ret);
		if (ret > 0)
			ret += dio->done_before;
	}
	trace_iomap_dio_complete(iocb, dio->error, ret);
	kfree(dio);
	return ret;
}
EXPORT_SYMBOL_GPL(iomap_dio_complete);

static ssize_t iomap_dio_deferred_complete(void *data)
{
	return iomap_dio_complete(data);
}

static void iomap_dio_complete_work(struct work_struct *work)
{
	struct iomap_dio *dio = container_of(work, struct iomap_dio, aio.work);
	struct kiocb *iocb = dio->iocb;

	iocb->ki_complete(iocb, iomap_dio_complete(dio));
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

/*
 * Called when dio->ref reaches zero from an I/O completion.
 */
static void iomap_dio_done(struct iomap_dio *dio)
{
	struct kiocb *iocb = dio->iocb;

	if (dio->wait_for_completion) {
		/*
		 * Synchronous I/O, task itself will handle any completion work
		 * that needs after IO. All we need to do is wake the task.
		 */
		struct task_struct *waiter = dio->submit.waiter;

		WRITE_ONCE(dio->submit.waiter, NULL);
		blk_wake_io_task(waiter);
	} else if (dio->flags & IOMAP_DIO_INLINE_COMP) {
		WRITE_ONCE(iocb->private, NULL);
		iomap_dio_complete_work(&dio->aio.work);
	} else if (dio->flags & IOMAP_DIO_CALLER_COMP) {
		/*
		 * If this dio is flagged with IOMAP_DIO_CALLER_COMP, then
		 * schedule our completion that way to avoid an async punt to a
		 * workqueue.
		 */
		/* only polled IO cares about private cleared */
		iocb->private = dio;
		iocb->dio_complete = iomap_dio_deferred_complete;

		/*
		 * Invoke ->ki_complete() directly. We've assigned our
		 * dio_complete callback handler, and since the issuer set
		 * IOCB_DIO_CALLER_COMP, we know their ki_complete handler will
		 * notice ->dio_complete being set and will defer calling that
		 * handler until it can be done from a safe task context.
		 *
		 * Note that the 'res' being passed in here is not important
		 * for this case. The actual completion value of the request
		 * will be gotten from dio_complete when that is run by the
		 * issuer.
		 */
		iocb->ki_complete(iocb, 0);
	} else {
		struct inode *inode = file_inode(iocb->ki_filp);

		/*
		 * Async DIO completion that requires filesystem level
		 * completion work gets punted to a work queue to complete as
		 * the operation may require more IO to be issued to finalise
		 * filesystem metadata changes or guarantee data integrity.
		 */
		INIT_WORK(&dio->aio.work, iomap_dio_complete_work);
		queue_work(inode->i_sb->s_dio_done_wq, &dio->aio.work);
	}
}

void iomap_dio_bio_end_io(struct bio *bio)
{
	struct iomap_dio *dio = bio->bi_private;
	bool should_dirty = (dio->flags & IOMAP_DIO_DIRTY);

	if (bio->bi_status)
		iomap_dio_set_error(dio, blk_status_to_errno(bio->bi_status));

	if (atomic_dec_and_test(&dio->ref))
		iomap_dio_done(dio);

	if (should_dirty) {
		bio_check_pages_dirty(bio);
	} else {
		bio_release_pages(bio, false);
		bio_put(bio);
	}
}
EXPORT_SYMBOL_GPL(iomap_dio_bio_end_io);

u32 iomap_finish_ioend_direct(struct iomap_ioend *ioend)
{
	struct iomap_dio *dio = ioend->io_bio.bi_private;
	bool should_dirty = (dio->flags & IOMAP_DIO_DIRTY);
	u32 vec_count = ioend->io_bio.bi_vcnt;

	if (ioend->io_error)
		iomap_dio_set_error(dio, ioend->io_error);

	if (atomic_dec_and_test(&dio->ref)) {
		/*
		 * Try to avoid another context switch for the completion given
		 * that we are already called from the ioend completion
		 * workqueue, but never invalidate pages from this thread to
		 * avoid deadlocks with buffered I/O completions.  Tough luck if
		 * you hit the tiny race with someone dirtying the range now
		 * between this check and the actual completion.
		 */
		if (!dio->iocb->ki_filp->f_mapping->nrpages) {
			dio->flags |= IOMAP_DIO_INLINE_COMP;
			dio->flags |= IOMAP_DIO_NO_INVALIDATE;
		}
		dio->flags &= ~IOMAP_DIO_CALLER_COMP;
		iomap_dio_done(dio);
	}

	if (should_dirty) {
		bio_check_pages_dirty(&ioend->io_bio);
	} else {
		bio_release_pages(&ioend->io_bio, false);
		bio_put(&ioend->io_bio);
	}

	/*
	 * Return the number of bvecs completed as even direct I/O completions
	 * do significant per-folio work and we'll still want to give up the
	 * CPU after a lot of completions.
	 */
	return vec_count;
}

static int iomap_dio_zero(const struct iomap_iter *iter, struct iomap_dio *dio,
		loff_t pos, unsigned len)
{
	struct inode *inode = file_inode(dio->iocb->ki_filp);
	struct bio *bio;

	if (!len)
		return 0;
	/*
	 * Max block size supported is 64k
	 */
	if (WARN_ON_ONCE(len > IOMAP_ZERO_PAGE_SIZE))
		return -EINVAL;

	bio = iomap_dio_alloc_bio(iter, dio, 1, REQ_OP_WRITE | REQ_SYNC | REQ_IDLE);
	fscrypt_set_bio_crypt_ctx(bio, inode, pos >> inode->i_blkbits,
				  GFP_KERNEL);
	bio->bi_iter.bi_sector = iomap_sector(&iter->iomap, pos);
	bio->bi_private = dio;
	bio->bi_end_io = iomap_dio_bio_end_io;

	__bio_add_page(bio, zero_page, len, 0);
	iomap_dio_submit_bio(iter, dio, bio, pos);
	return 0;
}

/*
 * Use a FUA write if we need datasync semantics and this is a pure data I/O
 * that doesn't require any metadata updates (including after I/O completion
 * such as unwritten extent conversion) and the underlying device either
 * doesn't have a volatile write cache or supports FUA.
 * This allows us to avoid cache flushes on I/O completion.
 */
static inline bool iomap_dio_can_use_fua(const struct iomap *iomap,
		struct iomap_dio *dio)
{
	if (iomap->flags & (IOMAP_F_SHARED | IOMAP_F_DIRTY))
		return false;
	if (!(dio->flags & IOMAP_DIO_WRITE_THROUGH))
		return false;
	return !bdev_write_cache(iomap->bdev) || bdev_fua(iomap->bdev);
}

static int iomap_dio_bio_iter(struct iomap_iter *iter, struct iomap_dio *dio)
{
	const struct iomap *iomap = &iter->iomap;
	struct inode *inode = iter->inode;
	unsigned int fs_block_size = i_blocksize(inode), pad;
	const loff_t length = iomap_length(iter);
	loff_t pos = iter->pos;
	blk_opf_t bio_opf = REQ_SYNC | REQ_IDLE;
	struct bio *bio;
	bool need_zeroout = false;
	int nr_pages, ret = 0;
	u64 copied = 0;
	size_t orig_count;

	if ((pos | length) & (bdev_logical_block_size(iomap->bdev) - 1) ||
	    !bdev_iter_is_aligned(iomap->bdev, dio->submit.iter))
		return -EINVAL;

	if (dio->flags & IOMAP_DIO_WRITE) {
		bio_opf |= REQ_OP_WRITE;

		if (iomap->flags & IOMAP_F_ATOMIC_BIO) {
			/*
			 * Ensure that the mapping covers the full write
			 * length, otherwise it won't be submitted as a single
			 * bio, which is required to use hardware atomics.
			 */
			if (length != iter->len)
				return -EINVAL;
			bio_opf |= REQ_ATOMIC;
		}

		if (iomap->type == IOMAP_UNWRITTEN) {
			dio->flags |= IOMAP_DIO_UNWRITTEN;
			need_zeroout = true;
		}

		if (iomap->flags & IOMAP_F_SHARED)
			dio->flags |= IOMAP_DIO_COW;

		if (iomap->flags & IOMAP_F_NEW) {
			need_zeroout = true;
		} else if (iomap->type == IOMAP_MAPPED) {
			if (iomap_dio_can_use_fua(iomap, dio))
				bio_opf |= REQ_FUA;
			else
				dio->flags &= ~IOMAP_DIO_WRITE_THROUGH;
		}

		/*
		 * We can only do deferred completion for pure overwrites that
		 * don't require additional I/O at completion time.
		 *
		 * This rules out writes that need zeroing or extent conversion,
		 * extend the file size, or issue metadata I/O or cache flushes
		 * during completion processing.
		 */
		if (need_zeroout || (pos >= i_size_read(inode)) ||
		    ((dio->flags & IOMAP_DIO_NEED_SYNC) &&
		     !(bio_opf & REQ_FUA)))
			dio->flags &= ~IOMAP_DIO_CALLER_COMP;
	} else {
		bio_opf |= REQ_OP_READ;
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

	/*
	 * The rules for polled IO completions follow the guidelines as the
	 * ones we set for inline and deferred completions. If none of those
	 * are available for this IO, clear the polled flag.
	 */
	if (!(dio->flags & (IOMAP_DIO_INLINE_COMP|IOMAP_DIO_CALLER_COMP)))
		dio->iocb->ki_flags &= ~IOCB_HIPRI;

	if (need_zeroout) {
		/* zero out from the start of the block to the write offset */
		pad = pos & (fs_block_size - 1);

		ret = iomap_dio_zero(iter, dio, pos - pad, pad);
		if (ret)
			goto out;
	}

	nr_pages = bio_iov_vecs_to_alloc(dio->submit.iter, BIO_MAX_VECS);
	do {
		size_t n;
		if (dio->error) {
			iov_iter_revert(dio->submit.iter, copied);
			copied = ret = 0;
			goto out;
		}

		bio = iomap_dio_alloc_bio(iter, dio, nr_pages, bio_opf);
		fscrypt_set_bio_crypt_ctx(bio, inode, pos >> inode->i_blkbits,
					  GFP_KERNEL);
		bio->bi_iter.bi_sector = iomap_sector(iomap, pos);
		bio->bi_write_hint = inode->i_write_hint;
		bio->bi_ioprio = dio->iocb->ki_ioprio;
		bio->bi_private = dio;
		bio->bi_end_io = iomap_dio_bio_end_io;

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
		if (WARN_ON_ONCE((bio_opf & REQ_ATOMIC) && n != length)) {
			/*
			 * An atomic write bio must cover the complete length,
			 * which it doesn't, so error. We may need to zero out
			 * the tail (complete FS block), similar to when
			 * bio_iov_iter_get_pages() returns an error, above.
			 */
			ret = -EINVAL;
			bio_put(bio);
			goto zero_tail;
		}
		if (dio->flags & IOMAP_DIO_WRITE)
			task_io_account_write(n);
		else if (dio->flags & IOMAP_DIO_DIRTY)
			bio_set_pages_dirty(bio);

		dio->size += n;
		copied += n;

		nr_pages = bio_iov_vecs_to_alloc(dio->submit.iter,
						 BIO_MAX_VECS);
		/*
		 * We can only poll for single bio I/Os.
		 */
		if (nr_pages)
			dio->iocb->ki_flags &= ~IOCB_HIPRI;
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
			ret = iomap_dio_zero(iter, dio, pos,
					     fs_block_size - pad);
	}
out:
	/* Undo iter limitation to current extent */
	iov_iter_reexpand(dio->submit.iter, orig_count - copied);
	if (copied)
		return iomap_iter_advance(iter, &copied);
	return ret;
}

static int iomap_dio_hole_iter(struct iomap_iter *iter, struct iomap_dio *dio)
{
	loff_t length = iov_iter_zero(iomap_length(iter), dio->submit.iter);

	dio->size += length;
	if (!length)
		return -EFAULT;
	return iomap_iter_advance(iter, &length);
}

static int iomap_dio_inline_iter(struct iomap_iter *iomi, struct iomap_dio *dio)
{
	const struct iomap *iomap = &iomi->iomap;
	struct iov_iter *iter = dio->submit.iter;
	void *inline_data = iomap_inline_data(iomap, iomi->pos);
	loff_t length = iomap_length(iomi);
	loff_t pos = iomi->pos;
	u64 copied;

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
	return iomap_iter_advance(iomi, &copied);
}

static int iomap_dio_iter(struct iomap_iter *iter, struct iomap_dio *dio)
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
		unsigned int dio_flags, void *private, size_t done_before)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct iomap_iter iomi = {
		.inode		= inode,
		.pos		= iocb->ki_pos,
		.len		= iov_iter_count(iter),
		.flags		= IOMAP_DIRECT,
		.private	= private,
	};
	bool wait_for_completion =
		is_sync_kiocb(iocb) || (dio_flags & IOMAP_DIO_FORCE_WAIT);
	struct blk_plug plug;
	struct iomap_dio *dio;
	loff_t ret = 0;

	trace_iomap_dio_rw_begin(iocb, iter, dio_flags, done_before);

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

	if (iocb->ki_flags & IOCB_NOWAIT)
		iomi.flags |= IOMAP_NOWAIT;

	if (iov_iter_rw(iter) == READ) {
		/* reads can always complete inline */
		dio->flags |= IOMAP_DIO_INLINE_COMP;

		if (iomi.pos >= dio->i_size)
			goto out_free_dio;

		if (user_backed_iter(iter))
			dio->flags |= IOMAP_DIO_DIRTY;

		ret = kiocb_write_and_wait(iocb, iomi.len);
		if (ret)
			goto out_free_dio;
	} else {
		iomi.flags |= IOMAP_WRITE;
		dio->flags |= IOMAP_DIO_WRITE;

		/*
		 * Flag as supporting deferred completions, if the issuer
		 * groks it. This can avoid a workqueue punt for writes.
		 * We may later clear this flag if we need to do other IO
		 * as part of this IO completion.
		 */
		if (iocb->ki_flags & IOCB_DIO_CALLER_COMP)
			dio->flags |= IOMAP_DIO_CALLER_COMP;

		if (dio_flags & IOMAP_DIO_OVERWRITE_ONLY) {
			ret = -EAGAIN;
			if (iomi.pos >= dio->i_size ||
			    iomi.pos + iomi.len > dio->i_size)
				goto out_free_dio;
			iomi.flags |= IOMAP_OVERWRITE_ONLY;
		}

		if (iocb->ki_flags & IOCB_ATOMIC)
			iomi.flags |= IOMAP_ATOMIC;

		/* for data sync or sync, we need sync completion processing */
		if (iocb_is_dsync(iocb)) {
			dio->flags |= IOMAP_DIO_NEED_SYNC;

		       /*
			* For datasync only writes, we optimistically try using
			* WRITE_THROUGH for this IO. This flag requires either
			* FUA writes through the device's write cache, or a
			* normal write to a device without a volatile write
			* cache. For the former, Any non-FUA write that occurs
			* will clear this flag, hence we know before completion
			* whether a cache flush is necessary.
			*/
			if (!(iocb->ki_flags & IOCB_SYNC))
				dio->flags |= IOMAP_DIO_WRITE_THROUGH;
		}

		/*
		 * Try to invalidate cache pages for the range we are writing.
		 * If this invalidation fails, let the caller fall back to
		 * buffered I/O.
		 */
		ret = kiocb_invalidate_pages(iocb, iomi.len);
		if (ret) {
			if (ret != -EAGAIN) {
				trace_iomap_dio_invalidate_fail(inode, iomi.pos,
								iomi.len);
				if (iocb->ki_flags & IOCB_ATOMIC) {
					/*
					 * folio invalidation failed, maybe
					 * this is transient, unlock and see if
					 * the caller tries again.
					 */
					ret = -EAGAIN;
				} else {
					/* fall back to buffered write */
					ret = -ENOTBLK;
				}
			}
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
	while ((ret = iomap_iter(&iomi, ops)) > 0) {
		iomi.status = iomap_dio_iter(&iomi, dio);

		/*
		 * We can only poll for single bio I/Os.
		 */
		iocb->ki_flags &= ~IOCB_HIPRI;
	}

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
	 * If all the writes we issued were already written through to the
	 * media, we don't need to flush the cache on IO completion. Clear the
	 * sync flag for this case.
	 */
	if (dio->flags & IOMAP_DIO_WRITE_THROUGH)
		dio->flags &= ~IOMAP_DIO_NEED_SYNC;

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
		if (!wait_for_completion) {
			trace_iomap_dio_rw_queued(inode, iomi.pos, iomi.len);
			return ERR_PTR(-EIOCBQUEUED);
		}

		for (;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (!READ_ONCE(dio->submit.waiter))
				break;

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
		unsigned int dio_flags, void *private, size_t done_before)
{
	struct iomap_dio *dio;

	dio = __iomap_dio_rw(iocb, iter, ops, dops, dio_flags, private,
			     done_before);
	if (IS_ERR_OR_NULL(dio))
		return PTR_ERR_OR_ZERO(dio);
	return iomap_dio_complete(dio);
}
EXPORT_SYMBOL_GPL(iomap_dio_rw);

static int __init iomap_dio_init(void)
{
	zero_page = alloc_pages(GFP_KERNEL | __GFP_ZERO,
				IOMAP_ZERO_PAGE_ORDER);

	if (!zero_page)
		return -ENOMEM;

	return 0;
}
fs_initcall(iomap_dio_init);
