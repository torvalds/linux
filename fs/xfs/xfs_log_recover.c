/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_log_priv.h"
#include "xfs_buf_item.h"
#include "xfs_log_recover.h"
#include "xfs_extfree_item.h"
#include "xfs_trans_priv.h"
#include "xfs_quota.h"
#include "xfs_rw.h"
#include "xfs_utils.h"

STATIC int	xlog_find_zeroed(xlog_t *, xfs_daddr_t *);
STATIC int	xlog_clear_stale_blocks(xlog_t *, xfs_lsn_t);
STATIC void	xlog_recover_insert_item_backq(xlog_recover_item_t **q,
					       xlog_recover_item_t *item);
#if defined(DEBUG)
STATIC void	xlog_recover_check_summary(xlog_t *);
#else
#define	xlog_recover_check_summary(log)
#endif


/*
 * Sector aligned buffer routines for buffer create/read/write/access
 */

#define XLOG_SECTOR_ROUNDUP_BBCOUNT(log, bbs)	\
	( ((log)->l_sectbb_mask && (bbs & (log)->l_sectbb_mask)) ? \
	((bbs + (log)->l_sectbb_mask + 1) & ~(log)->l_sectbb_mask) : (bbs) )
#define XLOG_SECTOR_ROUNDDOWN_BLKNO(log, bno)	((bno) & ~(log)->l_sectbb_mask)

xfs_buf_t *
xlog_get_bp(
	xlog_t		*log,
	int		nbblks)
{
	if (nbblks <= 0 || nbblks > log->l_logBBsize) {
		xlog_warn("XFS: Invalid block length (0x%x) given for buffer", nbblks);
		XFS_ERROR_REPORT("xlog_get_bp(1)",
				 XFS_ERRLEVEL_HIGH, log->l_mp);
		return NULL;
	}

	if (log->l_sectbb_log) {
		if (nbblks > 1)
			nbblks += XLOG_SECTOR_ROUNDUP_BBCOUNT(log, 1);
		nbblks = XLOG_SECTOR_ROUNDUP_BBCOUNT(log, nbblks);
	}
	return xfs_buf_get_noaddr(BBTOB(nbblks), log->l_mp->m_logdev_targp);
}

void
xlog_put_bp(
	xfs_buf_t	*bp)
{
	xfs_buf_free(bp);
}

STATIC xfs_caddr_t
xlog_align(
	xlog_t		*log,
	xfs_daddr_t	blk_no,
	int		nbblks,
	xfs_buf_t	*bp)
{
	xfs_caddr_t	ptr;

	if (!log->l_sectbb_log)
		return XFS_BUF_PTR(bp);

	ptr = XFS_BUF_PTR(bp) + BBTOB((int)blk_no & log->l_sectbb_mask);
	ASSERT(XFS_BUF_SIZE(bp) >=
		BBTOB(nbblks + (blk_no & log->l_sectbb_mask)));
	return ptr;
}


/*
 * nbblks should be uint, but oh well.  Just want to catch that 32-bit length.
 */
STATIC int
xlog_bread_noalign(
	xlog_t		*log,
	xfs_daddr_t	blk_no,
	int		nbblks,
	xfs_buf_t	*bp)
{
	int		error;

	if (nbblks <= 0 || nbblks > log->l_logBBsize) {
		xlog_warn("XFS: Invalid block length (0x%x) given for buffer", nbblks);
		XFS_ERROR_REPORT("xlog_bread(1)",
				 XFS_ERRLEVEL_HIGH, log->l_mp);
		return EFSCORRUPTED;
	}

	if (log->l_sectbb_log) {
		blk_no = XLOG_SECTOR_ROUNDDOWN_BLKNO(log, blk_no);
		nbblks = XLOG_SECTOR_ROUNDUP_BBCOUNT(log, nbblks);
	}

	ASSERT(nbblks > 0);
	ASSERT(BBTOB(nbblks) <= XFS_BUF_SIZE(bp));
	ASSERT(bp);

	XFS_BUF_SET_ADDR(bp, log->l_logBBstart + blk_no);
	XFS_BUF_READ(bp);
	XFS_BUF_BUSY(bp);
	XFS_BUF_SET_COUNT(bp, BBTOB(nbblks));
	XFS_BUF_SET_TARGET(bp, log->l_mp->m_logdev_targp);

	xfsbdstrat(log->l_mp, bp);
	error = xfs_iowait(bp);
	if (error)
		xfs_ioerror_alert("xlog_bread", log->l_mp,
				  bp, XFS_BUF_ADDR(bp));
	return error;
}

STATIC int
xlog_bread(
	xlog_t		*log,
	xfs_daddr_t	blk_no,
	int		nbblks,
	xfs_buf_t	*bp,
	xfs_caddr_t	*offset)
{
	int		error;

	error = xlog_bread_noalign(log, blk_no, nbblks, bp);
	if (error)
		return error;

	*offset = xlog_align(log, blk_no, nbblks, bp);
	return 0;
}

/*
 * Write out the buffer at the given block for the given number of blocks.
 * The buffer is kept locked across the write and is returned locked.
 * This can only be used for synchronous log writes.
 */
STATIC int
xlog_bwrite(
	xlog_t		*log,
	xfs_daddr_t	blk_no,
	int		nbblks,
	xfs_buf_t	*bp)
{
	int		error;

	if (nbblks <= 0 || nbblks > log->l_logBBsize) {
		xlog_warn("XFS: Invalid block length (0x%x) given for buffer", nbblks);
		XFS_ERROR_REPORT("xlog_bwrite(1)",
				 XFS_ERRLEVEL_HIGH, log->l_mp);
		return EFSCORRUPTED;
	}

	if (log->l_sectbb_log) {
		blk_no = XLOG_SECTOR_ROUNDDOWN_BLKNO(log, blk_no);
		nbblks = XLOG_SECTOR_ROUNDUP_BBCOUNT(log, nbblks);
	}

	ASSERT(nbblks > 0);
	ASSERT(BBTOB(nbblks) <= XFS_BUF_SIZE(bp));

	XFS_BUF_SET_ADDR(bp, log->l_logBBstart + blk_no);
	XFS_BUF_ZEROFLAGS(bp);
	XFS_BUF_BUSY(bp);
	XFS_BUF_HOLD(bp);
	XFS_BUF_PSEMA(bp, PRIBIO);
	XFS_BUF_SET_COUNT(bp, BBTOB(nbblks));
	XFS_BUF_SET_TARGET(bp, log->l_mp->m_logdev_targp);

	if ((error = xfs_bwrite(log->l_mp, bp)))
		xfs_ioerror_alert("xlog_bwrite", log->l_mp,
				  bp, XFS_BUF_ADDR(bp));
	return error;
}

#ifdef DEBUG
/*
 * dump debug superblock and log record information
 */
STATIC void
xlog_header_check_dump(
	xfs_mount_t		*mp,
	xlog_rec_header_t	*head)
{
	int			b;

	cmn_err(CE_DEBUG, "%s:  SB : uuid = ", __func__);
	for (b = 0; b < 16; b++)
		cmn_err(CE_DEBUG, "%02x", ((__uint8_t *)&mp->m_sb.sb_uuid)[b]);
	cmn_err(CE_DEBUG, ", fmt = %d\n", XLOG_FMT);
	cmn_err(CE_DEBUG, "    log : uuid = ");
	for (b = 0; b < 16; b++)
		cmn_err(CE_DEBUG, "%02x", ((__uint8_t *)&head->h_fs_uuid)[b]);
	cmn_err(CE_DEBUG, ", fmt = %d\n", be32_to_cpu(head->h_fmt));
}
#else
#define xlog_header_check_dump(mp, head)
#endif

/*
 * check log record header for recovery
 */
STATIC int
xlog_header_check_recover(
	xfs_mount_t		*mp,
	xlog_rec_header_t	*head)
{
	ASSERT(be32_to_cpu(head->h_magicno) == XLOG_HEADER_MAGIC_NUM);

	/*
	 * IRIX doesn't write the h_fmt field and leaves it zeroed
	 * (XLOG_FMT_UNKNOWN). This stops us from trying to recover
	 * a dirty log created in IRIX.
	 */
	if (unlikely(be32_to_cpu(head->h_fmt) != XLOG_FMT)) {
		xlog_warn(
	"XFS: dirty log written in incompatible format - can't recover");
		xlog_header_check_dump(mp, head);
		XFS_ERROR_REPORT("xlog_header_check_recover(1)",
				 XFS_ERRLEVEL_HIGH, mp);
		return XFS_ERROR(EFSCORRUPTED);
	} else if (unlikely(!uuid_equal(&mp->m_sb.sb_uuid, &head->h_fs_uuid))) {
		xlog_warn(
	"XFS: dirty log entry has mismatched uuid - can't recover");
		xlog_header_check_dump(mp, head);
		XFS_ERROR_REPORT("xlog_header_check_recover(2)",
				 XFS_ERRLEVEL_HIGH, mp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	return 0;
}

/*
 * read the head block of the log and check the header
 */
STATIC int
xlog_header_check_mount(
	xfs_mount_t		*mp,
	xlog_rec_header_t	*head)
{
	ASSERT(be32_to_cpu(head->h_magicno) == XLOG_HEADER_MAGIC_NUM);

	if (uuid_is_nil(&head->h_fs_uuid)) {
		/*
		 * IRIX doesn't write the h_fs_uuid or h_fmt fields. If
		 * h_fs_uuid is nil, we assume this log was last mounted
		 * by IRIX and continue.
		 */
		xlog_warn("XFS: nil uuid in log - IRIX style log");
	} else if (unlikely(!uuid_equal(&mp->m_sb.sb_uuid, &head->h_fs_uuid))) {
		xlog_warn("XFS: log has mismatched uuid - can't recover");
		xlog_header_check_dump(mp, head);
		XFS_ERROR_REPORT("xlog_header_check_mount",
				 XFS_ERRLEVEL_HIGH, mp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	return 0;
}

STATIC void
xlog_recover_iodone(
	struct xfs_buf	*bp)
{
	if (XFS_BUF_GETERROR(bp)) {
		/*
		 * We're not going to bother about retrying
		 * this during recovery. One strike!
		 */
		xfs_ioerror_alert("xlog_recover_iodone",
				  bp->b_mount, bp, XFS_BUF_ADDR(bp));
		xfs_force_shutdown(bp->b_mount, SHUTDOWN_META_IO_ERROR);
	}
	bp->b_mount = NULL;
	XFS_BUF_CLR_IODONE_FUNC(bp);
	xfs_biodone(bp);
}

/*
 * This routine finds (to an approximation) the first block in the physical
 * log which contains the given cycle.  It uses a binary search algorithm.
 * Note that the algorithm can not be perfect because the disk will not
 * necessarily be perfect.
 */
STATIC int
xlog_find_cycle_start(
	xlog_t		*log,
	xfs_buf_t	*bp,
	xfs_daddr_t	first_blk,
	xfs_daddr_t	*last_blk,
	uint		cycle)
{
	xfs_caddr_t	offset;
	xfs_daddr_t	mid_blk;
	uint		mid_cycle;
	int		error;

	mid_blk = BLK_AVG(first_blk, *last_blk);
	while (mid_blk != first_blk && mid_blk != *last_blk) {
		error = xlog_bread(log, mid_blk, 1, bp, &offset);
		if (error)
			return error;
		mid_cycle = xlog_get_cycle(offset);
		if (mid_cycle == cycle) {
			*last_blk = mid_blk;
			/* last_half_cycle == mid_cycle */
		} else {
			first_blk = mid_blk;
			/* first_half_cycle == mid_cycle */
		}
		mid_blk = BLK_AVG(first_blk, *last_blk);
	}
	ASSERT((mid_blk == first_blk && mid_blk+1 == *last_blk) ||
	       (mid_blk == *last_blk && mid_blk-1 == first_blk));

	return 0;
}

/*
 * Check that the range of blocks does not contain the cycle number
 * given.  The scan needs to occur from front to back and the ptr into the
 * region must be updated since a later routine will need to perform another
 * test.  If the region is completely good, we end up returning the same
 * last block number.
 *
 * Set blkno to -1 if we encounter no errors.  This is an invalid block number
 * since we don't ever expect logs to get this large.
 */
STATIC int
xlog_find_verify_cycle(
	xlog_t		*log,
	xfs_daddr_t	start_blk,
	int		nbblks,
	uint		stop_on_cycle_no,
	xfs_daddr_t	*new_blk)
{
	xfs_daddr_t	i, j;
	uint		cycle;
	xfs_buf_t	*bp;
	xfs_daddr_t	bufblks;
	xfs_caddr_t	buf = NULL;
	int		error = 0;

	bufblks = 1 << ffs(nbblks);

	while (!(bp = xlog_get_bp(log, bufblks))) {
		/* can't get enough memory to do everything in one big buffer */
		bufblks >>= 1;
		if (bufblks <= log->l_sectbb_log)
			return ENOMEM;
	}

	for (i = start_blk; i < start_blk + nbblks; i += bufblks) {
		int	bcount;

		bcount = min(bufblks, (start_blk + nbblks - i));

		error = xlog_bread(log, i, bcount, bp, &buf);
		if (error)
			goto out;

		for (j = 0; j < bcount; j++) {
			cycle = xlog_get_cycle(buf);
			if (cycle == stop_on_cycle_no) {
				*new_blk = i+j;
				goto out;
			}

			buf += BBSIZE;
		}
	}

	*new_blk = -1;

out:
	xlog_put_bp(bp);
	return error;
}

/*
 * Potentially backup over partial log record write.
 *
 * In the typical case, last_blk is the number of the block directly after
 * a good log record.  Therefore, we subtract one to get the block number
 * of the last block in the given buffer.  extra_bblks contains the number
 * of blocks we would have read on a previous read.  This happens when the
 * last log record is split over the end of the physical log.
 *
 * extra_bblks is the number of blocks potentially verified on a previous
 * call to this routine.
 */
STATIC int
xlog_find_verify_log_record(
	xlog_t			*log,
	xfs_daddr_t		start_blk,
	xfs_daddr_t		*last_blk,
	int			extra_bblks)
{
	xfs_daddr_t		i;
	xfs_buf_t		*bp;
	xfs_caddr_t		offset = NULL;
	xlog_rec_header_t	*head = NULL;
	int			error = 0;
	int			smallmem = 0;
	int			num_blks = *last_blk - start_blk;
	int			xhdrs;

	ASSERT(start_blk != 0 || *last_blk != start_blk);

	if (!(bp = xlog_get_bp(log, num_blks))) {
		if (!(bp = xlog_get_bp(log, 1)))
			return ENOMEM;
		smallmem = 1;
	} else {
		error = xlog_bread(log, start_blk, num_blks, bp, &offset);
		if (error)
			goto out;
		offset += ((num_blks - 1) << BBSHIFT);
	}

	for (i = (*last_blk) - 1; i >= 0; i--) {
		if (i < start_blk) {
			/* valid log record not found */
			xlog_warn(
		"XFS: Log inconsistent (didn't find previous header)");
			ASSERT(0);
			error = XFS_ERROR(EIO);
			goto out;
		}

		if (smallmem) {
			error = xlog_bread(log, i, 1, bp, &offset);
			if (error)
				goto out;
		}

		head = (xlog_rec_header_t *)offset;

		if (XLOG_HEADER_MAGIC_NUM == be32_to_cpu(head->h_magicno))
			break;

		if (!smallmem)
			offset -= BBSIZE;
	}

	/*
	 * We hit the beginning of the physical log & still no header.  Return
	 * to caller.  If caller can handle a return of -1, then this routine
	 * will be called again for the end of the physical log.
	 */
	if (i == -1) {
		error = -1;
		goto out;
	}

	/*
	 * We have the final block of the good log (the first block
	 * of the log record _before_ the head. So we check the uuid.
	 */
	if ((error = xlog_header_check_mount(log->l_mp, head)))
		goto out;

	/*
	 * We may have found a log record header before we expected one.
	 * last_blk will be the 1st block # with a given cycle #.  We may end
	 * up reading an entire log record.  In this case, we don't want to
	 * reset last_blk.  Only when last_blk points in the middle of a log
	 * record do we update last_blk.
	 */
	if (xfs_sb_version_haslogv2(&log->l_mp->m_sb)) {
		uint	h_size = be32_to_cpu(head->h_size);

		xhdrs = h_size / XLOG_HEADER_CYCLE_SIZE;
		if (h_size % XLOG_HEADER_CYCLE_SIZE)
			xhdrs++;
	} else {
		xhdrs = 1;
	}

	if (*last_blk - i + extra_bblks !=
	    BTOBB(be32_to_cpu(head->h_len)) + xhdrs)
		*last_blk = i;

out:
	xlog_put_bp(bp);
	return error;
}

/*
 * Head is defined to be the point of the log where the next log write
 * write could go.  This means that incomplete LR writes at the end are
 * eliminated when calculating the head.  We aren't guaranteed that previous
 * LR have complete transactions.  We only know that a cycle number of
 * current cycle number -1 won't be present in the log if we start writing
 * from our current block number.
 *
 * last_blk contains the block number of the first block with a given
 * cycle number.
 *
 * Return: zero if normal, non-zero if error.
 */
STATIC int
xlog_find_head(
	xlog_t 		*log,
	xfs_daddr_t	*return_head_blk)
{
	xfs_buf_t	*bp;
	xfs_caddr_t	offset;
	xfs_daddr_t	new_blk, first_blk, start_blk, last_blk, head_blk;
	int		num_scan_bblks;
	uint		first_half_cycle, last_half_cycle;
	uint		stop_on_cycle;
	int		error, log_bbnum = log->l_logBBsize;

	/* Is the end of the log device zeroed? */
	if ((error = xlog_find_zeroed(log, &first_blk)) == -1) {
		*return_head_blk = first_blk;

		/* Is the whole lot zeroed? */
		if (!first_blk) {
			/* Linux XFS shouldn't generate totally zeroed logs -
			 * mkfs etc write a dummy unmount record to a fresh
			 * log so we can store the uuid in there
			 */
			xlog_warn("XFS: totally zeroed log");
		}

		return 0;
	} else if (error) {
		xlog_warn("XFS: empty log check failed");
		return error;
	}

	first_blk = 0;			/* get cycle # of 1st block */
	bp = xlog_get_bp(log, 1);
	if (!bp)
		return ENOMEM;

	error = xlog_bread(log, 0, 1, bp, &offset);
	if (error)
		goto bp_err;

	first_half_cycle = xlog_get_cycle(offset);

	last_blk = head_blk = log_bbnum - 1;	/* get cycle # of last block */
	error = xlog_bread(log, last_blk, 1, bp, &offset);
	if (error)
		goto bp_err;

	last_half_cycle = xlog_get_cycle(offset);
	ASSERT(last_half_cycle != 0);

	/*
	 * If the 1st half cycle number is equal to the last half cycle number,
	 * then the entire log is stamped with the same cycle number.  In this
	 * case, head_blk can't be set to zero (which makes sense).  The below
	 * math doesn't work out properly with head_blk equal to zero.  Instead,
	 * we set it to log_bbnum which is an invalid block number, but this
	 * value makes the math correct.  If head_blk doesn't changed through
	 * all the tests below, *head_blk is set to zero at the very end rather
	 * than log_bbnum.  In a sense, log_bbnum and zero are the same block
	 * in a circular file.
	 */
	if (first_half_cycle == last_half_cycle) {
		/*
		 * In this case we believe that the entire log should have
		 * cycle number last_half_cycle.  We need to scan backwards
		 * from the end verifying that there are no holes still
		 * containing last_half_cycle - 1.  If we find such a hole,
		 * then the start of that hole will be the new head.  The
		 * simple case looks like
		 *        x | x ... | x - 1 | x
		 * Another case that fits this picture would be
		 *        x | x + 1 | x ... | x
		 * In this case the head really is somewhere at the end of the
		 * log, as one of the latest writes at the beginning was
		 * incomplete.
		 * One more case is
		 *        x | x + 1 | x ... | x - 1 | x
		 * This is really the combination of the above two cases, and
		 * the head has to end up at the start of the x-1 hole at the
		 * end of the log.
		 *
		 * In the 256k log case, we will read from the beginning to the
		 * end of the log and search for cycle numbers equal to x-1.
		 * We don't worry about the x+1 blocks that we encounter,
		 * because we know that they cannot be the head since the log
		 * started with x.
		 */
		head_blk = log_bbnum;
		stop_on_cycle = last_half_cycle - 1;
	} else {
		/*
		 * In this case we want to find the first block with cycle
		 * number matching last_half_cycle.  We expect the log to be
		 * some variation on
		 *        x + 1 ... | x ...
		 * The first block with cycle number x (last_half_cycle) will
		 * be where the new head belongs.  First we do a binary search
		 * for the first occurrence of last_half_cycle.  The binary
		 * search may not be totally accurate, so then we scan back
		 * from there looking for occurrences of last_half_cycle before
		 * us.  If that backwards scan wraps around the beginning of
		 * the log, then we look for occurrences of last_half_cycle - 1
		 * at the end of the log.  The cases we're looking for look
		 * like
		 *        x + 1 ... | x | x + 1 | x ...
		 *                               ^ binary search stopped here
		 * or
		 *        x + 1 ... | x ... | x - 1 | x
		 *        <---------> less than scan distance
		 */
		stop_on_cycle = last_half_cycle;
		if ((error = xlog_find_cycle_start(log, bp, first_blk,
						&head_blk, last_half_cycle)))
			goto bp_err;
	}

	/*
	 * Now validate the answer.  Scan back some number of maximum possible
	 * blocks and make sure each one has the expected cycle number.  The
	 * maximum is determined by the total possible amount of buffering
	 * in the in-core log.  The following number can be made tighter if
	 * we actually look at the block size of the filesystem.
	 */
	num_scan_bblks = XLOG_TOTAL_REC_SHIFT(log);
	if (head_blk >= num_scan_bblks) {
		/*
		 * We are guaranteed that the entire check can be performed
		 * in one buffer.
		 */
		start_blk = head_blk - num_scan_bblks;
		if ((error = xlog_find_verify_cycle(log,
						start_blk, num_scan_bblks,
						stop_on_cycle, &new_blk)))
			goto bp_err;
		if (new_blk != -1)
			head_blk = new_blk;
	} else {		/* need to read 2 parts of log */
		/*
		 * We are going to scan backwards in the log in two parts.
		 * First we scan the physical end of the log.  In this part
		 * of the log, we are looking for blocks with cycle number
		 * last_half_cycle - 1.
		 * If we find one, then we know that the log starts there, as
		 * we've found a hole that didn't get written in going around
		 * the end of the physical log.  The simple case for this is
		 *        x + 1 ... | x ... | x - 1 | x
		 *        <---------> less than scan distance
		 * If all of the blocks at the end of the log have cycle number
		 * last_half_cycle, then we check the blocks at the start of
		 * the log looking for occurrences of last_half_cycle.  If we
		 * find one, then our current estimate for the location of the
		 * first occurrence of last_half_cycle is wrong and we move
		 * back to the hole we've found.  This case looks like
		 *        x + 1 ... | x | x + 1 | x ...
		 *                               ^ binary search stopped here
		 * Another case we need to handle that only occurs in 256k
		 * logs is
		 *        x + 1 ... | x ... | x+1 | x ...
		 *                   ^ binary search stops here
		 * In a 256k log, the scan at the end of the log will see the
		 * x + 1 blocks.  We need to skip past those since that is
		 * certainly not the head of the log.  By searching for
		 * last_half_cycle-1 we accomplish that.
		 */
		start_blk = log_bbnum - num_scan_bblks + head_blk;
		ASSERT(head_blk <= INT_MAX &&
			(xfs_daddr_t) num_scan_bblks - head_blk >= 0);
		if ((error = xlog_find_verify_cycle(log, start_blk,
					num_scan_bblks - (int)head_blk,
					(stop_on_cycle - 1), &new_blk)))
			goto bp_err;
		if (new_blk != -1) {
			head_blk = new_blk;
			goto bad_blk;
		}

		/*
		 * Scan beginning of log now.  The last part of the physical
		 * log is good.  This scan needs to verify that it doesn't find
		 * the last_half_cycle.
		 */
		start_blk = 0;
		ASSERT(head_blk <= INT_MAX);
		if ((error = xlog_find_verify_cycle(log,
					start_blk, (int)head_blk,
					stop_on_cycle, &new_blk)))
			goto bp_err;
		if (new_blk != -1)
			head_blk = new_blk;
	}

 bad_blk:
	/*
	 * Now we need to make sure head_blk is not pointing to a block in
	 * the middle of a log record.
	 */
	num_scan_bblks = XLOG_REC_SHIFT(log);
	if (head_blk >= num_scan_bblks) {
		start_blk = head_blk - num_scan_bblks; /* don't read head_blk */

		/* start ptr at last block ptr before head_blk */
		if ((error = xlog_find_verify_log_record(log, start_blk,
							&head_blk, 0)) == -1) {
			error = XFS_ERROR(EIO);
			goto bp_err;
		} else if (error)
			goto bp_err;
	} else {
		start_blk = 0;
		ASSERT(head_blk <= INT_MAX);
		if ((error = xlog_find_verify_log_record(log, start_blk,
							&head_blk, 0)) == -1) {
			/* We hit the beginning of the log during our search */
			start_blk = log_bbnum - num_scan_bblks + head_blk;
			new_blk = log_bbnum;
			ASSERT(start_blk <= INT_MAX &&
				(xfs_daddr_t) log_bbnum-start_blk >= 0);
			ASSERT(head_blk <= INT_MAX);
			if ((error = xlog_find_verify_log_record(log,
							start_blk, &new_blk,
							(int)head_blk)) == -1) {
				error = XFS_ERROR(EIO);
				goto bp_err;
			} else if (error)
				goto bp_err;
			if (new_blk != log_bbnum)
				head_blk = new_blk;
		} else if (error)
			goto bp_err;
	}

	xlog_put_bp(bp);
	if (head_blk == log_bbnum)
		*return_head_blk = 0;
	else
		*return_head_blk = head_blk;
	/*
	 * When returning here, we have a good block number.  Bad block
	 * means that during a previous crash, we didn't have a clean break
	 * from cycle number N to cycle number N-1.  In this case, we need
	 * to find the first block with cycle number N-1.
	 */
	return 0;

 bp_err:
	xlog_put_bp(bp);

	if (error)
	    xlog_warn("XFS: failed to find log head");
	return error;
}

/*
 * Find the sync block number or the tail of the log.
 *
 * This will be the block number of the last record to have its
 * associated buffers synced to disk.  Every log record header has
 * a sync lsn embedded in it.  LSNs hold block numbers, so it is easy
 * to get a sync block number.  The only concern is to figure out which
 * log record header to believe.
 *
 * The following algorithm uses the log record header with the largest
 * lsn.  The entire log record does not need to be valid.  We only care
 * that the header is valid.
 *
 * We could speed up search by using current head_blk buffer, but it is not
 * available.
 */
int
xlog_find_tail(
	xlog_t			*log,
	xfs_daddr_t		*head_blk,
	xfs_daddr_t		*tail_blk)
{
	xlog_rec_header_t	*rhead;
	xlog_op_header_t	*op_head;
	xfs_caddr_t		offset = NULL;
	xfs_buf_t		*bp;
	int			error, i, found;
	xfs_daddr_t		umount_data_blk;
	xfs_daddr_t		after_umount_blk;
	xfs_lsn_t		tail_lsn;
	int			hblks;

	found = 0;

	/*
	 * Find previous log record
	 */
	if ((error = xlog_find_head(log, head_blk)))
		return error;

	bp = xlog_get_bp(log, 1);
	if (!bp)
		return ENOMEM;
	if (*head_blk == 0) {				/* special case */
		error = xlog_bread(log, 0, 1, bp, &offset);
		if (error)
			goto bread_err;

		if (xlog_get_cycle(offset) == 0) {
			*tail_blk = 0;
			/* leave all other log inited values alone */
			goto exit;
		}
	}

	/*
	 * Search backwards looking for log record header block
	 */
	ASSERT(*head_blk < INT_MAX);
	for (i = (int)(*head_blk) - 1; i >= 0; i--) {
		error = xlog_bread(log, i, 1, bp, &offset);
		if (error)
			goto bread_err;

		if (XLOG_HEADER_MAGIC_NUM == be32_to_cpu(*(__be32 *)offset)) {
			found = 1;
			break;
		}
	}
	/*
	 * If we haven't found the log record header block, start looking
	 * again from the end of the physical log.  XXXmiken: There should be
	 * a check here to make sure we didn't search more than N blocks in
	 * the previous code.
	 */
	if (!found) {
		for (i = log->l_logBBsize - 1; i >= (int)(*head_blk); i--) {
			error = xlog_bread(log, i, 1, bp, &offset);
			if (error)
				goto bread_err;

			if (XLOG_HEADER_MAGIC_NUM ==
			    be32_to_cpu(*(__be32 *)offset)) {
				found = 2;
				break;
			}
		}
	}
	if (!found) {
		xlog_warn("XFS: xlog_find_tail: couldn't find sync record");
		ASSERT(0);
		return XFS_ERROR(EIO);
	}

	/* find blk_no of tail of log */
	rhead = (xlog_rec_header_t *)offset;
	*tail_blk = BLOCK_LSN(be64_to_cpu(rhead->h_tail_lsn));

	/*
	 * Reset log values according to the state of the log when we
	 * crashed.  In the case where head_blk == 0, we bump curr_cycle
	 * one because the next write starts a new cycle rather than
	 * continuing the cycle of the last good log record.  At this
	 * point we have guaranteed that all partial log records have been
	 * accounted for.  Therefore, we know that the last good log record
	 * written was complete and ended exactly on the end boundary
	 * of the physical log.
	 */
	log->l_prev_block = i;
	log->l_curr_block = (int)*head_blk;
	log->l_curr_cycle = be32_to_cpu(rhead->h_cycle);
	if (found == 2)
		log->l_curr_cycle++;
	log->l_tail_lsn = be64_to_cpu(rhead->h_tail_lsn);
	log->l_last_sync_lsn = be64_to_cpu(rhead->h_lsn);
	log->l_grant_reserve_cycle = log->l_curr_cycle;
	log->l_grant_reserve_bytes = BBTOB(log->l_curr_block);
	log->l_grant_write_cycle = log->l_curr_cycle;
	log->l_grant_write_bytes = BBTOB(log->l_curr_block);

	/*
	 * Look for unmount record.  If we find it, then we know there
	 * was a clean unmount.  Since 'i' could be the last block in
	 * the physical log, we convert to a log block before comparing
	 * to the head_blk.
	 *
	 * Save the current tail lsn to use to pass to
	 * xlog_clear_stale_blocks() below.  We won't want to clear the
	 * unmount record if there is one, so we pass the lsn of the
	 * unmount record rather than the block after it.
	 */
	if (xfs_sb_version_haslogv2(&log->l_mp->m_sb)) {
		int	h_size = be32_to_cpu(rhead->h_size);
		int	h_version = be32_to_cpu(rhead->h_version);

		if ((h_version & XLOG_VERSION_2) &&
		    (h_size > XLOG_HEADER_CYCLE_SIZE)) {
			hblks = h_size / XLOG_HEADER_CYCLE_SIZE;
			if (h_size % XLOG_HEADER_CYCLE_SIZE)
				hblks++;
		} else {
			hblks = 1;
		}
	} else {
		hblks = 1;
	}
	after_umount_blk = (i + hblks + (int)
		BTOBB(be32_to_cpu(rhead->h_len))) % log->l_logBBsize;
	tail_lsn = log->l_tail_lsn;
	if (*head_blk == after_umount_blk &&
	    be32_to_cpu(rhead->h_num_logops) == 1) {
		umount_data_blk = (i + hblks) % log->l_logBBsize;
		error = xlog_bread(log, umount_data_blk, 1, bp, &offset);
		if (error)
			goto bread_err;

		op_head = (xlog_op_header_t *)offset;
		if (op_head->oh_flags & XLOG_UNMOUNT_TRANS) {
			/*
			 * Set tail and last sync so that newly written
			 * log records will point recovery to after the
			 * current unmount record.
			 */
			log->l_tail_lsn =
				xlog_assign_lsn(log->l_curr_cycle,
						after_umount_blk);
			log->l_last_sync_lsn =
				xlog_assign_lsn(log->l_curr_cycle,
						after_umount_blk);
			*tail_blk = after_umount_blk;

			/*
			 * Note that the unmount was clean. If the unmount
			 * was not clean, we need to know this to rebuild the
			 * superblock counters from the perag headers if we
			 * have a filesystem using non-persistent counters.
			 */
			log->l_mp->m_flags |= XFS_MOUNT_WAS_CLEAN;
		}
	}

	/*
	 * Make sure that there are no blocks in front of the head
	 * with the same cycle number as the head.  This can happen
	 * because we allow multiple outstanding log writes concurrently,
	 * and the later writes might make it out before earlier ones.
	 *
	 * We use the lsn from before modifying it so that we'll never
	 * overwrite the unmount record after a clean unmount.
	 *
	 * Do this only if we are going to recover the filesystem
	 *
	 * NOTE: This used to say "if (!readonly)"
	 * However on Linux, we can & do recover a read-only filesystem.
	 * We only skip recovery if NORECOVERY is specified on mount,
	 * in which case we would not be here.
	 *
	 * But... if the -device- itself is readonly, just skip this.
	 * We can't recover this device anyway, so it won't matter.
	 */
	if (!xfs_readonly_buftarg(log->l_mp->m_logdev_targp)) {
		error = xlog_clear_stale_blocks(log, tail_lsn);
	}

bread_err:
exit:
	xlog_put_bp(bp);

	if (error)
		xlog_warn("XFS: failed to locate log tail");
	return error;
}

/*
 * Is the log zeroed at all?
 *
 * The last binary search should be changed to perform an X block read
 * once X becomes small enough.  You can then search linearly through
 * the X blocks.  This will cut down on the number of reads we need to do.
 *
 * If the log is partially zeroed, this routine will pass back the blkno
 * of the first block with cycle number 0.  It won't have a complete LR
 * preceding it.
 *
 * Return:
 *	0  => the log is completely written to
 *	-1 => use *blk_no as the first block of the log
 *	>0 => error has occurred
 */
STATIC int
xlog_find_zeroed(
	xlog_t		*log,
	xfs_daddr_t	*blk_no)
{
	xfs_buf_t	*bp;
	xfs_caddr_t	offset;
	uint	        first_cycle, last_cycle;
	xfs_daddr_t	new_blk, last_blk, start_blk;
	xfs_daddr_t     num_scan_bblks;
	int	        error, log_bbnum = log->l_logBBsize;

	*blk_no = 0;

	/* check totally zeroed log */
	bp = xlog_get_bp(log, 1);
	if (!bp)
		return ENOMEM;
	error = xlog_bread(log, 0, 1, bp, &offset);
	if (error)
		goto bp_err;

	first_cycle = xlog_get_cycle(offset);
	if (first_cycle == 0) {		/* completely zeroed log */
		*blk_no = 0;
		xlog_put_bp(bp);
		return -1;
	}

	/* check partially zeroed log */
	error = xlog_bread(log, log_bbnum-1, 1, bp, &offset);
	if (error)
		goto bp_err;

	last_cycle = xlog_get_cycle(offset);
	if (last_cycle != 0) {		/* log completely written to */
		xlog_put_bp(bp);
		return 0;
	} else if (first_cycle != 1) {
		/*
		 * If the cycle of the last block is zero, the cycle of
		 * the first block must be 1. If it's not, maybe we're
		 * not looking at a log... Bail out.
		 */
		xlog_warn("XFS: Log inconsistent or not a log (last==0, first!=1)");
		return XFS_ERROR(EINVAL);
	}

	/* we have a partially zeroed log */
	last_blk = log_bbnum-1;
	if ((error = xlog_find_cycle_start(log, bp, 0, &last_blk, 0)))
		goto bp_err;

	/*
	 * Validate the answer.  Because there is no way to guarantee that
	 * the entire log is made up of log records which are the same size,
	 * we scan over the defined maximum blocks.  At this point, the maximum
	 * is not chosen to mean anything special.   XXXmiken
	 */
	num_scan_bblks = XLOG_TOTAL_REC_SHIFT(log);
	ASSERT(num_scan_bblks <= INT_MAX);

	if (last_blk < num_scan_bblks)
		num_scan_bblks = last_blk;
	start_blk = last_blk - num_scan_bblks;

	/*
	 * We search for any instances of cycle number 0 that occur before
	 * our current estimate of the head.  What we're trying to detect is
	 *        1 ... | 0 | 1 | 0...
	 *                       ^ binary search ends here
	 */
	if ((error = xlog_find_verify_cycle(log, start_blk,
					 (int)num_scan_bblks, 0, &new_blk)))
		goto bp_err;
	if (new_blk != -1)
		last_blk = new_blk;

	/*
	 * Potentially backup over partial log record write.  We don't need
	 * to search the end of the log because we know it is zero.
	 */
	if ((error = xlog_find_verify_log_record(log, start_blk,
				&last_blk, 0)) == -1) {
	    error = XFS_ERROR(EIO);
	    goto bp_err;
	} else if (error)
	    goto bp_err;

	*blk_no = last_blk;
bp_err:
	xlog_put_bp(bp);
	if (error)
		return error;
	return -1;
}

/*
 * These are simple subroutines used by xlog_clear_stale_blocks() below
 * to initialize a buffer full of empty log record headers and write
 * them into the log.
 */
STATIC void
xlog_add_record(
	xlog_t			*log,
	xfs_caddr_t		buf,
	int			cycle,
	int			block,
	int			tail_cycle,
	int			tail_block)
{
	xlog_rec_header_t	*recp = (xlog_rec_header_t *)buf;

	memset(buf, 0, BBSIZE);
	recp->h_magicno = cpu_to_be32(XLOG_HEADER_MAGIC_NUM);
	recp->h_cycle = cpu_to_be32(cycle);
	recp->h_version = cpu_to_be32(
			xfs_sb_version_haslogv2(&log->l_mp->m_sb) ? 2 : 1);
	recp->h_lsn = cpu_to_be64(xlog_assign_lsn(cycle, block));
	recp->h_tail_lsn = cpu_to_be64(xlog_assign_lsn(tail_cycle, tail_block));
	recp->h_fmt = cpu_to_be32(XLOG_FMT);
	memcpy(&recp->h_fs_uuid, &log->l_mp->m_sb.sb_uuid, sizeof(uuid_t));
}

STATIC int
xlog_write_log_records(
	xlog_t		*log,
	int		cycle,
	int		start_block,
	int		blocks,
	int		tail_cycle,
	int		tail_block)
{
	xfs_caddr_t	offset;
	xfs_buf_t	*bp;
	int		balign, ealign;
	int		sectbb = XLOG_SECTOR_ROUNDUP_BBCOUNT(log, 1);
	int		end_block = start_block + blocks;
	int		bufblks;
	int		error = 0;
	int		i, j = 0;

	bufblks = 1 << ffs(blocks);
	while (!(bp = xlog_get_bp(log, bufblks))) {
		bufblks >>= 1;
		if (bufblks <= log->l_sectbb_log)
			return ENOMEM;
	}

	/* We may need to do a read at the start to fill in part of
	 * the buffer in the starting sector not covered by the first
	 * write below.
	 */
	balign = XLOG_SECTOR_ROUNDDOWN_BLKNO(log, start_block);
	if (balign != start_block) {
		error = xlog_bread_noalign(log, start_block, 1, bp);
		if (error)
			goto out_put_bp;

		j = start_block - balign;
	}

	for (i = start_block; i < end_block; i += bufblks) {
		int		bcount, endcount;

		bcount = min(bufblks, end_block - start_block);
		endcount = bcount - j;

		/* We may need to do a read at the end to fill in part of
		 * the buffer in the final sector not covered by the write.
		 * If this is the same sector as the above read, skip it.
		 */
		ealign = XLOG_SECTOR_ROUNDDOWN_BLKNO(log, end_block);
		if (j == 0 && (start_block + endcount > ealign)) {
			offset = XFS_BUF_PTR(bp);
			balign = BBTOB(ealign - start_block);
			error = XFS_BUF_SET_PTR(bp, offset + balign,
						BBTOB(sectbb));
			if (error)
				break;

			error = xlog_bread_noalign(log, ealign, sectbb, bp);
			if (error)
				break;

			error = XFS_BUF_SET_PTR(bp, offset, bufblks);
			if (error)
				break;
		}

		offset = xlog_align(log, start_block, endcount, bp);
		for (; j < endcount; j++) {
			xlog_add_record(log, offset, cycle, i+j,
					tail_cycle, tail_block);
			offset += BBSIZE;
		}
		error = xlog_bwrite(log, start_block, endcount, bp);
		if (error)
			break;
		start_block += endcount;
		j = 0;
	}

 out_put_bp:
	xlog_put_bp(bp);
	return error;
}

/*
 * This routine is called to blow away any incomplete log writes out
 * in front of the log head.  We do this so that we won't become confused
 * if we come up, write only a little bit more, and then crash again.
 * If we leave the partial log records out there, this situation could
 * cause us to think those partial writes are valid blocks since they
 * have the current cycle number.  We get rid of them by overwriting them
 * with empty log records with the old cycle number rather than the
 * current one.
 *
 * The tail lsn is passed in rather than taken from
 * the log so that we will not write over the unmount record after a
 * clean unmount in a 512 block log.  Doing so would leave the log without
 * any valid log records in it until a new one was written.  If we crashed
 * during that time we would not be able to recover.
 */
STATIC int
xlog_clear_stale_blocks(
	xlog_t		*log,
	xfs_lsn_t	tail_lsn)
{
	int		tail_cycle, head_cycle;
	int		tail_block, head_block;
	int		tail_distance, max_distance;
	int		distance;
	int		error;

	tail_cycle = CYCLE_LSN(tail_lsn);
	tail_block = BLOCK_LSN(tail_lsn);
	head_cycle = log->l_curr_cycle;
	head_block = log->l_curr_block;

	/*
	 * Figure out the distance between the new head of the log
	 * and the tail.  We want to write over any blocks beyond the
	 * head that we may have written just before the crash, but
	 * we don't want to overwrite the tail of the log.
	 */
	if (head_cycle == tail_cycle) {
		/*
		 * The tail is behind the head in the physical log,
		 * so the distance from the head to the tail is the
		 * distance from the head to the end of the log plus
		 * the distance from the beginning of the log to the
		 * tail.
		 */
		if (unlikely(head_block < tail_block || head_block >= log->l_logBBsize)) {
			XFS_ERROR_REPORT("xlog_clear_stale_blocks(1)",
					 XFS_ERRLEVEL_LOW, log->l_mp);
			return XFS_ERROR(EFSCORRUPTED);
		}
		tail_distance = tail_block + (log->l_logBBsize - head_block);
	} else {
		/*
		 * The head is behind the tail in the physical log,
		 * so the distance from the head to the tail is just
		 * the tail block minus the head block.
		 */
		if (unlikely(head_block >= tail_block || head_cycle != (tail_cycle + 1))){
			XFS_ERROR_REPORT("xlog_clear_stale_blocks(2)",
					 XFS_ERRLEVEL_LOW, log->l_mp);
			return XFS_ERROR(EFSCORRUPTED);
		}
		tail_distance = tail_block - head_block;
	}

	/*
	 * If the head is right up against the tail, we can't clear
	 * anything.
	 */
	if (tail_distance <= 0) {
		ASSERT(tail_distance == 0);
		return 0;
	}

	max_distance = XLOG_TOTAL_REC_SHIFT(log);
	/*
	 * Take the smaller of the maximum amount of outstanding I/O
	 * we could have and the distance to the tail to clear out.
	 * We take the smaller so that we don't overwrite the tail and
	 * we don't waste all day writing from the head to the tail
	 * for no reason.
	 */
	max_distance = MIN(max_distance, tail_distance);

	if ((head_block + max_distance) <= log->l_logBBsize) {
		/*
		 * We can stomp all the blocks we need to without
		 * wrapping around the end of the log.  Just do it
		 * in a single write.  Use the cycle number of the
		 * current cycle minus one so that the log will look like:
		 *     n ... | n - 1 ...
		 */
		error = xlog_write_log_records(log, (head_cycle - 1),
				head_block, max_distance, tail_cycle,
				tail_block);
		if (error)
			return error;
	} else {
		/*
		 * We need to wrap around the end of the physical log in
		 * order to clear all the blocks.  Do it in two separate
		 * I/Os.  The first write should be from the head to the
		 * end of the physical log, and it should use the current
		 * cycle number minus one just like above.
		 */
		distance = log->l_logBBsize - head_block;
		error = xlog_write_log_records(log, (head_cycle - 1),
				head_block, distance, tail_cycle,
				tail_block);

		if (error)
			return error;

		/*
		 * Now write the blocks at the start of the physical log.
		 * This writes the remainder of the blocks we want to clear.
		 * It uses the current cycle number since we're now on the
		 * same cycle as the head so that we get:
		 *    n ... n ... | n - 1 ...
		 *    ^^^^^ blocks we're writing
		 */
		distance = max_distance - (log->l_logBBsize - head_block);
		error = xlog_write_log_records(log, head_cycle, 0, distance,
				tail_cycle, tail_block);
		if (error)
			return error;
	}

	return 0;
}

/******************************************************************************
 *
 *		Log recover routines
 *
 ******************************************************************************
 */

STATIC xlog_recover_t *
xlog_recover_find_tid(
	xlog_recover_t		*q,
	xlog_tid_t		tid)
{
	xlog_recover_t		*p = q;

	while (p != NULL) {
		if (p->r_log_tid == tid)
		    break;
		p = p->r_next;
	}
	return p;
}

STATIC void
xlog_recover_put_hashq(
	xlog_recover_t		**q,
	xlog_recover_t		*trans)
{
	trans->r_next = *q;
	*q = trans;
}

STATIC void
xlog_recover_add_item(
	xlog_recover_item_t	**itemq)
{
	xlog_recover_item_t	*item;

	item = kmem_zalloc(sizeof(xlog_recover_item_t), KM_SLEEP);
	xlog_recover_insert_item_backq(itemq, item);
}

STATIC int
xlog_recover_add_to_cont_trans(
	xlog_recover_t		*trans,
	xfs_caddr_t		dp,
	int			len)
{
	xlog_recover_item_t	*item;
	xfs_caddr_t		ptr, old_ptr;
	int			old_len;

	item = trans->r_itemq;
	if (item == NULL) {
		/* finish copying rest of trans header */
		xlog_recover_add_item(&trans->r_itemq);
		ptr = (xfs_caddr_t) &trans->r_theader +
				sizeof(xfs_trans_header_t) - len;
		memcpy(ptr, dp, len); /* d, s, l */
		return 0;
	}
	item = item->ri_prev;

	old_ptr = item->ri_buf[item->ri_cnt-1].i_addr;
	old_len = item->ri_buf[item->ri_cnt-1].i_len;

	ptr = kmem_realloc(old_ptr, len+old_len, old_len, 0u);
	memcpy(&ptr[old_len], dp, len); /* d, s, l */
	item->ri_buf[item->ri_cnt-1].i_len += len;
	item->ri_buf[item->ri_cnt-1].i_addr = ptr;
	return 0;
}

/*
 * The next region to add is the start of a new region.  It could be
 * a whole region or it could be the first part of a new region.  Because
 * of this, the assumption here is that the type and size fields of all
 * format structures fit into the first 32 bits of the structure.
 *
 * This works because all regions must be 32 bit aligned.  Therefore, we
 * either have both fields or we have neither field.  In the case we have
 * neither field, the data part of the region is zero length.  We only have
 * a log_op_header and can throw away the header since a new one will appear
 * later.  If we have at least 4 bytes, then we can determine how many regions
 * will appear in the current log item.
 */
STATIC int
xlog_recover_add_to_trans(
	xlog_recover_t		*trans,
	xfs_caddr_t		dp,
	int			len)
{
	xfs_inode_log_format_t	*in_f;			/* any will do */
	xlog_recover_item_t	*item;
	xfs_caddr_t		ptr;

	if (!len)
		return 0;
	item = trans->r_itemq;
	if (item == NULL) {
		/* we need to catch log corruptions here */
		if (*(uint *)dp != XFS_TRANS_HEADER_MAGIC) {
			xlog_warn("XFS: xlog_recover_add_to_trans: "
				  "bad header magic number");
			ASSERT(0);
			return XFS_ERROR(EIO);
		}
		if (len == sizeof(xfs_trans_header_t))
			xlog_recover_add_item(&trans->r_itemq);
		memcpy(&trans->r_theader, dp, len); /* d, s, l */
		return 0;
	}

	ptr = kmem_alloc(len, KM_SLEEP);
	memcpy(ptr, dp, len);
	in_f = (xfs_inode_log_format_t *)ptr;

	if (item->ri_prev->ri_total != 0 &&
	     item->ri_prev->ri_total == item->ri_prev->ri_cnt) {
		xlog_recover_add_item(&trans->r_itemq);
	}
	item = trans->r_itemq;
	item = item->ri_prev;

	if (item->ri_total == 0) {		/* first region to be added */
		if (in_f->ilf_size == 0 ||
		    in_f->ilf_size > XLOG_MAX_REGIONS_IN_ITEM) {
			xlog_warn(
	"XFS: bad number of regions (%d) in inode log format",
				  in_f->ilf_size);
			ASSERT(0);
			return XFS_ERROR(EIO);
		}

		item->ri_total = in_f->ilf_size;
		item->ri_buf =
			kmem_zalloc(item->ri_total * sizeof(xfs_log_iovec_t),
				    KM_SLEEP);
	}
	ASSERT(item->ri_total > item->ri_cnt);
	/* Description region is ri_buf[0] */
	item->ri_buf[item->ri_cnt].i_addr = ptr;
	item->ri_buf[item->ri_cnt].i_len  = len;
	item->ri_cnt++;
	return 0;
}

STATIC void
xlog_recover_new_tid(
	xlog_recover_t		**q,
	xlog_tid_t		tid,
	xfs_lsn_t		lsn)
{
	xlog_recover_t		*trans;

	trans = kmem_zalloc(sizeof(xlog_recover_t), KM_SLEEP);
	trans->r_log_tid   = tid;
	trans->r_lsn	   = lsn;
	xlog_recover_put_hashq(q, trans);
}

STATIC int
xlog_recover_unlink_tid(
	xlog_recover_t		**q,
	xlog_recover_t		*trans)
{
	xlog_recover_t		*tp;
	int			found = 0;

	ASSERT(trans != NULL);
	if (trans == *q) {
		*q = (*q)->r_next;
	} else {
		tp = *q;
		while (tp) {
			if (tp->r_next == trans) {
				found = 1;
				break;
			}
			tp = tp->r_next;
		}
		if (!found) {
			xlog_warn(
			     "XFS: xlog_recover_unlink_tid: trans not found");
			ASSERT(0);
			return XFS_ERROR(EIO);
		}
		tp->r_next = tp->r_next->r_next;
	}
	return 0;
}

STATIC void
xlog_recover_insert_item_backq(
	xlog_recover_item_t	**q,
	xlog_recover_item_t	*item)
{
	if (*q == NULL) {
		item->ri_prev = item->ri_next = item;
		*q = item;
	} else {
		item->ri_next		= *q;
		item->ri_prev		= (*q)->ri_prev;
		(*q)->ri_prev		= item;
		item->ri_prev->ri_next	= item;
	}
}

STATIC void
xlog_recover_insert_item_frontq(
	xlog_recover_item_t	**q,
	xlog_recover_item_t	*item)
{
	xlog_recover_insert_item_backq(q, item);
	*q = item;
}

STATIC int
xlog_recover_reorder_trans(
	xlog_recover_t		*trans)
{
	xlog_recover_item_t	*first_item, *itemq, *itemq_next;
	xfs_buf_log_format_t	*buf_f;
	ushort			flags = 0;

	first_item = itemq = trans->r_itemq;
	trans->r_itemq = NULL;
	do {
		itemq_next = itemq->ri_next;
		buf_f = (xfs_buf_log_format_t *)itemq->ri_buf[0].i_addr;

		switch (ITEM_TYPE(itemq)) {
		case XFS_LI_BUF:
			flags = buf_f->blf_flags;
			if (!(flags & XFS_BLI_CANCEL)) {
				xlog_recover_insert_item_frontq(&trans->r_itemq,
								itemq);
				break;
			}
		case XFS_LI_INODE:
		case XFS_LI_DQUOT:
		case XFS_LI_QUOTAOFF:
		case XFS_LI_EFD:
		case XFS_LI_EFI:
			xlog_recover_insert_item_backq(&trans->r_itemq, itemq);
			break;
		default:
			xlog_warn(
	"XFS: xlog_recover_reorder_trans: unrecognized type of log operation");
			ASSERT(0);
			return XFS_ERROR(EIO);
		}
		itemq = itemq_next;
	} while (first_item != itemq);
	return 0;
}

/*
 * Build up the table of buf cancel records so that we don't replay
 * cancelled data in the second pass.  For buffer records that are
 * not cancel records, there is nothing to do here so we just return.
 *
 * If we get a cancel record which is already in the table, this indicates
 * that the buffer was cancelled multiple times.  In order to ensure
 * that during pass 2 we keep the record in the table until we reach its
 * last occurrence in the log, we keep a reference count in the cancel
 * record in the table to tell us how many times we expect to see this
 * record during the second pass.
 */
STATIC void
xlog_recover_do_buffer_pass1(
	xlog_t			*log,
	xfs_buf_log_format_t	*buf_f)
{
	xfs_buf_cancel_t	*bcp;
	xfs_buf_cancel_t	*nextp;
	xfs_buf_cancel_t	*prevp;
	xfs_buf_cancel_t	**bucket;
	xfs_daddr_t		blkno = 0;
	uint			len = 0;
	ushort			flags = 0;

	switch (buf_f->blf_type) {
	case XFS_LI_BUF:
		blkno = buf_f->blf_blkno;
		len = buf_f->blf_len;
		flags = buf_f->blf_flags;
		break;
	}

	/*
	 * If this isn't a cancel buffer item, then just return.
	 */
	if (!(flags & XFS_BLI_CANCEL))
		return;

	/*
	 * Insert an xfs_buf_cancel record into the hash table of
	 * them.  If there is already an identical record, bump
	 * its reference count.
	 */
	bucket = &log->l_buf_cancel_table[(__uint64_t)blkno %
					  XLOG_BC_TABLE_SIZE];
	/*
	 * If the hash bucket is empty then just insert a new record into
	 * the bucket.
	 */
	if (*bucket == NULL) {
		bcp = (xfs_buf_cancel_t *)kmem_alloc(sizeof(xfs_buf_cancel_t),
						     KM_SLEEP);
		bcp->bc_blkno = blkno;
		bcp->bc_len = len;
		bcp->bc_refcount = 1;
		bcp->bc_next = NULL;
		*bucket = bcp;
		return;
	}

	/*
	 * The hash bucket is not empty, so search for duplicates of our
	 * record.  If we find one them just bump its refcount.  If not
	 * then add us at the end of the list.
	 */
	prevp = NULL;
	nextp = *bucket;
	while (nextp != NULL) {
		if (nextp->bc_blkno == blkno && nextp->bc_len == len) {
			nextp->bc_refcount++;
			return;
		}
		prevp = nextp;
		nextp = nextp->bc_next;
	}
	ASSERT(prevp != NULL);
	bcp = (xfs_buf_cancel_t *)kmem_alloc(sizeof(xfs_buf_cancel_t),
					     KM_SLEEP);
	bcp->bc_blkno = blkno;
	bcp->bc_len = len;
	bcp->bc_refcount = 1;
	bcp->bc_next = NULL;
	prevp->bc_next = bcp;
}

/*
 * Check to see whether the buffer being recovered has a corresponding
 * entry in the buffer cancel record table.  If it does then return 1
 * so that it will be cancelled, otherwise return 0.  If the buffer is
 * actually a buffer cancel item (XFS_BLI_CANCEL is set), then decrement
 * the refcount on the entry in the table and remove it from the table
 * if this is the last reference.
 *
 * We remove the cancel record from the table when we encounter its
 * last occurrence in the log so that if the same buffer is re-used
 * again after its last cancellation we actually replay the changes
 * made at that point.
 */
STATIC int
xlog_check_buffer_cancelled(
	xlog_t			*log,
	xfs_daddr_t		blkno,
	uint			len,
	ushort			flags)
{
	xfs_buf_cancel_t	*bcp;
	xfs_buf_cancel_t	*prevp;
	xfs_buf_cancel_t	**bucket;

	if (log->l_buf_cancel_table == NULL) {
		/*
		 * There is nothing in the table built in pass one,
		 * so this buffer must not be cancelled.
		 */
		ASSERT(!(flags & XFS_BLI_CANCEL));
		return 0;
	}

	bucket = &log->l_buf_cancel_table[(__uint64_t)blkno %
					  XLOG_BC_TABLE_SIZE];
	bcp = *bucket;
	if (bcp == NULL) {
		/*
		 * There is no corresponding entry in the table built
		 * in pass one, so this buffer has not been cancelled.
		 */
		ASSERT(!(flags & XFS_BLI_CANCEL));
		return 0;
	}

	/*
	 * Search for an entry in the buffer cancel table that
	 * matches our buffer.
	 */
	prevp = NULL;
	while (bcp != NULL) {
		if (bcp->bc_blkno == blkno && bcp->bc_len == len) {
			/*
			 * We've go a match, so return 1 so that the
			 * recovery of this buffer is cancelled.
			 * If this buffer is actually a buffer cancel
			 * log item, then decrement the refcount on the
			 * one in the table and remove it if this is the
			 * last reference.
			 */
			if (flags & XFS_BLI_CANCEL) {
				bcp->bc_refcount--;
				if (bcp->bc_refcount == 0) {
					if (prevp == NULL) {
						*bucket = bcp->bc_next;
					} else {
						prevp->bc_next = bcp->bc_next;
					}
					kmem_free(bcp);
				}
			}
			return 1;
		}
		prevp = bcp;
		bcp = bcp->bc_next;
	}
	/*
	 * We didn't find a corresponding entry in the table, so
	 * return 0 so that the buffer is NOT cancelled.
	 */
	ASSERT(!(flags & XFS_BLI_CANCEL));
	return 0;
}

STATIC int
xlog_recover_do_buffer_pass2(
	xlog_t			*log,
	xfs_buf_log_format_t	*buf_f)
{
	xfs_daddr_t		blkno = 0;
	ushort			flags = 0;
	uint			len = 0;

	switch (buf_f->blf_type) {
	case XFS_LI_BUF:
		blkno = buf_f->blf_blkno;
		flags = buf_f->blf_flags;
		len = buf_f->blf_len;
		break;
	}

	return xlog_check_buffer_cancelled(log, blkno, len, flags);
}

/*
 * Perform recovery for a buffer full of inodes.  In these buffers,
 * the only data which should be recovered is that which corresponds
 * to the di_next_unlinked pointers in the on disk inode structures.
 * The rest of the data for the inodes is always logged through the
 * inodes themselves rather than the inode buffer and is recovered
 * in xlog_recover_do_inode_trans().
 *
 * The only time when buffers full of inodes are fully recovered is
 * when the buffer is full of newly allocated inodes.  In this case
 * the buffer will not be marked as an inode buffer and so will be
 * sent to xlog_recover_do_reg_buffer() below during recovery.
 */
STATIC int
xlog_recover_do_inode_buffer(
	xfs_mount_t		*mp,
	xlog_recover_item_t	*item,
	xfs_buf_t		*bp,
	xfs_buf_log_format_t	*buf_f)
{
	int			i;
	int			item_index;
	int			bit;
	int			nbits;
	int			reg_buf_offset;
	int			reg_buf_bytes;
	int			next_unlinked_offset;
	int			inodes_per_buf;
	xfs_agino_t		*logged_nextp;
	xfs_agino_t		*buffer_nextp;
	unsigned int		*data_map = NULL;
	unsigned int		map_size = 0;

	switch (buf_f->blf_type) {
	case XFS_LI_BUF:
		data_map = buf_f->blf_data_map;
		map_size = buf_f->blf_map_size;
		break;
	}
	/*
	 * Set the variables corresponding to the current region to
	 * 0 so that we'll initialize them on the first pass through
	 * the loop.
	 */
	reg_buf_offset = 0;
	reg_buf_bytes = 0;
	bit = 0;
	nbits = 0;
	item_index = 0;
	inodes_per_buf = XFS_BUF_COUNT(bp) >> mp->m_sb.sb_inodelog;
	for (i = 0; i < inodes_per_buf; i++) {
		next_unlinked_offset = (i * mp->m_sb.sb_inodesize) +
			offsetof(xfs_dinode_t, di_next_unlinked);

		while (next_unlinked_offset >=
		       (reg_buf_offset + reg_buf_bytes)) {
			/*
			 * The next di_next_unlinked field is beyond
			 * the current logged region.  Find the next
			 * logged region that contains or is beyond
			 * the current di_next_unlinked field.
			 */
			bit += nbits;
			bit = xfs_next_bit(data_map, map_size, bit);

			/*
			 * If there are no more logged regions in the
			 * buffer, then we're done.
			 */
			if (bit == -1) {
				return 0;
			}

			nbits = xfs_contig_bits(data_map, map_size,
							 bit);
			ASSERT(nbits > 0);
			reg_buf_offset = bit << XFS_BLI_SHIFT;
			reg_buf_bytes = nbits << XFS_BLI_SHIFT;
			item_index++;
		}

		/*
		 * If the current logged region starts after the current
		 * di_next_unlinked field, then move on to the next
		 * di_next_unlinked field.
		 */
		if (next_unlinked_offset < reg_buf_offset) {
			continue;
		}

		ASSERT(item->ri_buf[item_index].i_addr != NULL);
		ASSERT((item->ri_buf[item_index].i_len % XFS_BLI_CHUNK) == 0);
		ASSERT((reg_buf_offset + reg_buf_bytes) <= XFS_BUF_COUNT(bp));

		/*
		 * The current logged region contains a copy of the
		 * current di_next_unlinked field.  Extract its value
		 * and copy it to the buffer copy.
		 */
		logged_nextp = (xfs_agino_t *)
			       ((char *)(item->ri_buf[item_index].i_addr) +
				(next_unlinked_offset - reg_buf_offset));
		if (unlikely(*logged_nextp == 0)) {
			xfs_fs_cmn_err(CE_ALERT, mp,
				"bad inode buffer log record (ptr = 0x%p, bp = 0x%p).  XFS trying to replay bad (0) inode di_next_unlinked field",
				item, bp);
			XFS_ERROR_REPORT("xlog_recover_do_inode_buf",
					 XFS_ERRLEVEL_LOW, mp);
			return XFS_ERROR(EFSCORRUPTED);
		}

		buffer_nextp = (xfs_agino_t *)xfs_buf_offset(bp,
					      next_unlinked_offset);
		*buffer_nextp = *logged_nextp;
	}

	return 0;
}

/*
 * Perform a 'normal' buffer recovery.  Each logged region of the
 * buffer should be copied over the corresponding region in the
 * given buffer.  The bitmap in the buf log format structure indicates
 * where to place the logged data.
 */
/*ARGSUSED*/
STATIC void
xlog_recover_do_reg_buffer(
	xlog_recover_item_t	*item,
	xfs_buf_t		*bp,
	xfs_buf_log_format_t	*buf_f)
{
	int			i;
	int			bit;
	int			nbits;
	unsigned int		*data_map = NULL;
	unsigned int		map_size = 0;
	int                     error;

	switch (buf_f->blf_type) {
	case XFS_LI_BUF:
		data_map = buf_f->blf_data_map;
		map_size = buf_f->blf_map_size;
		break;
	}
	bit = 0;
	i = 1;  /* 0 is the buf format structure */
	while (1) {
		bit = xfs_next_bit(data_map, map_size, bit);
		if (bit == -1)
			break;
		nbits = xfs_contig_bits(data_map, map_size, bit);
		ASSERT(nbits > 0);
		ASSERT(item->ri_buf[i].i_addr != NULL);
		ASSERT(item->ri_buf[i].i_len % XFS_BLI_CHUNK == 0);
		ASSERT(XFS_BUF_COUNT(bp) >=
		       ((uint)bit << XFS_BLI_SHIFT)+(nbits<<XFS_BLI_SHIFT));

		/*
		 * Do a sanity check if this is a dquot buffer. Just checking
		 * the first dquot in the buffer should do. XXXThis is
		 * probably a good thing to do for other buf types also.
		 */
		error = 0;
		if (buf_f->blf_flags &
		   (XFS_BLI_UDQUOT_BUF|XFS_BLI_PDQUOT_BUF|XFS_BLI_GDQUOT_BUF)) {
			if (item->ri_buf[i].i_addr == NULL) {
				cmn_err(CE_ALERT,
					"XFS: NULL dquot in %s.", __func__);
				goto next;
			}
			if (item->ri_buf[i].i_len < sizeof(xfs_dqblk_t)) {
				cmn_err(CE_ALERT,
					"XFS: dquot too small (%d) in %s.",
					item->ri_buf[i].i_len, __func__);
				goto next;
			}
			error = xfs_qm_dqcheck((xfs_disk_dquot_t *)
					       item->ri_buf[i].i_addr,
					       -1, 0, XFS_QMOPT_DOWARN,
					       "dquot_buf_recover");
			if (error)
				goto next;
		}

		memcpy(xfs_buf_offset(bp,
			(uint)bit << XFS_BLI_SHIFT),	/* dest */
			item->ri_buf[i].i_addr,		/* source */
			nbits<<XFS_BLI_SHIFT);		/* length */
 next:
		i++;
		bit += nbits;
	}

	/* Shouldn't be any more regions */
	ASSERT(i == item->ri_total);
}

/*
 * Do some primitive error checking on ondisk dquot data structures.
 */
int
xfs_qm_dqcheck(
	xfs_disk_dquot_t *ddq,
	xfs_dqid_t	 id,
	uint		 type,	  /* used only when IO_dorepair is true */
	uint		 flags,
	char		 *str)
{
	xfs_dqblk_t	 *d = (xfs_dqblk_t *)ddq;
	int		errs = 0;

	/*
	 * We can encounter an uninitialized dquot buffer for 2 reasons:
	 * 1. If we crash while deleting the quotainode(s), and those blks got
	 *    used for user data. This is because we take the path of regular
	 *    file deletion; however, the size field of quotainodes is never
	 *    updated, so all the tricks that we play in itruncate_finish
	 *    don't quite matter.
	 *
	 * 2. We don't play the quota buffers when there's a quotaoff logitem.
	 *    But the allocation will be replayed so we'll end up with an
	 *    uninitialized quota block.
	 *
	 * This is all fine; things are still consistent, and we haven't lost
	 * any quota information. Just don't complain about bad dquot blks.
	 */
	if (be16_to_cpu(ddq->d_magic) != XFS_DQUOT_MAGIC) {
		if (flags & XFS_QMOPT_DOWARN)
			cmn_err(CE_ALERT,
			"%s : XFS dquot ID 0x%x, magic 0x%x != 0x%x",
			str, id, be16_to_cpu(ddq->d_magic), XFS_DQUOT_MAGIC);
		errs++;
	}
	if (ddq->d_version != XFS_DQUOT_VERSION) {
		if (flags & XFS_QMOPT_DOWARN)
			cmn_err(CE_ALERT,
			"%s : XFS dquot ID 0x%x, version 0x%x != 0x%x",
			str, id, ddq->d_version, XFS_DQUOT_VERSION);
		errs++;
	}

	if (ddq->d_flags != XFS_DQ_USER &&
	    ddq->d_flags != XFS_DQ_PROJ &&
	    ddq->d_flags != XFS_DQ_GROUP) {
		if (flags & XFS_QMOPT_DOWARN)
			cmn_err(CE_ALERT,
			"%s : XFS dquot ID 0x%x, unknown flags 0x%x",
			str, id, ddq->d_flags);
		errs++;
	}

	if (id != -1 && id != be32_to_cpu(ddq->d_id)) {
		if (flags & XFS_QMOPT_DOWARN)
			cmn_err(CE_ALERT,
			"%s : ondisk-dquot 0x%p, ID mismatch: "
			"0x%x expected, found id 0x%x",
			str, ddq, id, be32_to_cpu(ddq->d_id));
		errs++;
	}

	if (!errs && ddq->d_id) {
		if (ddq->d_blk_softlimit &&
		    be64_to_cpu(ddq->d_bcount) >=
				be64_to_cpu(ddq->d_blk_softlimit)) {
			if (!ddq->d_btimer) {
				if (flags & XFS_QMOPT_DOWARN)
					cmn_err(CE_ALERT,
					"%s : Dquot ID 0x%x (0x%p) "
					"BLK TIMER NOT STARTED",
					str, (int)be32_to_cpu(ddq->d_id), ddq);
				errs++;
			}
		}
		if (ddq->d_ino_softlimit &&
		    be64_to_cpu(ddq->d_icount) >=
				be64_to_cpu(ddq->d_ino_softlimit)) {
			if (!ddq->d_itimer) {
				if (flags & XFS_QMOPT_DOWARN)
					cmn_err(CE_ALERT,
					"%s : Dquot ID 0x%x (0x%p) "
					"INODE TIMER NOT STARTED",
					str, (int)be32_to_cpu(ddq->d_id), ddq);
				errs++;
			}
		}
		if (ddq->d_rtb_softlimit &&
		    be64_to_cpu(ddq->d_rtbcount) >=
				be64_to_cpu(ddq->d_rtb_softlimit)) {
			if (!ddq->d_rtbtimer) {
				if (flags & XFS_QMOPT_DOWARN)
					cmn_err(CE_ALERT,
					"%s : Dquot ID 0x%x (0x%p) "
					"RTBLK TIMER NOT STARTED",
					str, (int)be32_to_cpu(ddq->d_id), ddq);
				errs++;
			}
		}
	}

	if (!errs || !(flags & XFS_QMOPT_DQREPAIR))
		return errs;

	if (flags & XFS_QMOPT_DOWARN)
		cmn_err(CE_NOTE, "Re-initializing dquot ID 0x%x", id);

	/*
	 * Typically, a repair is only requested by quotacheck.
	 */
	ASSERT(id != -1);
	ASSERT(flags & XFS_QMOPT_DQREPAIR);
	memset(d, 0, sizeof(xfs_dqblk_t));

	d->dd_diskdq.d_magic = cpu_to_be16(XFS_DQUOT_MAGIC);
	d->dd_diskdq.d_version = XFS_DQUOT_VERSION;
	d->dd_diskdq.d_flags = type;
	d->dd_diskdq.d_id = cpu_to_be32(id);

	return errs;
}

/*
 * Perform a dquot buffer recovery.
 * Simple algorithm: if we have found a QUOTAOFF logitem of the same type
 * (ie. USR or GRP), then just toss this buffer away; don't recover it.
 * Else, treat it as a regular buffer and do recovery.
 */
STATIC void
xlog_recover_do_dquot_buffer(
	xfs_mount_t		*mp,
	xlog_t			*log,
	xlog_recover_item_t	*item,
	xfs_buf_t		*bp,
	xfs_buf_log_format_t	*buf_f)
{
	uint			type;

	/*
	 * Filesystems are required to send in quota flags at mount time.
	 */
	if (mp->m_qflags == 0) {
		return;
	}

	type = 0;
	if (buf_f->blf_flags & XFS_BLI_UDQUOT_BUF)
		type |= XFS_DQ_USER;
	if (buf_f->blf_flags & XFS_BLI_PDQUOT_BUF)
		type |= XFS_DQ_PROJ;
	if (buf_f->blf_flags & XFS_BLI_GDQUOT_BUF)
		type |= XFS_DQ_GROUP;
	/*
	 * This type of quotas was turned off, so ignore this buffer
	 */
	if (log->l_quotaoffs_flag & type)
		return;

	xlog_recover_do_reg_buffer(item, bp, buf_f);
}

/*
 * This routine replays a modification made to a buffer at runtime.
 * There are actually two types of buffer, regular and inode, which
 * are handled differently.  Inode buffers are handled differently
 * in that we only recover a specific set of data from them, namely
 * the inode di_next_unlinked fields.  This is because all other inode
 * data is actually logged via inode records and any data we replay
 * here which overlaps that may be stale.
 *
 * When meta-data buffers are freed at run time we log a buffer item
 * with the XFS_BLI_CANCEL bit set to indicate that previous copies
 * of the buffer in the log should not be replayed at recovery time.
 * This is so that if the blocks covered by the buffer are reused for
 * file data before we crash we don't end up replaying old, freed
 * meta-data into a user's file.
 *
 * To handle the cancellation of buffer log items, we make two passes
 * over the log during recovery.  During the first we build a table of
 * those buffers which have been cancelled, and during the second we
 * only replay those buffers which do not have corresponding cancel
 * records in the table.  See xlog_recover_do_buffer_pass[1,2] above
 * for more details on the implementation of the table of cancel records.
 */
STATIC int
xlog_recover_do_buffer_trans(
	xlog_t			*log,
	xlog_recover_item_t	*item,
	int			pass)
{
	xfs_buf_log_format_t	*buf_f;
	xfs_mount_t		*mp;
	xfs_buf_t		*bp;
	int			error;
	int			cancel;
	xfs_daddr_t		blkno;
	int			len;
	ushort			flags;

	buf_f = (xfs_buf_log_format_t *)item->ri_buf[0].i_addr;

	if (pass == XLOG_RECOVER_PASS1) {
		/*
		 * In this pass we're only looking for buf items
		 * with the XFS_BLI_CANCEL bit set.
		 */
		xlog_recover_do_buffer_pass1(log, buf_f);
		return 0;
	} else {
		/*
		 * In this pass we want to recover all the buffers
		 * which have not been cancelled and are not
		 * cancellation buffers themselves.  The routine
		 * we call here will tell us whether or not to
		 * continue with the replay of this buffer.
		 */
		cancel = xlog_recover_do_buffer_pass2(log, buf_f);
		if (cancel) {
			return 0;
		}
	}
	switch (buf_f->blf_type) {
	case XFS_LI_BUF:
		blkno = buf_f->blf_blkno;
		len = buf_f->blf_len;
		flags = buf_f->blf_flags;
		break;
	default:
		xfs_fs_cmn_err(CE_ALERT, log->l_mp,
			"xfs_log_recover: unknown buffer type 0x%x, logdev %s",
			buf_f->blf_type, log->l_mp->m_logname ?
			log->l_mp->m_logname : "internal");
		XFS_ERROR_REPORT("xlog_recover_do_buffer_trans",
				 XFS_ERRLEVEL_LOW, log->l_mp);
		return XFS_ERROR(EFSCORRUPTED);
	}

	mp = log->l_mp;
	if (flags & XFS_BLI_INODE_BUF) {
		bp = xfs_buf_read_flags(mp->m_ddev_targp, blkno, len,
								XFS_BUF_LOCK);
	} else {
		bp = xfs_buf_read(mp->m_ddev_targp, blkno, len, 0);
	}
	if (XFS_BUF_ISERROR(bp)) {
		xfs_ioerror_alert("xlog_recover_do..(read#1)", log->l_mp,
				  bp, blkno);
		error = XFS_BUF_GETERROR(bp);
		xfs_buf_relse(bp);
		return error;
	}

	error = 0;
	if (flags & XFS_BLI_INODE_BUF) {
		error = xlog_recover_do_inode_buffer(mp, item, bp, buf_f);
	} else if (flags &
		  (XFS_BLI_UDQUOT_BUF|XFS_BLI_PDQUOT_BUF|XFS_BLI_GDQUOT_BUF)) {
		xlog_recover_do_dquot_buffer(mp, log, item, bp, buf_f);
	} else {
		xlog_recover_do_reg_buffer(item, bp, buf_f);
	}
	if (error)
		return XFS_ERROR(error);

	/*
	 * Perform delayed write on the buffer.  Asynchronous writes will be
	 * slower when taking into account all the buffers to be flushed.
	 *
	 * Also make sure that only inode buffers with good sizes stay in
	 * the buffer cache.  The kernel moves inodes in buffers of 1 block
	 * or XFS_INODE_CLUSTER_SIZE bytes, whichever is bigger.  The inode
	 * buffers in the log can be a different size if the log was generated
	 * by an older kernel using unclustered inode buffers or a newer kernel
	 * running with a different inode cluster size.  Regardless, if the
	 * the inode buffer size isn't MAX(blocksize, XFS_INODE_CLUSTER_SIZE)
	 * for *our* value of XFS_INODE_CLUSTER_SIZE, then we need to keep
	 * the buffer out of the buffer cache so that the buffer won't
	 * overlap with future reads of those inodes.
	 */
	if (XFS_DINODE_MAGIC ==
	    be16_to_cpu(*((__be16 *)xfs_buf_offset(bp, 0))) &&
	    (XFS_BUF_COUNT(bp) != MAX(log->l_mp->m_sb.sb_blocksize,
			(__uint32_t)XFS_INODE_CLUSTER_SIZE(log->l_mp)))) {
		XFS_BUF_STALE(bp);
		error = xfs_bwrite(mp, bp);
	} else {
		ASSERT(bp->b_mount == NULL || bp->b_mount == mp);
		bp->b_mount = mp;
		XFS_BUF_SET_IODONE_FUNC(bp, xlog_recover_iodone);
		xfs_bdwrite(mp, bp);
	}

	return (error);
}

STATIC int
xlog_recover_do_inode_trans(
	xlog_t			*log,
	xlog_recover_item_t	*item,
	int			pass)
{
	xfs_inode_log_format_t	*in_f;
	xfs_mount_t		*mp;
	xfs_buf_t		*bp;
	xfs_dinode_t		*dip;
	xfs_ino_t		ino;
	int			len;
	xfs_caddr_t		src;
	xfs_caddr_t		dest;
	int			error;
	int			attr_index;
	uint			fields;
	xfs_icdinode_t		*dicp;
	int			need_free = 0;

	if (pass == XLOG_RECOVER_PASS1) {
		return 0;
	}

	if (item->ri_buf[0].i_len == sizeof(xfs_inode_log_format_t)) {
		in_f = (xfs_inode_log_format_t *)item->ri_buf[0].i_addr;
	} else {
		in_f = (xfs_inode_log_format_t *)kmem_alloc(
			sizeof(xfs_inode_log_format_t), KM_SLEEP);
		need_free = 1;
		error = xfs_inode_item_format_convert(&item->ri_buf[0], in_f);
		if (error)
			goto error;
	}
	ino = in_f->ilf_ino;
	mp = log->l_mp;

	/*
	 * Inode buffers can be freed, look out for it,
	 * and do not replay the inode.
	 */
	if (xlog_check_buffer_cancelled(log, in_f->ilf_blkno,
					in_f->ilf_len, 0)) {
		error = 0;
		goto error;
	}

	bp = xfs_buf_read_flags(mp->m_ddev_targp, in_f->ilf_blkno,
				in_f->ilf_len, XFS_BUF_LOCK);
	if (XFS_BUF_ISERROR(bp)) {
		xfs_ioerror_alert("xlog_recover_do..(read#2)", mp,
				  bp, in_f->ilf_blkno);
		error = XFS_BUF_GETERROR(bp);
		xfs_buf_relse(bp);
		goto error;
	}
	error = 0;
	ASSERT(in_f->ilf_fields & XFS_ILOG_CORE);
	dip = (xfs_dinode_t *)xfs_buf_offset(bp, in_f->ilf_boffset);

	/*
	 * Make sure the place we're flushing out to really looks
	 * like an inode!
	 */
	if (unlikely(be16_to_cpu(dip->di_magic) != XFS_DINODE_MAGIC)) {
		xfs_buf_relse(bp);
		xfs_fs_cmn_err(CE_ALERT, mp,
			"xfs_inode_recover: Bad inode magic number, dino ptr = 0x%p, dino bp = 0x%p, ino = %Ld",
			dip, bp, ino);
		XFS_ERROR_REPORT("xlog_recover_do_inode_trans(1)",
				 XFS_ERRLEVEL_LOW, mp);
		error = EFSCORRUPTED;
		goto error;
	}
	dicp = (xfs_icdinode_t *)(item->ri_buf[1].i_addr);
	if (unlikely(dicp->di_magic != XFS_DINODE_MAGIC)) {
		xfs_buf_relse(bp);
		xfs_fs_cmn_err(CE_ALERT, mp,
			"xfs_inode_recover: Bad inode log record, rec ptr 0x%p, ino %Ld",
			item, ino);
		XFS_ERROR_REPORT("xlog_recover_do_inode_trans(2)",
				 XFS_ERRLEVEL_LOW, mp);
		error = EFSCORRUPTED;
		goto error;
	}

	/* Skip replay when the on disk inode is newer than the log one */
	if (dicp->di_flushiter < be16_to_cpu(dip->di_flushiter)) {
		/*
		 * Deal with the wrap case, DI_MAX_FLUSH is less
		 * than smaller numbers
		 */
		if (be16_to_cpu(dip->di_flushiter) == DI_MAX_FLUSH &&
		    dicp->di_flushiter < (DI_MAX_FLUSH >> 1)) {
			/* do nothing */
		} else {
			xfs_buf_relse(bp);
			error = 0;
			goto error;
		}
	}
	/* Take the opportunity to reset the flush iteration count */
	dicp->di_flushiter = 0;

	if (unlikely((dicp->di_mode & S_IFMT) == S_IFREG)) {
		if ((dicp->di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (dicp->di_format != XFS_DINODE_FMT_BTREE)) {
			XFS_CORRUPTION_ERROR("xlog_recover_do_inode_trans(3)",
					 XFS_ERRLEVEL_LOW, mp, dicp);
			xfs_buf_relse(bp);
			xfs_fs_cmn_err(CE_ALERT, mp,
				"xfs_inode_recover: Bad regular inode log record, rec ptr 0x%p, ino ptr = 0x%p, ino bp = 0x%p, ino %Ld",
				item, dip, bp, ino);
			error = EFSCORRUPTED;
			goto error;
		}
	} else if (unlikely((dicp->di_mode & S_IFMT) == S_IFDIR)) {
		if ((dicp->di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (dicp->di_format != XFS_DINODE_FMT_BTREE) &&
		    (dicp->di_format != XFS_DINODE_FMT_LOCAL)) {
			XFS_CORRUPTION_ERROR("xlog_recover_do_inode_trans(4)",
					     XFS_ERRLEVEL_LOW, mp, dicp);
			xfs_buf_relse(bp);
			xfs_fs_cmn_err(CE_ALERT, mp,
				"xfs_inode_recover: Bad dir inode log record, rec ptr 0x%p, ino ptr = 0x%p, ino bp = 0x%p, ino %Ld",
				item, dip, bp, ino);
			error = EFSCORRUPTED;
			goto error;
		}
	}
	if (unlikely(dicp->di_nextents + dicp->di_anextents > dicp->di_nblocks)){
		XFS_CORRUPTION_ERROR("xlog_recover_do_inode_trans(5)",
				     XFS_ERRLEVEL_LOW, mp, dicp);
		xfs_buf_relse(bp);
		xfs_fs_cmn_err(CE_ALERT, mp,
			"xfs_inode_recover: Bad inode log record, rec ptr 0x%p, dino ptr 0x%p, dino bp 0x%p, ino %Ld, total extents = %d, nblocks = %Ld",
			item, dip, bp, ino,
			dicp->di_nextents + dicp->di_anextents,
			dicp->di_nblocks);
		error = EFSCORRUPTED;
		goto error;
	}
	if (unlikely(dicp->di_forkoff > mp->m_sb.sb_inodesize)) {
		XFS_CORRUPTION_ERROR("xlog_recover_do_inode_trans(6)",
				     XFS_ERRLEVEL_LOW, mp, dicp);
		xfs_buf_relse(bp);
		xfs_fs_cmn_err(CE_ALERT, mp,
			"xfs_inode_recover: Bad inode log rec ptr 0x%p, dino ptr 0x%p, dino bp 0x%p, ino %Ld, forkoff 0x%x",
			item, dip, bp, ino, dicp->di_forkoff);
		error = EFSCORRUPTED;
		goto error;
	}
	if (unlikely(item->ri_buf[1].i_len > sizeof(struct xfs_icdinode))) {
		XFS_CORRUPTION_ERROR("xlog_recover_do_inode_trans(7)",
				     XFS_ERRLEVEL_LOW, mp, dicp);
		xfs_buf_relse(bp);
		xfs_fs_cmn_err(CE_ALERT, mp,
			"xfs_inode_recover: Bad inode log record length %d, rec ptr 0x%p",
			item->ri_buf[1].i_len, item);
		error = EFSCORRUPTED;
		goto error;
	}

	/* The core is in in-core format */
	xfs_dinode_to_disk(dip, (xfs_icdinode_t *)item->ri_buf[1].i_addr);

	/* the rest is in on-disk format */
	if (item->ri_buf[1].i_len > sizeof(struct xfs_icdinode)) {
		memcpy((xfs_caddr_t) dip + sizeof(struct xfs_icdinode),
			item->ri_buf[1].i_addr + sizeof(struct xfs_icdinode),
			item->ri_buf[1].i_len  - sizeof(struct xfs_icdinode));
	}

	fields = in_f->ilf_fields;
	switch (fields & (XFS_ILOG_DEV | XFS_ILOG_UUID)) {
	case XFS_ILOG_DEV:
		xfs_dinode_put_rdev(dip, in_f->ilf_u.ilfu_rdev);
		break;
	case XFS_ILOG_UUID:
		memcpy(XFS_DFORK_DPTR(dip),
		       &in_f->ilf_u.ilfu_uuid,
		       sizeof(uuid_t));
		break;
	}

	if (in_f->ilf_size == 2)
		goto write_inode_buffer;
	len = item->ri_buf[2].i_len;
	src = item->ri_buf[2].i_addr;
	ASSERT(in_f->ilf_size <= 4);
	ASSERT((in_f->ilf_size == 3) || (fields & XFS_ILOG_AFORK));
	ASSERT(!(fields & XFS_ILOG_DFORK) ||
	       (len == in_f->ilf_dsize));

	switch (fields & XFS_ILOG_DFORK) {
	case XFS_ILOG_DDATA:
	case XFS_ILOG_DEXT:
		memcpy(XFS_DFORK_DPTR(dip), src, len);
		break;

	case XFS_ILOG_DBROOT:
		xfs_bmbt_to_bmdr(mp, (struct xfs_btree_block *)src, len,
				 (xfs_bmdr_block_t *)XFS_DFORK_DPTR(dip),
				 XFS_DFORK_DSIZE(dip, mp));
		break;

	default:
		/*
		 * There are no data fork flags set.
		 */
		ASSERT((fields & XFS_ILOG_DFORK) == 0);
		break;
	}

	/*
	 * If we logged any attribute data, recover it.  There may or
	 * may not have been any other non-core data logged in this
	 * transaction.
	 */
	if (in_f->ilf_fields & XFS_ILOG_AFORK) {
		if (in_f->ilf_fields & XFS_ILOG_DFORK) {
			attr_index = 3;
		} else {
			attr_index = 2;
		}
		len = item->ri_buf[attr_index].i_len;
		src = item->ri_buf[attr_index].i_addr;
		ASSERT(len == in_f->ilf_asize);

		switch (in_f->ilf_fields & XFS_ILOG_AFORK) {
		case XFS_ILOG_ADATA:
		case XFS_ILOG_AEXT:
			dest = XFS_DFORK_APTR(dip);
			ASSERT(len <= XFS_DFORK_ASIZE(dip, mp));
			memcpy(dest, src, len);
			break;

		case XFS_ILOG_ABROOT:
			dest = XFS_DFORK_APTR(dip);
			xfs_bmbt_to_bmdr(mp, (struct xfs_btree_block *)src,
					 len, (xfs_bmdr_block_t*)dest,
					 XFS_DFORK_ASIZE(dip, mp));
			break;

		default:
			xlog_warn("XFS: xlog_recover_do_inode_trans: Invalid flag");
			ASSERT(0);
			xfs_buf_relse(bp);
			error = EIO;
			goto error;
		}
	}

write_inode_buffer:
	ASSERT(bp->b_mount == NULL || bp->b_mount == mp);
	bp->b_mount = mp;
	XFS_BUF_SET_IODONE_FUNC(bp, xlog_recover_iodone);
	xfs_bdwrite(mp, bp);
error:
	if (need_free)
		kmem_free(in_f);
	return XFS_ERROR(error);
}

/*
 * Recover QUOTAOFF records. We simply make a note of it in the xlog_t
 * structure, so that we know not to do any dquot item or dquot buffer recovery,
 * of that type.
 */
STATIC int
xlog_recover_do_quotaoff_trans(
	xlog_t			*log,
	xlog_recover_item_t	*item,
	int			pass)
{
	xfs_qoff_logformat_t	*qoff_f;

	if (pass == XLOG_RECOVER_PASS2) {
		return (0);
	}

	qoff_f = (xfs_qoff_logformat_t *)item->ri_buf[0].i_addr;
	ASSERT(qoff_f);

	/*
	 * The logitem format's flag tells us if this was user quotaoff,
	 * group/project quotaoff or both.
	 */
	if (qoff_f->qf_flags & XFS_UQUOTA_ACCT)
		log->l_quotaoffs_flag |= XFS_DQ_USER;
	if (qoff_f->qf_flags & XFS_PQUOTA_ACCT)
		log->l_quotaoffs_flag |= XFS_DQ_PROJ;
	if (qoff_f->qf_flags & XFS_GQUOTA_ACCT)
		log->l_quotaoffs_flag |= XFS_DQ_GROUP;

	return (0);
}

/*
 * Recover a dquot record
 */
STATIC int
xlog_recover_do_dquot_trans(
	xlog_t			*log,
	xlog_recover_item_t	*item,
	int			pass)
{
	xfs_mount_t		*mp;
	xfs_buf_t		*bp;
	struct xfs_disk_dquot	*ddq, *recddq;
	int			error;
	xfs_dq_logformat_t	*dq_f;
	uint			type;

	if (pass == XLOG_RECOVER_PASS1) {
		return 0;
	}
	mp = log->l_mp;

	/*
	 * Filesystems are required to send in quota flags at mount time.
	 */
	if (mp->m_qflags == 0)
		return (0);

	recddq = (xfs_disk_dquot_t *)item->ri_buf[1].i_addr;

	if (item->ri_buf[1].i_addr == NULL) {
		cmn_err(CE_ALERT,
			"XFS: NULL dquot in %s.", __func__);
		return XFS_ERROR(EIO);
	}
	if (item->ri_buf[1].i_len < sizeof(xfs_dqblk_t)) {
		cmn_err(CE_ALERT,
			"XFS: dquot too small (%d) in %s.",
			item->ri_buf[1].i_len, __func__);
		return XFS_ERROR(EIO);
	}

	/*
	 * This type of quotas was turned off, so ignore this record.
	 */
	type = recddq->d_flags & (XFS_DQ_USER | XFS_DQ_PROJ | XFS_DQ_GROUP);
	ASSERT(type);
	if (log->l_quotaoffs_flag & type)
		return (0);

	/*
	 * At this point we know that quota was _not_ turned off.
	 * Since the mount flags are not indicating to us otherwise, this
	 * must mean that quota is on, and the dquot needs to be replayed.
	 * Remember that we may not have fully recovered the superblock yet,
	 * so we can't do the usual trick of looking at the SB quota bits.
	 *
	 * The other possibility, of course, is that the quota subsystem was
	 * removed since the last mount - ENOSYS.
	 */
	dq_f = (xfs_dq_logformat_t *)item->ri_buf[0].i_addr;
	ASSERT(dq_f);
	if ((error = xfs_qm_dqcheck(recddq,
			   dq_f->qlf_id,
			   0, XFS_QMOPT_DOWARN,
			   "xlog_recover_do_dquot_trans (log copy)"))) {
		return XFS_ERROR(EIO);
	}
	ASSERT(dq_f->qlf_len == 1);

	error = xfs_read_buf(mp, mp->m_ddev_targp,
			     dq_f->qlf_blkno,
			     XFS_FSB_TO_BB(mp, dq_f->qlf_len),
			     0, &bp);
	if (error) {
		xfs_ioerror_alert("xlog_recover_do..(read#3)", mp,
				  bp, dq_f->qlf_blkno);
		return error;
	}
	ASSERT(bp);
	ddq = (xfs_disk_dquot_t *)xfs_buf_offset(bp, dq_f->qlf_boffset);

	/*
	 * At least the magic num portion should be on disk because this
	 * was among a chunk of dquots created earlier, and we did some
	 * minimal initialization then.
	 */
	if (xfs_qm_dqcheck(ddq, dq_f->qlf_id, 0, XFS_QMOPT_DOWARN,
			   "xlog_recover_do_dquot_trans")) {
		xfs_buf_relse(bp);
		return XFS_ERROR(EIO);
	}

	memcpy(ddq, recddq, item->ri_buf[1].i_len);

	ASSERT(dq_f->qlf_size == 2);
	ASSERT(bp->b_mount == NULL || bp->b_mount == mp);
	bp->b_mount = mp;
	XFS_BUF_SET_IODONE_FUNC(bp, xlog_recover_iodone);
	xfs_bdwrite(mp, bp);

	return (0);
}

/*
 * This routine is called to create an in-core extent free intent
 * item from the efi format structure which was logged on disk.
 * It allocates an in-core efi, copies the extents from the format
 * structure into it, and adds the efi to the AIL with the given
 * LSN.
 */
STATIC int
xlog_recover_do_efi_trans(
	xlog_t			*log,
	xlog_recover_item_t	*item,
	xfs_lsn_t		lsn,
	int			pass)
{
	int			error;
	xfs_mount_t		*mp;
	xfs_efi_log_item_t	*efip;
	xfs_efi_log_format_t	*efi_formatp;

	if (pass == XLOG_RECOVER_PASS1) {
		return 0;
	}

	efi_formatp = (xfs_efi_log_format_t *)item->ri_buf[0].i_addr;

	mp = log->l_mp;
	efip = xfs_efi_init(mp, efi_formatp->efi_nextents);
	if ((error = xfs_efi_copy_format(&(item->ri_buf[0]),
					 &(efip->efi_format)))) {
		xfs_efi_item_free(efip);
		return error;
	}
	efip->efi_next_extent = efi_formatp->efi_nextents;
	efip->efi_flags |= XFS_EFI_COMMITTED;

	spin_lock(&log->l_ailp->xa_lock);
	/*
	 * xfs_trans_ail_update() drops the AIL lock.
	 */
	xfs_trans_ail_update(log->l_ailp, (xfs_log_item_t *)efip, lsn);
	return 0;
}


/*
 * This routine is called when an efd format structure is found in
 * a committed transaction in the log.  It's purpose is to cancel
 * the corresponding efi if it was still in the log.  To do this
 * it searches the AIL for the efi with an id equal to that in the
 * efd format structure.  If we find it, we remove the efi from the
 * AIL and free it.
 */
STATIC void
xlog_recover_do_efd_trans(
	xlog_t			*log,
	xlog_recover_item_t	*item,
	int			pass)
{
	xfs_efd_log_format_t	*efd_formatp;
	xfs_efi_log_item_t	*efip = NULL;
	xfs_log_item_t		*lip;
	__uint64_t		efi_id;
	struct xfs_ail_cursor	cur;
	struct xfs_ail		*ailp = log->l_ailp;

	if (pass == XLOG_RECOVER_PASS1) {
		return;
	}

	efd_formatp = (xfs_efd_log_format_t *)item->ri_buf[0].i_addr;
	ASSERT((item->ri_buf[0].i_len == (sizeof(xfs_efd_log_format_32_t) +
		((efd_formatp->efd_nextents - 1) * sizeof(xfs_extent_32_t)))) ||
	       (item->ri_buf[0].i_len == (sizeof(xfs_efd_log_format_64_t) +
		((efd_formatp->efd_nextents - 1) * sizeof(xfs_extent_64_t)))));
	efi_id = efd_formatp->efd_efi_id;

	/*
	 * Search for the efi with the id in the efd format structure
	 * in the AIL.
	 */
	spin_lock(&ailp->xa_lock);
	lip = xfs_trans_ail_cursor_first(ailp, &cur, 0);
	while (lip != NULL) {
		if (lip->li_type == XFS_LI_EFI) {
			efip = (xfs_efi_log_item_t *)lip;
			if (efip->efi_format.efi_id == efi_id) {
				/*
				 * xfs_trans_ail_delete() drops the
				 * AIL lock.
				 */
				xfs_trans_ail_delete(ailp, lip);
				xfs_efi_item_free(efip);
				spin_lock(&ailp->xa_lock);
				break;
			}
		}
		lip = xfs_trans_ail_cursor_next(ailp, &cur);
	}
	xfs_trans_ail_cursor_done(ailp, &cur);
	spin_unlock(&ailp->xa_lock);
}

/*
 * Perform the transaction
 *
 * If the transaction modifies a buffer or inode, do it now.  Otherwise,
 * EFIs and EFDs get queued up by adding entries into the AIL for them.
 */
STATIC int
xlog_recover_do_trans(
	xlog_t			*log,
	xlog_recover_t		*trans,
	int			pass)
{
	int			error = 0;
	xlog_recover_item_t	*item, *first_item;

	error = xlog_recover_reorder_trans(trans);
	if (error)
		return error;

	first_item = item = trans->r_itemq;
	do {
		switch (ITEM_TYPE(item)) {
		case XFS_LI_BUF:
			error = xlog_recover_do_buffer_trans(log, item, pass);
			break;
		case XFS_LI_INODE:
			error = xlog_recover_do_inode_trans(log, item, pass);
			break;
		case XFS_LI_EFI:
			error = xlog_recover_do_efi_trans(log, item,
							  trans->r_lsn, pass);
			break;
		case XFS_LI_EFD:
			xlog_recover_do_efd_trans(log, item, pass);
			error = 0;
			break;
		case XFS_LI_DQUOT:
			error = xlog_recover_do_dquot_trans(log, item, pass);
			break;
		case XFS_LI_QUOTAOFF:
			error = xlog_recover_do_quotaoff_trans(log, item,
							       pass);
			break;
		default:
			xlog_warn(
	"XFS: invalid item type (%d) xlog_recover_do_trans", ITEM_TYPE(item));
			ASSERT(0);
			error = XFS_ERROR(EIO);
			break;
		}

		if (error)
			return error;
		item = item->ri_next;
	} while (first_item != item);

	return 0;
}

/*
 * Free up any resources allocated by the transaction
 *
 * Remember that EFIs, EFDs, and IUNLINKs are handled later.
 */
STATIC void
xlog_recover_free_trans(
	xlog_recover_t		*trans)
{
	xlog_recover_item_t	*first_item, *item, *free_item;
	int			i;

	item = first_item = trans->r_itemq;
	do {
		free_item = item;
		item = item->ri_next;
		 /* Free the regions in the item. */
		for (i = 0; i < free_item->ri_cnt; i++) {
			kmem_free(free_item->ri_buf[i].i_addr);
		}
		/* Free the item itself */
		kmem_free(free_item->ri_buf);
		kmem_free(free_item);
	} while (first_item != item);
	/* Free the transaction recover structure */
	kmem_free(trans);
}

STATIC int
xlog_recover_commit_trans(
	xlog_t			*log,
	xlog_recover_t		**q,
	xlog_recover_t		*trans,
	int			pass)
{
	int			error;

	if ((error = xlog_recover_unlink_tid(q, trans)))
		return error;
	if ((error = xlog_recover_do_trans(log, trans, pass)))
		return error;
	xlog_recover_free_trans(trans);			/* no error */
	return 0;
}

STATIC int
xlog_recover_unmount_trans(
	xlog_recover_t		*trans)
{
	/* Do nothing now */
	xlog_warn("XFS: xlog_recover_unmount_trans: Unmount LR");
	return 0;
}

/*
 * There are two valid states of the r_state field.  0 indicates that the
 * transaction structure is in a normal state.  We have either seen the
 * start of the transaction or the last operation we added was not a partial
 * operation.  If the last operation we added to the transaction was a
 * partial operation, we need to mark r_state with XLOG_WAS_CONT_TRANS.
 *
 * NOTE: skip LRs with 0 data length.
 */
STATIC int
xlog_recover_process_data(
	xlog_t			*log,
	xlog_recover_t		*rhash[],
	xlog_rec_header_t	*rhead,
	xfs_caddr_t		dp,
	int			pass)
{
	xfs_caddr_t		lp;
	int			num_logops;
	xlog_op_header_t	*ohead;
	xlog_recover_t		*trans;
	xlog_tid_t		tid;
	int			error;
	unsigned long		hash;
	uint			flags;

	lp = dp + be32_to_cpu(rhead->h_len);
	num_logops = be32_to_cpu(rhead->h_num_logops);

	/* check the log format matches our own - else we can't recover */
	if (xlog_header_check_recover(log->l_mp, rhead))
		return (XFS_ERROR(EIO));

	while ((dp < lp) && num_logops) {
		ASSERT(dp + sizeof(xlog_op_header_t) <= lp);
		ohead = (xlog_op_header_t *)dp;
		dp += sizeof(xlog_op_header_t);
		if (ohead->oh_clientid != XFS_TRANSACTION &&
		    ohead->oh_clientid != XFS_LOG) {
			xlog_warn(
		"XFS: xlog_recover_process_data: bad clientid");
			ASSERT(0);
			return (XFS_ERROR(EIO));
		}
		tid = be32_to_cpu(ohead->oh_tid);
		hash = XLOG_RHASH(tid);
		trans = xlog_recover_find_tid(rhash[hash], tid);
		if (trans == NULL) {		   /* not found; add new tid */
			if (ohead->oh_flags & XLOG_START_TRANS)
				xlog_recover_new_tid(&rhash[hash], tid,
					be64_to_cpu(rhead->h_lsn));
		} else {
			if (dp + be32_to_cpu(ohead->oh_len) > lp) {
				xlog_warn(
			"XFS: xlog_recover_process_data: bad length");
				WARN_ON(1);
				return (XFS_ERROR(EIO));
			}
			flags = ohead->oh_flags & ~XLOG_END_TRANS;
			if (flags & XLOG_WAS_CONT_TRANS)
				flags &= ~XLOG_CONTINUE_TRANS;
			switch (flags) {
			case XLOG_COMMIT_TRANS:
				error = xlog_recover_commit_trans(log,
						&rhash[hash], trans, pass);
				break;
			case XLOG_UNMOUNT_TRANS:
				error = xlog_recover_unmount_trans(trans);
				break;
			case XLOG_WAS_CONT_TRANS:
				error = xlog_recover_add_to_cont_trans(trans,
						dp, be32_to_cpu(ohead->oh_len));
				break;
			case XLOG_START_TRANS:
				xlog_warn(
			"XFS: xlog_recover_process_data: bad transaction");
				ASSERT(0);
				error = XFS_ERROR(EIO);
				break;
			case 0:
			case XLOG_CONTINUE_TRANS:
				error = xlog_recover_add_to_trans(trans,
						dp, be32_to_cpu(ohead->oh_len));
				break;
			default:
				xlog_warn(
			"XFS: xlog_recover_process_data: bad flag");
				ASSERT(0);
				error = XFS_ERROR(EIO);
				break;
			}
			if (error)
				return error;
		}
		dp += be32_to_cpu(ohead->oh_len);
		num_logops--;
	}
	return 0;
}

/*
 * Process an extent free intent item that was recovered from
 * the log.  We need to free the extents that it describes.
 */
STATIC int
xlog_recover_process_efi(
	xfs_mount_t		*mp,
	xfs_efi_log_item_t	*efip)
{
	xfs_efd_log_item_t	*efdp;
	xfs_trans_t		*tp;
	int			i;
	int			error = 0;
	xfs_extent_t		*extp;
	xfs_fsblock_t		startblock_fsb;

	ASSERT(!(efip->efi_flags & XFS_EFI_RECOVERED));

	/*
	 * First check the validity of the extents described by the
	 * EFI.  If any are bad, then assume that all are bad and
	 * just toss the EFI.
	 */
	for (i = 0; i < efip->efi_format.efi_nextents; i++) {
		extp = &(efip->efi_format.efi_extents[i]);
		startblock_fsb = XFS_BB_TO_FSB(mp,
				   XFS_FSB_TO_DADDR(mp, extp->ext_start));
		if ((startblock_fsb == 0) ||
		    (extp->ext_len == 0) ||
		    (startblock_fsb >= mp->m_sb.sb_dblocks) ||
		    (extp->ext_len >= mp->m_sb.sb_agblocks)) {
			/*
			 * This will pull the EFI from the AIL and
			 * free the memory associated with it.
			 */
			xfs_efi_release(efip, efip->efi_format.efi_nextents);
			return XFS_ERROR(EIO);
		}
	}

	tp = xfs_trans_alloc(mp, 0);
	error = xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0, 0, 0);
	if (error)
		goto abort_error;
	efdp = xfs_trans_get_efd(tp, efip, efip->efi_format.efi_nextents);

	for (i = 0; i < efip->efi_format.efi_nextents; i++) {
		extp = &(efip->efi_format.efi_extents[i]);
		error = xfs_free_extent(tp, extp->ext_start, extp->ext_len);
		if (error)
			goto abort_error;
		xfs_trans_log_efd_extent(tp, efdp, extp->ext_start,
					 extp->ext_len);
	}

	efip->efi_flags |= XFS_EFI_RECOVERED;
	error = xfs_trans_commit(tp, 0);
	return error;

abort_error:
	xfs_trans_cancel(tp, XFS_TRANS_ABORT);
	return error;
}

/*
 * When this is called, all of the EFIs which did not have
 * corresponding EFDs should be in the AIL.  What we do now
 * is free the extents associated with each one.
 *
 * Since we process the EFIs in normal transactions, they
 * will be removed at some point after the commit.  This prevents
 * us from just walking down the list processing each one.
 * We'll use a flag in the EFI to skip those that we've already
 * processed and use the AIL iteration mechanism's generation
 * count to try to speed this up at least a bit.
 *
 * When we start, we know that the EFIs are the only things in
 * the AIL.  As we process them, however, other items are added
 * to the AIL.  Since everything added to the AIL must come after
 * everything already in the AIL, we stop processing as soon as
 * we see something other than an EFI in the AIL.
 */
STATIC int
xlog_recover_process_efis(
	xlog_t			*log)
{
	xfs_log_item_t		*lip;
	xfs_efi_log_item_t	*efip;
	int			error = 0;
	struct xfs_ail_cursor	cur;
	struct xfs_ail		*ailp;

	ailp = log->l_ailp;
	spin_lock(&ailp->xa_lock);
	lip = xfs_trans_ail_cursor_first(ailp, &cur, 0);
	while (lip != NULL) {
		/*
		 * We're done when we see something other than an EFI.
		 * There should be no EFIs left in the AIL now.
		 */
		if (lip->li_type != XFS_LI_EFI) {
#ifdef DEBUG
			for (; lip; lip = xfs_trans_ail_cursor_next(ailp, &cur))
				ASSERT(lip->li_type != XFS_LI_EFI);
#endif
			break;
		}

		/*
		 * Skip EFIs that we've already processed.
		 */
		efip = (xfs_efi_log_item_t *)lip;
		if (efip->efi_flags & XFS_EFI_RECOVERED) {
			lip = xfs_trans_ail_cursor_next(ailp, &cur);
			continue;
		}

		spin_unlock(&ailp->xa_lock);
		error = xlog_recover_process_efi(log->l_mp, efip);
		spin_lock(&ailp->xa_lock);
		if (error)
			goto out;
		lip = xfs_trans_ail_cursor_next(ailp, &cur);
	}
out:
	xfs_trans_ail_cursor_done(ailp, &cur);
	spin_unlock(&ailp->xa_lock);
	return error;
}

/*
 * This routine performs a transaction to null out a bad inode pointer
 * in an agi unlinked inode hash bucket.
 */
STATIC void
xlog_recover_clear_agi_bucket(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno,
	int		bucket)
{
	xfs_trans_t	*tp;
	xfs_agi_t	*agi;
	xfs_buf_t	*agibp;
	int		offset;
	int		error;

	tp = xfs_trans_alloc(mp, XFS_TRANS_CLEAR_AGI_BUCKET);
	error = xfs_trans_reserve(tp, 0, XFS_CLEAR_AGI_BUCKET_LOG_RES(mp),
				  0, 0, 0);
	if (error)
		goto out_abort;

	error = xfs_read_agi(mp, tp, agno, &agibp);
	if (error)
		goto out_abort;

	agi = XFS_BUF_TO_AGI(agibp);
	agi->agi_unlinked[bucket] = cpu_to_be32(NULLAGINO);
	offset = offsetof(xfs_agi_t, agi_unlinked) +
		 (sizeof(xfs_agino_t) * bucket);
	xfs_trans_log_buf(tp, agibp, offset,
			  (offset + sizeof(xfs_agino_t) - 1));

	error = xfs_trans_commit(tp, 0);
	if (error)
		goto out_error;
	return;

out_abort:
	xfs_trans_cancel(tp, XFS_TRANS_ABORT);
out_error:
	xfs_fs_cmn_err(CE_WARN, mp, "xlog_recover_clear_agi_bucket: "
			"failed to clear agi %d. Continuing.", agno);
	return;
}

STATIC xfs_agino_t
xlog_recover_process_one_iunlink(
	struct xfs_mount		*mp,
	xfs_agnumber_t			agno,
	xfs_agino_t			agino,
	int				bucket)
{
	struct xfs_buf			*ibp;
	struct xfs_dinode		*dip;
	struct xfs_inode		*ip;
	xfs_ino_t			ino;
	int				error;

	ino = XFS_AGINO_TO_INO(mp, agno, agino);
	error = xfs_iget(mp, NULL, ino, 0, 0, &ip, 0);
	if (error)
		goto fail;

	/*
	 * Get the on disk inode to find the next inode in the bucket.
	 */
	error = xfs_itobp(mp, NULL, ip, &dip, &ibp, XFS_BUF_LOCK);
	if (error)
		goto fail_iput;

	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_mode != 0);

	/* setup for the next pass */
	agino = be32_to_cpu(dip->di_next_unlinked);
	xfs_buf_relse(ibp);

	/*
	 * Prevent any DMAPI event from being sent when the reference on
	 * the inode is dropped.
	 */
	ip->i_d.di_dmevmask = 0;

	IRELE(ip);
	return agino;

 fail_iput:
	IRELE(ip);
 fail:
	/*
	 * We can't read in the inode this bucket points to, or this inode
	 * is messed up.  Just ditch this bucket of inodes.  We will lose
	 * some inodes and space, but at least we won't hang.
	 *
	 * Call xlog_recover_clear_agi_bucket() to perform a transaction to
	 * clear the inode pointer in the bucket.
	 */
	xlog_recover_clear_agi_bucket(mp, agno, bucket);
	return NULLAGINO;
}

/*
 * xlog_iunlink_recover
 *
 * This is called during recovery to process any inodes which
 * we unlinked but not freed when the system crashed.  These
 * inodes will be on the lists in the AGI blocks.  What we do
 * here is scan all the AGIs and fully truncate and free any
 * inodes found on the lists.  Each inode is removed from the
 * lists when it has been fully truncated and is freed.  The
 * freeing of the inode and its removal from the list must be
 * atomic.
 */
void
xlog_recover_process_iunlinks(
	xlog_t		*log)
{
	xfs_mount_t	*mp;
	xfs_agnumber_t	agno;
	xfs_agi_t	*agi;
	xfs_buf_t	*agibp;
	xfs_agino_t	agino;
	int		bucket;
	int		error;
	uint		mp_dmevmask;

	mp = log->l_mp;

	/*
	 * Prevent any DMAPI event from being sent while in this function.
	 */
	mp_dmevmask = mp->m_dmevmask;
	mp->m_dmevmask = 0;

	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		/*
		 * Find the agi for this ag.
		 */
		error = xfs_read_agi(mp, NULL, agno, &agibp);
		if (error) {
			/*
			 * AGI is b0rked. Don't process it.
			 *
			 * We should probably mark the filesystem as corrupt
			 * after we've recovered all the ag's we can....
			 */
			continue;
		}
		agi = XFS_BUF_TO_AGI(agibp);

		for (bucket = 0; bucket < XFS_AGI_UNLINKED_BUCKETS; bucket++) {
			agino = be32_to_cpu(agi->agi_unlinked[bucket]);
			while (agino != NULLAGINO) {
				/*
				 * Release the agi buffer so that it can
				 * be acquired in the normal course of the
				 * transaction to truncate and free the inode.
				 */
				xfs_buf_relse(agibp);

				agino = xlog_recover_process_one_iunlink(mp,
							agno, agino, bucket);

				/*
				 * Reacquire the agibuffer and continue around
				 * the loop. This should never fail as we know
				 * the buffer was good earlier on.
				 */
				error = xfs_read_agi(mp, NULL, agno, &agibp);
				ASSERT(error == 0);
				agi = XFS_BUF_TO_AGI(agibp);
			}
		}

		/*
		 * Release the buffer for the current agi so we can
		 * go on to the next one.
		 */
		xfs_buf_relse(agibp);
	}

	mp->m_dmevmask = mp_dmevmask;
}


#ifdef DEBUG
STATIC void
xlog_pack_data_checksum(
	xlog_t		*log,
	xlog_in_core_t	*iclog,
	int		size)
{
	int		i;
	__be32		*up;
	uint		chksum = 0;

	up = (__be32 *)iclog->ic_datap;
	/* divide length by 4 to get # words */
	for (i = 0; i < (size >> 2); i++) {
		chksum ^= be32_to_cpu(*up);
		up++;
	}
	iclog->ic_header.h_chksum = cpu_to_be32(chksum);
}
#else
#define xlog_pack_data_checksum(log, iclog, size)
#endif

/*
 * Stamp cycle number in every block
 */
void
xlog_pack_data(
	xlog_t			*log,
	xlog_in_core_t		*iclog,
	int			roundoff)
{
	int			i, j, k;
	int			size = iclog->ic_offset + roundoff;
	__be32			cycle_lsn;
	xfs_caddr_t		dp;

	xlog_pack_data_checksum(log, iclog, size);

	cycle_lsn = CYCLE_LSN_DISK(iclog->ic_header.h_lsn);

	dp = iclog->ic_datap;
	for (i = 0; i < BTOBB(size) &&
		i < (XLOG_HEADER_CYCLE_SIZE / BBSIZE); i++) {
		iclog->ic_header.h_cycle_data[i] = *(__be32 *)dp;
		*(__be32 *)dp = cycle_lsn;
		dp += BBSIZE;
	}

	if (xfs_sb_version_haslogv2(&log->l_mp->m_sb)) {
		xlog_in_core_2_t *xhdr = iclog->ic_data;

		for ( ; i < BTOBB(size); i++) {
			j = i / (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
			k = i % (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
			xhdr[j].hic_xheader.xh_cycle_data[k] = *(__be32 *)dp;
			*(__be32 *)dp = cycle_lsn;
			dp += BBSIZE;
		}

		for (i = 1; i < log->l_iclog_heads; i++) {
			xhdr[i].hic_xheader.xh_cycle = cycle_lsn;
		}
	}
}

#if defined(DEBUG) && defined(XFS_LOUD_RECOVERY)
STATIC void
xlog_unpack_data_checksum(
	xlog_rec_header_t	*rhead,
	xfs_caddr_t		dp,
	xlog_t			*log)
{
	__be32			*up = (__be32 *)dp;
	uint			chksum = 0;
	int			i;

	/* divide length by 4 to get # words */
	for (i=0; i < be32_to_cpu(rhead->h_len) >> 2; i++) {
		chksum ^= be32_to_cpu(*up);
		up++;
	}
	if (chksum != be32_to_cpu(rhead->h_chksum)) {
	    if (rhead->h_chksum ||
		((log->l_flags & XLOG_CHKSUM_MISMATCH) == 0)) {
		    cmn_err(CE_DEBUG,
			"XFS: LogR chksum mismatch: was (0x%x) is (0x%x)\n",
			    be32_to_cpu(rhead->h_chksum), chksum);
		    cmn_err(CE_DEBUG,
"XFS: Disregard message if filesystem was created with non-DEBUG kernel");
		    if (xfs_sb_version_haslogv2(&log->l_mp->m_sb)) {
			    cmn_err(CE_DEBUG,
				"XFS: LogR this is a LogV2 filesystem\n");
		    }
		    log->l_flags |= XLOG_CHKSUM_MISMATCH;
	    }
	}
}
#else
#define xlog_unpack_data_checksum(rhead, dp, log)
#endif

STATIC void
xlog_unpack_data(
	xlog_rec_header_t	*rhead,
	xfs_caddr_t		dp,
	xlog_t			*log)
{
	int			i, j, k;

	for (i = 0; i < BTOBB(be32_to_cpu(rhead->h_len)) &&
		  i < (XLOG_HEADER_CYCLE_SIZE / BBSIZE); i++) {
		*(__be32 *)dp = *(__be32 *)&rhead->h_cycle_data[i];
		dp += BBSIZE;
	}

	if (xfs_sb_version_haslogv2(&log->l_mp->m_sb)) {
		xlog_in_core_2_t *xhdr = (xlog_in_core_2_t *)rhead;
		for ( ; i < BTOBB(be32_to_cpu(rhead->h_len)); i++) {
			j = i / (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
			k = i % (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
			*(__be32 *)dp = xhdr[j].hic_xheader.xh_cycle_data[k];
			dp += BBSIZE;
		}
	}

	xlog_unpack_data_checksum(rhead, dp, log);
}

STATIC int
xlog_valid_rec_header(
	xlog_t			*log,
	xlog_rec_header_t	*rhead,
	xfs_daddr_t		blkno)
{
	int			hlen;

	if (unlikely(be32_to_cpu(rhead->h_magicno) != XLOG_HEADER_MAGIC_NUM)) {
		XFS_ERROR_REPORT("xlog_valid_rec_header(1)",
				XFS_ERRLEVEL_LOW, log->l_mp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	if (unlikely(
	    (!rhead->h_version ||
	    (be32_to_cpu(rhead->h_version) & (~XLOG_VERSION_OKBITS))))) {
		xlog_warn("XFS: %s: unrecognised log version (%d).",
			__func__, be32_to_cpu(rhead->h_version));
		return XFS_ERROR(EIO);
	}

	/* LR body must have data or it wouldn't have been written */
	hlen = be32_to_cpu(rhead->h_len);
	if (unlikely( hlen <= 0 || hlen > INT_MAX )) {
		XFS_ERROR_REPORT("xlog_valid_rec_header(2)",
				XFS_ERRLEVEL_LOW, log->l_mp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	if (unlikely( blkno > log->l_logBBsize || blkno > INT_MAX )) {
		XFS_ERROR_REPORT("xlog_valid_rec_header(3)",
				XFS_ERRLEVEL_LOW, log->l_mp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	return 0;
}

/*
 * Read the log from tail to head and process the log records found.
 * Handle the two cases where the tail and head are in the same cycle
 * and where the active portion of the log wraps around the end of
 * the physical log separately.  The pass parameter is passed through
 * to the routines called to process the data and is not looked at
 * here.
 */
STATIC int
xlog_do_recovery_pass(
	xlog_t			*log,
	xfs_daddr_t		head_blk,
	xfs_daddr_t		tail_blk,
	int			pass)
{
	xlog_rec_header_t	*rhead;
	xfs_daddr_t		blk_no;
	xfs_caddr_t		bufaddr, offset;
	xfs_buf_t		*hbp, *dbp;
	int			error = 0, h_size;
	int			bblks, split_bblks;
	int			hblks, split_hblks, wrapped_hblks;
	xlog_recover_t		*rhash[XLOG_RHASH_SIZE];

	ASSERT(head_blk != tail_blk);

	/*
	 * Read the header of the tail block and get the iclog buffer size from
	 * h_size.  Use this to tell how many sectors make up the log header.
	 */
	if (xfs_sb_version_haslogv2(&log->l_mp->m_sb)) {
		/*
		 * When using variable length iclogs, read first sector of
		 * iclog header and extract the header size from it.  Get a
		 * new hbp that is the correct size.
		 */
		hbp = xlog_get_bp(log, 1);
		if (!hbp)
			return ENOMEM;

		error = xlog_bread(log, tail_blk, 1, hbp, &offset);
		if (error)
			goto bread_err1;

		rhead = (xlog_rec_header_t *)offset;
		error = xlog_valid_rec_header(log, rhead, tail_blk);
		if (error)
			goto bread_err1;
		h_size = be32_to_cpu(rhead->h_size);
		if ((be32_to_cpu(rhead->h_version) & XLOG_VERSION_2) &&
		    (h_size > XLOG_HEADER_CYCLE_SIZE)) {
			hblks = h_size / XLOG_HEADER_CYCLE_SIZE;
			if (h_size % XLOG_HEADER_CYCLE_SIZE)
				hblks++;
			xlog_put_bp(hbp);
			hbp = xlog_get_bp(log, hblks);
		} else {
			hblks = 1;
		}
	} else {
		ASSERT(log->l_sectbb_log == 0);
		hblks = 1;
		hbp = xlog_get_bp(log, 1);
		h_size = XLOG_BIG_RECORD_BSIZE;
	}

	if (!hbp)
		return ENOMEM;
	dbp = xlog_get_bp(log, BTOBB(h_size));
	if (!dbp) {
		xlog_put_bp(hbp);
		return ENOMEM;
	}

	memset(rhash, 0, sizeof(rhash));
	if (tail_blk <= head_blk) {
		for (blk_no = tail_blk; blk_no < head_blk; ) {
			error = xlog_bread(log, blk_no, hblks, hbp, &offset);
			if (error)
				goto bread_err2;

			rhead = (xlog_rec_header_t *)offset;
			error = xlog_valid_rec_header(log, rhead, blk_no);
			if (error)
				goto bread_err2;

			/* blocks in data section */
			bblks = (int)BTOBB(be32_to_cpu(rhead->h_len));
			error = xlog_bread(log, blk_no + hblks, bblks, dbp,
					   &offset);
			if (error)
				goto bread_err2;

			xlog_unpack_data(rhead, offset, log);
			if ((error = xlog_recover_process_data(log,
						rhash, rhead, offset, pass)))
				goto bread_err2;
			blk_no += bblks + hblks;
		}
	} else {
		/*
		 * Perform recovery around the end of the physical log.
		 * When the head is not on the same cycle number as the tail,
		 * we can't do a sequential recovery as above.
		 */
		blk_no = tail_blk;
		while (blk_no < log->l_logBBsize) {
			/*
			 * Check for header wrapping around physical end-of-log
			 */
			offset = NULL;
			split_hblks = 0;
			wrapped_hblks = 0;
			if (blk_no + hblks <= log->l_logBBsize) {
				/* Read header in one read */
				error = xlog_bread(log, blk_no, hblks, hbp,
						   &offset);
				if (error)
					goto bread_err2;
			} else {
				/* This LR is split across physical log end */
				if (blk_no != log->l_logBBsize) {
					/* some data before physical log end */
					ASSERT(blk_no <= INT_MAX);
					split_hblks = log->l_logBBsize - (int)blk_no;
					ASSERT(split_hblks > 0);
					error = xlog_bread(log, blk_no,
							   split_hblks, hbp,
							   &offset);
					if (error)
						goto bread_err2;
				}

				/*
				 * Note: this black magic still works with
				 * large sector sizes (non-512) only because:
				 * - we increased the buffer size originally
				 *   by 1 sector giving us enough extra space
				 *   for the second read;
				 * - the log start is guaranteed to be sector
				 *   aligned;
				 * - we read the log end (LR header start)
				 *   _first_, then the log start (LR header end)
				 *   - order is important.
				 */
				wrapped_hblks = hblks - split_hblks;
				bufaddr = XFS_BUF_PTR(hbp);
				error = XFS_BUF_SET_PTR(hbp,
						bufaddr + BBTOB(split_hblks),
						BBTOB(hblks - split_hblks));
				if (error)
					goto bread_err2;

				error = xlog_bread_noalign(log, 0,
							   wrapped_hblks, hbp);
				if (error)
					goto bread_err2;

				error = XFS_BUF_SET_PTR(hbp, bufaddr,
							BBTOB(hblks));
				if (error)
					goto bread_err2;

				if (!offset)
					offset = xlog_align(log, 0,
							wrapped_hblks, hbp);
			}
			rhead = (xlog_rec_header_t *)offset;
			error = xlog_valid_rec_header(log, rhead,
						split_hblks ? blk_no : 0);
			if (error)
				goto bread_err2;

			bblks = (int)BTOBB(be32_to_cpu(rhead->h_len));
			blk_no += hblks;

			/* Read in data for log record */
			if (blk_no + bblks <= log->l_logBBsize) {
				error = xlog_bread(log, blk_no, bblks, dbp,
						   &offset);
				if (error)
					goto bread_err2;
			} else {
				/* This log record is split across the
				 * physical end of log */
				offset = NULL;
				split_bblks = 0;
				if (blk_no != log->l_logBBsize) {
					/* some data is before the physical
					 * end of log */
					ASSERT(!wrapped_hblks);
					ASSERT(blk_no <= INT_MAX);
					split_bblks =
						log->l_logBBsize - (int)blk_no;
					ASSERT(split_bblks > 0);
					error = xlog_bread(log, blk_no,
							split_bblks, dbp,
							&offset);
					if (error)
						goto bread_err2;
				}

				/*
				 * Note: this black magic still works with
				 * large sector sizes (non-512) only because:
				 * - we increased the buffer size originally
				 *   by 1 sector giving us enough extra space
				 *   for the second read;
				 * - the log start is guaranteed to be sector
				 *   aligned;
				 * - we read the log end (LR header start)
				 *   _first_, then the log start (LR header end)
				 *   - order is important.
				 */
				bufaddr = XFS_BUF_PTR(dbp);
				error = XFS_BUF_SET_PTR(dbp,
						bufaddr + BBTOB(split_bblks),
						BBTOB(bblks - split_bblks));
				if (error)
					goto bread_err2;

				error = xlog_bread_noalign(log, wrapped_hblks,
						bblks - split_bblks,
						dbp);
				if (error)
					goto bread_err2;

				error = XFS_BUF_SET_PTR(dbp, bufaddr, h_size);
				if (error)
					goto bread_err2;

				if (!offset)
					offset = xlog_align(log, wrapped_hblks,
						bblks - split_bblks, dbp);
			}
			xlog_unpack_data(rhead, offset, log);
			if ((error = xlog_recover_process_data(log, rhash,
							rhead, offset, pass)))
				goto bread_err2;
			blk_no += bblks;
		}

		ASSERT(blk_no >= log->l_logBBsize);
		blk_no -= log->l_logBBsize;

		/* read first part of physical log */
		while (blk_no < head_blk) {
			error = xlog_bread(log, blk_no, hblks, hbp, &offset);
			if (error)
				goto bread_err2;

			rhead = (xlog_rec_header_t *)offset;
			error = xlog_valid_rec_header(log, rhead, blk_no);
			if (error)
				goto bread_err2;

			bblks = (int)BTOBB(be32_to_cpu(rhead->h_len));
			error = xlog_bread(log, blk_no+hblks, bblks, dbp,
					   &offset);
			if (error)
				goto bread_err2;

			xlog_unpack_data(rhead, offset, log);
			if ((error = xlog_recover_process_data(log, rhash,
							rhead, offset, pass)))
				goto bread_err2;
			blk_no += bblks + hblks;
		}
	}

 bread_err2:
	xlog_put_bp(dbp);
 bread_err1:
	xlog_put_bp(hbp);
	return error;
}

/*
 * Do the recovery of the log.  We actually do this in two phases.
 * The two passes are necessary in order to implement the function
 * of cancelling a record written into the log.  The first pass
 * determines those things which have been cancelled, and the
 * second pass replays log items normally except for those which
 * have been cancelled.  The handling of the replay and cancellations
 * takes place in the log item type specific routines.
 *
 * The table of items which have cancel records in the log is allocated
 * and freed at this level, since only here do we know when all of
 * the log recovery has been completed.
 */
STATIC int
xlog_do_log_recovery(
	xlog_t		*log,
	xfs_daddr_t	head_blk,
	xfs_daddr_t	tail_blk)
{
	int		error;

	ASSERT(head_blk != tail_blk);

	/*
	 * First do a pass to find all of the cancelled buf log items.
	 * Store them in the buf_cancel_table for use in the second pass.
	 */
	log->l_buf_cancel_table =
		(xfs_buf_cancel_t **)kmem_zalloc(XLOG_BC_TABLE_SIZE *
						 sizeof(xfs_buf_cancel_t*),
						 KM_SLEEP);
	error = xlog_do_recovery_pass(log, head_blk, tail_blk,
				      XLOG_RECOVER_PASS1);
	if (error != 0) {
		kmem_free(log->l_buf_cancel_table);
		log->l_buf_cancel_table = NULL;
		return error;
	}
	/*
	 * Then do a second pass to actually recover the items in the log.
	 * When it is complete free the table of buf cancel items.
	 */
	error = xlog_do_recovery_pass(log, head_blk, tail_blk,
				      XLOG_RECOVER_PASS2);
#ifdef DEBUG
	if (!error) {
		int	i;

		for (i = 0; i < XLOG_BC_TABLE_SIZE; i++)
			ASSERT(log->l_buf_cancel_table[i] == NULL);
	}
#endif	/* DEBUG */

	kmem_free(log->l_buf_cancel_table);
	log->l_buf_cancel_table = NULL;

	return error;
}

/*
 * Do the actual recovery
 */
STATIC int
xlog_do_recover(
	xlog_t		*log,
	xfs_daddr_t	head_blk,
	xfs_daddr_t	tail_blk)
{
	int		error;
	xfs_buf_t	*bp;
	xfs_sb_t	*sbp;

	/*
	 * First replay the images in the log.
	 */
	error = xlog_do_log_recovery(log, head_blk, tail_blk);
	if (error) {
		return error;
	}

	XFS_bflush(log->l_mp->m_ddev_targp);

	/*
	 * If IO errors happened during recovery, bail out.
	 */
	if (XFS_FORCED_SHUTDOWN(log->l_mp)) {
		return (EIO);
	}

	/*
	 * We now update the tail_lsn since much of the recovery has completed
	 * and there may be space available to use.  If there were no extent
	 * or iunlinks, we can free up the entire log and set the tail_lsn to
	 * be the last_sync_lsn.  This was set in xlog_find_tail to be the
	 * lsn of the last known good LR on disk.  If there are extent frees
	 * or iunlinks they will have some entries in the AIL; so we look at
	 * the AIL to determine how to set the tail_lsn.
	 */
	xlog_assign_tail_lsn(log->l_mp);

	/*
	 * Now that we've finished replaying all buffer and inode
	 * updates, re-read in the superblock.
	 */
	bp = xfs_getsb(log->l_mp, 0);
	XFS_BUF_UNDONE(bp);
	ASSERT(!(XFS_BUF_ISWRITE(bp)));
	ASSERT(!(XFS_BUF_ISDELAYWRITE(bp)));
	XFS_BUF_READ(bp);
	XFS_BUF_UNASYNC(bp);
	xfsbdstrat(log->l_mp, bp);
	error = xfs_iowait(bp);
	if (error) {
		xfs_ioerror_alert("xlog_do_recover",
				  log->l_mp, bp, XFS_BUF_ADDR(bp));
		ASSERT(0);
		xfs_buf_relse(bp);
		return error;
	}

	/* Convert superblock from on-disk format */
	sbp = &log->l_mp->m_sb;
	xfs_sb_from_disk(sbp, XFS_BUF_TO_SBP(bp));
	ASSERT(sbp->sb_magicnum == XFS_SB_MAGIC);
	ASSERT(xfs_sb_good_version(sbp));
	xfs_buf_relse(bp);

	/* We've re-read the superblock so re-initialize per-cpu counters */
	xfs_icsb_reinit_counters(log->l_mp);

	xlog_recover_check_summary(log);

	/* Normal transactions can now occur */
	log->l_flags &= ~XLOG_ACTIVE_RECOVERY;
	return 0;
}

/*
 * Perform recovery and re-initialize some log variables in xlog_find_tail.
 *
 * Return error or zero.
 */
int
xlog_recover(
	xlog_t		*log)
{
	xfs_daddr_t	head_blk, tail_blk;
	int		error;

	/* find the tail of the log */
	if ((error = xlog_find_tail(log, &head_blk, &tail_blk)))
		return error;

	if (tail_blk != head_blk) {
		/* There used to be a comment here:
		 *
		 * disallow recovery on read-only mounts.  note -- mount
		 * checks for ENOSPC and turns it into an intelligent
		 * error message.
		 * ...but this is no longer true.  Now, unless you specify
		 * NORECOVERY (in which case this function would never be
		 * called), we just go ahead and recover.  We do this all
		 * under the vfs layer, so we can get away with it unless
		 * the device itself is read-only, in which case we fail.
		 */
		if ((error = xfs_dev_is_read_only(log->l_mp, "recovery"))) {
			return error;
		}

		cmn_err(CE_NOTE,
			"Starting XFS recovery on filesystem: %s (logdev: %s)",
			log->l_mp->m_fsname, log->l_mp->m_logname ?
			log->l_mp->m_logname : "internal");

		error = xlog_do_recover(log, head_blk, tail_blk);
		log->l_flags |= XLOG_RECOVERY_NEEDED;
	}
	return error;
}

/*
 * In the first part of recovery we replay inodes and buffers and build
 * up the list of extent free items which need to be processed.  Here
 * we process the extent free items and clean up the on disk unlinked
 * inode lists.  This is separated from the first part of recovery so
 * that the root and real-time bitmap inodes can be read in from disk in
 * between the two stages.  This is necessary so that we can free space
 * in the real-time portion of the file system.
 */
int
xlog_recover_finish(
	xlog_t		*log)
{
	/*
	 * Now we're ready to do the transactions needed for the
	 * rest of recovery.  Start with completing all the extent
	 * free intent records and then process the unlinked inode
	 * lists.  At this point, we essentially run in normal mode
	 * except that we're still performing recovery actions
	 * rather than accepting new requests.
	 */
	if (log->l_flags & XLOG_RECOVERY_NEEDED) {
		int	error;
		error = xlog_recover_process_efis(log);
		if (error) {
			cmn_err(CE_ALERT,
				"Failed to recover EFIs on filesystem: %s",
				log->l_mp->m_fsname);
			return error;
		}
		/*
		 * Sync the log to get all the EFIs out of the AIL.
		 * This isn't absolutely necessary, but it helps in
		 * case the unlink transactions would have problems
		 * pushing the EFIs out of the way.
		 */
		xfs_log_force(log->l_mp, (xfs_lsn_t)0,
			      (XFS_LOG_FORCE | XFS_LOG_SYNC));

		xlog_recover_process_iunlinks(log);

		xlog_recover_check_summary(log);

		cmn_err(CE_NOTE,
			"Ending XFS recovery on filesystem: %s (logdev: %s)",
			log->l_mp->m_fsname, log->l_mp->m_logname ?
			log->l_mp->m_logname : "internal");
		log->l_flags &= ~XLOG_RECOVERY_NEEDED;
	} else {
		cmn_err(CE_DEBUG,
			"!Ending clean XFS mount for filesystem: %s\n",
			log->l_mp->m_fsname);
	}
	return 0;
}


#if defined(DEBUG)
/*
 * Read all of the agf and agi counters and check that they
 * are consistent with the superblock counters.
 */
void
xlog_recover_check_summary(
	xlog_t		*log)
{
	xfs_mount_t	*mp;
	xfs_agf_t	*agfp;
	xfs_buf_t	*agfbp;
	xfs_buf_t	*agibp;
	xfs_buf_t	*sbbp;
#ifdef XFS_LOUD_RECOVERY
	xfs_sb_t	*sbp;
#endif
	xfs_agnumber_t	agno;
	__uint64_t	freeblks;
	__uint64_t	itotal;
	__uint64_t	ifree;
	int		error;

	mp = log->l_mp;

	freeblks = 0LL;
	itotal = 0LL;
	ifree = 0LL;
	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		error = xfs_read_agf(mp, NULL, agno, 0, &agfbp);
		if (error) {
			xfs_fs_cmn_err(CE_ALERT, mp,
					"xlog_recover_check_summary(agf)"
					"agf read failed agno %d error %d",
							agno, error);
		} else {
			agfp = XFS_BUF_TO_AGF(agfbp);
			freeblks += be32_to_cpu(agfp->agf_freeblks) +
				    be32_to_cpu(agfp->agf_flcount);
			xfs_buf_relse(agfbp);
		}

		error = xfs_read_agi(mp, NULL, agno, &agibp);
		if (!error) {
			struct xfs_agi	*agi = XFS_BUF_TO_AGI(agibp);

			itotal += be32_to_cpu(agi->agi_count);
			ifree += be32_to_cpu(agi->agi_freecount);
			xfs_buf_relse(agibp);
		}
	}

	sbbp = xfs_getsb(mp, 0);
#ifdef XFS_LOUD_RECOVERY
	sbp = &mp->m_sb;
	xfs_sb_from_disk(sbp, XFS_BUF_TO_SBP(sbbp));
	cmn_err(CE_NOTE,
		"xlog_recover_check_summary: sb_icount %Lu itotal %Lu",
		sbp->sb_icount, itotal);
	cmn_err(CE_NOTE,
		"xlog_recover_check_summary: sb_ifree %Lu itotal %Lu",
		sbp->sb_ifree, ifree);
	cmn_err(CE_NOTE,
		"xlog_recover_check_summary: sb_fdblocks %Lu freeblks %Lu",
		sbp->sb_fdblocks, freeblks);
#if 0
	/*
	 * This is turned off until I account for the allocation
	 * btree blocks which live in free space.
	 */
	ASSERT(sbp->sb_icount == itotal);
	ASSERT(sbp->sb_ifree == ifree);
	ASSERT(sbp->sb_fdblocks == freeblks);
#endif
#endif
	xfs_buf_relse(sbbp);
}
#endif /* DEBUG */
