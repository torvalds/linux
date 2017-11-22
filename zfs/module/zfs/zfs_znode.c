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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 */

/* Portions Copyright 2007 Jeremy Teo */

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/mntent.h>
#include <sys/mkdev.h>
#include <sys/u8_textprep.h>
#include <sys/dsl_dataset.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/mode.h>
#include <sys/atomic.h>
#include <vm/pvn.h>
#include "fs/fs_subr.h"
#include <sys/zfs_dir.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_fuid.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ctldir.h>
#include <sys/dnode.h>
#include <sys/fs/zfs.h>
#include <sys/kidmap.h>
#include <sys/zpl.h>
#endif /* _KERNEL */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/refcount.h>
#include <sys/stat.h>
#include <sys/zap.h>
#include <sys/zfs_znode.h>
#include <sys/sa.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>

#include "zfs_prop.h"
#include "zfs_comutil.h"

/*
 * Define ZNODE_STATS to turn on statistic gathering. By default, it is only
 * turned on when DEBUG is also defined.
 */
#ifdef	DEBUG
#define	ZNODE_STATS
#endif	/* DEBUG */

#ifdef	ZNODE_STATS
#define	ZNODE_STAT_ADD(stat)			((stat)++)
#else
#define	ZNODE_STAT_ADD(stat)			/* nothing */
#endif	/* ZNODE_STATS */

/*
 * Functions needed for userland (ie: libzpool) are not put under
 * #ifdef_KERNEL; the rest of the functions have dependencies
 * (such as VFS logic) that will not compile easily in userland.
 */
#ifdef _KERNEL

static kmem_cache_t *znode_cache = NULL;
static kmem_cache_t *znode_hold_cache = NULL;
unsigned int zfs_object_mutex_size = ZFS_OBJ_MTX_SZ;

/*ARGSUSED*/
static int
zfs_znode_cache_constructor(void *buf, void *arg, int kmflags)
{
	znode_t *zp = buf;

	inode_init_once(ZTOI(zp));
	list_link_init(&zp->z_link_node);

	mutex_init(&zp->z_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&zp->z_parent_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&zp->z_name_lock, NULL, RW_NOLOCKDEP, NULL);
	mutex_init(&zp->z_acl_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&zp->z_xattr_lock, NULL, RW_DEFAULT, NULL);

	zfs_rlock_init(&zp->z_range_lock);

	zp->z_dirlocks = NULL;
	zp->z_acl_cached = NULL;
	zp->z_xattr_cached = NULL;
	zp->z_xattr_parent = 0;
	zp->z_moved = 0;
	return (0);
}

/*ARGSUSED*/
static void
zfs_znode_cache_destructor(void *buf, void *arg)
{
	znode_t *zp = buf;

	ASSERT(!list_link_active(&zp->z_link_node));
	mutex_destroy(&zp->z_lock);
	rw_destroy(&zp->z_parent_lock);
	rw_destroy(&zp->z_name_lock);
	mutex_destroy(&zp->z_acl_lock);
	rw_destroy(&zp->z_xattr_lock);
	zfs_rlock_destroy(&zp->z_range_lock);

	ASSERT(zp->z_dirlocks == NULL);
	ASSERT(zp->z_acl_cached == NULL);
	ASSERT(zp->z_xattr_cached == NULL);
}

static int
zfs_znode_hold_cache_constructor(void *buf, void *arg, int kmflags)
{
	znode_hold_t *zh = buf;

	mutex_init(&zh->zh_lock, NULL, MUTEX_DEFAULT, NULL);
	refcount_create(&zh->zh_refcount);
	zh->zh_obj = ZFS_NO_OBJECT;

	return (0);
}

static void
zfs_znode_hold_cache_destructor(void *buf, void *arg)
{
	znode_hold_t *zh = buf;

	mutex_destroy(&zh->zh_lock);
	refcount_destroy(&zh->zh_refcount);
}

void
zfs_znode_init(void)
{
	/*
	 * Initialize zcache.  The KMC_SLAB hint is used in order that it be
	 * backed by kmalloc() when on the Linux slab in order that any
	 * wait_on_bit() operations on the related inode operate properly.
	 */
	ASSERT(znode_cache == NULL);
	znode_cache = kmem_cache_create("zfs_znode_cache",
	    sizeof (znode_t), 0, zfs_znode_cache_constructor,
	    zfs_znode_cache_destructor, NULL, NULL, NULL, KMC_SLAB);

	ASSERT(znode_hold_cache == NULL);
	znode_hold_cache = kmem_cache_create("zfs_znode_hold_cache",
	    sizeof (znode_hold_t), 0, zfs_znode_hold_cache_constructor,
	    zfs_znode_hold_cache_destructor, NULL, NULL, NULL, 0);
}

void
zfs_znode_fini(void)
{
	/*
	 * Cleanup zcache
	 */
	if (znode_cache)
		kmem_cache_destroy(znode_cache);
	znode_cache = NULL;

	if (znode_hold_cache)
		kmem_cache_destroy(znode_hold_cache);
	znode_hold_cache = NULL;
}

/*
 * The zfs_znode_hold_enter() / zfs_znode_hold_exit() functions are used to
 * serialize access to a znode and its SA buffer while the object is being
 * created or destroyed.  This kind of locking would normally reside in the
 * znode itself but in this case that's impossible because the znode and SA
 * buffer may not yet exist.  Therefore the locking is handled externally
 * with an array of mutexs and AVLs trees which contain per-object locks.
 *
 * In zfs_znode_hold_enter() a per-object lock is created as needed, inserted
 * in to the correct AVL tree and finally the per-object lock is held.  In
 * zfs_znode_hold_exit() the process is reversed.  The per-object lock is
 * released, removed from the AVL tree and destroyed if there are no waiters.
 *
 * This scheme has two important properties:
 *
 * 1) No memory allocations are performed while holding one of the z_hold_locks.
 *    This ensures evict(), which can be called from direct memory reclaim, will
 *    never block waiting on a z_hold_locks which just happens to have hashed
 *    to the same index.
 *
 * 2) All locks used to serialize access to an object are per-object and never
 *    shared.  This minimizes lock contention without creating a large number
 *    of dedicated locks.
 *
 * On the downside it does require znode_lock_t structures to be frequently
 * allocated and freed.  However, because these are backed by a kmem cache
 * and very short lived this cost is minimal.
 */
int
zfs_znode_hold_compare(const void *a, const void *b)
{
	const znode_hold_t *zh_a = (const znode_hold_t *)a;
	const znode_hold_t *zh_b = (const znode_hold_t *)b;

	return (AVL_CMP(zh_a->zh_obj, zh_b->zh_obj));
}

boolean_t
zfs_znode_held(zfsvfs_t *zfsvfs, uint64_t obj)
{
	znode_hold_t *zh, search;
	int i = ZFS_OBJ_HASH(zfsvfs, obj);
	boolean_t held;

	search.zh_obj = obj;

	mutex_enter(&zfsvfs->z_hold_locks[i]);
	zh = avl_find(&zfsvfs->z_hold_trees[i], &search, NULL);
	held = (zh && MUTEX_HELD(&zh->zh_lock)) ? B_TRUE : B_FALSE;
	mutex_exit(&zfsvfs->z_hold_locks[i]);

	return (held);
}

static znode_hold_t *
zfs_znode_hold_enter(zfsvfs_t *zfsvfs, uint64_t obj)
{
	znode_hold_t *zh, *zh_new, search;
	int i = ZFS_OBJ_HASH(zfsvfs, obj);
	boolean_t found = B_FALSE;

	zh_new = kmem_cache_alloc(znode_hold_cache, KM_SLEEP);
	zh_new->zh_obj = obj;
	search.zh_obj = obj;

	mutex_enter(&zfsvfs->z_hold_locks[i]);
	zh = avl_find(&zfsvfs->z_hold_trees[i], &search, NULL);
	if (likely(zh == NULL)) {
		zh = zh_new;
		avl_add(&zfsvfs->z_hold_trees[i], zh);
	} else {
		ASSERT3U(zh->zh_obj, ==, obj);
		found = B_TRUE;
	}
	refcount_add(&zh->zh_refcount, NULL);
	mutex_exit(&zfsvfs->z_hold_locks[i]);

	if (found == B_TRUE)
		kmem_cache_free(znode_hold_cache, zh_new);

	ASSERT(MUTEX_NOT_HELD(&zh->zh_lock));
	ASSERT3S(refcount_count(&zh->zh_refcount), >, 0);
	mutex_enter(&zh->zh_lock);

	return (zh);
}

static void
zfs_znode_hold_exit(zfsvfs_t *zfsvfs, znode_hold_t *zh)
{
	int i = ZFS_OBJ_HASH(zfsvfs, zh->zh_obj);
	boolean_t remove = B_FALSE;

	ASSERT(zfs_znode_held(zfsvfs, zh->zh_obj));
	ASSERT3S(refcount_count(&zh->zh_refcount), >, 0);
	mutex_exit(&zh->zh_lock);

	mutex_enter(&zfsvfs->z_hold_locks[i]);
	if (refcount_remove(&zh->zh_refcount, NULL) == 0) {
		avl_remove(&zfsvfs->z_hold_trees[i], zh);
		remove = B_TRUE;
	}
	mutex_exit(&zfsvfs->z_hold_locks[i]);

	if (remove == B_TRUE)
		kmem_cache_free(znode_hold_cache, zh);
}

int
zfs_create_share_dir(zfsvfs_t *zfsvfs, dmu_tx_t *tx)
{
#ifdef HAVE_SMB_SHARE
	zfs_acl_ids_t acl_ids;
	vattr_t vattr;
	znode_t *sharezp;
	vnode_t *vp;
	znode_t *zp;
	int error;

	vattr.va_mask = AT_MODE|AT_UID|AT_GID|AT_TYPE;
	vattr.va_mode = S_IFDIR | 0555;
	vattr.va_uid = crgetuid(kcred);
	vattr.va_gid = crgetgid(kcred);

	sharezp = kmem_cache_alloc(znode_cache, KM_SLEEP);
	sharezp->z_moved = 0;
	sharezp->z_unlinked = 0;
	sharezp->z_atime_dirty = 0;
	sharezp->z_zfsvfs = zfsvfs;
	sharezp->z_is_sa = zfsvfs->z_use_sa;

	vp = ZTOV(sharezp);
	vn_reinit(vp);
	vp->v_type = VDIR;

	VERIFY(0 == zfs_acl_ids_create(sharezp, IS_ROOT_NODE, &vattr,
	    kcred, NULL, &acl_ids));
	zfs_mknode(sharezp, &vattr, tx, kcred, IS_ROOT_NODE, &zp, &acl_ids);
	ASSERT3P(zp, ==, sharezp);
	ASSERT(!vn_in_dnlc(ZTOV(sharezp))); /* not valid to move */
	POINTER_INVALIDATE(&sharezp->z_zfsvfs);
	error = zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
	    ZFS_SHARES_DIR, 8, 1, &sharezp->z_id, tx);
	zfsvfs->z_shares_dir = sharezp->z_id;

	zfs_acl_ids_free(&acl_ids);
	// ZTOV(sharezp)->v_count = 0;
	sa_handle_destroy(sharezp->z_sa_hdl);
	kmem_cache_free(znode_cache, sharezp);

	return (error);
#else
	return (0);
#endif /* HAVE_SMB_SHARE */
}

static void
zfs_znode_sa_init(zfsvfs_t *zfsvfs, znode_t *zp,
    dmu_buf_t *db, dmu_object_type_t obj_type, sa_handle_t *sa_hdl)
{
	ASSERT(zfs_znode_held(zfsvfs, zp->z_id));

	mutex_enter(&zp->z_lock);

	ASSERT(zp->z_sa_hdl == NULL);
	ASSERT(zp->z_acl_cached == NULL);
	if (sa_hdl == NULL) {
		VERIFY(0 == sa_handle_get_from_db(zfsvfs->z_os, db, zp,
		    SA_HDL_SHARED, &zp->z_sa_hdl));
	} else {
		zp->z_sa_hdl = sa_hdl;
		sa_set_userp(sa_hdl, zp);
	}

	zp->z_is_sa = (obj_type == DMU_OT_SA) ? B_TRUE : B_FALSE;

	mutex_exit(&zp->z_lock);
}

void
zfs_znode_dmu_fini(znode_t *zp)
{
	ASSERT(zfs_znode_held(ZTOZSB(zp), zp->z_id) || zp->z_unlinked ||
	    RW_WRITE_HELD(&ZTOZSB(zp)->z_teardown_inactive_lock));

	sa_handle_destroy(zp->z_sa_hdl);
	zp->z_sa_hdl = NULL;
}

/*
 * Called by new_inode() to allocate a new inode.
 */
int
zfs_inode_alloc(struct super_block *sb, struct inode **ip)
{
	znode_t *zp;

	zp = kmem_cache_alloc(znode_cache, KM_SLEEP);
	*ip = ZTOI(zp);

	return (0);
}

/*
 * Called in multiple places when an inode should be destroyed.
 */
void
zfs_inode_destroy(struct inode *ip)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);

	mutex_enter(&zfsvfs->z_znodes_lock);
	if (list_link_active(&zp->z_link_node)) {
		list_remove(&zfsvfs->z_all_znodes, zp);
		zfsvfs->z_nr_znodes--;
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

	if (zp->z_acl_cached) {
		zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = NULL;
	}

	if (zp->z_xattr_cached) {
		nvlist_free(zp->z_xattr_cached);
		zp->z_xattr_cached = NULL;
	}

	kmem_cache_free(znode_cache, zp);
}

static void
zfs_inode_set_ops(zfsvfs_t *zfsvfs, struct inode *ip)
{
	uint64_t rdev = 0;

	switch (ip->i_mode & S_IFMT) {
	case S_IFREG:
		ip->i_op = &zpl_inode_operations;
		ip->i_fop = &zpl_file_operations;
		ip->i_mapping->a_ops = &zpl_address_space_operations;
		break;

	case S_IFDIR:
		ip->i_op = &zpl_dir_inode_operations;
		ip->i_fop = &zpl_dir_file_operations;
		ITOZ(ip)->z_zn_prefetch = B_TRUE;
		break;

	case S_IFLNK:
		ip->i_op = &zpl_symlink_inode_operations;
		break;

	/*
	 * rdev is only stored in a SA only for device files.
	 */
	case S_IFCHR:
	case S_IFBLK:
		(void) sa_lookup(ITOZ(ip)->z_sa_hdl, SA_ZPL_RDEV(zfsvfs), &rdev,
		    sizeof (rdev));
		/*FALLTHROUGH*/
	case S_IFIFO:
	case S_IFSOCK:
		init_special_inode(ip, ip->i_mode, rdev);
		ip->i_op = &zpl_special_inode_operations;
		break;

	default:
		zfs_panic_recover("inode %llu has invalid mode: 0x%x\n",
		    (u_longlong_t)ip->i_ino, ip->i_mode);

		/* Assume the inode is a file and attempt to continue */
		ip->i_mode = S_IFREG | 0644;
		ip->i_op = &zpl_inode_operations;
		ip->i_fop = &zpl_file_operations;
		ip->i_mapping->a_ops = &zpl_address_space_operations;
		break;
	}
}

void
zfs_set_inode_flags(znode_t *zp, struct inode *ip)
{
	/*
	 * Linux and Solaris have different sets of file attributes, so we
	 * restrict this conversion to the intersection of the two.
	 */
#ifdef HAVE_INODE_SET_FLAGS
	unsigned int flags = 0;
	if (zp->z_pflags & ZFS_IMMUTABLE)
		flags |= S_IMMUTABLE;
	if (zp->z_pflags & ZFS_APPENDONLY)
		flags |= S_APPEND;

	inode_set_flags(ip, flags, S_IMMUTABLE|S_APPEND);
#else
	if (zp->z_pflags & ZFS_IMMUTABLE)
		ip->i_flags |= S_IMMUTABLE;
	else
		ip->i_flags &= ~S_IMMUTABLE;

	if (zp->z_pflags & ZFS_APPENDONLY)
		ip->i_flags |= S_APPEND;
	else
		ip->i_flags &= ~S_APPEND;
#endif
}

/*
 * Update the embedded inode given the znode.  We should work toward
 * eliminating this function as soon as possible by removing values
 * which are duplicated between the znode and inode.  If the generic
 * inode has the correct field it should be used, and the ZFS code
 * updated to access the inode.  This can be done incrementally.
 */
void
zfs_inode_update(znode_t *zp)
{
	zfsvfs_t	*zfsvfs;
	struct inode	*ip;
	uint32_t	blksize;
	u_longlong_t	i_blocks;

	ASSERT(zp != NULL);
	zfsvfs = ZTOZSB(zp);
	ip = ZTOI(zp);

	/* Skip .zfs control nodes which do not exist on disk. */
	if (zfsctl_is_node(ip))
		return;

	dmu_object_size_from_db(sa_get_db(zp->z_sa_hdl), &blksize, &i_blocks);

	spin_lock(&ip->i_lock);
	ip->i_blocks = i_blocks;
	i_size_write(ip, zp->z_size);
	spin_unlock(&ip->i_lock);
}


/*
 * Construct a znode+inode and initialize.
 *
 * This does not do a call to dmu_set_user() that is
 * up to the caller to do, in case you don't want to
 * return the znode
 */
static znode_t *
zfs_znode_alloc(zfsvfs_t *zfsvfs, dmu_buf_t *db, int blksz,
    dmu_object_type_t obj_type, uint64_t obj, sa_handle_t *hdl)
{
	znode_t	*zp;
	struct inode *ip;
	uint64_t mode;
	uint64_t parent;
	uint64_t tmp_gen;
	uint64_t links;
	uint64_t z_uid, z_gid;
	uint64_t atime[2], mtime[2], ctime[2];
	sa_bulk_attr_t bulk[11];
	int count = 0;

	ASSERT(zfsvfs != NULL);

	ip = new_inode(zfsvfs->z_sb);
	if (ip == NULL)
		return (NULL);

	zp = ITOZ(ip);
	ASSERT(zp->z_dirlocks == NULL);
	ASSERT3P(zp->z_acl_cached, ==, NULL);
	ASSERT3P(zp->z_xattr_cached, ==, NULL);
	zp->z_moved = 0;
	zp->z_sa_hdl = NULL;
	zp->z_unlinked = 0;
	zp->z_atime_dirty = 0;
	zp->z_mapcnt = 0;
	zp->z_id = db->db_object;
	zp->z_blksz = blksz;
	zp->z_seq = 0x7A4653;
	zp->z_sync_cnt = 0;
	zp->z_is_mapped = B_FALSE;
	zp->z_is_ctldir = B_FALSE;
	zp->z_is_stale = B_FALSE;
	zp->z_range_lock.zr_size = &zp->z_size;
	zp->z_range_lock.zr_blksz = &zp->z_blksz;
	zp->z_range_lock.zr_max_blksz = &ZTOZSB(zp)->z_max_blksz;

	zfs_znode_sa_init(zfsvfs, zp, db, obj_type, hdl);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL, &mode, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GEN(zfsvfs), NULL, &tmp_gen, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL, &links, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PARENT(zfsvfs), NULL,
	    &parent, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL, &z_uid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL, &z_gid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL, &atime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);

	if (sa_bulk_lookup(zp->z_sa_hdl, bulk, count) != 0 || tmp_gen == 0) {
		if (hdl == NULL)
			sa_handle_destroy(zp->z_sa_hdl);
		zp->z_sa_hdl = NULL;
		goto error;
	}

	zp->z_mode = ip->i_mode = mode;
	ip->i_generation = (uint32_t)tmp_gen;
	ip->i_blkbits = SPA_MINBLOCKSHIFT;
	set_nlink(ip, (uint32_t)links);
	zfs_uid_write(ip, z_uid);
	zfs_gid_write(ip, z_gid);
	zfs_set_inode_flags(zp, ip);

	/* Cache the xattr parent id */
	if (zp->z_pflags & ZFS_XATTR)
		zp->z_xattr_parent = parent;

	ZFS_TIME_DECODE(&ip->i_atime, atime);
	ZFS_TIME_DECODE(&ip->i_mtime, mtime);
	ZFS_TIME_DECODE(&ip->i_ctime, ctime);

	ip->i_ino = obj;
	zfs_inode_update(zp);
	zfs_inode_set_ops(zfsvfs, ip);

	/*
	 * The only way insert_inode_locked() can fail is if the ip->i_ino
	 * number is already hashed for this super block.  This can never
	 * happen because the inode numbers map 1:1 with the object numbers.
	 *
	 * The one exception is rolling back a mounted file system, but in
	 * this case all the active inode are unhashed during the rollback.
	 */
	VERIFY3S(insert_inode_locked(ip), ==, 0);

	mutex_enter(&zfsvfs->z_znodes_lock);
	list_insert_tail(&zfsvfs->z_all_znodes, zp);
	zfsvfs->z_nr_znodes++;
	membar_producer();
	mutex_exit(&zfsvfs->z_znodes_lock);

	unlock_new_inode(ip);
	return (zp);

error:
	iput(ip);
	return (NULL);
}

/*
 * Safely mark an inode dirty.  Inodes which are part of a read-only
 * file system or snapshot may not be dirtied.
 */
void
zfs_mark_inode_dirty(struct inode *ip)
{
	zfsvfs_t *zfsvfs = ITOZSB(ip);

	if (zfs_is_readonly(zfsvfs) || dmu_objset_is_snapshot(zfsvfs->z_os))
		return;

	mark_inode_dirty(ip);
}

static uint64_t empty_xattr;
static uint64_t pad[4];
static zfs_acl_phys_t acl_phys;
/*
 * Create a new DMU object to hold a zfs znode.
 *
 *	IN:	dzp	- parent directory for new znode
 *		vap	- file attributes for new znode
 *		tx	- dmu transaction id for zap operations
 *		cr	- credentials of caller
 *		flag	- flags:
 *			  IS_ROOT_NODE	- new object will be root
 *			  IS_XATTR	- new object is an attribute
 *		bonuslen - length of bonus buffer
 *		setaclp  - File/Dir initial ACL
 *		fuidp	 - Tracks fuid allocation.
 *
 *	OUT:	zpp	- allocated znode
 *
 */
void
zfs_mknode(znode_t *dzp, vattr_t *vap, dmu_tx_t *tx, cred_t *cr,
    uint_t flag, znode_t **zpp, zfs_acl_ids_t *acl_ids)
{
	uint64_t	crtime[2], atime[2], mtime[2], ctime[2];
	uint64_t	mode, size, links, parent, pflags;
	uint64_t	dzp_pflags = 0;
	uint64_t	rdev = 0;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	dmu_buf_t	*db;
	timestruc_t	now;
	uint64_t	gen, obj;
	int		bonuslen;
	int		dnodesize;
	sa_handle_t	*sa_hdl;
	dmu_object_type_t obj_type;
	sa_bulk_attr_t	*sa_attrs;
	int		cnt = 0;
	zfs_acl_locator_cb_t locate = { 0 };
	znode_hold_t	*zh;

	if (zfsvfs->z_replay) {
		obj = vap->va_nodeid;
		now = vap->va_ctime;		/* see zfs_replay_create() */
		gen = vap->va_nblocks;		/* ditto */
		dnodesize = vap->va_fsid;	/* ditto */
	} else {
		obj = 0;
		gethrestime(&now);
		gen = dmu_tx_get_txg(tx);
		dnodesize = dmu_objset_dnodesize(zfsvfs->z_os);
	}

	if (dnodesize == 0)
		dnodesize = DNODE_MIN_SIZE;

	obj_type = zfsvfs->z_use_sa ? DMU_OT_SA : DMU_OT_ZNODE;

	bonuslen = (obj_type == DMU_OT_SA) ?
	    DN_BONUS_SIZE(dnodesize) : ZFS_OLD_ZNODE_PHYS_SIZE;

	/*
	 * Create a new DMU object.
	 */
	/*
	 * There's currently no mechanism for pre-reading the blocks that will
	 * be needed to allocate a new object, so we accept the small chance
	 * that there will be an i/o error and we will fail one of the
	 * assertions below.
	 */
	if (S_ISDIR(vap->va_mode)) {
		if (zfsvfs->z_replay) {
			VERIFY0(zap_create_claim_norm_dnsize(zfsvfs->z_os, obj,
			    zfsvfs->z_norm, DMU_OT_DIRECTORY_CONTENTS,
			    obj_type, bonuslen, dnodesize, tx));
		} else {
			obj = zap_create_norm_dnsize(zfsvfs->z_os,
			    zfsvfs->z_norm, DMU_OT_DIRECTORY_CONTENTS,
			    obj_type, bonuslen, dnodesize, tx);
		}
	} else {
		if (zfsvfs->z_replay) {
			VERIFY0(dmu_object_claim_dnsize(zfsvfs->z_os, obj,
			    DMU_OT_PLAIN_FILE_CONTENTS, 0,
			    obj_type, bonuslen, dnodesize, tx));
		} else {
			obj = dmu_object_alloc_dnsize(zfsvfs->z_os,
			    DMU_OT_PLAIN_FILE_CONTENTS, 0,
			    obj_type, bonuslen, dnodesize, tx);
		}
	}

	zh = zfs_znode_hold_enter(zfsvfs, obj);
	VERIFY0(sa_buf_hold(zfsvfs->z_os, obj, NULL, &db));

	/*
	 * If this is the root, fix up the half-initialized parent pointer
	 * to reference the just-allocated physical data area.
	 */
	if (flag & IS_ROOT_NODE) {
		dzp->z_id = obj;
	} else {
		dzp_pflags = dzp->z_pflags;
	}

	/*
	 * If parent is an xattr, so am I.
	 */
	if (dzp_pflags & ZFS_XATTR) {
		flag |= IS_XATTR;
	}

	if (zfsvfs->z_use_fuids)
		pflags = ZFS_ARCHIVE | ZFS_AV_MODIFIED;
	else
		pflags = 0;

	if (S_ISDIR(vap->va_mode)) {
		size = 2;		/* contents ("." and "..") */
		links = 2;
	} else {
		size = 0;
		links = (flag & IS_TMPFILE) ? 0 : 1;
	}

	if (S_ISBLK(vap->va_mode) || S_ISCHR(vap->va_mode))
		rdev = vap->va_rdev;

	parent = dzp->z_id;
	mode = acl_ids->z_mode;
	if (flag & IS_XATTR)
		pflags |= ZFS_XATTR;

	/*
	 * No execs denied will be deterimed when zfs_mode_compute() is called.
	 */
	pflags |= acl_ids->z_aclp->z_hints &
	    (ZFS_ACL_TRIVIAL|ZFS_INHERIT_ACE|ZFS_ACL_AUTO_INHERIT|
	    ZFS_ACL_DEFAULTED|ZFS_ACL_PROTECTED);

	ZFS_TIME_ENCODE(&now, crtime);
	ZFS_TIME_ENCODE(&now, ctime);

	if (vap->va_mask & ATTR_ATIME) {
		ZFS_TIME_ENCODE(&vap->va_atime, atime);
	} else {
		ZFS_TIME_ENCODE(&now, atime);
	}

	if (vap->va_mask & ATTR_MTIME) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
	} else {
		ZFS_TIME_ENCODE(&now, mtime);
	}

	/* Now add in all of the "SA" attributes */
	VERIFY(0 == sa_handle_get_from_db(zfsvfs->z_os, db, NULL, SA_HDL_SHARED,
	    &sa_hdl));

	/*
	 * Setup the array of attributes to be replaced/set on the new file
	 *
	 * order for  DMU_OT_ZNODE is critical since it needs to be constructed
	 * in the old znode_phys_t format.  Don't change this ordering
	 */
	sa_attrs = kmem_alloc(sizeof (sa_bulk_attr_t) * ZPL_END, KM_SLEEP);

	if (obj_type == DMU_OT_ZNODE) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_ATIME(zfsvfs),
		    NULL, &atime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MTIME(zfsvfs),
		    NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CTIME(zfsvfs),
		    NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CRTIME(zfsvfs),
		    NULL, &crtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GEN(zfsvfs),
		    NULL, &gen, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MODE(zfsvfs),
		    NULL, &mode, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_SIZE(zfsvfs),
		    NULL, &size, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_PARENT(zfsvfs),
		    NULL, &parent, 8);
	} else {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MODE(zfsvfs),
		    NULL, &mode, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_SIZE(zfsvfs),
		    NULL, &size, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GEN(zfsvfs),
		    NULL, &gen, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_UID(zfsvfs),
		    NULL, &acl_ids->z_fuid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GID(zfsvfs),
		    NULL, &acl_ids->z_fgid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_PARENT(zfsvfs),
		    NULL, &parent, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &pflags, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_ATIME(zfsvfs),
		    NULL, &atime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MTIME(zfsvfs),
		    NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CTIME(zfsvfs),
		    NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CRTIME(zfsvfs),
		    NULL, &crtime, 16);
	}

	SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_LINKS(zfsvfs), NULL, &links, 8);

	if (obj_type == DMU_OT_ZNODE) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_XATTR(zfsvfs), NULL,
		    &empty_xattr, 8);
	}
	if (obj_type == DMU_OT_ZNODE ||
	    (S_ISBLK(vap->va_mode) || S_ISCHR(vap->va_mode))) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_RDEV(zfsvfs),
		    NULL, &rdev, 8);
	}
	if (obj_type == DMU_OT_ZNODE) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &pflags, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_UID(zfsvfs), NULL,
		    &acl_ids->z_fuid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GID(zfsvfs), NULL,
		    &acl_ids->z_fgid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_PAD(zfsvfs), NULL, pad,
		    sizeof (uint64_t) * 4);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_ZNODE_ACL(zfsvfs), NULL,
		    &acl_phys, sizeof (zfs_acl_phys_t));
	} else if (acl_ids->z_aclp->z_version >= ZFS_ACL_VERSION_FUID) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_DACL_COUNT(zfsvfs), NULL,
		    &acl_ids->z_aclp->z_acl_count, 8);
		locate.cb_aclp = acl_ids->z_aclp;
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_DACL_ACES(zfsvfs),
		    zfs_acl_data_locator, &locate,
		    acl_ids->z_aclp->z_acl_bytes);
		mode = zfs_mode_compute(mode, acl_ids->z_aclp, &pflags,
		    acl_ids->z_fuid, acl_ids->z_fgid);
	}

	VERIFY(sa_replace_all_by_template(sa_hdl, sa_attrs, cnt, tx) == 0);

	if (!(flag & IS_ROOT_NODE)) {
		/*
		 * The call to zfs_znode_alloc() may fail if memory is low
		 * via the call path: alloc_inode() -> inode_init_always() ->
		 * security_inode_alloc() -> inode_alloc_security().  Since
		 * the existing code is written such that zfs_mknode() can
		 * not fail retry until sufficient memory has been reclaimed.
		 */
		do {
			*zpp = zfs_znode_alloc(zfsvfs, db, 0, obj_type, obj,
			    sa_hdl);
		} while (*zpp == NULL);

		VERIFY(*zpp != NULL);
		VERIFY(dzp != NULL);
	} else {
		/*
		 * If we are creating the root node, the "parent" we
		 * passed in is the znode for the root.
		 */
		*zpp = dzp;

		(*zpp)->z_sa_hdl = sa_hdl;
	}

	(*zpp)->z_pflags = pflags;
	(*zpp)->z_mode = ZTOI(*zpp)->i_mode = mode;
	(*zpp)->z_dnodesize = dnodesize;

	if (obj_type == DMU_OT_ZNODE ||
	    acl_ids->z_aclp->z_version < ZFS_ACL_VERSION_FUID) {
		VERIFY0(zfs_aclset_common(*zpp, acl_ids->z_aclp, cr, tx));
	}
	kmem_free(sa_attrs, sizeof (sa_bulk_attr_t) * ZPL_END);
	zfs_znode_hold_exit(zfsvfs, zh);
}

/*
 * Update in-core attributes.  It is assumed the caller will be doing an
 * sa_bulk_update to push the changes out.
 */
void
zfs_xvattr_set(znode_t *zp, xvattr_t *xvap, dmu_tx_t *tx)
{
	xoptattr_t *xoap;
	boolean_t update_inode = B_FALSE;

	xoap = xva_getxoptattr(xvap);
	ASSERT(xoap);

	if (XVA_ISSET_REQ(xvap, XAT_CREATETIME)) {
		uint64_t times[2];
		ZFS_TIME_ENCODE(&xoap->xoa_createtime, times);
		(void) sa_update(zp->z_sa_hdl, SA_ZPL_CRTIME(ZTOZSB(zp)),
		    &times, sizeof (times), tx);
		XVA_SET_RTN(xvap, XAT_CREATETIME);
	}
	if (XVA_ISSET_REQ(xvap, XAT_READONLY)) {
		ZFS_ATTR_SET(zp, ZFS_READONLY, xoap->xoa_readonly,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_READONLY);
	}
	if (XVA_ISSET_REQ(xvap, XAT_HIDDEN)) {
		ZFS_ATTR_SET(zp, ZFS_HIDDEN, xoap->xoa_hidden,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_HIDDEN);
	}
	if (XVA_ISSET_REQ(xvap, XAT_SYSTEM)) {
		ZFS_ATTR_SET(zp, ZFS_SYSTEM, xoap->xoa_system,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_SYSTEM);
	}
	if (XVA_ISSET_REQ(xvap, XAT_ARCHIVE)) {
		ZFS_ATTR_SET(zp, ZFS_ARCHIVE, xoap->xoa_archive,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_ARCHIVE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
		ZFS_ATTR_SET(zp, ZFS_IMMUTABLE, xoap->xoa_immutable,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_IMMUTABLE);

		update_inode = B_TRUE;
	}
	if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
		ZFS_ATTR_SET(zp, ZFS_NOUNLINK, xoap->xoa_nounlink,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_NOUNLINK);
	}
	if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
		ZFS_ATTR_SET(zp, ZFS_APPENDONLY, xoap->xoa_appendonly,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_APPENDONLY);

		update_inode = B_TRUE;
	}
	if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
		ZFS_ATTR_SET(zp, ZFS_NODUMP, xoap->xoa_nodump,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_NODUMP);
	}
	if (XVA_ISSET_REQ(xvap, XAT_OPAQUE)) {
		ZFS_ATTR_SET(zp, ZFS_OPAQUE, xoap->xoa_opaque,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_OPAQUE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
		ZFS_ATTR_SET(zp, ZFS_AV_QUARANTINED,
		    xoap->xoa_av_quarantined, zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_AV_QUARANTINED);
	}
	if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
		ZFS_ATTR_SET(zp, ZFS_AV_MODIFIED, xoap->xoa_av_modified,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_AV_MODIFIED);
	}
	if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP)) {
		zfs_sa_set_scanstamp(zp, xvap, tx);
		XVA_SET_RTN(xvap, XAT_AV_SCANSTAMP);
	}
	if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
		ZFS_ATTR_SET(zp, ZFS_REPARSE, xoap->xoa_reparse,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_REPARSE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_OFFLINE)) {
		ZFS_ATTR_SET(zp, ZFS_OFFLINE, xoap->xoa_offline,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_OFFLINE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_SPARSE)) {
		ZFS_ATTR_SET(zp, ZFS_SPARSE, xoap->xoa_sparse,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_SPARSE);
	}

	if (update_inode)
		zfs_set_inode_flags(zp, ZTOI(zp));
}

int
zfs_zget(zfsvfs_t *zfsvfs, uint64_t obj_num, znode_t **zpp)
{
	dmu_object_info_t doi;
	dmu_buf_t	*db;
	znode_t		*zp;
	znode_hold_t	*zh;
	int err;
	sa_handle_t	*hdl;

	*zpp = NULL;

again:
	zh = zfs_znode_hold_enter(zfsvfs, obj_num);

	err = sa_buf_hold(zfsvfs->z_os, obj_num, NULL, &db);
	if (err) {
		zfs_znode_hold_exit(zfsvfs, zh);
		return (err);
	}

	dmu_object_info_from_db(db, &doi);
	if (doi.doi_bonus_type != DMU_OT_SA &&
	    (doi.doi_bonus_type != DMU_OT_ZNODE ||
	    (doi.doi_bonus_type == DMU_OT_ZNODE &&
	    doi.doi_bonus_size < sizeof (znode_phys_t)))) {
		sa_buf_rele(db, NULL);
		zfs_znode_hold_exit(zfsvfs, zh);
		return (SET_ERROR(EINVAL));
	}

	hdl = dmu_buf_get_user(db);
	if (hdl != NULL) {
		zp = sa_get_userdata(hdl);


		/*
		 * Since "SA" does immediate eviction we
		 * should never find a sa handle that doesn't
		 * know about the znode.
		 */

		ASSERT3P(zp, !=, NULL);

		mutex_enter(&zp->z_lock);
		ASSERT3U(zp->z_id, ==, obj_num);
		/*
		 * If igrab() returns NULL the VFS has independently
		 * determined the inode should be evicted and has
		 * called iput_final() to start the eviction process.
		 * The SA handle is still valid but because the VFS
		 * requires that the eviction succeed we must drop
		 * our locks and references to allow the eviction to
		 * complete.  The zfs_zget() may then be retried.
		 *
		 * This unlikely case could be optimized by registering
		 * a sops->drop_inode() callback.  The callback would
		 * need to detect the active SA hold thereby informing
		 * the VFS that this inode should not be evicted.
		 */
		if (igrab(ZTOI(zp)) == NULL) {
			mutex_exit(&zp->z_lock);
			sa_buf_rele(db, NULL);
			zfs_znode_hold_exit(zfsvfs, zh);
			/* inode might need this to finish evict */
			cond_resched();
			goto again;
		}
		*zpp = zp;
		err = 0;
		mutex_exit(&zp->z_lock);
		sa_buf_rele(db, NULL);
		zfs_znode_hold_exit(zfsvfs, zh);
		return (err);
	}

	/*
	 * Not found create new znode/vnode but only if file exists.
	 *
	 * There is a small window where zfs_vget() could
	 * find this object while a file create is still in
	 * progress.  This is checked for in zfs_znode_alloc()
	 *
	 * if zfs_znode_alloc() fails it will drop the hold on the
	 * bonus buffer.
	 */
	zp = zfs_znode_alloc(zfsvfs, db, doi.doi_data_block_size,
	    doi.doi_bonus_type, obj_num, NULL);
	if (zp == NULL) {
		err = SET_ERROR(ENOENT);
	} else {
		*zpp = zp;
	}
	zfs_znode_hold_exit(zfsvfs, zh);
	return (err);
}

int
zfs_rezget(znode_t *zp)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	dmu_object_info_t doi;
	dmu_buf_t *db;
	uint64_t obj_num = zp->z_id;
	uint64_t mode;
	uint64_t links;
	sa_bulk_attr_t bulk[10];
	int err;
	int count = 0;
	uint64_t gen;
	uint64_t z_uid, z_gid;
	uint64_t atime[2], mtime[2], ctime[2];
	znode_hold_t *zh;

	/*
	 * skip ctldir, otherwise they will always get invalidated. This will
	 * cause funny behaviour for the mounted snapdirs. Especially for
	 * Linux >= 3.18, d_invalidate will detach the mountpoint and prevent
	 * anyone automount it again as long as someone is still using the
	 * detached mount.
	 */
	if (zp->z_is_ctldir)
		return (0);

	zh = zfs_znode_hold_enter(zfsvfs, obj_num);

	mutex_enter(&zp->z_acl_lock);
	if (zp->z_acl_cached) {
		zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = NULL;
	}
	mutex_exit(&zp->z_acl_lock);

	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	if (zp->z_xattr_cached) {
		nvlist_free(zp->z_xattr_cached);
		zp->z_xattr_cached = NULL;
	}
	rw_exit(&zp->z_xattr_lock);

	ASSERT(zp->z_sa_hdl == NULL);
	err = sa_buf_hold(zfsvfs->z_os, obj_num, NULL, &db);
	if (err) {
		zfs_znode_hold_exit(zfsvfs, zh);
		return (err);
	}

	dmu_object_info_from_db(db, &doi);
	if (doi.doi_bonus_type != DMU_OT_SA &&
	    (doi.doi_bonus_type != DMU_OT_ZNODE ||
	    (doi.doi_bonus_type == DMU_OT_ZNODE &&
	    doi.doi_bonus_size < sizeof (znode_phys_t)))) {
		sa_buf_rele(db, NULL);
		zfs_znode_hold_exit(zfsvfs, zh);
		return (SET_ERROR(EINVAL));
	}

	zfs_znode_sa_init(zfsvfs, zp, db, doi.doi_bonus_type, NULL);

	/* reload cached values */
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GEN(zfsvfs), NULL,
	    &gen, sizeof (gen));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, sizeof (zp->z_size));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &links, sizeof (links));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
	    &z_uid, sizeof (z_uid));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
	    &z_gid, sizeof (z_gid));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL,
	    &mode, sizeof (mode));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
	    &atime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
	    &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
	    &ctime, 16);

	if (sa_bulk_lookup(zp->z_sa_hdl, bulk, count)) {
		zfs_znode_dmu_fini(zp);
		zfs_znode_hold_exit(zfsvfs, zh);
		return (SET_ERROR(EIO));
	}

	zp->z_mode = ZTOI(zp)->i_mode = mode;
	zfs_uid_write(ZTOI(zp), z_uid);
	zfs_gid_write(ZTOI(zp), z_gid);

	ZFS_TIME_DECODE(&ZTOI(zp)->i_atime, atime);
	ZFS_TIME_DECODE(&ZTOI(zp)->i_mtime, mtime);
	ZFS_TIME_DECODE(&ZTOI(zp)->i_ctime, ctime);

	if (gen != ZTOI(zp)->i_generation) {
		zfs_znode_dmu_fini(zp);
		zfs_znode_hold_exit(zfsvfs, zh);
		return (SET_ERROR(EIO));
	}

	zp->z_unlinked = (ZTOI(zp)->i_nlink == 0);
	set_nlink(ZTOI(zp), (uint32_t)links);
	zfs_set_inode_flags(zp, ZTOI(zp));

	zp->z_blksz = doi.doi_data_block_size;
	zp->z_atime_dirty = 0;
	zfs_inode_update(zp);

	zfs_znode_hold_exit(zfsvfs, zh);

	return (0);
}

void
zfs_znode_delete(znode_t *zp, dmu_tx_t *tx)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	objset_t *os = zfsvfs->z_os;
	uint64_t obj = zp->z_id;
	uint64_t acl_obj = zfs_external_acl(zp);
	znode_hold_t *zh;

	zh = zfs_znode_hold_enter(zfsvfs, obj);
	if (acl_obj) {
		VERIFY(!zp->z_is_sa);
		VERIFY(0 == dmu_object_free(os, acl_obj, tx));
	}
	VERIFY(0 == dmu_object_free(os, obj, tx));
	zfs_znode_dmu_fini(zp);
	zfs_znode_hold_exit(zfsvfs, zh);
}

void
zfs_zinactive(znode_t *zp)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	uint64_t z_id = zp->z_id;
	znode_hold_t *zh;

	ASSERT(zp->z_sa_hdl);

	/*
	 * Don't allow a zfs_zget() while were trying to release this znode.
	 */
	zh = zfs_znode_hold_enter(zfsvfs, z_id);

	mutex_enter(&zp->z_lock);

	/*
	 * If this was the last reference to a file with no links,
	 * remove the file from the file system.
	 */
	if (zp->z_unlinked) {
		mutex_exit(&zp->z_lock);
		zfs_znode_hold_exit(zfsvfs, zh);
		zfs_rmnode(zp);
		return;
	}

	mutex_exit(&zp->z_lock);
	zfs_znode_dmu_fini(zp);

	zfs_znode_hold_exit(zfsvfs, zh);
}

static inline int
zfs_compare_timespec(struct timespec *t1, struct timespec *t2)
{
	if (t1->tv_sec < t2->tv_sec)
		return (-1);

	if (t1->tv_sec > t2->tv_sec)
		return (1);

	return (t1->tv_nsec - t2->tv_nsec);
}

/*
 * Prepare to update znode time stamps.
 *
 *	IN:	zp	- znode requiring timestamp update
 *		flag	- ATTR_MTIME, ATTR_CTIME flags
 *
 *	OUT:	zp	- z_seq
 *		mtime	- new mtime
 *		ctime	- new ctime
 *
 *	Note: We don't update atime here, because we rely on Linux VFS to do
 *	atime updating.
 */
void
zfs_tstamp_update_setup(znode_t *zp, uint_t flag, uint64_t mtime[2],
    uint64_t ctime[2])
{
	timestruc_t	now;

	gethrestime(&now);

	zp->z_seq++;

	if (flag & ATTR_MTIME) {
		ZFS_TIME_ENCODE(&now, mtime);
		ZFS_TIME_DECODE(&(ZTOI(zp)->i_mtime), mtime);
		if (ZTOZSB(zp)->z_use_fuids) {
			zp->z_pflags |= (ZFS_ARCHIVE |
			    ZFS_AV_MODIFIED);
		}
	}

	if (flag & ATTR_CTIME) {
		ZFS_TIME_ENCODE(&now, ctime);
		ZFS_TIME_DECODE(&(ZTOI(zp)->i_ctime), ctime);
		if (ZTOZSB(zp)->z_use_fuids)
			zp->z_pflags |= ZFS_ARCHIVE;
	}
}

/*
 * Grow the block size for a file.
 *
 *	IN:	zp	- znode of file to free data in.
 *		size	- requested block size
 *		tx	- open transaction.
 *
 * NOTE: this function assumes that the znode is write locked.
 */
void
zfs_grow_blocksize(znode_t *zp, uint64_t size, dmu_tx_t *tx)
{
	int		error;
	u_longlong_t	dummy;

	if (size <= zp->z_blksz)
		return;
	/*
	 * If the file size is already greater than the current blocksize,
	 * we will not grow.  If there is more than one block in a file,
	 * the blocksize cannot change.
	 */
	if (zp->z_blksz && zp->z_size > zp->z_blksz)
		return;

	error = dmu_object_set_blocksize(ZTOZSB(zp)->z_os, zp->z_id,
	    size, 0, tx);

	if (error == ENOTSUP)
		return;
	ASSERT0(error);

	/* What blocksize did we actually get? */
	dmu_object_size_from_db(sa_get_db(zp->z_sa_hdl), &zp->z_blksz, &dummy);
}

/*
 * Increase the file length
 *
 *	IN:	zp	- znode of file to free data in.
 *		end	- new end-of-file
 *
 *	RETURN:	0 on success, error code on failure
 */
static int
zfs_extend(znode_t *zp, uint64_t end)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	dmu_tx_t *tx;
	rl_t *rl;
	uint64_t newblksz;
	int error;

	/*
	 * We will change zp_size, lock the whole file.
	 */
	rl = zfs_range_lock(&zp->z_range_lock, 0, UINT64_MAX, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (end <= zp->z_size) {
		zfs_range_unlock(rl);
		return (0);
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	if (end > zp->z_blksz &&
	    (!ISP2(zp->z_blksz) || zp->z_blksz < zfsvfs->z_max_blksz)) {
		/*
		 * We are growing the file past the current block size.
		 */
		if (zp->z_blksz > ZTOZSB(zp)->z_max_blksz) {
			/*
			 * File's blocksize is already larger than the
			 * "recordsize" property.  Only let it grow to
			 * the next power of 2.
			 */
			ASSERT(!ISP2(zp->z_blksz));
			newblksz = MIN(end, 1 << highbit64(zp->z_blksz));
		} else {
			newblksz = MIN(end, ZTOZSB(zp)->z_max_blksz);
		}
		dmu_tx_hold_write(tx, zp->z_id, 0, newblksz);
	} else {
		newblksz = 0;
	}

	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_range_unlock(rl);
		return (error);
	}

	if (newblksz)
		zfs_grow_blocksize(zp, newblksz, tx);

	zp->z_size = end;

	VERIFY(0 == sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(ZTOZSB(zp)),
	    &zp->z_size, sizeof (zp->z_size), tx));

	zfs_range_unlock(rl);

	dmu_tx_commit(tx);

	return (0);
}

/*
 * zfs_zero_partial_page - Modeled after update_pages() but
 * with different arguments and semantics for use by zfs_freesp().
 *
 * Zeroes a piece of a single page cache entry for zp at offset
 * start and length len.
 *
 * Caller must acquire a range lock on the file for the region
 * being zeroed in order that the ARC and page cache stay in sync.
 */
static void
zfs_zero_partial_page(znode_t *zp, uint64_t start, uint64_t len)
{
	struct address_space *mp = ZTOI(zp)->i_mapping;
	struct page *pp;
	int64_t	off;
	void *pb;

	ASSERT((start & PAGE_MASK) == ((start + len - 1) & PAGE_MASK));

	off = start & (PAGE_SIZE - 1);
	start &= PAGE_MASK;

	pp = find_lock_page(mp, start >> PAGE_SHIFT);
	if (pp) {
		if (mapping_writably_mapped(mp))
			flush_dcache_page(pp);

		pb = kmap(pp);
		bzero(pb + off, len);
		kunmap(pp);

		if (mapping_writably_mapped(mp))
			flush_dcache_page(pp);

		mark_page_accessed(pp);
		SetPageUptodate(pp);
		ClearPageError(pp);
		unlock_page(pp);
		put_page(pp);
	}
}

/*
 * Free space in a file.
 *
 *	IN:	zp	- znode of file to free data in.
 *		off	- start of section to free.
 *		len	- length of section to free.
 *
 *	RETURN:	0 on success, error code on failure
 */
static int
zfs_free_range(znode_t *zp, uint64_t off, uint64_t len)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	rl_t *rl;
	int error;

	/*
	 * Lock the range being freed.
	 */
	rl = zfs_range_lock(&zp->z_range_lock, off, len, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (off >= zp->z_size) {
		zfs_range_unlock(rl);
		return (0);
	}

	if (off + len > zp->z_size)
		len = zp->z_size - off;

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, off, len);

	/*
	 * Zero partial page cache entries.  This must be done under a
	 * range lock in order to keep the ARC and page cache in sync.
	 */
	if (zp->z_is_mapped) {
		loff_t first_page, last_page, page_len;
		loff_t first_page_offset, last_page_offset;

		/* first possible full page in hole */
		first_page = (off + PAGE_SIZE - 1) >> PAGE_SHIFT;
		/* last page of hole */
		last_page = (off + len) >> PAGE_SHIFT;

		/* offset of first_page */
		first_page_offset = first_page << PAGE_SHIFT;
		/* offset of last_page */
		last_page_offset = last_page << PAGE_SHIFT;

		/* truncate whole pages */
		if (last_page_offset > first_page_offset) {
			truncate_inode_pages_range(ZTOI(zp)->i_mapping,
			    first_page_offset, last_page_offset - 1);
		}

		/* truncate sub-page ranges */
		if (first_page > last_page) {
			/* entire punched area within a single page */
			zfs_zero_partial_page(zp, off, len);
		} else {
			/* beginning of punched area at the end of a page */
			page_len  = first_page_offset - off;
			if (page_len > 0)
				zfs_zero_partial_page(zp, off, page_len);

			/* end of punched area at the beginning of a page */
			page_len = off + len - last_page_offset;
			if (page_len > 0)
				zfs_zero_partial_page(zp, last_page_offset,
				    page_len);
		}
	}
	zfs_range_unlock(rl);

	return (error);
}

/*
 * Truncate a file
 *
 *	IN:	zp	- znode of file to free data in.
 *		end	- new end-of-file.
 *
 *	RETURN:	0 on success, error code on failure
 */
static int
zfs_trunc(znode_t *zp, uint64_t end)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	dmu_tx_t *tx;
	rl_t *rl;
	int error;
	sa_bulk_attr_t bulk[2];
	int count = 0;

	/*
	 * We will change zp_size, lock the whole file.
	 */
	rl = zfs_range_lock(&zp->z_range_lock, 0, UINT64_MAX, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (end >= zp->z_size) {
		zfs_range_unlock(rl);
		return (0);
	}

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, end,  -1);
	if (error) {
		zfs_range_unlock(rl);
		return (error);
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_range_unlock(rl);
		return (error);
	}

	zp->z_size = end;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs),
	    NULL, &zp->z_size, sizeof (zp->z_size));

	if (end == 0) {
		zp->z_pflags &= ~ZFS_SPARSE;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &zp->z_pflags, 8);
	}
	VERIFY(sa_bulk_update(zp->z_sa_hdl, bulk, count, tx) == 0);

	dmu_tx_commit(tx);

	zfs_range_unlock(rl);

	return (0);
}

/*
 * Free space in a file
 *
 *	IN:	zp	- znode of file to free data in.
 *		off	- start of range
 *		len	- end of range (0 => EOF)
 *		flag	- current file open mode flags.
 *		log	- TRUE if this action should be logged
 *
 *	RETURN:	0 on success, error code on failure
 */
int
zfs_freesp(znode_t *zp, uint64_t off, uint64_t len, int flag, boolean_t log)
{
	dmu_tx_t *tx;
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	zilog_t *zilog = zfsvfs->z_log;
	uint64_t mode;
	uint64_t mtime[2], ctime[2];
	sa_bulk_attr_t bulk[3];
	int count = 0;
	int error;

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs), &mode,
	    sizeof (mode))) != 0)
		return (error);

	if (off > zp->z_size) {
		error =  zfs_extend(zp, off+len);
		if (error == 0 && log)
			goto log;
		goto out;
	}

	if (len == 0) {
		error = zfs_trunc(zp, off);
	} else {
		if ((error = zfs_free_range(zp, off, len)) == 0 &&
		    off + len > zp->z_size)
			error = zfs_extend(zp, off+len);
	}
	if (error || !log)
		goto out;
log:
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		goto out;
	}

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
	    NULL, &zp->z_pflags, 8);
	zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT(error == 0);

	zfs_log_truncate(zilog, tx, TX_TRUNCATE, zp, off, len);

	dmu_tx_commit(tx);

	zfs_inode_update(zp);
	error = 0;

out:
	/*
	 * Truncate the page cache - for file truncate operations, use
	 * the purpose-built API for truncations.  For punching operations,
	 * the truncation is handled under a range lock in zfs_free_range.
	 */
	if (len == 0)
		truncate_setsize(ZTOI(zp), off);
	return (error);
}

void
zfs_create_fs(objset_t *os, cred_t *cr, nvlist_t *zplprops, dmu_tx_t *tx)
{
	struct super_block *sb;
	zfsvfs_t	*zfsvfs;
	uint64_t	moid, obj, sa_obj, version;
	uint64_t	sense = ZFS_CASE_SENSITIVE;
	uint64_t	norm = 0;
	nvpair_t	*elem;
	int		size;
	int		error;
	int		i;
	znode_t		*rootzp = NULL;
	vattr_t		vattr;
	znode_t		*zp;
	zfs_acl_ids_t	acl_ids;

	/*
	 * First attempt to create master node.
	 */
	/*
	 * In an empty objset, there are no blocks to read and thus
	 * there can be no i/o errors (which we assert below).
	 */
	moid = MASTER_NODE_OBJ;
	error = zap_create_claim(os, moid, DMU_OT_MASTER_NODE,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	/*
	 * Set starting attributes.
	 */
	version = zfs_zpl_version_map(spa_version(dmu_objset_spa(os)));
	elem = NULL;
	while ((elem = nvlist_next_nvpair(zplprops, elem)) != NULL) {
		/* For the moment we expect all zpl props to be uint64_ts */
		uint64_t val;
		char *name;

		ASSERT(nvpair_type(elem) == DATA_TYPE_UINT64);
		VERIFY(nvpair_value_uint64(elem, &val) == 0);
		name = nvpair_name(elem);
		if (strcmp(name, zfs_prop_to_name(ZFS_PROP_VERSION)) == 0) {
			if (val < version)
				version = val;
		} else {
			error = zap_update(os, moid, name, 8, 1, &val, tx);
		}
		ASSERT(error == 0);
		if (strcmp(name, zfs_prop_to_name(ZFS_PROP_NORMALIZE)) == 0)
			norm = val;
		else if (strcmp(name, zfs_prop_to_name(ZFS_PROP_CASE)) == 0)
			sense = val;
	}
	ASSERT(version != 0);
	error = zap_update(os, moid, ZPL_VERSION_STR, 8, 1, &version, tx);

	/*
	 * Create zap object used for SA attribute registration
	 */

	if (version >= ZPL_VERSION_SA) {
		sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
		    DMU_OT_NONE, 0, tx);
		error = zap_add(os, moid, ZFS_SA_ATTRS, 8, 1, &sa_obj, tx);
		ASSERT(error == 0);
	} else {
		sa_obj = 0;
	}
	/*
	 * Create a delete queue.
	 */
	obj = zap_create(os, DMU_OT_UNLINKED_SET, DMU_OT_NONE, 0, tx);

	error = zap_add(os, moid, ZFS_UNLINKED_SET, 8, 1, &obj, tx);
	ASSERT(error == 0);

	/*
	 * Create root znode.  Create minimal znode/inode/zfsvfs/sb
	 * to allow zfs_mknode to work.
	 */
	vattr.va_mask = ATTR_MODE|ATTR_UID|ATTR_GID;
	vattr.va_mode = S_IFDIR|0755;
	vattr.va_uid = crgetuid(cr);
	vattr.va_gid = crgetgid(cr);

	rootzp = kmem_cache_alloc(znode_cache, KM_SLEEP);
	rootzp->z_moved = 0;
	rootzp->z_unlinked = 0;
	rootzp->z_atime_dirty = 0;
	rootzp->z_is_sa = USE_SA(version, os);

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);
	zfsvfs->z_os = os;
	zfsvfs->z_parent = zfsvfs;
	zfsvfs->z_version = version;
	zfsvfs->z_use_fuids = USE_FUIDS(version, os);
	zfsvfs->z_use_sa = USE_SA(version, os);
	zfsvfs->z_norm = norm;

	sb = kmem_zalloc(sizeof (struct super_block), KM_SLEEP);
	sb->s_fs_info = zfsvfs;

	ZTOI(rootzp)->i_sb = sb;

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);

	ASSERT(error == 0);

	/*
	 * Fold case on file systems that are always or sometimes case
	 * insensitive.
	 */
	if (sense == ZFS_CASE_INSENSITIVE || sense == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));

	size = MIN(1 << (highbit64(zfs_object_mutex_size)-1), ZFS_OBJ_MTX_MAX);
	zfsvfs->z_hold_size = size;
	zfsvfs->z_hold_trees = vmem_zalloc(sizeof (avl_tree_t) * size,
	    KM_SLEEP);
	zfsvfs->z_hold_locks = vmem_zalloc(sizeof (kmutex_t) * size, KM_SLEEP);
	for (i = 0; i != size; i++) {
		avl_create(&zfsvfs->z_hold_trees[i], zfs_znode_hold_compare,
		    sizeof (znode_hold_t), offsetof(znode_hold_t, zh_node));
		mutex_init(&zfsvfs->z_hold_locks[i], NULL, MUTEX_DEFAULT, NULL);
	}

	VERIFY(0 == zfs_acl_ids_create(rootzp, IS_ROOT_NODE, &vattr,
	    cr, NULL, &acl_ids));
	zfs_mknode(rootzp, &vattr, tx, cr, IS_ROOT_NODE, &zp, &acl_ids);
	ASSERT3P(zp, ==, rootzp);
	error = zap_add(os, moid, ZFS_ROOT_OBJ, 8, 1, &rootzp->z_id, tx);
	ASSERT(error == 0);
	zfs_acl_ids_free(&acl_ids);

	atomic_set(&ZTOI(rootzp)->i_count, 0);
	sa_handle_destroy(rootzp->z_sa_hdl);
	kmem_cache_free(znode_cache, rootzp);

	/*
	 * Create shares directory
	 */
	error = zfs_create_share_dir(zfsvfs, tx);
	ASSERT(error == 0);

	for (i = 0; i != size; i++) {
		avl_destroy(&zfsvfs->z_hold_trees[i]);
		mutex_destroy(&zfsvfs->z_hold_locks[i]);
	}

	mutex_destroy(&zfsvfs->z_znodes_lock);

	vmem_free(zfsvfs->z_hold_trees, sizeof (avl_tree_t) * size);
	vmem_free(zfsvfs->z_hold_locks, sizeof (kmutex_t) * size);
	kmem_free(sb, sizeof (struct super_block));
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}
#endif /* _KERNEL */

static int
zfs_sa_setup(objset_t *osp, sa_attr_type_t **sa_table)
{
	uint64_t sa_obj = 0;
	int error;

	error = zap_lookup(osp, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa_obj);
	if (error != 0 && error != ENOENT)
		return (error);

	error = sa_setup(osp, sa_obj, zfs_attr_table, ZPL_END, sa_table);
	return (error);
}

static int
zfs_grab_sa_handle(objset_t *osp, uint64_t obj, sa_handle_t **hdlp,
    dmu_buf_t **db, void *tag)
{
	dmu_object_info_t doi;
	int error;

	if ((error = sa_buf_hold(osp, obj, tag, db)) != 0)
		return (error);

	dmu_object_info_from_db(*db, &doi);
	if ((doi.doi_bonus_type != DMU_OT_SA &&
	    doi.doi_bonus_type != DMU_OT_ZNODE) ||
	    (doi.doi_bonus_type == DMU_OT_ZNODE &&
	    doi.doi_bonus_size < sizeof (znode_phys_t))) {
		sa_buf_rele(*db, tag);
		return (SET_ERROR(ENOTSUP));
	}

	error = sa_handle_get(osp, obj, NULL, SA_HDL_PRIVATE, hdlp);
	if (error != 0) {
		sa_buf_rele(*db, tag);
		return (error);
	}

	return (0);
}

void
zfs_release_sa_handle(sa_handle_t *hdl, dmu_buf_t *db, void *tag)
{
	sa_handle_destroy(hdl);
	sa_buf_rele(db, tag);
}

/*
 * Given an object number, return its parent object number and whether
 * or not the object is an extended attribute directory.
 */
static int
zfs_obj_to_pobj(objset_t *osp, sa_handle_t *hdl, sa_attr_type_t *sa_table,
    uint64_t *pobjp, int *is_xattrdir)
{
	uint64_t parent;
	uint64_t pflags;
	uint64_t mode;
	uint64_t parent_mode;
	sa_bulk_attr_t bulk[3];
	sa_handle_t *sa_hdl;
	dmu_buf_t *sa_db;
	int count = 0;
	int error;

	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_PARENT], NULL,
	    &parent, sizeof (parent));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_FLAGS], NULL,
	    &pflags, sizeof (pflags));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_MODE], NULL,
	    &mode, sizeof (mode));

	if ((error = sa_bulk_lookup(hdl, bulk, count)) != 0)
		return (error);

	/*
	 * When a link is removed its parent pointer is not changed and will
	 * be invalid.  There are two cases where a link is removed but the
	 * file stays around, when it goes to the delete queue and when there
	 * are additional links.
	 */
	error = zfs_grab_sa_handle(osp, parent, &sa_hdl, &sa_db, FTAG);
	if (error != 0)
		return (error);

	error = sa_lookup(sa_hdl, ZPL_MODE, &parent_mode, sizeof (parent_mode));
	zfs_release_sa_handle(sa_hdl, sa_db, FTAG);
	if (error != 0)
		return (error);

	*is_xattrdir = ((pflags & ZFS_XATTR) != 0) && S_ISDIR(mode);

	/*
	 * Extended attributes can be applied to files, directories, etc.
	 * Otherwise the parent must be a directory.
	 */
	if (!*is_xattrdir && !S_ISDIR(parent_mode))
		return (EINVAL);

	*pobjp = parent;

	return (0);
}

/*
 * Given an object number, return some zpl level statistics
 */
static int
zfs_obj_to_stats_impl(sa_handle_t *hdl, sa_attr_type_t *sa_table,
    zfs_stat_t *sb)
{
	sa_bulk_attr_t bulk[4];
	int count = 0;

	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_MODE], NULL,
	    &sb->zs_mode, sizeof (sb->zs_mode));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_GEN], NULL,
	    &sb->zs_gen, sizeof (sb->zs_gen));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_LINKS], NULL,
	    &sb->zs_links, sizeof (sb->zs_links));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_CTIME], NULL,
	    &sb->zs_ctime, sizeof (sb->zs_ctime));

	return (sa_bulk_lookup(hdl, bulk, count));
}

static int
zfs_obj_to_path_impl(objset_t *osp, uint64_t obj, sa_handle_t *hdl,
    sa_attr_type_t *sa_table, char *buf, int len)
{
	sa_handle_t *sa_hdl;
	sa_handle_t *prevhdl = NULL;
	dmu_buf_t *prevdb = NULL;
	dmu_buf_t *sa_db = NULL;
	char *path = buf + len - 1;
	int error;

	*path = '\0';
	sa_hdl = hdl;

	for (;;) {
		uint64_t pobj = 0;
		char component[MAXNAMELEN + 2];
		size_t complen;
		int is_xattrdir = 0;

		if (prevdb)
			zfs_release_sa_handle(prevhdl, prevdb, FTAG);

		if ((error = zfs_obj_to_pobj(osp, sa_hdl, sa_table, &pobj,
		    &is_xattrdir)) != 0)
			break;

		if (pobj == obj) {
			if (path[0] != '/')
				*--path = '/';
			break;
		}

		component[0] = '/';
		if (is_xattrdir) {
			(void) sprintf(component + 1, "<xattrdir>");
		} else {
			error = zap_value_search(osp, pobj, obj,
			    ZFS_DIRENT_OBJ(-1ULL), component + 1);
			if (error != 0)
				break;
		}

		complen = strlen(component);
		path -= complen;
		ASSERT(path >= buf);
		bcopy(component, path, complen);
		obj = pobj;

		if (sa_hdl != hdl) {
			prevhdl = sa_hdl;
			prevdb = sa_db;
		}
		error = zfs_grab_sa_handle(osp, obj, &sa_hdl, &sa_db, FTAG);
		if (error != 0) {
			sa_hdl = prevhdl;
			sa_db = prevdb;
			break;
		}
	}

	if (sa_hdl != NULL && sa_hdl != hdl) {
		ASSERT(sa_db != NULL);
		zfs_release_sa_handle(sa_hdl, sa_db, FTAG);
	}

	if (error == 0)
		(void) memmove(buf, path, buf + len - path);

	return (error);
}

int
zfs_obj_to_path(objset_t *osp, uint64_t obj, char *buf, int len)
{
	sa_attr_type_t *sa_table;
	sa_handle_t *hdl;
	dmu_buf_t *db;
	int error;

	error = zfs_sa_setup(osp, &sa_table);
	if (error != 0)
		return (error);

	error = zfs_grab_sa_handle(osp, obj, &hdl, &db, FTAG);
	if (error != 0)
		return (error);

	error = zfs_obj_to_path_impl(osp, obj, hdl, sa_table, buf, len);

	zfs_release_sa_handle(hdl, db, FTAG);
	return (error);
}

int
zfs_obj_to_stats(objset_t *osp, uint64_t obj, zfs_stat_t *sb,
    char *buf, int len)
{
	char *path = buf + len - 1;
	sa_attr_type_t *sa_table;
	sa_handle_t *hdl;
	dmu_buf_t *db;
	int error;

	*path = '\0';

	error = zfs_sa_setup(osp, &sa_table);
	if (error != 0)
		return (error);

	error = zfs_grab_sa_handle(osp, obj, &hdl, &db, FTAG);
	if (error != 0)
		return (error);

	error = zfs_obj_to_stats_impl(hdl, sa_table, sb);
	if (error != 0) {
		zfs_release_sa_handle(hdl, db, FTAG);
		return (error);
	}

	error = zfs_obj_to_path_impl(osp, obj, hdl, sa_table, buf, len);

	zfs_release_sa_handle(hdl, db, FTAG);
	return (error);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zfs_create_fs);
EXPORT_SYMBOL(zfs_obj_to_path);

/* CSTYLED */
module_param(zfs_object_mutex_size, uint, 0644);
MODULE_PARM_DESC(zfs_object_mutex_size, "Size of znode hold array");
#endif
