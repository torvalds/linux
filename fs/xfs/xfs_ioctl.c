// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_rtalloc.h"
#include "xfs_iwalk.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_fsops.h"
#include "xfs_discard.h"
#include "xfs_quota.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_trans.h"
#include "xfs_btree.h"
#include <linux/fsmap.h>
#include "xfs_fsmap.h"
#include "scrub/xfs_scrub.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_health.h"
#include "xfs_reflink.h"
#include "xfs_ioctl.h"
#include "xfs_xattr.h"
#include "xfs_rtbitmap.h"
#include "xfs_file.h"
#include "xfs_exchrange.h"
#include "xfs_handle.h"

#include <linux/mount.h>
#include <linux/fileattr.h>

/* Return 0 on success or positive error */
int
xfs_fsbulkstat_one_fmt(
	struct xfs_ibulk		*breq,
	const struct xfs_bulkstat	*bstat)
{
	struct xfs_bstat		bs1;

	xfs_bulkstat_to_bstat(breq->mp, &bs1, bstat);
	if (copy_to_user(breq->ubuffer, &bs1, sizeof(bs1)))
		return -EFAULT;
	return xfs_ibulk_advance(breq, sizeof(struct xfs_bstat));
}

int
xfs_fsinumbers_fmt(
	struct xfs_ibulk		*breq,
	const struct xfs_inumbers	*igrp)
{
	struct xfs_inogrp		ig1;

	xfs_inumbers_to_inogrp(&ig1, igrp);
	if (copy_to_user(breq->ubuffer, &ig1, sizeof(struct xfs_inogrp)))
		return -EFAULT;
	return xfs_ibulk_advance(breq, sizeof(struct xfs_inogrp));
}

STATIC int
xfs_ioc_fsbulkstat(
	struct file		*file,
	unsigned int		cmd,
	void			__user *arg)
{
	struct xfs_mount	*mp = XFS_I(file_inode(file))->i_mount;
	struct xfs_fsop_bulkreq	bulkreq;
	struct xfs_ibulk	breq = {
		.mp		= mp,
		.idmap		= file_mnt_idmap(file),
		.ocount		= 0,
	};
	xfs_ino_t		lastino;
	int			error;

	/* done = 1 if there are more stats to get and if bulkstat */
	/* should be called again (unused here, but used in dmapi) */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (copy_from_user(&bulkreq, arg, sizeof(struct xfs_fsop_bulkreq)))
		return -EFAULT;

	if (copy_from_user(&lastino, bulkreq.lastip, sizeof(__s64)))
		return -EFAULT;

	if (bulkreq.icount <= 0)
		return -EINVAL;

	if (bulkreq.ubuffer == NULL)
		return -EINVAL;

	breq.ubuffer = bulkreq.ubuffer;
	breq.icount = bulkreq.icount;

	/*
	 * FSBULKSTAT_SINGLE expects that *lastip contains the inode number
	 * that we want to stat.  However, FSINUMBERS and FSBULKSTAT expect
	 * that *lastip contains either zero or the number of the last inode to
	 * be examined by the previous call and return results starting with
	 * the next inode after that.  The new bulk request back end functions
	 * take the inode to start with, so we have to compute the startino
	 * parameter from lastino to maintain correct function.  lastino == 0
	 * is a special case because it has traditionally meant "first inode
	 * in filesystem".
	 */
	if (cmd == XFS_IOC_FSINUMBERS) {
		breq.startino = lastino ? lastino + 1 : 0;
		error = xfs_inumbers(&breq, xfs_fsinumbers_fmt);
		lastino = breq.startino - 1;
	} else if (cmd == XFS_IOC_FSBULKSTAT_SINGLE) {
		breq.startino = lastino;
		breq.icount = 1;
		error = xfs_bulkstat_one(&breq, xfs_fsbulkstat_one_fmt);
	} else {	/* XFS_IOC_FSBULKSTAT */
		breq.startino = lastino ? lastino + 1 : 0;
		error = xfs_bulkstat(&breq, xfs_fsbulkstat_one_fmt);
		lastino = breq.startino - 1;
	}

	if (error)
		return error;

	if (bulkreq.lastip != NULL &&
	    copy_to_user(bulkreq.lastip, &lastino, sizeof(xfs_ino_t)))
		return -EFAULT;

	if (bulkreq.ocount != NULL &&
	    copy_to_user(bulkreq.ocount, &breq.ocount, sizeof(__s32)))
		return -EFAULT;

	return 0;
}

/* Return 0 on success or positive error */
static int
xfs_bulkstat_fmt(
	struct xfs_ibulk		*breq,
	const struct xfs_bulkstat	*bstat)
{
	if (copy_to_user(breq->ubuffer, bstat, sizeof(struct xfs_bulkstat)))
		return -EFAULT;
	return xfs_ibulk_advance(breq, sizeof(struct xfs_bulkstat));
}

/*
 * Check the incoming bulk request @hdr from userspace and initialize the
 * internal @breq bulk request appropriately.  Returns 0 if the bulk request
 * should proceed; -ECANCELED if there's nothing to do; or the usual
 * negative error code.
 */
static int
xfs_bulk_ireq_setup(
	struct xfs_mount	*mp,
	const struct xfs_bulk_ireq *hdr,
	struct xfs_ibulk	*breq,
	void __user		*ubuffer)
{
	if (hdr->icount == 0 ||
	    (hdr->flags & ~XFS_BULK_IREQ_FLAGS_ALL) ||
	    memchr_inv(hdr->reserved, 0, sizeof(hdr->reserved)))
		return -EINVAL;

	breq->startino = hdr->ino;
	breq->ubuffer = ubuffer;
	breq->icount = hdr->icount;
	breq->ocount = 0;
	breq->flags = 0;

	/*
	 * The @ino parameter is a special value, so we must look it up here.
	 * We're not allowed to have IREQ_AGNO, and we only return one inode
	 * worth of data.
	 */
	if (hdr->flags & XFS_BULK_IREQ_SPECIAL) {
		if (hdr->flags & XFS_BULK_IREQ_AGNO)
			return -EINVAL;

		switch (hdr->ino) {
		case XFS_BULK_IREQ_SPECIAL_ROOT:
			breq->startino = mp->m_sb.sb_rootino;
			break;
		default:
			return -EINVAL;
		}
		breq->icount = 1;
	}

	/*
	 * The IREQ_AGNO flag means that we only want results from a given AG.
	 * If @hdr->ino is zero, we start iterating in that AG.  If @hdr->ino is
	 * beyond the specified AG then we return no results.
	 */
	if (hdr->flags & XFS_BULK_IREQ_AGNO) {
		if (hdr->agno >= mp->m_sb.sb_agcount)
			return -EINVAL;

		if (breq->startino == 0)
			breq->startino = XFS_AGINO_TO_INO(mp, hdr->agno, 0);
		else if (XFS_INO_TO_AGNO(mp, breq->startino) < hdr->agno)
			return -EINVAL;

		breq->flags |= XFS_IBULK_SAME_AG;

		/* Asking for an inode past the end of the AG?  We're done! */
		if (XFS_INO_TO_AGNO(mp, breq->startino) > hdr->agno)
			return -ECANCELED;
	} else if (hdr->agno)
		return -EINVAL;

	/* Asking for an inode past the end of the FS?  We're done! */
	if (XFS_INO_TO_AGNO(mp, breq->startino) >= mp->m_sb.sb_agcount)
		return -ECANCELED;

	if (hdr->flags & XFS_BULK_IREQ_NREXT64)
		breq->flags |= XFS_IBULK_NREXT64;

	return 0;
}

/*
 * Update the userspace bulk request @hdr to reflect the end state of the
 * internal bulk request @breq.
 */
static void
xfs_bulk_ireq_teardown(
	struct xfs_bulk_ireq	*hdr,
	struct xfs_ibulk	*breq)
{
	hdr->ino = breq->startino;
	hdr->ocount = breq->ocount;
}

/* Handle the v5 bulkstat ioctl. */
STATIC int
xfs_ioc_bulkstat(
	struct file			*file,
	unsigned int			cmd,
	struct xfs_bulkstat_req __user	*arg)
{
	struct xfs_mount		*mp = XFS_I(file_inode(file))->i_mount;
	struct xfs_bulk_ireq		hdr;
	struct xfs_ibulk		breq = {
		.mp			= mp,
		.idmap			= file_mnt_idmap(file),
	};
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (copy_from_user(&hdr, &arg->hdr, sizeof(hdr)))
		return -EFAULT;

	error = xfs_bulk_ireq_setup(mp, &hdr, &breq, arg->bulkstat);
	if (error == -ECANCELED)
		goto out_teardown;
	if (error < 0)
		return error;

	error = xfs_bulkstat(&breq, xfs_bulkstat_fmt);
	if (error)
		return error;

out_teardown:
	xfs_bulk_ireq_teardown(&hdr, &breq);
	if (copy_to_user(&arg->hdr, &hdr, sizeof(hdr)))
		return -EFAULT;

	return 0;
}

STATIC int
xfs_inumbers_fmt(
	struct xfs_ibulk		*breq,
	const struct xfs_inumbers	*igrp)
{
	if (copy_to_user(breq->ubuffer, igrp, sizeof(struct xfs_inumbers)))
		return -EFAULT;
	return xfs_ibulk_advance(breq, sizeof(struct xfs_inumbers));
}

/* Handle the v5 inumbers ioctl. */
STATIC int
xfs_ioc_inumbers(
	struct xfs_mount		*mp,
	unsigned int			cmd,
	struct xfs_inumbers_req __user	*arg)
{
	struct xfs_bulk_ireq		hdr;
	struct xfs_ibulk		breq = {
		.mp			= mp,
	};
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (copy_from_user(&hdr, &arg->hdr, sizeof(hdr)))
		return -EFAULT;

	error = xfs_bulk_ireq_setup(mp, &hdr, &breq, arg->inumbers);
	if (error == -ECANCELED)
		goto out_teardown;
	if (error < 0)
		return error;

	error = xfs_inumbers(&breq, xfs_inumbers_fmt);
	if (error)
		return error;

out_teardown:
	xfs_bulk_ireq_teardown(&hdr, &breq);
	if (copy_to_user(&arg->hdr, &hdr, sizeof(hdr)))
		return -EFAULT;

	return 0;
}

STATIC int
xfs_ioc_fsgeometry(
	struct xfs_mount	*mp,
	void			__user *arg,
	int			struct_version)
{
	struct xfs_fsop_geom	fsgeo;
	size_t			len;

	xfs_fs_geometry(mp, &fsgeo, struct_version);

	if (struct_version <= 3)
		len = sizeof(struct xfs_fsop_geom_v1);
	else if (struct_version == 4)
		len = sizeof(struct xfs_fsop_geom_v4);
	else {
		xfs_fsop_geom_health(mp, &fsgeo);
		len = sizeof(fsgeo);
	}

	if (copy_to_user(arg, &fsgeo, len))
		return -EFAULT;
	return 0;
}

STATIC int
xfs_ioc_ag_geometry(
	struct xfs_mount	*mp,
	void			__user *arg)
{
	struct xfs_perag	*pag;
	struct xfs_ag_geometry	ageo;
	int			error;

	if (copy_from_user(&ageo, arg, sizeof(ageo)))
		return -EFAULT;
	if (ageo.ag_flags)
		return -EINVAL;
	if (memchr_inv(&ageo.ag_reserved, 0, sizeof(ageo.ag_reserved)))
		return -EINVAL;

	pag = xfs_perag_get(mp, ageo.ag_number);
	if (!pag)
		return -EINVAL;

	error = xfs_ag_get_geometry(pag, &ageo);
	xfs_perag_put(pag);
	if (error)
		return error;

	if (copy_to_user(arg, &ageo, sizeof(ageo)))
		return -EFAULT;
	return 0;
}

/*
 * Linux extended inode flags interface.
 */

static void
xfs_fill_fsxattr(
	struct xfs_inode	*ip,
	int			whichfork,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);

	fileattr_fill_xflags(fa, xfs_ip2xflags(ip));

	if (ip->i_diflags & XFS_DIFLAG_EXTSIZE) {
		fa->fsx_extsize = XFS_FSB_TO_B(mp, ip->i_extsize);
	} else if (ip->i_diflags & XFS_DIFLAG_EXTSZINHERIT) {
		/*
		 * Don't let a misaligned extent size hint on a directory
		 * escape to userspace if it won't pass the setattr checks
		 * later.
		 */
		if ((ip->i_diflags & XFS_DIFLAG_RTINHERIT) &&
		    xfs_extlen_to_rtxmod(mp, ip->i_extsize) > 0) {
			fa->fsx_xflags &= ~(FS_XFLAG_EXTSIZE |
					    FS_XFLAG_EXTSZINHERIT);
			fa->fsx_extsize = 0;
		} else {
			fa->fsx_extsize = XFS_FSB_TO_B(mp, ip->i_extsize);
		}
	}

	if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
		fa->fsx_cowextsize = XFS_FSB_TO_B(mp, ip->i_cowextsize);
	fa->fsx_projid = ip->i_projid;
	if (ifp && !xfs_need_iread_extents(ifp))
		fa->fsx_nextents = xfs_iext_count(ifp);
	else
		fa->fsx_nextents = xfs_ifork_nextents(ifp);
}

STATIC int
xfs_ioc_fsgetxattra(
	xfs_inode_t		*ip,
	void			__user *arg)
{
	struct fileattr		fa;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	xfs_fill_fsxattr(ip, XFS_ATTR_FORK, &fa);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	return copy_fsxattr_to_user(&fa, arg);
}

int
xfs_fileattr_get(
	struct dentry		*dentry,
	struct fileattr		*fa)
{
	struct xfs_inode	*ip = XFS_I(d_inode(dentry));

	if (d_is_special(dentry))
		return -ENOTTY;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	xfs_fill_fsxattr(ip, XFS_DATA_FORK, fa);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	return 0;
}

static int
xfs_ioctl_setattr_xflags(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	bool			rtflag = (fa->fsx_xflags & FS_XFLAG_REALTIME);
	uint64_t		i_flags2;

	if (rtflag != XFS_IS_REALTIME_INODE(ip)) {
		/* Can't change realtime flag if any extents are allocated. */
		if (xfs_inode_has_filedata(ip))
			return -EINVAL;

		/*
		 * If S_DAX is enabled on this file, we can only switch the
		 * device if both support fsdax.  We can't update S_DAX because
		 * there might be other threads walking down the access paths.
		 */
		if (IS_DAX(VFS_I(ip)) &&
		    (mp->m_ddev_targp->bt_daxdev == NULL ||
		     (mp->m_rtdev_targp &&
		      mp->m_rtdev_targp->bt_daxdev == NULL)))
			return -EINVAL;
	}

	if (rtflag) {
		/* If realtime flag is set then must have realtime device */
		if (mp->m_sb.sb_rblocks == 0 || mp->m_sb.sb_rextsize == 0 ||
		    xfs_extlen_to_rtxmod(mp, ip->i_extsize))
			return -EINVAL;

		/* Clear reflink if we are actually able to set the rt flag. */
		if (xfs_is_reflink_inode(ip))
			ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;
	}

	/* diflags2 only valid for v3 inodes. */
	i_flags2 = xfs_flags2diflags2(ip, fa->fsx_xflags);
	if (i_flags2 && !xfs_has_v3inodes(mp))
		return -EINVAL;

	ip->i_diflags = xfs_flags2diflags(ip, fa->fsx_xflags);
	ip->i_diflags2 = i_flags2;

	xfs_diflags_to_iflags(ip, false);

	/*
	 * Make the stable writes flag match that of the device the inode
	 * resides on when flipping the RT flag.
	 */
	if (rtflag != XFS_IS_REALTIME_INODE(ip) && S_ISREG(VFS_I(ip)->i_mode))
		xfs_update_stable_writes(ip);

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	XFS_STATS_INC(mp, xs_ig_attrchg);
	return 0;
}

static void
xfs_ioctl_setattr_prepare_dax(
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct inode            *inode = VFS_I(ip);

	if (S_ISDIR(inode->i_mode))
		return;

	if (xfs_has_dax_always(mp) || xfs_has_dax_never(mp))
		return;

	if (((fa->fsx_xflags & FS_XFLAG_DAX) &&
	    !(ip->i_diflags2 & XFS_DIFLAG2_DAX)) ||
	    (!(fa->fsx_xflags & FS_XFLAG_DAX) &&
	     (ip->i_diflags2 & XFS_DIFLAG2_DAX)))
		d_mark_dontcache(inode);
}

/*
 * Set up the transaction structure for the setattr operation, checking that we
 * have permission to do so. On success, return a clean transaction and the
 * inode locked exclusively ready for further operation specific checks. On
 * failure, return an error without modifying or locking the inode.
 */
static struct xfs_trans *
xfs_ioctl_setattr_get_trans(
	struct xfs_inode	*ip,
	struct xfs_dquot	*pdqp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error = -EROFS;

	if (xfs_is_readonly(mp))
		goto out_error;
	error = -EIO;
	if (xfs_is_shutdown(mp))
		goto out_error;

	error = xfs_trans_alloc_ichange(ip, NULL, NULL, pdqp,
			has_capability_noaudit(current, CAP_FOWNER), &tp);
	if (error)
		goto out_error;

	if (xfs_has_wsync(mp))
		xfs_trans_set_sync(tp);

	return tp;

out_error:
	return ERR_PTR(error);
}

/*
 * Validate a proposed extent size hint.  For regular files, the hint can only
 * be changed if no extents are allocated.
 */
static int
xfs_ioctl_setattr_check_extsize(
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_failaddr_t		failaddr;
	uint16_t		new_diflags;

	if (!fa->fsx_valid)
		return 0;

	if (S_ISREG(VFS_I(ip)->i_mode) && xfs_inode_has_filedata(ip) &&
	    XFS_FSB_TO_B(mp, ip->i_extsize) != fa->fsx_extsize)
		return -EINVAL;

	if (fa->fsx_extsize & mp->m_blockmask)
		return -EINVAL;

	new_diflags = xfs_flags2diflags(ip, fa->fsx_xflags);

	/*
	 * Inode verifiers do not check that the extent size hint is an integer
	 * multiple of the rt extent size on a directory with both rtinherit
	 * and extszinherit flags set.  Don't let sysadmins misconfigure
	 * directories.
	 */
	if ((new_diflags & XFS_DIFLAG_RTINHERIT) &&
	    (new_diflags & XFS_DIFLAG_EXTSZINHERIT)) {
		unsigned int	rtextsize_bytes;

		rtextsize_bytes = XFS_FSB_TO_B(mp, mp->m_sb.sb_rextsize);
		if (fa->fsx_extsize % rtextsize_bytes)
			return -EINVAL;
	}

	failaddr = xfs_inode_validate_extsize(ip->i_mount,
			XFS_B_TO_FSB(mp, fa->fsx_extsize),
			VFS_I(ip)->i_mode, new_diflags);
	return failaddr != NULL ? -EINVAL : 0;
}

static int
xfs_ioctl_setattr_check_cowextsize(
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_failaddr_t		failaddr;
	uint64_t		new_diflags2;
	uint16_t		new_diflags;

	if (!fa->fsx_valid)
		return 0;

	if (fa->fsx_cowextsize & mp->m_blockmask)
		return -EINVAL;

	new_diflags = xfs_flags2diflags(ip, fa->fsx_xflags);
	new_diflags2 = xfs_flags2diflags2(ip, fa->fsx_xflags);

	failaddr = xfs_inode_validate_cowextsize(ip->i_mount,
			XFS_B_TO_FSB(mp, fa->fsx_cowextsize),
			VFS_I(ip)->i_mode, new_diflags, new_diflags2);
	return failaddr != NULL ? -EINVAL : 0;
}

static int
xfs_ioctl_setattr_check_projid(
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	if (!fa->fsx_valid)
		return 0;

	/* Disallow 32bit project ids if 32bit IDs are not enabled. */
	if (fa->fsx_projid > (uint16_t)-1 &&
	    !xfs_has_projid32(ip->i_mount))
		return -EINVAL;
	return 0;
}

int
xfs_fileattr_set(
	struct mnt_idmap	*idmap,
	struct dentry		*dentry,
	struct fileattr		*fa)
{
	struct xfs_inode	*ip = XFS_I(d_inode(dentry));
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_dquot	*olddquot = NULL;
	int			error;

	trace_xfs_ioctl_setattr(ip);

	if (d_is_special(dentry))
		return -ENOTTY;

	if (!fa->fsx_valid) {
		if (fa->flags & ~(FS_IMMUTABLE_FL | FS_APPEND_FL |
				  FS_NOATIME_FL | FS_NODUMP_FL |
				  FS_SYNC_FL | FS_DAX_FL | FS_PROJINHERIT_FL))
			return -EOPNOTSUPP;
	}

	error = xfs_ioctl_setattr_check_projid(ip, fa);
	if (error)
		return error;

	/*
	 * If disk quotas is on, we make sure that the dquots do exist on disk,
	 * before we start any other transactions. Trying to do this later
	 * is messy. We don't care to take a readlock to look at the ids
	 * in inode here, because we can't hold it across the trans_reserve.
	 * If the IDs do change before we take the ilock, we're covered
	 * because the i_*dquot fields will get updated anyway.
	 */
	if (fa->fsx_valid && XFS_IS_QUOTA_ON(mp)) {
		error = xfs_qm_vop_dqalloc(ip, VFS_I(ip)->i_uid,
				VFS_I(ip)->i_gid, fa->fsx_projid,
				XFS_QMOPT_PQUOTA, NULL, NULL, &pdqp);
		if (error)
			return error;
	}

	xfs_ioctl_setattr_prepare_dax(ip, fa);

	tp = xfs_ioctl_setattr_get_trans(ip, pdqp);
	if (IS_ERR(tp)) {
		error = PTR_ERR(tp);
		goto error_free_dquots;
	}

	error = xfs_ioctl_setattr_check_extsize(ip, fa);
	if (error)
		goto error_trans_cancel;

	error = xfs_ioctl_setattr_check_cowextsize(ip, fa);
	if (error)
		goto error_trans_cancel;

	error = xfs_ioctl_setattr_xflags(tp, ip, fa);
	if (error)
		goto error_trans_cancel;

	if (!fa->fsx_valid)
		goto skip_xattr;
	/*
	 * Change file ownership.  Must be the owner or privileged.  CAP_FSETID
	 * overrides the following restrictions:
	 *
	 * The set-user-ID and set-group-ID bits of a file will be cleared upon
	 * successful return from chown()
	 */

	if ((VFS_I(ip)->i_mode & (S_ISUID|S_ISGID)) &&
	    !capable_wrt_inode_uidgid(idmap, VFS_I(ip), CAP_FSETID))
		VFS_I(ip)->i_mode &= ~(S_ISUID|S_ISGID);

	/* Change the ownerships and register project quota modifications */
	if (ip->i_projid != fa->fsx_projid) {
		if (XFS_IS_PQUOTA_ON(mp)) {
			olddquot = xfs_qm_vop_chown(tp, ip,
						&ip->i_pdquot, pdqp);
		}
		ip->i_projid = fa->fsx_projid;
	}

	/*
	 * Only set the extent size hint if we've already determined that the
	 * extent size hint should be set on the inode. If no extent size flags
	 * are set on the inode then unconditionally clear the extent size hint.
	 */
	if (ip->i_diflags & (XFS_DIFLAG_EXTSIZE | XFS_DIFLAG_EXTSZINHERIT))
		ip->i_extsize = XFS_B_TO_FSB(mp, fa->fsx_extsize);
	else
		ip->i_extsize = 0;

	if (xfs_has_v3inodes(mp)) {
		if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
			ip->i_cowextsize = XFS_B_TO_FSB(mp, fa->fsx_cowextsize);
		else
			ip->i_cowextsize = 0;
	}

skip_xattr:
	error = xfs_trans_commit(tp);

	/*
	 * Release any dquot(s) the inode had kept before chown.
	 */
	xfs_qm_dqrele(olddquot);
	xfs_qm_dqrele(pdqp);

	return error;

error_trans_cancel:
	xfs_trans_cancel(tp);
error_free_dquots:
	xfs_qm_dqrele(pdqp);
	return error;
}

static bool
xfs_getbmap_format(
	struct kgetbmap		*p,
	struct getbmapx __user	*u,
	size_t			recsize)
{
	if (put_user(p->bmv_offset, &u->bmv_offset) ||
	    put_user(p->bmv_block, &u->bmv_block) ||
	    put_user(p->bmv_length, &u->bmv_length) ||
	    put_user(0, &u->bmv_count) ||
	    put_user(0, &u->bmv_entries))
		return false;
	if (recsize < sizeof(struct getbmapx))
		return true;
	if (put_user(0, &u->bmv_iflags) ||
	    put_user(p->bmv_oflags, &u->bmv_oflags) ||
	    put_user(0, &u->bmv_unused1) ||
	    put_user(0, &u->bmv_unused2))
		return false;
	return true;
}

STATIC int
xfs_ioc_getbmap(
	struct file		*file,
	unsigned int		cmd,
	void			__user *arg)
{
	struct getbmapx		bmx = { 0 };
	struct kgetbmap		*buf;
	size_t			recsize;
	int			error, i;

	switch (cmd) {
	case XFS_IOC_GETBMAPA:
		bmx.bmv_iflags = BMV_IF_ATTRFORK;
		fallthrough;
	case XFS_IOC_GETBMAP:
		/* struct getbmap is a strict subset of struct getbmapx. */
		recsize = sizeof(struct getbmap);
		break;
	case XFS_IOC_GETBMAPX:
		recsize = sizeof(struct getbmapx);
		break;
	default:
		return -EINVAL;
	}

	if (copy_from_user(&bmx, arg, recsize))
		return -EFAULT;

	if (bmx.bmv_count < 2)
		return -EINVAL;
	if (bmx.bmv_count >= INT_MAX / recsize)
		return -ENOMEM;

	buf = kvcalloc(bmx.bmv_count, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	error = xfs_getbmap(XFS_I(file_inode(file)), &bmx, buf);
	if (error)
		goto out_free_buf;

	error = -EFAULT;
	if (copy_to_user(arg, &bmx, recsize))
		goto out_free_buf;
	arg += recsize;

	for (i = 0; i < bmx.bmv_entries; i++) {
		if (!xfs_getbmap_format(buf + i, arg, recsize))
			goto out_free_buf;
		arg += recsize;
	}

	error = 0;
out_free_buf:
	kvfree(buf);
	return error;
}

int
xfs_ioc_swapext(
	xfs_swapext_t	*sxp)
{
	xfs_inode_t     *ip, *tip;
	struct fd	f, tmp;
	int		error = 0;

	/* Pull information for the target fd */
	f = fdget((int)sxp->sx_fdtarget);
	if (!fd_file(f)) {
		error = -EINVAL;
		goto out;
	}

	if (!(fd_file(f)->f_mode & FMODE_WRITE) ||
	    !(fd_file(f)->f_mode & FMODE_READ) ||
	    (fd_file(f)->f_flags & O_APPEND)) {
		error = -EBADF;
		goto out_put_file;
	}

	tmp = fdget((int)sxp->sx_fdtmp);
	if (!fd_file(tmp)) {
		error = -EINVAL;
		goto out_put_file;
	}

	if (!(fd_file(tmp)->f_mode & FMODE_WRITE) ||
	    !(fd_file(tmp)->f_mode & FMODE_READ) ||
	    (fd_file(tmp)->f_flags & O_APPEND)) {
		error = -EBADF;
		goto out_put_tmp_file;
	}

	if (IS_SWAPFILE(file_inode(fd_file(f))) ||
	    IS_SWAPFILE(file_inode(fd_file(tmp)))) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	/*
	 * We need to ensure that the fds passed in point to XFS inodes
	 * before we cast and access them as XFS structures as we have no
	 * control over what the user passes us here.
	 */
	if (fd_file(f)->f_op != &xfs_file_operations ||
	    fd_file(tmp)->f_op != &xfs_file_operations) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	ip = XFS_I(file_inode(fd_file(f)));
	tip = XFS_I(file_inode(fd_file(tmp)));

	if (ip->i_mount != tip->i_mount) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	if (ip->i_ino == tip->i_ino) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	if (xfs_is_shutdown(ip->i_mount)) {
		error = -EIO;
		goto out_put_tmp_file;
	}

	error = xfs_swap_extents(ip, tip, sxp);

 out_put_tmp_file:
	fdput(tmp);
 out_put_file:
	fdput(f);
 out:
	return error;
}

static int
xfs_ioc_getlabel(
	struct xfs_mount	*mp,
	char			__user *user_label)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	char			label[XFSLABEL_MAX + 1];

	/* Paranoia */
	BUILD_BUG_ON(sizeof(sbp->sb_fname) > FSLABEL_MAX);

	/* 1 larger than sb_fname, so this ensures a trailing NUL char */
	memset(label, 0, sizeof(label));
	spin_lock(&mp->m_sb_lock);
	strncpy(label, sbp->sb_fname, XFSLABEL_MAX);
	spin_unlock(&mp->m_sb_lock);

	if (copy_to_user(user_label, label, sizeof(label)))
		return -EFAULT;
	return 0;
}

static int
xfs_ioc_setlabel(
	struct file		*filp,
	struct xfs_mount	*mp,
	char			__user *newlabel)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	char			label[XFSLABEL_MAX + 1];
	size_t			len;
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	/*
	 * The generic ioctl allows up to FSLABEL_MAX chars, but XFS is much
	 * smaller, at 12 bytes.  We copy one more to be sure we find the
	 * (required) NULL character to test the incoming label length.
	 * NB: The on disk label doesn't need to be null terminated.
	 */
	if (copy_from_user(label, newlabel, XFSLABEL_MAX + 1))
		return -EFAULT;
	len = strnlen(label, XFSLABEL_MAX + 1);
	if (len > sizeof(sbp->sb_fname))
		return -EINVAL;

	error = mnt_want_write_file(filp);
	if (error)
		return error;

	spin_lock(&mp->m_sb_lock);
	memset(sbp->sb_fname, 0, sizeof(sbp->sb_fname));
	memcpy(sbp->sb_fname, label, len);
	spin_unlock(&mp->m_sb_lock);

	/*
	 * Now we do several things to satisfy userspace.
	 * In addition to normal logging of the primary superblock, we also
	 * immediately write these changes to sector zero for the primary, then
	 * update all backup supers (as xfs_db does for a label change), then
	 * invalidate the block device page cache.  This is so that any prior
	 * buffered reads from userspace (i.e. from blkid) are invalidated,
	 * and userspace will see the newly-written label.
	 */
	error = xfs_sync_sb_buf(mp);
	if (error)
		goto out;
	/*
	 * growfs also updates backup supers so lock against that.
	 */
	mutex_lock(&mp->m_growlock);
	error = xfs_update_secondary_sbs(mp);
	mutex_unlock(&mp->m_growlock);

	invalidate_bdev(mp->m_ddev_targp->bt_bdev);

out:
	mnt_drop_write_file(filp);
	return error;
}

static inline int
xfs_fs_eofblocks_from_user(
	struct xfs_fs_eofblocks		*src,
	struct xfs_icwalk		*dst)
{
	if (src->eof_version != XFS_EOFBLOCKS_VERSION)
		return -EINVAL;

	if (src->eof_flags & ~XFS_EOF_FLAGS_VALID)
		return -EINVAL;

	if (memchr_inv(&src->pad32, 0, sizeof(src->pad32)) ||
	    memchr_inv(src->pad64, 0, sizeof(src->pad64)))
		return -EINVAL;

	dst->icw_flags = 0;
	if (src->eof_flags & XFS_EOF_FLAGS_SYNC)
		dst->icw_flags |= XFS_ICWALK_FLAG_SYNC;
	if (src->eof_flags & XFS_EOF_FLAGS_UID)
		dst->icw_flags |= XFS_ICWALK_FLAG_UID;
	if (src->eof_flags & XFS_EOF_FLAGS_GID)
		dst->icw_flags |= XFS_ICWALK_FLAG_GID;
	if (src->eof_flags & XFS_EOF_FLAGS_PRID)
		dst->icw_flags |= XFS_ICWALK_FLAG_PRID;
	if (src->eof_flags & XFS_EOF_FLAGS_MINFILESIZE)
		dst->icw_flags |= XFS_ICWALK_FLAG_MINFILESIZE;

	dst->icw_prid = src->eof_prid;
	dst->icw_min_file_size = src->eof_min_file_size;

	dst->icw_uid = INVALID_UID;
	if (src->eof_flags & XFS_EOF_FLAGS_UID) {
		dst->icw_uid = make_kuid(current_user_ns(), src->eof_uid);
		if (!uid_valid(dst->icw_uid))
			return -EINVAL;
	}

	dst->icw_gid = INVALID_GID;
	if (src->eof_flags & XFS_EOF_FLAGS_GID) {
		dst->icw_gid = make_kgid(current_user_ns(), src->eof_gid);
		if (!gid_valid(dst->icw_gid))
			return -EINVAL;
	}
	return 0;
}

static int
xfs_ioctl_getset_resblocks(
	struct file		*filp,
	unsigned int		cmd,
	void __user		*arg)
{
	struct xfs_mount	*mp = XFS_I(file_inode(filp))->i_mount;
	struct xfs_fsop_resblks	fsop = { };
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (cmd == XFS_IOC_SET_RESBLKS) {
		if (xfs_is_readonly(mp))
			return -EROFS;

		if (copy_from_user(&fsop, arg, sizeof(fsop)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_reserve_blocks(mp, fsop.resblks);
		mnt_drop_write_file(filp);
		if (error)
			return error;
	}

	spin_lock(&mp->m_sb_lock);
	fsop.resblks = mp->m_resblks;
	fsop.resblks_avail = mp->m_resblks_avail;
	spin_unlock(&mp->m_sb_lock);

	if (copy_to_user(arg, &fsop, sizeof(fsop)))
		return -EFAULT;
	return 0;
}

static int
xfs_ioctl_fs_counts(
	struct xfs_mount	*mp,
	struct xfs_fsop_counts __user	*uarg)
{
	struct xfs_fsop_counts	out = {
		.allocino = percpu_counter_read_positive(&mp->m_icount),
		.freeino  = percpu_counter_read_positive(&mp->m_ifree),
		.freedata = percpu_counter_read_positive(&mp->m_fdblocks) -
				xfs_fdblocks_unavailable(mp),
		.freertx  = percpu_counter_read_positive(&mp->m_frextents),
	};

	if (copy_to_user(uarg, &out, sizeof(out)))
		return -EFAULT;
	return 0;
}

/*
 * These long-unused ioctls were removed from the official ioctl API in 5.17,
 * but retain these definitions so that we can log warnings about them.
 */
#define XFS_IOC_ALLOCSP		_IOW ('X', 10, struct xfs_flock64)
#define XFS_IOC_FREESP		_IOW ('X', 11, struct xfs_flock64)
#define XFS_IOC_ALLOCSP64	_IOW ('X', 36, struct xfs_flock64)
#define XFS_IOC_FREESP64	_IOW ('X', 37, struct xfs_flock64)

/*
 * Note: some of the ioctl's return positive numbers as a
 * byte count indicating success, such as readlink_by_handle.
 * So we don't "sign flip" like most other routines.  This means
 * true errors need to be returned as a negative value.
 */
long
xfs_file_ioctl(
	struct file		*filp,
	unsigned int		cmd,
	unsigned long		p)
{
	struct inode		*inode = file_inode(filp);
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	void			__user *arg = (void __user *)p;
	int			error;

	trace_xfs_file_ioctl(ip);

	switch (cmd) {
	case FITRIM:
		return xfs_ioc_trim(mp, arg);
	case FS_IOC_GETFSLABEL:
		return xfs_ioc_getlabel(mp, arg);
	case FS_IOC_SETFSLABEL:
		return xfs_ioc_setlabel(filp, mp, arg);
	case XFS_IOC_ALLOCSP:
	case XFS_IOC_FREESP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP64:
		xfs_warn_once(mp,
	"%s should use fallocate; XFS_IOC_{ALLOC,FREE}SP ioctl unsupported",
				current->comm);
		return -ENOTTY;
	case XFS_IOC_DIOINFO: {
		struct xfs_buftarg	*target = xfs_inode_buftarg(ip);
		struct dioattr		da;

		da.d_mem =  da.d_miniosz = target->bt_logical_sectorsize;
		da.d_maxiosz = INT_MAX & ~(da.d_miniosz - 1);

		if (copy_to_user(arg, &da, sizeof(da)))
			return -EFAULT;
		return 0;
	}

	case XFS_IOC_FSBULKSTAT_SINGLE:
	case XFS_IOC_FSBULKSTAT:
	case XFS_IOC_FSINUMBERS:
		return xfs_ioc_fsbulkstat(filp, cmd, arg);

	case XFS_IOC_BULKSTAT:
		return xfs_ioc_bulkstat(filp, cmd, arg);
	case XFS_IOC_INUMBERS:
		return xfs_ioc_inumbers(mp, cmd, arg);

	case XFS_IOC_FSGEOMETRY_V1:
		return xfs_ioc_fsgeometry(mp, arg, 3);
	case XFS_IOC_FSGEOMETRY_V4:
		return xfs_ioc_fsgeometry(mp, arg, 4);
	case XFS_IOC_FSGEOMETRY:
		return xfs_ioc_fsgeometry(mp, arg, 5);

	case XFS_IOC_AG_GEOMETRY:
		return xfs_ioc_ag_geometry(mp, arg);

	case XFS_IOC_GETVERSION:
		return put_user(inode->i_generation, (int __user *)arg);

	case XFS_IOC_FSGETXATTRA:
		return xfs_ioc_fsgetxattra(ip, arg);
	case XFS_IOC_GETPARENTS:
		return xfs_ioc_getparents(filp, arg);
	case XFS_IOC_GETPARENTS_BY_HANDLE:
		return xfs_ioc_getparents_by_handle(filp, arg);
	case XFS_IOC_GETBMAP:
	case XFS_IOC_GETBMAPA:
	case XFS_IOC_GETBMAPX:
		return xfs_ioc_getbmap(filp, cmd, arg);

	case FS_IOC_GETFSMAP:
		return xfs_ioc_getfsmap(ip, arg);

	case XFS_IOC_SCRUBV_METADATA:
		return xfs_ioc_scrubv_metadata(filp, arg);
	case XFS_IOC_SCRUB_METADATA:
		return xfs_ioc_scrub_metadata(filp, arg);

	case XFS_IOC_FD_TO_HANDLE:
	case XFS_IOC_PATH_TO_HANDLE:
	case XFS_IOC_PATH_TO_FSHANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(hreq)))
			return -EFAULT;
		return xfs_find_handle(cmd, &hreq);
	}
	case XFS_IOC_OPEN_BY_HANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(xfs_fsop_handlereq_t)))
			return -EFAULT;
		return xfs_open_by_handle(filp, &hreq);
	}

	case XFS_IOC_READLINK_BY_HANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(xfs_fsop_handlereq_t)))
			return -EFAULT;
		return xfs_readlink_by_handle(filp, &hreq);
	}
	case XFS_IOC_ATTRLIST_BY_HANDLE:
		return xfs_attrlist_by_handle(filp, arg);

	case XFS_IOC_ATTRMULTI_BY_HANDLE:
		return xfs_attrmulti_by_handle(filp, arg);

	case XFS_IOC_SWAPEXT: {
		struct xfs_swapext	sxp;

		if (copy_from_user(&sxp, arg, sizeof(xfs_swapext_t)))
			return -EFAULT;
		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_ioc_swapext(&sxp);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSCOUNTS:
		return xfs_ioctl_fs_counts(mp, arg);

	case XFS_IOC_SET_RESBLKS:
	case XFS_IOC_GET_RESBLKS:
		return xfs_ioctl_getset_resblocks(filp, cmd, arg);

	case XFS_IOC_FSGROWFSDATA: {
		struct xfs_growfs_data in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_data(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSGROWFSLOG: {
		struct xfs_growfs_log in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_log(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSGROWFSRT: {
		xfs_growfs_rt_t in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_rt(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_GOINGDOWN: {
		uint32_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (get_user(in, (uint32_t __user *)arg))
			return -EFAULT;

		return xfs_fs_goingdown(mp, in);
	}

	case XFS_IOC_ERROR_INJECTION: {
		xfs_error_injection_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		return xfs_errortag_add(mp, in.errtag);
	}

	case XFS_IOC_ERROR_CLEARALL:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		return xfs_errortag_clearall(mp);

	case XFS_IOC_FREE_EOFBLOCKS: {
		struct xfs_fs_eofblocks	eofb;
		struct xfs_icwalk	icw;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (xfs_is_readonly(mp))
			return -EROFS;

		if (copy_from_user(&eofb, arg, sizeof(eofb)))
			return -EFAULT;

		error = xfs_fs_eofblocks_from_user(&eofb, &icw);
		if (error)
			return error;

		trace_xfs_ioc_free_eofblocks(mp, &icw, _RET_IP_);

		sb_start_write(mp->m_super);
		error = xfs_blockgc_free_space(mp, &icw);
		sb_end_write(mp->m_super);
		return error;
	}

	case XFS_IOC_EXCHANGE_RANGE:
		return xfs_ioc_exchange_range(filp, arg);
	case XFS_IOC_START_COMMIT:
		return xfs_ioc_start_commit(filp, arg);
	case XFS_IOC_COMMIT_RANGE:
		return xfs_ioc_commit_range(filp, arg);

	default:
		return -ENOTTY;
	}
}
