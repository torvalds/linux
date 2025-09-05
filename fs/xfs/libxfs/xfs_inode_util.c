// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include <linux/iversion.h>
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_inode_util.h"
#include "xfs_trans.h"
#include "xfs_ialloc.h"
#include "xfs_health.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_ag.h"
#include "xfs_iunlink_item.h"
#include "xfs_inode_item.h"

uint16_t
xfs_flags2diflags(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	/* can't set PREALLOC this way, just preserve it */
	uint16_t		di_flags =
		(ip->i_diflags & XFS_DIFLAG_PREALLOC);

	if (xflags & FS_XFLAG_IMMUTABLE)
		di_flags |= XFS_DIFLAG_IMMUTABLE;
	if (xflags & FS_XFLAG_APPEND)
		di_flags |= XFS_DIFLAG_APPEND;
	if (xflags & FS_XFLAG_SYNC)
		di_flags |= XFS_DIFLAG_SYNC;
	if (xflags & FS_XFLAG_NOATIME)
		di_flags |= XFS_DIFLAG_NOATIME;
	if (xflags & FS_XFLAG_NODUMP)
		di_flags |= XFS_DIFLAG_NODUMP;
	if (xflags & FS_XFLAG_NODEFRAG)
		di_flags |= XFS_DIFLAG_NODEFRAG;
	if (xflags & FS_XFLAG_FILESTREAM)
		di_flags |= XFS_DIFLAG_FILESTREAM;
	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		if (xflags & FS_XFLAG_RTINHERIT)
			di_flags |= XFS_DIFLAG_RTINHERIT;
		if (xflags & FS_XFLAG_NOSYMLINKS)
			di_flags |= XFS_DIFLAG_NOSYMLINKS;
		if (xflags & FS_XFLAG_EXTSZINHERIT)
			di_flags |= XFS_DIFLAG_EXTSZINHERIT;
		if (xflags & FS_XFLAG_PROJINHERIT)
			di_flags |= XFS_DIFLAG_PROJINHERIT;
	} else if (S_ISREG(VFS_I(ip)->i_mode)) {
		if (xflags & FS_XFLAG_REALTIME)
			di_flags |= XFS_DIFLAG_REALTIME;
		if (xflags & FS_XFLAG_EXTSIZE)
			di_flags |= XFS_DIFLAG_EXTSIZE;
	}

	return di_flags;
}

uint64_t
xfs_flags2diflags2(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	uint64_t		di_flags2 =
		(ip->i_diflags2 & (XFS_DIFLAG2_REFLINK |
				   XFS_DIFLAG2_BIGTIME |
				   XFS_DIFLAG2_NREXT64));

	if (xflags & FS_XFLAG_DAX)
		di_flags2 |= XFS_DIFLAG2_DAX;
	if (xflags & FS_XFLAG_COWEXTSIZE)
		di_flags2 |= XFS_DIFLAG2_COWEXTSIZE;

	return di_flags2;
}

uint32_t
xfs_ip2xflags(
	struct xfs_inode	*ip)
{
	uint32_t		flags = 0;

	if (ip->i_diflags & XFS_DIFLAG_ANY) {
		if (ip->i_diflags & XFS_DIFLAG_REALTIME)
			flags |= FS_XFLAG_REALTIME;
		if (ip->i_diflags & XFS_DIFLAG_PREALLOC)
			flags |= FS_XFLAG_PREALLOC;
		if (ip->i_diflags & XFS_DIFLAG_IMMUTABLE)
			flags |= FS_XFLAG_IMMUTABLE;
		if (ip->i_diflags & XFS_DIFLAG_APPEND)
			flags |= FS_XFLAG_APPEND;
		if (ip->i_diflags & XFS_DIFLAG_SYNC)
			flags |= FS_XFLAG_SYNC;
		if (ip->i_diflags & XFS_DIFLAG_NOATIME)
			flags |= FS_XFLAG_NOATIME;
		if (ip->i_diflags & XFS_DIFLAG_NODUMP)
			flags |= FS_XFLAG_NODUMP;
		if (ip->i_diflags & XFS_DIFLAG_RTINHERIT)
			flags |= FS_XFLAG_RTINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_PROJINHERIT)
			flags |= FS_XFLAG_PROJINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_NOSYMLINKS)
			flags |= FS_XFLAG_NOSYMLINKS;
		if (ip->i_diflags & XFS_DIFLAG_EXTSIZE)
			flags |= FS_XFLAG_EXTSIZE;
		if (ip->i_diflags & XFS_DIFLAG_EXTSZINHERIT)
			flags |= FS_XFLAG_EXTSZINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_NODEFRAG)
			flags |= FS_XFLAG_NODEFRAG;
		if (ip->i_diflags & XFS_DIFLAG_FILESTREAM)
			flags |= FS_XFLAG_FILESTREAM;
	}

	if (ip->i_diflags2 & XFS_DIFLAG2_ANY) {
		if (ip->i_diflags2 & XFS_DIFLAG2_DAX)
			flags |= FS_XFLAG_DAX;
		if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
			flags |= FS_XFLAG_COWEXTSIZE;
	}

	if (xfs_inode_has_attr_fork(ip))
		flags |= FS_XFLAG_HASATTR;
	return flags;
}

prid_t
xfs_get_initial_prid(struct xfs_inode *dp)
{
	if (dp->i_diflags & XFS_DIFLAG_PROJINHERIT)
		return dp->i_projid;

	/* Assign to the root project by default. */
	return 0;
}

/* Propagate di_flags from a parent inode to a child inode. */
static inline void
xfs_inode_inherit_flags(
	struct xfs_inode	*ip,
	const struct xfs_inode	*pip)
{
	unsigned int		di_flags = 0;
	xfs_failaddr_t		failaddr;
	umode_t			mode = VFS_I(ip)->i_mode;

	if (S_ISDIR(mode)) {
		if (pip->i_diflags & XFS_DIFLAG_RTINHERIT)
			di_flags |= XFS_DIFLAG_RTINHERIT;
		if (pip->i_diflags & XFS_DIFLAG_EXTSZINHERIT) {
			di_flags |= XFS_DIFLAG_EXTSZINHERIT;
			ip->i_extsize = pip->i_extsize;
		}
		if (pip->i_diflags & XFS_DIFLAG_PROJINHERIT)
			di_flags |= XFS_DIFLAG_PROJINHERIT;
	} else if (S_ISREG(mode)) {
		if ((pip->i_diflags & XFS_DIFLAG_RTINHERIT) &&
		    xfs_has_realtime(ip->i_mount))
			di_flags |= XFS_DIFLAG_REALTIME;
		if (pip->i_diflags & XFS_DIFLAG_EXTSZINHERIT) {
			di_flags |= XFS_DIFLAG_EXTSIZE;
			ip->i_extsize = pip->i_extsize;
		}
	}
	if ((pip->i_diflags & XFS_DIFLAG_NOATIME) &&
	    xfs_inherit_noatime)
		di_flags |= XFS_DIFLAG_NOATIME;
	if ((pip->i_diflags & XFS_DIFLAG_NODUMP) &&
	    xfs_inherit_nodump)
		di_flags |= XFS_DIFLAG_NODUMP;
	if ((pip->i_diflags & XFS_DIFLAG_SYNC) &&
	    xfs_inherit_sync)
		di_flags |= XFS_DIFLAG_SYNC;
	if ((pip->i_diflags & XFS_DIFLAG_NOSYMLINKS) &&
	    xfs_inherit_nosymlinks)
		di_flags |= XFS_DIFLAG_NOSYMLINKS;
	if ((pip->i_diflags & XFS_DIFLAG_NODEFRAG) &&
	    xfs_inherit_nodefrag)
		di_flags |= XFS_DIFLAG_NODEFRAG;
	if (pip->i_diflags & XFS_DIFLAG_FILESTREAM)
		di_flags |= XFS_DIFLAG_FILESTREAM;

	ip->i_diflags |= di_flags;

	/*
	 * Inode verifiers on older kernels only check that the extent size
	 * hint is an integer multiple of the rt extent size on realtime files.
	 * They did not check the hint alignment on a directory with both
	 * rtinherit and extszinherit flags set.  If the misaligned hint is
	 * propagated from a directory into a new realtime file, new file
	 * allocations will fail due to math errors in the rt allocator and/or
	 * trip the verifiers.  Validate the hint settings in the new file so
	 * that we don't let broken hints propagate.
	 */
	failaddr = xfs_inode_validate_extsize(ip->i_mount, ip->i_extsize,
			VFS_I(ip)->i_mode, ip->i_diflags);
	if (failaddr) {
		ip->i_diflags &= ~(XFS_DIFLAG_EXTSIZE |
				   XFS_DIFLAG_EXTSZINHERIT);
		ip->i_extsize = 0;
	}
}

/* Propagate di_flags2 from a parent inode to a child inode. */
static inline void
xfs_inode_inherit_flags2(
	struct xfs_inode	*ip,
	const struct xfs_inode	*pip)
{
	xfs_failaddr_t		failaddr;

	if (pip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE) {
		ip->i_diflags2 |= XFS_DIFLAG2_COWEXTSIZE;
		ip->i_cowextsize = pip->i_cowextsize;
	}
	if (pip->i_diflags2 & XFS_DIFLAG2_DAX)
		ip->i_diflags2 |= XFS_DIFLAG2_DAX;
	if (xfs_is_metadir_inode(pip))
		ip->i_diflags2 |= XFS_DIFLAG2_METADATA;

	/* Don't let invalid cowextsize hints propagate. */
	failaddr = xfs_inode_validate_cowextsize(ip->i_mount, ip->i_cowextsize,
			VFS_I(ip)->i_mode, ip->i_diflags, ip->i_diflags2);
	if (failaddr) {
		ip->i_diflags2 &= ~XFS_DIFLAG2_COWEXTSIZE;
		ip->i_cowextsize = 0;
	}
}

/*
 * If we need to create attributes immediately after allocating the inode,
 * initialise an empty attribute fork right now. We use the default fork offset
 * for attributes here as we don't know exactly what size or how many
 * attributes we might be adding. We can do this safely here because we know
 * the data fork is completely empty and this saves us from needing to run a
 * separate transaction to set the fork offset in the immediate future.
 *
 * If we have parent pointers and the caller hasn't told us that the file will
 * never be linked into a directory tree, we /must/ create the attr fork.
 */
static inline bool
xfs_icreate_want_attrfork(
	struct xfs_mount		*mp,
	const struct xfs_icreate_args	*args)
{
	if (args->flags & XFS_ICREATE_INIT_XATTRS)
		return true;

	if (!(args->flags & XFS_ICREATE_UNLINKABLE) && xfs_has_parent(mp))
		return true;

	return false;
}

/* Initialise an inode's attributes. */
void
xfs_inode_init(
	struct xfs_trans	*tp,
	const struct xfs_icreate_args *args,
	struct xfs_inode	*ip)
{
	struct xfs_inode	*pip = args->pip;
	struct inode		*dir = pip ? VFS_I(pip) : NULL;
	struct xfs_mount	*mp = tp->t_mountp;
	struct inode		*inode = VFS_I(ip);
	unsigned int		flags;
	int			times = XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG |
					XFS_ICHGTIME_ACCESS;

	if (args->flags & XFS_ICREATE_TMPFILE)
		set_nlink(inode, 0);
	else if (S_ISDIR(args->mode))
		set_nlink(inode, 2);
	else
		set_nlink(inode, 1);
	inode->i_rdev = args->rdev;

	if (!args->idmap || pip == NULL) {
		/* creating a tree root, sb rooted, or detached file */
		inode->i_uid = GLOBAL_ROOT_UID;
		inode->i_gid = GLOBAL_ROOT_GID;
		ip->i_projid = 0;
		inode->i_mode = args->mode;
	} else {
		/* creating a child in the directory tree */
		if (dir && !(dir->i_mode & S_ISGID) && xfs_has_grpid(mp)) {
			inode_fsuid_set(inode, args->idmap);
			inode->i_gid = dir->i_gid;
			inode->i_mode = args->mode;
		} else {
			inode_init_owner(args->idmap, inode, dir, args->mode);
		}
		ip->i_projid = xfs_get_initial_prid(pip);
	}

	ip->i_disk_size = 0;
	ip->i_df.if_nextents = 0;
	ASSERT(ip->i_nblocks == 0);

	ip->i_extsize = 0;
	ip->i_diflags = 0;

	if (xfs_has_v3inodes(mp)) {
		inode_set_iversion(inode, 1);
		/* also covers the di_used_blocks union arm: */
		ip->i_cowextsize = 0;
		times |= XFS_ICHGTIME_CREATE;
	}

	xfs_trans_ichgtime(tp, ip, times);

	flags = XFS_ILOG_CORE;
	switch (args->mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		ip->i_df.if_format = XFS_DINODE_FMT_DEV;
		flags |= XFS_ILOG_DEV;
		break;
	case S_IFREG:
	case S_IFDIR:
		if (pip && (pip->i_diflags & XFS_DIFLAG_ANY))
			xfs_inode_inherit_flags(ip, pip);
		if (pip && (pip->i_diflags2 & XFS_DIFLAG2_ANY))
			xfs_inode_inherit_flags2(ip, pip);
		fallthrough;
	case S_IFLNK:
		ip->i_df.if_format = XFS_DINODE_FMT_EXTENTS;
		ip->i_df.if_bytes = 0;
		ip->i_df.if_data = NULL;
		break;
	default:
		ASSERT(0);
	}

	if (xfs_icreate_want_attrfork(mp, args)) {
		ip->i_forkoff = xfs_default_attroffset(ip) >> 3;
		xfs_ifork_init_attr(ip, XFS_DINODE_FMT_EXTENTS, 0);

		if (!xfs_has_attr(mp)) {
			spin_lock(&mp->m_sb_lock);
			xfs_add_attr(mp);
			spin_unlock(&mp->m_sb_lock);
			xfs_log_sb(tp);
		}
	}

	xfs_trans_log_inode(tp, ip, flags);
}

/*
 * In-Core Unlinked List Lookups
 * =============================
 *
 * Every inode is supposed to be reachable from some other piece of metadata
 * with the exception of the root directory.  Inodes with a connection to a
 * file descriptor but not linked from anywhere in the on-disk directory tree
 * are collectively known as unlinked inodes, though the filesystem itself
 * maintains links to these inodes so that on-disk metadata are consistent.
 *
 * XFS implements a per-AG on-disk hash table of unlinked inodes.  The AGI
 * header contains a number of buckets that point to an inode, and each inode
 * record has a pointer to the next inode in the hash chain.  This
 * singly-linked list causes scaling problems in the iunlink remove function
 * because we must walk that list to find the inode that points to the inode
 * being removed from the unlinked hash bucket list.
 *
 * Hence we keep an in-memory double linked list to link each inode on an
 * unlinked list. Because there are 64 unlinked lists per AGI, keeping pointer
 * based lists would require having 64 list heads in the perag, one for each
 * list. This is expensive in terms of memory (think millions of AGs) and cache
 * misses on lookups. Instead, use the fact that inodes on the unlinked list
 * must be referenced at the VFS level to keep them on the list and hence we
 * have an existence guarantee for inodes on the unlinked list.
 *
 * Given we have an existence guarantee, we can use lockless inode cache lookups
 * to resolve aginos to xfs inodes. This means we only need 8 bytes per inode
 * for the double linked unlinked list, and we don't need any extra locking to
 * keep the list safe as all manipulations are done under the AGI buffer lock.
 * Keeping the list up to date does not require memory allocation, just finding
 * the XFS inode and updating the next/prev unlinked list aginos.
 */

/*
 * Update the prev pointer of the next agino.  Returns -ENOLINK if the inode
 * is not in cache.
 */
static int
xfs_iunlink_update_backref(
	struct xfs_perag	*pag,
	xfs_agino_t		prev_agino,
	xfs_agino_t		next_agino)
{
	struct xfs_inode	*ip;

	/* No update necessary if we are at the end of the list. */
	if (next_agino == NULLAGINO)
		return 0;

	ip = xfs_iunlink_lookup(pag, next_agino);
	if (!ip)
		return -ENOLINK;

	ip->i_prev_unlinked = prev_agino;
	return 0;
}

/*
 * Point the AGI unlinked bucket at an inode and log the results.  The caller
 * is responsible for validating the old value.
 */
STATIC int
xfs_iunlink_update_bucket(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		*agibp,
	unsigned int		bucket_index,
	xfs_agino_t		new_agino)
{
	struct xfs_agi		*agi = agibp->b_addr;
	xfs_agino_t		old_value;
	int			offset;

	ASSERT(xfs_verify_agino_or_null(pag, new_agino));

	old_value = be32_to_cpu(agi->agi_unlinked[bucket_index]);
	trace_xfs_iunlink_update_bucket(pag, bucket_index, old_value,
			new_agino);

	/*
	 * We should never find the head of the list already set to the value
	 * passed in because either we're adding or removing ourselves from the
	 * head of the list.
	 */
	if (old_value == new_agino) {
		xfs_buf_mark_corrupt(agibp);
		xfs_ag_mark_sick(pag, XFS_SICK_AG_AGI);
		return -EFSCORRUPTED;
	}

	agi->agi_unlinked[bucket_index] = cpu_to_be32(new_agino);
	offset = offsetof(struct xfs_agi, agi_unlinked) +
			(sizeof(xfs_agino_t) * bucket_index);
	xfs_trans_log_buf(tp, agibp, offset, offset + sizeof(xfs_agino_t) - 1);
	return 0;
}

static int
xfs_iunlink_insert_inode(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		*agibp,
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agi		*agi = agibp->b_addr;
	xfs_agino_t		next_agino;
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	short			bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	int			error;

	/*
	 * Get the index into the agi hash table for the list this inode will
	 * go on.  Make sure the pointer isn't garbage and that this inode
	 * isn't already on the list.
	 */
	next_agino = be32_to_cpu(agi->agi_unlinked[bucket_index]);
	if (next_agino == agino ||
	    !xfs_verify_agino_or_null(pag, next_agino)) {
		xfs_buf_mark_corrupt(agibp);
		xfs_ag_mark_sick(pag, XFS_SICK_AG_AGI);
		return -EFSCORRUPTED;
	}

	/*
	 * Update the prev pointer in the next inode to point back to this
	 * inode.
	 */
	error = xfs_iunlink_update_backref(pag, agino, next_agino);
	if (error == -ENOLINK)
		error = xfs_iunlink_reload_next(tp, agibp, agino, next_agino);
	if (error)
		return error;

	if (next_agino != NULLAGINO) {
		/*
		 * There is already another inode in the bucket, so point this
		 * inode to the current head of the list.
		 */
		error = xfs_iunlink_log_inode(tp, ip, pag, next_agino);
		if (error)
			return error;
		ip->i_next_unlinked = next_agino;
	}

	/* Point the head of the list to point to this inode. */
	ip->i_prev_unlinked = NULLAGINO;
	return xfs_iunlink_update_bucket(tp, pag, agibp, bucket_index, agino);
}

/*
 * This is called when the inode's link count has gone to 0 or we are creating
 * a tmpfile via O_TMPFILE.  The inode @ip must have nlink == 0.
 *
 * We place the on-disk inode on a list in the AGI.  It will be pulled from this
 * list when the inode is freed.
 */
int
xfs_iunlink(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_perag	*pag;
	struct xfs_buf		*agibp;
	int			error;

	ASSERT(VFS_I(ip)->i_nlink == 0);
	ASSERT(VFS_I(ip)->i_mode != 0);
	trace_xfs_iunlink(ip);

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));

	/* Get the agi buffer first.  It ensures lock ordering on the list. */
	error = xfs_read_agi(pag, tp, 0, &agibp);
	if (error)
		goto out;

	error = xfs_iunlink_insert_inode(tp, pag, agibp, ip);
out:
	xfs_perag_put(pag);
	return error;
}

static int
xfs_iunlink_remove_inode(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		*agibp,
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agi		*agi = agibp->b_addr;
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	xfs_agino_t		head_agino;
	short			bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	int			error;

	trace_xfs_iunlink_remove(ip);

	/*
	 * Get the index into the agi hash table for the list this inode will
	 * go on.  Make sure the head pointer isn't garbage.
	 */
	head_agino = be32_to_cpu(agi->agi_unlinked[bucket_index]);
	if (!xfs_verify_agino(pag, head_agino)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				agi, sizeof(*agi));
		xfs_ag_mark_sick(pag, XFS_SICK_AG_AGI);
		return -EFSCORRUPTED;
	}

	/*
	 * Set our inode's next_unlinked pointer to NULL and then return
	 * the old pointer value so that we can update whatever was previous
	 * to us in the list to point to whatever was next in the list.
	 */
	error = xfs_iunlink_log_inode(tp, ip, pag, NULLAGINO);
	if (error)
		return error;

	/*
	 * Update the prev pointer in the next inode to point back to previous
	 * inode in the chain.
	 */
	error = xfs_iunlink_update_backref(pag, ip->i_prev_unlinked,
			ip->i_next_unlinked);
	if (error == -ENOLINK)
		error = xfs_iunlink_reload_next(tp, agibp, ip->i_prev_unlinked,
				ip->i_next_unlinked);
	if (error)
		return error;

	if (head_agino != agino) {
		struct xfs_inode	*prev_ip;

		prev_ip = xfs_iunlink_lookup(pag, ip->i_prev_unlinked);
		if (!prev_ip) {
			xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
			return -EFSCORRUPTED;
		}

		error = xfs_iunlink_log_inode(tp, prev_ip, pag,
				ip->i_next_unlinked);
		prev_ip->i_next_unlinked = ip->i_next_unlinked;
	} else {
		/* Point the head of the list to the next unlinked inode. */
		error = xfs_iunlink_update_bucket(tp, pag, agibp, bucket_index,
				ip->i_next_unlinked);
	}

	ip->i_next_unlinked = NULLAGINO;
	ip->i_prev_unlinked = 0;
	return error;
}

/*
 * Pull the on-disk inode from the AGI unlinked list.
 */
int
xfs_iunlink_remove(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_inode	*ip)
{
	struct xfs_buf		*agibp;
	int			error;

	trace_xfs_iunlink_remove(ip);

	/* Get the agi buffer first.  It ensures lock ordering on the list. */
	error = xfs_read_agi(pag, tp, 0, &agibp);
	if (error)
		return error;

	return xfs_iunlink_remove_inode(tp, pag, agibp, ip);
}

/*
 * Decrement the link count on an inode & log the change.  If this causes the
 * link count to go to zero, move the inode to AGI unlinked list so that it can
 * be freed when the last active reference goes away via xfs_inactive().
 */
int
xfs_droplink(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	struct inode		*inode = VFS_I(ip);

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	if (inode->i_nlink == 0) {
		xfs_info_ratelimited(tp->t_mountp,
 "Inode 0x%llx link count dropped below zero.  Pinning link count.",
				ip->i_ino);
		set_nlink(inode, XFS_NLINK_PINNED);
	}
	if (inode->i_nlink != XFS_NLINK_PINNED)
		drop_nlink(inode);

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	if (inode->i_nlink)
		return 0;

	return xfs_iunlink(tp, ip);
}

/*
 * Increment the link count on an inode & log the change.
 */
void
xfs_bumplink(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	struct inode		*inode = VFS_I(ip);

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	if (inode->i_nlink == XFS_NLINK_PINNED - 1)
		xfs_info_ratelimited(tp->t_mountp,
 "Inode 0x%llx link count exceeded maximum.  Pinning link count.",
				ip->i_ino);
	if (inode->i_nlink != XFS_NLINK_PINNED)
		inc_nlink(inode);

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}

/* Free an inode in the ondisk index and zero it out. */
int
xfs_inode_uninit(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_inode	*ip,
	struct xfs_icluster	*xic)
{
	struct xfs_mount	*mp = ip->i_mount;
	int			error;

	/*
	 * Free the inode first so that we guarantee that the AGI lock is going
	 * to be taken before we remove the inode from the unlinked list. This
	 * makes the AGI lock -> unlinked list modification order the same as
	 * used in O_TMPFILE creation.
	 */
	error = xfs_difree(tp, pag, ip->i_ino, xic);
	if (error)
		return error;

	error = xfs_iunlink_remove(tp, pag, ip);
	if (error)
		return error;

	/*
	 * Free any local-format data sitting around before we reset the
	 * data fork to extents format.  Note that the attr fork data has
	 * already been freed by xfs_attr_inactive.
	 */
	if (ip->i_df.if_format == XFS_DINODE_FMT_LOCAL) {
		kfree(ip->i_df.if_data);
		ip->i_df.if_data = NULL;
		ip->i_df.if_bytes = 0;
	}

	VFS_I(ip)->i_mode = 0;		/* mark incore inode as free */
	ip->i_diflags = 0;
	ip->i_diflags2 = mp->m_ino_geo.new_diflags2;
	ip->i_forkoff = 0;		/* mark the attr fork not in use */
	ip->i_df.if_format = XFS_DINODE_FMT_EXTENTS;

	/*
	 * Bump the generation count so no one will be confused
	 * by reincarnations of this inode.
	 */
	VFS_I(ip)->i_generation++;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return 0;
}
