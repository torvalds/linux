// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2026 Christoph Hellwig.
 * All Rights Reserved.
 */
#include "xfs_platform.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_iomap.h"
#include "xfs_trace.h"
#include "xfs_bmap_util.h"
#include "xfs_reflink.h"
#include "xfs_zone_alloc.h"
#include "xfs_ioend.h"
#include "xfs_error.h"
#include "xfs_errortag.h"
#include <linux/bio-integrity.h>

static void
xfs_dio_bounce_end_io(
	struct bio		*bio)
{
	struct iomap_ioend	*ioend = iomap_ioend_from_bio(bio);
	int			error = blk_status_to_errno(bio->bi_status);
	struct bio		*orig_bio = bio->bi_private;

	if ((ioend->io_flags & IOMAP_IOEND_INTEGRITY) && !bio->bi_status)
		error = iomap_ioend_integrity_verify(ioend);
	iomap_bounce_read_end_io(ioend, orig_bio, error);
}

static void
xfs_bounce_submit_ioend(
	struct iomap_ioend	*ioend)
{
	if (ioend->io_flags & IOMAP_IOEND_INTEGRITY)
		fs_bio_integrity_alloc(&ioend->io_bio);
	ioend->io_bio.bi_end_io = xfs_dio_bounce_end_io;
	bio_set_flag(&ioend->io_bio, BIO_COMPLETE_IN_TASK);
	submit_bio(&ioend->io_bio);
}

static void
xfs_end_bio_bounced(
	struct bio		*bio)
{
	/*
	 * Just complete the original ioends as all verification is done by the
	 * end_io handlers for the clone bio(s).
	 */
	iomap_finish_ioends(iomap_ioend_from_bio(bio),
			blk_status_to_errno(bio->bi_status));
}

static void
xfs_read_bounce_and_resubmit(
	struct iomap_ioend	*ioend)
{
	struct bio		*bio = &ioend->io_bio;
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	unsigned int		nofs_flag = memalloc_nofs_save();

	trace_xfs_bounce_reread(ip, ioend->io_offset, ioend->io_size);

	/*
	 * Free the bio integrity data for the original bio, as we'll allocate
	 * a new one for each sub-I/O, which could deadlock if we keep the
	 * integrity data for the original bio around.
	 */
	if (bio_integrity(bio))
		fs_bio_integrity_free(bio);

	/*
	 * Resubmit the bio through the iomap bounce machinery.  The original
	 * bio itself is not resubmitted to the block layer, but just used to
	 * track I/O completion of the cloned bios.
	 */
	bio_prepare_reissue(bio, xfs_inode_buftarg(ip)->bt_bdev);
	bio->bi_iter = (struct bvec_iter) {
		.bi_sector	= ioend->io_sector,
		.bi_size	= ioend->io_size,
		.bi_offset	= ioend->io_bvec_offset,
	};
	bio->bi_end_io = xfs_end_bio_bounced;
	iomap_bounce_read(ioend, bdev_logical_block_size(bio->bi_bdev),
			xfs_bounce_submit_ioend);
	memalloc_nofs_restore(nofs_flag);
}

static void
xfs_end_io_read(
	struct bio		*bio)
{
	struct iomap_ioend	*ioend = iomap_ioend_from_bio(bio);
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	struct xfs_mount	*mp = ip->i_mount;
	int			error = blk_status_to_errno(bio->bi_status);

	if (!error && (ioend->io_flags & IOMAP_IOEND_INTEGRITY)) {
		error = iomap_ioend_integrity_verify(ioend);
		if ((ioend->io_flags & IOMAP_IOEND_DIRECT) &&
		    READ_ONCE(mp->m_read_bounce) == XFS_READ_BOUNCE_LAZY) {
			/*
			 * We only really need to retry for guard tag errors,
			 * but right now we can't distinguish them from other
			 * (i.e, reftag) errors.
			 */
			if (error ||
			    XFS_TEST_ERROR(mp, XFS_ERRTAG_BOUNCE_REREAD)) {
				xfs_read_bounce_and_resubmit(ioend);
				return;
			}
		}
	}

	iomap_finish_ioends(ioend, error);
}

void
xfs_ioend_submit_read(
	struct inode		*inode,
	struct bio		*bio,
	loff_t			file_offset,
	u16			ioend_flags)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct iomap_ioend	*ioend;

	ioend = iomap_init_ioend(inode, bio, file_offset, ioend_flags);
	if ((ioend_flags & IOMAP_IOEND_DIRECT) &&
	    READ_ONCE(mp->m_read_bounce) == XFS_READ_BOUNCE_ALWAYS) {
		iomap_bounce_read(ioend, bdev_logical_block_size(bio->bi_bdev),
				xfs_bounce_submit_ioend);
		return;
	}

	if (ioend_flags & IOMAP_IOEND_INTEGRITY)
		fs_bio_integrity_alloc(bio);
	bio->bi_end_io = xfs_end_io_read;
	bio_set_flag(bio, BIO_COMPLETE_IN_TASK);
	submit_bio(bio);
}

static void
xfs_ioend_put_open_zones(
	struct iomap_ioend	*ioend)
{
	struct iomap_ioend *tmp;

	/*
	 * Put the open zone for all ioends merged into this one (if any).
	 */
	list_for_each_entry(tmp, &ioend->io_list, io_list)
		xfs_open_zone_put(tmp->io_private);

	/*
	 * The main ioend might not have an open zone if the submission failed
	 * before xfs_zone_alloc_and_submit got called.
	 */
	if (ioend->io_private)
		xfs_open_zone_put(ioend->io_private);
}

static void
xfs_end_ioend_write(
	struct iomap_ioend	*ioend)
{
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	struct xfs_mount	*mp = ip->i_mount;
	bool			is_zoned = xfs_is_zoned_inode(ip);
	xfs_off_t		offset = ioend->io_offset;
	size_t			size = ioend->io_size;
	unsigned int		nofs_flag;
	int			error;

	/*
	 * We can allocate memory here while doing writeback on behalf of
	 * memory reclaim.  To avoid memory allocation deadlocks set the
	 * task-wide nofs context for the following operations.
	 */
	nofs_flag = memalloc_nofs_save();

	/*
	 * Just clean up the in-memory structures if the fs has been shut down.
	 */
	if (xfs_is_shutdown(mp)) {
		error = -EIO;
		goto done;
	}

	/*
	 * Clean up all COW blocks and underlying data fork delalloc blocks on
	 * I/O error. The delalloc punch is required because this ioend was
	 * mapped to blocks in the COW fork and the associated pages are no
	 * longer dirty. If we don't remove delalloc blocks here, they become
	 * stale and can corrupt free space accounting on unmount.
	 */
	error = blk_status_to_errno(ioend->io_bio.bi_status);
	if (unlikely(error)) {
		/*
		 * Zoned writes update the in-core open zone accounting before
		 * I/O submission.  A failed write leaves that state
		 * inconsistent, so shut down the filesystem instead of letting
		 * later writers wait forever for open zone space to become
		 * available.
		 */
		if (is_zoned) {
			xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
			goto done;
		}
		if (ioend->io_flags & IOMAP_IOEND_SHARED) {
			ASSERT(!is_zoned);
			xfs_reflink_cancel_cow_range(ip, offset, size, true);
			xfs_bmap_punch_delalloc_range(ip, XFS_DATA_FORK, offset,
					offset + size, NULL);
		}
		goto done;
	}

	/*
	 * Success: commit the COW or unwritten blocks if needed.
	 */
	if (is_zoned)
		error = xfs_zoned_end_io(ip, offset, size, ioend->io_sector,
				ioend->io_private, NULLFSBLOCK);
	else if (ioend->io_flags & IOMAP_IOEND_SHARED)
		error = xfs_reflink_end_cow(ip, offset, size);
	else if (ioend->io_flags & IOMAP_IOEND_UNWRITTEN)
		error = xfs_iomap_write_unwritten(ip, offset, size, false);

	if (!error &&
	    !(ioend->io_flags & IOMAP_IOEND_DIRECT) &&
	    xfs_ioend_is_append(ioend))
		error = xfs_setfilesize(ip, offset, size);
done:
	if (is_zoned)
		xfs_ioend_put_open_zones(ioend);
	iomap_finish_ioends(ioend, error);
	memalloc_nofs_restore(nofs_flag);
}

/*
 * Finish all pending IO completions that require transactional modifications.
 *
 * We try to merge physical and logically contiguous ioends before completion to
 * minimise the number of transactions we need to perform during IO completion.
 * Both unwritten extent conversion and COW remapping need to iterate and modify
 * one physical extent at a time, so we gain nothing by merging physically
 * discontiguous extents here.
 *
 * The ioend chain length that we can be processing here is largely unbound in
 * length and we may have to perform significant amounts of work on each ioend
 * to complete it. Hence we have to be careful about holding the CPU for too
 * long in this loop.
 */
void
xfs_end_io(
	struct work_struct	*work)
{
	struct xfs_inode	*ip =
		container_of(work, struct xfs_inode, i_ioend_work);
	struct iomap_ioend	*ioend;
	struct list_head	tmp;
	unsigned long		flags;

	spin_lock_irqsave(&ip->i_ioend_lock, flags);
	list_replace_init(&ip->i_ioend_list, &tmp);
	spin_unlock_irqrestore(&ip->i_ioend_lock, flags);

	iomap_sort_ioends(&tmp);
	while ((ioend = list_first_entry_or_null(&tmp, struct iomap_ioend,
			io_list))) {
		list_del_init(&ioend->io_list);
		iomap_ioend_try_merge(ioend, &tmp);
		xfs_end_ioend_write(ioend);
		cond_resched();
	}
}

void
xfs_end_bio(
	struct bio		*bio)
{
	struct iomap_ioend	*ioend = iomap_ioend_from_bio(bio);
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	struct xfs_mount	*mp = ip->i_mount;
	unsigned long		flags;

	/*
	 * For Appends record the actually written block number and set the
	 * boundary flag if needed.
	 */
	if (IS_ENABLED(CONFIG_XFS_RT) && bio_is_zone_append(bio)) {
		ioend->io_sector = bio->bi_iter.bi_sector;
		xfs_mark_rtg_boundary(ioend);
	}

	spin_lock_irqsave(&ip->i_ioend_lock, flags);
	if (list_empty(&ip->i_ioend_list))
		WARN_ON_ONCE(!queue_work(mp->m_unwritten_workqueue,
					 &ip->i_ioend_work));
	list_add_tail(&ioend->io_list, &ip->i_ioend_list);
	spin_unlock_irqrestore(&ip->i_ioend_lock, flags);
}
