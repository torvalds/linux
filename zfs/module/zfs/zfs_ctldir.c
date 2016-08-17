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
 *
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

/*
 * ZFS control directory (a.k.a. ".zfs")
 *
 * This directory provides a common location for all ZFS meta-objects.
 * Currently, this is only the 'snapshot' and 'shares' directory, but this may
 * expand in the future.  The elements are built dynamically, as the hierarchy
 * does not actually exist on disk.
 *
 * For 'snapshot', we don't want to have all snapshots always mounted, because
 * this would take up a huge amount of space in /etc/mnttab.  We have three
 * types of objects:
 *
 *	ctldir ------> snapshotdir -------> snapshot
 *                                             |
 *                                             |
 *                                             V
 *                                         mounted fs
 *
 * The 'snapshot' node contains just enough information to lookup '..' and act
 * as a mountpoint for the snapshot.  Whenever we lookup a specific snapshot, we
 * perform an automount of the underlying filesystem and return the
 * corresponding inode.
 *
 * All mounts are handled automatically by an user mode helper which invokes
 * the mount mount procedure.  Unmounts are handled by allowing the mount
 * point to expire so the kernel may automatically unmount it.
 *
 * The '.zfs', '.zfs/snapshot', and all directories created under
 * '.zfs/snapshot' (ie: '.zfs/snapshot/<snapname>') all share the same
 * share the same zfs_sb_t as the head filesystem (what '.zfs' lives under).
 *
 * File systems mounted on top of the '.zfs/snapshot/<snapname>' paths
 * (ie: snapshots) are complete ZFS filesystems and have their own unique
 * zfs_sb_t.  However, the fsid reported by these mounts will be the same
 * as that used by the parent zfs_sb_t to make NFS happy.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/stat.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_deleg.h>
#include <sys/mount.h>
#include <sys/zpl.h>
#include "zfs_namecheck.h"

/*
 * Two AVL trees are maintained which contain all currently automounted
 * snapshots.  Every automounted snapshots maps to a single zfs_snapentry_t
 * entry which MUST:
 *
 *   - be attached to both trees, and
 *   - be unique, no duplicate entries are allowed.
 *
 * The zfs_snapshots_by_name tree is indexed by the full dataset name
 * while the zfs_snapshots_by_objsetid tree is indexed by the unique
 * objsetid.  This allows for fast lookups either by name or objsetid.
 */
static avl_tree_t zfs_snapshots_by_name;
static avl_tree_t zfs_snapshots_by_objsetid;
static krwlock_t zfs_snapshot_lock;

/*
 * Control Directory Tunables (.zfs)
 */
int zfs_expire_snapshot = ZFSCTL_EXPIRE_SNAPSHOT;
int zfs_admin_snapshot = 0;

/*
 * Dedicated task queue for unmounting snapshots.
 */
static taskq_t *zfs_expire_taskq;

typedef struct {
	char		*se_name;	/* full snapshot name */
	char		*se_path;	/* full mount path */
	spa_t		*se_spa;	/* pool spa */
	uint64_t	se_objsetid;	/* snapshot objset id */
	struct dentry   *se_root_dentry; /* snapshot root dentry */
	taskqid_t	se_taskqid;	/* scheduled unmount taskqid */
	avl_node_t	se_node_name;	/* zfs_snapshots_by_name link */
	avl_node_t	se_node_objsetid; /* zfs_snapshots_by_objsetid link */
	refcount_t	se_refcount;	/* reference count */
} zfs_snapentry_t;

static void zfsctl_snapshot_unmount_delay_impl(zfs_snapentry_t *se, int delay);

/*
 * Allocate a new zfs_snapentry_t being careful to make a copy of the
 * the snapshot name and provided mount point.  No reference is taken.
 */
static zfs_snapentry_t *
zfsctl_snapshot_alloc(char *full_name, char *full_path, spa_t *spa,
    uint64_t objsetid, struct dentry *root_dentry)
{
	zfs_snapentry_t *se;

	se = kmem_zalloc(sizeof (zfs_snapentry_t), KM_SLEEP);

	se->se_name = strdup(full_name);
	se->se_path = strdup(full_path);
	se->se_spa = spa;
	se->se_objsetid = objsetid;
	se->se_root_dentry = root_dentry;
	se->se_taskqid = -1;

	refcount_create(&se->se_refcount);

	return (se);
}

/*
 * Free a zfs_snapentry_t the called must ensure there are no active
 * references.
 */
static void
zfsctl_snapshot_free(zfs_snapentry_t *se)
{
	refcount_destroy(&se->se_refcount);
	strfree(se->se_name);
	strfree(se->se_path);

	kmem_free(se, sizeof (zfs_snapentry_t));
}

/*
 * Hold a reference on the zfs_snapentry_t.
 */
static void
zfsctl_snapshot_hold(zfs_snapentry_t *se)
{
	refcount_add(&se->se_refcount, NULL);
}

/*
 * Release a reference on the zfs_snapentry_t.  When the number of
 * references drops to zero the structure will be freed.
 */
static void
zfsctl_snapshot_rele(zfs_snapentry_t *se)
{
	if (refcount_remove(&se->se_refcount, NULL) == 0)
		zfsctl_snapshot_free(se);
}

/*
 * Add a zfs_snapentry_t to both the zfs_snapshots_by_name and
 * zfs_snapshots_by_objsetid trees.  While the zfs_snapentry_t is part
 * of the trees a reference is held.
 */
static void
zfsctl_snapshot_add(zfs_snapentry_t *se)
{
	ASSERT(RW_WRITE_HELD(&zfs_snapshot_lock));
	refcount_add(&se->se_refcount, NULL);
	avl_add(&zfs_snapshots_by_name, se);
	avl_add(&zfs_snapshots_by_objsetid, se);
}

/*
 * Remove a zfs_snapentry_t from both the zfs_snapshots_by_name and
 * zfs_snapshots_by_objsetid trees.  Upon removal a reference is dropped,
 * this can result in the structure being freed if that was the last
 * remaining reference.
 */
static void
zfsctl_snapshot_remove(zfs_snapentry_t *se)
{
	ASSERT(RW_WRITE_HELD(&zfs_snapshot_lock));
	avl_remove(&zfs_snapshots_by_name, se);
	avl_remove(&zfs_snapshots_by_objsetid, se);
	zfsctl_snapshot_rele(se);
}

/*
 * Snapshot name comparison function for the zfs_snapshots_by_name.
 */
static int
snapentry_compare_by_name(const void *a, const void *b)
{
	const zfs_snapentry_t *se_a = a;
	const zfs_snapentry_t *se_b = b;
	int ret;

	ret = strcmp(se_a->se_name, se_b->se_name);

	if (ret < 0)
		return (-1);
	else if (ret > 0)
		return (1);
	else
		return (0);
}

/*
 * Snapshot name comparison function for the zfs_snapshots_by_objsetid.
 */
static int
snapentry_compare_by_objsetid(const void *a, const void *b)
{
	const zfs_snapentry_t *se_a = a;
	const zfs_snapentry_t *se_b = b;

	if (se_a->se_spa != se_b->se_spa)
		return ((ulong_t)se_a->se_spa < (ulong_t)se_b->se_spa ? -1 : 1);

	if (se_a->se_objsetid < se_b->se_objsetid)
		return (-1);
	else if (se_a->se_objsetid > se_b->se_objsetid)
		return (1);
	else
		return (0);
}

/*
 * Find a zfs_snapentry_t in zfs_snapshots_by_name.  If the snapname
 * is found a pointer to the zfs_snapentry_t is returned and a reference
 * taken on the structure.  The caller is responsible for dropping the
 * reference with zfsctl_snapshot_rele().  If the snapname is not found
 * NULL will be returned.
 */
static zfs_snapentry_t *
zfsctl_snapshot_find_by_name(char *snapname)
{
	zfs_snapentry_t *se, search;

	ASSERT(RW_LOCK_HELD(&zfs_snapshot_lock));

	search.se_name = snapname;
	se = avl_find(&zfs_snapshots_by_name, &search, NULL);
	if (se)
		refcount_add(&se->se_refcount, NULL);

	return (se);
}

/*
 * Find a zfs_snapentry_t in zfs_snapshots_by_objsetid given the objset id
 * rather than the snapname.  In all other respects it behaves the same
 * as zfsctl_snapshot_find_by_name().
 */
static zfs_snapentry_t *
zfsctl_snapshot_find_by_objsetid(spa_t *spa, uint64_t objsetid)
{
	zfs_snapentry_t *se, search;

	ASSERT(RW_LOCK_HELD(&zfs_snapshot_lock));

	search.se_spa = spa;
	search.se_objsetid = objsetid;
	se = avl_find(&zfs_snapshots_by_objsetid, &search, NULL);
	if (se)
		refcount_add(&se->se_refcount, NULL);

	return (se);
}

/*
 * Rename a zfs_snapentry_t in the zfs_snapshots_by_name.  The structure is
 * removed, renamed, and added back to the new correct location in the tree.
 */
static int
zfsctl_snapshot_rename(char *old_snapname, char *new_snapname)
{
	zfs_snapentry_t *se;

	ASSERT(RW_WRITE_HELD(&zfs_snapshot_lock));

	se = zfsctl_snapshot_find_by_name(old_snapname);
	if (se == NULL)
		return (ENOENT);

	zfsctl_snapshot_remove(se);
	strfree(se->se_name);
	se->se_name = strdup(new_snapname);
	zfsctl_snapshot_add(se);
	zfsctl_snapshot_rele(se);

	return (0);
}

/*
 * Delayed task responsible for unmounting an expired automounted snapshot.
 */
static void
snapentry_expire(void *data)
{
	zfs_snapentry_t *se = (zfs_snapentry_t *)data;
	spa_t *spa = se->se_spa;
	uint64_t objsetid = se->se_objsetid;

	if (zfs_expire_snapshot <= 0) {
		zfsctl_snapshot_rele(se);
		return;
	}

	se->se_taskqid = -1;
	(void) zfsctl_snapshot_unmount(se->se_name, MNT_EXPIRE);
	zfsctl_snapshot_rele(se);

	/*
	 * Reschedule the unmount if the zfs_snapentry_t wasn't removed.
	 * This can occur when the snapshot is busy.
	 */
	rw_enter(&zfs_snapshot_lock, RW_READER);
	if ((se = zfsctl_snapshot_find_by_objsetid(spa, objsetid)) != NULL) {
		zfsctl_snapshot_unmount_delay_impl(se, zfs_expire_snapshot);
		zfsctl_snapshot_rele(se);
	}
	rw_exit(&zfs_snapshot_lock);
}

/*
 * Cancel an automatic unmount of a snapname.  This callback is responsible
 * for dropping the reference on the zfs_snapentry_t which was taken when
 * during dispatch.
 */
static void
zfsctl_snapshot_unmount_cancel(zfs_snapentry_t *se)
{
	ASSERT(RW_LOCK_HELD(&zfs_snapshot_lock));

	if (taskq_cancel_id(zfs_expire_taskq, se->se_taskqid) == 0) {
		se->se_taskqid = -1;
		zfsctl_snapshot_rele(se);
	}
}

/*
 * Dispatch the unmount task for delayed handling with a hold protecting it.
 */
static void
zfsctl_snapshot_unmount_delay_impl(zfs_snapentry_t *se, int delay)
{
	ASSERT3S(se->se_taskqid, ==, -1);

	if (delay <= 0)
		return;

	zfsctl_snapshot_hold(se);
	se->se_taskqid = taskq_dispatch_delay(zfs_expire_taskq,
	    snapentry_expire, se, TQ_SLEEP, ddi_get_lbolt() + delay * HZ);
}

/*
 * Schedule an automatic unmount of objset id to occur in delay seconds from
 * now.  Any previous delayed unmount will be cancelled in favor of the
 * updated deadline.  A reference is taken by zfsctl_snapshot_find_by_name()
 * and held until the outstanding task is handled or cancelled.
 */
int
zfsctl_snapshot_unmount_delay(spa_t *spa, uint64_t objsetid, int delay)
{
	zfs_snapentry_t *se;
	int error = ENOENT;

	rw_enter(&zfs_snapshot_lock, RW_READER);
	if ((se = zfsctl_snapshot_find_by_objsetid(spa, objsetid)) != NULL) {
		zfsctl_snapshot_unmount_cancel(se);
		zfsctl_snapshot_unmount_delay_impl(se, delay);
		zfsctl_snapshot_rele(se);
		error = 0;
	}
	rw_exit(&zfs_snapshot_lock);

	return (error);
}

/*
 * Check if snapname is currently mounted.  Returned non-zero when mounted
 * and zero when unmounted.
 */
static boolean_t
zfsctl_snapshot_ismounted(char *snapname)
{
	zfs_snapentry_t *se;
	boolean_t ismounted = B_FALSE;

	rw_enter(&zfs_snapshot_lock, RW_READER);
	if ((se = zfsctl_snapshot_find_by_name(snapname)) != NULL) {
		zfsctl_snapshot_rele(se);
		ismounted = B_TRUE;
	}
	rw_exit(&zfs_snapshot_lock);

	return (ismounted);
}

/*
 * Check if the given inode is a part of the virtual .zfs directory.
 */
boolean_t
zfsctl_is_node(struct inode *ip)
{
	return (ITOZ(ip)->z_is_ctldir);
}

/*
 * Check if the given inode is a .zfs/snapshots/snapname directory.
 */
boolean_t
zfsctl_is_snapdir(struct inode *ip)
{
	return (zfsctl_is_node(ip) && (ip->i_ino <= ZFSCTL_INO_SNAPDIRS));
}

/*
 * Allocate a new inode with the passed id and ops.
 */
static struct inode *
zfsctl_inode_alloc(zfs_sb_t *zsb, uint64_t id,
    const struct file_operations *fops, const struct inode_operations *ops)
{
	struct timespec now = current_fs_time(zsb->z_sb);
	struct inode *ip;
	znode_t *zp;

	ip = new_inode(zsb->z_sb);
	if (ip == NULL)
		return (NULL);

	zp = ITOZ(ip);
	ASSERT3P(zp->z_dirlocks, ==, NULL);
	ASSERT3P(zp->z_acl_cached, ==, NULL);
	ASSERT3P(zp->z_xattr_cached, ==, NULL);
	zp->z_id = id;
	zp->z_unlinked = 0;
	zp->z_atime_dirty = 0;
	zp->z_zn_prefetch = 0;
	zp->z_moved = 0;
	zp->z_sa_hdl = NULL;
	zp->z_blksz = 0;
	zp->z_seq = 0;
	zp->z_mapcnt = 0;
	zp->z_gen = 0;
	zp->z_size = 0;
	zp->z_links = 0;
	zp->z_pflags = 0;
	zp->z_uid = 0;
	zp->z_gid = 0;
	zp->z_mode = 0;
	zp->z_sync_cnt = 0;
	zp->z_is_mapped = B_FALSE;
	zp->z_is_ctldir = B_TRUE;
	zp->z_is_sa = B_FALSE;
	zp->z_is_stale = B_FALSE;
	ip->i_ino = id;
	ip->i_mode = (S_IFDIR | S_IRUGO | S_IXUGO);
	ip->i_uid = SUID_TO_KUID(0);
	ip->i_gid = SGID_TO_KGID(0);
	ip->i_blkbits = SPA_MINBLOCKSHIFT;
	ip->i_atime = now;
	ip->i_mtime = now;
	ip->i_ctime = now;
	ip->i_fop = fops;
	ip->i_op = ops;

	if (insert_inode_locked(ip)) {
		unlock_new_inode(ip);
		iput(ip);
		return (NULL);
	}

	mutex_enter(&zsb->z_znodes_lock);
	list_insert_tail(&zsb->z_all_znodes, zp);
	zsb->z_nr_znodes++;
	membar_producer();
	mutex_exit(&zsb->z_znodes_lock);

	unlock_new_inode(ip);

	return (ip);
}

/*
 * Lookup the inode with given id, it will be allocated if needed.
 */
static struct inode *
zfsctl_inode_lookup(zfs_sb_t *zsb, uint64_t id,
    const struct file_operations *fops, const struct inode_operations *ops)
{
	struct inode *ip = NULL;

	while (ip == NULL) {
		ip = ilookup(zsb->z_sb, (unsigned long)id);
		if (ip)
			break;

		/* May fail due to concurrent zfsctl_inode_alloc() */
		ip = zfsctl_inode_alloc(zsb, id, fops, ops);
	}

	return (ip);
}

/*
 * Create the '.zfs' directory.  This directory is cached as part of the VFS
 * structure.  This results in a hold on the zfs_sb_t.  The code in zfs_umount()
 * therefore checks against a vfs_count of 2 instead of 1.  This reference
 * is removed when the ctldir is destroyed in the unmount.  All other entities
 * under the '.zfs' directory are created dynamically as needed.
 *
 * Because the dynamically created '.zfs' directory entries assume the use
 * of 64-bit inode numbers this support must be disabled on 32-bit systems.
 */
int
zfsctl_create(zfs_sb_t *zsb)
{
#if defined(CONFIG_64BIT)
	ASSERT(zsb->z_ctldir == NULL);

	zsb->z_ctldir = zfsctl_inode_alloc(zsb, ZFSCTL_INO_ROOT,
	    &zpl_fops_root, &zpl_ops_root);
	if (zsb->z_ctldir == NULL)
		return (SET_ERROR(ENOENT));

	return (0);
#else
	return (SET_ERROR(EOPNOTSUPP));
#endif /* CONFIG_64BIT */
}

/*
 * Destroy the '.zfs' directory or remove a snapshot from zfs_snapshots_by_name.
 * Only called when the filesystem is unmounted.
 */
void
zfsctl_destroy(zfs_sb_t *zsb)
{
	if (zsb->z_issnap) {
		zfs_snapentry_t *se;
		spa_t *spa = zsb->z_os->os_spa;
		uint64_t objsetid = dmu_objset_id(zsb->z_os);

		rw_enter(&zfs_snapshot_lock, RW_WRITER);
		if ((se = zfsctl_snapshot_find_by_objsetid(spa, objsetid))
		    != NULL) {
			zfsctl_snapshot_unmount_cancel(se);
			zfsctl_snapshot_remove(se);
			zfsctl_snapshot_rele(se);
		}
		rw_exit(&zfs_snapshot_lock);
	} else if (zsb->z_ctldir) {
		iput(zsb->z_ctldir);
		zsb->z_ctldir = NULL;
	}
}

/*
 * Given a root znode, retrieve the associated .zfs directory.
 * Add a hold to the vnode and return it.
 */
struct inode *
zfsctl_root(znode_t *zp)
{
	ASSERT(zfs_has_ctldir(zp));
	igrab(ZTOZSB(zp)->z_ctldir);
	return (ZTOZSB(zp)->z_ctldir);
}
/*
 * Generate a long fid which includes the root object and objset of a
 * snapshot but not the generation number.  For the root object the
 * generation number is ignored when zero to avoid needing to open
 * the dataset when generating fids for the snapshot names.
 */
static int
zfsctl_snapdir_fid(struct inode *ip, fid_t *fidp)
{
	zfs_sb_t *zsb = ITOZSB(ip);
	zfid_short_t *zfid = (zfid_short_t *)fidp;
	zfid_long_t *zlfid = (zfid_long_t *)fidp;
	uint32_t gen = 0;
	uint64_t object;
	uint64_t objsetid;
	int i;

	object = zsb->z_root;
	objsetid = ZFSCTL_INO_SNAPDIRS - ip->i_ino;
	zfid->zf_len = LONG_FID_LEN;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(gen >> (8 * i));

	for (i = 0; i < sizeof (zlfid->zf_setid); i++)
		zlfid->zf_setid[i] = (uint8_t)(objsetid >> (8 * i));

	for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
		zlfid->zf_setgen[i] = 0;

	return (0);
}

/*
 * Generate an appropriate fid for an entry in the .zfs directory.
 */
int
zfsctl_fid(struct inode *ip, fid_t *fidp)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		i;

	ZFS_ENTER(zsb);

	if (fidp->fid_len < SHORT_FID_LEN) {
		fidp->fid_len = SHORT_FID_LEN;
		ZFS_EXIT(zsb);
		return (SET_ERROR(ENOSPC));
	}

	if (zfsctl_is_snapdir(ip)) {
		ZFS_EXIT(zsb);
		return (zfsctl_snapdir_fid(ip, fidp));
	}

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = SHORT_FID_LEN;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* .zfs znodes always have a generation number of 0 */
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = 0;

	ZFS_EXIT(zsb);
	return (0);
}

/*
 * Construct a full dataset name in full_name: "pool/dataset@snap_name"
 */
static int
zfsctl_snapshot_name(zfs_sb_t *zsb, const char *snap_name, int len,
    char *full_name)
{
	objset_t *os = zsb->z_os;

	if (zfs_component_namecheck(snap_name, NULL, NULL) != 0)
		return (SET_ERROR(EILSEQ));

	dmu_objset_name(os, full_name);
	if ((strlen(full_name) + 1 + strlen(snap_name)) >= len)
		return (SET_ERROR(ENAMETOOLONG));

	(void) strcat(full_name, "@");
	(void) strcat(full_name, snap_name);

	return (0);
}

/*
 * Returns full path in full_path: "/pool/dataset/.zfs/snapshot/snap_name/"
 */
static int
zfsctl_snapshot_path(struct path *path, int len, char *full_path)
{
	char *path_buffer, *path_ptr;
	int path_len, error = 0;

	path_buffer = kmem_alloc(len, KM_SLEEP);

	path_ptr = d_path(path, path_buffer, len);
	if (IS_ERR(path_ptr)) {
		error = -PTR_ERR(path_ptr);
		goto out;
	}

	path_len = path_buffer + len - 1 - path_ptr;
	if (path_len > len) {
		error = SET_ERROR(EFAULT);
		goto out;
	}

	memcpy(full_path, path_ptr, path_len);
	full_path[path_len] = '\0';
out:
	kmem_free(path_buffer, len);

	return (error);
}

/*
 * Returns full path in full_path: "/pool/dataset/.zfs/snapshot/snap_name/"
 */
static int
zfsctl_snapshot_path_objset(zfs_sb_t *zsb, uint64_t objsetid,
    int path_len, char *full_path)
{
	objset_t *os = zsb->z_os;
	fstrans_cookie_t cookie;
	char *snapname;
	boolean_t case_conflict;
	uint64_t id, pos = 0;
	int error = 0;

	if (zsb->z_mntopts->z_mntpoint == NULL)
		return (ENOENT);

	cookie = spl_fstrans_mark();
	snapname = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	while (error == 0) {
		dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
		error = dmu_snapshot_list_next(zsb->z_os, MAXNAMELEN,
		    snapname, &id, &pos, &case_conflict);
		dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
		if (error)
			goto out;

		if (id == objsetid)
			break;
	}

	memset(full_path, 0, path_len);
	snprintf(full_path, path_len - 1, "%s/.zfs/snapshot/%s",
	    zsb->z_mntopts->z_mntpoint, snapname);
out:
	kmem_free(snapname, MAXNAMELEN);
	spl_fstrans_unmark(cookie);

	return (error);
}

/*
 * Special case the handling of "..".
 */
int
zfsctl_root_lookup(struct inode *dip, char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	int error = 0;

	ZFS_ENTER(zsb);

	if (strcmp(name, "..") == 0) {
		*ipp = dip->i_sb->s_root->d_inode;
	} else if (strcmp(name, ZFS_SNAPDIR_NAME) == 0) {
		*ipp = zfsctl_inode_lookup(zsb, ZFSCTL_INO_SNAPDIR,
		    &zpl_fops_snapdir, &zpl_ops_snapdir);
	} else if (strcmp(name, ZFS_SHAREDIR_NAME) == 0) {
		*ipp = zfsctl_inode_lookup(zsb, ZFSCTL_INO_SHARES,
		    &zpl_fops_shares, &zpl_ops_shares);
	} else {
		*ipp = NULL;
	}

	if (*ipp == NULL)
		error = SET_ERROR(ENOENT);

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Lookup entry point for the 'snapshot' directory.  Try to open the
 * snapshot if it exist, creating the pseudo filesystem inode as necessary.
 * Perform a mount of the associated dataset on top of the inode.
 */
int
zfsctl_snapdir_lookup(struct inode *dip, char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	uint64_t id;
	int error;

	ZFS_ENTER(zsb);

	error = dmu_snapshot_lookup(zsb->z_os, name, &id);
	if (error) {
		ZFS_EXIT(zsb);
		return (error);
	}

	*ipp = zfsctl_inode_lookup(zsb, ZFSCTL_INO_SNAPDIRS - id,
	    &simple_dir_operations, &simple_dir_inode_operations);
	if (*ipp == NULL)
		error = SET_ERROR(ENOENT);

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Renaming a directory under '.zfs/snapshot' will automatically trigger
 * a rename of the snapshot to the new given name.  The rename is confined
 * to the '.zfs/snapshot' directory snapshots cannot be moved elsewhere.
 */
int
zfsctl_snapdir_rename(struct inode *sdip, char *snm,
    struct inode *tdip, char *tnm, cred_t *cr, int flags)
{
	zfs_sb_t *zsb = ITOZSB(sdip);
	char *to, *from, *real, *fsname;
	int error;

	if (!zfs_admin_snapshot)
		return (EACCES);

	ZFS_ENTER(zsb);

	to = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	from = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	real = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	fsname = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	if (zsb->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zsb->z_os, snm, real,
		    MAXNAMELEN, NULL);
		if (error == 0) {
			snm = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	dmu_objset_name(zsb->z_os, fsname);

	error = zfsctl_snapshot_name(ITOZSB(sdip), snm, MAXNAMELEN, from);
	if (error == 0)
		error = zfsctl_snapshot_name(ITOZSB(tdip), tnm, MAXNAMELEN, to);
	if (error == 0)
		error = zfs_secpolicy_rename_perms(from, to, cr);
	if (error != 0)
		goto out;

	/*
	 * Cannot move snapshots out of the snapdir.
	 */
	if (sdip != tdip) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * No-op when names are identical.
	 */
	if (strcmp(snm, tnm) == 0) {
		error = 0;
		goto out;
	}

	rw_enter(&zfs_snapshot_lock, RW_WRITER);

	error = dsl_dataset_rename_snapshot(fsname, snm, tnm, B_FALSE);
	if (error == 0)
		(void) zfsctl_snapshot_rename(snm, tnm);

	rw_exit(&zfs_snapshot_lock);
out:
	kmem_free(from, MAXNAMELEN);
	kmem_free(to, MAXNAMELEN);
	kmem_free(real, MAXNAMELEN);
	kmem_free(fsname, MAXNAMELEN);

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Removing a directory under '.zfs/snapshot' will automatically trigger
 * the removal of the snapshot with the given name.
 */
int
zfsctl_snapdir_remove(struct inode *dip, char *name, cred_t *cr, int flags)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	char *snapname, *real;
	int error;

	if (!zfs_admin_snapshot)
		return (EACCES);

	ZFS_ENTER(zsb);

	snapname = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	real = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	if (zsb->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zsb->z_os, name, real,
		    MAXNAMELEN, NULL);
		if (error == 0) {
			name = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	error = zfsctl_snapshot_name(ITOZSB(dip), name, MAXNAMELEN, snapname);
	if (error == 0)
		error = zfs_secpolicy_destroy_perms(snapname, cr);
	if (error != 0)
		goto out;

	error = zfsctl_snapshot_unmount(snapname, MNT_FORCE);
	if ((error == 0) || (error == ENOENT))
		error = dsl_destroy_snapshot(snapname, B_FALSE);
out:
	kmem_free(snapname, MAXNAMELEN);
	kmem_free(real, MAXNAMELEN);

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Creating a directory under '.zfs/snapshot' will automatically trigger
 * the creation of a new snapshot with the given name.
 */
int
zfsctl_snapdir_mkdir(struct inode *dip, char *dirname, vattr_t *vap,
	struct inode **ipp, cred_t *cr, int flags)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	char *dsname;
	int error;

	if (!zfs_admin_snapshot)
		return (EACCES);

	dsname = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	if (zfs_component_namecheck(dirname, NULL, NULL) != 0) {
		error = SET_ERROR(EILSEQ);
		goto out;
	}

	dmu_objset_name(zsb->z_os, dsname);

	error = zfs_secpolicy_snapshot_perms(dsname, cr);
	if (error != 0)
		goto out;

	if (error == 0) {
		error = dmu_objset_snapshot_one(dsname, dirname);
		if (error != 0)
			goto out;

		error = zfsctl_snapdir_lookup(dip, dirname, ipp,
		    0, cr, NULL, NULL);
	}
out:
	kmem_free(dsname, MAXNAMELEN);

	return (error);
}

/*
 * Attempt to unmount a snapshot by making a call to user space.
 * There is no assurance that this can or will succeed, is just a
 * best effort.  In the case where it does fail, perhaps because
 * it's in use, the unmount will fail harmlessly.
 */
int
zfsctl_snapshot_unmount(char *snapname, int flags)
{
	char *argv[] = { "/usr/bin/env", "umount", "-t", "zfs", "-n", NULL,
	    NULL };
	char *envp[] = { NULL };
	zfs_snapentry_t *se;
	int error;

	rw_enter(&zfs_snapshot_lock, RW_READER);
	if ((se = zfsctl_snapshot_find_by_name(snapname)) == NULL) {
		rw_exit(&zfs_snapshot_lock);
		return (ENOENT);
	}
	rw_exit(&zfs_snapshot_lock);

	if (flags & MNT_FORCE)
		argv[4] = "-fn";
	argv[5] = se->se_path;
	dprintf("unmount; path=%s\n", se->se_path);
	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	zfsctl_snapshot_rele(se);


	/*
	 * The umount system utility will return 256 on error.  We must
	 * assume this error is because the file system is busy so it is
	 * converted to the more sensible EBUSY.
	 */
	if (error)
		error = SET_ERROR(EBUSY);

	return (error);
}

#define	MOUNT_BUSY 0x80		/* Mount failed due to EBUSY (from mntent.h) */

int
zfsctl_snapshot_mount(struct path *path, int flags)
{
	struct dentry *dentry = path->dentry;
	struct inode *ip = dentry->d_inode;
	zfs_sb_t *zsb;
	zfs_sb_t *snap_zsb;
	zfs_snapentry_t *se;
	char *full_name, *full_path;
	char *argv[] = { "/usr/bin/env", "mount", "-t", "zfs", "-n", NULL, NULL,
	    NULL };
	char *envp[] = { NULL };
	int error;
	struct path spath;

	if (ip == NULL)
		return (EISDIR);

	zsb = ITOZSB(ip);
	ZFS_ENTER(zsb);

	full_name = kmem_zalloc(MAXNAMELEN, KM_SLEEP);
	full_path = kmem_zalloc(MAXPATHLEN, KM_SLEEP);

	error = zfsctl_snapshot_name(zsb, dname(dentry),
	    MAXNAMELEN, full_name);
	if (error)
		goto error;

	error = zfsctl_snapshot_path(path, MAXPATHLEN, full_path);
	if (error)
		goto error;

	/*
	 * Multiple concurrent automounts of a snapshot are never allowed.
	 * The snapshot may be manually mounted as many times as desired.
	 */
	if (zfsctl_snapshot_ismounted(full_name)) {
		error = 0;
		goto error;
	}

	/*
	 * Attempt to mount the snapshot from user space.  Normally this
	 * would be done using the vfs_kern_mount() function, however that
	 * function is marked GPL-only and cannot be used.  On error we
	 * careful to log the real error to the console and return EISDIR
	 * to safely abort the automount.  This should be very rare.
	 *
	 * If the user mode helper happens to return EBUSY, a concurrent
	 * mount is already in progress in which case the error is ignored.
	 * Take note that if the program was executed successfully the return
	 * value from call_usermodehelper() will be (exitcode << 8 + signal).
	 */
	dprintf("mount; name=%s path=%s\n", full_name, full_path);
	argv[5] = full_name;
	argv[6] = full_path;
	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (error) {
		if (!(error & MOUNT_BUSY << 8)) {
			cmn_err(CE_WARN, "Unable to automount %s/%s: %d",
			    full_path, full_name, error);
			error = SET_ERROR(EISDIR);
		} else {
			/*
			 * EBUSY, this could mean a concurrent mount, or the
			 * snapshot has already been mounted at completely
			 * different place. We return 0 so VFS will retry. For
			 * the latter case the VFS will retry several times
			 * and return ELOOP, which is probably not a very good
			 * behavior.
			 */
			error = 0;
		}
		goto error;
	}

	/*
	 * Follow down in to the mounted snapshot and set MNT_SHRINKABLE
	 * to identify this as an automounted filesystem.
	 */
	spath = *path;
	path_get(&spath);
	if (zpl_follow_down_one(&spath)) {
		snap_zsb = ITOZSB(spath.dentry->d_inode);
		snap_zsb->z_parent = zsb;
		dentry = spath.dentry;
		spath.mnt->mnt_flags |= MNT_SHRINKABLE;

		rw_enter(&zfs_snapshot_lock, RW_WRITER);
		se = zfsctl_snapshot_alloc(full_name, full_path,
		    snap_zsb->z_os->os_spa, dmu_objset_id(snap_zsb->z_os),
		    dentry);
		zfsctl_snapshot_add(se);
		zfsctl_snapshot_unmount_delay_impl(se, zfs_expire_snapshot);
		rw_exit(&zfs_snapshot_lock);
	}
	path_put(&spath);
error:
	kmem_free(full_name, MAXNAMELEN);
	kmem_free(full_path, MAXPATHLEN);

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Given the objset id of the snapshot return its zfs_sb_t as zsbp.
 */
int
zfsctl_lookup_objset(struct super_block *sb, uint64_t objsetid, zfs_sb_t **zsbp)
{
	zfs_snapentry_t *se;
	int error;
	spa_t *spa = ((zfs_sb_t *)(sb->s_fs_info))->z_os->os_spa;

	/*
	 * Verify that the snapshot is mounted then lookup the mounted root
	 * rather than the covered mount point.  This may fail if the
	 * snapshot has just been unmounted by an unrelated user space
	 * process.  This race cannot occur to an expired mount point
	 * because we hold the zfs_snapshot_lock to prevent the race.
	 */
	rw_enter(&zfs_snapshot_lock, RW_READER);
	if ((se = zfsctl_snapshot_find_by_objsetid(spa, objsetid)) != NULL) {
		zfs_sb_t *zsb;

		zsb = ITOZSB(se->se_root_dentry->d_inode);
		ASSERT3U(dmu_objset_id(zsb->z_os), ==, objsetid);

		if (time_after(jiffies, zsb->z_snap_defer_time +
		    MAX(zfs_expire_snapshot * HZ / 2, HZ))) {
			zsb->z_snap_defer_time = jiffies;
			zfsctl_snapshot_unmount_cancel(se);
			zfsctl_snapshot_unmount_delay_impl(se,
			    zfs_expire_snapshot);
		}

		*zsbp = zsb;
		zfsctl_snapshot_rele(se);
		error = SET_ERROR(0);
	} else {
		error = SET_ERROR(ENOENT);
	}
	rw_exit(&zfs_snapshot_lock);

	/*
	 * Automount the snapshot given the objset id by constructing the
	 * full mount point and performing a traversal.
	 */
	if (error == ENOENT) {
		struct path path;
		char *mnt;

		mnt = kmem_alloc(MAXPATHLEN, KM_SLEEP);
		error = zfsctl_snapshot_path_objset(sb->s_fs_info, objsetid,
		    MAXPATHLEN, mnt);
		if (error) {
			kmem_free(mnt, MAXPATHLEN);
			return (SET_ERROR(error));
		}

		error = kern_path(mnt, LOOKUP_FOLLOW|LOOKUP_DIRECTORY, &path);
		if (error == 0) {
			*zsbp = ITOZSB(path.dentry->d_inode);
			path_put(&path);
		}

		kmem_free(mnt, MAXPATHLEN);
	}

	return (error);
}

int
zfsctl_shares_lookup(struct inode *dip, char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	struct inode *ip;
	znode_t *dzp;
	int error;

	ZFS_ENTER(zsb);

	if (zsb->z_shares_dir == 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(ENOTSUP));
	}

	error = zfs_zget(zsb, zsb->z_shares_dir, &dzp);
	if (error) {
		ZFS_EXIT(zsb);
		return (error);
	}

	error = zfs_lookup(ZTOI(dzp), name, &ip, 0, cr, NULL, NULL);

	iput(ZTOI(dzp));
	ZFS_EXIT(zsb);

	return (error);
}


/*
 * Initialize the various pieces we'll need to create and manipulate .zfs
 * directories.  Currently this is unused but available.
 */
void
zfsctl_init(void)
{
	avl_create(&zfs_snapshots_by_name, snapentry_compare_by_name,
	    sizeof (zfs_snapentry_t), offsetof(zfs_snapentry_t,
	    se_node_name));
	avl_create(&zfs_snapshots_by_objsetid, snapentry_compare_by_objsetid,
	    sizeof (zfs_snapentry_t), offsetof(zfs_snapentry_t,
	    se_node_objsetid));
	rw_init(&zfs_snapshot_lock, NULL, RW_DEFAULT, NULL);

	zfs_expire_taskq = taskq_create("z_unmount", 1, defclsyspri,
	    1, 8, TASKQ_PREPOPULATE);
}

/*
 * Cleanup the various pieces we needed for .zfs directories.  In particular
 * ensure the expiry timer is canceled safely.
 */
void
zfsctl_fini(void)
{
	taskq_destroy(zfs_expire_taskq);

	avl_destroy(&zfs_snapshots_by_name);
	avl_destroy(&zfs_snapshots_by_objsetid);
	rw_destroy(&zfs_snapshot_lock);
}

module_param(zfs_admin_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_admin_snapshot, "Enable mkdir/rmdir/mv in .zfs/snapshot");

module_param(zfs_expire_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_expire_snapshot, "Seconds to expire .zfs/snapshot");
