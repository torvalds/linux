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
	inode_set_iversion(ip, 1);

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

	/*
	 * If required by a 32-bit system call, dynamically scale the
	 * block size up to 16MiB and decrease the block counts.  This
	 * allows for a maximum size of 64EiB to be reported.  The file
	 * counts must be artificially capped at 2^32-1.
	 */
	if (unlikely(zpl_is_32bit_api())) {
		while (statp->f_blocks > UINT32_MAX &&
		    statp->f_bsize < SPA_MAXBLOCKSIZE) {
			statp->f_frsize <<= 1;
			statp->f_bsize <<= 1;

			statp->f_blocks >>= 1;
			statp->f_bfree >>= 1;
			statp->f_bavail >>= 1;
		}

		uint64_t usedobjs = statp->f_files - statp->f_ffree;
		statp->f_ffree = MIN(statp->f_ffree, UINT32_MAX - usedobjs);
		statp->f_files = statp->f_ffree + usedobjs;
	}

	return (error);
}

static int
zpl_remount_fs(struct super_block *sb, int *flags, char *data)
{
	zfs_mnt_t zm = { .mnt_osname = NULL, .mnt_data = data };
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_remount(sb, flags, &zm);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
__zpl_show_options(struct seq_file *seq, zfsvfs_t *zfsvfs)
{
	seq_printf(seq, ",%s",
	    zfsvfs->z_flags & ZSB_XATTR ? "xattr" : "noxattr");

#ifdef CONFIG_FS_POSIX_ACL
	switch (zfsvfs->z_acl_type) {
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
	zfs_mnt_t *zm = (zfs_mnt_t *)data;
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_domount(sb, zm, silent);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_test_super(struct super_block *s, void *data)
{
	zfsvfs_t *zfsvfs = s->s_fs_info;
	objset_t *os = data;

	if (zfsvfs == NULL)
		return (0);

	return (os == zfsvfs->z_os);
}

static struct super_block *
zpl_mount_impl(struct file_system_type *fs_type, int flags, zfs_mnt_t *zm)
{
	struct super_block *s;
	objset_t *os;
	int err;

	err = dmu_objset_hold(zm->mnt_osname, FTAG, &os);
	if (err)
		return (ERR_PTR(-err));

	/*
	 * The dsl pool lock must be released prior to calling sget().
	 * It is possible sget() may block on the lock in grab_super()
	 * while deactivate_super() holds that same lock and waits for
	 * a txg sync.  If the dsl_pool lock is held over over sget()
	 * this can prevent the pool sync and cause a deadlock.
	 */
	dsl_pool_rele(dmu_objset_pool(os), FTAG);
	s = zpl_sget(fs_type, zpl_test_super, set_anon_super, flags, os);
	dsl_dataset_rele(dmu_objset_ds(os), FTAG);

	if (IS_ERR(s))
		return (ERR_CAST(s));

	if (s->s_root == NULL) {
		err = zpl_fill_super(s, zm, flags & SB_SILENT ? 1 : 0);
		if (err) {
			deactivate_locked_super(s);
			return (ERR_PTR(err));
		}
		s->s_flags |= SB_ACTIVE;
	} else if ((flags ^ s->s_flags) & SB_RDONLY) {
		deactivate_locked_super(s);
		return (ERR_PTR(-EBUSY));
	}

	return (s);
}

#ifdef HAVE_FST_MOUNT
static struct dentry *
zpl_mount(struct file_system_type *fs_type, int flags,
    const char *osname, void *data)
{
	zfs_mnt_t zm = { .mnt_osname = osname, .mnt_data = data };

	struct super_block *sb = zpl_mount_impl(fs_type, flags, &zm);
	if (IS_ERR(sb))
		return (ERR_CAST(sb));

	return (dget(sb->s_root));
}
#else
static int
zpl_get_sb(struct file_system_type *fs_type, int flags,
    const char *osname, void *data, struct vfsmount *mnt)
{
	zfs_mnt_t zm = { .mnt_osname = osname, .mnt_data = data };

	struct super_block *sb = zpl_mount_impl(fs_type, flags, &zm);
	if (IS_ERR(sb))
		return (PTR_ERR(sb));

	(void) simple_set_mnt(mnt, sb);

	return (0);
}
#endif /* HAVE_FST_MOUNT */

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

	(void) -zfs_prune(sb, nr_to_scan, &objects);
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
#ifdef HAVE_FST_MOUNT
	.mount			= zpl_mount,
#else
	.get_sb			= zpl_get_sb,
#endif /* HAVE_FST_MOUNT */
	.kill_sb		= zpl_kill_sb,
};
