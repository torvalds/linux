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
	uint32_t	first_index = 0;
	int		error = 0;
	int		last_error = 0;
	int		fflag = XFS_B_ASYNC;

	if (flags & SYNC_DELWRI)
		fflag = XFS_B_DELWRI;
	if (flags & SYNC_WAIT)
		fflag = 0;		/* synchronous overrides all */

	do {
		struct inode	*inode;
		xfs_inode_t	*ip = NULL;
		int		lock_flags = XFS_ILOCK_SHARED;

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

		/*
		 * Update the index for the next lookup. Catch overflows
		 * into the next AG range which can occur if we have inodes
		 * in the last block of the AG and we are currently
		 * pointing to the last inode.
		 */
		first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
		if (first_index < XFS_INO_TO_AGINO(mp, ip->i_ino)) {
			read_unlock(&pag->pag_ici_lock);
			break;
		}

		/* nothing to sync during shutdown */
		if (XFS_FORCED_SHUTDOWN(mp)) {
			read_unlock(&pag->pag_ici_lock);
			return 0;
		}

		/*
		 * If we can't get a reference on the inode, it must be
		 * in reclaim. Leave it for the reclaim code to flush.
		 */
		inode = VFS_I(ip);
		if (!igrab(inode)) {
			read_unlock(&pag->pag_ici_lock);
			continue;
		}
		read_unlock(&pag->pag_ici_lock);

		/* avoid new or bad inodes */
		if (is_bad_inode(inode) ||
		    xfs_iflags_test(ip, XFS_INEW)) {
			IRELE(ip);
			continue;
		}

		/*
		 * If we have to flush data or wait for I/O completion
		 * we need to hold the iolock.
		 */
		if ((flags & SYNC_DELWRI) && VN_DIRTY(inode)) {
			xfs_ilock(ip, XFS_IOLOCK_SHARED);
			lock_flags |= XFS_IOLOCK_SHARED;
			error = xfs_flush_pages(ip, 0, -1, fflag, FI_NONE);
			if (flags & SYNC_IOWAIT)
				xfs_ioend_wait(ip);
		}
		xfs_ilock(ip, XFS_ILOCK_SHARED);

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
		xfs_iput(ip, lock_flags);

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

STATIC void
xfs_quiesce_fs(
	struct xfs_mount	*mp)
{
	int	count = 0, pincount;

	xfs_flush_buftarg(mp->m_ddev_targp, 0);
	xfs_reclaim_inodes(mp, 0, XFS_IFLUSH_DELWRI_ELSE_ASYNC);

	/*
	 * This loop must run at least twice.  The first instance of the loop
	 * will flush most meta data but that will generate more meta data
	 * (typically directory updates).  Which then must be flushed and
	 * logged before we can write the unmount record.
	 */
	do {
		xfs_sync_inodes(mp, SYNC_ATTR|SYNC_WAIT);
		pincount = xfs_flush_buftarg(mp->m_ddev_targp, 1);
		if (!pincount) {
			delay(50);
			count++;
		}
	} while (count < 2);
}

/*
 * Second stage of a quiesce. The data is already synced, now we have to take
 * care of the metadata. New transactions are already blocked, so we need to
 * wait for any remaining transactions to drain out before proceding.
 */
void
xfs_quiesce_attr(
	struct xfs_mount	*mp)
{
	int	error = 0;

	/* wait for all modifications to complete */
	while (atomic_read(&mp->m_active_trans) > 0)
		delay(100);

	/* flush inodes and push all remaining buffers out to disk */
	xfs_quiesce_fs(mp);

	/*
	 * Just warn here till VFS can correctly support
	 * read-only remount without racing.
	 */
	WARN_ON(atomic_read(&mp->m_active_trans) != 0);

	/* Push the superblock and write an unmount record */
	error = xfs_log_sbcount(mp, 1);
	if (error)
		xfs_fs_cmn_err(CE_WARN, mp,
				"xfs_attr_quiesce: failed to log sb changes. "
				"Frozen image may not be consistent.");
	xfs_log_unmount_write(mp);
	xfs_unmountfs_writesb(mp);
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
		xfs_reclaim_inodes(mp, 0, XFS_IFLUSH_DELWRI_ELSE_ASYNC);
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

int
xfs_reclaim_inode(
	xfs_inode_t	*ip,
	int		locked,
	int		sync_mode)
{
	xfs_perag_t	*pag = xfs_get_perag(ip->i_mount, ip->i_ino);

	/* The hash lock here protects a thread in xfs_iget_core from
	 * racing with us on linking the inode back with a vnode.
	 * Once we have the XFS_IRECLAIM flag set it will not touch
	 * us.
	 */
	write_lock(&pag->pag_ici_lock);
	spin_lock(&ip->i_flags_lock);
	if (__xfs_iflags_test(ip, XFS_IRECLAIM) ||
	    !__xfs_iflags_test(ip, XFS_IRECLAIMABLE)) {
		spin_unlock(&ip->i_flags_lock);
		write_unlock(&pag->pag_ici_lock);
		if (locked) {
			xfs_ifunlock(ip);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}
		return 1;
	}
	__xfs_iflags_set(ip, XFS_IRECLAIM);
	spin_unlock(&ip->i_flags_lock);
	write_unlock(&pag->pag_ici_lock);
	xfs_put_perag(ip->i_mount, pag);

	/*
	 * If the inode is still dirty, then flush it out.  If the inode
	 * is not in the AIL, then it will be OK to flush it delwri as
	 * long as xfs_iflush() does not keep any references to the inode.
	 * We leave that decision up to xfs_iflush() since it has the
	 * knowledge of whether it's OK to simply do a delwri flush of
	 * the inode or whether we need to wait until the inode is
	 * pulled from the AIL.
	 * We get the flush lock regardless, though, just to make sure
	 * we don't free it while it is being flushed.
	 */
	if (!locked) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_iflock(ip);
	}

	/*
	 * In the case of a forced shutdown we rely on xfs_iflush() to
	 * wait for the inode to be unpinned before returning an error.
	 */
	if (!is_bad_inode(VFS_I(ip)) && xfs_iflush(ip, sync_mode) == 0) {
		/* synchronize with xfs_iflush_done */
		xfs_iflock(ip);
		xfs_ifunlock(ip);
	}

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_ireclaim(ip);
	return 0;
}

/*
 * We set the inode flag atomically with the radix tree tag.
 * Once we get tag lookups on the radix tree, this inode flag
 * can go away.
 */
void
xfs_inode_set_reclaim_tag(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_perag_t	*pag = xfs_get_perag(mp, ip->i_ino);

	read_lock(&pag->pag_ici_lock);
	spin_lock(&ip->i_flags_lock);
	radix_tree_tag_set(&pag->pag_ici_root,
			XFS_INO_TO_AGINO(mp, ip->i_ino), XFS_ICI_RECLAIM_TAG);
	__xfs_iflags_set(ip, XFS_IRECLAIMABLE);
	spin_unlock(&ip->i_flags_lock);
	read_unlock(&pag->pag_ici_lock);
	xfs_put_perag(mp, pag);
}

void
__xfs_inode_clear_reclaim_tag(
	xfs_mount_t	*mp,
	xfs_perag_t	*pag,
	xfs_inode_t	*ip)
{
	radix_tree_tag_clear(&pag->pag_ici_root,
			XFS_INO_TO_AGINO(mp, ip->i_ino), XFS_ICI_RECLAIM_TAG);
}

void
xfs_inode_clear_reclaim_tag(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_perag_t	*pag = xfs_get_perag(mp, ip->i_ino);

	read_lock(&pag->pag_ici_lock);
	spin_lock(&ip->i_flags_lock);
	__xfs_inode_clear_reclaim_tag(mp, pag, ip);
	spin_unlock(&ip->i_flags_lock);
	read_unlock(&pag->pag_ici_lock);
	xfs_put_perag(mp, pag);
}


STATIC void
xfs_reclaim_inodes_ag(
	xfs_mount_t	*mp,
	int		ag,
	int		noblock,
	int		mode)
{
	xfs_inode_t	*ip = NULL;
	xfs_perag_t	*pag = &mp->m_perag[ag];
	int		nr_found;
	uint32_t	first_index;
	int		skipped;

restart:
	first_index = 0;
	skipped = 0;
	do {
		/*
		 * use a gang lookup to find the next inode in the tree
		 * as the tree is sparse and a gang lookup walks to find
		 * the number of objects requested.
		 */
		read_lock(&pag->pag_ici_lock);
		nr_found = radix_tree_gang_lookup_tag(&pag->pag_ici_root,
					(void**)&ip, first_index, 1,
					XFS_ICI_RECLAIM_TAG);

		if (!nr_found) {
			read_unlock(&pag->pag_ici_lock);
			break;
		}

		/*
		 * Update the index for the next lookup. Catch overflows
		 * into the next AG range which can occur if we have inodes
		 * in the last block of the AG and we are currently
		 * pointing to the last inode.
		 */
		first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
		if (first_index < XFS_INO_TO_AGINO(mp, ip->i_ino)) {
			read_unlock(&pag->pag_ici_lock);
			break;
		}

		/* ignore if already under reclaim */
		if (xfs_iflags_test(ip, XFS_IRECLAIM)) {
			read_unlock(&pag->pag_ici_lock);
			continue;
		}

		if (noblock) {
			if (!xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
				read_unlock(&pag->pag_ici_lock);
				continue;
			}
			if (xfs_ipincount(ip) ||
			    !xfs_iflock_nowait(ip)) {
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				read_unlock(&pag->pag_ici_lock);
				continue;
			}
		}
		read_unlock(&pag->pag_ici_lock);

		/*
		 * hmmm - this is an inode already in reclaim. Do
		 * we even bother catching it here?
		 */
		if (xfs_reclaim_inode(ip, noblock, mode))
			skipped++;
	} while (nr_found);

	if (skipped) {
		delay(1);
		goto restart;
	}
	return;

}

int
xfs_reclaim_inodes(
	xfs_mount_t	*mp,
	int		 noblock,
	int		mode)
{
	int		i;

	for (i = 0; i < mp->m_sb.sb_agcount; i++) {
		if (!mp->m_perag[i].pag_ici_init)
			continue;
		xfs_reclaim_inodes_ag(mp, i, noblock, mode);
	}
	return 0;
}


