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
#include "xfs_quota.h"

#include <linux/kthread.h>
#include <linux/freezer.h>


STATIC xfs_inode_t *
xfs_inode_ag_lookup(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	uint32_t		*first_index,
	int			tag)
{
	int			nr_found;
	struct xfs_inode	*ip;

	/*
	 * use a gang lookup to find the next inode in the tree
	 * as the tree is sparse and a gang lookup walks to find
	 * the number of objects requested.
	 */
	if (tag == XFS_ICI_NO_TAG) {
		nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
				(void **)&ip, *first_index, 1);
	} else {
		nr_found = radix_tree_gang_lookup_tag(&pag->pag_ici_root,
				(void **)&ip, *first_index, 1, tag);
	}
	if (!nr_found)
		return NULL;

	/*
	 * Update the index for the next lookup. Catch overflows
	 * into the next AG range which can occur if we have inodes
	 * in the last block of the AG and we are currently
	 * pointing to the last inode.
	 */
	*first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
	if (*first_index < XFS_INO_TO_AGINO(mp, ip->i_ino))
		return NULL;
	return ip;
}

STATIC int
xfs_inode_ag_walk(
	struct xfs_mount	*mp,
	xfs_agnumber_t		ag,
	int			(*execute)(struct xfs_inode *ip,
					   struct xfs_perag *pag, int flags),
	int			flags,
	int			tag,
	int			exclusive)
{
	struct xfs_perag	*pag = &mp->m_perag[ag];
	uint32_t		first_index;
	int			last_error = 0;
	int			skipped;

restart:
	skipped = 0;
	first_index = 0;
	do {
		int		error = 0;
		xfs_inode_t	*ip;

		if (exclusive)
			write_lock(&pag->pag_ici_lock);
		else
			read_lock(&pag->pag_ici_lock);
		ip = xfs_inode_ag_lookup(mp, pag, &first_index, tag);
		if (!ip) {
			if (exclusive)
				write_unlock(&pag->pag_ici_lock);
			else
				read_unlock(&pag->pag_ici_lock);
			break;
		}

		/* execute releases pag->pag_ici_lock */
		error = execute(ip, pag, flags);
		if (error == EAGAIN) {
			skipped++;
			continue;
		}
		if (error)
			last_error = error;

		/* bail out if the filesystem is corrupted.  */
		if (error == EFSCORRUPTED)
			break;

	} while (1);

	if (skipped) {
		delay(1);
		goto restart;
	}

	xfs_put_perag(mp, pag);
	return last_error;
}

int
xfs_inode_ag_iterator(
	struct xfs_mount	*mp,
	int			(*execute)(struct xfs_inode *ip,
					   struct xfs_perag *pag, int flags),
	int			flags,
	int			tag,
	int			exclusive)
{
	int			error = 0;
	int			last_error = 0;
	xfs_agnumber_t		ag;

	for (ag = 0; ag < mp->m_sb.sb_agcount; ag++) {
		if (!mp->m_perag[ag].pag_ici_init)
			continue;
		error = xfs_inode_ag_walk(mp, ag, execute, flags, tag,
						exclusive);
		if (error) {
			last_error = error;
			if (error == EFSCORRUPTED)
				break;
		}
	}
	return XFS_ERROR(last_error);
}

/* must be called with pag_ici_lock held and releases it */
int
xfs_sync_inode_valid(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag)
{
	struct inode		*inode = VFS_I(ip);
	int			error = EFSCORRUPTED;

	/* nothing to sync during shutdown */
	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		goto out_unlock;

	/* avoid new or reclaimable inodes. Leave for reclaim code to flush */
	error = ENOENT;
	if (xfs_iflags_test(ip, XFS_INEW | XFS_IRECLAIMABLE | XFS_IRECLAIM))
		goto out_unlock;

	/* If we can't grab the inode, it must on it's way to reclaim. */
	if (!igrab(inode))
		goto out_unlock;

	if (is_bad_inode(inode)) {
		IRELE(ip);
		goto out_unlock;
	}

	/* inode is valid */
	error = 0;
out_unlock:
	read_unlock(&pag->pag_ici_lock);
	return error;
}

STATIC int
xfs_sync_inode_data(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag,
	int			flags)
{
	struct inode		*inode = VFS_I(ip);
	struct address_space *mapping = inode->i_mapping;
	int			error = 0;

	error = xfs_sync_inode_valid(ip, pag);
	if (error)
		return error;

	if (!mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		goto out_wait;

	if (!xfs_ilock_nowait(ip, XFS_IOLOCK_SHARED)) {
		if (flags & SYNC_TRYLOCK)
			goto out_wait;
		xfs_ilock(ip, XFS_IOLOCK_SHARED);
	}

	error = xfs_flush_pages(ip, 0, -1, (flags & SYNC_WAIT) ?
				0 : XFS_B_ASYNC, FI_NONE);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);

 out_wait:
	if (flags & SYNC_WAIT)
		xfs_ioend_wait(ip);
	IRELE(ip);
	return error;
}

STATIC int
xfs_sync_inode_attr(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag,
	int			flags)
{
	int			error = 0;

	error = xfs_sync_inode_valid(ip, pag);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	if (xfs_inode_clean(ip))
		goto out_unlock;
	if (!xfs_iflock_nowait(ip)) {
		if (!(flags & SYNC_WAIT))
			goto out_unlock;
		xfs_iflock(ip);
	}

	if (xfs_inode_clean(ip)) {
		xfs_ifunlock(ip);
		goto out_unlock;
	}

	error = xfs_iflush(ip, (flags & SYNC_WAIT) ?
			   XFS_IFLUSH_SYNC : XFS_IFLUSH_DELWRI);

 out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	IRELE(ip);
	return error;
}

/*
 * Write out pagecache data for the whole filesystem.
 */
int
xfs_sync_data(
	struct xfs_mount	*mp,
	int			flags)
{
	int			error;

	ASSERT((flags & ~(SYNC_TRYLOCK|SYNC_WAIT)) == 0);

	error = xfs_inode_ag_iterator(mp, xfs_sync_inode_data, flags,
				      XFS_ICI_NO_TAG, 0);
	if (error)
		return XFS_ERROR(error);

	xfs_log_force(mp, 0,
		      (flags & SYNC_WAIT) ?
		       XFS_LOG_FORCE | XFS_LOG_SYNC :
		       XFS_LOG_FORCE);
	return 0;
}

/*
 * Write out inode metadata (attributes) for the whole filesystem.
 */
int
xfs_sync_attr(
	struct xfs_mount	*mp,
	int			flags)
{
	ASSERT((flags & ~SYNC_WAIT) == 0);

	return xfs_inode_ag_iterator(mp, xfs_sync_inode_attr, flags,
				     XFS_ICI_NO_TAG, 0);
}

STATIC int
xfs_commit_dummy_trans(
	struct xfs_mount	*mp,
	uint			flags)
{
	struct xfs_inode	*ip = mp->m_rootip;
	struct xfs_trans	*tp;
	int			error;
	int			log_flags = XFS_LOG_FORCE;

	if (flags & SYNC_WAIT)
		log_flags |= XFS_LOG_SYNC;

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
	error = xfs_trans_commit(tp, 0);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	/* the log force ensures this transaction is pushed to disk */
	xfs_log_force(mp, 0, log_flags);
	return error;
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
	if (flags & SYNC_TRYLOCK) {
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

	error = xfs_bwrite(mp, bp);
	if (error)
		return error;

	/*
	 * If this is a data integrity sync make sure all pending buffers
	 * are flushed out for the log coverage check below.
	 */
	if (flags & SYNC_WAIT)
		xfs_flush_buftarg(mp->m_ddev_targp, 1);

	if (xfs_log_need_covered(mp))
		error = xfs_commit_dummy_trans(mp, flags);
	return error;

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
	xfs_sync_data(mp, 0);
	xfs_qm_sync(mp, SYNC_TRYLOCK);

	/* push and block till complete */
	xfs_sync_data(mp, SYNC_WAIT);
	xfs_qm_sync(mp, SYNC_WAIT);

	/* drop inode references pinned by filestreams */
	xfs_filestream_flush(mp);

	/* write superblock and hoover up shutdown errors */
	error = xfs_sync_fsdata(mp, SYNC_WAIT);

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
	xfs_reclaim_inodes(mp, XFS_IFLUSH_DELWRI_ELSE_ASYNC);

	/*
	 * This loop must run at least twice.  The first instance of the loop
	 * will flush most meta data but that will generate more meta data
	 * (typically directory updates).  Which then must be flushed and
	 * logged before we can write the unmount record.
	 */
	do {
		xfs_sync_attr(mp, SYNC_WAIT);
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
	void		(*syncer)(struct xfs_mount *, void *),
	struct completion *completion)
{
	struct xfs_sync_work *work;

	work = kmem_alloc(sizeof(struct xfs_sync_work), KM_SLEEP);
	INIT_LIST_HEAD(&work->w_list);
	work->w_syncer = syncer;
	work->w_data = data;
	work->w_mount = mp;
	work->w_completion = completion;
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
xfs_flush_inodes_work(
	struct xfs_mount *mp,
	void		*arg)
{
	struct inode	*inode = arg;
	xfs_sync_data(mp, SYNC_TRYLOCK);
	xfs_sync_data(mp, SYNC_TRYLOCK | SYNC_WAIT);
	iput(inode);
}

void
xfs_flush_inodes(
	xfs_inode_t	*ip)
{
	struct inode	*inode = VFS_I(ip);
	DECLARE_COMPLETION_ONSTACK(completion);

	igrab(inode);
	xfs_syncd_queue_work(ip->i_mount, inode, xfs_flush_inodes_work, &completion);
	wait_for_completion(&completion);
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
		xfs_reclaim_inodes(mp, XFS_IFLUSH_DELWRI_ELSE_ASYNC);
		/* dgc: errors ignored here */
		error = xfs_qm_sync(mp, SYNC_TRYLOCK);
		error = xfs_sync_fsdata(mp, SYNC_TRYLOCK);
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
	xfs_sync_work_t		*work, *n;
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
			if (work->w_completion)
				complete(work->w_completion);
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
	mp->m_sync_work.w_completion = NULL;
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

void
__xfs_inode_set_reclaim_tag(
	struct xfs_perag	*pag,
	struct xfs_inode	*ip)
{
	radix_tree_tag_set(&pag->pag_ici_root,
			   XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino),
			   XFS_ICI_RECLAIM_TAG);
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

	write_lock(&pag->pag_ici_lock);
	spin_lock(&ip->i_flags_lock);
	__xfs_inode_set_reclaim_tag(pag, ip);
	__xfs_iflags_set(ip, XFS_IRECLAIMABLE);
	spin_unlock(&ip->i_flags_lock);
	write_unlock(&pag->pag_ici_lock);
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

STATIC int
xfs_reclaim_inode(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag,
	int			sync_mode)
{
	/*
	 * The radix tree lock here protects a thread in xfs_iget from racing
	 * with us starting reclaim on the inode.  Once we have the
	 * XFS_IRECLAIM flag set it will not touch us.
	 */
	spin_lock(&ip->i_flags_lock);
	ASSERT_ALWAYS(__xfs_iflags_test(ip, XFS_IRECLAIMABLE));
	if (__xfs_iflags_test(ip, XFS_IRECLAIM)) {
		/* ignore as it is already under reclaim */
		spin_unlock(&ip->i_flags_lock);
		write_unlock(&pag->pag_ici_lock);
		return 0;
	}
	__xfs_iflags_set(ip, XFS_IRECLAIM);
	spin_unlock(&ip->i_flags_lock);
	write_unlock(&pag->pag_ici_lock);

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
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_iflock(ip);

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

int
xfs_reclaim_inodes(
	xfs_mount_t	*mp,
	int		mode)
{
	return xfs_inode_ag_iterator(mp, xfs_reclaim_inode, mode,
					XFS_ICI_RECLAIM_TAG, 1);
}
