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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 */

#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>

/*
 * Common open routine.  Disallow any write access.
 */
/* ARGSUSED */
static int
zpl_common_open(struct inode *ip, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE)
		return (-EACCES);

	return (generic_file_open(ip, filp));
}

/*
 * Get root directory contents.
 */
static int
zpl_root_iterate(struct file *filp, zpl_dir_context_t *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	int error = 0;

	ZFS_ENTER(zfsvfs);

	if (!zpl_dir_emit_dots(filp, ctx))
		goto out;

	if (ctx->pos == 2) {
		if (!zpl_dir_emit(ctx, ZFS_SNAPDIR_NAME,
		    strlen(ZFS_SNAPDIR_NAME), ZFSCTL_INO_SNAPDIR, DT_DIR))
			goto out;

		ctx->pos++;
	}

	if (ctx->pos == 3) {
		if (!zpl_dir_emit(ctx, ZFS_SHAREDIR_NAME,
		    strlen(ZFS_SHAREDIR_NAME), ZFSCTL_INO_SHARES, DT_DIR))
			goto out;

		ctx->pos++;
	}
out:
	ZFS_EXIT(zfsvfs);

	return (error);
}

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_root_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_root_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

/*
 * Get root directory attributes.
 */
/* ARGSUSED */
static int
zpl_root_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
{
	struct inode *ip = path->dentry->d_inode;

	generic_fillattr(ip, stat);
	stat->atime = current_time(ip);

	return (0);
}
ZPL_GETATTR_WRAPPER(zpl_root_getattr);

static struct dentry *
#ifdef HAVE_LOOKUP_NAMEIDATA
zpl_root_lookup(struct inode *dip, struct dentry *dentry, struct nameidata *nd)
#else
zpl_root_lookup(struct inode *dip, struct dentry *dentry, unsigned int flags)
#endif
{
	cred_t *cr = CRED();
	struct inode *ip;
	int error;

	crhold(cr);
	error = -zfsctl_root_lookup(dip, dname(dentry), &ip, 0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	if (error) {
		if (error == -ENOENT)
			return (d_splice_alias(NULL, dentry));
		else
			return (ERR_PTR(error));
	}

	return (d_splice_alias(ip, dentry));
}

/*
 * The '.zfs' control directory file and inode operations.
 */
const struct file_operations zpl_fops_root = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_root_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_root_iterate,
#else
	.readdir	= zpl_root_readdir,
#endif
};

const struct inode_operations zpl_ops_root = {
	.lookup		= zpl_root_lookup,
	.getattr	= zpl_root_getattr,
};

#ifdef HAVE_AUTOMOUNT
static struct vfsmount *
zpl_snapdir_automount(struct path *path)
{
	int error;

	error = -zfsctl_snapshot_mount(path, 0);
	if (error)
		return (ERR_PTR(error));

	/*
	 * Rather than returning the new vfsmount for the snapshot we must
	 * return NULL to indicate a mount collision.  This is done because
	 * the user space mount calls do_add_mount() which adds the vfsmount
	 * to the name space.  If we returned the new mount here it would be
	 * added again to the vfsmount list resulting in list corruption.
	 */
	return (NULL);
}
#endif /* HAVE_AUTOMOUNT */

/*
 * Negative dentries must always be revalidated so newly created snapshots
 * can be detected and automounted.  Normal dentries should be kept because
 * as of the 3.18 kernel revaliding the mountpoint dentry will result in
 * the snapshot being immediately unmounted.
 */
static int
#ifdef HAVE_D_REVALIDATE_NAMEIDATA
zpl_snapdir_revalidate(struct dentry *dentry, struct nameidata *i)
#else
zpl_snapdir_revalidate(struct dentry *dentry, unsigned int flags)
#endif
{
	return (!!dentry->d_inode);
}

dentry_operations_t zpl_dops_snapdirs = {
/*
 * Auto mounting of snapshots is only supported for 2.6.37 and
 * newer kernels.  Prior to this kernel the ops->follow_link()
 * callback was used as a hack to trigger the mount.  The
 * resulting vfsmount was then explicitly grafted in to the
 * name space.  While it might be possible to add compatibility
 * code to accomplish this it would require considerable care.
 */
#ifdef HAVE_AUTOMOUNT
	.d_automount	= zpl_snapdir_automount,
#endif /* HAVE_AUTOMOUNT */
	.d_revalidate	= zpl_snapdir_revalidate,
};

static struct dentry *
#ifdef HAVE_LOOKUP_NAMEIDATA
zpl_snapdir_lookup(struct inode *dip, struct dentry *dentry,
    struct nameidata *nd)
#else
zpl_snapdir_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
#endif

{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	struct inode *ip = NULL;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfsctl_snapdir_lookup(dip, dname(dentry), &ip,
	    0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error && error != -ENOENT)
		return (ERR_PTR(error));

	ASSERT(error == 0 || ip == NULL);
	d_clear_d_op(dentry);
	d_set_d_op(dentry, &zpl_dops_snapdirs);
#ifdef HAVE_AUTOMOUNT
	dentry->d_flags |= DCACHE_NEED_AUTOMOUNT;
#endif

	return (d_splice_alias(ip, dentry));
}

static int
zpl_snapdir_iterate(struct file *filp, zpl_dir_context_t *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	fstrans_cookie_t cookie;
	char snapname[MAXNAMELEN];
	boolean_t case_conflict;
	uint64_t id, pos;
	int error = 0;

	ZFS_ENTER(zfsvfs);
	cookie = spl_fstrans_mark();

	if (!zpl_dir_emit_dots(filp, ctx))
		goto out;

	pos = ctx->pos;
	while (error == 0) {
		dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os), FTAG);
		error = -dmu_snapshot_list_next(zfsvfs->z_os, MAXNAMELEN,
		    snapname, &id, &pos, &case_conflict);
		dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os), FTAG);
		if (error)
			goto out;

		if (!zpl_dir_emit(ctx, snapname, strlen(snapname),
		    ZFSCTL_INO_SHARES - id, DT_DIR))
			goto out;

		ctx->pos = pos;
	}
out:
	spl_fstrans_unmark(cookie);
	ZFS_EXIT(zfsvfs);

	if (error == -ENOENT)
		return (0);

	return (error);
}

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_snapdir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_snapdir_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

static int
zpl_snapdir_rename2(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry, unsigned int flags)
{
	cred_t *cr = CRED();
	int error;

	/* We probably don't want to support renameat2(2) in ctldir */
	if (flags)
		return (-EINVAL);

	crhold(cr);
	error = -zfsctl_snapdir_rename(sdip, dname(sdentry),
	    tdip, dname(tdentry), cr, 0);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

#ifndef HAVE_RENAME_WANTS_FLAGS
static int
zpl_snapdir_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry)
{
	return (zpl_snapdir_rename2(sdip, sdentry, tdip, tdentry, 0));
}
#endif

static int
zpl_snapdir_rmdir(struct inode *dip, struct dentry *dentry)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfsctl_snapdir_remove(dip, dname(dentry), cr, 0);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

static int
zpl_snapdir_mkdir(struct inode *dip, struct dentry *dentry, zpl_umode_t mode)
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dip, mode | S_IFDIR, cr);

	error = -zfsctl_snapdir_mkdir(dip, dname(dentry), vap, &ip, cr, 0);
	if (error == 0) {
		d_clear_d_op(dentry);
		d_set_d_op(dentry, &zpl_dops_snapdirs);
		d_instantiate(dentry, ip);
	}

	kmem_free(vap, sizeof (vattr_t));
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

/*
 * Get snapshot directory attributes.
 */
/* ARGSUSED */
static int
zpl_snapdir_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
{
	struct inode *ip = path->dentry->d_inode;
	zfsvfs_t *zfsvfs = ITOZSB(ip);

	ZFS_ENTER(zfsvfs);
	generic_fillattr(ip, stat);

	stat->nlink = stat->size = 2;
	stat->ctime = stat->mtime = dmu_objset_snap_cmtime(zfsvfs->z_os);
	stat->atime = current_time(ip);
	ZFS_EXIT(zfsvfs);

	return (0);
}
ZPL_GETATTR_WRAPPER(zpl_snapdir_getattr);

/*
 * The '.zfs/snapshot' directory file operations.  These mainly control
 * generating the list of available snapshots when doing an 'ls' in the
 * directory.  See zpl_snapdir_readdir().
 */
const struct file_operations zpl_fops_snapdir = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_snapdir_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_snapdir_iterate,
#else
	.readdir	= zpl_snapdir_readdir,
#endif

};

/*
 * The '.zfs/snapshot' directory inode operations.  These mainly control
 * creating an inode for a snapshot directory and initializing the needed
 * infrastructure to automount the snapshot.  See zpl_snapdir_lookup().
 */
const struct inode_operations zpl_ops_snapdir = {
	.lookup		= zpl_snapdir_lookup,
	.getattr	= zpl_snapdir_getattr,
#ifdef HAVE_RENAME_WANTS_FLAGS
	.rename		= zpl_snapdir_rename2,
#else
	.rename		= zpl_snapdir_rename,
#endif
	.rmdir		= zpl_snapdir_rmdir,
	.mkdir		= zpl_snapdir_mkdir,
};

static struct dentry *
#ifdef HAVE_LOOKUP_NAMEIDATA
zpl_shares_lookup(struct inode *dip, struct dentry *dentry,
    struct nameidata *nd)
#else
zpl_shares_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
#endif
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	struct inode *ip = NULL;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfsctl_shares_lookup(dip, dname(dentry), &ip,
	    0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error) {
		if (error == -ENOENT)
			return (d_splice_alias(NULL, dentry));
		else
			return (ERR_PTR(error));
	}

	return (d_splice_alias(ip, dentry));
}

static int
zpl_shares_iterate(struct file *filp, zpl_dir_context_t *ctx)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	znode_t *dzp;
	int error = 0;

	ZFS_ENTER(zfsvfs);
	cookie = spl_fstrans_mark();

	if (zfsvfs->z_shares_dir == 0) {
		zpl_dir_emit_dots(filp, ctx);
		goto out;
	}

	error = -zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp);
	if (error)
		goto out;

	crhold(cr);
	error = -zfs_readdir(ZTOI(dzp), ctx, cr);
	crfree(cr);

	iput(ZTOI(dzp));
out:
	spl_fstrans_unmark(cookie);
	ZFS_EXIT(zfsvfs);
	ASSERT3S(error, <=, 0);

	return (error);
}

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_shares_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_shares_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

/* ARGSUSED */
static int
zpl_shares_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
{
	struct inode *ip = path->dentry->d_inode;
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	znode_t *dzp;
	int error;

	ZFS_ENTER(zfsvfs);

	if (zfsvfs->z_shares_dir == 0) {
		generic_fillattr(path->dentry->d_inode, stat);
		stat->nlink = stat->size = 2;
		stat->atime = current_time(ip);
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	error = -zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp);
	if (error == 0) {
		error = -zfs_getattr_fast(ZTOI(dzp), stat);
		iput(ZTOI(dzp));
	}

	ZFS_EXIT(zfsvfs);
	ASSERT3S(error, <=, 0);

	return (error);
}
ZPL_GETATTR_WRAPPER(zpl_shares_getattr);

/*
 * The '.zfs/shares' directory file operations.
 */
const struct file_operations zpl_fops_shares = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_shares_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_shares_iterate,
#else
	.readdir	= zpl_shares_readdir,
#endif

};

/*
 * The '.zfs/shares' directory inode operations.
 */
const struct inode_operations zpl_ops_shares = {
	.lookup		= zpl_shares_lookup,
	.getattr	= zpl_shares_getattr,
};
