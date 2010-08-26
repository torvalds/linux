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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_alloc.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_trans_space.h"
#include "xfs_utils.h"
#include "xfs_qm.h"
#include "xfs_trace.h"

/*
 * The global quota manager. There is only one of these for the entire
 * system, _not_ one per file system. XQM keeps track of the overall
 * quota functionality, including maintaining the freelist and hash
 * tables of dquots.
 */
struct mutex	xfs_Gqm_lock;
struct xfs_qm	*xfs_Gqm;
uint		ndquot;

kmem_zone_t	*qm_dqzone;
kmem_zone_t	*qm_dqtrxzone;

static cred_t	xfs_zerocr;

STATIC void	xfs_qm_list_init(xfs_dqlist_t *, char *, int);
STATIC void	xfs_qm_list_destroy(xfs_dqlist_t *);

STATIC int	xfs_qm_init_quotainos(xfs_mount_t *);
STATIC int	xfs_qm_init_quotainfo(xfs_mount_t *);
STATIC int	xfs_qm_shake(struct shrinker *, int, gfp_t);

static struct shrinker xfs_qm_shaker = {
	.shrink = xfs_qm_shake,
	.seeks = DEFAULT_SEEKS,
};

#ifdef DEBUG
extern struct mutex	qcheck_lock;
#endif

#ifdef QUOTADEBUG
static void
xfs_qm_dquot_list_print(
	struct xfs_mount *mp)
{
	xfs_dquot_t	*dqp;
	int		i = 0;

	list_for_each_entry(dqp, &mp->m_quotainfo->qi_dqlist_lock, qi_mplist) {
		cmn_err(CE_DEBUG, "   %d. \"%d (%s)\"   "
				  "bcnt = %lld, icnt = %lld, refs = %d",
			i++, be32_to_cpu(dqp->q_core.d_id),
			DQFLAGTO_TYPESTR(dqp),
			(long long)be64_to_cpu(dqp->q_core.d_bcount),
			(long long)be64_to_cpu(dqp->q_core.d_icount),
			dqp->q_nrefs);
	}
}
#else
static void xfs_qm_dquot_list_print(struct xfs_mount *mp) { }
#endif

/*
 * Initialize the XQM structure.
 * Note that there is not one quota manager per file system.
 */
STATIC struct xfs_qm *
xfs_Gqm_init(void)
{
	xfs_dqhash_t	*udqhash, *gdqhash;
	xfs_qm_t	*xqm;
	size_t		hsize;
	uint		i;

	/*
	 * Initialize the dquot hash tables.
	 */
	udqhash = kmem_zalloc_greedy(&hsize,
				     XFS_QM_HASHSIZE_LOW * sizeof(xfs_dqhash_t),
				     XFS_QM_HASHSIZE_HIGH * sizeof(xfs_dqhash_t));
	if (!udqhash)
		goto out;

	gdqhash = kmem_zalloc_large(hsize);
	if (!gdqhash)
		goto out_free_udqhash;

	hsize /= sizeof(xfs_dqhash_t);
	ndquot = hsize << 8;

	xqm = kmem_zalloc(sizeof(xfs_qm_t), KM_SLEEP);
	xqm->qm_dqhashmask = hsize - 1;
	xqm->qm_usr_dqhtable = udqhash;
	xqm->qm_grp_dqhtable = gdqhash;
	ASSERT(xqm->qm_usr_dqhtable != NULL);
	ASSERT(xqm->qm_grp_dqhtable != NULL);

	for (i = 0; i < hsize; i++) {
		xfs_qm_list_init(&(xqm->qm_usr_dqhtable[i]), "uxdqh", i);
		xfs_qm_list_init(&(xqm->qm_grp_dqhtable[i]), "gxdqh", i);
	}

	/*
	 * Freelist of all dquots of all file systems
	 */
	INIT_LIST_HEAD(&xqm->qm_dqfrlist);
	xqm->qm_dqfrlist_cnt = 0;
	mutex_init(&xqm->qm_dqfrlist_lock);

	/*
	 * dquot zone. we register our own low-memory callback.
	 */
	if (!qm_dqzone) {
		xqm->qm_dqzone = kmem_zone_init(sizeof(xfs_dquot_t),
						"xfs_dquots");
		qm_dqzone = xqm->qm_dqzone;
	} else
		xqm->qm_dqzone = qm_dqzone;

	register_shrinker(&xfs_qm_shaker);

	/*
	 * The t_dqinfo portion of transactions.
	 */
	if (!qm_dqtrxzone) {
		xqm->qm_dqtrxzone = kmem_zone_init(sizeof(xfs_dquot_acct_t),
						   "xfs_dqtrx");
		qm_dqtrxzone = xqm->qm_dqtrxzone;
	} else
		xqm->qm_dqtrxzone = qm_dqtrxzone;

	atomic_set(&xqm->qm_totaldquots, 0);
	xqm->qm_dqfree_ratio = XFS_QM_DQFREE_RATIO;
	xqm->qm_nrefs = 0;
#ifdef DEBUG
	mutex_init(&qcheck_lock);
#endif
	return xqm;

 out_free_udqhash:
	kmem_free_large(udqhash);
 out:
	return NULL;
}

/*
 * Destroy the global quota manager when its reference count goes to zero.
 */
STATIC void
xfs_qm_destroy(
	struct xfs_qm	*xqm)
{
	struct xfs_dquot *dqp, *n;
	int		hsize, i;

	ASSERT(xqm != NULL);
	ASSERT(xqm->qm_nrefs == 0);
	unregister_shrinker(&xfs_qm_shaker);
	hsize = xqm->qm_dqhashmask + 1;
	for (i = 0; i < hsize; i++) {
		xfs_qm_list_destroy(&(xqm->qm_usr_dqhtable[i]));
		xfs_qm_list_destroy(&(xqm->qm_grp_dqhtable[i]));
	}
	kmem_free_large(xqm->qm_usr_dqhtable);
	kmem_free_large(xqm->qm_grp_dqhtable);
	xqm->qm_usr_dqhtable = NULL;
	xqm->qm_grp_dqhtable = NULL;
	xqm->qm_dqhashmask = 0;

	/* frlist cleanup */
	mutex_lock(&xqm->qm_dqfrlist_lock);
	list_for_each_entry_safe(dqp, n, &xqm->qm_dqfrlist, q_freelist) {
		xfs_dqlock(dqp);
#ifdef QUOTADEBUG
		cmn_err(CE_DEBUG, "FREELIST destroy 0x%p", dqp);
#endif
		list_del_init(&dqp->q_freelist);
		xfs_Gqm->qm_dqfrlist_cnt--;
		xfs_dqunlock(dqp);
		xfs_qm_dqdestroy(dqp);
	}
	mutex_unlock(&xqm->qm_dqfrlist_lock);
	mutex_destroy(&xqm->qm_dqfrlist_lock);
#ifdef DEBUG
	mutex_destroy(&qcheck_lock);
#endif
	kmem_free(xqm);
}

/*
 * Called at mount time to let XQM know that another file system is
 * starting quotas. This isn't crucial information as the individual mount
 * structures are pretty independent, but it helps the XQM keep a
 * global view of what's going on.
 */
/* ARGSUSED */
STATIC int
xfs_qm_hold_quotafs_ref(
	struct xfs_mount *mp)
{
	/*
	 * Need to lock the xfs_Gqm structure for things like this. For example,
	 * the structure could disappear between the entry to this routine and
	 * a HOLD operation if not locked.
	 */
	mutex_lock(&xfs_Gqm_lock);

	if (!xfs_Gqm) {
		xfs_Gqm = xfs_Gqm_init();
		if (!xfs_Gqm) {
			mutex_unlock(&xfs_Gqm_lock);
			return ENOMEM;
		}
	}

	/*
	 * We can keep a list of all filesystems with quotas mounted for
	 * debugging and statistical purposes, but ...
	 * Just take a reference and get out.
	 */
	xfs_Gqm->qm_nrefs++;
	mutex_unlock(&xfs_Gqm_lock);

	return 0;
}


/*
 * Release the reference that a filesystem took at mount time,
 * so that we know when we need to destroy the entire quota manager.
 */
/* ARGSUSED */
STATIC void
xfs_qm_rele_quotafs_ref(
	struct xfs_mount *mp)
{
	xfs_dquot_t	*dqp, *n;

	ASSERT(xfs_Gqm);
	ASSERT(xfs_Gqm->qm_nrefs > 0);

	/*
	 * Go thru the freelist and destroy all inactive dquots.
	 */
	mutex_lock(&xfs_Gqm->qm_dqfrlist_lock);

	list_for_each_entry_safe(dqp, n, &xfs_Gqm->qm_dqfrlist, q_freelist) {
		xfs_dqlock(dqp);
		if (dqp->dq_flags & XFS_DQ_INACTIVE) {
			ASSERT(dqp->q_mount == NULL);
			ASSERT(! XFS_DQ_IS_DIRTY(dqp));
			ASSERT(list_empty(&dqp->q_hashlist));
			ASSERT(list_empty(&dqp->q_mplist));
			list_del_init(&dqp->q_freelist);
			xfs_Gqm->qm_dqfrlist_cnt--;
			xfs_dqunlock(dqp);
			xfs_qm_dqdestroy(dqp);
		} else {
			xfs_dqunlock(dqp);
		}
	}
	mutex_unlock(&xfs_Gqm->qm_dqfrlist_lock);

	/*
	 * Destroy the entire XQM. If somebody mounts with quotaon, this'll
	 * be restarted.
	 */
	mutex_lock(&xfs_Gqm_lock);
	if (--xfs_Gqm->qm_nrefs == 0) {
		xfs_qm_destroy(xfs_Gqm);
		xfs_Gqm = NULL;
	}
	mutex_unlock(&xfs_Gqm_lock);
}

/*
 * Just destroy the quotainfo structure.
 */
void
xfs_qm_unmount(
	struct xfs_mount	*mp)
{
	if (mp->m_quotainfo) {
		xfs_qm_dqpurge_all(mp, XFS_QMOPT_QUOTALL);
		xfs_qm_destroy_quotainfo(mp);
	}
}


/*
 * This is called from xfs_mountfs to start quotas and initialize all
 * necessary data structures like quotainfo.  This is also responsible for
 * running a quotacheck as necessary.  We are guaranteed that the superblock
 * is consistently read in at this point.
 *
 * If we fail here, the mount will continue with quota turned off. We don't
 * need to inidicate success or failure at all.
 */
void
xfs_qm_mount_quotas(
	xfs_mount_t	*mp)
{
	int		error = 0;
	uint		sbf;

	/*
	 * If quotas on realtime volumes is not supported, we disable
	 * quotas immediately.
	 */
	if (mp->m_sb.sb_rextents) {
		cmn_err(CE_NOTE,
			"Cannot turn on quotas for realtime filesystem %s",
			mp->m_fsname);
		mp->m_qflags = 0;
		goto write_changes;
	}

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	/*
	 * Allocate the quotainfo structure inside the mount struct, and
	 * create quotainode(s), and change/rev superblock if necessary.
	 */
	error = xfs_qm_init_quotainfo(mp);
	if (error) {
		/*
		 * We must turn off quotas.
		 */
		ASSERT(mp->m_quotainfo == NULL);
		mp->m_qflags = 0;
		goto write_changes;
	}
	/*
	 * If any of the quotas are not consistent, do a quotacheck.
	 */
	if (XFS_QM_NEED_QUOTACHECK(mp)) {
		error = xfs_qm_quotacheck(mp);
		if (error) {
			/* Quotacheck failed and disabled quotas. */
			return;
		}
	}
	/* 
	 * If one type of quotas is off, then it will lose its
	 * quotachecked status, since we won't be doing accounting for
	 * that type anymore.
	 */
	if (!XFS_IS_UQUOTA_ON(mp))
		mp->m_qflags &= ~XFS_UQUOTA_CHKD;
	if (!(XFS_IS_GQUOTA_ON(mp) || XFS_IS_PQUOTA_ON(mp)))
		mp->m_qflags &= ~XFS_OQUOTA_CHKD;

 write_changes:
	/*
	 * We actually don't have to acquire the m_sb_lock at all.
	 * This can only be called from mount, and that's single threaded. XXX
	 */
	spin_lock(&mp->m_sb_lock);
	sbf = mp->m_sb.sb_qflags;
	mp->m_sb.sb_qflags = mp->m_qflags & XFS_MOUNT_QUOTA_ALL;
	spin_unlock(&mp->m_sb_lock);

	if (sbf != (mp->m_qflags & XFS_MOUNT_QUOTA_ALL)) {
		if (xfs_qm_write_sb_changes(mp, XFS_SB_QFLAGS)) {
			/*
			 * We could only have been turning quotas off.
			 * We aren't in very good shape actually because
			 * the incore structures are convinced that quotas are
			 * off, but the on disk superblock doesn't know that !
			 */
			ASSERT(!(XFS_IS_QUOTA_RUNNING(mp)));
			xfs_fs_cmn_err(CE_ALERT, mp,
				"XFS mount_quotas: Superblock update failed!");
		}
	}

	if (error) {
		xfs_fs_cmn_err(CE_WARN, mp,
			"Failed to initialize disk quotas.");
		return;
	}

#ifdef QUOTADEBUG
	if (XFS_IS_QUOTA_ON(mp))
		xfs_qm_internalqcheck(mp);
#endif
}

/*
 * Called from the vfsops layer.
 */
void
xfs_qm_unmount_quotas(
	xfs_mount_t	*mp)
{
	/*
	 * Release the dquots that root inode, et al might be holding,
	 * before we flush quotas and blow away the quotainfo structure.
	 */
	ASSERT(mp->m_rootip);
	xfs_qm_dqdetach(mp->m_rootip);
	if (mp->m_rbmip)
		xfs_qm_dqdetach(mp->m_rbmip);
	if (mp->m_rsumip)
		xfs_qm_dqdetach(mp->m_rsumip);

	/*
	 * Release the quota inodes.
	 */
	if (mp->m_quotainfo) {
		if (mp->m_quotainfo->qi_uquotaip) {
			IRELE(mp->m_quotainfo->qi_uquotaip);
			mp->m_quotainfo->qi_uquotaip = NULL;
		}
		if (mp->m_quotainfo->qi_gquotaip) {
			IRELE(mp->m_quotainfo->qi_gquotaip);
			mp->m_quotainfo->qi_gquotaip = NULL;
		}
	}
}

/*
 * Flush all dquots of the given file system to disk. The dquots are
 * _not_ purged from memory here, just their data written to disk.
 */
STATIC int
xfs_qm_dqflush_all(
	struct xfs_mount	*mp,
	int			sync_mode)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	int			recl;
	struct xfs_dquot	*dqp;
	int			niters;
	int			error;

	if (!q)
		return 0;
	niters = 0;
again:
	mutex_lock(&q->qi_dqlist_lock);
	list_for_each_entry(dqp, &q->qi_dqlist, q_mplist) {
		xfs_dqlock(dqp);
		if (! XFS_DQ_IS_DIRTY(dqp)) {
			xfs_dqunlock(dqp);
			continue;
		}

		/* XXX a sentinel would be better */
		recl = q->qi_dqreclaims;
		if (!xfs_dqflock_nowait(dqp)) {
			/*
			 * If we can't grab the flush lock then check
			 * to see if the dquot has been flushed delayed
			 * write.  If so, grab its buffer and send it
			 * out immediately.  We'll be able to acquire
			 * the flush lock when the I/O completes.
			 */
			xfs_qm_dqflock_pushbuf_wait(dqp);
		}
		/*
		 * Let go of the mplist lock. We don't want to hold it
		 * across a disk write.
		 */
		mutex_unlock(&q->qi_dqlist_lock);
		error = xfs_qm_dqflush(dqp, sync_mode);
		xfs_dqunlock(dqp);
		if (error)
			return error;

		mutex_lock(&q->qi_dqlist_lock);
		if (recl != q->qi_dqreclaims) {
			mutex_unlock(&q->qi_dqlist_lock);
			/* XXX restart limit */
			goto again;
		}
	}

	mutex_unlock(&q->qi_dqlist_lock);
	/* return ! busy */
	return 0;
}
/*
 * Release the group dquot pointers the user dquots may be
 * carrying around as a hint. mplist is locked on entry and exit.
 */
STATIC void
xfs_qm_detach_gdquots(
	struct xfs_mount	*mp)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	struct xfs_dquot	*dqp, *gdqp;
	int			nrecl;

 again:
	ASSERT(mutex_is_locked(&q->qi_dqlist_lock));
	list_for_each_entry(dqp, &q->qi_dqlist, q_mplist) {
		xfs_dqlock(dqp);
		if ((gdqp = dqp->q_gdquot)) {
			xfs_dqlock(gdqp);
			dqp->q_gdquot = NULL;
		}
		xfs_dqunlock(dqp);

		if (gdqp) {
			/*
			 * Can't hold the mplist lock across a dqput.
			 * XXXmust convert to marker based iterations here.
			 */
			nrecl = q->qi_dqreclaims;
			mutex_unlock(&q->qi_dqlist_lock);
			xfs_qm_dqput(gdqp);

			mutex_lock(&q->qi_dqlist_lock);
			if (nrecl != q->qi_dqreclaims)
				goto again;
		}
	}
}

/*
 * Go through all the incore dquots of this file system and take them
 * off the mplist and hashlist, if the dquot type matches the dqtype
 * parameter. This is used when turning off quota accounting for
 * users and/or groups, as well as when the filesystem is unmounting.
 */
STATIC int
xfs_qm_dqpurge_int(
	struct xfs_mount	*mp,
	uint			flags)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	struct xfs_dquot	*dqp, *n;
	uint			dqtype;
	int			nrecl;
	int			nmisses;

	if (!q)
		return 0;

	dqtype = (flags & XFS_QMOPT_UQUOTA) ? XFS_DQ_USER : 0;
	dqtype |= (flags & XFS_QMOPT_PQUOTA) ? XFS_DQ_PROJ : 0;
	dqtype |= (flags & XFS_QMOPT_GQUOTA) ? XFS_DQ_GROUP : 0;

	mutex_lock(&q->qi_dqlist_lock);

	/*
	 * In the first pass through all incore dquots of this filesystem,
	 * we release the group dquot pointers the user dquots may be
	 * carrying around as a hint. We need to do this irrespective of
	 * what's being turned off.
	 */
	xfs_qm_detach_gdquots(mp);

      again:
	nmisses = 0;
	ASSERT(mutex_is_locked(&q->qi_dqlist_lock));
	/*
	 * Try to get rid of all of the unwanted dquots. The idea is to
	 * get them off mplist and hashlist, but leave them on freelist.
	 */
	list_for_each_entry_safe(dqp, n, &q->qi_dqlist, q_mplist) {
		/*
		 * It's OK to look at the type without taking dqlock here.
		 * We're holding the mplist lock here, and that's needed for
		 * a dqreclaim.
		 */
		if ((dqp->dq_flags & dqtype) == 0)
			continue;

		if (!mutex_trylock(&dqp->q_hash->qh_lock)) {
			nrecl = q->qi_dqreclaims;
			mutex_unlock(&q->qi_dqlist_lock);
			mutex_lock(&dqp->q_hash->qh_lock);
			mutex_lock(&q->qi_dqlist_lock);

			/*
			 * XXXTheoretically, we can get into a very long
			 * ping pong game here.
			 * No one can be adding dquots to the mplist at
			 * this point, but somebody might be taking things off.
			 */
			if (nrecl != q->qi_dqreclaims) {
				mutex_unlock(&dqp->q_hash->qh_lock);
				goto again;
			}
		}

		/*
		 * Take the dquot off the mplist and hashlist. It may remain on
		 * freelist in INACTIVE state.
		 */
		nmisses += xfs_qm_dqpurge(dqp);
	}
	mutex_unlock(&q->qi_dqlist_lock);
	return nmisses;
}

int
xfs_qm_dqpurge_all(
	xfs_mount_t	*mp,
	uint		flags)
{
	int		ndquots;

	/*
	 * Purge the dquot cache.
	 * None of the dquots should really be busy at this point.
	 */
	if (mp->m_quotainfo) {
		while ((ndquots = xfs_qm_dqpurge_int(mp, flags))) {
			delay(ndquots * 10);
		}
	}
	return 0;
}

STATIC int
xfs_qm_dqattach_one(
	xfs_inode_t	*ip,
	xfs_dqid_t	id,
	uint		type,
	uint		doalloc,
	xfs_dquot_t	*udqhint, /* hint */
	xfs_dquot_t	**IO_idqpp)
{
	xfs_dquot_t	*dqp;
	int		error;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	error = 0;

	/*
	 * See if we already have it in the inode itself. IO_idqpp is
	 * &i_udquot or &i_gdquot. This made the code look weird, but
	 * made the logic a lot simpler.
	 */
	dqp = *IO_idqpp;
	if (dqp) {
		trace_xfs_dqattach_found(dqp);
		return 0;
	}

	/*
	 * udqhint is the i_udquot field in inode, and is non-NULL only
	 * when the type arg is group/project. Its purpose is to save a
	 * lookup by dqid (xfs_qm_dqget) by caching a group dquot inside
	 * the user dquot.
	 */
	if (udqhint) {
		ASSERT(type == XFS_DQ_GROUP || type == XFS_DQ_PROJ);
		xfs_dqlock(udqhint);

		/*
		 * No need to take dqlock to look at the id.
		 *
		 * The ID can't change until it gets reclaimed, and it won't
		 * be reclaimed as long as we have a ref from inode and we
		 * hold the ilock.
		 */
		dqp = udqhint->q_gdquot;
		if (dqp && be32_to_cpu(dqp->q_core.d_id) == id) {
			xfs_dqlock(dqp);
			XFS_DQHOLD(dqp);
			ASSERT(*IO_idqpp == NULL);
			*IO_idqpp = dqp;

			xfs_dqunlock(dqp);
			xfs_dqunlock(udqhint);
			return 0;
		}

		/*
		 * We can't hold a dquot lock when we call the dqget code.
		 * We'll deadlock in no time, because of (not conforming to)
		 * lock ordering - the inodelock comes before any dquot lock,
		 * and we may drop and reacquire the ilock in xfs_qm_dqget().
		 */
		xfs_dqunlock(udqhint);
	}

	/*
	 * Find the dquot from somewhere. This bumps the
	 * reference count of dquot and returns it locked.
	 * This can return ENOENT if dquot didn't exist on
	 * disk and we didn't ask it to allocate;
	 * ESRCH if quotas got turned off suddenly.
	 */
	error = xfs_qm_dqget(ip->i_mount, ip, id, type, XFS_QMOPT_DOWARN, &dqp);
	if (error)
		return error;

	trace_xfs_dqattach_get(dqp);

	/*
	 * dqget may have dropped and re-acquired the ilock, but it guarantees
	 * that the dquot returned is the one that should go in the inode.
	 */
	*IO_idqpp = dqp;
	xfs_dqunlock(dqp);
	return 0;
}


/*
 * Given a udquot and gdquot, attach a ptr to the group dquot in the
 * udquot as a hint for future lookups. The idea sounds simple, but the
 * execution isn't, because the udquot might have a group dquot attached
 * already and getting rid of that gets us into lock ordering constraints.
 * The process is complicated more by the fact that the dquots may or may not
 * be locked on entry.
 */
STATIC void
xfs_qm_dqattach_grouphint(
	xfs_dquot_t	*udq,
	xfs_dquot_t	*gdq)
{
	xfs_dquot_t	*tmp;

	xfs_dqlock(udq);

	if ((tmp = udq->q_gdquot)) {
		if (tmp == gdq) {
			xfs_dqunlock(udq);
			return;
		}

		udq->q_gdquot = NULL;
		/*
		 * We can't keep any dqlocks when calling dqrele,
		 * because the freelist lock comes before dqlocks.
		 */
		xfs_dqunlock(udq);
		/*
		 * we took a hard reference once upon a time in dqget,
		 * so give it back when the udquot no longer points at it
		 * dqput() does the unlocking of the dquot.
		 */
		xfs_qm_dqrele(tmp);

		xfs_dqlock(udq);
		xfs_dqlock(gdq);

	} else {
		ASSERT(XFS_DQ_IS_LOCKED(udq));
		xfs_dqlock(gdq);
	}

	ASSERT(XFS_DQ_IS_LOCKED(udq));
	ASSERT(XFS_DQ_IS_LOCKED(gdq));
	/*
	 * Somebody could have attached a gdquot here,
	 * when we dropped the uqlock. If so, just do nothing.
	 */
	if (udq->q_gdquot == NULL) {
		XFS_DQHOLD(gdq);
		udq->q_gdquot = gdq;
	}

	xfs_dqunlock(gdq);
	xfs_dqunlock(udq);
}


/*
 * Given a locked inode, attach dquot(s) to it, taking U/G/P-QUOTAON
 * into account.
 * If XFS_QMOPT_DQALLOC, the dquot(s) will be allocated if needed.
 * Inode may get unlocked and relocked in here, and the caller must deal with
 * the consequences.
 */
int
xfs_qm_dqattach_locked(
	xfs_inode_t	*ip,
	uint		flags)
{
	xfs_mount_t	*mp = ip->i_mount;
	uint		nquotas = 0;
	int		error = 0;

	if (!XFS_IS_QUOTA_RUNNING(mp) ||
	    !XFS_IS_QUOTA_ON(mp) ||
	    !XFS_NOT_DQATTACHED(mp, ip) ||
	    ip->i_ino == mp->m_sb.sb_uquotino ||
	    ip->i_ino == mp->m_sb.sb_gquotino)
		return 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	if (XFS_IS_UQUOTA_ON(mp)) {
		error = xfs_qm_dqattach_one(ip, ip->i_d.di_uid, XFS_DQ_USER,
						flags & XFS_QMOPT_DQALLOC,
						NULL, &ip->i_udquot);
		if (error)
			goto done;
		nquotas++;
	}

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	if (XFS_IS_OQUOTA_ON(mp)) {
		error = XFS_IS_GQUOTA_ON(mp) ?
			xfs_qm_dqattach_one(ip, ip->i_d.di_gid, XFS_DQ_GROUP,
						flags & XFS_QMOPT_DQALLOC,
						ip->i_udquot, &ip->i_gdquot) :
			xfs_qm_dqattach_one(ip, ip->i_d.di_projid, XFS_DQ_PROJ,
						flags & XFS_QMOPT_DQALLOC,
						ip->i_udquot, &ip->i_gdquot);
		/*
		 * Don't worry about the udquot that we may have
		 * attached above. It'll get detached, if not already.
		 */
		if (error)
			goto done;
		nquotas++;
	}

	/*
	 * Attach this group quota to the user quota as a hint.
	 * This WON'T, in general, result in a thrash.
	 */
	if (nquotas == 2) {
		ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
		ASSERT(ip->i_udquot);
		ASSERT(ip->i_gdquot);

		/*
		 * We may or may not have the i_udquot locked at this point,
		 * but this check is OK since we don't depend on the i_gdquot to
		 * be accurate 100% all the time. It is just a hint, and this
		 * will succeed in general.
		 */
		if (ip->i_udquot->q_gdquot == ip->i_gdquot)
			goto done;
		/*
		 * Attach i_gdquot to the gdquot hint inside the i_udquot.
		 */
		xfs_qm_dqattach_grouphint(ip->i_udquot, ip->i_gdquot);
	}

 done:
#ifdef QUOTADEBUG
	if (! error) {
		if (XFS_IS_UQUOTA_ON(mp))
			ASSERT(ip->i_udquot);
		if (XFS_IS_OQUOTA_ON(mp))
			ASSERT(ip->i_gdquot);
	}
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
#endif
	return error;
}

int
xfs_qm_dqattach(
	struct xfs_inode	*ip,
	uint			flags)
{
	int			error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	error = xfs_qm_dqattach_locked(ip, flags);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	return error;
}

/*
 * Release dquots (and their references) if any.
 * The inode should be locked EXCL except when this's called by
 * xfs_ireclaim.
 */
void
xfs_qm_dqdetach(
	xfs_inode_t	*ip)
{
	if (!(ip->i_udquot || ip->i_gdquot))
		return;

	trace_xfs_dquot_dqdetach(ip);

	ASSERT(ip->i_ino != ip->i_mount->m_sb.sb_uquotino);
	ASSERT(ip->i_ino != ip->i_mount->m_sb.sb_gquotino);
	if (ip->i_udquot) {
		xfs_qm_dqrele(ip->i_udquot);
		ip->i_udquot = NULL;
	}
	if (ip->i_gdquot) {
		xfs_qm_dqrele(ip->i_gdquot);
		ip->i_gdquot = NULL;
	}
}

int
xfs_qm_sync(
	struct xfs_mount	*mp,
	int			flags)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	int			recl, restarts;
	struct xfs_dquot	*dqp;
	int			error;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return 0;

	restarts = 0;

  again:
	mutex_lock(&q->qi_dqlist_lock);
	/*
	 * dqpurge_all() also takes the mplist lock and iterate thru all dquots
	 * in quotaoff. However, if the QUOTA_ACTIVE bits are not cleared
	 * when we have the mplist lock, we know that dquots will be consistent
	 * as long as we have it locked.
	 */
	if (!XFS_IS_QUOTA_ON(mp)) {
		mutex_unlock(&q->qi_dqlist_lock);
		return 0;
	}
	ASSERT(mutex_is_locked(&q->qi_dqlist_lock));
	list_for_each_entry(dqp, &q->qi_dqlist, q_mplist) {
		/*
		 * If this is vfs_sync calling, then skip the dquots that
		 * don't 'seem' to be dirty. ie. don't acquire dqlock.
		 * This is very similar to what xfs_sync does with inodes.
		 */
		if (flags & SYNC_TRYLOCK) {
			if (!XFS_DQ_IS_DIRTY(dqp))
				continue;
			if (!xfs_qm_dqlock_nowait(dqp))
				continue;
		} else {
			xfs_dqlock(dqp);
		}

		/*
		 * Now, find out for sure if this dquot is dirty or not.
		 */
		if (! XFS_DQ_IS_DIRTY(dqp)) {
			xfs_dqunlock(dqp);
			continue;
		}

		/* XXX a sentinel would be better */
		recl = q->qi_dqreclaims;
		if (!xfs_dqflock_nowait(dqp)) {
			if (flags & SYNC_TRYLOCK) {
				xfs_dqunlock(dqp);
				continue;
			}
			/*
			 * If we can't grab the flush lock then if the caller
			 * really wanted us to give this our best shot, so
			 * see if we can give a push to the buffer before we wait
			 * on the flush lock. At this point, we know that
			 * even though the dquot is being flushed,
			 * it has (new) dirty data.
			 */
			xfs_qm_dqflock_pushbuf_wait(dqp);
		}
		/*
		 * Let go of the mplist lock. We don't want to hold it
		 * across a disk write
		 */
		mutex_unlock(&q->qi_dqlist_lock);
		error = xfs_qm_dqflush(dqp, flags);
		xfs_dqunlock(dqp);
		if (error && XFS_FORCED_SHUTDOWN(mp))
			return 0;	/* Need to prevent umount failure */
		else if (error)
			return error;

		mutex_lock(&q->qi_dqlist_lock);
		if (recl != q->qi_dqreclaims) {
			if (++restarts >= XFS_QM_SYNC_MAX_RESTARTS)
				break;

			mutex_unlock(&q->qi_dqlist_lock);
			goto again;
		}
	}

	mutex_unlock(&q->qi_dqlist_lock);
	return 0;
}

/*
 * The hash chains and the mplist use the same xfs_dqhash structure as
 * their list head, but we can take the mplist qh_lock and one of the
 * hash qh_locks at the same time without any problem as they aren't
 * related.
 */
static struct lock_class_key xfs_quota_mplist_class;

/*
 * This initializes all the quota information that's kept in the
 * mount structure
 */
STATIC int
xfs_qm_init_quotainfo(
	xfs_mount_t	*mp)
{
	xfs_quotainfo_t *qinf;
	int		error;
	xfs_dquot_t	*dqp;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	/*
	 * Tell XQM that we exist as soon as possible.
	 */
	if ((error = xfs_qm_hold_quotafs_ref(mp))) {
		return error;
	}

	qinf = mp->m_quotainfo = kmem_zalloc(sizeof(xfs_quotainfo_t), KM_SLEEP);

	/*
	 * See if quotainodes are setup, and if not, allocate them,
	 * and change the superblock accordingly.
	 */
	if ((error = xfs_qm_init_quotainos(mp))) {
		kmem_free(qinf);
		mp->m_quotainfo = NULL;
		return error;
	}

	INIT_LIST_HEAD(&qinf->qi_dqlist);
	mutex_init(&qinf->qi_dqlist_lock);
	lockdep_set_class(&qinf->qi_dqlist_lock, &xfs_quota_mplist_class);

	qinf->qi_dqreclaims = 0;

	/* mutex used to serialize quotaoffs */
	mutex_init(&qinf->qi_quotaofflock);

	/* Precalc some constants */
	qinf->qi_dqchunklen = XFS_FSB_TO_BB(mp, XFS_DQUOT_CLUSTER_SIZE_FSB);
	ASSERT(qinf->qi_dqchunklen);
	qinf->qi_dqperchunk = BBTOB(qinf->qi_dqchunklen);
	do_div(qinf->qi_dqperchunk, sizeof(xfs_dqblk_t));

	mp->m_qflags |= (mp->m_sb.sb_qflags & XFS_ALL_QUOTA_CHKD);

	/*
	 * We try to get the limits from the superuser's limits fields.
	 * This is quite hacky, but it is standard quota practice.
	 * We look at the USR dquot with id == 0 first, but if user quotas
	 * are not enabled we goto the GRP dquot with id == 0.
	 * We don't really care to keep separate default limits for user
	 * and group quotas, at least not at this point.
	 */
	error = xfs_qm_dqget(mp, NULL, (xfs_dqid_t)0,
			     XFS_IS_UQUOTA_RUNNING(mp) ? XFS_DQ_USER : 
			     (XFS_IS_GQUOTA_RUNNING(mp) ? XFS_DQ_GROUP :
				XFS_DQ_PROJ),
			     XFS_QMOPT_DQSUSER|XFS_QMOPT_DOWARN,
			     &dqp);
	if (! error) {
		xfs_disk_dquot_t	*ddqp = &dqp->q_core;

		/*
		 * The warnings and timers set the grace period given to
		 * a user or group before he or she can not perform any
		 * more writing. If it is zero, a default is used.
		 */
		qinf->qi_btimelimit = ddqp->d_btimer ?
			be32_to_cpu(ddqp->d_btimer) : XFS_QM_BTIMELIMIT;
		qinf->qi_itimelimit = ddqp->d_itimer ?
			be32_to_cpu(ddqp->d_itimer) : XFS_QM_ITIMELIMIT;
		qinf->qi_rtbtimelimit = ddqp->d_rtbtimer ?
			be32_to_cpu(ddqp->d_rtbtimer) : XFS_QM_RTBTIMELIMIT;
		qinf->qi_bwarnlimit = ddqp->d_bwarns ?
			be16_to_cpu(ddqp->d_bwarns) : XFS_QM_BWARNLIMIT;
		qinf->qi_iwarnlimit = ddqp->d_iwarns ?
			be16_to_cpu(ddqp->d_iwarns) : XFS_QM_IWARNLIMIT;
		qinf->qi_rtbwarnlimit = ddqp->d_rtbwarns ?
			be16_to_cpu(ddqp->d_rtbwarns) : XFS_QM_RTBWARNLIMIT;
		qinf->qi_bhardlimit = be64_to_cpu(ddqp->d_blk_hardlimit);
		qinf->qi_bsoftlimit = be64_to_cpu(ddqp->d_blk_softlimit);
		qinf->qi_ihardlimit = be64_to_cpu(ddqp->d_ino_hardlimit);
		qinf->qi_isoftlimit = be64_to_cpu(ddqp->d_ino_softlimit);
		qinf->qi_rtbhardlimit = be64_to_cpu(ddqp->d_rtb_hardlimit);
		qinf->qi_rtbsoftlimit = be64_to_cpu(ddqp->d_rtb_softlimit);
 
		/*
		 * We sent the XFS_QMOPT_DQSUSER flag to dqget because
		 * we don't want this dquot cached. We haven't done a
		 * quotacheck yet, and quotacheck doesn't like incore dquots.
		 */
		xfs_qm_dqdestroy(dqp);
	} else {
		qinf->qi_btimelimit = XFS_QM_BTIMELIMIT;
		qinf->qi_itimelimit = XFS_QM_ITIMELIMIT;
		qinf->qi_rtbtimelimit = XFS_QM_RTBTIMELIMIT;
		qinf->qi_bwarnlimit = XFS_QM_BWARNLIMIT;
		qinf->qi_iwarnlimit = XFS_QM_IWARNLIMIT;
		qinf->qi_rtbwarnlimit = XFS_QM_RTBWARNLIMIT;
	}

	return 0;
}


/*
 * Gets called when unmounting a filesystem or when all quotas get
 * turned off.
 * This purges the quota inodes, destroys locks and frees itself.
 */
void
xfs_qm_destroy_quotainfo(
	xfs_mount_t	*mp)
{
	xfs_quotainfo_t *qi;

	qi = mp->m_quotainfo;
	ASSERT(qi != NULL);
	ASSERT(xfs_Gqm != NULL);

	/*
	 * Release the reference that XQM kept, so that we know
	 * when the XQM structure should be freed. We cannot assume
	 * that xfs_Gqm is non-null after this point.
	 */
	xfs_qm_rele_quotafs_ref(mp);

	ASSERT(list_empty(&qi->qi_dqlist));
	mutex_destroy(&qi->qi_dqlist_lock);

	if (qi->qi_uquotaip) {
		IRELE(qi->qi_uquotaip);
		qi->qi_uquotaip = NULL; /* paranoia */
	}
	if (qi->qi_gquotaip) {
		IRELE(qi->qi_gquotaip);
		qi->qi_gquotaip = NULL;
	}
	mutex_destroy(&qi->qi_quotaofflock);
	kmem_free(qi);
	mp->m_quotainfo = NULL;
}



/* ------------------- PRIVATE STATIC FUNCTIONS ----------------------- */

/* ARGSUSED */
STATIC void
xfs_qm_list_init(
	xfs_dqlist_t	*list,
	char		*str,
	int		n)
{
	mutex_init(&list->qh_lock);
	INIT_LIST_HEAD(&list->qh_list);
	list->qh_version = 0;
	list->qh_nelems = 0;
}

STATIC void
xfs_qm_list_destroy(
	xfs_dqlist_t	*list)
{
	mutex_destroy(&(list->qh_lock));
}


/*
 * Stripped down version of dqattach. This doesn't attach, or even look at the
 * dquots attached to the inode. The rationale is that there won't be any
 * attached at the time this is called from quotacheck.
 */
STATIC int
xfs_qm_dqget_noattach(
	xfs_inode_t	*ip,
	xfs_dquot_t	**O_udqpp,
	xfs_dquot_t	**O_gdqpp)
{
	int		error;
	xfs_mount_t	*mp;
	xfs_dquot_t	*udqp, *gdqp;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	mp = ip->i_mount;
	udqp = NULL;
	gdqp = NULL;

	if (XFS_IS_UQUOTA_ON(mp)) {
		ASSERT(ip->i_udquot == NULL);
		/*
		 * We want the dquot allocated if it doesn't exist.
		 */
		if ((error = xfs_qm_dqget(mp, ip, ip->i_d.di_uid, XFS_DQ_USER,
					 XFS_QMOPT_DQALLOC | XFS_QMOPT_DOWARN,
					 &udqp))) {
			/*
			 * Shouldn't be able to turn off quotas here.
			 */
			ASSERT(error != ESRCH);
			ASSERT(error != ENOENT);
			return error;
		}
		ASSERT(udqp);
	}

	if (XFS_IS_OQUOTA_ON(mp)) {
		ASSERT(ip->i_gdquot == NULL);
		if (udqp)
			xfs_dqunlock(udqp);
		error = XFS_IS_GQUOTA_ON(mp) ?
				xfs_qm_dqget(mp, ip,
					     ip->i_d.di_gid, XFS_DQ_GROUP,
					     XFS_QMOPT_DQALLOC|XFS_QMOPT_DOWARN,
					     &gdqp) :
				xfs_qm_dqget(mp, ip,
					     ip->i_d.di_projid, XFS_DQ_PROJ,
					     XFS_QMOPT_DQALLOC|XFS_QMOPT_DOWARN,
					     &gdqp);
		if (error) {
			if (udqp)
				xfs_qm_dqrele(udqp);
			ASSERT(error != ESRCH);
			ASSERT(error != ENOENT);
			return error;
		}
		ASSERT(gdqp);

		/* Reacquire the locks in the right order */
		if (udqp) {
			if (! xfs_qm_dqlock_nowait(udqp)) {
				xfs_dqunlock(gdqp);
				xfs_dqlock(udqp);
				xfs_dqlock(gdqp);
			}
		}
	}

	*O_udqpp = udqp;
	*O_gdqpp = gdqp;

#ifdef QUOTADEBUG
	if (udqp) ASSERT(XFS_DQ_IS_LOCKED(udqp));
	if (gdqp) ASSERT(XFS_DQ_IS_LOCKED(gdqp));
#endif
	return 0;
}

/*
 * Create an inode and return with a reference already taken, but unlocked
 * This is how we create quota inodes
 */
STATIC int
xfs_qm_qino_alloc(
	xfs_mount_t	*mp,
	xfs_inode_t	**ip,
	__int64_t	sbfields,
	uint		flags)
{
	xfs_trans_t	*tp;
	int		error;
	int		committed;

	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_QINOCREATE);
	if ((error = xfs_trans_reserve(tp,
				      XFS_QM_QINOCREATE_SPACE_RES(mp),
				      XFS_CREATE_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_CREATE_LOG_COUNT))) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	if ((error = xfs_dir_ialloc(&tp, NULL, S_IFREG, 1, 0,
				   &xfs_zerocr, 0, 1, ip, &committed))) {
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT);
		return error;
	}

	/*
	 * Keep an extra reference to this quota inode. This inode is
	 * locked exclusively and joined to the transaction already.
	 */
	ASSERT(xfs_isilocked(*ip, XFS_ILOCK_EXCL));
	IHOLD(*ip);

	/*
	 * Make the changes in the superblock, and log those too.
	 * sbfields arg may contain fields other than *QUOTINO;
	 * VERSIONNUM for example.
	 */
	spin_lock(&mp->m_sb_lock);
	if (flags & XFS_QMOPT_SBVERSION) {
		ASSERT(!xfs_sb_version_hasquota(&mp->m_sb));
		ASSERT((sbfields & (XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO |
				   XFS_SB_GQUOTINO | XFS_SB_QFLAGS)) ==
		       (XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO |
			XFS_SB_GQUOTINO | XFS_SB_QFLAGS));

		xfs_sb_version_addquota(&mp->m_sb);
		mp->m_sb.sb_uquotino = NULLFSINO;
		mp->m_sb.sb_gquotino = NULLFSINO;

		/* qflags will get updated _after_ quotacheck */
		mp->m_sb.sb_qflags = 0;
	}
	if (flags & XFS_QMOPT_UQUOTA)
		mp->m_sb.sb_uquotino = (*ip)->i_ino;
	else
		mp->m_sb.sb_gquotino = (*ip)->i_ino;
	spin_unlock(&mp->m_sb_lock);
	xfs_mod_sb(tp, sbfields);

	if ((error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES))) {
		xfs_fs_cmn_err(CE_ALERT, mp, "XFS qino_alloc failed!");
		return error;
	}
	return 0;
}


STATIC void
xfs_qm_reset_dqcounts(
	xfs_mount_t	*mp,
	xfs_buf_t	*bp,
	xfs_dqid_t	id,
	uint		type)
{
	xfs_disk_dquot_t	*ddq;
	int			j;

	trace_xfs_reset_dqcounts(bp, _RET_IP_);

	/*
	 * Reset all counters and timers. They'll be
	 * started afresh by xfs_qm_quotacheck.
	 */
#ifdef DEBUG
	j = XFS_FSB_TO_B(mp, XFS_DQUOT_CLUSTER_SIZE_FSB);
	do_div(j, sizeof(xfs_dqblk_t));
	ASSERT(mp->m_quotainfo->qi_dqperchunk == j);
#endif
	ddq = (xfs_disk_dquot_t *)XFS_BUF_PTR(bp);
	for (j = 0; j < mp->m_quotainfo->qi_dqperchunk; j++) {
		/*
		 * Do a sanity check, and if needed, repair the dqblk. Don't
		 * output any warnings because it's perfectly possible to
		 * find uninitialised dquot blks. See comment in xfs_qm_dqcheck.
		 */
		(void) xfs_qm_dqcheck(ddq, id+j, type, XFS_QMOPT_DQREPAIR,
				      "xfs_quotacheck");
		ddq->d_bcount = 0;
		ddq->d_icount = 0;
		ddq->d_rtbcount = 0;
		ddq->d_btimer = 0;
		ddq->d_itimer = 0;
		ddq->d_rtbtimer = 0;
		ddq->d_bwarns = 0;
		ddq->d_iwarns = 0;
		ddq->d_rtbwarns = 0;
		ddq = (xfs_disk_dquot_t *) ((xfs_dqblk_t *)ddq + 1);
	}
}

STATIC int
xfs_qm_dqiter_bufs(
	xfs_mount_t	*mp,
	xfs_dqid_t	firstid,
	xfs_fsblock_t	bno,
	xfs_filblks_t	blkcnt,
	uint		flags)
{
	xfs_buf_t	*bp;
	int		error;
	int		notcommitted;
	int		incr;
	int		type;

	ASSERT(blkcnt > 0);
	notcommitted = 0;
	incr = (blkcnt > XFS_QM_MAX_DQCLUSTER_LOGSZ) ?
		XFS_QM_MAX_DQCLUSTER_LOGSZ : blkcnt;
	type = flags & XFS_QMOPT_UQUOTA ? XFS_DQ_USER :
		(flags & XFS_QMOPT_PQUOTA ? XFS_DQ_PROJ : XFS_DQ_GROUP);
	error = 0;

	/*
	 * Blkcnt arg can be a very big number, and might even be
	 * larger than the log itself. So, we have to break it up into
	 * manageable-sized transactions.
	 * Note that we don't start a permanent transaction here; we might
	 * not be able to get a log reservation for the whole thing up front,
	 * and we don't really care to either, because we just discard
	 * everything if we were to crash in the middle of this loop.
	 */
	while (blkcnt--) {
		error = xfs_trans_read_buf(mp, NULL, mp->m_ddev_targp,
			      XFS_FSB_TO_DADDR(mp, bno),
			      mp->m_quotainfo->qi_dqchunklen, 0, &bp);
		if (error)
			break;

		xfs_qm_reset_dqcounts(mp, bp, firstid, type);
		xfs_bdwrite(mp, bp);
		/*
		 * goto the next block.
		 */
		bno++;
		firstid += mp->m_quotainfo->qi_dqperchunk;
	}
	return error;
}

/*
 * Iterate over all allocated USR/GRP/PRJ dquots in the system, calling a
 * caller supplied function for every chunk of dquots that we find.
 */
STATIC int
xfs_qm_dqiterate(
	xfs_mount_t	*mp,
	xfs_inode_t	*qip,
	uint		flags)
{
	xfs_bmbt_irec_t		*map;
	int			i, nmaps;	/* number of map entries */
	int			error;		/* return value */
	xfs_fileoff_t		lblkno;
	xfs_filblks_t		maxlblkcnt;
	xfs_dqid_t		firstid;
	xfs_fsblock_t		rablkno;
	xfs_filblks_t		rablkcnt;

	error = 0;
	/*
	 * This looks racy, but we can't keep an inode lock across a
	 * trans_reserve. But, this gets called during quotacheck, and that
	 * happens only at mount time which is single threaded.
	 */
	if (qip->i_d.di_nblocks == 0)
		return 0;

	map = kmem_alloc(XFS_DQITER_MAP_SIZE * sizeof(*map), KM_SLEEP);

	lblkno = 0;
	maxlblkcnt = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_MAXIOFFSET(mp));
	do {
		nmaps = XFS_DQITER_MAP_SIZE;
		/*
		 * We aren't changing the inode itself. Just changing
		 * some of its data. No new blocks are added here, and
		 * the inode is never added to the transaction.
		 */
		xfs_ilock(qip, XFS_ILOCK_SHARED);
		error = xfs_bmapi(NULL, qip, lblkno,
				  maxlblkcnt - lblkno,
				  XFS_BMAPI_METADATA,
				  NULL,
				  0, map, &nmaps, NULL);
		xfs_iunlock(qip, XFS_ILOCK_SHARED);
		if (error)
			break;

		ASSERT(nmaps <= XFS_DQITER_MAP_SIZE);
		for (i = 0; i < nmaps; i++) {
			ASSERT(map[i].br_startblock != DELAYSTARTBLOCK);
			ASSERT(map[i].br_blockcount);


			lblkno += map[i].br_blockcount;

			if (map[i].br_startblock == HOLESTARTBLOCK)
				continue;

			firstid = (xfs_dqid_t) map[i].br_startoff *
				mp->m_quotainfo->qi_dqperchunk;
			/*
			 * Do a read-ahead on the next extent.
			 */
			if ((i+1 < nmaps) &&
			    (map[i+1].br_startblock != HOLESTARTBLOCK)) {
				rablkcnt =  map[i+1].br_blockcount;
				rablkno = map[i+1].br_startblock;
				while (rablkcnt--) {
					xfs_baread(mp->m_ddev_targp,
					       XFS_FSB_TO_DADDR(mp, rablkno),
					       mp->m_quotainfo->qi_dqchunklen);
					rablkno++;
				}
			}
			/*
			 * Iterate thru all the blks in the extent and
			 * reset the counters of all the dquots inside them.
			 */
			if ((error = xfs_qm_dqiter_bufs(mp,
						       firstid,
						       map[i].br_startblock,
						       map[i].br_blockcount,
						       flags))) {
				break;
			}
		}

		if (error)
			break;
	} while (nmaps > 0);

	kmem_free(map);

	return error;
}

/*
 * Called by dqusage_adjust in doing a quotacheck.
 * Given the inode, and a dquot (either USR or GRP, doesn't matter),
 * this updates its incore copy as well as the buffer copy. This is
 * so that once the quotacheck is done, we can just log all the buffers,
 * as opposed to logging numerous updates to individual dquots.
 */
STATIC void
xfs_qm_quotacheck_dqadjust(
	xfs_dquot_t		*dqp,
	xfs_qcnt_t		nblks,
	xfs_qcnt_t		rtblks)
{
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	trace_xfs_dqadjust(dqp);

	/*
	 * Adjust the inode count and the block count to reflect this inode's
	 * resource usage.
	 */
	be64_add_cpu(&dqp->q_core.d_icount, 1);
	dqp->q_res_icount++;
	if (nblks) {
		be64_add_cpu(&dqp->q_core.d_bcount, nblks);
		dqp->q_res_bcount += nblks;
	}
	if (rtblks) {
		be64_add_cpu(&dqp->q_core.d_rtbcount, rtblks);
		dqp->q_res_rtbcount += rtblks;
	}

	/*
	 * Set default limits, adjust timers (since we changed usages)
	 *
	 * There are no timers for the default values set in the root dquot.
	 */
	if (dqp->q_core.d_id) {
		xfs_qm_adjust_dqlimits(dqp->q_mount, &dqp->q_core);
		xfs_qm_adjust_dqtimers(dqp->q_mount, &dqp->q_core);
	}

	dqp->dq_flags |= XFS_DQ_DIRTY;
}

STATIC int
xfs_qm_get_rtblks(
	xfs_inode_t	*ip,
	xfs_qcnt_t	*O_rtblks)
{
	xfs_filblks_t	rtblks;			/* total rt blks */
	xfs_extnum_t	idx;			/* extent record index */
	xfs_ifork_t	*ifp;			/* inode fork pointer */
	xfs_extnum_t	nextents;		/* number of extent entries */
	int		error;

	ASSERT(XFS_IS_REALTIME_INODE(ip));
	ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
	if (!(ifp->if_flags & XFS_IFEXTENTS)) {
		if ((error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK)))
			return error;
	}
	rtblks = 0;
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	for (idx = 0; idx < nextents; idx++)
		rtblks += xfs_bmbt_get_blockcount(xfs_iext_get_ext(ifp, idx));
	*O_rtblks = (xfs_qcnt_t)rtblks;
	return 0;
}

/*
 * callback routine supplied to bulkstat(). Given an inumber, find its
 * dquots and update them to account for resources taken by that inode.
 */
/* ARGSUSED */
STATIC int
xfs_qm_dqusage_adjust(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		__user *buffer,	/* not used */
	int		ubsize,		/* not used */
	int		*ubused,	/* not used */
	int		*res)		/* result code value */
{
	xfs_inode_t	*ip;
	xfs_dquot_t	*udqp, *gdqp;
	xfs_qcnt_t	nblks, rtblks;
	int		error;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	/*
	 * rootino must have its resources accounted for, not so with the quota
	 * inodes.
	 */
	if (ino == mp->m_sb.sb_uquotino || ino == mp->m_sb.sb_gquotino) {
		*res = BULKSTAT_RV_NOTHING;
		return XFS_ERROR(EINVAL);
	}

	/*
	 * We don't _need_ to take the ilock EXCL. However, the xfs_qm_dqget
	 * interface expects the inode to be exclusively locked because that's
	 * the case in all other instances. It's OK that we do this because
	 * quotacheck is done only at mount time.
	 */
	if ((error = xfs_iget(mp, NULL, ino, 0, XFS_ILOCK_EXCL, &ip))) {
		*res = BULKSTAT_RV_NOTHING;
		return error;
	}

	/*
	 * Obtain the locked dquots. In case of an error (eg. allocation
	 * fails for ENOSPC), we return the negative of the error number
	 * to bulkstat, so that it can get propagated to quotacheck() and
	 * making us disable quotas for the file system.
	 */
	if ((error = xfs_qm_dqget_noattach(ip, &udqp, &gdqp))) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		IRELE(ip);
		*res = BULKSTAT_RV_GIVEUP;
		return error;
	}

	rtblks = 0;
	if (! XFS_IS_REALTIME_INODE(ip)) {
		nblks = (xfs_qcnt_t)ip->i_d.di_nblocks;
	} else {
		/*
		 * Walk thru the extent list and count the realtime blocks.
		 */
		if ((error = xfs_qm_get_rtblks(ip, &rtblks))) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			IRELE(ip);
			if (udqp)
				xfs_qm_dqput(udqp);
			if (gdqp)
				xfs_qm_dqput(gdqp);
			*res = BULKSTAT_RV_GIVEUP;
			return error;
		}
		nblks = (xfs_qcnt_t)ip->i_d.di_nblocks - rtblks;
	}
	ASSERT(ip->i_delayed_blks == 0);

	/*
	 * We can't release the inode while holding its dquot locks.
	 * The inode can go into inactive and might try to acquire the dquotlocks.
	 * So, just unlock here and do a vn_rele at the end.
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	/*
	 * Add the (disk blocks and inode) resources occupied by this
	 * inode to its dquots. We do this adjustment in the incore dquot,
	 * and also copy the changes to its buffer.
	 * We don't care about putting these changes in a transaction
	 * envelope because if we crash in the middle of a 'quotacheck'
	 * we have to start from the beginning anyway.
	 * Once we're done, we'll log all the dquot bufs.
	 *
	 * The *QUOTA_ON checks below may look pretty racy, but quotachecks
	 * and quotaoffs don't race. (Quotachecks happen at mount time only).
	 */
	if (XFS_IS_UQUOTA_ON(mp)) {
		ASSERT(udqp);
		xfs_qm_quotacheck_dqadjust(udqp, nblks, rtblks);
		xfs_qm_dqput(udqp);
	}
	if (XFS_IS_OQUOTA_ON(mp)) {
		ASSERT(gdqp);
		xfs_qm_quotacheck_dqadjust(gdqp, nblks, rtblks);
		xfs_qm_dqput(gdqp);
	}
	/*
	 * Now release the inode. This will send it to 'inactive', and
	 * possibly even free blocks.
	 */
	IRELE(ip);

	/*
	 * Goto next inode.
	 */
	*res = BULKSTAT_RV_DIDONE;
	return 0;
}

/*
 * Walk thru all the filesystem inodes and construct a consistent view
 * of the disk quota world. If the quotacheck fails, disable quotas.
 */
int
xfs_qm_quotacheck(
	xfs_mount_t	*mp)
{
	int		done, count, error;
	xfs_ino_t	lastino;
	size_t		structsz;
	xfs_inode_t	*uip, *gip;
	uint		flags;

	count = INT_MAX;
	structsz = 1;
	lastino = 0;
	flags = 0;

	ASSERT(mp->m_quotainfo->qi_uquotaip || mp->m_quotainfo->qi_gquotaip);
	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	/*
	 * There should be no cached dquots. The (simplistic) quotacheck
	 * algorithm doesn't like that.
	 */
	ASSERT(list_empty(&mp->m_quotainfo->qi_dqlist));

	cmn_err(CE_NOTE, "XFS quotacheck %s: Please wait.", mp->m_fsname);

	/*
	 * First we go thru all the dquots on disk, USR and GRP/PRJ, and reset
	 * their counters to zero. We need a clean slate.
	 * We don't log our changes till later.
	 */
	uip = mp->m_quotainfo->qi_uquotaip;
	if (uip) {
		error = xfs_qm_dqiterate(mp, uip, XFS_QMOPT_UQUOTA);
		if (error)
			goto error_return;
		flags |= XFS_UQUOTA_CHKD;
	}

	gip = mp->m_quotainfo->qi_gquotaip;
	if (gip) {
		error = xfs_qm_dqiterate(mp, gip, XFS_IS_GQUOTA_ON(mp) ?
					XFS_QMOPT_GQUOTA : XFS_QMOPT_PQUOTA);
		if (error)
			goto error_return;
		flags |= XFS_OQUOTA_CHKD;
	}

	do {
		/*
		 * Iterate thru all the inodes in the file system,
		 * adjusting the corresponding dquot counters in core.
		 */
		error = xfs_bulkstat(mp, &lastino, &count,
				     xfs_qm_dqusage_adjust,
				     structsz, NULL, &done);
		if (error)
			break;

	} while (!done);

	/*
	 * We've made all the changes that we need to make incore.
	 * Flush them down to disk buffers if everything was updated
	 * successfully.
	 */
	if (!error)
		error = xfs_qm_dqflush_all(mp, 0);

	/*
	 * We can get this error if we couldn't do a dquot allocation inside
	 * xfs_qm_dqusage_adjust (via bulkstat). We don't care about the
	 * dirty dquots that might be cached, we just want to get rid of them
	 * and turn quotaoff. The dquots won't be attached to any of the inodes
	 * at this point (because we intentionally didn't in dqget_noattach).
	 */
	if (error) {
		xfs_qm_dqpurge_all(mp, XFS_QMOPT_QUOTALL);
		goto error_return;
	}

	/*
	 * We didn't log anything, because if we crashed, we'll have to
	 * start the quotacheck from scratch anyway. However, we must make
	 * sure that our dquot changes are secure before we put the
	 * quotacheck'd stamp on the superblock. So, here we do a synchronous
	 * flush.
	 */
	XFS_bflush(mp->m_ddev_targp);

	/*
	 * If one type of quotas is off, then it will lose its
	 * quotachecked status, since we won't be doing accounting for
	 * that type anymore.
	 */
	mp->m_qflags &= ~(XFS_OQUOTA_CHKD | XFS_UQUOTA_CHKD);
	mp->m_qflags |= flags;

	xfs_qm_dquot_list_print(mp);

 error_return:
	if (error) {
		cmn_err(CE_WARN, "XFS quotacheck %s: Unsuccessful (Error %d): "
			"Disabling quotas.",
			mp->m_fsname, error);
		/*
		 * We must turn off quotas.
		 */
		ASSERT(mp->m_quotainfo != NULL);
		ASSERT(xfs_Gqm != NULL);
		xfs_qm_destroy_quotainfo(mp);
		if (xfs_mount_reset_sbqflags(mp)) {
			cmn_err(CE_WARN, "XFS quotacheck %s: "
				"Failed to reset quota flags.", mp->m_fsname);
		}
	} else {
		cmn_err(CE_NOTE, "XFS quotacheck %s: Done.", mp->m_fsname);
	}
	return (error);
}

/*
 * This is called after the superblock has been read in and we're ready to
 * iget the quota inodes.
 */
STATIC int
xfs_qm_init_quotainos(
	xfs_mount_t	*mp)
{
	xfs_inode_t	*uip, *gip;
	int		error;
	__int64_t	sbflags;
	uint		flags;

	ASSERT(mp->m_quotainfo);
	uip = gip = NULL;
	sbflags = 0;
	flags = 0;

	/*
	 * Get the uquota and gquota inodes
	 */
	if (xfs_sb_version_hasquota(&mp->m_sb)) {
		if (XFS_IS_UQUOTA_ON(mp) &&
		    mp->m_sb.sb_uquotino != NULLFSINO) {
			ASSERT(mp->m_sb.sb_uquotino > 0);
			if ((error = xfs_iget(mp, NULL, mp->m_sb.sb_uquotino,
					     0, 0, &uip)))
				return XFS_ERROR(error);
		}
		if (XFS_IS_OQUOTA_ON(mp) &&
		    mp->m_sb.sb_gquotino != NULLFSINO) {
			ASSERT(mp->m_sb.sb_gquotino > 0);
			if ((error = xfs_iget(mp, NULL, mp->m_sb.sb_gquotino,
					     0, 0, &gip))) {
				if (uip)
					IRELE(uip);
				return XFS_ERROR(error);
			}
		}
	} else {
		flags |= XFS_QMOPT_SBVERSION;
		sbflags |= (XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO |
			    XFS_SB_GQUOTINO | XFS_SB_QFLAGS);
	}

	/*
	 * Create the two inodes, if they don't exist already. The changes
	 * made above will get added to a transaction and logged in one of
	 * the qino_alloc calls below.  If the device is readonly,
	 * temporarily switch to read-write to do this.
	 */
	if (XFS_IS_UQUOTA_ON(mp) && uip == NULL) {
		if ((error = xfs_qm_qino_alloc(mp, &uip,
					      sbflags | XFS_SB_UQUOTINO,
					      flags | XFS_QMOPT_UQUOTA)))
			return XFS_ERROR(error);

		flags &= ~XFS_QMOPT_SBVERSION;
	}
	if (XFS_IS_OQUOTA_ON(mp) && gip == NULL) {
		flags |= (XFS_IS_GQUOTA_ON(mp) ?
				XFS_QMOPT_GQUOTA : XFS_QMOPT_PQUOTA);
		error = xfs_qm_qino_alloc(mp, &gip,
					  sbflags | XFS_SB_GQUOTINO, flags);
		if (error) {
			if (uip)
				IRELE(uip);

			return XFS_ERROR(error);
		}
	}

	mp->m_quotainfo->qi_uquotaip = uip;
	mp->m_quotainfo->qi_gquotaip = gip;

	return 0;
}



/*
 * Just pop the least recently used dquot off the freelist and
 * recycle it. The returned dquot is locked.
 */
STATIC xfs_dquot_t *
xfs_qm_dqreclaim_one(void)
{
	xfs_dquot_t	*dqpout;
	xfs_dquot_t	*dqp;
	int		restarts;

	restarts = 0;
	dqpout = NULL;

	/* lockorder: hashchainlock, freelistlock, mplistlock, dqlock, dqflock */
startagain:
	mutex_lock(&xfs_Gqm->qm_dqfrlist_lock);

	list_for_each_entry(dqp, &xfs_Gqm->qm_dqfrlist, q_freelist) {
		struct xfs_mount *mp = dqp->q_mount;
		xfs_dqlock(dqp);

		/*
		 * We are racing with dqlookup here. Naturally we don't
		 * want to reclaim a dquot that lookup wants. We release the
		 * freelist lock and start over, so that lookup will grab
		 * both the dquot and the freelistlock.
		 */
		if (dqp->dq_flags & XFS_DQ_WANT) {
			ASSERT(! (dqp->dq_flags & XFS_DQ_INACTIVE));

			trace_xfs_dqreclaim_want(dqp);

			xfs_dqunlock(dqp);
			mutex_unlock(&xfs_Gqm->qm_dqfrlist_lock);
			if (++restarts >= XFS_QM_RECLAIM_MAX_RESTARTS)
				return NULL;
			XQM_STATS_INC(xqmstats.xs_qm_dqwants);
			goto startagain;
		}

		/*
		 * If the dquot is inactive, we are assured that it is
		 * not on the mplist or the hashlist, and that makes our
		 * life easier.
		 */
		if (dqp->dq_flags & XFS_DQ_INACTIVE) {
			ASSERT(mp == NULL);
			ASSERT(! XFS_DQ_IS_DIRTY(dqp));
			ASSERT(list_empty(&dqp->q_hashlist));
			ASSERT(list_empty(&dqp->q_mplist));
			list_del_init(&dqp->q_freelist);
			xfs_Gqm->qm_dqfrlist_cnt--;
			xfs_dqunlock(dqp);
			dqpout = dqp;
			XQM_STATS_INC(xqmstats.xs_qm_dqinact_reclaims);
			break;
		}

		ASSERT(dqp->q_hash);
		ASSERT(!list_empty(&dqp->q_mplist));

		/*
		 * Try to grab the flush lock. If this dquot is in the process of
		 * getting flushed to disk, we don't want to reclaim it.
		 */
		if (!xfs_dqflock_nowait(dqp)) {
			xfs_dqunlock(dqp);
			continue;
		}

		/*
		 * We have the flush lock so we know that this is not in the
		 * process of being flushed. So, if this is dirty, flush it
		 * DELWRI so that we don't get a freelist infested with
		 * dirty dquots.
		 */
		if (XFS_DQ_IS_DIRTY(dqp)) {
			int	error;

			trace_xfs_dqreclaim_dirty(dqp);

			/*
			 * We flush it delayed write, so don't bother
			 * releasing the freelist lock.
			 */
			error = xfs_qm_dqflush(dqp, 0);
			if (error) {
				xfs_fs_cmn_err(CE_WARN, mp,
			"xfs_qm_dqreclaim: dquot %p flush failed", dqp);
			}
			xfs_dqunlock(dqp); /* dqflush unlocks dqflock */
			continue;
		}

		/*
		 * We're trying to get the hashlock out of order. This races
		 * with dqlookup; so, we giveup and goto the next dquot if
		 * we couldn't get the hashlock. This way, we won't starve
		 * a dqlookup process that holds the hashlock that is
		 * waiting for the freelist lock.
		 */
		if (!mutex_trylock(&dqp->q_hash->qh_lock)) {
			restarts++;
			goto dqfunlock;
		}

		/*
		 * This races with dquot allocation code as well as dqflush_all
		 * and reclaim code. So, if we failed to grab the mplist lock,
		 * giveup everything and start over.
		 */
		if (!mutex_trylock(&mp->m_quotainfo->qi_dqlist_lock)) {
			restarts++;
			mutex_unlock(&dqp->q_hash->qh_lock);
			xfs_dqfunlock(dqp);
			xfs_dqunlock(dqp);
			mutex_unlock(&xfs_Gqm->qm_dqfrlist_lock);
			if (restarts++ >= XFS_QM_RECLAIM_MAX_RESTARTS)
				return NULL;
			goto startagain;
		}

		ASSERT(dqp->q_nrefs == 0);
		list_del_init(&dqp->q_mplist);
		mp->m_quotainfo->qi_dquots--;
		mp->m_quotainfo->qi_dqreclaims++;
		list_del_init(&dqp->q_hashlist);
		dqp->q_hash->qh_version++;
		list_del_init(&dqp->q_freelist);
		xfs_Gqm->qm_dqfrlist_cnt--;
		dqpout = dqp;
		mutex_unlock(&mp->m_quotainfo->qi_dqlist_lock);
		mutex_unlock(&dqp->q_hash->qh_lock);
dqfunlock:
		xfs_dqfunlock(dqp);
		xfs_dqunlock(dqp);
		if (dqpout)
			break;
		if (restarts >= XFS_QM_RECLAIM_MAX_RESTARTS)
			return NULL;
	}
	mutex_unlock(&xfs_Gqm->qm_dqfrlist_lock);
	return dqpout;
}

/*
 * Traverse the freelist of dquots and attempt to reclaim a maximum of
 * 'howmany' dquots. This operation races with dqlookup(), and attempts to
 * favor the lookup function ...
 */
STATIC int
xfs_qm_shake_freelist(
	int	howmany)
{
	int		nreclaimed = 0;
	xfs_dquot_t	*dqp;

	if (howmany <= 0)
		return 0;

	while (nreclaimed < howmany) {
		dqp = xfs_qm_dqreclaim_one();
		if (!dqp)
			return nreclaimed;
		xfs_qm_dqdestroy(dqp);
		nreclaimed++;
	}
	return nreclaimed;
}

/*
 * The kmem_shake interface is invoked when memory is running low.
 */
/* ARGSUSED */
STATIC int
xfs_qm_shake(
	struct shrinker	*shrink,
	int		nr_to_scan,
	gfp_t		gfp_mask)
{
	int	ndqused, nfree, n;

	if (!kmem_shake_allow(gfp_mask))
		return 0;
	if (!xfs_Gqm)
		return 0;

	nfree = xfs_Gqm->qm_dqfrlist_cnt; /* free dquots */
	/* incore dquots in all f/s's */
	ndqused = atomic_read(&xfs_Gqm->qm_totaldquots) - nfree;

	ASSERT(ndqused >= 0);

	if (nfree <= ndqused && nfree < ndquot)
		return 0;

	ndqused *= xfs_Gqm->qm_dqfree_ratio;	/* target # of free dquots */
	n = nfree - ndqused - ndquot;		/* # over target */

	return xfs_qm_shake_freelist(MAX(nfree, n));
}


/*------------------------------------------------------------------*/

/*
 * Return a new incore dquot. Depending on the number of
 * dquots in the system, we either allocate a new one on the kernel heap,
 * or reclaim a free one.
 * Return value is B_TRUE if we allocated a new dquot, B_FALSE if we managed
 * to reclaim an existing one from the freelist.
 */
boolean_t
xfs_qm_dqalloc_incore(
	xfs_dquot_t **O_dqpp)
{
	xfs_dquot_t	*dqp;

	/*
	 * Check against high water mark to see if we want to pop
	 * a nincompoop dquot off the freelist.
	 */
	if (atomic_read(&xfs_Gqm->qm_totaldquots) >= ndquot) {
		/*
		 * Try to recycle a dquot from the freelist.
		 */
		if ((dqp = xfs_qm_dqreclaim_one())) {
			XQM_STATS_INC(xqmstats.xs_qm_dqreclaims);
			/*
			 * Just zero the core here. The rest will get
			 * reinitialized by caller. XXX we shouldn't even
			 * do this zero ...
			 */
			memset(&dqp->q_core, 0, sizeof(dqp->q_core));
			*O_dqpp = dqp;
			return B_FALSE;
		}
		XQM_STATS_INC(xqmstats.xs_qm_dqreclaim_misses);
	}

	/*
	 * Allocate a brand new dquot on the kernel heap and return it
	 * to the caller to initialize.
	 */
	ASSERT(xfs_Gqm->qm_dqzone != NULL);
	*O_dqpp = kmem_zone_zalloc(xfs_Gqm->qm_dqzone, KM_SLEEP);
	atomic_inc(&xfs_Gqm->qm_totaldquots);

	return B_TRUE;
}


/*
 * Start a transaction and write the incore superblock changes to
 * disk. flags parameter indicates which fields have changed.
 */
int
xfs_qm_write_sb_changes(
	xfs_mount_t	*mp,
	__int64_t	flags)
{
	xfs_trans_t	*tp;
	int		error;

#ifdef QUOTADEBUG
	cmn_err(CE_NOTE, "Writing superblock quota changes :%s", mp->m_fsname);
#endif
	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_SBCHANGE);
	if ((error = xfs_trans_reserve(tp, 0,
				      mp->m_sb.sb_sectsize + 128, 0,
				      0,
				      XFS_DEFAULT_LOG_COUNT))) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	xfs_mod_sb(tp, flags);
	error = xfs_trans_commit(tp, 0);

	return error;
}


/* --------------- utility functions for vnodeops ---------------- */


/*
 * Given an inode, a uid and gid (from cred_t) make sure that we have
 * allocated relevant dquot(s) on disk, and that we won't exceed inode
 * quotas by creating this file.
 * This also attaches dquot(s) to the given inode after locking it,
 * and returns the dquots corresponding to the uid and/or gid.
 *
 * in	: inode (unlocked)
 * out	: udquot, gdquot with references taken and unlocked
 */
int
xfs_qm_vop_dqalloc(
	struct xfs_inode	*ip,
	uid_t			uid,
	gid_t			gid,
	prid_t			prid,
	uint			flags,
	struct xfs_dquot	**O_udqpp,
	struct xfs_dquot	**O_gdqpp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_dquot	*uq, *gq;
	int			error;
	uint			lockflags;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return 0;

	lockflags = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lockflags);

	if ((flags & XFS_QMOPT_INHERIT) && XFS_INHERIT_GID(ip))
		gid = ip->i_d.di_gid;

	/*
	 * Attach the dquot(s) to this inode, doing a dquot allocation
	 * if necessary. The dquot(s) will not be locked.
	 */
	if (XFS_NOT_DQATTACHED(mp, ip)) {
		error = xfs_qm_dqattach_locked(ip, XFS_QMOPT_DQALLOC);
		if (error) {
			xfs_iunlock(ip, lockflags);
			return error;
		}
	}

	uq = gq = NULL;
	if ((flags & XFS_QMOPT_UQUOTA) && XFS_IS_UQUOTA_ON(mp)) {
		if (ip->i_d.di_uid != uid) {
			/*
			 * What we need is the dquot that has this uid, and
			 * if we send the inode to dqget, the uid of the inode
			 * takes priority over what's sent in the uid argument.
			 * We must unlock inode here before calling dqget if
			 * we're not sending the inode, because otherwise
			 * we'll deadlock by doing trans_reserve while
			 * holding ilock.
			 */
			xfs_iunlock(ip, lockflags);
			if ((error = xfs_qm_dqget(mp, NULL, (xfs_dqid_t) uid,
						 XFS_DQ_USER,
						 XFS_QMOPT_DQALLOC |
						 XFS_QMOPT_DOWARN,
						 &uq))) {
				ASSERT(error != ENOENT);
				return error;
			}
			/*
			 * Get the ilock in the right order.
			 */
			xfs_dqunlock(uq);
			lockflags = XFS_ILOCK_SHARED;
			xfs_ilock(ip, lockflags);
		} else {
			/*
			 * Take an extra reference, because we'll return
			 * this to caller
			 */
			ASSERT(ip->i_udquot);
			uq = ip->i_udquot;
			xfs_dqlock(uq);
			XFS_DQHOLD(uq);
			xfs_dqunlock(uq);
		}
	}
	if ((flags & XFS_QMOPT_GQUOTA) && XFS_IS_GQUOTA_ON(mp)) {
		if (ip->i_d.di_gid != gid) {
			xfs_iunlock(ip, lockflags);
			if ((error = xfs_qm_dqget(mp, NULL, (xfs_dqid_t)gid,
						 XFS_DQ_GROUP,
						 XFS_QMOPT_DQALLOC |
						 XFS_QMOPT_DOWARN,
						 &gq))) {
				if (uq)
					xfs_qm_dqrele(uq);
				ASSERT(error != ENOENT);
				return error;
			}
			xfs_dqunlock(gq);
			lockflags = XFS_ILOCK_SHARED;
			xfs_ilock(ip, lockflags);
		} else {
			ASSERT(ip->i_gdquot);
			gq = ip->i_gdquot;
			xfs_dqlock(gq);
			XFS_DQHOLD(gq);
			xfs_dqunlock(gq);
		}
	} else if ((flags & XFS_QMOPT_PQUOTA) && XFS_IS_PQUOTA_ON(mp)) {
		if (ip->i_d.di_projid != prid) {
			xfs_iunlock(ip, lockflags);
			if ((error = xfs_qm_dqget(mp, NULL, (xfs_dqid_t)prid,
						 XFS_DQ_PROJ,
						 XFS_QMOPT_DQALLOC |
						 XFS_QMOPT_DOWARN,
						 &gq))) {
				if (uq)
					xfs_qm_dqrele(uq);
				ASSERT(error != ENOENT);
				return (error);
			}
			xfs_dqunlock(gq);
			lockflags = XFS_ILOCK_SHARED;
			xfs_ilock(ip, lockflags);
		} else {
			ASSERT(ip->i_gdquot);
			gq = ip->i_gdquot;
			xfs_dqlock(gq);
			XFS_DQHOLD(gq);
			xfs_dqunlock(gq);
		}
	}
	if (uq)
		trace_xfs_dquot_dqalloc(ip);

	xfs_iunlock(ip, lockflags);
	if (O_udqpp)
		*O_udqpp = uq;
	else if (uq)
		xfs_qm_dqrele(uq);
	if (O_gdqpp)
		*O_gdqpp = gq;
	else if (gq)
		xfs_qm_dqrele(gq);
	return 0;
}

/*
 * Actually transfer ownership, and do dquot modifications.
 * These were already reserved.
 */
xfs_dquot_t *
xfs_qm_vop_chown(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_dquot_t	**IO_olddq,
	xfs_dquot_t	*newdq)
{
	xfs_dquot_t	*prevdq;
	uint		bfield = XFS_IS_REALTIME_INODE(ip) ?
				 XFS_TRANS_DQ_RTBCOUNT : XFS_TRANS_DQ_BCOUNT;


	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(XFS_IS_QUOTA_RUNNING(ip->i_mount));

	/* old dquot */
	prevdq = *IO_olddq;
	ASSERT(prevdq);
	ASSERT(prevdq != newdq);

	xfs_trans_mod_dquot(tp, prevdq, bfield, -(ip->i_d.di_nblocks));
	xfs_trans_mod_dquot(tp, prevdq, XFS_TRANS_DQ_ICOUNT, -1);

	/* the sparkling new dquot */
	xfs_trans_mod_dquot(tp, newdq, bfield, ip->i_d.di_nblocks);
	xfs_trans_mod_dquot(tp, newdq, XFS_TRANS_DQ_ICOUNT, 1);

	/*
	 * Take an extra reference, because the inode
	 * is going to keep this dquot pointer even
	 * after the trans_commit.
	 */
	xfs_dqlock(newdq);
	XFS_DQHOLD(newdq);
	xfs_dqunlock(newdq);
	*IO_olddq = newdq;

	return prevdq;
}

/*
 * Quota reservations for setattr(AT_UID|AT_GID|AT_PROJID).
 */
int
xfs_qm_vop_chown_reserve(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_dquot_t	*udqp,
	xfs_dquot_t	*gdqp,
	uint		flags)
{
	xfs_mount_t	*mp = ip->i_mount;
	uint		delblks, blkflags, prjflags = 0;
	xfs_dquot_t	*unresudq, *unresgdq, *delblksudq, *delblksgdq;
	int		error;


	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	delblks = ip->i_delayed_blks;
	delblksudq = delblksgdq = unresudq = unresgdq = NULL;
	blkflags = XFS_IS_REALTIME_INODE(ip) ?
			XFS_QMOPT_RES_RTBLKS : XFS_QMOPT_RES_REGBLKS;

	if (XFS_IS_UQUOTA_ON(mp) && udqp &&
	    ip->i_d.di_uid != (uid_t)be32_to_cpu(udqp->q_core.d_id)) {
		delblksudq = udqp;
		/*
		 * If there are delayed allocation blocks, then we have to
		 * unreserve those from the old dquot, and add them to the
		 * new dquot.
		 */
		if (delblks) {
			ASSERT(ip->i_udquot);
			unresudq = ip->i_udquot;
		}
	}
	if (XFS_IS_OQUOTA_ON(ip->i_mount) && gdqp) {
		if (XFS_IS_PQUOTA_ON(ip->i_mount) &&
		     ip->i_d.di_projid != be32_to_cpu(gdqp->q_core.d_id))
			prjflags = XFS_QMOPT_ENOSPC;

		if (prjflags ||
		    (XFS_IS_GQUOTA_ON(ip->i_mount) &&
		     ip->i_d.di_gid != be32_to_cpu(gdqp->q_core.d_id))) {
			delblksgdq = gdqp;
			if (delblks) {
				ASSERT(ip->i_gdquot);
				unresgdq = ip->i_gdquot;
			}
		}
	}

	if ((error = xfs_trans_reserve_quota_bydquots(tp, ip->i_mount,
				delblksudq, delblksgdq, ip->i_d.di_nblocks, 1,
				flags | blkflags | prjflags)))
		return (error);

	/*
	 * Do the delayed blks reservations/unreservations now. Since, these
	 * are done without the help of a transaction, if a reservation fails
	 * its previous reservations won't be automatically undone by trans
	 * code. So, we have to do it manually here.
	 */
	if (delblks) {
		/*
		 * Do the reservations first. Unreservation can't fail.
		 */
		ASSERT(delblksudq || delblksgdq);
		ASSERT(unresudq || unresgdq);
		if ((error = xfs_trans_reserve_quota_bydquots(NULL, ip->i_mount,
				delblksudq, delblksgdq, (xfs_qcnt_t)delblks, 0,
				flags | blkflags | prjflags)))
			return (error);
		xfs_trans_reserve_quota_bydquots(NULL, ip->i_mount,
				unresudq, unresgdq, -((xfs_qcnt_t)delblks), 0,
				blkflags);
	}

	return (0);
}

int
xfs_qm_vop_rename_dqattach(
	struct xfs_inode	**i_tab)
{
	struct xfs_mount	*mp = i_tab[0]->i_mount;
	int			i;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return 0;

	for (i = 0; (i < 4 && i_tab[i]); i++) {
		struct xfs_inode	*ip = i_tab[i];
		int			error;

		/*
		 * Watch out for duplicate entries in the table.
		 */
		if (i == 0 || ip != i_tab[i-1]) {
			if (XFS_NOT_DQATTACHED(mp, ip)) {
				error = xfs_qm_dqattach(ip, 0);
				if (error)
					return error;
			}
		}
	}
	return 0;
}

void
xfs_qm_vop_create_dqattach(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct xfs_dquot	*udqp,
	struct xfs_dquot	*gdqp)
{
	struct xfs_mount	*mp = tp->t_mountp;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	if (udqp) {
		xfs_dqlock(udqp);
		XFS_DQHOLD(udqp);
		xfs_dqunlock(udqp);
		ASSERT(ip->i_udquot == NULL);
		ip->i_udquot = udqp;
		ASSERT(XFS_IS_UQUOTA_ON(mp));
		ASSERT(ip->i_d.di_uid == be32_to_cpu(udqp->q_core.d_id));
		xfs_trans_mod_dquot(tp, udqp, XFS_TRANS_DQ_ICOUNT, 1);
	}
	if (gdqp) {
		xfs_dqlock(gdqp);
		XFS_DQHOLD(gdqp);
		xfs_dqunlock(gdqp);
		ASSERT(ip->i_gdquot == NULL);
		ip->i_gdquot = gdqp;
		ASSERT(XFS_IS_OQUOTA_ON(mp));
		ASSERT((XFS_IS_GQUOTA_ON(mp) ?
			ip->i_d.di_gid : ip->i_d.di_projid) ==
				be32_to_cpu(gdqp->q_core.d_id));
		xfs_trans_mod_dquot(tp, gdqp, XFS_TRANS_DQ_ICOUNT, 1);
	}
}

