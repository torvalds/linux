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
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_inode_item.h"
#include "xfs_quota.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_bmap_util.h"
#include "xfs_dquot_item.h"
#include "xfs_dquot.h"

#include <linux/kthread.h>
#include <linux/freezer.h>

/*
 * Allocate and initialise an xfs_inode.
 */
struct xfs_inode *
xfs_inode_alloc(
	struct xfs_mount	*mp,
	xfs_ino_t		ino)
{
	struct xfs_inode	*ip;

	/*
	 * if this didn't occur in transactions, we could use
	 * KM_MAYFAIL and return NULL here on ENOMEM. Set the
	 * code up to do this anyway.
	 */
	ip = kmem_zone_alloc(xfs_inode_zone, KM_SLEEP);
	if (!ip)
		return NULL;
	if (inode_init_always(mp->m_super, VFS_I(ip))) {
		kmem_zone_free(xfs_inode_zone, ip);
		return NULL;
	}

	/* VFS doesn't initialise i_mode! */
	VFS_I(ip)->i_mode = 0;

	XFS_STATS_INC(mp, vn_active);
	ASSERT(atomic_read(&ip->i_pincount) == 0);
	ASSERT(!spin_is_locked(&ip->i_flags_lock));
	ASSERT(!xfs_isiflocked(ip));
	ASSERT(ip->i_ino == 0);

	mrlock_init(&ip->i_iolock, MRLOCK_BARRIER, "xfsio", ip->i_ino);

	/* initialise the xfs inode */
	ip->i_ino = ino;
	ip->i_mount = mp;
	memset(&ip->i_imap, 0, sizeof(struct xfs_imap));
	ip->i_afp = NULL;
	memset(&ip->i_df, 0, sizeof(xfs_ifork_t));
	ip->i_flags = 0;
	ip->i_delayed_blks = 0;
	memset(&ip->i_d, 0, sizeof(ip->i_d));

	return ip;
}

STATIC void
xfs_inode_free_callback(
	struct rcu_head		*head)
{
	struct inode		*inode = container_of(head, struct inode, i_rcu);
	struct xfs_inode	*ip = XFS_I(inode);

	switch (VFS_I(ip)->i_mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		xfs_idestroy_fork(ip, XFS_DATA_FORK);
		break;
	}

	if (ip->i_afp)
		xfs_idestroy_fork(ip, XFS_ATTR_FORK);

	if (ip->i_itemp) {
		ASSERT(!(ip->i_itemp->ili_item.li_flags & XFS_LI_IN_AIL));
		xfs_inode_item_destroy(ip);
		ip->i_itemp = NULL;
	}

	kmem_zone_free(xfs_inode_zone, ip);
}

static void
__xfs_inode_free(
	struct xfs_inode	*ip)
{
	/* asserts to verify all state is correct here */
	ASSERT(atomic_read(&ip->i_pincount) == 0);
	ASSERT(!xfs_isiflocked(ip));
	XFS_STATS_DEC(ip->i_mount, vn_active);

	call_rcu(&VFS_I(ip)->i_rcu, xfs_inode_free_callback);
}

void
xfs_inode_free(
	struct xfs_inode	*ip)
{
	/*
	 * Because we use RCU freeing we need to ensure the inode always
	 * appears to be reclaimed with an invalid inode number when in the
	 * free state. The ip->i_flags_lock provides the barrier against lookup
	 * races.
	 */
	spin_lock(&ip->i_flags_lock);
	ip->i_flags = XFS_IRECLAIM;
	ip->i_ino = 0;
	spin_unlock(&ip->i_flags_lock);

	__xfs_inode_free(ip);
}

/*
 * Queue a new inode reclaim pass if there are reclaimable inodes and there
 * isn't a reclaim pass already in progress. By default it runs every 5s based
 * on the xfs periodic sync default of 30s. Perhaps this should have it's own
 * tunable, but that can be done if this method proves to be ineffective or too
 * aggressive.
 */
static void
xfs_reclaim_work_queue(
	struct xfs_mount        *mp)
{

	rcu_read_lock();
	if (radix_tree_tagged(&mp->m_perag_tree, XFS_ICI_RECLAIM_TAG)) {
		queue_delayed_work(mp->m_reclaim_workqueue, &mp->m_reclaim_work,
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
void
xfs_reclaim_worker(
	struct work_struct *work)
{
	struct xfs_mount *mp = container_of(to_delayed_work(work),
					struct xfs_mount, m_reclaim_work);

	xfs_reclaim_inodes(mp, SYNC_TRYLOCK);
	xfs_reclaim_work_queue(mp);
}

static void
xfs_perag_set_reclaim_tag(
	struct xfs_perag	*pag)
{
	struct xfs_mount	*mp = pag->pag_mount;

	ASSERT(spin_is_locked(&pag->pag_ici_lock));
	if (pag->pag_ici_reclaimable++)
		return;

	/* propagate the reclaim tag up into the perag radix tree */
	spin_lock(&mp->m_perag_lock);
	radix_tree_tag_set(&mp->m_perag_tree, pag->pag_agno,
			   XFS_ICI_RECLAIM_TAG);
	spin_unlock(&mp->m_perag_lock);

	/* schedule periodic background inode reclaim */
	xfs_reclaim_work_queue(mp);

	trace_xfs_perag_set_reclaim(mp, pag->pag_agno, -1, _RET_IP_);
}

static void
xfs_perag_clear_reclaim_tag(
	struct xfs_perag	*pag)
{
	struct xfs_mount	*mp = pag->pag_mount;

	ASSERT(spin_is_locked(&pag->pag_ici_lock));
	if (--pag->pag_ici_reclaimable)
		return;

	/* clear the reclaim tag from the perag radix tree */
	spin_lock(&mp->m_perag_lock);
	radix_tree_tag_clear(&mp->m_perag_tree, pag->pag_agno,
			     XFS_ICI_RECLAIM_TAG);
	spin_unlock(&mp->m_perag_lock);
	trace_xfs_perag_clear_reclaim(mp, pag->pag_agno, -1, _RET_IP_);
}


/*
 * We set the inode flag atomically with the radix tree tag.
 * Once we get tag lookups on the radix tree, this inode flag
 * can go away.
 */
void
xfs_inode_set_reclaim_tag(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_perag	*pag;

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));
	spin_lock(&pag->pag_ici_lock);
	spin_lock(&ip->i_flags_lock);

	radix_tree_tag_set(&pag->pag_ici_root, XFS_INO_TO_AGINO(mp, ip->i_ino),
			   XFS_ICI_RECLAIM_TAG);
	xfs_perag_set_reclaim_tag(pag);
	__xfs_iflags_set(ip, XFS_IRECLAIMABLE);

	spin_unlock(&ip->i_flags_lock);
	spin_unlock(&pag->pag_ici_lock);
	xfs_perag_put(pag);
}

STATIC void
xfs_inode_clear_reclaim_tag(
	struct xfs_perag	*pag,
	xfs_ino_t		ino)
{
	radix_tree_tag_clear(&pag->pag_ici_root,
			     XFS_INO_TO_AGINO(pag->pag_mount, ino),
			     XFS_ICI_RECLAIM_TAG);
	xfs_perag_clear_reclaim_tag(pag);
}

/*
 * When we recycle a reclaimable inode, we need to re-initialise the VFS inode
 * part of the structure. This is made more complex by the fact we store
 * information about the on-disk values in the VFS inode and so we can't just
 * overwrite the values unconditionally. Hence we save the parameters we
 * need to retain across reinitialisation, and rewrite them into the VFS inode
 * after reinitialisation even if it fails.
 */
static int
xfs_reinit_inode(
	struct xfs_mount	*mp,
	struct inode		*inode)
{
	int		error;
	uint32_t	nlink = inode->i_nlink;
	uint32_t	generation = inode->i_generation;
	uint64_t	version = inode->i_version;
	umode_t		mode = inode->i_mode;

	error = inode_init_always(mp->m_super, inode);

	set_nlink(inode, nlink);
	inode->i_generation = generation;
	inode->i_version = version;
	inode->i_mode = mode;
	return error;
}

/*
 * Check the validity of the inode we just found it the cache
 */
static int
xfs_iget_cache_hit(
	struct xfs_perag	*pag,
	struct xfs_inode	*ip,
	xfs_ino_t		ino,
	int			flags,
	int			lock_flags) __releases(RCU)
{
	struct inode		*inode = VFS_I(ip);
	struct xfs_mount	*mp = ip->i_mount;
	int			error;

	/*
	 * check for re-use of an inode within an RCU grace period due to the
	 * radix tree nodes not being updated yet. We monitor for this by
	 * setting the inode number to zero before freeing the inode structure.
	 * If the inode has been reallocated and set up, then the inode number
	 * will not match, so check for that, too.
	 */
	spin_lock(&ip->i_flags_lock);
	if (ip->i_ino != ino) {
		trace_xfs_iget_skip(ip);
		XFS_STATS_INC(mp, xs_ig_frecycle);
		error = -EAGAIN;
		goto out_error;
	}


	/*
	 * If we are racing with another cache hit that is currently
	 * instantiating this inode or currently recycling it out of
	 * reclaimabe state, wait for the initialisation to complete
	 * before continuing.
	 *
	 * XXX(hch): eventually we should do something equivalent to
	 *	     wait_on_inode to wait for these flags to be cleared
	 *	     instead of polling for it.
	 */
	if (ip->i_flags & (XFS_INEW|XFS_IRECLAIM)) {
		trace_xfs_iget_skip(ip);
		XFS_STATS_INC(mp, xs_ig_frecycle);
		error = -EAGAIN;
		goto out_error;
	}

	/*
	 * If lookup is racing with unlink return an error immediately.
	 */
	if (VFS_I(ip)->i_mode == 0 && !(flags & XFS_IGET_CREATE)) {
		error = -ENOENT;
		goto out_error;
	}

	/*
	 * If IRECLAIMABLE is set, we've torn down the VFS inode already.
	 * Need to carefully get it back into useable state.
	 */
	if (ip->i_flags & XFS_IRECLAIMABLE) {
		trace_xfs_iget_reclaim(ip);

		/*
		 * We need to set XFS_IRECLAIM to prevent xfs_reclaim_inode
		 * from stomping over us while we recycle the inode.  We can't
		 * clear the radix tree reclaimable tag yet as it requires
		 * pag_ici_lock to be held exclusive.
		 */
		ip->i_flags |= XFS_IRECLAIM;

		spin_unlock(&ip->i_flags_lock);
		rcu_read_unlock();

		error = xfs_reinit_inode(mp, inode);
		if (error) {
			/*
			 * Re-initializing the inode failed, and we are in deep
			 * trouble.  Try to re-add it to the reclaim list.
			 */
			rcu_read_lock();
			spin_lock(&ip->i_flags_lock);

			ip->i_flags &= ~(XFS_INEW | XFS_IRECLAIM);
			ASSERT(ip->i_flags & XFS_IRECLAIMABLE);
			trace_xfs_iget_reclaim_fail(ip);
			goto out_error;
		}

		spin_lock(&pag->pag_ici_lock);
		spin_lock(&ip->i_flags_lock);

		/*
		 * Clear the per-lifetime state in the inode as we are now
		 * effectively a new inode and need to return to the initial
		 * state before reuse occurs.
		 */
		ip->i_flags &= ~XFS_IRECLAIM_RESET_FLAGS;
		ip->i_flags |= XFS_INEW;
		xfs_inode_clear_reclaim_tag(pag, ip->i_ino);
		inode->i_state = I_NEW;

		ASSERT(!rwsem_is_locked(&ip->i_iolock.mr_lock));
		mrlock_init(&ip->i_iolock, MRLOCK_BARRIER, "xfsio", ip->i_ino);

		spin_unlock(&ip->i_flags_lock);
		spin_unlock(&pag->pag_ici_lock);
	} else {
		/* If the VFS inode is being torn down, pause and try again. */
		if (!igrab(inode)) {
			trace_xfs_iget_skip(ip);
			error = -EAGAIN;
			goto out_error;
		}

		/* We've got a live one. */
		spin_unlock(&ip->i_flags_lock);
		rcu_read_unlock();
		trace_xfs_iget_hit(ip);
	}

	if (lock_flags != 0)
		xfs_ilock(ip, lock_flags);

	xfs_iflags_clear(ip, XFS_ISTALE | XFS_IDONTCACHE);
	XFS_STATS_INC(mp, xs_ig_found);

	return 0;

out_error:
	spin_unlock(&ip->i_flags_lock);
	rcu_read_unlock();
	return error;
}


static int
xfs_iget_cache_miss(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	xfs_trans_t		*tp,
	xfs_ino_t		ino,
	struct xfs_inode	**ipp,
	int			flags,
	int			lock_flags)
{
	struct xfs_inode	*ip;
	int			error;
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, ino);
	int			iflags;

	ip = xfs_inode_alloc(mp, ino);
	if (!ip)
		return -ENOMEM;

	error = xfs_iread(mp, tp, ip, flags);
	if (error)
		goto out_destroy;

	trace_xfs_iget_miss(ip);

	if ((VFS_I(ip)->i_mode == 0) && !(flags & XFS_IGET_CREATE)) {
		error = -ENOENT;
		goto out_destroy;
	}

	/*
	 * Preload the radix tree so we can insert safely under the
	 * write spinlock. Note that we cannot sleep inside the preload
	 * region. Since we can be called from transaction context, don't
	 * recurse into the file system.
	 */
	if (radix_tree_preload(GFP_NOFS)) {
		error = -EAGAIN;
		goto out_destroy;
	}

	/*
	 * Because the inode hasn't been added to the radix-tree yet it can't
	 * be found by another thread, so we can do the non-sleeping lock here.
	 */
	if (lock_flags) {
		if (!xfs_ilock_nowait(ip, lock_flags))
			BUG();
	}

	/*
	 * These values must be set before inserting the inode into the radix
	 * tree as the moment it is inserted a concurrent lookup (allowed by the
	 * RCU locking mechanism) can find it and that lookup must see that this
	 * is an inode currently under construction (i.e. that XFS_INEW is set).
	 * The ip->i_flags_lock that protects the XFS_INEW flag forms the
	 * memory barrier that ensures this detection works correctly at lookup
	 * time.
	 */
	iflags = XFS_INEW;
	if (flags & XFS_IGET_DONTCACHE)
		iflags |= XFS_IDONTCACHE;
	ip->i_udquot = NULL;
	ip->i_gdquot = NULL;
	ip->i_pdquot = NULL;
	xfs_iflags_set(ip, iflags);

	/* insert the new inode */
	spin_lock(&pag->pag_ici_lock);
	error = radix_tree_insert(&pag->pag_ici_root, agino, ip);
	if (unlikely(error)) {
		WARN_ON(error != -EEXIST);
		XFS_STATS_INC(mp, xs_ig_dup);
		error = -EAGAIN;
		goto out_preload_end;
	}
	spin_unlock(&pag->pag_ici_lock);
	radix_tree_preload_end();

	*ipp = ip;
	return 0;

out_preload_end:
	spin_unlock(&pag->pag_ici_lock);
	radix_tree_preload_end();
	if (lock_flags)
		xfs_iunlock(ip, lock_flags);
out_destroy:
	__destroy_inode(VFS_I(ip));
	xfs_inode_free(ip);
	return error;
}

/*
 * Look up an inode by number in the given file system.
 * The inode is looked up in the cache held in each AG.
 * If the inode is found in the cache, initialise the vfs inode
 * if necessary.
 *
 * If it is not in core, read it in from the file system's device,
 * add it to the cache and initialise the vfs inode.
 *
 * The inode is locked according to the value of the lock_flags parameter.
 * This flag parameter indicates how and if the inode's IO lock and inode lock
 * should be taken.
 *
 * mp -- the mount point structure for the current file system.  It points
 *       to the inode hash table.
 * tp -- a pointer to the current transaction if there is one.  This is
 *       simply passed through to the xfs_iread() call.
 * ino -- the number of the inode desired.  This is the unique identifier
 *        within the file system for the inode being requested.
 * lock_flags -- flags indicating how to lock the inode.  See the comment
 *		 for xfs_ilock() for a list of valid values.
 */
int
xfs_iget(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	uint		flags,
	uint		lock_flags,
	xfs_inode_t	**ipp)
{
	xfs_inode_t	*ip;
	int		error;
	xfs_perag_t	*pag;
	xfs_agino_t	agino;

	/*
	 * xfs_reclaim_inode() uses the ILOCK to ensure an inode
	 * doesn't get freed while it's being referenced during a
	 * radix tree traversal here.  It assumes this function
	 * aqcuires only the ILOCK (and therefore it has no need to
	 * involve the IOLOCK in this synchronization).
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_EXCL | XFS_IOLOCK_SHARED)) == 0);

	/* reject inode numbers outside existing AGs */
	if (!ino || XFS_INO_TO_AGNO(mp, ino) >= mp->m_sb.sb_agcount)
		return -EINVAL;

	XFS_STATS_INC(mp, xs_ig_attempts);

	/* get the perag structure and ensure that it's inode capable */
	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ino));
	agino = XFS_INO_TO_AGINO(mp, ino);

again:
	error = 0;
	rcu_read_lock();
	ip = radix_tree_lookup(&pag->pag_ici_root, agino);

	if (ip) {
		error = xfs_iget_cache_hit(pag, ip, ino, flags, lock_flags);
		if (error)
			goto out_error_or_again;
	} else {
		rcu_read_unlock();
		XFS_STATS_INC(mp, xs_ig_missed);

		error = xfs_iget_cache_miss(mp, pag, tp, ino, &ip,
							flags, lock_flags);
		if (error)
			goto out_error_or_again;
	}
	xfs_perag_put(pag);

	*ipp = ip;

	/*
	 * If we have a real type for an on-disk inode, we can setup the inode
	 * now.	 If it's a new inode being created, xfs_ialloc will handle it.
	 */
	if (xfs_iflags_test(ip, XFS_INEW) && VFS_I(ip)->i_mode != 0)
		xfs_setup_existing_inode(ip);
	return 0;

out_error_or_again:
	if (error == -EAGAIN) {
		delay(1);
		goto again;
	}
	xfs_perag_put(pag);
	return error;
}

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
		return -EFSCORRUPTED;

	/* If we can't grab the inode, it must on it's way to reclaim. */
	if (!igrab(inode))
		return -ENOENT;

	/* inode is valid */
	return 0;

out_unlock_noent:
	spin_unlock(&ip->i_flags_lock);
	return -ENOENT;
}

STATIC int
xfs_inode_ag_walk(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	int			(*execute)(struct xfs_inode *ip, int flags,
					   void *args),
	int			flags,
	void			*args,
	int			tag)
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

		if (tag == -1)
			nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
					(void **)batch, first_index,
					XFS_LOOKUP_BATCH);
		else
			nr_found = radix_tree_gang_lookup_tag(
					&pag->pag_ici_root,
					(void **) batch, first_index,
					XFS_LOOKUP_BATCH, tag);

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
			error = execute(batch[i], flags, args);
			IRELE(batch[i]);
			if (error == -EAGAIN) {
				skipped++;
				continue;
			}
			if (error && last_error != -EFSCORRUPTED)
				last_error = error;
		}

		/* bail out if the filesystem is corrupted.  */
		if (error == -EFSCORRUPTED)
			break;

		cond_resched();

	} while (nr_found && !done);

	if (skipped) {
		delay(1);
		goto restart;
	}
	return last_error;
}

/*
 * Background scanning to trim post-EOF preallocated space. This is queued
 * based on the 'speculative_prealloc_lifetime' tunable (5m by default).
 */
void
xfs_queue_eofblocks(
	struct xfs_mount *mp)
{
	rcu_read_lock();
	if (radix_tree_tagged(&mp->m_perag_tree, XFS_ICI_EOFBLOCKS_TAG))
		queue_delayed_work(mp->m_eofblocks_workqueue,
				   &mp->m_eofblocks_work,
				   msecs_to_jiffies(xfs_eofb_secs * 1000));
	rcu_read_unlock();
}

void
xfs_eofblocks_worker(
	struct work_struct *work)
{
	struct xfs_mount *mp = container_of(to_delayed_work(work),
				struct xfs_mount, m_eofblocks_work);
	xfs_icache_free_eofblocks(mp, NULL);
	xfs_queue_eofblocks(mp);
}

int
xfs_inode_ag_iterator(
	struct xfs_mount	*mp,
	int			(*execute)(struct xfs_inode *ip, int flags,
					   void *args),
	int			flags,
	void			*args)
{
	struct xfs_perag	*pag;
	int			error = 0;
	int			last_error = 0;
	xfs_agnumber_t		ag;

	ag = 0;
	while ((pag = xfs_perag_get(mp, ag))) {
		ag = pag->pag_agno + 1;
		error = xfs_inode_ag_walk(mp, pag, execute, flags, args, -1);
		xfs_perag_put(pag);
		if (error) {
			last_error = error;
			if (error == -EFSCORRUPTED)
				break;
		}
	}
	return last_error;
}

int
xfs_inode_ag_iterator_tag(
	struct xfs_mount	*mp,
	int			(*execute)(struct xfs_inode *ip, int flags,
					   void *args),
	int			flags,
	void			*args,
	int			tag)
{
	struct xfs_perag	*pag;
	int			error = 0;
	int			last_error = 0;
	xfs_agnumber_t		ag;

	ag = 0;
	while ((pag = xfs_perag_get_tag(mp, ag, tag))) {
		ag = pag->pag_agno + 1;
		error = xfs_inode_ag_walk(mp, pag, execute, flags, args, tag);
		xfs_perag_put(pag);
		if (error) {
			last_error = error;
			if (error == -EFSCORRUPTED)
				break;
		}
	}
	return last_error;
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
	 * If we are asked for non-blocking operation, do unlocked checks to
	 * see if the inode already is being flushed or in reclaim to avoid
	 * lock traffic.
	 */
	if ((flags & SYNC_TRYLOCK) &&
	    __xfs_iflags_test(ip, XFS_IFLOCK | XFS_IRECLAIM))
		return 1;

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
 * Inodes in different states need to be treated differently. The following
 * table lists the inode states and the reclaim actions necessary:
 *
 *	inode state	     iflush ret		required action
 *      ---------------      ----------         ---------------
 *	bad			-		reclaim
 *	shutdown		EIO		unpin and reclaim
 *	clean, unpinned		0		reclaim
 *	stale, unpinned		0		reclaim
 *	clean, pinned(*)	0		requeue
 *	stale, pinned		EAGAIN		requeue
 *	dirty, async		-		requeue
 *	dirty, sync		0		reclaim
 *
 * (*) dgc: I don't think the clean, pinned state is possible but it gets
 * handled anyway given the order of checks implemented.
 *
 * Also, because we get the flush lock first, we know that any inode that has
 * been flushed delwri has had the flush completed by the time we check that
 * the inode is clean.
 *
 * Note that because the inode is flushed delayed write by AIL pushing, the
 * flush lock may already be held here and waiting on it can result in very
 * long latencies.  Hence for sync reclaims, where we wait on the flush lock,
 * the caller should push the AIL first before trying to reclaim inodes to
 * minimise the amount of time spent waiting.  For background relaim, we only
 * bother to reclaim clean inodes anyway.
 *
 * Hence the order of actions after gaining the locks should be:
 *	bad		=> reclaim
 *	shutdown	=> unpin and reclaim
 *	pinned, async	=> requeue
 *	pinned, sync	=> unpin
 *	stale		=> reclaim
 *	clean		=> reclaim
 *	dirty, async	=> requeue
 *	dirty, sync	=> flush, wait and reclaim
 */
STATIC int
xfs_reclaim_inode(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag,
	int			sync_mode)
{
	struct xfs_buf		*bp = NULL;
	xfs_ino_t		ino = ip->i_ino; /* for radix_tree_delete */
	int			error;

restart:
	error = 0;
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (!xfs_iflock_nowait(ip)) {
		if (!(sync_mode & SYNC_WAIT))
			goto out;
		xfs_iflock(ip);
	}

	if (XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		xfs_iunpin_wait(ip);
		xfs_iflush_abort(ip, false);
		goto reclaim;
	}
	if (xfs_ipincount(ip)) {
		if (!(sync_mode & SYNC_WAIT))
			goto out_ifunlock;
		xfs_iunpin_wait(ip);
	}
	if (xfs_iflags_test(ip, XFS_ISTALE))
		goto reclaim;
	if (xfs_inode_clean(ip))
		goto reclaim;

	/*
	 * Never flush out dirty data during non-blocking reclaim, as it would
	 * just contend with AIL pushing trying to do the same job.
	 */
	if (!(sync_mode & SYNC_WAIT))
		goto out_ifunlock;

	/*
	 * Now we have an inode that needs flushing.
	 *
	 * Note that xfs_iflush will never block on the inode buffer lock, as
	 * xfs_ifree_cluster() can lock the inode buffer before it locks the
	 * ip->i_lock, and we are doing the exact opposite here.  As a result,
	 * doing a blocking xfs_imap_to_bp() to get the cluster buffer would
	 * result in an ABBA deadlock with xfs_ifree_cluster().
	 *
	 * As xfs_ifree_cluser() must gather all inodes that are active in the
	 * cache to mark them stale, if we hit this case we don't actually want
	 * to do IO here - we want the inode marked stale so we can simply
	 * reclaim it.  Hence if we get an EAGAIN error here,  just unlock the
	 * inode, back off and try again.  Hopefully the next pass through will
	 * see the stale flag set on the inode.
	 */
	error = xfs_iflush(ip, &bp);
	if (error == -EAGAIN) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		/* backoff longer than in xfs_ifree_cluster */
		delay(2);
		goto restart;
	}

	if (!error) {
		error = xfs_bwrite(bp);
		xfs_buf_relse(bp);
	}

	xfs_iflock(ip);
reclaim:
	/*
	 * Because we use RCU freeing we need to ensure the inode always appears
	 * to be reclaimed with an invalid inode number when in the free state.
	 * We do this as early as possible under the ILOCK and flush lock so
	 * that xfs_iflush_cluster() can be guaranteed to detect races with us
	 * here. By doing this, we guarantee that once xfs_iflush_cluster has
	 * locked both the XFS_ILOCK and the flush lock that it will see either
	 * a valid, flushable inode that will serialise correctly against the
	 * locks below, or it will see a clean (and invalid) inode that it can
	 * skip.
	 */
	spin_lock(&ip->i_flags_lock);
	ip->i_flags = XFS_IRECLAIM;
	ip->i_ino = 0;
	spin_unlock(&ip->i_flags_lock);

	xfs_ifunlock(ip);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	XFS_STATS_INC(ip->i_mount, xs_ig_reclaims);
	/*
	 * Remove the inode from the per-AG radix tree.
	 *
	 * Because radix_tree_delete won't complain even if the item was never
	 * added to the tree assert that it's been there before to catch
	 * problems with the inode life time early on.
	 */
	spin_lock(&pag->pag_ici_lock);
	if (!radix_tree_delete(&pag->pag_ici_root,
				XFS_INO_TO_AGINO(ip->i_mount, ino)))
		ASSERT(0);
	xfs_perag_clear_reclaim_tag(pag);
	spin_unlock(&pag->pag_ici_lock);

	/*
	 * Here we do an (almost) spurious inode lock in order to coordinate
	 * with inode cache radix tree lookups.  This is because the lookup
	 * can reference the inodes in the cache without taking references.
	 *
	 * We make that OK here by ensuring that we wait until the inode is
	 * unlocked after the lookup before we go ahead and free it.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_qm_dqdetach(ip);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	__xfs_inode_free(ip);
	return error;

out_ifunlock:
	xfs_ifunlock(ip);
out:
	xfs_iflags_clear(ip, XFS_IRECLAIM);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	/*
	 * We could return -EAGAIN here to make reclaim rescan the inode tree in
	 * a short while. However, this just burns CPU time scanning the tree
	 * waiting for IO to complete and the reclaim work never goes back to
	 * the idle state. Instead, return 0 to let the next scheduled
	 * background reclaim attempt to reclaim the inode again.
	 */
	return 0;
}

/*
 * Walk the AGs and reclaim the inodes in them. Even if the filesystem is
 * corrupted, we still want to try to reclaim all the inodes. If we don't,
 * then a shut down during filesystem unmount reclaim walk leak all the
 * unreclaimed inodes.
 */
STATIC int
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
				if (error && last_error != -EFSCORRUPTED)
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
	return last_error;
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
long
xfs_reclaim_inodes_nr(
	struct xfs_mount	*mp,
	int			nr_to_scan)
{
	/* kick background reclaimer and push the AIL */
	xfs_reclaim_work_queue(mp);
	xfs_ail_push_all(mp->m_ail);

	return xfs_reclaim_inodes_ag(mp, SYNC_TRYLOCK | SYNC_WAIT, &nr_to_scan);
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

STATIC int
xfs_inode_match_id(
	struct xfs_inode	*ip,
	struct xfs_eofblocks	*eofb)
{
	if ((eofb->eof_flags & XFS_EOF_FLAGS_UID) &&
	    !uid_eq(VFS_I(ip)->i_uid, eofb->eof_uid))
		return 0;

	if ((eofb->eof_flags & XFS_EOF_FLAGS_GID) &&
	    !gid_eq(VFS_I(ip)->i_gid, eofb->eof_gid))
		return 0;

	if ((eofb->eof_flags & XFS_EOF_FLAGS_PRID) &&
	    xfs_get_projid(ip) != eofb->eof_prid)
		return 0;

	return 1;
}

/*
 * A union-based inode filtering algorithm. Process the inode if any of the
 * criteria match. This is for global/internal scans only.
 */
STATIC int
xfs_inode_match_id_union(
	struct xfs_inode	*ip,
	struct xfs_eofblocks	*eofb)
{
	if ((eofb->eof_flags & XFS_EOF_FLAGS_UID) &&
	    uid_eq(VFS_I(ip)->i_uid, eofb->eof_uid))
		return 1;

	if ((eofb->eof_flags & XFS_EOF_FLAGS_GID) &&
	    gid_eq(VFS_I(ip)->i_gid, eofb->eof_gid))
		return 1;

	if ((eofb->eof_flags & XFS_EOF_FLAGS_PRID) &&
	    xfs_get_projid(ip) == eofb->eof_prid)
		return 1;

	return 0;
}

STATIC int
xfs_inode_free_eofblocks(
	struct xfs_inode	*ip,
	int			flags,
	void			*args)
{
	int ret;
	struct xfs_eofblocks *eofb = args;
	bool need_iolock = true;
	int match;

	ASSERT(!eofb || (eofb && eofb->eof_scan_owner != 0));

	if (!xfs_can_free_eofblocks(ip, false)) {
		/* inode could be preallocated or append-only */
		trace_xfs_inode_free_eofblocks_invalid(ip);
		xfs_inode_clear_eofblocks_tag(ip);
		return 0;
	}

	/*
	 * If the mapping is dirty the operation can block and wait for some
	 * time. Unless we are waiting, skip it.
	 */
	if (!(flags & SYNC_WAIT) &&
	    mapping_tagged(VFS_I(ip)->i_mapping, PAGECACHE_TAG_DIRTY))
		return 0;

	if (eofb) {
		if (eofb->eof_flags & XFS_EOF_FLAGS_UNION)
			match = xfs_inode_match_id_union(ip, eofb);
		else
			match = xfs_inode_match_id(ip, eofb);
		if (!match)
			return 0;

		/* skip the inode if the file size is too small */
		if (eofb->eof_flags & XFS_EOF_FLAGS_MINFILESIZE &&
		    XFS_ISIZE(ip) < eofb->eof_min_file_size)
			return 0;

		/*
		 * A scan owner implies we already hold the iolock. Skip it in
		 * xfs_free_eofblocks() to avoid deadlock. This also eliminates
		 * the possibility of EAGAIN being returned.
		 */
		if (eofb->eof_scan_owner == ip->i_ino)
			need_iolock = false;
	}

	ret = xfs_free_eofblocks(ip->i_mount, ip, need_iolock);

	/* don't revisit the inode if we're not waiting */
	if (ret == -EAGAIN && !(flags & SYNC_WAIT))
		ret = 0;

	return ret;
}

int
xfs_icache_free_eofblocks(
	struct xfs_mount	*mp,
	struct xfs_eofblocks	*eofb)
{
	int flags = SYNC_TRYLOCK;

	if (eofb && (eofb->eof_flags & XFS_EOF_FLAGS_SYNC))
		flags = SYNC_WAIT;

	return xfs_inode_ag_iterator_tag(mp, xfs_inode_free_eofblocks, flags,
					 eofb, XFS_ICI_EOFBLOCKS_TAG);
}

/*
 * Run eofblocks scans on the quotas applicable to the inode. For inodes with
 * multiple quotas, we don't know exactly which quota caused an allocation
 * failure. We make a best effort by including each quota under low free space
 * conditions (less than 1% free space) in the scan.
 */
int
xfs_inode_free_quota_eofblocks(
	struct xfs_inode *ip)
{
	int scan = 0;
	struct xfs_eofblocks eofb = {0};
	struct xfs_dquot *dq;

	ASSERT(xfs_isilocked(ip, XFS_IOLOCK_EXCL));

	/*
	 * Set the scan owner to avoid a potential livelock. Otherwise, the scan
	 * can repeatedly trylock on the inode we're currently processing. We
	 * run a sync scan to increase effectiveness and use the union filter to
	 * cover all applicable quotas in a single scan.
	 */
	eofb.eof_scan_owner = ip->i_ino;
	eofb.eof_flags = XFS_EOF_FLAGS_UNION|XFS_EOF_FLAGS_SYNC;

	if (XFS_IS_UQUOTA_ENFORCED(ip->i_mount)) {
		dq = xfs_inode_dquot(ip, XFS_DQ_USER);
		if (dq && xfs_dquot_lowsp(dq)) {
			eofb.eof_uid = VFS_I(ip)->i_uid;
			eofb.eof_flags |= XFS_EOF_FLAGS_UID;
			scan = 1;
		}
	}

	if (XFS_IS_GQUOTA_ENFORCED(ip->i_mount)) {
		dq = xfs_inode_dquot(ip, XFS_DQ_GROUP);
		if (dq && xfs_dquot_lowsp(dq)) {
			eofb.eof_gid = VFS_I(ip)->i_gid;
			eofb.eof_flags |= XFS_EOF_FLAGS_GID;
			scan = 1;
		}
	}

	if (scan)
		xfs_icache_free_eofblocks(ip->i_mount, &eofb);

	return scan;
}

void
xfs_inode_set_eofblocks_tag(
	xfs_inode_t	*ip)
{
	struct xfs_mount *mp = ip->i_mount;
	struct xfs_perag *pag;
	int tagged;

	/*
	 * Don't bother locking the AG and looking up in the radix trees
	 * if we already know that we have the tag set.
	 */
	if (ip->i_flags & XFS_IEOFBLOCKS)
		return;
	spin_lock(&ip->i_flags_lock);
	ip->i_flags |= XFS_IEOFBLOCKS;
	spin_unlock(&ip->i_flags_lock);

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));
	spin_lock(&pag->pag_ici_lock);
	trace_xfs_inode_set_eofblocks_tag(ip);

	tagged = radix_tree_tagged(&pag->pag_ici_root,
				   XFS_ICI_EOFBLOCKS_TAG);
	radix_tree_tag_set(&pag->pag_ici_root,
			   XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino),
			   XFS_ICI_EOFBLOCKS_TAG);
	if (!tagged) {
		/* propagate the eofblocks tag up into the perag radix tree */
		spin_lock(&ip->i_mount->m_perag_lock);
		radix_tree_tag_set(&ip->i_mount->m_perag_tree,
				   XFS_INO_TO_AGNO(ip->i_mount, ip->i_ino),
				   XFS_ICI_EOFBLOCKS_TAG);
		spin_unlock(&ip->i_mount->m_perag_lock);

		/* kick off background trimming */
		xfs_queue_eofblocks(ip->i_mount);

		trace_xfs_perag_set_eofblocks(ip->i_mount, pag->pag_agno,
					      -1, _RET_IP_);
	}

	spin_unlock(&pag->pag_ici_lock);
	xfs_perag_put(pag);
}

void
xfs_inode_clear_eofblocks_tag(
	xfs_inode_t	*ip)
{
	struct xfs_mount *mp = ip->i_mount;
	struct xfs_perag *pag;

	spin_lock(&ip->i_flags_lock);
	ip->i_flags &= ~XFS_IEOFBLOCKS;
	spin_unlock(&ip->i_flags_lock);

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));
	spin_lock(&pag->pag_ici_lock);
	trace_xfs_inode_clear_eofblocks_tag(ip);

	radix_tree_tag_clear(&pag->pag_ici_root,
			     XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino),
			     XFS_ICI_EOFBLOCKS_TAG);
	if (!radix_tree_tagged(&pag->pag_ici_root, XFS_ICI_EOFBLOCKS_TAG)) {
		/* clear the eofblocks tag from the perag radix tree */
		spin_lock(&ip->i_mount->m_perag_lock);
		radix_tree_tag_clear(&ip->i_mount->m_perag_tree,
				     XFS_INO_TO_AGNO(ip->i_mount, ip->i_ino),
				     XFS_ICI_EOFBLOCKS_TAG);
		spin_unlock(&ip->i_mount->m_perag_lock);
		trace_xfs_perag_clear_eofblocks(ip->i_mount, pag->pag_agno,
					       -1, _RET_IP_);
	}

	spin_unlock(&pag->pag_ici_lock);
	xfs_perag_put(pag);
}

