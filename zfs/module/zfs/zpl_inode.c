/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */


#include <sys/zfs_ctldir.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/dmu_objset.h>
#include <sys/vfs.h>
#include <sys/zpl.h>
#include <sys/file.h>


static struct dentry *
#ifdef HAVE_LOOKUP_NAMEIDATA
zpl_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
#else
zpl_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
#endif
{
	cred_t *cr = CRED();
	struct inode *ip;
	int error;
	fstrans_cookie_t cookie;
	pathname_t *ppn = NULL;
	pathname_t pn;
	int zfs_flags = 0;
	zfs_sb_t *zsb = dentry->d_sb->s_fs_info;

	if (dlen(dentry) > ZFS_MAXNAMELEN)
		return (ERR_PTR(-ENAMETOOLONG));

	crhold(cr);
	cookie = spl_fstrans_mark();

	/* If we are a case insensitive fs, we need the real name */
	if (zsb->z_case == ZFS_CASE_INSENSITIVE) {
		zfs_flags = FIGNORECASE;
		pn.pn_bufsize = ZFS_MAXNAMELEN;
		pn.pn_buf = kmem_zalloc(ZFS_MAXNAMELEN, KM_SLEEP);
		ppn = &pn;
	}

	error = -zfs_lookup(dir, dname(dentry), &ip, zfs_flags, cr, NULL, ppn);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	spin_lock(&dentry->d_lock);
	dentry->d_time = jiffies;
#ifndef HAVE_S_D_OP
	d_set_d_op(dentry, &zpl_dentry_operations);
#endif /* HAVE_S_D_OP */
	spin_unlock(&dentry->d_lock);

	if (error) {
		/*
		 * If we have a case sensitive fs, we do not want to
		 * insert negative entries, so return NULL for ENOENT.
		 * Fall through if the error is not ENOENT. Also free memory.
		 */
		if (ppn) {
			kmem_free(pn.pn_buf, ZFS_MAXNAMELEN);
			if (error == -ENOENT)
				return (NULL);
		}

		if (error == -ENOENT)
			return (d_splice_alias(NULL, dentry));
		else
			return (ERR_PTR(error));
	}

	/*
	 * If we are case insensitive, call the correct function
	 * to install the name.
	 */
	if (ppn) {
		struct dentry *new_dentry;
		struct qstr ci_name;

		if (strcmp(dname(dentry), pn.pn_buf) == 0) {
			new_dentry = d_splice_alias(ip,  dentry);
		} else {
			ci_name.name = pn.pn_buf;
			ci_name.len = strlen(pn.pn_buf);
			new_dentry = d_add_ci(dentry, ip, &ci_name);
		}
		kmem_free(pn.pn_buf, ZFS_MAXNAMELEN);
		return (new_dentry);
	} else {
		return (d_splice_alias(ip, dentry));
	}
}

void
zpl_vap_init(vattr_t *vap, struct inode *dir, zpl_umode_t mode, cred_t *cr)
{
	vap->va_mask = ATTR_MODE;
	vap->va_mode = mode;
	vap->va_uid = crgetfsuid(cr);

	if (dir && dir->i_mode & S_ISGID) {
		vap->va_gid = KGID_TO_SGID(dir->i_gid);
		if (S_ISDIR(mode))
			vap->va_mode |= S_ISGID;
	} else {
		vap->va_gid = crgetfsgid(cr);
	}
}

static int
#ifdef HAVE_CREATE_NAMEIDATA
zpl_create(struct inode *dir, struct dentry *dentry, zpl_umode_t mode,
    struct nameidata *nd)
#else
zpl_create(struct inode *dir, struct dentry *dentry, zpl_umode_t mode,
    bool flag)
#endif
{
	cred_t *cr = CRED();
	struct inode *ip;
	vattr_t *vap;
	int error;
	fstrans_cookie_t cookie;

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dir, mode, cr);

	cookie = spl_fstrans_mark();
	error = -zfs_create(dir, dname(dentry), vap, 0, mode, &ip, cr, 0, NULL);
	if (error == 0) {
		d_instantiate(dentry, ip);

		error = zpl_xattr_security_init(ip, dir, &dentry->d_name);
		if (error == 0)
			error = zpl_init_acl(ip, dir);

		if (error)
			(void) zfs_remove(dir, dname(dentry), cr);
	}

	spl_fstrans_unmark(cookie);
	kmem_free(vap, sizeof (vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_mknod(struct inode *dir, struct dentry *dentry, zpl_umode_t mode,
    dev_t rdev)
{
	cred_t *cr = CRED();
	struct inode *ip;
	vattr_t *vap;
	int error;
	fstrans_cookie_t cookie;

	/*
	 * We currently expect Linux to supply rdev=0 for all sockets
	 * and fifos, but we want to know if this behavior ever changes.
	 */
	if (S_ISSOCK(mode) || S_ISFIFO(mode))
		ASSERT(rdev == 0);

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dir, mode, cr);
	vap->va_rdev = rdev;

	cookie = spl_fstrans_mark();
	error = -zfs_create(dir, dname(dentry), vap, 0, mode, &ip, cr, 0, NULL);
	if (error == 0) {
		d_instantiate(dentry, ip);

		error = zpl_xattr_security_init(ip, dir, &dentry->d_name);
		if (error == 0)
			error = zpl_init_acl(ip, dir);

		if (error)
			(void) zfs_remove(dir, dname(dentry), cr);
	}

	spl_fstrans_unmark(cookie);
	kmem_free(vap, sizeof (vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_unlink(struct inode *dir, struct dentry *dentry)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;
	zfs_sb_t *zsb = dentry->d_sb->s_fs_info;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_remove(dir, dname(dentry), cr);

	/*
	 * For a CI FS we must invalidate the dentry to prevent the
	 * creation of negative entries.
	 */
	if (error == 0 && zsb->z_case == ZFS_CASE_INSENSITIVE)
		d_invalidate(dentry);

	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_mkdir(struct inode *dir, struct dentry *dentry, zpl_umode_t mode)
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;
	fstrans_cookie_t cookie;

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dir, mode | S_IFDIR, cr);

	cookie = spl_fstrans_mark();
	error = -zfs_mkdir(dir, dname(dentry), vap, &ip, cr, 0, NULL);
	if (error == 0) {
		d_instantiate(dentry, ip);

		error = zpl_xattr_security_init(ip, dir, &dentry->d_name);
		if (error == 0)
			error = zpl_init_acl(ip, dir);

		if (error)
			(void) zfs_rmdir(dir, dname(dentry), NULL, cr, 0);
	}

	spl_fstrans_unmark(cookie);
	kmem_free(vap, sizeof (vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_rmdir(struct inode * dir, struct dentry *dentry)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;
	zfs_sb_t *zsb = dentry->d_sb->s_fs_info;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_rmdir(dir, dname(dentry), NULL, cr, 0);

	/*
	 * For a CI FS we must invalidate the dentry to prevent the
	 * creation of negative entries.
	 */
	if (error == 0 && zsb->z_case == ZFS_CASE_INSENSITIVE)
		d_invalidate(dentry);

	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_getattr_impl(const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
{
	int error;
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();

	/*
	 * XXX request_mask and query_flags currently ignored.
	 */

	error = -zfs_getattr_fast(path->dentry->d_inode, stat);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}
ZPL_GETATTR_WRAPPER(zpl_getattr);

static int
zpl_setattr(struct dentry *dentry, struct iattr *ia)
{
	struct inode *ip = dentry->d_inode;
	cred_t *cr = CRED();
	vattr_t *vap;
	int error;
	fstrans_cookie_t cookie;

	error = setattr_prepare(dentry, ia);
	if (error)
		return (error);

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
	vap->va_mask = ia->ia_valid & ATTR_IATTR_MASK;
	vap->va_mode = ia->ia_mode;
	vap->va_uid = KUID_TO_SUID(ia->ia_uid);
	vap->va_gid = KGID_TO_SGID(ia->ia_gid);
	vap->va_size = ia->ia_size;
	vap->va_atime = ia->ia_atime;
	vap->va_mtime = ia->ia_mtime;
	vap->va_ctime = ia->ia_ctime;

	if (vap->va_mask & ATTR_ATIME)
		ip->i_atime = ia->ia_atime;

	cookie = spl_fstrans_mark();
	error = -zfs_setattr(ip, vap, 0, cr);
	if (!error && (ia->ia_valid & ATTR_MODE))
		error = zpl_chmod_acl(ip);

	spl_fstrans_unmark(cookie);
	kmem_free(vap, sizeof (vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_rename2(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry, unsigned int flags)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	/* We don't have renameat2(2) support */
	if (flags)
		return (-EINVAL);

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_rename(sdip, dname(sdentry), tdip, dname(tdentry), cr, 0);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifndef HAVE_RENAME_WANTS_FLAGS
static int
zpl_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry)
{
	return (zpl_rename2(sdip, sdentry, tdip, tdentry, 0));
}
#endif

static int
zpl_symlink(struct inode *dir, struct dentry *dentry, const char *name)
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;
	fstrans_cookie_t cookie;

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dir, S_IFLNK | S_IRWXUGO, cr);

	cookie = spl_fstrans_mark();
	error = -zfs_symlink(dir, dname(dentry), vap, (char *)name, &ip, cr, 0);
	if (error == 0) {
		d_instantiate(dentry, ip);

		error = zpl_xattr_security_init(ip, dir, &dentry->d_name);
		if (error)
			(void) zfs_remove(dir, dname(dentry), cr);
	}

	spl_fstrans_unmark(cookie);
	kmem_free(vap, sizeof (vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#if defined(HAVE_PUT_LINK_COOKIE)
static void
zpl_put_link(struct inode *unused, void *cookie)
{
	kmem_free(cookie, MAXPATHLEN);
}
#elif defined(HAVE_PUT_LINK_NAMEIDATA)
static void
zpl_put_link(struct dentry *dentry, struct nameidata *nd, void *ptr)
{
	const char *link = nd_get_link(nd);

	if (!IS_ERR(link))
		kmem_free(link, MAXPATHLEN);
}
#elif defined(HAVE_PUT_LINK_DELAYED)
static void
zpl_put_link(void *ptr)
{
	kmem_free(ptr, MAXPATHLEN);
}
#endif

static int
zpl_get_link_common(struct dentry *dentry, struct inode *ip, char **link)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	struct iovec iov;
	uio_t uio;
	int error;

	crhold(cr);
	*link = NULL;
	iov.iov_len = MAXPATHLEN;
	iov.iov_base = kmem_zalloc(MAXPATHLEN, KM_SLEEP);

	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_skip = 0;
	uio.uio_resid = (MAXPATHLEN - 1);
	uio.uio_segflg = UIO_SYSSPACE;

	cookie = spl_fstrans_mark();
	error = -zfs_readlink(ip, &uio, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error)
		kmem_free(iov.iov_base, MAXPATHLEN);
	else
		*link = iov.iov_base;

	return (error);
}

#if defined(HAVE_GET_LINK_DELAYED)
const char *
zpl_get_link(struct dentry *dentry, struct inode *inode,
    struct delayed_call *done)
{
	char *link = NULL;
	int error;

	if (!dentry)
		return (ERR_PTR(-ECHILD));

	error = zpl_get_link_common(dentry, inode, &link);
	if (error)
		return (ERR_PTR(error));

	set_delayed_call(done, zpl_put_link, link);

	return (link);
}
#elif defined(HAVE_GET_LINK_COOKIE)
const char *
zpl_get_link(struct dentry *dentry, struct inode *inode, void **cookie)
{
	char *link = NULL;
	int error;

	if (!dentry)
		return (ERR_PTR(-ECHILD));

	error = zpl_get_link_common(dentry, inode, &link);
	if (error)
		return (ERR_PTR(error));

	return (*cookie = link);
}
#elif defined(HAVE_FOLLOW_LINK_COOKIE)
const char *
zpl_follow_link(struct dentry *dentry, void **cookie)
{
	char *link = NULL;
	int error;

	error = zpl_get_link_common(dentry, dentry->d_inode, &link);
	if (error)
		return (ERR_PTR(error));

	return (*cookie = link);
}
#elif defined(HAVE_FOLLOW_LINK_NAMEIDATA)
static void *
zpl_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char *link = NULL;
	int error;

	error = zpl_get_link_common(dentry, dentry->d_inode, &link);
	if (error)
		nd_set_link(nd, ERR_PTR(error));
	else
		nd_set_link(nd, link);

	return (NULL);
}
#endif

static int
zpl_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	cred_t *cr = CRED();
	struct inode *ip = old_dentry->d_inode;
	int error;
	fstrans_cookie_t cookie;

	if (ip->i_nlink >= ZFS_LINK_MAX)
		return (-EMLINK);

	crhold(cr);
	ip->i_ctime = CURRENT_TIME_SEC;
	igrab(ip); /* Use ihold() if available */

	cookie = spl_fstrans_mark();
	error = -zfs_link(dir, ip, dname(dentry), cr);
	if (error) {
		iput(ip);
		goto out;
	}

	d_instantiate(dentry, ip);
out:
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifdef HAVE_INODE_TRUNCATE_RANGE
static void
zpl_truncate_range(struct inode *ip, loff_t start, loff_t end)
{
	cred_t *cr = CRED();
	flock64_t bf;
	fstrans_cookie_t cookie;

	ASSERT3S(start, <=, end);

	/*
	 * zfs_freesp() will interpret (len == 0) as meaning "truncate until
	 * the end of the file". We don't want that.
	 */
	if (start == end)
		return;

	crhold(cr);

	bf.l_type = F_WRLCK;
	bf.l_whence = 0;
	bf.l_start = start;
	bf.l_len = end - start;
	bf.l_pid = 0;
	cookie = spl_fstrans_mark();
	zfs_space(ip, F_FREESP, &bf, FWRITE, start, cr);
	spl_fstrans_unmark(cookie);

	crfree(cr);
}
#endif /* HAVE_INODE_TRUNCATE_RANGE */

#ifdef HAVE_INODE_FALLOCATE
static long
zpl_fallocate(struct inode *ip, int mode, loff_t offset, loff_t len)
{
	return (zpl_fallocate_common(ip, mode, offset, len));
}
#endif /* HAVE_INODE_FALLOCATE */

static int
#ifdef HAVE_D_REVALIDATE_NAMEIDATA
zpl_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	unsigned int flags = (nd ? nd->flags : 0);
#else
zpl_revalidate(struct dentry *dentry, unsigned int flags)
{
#endif /* HAVE_D_REVALIDATE_NAMEIDATA */
	zfs_sb_t *zsb = dentry->d_sb->s_fs_info;
	int error;

	if (flags & LOOKUP_RCU)
		return (-ECHILD);

	/*
	 * Automounted snapshots rely on periodic dentry revalidation
	 * to defer snapshots from being automatically unmounted.
	 */
	if (zsb->z_issnap) {
		if (time_after(jiffies, zsb->z_snap_defer_time +
		    MAX(zfs_expire_snapshot * HZ / 2, HZ))) {
			zsb->z_snap_defer_time = jiffies;
			zfsctl_snapshot_unmount_delay(zsb->z_os->os_spa,
			    dmu_objset_id(zsb->z_os), zfs_expire_snapshot);
		}
	}

	/*
	 * After a rollback negative dentries created before the rollback
	 * time must be invalidated.  Otherwise they can obscure files which
	 * are only present in the rolled back dataset.
	 */
	if (dentry->d_inode == NULL) {
		spin_lock(&dentry->d_lock);
		error = time_before(dentry->d_time, zsb->z_rollback_time);
		spin_unlock(&dentry->d_lock);

		if (error)
			return (0);
	}

	/*
	 * The dentry may reference a stale inode if a mounted file system
	 * was rolled back to a point in time where the object didn't exist.
	 */
	if (dentry->d_inode && ITOZ(dentry->d_inode)->z_is_stale)
		return (0);

	return (1);
}

const struct inode_operations zpl_inode_operations = {
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
#ifdef HAVE_GENERIC_SETXATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
#endif
	.listxattr	= zpl_xattr_list,
#ifdef HAVE_INODE_TRUNCATE_RANGE
	.truncate_range = zpl_truncate_range,
#endif /* HAVE_INODE_TRUNCATE_RANGE */
#ifdef HAVE_INODE_FALLOCATE
	.fallocate	= zpl_fallocate,
#endif /* HAVE_INODE_FALLOCATE */
#if defined(CONFIG_FS_POSIX_ACL)
#if defined(HAVE_SET_ACL)
	.set_acl	= zpl_set_acl,
#endif
#if defined(HAVE_GET_ACL)
	.get_acl	= zpl_get_acl,
#elif defined(HAVE_CHECK_ACL)
	.check_acl	= zpl_check_acl,
#elif defined(HAVE_PERMISSION)
	.permission	= zpl_permission,
#endif /* HAVE_GET_ACL | HAVE_CHECK_ACL | HAVE_PERMISSION */
#endif /* CONFIG_FS_POSIX_ACL */
};

const struct inode_operations zpl_dir_inode_operations = {
	.create		= zpl_create,
	.lookup		= zpl_lookup,
	.link		= zpl_link,
	.unlink		= zpl_unlink,
	.symlink	= zpl_symlink,
	.mkdir		= zpl_mkdir,
	.rmdir		= zpl_rmdir,
	.mknod		= zpl_mknod,
#ifdef HAVE_RENAME_WANTS_FLAGS
	.rename		= zpl_rename2,
#else
	.rename		= zpl_rename,
#endif
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
#ifdef HAVE_GENERIC_SETXATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
#endif
	.listxattr	= zpl_xattr_list,
#if defined(CONFIG_FS_POSIX_ACL)
#if defined(HAVE_SET_ACL)
	.set_acl	= zpl_set_acl,
#endif
#if defined(HAVE_GET_ACL)
	.get_acl	= zpl_get_acl,
#elif defined(HAVE_CHECK_ACL)
	.check_acl	= zpl_check_acl,
#elif defined(HAVE_PERMISSION)
	.permission	= zpl_permission,
#endif /* HAVE_GET_ACL | HAVE_CHECK_ACL | HAVE_PERMISSION */
#endif /* CONFIG_FS_POSIX_ACL */
};

const struct inode_operations zpl_symlink_inode_operations = {
#ifdef HAVE_GENERIC_READLINK
	.readlink	= generic_readlink,
#endif
#if defined(HAVE_GET_LINK_DELAYED) || defined(HAVE_GET_LINK_COOKIE)
	.get_link	= zpl_get_link,
#elif defined(HAVE_FOLLOW_LINK_COOKIE) || defined(HAVE_FOLLOW_LINK_NAMEIDATA)
	.follow_link	= zpl_follow_link,
#endif
#if defined(HAVE_PUT_LINK_COOKIE) || defined(HAVE_PUT_LINK_NAMEIDATA)
	.put_link	= zpl_put_link,
#endif
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
#ifdef HAVE_GENERIC_SETXATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
#endif
	.listxattr	= zpl_xattr_list,
};

const struct inode_operations zpl_special_inode_operations = {
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
#ifdef HAVE_GENERIC_SETXATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
#endif
	.listxattr	= zpl_xattr_list,
#if defined(CONFIG_FS_POSIX_ACL)
#if defined(HAVE_SET_ACL)
	.set_acl	= zpl_set_acl,
#endif
#if defined(HAVE_GET_ACL)
	.get_acl	= zpl_get_acl,
#elif defined(HAVE_CHECK_ACL)
	.check_acl	= zpl_check_acl,
#elif defined(HAVE_PERMISSION)
	.permission	= zpl_permission,
#endif /* HAVE_GET_ACL | HAVE_CHECK_ACL | HAVE_PERMISSION */
#endif /* CONFIG_FS_POSIX_ACL */
};

dentry_operations_t zpl_dentry_operations = {
	.d_revalidate	= zpl_revalidate,
};
