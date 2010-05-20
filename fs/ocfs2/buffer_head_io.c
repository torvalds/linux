/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * io.c
 *
 * Buffer cache handling
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/highmem.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "inode.h"
#include "journal.h"
#include "uptodate.h"

#include "buffer_head_io.h"

/*
 * Bits on bh->b_state used by ocfs2.
 *
 * These MUST be after the JBD2 bits.  Hence, we use BH_JBDPrivateStart.
 */
enum ocfs2_state_bits {
	BH_NeedsValidate = BH_JBDPrivateStart,
};

/* Expand the magic b_state functions */
BUFFER_FNS(NeedsValidate, needs_validate);

int ocfs2_write_block(struct ocfs2_super *osb, struct buffer_head *bh,
		      struct ocfs2_caching_info *ci)
{
	int ret = 0;

	mlog_entry("(bh->b_blocknr = %llu, ci=%p)\n",
		   (unsigned long long)bh->b_blocknr, ci);

	BUG_ON(bh->b_blocknr < OCFS2_SUPER_BLOCK_BLKNO);
	BUG_ON(buffer_jbd(bh));

	/* No need to check for a soft readonly file system here. non
	 * journalled writes are only ever done on system files which
	 * can get modified during recovery even if read-only. */
	if (ocfs2_is_hard_readonly(osb)) {
		ret = -EROFS;
		goto out;
	}

	ocfs2_metadata_cache_io_lock(ci);

	lock_buffer(bh);
	set_buffer_uptodate(bh);

	/* remove from dirty list before I/O. */
	clear_buffer_dirty(bh);

	get_bh(bh); /* for end_buffer_write_sync() */
	bh->b_end_io = end_buffer_write_sync;
	submit_bh(WRITE, bh);

	wait_on_buffer(bh);

	if (buffer_uptodate(bh)) {
		ocfs2_set_buffer_uptodate(ci, bh);
	} else {
		/* We don't need to remove the clustered uptodate
		 * information for this bh as it's not marked locally
		 * uptodate. */
		ret = -EIO;
		put_bh(bh);
	}

	ocfs2_metadata_cache_io_unlock(ci);
out:
	mlog_exit(ret);
	return ret;
}

int ocfs2_read_blocks_sync(struct ocfs2_super *osb, u64 block,
			   unsigned int nr, struct buffer_head *bhs[])
{
	int status = 0;
	unsigned int i;
	struct buffer_head *bh;

	if (!nr) {
		mlog(ML_BH_IO, "No buffers will be read!\n");
		goto bail;
	}

	for (i = 0 ; i < nr ; i++) {
		if (bhs[i] == NULL) {
			bhs[i] = sb_getblk(osb->sb, block++);
			if (bhs[i] == NULL) {
				status = -EIO;
				mlog_errno(status);
				goto bail;
			}
		}
		bh = bhs[i];

		if (buffer_jbd(bh)) {
			mlog(ML_BH_IO,
			     "trying to sync read a jbd "
			     "managed bh (blocknr = %llu), skipping\n",
			     (unsigned long long)bh->b_blocknr);
			continue;
		}

		if (buffer_dirty(bh)) {
			/* This should probably be a BUG, or
			 * at least return an error. */
			mlog(ML_ERROR,
			     "trying to sync read a dirty "
			     "buffer! (blocknr = %llu), skipping\n",
			     (unsigned long long)bh->b_blocknr);
			continue;
		}

		lock_buffer(bh);
		if (buffer_jbd(bh)) {
			mlog(ML_ERROR,
			     "block %llu had the JBD bit set "
			     "while I was in lock_buffer!",
			     (unsigned long long)bh->b_blocknr);
			BUG();
		}

		clear_buffer_uptodate(bh);
		get_bh(bh); /* for end_buffer_read_sync() */
		bh->b_end_io = end_buffer_read_sync;
		submit_bh(READ, bh);
	}

	for (i = nr; i > 0; i--) {
		bh = bhs[i - 1];

		/* No need to wait on the buffer if it's managed by JBD. */
		if (!buffer_jbd(bh))
			wait_on_buffer(bh);

		if (!buffer_uptodate(bh)) {
			/* Status won't be cleared from here on out,
			 * so we can safely record this and loop back
			 * to cleanup the other buffers. */
			status = -EIO;
			put_bh(bh);
			bhs[i - 1] = NULL;
		}
	}

bail:
	return status;
}

int ocfs2_read_blocks(struct ocfs2_caching_info *ci, u64 block, int nr,
		      struct buffer_head *bhs[], int flags,
		      int (*validate)(struct super_block *sb,
				      struct buffer_head *bh))
{
	int status = 0;
	int i, ignore_cache = 0;
	struct buffer_head *bh;
	struct super_block *sb = ocfs2_metadata_cache_get_super(ci);

	mlog_entry("(ci=%p, block=(%llu), nr=(%d), flags=%d)\n",
		   ci, (unsigned long long)block, nr, flags);

	BUG_ON(!ci);
	BUG_ON((flags & OCFS2_BH_READAHEAD) &&
	       (flags & OCFS2_BH_IGNORE_CACHE));

	if (bhs == NULL) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	if (nr < 0) {
		mlog(ML_ERROR, "asked to read %d blocks!\n", nr);
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	if (nr == 0) {
		mlog(ML_BH_IO, "No buffers will be read!\n");
		status = 0;
		goto bail;
	}

	ocfs2_metadata_cache_io_lock(ci);
	for (i = 0 ; i < nr ; i++) {
		if (bhs[i] == NULL) {
			bhs[i] = sb_getblk(sb, block++);
			if (bhs[i] == NULL) {
				ocfs2_metadata_cache_io_unlock(ci);
				status = -EIO;
				mlog_errno(status);
				goto bail;
			}
		}
		bh = bhs[i];
		ignore_cache = (flags & OCFS2_BH_IGNORE_CACHE);

		/* There are three read-ahead cases here which we need to
		 * be concerned with. All three assume a buffer has
		 * previously been submitted with OCFS2_BH_READAHEAD
		 * and it hasn't yet completed I/O.
		 *
		 * 1) The current request is sync to disk. This rarely
		 *    happens these days, and never when performance
		 *    matters - the code can just wait on the buffer
		 *    lock and re-submit.
		 *
		 * 2) The current request is cached, but not
		 *    readahead. ocfs2_buffer_uptodate() will return
		 *    false anyway, so we'll wind up waiting on the
		 *    buffer lock to do I/O. We re-check the request
		 *    with after getting the lock to avoid a re-submit.
		 *
		 * 3) The current request is readahead (and so must
		 *    also be a caching one). We short circuit if the
		 *    buffer is locked (under I/O) and if it's in the
		 *    uptodate cache. The re-check from #2 catches the
		 *    case that the previous read-ahead completes just
		 *    before our is-it-in-flight check.
		 */

		if (!ignore_cache && !ocfs2_buffer_uptodate(ci, bh)) {
			mlog(ML_UPTODATE,
			     "bh (%llu), owner %llu not uptodate\n",
			     (unsigned long long)bh->b_blocknr,
			     (unsigned long long)ocfs2_metadata_cache_owner(ci));
			/* We're using ignore_cache here to say
			 * "go to disk" */
			ignore_cache = 1;
		}

		if (buffer_jbd(bh)) {
			if (ignore_cache)
				mlog(ML_BH_IO, "trying to sync read a jbd "
					       "managed bh (blocknr = %llu)\n",
				     (unsigned long long)bh->b_blocknr);
			continue;
		}

		if (ignore_cache) {
			if (buffer_dirty(bh)) {
				/* This should probably be a BUG, or
				 * at least return an error. */
				mlog(ML_BH_IO, "asking me to sync read a dirty "
					       "buffer! (blocknr = %llu)\n",
				     (unsigned long long)bh->b_blocknr);
				continue;
			}

			/* A read-ahead request was made - if the
			 * buffer is already under read-ahead from a
			 * previously submitted request than we are
			 * done here. */
			if ((flags & OCFS2_BH_READAHEAD)
			    && ocfs2_buffer_read_ahead(ci, bh))
				continue;

			lock_buffer(bh);
			if (buffer_jbd(bh)) {
#ifdef CATCH_BH_JBD_RACES
				mlog(ML_ERROR, "block %llu had the JBD bit set "
					       "while I was in lock_buffer!",
				     (unsigned long long)bh->b_blocknr);
				BUG();
#else
				unlock_buffer(bh);
				continue;
#endif
			}

			/* Re-check ocfs2_buffer_uptodate() as a
			 * previously read-ahead buffer may have
			 * completed I/O while we were waiting for the
			 * buffer lock. */
			if (!(flags & OCFS2_BH_IGNORE_CACHE)
			    && !(flags & OCFS2_BH_READAHEAD)
			    && ocfs2_buffer_uptodate(ci, bh)) {
				unlock_buffer(bh);
				continue;
			}

			clear_buffer_uptodate(bh);
			get_bh(bh); /* for end_buffer_read_sync() */
			if (validate)
				set_buffer_needs_validate(bh);
			bh->b_end_io = end_buffer_read_sync;
			submit_bh(READ, bh);
			continue;
		}
	}

	status = 0;

	for (i = (nr - 1); i >= 0; i--) {
		bh = bhs[i];

		if (!(flags & OCFS2_BH_READAHEAD)) {
			/* We know this can't have changed as we hold the
			 * owner sem. Avoid doing any work on the bh if the
			 * journal has it. */
			if (!buffer_jbd(bh))
				wait_on_buffer(bh);

			if (!buffer_uptodate(bh)) {
				/* Status won't be cleared from here on out,
				 * so we can safely record this and loop back
				 * to cleanup the other buffers. Don't need to
				 * remove the clustered uptodate information
				 * for this bh as it's not marked locally
				 * uptodate. */
				status = -EIO;
				put_bh(bh);
				bhs[i] = NULL;
				continue;
			}

			if (buffer_needs_validate(bh)) {
				/* We never set NeedsValidate if the
				 * buffer was held by the journal, so
				 * that better not have changed */
				BUG_ON(buffer_jbd(bh));
				clear_buffer_needs_validate(bh);
				status = validate(sb, bh);
				if (status) {
					put_bh(bh);
					bhs[i] = NULL;
					continue;
				}
			}
		}

		/* Always set the buffer in the cache, even if it was
		 * a forced read, or read-ahead which hasn't yet
		 * completed. */
		ocfs2_set_buffer_uptodate(ci, bh);
	}
	ocfs2_metadata_cache_io_unlock(ci);

	mlog(ML_BH_IO, "block=(%llu), nr=(%d), cached=%s, flags=0x%x\n",
	     (unsigned long long)block, nr,
	     ((flags & OCFS2_BH_IGNORE_CACHE) || ignore_cache) ? "no" : "yes",
	     flags);

bail:

	mlog_exit(status);
	return status;
}

/* Check whether the blkno is the super block or one of the backups. */
static void ocfs2_check_super_or_backup(struct super_block *sb,
					sector_t blkno)
{
	int i;
	u64 backup_blkno;

	if (blkno == OCFS2_SUPER_BLOCK_BLKNO)
		return;

	for (i = 0; i < OCFS2_MAX_BACKUP_SUPERBLOCKS; i++) {
		backup_blkno = ocfs2_backup_super_blkno(sb, i);
		if (backup_blkno == blkno)
			return;
	}

	BUG();
}

/*
 * Write super block and backups doesn't need to collaborate with journal,
 * so we don't need to lock ip_io_mutex and ci doesn't need to bea passed
 * into this function.
 */
int ocfs2_write_super_or_backup(struct ocfs2_super *osb,
				struct buffer_head *bh)
{
	int ret = 0;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)bh->b_data;

	mlog_entry_void();

	BUG_ON(buffer_jbd(bh));
	ocfs2_check_super_or_backup(osb->sb, bh->b_blocknr);

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb)) {
		ret = -EROFS;
		goto out;
	}

	lock_buffer(bh);
	set_buffer_uptodate(bh);

	/* remove from dirty list before I/O. */
	clear_buffer_dirty(bh);

	get_bh(bh); /* for end_buffer_write_sync() */
	bh->b_end_io = end_buffer_write_sync;
	ocfs2_compute_meta_ecc(osb->sb, bh->b_data, &di->i_check);
	submit_bh(WRITE, bh);

	wait_on_buffer(bh);

	if (!buffer_uptodate(bh)) {
		ret = -EIO;
		put_bh(bh);
	}

out:
	mlog_exit(ret);
	return ret;
}
