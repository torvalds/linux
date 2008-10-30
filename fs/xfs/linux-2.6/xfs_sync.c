/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_inode.h"
#include "xfs_dinode.h"
#include "xfs_error.h"
#include "xfs_mru_cache.h"
#include "xfs_filestream.h"
#include "xfs_vnodeops.h"
#include "xfs_utils.h"
#include "xfs_buf_item.h"
#include "xfs_inode_item.h"
#include "xfs_rw.h"

#include <linux/kthread.h>
#include <linux/freezer.h>

/*
 * Sync all the inodes in the given AG according to the
 * direction given by the flags.
 */
STATIC int
xfs_sync_inodes_ag(
	xfs_mount_t	*mp,
	int		ag,
	int		flags)
{
	xfs_perag_t	*pag = &mp->m_perag[ag];
	int		nr_found;
	int		first_index = 0;
	int		error = 0;
	int		last_error = 0;
	int		fflag = XFS_B_ASYNC;
	int		lock_flags = XFS_ILOCK_SHARED;

	if (flags & SYNC_DELWRI)
		fflag = XFS_B_DELWRI;
	if (flags & SYNC_WAIT)
		fflag = 0;		/* synchronous overrides all */

	if (flags & SYNC_DELWRI) {
		/*
		 * We need the I/O lock if we're going to call any of
		 * the flush/inval routines.
		 */
		lock_flags |= XFS_IOLOCK_SHARED;
	}

	do {
		struct inode	*inode;
		boolean_t	inode_refed;
		xfs_inode_t	*ip = NULL;

		/*
		 * use a gang lookup to find the next inode in the tree
		 * as the tree is sparse and a gang lookup walks to find
		 * the number of objects requested.
		 */
		read_lock(&pag->pag_ici_lock);
		nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
				(void**)&ip, first_index, 1);

		if (!nr_found) {
			read_unlock(&pag->pag_ici_lock);
			break;
		}

		/* update the index for the next lookup */
		first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);

		/*
		 * skip inodes in reclaim. Let xfs_syncsub do that for
		 * us so we don't need to worry.
		 */
		if (xfs_iflags_test(ip, (XFS_IRECLAIM|XFS_IRECLAIMABLE))) {
			read_unlock(&pag->pag_ici_lock);
			continue;
		}

		/* bad inodes are dealt with elsewhere */
		inode = VFS_I(ip);
		if (is_bad_inode(inode)) {
			read_unlock(&pag->pag_ici_lock);
			continue;
		}

		/* nothing to sync during shutdown */
		if (XFS_FORCED_SHUTDOWN(mp)) {
			read_unlock(&pag->pag_ici_lock);
			return 0;
		}

		/*
		 * If we can't get a reference on the VFS_I, the inode must be
		 * in reclaim. If we can get the inode lock without blocking,
		 * it is safe to flush the inode because we hold the tree lock
		 * and xfs_iextract will block right now. Hence if we lock the
		 * inode while holding the tree lock, xfs_ireclaim() is
		 * guaranteed to block on the inode lock we now hold and hence
		 * it is safe to reference the inode until we drop the inode
		 * locks completely.
		 */
		inode_refed = B_FALSE;
		if (igrab(inode)) {
			read_unlock(&pag->pag_ici_lock);
			xfs_ilock(ip, lock_flags);
			inode_refed = B_TRUE;
		} else {
			if (!xfs_ilock_nowait(ip, lock_flags)) {
				/* leave it to reclaim */
				read_unlock(&pag->pag_ici_lock);
				continue;
			}
			read_unlock(&pag->pag_ici_lock);
		}

		/*
		 * If we have to flush data or wait for I/O completion
		 * we need to drop the ilock that we currently hold.
		 * If we need to drop the lock, insert a marker if we
		 * have not already done so.
		 */
		if ((flags & SYNC_DELWRI) && VN_DIRTY(inode)) {
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			error = xfs_flush_pages(ip, 0, -1, fflag, FI_NONE);
			if (flags & SYNC_IOWAIT)
				vn_iowait(ip);
			xfs_ilock(ip, XFS_ILOCK_SHARED);
		}

		if ((flags & SYNC_ATTR) && !xfs_inode_clean(ip)) {
			if (flags & SYNC_WAIT) {
				xfs_iflock(ip);
				if (!xfs_inode_clean(ip))
					error = xfs_iflush(ip, XFS_IFLUSH_SYNC);
				else
					xfs_ifunlock(ip);
			} else if (xfs_iflock_nowait(ip)) {
				if (!xfs_inode_clean(ip))
					error = xfs_iflush(ip, XFS_IFLUSH_DELWRI);
				else
					xfs_ifunlock(ip);
			}
		}

		if (lock_flags)
			xfs_iunlock(ip, lock_flags);

		if (inode_refed) {
			IRELE(ip);
		}

		if (error)
			last_error = error;
		/*
		 * bail out if the filesystem is corrupted.
		 */
		if (error == EFSCORRUPTED)
			return XFS_ERROR(error);

	} while (nr_found);

	return last_error;
}

int
xfs_sync_inodes(
	xfs_mount_t	*mp,
	int		flags)
{
	int		error;
	int		last_error;
	int		i;
	int		lflags = XFS_LOG_FORCE;

	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return 0;
	error = 0;
	last_error = 0;

	if (flags & SYNC_WAIT)
		lflags |= XFS_LOG_SYNC;

	for (i = 0; i < mp->m_sb.sb_agcount; i++) {
		if (!mp->m_perag[i].pag_ici_init)
			continue;
		error = xfs_sync_inodes_ag(mp, i, flags);
		if (error)
			last_error = error;
		if (error == EFSCORRUPTED)
			break;
	}
	if (flags & SYNC_DELWRI)
		xfs_log_force(mp, 0, lflags);

	return XFS_ERROR(last_error);
}

STATIC int
xfs_commit_dummy_trans(
	struct xfs_mount	*mp,
	uint			log_flags)
{
	struct xfs_inode	*ip = mp->m_rootip;
	struct xfs_trans	*tp;
	int			error;

	/*
	 * Put a dummy transaction in the log to tell recovery
	 * that all others are OK.
	 */
	tp = xfs_trans_alloc(mp, XFS_TRANS_DUMMY1);
	error = xfs_trans_reserve(tp, 0, XFS_ICHANGE_LOG_RES(mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	/* XXX(hch): ignoring the error here.. */
	error = xfs_trans_commit(tp, 0);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	xfs_log_force(mp, 0, log_flags);
	return 0;
}

int
xfs_sync_fsdata(
	struct xfs_mount	*mp,
	int			flags)
{
	struct xfs_buf		*bp;
	struct xfs_buf_log_item	*bip;
	int			error = 0;

	/*
	 * If this is xfssyncd() then only sync the superblock if we can
	 * lock it without sleeping and it is not pinned.
	 */
	if (flags & SYNC_BDFLUSH) {
		ASSERT(!(flags & SYNC_WAIT));

		bp = xfs_getsb(mp, XFS_BUF_TRYLOCK);
		if (!bp)
			goto out;

		bip = XFS_BUF_FSPRIVATE(bp, struct xfs_buf_log_item *);
		if (!bip || !xfs_buf_item_dirty(bip) || XFS_BUF_ISPINNED(bp))
			goto out_brelse;
	} else {
		bp = xfs_getsb(mp, 0);

		/*
		 * If the buffer is pinned then push on the log so we won't
		 * get stuck waiting in the write for someone, maybe
		 * ourselves, to flush the log.
		 *
		 * Even though we just pushed the log above, we did not have
		 * the superblock buffer locked at that point so it can
		 * become pinned in between there and here.
		 */
		if (XFS_BUF_ISPINNED(bp))
			xfs_log_force(mp, 0, XFS_LOG_FORCE);
	}


	if (flags & SYNC_WAIT)
		XFS_BUF_UNASYNC(bp);
	else
		XFS_BUF_ASYNC(bp);

	return xfs_bwrite(mp, bp);

 out_brelse:
	xfs_buf_relse(bp);
 out:
	return error;
}

/*
 * When remounting a filesystem read-only or freezing the filesystem, we have
 * two phases to execute. This first phase is syncing the data before we
 * quiesce the filesystem, and the second is flushing all the inodes out after
 * we've waited for all the transactions created by the first phase to
 * complete. The second phase ensures that the inodes are written to their
 * location on disk rather than just existing in transactions in the log. This
 * means after a quiesce there is no log replay required to write the inodes to
 * disk (this is the main difference between a sync and a quiesce).
 */
/*
 * First stage of freeze - no writers will make progress now we are here,
 * so we flush delwri and delalloc buffers here, then wait for all I/O to
 * complete.  Data is frozen at that point. Metadata is not frozen,
 * transactions can still occur here so don't bother flushing the buftarg
 * because it'll just get dirty again.
 */
int
xfs_quiesce_data(
	struct xfs_mount	*mp)
{
	int error;

	/* push non-blocking */
	xfs_sync_inodes(mp, SYNC_DELWRI|SYNC_BDFLUSH);
	XFS_QM_DQSYNC(mp, SYNC_BDFLUSH);
	xfs_filestream_flush(mp);

	/* push and block */
	xfs_sync_inodes(mp, SYNC_DELWRI|SYNC_WAIT|SYNC_IOWAIT);
	XFS_QM_DQSYNC(mp, SYNC_WAIT);

	/* write superblock and hoover up shutdown errors */
	error = xfs_sync_fsdata(mp, 0);

	/* flush data-only devices */
	if (mp->m_rtdev_targp)
		XFS_bflush(mp->m_rtdev_targp);

	return error;
}

/*
 * Enqueue a work item to be picked up by the vfs xfssyncd thread.
 * Doing this has two advantages:
 * - It saves on stack space, which is tight in certain situations
 * - It can be used (with care) as a mechanism to avoid deadlocks.
 * Flushing while allocating in a full filesystem requires both.
 */
STATIC void
xfs_syncd_queue_work(
	struct xfs_mount *mp,
	void		*data,
	void		(*syncer)(struct xfs_mount *, void *))
{
	struct bhv_vfs_sync_work *work;

	work = kmem_alloc(sizeof(struct bhv_vfs_sync_work), KM_SLEEP);
	INIT_LIST_HEAD(&work->w_list);
	work->w_syncer = syncer;
	work->w_data = data;
	work->w_mount = mp;
	spin_lock(&mp->m_sync_lock);
	list_add_tail(&work->w_list, &mp->m_sync_list);
	spin_unlock(&mp->m_sync_lock);
	wake_up_process(mp->m_sync_task);
}

/*
 * Flush delayed allocate data, attempting to free up reserved space
 * from existing allocations.  At this point a new allocation attempt
 * has failed with ENOSPC and we are in the process of scratching our
 * heads, looking about for more room...
 */
STATIC void
xfs_flush_inode_work(
	struct xfs_mount *mp,
	void		*arg)
{
	struct inode	*inode = arg;
	filemap_flush(inode->i_mapping);
	iput(inode);
}

void
xfs_flush_inode(
	xfs_inode_t	*ip)
{
	struct inode	*inode = VFS_I(ip);

	igrab(inode);
	xfs_syncd_queue_work(ip->i_mount, inode, xfs_flush_inode_work);
	delay(msecs_to_jiffies(500));
}

/*
 * This is the "bigger hammer" version of xfs_flush_inode_work...
 * (IOW, "If at first you don't succeed, use a Bigger Hammer").
 */
STATIC void
xfs_flush_device_work(
	struct xfs_mount *mp,
	void		*arg)
{
	struct inode	*inode = arg;
	sync_blockdev(mp->m_super->s_bdev);
	iput(inode);
}

void
xfs_flush_device(
	xfs_inode_t	*ip)
{
	struct inode	*inode = VFS_I(ip);

	igrab(inode);
	xfs_syncd_queue_work(ip->i_mount, inode, xfs_flush_device_work);
	delay(msecs_to_jiffies(500));
	xfs_log_force(ip->i_mount, (xfs_lsn_t)0, XFS_LOG_FORCE|XFS_LOG_SYNC);
}

/*
 * Every sync period we need to unpin all items, reclaim inodes, sync
 * quota and write out the superblock. We might need to cover the log
 * to indicate it is idle.
 */
STATIC void
xfs_sync_worker(
	struct xfs_mount *mp,
	void		*unused)
{
	int		error;

	if (!(mp->m_flags & XFS_MOUNT_RDONLY)) {
		xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);
		xfs_finish_reclaim_all(mp, 1, XFS_IFLUSH_DELWRI_ELSE_ASYNC);
		/* dgc: errors ignored here */
		error = XFS_QM_DQSYNC(mp, SYNC_BDFLUSH);
		error = xfs_sync_fsdata(mp, SYNC_BDFLUSH);
		if (xfs_log_need_covered(mp))
			error = xfs_commit_dummy_trans(mp, XFS_LOG_FORCE);
	}
	mp->m_sync_seq++;
	wake_up(&mp->m_wait_single_sync_task);
}

STATIC int
xfssyncd(
	void			*arg)
{
	struct xfs_mount	*mp = arg;
	long			timeleft;
	bhv_vfs_sync_work_t	*work, *n;
	LIST_HEAD		(tmp);

	set_freezable();
	timeleft = xfs_syncd_centisecs * msecs_to_jiffies(10);
	for (;;) {
		timeleft = schedule_timeout_interruptible(timeleft);
		/* swsusp */
		try_to_freeze();
		if (kthread_should_stop() && list_empty(&mp->m_sync_list))
			break;

		spin_lock(&mp->m_sync_lock);
		/*
		 * We can get woken by laptop mode, to do a sync -
		 * that's the (only!) case where the list would be
		 * empty with time remaining.
		 */
		if (!timeleft || list_empty(&mp->m_sync_list)) {
			if (!timeleft)
				timeleft = xfs_syncd_centisecs *
							msecs_to_jiffies(10);
			INIT_LIST_HEAD(&mp->m_sync_work.w_list);
			list_add_tail(&mp->m_sync_work.w_list,
					&mp->m_sync_list);
		}
		list_for_each_entry_safe(work, n, &mp->m_sync_list, w_list)
			list_move(&work->w_list, &tmp);
		spin_unlock(&mp->m_sync_lock);

		list_for_each_entry_safe(work, n, &tmp, w_list) {
			(*work->w_syncer)(mp, work->w_data);
			list_del(&work->w_list);
			if (work == &mp->m_sync_work)
				continue;
			kmem_free(work);
		}
	}

	return 0;
}

int
xfs_syncd_init(
	struct xfs_mount	*mp)
{
	mp->m_sync_work.w_syncer = xfs_sync_worker;
	mp->m_sync_work.w_mount = mp;
	mp->m_sync_task = kthread_run(xfssyncd, mp, "xfssyncd");
	if (IS_ERR(mp->m_sync_task))
		return -PTR_ERR(mp->m_sync_task);
	return 0;
}

void
xfs_syncd_stop(
	struct xfs_mount	*mp)
{
	kthread_stop(mp->m_sync_task);
}

