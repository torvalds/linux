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
	struct xfs_perag	*pag,
	int			(*execute)(struct xfs_inode *ip,
					   struct xfs_perag *pag, int flags),
	int			flags,
	int			tag,
	int			exclusive,
	int			*nr_to_scan)
{
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

	} while ((*nr_to_scan)--);

	if (skipped) {
		delay(1);
		goto restart;
	}
	return last_error;
}

/*
 * Select the next per-ag structure to iterate during the walk. The reclaim
 * walk is optimised only to walk AGs with reclaimable inodes in them.
 */
static struct xfs_perag *
xfs_inode_ag_iter_next_pag(
	struct xfs_mount	*mp,
	xfs_agnumber_t		*first,
	int			tag)
{
	struct xfs_perag	*pag = NULL;

	if (tag == XFS_ICI_RECLAIM_TAG) {
		int found;
		int ref;

		spin_lock(&mp->m_perag_lock);
		found = radix_tree_gang_lookup_tag(&mp->m_perag_tree,
				(void **)&pag, *first, 1, tag);
		if (found <= 0) {
			spin_unlock(&mp->m_perag_lock);
			return NULL;
		}
		*first = pag->pag_agno + 1;
		/* open coded pag reference increment */
		ref = atomic_inc_return(&pag->pag_ref);
		spin_unlock(&mp->m_perag_lock);
		trace_xfs_perag_get_reclaim(mp, pag->pag_agno, ref, _RET_IP_);
	} else {
		pag = xfs_perag_get(mp, *first);
		(*first)++;
	}
	return pag;
}

int
xfs_inode_ag_iterator(
	struct xfs_mount	*mp,
	int			(*execute)(struct xfs_inode *ip,
					   struct xfs_perag *pag, int flags),
	int			flags,
	int			tag,
	int			exclusive,
	int			*nr_to_scan)
{
	struct xfs_perag	*pag;
	int			error = 0;
	int			last_error = 0;
	xfs_agnumber_t		ag;
	int			nr;

	nr = nr_to_scan ? *nr_to_scan : INT_MAX;
	ag = 0;
	while ((pag = xfs_inode_ag_iter_next_pag(mp, &ag, tag))) {
		error = xfs_inode_ag_walk(mp, pag, execute, flags, tag,
						exclusive, &nr);
		xfs_perag_put(pag);
		if (error) {
			last_error = error;
			if (error == EFSCORRUPTED)
				break;
		}
		if (nr <= 0)
			break;
	}
	if (nr_to_scan)
		*nr_to_scan = nr;
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
				0 : XBF_ASYNC, FI_NONE);
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

	error = xfs_iflush(ip, flags);

 out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	IRELE(ip);
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

	error = xfs_inode_ag_iterator(mp, xfs_sync_inode_data, flags,
				      XFS_ICI_NO_TAG, 0, NULL);
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

	return xfs_inode_ag_iterator(mp, xfs_sync_inode_attr, flags,
				     XFS_ICI_NO_TAG, 0, NULL);
}

STATIC int
xfs_sync_fsdata(
	struct xfs_mount	*mp)
{
	struct xfs_buf		*bp;

	/*
	 * If the buffer is pinned then push on the log so we won't get stuck
	 * waiting in the write for someone, maybe ourselves, to flush the log.
	 *
	 * Even though we just pushed the log above, we did not have the
	 * superblock buffer locked at that point so it can become pinned in
	 * between there and here.
	 */
	bp = xfs_getsb(mp, 0);
	if (XFS_BUF_ISPINNED(bp))
		xfs_log_force(mp, 0);

	return xfs_bwrite(mp, bp);
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

	/* push non-blocking */
	xfs_sync_data(mp, 0);
	xfs_qm_sync(mp, SYNC_TRYLOCK);

	/* push and block till complete */
	xfs_sync_data(mp, SYNC_WAIT);
	xfs_qm_sync(mp, SYNC_WAIT);

	/* write superblock and hoover up shutdown errors */
	error = xfs_sync_fsdata(mp);

	/* make sure all delwri buffers are written out */
	xfs_flush_buftarg(mp->m_ddev_targp, 1);

	/* mark the log as covered if needed */
	if (xfs_log_need_covered(mp))
		error2 = xfs_fs_log_dummy(mp, SYNC_WAIT);

	/* flush data-only devices */
	if (mp->m_rtdev_targp)
		XFS_bflush(mp->m_rtdev_targp);

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
	xfs_log_force(ip->i_mount, XFS_LOG_SYNC);
}

/*
 * Every sync period we need to unpin all items, reclaim inodes and sync
 * disk quotas.  We might need to cover the log to indicate that the
 * filesystem is idle and not frozen.
 */
STATIC void
xfs_sync_worker(
	struct xfs_mount *mp,
	void		*unused)
{
	int		error;

	if (!(mp->m_flags & XFS_MOUNT_RDONLY)) {
		xfs_log_force(mp, 0);
		xfs_reclaim_inodes(mp, 0);
		/* dgc: errors ignored here */
		error = xfs_qm_sync(mp, SYNC_TRYLOCK);
		if (mp->m_super->s_frozen == SB_UNFROZEN &&
		    xfs_log_need_covered(mp))
			error = xfs_fs_log_dummy(mp, 0);
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
		if (list_empty(&mp->m_sync_list))
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
		list_splice_init(&mp->m_sync_list, &tmp);
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
	mp->m_sync_task = kthread_run(xfssyncd, mp, "xfssyncd/%s", mp->m_fsname);
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

	if (!pag->pag_ici_reclaimable) {
		/* propagate the reclaim tag up into the perag radix tree */
		spin_lock(&ip->i_mount->m_perag_lock);
		radix_tree_tag_set(&ip->i_mount->m_perag_tree,
				XFS_INO_TO_AGNO(ip->i_mount, ip->i_ino),
				XFS_ICI_RECLAIM_TAG);
		spin_unlock(&ip->i_mount->m_perag_lock);
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
	write_lock(&pag->pag_ici_lock);
	spin_lock(&ip->i_flags_lock);
	__xfs_inode_set_reclaim_tag(pag, ip);
	__xfs_iflags_set(ip, XFS_IRECLAIMABLE);
	spin_unlock(&ip->i_flags_lock);
	write_unlock(&pag->pag_ici_lock);
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
	int	error = 0;

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

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (!xfs_iflock_nowait(ip)) {
		if (!(sync_mode & SYNC_WAIT))
			goto out;
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

	/* Now we have an inode that needs flushing */
	error = xfs_iflush(ip, sync_mode);
	if (sync_mode & SYNC_WAIT) {
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
		xfs_fs_cmn_err(CE_WARN, ip->i_mount,
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
	write_lock(&pag->pag_ici_lock);
	if (!radix_tree_delete(&pag->pag_ici_root,
				XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino)))
		ASSERT(0);
	__xfs_inode_clear_reclaim(pag, ip);
	write_unlock(&pag->pag_ici_lock);

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

int
xfs_reclaim_inodes(
	xfs_mount_t	*mp,
	int		mode)
{
	return xfs_inode_ag_iterator(mp, xfs_reclaim_inode, mode,
					XFS_ICI_RECLAIM_TAG, 1, NULL);
}

/*
 * Shrinker infrastructure.
 */
static int
xfs_reclaim_inode_shrink(
	struct shrinker	*shrink,
	int		nr_to_scan,
	gfp_t		gfp_mask)
{
	struct xfs_mount *mp;
	struct xfs_perag *pag;
	xfs_agnumber_t	ag;
	int		reclaimable;

	mp = container_of(shrink, struct xfs_mount, m_inode_shrink);
	if (nr_to_scan) {
		if (!(gfp_mask & __GFP_FS))
			return -1;

		xfs_inode_ag_iterator(mp, xfs_reclaim_inode, 0,
					XFS_ICI_RECLAIM_TAG, 1, &nr_to_scan);
		/* if we don't exhaust the scan, don't bother coming back */
		if (nr_to_scan > 0)
			return -1;
       }

	reclaimable = 0;
	ag = 0;
	while ((pag = xfs_inode_ag_iter_next_pag(mp, &ag,
					XFS_ICI_RECLAIM_TAG))) {
		reclaimable += pag->pag_ici_reclaimable;
		xfs_perag_put(pag);
	}
	return reclaimable;
}

void
xfs_inode_shrinker_register(
	struct xfs_mount	*mp)
{
	mp->m_inode_shrink.shrink = xfs_reclaim_inode_shrink;
	mp->m_inode_shrink.seeks = DEFAULT_SEEKS;
	register_shrinker(&mp->m_inode_shrink);
}

void
xfs_inode_shrinker_unregister(
	struct xfs_mount	*mp)
{
	unregister_shrinker(&mp->m_inode_shrink);
}
