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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_ZNODE_H
#define	_SYS_FS_ZFS_ZNODE_H

#ifdef _KERNEL
#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/attr.h>
#include <sys/list.h>
#include <sys/dmu.h>
#include <sys/sa.h>
#include <sys/zfs_vfsops.h>
#include <sys/rrwlock.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>
#include <sys/zfs_rlock.h>
#endif
#include <sys/zfs_acl.h>
#include <sys/zil.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Additional file level attributes, that are stored
 * in the upper half of zp_flags
 */
#define	ZFS_READONLY		0x0000000100000000ull
#define	ZFS_HIDDEN		0x0000000200000000ull
#define	ZFS_SYSTEM		0x0000000400000000ull
#define	ZFS_ARCHIVE		0x0000000800000000ull
#define	ZFS_IMMUTABLE		0x0000001000000000ull
#define	ZFS_NOUNLINK		0x0000002000000000ull
#define	ZFS_APPENDONLY		0x0000004000000000ull
#define	ZFS_NODUMP		0x0000008000000000ull
#define	ZFS_OPAQUE		0x0000010000000000ull
#define	ZFS_AV_QUARANTINED	0x0000020000000000ull
#define	ZFS_AV_MODIFIED		0x0000040000000000ull
#define	ZFS_REPARSE		0x0000080000000000ull
#define	ZFS_OFFLINE		0x0000100000000000ull
#define	ZFS_SPARSE		0x0000200000000000ull

#define	ZFS_ATTR_SET(zp, attr, value, pflags, tx) \
{ \
	if (value) \
		pflags |= attr; \
	else \
		pflags &= ~attr; \
	VERIFY(0 == sa_update(zp->z_sa_hdl, SA_ZPL_FLAGS(ZTOZSB(zp)), \
	    &pflags, sizeof (pflags), tx)); \
}

/*
 * Define special zfs pflags
 */
#define	ZFS_XATTR		0x1		/* is an extended attribute */
#define	ZFS_INHERIT_ACE		0x2		/* ace has inheritable ACEs */
#define	ZFS_ACL_TRIVIAL		0x4		/* files ACL is trivial */
#define	ZFS_ACL_OBJ_ACE		0x8		/* ACL has CMPLX Object ACE */
#define	ZFS_ACL_PROTECTED	0x10		/* ACL protected */
#define	ZFS_ACL_DEFAULTED	0x20		/* ACL should be defaulted */
#define	ZFS_ACL_AUTO_INHERIT	0x40		/* ACL should be inherited */
#define	ZFS_BONUS_SCANSTAMP	0x80		/* Scanstamp in bonus area */
#define	ZFS_NO_EXECS_DENIED	0x100		/* exec was given to everyone */

#define	SA_ZPL_ATIME(z)		z->z_attr_table[ZPL_ATIME]
#define	SA_ZPL_MTIME(z)		z->z_attr_table[ZPL_MTIME]
#define	SA_ZPL_CTIME(z)		z->z_attr_table[ZPL_CTIME]
#define	SA_ZPL_CRTIME(z)	z->z_attr_table[ZPL_CRTIME]
#define	SA_ZPL_GEN(z)		z->z_attr_table[ZPL_GEN]
#define	SA_ZPL_DACL_ACES(z)	z->z_attr_table[ZPL_DACL_ACES]
#define	SA_ZPL_XATTR(z)		z->z_attr_table[ZPL_XATTR]
#define	SA_ZPL_SYMLINK(z)	z->z_attr_table[ZPL_SYMLINK]
#define	SA_ZPL_RDEV(z)		z->z_attr_table[ZPL_RDEV]
#define	SA_ZPL_SCANSTAMP(z)	z->z_attr_table[ZPL_SCANSTAMP]
#define	SA_ZPL_UID(z)		z->z_attr_table[ZPL_UID]
#define	SA_ZPL_GID(z)		z->z_attr_table[ZPL_GID]
#define	SA_ZPL_PARENT(z)	z->z_attr_table[ZPL_PARENT]
#define	SA_ZPL_LINKS(z)		z->z_attr_table[ZPL_LINKS]
#define	SA_ZPL_MODE(z)		z->z_attr_table[ZPL_MODE]
#define	SA_ZPL_DACL_COUNT(z)	z->z_attr_table[ZPL_DACL_COUNT]
#define	SA_ZPL_FLAGS(z)		z->z_attr_table[ZPL_FLAGS]
#define	SA_ZPL_SIZE(z)		z->z_attr_table[ZPL_SIZE]
#define	SA_ZPL_ZNODE_ACL(z)	z->z_attr_table[ZPL_ZNODE_ACL]
#define	SA_ZPL_DXATTR(z)	z->z_attr_table[ZPL_DXATTR]
#define	SA_ZPL_PAD(z)		z->z_attr_table[ZPL_PAD]

/*
 * Is ID ephemeral?
 */
#define	IS_EPHEMERAL(x)		(x > MAXUID)

/*
 * Should we use FUIDs?
 */
#define	USE_FUIDS(version, os)	(version >= ZPL_VERSION_FUID && \
    spa_version(dmu_objset_spa(os)) >= SPA_VERSION_FUID)
#define	USE_SA(version, os) (version >= ZPL_VERSION_SA && \
    spa_version(dmu_objset_spa(os)) >= SPA_VERSION_SA)

#define	MASTER_NODE_OBJ	1

/*
 * Special attributes for master node.
 * "userquota@" and "groupquota@" are also valid (from
 * zfs_userquota_prop_prefixes[]).
 */
#define	ZFS_FSID		"FSID"
#define	ZFS_UNLINKED_SET	"DELETE_QUEUE"
#define	ZFS_ROOT_OBJ		"ROOT"
#define	ZPL_VERSION_STR		"VERSION"
#define	ZFS_FUID_TABLES		"FUID"
#define	ZFS_SHARES_DIR		"SHARES"
#define	ZFS_SA_ATTRS		"SA_ATTRS"

/*
 * Convert mode bits (zp_mode) to BSD-style DT_* values for storing in
 * the directory entries.  On Linux systems this value is already
 * defined correctly as part of the /usr/include/dirent.h header file.
 */
#ifndef IFTODT
#define	IFTODT(mode) (((mode) & S_IFMT) >> 12)
#endif

/*
 * The directory entry has the type (currently unused on Solaris) in the
 * top 4 bits, and the object number in the low 48 bits.  The "middle"
 * 12 bits are unused.
 */
#define	ZFS_DIRENT_TYPE(de) BF64_GET(de, 60, 4)
#define	ZFS_DIRENT_OBJ(de) BF64_GET(de, 0, 48)

/*
 * Directory entry locks control access to directory entries.
 * They are used to protect creates, deletes, and renames.
 * Each directory znode has a mutex and a list of locked names.
 */
#ifdef _KERNEL
typedef struct zfs_dirlock {
	char		*dl_name;	/* directory entry being locked */
	uint32_t	dl_sharecnt;	/* 0 if exclusive, > 0 if shared */
	uint8_t		dl_namelock;	/* 1 if z_name_lock is NOT held */
	uint16_t	dl_namesize;	/* set if dl_name was allocated */
	kcondvar_t	dl_cv;		/* wait for entry to be unlocked */
	struct znode	*dl_dzp;	/* directory znode */
	struct zfs_dirlock *dl_next;	/* next in z_dirlocks list */
} zfs_dirlock_t;

typedef struct znode {
	uint64_t	z_id;		/* object ID for this znode */
	kmutex_t	z_lock;		/* znode modification lock */
	krwlock_t	z_parent_lock;	/* parent lock for directories */
	krwlock_t	z_name_lock;	/* "master" lock for dirent locks */
	zfs_dirlock_t	*z_dirlocks;	/* directory entry lock list */
	zfs_rlock_t	z_range_lock;	/* file range lock */
	uint8_t		z_unlinked;	/* file has been unlinked */
	uint8_t		z_atime_dirty;	/* atime needs to be synced */
	uint8_t		z_zn_prefetch;	/* Prefetch znodes? */
	uint8_t		z_moved;	/* Has this znode been moved? */
	uint_t		z_blksz;	/* block size in bytes */
	uint_t		z_seq;		/* modification sequence number */
	uint64_t	z_mapcnt;	/* number of pages mapped to file */
	uint64_t	z_dnodesize;	/* dnode size */
	uint64_t	z_size;		/* file size (cached) */
	uint64_t	z_pflags;	/* pflags (cached) */
	uint32_t	z_sync_cnt;	/* synchronous open count */
	mode_t		z_mode;		/* mode (cached) */
	kmutex_t	z_acl_lock;	/* acl data lock */
	zfs_acl_t	*z_acl_cached;	/* cached acl */
	krwlock_t	z_xattr_lock;	/* xattr data lock */
	nvlist_t	*z_xattr_cached; /* cached xattrs */
	uint64_t	z_xattr_parent;	/* parent obj for this xattr */
	list_node_t	z_link_node;	/* all znodes in fs link */
	sa_handle_t	*z_sa_hdl;	/* handle to sa data */
	boolean_t	z_is_sa;	/* are we native sa? */
	boolean_t	z_is_mapped;	/* are we mmap'ed */
	boolean_t	z_is_ctldir;	/* are we .zfs entry */
	boolean_t	z_is_stale;	/* are we stale due to rollback? */
	struct inode	z_inode;	/* generic vfs inode */
} znode_t;

typedef struct znode_hold {
	uint64_t	zh_obj;		/* object id */
	kmutex_t	zh_lock;	/* lock serializing object access */
	avl_node_t	zh_node;	/* avl tree linkage */
	refcount_t	zh_refcount;	/* active consumer reference count */
} znode_hold_t;

/*
 * Range locking rules
 * --------------------
 * 1. When truncating a file (zfs_create, zfs_setattr, zfs_space) the whole
 *    file range needs to be locked as RL_WRITER. Only then can the pages be
 *    freed etc and zp_size reset. zp_size must be set within range lock.
 * 2. For writes and punching holes (zfs_write & zfs_space) just the range
 *    being written or freed needs to be locked as RL_WRITER.
 *    Multiple writes at the end of the file must coordinate zp_size updates
 *    to ensure data isn't lost. A compare and swap loop is currently used
 *    to ensure the file size is at least the offset last written.
 * 3. For reads (zfs_read, zfs_get_data & zfs_putapage) just the range being
 *    read needs to be locked as RL_READER. A check against zp_size can then
 *    be made for reading beyond end of file.
 */

/*
 * Convert between znode pointers and inode pointers
 */
#define	ZTOI(znode)	(&((znode)->z_inode))
#define	ITOZ(inode)	(container_of((inode), znode_t, z_inode))
#define	ZTOZSB(znode)	((zfsvfs_t *)(ZTOI(znode)->i_sb->s_fs_info))
#define	ITOZSB(inode)	((zfsvfs_t *)((inode)->i_sb->s_fs_info))

#define	S_ISDEV(mode)	(S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))

/* Called on entry to each ZFS vnode and vfs operation  */
#define	ZFS_ENTER(zfsvfs) \
	{ \
		rrm_enter_read(&(zfsvfs)->z_teardown_lock, FTAG); \
		if ((zfsvfs)->z_unmounted) { \
			ZFS_EXIT(zfsvfs); \
			return (EIO); \
		} \
	}

/* Must be called before exiting the vop */
#define	ZFS_EXIT(zfsvfs) \
	{ \
		rrm_exit(&(zfsvfs)->z_teardown_lock, FTAG); \
	}

/* Verifies the znode is valid */
#define	ZFS_VERIFY_ZP(zp) \
	if ((zp)->z_sa_hdl == NULL) { \
		ZFS_EXIT(ZTOZSB(zp)); \
		return (EIO); \
	}

/*
 * Macros for dealing with dmu_buf_hold
 */
#define	ZFS_OBJ_MTX_SZ		64
#define	ZFS_OBJ_MTX_MAX		(1024 * 1024)
#define	ZFS_OBJ_HASH(zfsvfs, obj)	((obj) & ((zfsvfs->z_hold_size) - 1))

extern unsigned int zfs_object_mutex_size;

/* Encode ZFS stored time values from a struct timespec */
#define	ZFS_TIME_ENCODE(tp, stmp)		\
{						\
	(stmp)[0] = (uint64_t)(tp)->tv_sec;	\
	(stmp)[1] = (uint64_t)(tp)->tv_nsec;	\
}

/* Decode ZFS stored time values to a struct timespec */
#define	ZFS_TIME_DECODE(tp, stmp)		\
{						\
	(tp)->tv_sec = (time_t)(stmp)[0];		\
	(tp)->tv_nsec = (long)(stmp)[1];		\
}

/*
 * Timestamp defines
 */
#define	ACCESSED		(ATTR_ATIME)
#define	STATE_CHANGED		(ATTR_CTIME)
#define	CONTENT_MODIFIED	(ATTR_MTIME | ATTR_CTIME)

extern int	zfs_init_fs(zfsvfs_t *, znode_t **);
extern void	zfs_set_dataprop(objset_t *);
extern void	zfs_create_fs(objset_t *os, cred_t *cr, nvlist_t *,
    dmu_tx_t *tx);
extern void	zfs_tstamp_update_setup(znode_t *, uint_t, uint64_t [2],
    uint64_t [2]);
extern void	zfs_grow_blocksize(znode_t *, uint64_t, dmu_tx_t *);
extern int	zfs_freesp(znode_t *, uint64_t, uint64_t, int, boolean_t);
extern void	zfs_znode_init(void);
extern void	zfs_znode_fini(void);
extern int	zfs_znode_hold_compare(const void *, const void *);
extern int	zfs_zget(zfsvfs_t *, uint64_t, znode_t **);
extern int	zfs_rezget(znode_t *);
extern void	zfs_zinactive(znode_t *);
extern void	zfs_znode_delete(znode_t *, dmu_tx_t *);
extern void	zfs_remove_op_tables(void);
extern int	zfs_create_op_tables(void);
extern int	zfs_sync(struct super_block *, int, cred_t *);
extern dev_t	zfs_cmpldev(uint64_t);
extern int	zfs_get_zplprop(objset_t *os, zfs_prop_t prop, uint64_t *value);
extern int	zfs_get_stats(objset_t *os, nvlist_t *nv);
extern boolean_t zfs_get_vfs_flag_unmounted(objset_t *os);
extern void	zfs_znode_dmu_fini(znode_t *);
extern int	zfs_inode_alloc(struct super_block *, struct inode **ip);
extern void	zfs_inode_destroy(struct inode *);
extern void	zfs_inode_update(znode_t *);
extern void	zfs_mark_inode_dirty(struct inode *);

extern void zfs_log_create(zilog_t *zilog, dmu_tx_t *tx, uint64_t txtype,
    znode_t *dzp, znode_t *zp, char *name, vsecattr_t *, zfs_fuid_info_t *,
    vattr_t *vap);
extern int zfs_log_create_txtype(zil_create_t, vsecattr_t *vsecp,
    vattr_t *vap);
extern void zfs_log_remove(zilog_t *zilog, dmu_tx_t *tx, uint64_t txtype,
    znode_t *dzp, char *name, uint64_t foid);
#define	ZFS_NO_OBJECT	0	/* no object id */
extern void zfs_log_link(zilog_t *zilog, dmu_tx_t *tx, uint64_t txtype,
    znode_t *dzp, znode_t *zp, char *name);
extern void zfs_log_symlink(zilog_t *zilog, dmu_tx_t *tx, uint64_t txtype,
    znode_t *dzp, znode_t *zp, char *name, char *link);
extern void zfs_log_rename(zilog_t *zilog, dmu_tx_t *tx, uint64_t txtype,
    znode_t *sdzp, char *sname, znode_t *tdzp, char *dname, znode_t *szp);
extern void zfs_log_write(zilog_t *zilog, dmu_tx_t *tx, int txtype,
    znode_t *zp, offset_t off, ssize_t len, int ioflag,
    zil_callback_t callback, void *callback_data);
extern void zfs_log_truncate(zilog_t *zilog, dmu_tx_t *tx, int txtype,
    znode_t *zp, uint64_t off, uint64_t len);
extern void zfs_log_setattr(zilog_t *zilog, dmu_tx_t *tx, int txtype,
    znode_t *zp, vattr_t *vap, uint_t mask_applied, zfs_fuid_info_t *fuidp);
extern void zfs_log_acl(zilog_t *zilog, dmu_tx_t *tx, znode_t *zp,
    vsecattr_t *vsecp, zfs_fuid_info_t *fuidp);
extern void zfs_xvattr_set(znode_t *zp, xvattr_t *xvap, dmu_tx_t *tx);
extern void zfs_upgrade(zfsvfs_t *zfsvfs, dmu_tx_t *tx);
extern int zfs_create_share_dir(zfsvfs_t *zfsvfs, dmu_tx_t *tx);

#if defined(HAVE_UIO_RW)
extern caddr_t zfs_map_page(page_t *, enum seg_rw);
extern void zfs_unmap_page(page_t *, caddr_t);
#endif /* HAVE_UIO_RW */

extern zil_get_data_t zfs_get_data;
extern zil_replay_func_t zfs_replay_vector[TX_MAX_TYPE];
extern int zfsfstype;

#endif /* _KERNEL */

extern int zfs_obj_to_path(objset_t *osp, uint64_t obj, char *buf, int len);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_ZNODE_H */
