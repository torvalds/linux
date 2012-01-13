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
#include "xfs_trans_priv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_dinode.h"
#include "xfs_error.h"
#include "xfs_filestream.h"
#include "xfs_vnodeops.h"
#include "xfs_inode_item.h"
#include "xfs_quota.h"
#include "xfs_trace.h"
#include "xfs_fsops.h"

#include <linux/kthread.h>
#include <linux/freezer.h>

struct workqueue_struct	*xfs_syncd_wq;	/* sync workqueue */

/*
 * The inode lookup is done in batches to keep the amount of lock traffic and
 * radix tree lookups to a minimum. The batch size is a trade off between
 * lookup reduction and stack usage. This is in the reclaim path, so we can't
 * be too greedy.
 */
#define XFS_LOOKUP_BATCH	32

STATIC int
xfs_inode_ag_walk_grab(
	struct xfs_inode	*ip)
{
	struct inode		*inode = VFS_I(ip);

	ASSERT(rcu_read_lock_held());

	/*
	 * check for stale RCU freed inode
	 *
	 * If the inode has been reallocated, it doesn't matter if it's not in
	 * the AG we are walking - we are walking for writeback, so if it
	 * passes all the "valid inode" checks and is dirty, then we'll write
	 * it back anyway.  If it has been reallocated and still being
	 * initialised, the XFS_INEW check below will catch it.
	 */
	spin_lock(&ip->i_flags_lock);
	if (!ip->i_ino)
		goto out_unlock_noent;

	/* avoid new or reclaimable inodes. Leave for reclaim code to flush */
	if (__xfs_iflags_test(ip, XFS_INEW | XFS_IRECLAIMABLE | XFS_IRECLAIM))
		goto out_unlock_noent;
	spin_unlock(&ip->i_flags_lock);

	/* nothing to sync during shutdown */
	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return EFSCORRUPTED;

	/* If we can't grab the inode, it must on it's way to reclaim. */
	if (!igrab(inode))
		return ENOENT;

	if (is_bad_inode(inode)) {
		IRELE(ip);
		return ENOENT;
	}

	/* inode is valid */
	return 0;

out_unlock_noent:
	spin_unlock(&ip->i_flags_lock);
	return ENOENT;
}

STATIC int
xfs_inode_ag_walk(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	int			(*execute)(struct xfs_inode *ip,
					   struct xfs_perag *pag, int flags),
	int			flags)
{
	uint32_t		first_index;
	int			last_error = 0;
	int			skipped;
	int			done;
	int			nr_found;

restart:
	done = 0;
	skipped = 0;
	first_index = 0;
	nr_found = 0;
	do {
		struct xfs_inode *batch[XFS_LOOKUP_BATCH];
		int		error = 0;
		int		i;

		rcu_read_lock();
		nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
					(void **)batch, first_index,
					XFS_LOOKUP_BATCH);
		if (!nr_found) {
			rcu_read_unlock();
			break;
		}

		/*
		 * Grab the inodes before we drop the lock. if we found
		 * nothing, nr == 0 and the loop will be skipped.
		 */
		for (i = 0; i < nr_found; i++) {
			struct xfs_inode *ip = batch[i];

			if (done || xfs_inode_ag_walk_grab(ip))
				batch[i] = NULL;

			/*
			 * Update the index for the next lookup. Catch
			 * overflows into the next AG range which can occur if
			 * we have inodes in the last block of the AG and we
			 * are currently pointing to the last inode.
			 *
			 * Because we may see inodes that are from the wrong AG
			 * due to RCU freeing and reallocation, only update the
			 * index if it lies in this AG. It was a race that lead
			 * us to see this inode, so another lookup from the
			 * same index will not find it again.
			 */
			if (XFS_INO_TO_AGNO(mp, ip->i_ino) != pag->pag_agno)
				continue;
			first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
			if (first_index < XFS_INO_TO_AGINO(mp, ip->i_ino))
				done = 1;
		}

		/* unlock now we've grabbed the inodes. */
		rcu_read_unlock();

		for (i = 0; i < nr_found; i++) {
			if (!batch[i])
				continue;
			error = execute(batch[i], pag, flags);
			IRELE(batch[i]);
			if (error == EAGAIN) {
				skipped++;
				continue;
			}
			if (error && last_error != EFSCORRUPTED)
				last_error = error;
		}

		/* bail out if the filesystem is corrupted.  */
		if (error == EFSCORRUPTED)
			break;

		cond_resched();

	} while (nr_found && !done);

	if (skipped) {
		delay(1);
		goto restart;
	}
	return last_error;
}

int
xfs_inode_ag_iterator(
	struct xfs_mount	*mp,
	int			(*execute)(struct xfs_inode *ip,
					   struct xfs_perag *pag, int flags),
	int			flags)
{
	struct xfs_perag	*pag;
	int			error = 0;
	int			last_error = 0;
	xfs_agnumber_t		ag;

	ag = 0;
	while ((pag = xfs_perag_get(mp, ag))) {
		ag = pag->pag_agno + 1;
		error = xfs_inode_ag_walk(mp, pag, execute, flags);
		xfs_perag_put(pag);
		if (error) {
			last_error = error;
			if (error == EFSCORRUPTED)
				break;
		}
	}
	return XFS_ERROR(last_error);
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

	if (!mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		return 0;

	if (!xfs_ilock_nowait(ip, XFS_IOLOCK_SHARED)) {
		if (flags & SYNC_TRYLOCK)
			return 0;
		xfs_ilock(ip, XFS_IOLOCK_SHARED);
	}

	error = xfs_flush_pages(ip, 0, -1, (flags & SYNC_WAIT) ?
				0 : XBF_ASYNC, FI_NONE);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return error;
}

STATIC int
xfs_sync_inode_attr(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag,
	int			flags)
{
	int			error = 0;

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

	error = xfs_iflush(ip, flags);

	/*
	 * We don't want to try again on non-blocking flushes that can't run
	 * again immediately. If an inode really must be written, then that's
	 * what the SYNC_WAIT flag is for.
	 */
	if (error == EAGAIN) {
		ASSERT(!(flags & SYNC_WAIT));
		error = 0;
	}

 out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return error;
}

/*
 * Write out pagecache data for the whole filesystem.
 */
STATIC int
xfs_sync_data(
	struct xfs_mount	*mp,
	int			flags)
{
	int			error;

	ASSERT((flags & ~(SYNC_TRYLOCK|SYNC_WAIT)) == 0);

	error = xfs_inode_ag_iterator(mp, xfs_sync_inode_data, flags);
	if (error)
		return XFS_ERROR(error);

	xfs_log_force(mp, (flags & SYNC_WAIT) ? XFS_LOG_SYNC : 0);
	return 0;
}

/*
 * Write out inode metadata (attributes) for the whole filesystem.
 */
STATIC int
xfs_sync_attr(
	struct xfs_mount	*mp,
	int			flags)
{
	ASSERT((flags & ~SYNC_WAIT) == 0);

	return xfs_inode_ag_iterator(mp, xfs_sync_inode_attr, flags);
}

STATIC int
xfs_sync_fsdata(
	struct xfs_mount	*mp)
{
	struct xfs_buf		*bp;
	int			error;

	/*
	 * If the buffer is pinned then push on the log so we won't get stuck
	 * waiting in the write for someone, maybe ourselves, to flush the log.
	 *
	 * Even though we just pushed the log above, we did not have the
	 * superblock buffer locked at that point so it can become pinned in
	 * between there and here.
	 */
	bp = xfs_getsb(mp, 0);
	if (xfs_buf_ispinned(bp))
		xfs_log_force(mp, 0);
	error = xfs_bwrite(bp);
	xfs_buf_relse(bp);
	return error;
}

int
xfs_log_dirty_inode(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag,
	int			flags)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	if (!ip->i_update_core)
		return 0;

	tp = xfs_trans_alloc(mp, XFS_TRANS_FSYNC_TS);
	error = xfs_trans_reserve(tp, 0, XFS_FSYNC_TS_LOG_RES(mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return xfs_trans_commit(tp, 0);
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
	int			error, error2 = 0;

	/*
	 * Log all pending size and timestamp updates.  The vfs writeback
	 * code is supposed to do this, but due to its overagressive
	 * livelock detection it will skip inodes where appending writes
	 * were written out in the first non-blocking sync phase if their
	 * completion took long enough that it happened after taking the
	 * timestamp for the cut-off in the blocking phase.
	 */
	xfs_inode_ag_iterator(mp, xfs_log_dirty_inode, 0);

	xfs_qm_sync(mp, SYNC_TRYLOCK);
	xfs_qm_sync(mp, SYNC_WAIT);

	/* force out the newly dirtied log buffers */
	xfs_log_force(mp, XFS_LOG_SYNC);

	/* write superblock and hoover up shutdown errors */
	error = xfs_sync_fsdata(mp);

	/* make sure all delwri buffers are written out */
	xfs_flush_buftarg(mp->m_ddev_targp, 1);

	/* mark the log as covered if needed */
	if (xfs_log_need_covered(mp))
		error2 = xfs_fs_log_dummy(mp);

	/* flush data-only devices */
	if (mp->m_rtdev_targp)
		xfs_flush_buftarg(mp->m_rtdev_targp, 1);

	return error ? error : error2;
}

STATIC void
xfs_quiesce_fs(
	struct xfs_mount	*mp)
{
	int	count = 0, pincount;

	xfs_reclaim_inodes(mp, 0);
	xfs_flush_buftarg(mp->m_ddev_targp, 0);

	/*
	 * This loop must run at least twice.  The first instance of the loop
	 * will flush most meta data but that will generate more meta data
	 * (typically directory updates).  Which then must be flushed and
	 * logged before we can write the unmount record. We also so sync
	 * reclaim of inodes to catch any that the above delwri flush skipped.
	 */
	do {
		xfs_reclaim_inodes(mp, SYNC_WAIT);
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
 * wait for any remaining transactions to drain out before proceeding.
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
	error = xfs_log_sbcount(mp);
	if (error)
		xfs_warn(mp, "xfs_attr_quiesce: failed to log sb changes. "
				"Frozen image may not be consistent.");
	xfs_log_unmount_write(mp);
	xfs_unmountfs_writesb(mp);
}

static void
xfs_syncd_queue_sync(
	struct xfs_mount        *mp)
{
	queue_delayed_work(xfs_syncd_wq, &mp->m_sync_work,
				msecs_to_jiffies(xfs_syncd_centisecs * 10));
}

/*
 * Every sync period we need to unpin all items, reclaim inodes and sync
 * disk quotas.  We might need to cover the log to indicate that the
 * filesystem is idle and not frozen.
 */
STATIC void
xfs_sync_worker(
	struct work_struct *work)
{
	struct xfs_mount *mp = container_of(to_delayed_work(work),
					struct xfs_mount, m_sync_work);
	int		error;

	if (!(mp->m_flags & XFS_MOUNT_RDONLY)) {
		/* dgc: errors ignored here */
		if (mp->m_super->s_frozen == SB_UNFROZEN &&
		    xfs_log_need_covered(mp))
			error = xfs_fs_log_dummy(mp);
		else
			xfs_log_force(mp, 0);
		error = xfs_qm_sync(mp, SYNC_TRYLOCK);

		/* start pushing all the metadata that is currently dirty */
		xfs_ail_push_all(mp->m_ail);
	}

	/* queue us up again */
	xfs_syncd_queue_sync(mp);
}

/*
 * Queue a new inode reclaim pass if there are reclaimable inodes and there
 * isn't a reclaim pass already in progress. By default it runs every 5s based
 * on the xfs syncd work default of 30s. Perhaps this should have it's own
 * tunable, but that can be done if this method proves to be ineffective or too
 * aggressive.
 */
static void
xfs_syncd_queue_reclaim(
	struct xfs_mount        *mp)
{

	/*
	 * We can have inodes enter reclaim after we've shut down the syncd
	 * workqueue during unmount, so don't allow reclaim work to be queued
	 * during unmount.
	 */
	if (!(mp->m_super->s_flags & MS_ACTIVE))
		return;

	rcu_read_lock();
	if (radix_tree_tagged(&mp->m_perag_tree, XFS_ICI_RECLAIM_TAG)) {
		queue_delayed_work(xfs_syncd_wq, &mp->m_reclaim_work,
			msecs_to_jiffies(xfs_syncd_centisecs / 6 * 10));
	}
	rcu_read_unlock();
}

/*
 * This is a fast pass over the inode cache to try to get reclaim moving on as
 * many inodes as possible in a short period of time. It kicks itself every few
 * seconds, as well as being kicked by the inode cache shrinker when memory
 * goes low. It scans as quickly as possible avoiding locked inodes or those
 * already being flushed, and once done schedules a future pass.
 */
STATIC void
xfs_reclaim_worker(
	struct work_struct *work)
{
	struct xfs_mount *mp = container_of(to_delayed_work(work),
					struct xfs_mount, m_reclaim_work);

	xfs_reclaim_inodes(mp, SYNC_TRYLOCK);
	xfs_syncd_queue_reclaim(mp);
}

/*
 * Flush delayed allocate data, attempting to free up reserved space
 * from existing allocations.  At this point a new allocation attempt
 * has failed with ENOSPC and we are in the process of scratching our
 * heads, looking about for more room.
 *
 * Queue a new data flush if there isn't one already in progress and
 * wait for completion of the flush. This means that we only ever have one
 * inode flush in progress no matter how many ENOSPC events are occurring and
 * so will prevent the system from bogging down due to every concurrent
 * ENOSPC event scanning all the active inodes in the system for writeback.
 */
void
xfs_flush_inodes(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;

	queue_work(xfs_syncd_wq, &mp->m_flush_work);
	flush_work_sync(&mp->m_flush_work);
}

STATIC void
xfs_flush_worker(
	struct work_struct *work)
{
	struct xfs_mount *mp = container_of(work,
					struct xfs_mount, m_flush_work);

	xfs_sync_data(mp, SYNC_TRYLOCK);
	xfs_sync_data(mp, SYNC_TRYLOCK | SYNC_WAIT);
}

int
xfs_syncd_init(
	struct xfs_mount	*mp)
{
	INIT_WORK(&mp->m_flush_work, xfs_flush_worker);
	INIT_DELAYED_WORK(&mp->m_sync_work, xfs_sync_worker);
	INIT_DELAYED_WORK(&mp->m_reclaim_work, xfs_reclaim_worker);

	xfs_syncd_queue_sync(mp);
	xfs_syncd_queue_reclaim(mp);

	return 0;
}

void
xfs_syncd_stop(
	struct xfs_mount	*mp)
{
	cancel_delayed_work_sync(&mp->m_sync_work);
	cancel_delayed_work_sync(&mp->m_reclaim_work);
	cancel_work_sync(&mp->m_flush_work);
}

void
__xfs_inode_set_reclaim_tag(
	struct xfs_perag	*pag,
	struct xfs_inode	*ip)
{
	radix_tree_tag_set(&pag->pag_ici_root,
			   XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino),
			   XFS_ICI_RECLAIM_TAG);

	if (!pag->pag_ici_reclaimable) {
		/* propagate the reclaim tag up into the perag radix tree */
		spin_lock(&ip->i_mount->m_perag_lock);
		radix_tree_tag_set(&ip->i_mount->m_perag_tree,
				XFS_INO_TO_AGNO(ip->i_mount, ip->i_ino),
				XFS_ICI_RECLAIM_TAG);
		spin_unlock(&ip->i_mount->m_perag_lock);

		/* schedule periodic background inode reclaim */
		xfs_syncd_queue_reclaim(ip->i_mount);

		trace_xfs_perag_set_reclaim(ip->i_mount, pag->pag_agno,
							-1, _RET_IP_);
	}
	pag->pag_ici_reclaimable++;
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
	struct xfs_mount *mp = ip->i_mount;
	struct xfs_perag *pag;

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));
	spin_lock(&pag->pag_ici_lock);
	spin_lock(&ip->i_flags_lock);
	__xfs_inode_set_reclaim_tag(pag, ip);
	__xfs_iflags_set(ip, XFS_IRECLAIMABLE);
	spin_unlock(&ip->i_flags_lock);
	spin_unlock(&pag->pag_ici_lock);
	xfs_perag_put(pag);
}

STATIC void
__xfs_inode_clear_reclaim(
	xfs_perag_t	*pag,
	xfs_inode_t	*ip)
{
	pag->pag_ici_reclaimable--;
	if (!pag->pag_ici_reclaimable) {
		/* clear the reclaim tag from the perag radix tree */
		spin_lock(&ip->i_mount->m_perag_lock);
		radix_tree_tag_clear(&ip->i_mount->m_perag_tree,
				XFS_INO_TO_AGNO(ip->i_mount, ip->i_ino),
				XFS_ICI_RECLAIM_TAG);
		spin_unlock(&ip->i_mount->m_perag_lock);
		trace_xfs_perag_clear_reclaim(ip->i_mount, pag->pag_agno,
							-1, _RET_IP_);
	}
}

void
__xfs_inode_clear_reclaim_tag(
	xfs_mount_t	*mp,
	xfs_perag_t	*pag,
	xfs_inode_t	*ip)
{
	radix_tree_tag_clear(&pag->pag_ici_root,
			XFS_INO_TO_AGINO(mp, ip->i_ino), XFS_ICI_RECLAIM_TAG);
	__xfs_inode_clear_reclaim(pag, ip);
}

/*
 * Grab the inode for reclaim exclusively.
 * Return 0 if we grabbed it, non-zero otherwise.
 */
STATIC int
xfs_reclaim_inode_grab(
	struct xfs_inode	*ip,
	int			flags)
{
	ASSERT(rcu_read_lock_held());

	/* quick check for stale RCU freed inode */
	if (!ip->i_ino)
		return 1;

	/*
	 * do some unlocked checks first to avoid unnecessary lock traffic.
	 * The first is a flush lock check, the second is a already in reclaim
	 * check. Only do these checks if we are not going to block on locks.
	 */
	if ((flags & SYNC_TRYLOCK) &&
	    (!ip->i_flush.done || __xfs_iflags_test(ip, XFS_IRECLAIM))) {
		return 1;
	}

	/*
	 * The radix tree lock here protects a thread in xfs_iget from racing
	 * with us starting reclaim on the inode.  Once we have the
	 * XFS_IRECLAIM flag set it will not touch us.
	 *
	 * Due to RCU lookup, we may find inodes that have been freed and only
	 * have XFS_IRECLAIM set.  Indeed, we may see reallocated inodes that
	 * aren't candidates for reclaim at all, so we must check the
	 * XFS_IRECLAIMABLE is set first before proceeding to reclaim.
	 */
	spin_lock(&ip->i_flags_lock);
	if (!__xfs_iflags_test(ip, XFS_IRECLAIMABLE) ||
	    __xfs_iflags_test(ip, XFS_IRECLAIM)) {
		/* not a reclaim candidate. */
		spin_unlock(&ip->i_flags_lock);
		return 1;
	}
	__xfs_iflags_set(ip, XFS_IRECLAIM);
	spin_unlock(&ip->i_flags_lock);
	return 0;
}

/*
 * Inodes in different states need to be treated differently, and the return
 * value of xfs_iflush is not sufficient to get this right. The following table
 * lists the inode states and the reclaim actions necessary for non-blocking
 * reclaim:
 *
 *
 *	inode state	     iflush ret		required action
 *      ---------------      ----------         ---------------
 *	bad			-		reclaim
 *	shutdown		EIO		unpin and reclaim
 *	clean, unpinned		0		reclaim
 *	stale, unpinned		0		reclaim
 *	clean, pinned(*)	0		requeue
 *	stale, pinned		EAGAIN		requeue
 *	dirty, delwri ok	0		requeue
 *	dirty, delwri blocked	EAGAIN		requeue
 *	dirty, sync flush	0		reclaim
 *
 * (*) dgc: I don't think the clean, pinned state is possible but it gets
 * handled anyway given the order of checks implemented.
 *
 * As can be seen from the table, the return value of xfs_iflush() is not
 * sufficient to correctly decide the reclaim action here. The checks in
 * xfs_iflush() might look like duplicates, but they are not.
 *
 * Also, because we get the flush lock first, we know that any inode that has
 * been flushed delwri has had the flush completed by the time we check that
 * the inode is clean. The clean inode check needs to be done before flushing
 * the inode delwri otherwise we would loop forever requeuing clean inodes as
 * we cannot tell apart a successful delwri flush and a clean inode from the
 * return value of xfs_iflush().
 *
 * Note that because the inode is flushed delayed write by background
 * writeback, the flush lock may already be held here and waiting on it can
 * result in very long latencies. Hence for sync reclaims, where we wait on the
 * flush lock, the caller should push out delayed write inodes first before
 * trying to reclaim them to minimise the amount of time spent waiting. For
 * background relaim, we just requeue the inode for the next pass.
 *
 * Hence the order of actions after gaining the locks should be:
 *	bad		=> reclaim
 *	shutdown	=> unpin and reclaim
 *	pinned, delwri	=> requeue
 *	pinned, sync	=> unpin
 *	stale		=> reclaim
 *	clean		=> reclaim
 *	dirty, delwri	=> flush and requeue
 *	dirty, sync	=> flush, wait and reclaim
 */
STATIC int
xfs_reclaim_inode(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag,
	int			sync_mode)
{
	int	error;

restart:
	error = 0;
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (!xfs_iflock_nowait(ip)) {
		if (!(sync_mode & SYNC_WAIT))
			goto out;

		/*
		 * If we only have a single dirty inode in a cluster there is
		 * a fair chance that the AIL push may have pushed it into
		 * the buffer, but xfsbufd won't touch it until 30 seconds
		 * from now, and thus we will lock up here.
		 *
		 * Promote the inode buffer to the front of the delwri list
		 * and wake up xfsbufd now.
		 */
		xfs_promote_inode(ip);
		xfs_iflock(ip);
	}

	if (is_bad_inode(VFS_I(ip)))
		goto reclaim;
	if (XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		xfs_iunpin_wait(ip);
		goto reclaim;
	}
	if (xfs_ipincount(ip)) {
		if (!(sync_mode & SYNC_WAIT)) {
			xfs_ifunlock(ip);
			goto out;
		}
		xfs_iunpin_wait(ip);
	}
	if (xfs_iflags_test(ip, XFS_ISTALE))
		goto reclaim;
	if (xfs_inode_clean(ip))
		goto reclaim;

	/*
	 * Now we have an inode that needs flushing.
	 *
	 * We do a nonblocking flush here even if we are doing a SYNC_WAIT
	 * reclaim as we can deadlock with inode cluster removal.
	 * xfs_ifree_cluster() can lock the inode buffer before it locks the
	 * ip->i_lock, and we are doing the exact opposite here. As a result,
	 * doing a blocking xfs_itobp() to get the cluster buffer will result
	 * in an ABBA deadlock with xfs_ifree_cluster().
	 *
	 * As xfs_ifree_cluser() must gather all inodes that are active in the
	 * cache to mark them stale, if we hit this case we don't actually want
	 * to do IO here - we want the inode marked stale so we can simply
	 * reclaim it. Hence if we get an EAGAIN error on a SYNC_WAIT flush,
	 * just unlock the inode, back off and try again. Hopefully the next
	 * pass through will see the stale flag set on the inode.
	 */
	error = xfs_iflush(ip, SYNC_TRYLOCK | sync_mode);
	if (sync_mode & SYNC_WAIT) {
		if (error == EAGAIN) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			/* backoff longer than in xfs_ifree_cluster */
			delay(2);
			goto restart;
		}
		xfs_iflock(ip);
		goto reclaim;
	}

	/*
	 * When we have to flush an inode but don't have SYNC_WAIT set, we
	 * flush the inode out using a delwri buffer and wait for the next
	 * call into reclaim to find it in a clean state instead of waiting for
	 * it now. We also don't return errors here - if the error is transient
	 * then the next reclaim pass will flush the inode, and if the error
	 * is permanent then the next sync reclaim will reclaim the inode and
	 * pass on the error.
	 */
	if (error && error != EAGAIN && !XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		xfs_warn(ip->i_mount,
			"inode 0x%llx background reclaim flush failed with %d",
			(long long)ip->i_ino, error);
	}
out:
	xfs_iflags_clear(ip, XFS_IRECLAIM);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	/*
	 * We could return EAGAIN here to make reclaim rescan the inode tree in
	 * a short while. However, this just burns CPU time scanning the tree
	 * waiting for IO to complete and xfssyncd never goes back to the idle
	 * state. Instead, return 0 to let the next scheduled background reclaim
	 * attempt to reclaim the inode again.
	 */
	return 0;

reclaim:
	xfs_ifunlock(ip);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	XFS_STATS_INC(xs_ig_reclaims);
	/*
	 * Remove the inode from the per-AG radix tree.
	 *
	 * Because radix_tree_delete won't complain even if the item was never
	 * added to the tree assert that it's been there before to catch
	 * problems with the inode life time early on.
	 */
	spin_lock(&pag->pag_ici_lock);
	if (!radix_tree_delete(&pag->pag_ici_root,
				XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino)))
		ASSERT(0);
	__xfs_inode_clear_reclaim(pag, ip);
	spin_unlock(&pag->pag_ici_lock);

	/*
	 * Here we do an (almost) spurious inode lock in order to coordinate
	 * with inode cache radix tree lookups.  This is because the lookup
	 * can reference the inodes in the cache without taking references.
	 *
	 * We make that OK here by ensuring that we wait until the inode is
	 * unlocked after the lookup before we go ahead and free it.  We get
	 * both the ilock and the iolock because the code may need to drop the
	 * ilock one but will still hold the iolock.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_qm_dqdetach(ip);
	xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

	xfs_inode_free(ip);
	return error;

}

/*
 * Walk the AGs and reclaim the inodes in them. Even if the filesystem is
 * corrupted, we still want to try to reclaim all the inodes. If we don't,
 * then a shut down during filesystem unmount reclaim walk leak all the
 * unreclaimed inodes.
 */
int
xfs_reclaim_inodes_ag(
	struct xfs_mount	*mp,
	int			flags,
	int			*nr_to_scan)
{
	struct xfs_perag	*pag;
	int			error = 0;
	int			last_error = 0;
	xfs_agnumber_t		ag;
	int			trylock = flags & SYNC_TRYLOCK;
	int			skipped;

restart:
	ag = 0;
	skipped = 0;
	while ((pag = xfs_perag_get_tag(mp, ag, XFS_ICI_RECLAIM_TAG))) {
		unsigned long	first_index = 0;
		int		done = 0;
		int		nr_found = 0;

		ag = pag->pag_agno + 1;

		if (trylock) {
			if (!mutex_trylock(&pag->pag_ici_reclaim_lock)) {
				skipped++;
				xfs_perag_put(pag);
				continue;
			}
			first_index = pag->pag_ici_reclaim_cursor;
		} else
			mutex_lock(&pag->pag_ici_reclaim_lock);

		do {
			struct xfs_inode *batch[XFS_LOOKUP_BATCH];
			int	i;

			rcu_read_lock();
			nr_found = radix_tree_gang_lookup_tag(
					&pag->pag_ici_root,
					(void **)batch, first_index,
					XFS_LOOKUP_BATCH,
					XFS_ICI_RECLAIM_TAG);
			if (!nr_found) {
				done = 1;
				rcu_read_unlock();
				break;
			}

			/*
			 * Grab the inodes before we drop the lock. if we found
			 * nothing, nr == 0 and the loop will be skipped.
			 */
			for (i = 0; i < nr_found; i++) {
				struct xfs_inode *ip = batch[i];

				if (done || xfs_reclaim_inode_grab(ip, flags))
					batch[i] = NULL;

				/*
				 * Update the index for the next lookup. Catch
				 * overflows into the next AG range which can
				 * occur if we have inodes in the last block of
				 * the AG and we are currently pointing to the
				 * last inode.
				 *
				 * Because we may see inodes that are from the
				 * wrong AG due to RCU freeing and
				 * reallocation, only update the index if it
				 * lies in this AG. It was a race that lead us
				 * to see this inode, so another lookup from
				 * the same index will not find it again.
				 */
				if (XFS_INO_TO_AGNO(mp, ip->i_ino) !=
								pag->pag_agno)
					continue;
				first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
				if (first_index < XFS_INO_TO_AGINO(mp, ip->i_ino))
					done = 1;
			}

			/* unlock now we've grabbed the inodes. */
			rcu_read_unlock();

			for (i = 0; i < nr_found; i++) {
				if (!batch[i])
					continue;
				error = xfs_reclaim_inode(batch[i], pag, flags);
				if (error && last_error != EFSCORRUPTED)
					last_error = error;
			}

			*nr_to_scan -= XFS_LOOKUP_BATCH;

			cond_resched();

		} while (nr_found && !done && *nr_to_scan > 0);

		if (trylock && !done)
			pag->pag_ici_reclaim_cursor = first_index;
		else
			pag->pag_ici_reclaim_cursor = 0;
		mutex_unlock(&pag->pag_ici_reclaim_lock);
		xfs_perag_put(pag);
	}

	/*
	 * if we skipped any AG, and we still have scan count remaining, do
	 * another pass this time using blocking reclaim semantics (i.e
	 * waiting on the reclaim locks and ignoring the reclaim cursors). This
	 * ensure that when we get more reclaimers than AGs we block rather
	 * than spin trying to execute reclaim.
	 */
	if (skipped && (flags & SYNC_WAIT) && *nr_to_scan > 0) {
		trylock = 0;
		goto restart;
	}
	return XFS_ERROR(last_error);
}

int
xfs_reclaim_inodes(
	xfs_mount_t	*mp,
	int		mode)
{
	int		nr_to_scan = INT_MAX;

	return xfs_reclaim_inodes_ag(mp, mode, &nr_to_scan);
}

/*
 * Scan a certain number of inodes for reclaim.
 *
 * When called we make sure that there is a background (fast) inode reclaim in
 * progress, while we will throttle the speed of reclaim via doing synchronous
 * reclaim of inodes. That means if we come across dirty inodes, we wait for
 * them to be cleaned, which we hope will not be very long due to the
 * background walker having already kicked the IO off on those dirty inodes.
 */
void
xfs_reclaim_inodes_nr(
	struct xfs_mount	*mp,
	int			nr_to_scan)
{
	/* kick background reclaimer and push the AIL */
	xfs_syncd_queue_reclaim(mp);
	xfs_ail_push_all(mp->m_ail);

	xfs_reclaim_inodes_ag(mp, SYNC_TRYLOCK | SYNC_WAIT, &nr_to_scan);
}

/*
 * Return the number of reclaimable inodes in the filesystem for
 * the shrinker to determine how much to reclaim.
 */
int
xfs_reclaim_inodes_count(
	struct xfs_mount	*mp)
{
	struct xfs_perag	*pag;
	xfs_agnumber_t		ag = 0;
	int			reclaimable = 0;

	while ((pag = xfs_perag_get_tag(mp, ag, XFS_ICI_RECLAIM_TAG))) {
		ag = pag->pag_agno + 1;
		reclaimable += pag->pag_ici_reclaimable;
		xfs_perag_put(pag);
	}
	return reclaimable;
}

