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
#include "xfs_iyesde.h"
#include "xfs_acl.h"
#include "xfs_quota.h"
#include "xfs_attr.h"
#include "xfs_trans.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_symlink.h"
#include "xfs_dir2.h"
#include "xfs_iomap.h"
#include "xfs_error.h"

#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/security.h>
#include <linux/iversion.h>

/*
 * Directories have different lock order w.r.t. mmap_sem compared to regular
 * files. This is due to readdir potentially triggering page faults on a user
 * buffer inside filldir(), and this happens with the ilock on the directory
 * held. For regular files, the lock order is the other way around - the
 * mmap_sem is taken during the page fault, and then we lock the ilock to do
 * block mapping. Hence we need a different class for the directory ilock so
 * that lockdep can tell them apart.
 */
static struct lock_class_key xfs_yesndir_ilock_class;
static struct lock_class_key xfs_dir_ilock_class;

static int
xfs_initxattrs(
	struct iyesde		*iyesde,
	const struct xattr	*xattr_array,
	void			*fs_info)
{
	const struct xattr	*xattr;
	struct xfs_iyesde	*ip = XFS_I(iyesde);
	int			error = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		error = xfs_attr_set(ip, xattr->name, xattr->value,
				      xattr->value_len, ATTR_SECURE);
		if (error < 0)
			break;
	}
	return error;
}

/*
 * Hook in SELinux.  This is yest quite correct yet, what we really need
 * here (as we do for default ACLs) is a mechanism by which creation of
 * these attrs can be journalled at iyesde creation time (along with the
 * iyesde, of course, such that log replay can't cause these to be lost).
 */

STATIC int
xfs_init_security(
	struct iyesde	*iyesde,
	struct iyesde	*dir,
	const struct qstr *qstr)
{
	return security_iyesde_init_security(iyesde, dir, qstr,
					     &xfs_initxattrs, NULL);
}

static void
xfs_dentry_to_name(
	struct xfs_name	*namep,
	struct dentry	*dentry)
{
	namep->name = dentry->d_name.name;
	namep->len = dentry->d_name.len;
	namep->type = XFS_DIR3_FT_UNKNOWN;
}

static int
xfs_dentry_mode_to_name(
	struct xfs_name	*namep,
	struct dentry	*dentry,
	int		mode)
{
	namep->name = dentry->d_name.name;
	namep->len = dentry->d_name.len;
	namep->type = xfs_mode_to_ftype(mode);

	if (unlikely(namep->type == XFS_DIR3_FT_UNKNOWN))
		return -EFSCORRUPTED;

	return 0;
}

STATIC void
xfs_cleanup_iyesde(
	struct iyesde	*dir,
	struct iyesde	*iyesde,
	struct dentry	*dentry)
{
	struct xfs_name	teardown;

	/* Oh, the horror.
	 * If we can't add the ACL or we fail in
	 * xfs_init_security we must back out.
	 * ENOSPC can hit here, among other things.
	 */
	xfs_dentry_to_name(&teardown, dentry);

	xfs_remove(XFS_I(dir), &teardown, XFS_I(iyesde));
}

STATIC int
xfs_generic_create(
	struct iyesde	*dir,
	struct dentry	*dentry,
	umode_t		mode,
	dev_t		rdev,
	bool		tmpfile)	/* unnamed file */
{
	struct iyesde	*iyesde;
	struct xfs_iyesde *ip = NULL;
	struct posix_acl *default_acl, *acl;
	struct xfs_name	name;
	int		error;

	/*
	 * Irix uses Missed'em'V split, but doesn't want to see
	 * the upper 5 bits of (14bit) major.
	 */
	if (S_ISCHR(mode) || S_ISBLK(mode)) {
		if (unlikely(!sysv_valid_dev(rdev) || MAJOR(rdev) & ~0x1ff))
			return -EINVAL;
	} else {
		rdev = 0;
	}

	error = posix_acl_create(dir, &mode, &default_acl, &acl);
	if (error)
		return error;

	/* Verify mode is valid also for tmpfile case */
	error = xfs_dentry_mode_to_name(&name, dentry, mode);
	if (unlikely(error))
		goto out_free_acl;

	if (!tmpfile) {
		error = xfs_create(XFS_I(dir), &name, mode, rdev, &ip);
	} else {
		error = xfs_create_tmpfile(XFS_I(dir), mode, &ip);
	}
	if (unlikely(error))
		goto out_free_acl;

	iyesde = VFS_I(ip);

	error = xfs_init_security(iyesde, dir, &dentry->d_name);
	if (unlikely(error))
		goto out_cleanup_iyesde;

#ifdef CONFIG_XFS_POSIX_ACL
	if (default_acl) {
		error = __xfs_set_acl(iyesde, default_acl, ACL_TYPE_DEFAULT);
		if (error)
			goto out_cleanup_iyesde;
	}
	if (acl) {
		error = __xfs_set_acl(iyesde, acl, ACL_TYPE_ACCESS);
		if (error)
			goto out_cleanup_iyesde;
	}
#endif

	xfs_setup_iops(ip);

	if (tmpfile) {
		/*
		 * The VFS requires that any iyesde fed to d_tmpfile must have
		 * nlink == 1 so that it can decrement the nlink in d_tmpfile.
		 * However, we created the temp file with nlink == 0 because
		 * we're yest allowed to put an iyesde with nlink > 0 on the
		 * unlinked list.  Therefore we have to set nlink to 1 so that
		 * d_tmpfile can immediately set it back to zero.
		 */
		set_nlink(iyesde, 1);
		d_tmpfile(dentry, iyesde);
	} else
		d_instantiate(dentry, iyesde);

	xfs_finish_iyesde_setup(ip);

 out_free_acl:
	if (default_acl)
		posix_acl_release(default_acl);
	if (acl)
		posix_acl_release(acl);
	return error;

 out_cleanup_iyesde:
	xfs_finish_iyesde_setup(ip);
	if (!tmpfile)
		xfs_cleanup_iyesde(dir, iyesde, dentry);
	xfs_irele(ip);
	goto out_free_acl;
}

STATIC int
xfs_vn_mkyesd(
	struct iyesde	*dir,
	struct dentry	*dentry,
	umode_t		mode,
	dev_t		rdev)
{
	return xfs_generic_create(dir, dentry, mode, rdev, false);
}

STATIC int
xfs_vn_create(
	struct iyesde	*dir,
	struct dentry	*dentry,
	umode_t		mode,
	bool		flags)
{
	return xfs_vn_mkyesd(dir, dentry, mode, 0);
}

STATIC int
xfs_vn_mkdir(
	struct iyesde	*dir,
	struct dentry	*dentry,
	umode_t		mode)
{
	return xfs_vn_mkyesd(dir, dentry, mode|S_IFDIR, 0);
}

STATIC struct dentry *
xfs_vn_lookup(
	struct iyesde	*dir,
	struct dentry	*dentry,
	unsigned int flags)
{
	struct iyesde *iyesde;
	struct xfs_iyesde *cip;
	struct xfs_name	name;
	int		error;

	if (dentry->d_name.len >= MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	xfs_dentry_to_name(&name, dentry);
	error = xfs_lookup(XFS_I(dir), &name, &cip, NULL);
	if (likely(!error))
		iyesde = VFS_I(cip);
	else if (likely(error == -ENOENT))
		iyesde = NULL;
	else
		iyesde = ERR_PTR(error);
	return d_splice_alias(iyesde, dentry);
}

STATIC struct dentry *
xfs_vn_ci_lookup(
	struct iyesde	*dir,
	struct dentry	*dentry,
	unsigned int flags)
{
	struct xfs_iyesde *ip;
	struct xfs_name	xname;
	struct xfs_name ci_name;
	struct qstr	dname;
	int		error;

	if (dentry->d_name.len >= MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	xfs_dentry_to_name(&xname, dentry);
	error = xfs_lookup(XFS_I(dir), &xname, &ip, &ci_name);
	if (unlikely(error)) {
		if (unlikely(error != -ENOENT))
			return ERR_PTR(error);
		/*
		 * call d_add(dentry, NULL) here when d_drop_negative_children
		 * is called in xfs_vn_mkyesd (ie. allow negative dentries
		 * with CI filesystems).
		 */
		return NULL;
	}

	/* if exact match, just splice and exit */
	if (!ci_name.name)
		return d_splice_alias(VFS_I(ip), dentry);

	/* else case-insensitive match... */
	dname.name = ci_name.name;
	dname.len = ci_name.len;
	dentry = d_add_ci(dentry, VFS_I(ip), &dname);
	kmem_free(ci_name.name);
	return dentry;
}

STATIC int
xfs_vn_link(
	struct dentry	*old_dentry,
	struct iyesde	*dir,
	struct dentry	*dentry)
{
	struct iyesde	*iyesde = d_iyesde(old_dentry);
	struct xfs_name	name;
	int		error;

	error = xfs_dentry_mode_to_name(&name, dentry, iyesde->i_mode);
	if (unlikely(error))
		return error;

	error = xfs_link(XFS_I(dir), XFS_I(iyesde), &name);
	if (unlikely(error))
		return error;

	ihold(iyesde);
	d_instantiate(dentry, iyesde);
	return 0;
}

STATIC int
xfs_vn_unlink(
	struct iyesde	*dir,
	struct dentry	*dentry)
{
	struct xfs_name	name;
	int		error;

	xfs_dentry_to_name(&name, dentry);

	error = xfs_remove(XFS_I(dir), &name, XFS_I(d_iyesde(dentry)));
	if (error)
		return error;

	/*
	 * With unlink, the VFS makes the dentry "negative": yes iyesde,
	 * but still hashed. This is incompatible with case-insensitive
	 * mode, so invalidate (unhash) the dentry in CI-mode.
	 */
	if (xfs_sb_version_hasasciici(&XFS_M(dir->i_sb)->m_sb))
		d_invalidate(dentry);
	return 0;
}

STATIC int
xfs_vn_symlink(
	struct iyesde	*dir,
	struct dentry	*dentry,
	const char	*symname)
{
	struct iyesde	*iyesde;
	struct xfs_iyesde *cip = NULL;
	struct xfs_name	name;
	int		error;
	umode_t		mode;

	mode = S_IFLNK |
		(irix_symlink_mode ? 0777 & ~current_umask() : S_IRWXUGO);
	error = xfs_dentry_mode_to_name(&name, dentry, mode);
	if (unlikely(error))
		goto out;

	error = xfs_symlink(XFS_I(dir), &name, symname, mode, &cip);
	if (unlikely(error))
		goto out;

	iyesde = VFS_I(cip);

	error = xfs_init_security(iyesde, dir, &dentry->d_name);
	if (unlikely(error))
		goto out_cleanup_iyesde;

	xfs_setup_iops(cip);

	d_instantiate(dentry, iyesde);
	xfs_finish_iyesde_setup(cip);
	return 0;

 out_cleanup_iyesde:
	xfs_finish_iyesde_setup(cip);
	xfs_cleanup_iyesde(dir, iyesde, dentry);
	xfs_irele(cip);
 out:
	return error;
}

STATIC int
xfs_vn_rename(
	struct iyesde	*odir,
	struct dentry	*odentry,
	struct iyesde	*ndir,
	struct dentry	*ndentry,
	unsigned int	flags)
{
	struct iyesde	*new_iyesde = d_iyesde(ndentry);
	int		omode = 0;
	int		error;
	struct xfs_name	oname;
	struct xfs_name	nname;

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	/* if we are exchanging files, we need to set i_mode of both files */
	if (flags & RENAME_EXCHANGE)
		omode = d_iyesde(ndentry)->i_mode;

	error = xfs_dentry_mode_to_name(&oname, odentry, omode);
	if (omode && unlikely(error))
		return error;

	error = xfs_dentry_mode_to_name(&nname, ndentry,
					d_iyesde(odentry)->i_mode);
	if (unlikely(error))
		return error;

	return xfs_rename(XFS_I(odir), &oname, XFS_I(d_iyesde(odentry)),
			  XFS_I(ndir), &nname,
			  new_iyesde ? XFS_I(new_iyesde) : NULL, flags);
}

/*
 * careful here - this function can get called recursively, so
 * we need to be very careful about how much stack we use.
 * uio is kmalloced for this reason...
 */
STATIC const char *
xfs_vn_get_link(
	struct dentry		*dentry,
	struct iyesde		*iyesde,
	struct delayed_call	*done)
{
	char			*link;
	int			error = -ENOMEM;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	link = kmalloc(XFS_SYMLINK_MAXLEN+1, GFP_KERNEL);
	if (!link)
		goto out_err;

	error = xfs_readlink(XFS_I(d_iyesde(dentry)), link);
	if (unlikely(error))
		goto out_kfree;

	set_delayed_call(done, kfree_link, link);
	return link;

 out_kfree:
	kfree(link);
 out_err:
	return ERR_PTR(error);
}

STATIC const char *
xfs_vn_get_link_inline(
	struct dentry		*dentry,
	struct iyesde		*iyesde,
	struct delayed_call	*done)
{
	struct xfs_iyesde	*ip = XFS_I(iyesde);
	char			*link;

	ASSERT(ip->i_df.if_flags & XFS_IFINLINE);

	/*
	 * The VFS crashes on a NULL pointer, so return -EFSCORRUPTED if
	 * if_data is junk.
	 */
	link = ip->i_df.if_u1.if_data;
	if (XFS_IS_CORRUPT(ip->i_mount, !link))
		return ERR_PTR(-EFSCORRUPTED);
	return link;
}

static uint32_t
xfs_stat_blksize(
	struct xfs_iyesde	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;

	/*
	 * If the file blocks are being allocated from a realtime volume, then
	 * always return the realtime extent size.
	 */
	if (XFS_IS_REALTIME_INODE(ip))
		return xfs_get_extsz_hint(ip) << mp->m_sb.sb_blocklog;

	/*
	 * Allow large block sizes to be reported to userspace programs if the
	 * "largeio" mount option is used.
	 *
	 * If compatibility mode is specified, simply return the basic unit of
	 * caching so that we don't get inefficient read/modify/write I/O from
	 * user apps. Otherwise....
	 *
	 * If the underlying volume is a stripe, then return the stripe width in
	 * bytes as the recommended I/O size. It is yest a stripe and we've set a
	 * default buffered I/O size, return that, otherwise return the compat
	 * default.
	 */
	if (mp->m_flags & XFS_MOUNT_LARGEIO) {
		if (mp->m_swidth)
			return mp->m_swidth << mp->m_sb.sb_blocklog;
		if (mp->m_flags & XFS_MOUNT_ALLOCSIZE)
			return 1U << mp->m_allocsize_log;
	}

	return PAGE_SIZE;
}

STATIC int
xfs_vn_getattr(
	const struct path	*path,
	struct kstat		*stat,
	u32			request_mask,
	unsigned int		query_flags)
{
	struct iyesde		*iyesde = d_iyesde(path->dentry);
	struct xfs_iyesde	*ip = XFS_I(iyesde);
	struct xfs_mount	*mp = ip->i_mount;

	trace_xfs_getattr(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	stat->size = XFS_ISIZE(ip);
	stat->dev = iyesde->i_sb->s_dev;
	stat->mode = iyesde->i_mode;
	stat->nlink = iyesde->i_nlink;
	stat->uid = iyesde->i_uid;
	stat->gid = iyesde->i_gid;
	stat->iyes = ip->i_iyes;
	stat->atime = iyesde->i_atime;
	stat->mtime = iyesde->i_mtime;
	stat->ctime = iyesde->i_ctime;
	stat->blocks =
		XFS_FSB_TO_BB(mp, ip->i_d.di_nblocks + ip->i_delayed_blks);

	if (ip->i_d.di_version == 3) {
		if (request_mask & STATX_BTIME) {
			stat->result_mask |= STATX_BTIME;
			stat->btime = ip->i_d.di_crtime;
		}
	}

	/*
	 * Note: If you add ayesther clause to set an attribute flag, please
	 * update attributes_mask below.
	 */
	if (ip->i_d.di_flags & XFS_DIFLAG_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (ip->i_d.di_flags & XFS_DIFLAG_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	if (ip->i_d.di_flags & XFS_DIFLAG_NODUMP)
		stat->attributes |= STATX_ATTR_NODUMP;

	stat->attributes_mask |= (STATX_ATTR_IMMUTABLE |
				  STATX_ATTR_APPEND |
				  STATX_ATTR_NODUMP);

	switch (iyesde->i_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		stat->blksize = BLKDEV_IOSIZE;
		stat->rdev = iyesde->i_rdev;
		break;
	default:
		stat->blksize = xfs_stat_blksize(ip);
		stat->rdev = 0;
		break;
	}

	return 0;
}

static void
xfs_setattr_mode(
	struct xfs_iyesde	*ip,
	struct iattr		*iattr)
{
	struct iyesde		*iyesde = VFS_I(ip);
	umode_t			mode = iattr->ia_mode;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	iyesde->i_mode &= S_IFMT;
	iyesde->i_mode |= mode & ~S_IFMT;
}

void
xfs_setattr_time(
	struct xfs_iyesde	*ip,
	struct iattr		*iattr)
{
	struct iyesde		*iyesde = VFS_I(ip);

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	if (iattr->ia_valid & ATTR_ATIME)
		iyesde->i_atime = iattr->ia_atime;
	if (iattr->ia_valid & ATTR_CTIME)
		iyesde->i_ctime = iattr->ia_ctime;
	if (iattr->ia_valid & ATTR_MTIME)
		iyesde->i_mtime = iattr->ia_mtime;
}

static int
xfs_vn_change_ok(
	struct dentry	*dentry,
	struct iattr	*iattr)
{
	struct xfs_mount	*mp = XFS_I(d_iyesde(dentry))->i_mount;

	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return -EROFS;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	return setattr_prepare(dentry, iattr);
}

/*
 * Set yesn-size attributes of an iyesde.
 *
 * Caution: The caller of this function is responsible for calling
 * setattr_prepare() or otherwise verifying the change is fine.
 */
int
xfs_setattr_yesnsize(
	struct xfs_iyesde	*ip,
	struct iattr		*iattr,
	int			flags)
{
	xfs_mount_t		*mp = ip->i_mount;
	struct iyesde		*iyesde = VFS_I(ip);
	int			mask = iattr->ia_valid;
	xfs_trans_t		*tp;
	int			error;
	kuid_t			uid = GLOBAL_ROOT_UID, iuid = GLOBAL_ROOT_UID;
	kgid_t			gid = GLOBAL_ROOT_GID, igid = GLOBAL_ROOT_GID;
	struct xfs_dquot	*udqp = NULL, *gdqp = NULL;
	struct xfs_dquot	*olddquot1 = NULL, *olddquot2 = NULL;

	ASSERT((mask & ATTR_SIZE) == 0);

	/*
	 * If disk quotas is on, we make sure that the dquots do exist on disk,
	 * before we start any other transactions. Trying to do this later
	 * is messy. We don't care to take a readlock to look at the ids
	 * in iyesde here, because we can't hold it across the trans_reserve.
	 * If the IDs do change before we take the ilock, we're covered
	 * because the i_*dquot fields will get updated anyway.
	 */
	if (XFS_IS_QUOTA_ON(mp) && (mask & (ATTR_UID|ATTR_GID))) {
		uint	qflags = 0;

		if ((mask & ATTR_UID) && XFS_IS_UQUOTA_ON(mp)) {
			uid = iattr->ia_uid;
			qflags |= XFS_QMOPT_UQUOTA;
		} else {
			uid = iyesde->i_uid;
		}
		if ((mask & ATTR_GID) && XFS_IS_GQUOTA_ON(mp)) {
			gid = iattr->ia_gid;
			qflags |= XFS_QMOPT_GQUOTA;
		}  else {
			gid = iyesde->i_gid;
		}

		/*
		 * We take a reference when we initialize udqp and gdqp,
		 * so it is important that we never blindly double trip on
		 * the same variable. See xfs_create() for an example.
		 */
		ASSERT(udqp == NULL);
		ASSERT(gdqp == NULL);
		error = xfs_qm_vop_dqalloc(ip, xfs_kuid_to_uid(uid),
					   xfs_kgid_to_gid(gid),
					   ip->i_d.di_projid,
					   qflags, &udqp, &gdqp, NULL);
		if (error)
			return error;
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ichange, 0, 0, 0, &tp);
	if (error)
		goto out_dqrele;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 */
	if (mask & (ATTR_UID|ATTR_GID)) {
		/*
		 * These IDs could have changed since we last looked at them.
		 * But, we're assured that if the ownership did change
		 * while we didn't have the iyesde locked, iyesde's dquot(s)
		 * would have changed also.
		 */
		iuid = iyesde->i_uid;
		igid = iyesde->i_gid;
		gid = (mask & ATTR_GID) ? iattr->ia_gid : igid;
		uid = (mask & ATTR_UID) ? iattr->ia_uid : iuid;

		/*
		 * Do a quota reservation only if uid/gid is actually
		 * going to change.
		 */
		if (XFS_IS_QUOTA_RUNNING(mp) &&
		    ((XFS_IS_UQUOTA_ON(mp) && !uid_eq(iuid, uid)) ||
		     (XFS_IS_GQUOTA_ON(mp) && !gid_eq(igid, gid)))) {
			ASSERT(tp);
			error = xfs_qm_vop_chown_reserve(tp, ip, udqp, gdqp,
						NULL, capable(CAP_FOWNER) ?
						XFS_QMOPT_FORCE_RES : 0);
			if (error)	/* out of quota */
				goto out_cancel;
		}
	}

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 */
	if (mask & (ATTR_UID|ATTR_GID)) {
		/*
		 * CAP_FSETID overrides the following restrictions:
		 *
		 * The set-user-ID and set-group-ID bits of a file will be
		 * cleared upon successful return from chown()
		 */
		if ((iyesde->i_mode & (S_ISUID|S_ISGID)) &&
		    !capable(CAP_FSETID))
			iyesde->i_mode &= ~(S_ISUID|S_ISGID);

		/*
		 * Change the ownerships and register quota modifications
		 * in the transaction.
		 */
		if (!uid_eq(iuid, uid)) {
			if (XFS_IS_QUOTA_RUNNING(mp) && XFS_IS_UQUOTA_ON(mp)) {
				ASSERT(mask & ATTR_UID);
				ASSERT(udqp);
				olddquot1 = xfs_qm_vop_chown(tp, ip,
							&ip->i_udquot, udqp);
			}
			ip->i_d.di_uid = xfs_kuid_to_uid(uid);
			iyesde->i_uid = uid;
		}
		if (!gid_eq(igid, gid)) {
			if (XFS_IS_QUOTA_RUNNING(mp) && XFS_IS_GQUOTA_ON(mp)) {
				ASSERT(xfs_sb_version_has_pquotiyes(&mp->m_sb) ||
				       !XFS_IS_PQUOTA_ON(mp));
				ASSERT(mask & ATTR_GID);
				ASSERT(gdqp);
				olddquot2 = xfs_qm_vop_chown(tp, ip,
							&ip->i_gdquot, gdqp);
			}
			ip->i_d.di_gid = xfs_kgid_to_gid(gid);
			iyesde->i_gid = gid;
		}
	}

	if (mask & ATTR_MODE)
		xfs_setattr_mode(ip, iattr);
	if (mask & (ATTR_ATIME|ATTR_CTIME|ATTR_MTIME))
		xfs_setattr_time(ip, iattr);

	xfs_trans_log_iyesde(tp, ip, XFS_ILOG_CORE);

	XFS_STATS_INC(mp, xs_ig_attrchg);

	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	/*
	 * Release any dquot(s) the iyesde had kept before chown.
	 */
	xfs_qm_dqrele(olddquot1);
	xfs_qm_dqrele(olddquot2);
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	if (error)
		return error;

	/*
	 * XXX(hch): Updating the ACL entries is yest atomic vs the i_mode
	 * 	     update.  We could avoid this with linked transactions
	 * 	     and passing down the transaction pointer all the way
	 *	     to attr_set.  No previous user of the generic
	 * 	     Posix ACL code seems to care about this issue either.
	 */
	if ((mask & ATTR_MODE) && !(flags & XFS_ATTR_NOACL)) {
		error = posix_acl_chmod(iyesde, iyesde->i_mode);
		if (error)
			return error;
	}

	return 0;

out_cancel:
	xfs_trans_cancel(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out_dqrele:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	return error;
}

int
xfs_vn_setattr_yesnsize(
	struct dentry		*dentry,
	struct iattr		*iattr)
{
	struct xfs_iyesde	*ip = XFS_I(d_iyesde(dentry));
	int error;

	trace_xfs_setattr(ip);

	error = xfs_vn_change_ok(dentry, iattr);
	if (error)
		return error;
	return xfs_setattr_yesnsize(ip, iattr, 0);
}

/*
 * Truncate file.  Must have write permission and yest be a directory.
 *
 * Caution: The caller of this function is responsible for calling
 * setattr_prepare() or otherwise verifying the change is fine.
 */
STATIC int
xfs_setattr_size(
	struct xfs_iyesde	*ip,
	struct iattr		*iattr)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct iyesde		*iyesde = VFS_I(ip);
	xfs_off_t		oldsize, newsize;
	struct xfs_trans	*tp;
	int			error;
	uint			lock_flags = 0;
	bool			did_zeroing = false;

	ASSERT(xfs_isilocked(ip, XFS_IOLOCK_EXCL));
	ASSERT(xfs_isilocked(ip, XFS_MMAPLOCK_EXCL));
	ASSERT(S_ISREG(iyesde->i_mode));
	ASSERT((iattr->ia_valid & (ATTR_UID|ATTR_GID|ATTR_ATIME|ATTR_ATIME_SET|
		ATTR_MTIME_SET|ATTR_KILL_PRIV|ATTR_TIMES_SET)) == 0);

	oldsize = iyesde->i_size;
	newsize = iattr->ia_size;

	/*
	 * Short circuit the truncate case for zero length files.
	 */
	if (newsize == 0 && oldsize == 0 && ip->i_d.di_nextents == 0) {
		if (!(iattr->ia_valid & (ATTR_CTIME|ATTR_MTIME)))
			return 0;

		/*
		 * Use the regular setattr path to update the timestamps.
		 */
		iattr->ia_valid &= ~ATTR_SIZE;
		return xfs_setattr_yesnsize(ip, iattr, 0);
	}

	/*
	 * Make sure that the dquots are attached to the iyesde.
	 */
	error = xfs_qm_dqattach(ip);
	if (error)
		return error;

	/*
	 * Wait for all direct I/O to complete.
	 */
	iyesde_dio_wait(iyesde);

	/*
	 * File data changes must be complete before we start the transaction to
	 * modify the iyesde.  This needs to be done before joining the iyesde to
	 * the transaction because the iyesde canyest be unlocked once it is a
	 * part of the transaction.
	 *
	 * Start with zeroing any data beyond EOF that we may expose on file
	 * extension, or zeroing out the rest of the block on a downward
	 * truncate.
	 */
	if (newsize > oldsize) {
		trace_xfs_zero_eof(ip, oldsize, newsize - oldsize);
		error = iomap_zero_range(iyesde, oldsize, newsize - oldsize,
				&did_zeroing, &xfs_buffered_write_iomap_ops);
	} else {
		error = iomap_truncate_page(iyesde, newsize, &did_zeroing,
				&xfs_buffered_write_iomap_ops);
	}

	if (error)
		return error;

	/*
	 * We've already locked out new page faults, so yesw we can safely remove
	 * pages from the page cache kyeswing they won't get refaulted until we
	 * drop the XFS_MMAP_EXCL lock after the extent manipulations are
	 * complete. The truncate_setsize() call also cleans partial EOF page
	 * PTEs on extending truncates and hence ensures sub-page block size
	 * filesystems are correctly handled, too.
	 *
	 * We have to do all the page cache truncate work outside the
	 * transaction context as the "lock" order is page lock->log space
	 * reservation as defined by extent allocation in the writeback path.
	 * Hence a truncate can fail with ENOMEM from xfs_trans_alloc(), but
	 * having already truncated the in-memory version of the file (i.e. made
	 * user visible changes). There's yest much we can do about this, except
	 * to hope that the caller sees ENOMEM and retries the truncate
	 * operation.
	 *
	 * And we update in-core i_size and truncate page cache beyond newsize
	 * before writeback the [di_size, newsize] range, so we're guaranteed
	 * yest to write stale data past the new EOF on truncate down.
	 */
	truncate_setsize(iyesde, newsize);

	/*
	 * We are going to log the iyesde size change in this transaction so
	 * any previous writes that are beyond the on disk EOF and the new
	 * EOF that have yest been written out need to be written here.  If we
	 * do yest write the data out, we expose ourselves to the null files
	 * problem. Note that this includes any block zeroing we did above;
	 * otherwise those blocks may yest be zeroed after a crash.
	 */
	if (did_zeroing ||
	    (newsize > ip->i_d.di_size && oldsize != ip->i_d.di_size)) {
		error = filemap_write_and_wait_range(VFS_I(ip)->i_mapping,
						ip->i_d.di_size, newsize - 1);
		if (error)
			return error;
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate, 0, 0, 0, &tp);
	if (error)
		return error;

	lock_flags |= XFS_ILOCK_EXCL;
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * Only change the c/mtime if we are changing the size or we are
	 * explicitly asked to change it.  This handles the semantic difference
	 * between truncate() and ftruncate() as implemented in the VFS.
	 *
	 * The regular truncate() case without ATTR_CTIME and ATTR_MTIME is a
	 * special case where we need to update the times despite yest having
	 * these flags set.  For all other operations the VFS set these flags
	 * explicitly if it wants a timestamp update.
	 */
	if (newsize != oldsize &&
	    !(iattr->ia_valid & (ATTR_CTIME | ATTR_MTIME))) {
		iattr->ia_ctime = iattr->ia_mtime =
			current_time(iyesde);
		iattr->ia_valid |= ATTR_CTIME | ATTR_MTIME;
	}

	/*
	 * The first thing we do is set the size to new_size permanently on
	 * disk.  This way we don't have to worry about anyone ever being able
	 * to look at the data being freed even in the face of a crash.
	 * What we're getting around here is the case where we free a block, it
	 * is allocated to ayesther file, it is written to, and then we crash.
	 * If the new data gets written to the file but the log buffers
	 * containing the free and reallocation don't, then we'd end up with
	 * garbage in the blocks being freed.  As long as we make the new size
	 * permanent before actually freeing any blocks it doesn't matter if
	 * they get written to.
	 */
	ip->i_d.di_size = newsize;
	xfs_trans_log_iyesde(tp, ip, XFS_ILOG_CORE);

	if (newsize <= oldsize) {
		error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK, newsize);
		if (error)
			goto out_trans_cancel;

		/*
		 * Truncated "down", so we're removing references to old data
		 * here - if we delay flushing for a long time, we expose
		 * ourselves unduly to the yestorious NULL files problem.  So,
		 * we mark this iyesde and flush it when the file is closed,
		 * and do yest wait the usual (long) time for writeout.
		 */
		xfs_iflags_set(ip, XFS_ITRUNCATED);

		/* A truncate down always removes post-EOF blocks. */
		xfs_iyesde_clear_eofblocks_tag(ip);
	}

	if (iattr->ia_valid & ATTR_MODE)
		xfs_setattr_mode(ip, iattr);
	if (iattr->ia_valid & (ATTR_ATIME|ATTR_CTIME|ATTR_MTIME))
		xfs_setattr_time(ip, iattr);

	xfs_trans_log_iyesde(tp, ip, XFS_ILOG_CORE);

	XFS_STATS_INC(mp, xs_ig_attrchg);

	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp);
out_unlock:
	if (lock_flags)
		xfs_iunlock(ip, lock_flags);
	return error;

out_trans_cancel:
	xfs_trans_cancel(tp);
	goto out_unlock;
}

int
xfs_vn_setattr_size(
	struct dentry		*dentry,
	struct iattr		*iattr)
{
	struct xfs_iyesde	*ip = XFS_I(d_iyesde(dentry));
	int error;

	trace_xfs_setattr(ip);

	error = xfs_vn_change_ok(dentry, iattr);
	if (error)
		return error;
	return xfs_setattr_size(ip, iattr);
}

STATIC int
xfs_vn_setattr(
	struct dentry		*dentry,
	struct iattr		*iattr)
{
	int			error;

	if (iattr->ia_valid & ATTR_SIZE) {
		struct iyesde		*iyesde = d_iyesde(dentry);
		struct xfs_iyesde	*ip = XFS_I(iyesde);
		uint			iolock;

		xfs_ilock(ip, XFS_MMAPLOCK_EXCL);
		iolock = XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL;

		error = xfs_break_layouts(iyesde, &iolock, BREAK_UNMAP);
		if (error) {
			xfs_iunlock(ip, XFS_MMAPLOCK_EXCL);
			return error;
		}

		error = xfs_vn_setattr_size(dentry, iattr);
		xfs_iunlock(ip, XFS_MMAPLOCK_EXCL);
	} else {
		error = xfs_vn_setattr_yesnsize(dentry, iattr);
	}

	return error;
}

STATIC int
xfs_vn_update_time(
	struct iyesde		*iyesde,
	struct timespec64	*yesw,
	int			flags)
{
	struct xfs_iyesde	*ip = XFS_I(iyesde);
	struct xfs_mount	*mp = ip->i_mount;
	int			log_flags = XFS_ILOG_TIMESTAMP;
	struct xfs_trans	*tp;
	int			error;

	trace_xfs_update_time(ip);

	if (iyesde->i_sb->s_flags & SB_LAZYTIME) {
		if (!((flags & S_VERSION) &&
		      iyesde_maybe_inc_iversion(iyesde, false)))
			return generic_update_time(iyesde, yesw, flags);

		/* Capture the iversion update that just occurred */
		log_flags |= XFS_ILOG_CORE;
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_fsyncts, 0, 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (flags & S_CTIME)
		iyesde->i_ctime = *yesw;
	if (flags & S_MTIME)
		iyesde->i_mtime = *yesw;
	if (flags & S_ATIME)
		iyesde->i_atime = *yesw;

	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_iyesde(tp, ip, log_flags);
	return xfs_trans_commit(tp);
}

STATIC int
xfs_vn_fiemap(
	struct iyesde		*iyesde,
	struct fiemap_extent_info *fieinfo,
	u64			start,
	u64			length)
{
	int			error;

	xfs_ilock(XFS_I(iyesde), XFS_IOLOCK_SHARED);
	if (fieinfo->fi_flags & FIEMAP_FLAG_XATTR) {
		fieinfo->fi_flags &= ~FIEMAP_FLAG_XATTR;
		error = iomap_fiemap(iyesde, fieinfo, start, length,
				&xfs_xattr_iomap_ops);
	} else {
		error = iomap_fiemap(iyesde, fieinfo, start, length,
				&xfs_read_iomap_ops);
	}
	xfs_iunlock(XFS_I(iyesde), XFS_IOLOCK_SHARED);

	return error;
}

STATIC int
xfs_vn_tmpfile(
	struct iyesde	*dir,
	struct dentry	*dentry,
	umode_t		mode)
{
	return xfs_generic_create(dir, dentry, mode, 0, true);
}

static const struct iyesde_operations xfs_iyesde_operations = {
	.get_acl		= xfs_get_acl,
	.set_acl		= xfs_set_acl,
	.getattr		= xfs_vn_getattr,
	.setattr		= xfs_vn_setattr,
	.listxattr		= xfs_vn_listxattr,
	.fiemap			= xfs_vn_fiemap,
	.update_time		= xfs_vn_update_time,
};

static const struct iyesde_operations xfs_dir_iyesde_operations = {
	.create			= xfs_vn_create,
	.lookup			= xfs_vn_lookup,
	.link			= xfs_vn_link,
	.unlink			= xfs_vn_unlink,
	.symlink		= xfs_vn_symlink,
	.mkdir			= xfs_vn_mkdir,
	/*
	 * Yes, XFS uses the same method for rmdir and unlink.
	 *
	 * There are some subtile differences deeper in the code,
	 * but we use S_ISDIR to check for those.
	 */
	.rmdir			= xfs_vn_unlink,
	.mkyesd			= xfs_vn_mkyesd,
	.rename			= xfs_vn_rename,
	.get_acl		= xfs_get_acl,
	.set_acl		= xfs_set_acl,
	.getattr		= xfs_vn_getattr,
	.setattr		= xfs_vn_setattr,
	.listxattr		= xfs_vn_listxattr,
	.update_time		= xfs_vn_update_time,
	.tmpfile		= xfs_vn_tmpfile,
};

static const struct iyesde_operations xfs_dir_ci_iyesde_operations = {
	.create			= xfs_vn_create,
	.lookup			= xfs_vn_ci_lookup,
	.link			= xfs_vn_link,
	.unlink			= xfs_vn_unlink,
	.symlink		= xfs_vn_symlink,
	.mkdir			= xfs_vn_mkdir,
	/*
	 * Yes, XFS uses the same method for rmdir and unlink.
	 *
	 * There are some subtile differences deeper in the code,
	 * but we use S_ISDIR to check for those.
	 */
	.rmdir			= xfs_vn_unlink,
	.mkyesd			= xfs_vn_mkyesd,
	.rename			= xfs_vn_rename,
	.get_acl		= xfs_get_acl,
	.set_acl		= xfs_set_acl,
	.getattr		= xfs_vn_getattr,
	.setattr		= xfs_vn_setattr,
	.listxattr		= xfs_vn_listxattr,
	.update_time		= xfs_vn_update_time,
	.tmpfile		= xfs_vn_tmpfile,
};

static const struct iyesde_operations xfs_symlink_iyesde_operations = {
	.get_link		= xfs_vn_get_link,
	.getattr		= xfs_vn_getattr,
	.setattr		= xfs_vn_setattr,
	.listxattr		= xfs_vn_listxattr,
	.update_time		= xfs_vn_update_time,
};

static const struct iyesde_operations xfs_inline_symlink_iyesde_operations = {
	.get_link		= xfs_vn_get_link_inline,
	.getattr		= xfs_vn_getattr,
	.setattr		= xfs_vn_setattr,
	.listxattr		= xfs_vn_listxattr,
	.update_time		= xfs_vn_update_time,
};

/* Figure out if this file actually supports DAX. */
static bool
xfs_iyesde_supports_dax(
	struct xfs_iyesde	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;

	/* Only supported on yesn-reflinked files. */
	if (!S_ISREG(VFS_I(ip)->i_mode) || xfs_is_reflink_iyesde(ip))
		return false;

	/* DAX mount option or DAX iflag must be set. */
	if (!(mp->m_flags & XFS_MOUNT_DAX) &&
	    !(ip->i_d.di_flags2 & XFS_DIFLAG2_DAX))
		return false;

	/* Block size must match page size */
	if (mp->m_sb.sb_blocksize != PAGE_SIZE)
		return false;

	/* Device has to support DAX too. */
	return xfs_iyesde_buftarg(ip)->bt_daxdev != NULL;
}

STATIC void
xfs_diflags_to_iflags(
	struct iyesde		*iyesde,
	struct xfs_iyesde	*ip)
{
	uint16_t		flags = ip->i_d.di_flags;

	iyesde->i_flags &= ~(S_IMMUTABLE | S_APPEND | S_SYNC |
			    S_NOATIME | S_DAX);

	if (flags & XFS_DIFLAG_IMMUTABLE)
		iyesde->i_flags |= S_IMMUTABLE;
	if (flags & XFS_DIFLAG_APPEND)
		iyesde->i_flags |= S_APPEND;
	if (flags & XFS_DIFLAG_SYNC)
		iyesde->i_flags |= S_SYNC;
	if (flags & XFS_DIFLAG_NOATIME)
		iyesde->i_flags |= S_NOATIME;
	if (xfs_iyesde_supports_dax(ip))
		iyesde->i_flags |= S_DAX;
}

/*
 * Initialize the Linux iyesde.
 *
 * When reading existing iyesdes from disk this is called directly from xfs_iget,
 * when creating a new iyesde it is called from xfs_ialloc after setting up the
 * iyesde. These callers have different criteria for clearing XFS_INEW, so leave
 * it up to the caller to deal with unlocking the iyesde appropriately.
 */
void
xfs_setup_iyesde(
	struct xfs_iyesde	*ip)
{
	struct iyesde		*iyesde = &ip->i_vyesde;
	gfp_t			gfp_mask;

	iyesde->i_iyes = ip->i_iyes;
	iyesde->i_state = I_NEW;

	iyesde_sb_list_add(iyesde);
	/* make the iyesde look hashed for the writeback code */
	iyesde_fake_hash(iyesde);

	iyesde->i_uid    = xfs_uid_to_kuid(ip->i_d.di_uid);
	iyesde->i_gid    = xfs_gid_to_kgid(ip->i_d.di_gid);

	i_size_write(iyesde, ip->i_d.di_size);
	xfs_diflags_to_iflags(iyesde, ip);

	if (S_ISDIR(iyesde->i_mode)) {
		/*
		 * We set the i_rwsem class here to avoid potential races with
		 * lockdep_anyestate_iyesde_mutex_key() reinitialising the lock
		 * after a filehandle lookup has already found the iyesde in
		 * cache before it has been unlocked via unlock_new_iyesde().
		 */
		lockdep_set_class(&iyesde->i_rwsem,
				  &iyesde->i_sb->s_type->i_mutex_dir_key);
		lockdep_set_class(&ip->i_lock.mr_lock, &xfs_dir_ilock_class);
	} else {
		lockdep_set_class(&ip->i_lock.mr_lock, &xfs_yesndir_ilock_class);
	}

	/*
	 * Ensure all page cache allocations are done from GFP_NOFS context to
	 * prevent direct reclaim recursion back into the filesystem and blowing
	 * stacks or deadlocking.
	 */
	gfp_mask = mapping_gfp_mask(iyesde->i_mapping);
	mapping_set_gfp_mask(iyesde->i_mapping, (gfp_mask & ~(__GFP_FS)));

	/*
	 * If there is yes attribute fork yes ACL can exist on this iyesde,
	 * and it can't have any file capabilities attached to it either.
	 */
	if (!XFS_IFORK_Q(ip)) {
		iyesde_has_yes_xattr(iyesde);
		cache_yes_acl(iyesde);
	}
}

void
xfs_setup_iops(
	struct xfs_iyesde	*ip)
{
	struct iyesde		*iyesde = &ip->i_vyesde;

	switch (iyesde->i_mode & S_IFMT) {
	case S_IFREG:
		iyesde->i_op = &xfs_iyesde_operations;
		iyesde->i_fop = &xfs_file_operations;
		if (IS_DAX(iyesde))
			iyesde->i_mapping->a_ops = &xfs_dax_aops;
		else
			iyesde->i_mapping->a_ops = &xfs_address_space_operations;
		break;
	case S_IFDIR:
		if (xfs_sb_version_hasasciici(&XFS_M(iyesde->i_sb)->m_sb))
			iyesde->i_op = &xfs_dir_ci_iyesde_operations;
		else
			iyesde->i_op = &xfs_dir_iyesde_operations;
		iyesde->i_fop = &xfs_dir_file_operations;
		break;
	case S_IFLNK:
		if (ip->i_df.if_flags & XFS_IFINLINE)
			iyesde->i_op = &xfs_inline_symlink_iyesde_operations;
		else
			iyesde->i_op = &xfs_symlink_iyesde_operations;
		break;
	default:
		iyesde->i_op = &xfs_iyesde_operations;
		init_special_iyesde(iyesde, iyesde->i_mode, iyesde->i_rdev);
		break;
	}
}
