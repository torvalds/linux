// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_trans_priv.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_error.h"
#include "xfs_buf_item.h"
#include "xfs_ag.h"
#include "xfs_quota.h"


#define BLK_AVG(blk1, blk2)	((blk1+blk2) >> 1)

STATIC int
xlog_find_zeroed(
	struct xlog	*,
	xfs_daddr_t	*);
STATIC int
xlog_clear_stale_blocks(
	struct xlog	*,
	xfs_lsn_t);
#if defined(DEBUG)
STATIC void
xlog_recover_check_summary(
	struct xlog *);
#else
#define	xlog_recover_check_summary(log)
#endif
STATIC int
xlog_do_recovery_pass(
        struct xlog *, xfs_daddr_t, xfs_daddr_t, int, xfs_daddr_t *);

/*
 * Sector aligned buffer routines for buffer create/read/write/access
 */

/*
 * Verify the log-relative block number and length in basic blocks are valid for
 * an operation involving the given XFS log buffer. Returns true if the fields
 * are valid, false otherwise.
 */
static inline bool
xlog_verify_bno(
	struct xlog	*log,
	xfs_daddr_t	blk_no,
	int		bbcount)
{
	if (blk_no < 0 || blk_no >= log->l_logBBsize)
		return false;
	if (bbcount <= 0 || (blk_no + bbcount) > log->l_logBBsize)
		return false;
	return true;
}

/*
 * Allocate a buffer to hold log data.  The buffer needs to be able to map to
 * a range of nbblks basic blocks at any valid offset within the log.
 */
static char *
xlog_alloc_buffer(
	struct xlog	*log,
	int		nbblks)
{
	/*
	 * Pass log block 0 since we don't have an addr yet, buffer will be
	 * verified on read.
	 */
	if (XFS_IS_CORRUPT(log->l_mp, !xlog_verify_bno(log, 0, nbblks))) {
		xfs_warn(log->l_mp, "Invalid block length (0x%x) for buffer",
			nbblks);
		return NULL;
	}

	/*
	 * We do log I/O in units of log sectors (a power-of-2 multiple of the
	 * basic block size), so we round up the requested size to accommodate
	 * the basic blocks required for complete log sectors.
	 *
	 * In addition, the buffer may be used for a non-sector-aligned block
	 * offset, in which case an I/O of the requested size could extend
	 * beyond the end of the buffer.  If the requested size is only 1 basic
	 * block it will never straddle a sector boundary, so this won't be an
	 * issue.  Nor will this be a problem if the log I/O is done in basic
	 * blocks (sector size 1).  But otherwise we extend the buffer by one
	 * extra log sector to ensure there's space to accommodate this
	 * possibility.
	 */
	if (nbblks > 1 && log->l_sectBBsize > 1)
		nbblks += log->l_sectBBsize;
	nbblks = round_up(nbblks, log->l_sectBBsize);
	return kvzalloc(BBTOB(nbblks), GFP_KERNEL | __GFP_RETRY_MAYFAIL);
}

/*
 * Return the address of the start of the given block number's data
 * in a log buffer.  The buffer covers a log sector-aligned region.
 */
static inline unsigned int
xlog_align(
	struct xlog	*log,
	xfs_daddr_t	blk_no)
{
	return BBTOB(blk_no & ((xfs_daddr_t)log->l_sectBBsize - 1));
}

static int
xlog_do_io(
	struct xlog		*log,
	xfs_daddr_t		blk_no,
	unsigned int		nbblks,
	char			*data,
	unsigned int		op)
{
	int			error;

	if (XFS_IS_CORRUPT(log->l_mp, !xlog_verify_bno(log, blk_no, nbblks))) {
		xfs_warn(log->l_mp,
			 "Invalid log block/length (0x%llx, 0x%x) for buffer",
			 blk_no, nbblks);
		return -EFSCORRUPTED;
	}

	blk_no = round_down(blk_no, log->l_sectBBsize);
	nbblks = round_up(nbblks, log->l_sectBBsize);
	ASSERT(nbblks > 0);

	error = xfs_rw_bdev(log->l_targ->bt_bdev, log->l_logBBstart + blk_no,
			BBTOB(nbblks), data, op);
	if (error && !xlog_is_shutdown(log)) {
		xfs_alert(log->l_mp,
			  "log recovery %s I/O error at daddr 0x%llx len %d error %d",
			  op == REQ_OP_WRITE ? "write" : "read",
			  blk_no, nbblks, error);
	}
	return error;
}

STATIC int
xlog_bread_noalign(
	struct xlog	*log,
	xfs_daddr_t	blk_no,
	int		nbblks,
	char		*data)
{
	return xlog_do_io(log, blk_no, nbblks, data, REQ_OP_READ);
}

STATIC int
xlog_bread(
	struct xlog	*log,
	xfs_daddr_t	blk_no,
	int		nbblks,
	char		*data,
	char		**offset)
{
	int		error;

	error = xlog_do_io(log, blk_no, nbblks, data, REQ_OP_READ);
	if (!error)
		*offset = data + xlog_align(log, blk_no);
	return error;
}

STATIC int
xlog_bwrite(
	struct xlog	*log,
	xfs_daddr_t	blk_no,
	int		nbblks,
	char		*data)
{
	return xlog_do_io(log, blk_no, nbblks, data, REQ_OP_WRITE);
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
	xfs_debug(mp, "%s:  SB : uuid = %pU, fmt = %d",
		__func__, &mp->m_sb.sb_uuid, XLOG_FMT);
	xfs_debug(mp, "    log : uuid = %pU, fmt = %d",
		&head->h_fs_uuid, be32_to_cpu(head->h_fmt));
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
	ASSERT(head->h_magicno == cpu_to_be32(XLOG_HEADER_MAGIC_NUM));

	/*
	 * IRIX doesn't write the h_fmt field and leaves it zeroed
	 * (XLOG_FMT_UNKNOWN). This stops us from trying to recover
	 * a dirty log created in IRIX.
	 */
	if (XFS_IS_CORRUPT(mp, head->h_fmt != cpu_to_be32(XLOG_FMT))) {
		xfs_warn(mp,
	"dirty log written in incompatible format - can't recover");
		xlog_header_check_dump(mp, head);
		return -EFSCORRUPTED;
	}
	if (XFS_IS_CORRUPT(mp, !uuid_equal(&mp->m_sb.sb_uuid,
					   &head->h_fs_uuid))) {
		xfs_warn(mp,
	"dirty log entry has mismatched uuid - can't recover");
		xlog_header_check_dump(mp, head);
		return -EFSCORRUPTED;
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
	ASSERT(head->h_magicno == cpu_to_be32(XLOG_HEADER_MAGIC_NUM));

	if (uuid_is_null(&head->h_fs_uuid)) {
		/*
		 * IRIX doesn't write the h_fs_uuid or h_fmt fields. If
		 * h_fs_uuid is null, we assume this log was last mounted
		 * by IRIX and continue.
		 */
		xfs_warn(mp, "null uuid in log - IRIX style log");
	} else if (XFS_IS_CORRUPT(mp, !uuid_equal(&mp->m_sb.sb_uuid,
						  &head->h_fs_uuid))) {
		xfs_warn(mp, "log has mismatched uuid - can't recover");
		xlog_header_check_dump(mp, head);
		return -EFSCORRUPTED;
	}
	return 0;
}

/*
 * This routine finds (to an approximation) the first block in the physical
 * log which contains the given cycle.  It uses a binary search algorithm.
 * Note that the algorithm can not be perfect because the disk will not
 * necessarily be perfect.
 */
STATIC int
xlog_find_cycle_start(
	struct xlog	*log,
	char		*buffer,
	xfs_daddr_t	first_blk,
	xfs_daddr_t	*last_blk,
	uint		cycle)
{
	char		*offset;
	xfs_daddr_t	mid_blk;
	xfs_daddr_t	end_blk;
	uint		mid_cycle;
	int		error;

	end_blk = *last_blk;
	mid_blk = BLK_AVG(first_blk, end_blk);
	while (mid_blk != first_blk && mid_blk != end_blk) {
		error = xlog_bread(log, mid_blk, 1, buffer, &offset);
		if (error)
			return error;
		mid_cycle = xlog_get_cycle(offset);
		if (mid_cycle == cycle)
			end_blk = mid_blk;   /* last_half_cycle == mid_cycle */
		else
			first_blk = mid_blk; /* first_half_cycle == mid_cycle */
		mid_blk = BLK_AVG(first_blk, end_blk);
	}
	ASSERT((mid_blk == first_blk && mid_blk+1 == end_blk) ||
	       (mid_blk == end_blk && mid_blk-1 == first_blk));

	*last_blk = end_blk;

	return 0;
}

/*
 * Check that a range of blocks does not contain stop_on_cycle_no.
 * Fill in *new_blk with the block offset where such a block is
 * found, or with -1 (an invalid block number) if there is no such
 * block in the range.  The scan needs to occur from front to back
 * and the pointer into the region must be updated since a later
 * routine will need to perform another test.
 */
STATIC int
xlog_find_verify_cycle(
	struct xlog	*log,
	xfs_daddr_t	start_blk,
	int		nbblks,
	uint		stop_on_cycle_no,
	xfs_daddr_t	*new_blk)
{
	xfs_daddr_t	i, j;
	uint		cycle;
	char		*buffer;
	xfs_daddr_t	bufblks;
	char		*buf = NULL;
	int		error = 0;

	/*
	 * Greedily allocate a buffer big enough to handle the full
	 * range of basic blocks we'll be examining.  If that fails,
	 * try a smaller size.  We need to be able to read at least
	 * a log sector, or we're out of luck.
	 */
	bufblks = 1 << ffs(nbblks);
	while (bufblks > log->l_logBBsize)
		bufblks >>= 1;
	while (!(buffer = xlog_alloc_buffer(log, bufblks))) {
		bufblks >>= 1;
		if (bufblks < log->l_sectBBsize)
			return -ENOMEM;
	}

	for (i = start_blk; i < start_blk + nbblks; i += bufblks) {
		int	bcount;

		bcount = min(bufblks, (start_blk + nbblks - i));

		error = xlog_bread(log, i, bcount, buffer, &buf);
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
	kmem_free(buffer);
	return error;
}

static inline int
xlog_logrec_hblks(struct xlog *log, struct xlog_rec_header *rh)
{
	if (xfs_has_logv2(log->l_mp)) {
		int	h_size = be32_to_cpu(rh->h_size);

		if ((be32_to_cpu(rh->h_version) & XLOG_VERSION_2) &&
		    h_size > XLOG_HEADER_CYCLE_SIZE)
			return DIV_ROUND_UP(h_size, XLOG_HEADER_CYCLE_SIZE);
	}
	return 1;
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
	struct xlog		*log,
	xfs_daddr_t		start_blk,
	xfs_daddr_t		*last_blk,
	int			extra_bblks)
{
	xfs_daddr_t		i;
	char			*buffer;
	char			*offset = NULL;
	xlog_rec_header_t	*head = NULL;
	int			error = 0;
	int			smallmem = 0;
	int			num_blks = *last_blk - start_blk;
	int			xhdrs;

	ASSERT(start_blk != 0 || *last_blk != start_blk);

	buffer = xlog_alloc_buffer(log, num_blks);
	if (!buffer) {
		buffer = xlog_alloc_buffer(log, 1);
		if (!buffer)
			return -ENOMEM;
		smallmem = 1;
	} else {
		error = xlog_bread(log, start_blk, num_blks, buffer, &offset);
		if (error)
			goto out;
		offset += ((num_blks - 1) << BBSHIFT);
	}

	for (i = (*last_blk) - 1; i >= 0; i--) {
		if (i < start_blk) {
			/* valid log record not found */
			xfs_warn(log->l_mp,
		"Log inconsistent (didn't find previous header)");
			ASSERT(0);
			error = -EFSCORRUPTED;
			goto out;
		}

		if (smallmem) {
			error = xlog_bread(log, i, 1, buffer, &offset);
			if (error)
				goto out;
		}

		head = (xlog_rec_header_t *)offset;

		if (head->h_magicno == cpu_to_be32(XLOG_HEADER_MAGIC_NUM))
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
		error = 1;
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
	xhdrs = xlog_logrec_hblks(log, head);

	if (*last_blk - i + extra_bblks !=
	    BTOBB(be32_to_cpu(head->h_len)) + xhdrs)
		*last_blk = i;

out:
	kmem_free(buffer);
	return error;
}

/*
 * Head is defined to be the point of the log where the next log write
 * could go.  This means that incomplete LR writes at the end are
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
	struct xlog	*log,
	xfs_daddr_t	*return_head_blk)
{
	char		*buffer;
	char		*offset;
	xfs_daddr_t	new_blk, first_blk, start_blk, last_blk, head_blk;
	int		num_scan_bblks;
	uint		first_half_cycle, last_half_cycle;
	uint		stop_on_cycle;
	int		error, log_bbnum = log->l_logBBsize;

	/* Is the end of the log device zeroed? */
	error = xlog_find_zeroed(log, &first_blk);
	if (error < 0) {
		xfs_warn(log->l_mp, "empty log check failed");
		return error;
	}
	if (error == 1) {
		*return_head_blk = first_blk;

		/* Is the whole lot zeroed? */
		if (!first_blk) {
			/* Linux XFS shouldn't generate totally zeroed logs -
			 * mkfs etc write a dummy unmount record to a fresh
			 * log so we can store the uuid in there
			 */
			xfs_warn(log->l_mp, "totally zeroed log");
		}

		return 0;
	}

	first_blk = 0;			/* get cycle # of 1st block */
	buffer = xlog_alloc_buffer(log, 1);
	if (!buffer)
		return -ENOMEM;

	error = xlog_bread(log, 0, 1, buffer, &offset);
	if (error)
		goto out_free_buffer;

	first_half_cycle = xlog_get_cycle(offset);

	last_blk = head_blk = log_bbnum - 1;	/* get cycle # of last block */
	error = xlog_bread(log, last_blk, 1, buffer, &offset);
	if (error)
		goto out_free_buffer;

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
		 *        x + 1 ... | x ... | x
		 * The first block with cycle number x (last_half_cycle) will
		 * be where the new head belongs.  First we do a binary search
		 * for the first occurrence of last_half_cycle.  The binary
		 * search may not be totally accurate, so then we scan back
		 * from there looking for occurrences of last_half_cycle before
		 * us.  If that backwards scan wraps around the beginning of
		 * the log, then we look for occurrences of last_half_cycle - 1
		 * at the end of the log.  The cases we're looking for look
		 * like
		 *                               v binary search stopped here
		 *        x + 1 ... | x | x + 1 | x ... | x
		 *                   ^ but we want to locate this spot
		 * or
		 *        <---------> less than scan distance
		 *        x + 1 ... | x ... | x - 1 | x
		 *                           ^ we want to locate this spot
		 */
		stop_on_cycle = last_half_cycle;
		error = xlog_find_cycle_start(log, buffer, first_blk, &head_blk,
				last_half_cycle);
		if (error)
			goto out_free_buffer;
	}

	/*
	 * Now validate the answer.  Scan back some number of maximum possible
	 * blocks and make sure each one has the expected cycle number.  The
	 * maximum is determined by the total possible amount of buffering
	 * in the in-core log.  The following number can be made tighter if
	 * we actually look at the block size of the filesystem.
	 */
	num_scan_bblks = min_t(int, log_bbnum, XLOG_TOTAL_REC_SHIFT(log));
	if (head_blk >= num_scan_bblks) {
		/*
		 * We are guaranteed that the entire check can be performed
		 * in one buffer.
		 */
		start_blk = head_blk - num_scan_bblks;
		if ((error = xlog_find_verify_cycle(log,
						start_blk, num_scan_bblks,
						stop_on_cycle, &new_blk)))
			goto out_free_buffer;
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
		ASSERT(head_blk <= INT_MAX &&
			(xfs_daddr_t) num_scan_bblks >= head_blk);
		start_blk = log_bbnum - (num_scan_bblks - head_blk);
		if ((error = xlog_find_verify_cycle(log, start_blk,
					num_scan_bblks - (int)head_blk,
					(stop_on_cycle - 1), &new_blk)))
			goto out_free_buffer;
		if (new_blk != -1) {
			head_blk = new_blk;
			goto validate_head;
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
			goto out_free_buffer;
		if (new_blk != -1)
			head_blk = new_blk;
	}

validate_head:
	/*
	 * Now we need to make sure head_blk is not pointing to a block in
	 * the middle of a log record.
	 */
	num_scan_bblks = XLOG_REC_SHIFT(log);
	if (head_blk >= num_scan_bblks) {
		start_blk = head_blk - num_scan_bblks; /* don't read head_blk */

		/* start ptr at last block ptr before head_blk */
		error = xlog_find_verify_log_record(log, start_blk, &head_blk, 0);
		if (error == 1)
			error = -EIO;
		if (error)
			goto out_free_buffer;
	} else {
		start_blk = 0;
		ASSERT(head_blk <= INT_MAX);
		error = xlog_find_verify_log_record(log, start_blk, &head_blk, 0);
		if (error < 0)
			goto out_free_buffer;
		if (error == 1) {
			/* We hit the beginning of the log during our search */
			start_blk = log_bbnum - (num_scan_bblks - head_blk);
			new_blk = log_bbnum;
			ASSERT(start_blk <= INT_MAX &&
				(xfs_daddr_t) log_bbnum-start_blk >= 0);
			ASSERT(head_blk <= INT_MAX);
			error = xlog_find_verify_log_record(log, start_blk,
							&new_blk, (int)head_blk);
			if (error == 1)
				error = -EIO;
			if (error)
				goto out_free_buffer;
			if (new_blk != log_bbnum)
				head_blk = new_blk;
		} else if (error)
			goto out_free_buffer;
	}

	kmem_free(buffer);
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

out_free_buffer:
	kmem_free(buffer);
	if (error)
		xfs_warn(log->l_mp, "failed to find log head");
	return error;
}

/*
 * Seek backwards in the log for log record headers.
 *
 * Given a starting log block, walk backwards until we find the provided number
 * of records or hit the provided tail block. The return value is the number of
 * records encountered or a negative error code. The log block and buffer
 * pointer of the last record seen are returned in rblk and rhead respectively.
 */
STATIC int
xlog_rseek_logrec_hdr(
	struct xlog		*log,
	xfs_daddr_t		head_blk,
	xfs_daddr_t		tail_blk,
	int			count,
	char			*buffer,
	xfs_daddr_t		*rblk,
	struct xlog_rec_header	**rhead,
	bool			*wrapped)
{
	int			i;
	int			error;
	int			found = 0;
	char			*offset = NULL;
	xfs_daddr_t		end_blk;

	*wrapped = false;

	/*
	 * Walk backwards from the head block until we hit the tail or the first
	 * block in the log.
	 */
	end_blk = head_blk > tail_blk ? tail_blk : 0;
	for (i = (int) head_blk - 1; i >= end_blk; i--) {
		error = xlog_bread(log, i, 1, buffer, &offset);
		if (error)
			goto out_error;

		if (*(__be32 *) offset == cpu_to_be32(XLOG_HEADER_MAGIC_NUM)) {
			*rblk = i;
			*rhead = (struct xlog_rec_header *) offset;
			if (++found == count)
				break;
		}
	}

	/*
	 * If we haven't hit the tail block or the log record header count,
	 * start looking again from the end of the physical log. Note that
	 * callers can pass head == tail if the tail is not yet known.
	 */
	if (tail_blk >= head_blk && found != count) {
		for (i = log->l_logBBsize - 1; i >= (int) tail_blk; i--) {
			error = xlog_bread(log, i, 1, buffer, &offset);
			if (error)
				goto out_error;

			if (*(__be32 *)offset ==
			    cpu_to_be32(XLOG_HEADER_MAGIC_NUM)) {
				*wrapped = true;
				*rblk = i;
				*rhead = (struct xlog_rec_header *) offset;
				if (++found == count)
					break;
			}
		}
	}

	return found;

out_error:
	return error;
}

/*
 * Seek forward in the log for log record headers.
 *
 * Given head and tail blocks, walk forward from the tail block until we find
 * the provided number of records or hit the head block. The return value is the
 * number of records encountered or a negative error code. The log block and
 * buffer pointer of the last record seen are returned in rblk and rhead
 * respectively.
 */
STATIC int
xlog_seek_logrec_hdr(
	struct xlog		*log,
	xfs_daddr_t		head_blk,
	xfs_daddr_t		tail_blk,
	int			count,
	char			*buffer,
	xfs_daddr_t		*rblk,
	struct xlog_rec_header	**rhead,
	bool			*wrapped)
{
	int			i;
	int			error;
	int			found = 0;
	char			*offset = NULL;
	xfs_daddr_t		end_blk;

	*wrapped = false;

	/*
	 * Walk forward from the tail block until we hit the head or the last
	 * block in the log.
	 */
	end_blk = head_blk > tail_blk ? head_blk : log->l_logBBsize - 1;
	for (i = (int) tail_blk; i <= end_blk; i++) {
		error = xlog_bread(log, i, 1, buffer, &offset);
		if (error)
			goto out_error;

		if (*(__be32 *) offset == cpu_to_be32(XLOG_HEADER_MAGIC_NUM)) {
			*rblk = i;
			*rhead = (struct xlog_rec_header *) offset;
			if (++found == count)
				break;
		}
	}

	/*
	 * If we haven't hit the head block or the log record header count,
	 * start looking again from the start of the physical log.
	 */
	if (tail_blk > head_blk && found != count) {
		for (i = 0; i < (int) head_blk; i++) {
			error = xlog_bread(log, i, 1, buffer, &offset);
			if (error)
				goto out_error;

			if (*(__be32 *)offset ==
			    cpu_to_be32(XLOG_HEADER_MAGIC_NUM)) {
				*wrapped = true;
				*rblk = i;
				*rhead = (struct xlog_rec_header *) offset;
				if (++found == count)
					break;
			}
		}
	}

	return found;

out_error:
	return error;
}

/*
 * Calculate distance from head to tail (i.e., unused space in the log).
 */
static inline int
xlog_tail_distance(
	struct xlog	*log,
	xfs_daddr_t	head_blk,
	xfs_daddr_t	tail_blk)
{
	if (head_blk < tail_blk)
		return tail_blk - head_blk;

	return tail_blk + (log->l_logBBsize - head_blk);
}

/*
 * Verify the log tail. This is particularly important when torn or incomplete
 * writes have been detected near the front of the log and the head has been
 * walked back accordingly.
 *
 * We also have to handle the case where the tail was pinned and the head
 * blocked behind the tail right before a crash. If the tail had been pushed
 * immediately prior to the crash and the subsequent checkpoint was only
 * partially written, it's possible it overwrote the last referenced tail in the
 * log with garbage. This is not a coherency problem because the tail must have
 * been pushed before it can be overwritten, but appears as log corruption to
 * recovery because we have no way to know the tail was updated if the
 * subsequent checkpoint didn't write successfully.
 *
 * Therefore, CRC check the log from tail to head. If a failure occurs and the
 * offending record is within max iclog bufs from the head, walk the tail
 * forward and retry until a valid tail is found or corruption is detected out
 * of the range of a possible overwrite.
 */
STATIC int
xlog_verify_tail(
	struct xlog		*log,
	xfs_daddr_t		head_blk,
	xfs_daddr_t		*tail_blk,
	int			hsize)
{
	struct xlog_rec_header	*thead;
	char			*buffer;
	xfs_daddr_t		first_bad;
	int			error = 0;
	bool			wrapped;
	xfs_daddr_t		tmp_tail;
	xfs_daddr_t		orig_tail = *tail_blk;

	buffer = xlog_alloc_buffer(log, 1);
	if (!buffer)
		return -ENOMEM;

	/*
	 * Make sure the tail points to a record (returns positive count on
	 * success).
	 */
	error = xlog_seek_logrec_hdr(log, head_blk, *tail_blk, 1, buffer,
			&tmp_tail, &thead, &wrapped);
	if (error < 0)
		goto out;
	if (*tail_blk != tmp_tail)
		*tail_blk = tmp_tail;

	/*
	 * Run a CRC check from the tail to the head. We can't just check
	 * MAX_ICLOGS records past the tail because the tail may point to stale
	 * blocks cleared during the search for the head/tail. These blocks are
	 * overwritten with zero-length records and thus record count is not a
	 * reliable indicator of the iclog state before a crash.
	 */
	first_bad = 0;
	error = xlog_do_recovery_pass(log, head_blk, *tail_blk,
				      XLOG_RECOVER_CRCPASS, &first_bad);
	while ((error == -EFSBADCRC || error == -EFSCORRUPTED) && first_bad) {
		int	tail_distance;

		/*
		 * Is corruption within range of the head? If so, retry from
		 * the next record. Otherwise return an error.
		 */
		tail_distance = xlog_tail_distance(log, head_blk, first_bad);
		if (tail_distance > BTOBB(XLOG_MAX_ICLOGS * hsize))
			break;

		/* skip to the next record; returns positive count on success */
		error = xlog_seek_logrec_hdr(log, head_blk, first_bad, 2,
				buffer, &tmp_tail, &thead, &wrapped);
		if (error < 0)
			goto out;

		*tail_blk = tmp_tail;
		first_bad = 0;
		error = xlog_do_recovery_pass(log, head_blk, *tail_blk,
					      XLOG_RECOVER_CRCPASS, &first_bad);
	}

	if (!error && *tail_blk != orig_tail)
		xfs_warn(log->l_mp,
		"Tail block (0x%llx) overwrite detected. Updated to 0x%llx",
			 orig_tail, *tail_blk);
out:
	kmem_free(buffer);
	return error;
}

/*
 * Detect and trim torn writes from the head of the log.
 *
 * Storage without sector atomicity guarantees can result in torn writes in the
 * log in the event of a crash. Our only means to detect this scenario is via
 * CRC verification. While we can't always be certain that CRC verification
 * failure is due to a torn write vs. an unrelated corruption, we do know that
 * only a certain number (XLOG_MAX_ICLOGS) of log records can be written out at
 * one time. Therefore, CRC verify up to XLOG_MAX_ICLOGS records at the head of
 * the log and treat failures in this range as torn writes as a matter of
 * policy. In the event of CRC failure, the head is walked back to the last good
 * record in the log and the tail is updated from that record and verified.
 */
STATIC int
xlog_verify_head(
	struct xlog		*log,
	xfs_daddr_t		*head_blk,	/* in/out: unverified head */
	xfs_daddr_t		*tail_blk,	/* out: tail block */
	char			*buffer,
	xfs_daddr_t		*rhead_blk,	/* start blk of last record */
	struct xlog_rec_header	**rhead,	/* ptr to last record */
	bool			*wrapped)	/* last rec. wraps phys. log */
{
	struct xlog_rec_header	*tmp_rhead;
	char			*tmp_buffer;
	xfs_daddr_t		first_bad;
	xfs_daddr_t		tmp_rhead_blk;
	int			found;
	int			error;
	bool			tmp_wrapped;

	/*
	 * Check the head of the log for torn writes. Search backwards from the
	 * head until we hit the tail or the maximum number of log record I/Os
	 * that could have been in flight at one time. Use a temporary buffer so
	 * we don't trash the rhead/buffer pointers from the caller.
	 */
	tmp_buffer = xlog_alloc_buffer(log, 1);
	if (!tmp_buffer)
		return -ENOMEM;
	error = xlog_rseek_logrec_hdr(log, *head_blk, *tail_blk,
				      XLOG_MAX_ICLOGS, tmp_buffer,
				      &tmp_rhead_blk, &tmp_rhead, &tmp_wrapped);
	kmem_free(tmp_buffer);
	if (error < 0)
		return error;

	/*
	 * Now run a CRC verification pass over the records starting at the
	 * block found above to the current head. If a CRC failure occurs, the
	 * log block of the first bad record is saved in first_bad.
	 */
	error = xlog_do_recovery_pass(log, *head_blk, tmp_rhead_blk,
				      XLOG_RECOVER_CRCPASS, &first_bad);
	if ((error == -EFSBADCRC || error == -EFSCORRUPTED) && first_bad) {
		/*
		 * We've hit a potential torn write. Reset the error and warn
		 * about it.
		 */
		error = 0;
		xfs_warn(log->l_mp,
"Torn write (CRC failure) detected at log block 0x%llx. Truncating head block from 0x%llx.",
			 first_bad, *head_blk);

		/*
		 * Get the header block and buffer pointer for the last good
		 * record before the bad record.
		 *
		 * Note that xlog_find_tail() clears the blocks at the new head
		 * (i.e., the records with invalid CRC) if the cycle number
		 * matches the current cycle.
		 */
		found = xlog_rseek_logrec_hdr(log, first_bad, *tail_blk, 1,
				buffer, rhead_blk, rhead, wrapped);
		if (found < 0)
			return found;
		if (found == 0)		/* XXX: right thing to do here? */
			return -EIO;

		/*
		 * Reset the head block to the starting block of the first bad
		 * log record and set the tail block based on the last good
		 * record.
		 *
		 * Bail out if the updated head/tail match as this indicates
		 * possible corruption outside of the acceptable
		 * (XLOG_MAX_ICLOGS) range. This is a job for xfs_repair...
		 */
		*head_blk = first_bad;
		*tail_blk = BLOCK_LSN(be64_to_cpu((*rhead)->h_tail_lsn));
		if (*head_blk == *tail_blk) {
			ASSERT(0);
			return 0;
		}
	}
	if (error)
		return error;

	return xlog_verify_tail(log, *head_blk, tail_blk,
				be32_to_cpu((*rhead)->h_size));
}

/*
 * We need to make sure we handle log wrapping properly, so we can't use the
 * calculated logbno directly. Make sure it wraps to the correct bno inside the
 * log.
 *
 * The log is limited to 32 bit sizes, so we use the appropriate modulus
 * operation here and cast it back to a 64 bit daddr on return.
 */
static inline xfs_daddr_t
xlog_wrap_logbno(
	struct xlog		*log,
	xfs_daddr_t		bno)
{
	int			mod;

	div_s64_rem(bno, log->l_logBBsize, &mod);
	return mod;
}

/*
 * Check whether the head of the log points to an unmount record. In other
 * words, determine whether the log is clean. If so, update the in-core state
 * appropriately.
 */
static int
xlog_check_unmount_rec(
	struct xlog		*log,
	xfs_daddr_t		*head_blk,
	xfs_daddr_t		*tail_blk,
	struct xlog_rec_header	*rhead,
	xfs_daddr_t		rhead_blk,
	char			*buffer,
	bool			*clean)
{
	struct xlog_op_header	*op_head;
	xfs_daddr_t		umount_data_blk;
	xfs_daddr_t		after_umount_blk;
	int			hblks;
	int			error;
	char			*offset;

	*clean = false;

	/*
	 * Look for unmount record. If we find it, then we know there was a
	 * clean unmount. Since 'i' could be the last block in the physical
	 * log, we convert to a log block before comparing to the head_blk.
	 *
	 * Save the current tail lsn to use to pass to xlog_clear_stale_blocks()
	 * below. We won't want to clear the unmount record if there is one, so
	 * we pass the lsn of the unmount record rather than the block after it.
	 */
	hblks = xlog_logrec_hblks(log, rhead);
	after_umount_blk = xlog_wrap_logbno(log,
			rhead_blk + hblks + BTOBB(be32_to_cpu(rhead->h_len)));

	if (*head_blk == after_umount_blk &&
	    be32_to_cpu(rhead->h_num_logops) == 1) {
		umount_data_blk = xlog_wrap_logbno(log, rhead_blk + hblks);
		error = xlog_bread(log, umount_data_blk, 1, buffer, &offset);
		if (error)
			return error;

		op_head = (struct xlog_op_header *)offset;
		if (op_head->oh_flags & XLOG_UNMOUNT_TRANS) {
			/*
			 * Set tail and last sync so that newly written log
			 * records will point recovery to after the current
			 * unmount record.
			 */
			xlog_assign_atomic_lsn(&log->l_tail_lsn,
					log->l_curr_cycle, after_umount_blk);
			xlog_assign_atomic_lsn(&log->l_last_sync_lsn,
					log->l_curr_cycle, after_umount_blk);
			*tail_blk = after_umount_blk;

			*clean = true;
		}
	}

	return 0;
}

static void
xlog_set_state(
	struct xlog		*log,
	xfs_daddr_t		head_blk,
	struct xlog_rec_header	*rhead,
	xfs_daddr_t		rhead_blk,
	bool			bump_cycle)
{
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
	log->l_prev_block = rhead_blk;
	log->l_curr_block = (int)head_blk;
	log->l_curr_cycle = be32_to_cpu(rhead->h_cycle);
	if (bump_cycle)
		log->l_curr_cycle++;
	atomic64_set(&log->l_tail_lsn, be64_to_cpu(rhead->h_tail_lsn));
	atomic64_set(&log->l_last_sync_lsn, be64_to_cpu(rhead->h_lsn));
	xlog_assign_grant_head(&log->l_reserve_head.grant, log->l_curr_cycle,
					BBTOB(log->l_curr_block));
	xlog_assign_grant_head(&log->l_write_head.grant, log->l_curr_cycle,
					BBTOB(log->l_curr_block));
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
STATIC int
xlog_find_tail(
	struct xlog		*log,
	xfs_daddr_t		*head_blk,
	xfs_daddr_t		*tail_blk)
{
	xlog_rec_header_t	*rhead;
	char			*offset = NULL;
	char			*buffer;
	int			error;
	xfs_daddr_t		rhead_blk;
	xfs_lsn_t		tail_lsn;
	bool			wrapped = false;
	bool			clean = false;

	/*
	 * Find previous log record
	 */
	if ((error = xlog_find_head(log, head_blk)))
		return error;
	ASSERT(*head_blk < INT_MAX);

	buffer = xlog_alloc_buffer(log, 1);
	if (!buffer)
		return -ENOMEM;
	if (*head_blk == 0) {				/* special case */
		error = xlog_bread(log, 0, 1, buffer, &offset);
		if (error)
			goto done;

		if (xlog_get_cycle(offset) == 0) {
			*tail_blk = 0;
			/* leave all other log inited values alone */
			goto done;
		}
	}

	/*
	 * Search backwards through the log looking for the log record header
	 * block. This wraps all the way back around to the head so something is
	 * seriously wrong if we can't find it.
	 */
	error = xlog_rseek_logrec_hdr(log, *head_blk, *head_blk, 1, buffer,
				      &rhead_blk, &rhead, &wrapped);
	if (error < 0)
		goto done;
	if (!error) {
		xfs_warn(log->l_mp, "%s: couldn't find sync record", __func__);
		error = -EFSCORRUPTED;
		goto done;
	}
	*tail_blk = BLOCK_LSN(be64_to_cpu(rhead->h_tail_lsn));

	/*
	 * Set the log state based on the current head record.
	 */
	xlog_set_state(log, *head_blk, rhead, rhead_blk, wrapped);
	tail_lsn = atomic64_read(&log->l_tail_lsn);

	/*
	 * Look for an unmount record at the head of the log. This sets the log
	 * state to determine whether recovery is necessary.
	 */
	error = xlog_check_unmount_rec(log, head_blk, tail_blk, rhead,
				       rhead_blk, buffer, &clean);
	if (error)
		goto done;

	/*
	 * Verify the log head if the log is not clean (e.g., we have anything
	 * but an unmount record at the head). This uses CRC verification to
	 * detect and trim torn writes. If discovered, CRC failures are
	 * considered torn writes and the log head is trimmed accordingly.
	 *
	 * Note that we can only run CRC verification when the log is dirty
	 * because there's no guarantee that the log data behind an unmount
	 * record is compatible with the current architecture.
	 */
	if (!clean) {
		xfs_daddr_t	orig_head = *head_blk;

		error = xlog_verify_head(log, head_blk, tail_blk, buffer,
					 &rhead_blk, &rhead, &wrapped);
		if (error)
			goto done;

		/* update in-core state again if the head changed */
		if (*head_blk != orig_head) {
			xlog_set_state(log, *head_blk, rhead, rhead_blk,
				       wrapped);
			tail_lsn = atomic64_read(&log->l_tail_lsn);
			error = xlog_check_unmount_rec(log, head_blk, tail_blk,
						       rhead, rhead_blk, buffer,
						       &clean);
			if (error)
				goto done;
		}
	}

	/*
	 * Note that the unmount was clean. If the unmount was not clean, we
	 * need to know this to rebuild the superblock counters from the perag
	 * headers if we have a filesystem using non-persistent counters.
	 */
	if (clean)
		set_bit(XFS_OPSTATE_CLEAN, &log->l_mp->m_opstate);

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
	if (!xfs_readonly_buftarg(log->l_targ))
		error = xlog_clear_stale_blocks(log, tail_lsn);

done:
	kmem_free(buffer);

	if (error)
		xfs_warn(log->l_mp, "failed to locate log tail");
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
 *	1 => use *blk_no as the first block of the log
 *	<0 => error has occurred
 */
STATIC int
xlog_find_zeroed(
	struct xlog	*log,
	xfs_daddr_t	*blk_no)
{
	char		*buffer;
	char		*offset;
	uint	        first_cycle, last_cycle;
	xfs_daddr_t	new_blk, last_blk, start_blk;
	xfs_daddr_t     num_scan_bblks;
	int	        error, log_bbnum = log->l_logBBsize;

	*blk_no = 0;

	/* check totally zeroed log */
	buffer = xlog_alloc_buffer(log, 1);
	if (!buffer)
		return -ENOMEM;
	error = xlog_bread(log, 0, 1, buffer, &offset);
	if (error)
		goto out_free_buffer;

	first_cycle = xlog_get_cycle(offset);
	if (first_cycle == 0) {		/* completely zeroed log */
		*blk_no = 0;
		kmem_free(buffer);
		return 1;
	}

	/* check partially zeroed log */
	error = xlog_bread(log, log_bbnum-1, 1, buffer, &offset);
	if (error)
		goto out_free_buffer;

	last_cycle = xlog_get_cycle(offset);
	if (last_cycle != 0) {		/* log completely written to */
		kmem_free(buffer);
		return 0;
	}

	/* we have a partially zeroed log */
	last_blk = log_bbnum-1;
	error = xlog_find_cycle_start(log, buffer, 0, &last_blk, 0);
	if (error)
		goto out_free_buffer;

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
		goto out_free_buffer;
	if (new_blk != -1)
		last_blk = new_blk;

	/*
	 * Potentially backup over partial log record write.  We don't need
	 * to search the end of the log because we know it is zero.
	 */
	error = xlog_find_verify_log_record(log, start_blk, &last_blk, 0);
	if (error == 1)
		error = -EIO;
	if (error)
		goto out_free_buffer;

	*blk_no = last_blk;
out_free_buffer:
	kmem_free(buffer);
	if (error)
		return error;
	return 1;
}

/*
 * These are simple subroutines used by xlog_clear_stale_blocks() below
 * to initialize a buffer full of empty log record headers and write
 * them into the log.
 */
STATIC void
xlog_add_record(
	struct xlog		*log,
	char			*buf,
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
			xfs_has_logv2(log->l_mp) ? 2 : 1);
	recp->h_lsn = cpu_to_be64(xlog_assign_lsn(cycle, block));
	recp->h_tail_lsn = cpu_to_be64(xlog_assign_lsn(tail_cycle, tail_block));
	recp->h_fmt = cpu_to_be32(XLOG_FMT);
	memcpy(&recp->h_fs_uuid, &log->l_mp->m_sb.sb_uuid, sizeof(uuid_t));
}

STATIC int
xlog_write_log_records(
	struct xlog	*log,
	int		cycle,
	int		start_block,
	int		blocks,
	int		tail_cycle,
	int		tail_block)
{
	char		*offset;
	char		*buffer;
	int		balign, ealign;
	int		sectbb = log->l_sectBBsize;
	int		end_block = start_block + blocks;
	int		bufblks;
	int		error = 0;
	int		i, j = 0;

	/*
	 * Greedily allocate a buffer big enough to handle the full
	 * range of basic blocks to be written.  If that fails, try
	 * a smaller size.  We need to be able to write at least a
	 * log sector, or we're out of luck.
	 */
	bufblks = 1 << ffs(blocks);
	while (bufblks > log->l_logBBsize)
		bufblks >>= 1;
	while (!(buffer = xlog_alloc_buffer(log, bufblks))) {
		bufblks >>= 1;
		if (bufblks < sectbb)
			return -ENOMEM;
	}

	/* We may need to do a read at the start to fill in part of
	 * the buffer in the starting sector not covered by the first
	 * write below.
	 */
	balign = round_down(start_block, sectbb);
	if (balign != start_block) {
		error = xlog_bread_noalign(log, start_block, 1, buffer);
		if (error)
			goto out_free_buffer;

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
		ealign = round_down(end_block, sectbb);
		if (j == 0 && (start_block + endcount > ealign)) {
			error = xlog_bread_noalign(log, ealign, sectbb,
					buffer + BBTOB(ealign - start_block));
			if (error)
				break;

		}

		offset = buffer + xlog_align(log, start_block);
		for (; j < endcount; j++) {
			xlog_add_record(log, offset, cycle, i+j,
					tail_cycle, tail_block);
			offset += BBSIZE;
		}
		error = xlog_bwrite(log, start_block, endcount, buffer);
		if (error)
			break;
		start_block += endcount;
		j = 0;
	}

out_free_buffer:
	kmem_free(buffer);
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
	struct xlog	*log,
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
		if (XFS_IS_CORRUPT(log->l_mp,
				   head_block < tail_block ||
				   head_block >= log->l_logBBsize))
			return -EFSCORRUPTED;
		tail_distance = tail_block + (log->l_logBBsize - head_block);
	} else {
		/*
		 * The head is behind the tail in the physical log,
		 * so the distance from the head to the tail is just
		 * the tail block minus the head block.
		 */
		if (XFS_IS_CORRUPT(log->l_mp,
				   head_block >= tail_block ||
				   head_cycle != tail_cycle + 1))
			return -EFSCORRUPTED;
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
	max_distance = min(max_distance, tail_distance);

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

/*
 * Release the recovered intent item in the AIL that matches the given intent
 * type and intent id.
 */
void
xlog_recover_release_intent(
	struct xlog		*log,
	unsigned short		intent_type,
	uint64_t		intent_id)
{
	struct xfs_ail_cursor	cur;
	struct xfs_log_item	*lip;
	struct xfs_ail		*ailp = log->l_ailp;

	spin_lock(&ailp->ail_lock);
	for (lip = xfs_trans_ail_cursor_first(ailp, &cur, 0); lip != NULL;
	     lip = xfs_trans_ail_cursor_next(ailp, &cur)) {
		if (lip->li_type != intent_type)
			continue;
		if (!lip->li_ops->iop_match(lip, intent_id))
			continue;

		spin_unlock(&ailp->ail_lock);
		lip->li_ops->iop_release(lip);
		spin_lock(&ailp->ail_lock);
		break;
	}

	xfs_trans_ail_cursor_done(&cur);
	spin_unlock(&ailp->ail_lock);
}

int
xlog_recover_iget(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	struct xfs_inode	**ipp)
{
	int			error;

	error = xfs_iget(mp, NULL, ino, 0, 0, ipp);
	if (error)
		return error;

	error = xfs_qm_dqattach(*ipp);
	if (error) {
		xfs_irele(*ipp);
		return error;
	}

	if (VFS_I(*ipp)->i_nlink == 0)
		xfs_iflags_set(*ipp, XFS_IRECOVERY);

	return 0;
}

/******************************************************************************
 *
 *		Log recover routines
 *
 ******************************************************************************
 */
static const struct xlog_recover_item_ops *xlog_recover_item_ops[] = {
	&xlog_buf_item_ops,
	&xlog_inode_item_ops,
	&xlog_dquot_item_ops,
	&xlog_quotaoff_item_ops,
	&xlog_icreate_item_ops,
	&xlog_efi_item_ops,
	&xlog_efd_item_ops,
	&xlog_rui_item_ops,
	&xlog_rud_item_ops,
	&xlog_cui_item_ops,
	&xlog_cud_item_ops,
	&xlog_bui_item_ops,
	&xlog_bud_item_ops,
};

static const struct xlog_recover_item_ops *
xlog_find_item_ops(
	struct xlog_recover_item		*item)
{
	unsigned int				i;

	for (i = 0; i < ARRAY_SIZE(xlog_recover_item_ops); i++)
		if (ITEM_TYPE(item) == xlog_recover_item_ops[i]->item_type)
			return xlog_recover_item_ops[i];

	return NULL;
}

/*
 * Sort the log items in the transaction.
 *
 * The ordering constraints are defined by the inode allocation and unlink
 * behaviour. The rules are:
 *
 *	1. Every item is only logged once in a given transaction. Hence it
 *	   represents the last logged state of the item. Hence ordering is
 *	   dependent on the order in which operations need to be performed so
 *	   required initial conditions are always met.
 *
 *	2. Cancelled buffers are recorded in pass 1 in a separate table and
 *	   there's nothing to replay from them so we can simply cull them
 *	   from the transaction. However, we can't do that until after we've
 *	   replayed all the other items because they may be dependent on the
 *	   cancelled buffer and replaying the cancelled buffer can remove it
 *	   form the cancelled buffer table. Hence they have tobe done last.
 *
 *	3. Inode allocation buffers must be replayed before inode items that
 *	   read the buffer and replay changes into it. For filesystems using the
 *	   ICREATE transactions, this means XFS_LI_ICREATE objects need to get
 *	   treated the same as inode allocation buffers as they create and
 *	   initialise the buffers directly.
 *
 *	4. Inode unlink buffers must be replayed after inode items are replayed.
 *	   This ensures that inodes are completely flushed to the inode buffer
 *	   in a "free" state before we remove the unlinked inode list pointer.
 *
 * Hence the ordering needs to be inode allocation buffers first, inode items
 * second, inode unlink buffers third and cancelled buffers last.
 *
 * But there's a problem with that - we can't tell an inode allocation buffer
 * apart from a regular buffer, so we can't separate them. We can, however,
 * tell an inode unlink buffer from the others, and so we can separate them out
 * from all the other buffers and move them to last.
 *
 * Hence, 4 lists, in order from head to tail:
 *	- buffer_list for all buffers except cancelled/inode unlink buffers
 *	- item_list for all non-buffer items
 *	- inode_buffer_list for inode unlink buffers
 *	- cancel_list for the cancelled buffers
 *
 * Note that we add objects to the tail of the lists so that first-to-last
 * ordering is preserved within the lists. Adding objects to the head of the
 * list means when we traverse from the head we walk them in last-to-first
 * order. For cancelled buffers and inode unlink buffers this doesn't matter,
 * but for all other items there may be specific ordering that we need to
 * preserve.
 */
STATIC int
xlog_recover_reorder_trans(
	struct xlog		*log,
	struct xlog_recover	*trans,
	int			pass)
{
	struct xlog_recover_item *item, *n;
	int			error = 0;
	LIST_HEAD(sort_list);
	LIST_HEAD(cancel_list);
	LIST_HEAD(buffer_list);
	LIST_HEAD(inode_buffer_list);
	LIST_HEAD(item_list);

	list_splice_init(&trans->r_itemq, &sort_list);
	list_for_each_entry_safe(item, n, &sort_list, ri_list) {
		enum xlog_recover_reorder	fate = XLOG_REORDER_ITEM_LIST;

		item->ri_ops = xlog_find_item_ops(item);
		if (!item->ri_ops) {
			xfs_warn(log->l_mp,
				"%s: unrecognized type of log operation (%d)",
				__func__, ITEM_TYPE(item));
			ASSERT(0);
			/*
			 * return the remaining items back to the transaction
			 * item list so they can be freed in caller.
			 */
			if (!list_empty(&sort_list))
				list_splice_init(&sort_list, &trans->r_itemq);
			error = -EFSCORRUPTED;
			break;
		}

		if (item->ri_ops->reorder)
			fate = item->ri_ops->reorder(item);

		switch (fate) {
		case XLOG_REORDER_BUFFER_LIST:
			list_move_tail(&item->ri_list, &buffer_list);
			break;
		case XLOG_REORDER_CANCEL_LIST:
			trace_xfs_log_recover_item_reorder_head(log,
					trans, item, pass);
			list_move(&item->ri_list, &cancel_list);
			break;
		case XLOG_REORDER_INODE_BUFFER_LIST:
			list_move(&item->ri_list, &inode_buffer_list);
			break;
		case XLOG_REORDER_ITEM_LIST:
			trace_xfs_log_recover_item_reorder_tail(log,
							trans, item, pass);
			list_move_tail(&item->ri_list, &item_list);
			break;
		}
	}

	ASSERT(list_empty(&sort_list));
	if (!list_empty(&buffer_list))
		list_splice(&buffer_list, &trans->r_itemq);
	if (!list_empty(&item_list))
		list_splice_tail(&item_list, &trans->r_itemq);
	if (!list_empty(&inode_buffer_list))
		list_splice_tail(&inode_buffer_list, &trans->r_itemq);
	if (!list_empty(&cancel_list))
		list_splice_tail(&cancel_list, &trans->r_itemq);
	return error;
}

void
xlog_buf_readahead(
	struct xlog		*log,
	xfs_daddr_t		blkno,
	uint			len,
	const struct xfs_buf_ops *ops)
{
	if (!xlog_is_buffer_cancelled(log, blkno, len))
		xfs_buf_readahead(log->l_mp->m_ddev_targp, blkno, len, ops);
}

STATIC int
xlog_recover_items_pass2(
	struct xlog                     *log,
	struct xlog_recover             *trans,
	struct list_head                *buffer_list,
	struct list_head                *item_list)
{
	struct xlog_recover_item	*item;
	int				error = 0;

	list_for_each_entry(item, item_list, ri_list) {
		trace_xfs_log_recover_item_recover(log, trans, item,
				XLOG_RECOVER_PASS2);

		if (item->ri_ops->commit_pass2)
			error = item->ri_ops->commit_pass2(log, buffer_list,
					item, trans->r_lsn);
		if (error)
			return error;
	}

	return error;
}

/*
 * Perform the transaction.
 *
 * If the transaction modifies a buffer or inode, do it now.  Otherwise,
 * EFIs and EFDs get queued up by adding entries into the AIL for them.
 */
STATIC int
xlog_recover_commit_trans(
	struct xlog		*log,
	struct xlog_recover	*trans,
	int			pass,
	struct list_head	*buffer_list)
{
	int				error = 0;
	int				items_queued = 0;
	struct xlog_recover_item	*item;
	struct xlog_recover_item	*next;
	LIST_HEAD			(ra_list);
	LIST_HEAD			(done_list);

	#define XLOG_RECOVER_COMMIT_QUEUE_MAX 100

	hlist_del_init(&trans->r_list);

	error = xlog_recover_reorder_trans(log, trans, pass);
	if (error)
		return error;

	list_for_each_entry_safe(item, next, &trans->r_itemq, ri_list) {
		trace_xfs_log_recover_item_recover(log, trans, item, pass);

		switch (pass) {
		case XLOG_RECOVER_PASS1:
			if (item->ri_ops->commit_pass1)
				error = item->ri_ops->commit_pass1(log, item);
			break;
		case XLOG_RECOVER_PASS2:
			if (item->ri_ops->ra_pass2)
				item->ri_ops->ra_pass2(log, item);
			list_move_tail(&item->ri_list, &ra_list);
			items_queued++;
			if (items_queued >= XLOG_RECOVER_COMMIT_QUEUE_MAX) {
				error = xlog_recover_items_pass2(log, trans,
						buffer_list, &ra_list);
				list_splice_tail_init(&ra_list, &done_list);
				items_queued = 0;
			}

			break;
		default:
			ASSERT(0);
		}

		if (error)
			goto out;
	}

out:
	if (!list_empty(&ra_list)) {
		if (!error)
			error = xlog_recover_items_pass2(log, trans,
					buffer_list, &ra_list);
		list_splice_tail_init(&ra_list, &done_list);
	}

	if (!list_empty(&done_list))
		list_splice_init(&done_list, &trans->r_itemq);

	return error;
}

STATIC void
xlog_recover_add_item(
	struct list_head	*head)
{
	struct xlog_recover_item *item;

	item = kmem_zalloc(sizeof(struct xlog_recover_item), 0);
	INIT_LIST_HEAD(&item->ri_list);
	list_add_tail(&item->ri_list, head);
}

STATIC int
xlog_recover_add_to_cont_trans(
	struct xlog		*log,
	struct xlog_recover	*trans,
	char			*dp,
	int			len)
{
	struct xlog_recover_item *item;
	char			*ptr, *old_ptr;
	int			old_len;

	/*
	 * If the transaction is empty, the header was split across this and the
	 * previous record. Copy the rest of the header.
	 */
	if (list_empty(&trans->r_itemq)) {
		ASSERT(len <= sizeof(struct xfs_trans_header));
		if (len > sizeof(struct xfs_trans_header)) {
			xfs_warn(log->l_mp, "%s: bad header length", __func__);
			return -EFSCORRUPTED;
		}

		xlog_recover_add_item(&trans->r_itemq);
		ptr = (char *)&trans->r_theader +
				sizeof(struct xfs_trans_header) - len;
		memcpy(ptr, dp, len);
		return 0;
	}

	/* take the tail entry */
	item = list_entry(trans->r_itemq.prev, struct xlog_recover_item,
			  ri_list);

	old_ptr = item->ri_buf[item->ri_cnt-1].i_addr;
	old_len = item->ri_buf[item->ri_cnt-1].i_len;

	ptr = kvrealloc(old_ptr, old_len, len + old_len, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;
	memcpy(&ptr[old_len], dp, len);
	item->ri_buf[item->ri_cnt-1].i_len += len;
	item->ri_buf[item->ri_cnt-1].i_addr = ptr;
	trace_xfs_log_recover_item_add_cont(log, trans, item, 0);
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
	struct xlog		*log,
	struct xlog_recover	*trans,
	char			*dp,
	int			len)
{
	struct xfs_inode_log_format	*in_f;			/* any will do */
	struct xlog_recover_item *item;
	char			*ptr;

	if (!len)
		return 0;
	if (list_empty(&trans->r_itemq)) {
		/* we need to catch log corruptions here */
		if (*(uint *)dp != XFS_TRANS_HEADER_MAGIC) {
			xfs_warn(log->l_mp, "%s: bad header magic number",
				__func__);
			ASSERT(0);
			return -EFSCORRUPTED;
		}

		if (len > sizeof(struct xfs_trans_header)) {
			xfs_warn(log->l_mp, "%s: bad header length", __func__);
			ASSERT(0);
			return -EFSCORRUPTED;
		}

		/*
		 * The transaction header can be arbitrarily split across op
		 * records. If we don't have the whole thing here, copy what we
		 * do have and handle the rest in the next record.
		 */
		if (len == sizeof(struct xfs_trans_header))
			xlog_recover_add_item(&trans->r_itemq);
		memcpy(&trans->r_theader, dp, len);
		return 0;
	}

	ptr = kmem_alloc(len, 0);
	memcpy(ptr, dp, len);
	in_f = (struct xfs_inode_log_format *)ptr;

	/* take the tail entry */
	item = list_entry(trans->r_itemq.prev, struct xlog_recover_item,
			  ri_list);
	if (item->ri_total != 0 &&
	     item->ri_total == item->ri_cnt) {
		/* tail item is in use, get a new one */
		xlog_recover_add_item(&trans->r_itemq);
		item = list_entry(trans->r_itemq.prev,
					struct xlog_recover_item, ri_list);
	}

	if (item->ri_total == 0) {		/* first region to be added */
		if (in_f->ilf_size == 0 ||
		    in_f->ilf_size > XLOG_MAX_REGIONS_IN_ITEM) {
			xfs_warn(log->l_mp,
		"bad number of regions (%d) in inode log format",
				  in_f->ilf_size);
			ASSERT(0);
			kmem_free(ptr);
			return -EFSCORRUPTED;
		}

		item->ri_total = in_f->ilf_size;
		item->ri_buf =
			kmem_zalloc(item->ri_total * sizeof(xfs_log_iovec_t),
				    0);
	}

	if (item->ri_total <= item->ri_cnt) {
		xfs_warn(log->l_mp,
	"log item region count (%d) overflowed size (%d)",
				item->ri_cnt, item->ri_total);
		ASSERT(0);
		kmem_free(ptr);
		return -EFSCORRUPTED;
	}

	/* Description region is ri_buf[0] */
	item->ri_buf[item->ri_cnt].i_addr = ptr;
	item->ri_buf[item->ri_cnt].i_len  = len;
	item->ri_cnt++;
	trace_xfs_log_recover_item_add(log, trans, item, 0);
	return 0;
}

/*
 * Free up any resources allocated by the transaction
 *
 * Remember that EFIs, EFDs, and IUNLINKs are handled later.
 */
STATIC void
xlog_recover_free_trans(
	struct xlog_recover	*trans)
{
	struct xlog_recover_item *item, *n;
	int			i;

	hlist_del_init(&trans->r_list);

	list_for_each_entry_safe(item, n, &trans->r_itemq, ri_list) {
		/* Free the regions in the item. */
		list_del(&item->ri_list);
		for (i = 0; i < item->ri_cnt; i++)
			kmem_free(item->ri_buf[i].i_addr);
		/* Free the item itself */
		kmem_free(item->ri_buf);
		kmem_free(item);
	}
	/* Free the transaction recover structure */
	kmem_free(trans);
}

/*
 * On error or completion, trans is freed.
 */
STATIC int
xlog_recovery_process_trans(
	struct xlog		*log,
	struct xlog_recover	*trans,
	char			*dp,
	unsigned int		len,
	unsigned int		flags,
	int			pass,
	struct list_head	*buffer_list)
{
	int			error = 0;
	bool			freeit = false;

	/* mask off ophdr transaction container flags */
	flags &= ~XLOG_END_TRANS;
	if (flags & XLOG_WAS_CONT_TRANS)
		flags &= ~XLOG_CONTINUE_TRANS;

	/*
	 * Callees must not free the trans structure. We'll decide if we need to
	 * free it or not based on the operation being done and it's result.
	 */
	switch (flags) {
	/* expected flag values */
	case 0:
	case XLOG_CONTINUE_TRANS:
		error = xlog_recover_add_to_trans(log, trans, dp, len);
		break;
	case XLOG_WAS_CONT_TRANS:
		error = xlog_recover_add_to_cont_trans(log, trans, dp, len);
		break;
	case XLOG_COMMIT_TRANS:
		error = xlog_recover_commit_trans(log, trans, pass,
						  buffer_list);
		/* success or fail, we are now done with this transaction. */
		freeit = true;
		break;

	/* unexpected flag values */
	case XLOG_UNMOUNT_TRANS:
		/* just skip trans */
		xfs_warn(log->l_mp, "%s: Unmount LR", __func__);
		freeit = true;
		break;
	case XLOG_START_TRANS:
	default:
		xfs_warn(log->l_mp, "%s: bad flag 0x%x", __func__, flags);
		ASSERT(0);
		error = -EFSCORRUPTED;
		break;
	}
	if (error || freeit)
		xlog_recover_free_trans(trans);
	return error;
}

/*
 * Lookup the transaction recovery structure associated with the ID in the
 * current ophdr. If the transaction doesn't exist and the start flag is set in
 * the ophdr, then allocate a new transaction for future ID matches to find.
 * Either way, return what we found during the lookup - an existing transaction
 * or nothing.
 */
STATIC struct xlog_recover *
xlog_recover_ophdr_to_trans(
	struct hlist_head	rhash[],
	struct xlog_rec_header	*rhead,
	struct xlog_op_header	*ohead)
{
	struct xlog_recover	*trans;
	xlog_tid_t		tid;
	struct hlist_head	*rhp;

	tid = be32_to_cpu(ohead->oh_tid);
	rhp = &rhash[XLOG_RHASH(tid)];
	hlist_for_each_entry(trans, rhp, r_list) {
		if (trans->r_log_tid == tid)
			return trans;
	}

	/*
	 * skip over non-start transaction headers - we could be
	 * processing slack space before the next transaction starts
	 */
	if (!(ohead->oh_flags & XLOG_START_TRANS))
		return NULL;

	ASSERT(be32_to_cpu(ohead->oh_len) == 0);

	/*
	 * This is a new transaction so allocate a new recovery container to
	 * hold the recovery ops that will follow.
	 */
	trans = kmem_zalloc(sizeof(struct xlog_recover), 0);
	trans->r_log_tid = tid;
	trans->r_lsn = be64_to_cpu(rhead->h_lsn);
	INIT_LIST_HEAD(&trans->r_itemq);
	INIT_HLIST_NODE(&trans->r_list);
	hlist_add_head(&trans->r_list, rhp);

	/*
	 * Nothing more to do for this ophdr. Items to be added to this new
	 * transaction will be in subsequent ophdr containers.
	 */
	return NULL;
}

STATIC int
xlog_recover_process_ophdr(
	struct xlog		*log,
	struct hlist_head	rhash[],
	struct xlog_rec_header	*rhead,
	struct xlog_op_header	*ohead,
	char			*dp,
	char			*end,
	int			pass,
	struct list_head	*buffer_list)
{
	struct xlog_recover	*trans;
	unsigned int		len;
	int			error;

	/* Do we understand who wrote this op? */
	if (ohead->oh_clientid != XFS_TRANSACTION &&
	    ohead->oh_clientid != XFS_LOG) {
		xfs_warn(log->l_mp, "%s: bad clientid 0x%x",
			__func__, ohead->oh_clientid);
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	/*
	 * Check the ophdr contains all the data it is supposed to contain.
	 */
	len = be32_to_cpu(ohead->oh_len);
	if (dp + len > end) {
		xfs_warn(log->l_mp, "%s: bad length 0x%x", __func__, len);
		WARN_ON(1);
		return -EFSCORRUPTED;
	}

	trans = xlog_recover_ophdr_to_trans(rhash, rhead, ohead);
	if (!trans) {
		/* nothing to do, so skip over this ophdr */
		return 0;
	}

	/*
	 * The recovered buffer queue is drained only once we know that all
	 * recovery items for the current LSN have been processed. This is
	 * required because:
	 *
	 * - Buffer write submission updates the metadata LSN of the buffer.
	 * - Log recovery skips items with a metadata LSN >= the current LSN of
	 *   the recovery item.
	 * - Separate recovery items against the same metadata buffer can share
	 *   a current LSN. I.e., consider that the LSN of a recovery item is
	 *   defined as the starting LSN of the first record in which its
	 *   transaction appears, that a record can hold multiple transactions,
	 *   and/or that a transaction can span multiple records.
	 *
	 * In other words, we are allowed to submit a buffer from log recovery
	 * once per current LSN. Otherwise, we may incorrectly skip recovery
	 * items and cause corruption.
	 *
	 * We don't know up front whether buffers are updated multiple times per
	 * LSN. Therefore, track the current LSN of each commit log record as it
	 * is processed and drain the queue when it changes. Use commit records
	 * because they are ordered correctly by the logging code.
	 */
	if (log->l_recovery_lsn != trans->r_lsn &&
	    ohead->oh_flags & XLOG_COMMIT_TRANS) {
		error = xfs_buf_delwri_submit(buffer_list);
		if (error)
			return error;
		log->l_recovery_lsn = trans->r_lsn;
	}

	return xlog_recovery_process_trans(log, trans, dp, len,
					   ohead->oh_flags, pass, buffer_list);
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
	struct xlog		*log,
	struct hlist_head	rhash[],
	struct xlog_rec_header	*rhead,
	char			*dp,
	int			pass,
	struct list_head	*buffer_list)
{
	struct xlog_op_header	*ohead;
	char			*end;
	int			num_logops;
	int			error;

	end = dp + be32_to_cpu(rhead->h_len);
	num_logops = be32_to_cpu(rhead->h_num_logops);

	/* check the log format matches our own - else we can't recover */
	if (xlog_header_check_recover(log->l_mp, rhead))
		return -EIO;

	trace_xfs_log_recover_record(log, rhead, pass);
	while ((dp < end) && num_logops) {

		ohead = (struct xlog_op_header *)dp;
		dp += sizeof(*ohead);
		ASSERT(dp <= end);

		/* errors will abort recovery */
		error = xlog_recover_process_ophdr(log, rhash, rhead, ohead,
						   dp, end, pass, buffer_list);
		if (error)
			return error;

		dp += be32_to_cpu(ohead->oh_len);
		num_logops--;
	}
	return 0;
}

/* Take all the collected deferred ops and finish them in order. */
static int
xlog_finish_defer_ops(
	struct xfs_mount	*mp,
	struct list_head	*capture_list)
{
	struct xfs_defer_capture *dfc, *next;
	struct xfs_trans	*tp;
	struct xfs_inode	*ip;
	int			error = 0;

	list_for_each_entry_safe(dfc, next, capture_list, dfc_list) {
		struct xfs_trans_res	resv;

		/*
		 * Create a new transaction reservation from the captured
		 * information.  Set logcount to 1 to force the new transaction
		 * to regrant every roll so that we can make forward progress
		 * in recovery no matter how full the log might be.
		 */
		resv.tr_logres = dfc->dfc_logres;
		resv.tr_logcount = 1;
		resv.tr_logflags = XFS_TRANS_PERM_LOG_RES;

		error = xfs_trans_alloc(mp, &resv, dfc->dfc_blkres,
				dfc->dfc_rtxres, XFS_TRANS_RESERVE, &tp);
		if (error) {
			xfs_force_shutdown(mp, SHUTDOWN_LOG_IO_ERROR);
			return error;
		}

		/*
		 * Transfer to this new transaction all the dfops we captured
		 * from recovering a single intent item.
		 */
		list_del_init(&dfc->dfc_list);
		xfs_defer_ops_continue(dfc, tp, &ip);

		error = xfs_trans_commit(tp);
		if (ip) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			xfs_irele(ip);
		}
		if (error)
			return error;
	}

	ASSERT(list_empty(capture_list));
	return 0;
}

/* Release all the captured defer ops and capture structures in this list. */
static void
xlog_abort_defer_ops(
	struct xfs_mount		*mp,
	struct list_head		*capture_list)
{
	struct xfs_defer_capture	*dfc;
	struct xfs_defer_capture	*next;

	list_for_each_entry_safe(dfc, next, capture_list, dfc_list) {
		list_del_init(&dfc->dfc_list);
		xfs_defer_ops_release(mp, dfc);
	}
}
/*
 * When this is called, all of the log intent items which did not have
 * corresponding log done items should be in the AIL.  What we do now
 * is update the data structures associated with each one.
 *
 * Since we process the log intent items in normal transactions, they
 * will be removed at some point after the commit.  This prevents us
 * from just walking down the list processing each one.  We'll use a
 * flag in the intent item to skip those that we've already processed
 * and use the AIL iteration mechanism's generation count to try to
 * speed this up at least a bit.
 *
 * When we start, we know that the intents are the only things in the
 * AIL.  As we process them, however, other items are added to the
 * AIL.
 */
STATIC int
xlog_recover_process_intents(
	struct xlog		*log)
{
	LIST_HEAD(capture_list);
	struct xfs_ail_cursor	cur;
	struct xfs_log_item	*lip;
	struct xfs_ail		*ailp;
	int			error = 0;
#if defined(DEBUG) || defined(XFS_WARN)
	xfs_lsn_t		last_lsn;
#endif

	ailp = log->l_ailp;
	spin_lock(&ailp->ail_lock);
#if defined(DEBUG) || defined(XFS_WARN)
	last_lsn = xlog_assign_lsn(log->l_curr_cycle, log->l_curr_block);
#endif
	for (lip = xfs_trans_ail_cursor_first(ailp, &cur, 0);
	     lip != NULL;
	     lip = xfs_trans_ail_cursor_next(ailp, &cur)) {
		/*
		 * We're done when we see something other than an intent.
		 * There should be no intents left in the AIL now.
		 */
		if (!xlog_item_is_intent(lip)) {
#ifdef DEBUG
			for (; lip; lip = xfs_trans_ail_cursor_next(ailp, &cur))
				ASSERT(!xlog_item_is_intent(lip));
#endif
			break;
		}

		/*
		 * We should never see a redo item with a LSN higher than
		 * the last transaction we found in the log at the start
		 * of recovery.
		 */
		ASSERT(XFS_LSN_CMP(last_lsn, lip->li_lsn) >= 0);

		/*
		 * NOTE: If your intent processing routine can create more
		 * deferred ops, you /must/ attach them to the capture list in
		 * the recover routine or else those subsequent intents will be
		 * replayed in the wrong order!
		 */
		spin_unlock(&ailp->ail_lock);
		error = lip->li_ops->iop_recover(lip, &capture_list);
		spin_lock(&ailp->ail_lock);
		if (error) {
			trace_xlog_intent_recovery_failed(log->l_mp, error,
					lip->li_ops->iop_recover);
			break;
		}
	}

	xfs_trans_ail_cursor_done(&cur);
	spin_unlock(&ailp->ail_lock);
	if (error)
		goto err;

	error = xlog_finish_defer_ops(log->l_mp, &capture_list);
	if (error)
		goto err;

	return 0;
err:
	xlog_abort_defer_ops(log->l_mp, &capture_list);
	return error;
}

/*
 * A cancel occurs when the mount has failed and we're bailing out.
 * Release all pending log intent items so they don't pin the AIL.
 */
STATIC void
xlog_recover_cancel_intents(
	struct xlog		*log)
{
	struct xfs_log_item	*lip;
	struct xfs_ail_cursor	cur;
	struct xfs_ail		*ailp;

	ailp = log->l_ailp;
	spin_lock(&ailp->ail_lock);
	lip = xfs_trans_ail_cursor_first(ailp, &cur, 0);
	while (lip != NULL) {
		/*
		 * We're done when we see something other than an intent.
		 * There should be no intents left in the AIL now.
		 */
		if (!xlog_item_is_intent(lip)) {
#ifdef DEBUG
			for (; lip; lip = xfs_trans_ail_cursor_next(ailp, &cur))
				ASSERT(!xlog_item_is_intent(lip));
#endif
			break;
		}

		spin_unlock(&ailp->ail_lock);
		lip->li_ops->iop_release(lip);
		spin_lock(&ailp->ail_lock);
		lip = xfs_trans_ail_cursor_next(ailp, &cur);
	}

	xfs_trans_ail_cursor_done(&cur);
	spin_unlock(&ailp->ail_lock);
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
	struct xfs_buf	*agibp;
	int		offset;
	int		error;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_clearagi, 0, 0, 0, &tp);
	if (error)
		goto out_error;

	error = xfs_read_agi(mp, tp, agno, &agibp);
	if (error)
		goto out_abort;

	agi = agibp->b_addr;
	agi->agi_unlinked[bucket] = cpu_to_be32(NULLAGINO);
	offset = offsetof(xfs_agi_t, agi_unlinked) +
		 (sizeof(xfs_agino_t) * bucket);
	xfs_trans_log_buf(tp, agibp, offset,
			  (offset + sizeof(xfs_agino_t) - 1));

	error = xfs_trans_commit(tp);
	if (error)
		goto out_error;
	return;

out_abort:
	xfs_trans_cancel(tp);
out_error:
	xfs_warn(mp, "%s: failed to clear agi %d. Continuing.", __func__, agno);
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
	error = xfs_iget(mp, NULL, ino, 0, 0, &ip);
	if (error)
		goto fail;

	/*
	 * Get the on disk inode to find the next inode in the bucket.
	 */
	error = xfs_imap_to_bp(mp, NULL, &ip->i_imap, &ibp);
	if (error)
		goto fail_iput;
	dip = xfs_buf_offset(ibp, ip->i_imap.im_boffset);

	xfs_iflags_clear(ip, XFS_IRECOVERY);
	ASSERT(VFS_I(ip)->i_nlink == 0);
	ASSERT(VFS_I(ip)->i_mode != 0);

	/* setup for the next pass */
	agino = be32_to_cpu(dip->di_next_unlinked);
	xfs_buf_relse(ibp);

	xfs_irele(ip);
	return agino;

 fail_iput:
	xfs_irele(ip);
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
 * Recover AGI unlinked lists
 *
 * This is called during recovery to process any inodes which we unlinked but
 * not freed when the system crashed.  These inodes will be on the lists in the
 * AGI blocks. What we do here is scan all the AGIs and fully truncate and free
 * any inodes found on the lists. Each inode is removed from the lists when it
 * has been fully truncated and is freed. The freeing of the inode and its
 * removal from the list must be atomic.
 *
 * If everything we touch in the agi processing loop is already in memory, this
 * loop can hold the cpu for a long time. It runs without lock contention,
 * memory allocation contention, the need wait for IO, etc, and so will run
 * until we either run out of inodes to process, run low on memory or we run out
 * of log space.
 *
 * This behaviour is bad for latency on single CPU and non-preemptible kernels,
 * and can prevent other filesystem work (such as CIL pushes) from running. This
 * can lead to deadlocks if the recovery process runs out of log reservation
 * space. Hence we need to yield the CPU when there is other kernel work
 * scheduled on this CPU to ensure other scheduled work can run without undue
 * latency.
 */
STATIC void
xlog_recover_process_iunlinks(
	struct xlog	*log)
{
	struct xfs_mount	*mp = log->l_mp;
	struct xfs_perag	*pag;
	xfs_agnumber_t		agno;
	struct xfs_agi		*agi;
	struct xfs_buf		*agibp;
	xfs_agino_t		agino;
	int			bucket;
	int			error;

	for_each_perag(mp, agno, pag) {
		error = xfs_read_agi(mp, NULL, pag->pag_agno, &agibp);
		if (error) {
			/*
			 * AGI is b0rked. Don't process it.
			 *
			 * We should probably mark the filesystem as corrupt
			 * after we've recovered all the ag's we can....
			 */
			continue;
		}
		/*
		 * Unlock the buffer so that it can be acquired in the normal
		 * course of the transaction to truncate and free each inode.
		 * Because we are not racing with anyone else here for the AGI
		 * buffer, we don't even need to hold it locked to read the
		 * initial unlinked bucket entries out of the buffer. We keep
		 * buffer reference though, so that it stays pinned in memory
		 * while we need the buffer.
		 */
		agi = agibp->b_addr;
		xfs_buf_unlock(agibp);

		for (bucket = 0; bucket < XFS_AGI_UNLINKED_BUCKETS; bucket++) {
			agino = be32_to_cpu(agi->agi_unlinked[bucket]);
			while (agino != NULLAGINO) {
				agino = xlog_recover_process_one_iunlink(mp,
						pag->pag_agno, agino, bucket);
				cond_resched();
			}
		}
		xfs_buf_rele(agibp);
	}

	/*
	 * Flush the pending unlinked inodes to ensure that the inactivations
	 * are fully completed on disk and the incore inodes can be reclaimed
	 * before we signal that recovery is complete.
	 */
	xfs_inodegc_flush(mp);
}

STATIC void
xlog_unpack_data(
	struct xlog_rec_header	*rhead,
	char			*dp,
	struct xlog		*log)
{
	int			i, j, k;

	for (i = 0; i < BTOBB(be32_to_cpu(rhead->h_len)) &&
		  i < (XLOG_HEADER_CYCLE_SIZE / BBSIZE); i++) {
		*(__be32 *)dp = *(__be32 *)&rhead->h_cycle_data[i];
		dp += BBSIZE;
	}

	if (xfs_has_logv2(log->l_mp)) {
		xlog_in_core_2_t *xhdr = (xlog_in_core_2_t *)rhead;
		for ( ; i < BTOBB(be32_to_cpu(rhead->h_len)); i++) {
			j = i / (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
			k = i % (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
			*(__be32 *)dp = xhdr[j].hic_xheader.xh_cycle_data[k];
			dp += BBSIZE;
		}
	}
}

/*
 * CRC check, unpack and process a log record.
 */
STATIC int
xlog_recover_process(
	struct xlog		*log,
	struct hlist_head	rhash[],
	struct xlog_rec_header	*rhead,
	char			*dp,
	int			pass,
	struct list_head	*buffer_list)
{
	__le32			old_crc = rhead->h_crc;
	__le32			crc;

	crc = xlog_cksum(log, rhead, dp, be32_to_cpu(rhead->h_len));

	/*
	 * Nothing else to do if this is a CRC verification pass. Just return
	 * if this a record with a non-zero crc. Unfortunately, mkfs always
	 * sets old_crc to 0 so we must consider this valid even on v5 supers.
	 * Otherwise, return EFSBADCRC on failure so the callers up the stack
	 * know precisely what failed.
	 */
	if (pass == XLOG_RECOVER_CRCPASS) {
		if (old_crc && crc != old_crc)
			return -EFSBADCRC;
		return 0;
	}

	/*
	 * We're in the normal recovery path. Issue a warning if and only if the
	 * CRC in the header is non-zero. This is an advisory warning and the
	 * zero CRC check prevents warnings from being emitted when upgrading
	 * the kernel from one that does not add CRCs by default.
	 */
	if (crc != old_crc) {
		if (old_crc || xfs_has_crc(log->l_mp)) {
			xfs_alert(log->l_mp,
		"log record CRC mismatch: found 0x%x, expected 0x%x.",
					le32_to_cpu(old_crc),
					le32_to_cpu(crc));
			xfs_hex_dump(dp, 32);
		}

		/*
		 * If the filesystem is CRC enabled, this mismatch becomes a
		 * fatal log corruption failure.
		 */
		if (xfs_has_crc(log->l_mp)) {
			XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, log->l_mp);
			return -EFSCORRUPTED;
		}
	}

	xlog_unpack_data(rhead, dp, log);

	return xlog_recover_process_data(log, rhash, rhead, dp, pass,
					 buffer_list);
}

STATIC int
xlog_valid_rec_header(
	struct xlog		*log,
	struct xlog_rec_header	*rhead,
	xfs_daddr_t		blkno,
	int			bufsize)
{
	int			hlen;

	if (XFS_IS_CORRUPT(log->l_mp,
			   rhead->h_magicno != cpu_to_be32(XLOG_HEADER_MAGIC_NUM)))
		return -EFSCORRUPTED;
	if (XFS_IS_CORRUPT(log->l_mp,
			   (!rhead->h_version ||
			   (be32_to_cpu(rhead->h_version) &
			    (~XLOG_VERSION_OKBITS))))) {
		xfs_warn(log->l_mp, "%s: unrecognised log version (%d).",
			__func__, be32_to_cpu(rhead->h_version));
		return -EFSCORRUPTED;
	}

	/*
	 * LR body must have data (or it wouldn't have been written)
	 * and h_len must not be greater than LR buffer size.
	 */
	hlen = be32_to_cpu(rhead->h_len);
	if (XFS_IS_CORRUPT(log->l_mp, hlen <= 0 || hlen > bufsize))
		return -EFSCORRUPTED;

	if (XFS_IS_CORRUPT(log->l_mp,
			   blkno > log->l_logBBsize || blkno > INT_MAX))
		return -EFSCORRUPTED;
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
	struct xlog		*log,
	xfs_daddr_t		head_blk,
	xfs_daddr_t		tail_blk,
	int			pass,
	xfs_daddr_t		*first_bad)	/* out: first bad log rec */
{
	xlog_rec_header_t	*rhead;
	xfs_daddr_t		blk_no, rblk_no;
	xfs_daddr_t		rhead_blk;
	char			*offset;
	char			*hbp, *dbp;
	int			error = 0, h_size, h_len;
	int			error2 = 0;
	int			bblks, split_bblks;
	int			hblks, split_hblks, wrapped_hblks;
	int			i;
	struct hlist_head	rhash[XLOG_RHASH_SIZE];
	LIST_HEAD		(buffer_list);

	ASSERT(head_blk != tail_blk);
	blk_no = rhead_blk = tail_blk;

	for (i = 0; i < XLOG_RHASH_SIZE; i++)
		INIT_HLIST_HEAD(&rhash[i]);

	/*
	 * Read the header of the tail block and get the iclog buffer size from
	 * h_size.  Use this to tell how many sectors make up the log header.
	 */
	if (xfs_has_logv2(log->l_mp)) {
		/*
		 * When using variable length iclogs, read first sector of
		 * iclog header and extract the header size from it.  Get a
		 * new hbp that is the correct size.
		 */
		hbp = xlog_alloc_buffer(log, 1);
		if (!hbp)
			return -ENOMEM;

		error = xlog_bread(log, tail_blk, 1, hbp, &offset);
		if (error)
			goto bread_err1;

		rhead = (xlog_rec_header_t *)offset;

		/*
		 * xfsprogs has a bug where record length is based on lsunit but
		 * h_size (iclog size) is hardcoded to 32k. Now that we
		 * unconditionally CRC verify the unmount record, this means the
		 * log buffer can be too small for the record and cause an
		 * overrun.
		 *
		 * Detect this condition here. Use lsunit for the buffer size as
		 * long as this looks like the mkfs case. Otherwise, return an
		 * error to avoid a buffer overrun.
		 */
		h_size = be32_to_cpu(rhead->h_size);
		h_len = be32_to_cpu(rhead->h_len);
		if (h_len > h_size && h_len <= log->l_mp->m_logbsize &&
		    rhead->h_num_logops == cpu_to_be32(1)) {
			xfs_warn(log->l_mp,
		"invalid iclog size (%d bytes), using lsunit (%d bytes)",
				 h_size, log->l_mp->m_logbsize);
			h_size = log->l_mp->m_logbsize;
		}

		error = xlog_valid_rec_header(log, rhead, tail_blk, h_size);
		if (error)
			goto bread_err1;

		hblks = xlog_logrec_hblks(log, rhead);
		if (hblks != 1) {
			kmem_free(hbp);
			hbp = xlog_alloc_buffer(log, hblks);
		}
	} else {
		ASSERT(log->l_sectBBsize == 1);
		hblks = 1;
		hbp = xlog_alloc_buffer(log, 1);
		h_size = XLOG_BIG_RECORD_BSIZE;
	}

	if (!hbp)
		return -ENOMEM;
	dbp = xlog_alloc_buffer(log, BTOBB(h_size));
	if (!dbp) {
		kmem_free(hbp);
		return -ENOMEM;
	}

	memset(rhash, 0, sizeof(rhash));
	if (tail_blk > head_blk) {
		/*
		 * Perform recovery around the end of the physical log.
		 * When the head is not on the same cycle number as the tail,
		 * we can't do a sequential recovery.
		 */
		while (blk_no < log->l_logBBsize) {
			/*
			 * Check for header wrapping around physical end-of-log
			 */
			offset = hbp;
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
				error = xlog_bread_noalign(log, 0,
						wrapped_hblks,
						offset + BBTOB(split_hblks));
				if (error)
					goto bread_err2;
			}
			rhead = (xlog_rec_header_t *)offset;
			error = xlog_valid_rec_header(log, rhead,
					split_hblks ? blk_no : 0, h_size);
			if (error)
				goto bread_err2;

			bblks = (int)BTOBB(be32_to_cpu(rhead->h_len));
			blk_no += hblks;

			/*
			 * Read the log record data in multiple reads if it
			 * wraps around the end of the log. Note that if the
			 * header already wrapped, blk_no could point past the
			 * end of the log. The record data is contiguous in
			 * that case.
			 */
			if (blk_no + bblks <= log->l_logBBsize ||
			    blk_no >= log->l_logBBsize) {
				rblk_no = xlog_wrap_logbno(log, blk_no);
				error = xlog_bread(log, rblk_no, bblks, dbp,
						   &offset);
				if (error)
					goto bread_err2;
			} else {
				/* This log record is split across the
				 * physical end of log */
				offset = dbp;
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
				error = xlog_bread_noalign(log, 0,
						bblks - split_bblks,
						offset + BBTOB(split_bblks));
				if (error)
					goto bread_err2;
			}

			error = xlog_recover_process(log, rhash, rhead, offset,
						     pass, &buffer_list);
			if (error)
				goto bread_err2;

			blk_no += bblks;
			rhead_blk = blk_no;
		}

		ASSERT(blk_no >= log->l_logBBsize);
		blk_no -= log->l_logBBsize;
		rhead_blk = blk_no;
	}

	/* read first part of physical log */
	while (blk_no < head_blk) {
		error = xlog_bread(log, blk_no, hblks, hbp, &offset);
		if (error)
			goto bread_err2;

		rhead = (xlog_rec_header_t *)offset;
		error = xlog_valid_rec_header(log, rhead, blk_no, h_size);
		if (error)
			goto bread_err2;

		/* blocks in data section */
		bblks = (int)BTOBB(be32_to_cpu(rhead->h_len));
		error = xlog_bread(log, blk_no+hblks, bblks, dbp,
				   &offset);
		if (error)
			goto bread_err2;

		error = xlog_recover_process(log, rhash, rhead, offset, pass,
					     &buffer_list);
		if (error)
			goto bread_err2;

		blk_no += bblks + hblks;
		rhead_blk = blk_no;
	}

 bread_err2:
	kmem_free(dbp);
 bread_err1:
	kmem_free(hbp);

	/*
	 * Submit buffers that have been added from the last record processed,
	 * regardless of error status.
	 */
	if (!list_empty(&buffer_list))
		error2 = xfs_buf_delwri_submit(&buffer_list);

	if (error && first_bad)
		*first_bad = rhead_blk;

	/*
	 * Transactions are freed at commit time but transactions without commit
	 * records on disk are never committed. Free any that may be left in the
	 * hash table.
	 */
	for (i = 0; i < XLOG_RHASH_SIZE; i++) {
		struct hlist_node	*tmp;
		struct xlog_recover	*trans;

		hlist_for_each_entry_safe(trans, tmp, &rhash[i], r_list)
			xlog_recover_free_trans(trans);
	}

	return error ? error : error2;
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
	struct xlog	*log,
	xfs_daddr_t	head_blk,
	xfs_daddr_t	tail_blk)
{
	int		error, i;

	ASSERT(head_blk != tail_blk);

	/*
	 * First do a pass to find all of the cancelled buf log items.
	 * Store them in the buf_cancel_table for use in the second pass.
	 */
	log->l_buf_cancel_table = kmem_zalloc(XLOG_BC_TABLE_SIZE *
						 sizeof(struct list_head),
						 0);
	for (i = 0; i < XLOG_BC_TABLE_SIZE; i++)
		INIT_LIST_HEAD(&log->l_buf_cancel_table[i]);

	error = xlog_do_recovery_pass(log, head_blk, tail_blk,
				      XLOG_RECOVER_PASS1, NULL);
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
				      XLOG_RECOVER_PASS2, NULL);
#ifdef DEBUG
	if (!error) {
		int	i;

		for (i = 0; i < XLOG_BC_TABLE_SIZE; i++)
			ASSERT(list_empty(&log->l_buf_cancel_table[i]));
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
	struct xlog		*log,
	xfs_daddr_t		head_blk,
	xfs_daddr_t		tail_blk)
{
	struct xfs_mount	*mp = log->l_mp;
	struct xfs_buf		*bp = mp->m_sb_bp;
	struct xfs_sb		*sbp = &mp->m_sb;
	int			error;

	trace_xfs_log_recover(log, head_blk, tail_blk);

	/*
	 * First replay the images in the log.
	 */
	error = xlog_do_log_recovery(log, head_blk, tail_blk);
	if (error)
		return error;

	if (xlog_is_shutdown(log))
		return -EIO;

	/*
	 * We now update the tail_lsn since much of the recovery has completed
	 * and there may be space available to use.  If there were no extent
	 * or iunlinks, we can free up the entire log and set the tail_lsn to
	 * be the last_sync_lsn.  This was set in xlog_find_tail to be the
	 * lsn of the last known good LR on disk.  If there are extent frees
	 * or iunlinks they will have some entries in the AIL; so we look at
	 * the AIL to determine how to set the tail_lsn.
	 */
	xlog_assign_tail_lsn(mp);

	/*
	 * Now that we've finished replaying all buffer and inode updates,
	 * re-read the superblock and reverify it.
	 */
	xfs_buf_lock(bp);
	xfs_buf_hold(bp);
	error = _xfs_buf_read(bp, XBF_READ);
	if (error) {
		if (!xlog_is_shutdown(log)) {
			xfs_buf_ioerror_alert(bp, __this_address);
			ASSERT(0);
		}
		xfs_buf_relse(bp);
		return error;
	}

	/* Convert superblock from on-disk format */
	xfs_sb_from_disk(sbp, bp->b_addr);
	xfs_buf_relse(bp);

	/* re-initialise in-core superblock and geometry structures */
	mp->m_features |= xfs_sb_version_to_features(sbp);
	xfs_reinit_percpu_counters(mp);
	error = xfs_initialize_perag(mp, sbp->sb_agcount, &mp->m_maxagi);
	if (error) {
		xfs_warn(mp, "Failed post-recovery per-ag init: %d", error);
		return error;
	}
	mp->m_alloc_set_aside = xfs_alloc_set_aside(mp);

	xlog_recover_check_summary(log);

	/* Normal transactions can now occur */
	clear_bit(XLOG_ACTIVE_RECOVERY, &log->l_opstate);
	return 0;
}

/*
 * Perform recovery and re-initialize some log variables in xlog_find_tail.
 *
 * Return error or zero.
 */
int
xlog_recover(
	struct xlog	*log)
{
	xfs_daddr_t	head_blk, tail_blk;
	int		error;

	/* find the tail of the log */
	error = xlog_find_tail(log, &head_blk, &tail_blk);
	if (error)
		return error;

	/*
	 * The superblock was read before the log was available and thus the LSN
	 * could not be verified. Check the superblock LSN against the current
	 * LSN now that it's known.
	 */
	if (xfs_has_crc(log->l_mp) &&
	    !xfs_log_check_lsn(log->l_mp, log->l_mp->m_sb.sb_lsn))
		return -EINVAL;

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

		/*
		 * Version 5 superblock log feature mask validation. We know the
		 * log is dirty so check if there are any unknown log features
		 * in what we need to recover. If there are unknown features
		 * (e.g. unsupported transactions, then simply reject the
		 * attempt at recovery before touching anything.
		 */
		if (xfs_sb_is_v5(&log->l_mp->m_sb) &&
		    xfs_sb_has_incompat_log_feature(&log->l_mp->m_sb,
					XFS_SB_FEAT_INCOMPAT_LOG_UNKNOWN)) {
			xfs_warn(log->l_mp,
"Superblock has unknown incompatible log features (0x%x) enabled.",
				(log->l_mp->m_sb.sb_features_log_incompat &
					XFS_SB_FEAT_INCOMPAT_LOG_UNKNOWN));
			xfs_warn(log->l_mp,
"The log can not be fully and/or safely recovered by this kernel.");
			xfs_warn(log->l_mp,
"Please recover the log on a kernel that supports the unknown features.");
			return -EINVAL;
		}

		/*
		 * Delay log recovery if the debug hook is set. This is debug
		 * instrumentation to coordinate simulation of I/O failures with
		 * log recovery.
		 */
		if (xfs_globals.log_recovery_delay) {
			xfs_notice(log->l_mp,
				"Delaying log recovery for %d seconds.",
				xfs_globals.log_recovery_delay);
			msleep(xfs_globals.log_recovery_delay * 1000);
		}

		xfs_notice(log->l_mp, "Starting recovery (logdev: %s)",
				log->l_mp->m_logname ? log->l_mp->m_logname
						     : "internal");

		error = xlog_do_recover(log, head_blk, tail_blk);
		set_bit(XLOG_RECOVERY_NEEDED, &log->l_opstate);
	}
	return error;
}

/*
 * In the first part of recovery we replay inodes and buffers and build up the
 * list of intents which need to be processed. Here we process the intents and
 * clean up the on disk unlinked inode lists. This is separated from the first
 * part of recovery so that the root and real-time bitmap inodes can be read in
 * from disk in between the two stages.  This is necessary so that we can free
 * space in the real-time portion of the file system.
 */
int
xlog_recover_finish(
	struct xlog	*log)
{
	int	error;

	error = xlog_recover_process_intents(log);
	if (error) {
		/*
		 * Cancel all the unprocessed intent items now so that we don't
		 * leave them pinned in the AIL.  This can cause the AIL to
		 * livelock on the pinned item if anyone tries to push the AIL
		 * (inode reclaim does this) before we get around to
		 * xfs_log_mount_cancel.
		 */
		xlog_recover_cancel_intents(log);
		xfs_alert(log->l_mp, "Failed to recover intents");
		xfs_force_shutdown(log->l_mp, SHUTDOWN_LOG_IO_ERROR);
		return error;
	}

	/*
	 * Sync the log to get all the intents out of the AIL.  This isn't
	 * absolutely necessary, but it helps in case the unlink transactions
	 * would have problems pushing the intents out of the way.
	 */
	xfs_log_force(log->l_mp, XFS_LOG_SYNC);

	/*
	 * Now that we've recovered the log and all the intents, we can clear
	 * the log incompat feature bits in the superblock because there's no
	 * longer anything to protect.  We rely on the AIL push to write out the
	 * updated superblock after everything else.
	 */
	if (xfs_clear_incompat_log_features(log->l_mp)) {
		error = xfs_sync_sb(log->l_mp, false);
		if (error < 0) {
			xfs_alert(log->l_mp,
	"Failed to clear log incompat features on recovery");
			return error;
		}
	}

	xlog_recover_process_iunlinks(log);
	xlog_recover_check_summary(log);
	return 0;
}

void
xlog_recover_cancel(
	struct xlog	*log)
{
	if (xlog_recovery_needed(log))
		xlog_recover_cancel_intents(log);
}

#if defined(DEBUG)
/*
 * Read all of the agf and agi counters and check that they
 * are consistent with the superblock counters.
 */
STATIC void
xlog_recover_check_summary(
	struct xlog		*log)
{
	struct xfs_mount	*mp = log->l_mp;
	struct xfs_perag	*pag;
	struct xfs_buf		*agfbp;
	struct xfs_buf		*agibp;
	xfs_agnumber_t		agno;
	uint64_t		freeblks;
	uint64_t		itotal;
	uint64_t		ifree;
	int			error;

	mp = log->l_mp;

	freeblks = 0LL;
	itotal = 0LL;
	ifree = 0LL;
	for_each_perag(mp, agno, pag) {
		error = xfs_read_agf(mp, NULL, pag->pag_agno, 0, &agfbp);
		if (error) {
			xfs_alert(mp, "%s agf read failed agno %d error %d",
						__func__, pag->pag_agno, error);
		} else {
			struct xfs_agf	*agfp = agfbp->b_addr;

			freeblks += be32_to_cpu(agfp->agf_freeblks) +
				    be32_to_cpu(agfp->agf_flcount);
			xfs_buf_relse(agfbp);
		}

		error = xfs_read_agi(mp, NULL, pag->pag_agno, &agibp);
		if (error) {
			xfs_alert(mp, "%s agi read failed agno %d error %d",
						__func__, pag->pag_agno, error);
		} else {
			struct xfs_agi	*agi = agibp->b_addr;

			itotal += be32_to_cpu(agi->agi_count);
			ifree += be32_to_cpu(agi->agi_freecount);
			xfs_buf_relse(agibp);
		}
	}
}
#endif /* DEBUG */
