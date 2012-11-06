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
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_itable.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_utils.h"
#include "xfs_rtalloc.h"
#include "xfs_trans_space.h"
#include "xfs_log_priv.h"
#include "xfs_filestream.h"
#include "xfs_vnodeops.h"
#include "xfs_trace.h"
#include "xfs_icache.h"

/*
 * The maximum pathlen is 1024 bytes. Since the minimum file system
 * blocksize is 512 bytes, we can get a max of 2 extents back from
 * bmapi.
 */
#define SYMLINK_MAPS 2

STATIC int
xfs_readlink_bmap(
	xfs_inode_t	*ip,
	char		*link)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		pathlen = ip->i_d.di_size;
	int             nmaps = SYMLINK_MAPS;
	xfs_bmbt_irec_t mval[SYMLINK_MAPS];
	xfs_daddr_t	d;
	int		byte_cnt;
	int		n;
	xfs_buf_t	*bp;
	int		error = 0;

	error = xfs_bmapi_read(ip, 0, XFS_B_TO_FSB(mp, pathlen), mval, &nmaps,
			       0);
	if (error)
		goto out;

	for (n = 0; n < nmaps; n++) {
		d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
		byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);

		bp = xfs_buf_read(mp->m_ddev_targp, d, BTOBB(byte_cnt), 0);
		if (!bp)
			return XFS_ERROR(ENOMEM);
		error = bp->b_error;
		if (error) {
			xfs_buf_ioerror_alert(bp, __func__);
			xfs_buf_relse(bp);
			goto out;
		}
		if (pathlen < byte_cnt)
			byte_cnt = pathlen;
		pathlen -= byte_cnt;

		memcpy(link, bp->b_addr, byte_cnt);
		xfs_buf_relse(bp);
	}

	link[ip->i_d.di_size] = '\0';
	error = 0;

 out:
	return error;
}

int
xfs_readlink(
	xfs_inode_t     *ip,
	char		*link)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_fsize_t	pathlen;
	int		error = 0;

	trace_xfs_readlink(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	xfs_ilock(ip, XFS_ILOCK_SHARED);

	pathlen = ip->i_d.di_size;
	if (!pathlen)
		goto out;

	if (pathlen < 0 || pathlen > MAXPATHLEN) {
		xfs_alert(mp, "%s: inode (%llu) bad symlink length (%lld)",
			 __func__, (unsigned long long) ip->i_ino,
			 (long long) pathlen);
		ASSERT(0);
		error = XFS_ERROR(EFSCORRUPTED);
		goto out;
	}


	if (ip->i_df.if_flags & XFS_IFINLINE) {
		memcpy(link, ip->i_df.if_u1.if_data, pathlen);
		link[pathlen] = '\0';
	} else {
		error = xfs_readlink_bmap(ip, link);
	}

 out:
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return error;
}

/*
 * This is called by xfs_inactive to free any blocks beyond eof
 * when the link count isn't zero and by xfs_dm_punch_hole() when
 * punching a hole to EOF.
 */
int
xfs_free_eofblocks(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	bool		need_iolock)
{
	xfs_trans_t	*tp;
	int		error;
	xfs_fileoff_t	end_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	map_len;
	int		nimaps;
	xfs_bmbt_irec_t	imap;

	/*
	 * Figure out if there are any blocks beyond the end
	 * of the file.  If not, then there is nothing to do.
	 */
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_ISIZE(ip));
	last_fsb = XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes);
	if (last_fsb <= end_fsb)
		return 0;
	map_len = last_fsb - end_fsb;

	nimaps = 1;
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	error = xfs_bmapi_read(ip, end_fsb, map_len, &imap, &nimaps, 0);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (!error && (nimaps != 0) &&
	    (imap.br_startblock != HOLESTARTBLOCK ||
	     ip->i_delayed_blks)) {
		/*
		 * Attach the dquots to the inode up front.
		 */
		error = xfs_qm_dqattach(ip, 0);
		if (error)
			return error;

		/*
		 * There are blocks after the end of file.
		 * Free them up now by truncating the file to
		 * its current size.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);

		if (need_iolock) {
			if (!xfs_ilock_nowait(ip, XFS_IOLOCK_EXCL)) {
				xfs_trans_cancel(tp, 0);
				return EAGAIN;
			}
		}

		error = xfs_trans_reserve(tp, 0,
					  XFS_ITRUNCATE_LOG_RES(mp),
					  0, XFS_TRANS_PERM_LOG_RES,
					  XFS_ITRUNCATE_LOG_COUNT);
		if (error) {
			ASSERT(XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			if (need_iolock)
				xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return error;
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, 0);

		/*
		 * Do not update the on-disk file size.  If we update the
		 * on-disk file size and then the system crashes before the
		 * contents of the file are flushed to disk then the files
		 * may be full of holes (ie NULL files bug).
		 */
		error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK,
					      XFS_ISIZE(ip));
		if (error) {
			/*
			 * If we get an error at this point we simply don't
			 * bother truncating the file.
			 */
			xfs_trans_cancel(tp,
					 (XFS_TRANS_RELEASE_LOG_RES |
					  XFS_TRANS_ABORT));
		} else {
			error = xfs_trans_commit(tp,
						XFS_TRANS_RELEASE_LOG_RES);
			if (!error)
				xfs_inode_clear_eofblocks_tag(ip);
		}

		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (need_iolock)
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	}
	return error;
}

/*
 * Free a symlink that has blocks associated with it.
 */
STATIC int
xfs_inactive_symlink_rmt(
	xfs_inode_t	*ip,
	xfs_trans_t	**tpp)
{
	xfs_buf_t	*bp;
	int		committed;
	int		done;
	int		error;
	xfs_fsblock_t	first_block;
	xfs_bmap_free_t	free_list;
	int		i;
	xfs_mount_t	*mp;
	xfs_bmbt_irec_t	mval[SYMLINK_MAPS];
	int		nmaps;
	xfs_trans_t	*ntp;
	int		size;
	xfs_trans_t	*tp;

	tp = *tpp;
	mp = ip->i_mount;
	ASSERT(ip->i_d.di_size > XFS_IFORK_DSIZE(ip));
	/*
	 * We're freeing a symlink that has some
	 * blocks allocated to it.  Free the
	 * blocks here.  We know that we've got
	 * either 1 or 2 extents and that we can
	 * free them all in one bunmapi call.
	 */
	ASSERT(ip->i_d.di_nextents > 0 && ip->i_d.di_nextents <= 2);

	/*
	 * Lock the inode, fix the size, and join it to the transaction.
	 * Hold it so in the normal path, we still have it locked for
	 * the second transaction.  In the error paths we need it
	 * held so the cancel won't rele it, see below.
	 */
	size = (int)ip->i_d.di_size;
	ip->i_d.di_size = 0;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	/*
	 * Find the block(s) so we can inval and unmap them.
	 */
	done = 0;
	xfs_bmap_init(&free_list, &first_block);
	nmaps = ARRAY_SIZE(mval);
	error = xfs_bmapi_read(ip, 0, XFS_B_TO_FSB(mp, size),
				mval, &nmaps, 0);
	if (error)
		goto error0;
	/*
	 * Invalidate the block(s).
	 */
	for (i = 0; i < nmaps; i++) {
		bp = xfs_trans_get_buf(tp, mp->m_ddev_targp,
			XFS_FSB_TO_DADDR(mp, mval[i].br_startblock),
			XFS_FSB_TO_BB(mp, mval[i].br_blockcount), 0);
		if (!bp) {
			error = ENOMEM;
			goto error1;
		}
		xfs_trans_binval(tp, bp);
	}
	/*
	 * Unmap the dead block(s) to the free_list.
	 */
	if ((error = xfs_bunmapi(tp, ip, 0, size, XFS_BMAPI_METADATA, nmaps,
			&first_block, &free_list, &done)))
		goto error1;
	ASSERT(done);
	/*
	 * Commit the first transaction.  This logs the EFI and the inode.
	 */
	if ((error = xfs_bmap_finish(&tp, &free_list, &committed)))
		goto error1;
	/*
	 * The transaction must have been committed, since there were
	 * actually extents freed by xfs_bunmapi.  See xfs_bmap_finish.
	 * The new tp has the extent freeing and EFDs.
	 */
	ASSERT(committed);
	/*
	 * The first xact was committed, so add the inode to the new one.
	 * Mark it dirty so it will be logged and moved forward in the log as
	 * part of every commit.
	 */
	xfs_trans_ijoin(tp, ip, 0);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	/*
	 * Get a new, empty transaction to return to our caller.
	 */
	ntp = xfs_trans_dup(tp);
	/*
	 * Commit the transaction containing extent freeing and EFDs.
	 * If we get an error on the commit here or on the reserve below,
	 * we need to unlock the inode since the new transaction doesn't
	 * have the inode attached.
	 */
	error = xfs_trans_commit(tp, 0);
	tp = ntp;
	if (error) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		goto error0;
	}
	/*
	 * transaction commit worked ok so we can drop the extra ticket
	 * reference that we gained in xfs_trans_dup()
	 */
	xfs_log_ticket_put(tp->t_ticket);

	/*
	 * Remove the memory for extent descriptions (just bookkeeping).
	 */
	if (ip->i_df.if_bytes)
		xfs_idata_realloc(ip, -ip->i_df.if_bytes, XFS_DATA_FORK);
	ASSERT(ip->i_df.if_bytes == 0);
	/*
	 * Put an itruncate log reservation in the new transaction
	 * for our caller.
	 */
	if ((error = xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_ITRUNCATE_LOG_COUNT))) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		goto error0;
	}

	xfs_trans_ijoin(tp, ip, 0);
	*tpp = tp;
	return 0;

 error1:
	xfs_bmap_cancel(&free_list);
 error0:
	return error;
}

int
xfs_release(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		error;

	if (!S_ISREG(ip->i_d.di_mode) || (ip->i_d.di_mode == 0))
		return 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return 0;

	if (!XFS_FORCED_SHUTDOWN(mp)) {
		int truncated;

		/*
		 * If we are using filestreams, and we have an unlinked
		 * file that we are processing the last close on, then nothing
		 * will be able to reopen and write to this file. Purge this
		 * inode from the filestreams cache so that it doesn't delay
		 * teardown of the inode.
		 */
		if ((ip->i_d.di_nlink == 0) && xfs_inode_is_filestream(ip))
			xfs_filestream_deassociate(ip);

		/*
		 * If we previously truncated this file and removed old data
		 * in the process, we want to initiate "early" writeout on
		 * the last close.  This is an attempt to combat the notorious
		 * NULL files problem which is particularly noticeable from a
		 * truncate down, buffered (re-)write (delalloc), followed by
		 * a crash.  What we are effectively doing here is
		 * significantly reducing the time window where we'd otherwise
		 * be exposed to that problem.
		 */
		truncated = xfs_iflags_test_and_clear(ip, XFS_ITRUNCATED);
		if (truncated) {
			xfs_iflags_clear(ip, XFS_IDIRTY_RELEASE);
			if (VN_DIRTY(VFS_I(ip)) && ip->i_delayed_blks > 0)
				xfs_flush_pages(ip, 0, -1, XBF_ASYNC, FI_NONE);
		}
	}

	if (ip->i_d.di_nlink == 0)
		return 0;

	if (xfs_can_free_eofblocks(ip, false)) {

		/*
		 * If we can't get the iolock just skip truncating the blocks
		 * past EOF because we could deadlock with the mmap_sem
		 * otherwise.  We'll get another chance to drop them once the
		 * last reference to the inode is dropped, so we'll never leak
		 * blocks permanently.
		 *
		 * Further, check if the inode is being opened, written and
		 * closed frequently and we have delayed allocation blocks
		 * outstanding (e.g. streaming writes from the NFS server),
		 * truncating the blocks past EOF will cause fragmentation to
		 * occur.
		 *
		 * In this case don't do the truncation, either, but we have to
		 * be careful how we detect this case. Blocks beyond EOF show
		 * up as i_delayed_blks even when the inode is clean, so we
		 * need to truncate them away first before checking for a dirty
		 * release. Hence on the first dirty close we will still remove
		 * the speculative allocation, but after that we will leave it
		 * in place.
		 */
		if (xfs_iflags_test(ip, XFS_IDIRTY_RELEASE))
			return 0;

		error = xfs_free_eofblocks(mp, ip, true);
		if (error && error != EAGAIN)
			return error;

		/* delalloc blocks after truncation means it really is dirty */
		if (ip->i_delayed_blks)
			xfs_iflags_set(ip, XFS_IDIRTY_RELEASE);
	}
	return 0;
}

/*
 * xfs_inactive
 *
 * This is called when the vnode reference count for the vnode
 * goes to zero.  If the file has been unlinked, then it must
 * now be truncated.  Also, we clear all of the read-ahead state
 * kept for the inode here since the file is now closed.
 */
int
xfs_inactive(
	xfs_inode_t	*ip)
{
	xfs_bmap_free_t	free_list;
	xfs_fsblock_t	first_block;
	int		committed;
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	int		error;
	int		truncate = 0;

	/*
	 * If the inode is already free, then there can be nothing
	 * to clean up here.
	 */
	if (ip->i_d.di_mode == 0 || is_bad_inode(VFS_I(ip))) {
		ASSERT(ip->i_df.if_real_bytes == 0);
		ASSERT(ip->i_df.if_broot_bytes == 0);
		return VN_INACTIVE_CACHE;
	}

	mp = ip->i_mount;

	error = 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		goto out;

	if (ip->i_d.di_nlink != 0) {
		/*
		 * force is true because we are evicting an inode from the
		 * cache. Post-eof blocks must be freed, lest we end up with
		 * broken free space accounting.
		 */
		if (xfs_can_free_eofblocks(ip, true)) {
			error = xfs_free_eofblocks(mp, ip, false);
			if (error)
				return VN_INACTIVE_CACHE;
		}
		goto out;
	}

	if (S_ISREG(ip->i_d.di_mode) &&
	    (ip->i_d.di_size != 0 || XFS_ISIZE(ip) != 0 ||
	     ip->i_d.di_nextents > 0 || ip->i_delayed_blks > 0))
		truncate = 1;

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return VN_INACTIVE_CACHE;

	tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
	error = xfs_trans_reserve(tp, 0,
			(truncate || S_ISLNK(ip->i_d.di_mode)) ?
				XFS_ITRUNCATE_LOG_RES(mp) :
				XFS_IFREE_LOG_RES(mp),
			0,
			XFS_TRANS_PERM_LOG_RES,
			XFS_ITRUNCATE_LOG_COUNT);
	if (error) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		xfs_trans_cancel(tp, 0);
		return VN_INACTIVE_CACHE;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	if (S_ISLNK(ip->i_d.di_mode)) {
		/*
		 * Zero length symlinks _can_ exist.
		 */
		if (ip->i_d.di_size > XFS_IFORK_DSIZE(ip)) {
			error = xfs_inactive_symlink_rmt(ip, &tp);
			if (error)
				goto out_cancel;
		} else if (ip->i_df.if_bytes > 0) {
			xfs_idata_realloc(ip, -(ip->i_df.if_bytes),
					  XFS_DATA_FORK);
			ASSERT(ip->i_df.if_bytes == 0);
		}
	} else if (truncate) {
		ip->i_d.di_size = 0;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

		error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK, 0);
		if (error)
			goto out_cancel;

		ASSERT(ip->i_d.di_nextents == 0);
	}

	/*
	 * If there are attributes associated with the file then blow them away
	 * now.  The code calls a routine that recursively deconstructs the
	 * attribute fork.  We need to just commit the current transaction
	 * because we can't use it for xfs_attr_inactive().
	 */
	if (ip->i_d.di_anextents > 0) {
		ASSERT(ip->i_d.di_forkoff != 0);

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		if (error)
			goto out_unlock;

		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		error = xfs_attr_inactive(ip);
		if (error)
			goto out;

		tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
		error = xfs_trans_reserve(tp, 0,
					  XFS_IFREE_LOG_RES(mp),
					  0, XFS_TRANS_PERM_LOG_RES,
					  XFS_INACTIVE_LOG_COUNT);
		if (error) {
			xfs_trans_cancel(tp, 0);
			goto out;
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, 0);
	}

	if (ip->i_afp)
		xfs_idestroy_fork(ip, XFS_ATTR_FORK);

	ASSERT(ip->i_d.di_anextents == 0);

	/*
	 * Free the inode.
	 */
	xfs_bmap_init(&free_list, &first_block);
	error = xfs_ifree(tp, ip, &free_list);
	if (error) {
		/*
		 * If we fail to free the inode, shut down.  The cancel
		 * might do that, we need to make sure.  Otherwise the
		 * inode might be lost for a long time or forever.
		 */
		if (!XFS_FORCED_SHUTDOWN(mp)) {
			xfs_notice(mp, "%s: xfs_ifree returned error %d",
				__func__, error);
			xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		}
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES|XFS_TRANS_ABORT);
	} else {
		/*
		 * Credit the quota account(s). The inode is gone.
		 */
		xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_ICOUNT, -1);

		/*
		 * Just ignore errors at this point.  There is nothing we can
		 * do except to try to keep going. Make sure it's not a silent
		 * error.
		 */
		error = xfs_bmap_finish(&tp,  &free_list, &committed);
		if (error)
			xfs_notice(mp, "%s: xfs_bmap_finish returned error %d",
				__func__, error);
		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		if (error)
			xfs_notice(mp, "%s: xfs_trans_commit returned error %d",
				__func__, error);
	}

	/*
	 * Release the dquots held by inode, if any.
	 */
	xfs_qm_dqdetach(ip);
out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out:
	return VN_INACTIVE_CACHE;
out_cancel:
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	goto out_unlock;
}

/*
 * Lookups up an inode from "name". If ci_name is not NULL, then a CI match
 * is allowed, otherwise it has to be an exact match. If a CI match is found,
 * ci_name->name will point to a the actual name (caller must free) or
 * will be set to NULL if an exact match is found.
 */
int
xfs_lookup(
	xfs_inode_t		*dp,
	struct xfs_name		*name,
	xfs_inode_t		**ipp,
	struct xfs_name		*ci_name)
{
	xfs_ino_t		inum;
	int			error;
	uint			lock_mode;

	trace_xfs_lookup(dp, name);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return XFS_ERROR(EIO);

	lock_mode = xfs_ilock_map_shared(dp);
	error = xfs_dir_lookup(NULL, dp, name, &inum, ci_name);
	xfs_iunlock_map_shared(dp, lock_mode);

	if (error)
		goto out;

	error = xfs_iget(dp->i_mount, NULL, inum, 0, 0, ipp);
	if (error)
		goto out_free_name;

	return 0;

out_free_name:
	if (ci_name)
		kmem_free(ci_name->name);
out:
	*ipp = NULL;
	return error;
}

int
xfs_create(
	xfs_inode_t		*dp,
	struct xfs_name		*name,
	umode_t			mode,
	xfs_dev_t		rdev,
	xfs_inode_t		**ipp)
{
	int			is_dir = S_ISDIR(mode);
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_inode	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	int			error;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	boolean_t		unlock_dp_on_error = B_FALSE;
	uint			cancel_flags;
	int			committed;
	prid_t			prid;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	uint			resblks;
	uint			log_res;
	uint			log_count;

	trace_xfs_create(dp, name);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		prid = xfs_get_projid(dp);
	else
		prid = XFS_PROJID_DEFAULT;

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, current_fsuid(), current_fsgid(), prid,
			XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT, &udqp, &gdqp);
	if (error)
		return error;

	if (is_dir) {
		rdev = 0;
		resblks = XFS_MKDIR_SPACE_RES(mp, name->len);
		log_res = XFS_MKDIR_LOG_RES(mp);
		log_count = XFS_MKDIR_LOG_COUNT;
		tp = xfs_trans_alloc(mp, XFS_TRANS_MKDIR);
	} else {
		resblks = XFS_CREATE_SPACE_RES(mp, name->len);
		log_res = XFS_CREATE_LOG_RES(mp);
		log_count = XFS_CREATE_LOG_COUNT;
		tp = xfs_trans_alloc(mp, XFS_TRANS_CREATE);
	}

	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;

	/*
	 * Initially assume that the file does not exist and
	 * reserve the resources for that case.  If that is not
	 * the case we'll drop the one we have and get a more
	 * appropriate transaction later.
	 */
	error = xfs_trans_reserve(tp, resblks, log_res, 0,
			XFS_TRANS_PERM_LOG_RES, log_count);
	if (error == ENOSPC) {
		/* flush outstanding delalloc blocks and retry */
		xfs_flush_inodes(mp);
		error = xfs_trans_reserve(tp, resblks, log_res, 0,
				XFS_TRANS_PERM_LOG_RES, log_count);
	}
	if (error == ENOSPC) {
		/* No space at all so try a "no-allocation" reservation */
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, log_res, 0,
				XFS_TRANS_PERM_LOG_RES, log_count);
	}
	if (error) {
		cancel_flags = 0;
		goto out_trans_cancel;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	unlock_dp_on_error = B_TRUE;

	xfs_bmap_init(&free_list, &first_block);

	/*
	 * Reserve disk quota and the inode.
	 */
	error = xfs_trans_reserve_quota(tp, mp, udqp, gdqp, resblks, 1, 0);
	if (error)
		goto out_trans_cancel;

	error = xfs_dir_canenter(tp, dp, name, resblks);
	if (error)
		goto out_trans_cancel;

	/*
	 * A newly created regular or special file just has one directory
	 * entry pointing to them, but a directory also the "." entry
	 * pointing to itself.
	 */
	error = xfs_dir_ialloc(&tp, dp, mode, is_dir ? 2 : 1, rdev,
			       prid, resblks > 0, &ip, &committed);
	if (error) {
		if (error == ENOSPC)
			goto out_trans_cancel;
		goto out_trans_abort;
	}

	/*
	 * Now we join the directory inode to the transaction.  We do not do it
	 * earlier because xfs_dir_ialloc might commit the previous transaction
	 * (and release all the locks).  An error from here on will result in
	 * the transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	unlock_dp_on_error = B_FALSE;

	error = xfs_dir_createname(tp, dp, name, ip->i_ino,
					&first_block, &free_list, resblks ?
					resblks - XFS_IALLOC_SPACE_RES(mp) : 0);
	if (error) {
		ASSERT(error != ENOSPC);
		goto out_trans_abort;
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	if (is_dir) {
		error = xfs_dir_init(tp, ip, dp);
		if (error)
			goto out_bmap_cancel;

		error = xfs_bumplink(tp, dp);
		if (error)
			goto out_bmap_cancel;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * create transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp);

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error)
		goto out_bmap_cancel;

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error)
		goto out_release_inode;

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	*ipp = ip;
	return 0;

 out_bmap_cancel:
	xfs_bmap_cancel(&free_list);
 out_trans_abort:
	cancel_flags |= XFS_TRANS_ABORT;
 out_trans_cancel:
	xfs_trans_cancel(tp, cancel_flags);
 out_release_inode:
	/*
	 * Wait until after the current transaction is aborted to
	 * release the inode.  This prevents recursive transactions
	 * and deadlocks from xfs_inactive.
	 */
	if (ip)
		IRELE(ip);

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	if (unlock_dp_on_error)
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	return error;
}

#ifdef DEBUG
int xfs_locked_n;
int xfs_small_retries;
int xfs_middle_retries;
int xfs_lots_retries;
int xfs_lock_delays;
#endif

/*
 * Bump the subclass so xfs_lock_inodes() acquires each lock with
 * a different value
 */
static inline int
xfs_lock_inumorder(int lock_mode, int subclass)
{
	if (lock_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL))
		lock_mode |= (subclass + XFS_LOCK_INUMORDER) << XFS_IOLOCK_SHIFT;
	if (lock_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL))
		lock_mode |= (subclass + XFS_LOCK_INUMORDER) << XFS_ILOCK_SHIFT;

	return lock_mode;
}

/*
 * The following routine will lock n inodes in exclusive mode.
 * We assume the caller calls us with the inodes in i_ino order.
 *
 * We need to detect deadlock where an inode that we lock
 * is in the AIL and we start waiting for another inode that is locked
 * by a thread in a long running transaction (such as truncate). This can
 * result in deadlock since the long running trans might need to wait
 * for the inode we just locked in order to push the tail and free space
 * in the log.
 */
void
xfs_lock_inodes(
	xfs_inode_t	**ips,
	int		inodes,
	uint		lock_mode)
{
	int		attempts = 0, i, j, try_lock;
	xfs_log_item_t	*lp;

	ASSERT(ips && (inodes >= 2)); /* we need at least two */

	try_lock = 0;
	i = 0;

again:
	for (; i < inodes; i++) {
		ASSERT(ips[i]);

		if (i && (ips[i] == ips[i-1]))	/* Already locked */
			continue;

		/*
		 * If try_lock is not set yet, make sure all locked inodes
		 * are not in the AIL.
		 * If any are, set try_lock to be used later.
		 */

		if (!try_lock) {
			for (j = (i - 1); j >= 0 && !try_lock; j--) {
				lp = (xfs_log_item_t *)ips[j]->i_itemp;
				if (lp && (lp->li_flags & XFS_LI_IN_AIL)) {
					try_lock++;
				}
			}
		}

		/*
		 * If any of the previous locks we have locked is in the AIL,
		 * we must TRY to get the second and subsequent locks. If
		 * we can't get any, we must release all we have
		 * and try again.
		 */

		if (try_lock) {
			/* try_lock must be 0 if i is 0. */
			/*
			 * try_lock means we have an inode locked
			 * that is in the AIL.
			 */
			ASSERT(i != 0);
			if (!xfs_ilock_nowait(ips[i], xfs_lock_inumorder(lock_mode, i))) {
				attempts++;

				/*
				 * Unlock all previous guys and try again.
				 * xfs_iunlock will try to push the tail
				 * if the inode is in the AIL.
				 */

				for(j = i - 1; j >= 0; j--) {

					/*
					 * Check to see if we've already
					 * unlocked this one.
					 * Not the first one going back,
					 * and the inode ptr is the same.
					 */
					if ((j != (i - 1)) && ips[j] ==
								ips[j+1])
						continue;

					xfs_iunlock(ips[j], lock_mode);
				}

				if ((attempts % 5) == 0) {
					delay(1); /* Don't just spin the CPU */
#ifdef DEBUG
					xfs_lock_delays++;
#endif
				}
				i = 0;
				try_lock = 0;
				goto again;
			}
		} else {
			xfs_ilock(ips[i], xfs_lock_inumorder(lock_mode, i));
		}
	}

#ifdef DEBUG
	if (attempts) {
		if (attempts < 5) xfs_small_retries++;
		else if (attempts < 100) xfs_middle_retries++;
		else xfs_lots_retries++;
	} else {
		xfs_locked_n++;
	}
#endif
}

/*
 * xfs_lock_two_inodes() can only be used to lock one type of lock
 * at a time - the iolock or the ilock, but not both at once. If
 * we lock both at once, lockdep will report false positives saying
 * we have violated locking orders.
 */
void
xfs_lock_two_inodes(
	xfs_inode_t		*ip0,
	xfs_inode_t		*ip1,
	uint			lock_mode)
{
	xfs_inode_t		*temp;
	int			attempts = 0;
	xfs_log_item_t		*lp;

	if (lock_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL))
		ASSERT((lock_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)) == 0);
	ASSERT(ip0->i_ino != ip1->i_ino);

	if (ip0->i_ino > ip1->i_ino) {
		temp = ip0;
		ip0 = ip1;
		ip1 = temp;
	}

 again:
	xfs_ilock(ip0, xfs_lock_inumorder(lock_mode, 0));

	/*
	 * If the first lock we have locked is in the AIL, we must TRY to get
	 * the second lock. If we can't get it, we must release the first one
	 * and try again.
	 */
	lp = (xfs_log_item_t *)ip0->i_itemp;
	if (lp && (lp->li_flags & XFS_LI_IN_AIL)) {
		if (!xfs_ilock_nowait(ip1, xfs_lock_inumorder(lock_mode, 1))) {
			xfs_iunlock(ip0, lock_mode);
			if ((++attempts % 5) == 0)
				delay(1); /* Don't just spin the CPU */
			goto again;
		}
	} else {
		xfs_ilock(ip1, xfs_lock_inumorder(lock_mode, 1));
	}
}

int
xfs_remove(
	xfs_inode_t             *dp,
	struct xfs_name		*name,
	xfs_inode_t		*ip)
{
	xfs_mount_t		*mp = dp->i_mount;
	xfs_trans_t             *tp = NULL;
	int			is_dir = S_ISDIR(ip->i_d.di_mode);
	int                     error = 0;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			committed;
	int			link_zero;
	uint			resblks;
	uint			log_count;

	trace_xfs_remove(dp, name);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	error = xfs_qm_dqattach(dp, 0);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		goto std_return;

	if (is_dir) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_RMDIR);
		log_count = XFS_DEFAULT_LOG_COUNT;
	} else {
		tp = xfs_trans_alloc(mp, XFS_TRANS_REMOVE);
		log_count = XFS_REMOVE_LOG_COUNT;
	}
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;

	/*
	 * We try to get the real space reservation first,
	 * allowing for directory btree deletion(s) implying
	 * possible bmap insert(s).  If we can't get the space
	 * reservation then we use 0 instead, and avoid the bmap
	 * btree insert(s) in the directory code by, if the bmap
	 * insert tries to happen, instead trimming the LAST
	 * block from the directory.
	 */
	resblks = XFS_REMOVE_SPACE_RES(mp);
	error = xfs_trans_reserve(tp, resblks, XFS_REMOVE_LOG_RES(mp), 0,
				  XFS_TRANS_PERM_LOG_RES, log_count);
	if (error == ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_REMOVE_LOG_RES(mp), 0,
					  XFS_TRANS_PERM_LOG_RES, log_count);
	}
	if (error) {
		ASSERT(error != ENOSPC);
		cancel_flags = 0;
		goto out_trans_cancel;
	}

	xfs_lock_two_inodes(dp, ip, XFS_ILOCK_EXCL);

	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	/*
	 * If we're removing a directory perform some additional validation.
	 */
	if (is_dir) {
		ASSERT(ip->i_d.di_nlink >= 2);
		if (ip->i_d.di_nlink != 2) {
			error = XFS_ERROR(ENOTEMPTY);
			goto out_trans_cancel;
		}
		if (!xfs_dir_isempty(ip)) {
			error = XFS_ERROR(ENOTEMPTY);
			goto out_trans_cancel;
		}
	}

	xfs_bmap_init(&free_list, &first_block);
	error = xfs_dir_removename(tp, dp, name, ip->i_ino,
					&first_block, &free_list, resblks);
	if (error) {
		ASSERT(error != ENOENT);
		goto out_bmap_cancel;
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	if (is_dir) {
		/*
		 * Drop the link from ip's "..".
		 */
		error = xfs_droplink(tp, dp);
		if (error)
			goto out_bmap_cancel;

		/*
		 * Drop the "." link from ip to self.
		 */
		error = xfs_droplink(tp, ip);
		if (error)
			goto out_bmap_cancel;
	} else {
		/*
		 * When removing a non-directory we need to log the parent
		 * inode here.  For a directory this is done implicitly
		 * by the xfs_droplink call for the ".." entry.
		 */
		xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
	}

	/*
	 * Drop the link from dp to ip.
	 */
	error = xfs_droplink(tp, ip);
	if (error)
		goto out_bmap_cancel;

	/*
	 * Determine if this is the last link while
	 * we are in the transaction.
	 */
	link_zero = (ip->i_d.di_nlink == 0);

	/*
	 * If this is a synchronous mount, make sure that the
	 * remove transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error)
		goto out_bmap_cancel;

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error)
		goto std_return;

	/*
	 * If we are using filestreams, kill the stream association.
	 * If the file is still open it may get a new one but that
	 * will get killed on last close in xfs_close() so we don't
	 * have to worry about that.
	 */
	if (!is_dir && link_zero && xfs_inode_is_filestream(ip))
		xfs_filestream_deassociate(ip);

	return 0;

 out_bmap_cancel:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
 out_trans_cancel:
	xfs_trans_cancel(tp, cancel_flags);
 std_return:
	return error;
}

int
xfs_link(
	xfs_inode_t		*tdp,
	xfs_inode_t		*sip,
	struct xfs_name		*target_name)
{
	xfs_mount_t		*mp = tdp->i_mount;
	xfs_trans_t		*tp;
	int			error;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			committed;
	int			resblks;

	trace_xfs_link(tdp, target_name);

	ASSERT(!S_ISDIR(sip->i_d.di_mode));

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	error = xfs_qm_dqattach(sip, 0);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(tdp, 0);
	if (error)
		goto std_return;

	tp = xfs_trans_alloc(mp, XFS_TRANS_LINK);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	resblks = XFS_LINK_SPACE_RES(mp, target_name->len);
	error = xfs_trans_reserve(tp, resblks, XFS_LINK_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_LINK_LOG_COUNT);
	if (error == ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_LINK_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_LINK_LOG_COUNT);
	}
	if (error) {
		cancel_flags = 0;
		goto error_return;
	}

	xfs_lock_two_inodes(sip, tdp, XFS_ILOCK_EXCL);

	xfs_trans_ijoin(tp, sip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, tdp, XFS_ILOCK_EXCL);

	/*
	 * If we are using project inheritance, we only allow hard link
	 * creation in our tree when the project IDs are the same; else
	 * the tree quota mechanism could be circumvented.
	 */
	if (unlikely((tdp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
		     (xfs_get_projid(tdp) != xfs_get_projid(sip)))) {
		error = XFS_ERROR(EXDEV);
		goto error_return;
	}

	error = xfs_dir_canenter(tp, tdp, target_name, resblks);
	if (error)
		goto error_return;

	xfs_bmap_init(&free_list, &first_block);

	error = xfs_dir_createname(tp, tdp, target_name, sip->i_ino,
					&first_block, &free_list, resblks);
	if (error)
		goto abort_return;
	xfs_trans_ichgtime(tp, tdp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, tdp, XFS_ILOG_CORE);

	error = xfs_bumplink(tp, sip);
	if (error)
		goto abort_return;

	/*
	 * If this is a synchronous mount, make sure that the
	 * link transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish (&tp, &free_list, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		goto abort_return;
	}

	return xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
 error_return:
	xfs_trans_cancel(tp, cancel_flags);
 std_return:
	return error;
}

int
xfs_symlink(
	xfs_inode_t		*dp,
	struct xfs_name		*link_name,
	const char		*target_path,
	umode_t			mode,
	xfs_inode_t		**ipp)
{
	xfs_mount_t		*mp = dp->i_mount;
	xfs_trans_t		*tp;
	xfs_inode_t		*ip;
	int			error;
	int			pathlen;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	boolean_t		unlock_dp_on_error = B_FALSE;
	uint			cancel_flags;
	int			committed;
	xfs_fileoff_t		first_fsb;
	xfs_filblks_t		fs_blocks;
	int			nmaps;
	xfs_bmbt_irec_t		mval[SYMLINK_MAPS];
	xfs_daddr_t		d;
	const char		*cur_chunk;
	int			byte_cnt;
	int			n;
	xfs_buf_t		*bp;
	prid_t			prid;
	struct xfs_dquot	*udqp, *gdqp;
	uint			resblks;

	*ipp = NULL;
	error = 0;
	ip = NULL;
	tp = NULL;

	trace_xfs_symlink(dp, link_name);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	/*
	 * Check component lengths of the target path name.
	 */
	pathlen = strlen(target_path);
	if (pathlen >= MAXPATHLEN)      /* total string too long */
		return XFS_ERROR(ENAMETOOLONG);

	udqp = gdqp = NULL;
	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		prid = xfs_get_projid(dp);
	else
		prid = XFS_PROJID_DEFAULT;

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, current_fsuid(), current_fsgid(), prid,
			XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT, &udqp, &gdqp);
	if (error)
		goto std_return;

	tp = xfs_trans_alloc(mp, XFS_TRANS_SYMLINK);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	/*
	 * The symlink will fit into the inode data fork?
	 * There can't be any attributes so we get the whole variable part.
	 */
	if (pathlen <= XFS_LITINO(mp))
		fs_blocks = 0;
	else
		fs_blocks = XFS_B_TO_FSB(mp, pathlen);
	resblks = XFS_SYMLINK_SPACE_RES(mp, link_name->len, fs_blocks);
	error = xfs_trans_reserve(tp, resblks, XFS_SYMLINK_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_SYMLINK_LOG_COUNT);
	if (error == ENOSPC && fs_blocks == 0) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_SYMLINK_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_SYMLINK_LOG_COUNT);
	}
	if (error) {
		cancel_flags = 0;
		goto error_return;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	unlock_dp_on_error = B_TRUE;

	/*
	 * Check whether the directory allows new symlinks or not.
	 */
	if (dp->i_d.di_flags & XFS_DIFLAG_NOSYMLINKS) {
		error = XFS_ERROR(EPERM);
		goto error_return;
	}

	/*
	 * Reserve disk quota : blocks and inode.
	 */
	error = xfs_trans_reserve_quota(tp, mp, udqp, gdqp, resblks, 1, 0);
	if (error)
		goto error_return;

	/*
	 * Check for ability to enter directory entry, if no space reserved.
	 */
	error = xfs_dir_canenter(tp, dp, link_name, resblks);
	if (error)
		goto error_return;
	/*
	 * Initialize the bmap freelist prior to calling either
	 * bmapi or the directory create code.
	 */
	xfs_bmap_init(&free_list, &first_block);

	/*
	 * Allocate an inode for the symlink.
	 */
	error = xfs_dir_ialloc(&tp, dp, S_IFLNK | (mode & ~S_IFMT), 1, 0,
			       prid, resblks > 0, &ip, NULL);
	if (error) {
		if (error == ENOSPC)
			goto error_return;
		goto error1;
	}

	/*
	 * An error after we've joined dp to the transaction will result in the
	 * transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	unlock_dp_on_error = B_FALSE;

	/*
	 * Also attach the dquot(s) to it, if applicable.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp);

	if (resblks)
		resblks -= XFS_IALLOC_SPACE_RES(mp);
	/*
	 * If the symlink will fit into the inode, write it inline.
	 */
	if (pathlen <= XFS_IFORK_DSIZE(ip)) {
		xfs_idata_realloc(ip, pathlen, XFS_DATA_FORK);
		memcpy(ip->i_df.if_u1.if_data, target_path, pathlen);
		ip->i_d.di_size = pathlen;

		/*
		 * The inode was initially created in extent format.
		 */
		ip->i_df.if_flags &= ~(XFS_IFEXTENTS | XFS_IFBROOT);
		ip->i_df.if_flags |= XFS_IFINLINE;

		ip->i_d.di_format = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_DDATA | XFS_ILOG_CORE);

	} else {
		first_fsb = 0;
		nmaps = SYMLINK_MAPS;

		error = xfs_bmapi_write(tp, ip, first_fsb, fs_blocks,
				  XFS_BMAPI_METADATA, &first_block, resblks,
				  mval, &nmaps, &free_list);
		if (error)
			goto error2;

		if (resblks)
			resblks -= fs_blocks;
		ip->i_d.di_size = pathlen;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

		cur_chunk = target_path;
		for (n = 0; n < nmaps; n++) {
			d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
			byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);
			bp = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
					       BTOBB(byte_cnt), 0);
			if (!bp) {
				error = ENOMEM;
				goto error2;
			}
			if (pathlen < byte_cnt) {
				byte_cnt = pathlen;
			}
			pathlen -= byte_cnt;

			memcpy(bp->b_addr, cur_chunk, byte_cnt);
			cur_chunk += byte_cnt;

			xfs_trans_log_buf(tp, bp, 0, byte_cnt - 1);
		}
	}

	/*
	 * Create the directory entry for the symlink.
	 */
	error = xfs_dir_createname(tp, dp, link_name, ip->i_ino,
					&first_block, &free_list, resblks);
	if (error)
		goto error2;
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	/*
	 * If this is a synchronous mount, make sure that the
	 * symlink transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error) {
		goto error2;
	}
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	*ipp = ip;
	return 0;

 error2:
	IRELE(ip);
 error1:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	if (unlock_dp_on_error)
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
 std_return:
	return error;
}

int
xfs_set_dmattrs(
	xfs_inode_t     *ip,
	u_int		evmask,
	u_int16_t	state)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_trans_t	*tp;
	int		error;

	if (!capable(CAP_SYS_ADMIN))
		return XFS_ERROR(EPERM);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	tp = xfs_trans_alloc(mp, XFS_TRANS_SET_DMATTRS);
	error = xfs_trans_reserve(tp, 0, XFS_ICHANGE_LOG_RES (mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	ip->i_d.di_dmevmask = evmask;
	ip->i_d.di_dmstate  = state;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	error = xfs_trans_commit(tp, 0);

	return error;
}

/*
 * xfs_alloc_file_space()
 *      This routine allocates disk space for the given file.
 *
 *	If alloc_type == 0, this request is for an ALLOCSP type
 *	request which will change the file size.  In this case, no
 *	DMAPI event will be generated by the call.  A TRUNCATE event
 *	will be generated later by xfs_setattr.
 *
 *	If alloc_type != 0, this request is for a RESVSP type
 *	request, and a DMAPI DM_EVENT_WRITE will be generated if the
 *	lower block boundary byte address is less than the file's
 *	length.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
STATIC int
xfs_alloc_file_space(
	xfs_inode_t		*ip,
	xfs_off_t		offset,
	xfs_off_t		len,
	int			alloc_type,
	int			attr_flags)
{
	xfs_mount_t		*mp = ip->i_mount;
	xfs_off_t		count;
	xfs_filblks_t		allocated_fsb;
	xfs_filblks_t		allocatesize_fsb;
	xfs_extlen_t		extsz, temp;
	xfs_fileoff_t		startoffset_fsb;
	xfs_fsblock_t		firstfsb;
	int			nimaps;
	int			quota_flag;
	int			rt;
	xfs_trans_t		*tp;
	xfs_bmbt_irec_t		imaps[1], *imapp;
	xfs_bmap_free_t		free_list;
	uint			qblocks, resblks, resrtextents;
	int			committed;
	int			error;

	trace_xfs_alloc_file_space(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	if (len <= 0)
		return XFS_ERROR(EINVAL);

	rt = XFS_IS_REALTIME_INODE(ip);
	extsz = xfs_get_extsz_hint(ip);

	count = len;
	imapp = &imaps[0];
	nimaps = 1;
	startoffset_fsb	= XFS_B_TO_FSBT(mp, offset);
	allocatesize_fsb = XFS_B_TO_FSB(mp, count);

	/*
	 * Allocate file space until done or until there is an error
	 */
	while (allocatesize_fsb && !error) {
		xfs_fileoff_t	s, e;

		/*
		 * Determine space reservations for data/realtime.
		 */
		if (unlikely(extsz)) {
			s = startoffset_fsb;
			do_div(s, extsz);
			s *= extsz;
			e = startoffset_fsb + allocatesize_fsb;
			if ((temp = do_mod(startoffset_fsb, extsz)))
				e += temp;
			if ((temp = do_mod(e, extsz)))
				e += extsz - temp;
		} else {
			s = 0;
			e = allocatesize_fsb;
		}

		/*
		 * The transaction reservation is limited to a 32-bit block
		 * count, hence we need to limit the number of blocks we are
		 * trying to reserve to avoid an overflow. We can't allocate
		 * more than @nimaps extents, and an extent is limited on disk
		 * to MAXEXTLEN (21 bits), so use that to enforce the limit.
		 */
		resblks = min_t(xfs_fileoff_t, (e - s), (MAXEXTLEN * nimaps));
		if (unlikely(rt)) {
			resrtextents = qblocks = resblks;
			resrtextents /= mp->m_sb.sb_rextsize;
			resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
			quota_flag = XFS_QMOPT_RES_RTBLKS;
		} else {
			resrtextents = 0;
			resblks = qblocks = XFS_DIOSTRAT_SPACE_RES(mp, resblks);
			quota_flag = XFS_QMOPT_RES_REGBLKS;
		}

		/*
		 * Allocate and setup the transaction.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		error = xfs_trans_reserve(tp, resblks,
					  XFS_WRITE_LOG_RES(mp), resrtextents,
					  XFS_TRANS_PERM_LOG_RES,
					  XFS_WRITE_LOG_COUNT);
		/*
		 * Check for running out of space
		 */
		if (error) {
			/*
			 * Free the transaction structure.
			 */
			ASSERT(error == ENOSPC || XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			break;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_trans_reserve_quota_nblks(tp, ip, qblocks,
						      0, quota_flag);
		if (error)
			goto error1;

		xfs_trans_ijoin(tp, ip, 0);

		xfs_bmap_init(&free_list, &firstfsb);
		error = xfs_bmapi_write(tp, ip, startoffset_fsb,
					allocatesize_fsb, alloc_type, &firstfsb,
					0, imapp, &nimaps, &free_list);
		if (error) {
			goto error0;
		}

		/*
		 * Complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error) {
			break;
		}

		allocated_fsb = imapp->br_blockcount;

		if (nimaps == 0) {
			error = XFS_ERROR(ENOSPC);
			break;
		}

		startoffset_fsb += allocated_fsb;
		allocatesize_fsb -= allocated_fsb;
	}

	return error;

error0:	/* Cancel bmap, unlock inode, unreserve quota blocks, cancel trans */
	xfs_bmap_cancel(&free_list);
	xfs_trans_unreserve_quota_nblks(tp, ip, (long)qblocks, 0, quota_flag);

error1:	/* Just cancel transaction */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Zero file bytes between startoff and endoff inclusive.
 * The iolock is held exclusive and no blocks are buffered.
 *
 * This function is used by xfs_free_file_space() to zero
 * partial blocks when the range to free is not block aligned.
 * When unreserving space with boundaries that are not block
 * aligned we round up the start and round down the end
 * boundaries and then use this function to zero the parts of
 * the blocks that got dropped during the rounding.
 */
STATIC int
xfs_zero_remaining_bytes(
	xfs_inode_t		*ip,
	xfs_off_t		startoff,
	xfs_off_t		endoff)
{
	xfs_bmbt_irec_t		imap;
	xfs_fileoff_t		offset_fsb;
	xfs_off_t		lastoffset;
	xfs_off_t		offset;
	xfs_buf_t		*bp;
	xfs_mount_t		*mp = ip->i_mount;
	int			nimap;
	int			error = 0;

	/*
	 * Avoid doing I/O beyond eof - it's not necessary
	 * since nothing can read beyond eof.  The space will
	 * be zeroed when the file is extended anyway.
	 */
	if (startoff >= XFS_ISIZE(ip))
		return 0;

	if (endoff > XFS_ISIZE(ip))
		endoff = XFS_ISIZE(ip);

	bp = xfs_buf_get_uncached(XFS_IS_REALTIME_INODE(ip) ?
					mp->m_rtdev_targp : mp->m_ddev_targp,
				  BTOBB(mp->m_sb.sb_blocksize), 0);
	if (!bp)
		return XFS_ERROR(ENOMEM);

	xfs_buf_unlock(bp);

	for (offset = startoff; offset <= endoff; offset = lastoffset + 1) {
		offset_fsb = XFS_B_TO_FSBT(mp, offset);
		nimap = 1;
		error = xfs_bmapi_read(ip, offset_fsb, 1, &imap, &nimap, 0);
		if (error || nimap < 1)
			break;
		ASSERT(imap.br_blockcount >= 1);
		ASSERT(imap.br_startoff == offset_fsb);
		lastoffset = XFS_FSB_TO_B(mp, imap.br_startoff + 1) - 1;
		if (lastoffset > endoff)
			lastoffset = endoff;
		if (imap.br_startblock == HOLESTARTBLOCK)
			continue;
		ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
		if (imap.br_state == XFS_EXT_UNWRITTEN)
			continue;
		XFS_BUF_UNDONE(bp);
		XFS_BUF_UNWRITE(bp);
		XFS_BUF_READ(bp);
		XFS_BUF_SET_ADDR(bp, xfs_fsb_to_db(ip, imap.br_startblock));
		xfsbdstrat(mp, bp);
		error = xfs_buf_iowait(bp);
		if (error) {
			xfs_buf_ioerror_alert(bp,
					"xfs_zero_remaining_bytes(read)");
			break;
		}
		memset(bp->b_addr +
			(offset - XFS_FSB_TO_B(mp, imap.br_startoff)),
		      0, lastoffset - offset + 1);
		XFS_BUF_UNDONE(bp);
		XFS_BUF_UNREAD(bp);
		XFS_BUF_WRITE(bp);
		xfsbdstrat(mp, bp);
		error = xfs_buf_iowait(bp);
		if (error) {
			xfs_buf_ioerror_alert(bp,
					"xfs_zero_remaining_bytes(write)");
			break;
		}
	}
	xfs_buf_free(bp);
	return error;
}

/*
 * xfs_free_file_space()
 *      This routine frees disk space for the given file.
 *
 *	This routine is only called by xfs_change_file_space
 *	for an UNRESVSP type call.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
STATIC int
xfs_free_file_space(
	xfs_inode_t		*ip,
	xfs_off_t		offset,
	xfs_off_t		len,
	int			attr_flags)
{
	int			committed;
	int			done;
	xfs_fileoff_t		endoffset_fsb;
	int			error;
	xfs_fsblock_t		firstfsb;
	xfs_bmap_free_t		free_list;
	xfs_bmbt_irec_t		imap;
	xfs_off_t		ioffset;
	xfs_extlen_t		mod=0;
	xfs_mount_t		*mp;
	int			nimap;
	uint			resblks;
	uint			rounding;
	int			rt;
	xfs_fileoff_t		startoffset_fsb;
	xfs_trans_t		*tp;
	int			need_iolock = 1;

	mp = ip->i_mount;

	trace_xfs_free_file_space(ip);

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	error = 0;
	if (len <= 0)	/* if nothing being freed */
		return error;
	rt = XFS_IS_REALTIME_INODE(ip);
	startoffset_fsb	= XFS_B_TO_FSB(mp, offset);
	endoffset_fsb = XFS_B_TO_FSBT(mp, offset + len);

	if (attr_flags & XFS_ATTR_NOLOCK)
		need_iolock = 0;
	if (need_iolock) {
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
		/* wait for the completion of any pending DIOs */
		inode_dio_wait(VFS_I(ip));
	}

	rounding = max_t(uint, 1 << mp->m_sb.sb_blocklog, PAGE_CACHE_SIZE);
	ioffset = offset & ~(rounding - 1);

	if (VN_CACHED(VFS_I(ip)) != 0) {
		error = xfs_flushinval_pages(ip, ioffset, -1, FI_REMAPF_LOCKED);
		if (error)
			goto out_unlock_iolock;
	}

	/*
	 * Need to zero the stuff we're not freeing, on disk.
	 * If it's a realtime file & can't use unwritten extents then we
	 * actually need to zero the extent edges.  Otherwise xfs_bunmapi
	 * will take care of it for us.
	 */
	if (rt && !xfs_sb_version_hasextflgbit(&mp->m_sb)) {
		nimap = 1;
		error = xfs_bmapi_read(ip, startoffset_fsb, 1,
					&imap, &nimap, 0);
		if (error)
			goto out_unlock_iolock;
		ASSERT(nimap == 0 || nimap == 1);
		if (nimap && imap.br_startblock != HOLESTARTBLOCK) {
			xfs_daddr_t	block;

			ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
			block = imap.br_startblock;
			mod = do_div(block, mp->m_sb.sb_rextsize);
			if (mod)
				startoffset_fsb += mp->m_sb.sb_rextsize - mod;
		}
		nimap = 1;
		error = xfs_bmapi_read(ip, endoffset_fsb - 1, 1,
					&imap, &nimap, 0);
		if (error)
			goto out_unlock_iolock;
		ASSERT(nimap == 0 || nimap == 1);
		if (nimap && imap.br_startblock != HOLESTARTBLOCK) {
			ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
			mod++;
			if (mod && (mod != mp->m_sb.sb_rextsize))
				endoffset_fsb -= mod;
		}
	}
	if ((done = (endoffset_fsb <= startoffset_fsb)))
		/*
		 * One contiguous piece to clear
		 */
		error = xfs_zero_remaining_bytes(ip, offset, offset + len - 1);
	else {
		/*
		 * Some full blocks, possibly two pieces to clear
		 */
		if (offset < XFS_FSB_TO_B(mp, startoffset_fsb))
			error = xfs_zero_remaining_bytes(ip, offset,
				XFS_FSB_TO_B(mp, startoffset_fsb) - 1);
		if (!error &&
		    XFS_FSB_TO_B(mp, endoffset_fsb) < offset + len)
			error = xfs_zero_remaining_bytes(ip,
				XFS_FSB_TO_B(mp, endoffset_fsb),
				offset + len - 1);
	}

	/*
	 * free file space until done or until there is an error
	 */
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
	while (!error && !done) {

		/*
		 * allocate and setup the transaction. Allow this
		 * transaction to dip into the reserve blocks to ensure
		 * the freeing of the space succeeds at ENOSPC.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		tp->t_flags |= XFS_TRANS_RESERVE;
		error = xfs_trans_reserve(tp,
					  resblks,
					  XFS_WRITE_LOG_RES(mp),
					  0,
					  XFS_TRANS_PERM_LOG_RES,
					  XFS_WRITE_LOG_COUNT);

		/*
		 * check for running out of space
		 */
		if (error) {
			/*
			 * Free the transaction structure.
			 */
			ASSERT(error == ENOSPC || XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			break;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_trans_reserve_quota(tp, mp,
				ip->i_udquot, ip->i_gdquot,
				resblks, 0, XFS_QMOPT_RES_REGBLKS);
		if (error)
			goto error1;

		xfs_trans_ijoin(tp, ip, 0);

		/*
		 * issue the bunmapi() call to free the blocks
		 */
		xfs_bmap_init(&free_list, &firstfsb);
		error = xfs_bunmapi(tp, ip, startoffset_fsb,
				  endoffset_fsb - startoffset_fsb,
				  0, 2, &firstfsb, &free_list, &done);
		if (error) {
			goto error0;
		}

		/*
		 * complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

 out_unlock_iolock:
	if (need_iolock)
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;

 error0:
	xfs_bmap_cancel(&free_list);
 error1:
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, need_iolock ? (XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL) :
		    XFS_ILOCK_EXCL);
	return error;
}

/*
 * xfs_change_file_space()
 *      This routine allocates or frees disk space for the given file.
 *      The user specified parameters are checked for alignment and size
 *      limitations.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
int
xfs_change_file_space(
	xfs_inode_t	*ip,
	int		cmd,
	xfs_flock64_t	*bf,
	xfs_off_t	offset,
	int		attr_flags)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		clrprealloc;
	int		error;
	xfs_fsize_t	fsize;
	int		setprealloc;
	xfs_off_t	startoffset;
	xfs_off_t	llen;
	xfs_trans_t	*tp;
	struct iattr	iattr;
	int		prealloc_type;

	if (!S_ISREG(ip->i_d.di_mode))
		return XFS_ERROR(EINVAL);

	switch (bf->l_whence) {
	case 0: /*SEEK_SET*/
		break;
	case 1: /*SEEK_CUR*/
		bf->l_start += offset;
		break;
	case 2: /*SEEK_END*/
		bf->l_start += XFS_ISIZE(ip);
		break;
	default:
		return XFS_ERROR(EINVAL);
	}

	llen = bf->l_len > 0 ? bf->l_len - 1 : bf->l_len;

	if (bf->l_start < 0 ||
	    bf->l_start > mp->m_super->s_maxbytes ||
	    bf->l_start + llen < 0 ||
	    bf->l_start + llen > mp->m_super->s_maxbytes)
		return XFS_ERROR(EINVAL);

	bf->l_whence = 0;

	startoffset = bf->l_start;
	fsize = XFS_ISIZE(ip);

	/*
	 * XFS_IOC_RESVSP and XFS_IOC_UNRESVSP will reserve or unreserve
	 * file space.
	 * These calls do NOT zero the data space allocated to the file,
	 * nor do they change the file size.
	 *
	 * XFS_IOC_ALLOCSP and XFS_IOC_FREESP will allocate and free file
	 * space.
	 * These calls cause the new file data to be zeroed and the file
	 * size to be changed.
	 */
	setprealloc = clrprealloc = 0;
	prealloc_type = XFS_BMAPI_PREALLOC;

	switch (cmd) {
	case XFS_IOC_ZERO_RANGE:
		prealloc_type |= XFS_BMAPI_CONVERT;
		xfs_tosspages(ip, startoffset, startoffset + bf->l_len, 0);
		/* FALLTHRU */
	case XFS_IOC_RESVSP:
	case XFS_IOC_RESVSP64:
		error = xfs_alloc_file_space(ip, startoffset, bf->l_len,
						prealloc_type, attr_flags);
		if (error)
			return error;
		setprealloc = 1;
		break;

	case XFS_IOC_UNRESVSP:
	case XFS_IOC_UNRESVSP64:
		if ((error = xfs_free_file_space(ip, startoffset, bf->l_len,
								attr_flags)))
			return error;
		break;

	case XFS_IOC_ALLOCSP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP:
	case XFS_IOC_FREESP64:
		/*
		 * These operations actually do IO when extending the file, but
		 * the allocation is done seperately to the zeroing that is
		 * done. This set of operations need to be serialised against
		 * other IO operations, such as truncate and buffered IO. We
		 * need to take the IOLOCK here to serialise the allocation and
		 * zeroing IO to prevent other IOLOCK holders (e.g. getbmap,
		 * truncate, direct IO) from racing against the transient
		 * allocated but not written state we can have here.
		 */
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
		if (startoffset > fsize) {
			error = xfs_alloc_file_space(ip, fsize,
					startoffset - fsize, 0,
					attr_flags | XFS_ATTR_NOLOCK);
			if (error) {
				xfs_iunlock(ip, XFS_IOLOCK_EXCL);
				break;
			}
		}

		iattr.ia_valid = ATTR_SIZE;
		iattr.ia_size = startoffset;

		error = xfs_setattr_size(ip, &iattr,
					 attr_flags | XFS_ATTR_NOLOCK);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);

		if (error)
			return error;

		clrprealloc = 1;
		break;

	default:
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}

	/*
	 * update the inode timestamp, mode, and prealloc flag bits
	 */
	tp = xfs_trans_alloc(mp, XFS_TRANS_WRITEID);

	if ((error = xfs_trans_reserve(tp, 0, XFS_WRITEID_LOG_RES(mp),
				      0, 0, 0))) {
		/* ASSERT(0); */
		xfs_trans_cancel(tp, 0);
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	if ((attr_flags & XFS_ATTR_DMI) == 0) {
		ip->i_d.di_mode &= ~S_ISUID;

		/*
		 * Note that we don't have to worry about mandatory
		 * file locking being disabled here because we only
		 * clear the S_ISGID bit if the Group execute bit is
		 * on, but if it was on then mandatory locking wouldn't
		 * have been enabled.
		 */
		if (ip->i_d.di_mode & S_IXGRP)
			ip->i_d.di_mode &= ~S_ISGID;

		xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	}
	if (setprealloc)
		ip->i_d.di_flags |= XFS_DIFLAG_PREALLOC;
	else if (clrprealloc)
		ip->i_d.di_flags &= ~XFS_DIFLAG_PREALLOC;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	if (attr_flags & XFS_ATTR_SYNC)
		xfs_trans_set_sync(tp);
	return xfs_trans_commit(tp, 0);
}
