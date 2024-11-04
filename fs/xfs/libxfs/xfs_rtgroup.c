// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_rmap_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_health.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_defer.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_trace.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_rtgroup.h"
#include "xfs_rtbitmap.h"
#include "xfs_metafile.h"
#include "xfs_metadir.h"

int
xfs_rtgroup_alloc(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno,
	xfs_rgnumber_t		rgcount,
	xfs_rtbxlen_t		rextents)
{
	struct xfs_rtgroup	*rtg;
	int			error;

	rtg = kzalloc(sizeof(struct xfs_rtgroup), GFP_KERNEL);
	if (!rtg)
		return -ENOMEM;

	error = xfs_group_insert(mp, rtg_group(rtg), rgno, XG_TYPE_RTG);
	if (error)
		goto out_free_rtg;
	return 0;

out_free_rtg:
	kfree(rtg);
	return error;
}

void
xfs_rtgroup_free(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno)
{
	xfs_group_free(mp, rgno, XG_TYPE_RTG, NULL);
}

/* Free a range of incore rtgroup objects. */
void
xfs_free_rtgroups(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		first_rgno,
	xfs_rgnumber_t		end_rgno)
{
	xfs_rgnumber_t		rgno;

	for (rgno = first_rgno; rgno < end_rgno; rgno++)
		xfs_rtgroup_free(mp, rgno);
}

/* Initialize some range of incore rtgroup objects. */
int
xfs_initialize_rtgroups(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		first_rgno,
	xfs_rgnumber_t		end_rgno,
	xfs_rtbxlen_t		rextents)
{
	xfs_rgnumber_t		index;
	int			error;

	if (first_rgno >= end_rgno)
		return 0;

	for (index = first_rgno; index < end_rgno; index++) {
		error = xfs_rtgroup_alloc(mp, index, end_rgno, rextents);
		if (error)
			goto out_unwind_new_rtgs;
	}

	return 0;

out_unwind_new_rtgs:
	xfs_free_rtgroups(mp, first_rgno, index);
	return error;
}

/* Compute the number of rt extents in this realtime group. */
xfs_rtxnum_t
__xfs_rtgroup_extents(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno,
	xfs_rgnumber_t		rgcount,
	xfs_rtbxlen_t		rextents)
{
	ASSERT(rgno < rgcount);
	if (rgno == rgcount - 1)
		return rextents - ((xfs_rtxnum_t)rgno * mp->m_sb.sb_rgextents);

	ASSERT(xfs_has_rtgroups(mp));
	return mp->m_sb.sb_rgextents;
}

xfs_rtxnum_t
xfs_rtgroup_extents(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno)
{
	return __xfs_rtgroup_extents(mp, rgno, mp->m_sb.sb_rgcount,
			mp->m_sb.sb_rextents);
}

/*
 * Update the rt extent count of the previous tail rtgroup if it changed during
 * recovery (i.e. recovery of a growfs).
 */
int
xfs_update_last_rtgroup_size(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		prev_rgcount)
{
	struct xfs_rtgroup	*rtg;

	ASSERT(prev_rgcount > 0);

	rtg = xfs_rtgroup_grab(mp, prev_rgcount - 1);
	if (!rtg)
		return -EFSCORRUPTED;
	rtg->rtg_extents = __xfs_rtgroup_extents(mp, prev_rgcount - 1,
			mp->m_sb.sb_rgcount, mp->m_sb.sb_rextents);
	xfs_rtgroup_rele(rtg);
	return 0;
}

/* Lock metadata inodes associated with this rt group. */
void
xfs_rtgroup_lock(
	struct xfs_rtgroup	*rtg,
	unsigned int		rtglock_flags)
{
	ASSERT(!(rtglock_flags & ~XFS_RTGLOCK_ALL_FLAGS));
	ASSERT(!(rtglock_flags & XFS_RTGLOCK_BITMAP_SHARED) ||
	       !(rtglock_flags & XFS_RTGLOCK_BITMAP));

	if (rtglock_flags & XFS_RTGLOCK_BITMAP) {
		/*
		 * Lock both realtime free space metadata inodes for a freespace
		 * update.
		 */
		xfs_ilock(rtg->rtg_inodes[XFS_RTGI_BITMAP], XFS_ILOCK_EXCL);
		xfs_ilock(rtg->rtg_inodes[XFS_RTGI_SUMMARY], XFS_ILOCK_EXCL);
	} else if (rtglock_flags & XFS_RTGLOCK_BITMAP_SHARED) {
		xfs_ilock(rtg->rtg_inodes[XFS_RTGI_BITMAP], XFS_ILOCK_SHARED);
	}
}

/* Unlock metadata inodes associated with this rt group. */
void
xfs_rtgroup_unlock(
	struct xfs_rtgroup	*rtg,
	unsigned int		rtglock_flags)
{
	ASSERT(!(rtglock_flags & ~XFS_RTGLOCK_ALL_FLAGS));
	ASSERT(!(rtglock_flags & XFS_RTGLOCK_BITMAP_SHARED) ||
	       !(rtglock_flags & XFS_RTGLOCK_BITMAP));

	if (rtglock_flags & XFS_RTGLOCK_BITMAP) {
		xfs_iunlock(rtg->rtg_inodes[XFS_RTGI_SUMMARY], XFS_ILOCK_EXCL);
		xfs_iunlock(rtg->rtg_inodes[XFS_RTGI_BITMAP], XFS_ILOCK_EXCL);
	} else if (rtglock_flags & XFS_RTGLOCK_BITMAP_SHARED) {
		xfs_iunlock(rtg->rtg_inodes[XFS_RTGI_BITMAP], XFS_ILOCK_SHARED);
	}
}

/*
 * Join realtime group metadata inodes to the transaction.  The ILOCKs will be
 * released on transaction commit.
 */
void
xfs_rtgroup_trans_join(
	struct xfs_trans	*tp,
	struct xfs_rtgroup	*rtg,
	unsigned int		rtglock_flags)
{
	ASSERT(!(rtglock_flags & ~XFS_RTGLOCK_ALL_FLAGS));
	ASSERT(!(rtglock_flags & XFS_RTGLOCK_BITMAP_SHARED));

	if (rtglock_flags & XFS_RTGLOCK_BITMAP) {
		xfs_trans_ijoin(tp, rtg->rtg_inodes[XFS_RTGI_BITMAP],
				XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, rtg->rtg_inodes[XFS_RTGI_SUMMARY],
				XFS_ILOCK_EXCL);
	}
}

#ifdef CONFIG_PROVE_LOCKING
static struct lock_class_key xfs_rtginode_lock_class;

static int
xfs_rtginode_ilock_cmp_fn(
	const struct lockdep_map	*m1,
	const struct lockdep_map	*m2)
{
	const struct xfs_inode *ip1 =
		container_of(m1, struct xfs_inode, i_lock.dep_map);
	const struct xfs_inode *ip2 =
		container_of(m2, struct xfs_inode, i_lock.dep_map);

	if (ip1->i_projid < ip2->i_projid)
		return -1;
	if (ip1->i_projid > ip2->i_projid)
		return 1;
	return 0;
}

static inline void
xfs_rtginode_ilock_print_fn(
	const struct lockdep_map	*m)
{
	const struct xfs_inode *ip =
		container_of(m, struct xfs_inode, i_lock.dep_map);

	printk(KERN_CONT " rgno=%u", ip->i_projid);
}

/*
 * Most of the time each of the RTG inode locks are only taken one at a time.
 * But when committing deferred ops, more than one of a kind can be taken.
 * However, deferred rt ops will be committed in rgno order so there is no
 * potential for deadlocks.  The code here is needed to tell lockdep about this
 * order.
 */
static inline void
xfs_rtginode_lockdep_setup(
	struct xfs_inode	*ip,
	xfs_rgnumber_t		rgno,
	enum xfs_rtg_inodes	type)
{
	lockdep_set_class_and_subclass(&ip->i_lock, &xfs_rtginode_lock_class,
			type);
	lock_set_cmp_fn(&ip->i_lock, xfs_rtginode_ilock_cmp_fn,
			xfs_rtginode_ilock_print_fn);
}
#else
#define xfs_rtginode_lockdep_setup(ip, rgno, type)	do { } while (0)
#endif /* CONFIG_PROVE_LOCKING */

struct xfs_rtginode_ops {
	const char		*name;	/* short name */

	enum xfs_metafile_type	metafile_type;

	/* Does the fs have this feature? */
	bool			(*enabled)(struct xfs_mount *mp);

	/* Create this rtgroup metadata inode and initialize it. */
	int			(*create)(struct xfs_rtgroup *rtg,
					  struct xfs_inode *ip,
					  struct xfs_trans *tp,
					  bool init);
};

static const struct xfs_rtginode_ops xfs_rtginode_ops[XFS_RTGI_MAX] = {
	[XFS_RTGI_BITMAP] = {
		.name		= "bitmap",
		.metafile_type	= XFS_METAFILE_RTBITMAP,
		.create		= xfs_rtbitmap_create,
	},
	[XFS_RTGI_SUMMARY] = {
		.name		= "summary",
		.metafile_type	= XFS_METAFILE_RTSUMMARY,
		.create		= xfs_rtsummary_create,
	},
};

/* Return the shortname of this rtgroup inode. */
const char *
xfs_rtginode_name(
	enum xfs_rtg_inodes	type)
{
	return xfs_rtginode_ops[type].name;
}

/* Return the metafile type of this rtgroup inode. */
enum xfs_metafile_type
xfs_rtginode_metafile_type(
	enum xfs_rtg_inodes	type)
{
	return xfs_rtginode_ops[type].metafile_type;
}

/* Should this rtgroup inode be present? */
bool
xfs_rtginode_enabled(
	struct xfs_rtgroup	*rtg,
	enum xfs_rtg_inodes	type)
{
	const struct xfs_rtginode_ops *ops = &xfs_rtginode_ops[type];

	if (!ops->enabled)
		return true;
	return ops->enabled(rtg_mount(rtg));
}

/* Load and existing rtgroup inode into the rtgroup structure. */
int
xfs_rtginode_load(
	struct xfs_rtgroup	*rtg,
	enum xfs_rtg_inodes	type,
	struct xfs_trans	*tp)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_inode	*ip;
	const struct xfs_rtginode_ops *ops = &xfs_rtginode_ops[type];
	int			error;

	if (!xfs_rtginode_enabled(rtg, type))
		return 0;

	if (!xfs_has_rtgroups(mp)) {
		xfs_ino_t	ino;

		switch (type) {
		case XFS_RTGI_BITMAP:
			ino = mp->m_sb.sb_rbmino;
			break;
		case XFS_RTGI_SUMMARY:
			ino = mp->m_sb.sb_rsumino;
			break;
		default:
			/* None of the other types exist on !rtgroups */
			return 0;
		}

		error = xfs_trans_metafile_iget(tp, ino, ops->metafile_type,
				&ip);
	} else {
		const char	*path;

		if (!mp->m_rtdirip)
			return -EFSCORRUPTED;

		path = xfs_rtginode_path(rtg_rgno(rtg), type);
		if (!path)
			return -ENOMEM;
		error = xfs_metadir_load(tp, mp->m_rtdirip, path,
				ops->metafile_type, &ip);
		kfree(path);
	}

	if (error)
		return error;

	if (XFS_IS_CORRUPT(mp, ip->i_df.if_format != XFS_DINODE_FMT_EXTENTS &&
			       ip->i_df.if_format != XFS_DINODE_FMT_BTREE)) {
		xfs_irele(ip);
		return -EFSCORRUPTED;
	}

	if (XFS_IS_CORRUPT(mp, ip->i_projid != rtg_rgno(rtg))) {
		xfs_irele(ip);
		return -EFSCORRUPTED;
	}

	xfs_rtginode_lockdep_setup(ip, rtg_rgno(rtg), type);
	rtg->rtg_inodes[type] = ip;
	return 0;
}

/* Release an rtgroup metadata inode. */
void
xfs_rtginode_irele(
	struct xfs_inode	**ipp)
{
	if (*ipp)
		xfs_irele(*ipp);
	*ipp = NULL;
}

/* Add a metadata inode for a realtime rmap btree. */
int
xfs_rtginode_create(
	struct xfs_rtgroup		*rtg,
	enum xfs_rtg_inodes		type,
	bool				init)
{
	const struct xfs_rtginode_ops	*ops = &xfs_rtginode_ops[type];
	struct xfs_mount		*mp = rtg_mount(rtg);
	struct xfs_metadir_update	upd = {
		.dp			= mp->m_rtdirip,
		.metafile_type		= ops->metafile_type,
	};
	int				error;

	if (!xfs_rtginode_enabled(rtg, type))
		return 0;

	if (!mp->m_rtdirip)
		return -EFSCORRUPTED;

	upd.path = xfs_rtginode_path(rtg_rgno(rtg), type);
	if (!upd.path)
		return -ENOMEM;

	error = xfs_metadir_start_create(&upd);
	if (error)
		goto out_path;

	error = xfs_metadir_create(&upd, S_IFREG);
	if (error)
		return error;

	xfs_rtginode_lockdep_setup(upd.ip, rtg_rgno(rtg), type);

	upd.ip->i_projid = rtg_rgno(rtg);
	error = ops->create(rtg, upd.ip, upd.tp, init);
	if (error)
		goto out_cancel;

	error = xfs_metadir_commit(&upd);
	if (error)
		goto out_path;

	kfree(upd.path);
	xfs_finish_inode_setup(upd.ip);
	rtg->rtg_inodes[type] = upd.ip;
	return 0;

out_cancel:
	xfs_metadir_cancel(&upd, error);
	/* Have to finish setting up the inode to ensure it's deleted. */
	if (upd.ip) {
		xfs_finish_inode_setup(upd.ip);
		xfs_irele(upd.ip);
	}
out_path:
	kfree(upd.path);
	return error;
}

/* Create the parent directory for all rtgroup inodes and load it. */
int
xfs_rtginode_mkdir_parent(
	struct xfs_mount	*mp)
{
	if (!mp->m_metadirip)
		return -EFSCORRUPTED;

	return xfs_metadir_mkdir(mp->m_metadirip, "rtgroups", &mp->m_rtdirip);
}

/* Load the parent directory of all rtgroup inodes. */
int
xfs_rtginode_load_parent(
	struct xfs_trans	*tp)
{
	struct xfs_mount	*mp = tp->t_mountp;

	if (!mp->m_metadirip)
		return -EFSCORRUPTED;

	return xfs_metadir_load(tp, mp->m_metadirip, "rtgroups",
			XFS_METAFILE_DIR, &mp->m_rtdirip);
}
