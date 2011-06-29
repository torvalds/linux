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
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_trans_space.h"
#include "xfs_rw.h"
#include "xfs_vnodeops.h"
#include "xfs_trace.h"

/*
 * xfs_attr.c
 *
 * Provide the external interfaces to manage attribute lists.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Internal routines when attribute list fits inside the inode.
 */
STATIC int xfs_attr_shortform_addname(xfs_da_args_t *args);

/*
 * Internal routines when attribute list is one block.
 */
STATIC int xfs_attr_leaf_get(xfs_da_args_t *args);
STATIC int xfs_attr_leaf_addname(xfs_da_args_t *args);
STATIC int xfs_attr_leaf_removename(xfs_da_args_t *args);
STATIC int xfs_attr_leaf_list(xfs_attr_list_context_t *context);

/*
 * Internal routines when attribute list is more than one block.
 */
STATIC int xfs_attr_node_get(xfs_da_args_t *args);
STATIC int xfs_attr_node_addname(xfs_da_args_t *args);
STATIC int xfs_attr_node_removename(xfs_da_args_t *args);
STATIC int xfs_attr_node_list(xfs_attr_list_context_t *context);
STATIC int xfs_attr_fillstate(xfs_da_state_t *state);
STATIC int xfs_attr_refillstate(xfs_da_state_t *state);

/*
 * Routines to manipulate out-of-line attribute values.
 */
STATIC int xfs_attr_rmtval_set(xfs_da_args_t *args);
STATIC int xfs_attr_rmtval_remove(xfs_da_args_t *args);

#define ATTR_RMTVALUE_MAPSIZE	1	/* # of map entries at once */

STATIC int
xfs_attr_name_to_xname(
	struct xfs_name	*xname,
	const unsigned char *aname)
{
	if (!aname)
		return EINVAL;
	xname->name = aname;
	xname->len = strlen((char *)aname);
	if (xname->len >= MAXNAMELEN)
		return EFAULT;		/* match IRIX behaviour */

	return 0;
}

STATIC int
xfs_inode_hasattr(
	struct xfs_inode	*ip)
{
	if (!XFS_IFORK_Q(ip) ||
	    (ip->i_d.di_aformat == XFS_DINODE_FMT_EXTENTS &&
	     ip->i_d.di_anextents == 0))
		return 0;
	return 1;
}

/*========================================================================
 * Overall external interface routines.
 *========================================================================*/

STATIC int
xfs_attr_get_int(
	struct xfs_inode	*ip,
	struct xfs_name		*name,
	unsigned char		*value,
	int			*valuelenp,
	int			flags)
{
	xfs_da_args_t   args;
	int             error;

	if (!xfs_inode_hasattr(ip))
		return ENOATTR;

	/*
	 * Fill in the arg structure for this request.
	 */
	memset((char *)&args, 0, sizeof(args));
	args.name = name->name;
	args.namelen = name->len;
	args.value = value;
	args.valuelen = *valuelenp;
	args.flags = flags;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.dp = ip;
	args.whichfork = XFS_ATTR_FORK;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (ip->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		error = xfs_attr_shortform_getvalue(&args);
	} else if (xfs_bmap_one_block(ip, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_get(&args);
	} else {
		error = xfs_attr_node_get(&args);
	}

	/*
	 * Return the number of bytes in the value to the caller.
	 */
	*valuelenp = args.valuelen;

	if (error == EEXIST)
		error = 0;
	return(error);
}

int
xfs_attr_get(
	xfs_inode_t	*ip,
	const unsigned char *name,
	unsigned char	*value,
	int		*valuelenp,
	int		flags)
{
	int		error;
	struct xfs_name	xname;

	XFS_STATS_INC(xs_attr_get);

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return(EIO);

	error = xfs_attr_name_to_xname(&xname, name);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	error = xfs_attr_get_int(ip, &xname, value, valuelenp, flags);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return(error);
}

/*
 * Calculate how many blocks we need for the new attribute,
 */
STATIC int
xfs_attr_calc_size(
	struct xfs_inode 	*ip,
	int			namelen,
	int			valuelen,
	int			*local)
{
	struct xfs_mount 	*mp = ip->i_mount;
	int			size;
	int			nblks;

	/*
	 * Determine space new attribute will use, and if it would be
	 * "local" or "remote" (note: local != inline).
	 */
	size = xfs_attr_leaf_newentsize(namelen, valuelen,
					mp->m_sb.sb_blocksize, local);

	nblks = XFS_DAENTER_SPACE_RES(mp, XFS_ATTR_FORK);
	if (*local) {
		if (size > (mp->m_sb.sb_blocksize >> 1)) {
			/* Double split possible */
			nblks *= 2;
		}
	} else {
		/*
		 * Out of line attribute, cannot double split, but
		 * make room for the attribute value itself.
		 */
		uint	dblocks = XFS_B_TO_FSB(mp, valuelen);
		nblks += dblocks;
		nblks += XFS_NEXTENTADD_SPACE_RES(mp, dblocks, XFS_ATTR_FORK);
	}

	return nblks;
}

STATIC int
xfs_attr_set_int(
	struct xfs_inode *dp,
	struct xfs_name	*name,
	unsigned char	*value,
	int		valuelen,
	int		flags)
{
	xfs_da_args_t	args;
	xfs_fsblock_t	firstblock;
	xfs_bmap_free_t flist;
	int		error, err2, committed;
	xfs_mount_t	*mp = dp->i_mount;
	int             rsvd = (flags & ATTR_ROOT) != 0;
	int		local;

	/*
	 * Attach the dquots to the inode.
	 */
	error = xfs_qm_dqattach(dp, 0);
	if (error)
		return error;

	/*
	 * If the inode doesn't have an attribute fork, add one.
	 * (inode must not be locked when we call this routine)
	 */
	if (XFS_IFORK_Q(dp) == 0) {
		int sf_size = sizeof(xfs_attr_sf_hdr_t) +
			      XFS_ATTR_SF_ENTSIZE_BYNAME(name->len, valuelen);

		if ((error = xfs_bmap_add_attrfork(dp, sf_size, rsvd)))
			return(error);
	}

	/*
	 * Fill in the arg structure for this request.
	 */
	memset((char *)&args, 0, sizeof(args));
	args.name = name->name;
	args.namelen = name->len;
	args.value = value;
	args.valuelen = valuelen;
	args.flags = flags;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.dp = dp;
	args.firstblock = &firstblock;
	args.flist = &flist;
	args.whichfork = XFS_ATTR_FORK;
	args.op_flags = XFS_DA_OP_ADDNAME | XFS_DA_OP_OKNOENT;

	/* Size is now blocks for attribute data */
	args.total = xfs_attr_calc_size(dp, name->len, valuelen, &local);

	/*
	 * Start our first transaction of the day.
	 *
	 * All future transactions during this code must be "chained" off
	 * this one via the trans_dup() call.  All transactions will contain
	 * the inode, and the inode will always be marked with trans_ihold().
	 * Since the inode will be locked in all transactions, we must log
	 * the inode in every transaction to let it float upward through
	 * the log.
	 */
	args.trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_SET);

	/*
	 * Root fork attributes can use reserved data blocks for this
	 * operation if necessary
	 */

	if (rsvd)
		args.trans->t_flags |= XFS_TRANS_RESERVE;

	if ((error = xfs_trans_reserve(args.trans, args.total,
			XFS_ATTRSET_LOG_RES(mp, args.total), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_ATTRSET_LOG_COUNT))) {
		xfs_trans_cancel(args.trans, 0);
		return(error);
	}
	xfs_ilock(dp, XFS_ILOCK_EXCL);

	error = xfs_trans_reserve_quota_nblks(args.trans, dp, args.total, 0,
				rsvd ? XFS_QMOPT_RES_REGBLKS | XFS_QMOPT_FORCE_RES :
				       XFS_QMOPT_RES_REGBLKS);
	if (error) {
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
		xfs_trans_cancel(args.trans, XFS_TRANS_RELEASE_LOG_RES);
		return (error);
	}

	xfs_trans_ijoin(args.trans, dp);

	/*
	 * If the attribute list is non-existent or a shortform list,
	 * upgrade it to a single-leaf-block attribute list.
	 */
	if ((dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) ||
	    ((dp->i_d.di_aformat == XFS_DINODE_FMT_EXTENTS) &&
	     (dp->i_d.di_anextents == 0))) {

		/*
		 * Build initial attribute list (if required).
		 */
		if (dp->i_d.di_aformat == XFS_DINODE_FMT_EXTENTS)
			xfs_attr_shortform_create(&args);

		/*
		 * Try to add the attr to the attribute list in
		 * the inode.
		 */
		error = xfs_attr_shortform_addname(&args);
		if (error != ENOSPC) {
			/*
			 * Commit the shortform mods, and we're done.
			 * NOTE: this is also the error path (EEXIST, etc).
			 */
			ASSERT(args.trans != NULL);

			/*
			 * If this is a synchronous mount, make sure that
			 * the transaction goes to disk before returning
			 * to the user.
			 */
			if (mp->m_flags & XFS_MOUNT_WSYNC) {
				xfs_trans_set_sync(args.trans);
			}

			if (!error && (flags & ATTR_KERNOTIME) == 0) {
				xfs_trans_ichgtime(args.trans, dp,
							XFS_ICHGTIME_CHG);
			}
			err2 = xfs_trans_commit(args.trans,
						 XFS_TRANS_RELEASE_LOG_RES);
			xfs_iunlock(dp, XFS_ILOCK_EXCL);

			return(error == 0 ? err2 : error);
		}

		/*
		 * It won't fit in the shortform, transform to a leaf block.
		 * GROT: another possible req'mt for a double-split btree op.
		 */
		xfs_bmap_init(args.flist, args.firstblock);
		error = xfs_attr_shortform_to_leaf(&args);
		if (!error) {
			error = xfs_bmap_finish(&args.trans, args.flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args.trans = NULL;
			xfs_bmap_cancel(&flist);
			goto out;
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args.trans, dp);

		/*
		 * Commit the leaf transformation.  We'll need another (linked)
		 * transaction to add the new attribute to the leaf.
		 */

		error = xfs_trans_roll(&args.trans, dp);
		if (error)
			goto out;

	}

	if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_addname(&args);
	} else {
		error = xfs_attr_node_addname(&args);
	}
	if (error) {
		goto out;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(args.trans);
	}

	if ((flags & ATTR_KERNOTIME) == 0)
		xfs_trans_ichgtime(args.trans, dp, XFS_ICHGTIME_CHG);

	/*
	 * Commit the last in the sequence of transactions.
	 */
	xfs_trans_log_inode(args.trans, dp, XFS_ILOG_CORE);
	error = xfs_trans_commit(args.trans, XFS_TRANS_RELEASE_LOG_RES);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);

	return(error);

out:
	if (args.trans)
		xfs_trans_cancel(args.trans,
			XFS_TRANS_RELEASE_LOG_RES|XFS_TRANS_ABORT);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
	return(error);
}

int
xfs_attr_set(
	xfs_inode_t	*dp,
	const unsigned char *name,
	unsigned char	*value,
	int		valuelen,
	int		flags)
{
	int             error;
	struct xfs_name	xname;

	XFS_STATS_INC(xs_attr_set);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return (EIO);

	error = xfs_attr_name_to_xname(&xname, name);
	if (error)
		return error;

	return xfs_attr_set_int(dp, &xname, value, valuelen, flags);
}

/*
 * Generic handler routine to remove a name from an attribute list.
 * Transitions attribute list from Btree to shortform as necessary.
 */
STATIC int
xfs_attr_remove_int(xfs_inode_t *dp, struct xfs_name *name, int flags)
{
	xfs_da_args_t	args;
	xfs_fsblock_t	firstblock;
	xfs_bmap_free_t	flist;
	int		error;
	xfs_mount_t	*mp = dp->i_mount;

	/*
	 * Fill in the arg structure for this request.
	 */
	memset((char *)&args, 0, sizeof(args));
	args.name = name->name;
	args.namelen = name->len;
	args.flags = flags;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.dp = dp;
	args.firstblock = &firstblock;
	args.flist = &flist;
	args.total = 0;
	args.whichfork = XFS_ATTR_FORK;

	/*
	 * we have no control over the attribute names that userspace passes us
	 * to remove, so we have to allow the name lookup prior to attribute
	 * removal to fail.
	 */
	args.op_flags = XFS_DA_OP_OKNOENT;

	/*
	 * Attach the dquots to the inode.
	 */
	error = xfs_qm_dqattach(dp, 0);
	if (error)
		return error;

	/*
	 * Start our first transaction of the day.
	 *
	 * All future transactions during this code must be "chained" off
	 * this one via the trans_dup() call.  All transactions will contain
	 * the inode, and the inode will always be marked with trans_ihold().
	 * Since the inode will be locked in all transactions, we must log
	 * the inode in every transaction to let it float upward through
	 * the log.
	 */
	args.trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_RM);

	/*
	 * Root fork attributes can use reserved data blocks for this
	 * operation if necessary
	 */

	if (flags & ATTR_ROOT)
		args.trans->t_flags |= XFS_TRANS_RESERVE;

	if ((error = xfs_trans_reserve(args.trans,
				      XFS_ATTRRM_SPACE_RES(mp),
				      XFS_ATTRRM_LOG_RES(mp),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_ATTRRM_LOG_COUNT))) {
		xfs_trans_cancel(args.trans, 0);
		return(error);
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL);
	/*
	 * No need to make quota reservations here. We expect to release some
	 * blocks not allocate in the common case.
	 */
	xfs_trans_ijoin(args.trans, dp);

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (!xfs_inode_hasattr(dp)) {
		error = XFS_ERROR(ENOATTR);
		goto out;
	}
	if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		ASSERT(dp->i_afp->if_flags & XFS_IFINLINE);
		error = xfs_attr_shortform_remove(&args);
		if (error) {
			goto out;
		}
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_removename(&args);
	} else {
		error = xfs_attr_node_removename(&args);
	}
	if (error) {
		goto out;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(args.trans);
	}

	if ((flags & ATTR_KERNOTIME) == 0)
		xfs_trans_ichgtime(args.trans, dp, XFS_ICHGTIME_CHG);

	/*
	 * Commit the last in the sequence of transactions.
	 */
	xfs_trans_log_inode(args.trans, dp, XFS_ILOG_CORE);
	error = xfs_trans_commit(args.trans, XFS_TRANS_RELEASE_LOG_RES);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);

	return(error);

out:
	if (args.trans)
		xfs_trans_cancel(args.trans,
			XFS_TRANS_RELEASE_LOG_RES|XFS_TRANS_ABORT);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
	return(error);
}

int
xfs_attr_remove(
	xfs_inode_t	*dp,
	const unsigned char *name,
	int		flags)
{
	int		error;
	struct xfs_name	xname;

	XFS_STATS_INC(xs_attr_remove);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return (EIO);

	error = xfs_attr_name_to_xname(&xname, name);
	if (error)
		return error;

	xfs_ilock(dp, XFS_ILOCK_SHARED);
	if (!xfs_inode_hasattr(dp)) {
		xfs_iunlock(dp, XFS_ILOCK_SHARED);
		return XFS_ERROR(ENOATTR);
	}
	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	return xfs_attr_remove_int(dp, &xname, flags);
}

int
xfs_attr_list_int(xfs_attr_list_context_t *context)
{
	int error;
	xfs_inode_t *dp = context->dp;

	XFS_STATS_INC(xs_attr_list);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return EIO;

	xfs_ilock(dp, XFS_ILOCK_SHARED);

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (!xfs_inode_hasattr(dp)) {
		error = 0;
	} else if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		error = xfs_attr_shortform_list(context);
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_list(context);
	} else {
		error = xfs_attr_node_list(context);
	}

	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	return error;
}

#define	ATTR_ENTBASESIZE		/* minimum bytes used by an attr */ \
	(((struct attrlist_ent *) 0)->a_name - (char *) 0)
#define	ATTR_ENTSIZE(namelen)		/* actual bytes used by an attr */ \
	((ATTR_ENTBASESIZE + (namelen) + 1 + sizeof(u_int32_t)-1) \
	 & ~(sizeof(u_int32_t)-1))

/*
 * Format an attribute and copy it out to the user's buffer.
 * Take care to check values and protect against them changing later,
 * we may be reading them directly out of a user buffer.
 */
/*ARGSUSED*/
STATIC int
xfs_attr_put_listent(
	xfs_attr_list_context_t *context,
	int		flags,
	unsigned char	*name,
	int		namelen,
	int		valuelen,
	unsigned char	*value)
{
	struct attrlist *alist = (struct attrlist *)context->alist;
	attrlist_ent_t *aep;
	int arraytop;

	ASSERT(!(context->flags & ATTR_KERNOVAL));
	ASSERT(context->count >= 0);
	ASSERT(context->count < (ATTR_MAX_VALUELEN/8));
	ASSERT(context->firstu >= sizeof(*alist));
	ASSERT(context->firstu <= context->bufsize);

	/*
	 * Only list entries in the right namespace.
	 */
	if (((context->flags & ATTR_SECURE) == 0) !=
	    ((flags & XFS_ATTR_SECURE) == 0))
		return 0;
	if (((context->flags & ATTR_ROOT) == 0) !=
	    ((flags & XFS_ATTR_ROOT) == 0))
		return 0;

	arraytop = sizeof(*alist) +
			context->count * sizeof(alist->al_offset[0]);
	context->firstu -= ATTR_ENTSIZE(namelen);
	if (context->firstu < arraytop) {
		trace_xfs_attr_list_full(context);
		alist->al_more = 1;
		context->seen_enough = 1;
		return 1;
	}

	aep = (attrlist_ent_t *)&context->alist[context->firstu];
	aep->a_valuelen = valuelen;
	memcpy(aep->a_name, name, namelen);
	aep->a_name[namelen] = 0;
	alist->al_offset[context->count++] = context->firstu;
	alist->al_count = context->count;
	trace_xfs_attr_list_add(context);
	return 0;
}

/*
 * Generate a list of extended attribute names and optionally
 * also value lengths.  Positive return value follows the XFS
 * convention of being an error, zero or negative return code
 * is the length of the buffer returned (negated), indicating
 * success.
 */
int
xfs_attr_list(
	xfs_inode_t	*dp,
	char		*buffer,
	int		bufsize,
	int		flags,
	attrlist_cursor_kern_t *cursor)
{
	xfs_attr_list_context_t context;
	struct attrlist *alist;
	int error;

	/*
	 * Validate the cursor.
	 */
	if (cursor->pad1 || cursor->pad2)
		return(XFS_ERROR(EINVAL));
	if ((cursor->initted == 0) &&
	    (cursor->hashval || cursor->blkno || cursor->offset))
		return XFS_ERROR(EINVAL);

	/*
	 * Check for a properly aligned buffer.
	 */
	if (((long)buffer) & (sizeof(int)-1))
		return XFS_ERROR(EFAULT);
	if (flags & ATTR_KERNOVAL)
		bufsize = 0;

	/*
	 * Initialize the output buffer.
	 */
	memset(&context, 0, sizeof(context));
	context.dp = dp;
	context.cursor = cursor;
	context.resynch = 1;
	context.flags = flags;
	context.alist = buffer;
	context.bufsize = (bufsize & ~(sizeof(int)-1));  /* align */
	context.firstu = context.bufsize;
	context.put_listent = xfs_attr_put_listent;

	alist = (struct attrlist *)context.alist;
	alist->al_count = 0;
	alist->al_more = 0;
	alist->al_offset[0] = context.bufsize;

	error = xfs_attr_list_int(&context);
	ASSERT(error >= 0);
	return error;
}

int								/* error */
xfs_attr_inactive(xfs_inode_t *dp)
{
	xfs_trans_t *trans;
	xfs_mount_t *mp;
	int error;

	mp = dp->i_mount;
	ASSERT(! XFS_NOT_DQATTACHED(mp, dp));

	xfs_ilock(dp, XFS_ILOCK_SHARED);
	if (!xfs_inode_hasattr(dp) ||
	    dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		xfs_iunlock(dp, XFS_ILOCK_SHARED);
		return 0;
	}
	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	/*
	 * Start our first transaction of the day.
	 *
	 * All future transactions during this code must be "chained" off
	 * this one via the trans_dup() call.  All transactions will contain
	 * the inode, and the inode will always be marked with trans_ihold().
	 * Since the inode will be locked in all transactions, we must log
	 * the inode in every transaction to let it float upward through
	 * the log.
	 */
	trans = xfs_trans_alloc(mp, XFS_TRANS_ATTRINVAL);
	if ((error = xfs_trans_reserve(trans, 0, XFS_ATTRINVAL_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_ATTRINVAL_LOG_COUNT))) {
		xfs_trans_cancel(trans, 0);
		return(error);
	}
	xfs_ilock(dp, XFS_ILOCK_EXCL);

	/*
	 * No need to make quota reservations here. We expect to release some
	 * blocks, not allocate, in the common case.
	 */
	xfs_trans_ijoin(trans, dp);

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (!xfs_inode_hasattr(dp) ||
	    dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		error = 0;
		goto out;
	}
	error = xfs_attr_root_inactive(&trans, dp);
	if (error)
		goto out;
	/*
	 * signal synchronous inactive transactions unless this
	 * is a synchronous mount filesystem in which case we
	 * know that we're here because we've been called out of
	 * xfs_inactive which means that the last reference is gone
	 * and the unlink transaction has already hit the disk so
	 * async inactive transactions are safe.
	 */
	if ((error = xfs_itruncate_finish(&trans, dp, 0LL, XFS_ATTR_FORK,
				(!(mp->m_flags & XFS_MOUNT_WSYNC)
				 ? 1 : 0))))
		goto out;

	/*
	 * Commit the last in the sequence of transactions.
	 */
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);
	error = xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);

	return(error);

out:
	xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES|XFS_TRANS_ABORT);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
	return(error);
}



/*========================================================================
 * External routines when attribute list is inside the inode
 *========================================================================*/

/*
 * Add a name to the shortform attribute list structure
 * This is the external routine.
 */
STATIC int
xfs_attr_shortform_addname(xfs_da_args_t *args)
{
	int newsize, forkoff, retval;

	retval = xfs_attr_shortform_lookup(args);
	if ((args->flags & ATTR_REPLACE) && (retval == ENOATTR)) {
		return(retval);
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE)
			return(retval);
		retval = xfs_attr_shortform_remove(args);
		ASSERT(retval == 0);
	}

	if (args->namelen >= XFS_ATTR_SF_ENTSIZE_MAX ||
	    args->valuelen >= XFS_ATTR_SF_ENTSIZE_MAX)
		return(XFS_ERROR(ENOSPC));

	newsize = XFS_ATTR_SF_TOTSIZE(args->dp);
	newsize += XFS_ATTR_SF_ENTSIZE_BYNAME(args->namelen, args->valuelen);

	forkoff = xfs_attr_shortform_bytesfit(args->dp, newsize);
	if (!forkoff)
		return(XFS_ERROR(ENOSPC));

	xfs_attr_shortform_add(args, forkoff);
	return(0);
}


/*========================================================================
 * External routines when attribute list is one block
 *========================================================================*/

/*
 * Add a name to the leaf attribute list structure
 *
 * This leaf block cannot have a "remote" value, we only call this routine
 * if bmap_one_block() says there is only one block (ie: no remote blks).
 */
STATIC int
xfs_attr_leaf_addname(xfs_da_args_t *args)
{
	xfs_inode_t *dp;
	xfs_dabuf_t *bp;
	int retval, error, committed, forkoff;

	/*
	 * Read the (only) block in the attribute list in.
	 */
	dp = args->dp;
	args->blkno = 0;
	error = xfs_da_read_buf(args->trans, args->dp, args->blkno, -1, &bp,
					     XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);

	/*
	 * Look up the given attribute in the leaf block.  Figure out if
	 * the given flags produce an error or call for an atomic rename.
	 */
	retval = xfs_attr_leaf_lookup_int(bp, args);
	if ((args->flags & ATTR_REPLACE) && (retval == ENOATTR)) {
		xfs_da_brelse(args->trans, bp);
		return(retval);
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE) {	/* pure create op */
			xfs_da_brelse(args->trans, bp);
			return(retval);
		}
		args->op_flags |= XFS_DA_OP_RENAME;	/* an atomic rename */
		args->blkno2 = args->blkno;		/* set 2nd entry info*/
		args->index2 = args->index;
		args->rmtblkno2 = args->rmtblkno;
		args->rmtblkcnt2 = args->rmtblkcnt;
	}

	/*
	 * Add the attribute to the leaf block, transitioning to a Btree
	 * if required.
	 */
	retval = xfs_attr_leaf_add(bp, args);
	xfs_da_buf_done(bp);
	if (retval == ENOSPC) {
		/*
		 * Promote the attribute list to the Btree format, then
		 * Commit that transaction so that the node_addname() call
		 * can manage its own transactions.
		 */
		xfs_bmap_init(args->flist, args->firstblock);
		error = xfs_attr_leaf_to_node(args);
		if (!error) {
			error = xfs_bmap_finish(&args->trans, args->flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args->trans = NULL;
			xfs_bmap_cancel(args->flist);
			return(error);
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args->trans, dp);

		/*
		 * Commit the current trans (including the inode) and start
		 * a new one.
		 */
		error = xfs_trans_roll(&args->trans, dp);
		if (error)
			return (error);

		/*
		 * Fob the whole rest of the problem off on the Btree code.
		 */
		error = xfs_attr_node_addname(args);
		return(error);
	}

	/*
	 * Commit the transaction that added the attr name so that
	 * later routines can manage their own transactions.
	 */
	error = xfs_trans_roll(&args->trans, dp);
	if (error)
		return (error);

	/*
	 * If there was an out-of-line value, allocate the blocks we
	 * identified for its storage and copy the value.  This is done
	 * after we create the attribute so that we don't overflow the
	 * maximum size of a transaction and/or hit a deadlock.
	 */
	if (args->rmtblkno > 0) {
		error = xfs_attr_rmtval_set(args);
		if (error)
			return(error);
	}

	/*
	 * If this is an atomic rename operation, we must "flip" the
	 * incomplete flags on the "new" and "old" attribute/value pairs
	 * so that one disappears and one appears atomically.  Then we
	 * must remove the "old" attribute/value pair.
	 */
	if (args->op_flags & XFS_DA_OP_RENAME) {
		/*
		 * In a separate transaction, set the incomplete flag on the
		 * "old" attr and clear the incomplete flag on the "new" attr.
		 */
		error = xfs_attr_leaf_flipflags(args);
		if (error)
			return(error);

		/*
		 * Dismantle the "old" attribute/value pair by removing
		 * a "remote" value (if it exists).
		 */
		args->index = args->index2;
		args->blkno = args->blkno2;
		args->rmtblkno = args->rmtblkno2;
		args->rmtblkcnt = args->rmtblkcnt2;
		if (args->rmtblkno) {
			error = xfs_attr_rmtval_remove(args);
			if (error)
				return(error);
		}

		/*
		 * Read in the block containing the "old" attr, then
		 * remove the "old" attr from that block (neat, huh!)
		 */
		error = xfs_da_read_buf(args->trans, args->dp, args->blkno, -1,
						     &bp, XFS_ATTR_FORK);
		if (error)
			return(error);
		ASSERT(bp != NULL);
		(void)xfs_attr_leaf_remove(bp, args);

		/*
		 * If the result is small enough, shrink it all into the inode.
		 */
		if ((forkoff = xfs_attr_shortform_allfit(bp, dp))) {
			xfs_bmap_init(args->flist, args->firstblock);
			error = xfs_attr_leaf_to_shortform(bp, args, forkoff);
			/* bp is gone due to xfs_da_shrink_inode */
			if (!error) {
				error = xfs_bmap_finish(&args->trans,
							args->flist,
							&committed);
			}
			if (error) {
				ASSERT(committed);
				args->trans = NULL;
				xfs_bmap_cancel(args->flist);
				return(error);
			}

			/*
			 * bmap_finish() may have committed the last trans
			 * and started a new one.  We need the inode to be
			 * in all transactions.
			 */
			if (committed)
				xfs_trans_ijoin(args->trans, dp);
		} else
			xfs_da_buf_done(bp);

		/*
		 * Commit the remove and start the next trans in series.
		 */
		error = xfs_trans_roll(&args->trans, dp);

	} else if (args->rmtblkno > 0) {
		/*
		 * Added a "remote" value, just clear the incomplete flag.
		 */
		error = xfs_attr_leaf_clearflag(args);
	}
	return(error);
}

/*
 * Remove a name from the leaf attribute list structure
 *
 * This leaf block cannot have a "remote" value, we only call this routine
 * if bmap_one_block() says there is only one block (ie: no remote blks).
 */
STATIC int
xfs_attr_leaf_removename(xfs_da_args_t *args)
{
	xfs_inode_t *dp;
	xfs_dabuf_t *bp;
	int error, committed, forkoff;

	/*
	 * Remove the attribute.
	 */
	dp = args->dp;
	args->blkno = 0;
	error = xfs_da_read_buf(args->trans, args->dp, args->blkno, -1, &bp,
					     XFS_ATTR_FORK);
	if (error) {
		return(error);
	}

	ASSERT(bp != NULL);
	error = xfs_attr_leaf_lookup_int(bp, args);
	if (error == ENOATTR) {
		xfs_da_brelse(args->trans, bp);
		return(error);
	}

	(void)xfs_attr_leaf_remove(bp, args);

	/*
	 * If the result is small enough, shrink it all into the inode.
	 */
	if ((forkoff = xfs_attr_shortform_allfit(bp, dp))) {
		xfs_bmap_init(args->flist, args->firstblock);
		error = xfs_attr_leaf_to_shortform(bp, args, forkoff);
		/* bp is gone due to xfs_da_shrink_inode */
		if (!error) {
			error = xfs_bmap_finish(&args->trans, args->flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args->trans = NULL;
			xfs_bmap_cancel(args->flist);
			return(error);
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args->trans, dp);
	} else
		xfs_da_buf_done(bp);
	return(0);
}

/*
 * Look up a name in a leaf attribute list structure.
 *
 * This leaf block cannot have a "remote" value, we only call this routine
 * if bmap_one_block() says there is only one block (ie: no remote blks).
 */
STATIC int
xfs_attr_leaf_get(xfs_da_args_t *args)
{
	xfs_dabuf_t *bp;
	int error;

	args->blkno = 0;
	error = xfs_da_read_buf(args->trans, args->dp, args->blkno, -1, &bp,
					     XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);

	error = xfs_attr_leaf_lookup_int(bp, args);
	if (error != EEXIST)  {
		xfs_da_brelse(args->trans, bp);
		return(error);
	}
	error = xfs_attr_leaf_getvalue(bp, args);
	xfs_da_brelse(args->trans, bp);
	if (!error && (args->rmtblkno > 0) && !(args->flags & ATTR_KERNOVAL)) {
		error = xfs_attr_rmtval_get(args);
	}
	return(error);
}

/*
 * Copy out attribute entries for attr_list(), for leaf attribute lists.
 */
STATIC int
xfs_attr_leaf_list(xfs_attr_list_context_t *context)
{
	xfs_attr_leafblock_t *leaf;
	int error;
	xfs_dabuf_t *bp;

	context->cursor->blkno = 0;
	error = xfs_da_read_buf(NULL, context->dp, 0, -1, &bp, XFS_ATTR_FORK);
	if (error)
		return XFS_ERROR(error);
	ASSERT(bp != NULL);
	leaf = bp->data;
	if (unlikely(be16_to_cpu(leaf->hdr.info.magic) != XFS_ATTR_LEAF_MAGIC)) {
		XFS_CORRUPTION_ERROR("xfs_attr_leaf_list", XFS_ERRLEVEL_LOW,
				     context->dp->i_mount, leaf);
		xfs_da_brelse(NULL, bp);
		return XFS_ERROR(EFSCORRUPTED);
	}

	error = xfs_attr_leaf_list_int(bp, context);
	xfs_da_brelse(NULL, bp);
	return XFS_ERROR(error);
}


/*========================================================================
 * External routines when attribute list size > XFS_LBSIZE(mp).
 *========================================================================*/

/*
 * Add a name to a Btree-format attribute list.
 *
 * This will involve walking down the Btree, and may involve splitting
 * leaf nodes and even splitting intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 *
 * "Remote" attribute values confuse the issue and atomic rename operations
 * add a whole extra layer of confusion on top of that.
 */
STATIC int
xfs_attr_node_addname(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	xfs_inode_t *dp;
	xfs_mount_t *mp;
	int committed, retval, error;

	/*
	 * Fill in bucket of arguments/results/context to carry around.
	 */
	dp = args->dp;
	mp = dp->i_mount;
restart:
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = mp;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	state->node_ents = state->mp->m_attr_node_ents;

	/*
	 * Search to see if name already exists, and get back a pointer
	 * to where it should go.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error)
		goto out;
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
	if ((args->flags & ATTR_REPLACE) && (retval == ENOATTR)) {
		goto out;
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE)
			goto out;
		args->op_flags |= XFS_DA_OP_RENAME;	/* atomic rename op */
		args->blkno2 = args->blkno;		/* set 2nd entry info*/
		args->index2 = args->index;
		args->rmtblkno2 = args->rmtblkno;
		args->rmtblkcnt2 = args->rmtblkcnt;
		args->rmtblkno = 0;
		args->rmtblkcnt = 0;
	}

	retval = xfs_attr_leaf_add(blk->bp, state->args);
	if (retval == ENOSPC) {
		if (state->path.active == 1) {
			/*
			 * Its really a single leaf node, but it had
			 * out-of-line values so it looked like it *might*
			 * have been a b-tree.
			 */
			xfs_da_state_free(state);
			xfs_bmap_init(args->flist, args->firstblock);
			error = xfs_attr_leaf_to_node(args);
			if (!error) {
				error = xfs_bmap_finish(&args->trans,
							args->flist,
							&committed);
			}
			if (error) {
				ASSERT(committed);
				args->trans = NULL;
				xfs_bmap_cancel(args->flist);
				goto out;
			}

			/*
			 * bmap_finish() may have committed the last trans
			 * and started a new one.  We need the inode to be
			 * in all transactions.
			 */
			if (committed)
				xfs_trans_ijoin(args->trans, dp);

			/*
			 * Commit the node conversion and start the next
			 * trans in the chain.
			 */
			error = xfs_trans_roll(&args->trans, dp);
			if (error)
				goto out;

			goto restart;
		}

		/*
		 * Split as many Btree elements as required.
		 * This code tracks the new and old attr's location
		 * in the index/blkno/rmtblkno/rmtblkcnt fields and
		 * in the index2/blkno2/rmtblkno2/rmtblkcnt2 fields.
		 */
		xfs_bmap_init(args->flist, args->firstblock);
		error = xfs_da_split(state);
		if (!error) {
			error = xfs_bmap_finish(&args->trans, args->flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args->trans = NULL;
			xfs_bmap_cancel(args->flist);
			goto out;
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args->trans, dp);
	} else {
		/*
		 * Addition succeeded, update Btree hashvals.
		 */
		xfs_da_fixhashpath(state, &state->path);
	}

	/*
	 * Kill the state structure, we're done with it and need to
	 * allow the buffers to come back later.
	 */
	xfs_da_state_free(state);
	state = NULL;

	/*
	 * Commit the leaf addition or btree split and start the next
	 * trans in the chain.
	 */
	error = xfs_trans_roll(&args->trans, dp);
	if (error)
		goto out;

	/*
	 * If there was an out-of-line value, allocate the blocks we
	 * identified for its storage and copy the value.  This is done
	 * after we create the attribute so that we don't overflow the
	 * maximum size of a transaction and/or hit a deadlock.
	 */
	if (args->rmtblkno > 0) {
		error = xfs_attr_rmtval_set(args);
		if (error)
			return(error);
	}

	/*
	 * If this is an atomic rename operation, we must "flip" the
	 * incomplete flags on the "new" and "old" attribute/value pairs
	 * so that one disappears and one appears atomically.  Then we
	 * must remove the "old" attribute/value pair.
	 */
	if (args->op_flags & XFS_DA_OP_RENAME) {
		/*
		 * In a separate transaction, set the incomplete flag on the
		 * "old" attr and clear the incomplete flag on the "new" attr.
		 */
		error = xfs_attr_leaf_flipflags(args);
		if (error)
			goto out;

		/*
		 * Dismantle the "old" attribute/value pair by removing
		 * a "remote" value (if it exists).
		 */
		args->index = args->index2;
		args->blkno = args->blkno2;
		args->rmtblkno = args->rmtblkno2;
		args->rmtblkcnt = args->rmtblkcnt2;
		if (args->rmtblkno) {
			error = xfs_attr_rmtval_remove(args);
			if (error)
				return(error);
		}

		/*
		 * Re-find the "old" attribute entry after any split ops.
		 * The INCOMPLETE flag means that we will find the "old"
		 * attr, not the "new" one.
		 */
		args->flags |= XFS_ATTR_INCOMPLETE;
		state = xfs_da_state_alloc();
		state->args = args;
		state->mp = mp;
		state->blocksize = state->mp->m_sb.sb_blocksize;
		state->node_ents = state->mp->m_attr_node_ents;
		state->inleaf = 0;
		error = xfs_da_node_lookup_int(state, &retval);
		if (error)
			goto out;

		/*
		 * Remove the name and update the hashvals in the tree.
		 */
		blk = &state->path.blk[ state->path.active-1 ];
		ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
		error = xfs_attr_leaf_remove(blk->bp, args);
		xfs_da_fixhashpath(state, &state->path);

		/*
		 * Check to see if the tree needs to be collapsed.
		 */
		if (retval && (state->path.active > 1)) {
			xfs_bmap_init(args->flist, args->firstblock);
			error = xfs_da_join(state);
			if (!error) {
				error = xfs_bmap_finish(&args->trans,
							args->flist,
							&committed);
			}
			if (error) {
				ASSERT(committed);
				args->trans = NULL;
				xfs_bmap_cancel(args->flist);
				goto out;
			}

			/*
			 * bmap_finish() may have committed the last trans
			 * and started a new one.  We need the inode to be
			 * in all transactions.
			 */
			if (committed)
				xfs_trans_ijoin(args->trans, dp);
		}

		/*
		 * Commit and start the next trans in the chain.
		 */
		error = xfs_trans_roll(&args->trans, dp);
		if (error)
			goto out;

	} else if (args->rmtblkno > 0) {
		/*
		 * Added a "remote" value, just clear the incomplete flag.
		 */
		error = xfs_attr_leaf_clearflag(args);
		if (error)
			goto out;
	}
	retval = error = 0;

out:
	if (state)
		xfs_da_state_free(state);
	if (error)
		return(error);
	return(retval);
}

/*
 * Remove a name from a B-tree attribute list.
 *
 * This will involve walking down the Btree, and may involve joining
 * leaf nodes and even joining intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_attr_node_removename(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	xfs_inode_t *dp;
	xfs_dabuf_t *bp;
	int retval, error, committed, forkoff;

	/*
	 * Tie a string around our finger to remind us where we are.
	 */
	dp = args->dp;
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = dp->i_mount;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	state->node_ents = state->mp->m_attr_node_ents;

	/*
	 * Search to see if name exists, and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error || (retval != EEXIST)) {
		if (error == 0)
			error = retval;
		goto out;
	}

	/*
	 * If there is an out-of-line value, de-allocate the blocks.
	 * This is done before we remove the attribute so that we don't
	 * overflow the maximum size of a transaction and/or hit a deadlock.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->bp != NULL);
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
	if (args->rmtblkno > 0) {
		/*
		 * Fill in disk block numbers in the state structure
		 * so that we can get the buffers back after we commit
		 * several transactions in the following calls.
		 */
		error = xfs_attr_fillstate(state);
		if (error)
			goto out;

		/*
		 * Mark the attribute as INCOMPLETE, then bunmapi() the
		 * remote value.
		 */
		error = xfs_attr_leaf_setflag(args);
		if (error)
			goto out;
		error = xfs_attr_rmtval_remove(args);
		if (error)
			goto out;

		/*
		 * Refill the state structure with buffers, the prior calls
		 * released our buffers.
		 */
		error = xfs_attr_refillstate(state);
		if (error)
			goto out;
	}

	/*
	 * Remove the name and update the hashvals in the tree.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
	retval = xfs_attr_leaf_remove(blk->bp, args);
	xfs_da_fixhashpath(state, &state->path);

	/*
	 * Check to see if the tree needs to be collapsed.
	 */
	if (retval && (state->path.active > 1)) {
		xfs_bmap_init(args->flist, args->firstblock);
		error = xfs_da_join(state);
		if (!error) {
			error = xfs_bmap_finish(&args->trans, args->flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args->trans = NULL;
			xfs_bmap_cancel(args->flist);
			goto out;
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args->trans, dp);

		/*
		 * Commit the Btree join operation and start a new trans.
		 */
		error = xfs_trans_roll(&args->trans, dp);
		if (error)
			goto out;
	}

	/*
	 * If the result is small enough, push it all into the inode.
	 */
	if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		/*
		 * Have to get rid of the copy of this dabuf in the state.
		 */
		ASSERT(state->path.active == 1);
		ASSERT(state->path.blk[0].bp);
		xfs_da_buf_done(state->path.blk[0].bp);
		state->path.blk[0].bp = NULL;

		error = xfs_da_read_buf(args->trans, args->dp, 0, -1, &bp,
						     XFS_ATTR_FORK);
		if (error)
			goto out;
		ASSERT(be16_to_cpu(((xfs_attr_leafblock_t *)
				      bp->data)->hdr.info.magic)
						       == XFS_ATTR_LEAF_MAGIC);

		if ((forkoff = xfs_attr_shortform_allfit(bp, dp))) {
			xfs_bmap_init(args->flist, args->firstblock);
			error = xfs_attr_leaf_to_shortform(bp, args, forkoff);
			/* bp is gone due to xfs_da_shrink_inode */
			if (!error) {
				error = xfs_bmap_finish(&args->trans,
							args->flist,
							&committed);
			}
			if (error) {
				ASSERT(committed);
				args->trans = NULL;
				xfs_bmap_cancel(args->flist);
				goto out;
			}

			/*
			 * bmap_finish() may have committed the last trans
			 * and started a new one.  We need the inode to be
			 * in all transactions.
			 */
			if (committed)
				xfs_trans_ijoin(args->trans, dp);
		} else
			xfs_da_brelse(args->trans, bp);
	}
	error = 0;

out:
	xfs_da_state_free(state);
	return(error);
}

/*
 * Fill in the disk block numbers in the state structure for the buffers
 * that are attached to the state structure.
 * This is done so that we can quickly reattach ourselves to those buffers
 * after some set of transaction commits have released these buffers.
 */
STATIC int
xfs_attr_fillstate(xfs_da_state_t *state)
{
	xfs_da_state_path_t *path;
	xfs_da_state_blk_t *blk;
	int level;

	/*
	 * Roll down the "path" in the state structure, storing the on-disk
	 * block number for those buffers in the "path".
	 */
	path = &state->path;
	ASSERT((path->active >= 0) && (path->active < XFS_DA_NODE_MAXDEPTH));
	for (blk = path->blk, level = 0; level < path->active; blk++, level++) {
		if (blk->bp) {
			blk->disk_blkno = xfs_da_blkno(blk->bp);
			xfs_da_buf_done(blk->bp);
			blk->bp = NULL;
		} else {
			blk->disk_blkno = 0;
		}
	}

	/*
	 * Roll down the "altpath" in the state structure, storing the on-disk
	 * block number for those buffers in the "altpath".
	 */
	path = &state->altpath;
	ASSERT((path->active >= 0) && (path->active < XFS_DA_NODE_MAXDEPTH));
	for (blk = path->blk, level = 0; level < path->active; blk++, level++) {
		if (blk->bp) {
			blk->disk_blkno = xfs_da_blkno(blk->bp);
			xfs_da_buf_done(blk->bp);
			blk->bp = NULL;
		} else {
			blk->disk_blkno = 0;
		}
	}

	return(0);
}

/*
 * Reattach the buffers to the state structure based on the disk block
 * numbers stored in the state structure.
 * This is done after some set of transaction commits have released those
 * buffers from our grip.
 */
STATIC int
xfs_attr_refillstate(xfs_da_state_t *state)
{
	xfs_da_state_path_t *path;
	xfs_da_state_blk_t *blk;
	int level, error;

	/*
	 * Roll down the "path" in the state structure, storing the on-disk
	 * block number for those buffers in the "path".
	 */
	path = &state->path;
	ASSERT((path->active >= 0) && (path->active < XFS_DA_NODE_MAXDEPTH));
	for (blk = path->blk, level = 0; level < path->active; blk++, level++) {
		if (blk->disk_blkno) {
			error = xfs_da_read_buf(state->args->trans,
						state->args->dp,
						blk->blkno, blk->disk_blkno,
						&blk->bp, XFS_ATTR_FORK);
			if (error)
				return(error);
		} else {
			blk->bp = NULL;
		}
	}

	/*
	 * Roll down the "altpath" in the state structure, storing the on-disk
	 * block number for those buffers in the "altpath".
	 */
	path = &state->altpath;
	ASSERT((path->active >= 0) && (path->active < XFS_DA_NODE_MAXDEPTH));
	for (blk = path->blk, level = 0; level < path->active; blk++, level++) {
		if (blk->disk_blkno) {
			error = xfs_da_read_buf(state->args->trans,
						state->args->dp,
						blk->blkno, blk->disk_blkno,
						&blk->bp, XFS_ATTR_FORK);
			if (error)
				return(error);
		} else {
			blk->bp = NULL;
		}
	}

	return(0);
}

/*
 * Look up a filename in a node attribute list.
 *
 * This routine gets called for any attribute fork that has more than one
 * block, ie: both true Btree attr lists and for single-leaf-blocks with
 * "remote" values taking up more blocks.
 */
STATIC int
xfs_attr_node_get(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	int error, retval;
	int i;

	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	state->node_ents = state->mp->m_attr_node_ents;

	/*
	 * Search to see if name exists, and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error) {
		retval = error;
	} else if (retval == EEXIST) {
		blk = &state->path.blk[ state->path.active-1 ];
		ASSERT(blk->bp != NULL);
		ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);

		/*
		 * Get the value, local or "remote"
		 */
		retval = xfs_attr_leaf_getvalue(blk->bp, args);
		if (!retval && (args->rmtblkno > 0)
		    && !(args->flags & ATTR_KERNOVAL)) {
			retval = xfs_attr_rmtval_get(args);
		}
	}

	/*
	 * If not in a transaction, we have to release all the buffers.
	 */
	for (i = 0; i < state->path.active; i++) {
		xfs_da_brelse(args->trans, state->path.blk[i].bp);
		state->path.blk[i].bp = NULL;
	}

	xfs_da_state_free(state);
	return(retval);
}

STATIC int							/* error */
xfs_attr_node_list(xfs_attr_list_context_t *context)
{
	attrlist_cursor_kern_t *cursor;
	xfs_attr_leafblock_t *leaf;
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	int error, i;
	xfs_dabuf_t *bp;

	cursor = context->cursor;
	cursor->initted = 1;

	/*
	 * Do all sorts of validation on the passed-in cursor structure.
	 * If anything is amiss, ignore the cursor and look up the hashval
	 * starting from the btree root.
	 */
	bp = NULL;
	if (cursor->blkno > 0) {
		error = xfs_da_read_buf(NULL, context->dp, cursor->blkno, -1,
					      &bp, XFS_ATTR_FORK);
		if ((error != 0) && (error != EFSCORRUPTED))
			return(error);
		if (bp) {
			node = bp->data;
			switch (be16_to_cpu(node->hdr.info.magic)) {
			case XFS_DA_NODE_MAGIC:
				trace_xfs_attr_list_wrong_blk(context);
				xfs_da_brelse(NULL, bp);
				bp = NULL;
				break;
			case XFS_ATTR_LEAF_MAGIC:
				leaf = bp->data;
				if (cursor->hashval > be32_to_cpu(leaf->entries[
				    be16_to_cpu(leaf->hdr.count)-1].hashval)) {
					trace_xfs_attr_list_wrong_blk(context);
					xfs_da_brelse(NULL, bp);
					bp = NULL;
				} else if (cursor->hashval <=
					     be32_to_cpu(leaf->entries[0].hashval)) {
					trace_xfs_attr_list_wrong_blk(context);
					xfs_da_brelse(NULL, bp);
					bp = NULL;
				}
				break;
			default:
				trace_xfs_attr_list_wrong_blk(context);
				xfs_da_brelse(NULL, bp);
				bp = NULL;
			}
		}
	}

	/*
	 * We did not find what we expected given the cursor's contents,
	 * so we start from the top and work down based on the hash value.
	 * Note that start of node block is same as start of leaf block.
	 */
	if (bp == NULL) {
		cursor->blkno = 0;
		for (;;) {
			error = xfs_da_read_buf(NULL, context->dp,
						      cursor->blkno, -1, &bp,
						      XFS_ATTR_FORK);
			if (error)
				return(error);
			if (unlikely(bp == NULL)) {
				XFS_ERROR_REPORT("xfs_attr_node_list(2)",
						 XFS_ERRLEVEL_LOW,
						 context->dp->i_mount);
				return(XFS_ERROR(EFSCORRUPTED));
			}
			node = bp->data;
			if (be16_to_cpu(node->hdr.info.magic)
							== XFS_ATTR_LEAF_MAGIC)
				break;
			if (unlikely(be16_to_cpu(node->hdr.info.magic)
							!= XFS_DA_NODE_MAGIC)) {
				XFS_CORRUPTION_ERROR("xfs_attr_node_list(3)",
						     XFS_ERRLEVEL_LOW,
						     context->dp->i_mount,
						     node);
				xfs_da_brelse(NULL, bp);
				return(XFS_ERROR(EFSCORRUPTED));
			}
			btree = node->btree;
			for (i = 0; i < be16_to_cpu(node->hdr.count);
								btree++, i++) {
				if (cursor->hashval
						<= be32_to_cpu(btree->hashval)) {
					cursor->blkno = be32_to_cpu(btree->before);
					trace_xfs_attr_list_node_descend(context,
									 btree);
					break;
				}
			}
			if (i == be16_to_cpu(node->hdr.count)) {
				xfs_da_brelse(NULL, bp);
				return(0);
			}
			xfs_da_brelse(NULL, bp);
		}
	}
	ASSERT(bp != NULL);

	/*
	 * Roll upward through the blocks, processing each leaf block in
	 * order.  As long as there is space in the result buffer, keep
	 * adding the information.
	 */
	for (;;) {
		leaf = bp->data;
		if (unlikely(be16_to_cpu(leaf->hdr.info.magic)
						!= XFS_ATTR_LEAF_MAGIC)) {
			XFS_CORRUPTION_ERROR("xfs_attr_node_list(4)",
					     XFS_ERRLEVEL_LOW,
					     context->dp->i_mount, leaf);
			xfs_da_brelse(NULL, bp);
			return(XFS_ERROR(EFSCORRUPTED));
		}
		error = xfs_attr_leaf_list_int(bp, context);
		if (error) {
			xfs_da_brelse(NULL, bp);
			return error;
		}
		if (context->seen_enough || leaf->hdr.info.forw == 0)
			break;
		cursor->blkno = be32_to_cpu(leaf->hdr.info.forw);
		xfs_da_brelse(NULL, bp);
		error = xfs_da_read_buf(NULL, context->dp, cursor->blkno, -1,
					      &bp, XFS_ATTR_FORK);
		if (error)
			return(error);
		if (unlikely((bp == NULL))) {
			XFS_ERROR_REPORT("xfs_attr_node_list(5)",
					 XFS_ERRLEVEL_LOW,
					 context->dp->i_mount);
			return(XFS_ERROR(EFSCORRUPTED));
		}
	}
	xfs_da_brelse(NULL, bp);
	return(0);
}


/*========================================================================
 * External routines for manipulating out-of-line attribute values.
 *========================================================================*/

/*
 * Read the value associated with an attribute from the out-of-line buffer
 * that we stored it in.
 */
int
xfs_attr_rmtval_get(xfs_da_args_t *args)
{
	xfs_bmbt_irec_t map[ATTR_RMTVALUE_MAPSIZE];
	xfs_mount_t *mp;
	xfs_daddr_t dblkno;
	void *dst;
	xfs_buf_t *bp;
	int nmap, error, tmp, valuelen, blkcnt, i;
	xfs_dablk_t lblkno;

	ASSERT(!(args->flags & ATTR_KERNOVAL));

	mp = args->dp->i_mount;
	dst = args->value;
	valuelen = args->valuelen;
	lblkno = args->rmtblkno;
	while (valuelen > 0) {
		nmap = ATTR_RMTVALUE_MAPSIZE;
		error = xfs_bmapi(args->trans, args->dp, (xfs_fileoff_t)lblkno,
				  args->rmtblkcnt,
				  XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				  NULL, 0, map, &nmap, NULL);
		if (error)
			return(error);
		ASSERT(nmap >= 1);

		for (i = 0; (i < nmap) && (valuelen > 0); i++) {
			ASSERT((map[i].br_startblock != DELAYSTARTBLOCK) &&
			       (map[i].br_startblock != HOLESTARTBLOCK));
			dblkno = XFS_FSB_TO_DADDR(mp, map[i].br_startblock);
			blkcnt = XFS_FSB_TO_BB(mp, map[i].br_blockcount);
			error = xfs_read_buf(mp, mp->m_ddev_targp, dblkno,
					     blkcnt, XBF_LOCK | XBF_DONT_BLOCK,
					     &bp);
			if (error)
				return(error);

			tmp = (valuelen < XFS_BUF_SIZE(bp))
				? valuelen : XFS_BUF_SIZE(bp);
			xfs_buf_iomove(bp, 0, tmp, dst, XBRW_READ);
			xfs_buf_relse(bp);
			dst += tmp;
			valuelen -= tmp;

			lblkno += map[i].br_blockcount;
		}
	}
	ASSERT(valuelen == 0);
	return(0);
}

/*
 * Write the value associated with an attribute into the out-of-line buffer
 * that we have defined for it.
 */
STATIC int
xfs_attr_rmtval_set(xfs_da_args_t *args)
{
	xfs_mount_t *mp;
	xfs_fileoff_t lfileoff;
	xfs_inode_t *dp;
	xfs_bmbt_irec_t map;
	xfs_daddr_t dblkno;
	void *src;
	xfs_buf_t *bp;
	xfs_dablk_t lblkno;
	int blkcnt, valuelen, nmap, error, tmp, committed;

	dp = args->dp;
	mp = dp->i_mount;
	src = args->value;

	/*
	 * Find a "hole" in the attribute address space large enough for
	 * us to drop the new attribute's value into.
	 */
	blkcnt = XFS_B_TO_FSB(mp, args->valuelen);
	lfileoff = 0;
	error = xfs_bmap_first_unused(args->trans, args->dp, blkcnt, &lfileoff,
						   XFS_ATTR_FORK);
	if (error) {
		return(error);
	}
	args->rmtblkno = lblkno = (xfs_dablk_t)lfileoff;
	args->rmtblkcnt = blkcnt;

	/*
	 * Roll through the "value", allocating blocks on disk as required.
	 */
	while (blkcnt > 0) {
		/*
		 * Allocate a single extent, up to the size of the value.
		 */
		xfs_bmap_init(args->flist, args->firstblock);
		nmap = 1;
		error = xfs_bmapi(args->trans, dp, (xfs_fileoff_t)lblkno,
				  blkcnt,
				  XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA |
							XFS_BMAPI_WRITE,
				  args->firstblock, args->total, &map, &nmap,
				  args->flist);
		if (!error) {
			error = xfs_bmap_finish(&args->trans, args->flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args->trans = NULL;
			xfs_bmap_cancel(args->flist);
			return(error);
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args->trans, dp);

		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));
		lblkno += map.br_blockcount;
		blkcnt -= map.br_blockcount;

		/*
		 * Start the next trans in the chain.
		 */
		error = xfs_trans_roll(&args->trans, dp);
		if (error)
			return (error);
	}

	/*
	 * Roll through the "value", copying the attribute value to the
	 * already-allocated blocks.  Blocks are written synchronously
	 * so that we can know they are all on disk before we turn off
	 * the INCOMPLETE flag.
	 */
	lblkno = args->rmtblkno;
	valuelen = args->valuelen;
	while (valuelen > 0) {
		/*
		 * Try to remember where we decided to put the value.
		 */
		xfs_bmap_init(args->flist, args->firstblock);
		nmap = 1;
		error = xfs_bmapi(NULL, dp, (xfs_fileoff_t)lblkno,
				  args->rmtblkcnt,
				  XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				  args->firstblock, 0, &map, &nmap,
				  NULL);
		if (error) {
			return(error);
		}
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		blkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);

		bp = xfs_buf_get(mp->m_ddev_targp, dblkno, blkcnt,
				 XBF_LOCK | XBF_DONT_BLOCK);
		ASSERT(bp);
		ASSERT(!XFS_BUF_GETERROR(bp));

		tmp = (valuelen < XFS_BUF_SIZE(bp)) ? valuelen :
							XFS_BUF_SIZE(bp);
		xfs_buf_iomove(bp, 0, tmp, src, XBRW_WRITE);
		if (tmp < XFS_BUF_SIZE(bp))
			xfs_buf_zero(bp, tmp, XFS_BUF_SIZE(bp) - tmp);
		if ((error = xfs_bwrite(mp, bp))) {/* GROT: NOTE: synchronous write */
			return (error);
		}
		src += tmp;
		valuelen -= tmp;

		lblkno += map.br_blockcount;
	}
	ASSERT(valuelen == 0);
	return(0);
}

/*
 * Remove the value associated with an attribute by deleting the
 * out-of-line buffer that it is stored on.
 */
STATIC int
xfs_attr_rmtval_remove(xfs_da_args_t *args)
{
	xfs_mount_t *mp;
	xfs_bmbt_irec_t map;
	xfs_buf_t *bp;
	xfs_daddr_t dblkno;
	xfs_dablk_t lblkno;
	int valuelen, blkcnt, nmap, error, done, committed;

	mp = args->dp->i_mount;

	/*
	 * Roll through the "value", invalidating the attribute value's
	 * blocks.
	 */
	lblkno = args->rmtblkno;
	valuelen = args->rmtblkcnt;
	while (valuelen > 0) {
		/*
		 * Try to remember where we decided to put the value.
		 */
		xfs_bmap_init(args->flist, args->firstblock);
		nmap = 1;
		error = xfs_bmapi(NULL, args->dp, (xfs_fileoff_t)lblkno,
					args->rmtblkcnt,
					XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
					args->firstblock, 0, &map, &nmap,
					args->flist);
		if (error) {
			return(error);
		}
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		blkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);

		/*
		 * If the "remote" value is in the cache, remove it.
		 */
		bp = xfs_incore(mp->m_ddev_targp, dblkno, blkcnt, XBF_TRYLOCK);
		if (bp) {
			XFS_BUF_STALE(bp);
			XFS_BUF_UNDELAYWRITE(bp);
			xfs_buf_relse(bp);
			bp = NULL;
		}

		valuelen -= map.br_blockcount;

		lblkno += map.br_blockcount;
	}

	/*
	 * Keep de-allocating extents until the remote-value region is gone.
	 */
	lblkno = args->rmtblkno;
	blkcnt = args->rmtblkcnt;
	done = 0;
	while (!done) {
		xfs_bmap_init(args->flist, args->firstblock);
		error = xfs_bunmapi(args->trans, args->dp, lblkno, blkcnt,
				    XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				    1, args->firstblock, args->flist,
				    &done);
		if (!error) {
			error = xfs_bmap_finish(&args->trans, args->flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args->trans = NULL;
			xfs_bmap_cancel(args->flist);
			return(error);
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args->trans, args->dp);

		/*
		 * Close out trans and start the next one in the chain.
		 */
		error = xfs_trans_roll(&args->trans, args->dp);
		if (error)
			return (error);
	}
	return(0);
}
