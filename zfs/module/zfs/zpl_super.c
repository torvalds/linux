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
 */


#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>


static struct inode *
zpl_inode_alloc(struct super_block *sb)
{
	struct inode *ip;

	VERIFY3S(zfs_inode_alloc(sb, &ip), ==, 0);
	ip->i_version = 1;

	return (ip);
}

static void
zpl_inode_destroy(struct inode *ip)
{
	ASSERT(atomic_read(&ip->i_count) == 0);
	zfs_inode_destroy(ip);
}

/*
 * Called from __mark_inode_dirty() to reflect that something in the
 * inode has changed.  We use it to ensure the znode system attributes
 * are always strictly update to date with respect to the inode.
 */
#ifdef HAVE_DIRTY_INODE_WITH_FLAGS
static void
zpl_dirty_inode(struct inode *ip, int flags)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	zfs_dirty_inode(ip, flags);
	spl_fstrans_unmark(cookie);
}
#else
static void
zpl_dirty_inode(struct inode *ip)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	zfs_dirty_inode(ip, 0);
	spl_fstrans_unmark(cookie);
}
#endif /* HAVE_DIRTY_INODE_WITH_FLAGS */

/*
 * When ->drop_inode() is called its return value indicates if the
 * inode should be evicted from the inode cache.  If the inode is
 * unhashed and has no links the default policy is to evict it
 * immediately.
 *
 * Prior to 2.6.36 this eviction was accomplished by the vfs calling
 * ->delete_inode().  It was ->delete_inode()'s responsibility to
 * truncate the inode pages and call clear_inode().  The call to
 * clear_inode() synchronously invalidates all the buffers and
 * calls ->clear_inode().  It was ->clear_inode()'s responsibility
 * to cleanup and filesystem specific data before freeing the inode.
 *
 * This elaborate mechanism was replaced by ->evict_inode() which
 * does the job of both ->delete_inode() and ->clear_inode().  It
 * will be called exactly once, and when it returns the inode must
 * be in a state where it can simply be freed.i
 *
 * The ->evict_inode() callback must minimally truncate the inode pages,
 * and call clear_inode().  For 2.6.35 and later kernels this will
 * simply update the inode state, with the sync occurring before the
 * truncate in evict().  For earlier kernels clear_inode() maps to
 * end_writeback() which is responsible for completing all outstanding
 * write back.  In either case, once this is done it is safe to cleanup
 * any remaining inode specific data via zfs_inactive().
 * remaining filesystem specific data.
 */
#ifdef HAVE_EVICT_INODE
static void
zpl_evict_inode(struct inode *ip)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	truncate_setsize(ip, 0);
	clear_inode(ip);
	zfs_inactive(ip);
	spl_fstrans_unmark(cookie);
}

#else

static void
zpl_drop_inode(struct inode *ip)
{
	generic_delete_inode(ip);
}

static void
zpl_clear_inode(struct inode *ip)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	zfs_inactive(ip);
	spl_fstrans_unmark(cookie);
}

static void
zpl_inode_delete(struct inode *ip)
{
	truncate_setsize(ip, 0);
	clear_inode(ip);
}
#endif /* HAVE_EVICT_INODE */

static void
zpl_put_super(struct super_block *sb)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_umount(sb);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);
}

static int
zpl_sync_fs(struct super_block *sb, int wait)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_sync(sb, wait, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_statfs(struct dentry *dentry, struct kstatfs *statp)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_statvfs(dentry, statp);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

enum {
	TOKEN_RO,
	TOKEN_RW,
	TOKEN_SETUID,
	TOKEN_NOSETUID,
	TOKEN_EXEC,
	TOKEN_NOEXEC,
	TOKEN_DEVICES,
	TOKEN_NODEVICES,
	TOKEN_DIRXATTR,
	TOKEN_SAXATTR,
	TOKEN_XATTR,
	TOKEN_NOXATTR,
	TOKEN_ATIME,
	TOKEN_NOATIME,
	TOKEN_RELATIME,
	TOKEN_NORELATIME,
	TOKEN_NBMAND,
	TOKEN_NONBMAND,
	TOKEN_MNTPOINT,
	TOKEN_LAST,
};

static const match_table_t zpl_tokens = {
	{ TOKEN_RO,		MNTOPT_RO },
	{ TOKEN_RW,		MNTOPT_RW },
	{ TOKEN_SETUID,		MNTOPT_SETUID },
	{ TOKEN_NOSETUID,	MNTOPT_NOSETUID },
	{ TOKEN_EXEC,		MNTOPT_EXEC },
	{ TOKEN_NOEXEC,		MNTOPT_NOEXEC },
	{ TOKEN_DEVICES,	MNTOPT_DEVICES },
	{ TOKEN_NODEVICES,	MNTOPT_NODEVICES },
	{ TOKEN_DIRXATTR,	MNTOPT_DIRXATTR },
	{ TOKEN_SAXATTR,	MNTOPT_SAXATTR },
	{ TOKEN_XATTR,		MNTOPT_XATTR },
	{ TOKEN_NOXATTR,	MNTOPT_NOXATTR },
	{ TOKEN_ATIME,		MNTOPT_ATIME },
	{ TOKEN_NOATIME,	MNTOPT_NOATIME },
	{ TOKEN_RELATIME,	MNTOPT_RELATIME },
	{ TOKEN_NORELATIME,	MNTOPT_NORELATIME },
	{ TOKEN_NBMAND,		MNTOPT_NBMAND },
	{ TOKEN_NONBMAND,	MNTOPT_NONBMAND },
	{ TOKEN_MNTPOINT,	MNTOPT_MNTPOINT "=%s" },
	{ TOKEN_LAST,		NULL },
};

static int
zpl_parse_option(char *option, int token, substring_t *args, zfs_mntopts_t *zmo)
{
	switch (token) {
	case TOKEN_RO:
		zmo->z_readonly = B_TRUE;
		zmo->z_do_readonly = B_TRUE;
		break;
	case TOKEN_RW:
		zmo->z_readonly = B_FALSE;
		zmo->z_do_readonly = B_TRUE;
		break;
	case TOKEN_SETUID:
		zmo->z_setuid = B_TRUE;
		zmo->z_do_setuid = B_TRUE;
		break;
	case TOKEN_NOSETUID:
		zmo->z_setuid = B_FALSE;
		zmo->z_do_setuid = B_TRUE;
		break;
	case TOKEN_EXEC:
		zmo->z_exec = B_TRUE;
		zmo->z_do_exec = B_TRUE;
		break;
	case TOKEN_NOEXEC:
		zmo->z_exec = B_FALSE;
		zmo->z_do_exec = B_TRUE;
		break;
	case TOKEN_DEVICES:
		zmo->z_devices = B_TRUE;
		zmo->z_do_devices = B_TRUE;
		break;
	case TOKEN_NODEVICES:
		zmo->z_devices = B_FALSE;
		zmo->z_do_devices = B_TRUE;
		break;
	case TOKEN_DIRXATTR:
		zmo->z_xattr = ZFS_XATTR_DIR;
		zmo->z_do_xattr = B_TRUE;
		break;
	case TOKEN_SAXATTR:
		zmo->z_xattr = ZFS_XATTR_SA;
		zmo->z_do_xattr = B_TRUE;
		break;
	case TOKEN_XATTR:
		zmo->z_xattr = ZFS_XATTR_DIR;
		zmo->z_do_xattr = B_TRUE;
		break;
	case TOKEN_NOXATTR:
		zmo->z_xattr = ZFS_XATTR_OFF;
		zmo->z_do_xattr = B_TRUE;
		break;
	case TOKEN_ATIME:
		zmo->z_atime = B_TRUE;
		zmo->z_do_atime = B_TRUE;
		break;
	case TOKEN_NOATIME:
		zmo->z_atime = B_FALSE;
		zmo->z_do_atime = B_TRUE;
		break;
	case TOKEN_RELATIME:
		zmo->z_relatime = B_TRUE;
		zmo->z_do_relatime = B_TRUE;
		break;
	case TOKEN_NORELATIME:
		zmo->z_relatime = B_FALSE;
		zmo->z_do_relatime = B_TRUE;
		break;
	case TOKEN_NBMAND:
		zmo->z_nbmand = B_TRUE;
		zmo->z_do_nbmand = B_TRUE;
		break;
	case TOKEN_NONBMAND:
		zmo->z_nbmand = B_FALSE;
		zmo->z_do_nbmand = B_TRUE;
		break;
	case TOKEN_MNTPOINT:
		zmo->z_mntpoint = match_strdup(&args[0]);
		if (zmo->z_mntpoint == NULL)
			return (-ENOMEM);

		break;
	default:
		break;
	}

	return (0);
}

/*
 * Parse the mntopts string storing the results in provided zmo argument.
 * If an error occurs the zmo argument will not be modified.  The caller
 * needs to set isremount when recycling an existing zfs_mntopts_t.
 */
static int
zpl_parse_options(char *osname, char *mntopts, zfs_mntopts_t *zmo,
    boolean_t isremount)
{
	zfs_mntopts_t *tmp_zmo;
	int error;

	tmp_zmo = zfs_mntopts_alloc();
	tmp_zmo->z_osname = strdup(osname);

	if (mntopts) {
		substring_t args[MAX_OPT_ARGS];
		char *tmp_mntopts, *p, *t;
		int token;

		t = tmp_mntopts = strdup(mntopts);

		while ((p = strsep(&t, ",")) != NULL) {
			if (!*p)
				continue;

			args[0].to = args[0].from = NULL;
			token = match_token(p, zpl_tokens, args);
			error = zpl_parse_option(p, token, args, tmp_zmo);
			if (error) {
				zfs_mntopts_free(tmp_zmo);
				strfree(tmp_mntopts);
				return (error);
			}
		}

		strfree(tmp_mntopts);
	}

	if (isremount == B_TRUE) {
		if (zmo->z_osname)
			strfree(zmo->z_osname);

		if (zmo->z_mntpoint)
			strfree(zmo->z_mntpoint);
	} else {
		ASSERT3P(zmo->z_osname, ==, NULL);
		ASSERT3P(zmo->z_mntpoint, ==, NULL);
	}

	memcpy(zmo, tmp_zmo, sizeof (zfs_mntopts_t));
	kmem_free(tmp_zmo, sizeof (zfs_mntopts_t));

	return (0);
}

static int
zpl_remount_fs(struct super_block *sb, int *flags, char *data)
{
	zfs_sb_t *zsb = sb->s_fs_info;
	fstrans_cookie_t cookie;
	int error;

	error = zpl_parse_options(zsb->z_mntopts->z_osname, data,
	    zsb->z_mntopts, B_TRUE);
	if (error)
		return (error);

	cookie = spl_fstrans_mark();
	error = -zfs_remount(sb, flags, zsb->z_mntopts);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
__zpl_show_options(struct seq_file *seq, zfs_sb_t *zsb)
{
	seq_printf(seq, ",%s", zsb->z_flags & ZSB_XATTR ? "xattr" : "noxattr");

#ifdef CONFIG_FS_POSIX_ACL
	switch (zsb->z_acl_type) {
	case ZFS_ACLTYPE_POSIXACL:
		seq_puts(seq, ",posixacl");
		break;
	default:
		seq_puts(seq, ",noacl");
		break;
	}
#endif /* CONFIG_FS_POSIX_ACL */

	return (0);
}

#ifdef HAVE_SHOW_OPTIONS_WITH_DENTRY
static int
zpl_show_options(struct seq_file *seq, struct dentry *root)
{
	return (__zpl_show_options(seq, root->d_sb->s_fs_info));
}
#else
static int
zpl_show_options(struct seq_file *seq, struct vfsmount *vfsp)
{
	return (__zpl_show_options(seq, vfsp->mnt_sb->s_fs_info));
}
#endif /* HAVE_SHOW_OPTIONS_WITH_DENTRY */

static int
zpl_fill_super(struct super_block *sb, void *data, int silent)
{
	zfs_mntopts_t *zmo = (zfs_mntopts_t *)data;
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_domount(sb, zmo, silent);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifdef HAVE_MOUNT_NODEV
static struct dentry *
zpl_mount(struct file_system_type *fs_type, int flags,
    const char *osname, void *data)
{
	zfs_mntopts_t *zmo = zfs_mntopts_alloc();
	int error;

	error = zpl_parse_options((char *)osname, (char *)data, zmo, B_FALSE);
	if (error) {
		zfs_mntopts_free(zmo);
		return (ERR_PTR(error));
	}

	return (mount_nodev(fs_type, flags, zmo, zpl_fill_super));
}
#else
static int
zpl_get_sb(struct file_system_type *fs_type, int flags,
    const char *osname, void *data, struct vfsmount *mnt)
{
	zfs_mntopts_t *zmo = zfs_mntopts_alloc();
	int error;

	error = zpl_parse_options((char *)osname, (char *)data, zmo, B_FALSE);
	if (error) {
		zfs_mntopts_free(zmo);
		return (error);
	}

	return (get_sb_nodev(fs_type, flags, zmo, zpl_fill_super, mnt));
}
#endif /* HAVE_MOUNT_NODEV */

static void
zpl_kill_sb(struct super_block *sb)
{
	zfs_preumount(sb);
	kill_anon_super(sb);

#ifdef HAVE_S_INSTANCES_LIST_HEAD
	sb->s_instances.next = &(zpl_fs_type.fs_supers);
#endif /* HAVE_S_INSTANCES_LIST_HEAD */
}

void
zpl_prune_sb(int64_t nr_to_scan, void *arg)
{
	struct super_block *sb = (struct super_block *)arg;
	int objects = 0;

	(void) -zfs_sb_prune(sb, nr_to_scan, &objects);
}

#ifdef HAVE_NR_CACHED_OBJECTS
static int
zpl_nr_cached_objects(struct super_block *sb)
{
	return (0);
}
#endif /* HAVE_NR_CACHED_OBJECTS */

#ifdef HAVE_FREE_CACHED_OBJECTS
static void
zpl_free_cached_objects(struct super_block *sb, int nr_to_scan)
{
	/* noop */
}
#endif /* HAVE_FREE_CACHED_OBJECTS */

const struct super_operations zpl_super_operations = {
	.alloc_inode		= zpl_inode_alloc,
	.destroy_inode		= zpl_inode_destroy,
	.dirty_inode		= zpl_dirty_inode,
	.write_inode		= NULL,
#ifdef HAVE_EVICT_INODE
	.evict_inode		= zpl_evict_inode,
#else
	.drop_inode		= zpl_drop_inode,
	.clear_inode		= zpl_clear_inode,
	.delete_inode		= zpl_inode_delete,
#endif /* HAVE_EVICT_INODE */
	.put_super		= zpl_put_super,
	.sync_fs		= zpl_sync_fs,
	.statfs			= zpl_statfs,
	.remount_fs		= zpl_remount_fs,
	.show_options		= zpl_show_options,
	.show_stats		= NULL,
#ifdef HAVE_NR_CACHED_OBJECTS
	.nr_cached_objects	= zpl_nr_cached_objects,
#endif /* HAVE_NR_CACHED_OBJECTS */
#ifdef HAVE_FREE_CACHED_OBJECTS
	.free_cached_objects	= zpl_free_cached_objects,
#endif /* HAVE_FREE_CACHED_OBJECTS */
};

struct file_system_type zpl_fs_type = {
	.owner			= THIS_MODULE,
	.name			= ZFS_DRIVER,
#ifdef HAVE_MOUNT_NODEV
	.mount			= zpl_mount,
#else
	.get_sb			= zpl_get_sb,
#endif /* HAVE_MOUNT_NODEV */
	.kill_sb		= zpl_kill_sb,
};
