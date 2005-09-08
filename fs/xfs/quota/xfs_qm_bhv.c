/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bit.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"

#include "xfs_qm.h"

#define MNTOPT_QUOTA	"quota"		/* disk quotas (user) */
#define MNTOPT_NOQUOTA	"noquota"	/* no quotas */
#define MNTOPT_USRQUOTA	"usrquota"	/* user quota enabled */
#define MNTOPT_GRPQUOTA	"grpquota"	/* group quota enabled */
#define MNTOPT_PRJQUOTA	"prjquota"	/* project quota enabled */
#define MNTOPT_UQUOTA	"uquota"	/* user quota (IRIX variant) */
#define MNTOPT_GQUOTA	"gquota"	/* group quota (IRIX variant) */
#define MNTOPT_PQUOTA	"pquota"	/* project quota (IRIX variant) */
#define MNTOPT_UQUOTANOENF "uqnoenforce"/* user quota limit enforcement */
#define MNTOPT_GQUOTANOENF "gqnoenforce"/* group quota limit enforcement */
#define MNTOPT_PQUOTANOENF "pqnoenforce"/* project quota limit enforcement */
#define MNTOPT_QUOTANOENF  "qnoenforce"	/* same as uqnoenforce */

STATIC int
xfs_qm_parseargs(
	struct bhv_desc		*bhv,
	char			*options,
	struct xfs_mount_args	*args,
	int			update)
{
	size_t			length;
	char			*local_options = options;
	char			*this_char;
	int			error;
	int			referenced = update;

	while ((this_char = strsep(&local_options, ",")) != NULL) {
		length = strlen(this_char);
		if (local_options)
			length++;

		if (!strcmp(this_char, MNTOPT_NOQUOTA)) {
			args->flags &= ~(XFSMNT_UQUOTAENF|XFSMNT_UQUOTA);
			args->flags &= ~(XFSMNT_GQUOTAENF|XFSMNT_GQUOTA);
			referenced = update;
		} else if (!strcmp(this_char, MNTOPT_QUOTA) ||
			   !strcmp(this_char, MNTOPT_UQUOTA) ||
			   !strcmp(this_char, MNTOPT_USRQUOTA)) {
			args->flags |= XFSMNT_UQUOTA | XFSMNT_UQUOTAENF;
			referenced = 1;
		} else if (!strcmp(this_char, MNTOPT_QUOTANOENF) ||
			   !strcmp(this_char, MNTOPT_UQUOTANOENF)) {
			args->flags |= XFSMNT_UQUOTA;
			args->flags &= ~XFSMNT_UQUOTAENF;
			referenced = 1;
		} else if (!strcmp(this_char, MNTOPT_PQUOTA) ||
			   !strcmp(this_char, MNTOPT_PRJQUOTA)) {
			args->flags |= XFSMNT_PQUOTA | XFSMNT_PQUOTAENF;
			referenced = 1;
		} else if (!strcmp(this_char, MNTOPT_PQUOTANOENF)) {
			args->flags |= XFSMNT_PQUOTA;
			args->flags &= ~XFSMNT_PQUOTAENF;
			referenced = 1;
		} else if (!strcmp(this_char, MNTOPT_GQUOTA) ||
			   !strcmp(this_char, MNTOPT_GRPQUOTA)) {
			args->flags |= XFSMNT_GQUOTA | XFSMNT_GQUOTAENF;
			referenced = 1;
		} else if (!strcmp(this_char, MNTOPT_GQUOTANOENF)) {
			args->flags |= XFSMNT_GQUOTA;
			args->flags &= ~XFSMNT_GQUOTAENF;
			referenced = 1;
		} else {
			if (local_options)
				*(local_options-1) = ',';
			continue;
		}

		while (length--)
			*this_char++ = ',';
	}

	if ((args->flags & XFSMNT_GQUOTA) && (args->flags & XFSMNT_PQUOTA)) {
		cmn_err(CE_WARN,
			"XFS: cannot mount with both project and group quota");
		return XFS_ERROR(EINVAL);
	}

	PVFS_PARSEARGS(BHV_NEXT(bhv), options, args, update, error);
	if (!error && !referenced)
		bhv_remove_vfsops(bhvtovfs(bhv), VFS_POSITION_QM);
	return error;
}

STATIC int
xfs_qm_showargs(
	struct bhv_desc		*bhv,
	struct seq_file		*m)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	struct xfs_mount	*mp = XFS_VFSTOM(vfsp);
	int			error;

	if (mp->m_qflags & XFS_UQUOTA_ACCT) {
		(mp->m_qflags & XFS_UQUOTA_ENFD) ?
			seq_puts(m, "," MNTOPT_USRQUOTA) :
			seq_puts(m, "," MNTOPT_UQUOTANOENF);
	}

	if (mp->m_qflags & XFS_PQUOTA_ACCT) {
		(mp->m_qflags & XFS_OQUOTA_ENFD) ?
			seq_puts(m, "," MNTOPT_PRJQUOTA) :
			seq_puts(m, "," MNTOPT_PQUOTANOENF);
	}

	if (mp->m_qflags & XFS_GQUOTA_ACCT) {
		(mp->m_qflags & XFS_OQUOTA_ENFD) ?
			seq_puts(m, "," MNTOPT_GRPQUOTA) :
			seq_puts(m, "," MNTOPT_GQUOTANOENF);
	}

	if (!(mp->m_qflags & XFS_ALL_QUOTA_ACCT))
		seq_puts(m, "," MNTOPT_NOQUOTA);

	PVFS_SHOWARGS(BHV_NEXT(bhv), m, error);
	return error;
}

STATIC int
xfs_qm_mount(
	struct bhv_desc		*bhv,
	struct xfs_mount_args	*args,
	struct cred		*cr)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	struct xfs_mount	*mp = XFS_VFSTOM(vfsp);
	int			error;

	if (args->flags & (XFSMNT_UQUOTA | XFSMNT_GQUOTA | XFSMNT_PQUOTA))
		xfs_qm_mount_quotainit(mp, args->flags);
	PVFS_MOUNT(BHV_NEXT(bhv), args, cr, error);
	return error;
}

STATIC int
xfs_qm_syncall(
	struct bhv_desc		*bhv,
	int			flags,
	cred_t			*credp)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	struct xfs_mount	*mp = XFS_VFSTOM(vfsp);
	int			error;

	/*
	 * Get the Quota Manager to flush the dquots.
	 */
	if (XFS_IS_QUOTA_ON(mp)) {
		if ((error = xfs_qm_sync(mp, flags))) {
			/*
			 * If we got an IO error, we will be shutting down.
			 * So, there's nothing more for us to do here.
			 */
			ASSERT(error != EIO || XFS_FORCED_SHUTDOWN(mp));
			if (XFS_FORCED_SHUTDOWN(mp)) {
				return XFS_ERROR(error);
			}
		}
	}
	PVFS_SYNC(BHV_NEXT(bhv), flags, credp, error);
	return error;
}

STATIC int
xfs_qm_newmount(
	xfs_mount_t	*mp,
	uint		*needquotamount,
	uint		*quotaflags)
{
	uint		quotaondisk;
	uint		uquotaondisk = 0, gquotaondisk = 0, pquotaondisk = 0;

	*quotaflags = 0;
	*needquotamount = B_FALSE;

	quotaondisk = XFS_SB_VERSION_HASQUOTA(&mp->m_sb) &&
				(mp->m_sb.sb_qflags & XFS_ALL_QUOTA_ACCT);

	if (quotaondisk) {
		uquotaondisk = mp->m_sb.sb_qflags & XFS_UQUOTA_ACCT;
		pquotaondisk = mp->m_sb.sb_qflags & XFS_PQUOTA_ACCT;
		gquotaondisk = mp->m_sb.sb_qflags & XFS_GQUOTA_ACCT;
	}

	/*
	 * If the device itself is read-only, we can't allow
	 * the user to change the state of quota on the mount -
	 * this would generate a transaction on the ro device,
	 * which would lead to an I/O error and shutdown
	 */

	if (((uquotaondisk && !XFS_IS_UQUOTA_ON(mp)) ||
	    (!uquotaondisk &&  XFS_IS_UQUOTA_ON(mp)) ||
	     (pquotaondisk && !XFS_IS_PQUOTA_ON(mp)) ||
	    (!pquotaondisk &&  XFS_IS_PQUOTA_ON(mp)) ||
	     (gquotaondisk && !XFS_IS_GQUOTA_ON(mp)) ||
	    (!gquotaondisk &&  XFS_IS_OQUOTA_ON(mp)))  &&
	    xfs_dev_is_read_only(mp, "changing quota state")) {
		cmn_err(CE_WARN,
			"XFS: please mount with%s%s%s%s.",
			(!quotaondisk ? "out quota" : ""),
			(uquotaondisk ? " usrquota" : ""),
			(pquotaondisk ? " prjquota" : ""),
			(gquotaondisk ? " grpquota" : ""));
		return XFS_ERROR(EPERM);
	}

	if (XFS_IS_QUOTA_ON(mp) || quotaondisk) {
		/*
		 * Call mount_quotas at this point only if we won't have to do
		 * a quotacheck.
		 */
		if (quotaondisk && !XFS_QM_NEED_QUOTACHECK(mp)) {
			/*
			 * If an error occured, qm_mount_quotas code
			 * has already disabled quotas. So, just finish
			 * mounting, and get on with the boring life
			 * without disk quotas.
			 */
			xfs_qm_mount_quotas(mp, 0);
		} else {
			/*
			 * Clear the quota flags, but remember them. This
			 * is so that the quota code doesn't get invoked
			 * before we're ready. This can happen when an
			 * inode goes inactive and wants to free blocks,
			 * or via xfs_log_mount_finish.
			 */
			*needquotamount = B_TRUE;
			*quotaflags = mp->m_qflags;
			mp->m_qflags = 0;
		}
	}

	return 0;
}

STATIC int
xfs_qm_endmount(
	xfs_mount_t	*mp,
	uint		needquotamount,
	uint		quotaflags,
	int		mfsi_flags)
{
	if (needquotamount) {
		ASSERT(mp->m_qflags == 0);
		mp->m_qflags = quotaflags;
		xfs_qm_mount_quotas(mp, mfsi_flags);
	}

#if defined(DEBUG) && defined(XFS_LOUD_RECOVERY)
	if (! (XFS_IS_QUOTA_ON(mp)))
		xfs_fs_cmn_err(CE_NOTE, mp, "Disk quotas not turned on");
	else
		xfs_fs_cmn_err(CE_NOTE, mp, "Disk quotas turned on");
#endif

#ifdef QUOTADEBUG
	if (XFS_IS_QUOTA_ON(mp) && xfs_qm_internalqcheck(mp))
		cmn_err(CE_WARN, "XFS: mount internalqcheck failed");
#endif

	return 0;
}

STATIC void
xfs_qm_dqrele_null(
	xfs_dquot_t	*dq)
{
	/*
	 * Called from XFS, where we always check first for a NULL dquot.
	 */
	if (!dq)
		return;
	xfs_qm_dqrele(dq);
}


STATIC struct xfs_qmops xfs_qmcore_xfs = {
	.xfs_qminit		= xfs_qm_newmount,
	.xfs_qmdone		= xfs_qm_unmount_quotadestroy,
	.xfs_qmmount		= xfs_qm_endmount,
	.xfs_qmunmount		= xfs_qm_unmount_quotas,
	.xfs_dqrele		= xfs_qm_dqrele_null,
	.xfs_dqattach		= xfs_qm_dqattach,
	.xfs_dqdetach		= xfs_qm_dqdetach,
	.xfs_dqpurgeall		= xfs_qm_dqpurge_all,
	.xfs_dqvopalloc		= xfs_qm_vop_dqalloc,
	.xfs_dqvopcreate	= xfs_qm_vop_dqattach_and_dqmod_newinode,
	.xfs_dqvoprename	= xfs_qm_vop_rename_dqattach,
	.xfs_dqvopchown		= xfs_qm_vop_chown,
	.xfs_dqvopchownresv	= xfs_qm_vop_chown_reserve,
	.xfs_dqtrxops		= &xfs_trans_dquot_ops,
};

struct bhv_vfsops xfs_qmops = { {
	BHV_IDENTITY_INIT(VFS_BHV_QM, VFS_POSITION_QM),
	.vfs_parseargs		= xfs_qm_parseargs,
	.vfs_showargs		= xfs_qm_showargs,
	.vfs_mount		= xfs_qm_mount,
	.vfs_sync		= xfs_qm_syncall,
	.vfs_quotactl		= xfs_qm_quotactl, },
};


void __init
xfs_qm_init(void)
{
	static char	message[] __initdata =
		KERN_INFO "SGI XFS Quota Management subsystem\n";

	printk(message);
	mutex_init(&xfs_Gqm_lock, MUTEX_DEFAULT, "xfs_qmlock");
	vfs_bhv_set_custom(&xfs_qmops, &xfs_qmcore_xfs);
	xfs_qm_init_procfs();
}

void __exit
xfs_qm_exit(void)
{
	vfs_bhv_clr_custom(&xfs_qmops);
	xfs_qm_cleanup_procfs();
	if (qm_dqzone)
		kmem_cache_destroy(qm_dqzone);
	if (qm_dqtrxzone)
		kmem_cache_destroy(qm_dqtrxzone);
}
