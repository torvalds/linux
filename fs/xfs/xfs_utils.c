/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_itable.h"
#include "xfs_utils.h"


/*
 * Allocates a new inode from disk and return a pointer to the
 * incore copy. This routine will internally commit the current
 * transaction and allocate a new one if the Space Manager needed
 * to do an allocation to replenish the inode free-list.
 *
 * This routine is designed to be called from xfs_create and
 * xfs_create_dir.
 *
 */
int
xfs_dir_ialloc(
	xfs_trans_t	**tpp,		/* input: current transaction;
					   output: may be a new transaction. */
	xfs_inode_t	*dp,		/* directory within whose allocate
					   the inode. */
	umode_t		mode,
	xfs_nlink_t	nlink,
	xfs_dev_t	rdev,
	prid_t		prid,		/* project id */
	int		okalloc,	/* ok to allocate new space */
	xfs_inode_t	**ipp,		/* pointer to inode; it will be
					   locked. */
	int		*committed)

{
	xfs_trans_t	*tp;
	xfs_trans_t	*ntp;
	xfs_inode_t	*ip;
	xfs_buf_t	*ialloc_context = NULL;
	int		code;
	uint		log_res;
	uint		log_count;
	void		*dqinfo;
	uint		tflags;

	tp = *tpp;
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);

	/*
	 * xfs_ialloc will return a pointer to an incore inode if
	 * the Space Manager has an available inode on the free
	 * list. Otherwise, it will do an allocation and replenish
	 * the freelist.  Since we can only do one allocation per
	 * transaction without deadlocks, we will need to commit the
	 * current transaction and start a new one.  We will then
	 * need to call xfs_ialloc again to get the inode.
	 *
	 * If xfs_ialloc did an allocation to replenish the freelist,
	 * it returns the bp containing the head of the freelist as
	 * ialloc_context. We will hold a lock on it across the
	 * transaction commit so that no other process can steal
	 * the inode(s) that we've just allocated.
	 */
	code = xfs_ialloc(tp, dp, mode, nlink, rdev, prid, okalloc,
			  &ialloc_context, &ip);

	/*
	 * Return an error if we were unable to allocate a new inode.
	 * This should only happen if we run out of space on disk or
	 * encounter a disk error.
	 */
	if (code) {
		*ipp = NULL;
		return code;
	}
	if (!ialloc_context && !ip) {
		*ipp = NULL;
		return XFS_ERROR(ENOSPC);
	}

	/*
	 * If the AGI buffer is non-NULL, then we were unable to get an
	 * inode in one operation.  We need to commit the current
	 * transaction and call xfs_ialloc() again.  It is guaranteed
	 * to succeed the second time.
	 */
	if (ialloc_context) {
		/*
		 * Normally, xfs_trans_commit releases all the locks.
		 * We call bhold to hang on to the ialloc_context across
		 * the commit.  Holding this buffer prevents any other
		 * processes from doing any allocations in this
		 * allocation group.
		 */
		xfs_trans_bhold(tp, ialloc_context);
		/*
		 * Save the log reservation so we can use
		 * them in the next transaction.
		 */
		log_res = xfs_trans_get_log_res(tp);
		log_count = xfs_trans_get_log_count(tp);

		/*
		 * We want the quota changes to be associated with the next
		 * transaction, NOT this one. So, detach the dqinfo from this
		 * and attach it to the next transaction.
		 */
		dqinfo = NULL;
		tflags = 0;
		if (tp->t_dqinfo) {
			dqinfo = (void *)tp->t_dqinfo;
			tp->t_dqinfo = NULL;
			tflags = tp->t_flags & XFS_TRANS_DQ_DIRTY;
			tp->t_flags &= ~(XFS_TRANS_DQ_DIRTY);
		}

		ntp = xfs_trans_dup(tp);
		code = xfs_trans_commit(tp, 0);
		tp = ntp;
		if (committed != NULL) {
			*committed = 1;
		}
		/*
		 * If we get an error during the commit processing,
		 * release the buffer that is still held and return
		 * to the caller.
		 */
		if (code) {
			xfs_buf_relse(ialloc_context);
			if (dqinfo) {
				tp->t_dqinfo = dqinfo;
				xfs_trans_free_dqinfo(tp);
			}
			*tpp = ntp;
			*ipp = NULL;
			return code;
		}

		/*
		 * transaction commit worked ok so we can drop the extra ticket
		 * reference that we gained in xfs_trans_dup()
		 */
		xfs_log_ticket_put(tp->t_ticket);
		code = xfs_trans_reserve(tp, 0, log_res, 0,
					 XFS_TRANS_PERM_LOG_RES, log_count);
		/*
		 * Re-attach the quota info that we detached from prev trx.
		 */
		if (dqinfo) {
			tp->t_dqinfo = dqinfo;
			tp->t_flags |= tflags;
		}

		if (code) {
			xfs_buf_relse(ialloc_context);
			*tpp = ntp;
			*ipp = NULL;
			return code;
		}
		xfs_trans_bjoin(tp, ialloc_context);

		/*
		 * Call ialloc again. Since we've locked out all
		 * other allocations in this allocation group,
		 * this call should always succeed.
		 */
		code = xfs_ialloc(tp, dp, mode, nlink, rdev, prid,
				  okalloc, &ialloc_context, &ip);

		/*
		 * If we get an error at this point, return to the caller
		 * so that the current transaction can be aborted.
		 */
		if (code) {
			*tpp = tp;
			*ipp = NULL;
			return code;
		}
		ASSERT(!ialloc_context && ip);

	} else {
		if (committed != NULL)
			*committed = 0;
	}

	*ipp = ip;
	*tpp = tp;

	return 0;
}

/*
 * Decrement the link count on an inode & log the change.
 * If this causes the link count to go to zero, initiate the
 * logging activity required to truncate a file.
 */
int				/* error */
xfs_droplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	int	error;

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	ASSERT (ip->i_d.di_nlink > 0);
	ip->i_d.di_nlink--;
	drop_nlink(VFS_I(ip));
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = 0;
	if (ip->i_d.di_nlink == 0) {
		/*
		 * We're dropping the last link to this file.
		 * Move the on-disk inode to the AGI unlinked list.
		 * From xfs_inactive() we will pull the inode from
		 * the list and free it.
		 */
		error = xfs_iunlink(tp, ip);
	}
	return error;
}

/*
 * This gets called when the inode's version needs to be changed from 1 to 2.
 * Currently this happens when the nlink field overflows the old 16-bit value
 * or when chproj is called to change the project for the first time.
 * As a side effect the superblock version will also get rev'd
 * to contain the NLINK bit.
 */
void
xfs_bump_ino_vers2(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(ip->i_d.di_version == 1);

	ip->i_d.di_version = 2;
	ip->i_d.di_onlink = 0;
	memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));
	mp = tp->t_mountp;
	if (!xfs_sb_version_hasnlink(&mp->m_sb)) {
		spin_lock(&mp->m_sb_lock);
		if (!xfs_sb_version_hasnlink(&mp->m_sb)) {
			xfs_sb_version_addnlink(&mp->m_sb);
			spin_unlock(&mp->m_sb_lock);
			xfs_mod_sb(tp, XFS_SB_VERSIONNUM);
		} else {
			spin_unlock(&mp->m_sb_lock);
		}
	}
	/* Caller must log the inode */
}

/*
 * Increment the link count on an inode & log the change.
 */
int
xfs_bumplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	ASSERT(ip->i_d.di_nlink > 0);
	ip->i_d.di_nlink++;
	inc_nlink(VFS_I(ip));
	if ((ip->i_d.di_version == 1) &&
	    (ip->i_d.di_nlink > XFS_MAXLINK_1)) {
		/*
		 * The inode has increased its number of links beyond
		 * what can fit in an old format inode.  It now needs
		 * to be converted to a version 2 inode with a 32 bit
		 * link count.  If this is the first inode in the file
		 * system to do this, then we need to bump the superblock
		 * version number as well.
		 */
		xfs_bump_ino_vers2(tp, ip);
	}

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return 0;
}
