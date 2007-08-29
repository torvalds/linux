/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_VNODE_H__
#define __XFS_VNODE_H__

struct file;
struct bhv_vfs;
struct bhv_vattr;
struct xfs_iomap;
struct attrlist_cursor_kern;

typedef struct dentry	bhv_vname_t;
typedef __u64		bhv_vnumber_t;

typedef enum bhv_vflags {
	VMODIFIED	= 0x08,	/* XFS inode state possibly differs */
				/* to the Linux inode state. */
	VTRUNCATED	= 0x40,	/* truncated down so flush-on-close */
} bhv_vflags_t;

/*
 * MP locking protocols:
 *	v_flag, 				VN_LOCK/VN_UNLOCK
 */
typedef struct bhv_vnode {
	bhv_vflags_t	v_flag;			/* vnode flags (see above) */
	bhv_vnumber_t	v_number;		/* in-core vnode number */
	spinlock_t	v_lock;			/* VN_LOCK/VN_UNLOCK */
	atomic_t	v_iocount;		/* outstanding I/O count */
#ifdef XFS_VNODE_TRACE
	struct ktrace	*v_trace;		/* trace header structure    */
#endif
	struct inode	v_inode;		/* Linux inode */
	/* inode MUST be last */
} bhv_vnode_t;

#define VN_ISLNK(vp)	S_ISLNK((vp)->v_inode.i_mode)
#define VN_ISREG(vp)	S_ISREG((vp)->v_inode.i_mode)
#define VN_ISDIR(vp)	S_ISDIR((vp)->v_inode.i_mode)
#define VN_ISCHR(vp)	S_ISCHR((vp)->v_inode.i_mode)
#define VN_ISBLK(vp)	S_ISBLK((vp)->v_inode.i_mode)

/*
 * Vnode to Linux inode mapping.
 */
static inline struct bhv_vnode *vn_from_inode(struct inode *inode)
{
	return container_of(inode, bhv_vnode_t, v_inode);
}
static inline struct inode *vn_to_inode(struct bhv_vnode *vnode)
{
	return &vnode->v_inode;
}

/*
 * Values for the vop_rwlock/rwunlock flags parameter.
 */
typedef enum bhv_vrwlock {
	VRWLOCK_NONE,
	VRWLOCK_READ,
	VRWLOCK_WRITE,
	VRWLOCK_WRITE_DIRECT,
	VRWLOCK_TRY_READ,
	VRWLOCK_TRY_WRITE
} bhv_vrwlock_t;

/*
 * Return values for xfs_inactive.  A return value of
 * VN_INACTIVE_NOCACHE implies that the file system behavior
 * has disassociated its state and bhv_desc_t from the vnode.
 */
#define	VN_INACTIVE_CACHE	0
#define	VN_INACTIVE_NOCACHE	1

/*
 * Flags for read/write calls - same values as IRIX
 */
#define IO_ISAIO	0x00001		/* don't wait for completion */
#define IO_ISDIRECT	0x00004		/* bypass page cache */
#define IO_INVIS	0x00020		/* don't update inode timestamps */

/*
 * Flags for vop_iflush call
 */
#define FLUSH_SYNC		1	/* wait for flush to complete	*/
#define FLUSH_INODE		2	/* flush the inode itself	*/
#define FLUSH_LOG		4	/* force the last log entry for
					 * this inode out to disk	*/

/*
 * Flush/Invalidate options for vop_toss/flush/flushinval_pages.
 */
#define FI_NONE			0	/* none */
#define FI_REMAPF		1	/* Do a remapf prior to the operation */
#define FI_REMAPF_LOCKED	2	/* Do a remapf prior to the operation.
					   Prevent VM access to the pages until
					   the operation completes. */

/*
 * Vnode attributes.  va_mask indicates those attributes the caller
 * wants to set or extract.
 */
typedef struct bhv_vattr {
	int		va_mask;	/* bit-mask of attributes present */
	mode_t		va_mode;	/* file access mode and type */
	xfs_nlink_t	va_nlink;	/* number of references to file */
	uid_t		va_uid;		/* owner user id */
	gid_t		va_gid;		/* owner group id */
	xfs_ino_t	va_nodeid;	/* file id */
	xfs_off_t	va_size;	/* file size in bytes */
	u_long		va_blocksize;	/* blocksize preferred for i/o */
	struct timespec	va_atime;	/* time of last access */
	struct timespec	va_mtime;	/* time of last modification */
	struct timespec	va_ctime;	/* time file changed */
	u_int		va_gen;		/* generation number of file */
	xfs_dev_t	va_rdev;	/* device the special file represents */
	__int64_t	va_nblocks;	/* number of blocks allocated */
	u_long		va_xflags;	/* random extended file flags */
	u_long		va_extsize;	/* file extent size */
	u_long		va_nextents;	/* number of extents in file */
	u_long		va_anextents;	/* number of attr extents in file */
	prid_t		va_projid;	/* project id */
} bhv_vattr_t;

/*
 * setattr or getattr attributes
 */
#define XFS_AT_TYPE		0x00000001
#define XFS_AT_MODE		0x00000002
#define XFS_AT_UID		0x00000004
#define XFS_AT_GID		0x00000008
#define XFS_AT_FSID		0x00000010
#define XFS_AT_NODEID		0x00000020
#define XFS_AT_NLINK		0x00000040
#define XFS_AT_SIZE		0x00000080
#define XFS_AT_ATIME		0x00000100
#define XFS_AT_MTIME		0x00000200
#define XFS_AT_CTIME		0x00000400
#define XFS_AT_RDEV		0x00000800
#define XFS_AT_BLKSIZE		0x00001000
#define XFS_AT_NBLOCKS		0x00002000
#define XFS_AT_VCODE		0x00004000
#define XFS_AT_MAC		0x00008000
#define XFS_AT_UPDATIME		0x00010000
#define XFS_AT_UPDMTIME		0x00020000
#define XFS_AT_UPDCTIME		0x00040000
#define XFS_AT_ACL		0x00080000
#define XFS_AT_CAP		0x00100000
#define XFS_AT_INF		0x00200000
#define XFS_AT_XFLAGS		0x00400000
#define XFS_AT_EXTSIZE		0x00800000
#define XFS_AT_NEXTENTS		0x01000000
#define XFS_AT_ANEXTENTS	0x02000000
#define XFS_AT_PROJID		0x04000000
#define XFS_AT_SIZE_NOPERM	0x08000000
#define XFS_AT_GENCOUNT		0x10000000

#define XFS_AT_ALL	(XFS_AT_TYPE|XFS_AT_MODE|XFS_AT_UID|XFS_AT_GID|\
		XFS_AT_FSID|XFS_AT_NODEID|XFS_AT_NLINK|XFS_AT_SIZE|\
		XFS_AT_ATIME|XFS_AT_MTIME|XFS_AT_CTIME|XFS_AT_RDEV|\
		XFS_AT_BLKSIZE|XFS_AT_NBLOCKS|XFS_AT_VCODE|XFS_AT_MAC|\
		XFS_AT_ACL|XFS_AT_CAP|XFS_AT_INF|XFS_AT_XFLAGS|XFS_AT_EXTSIZE|\
		XFS_AT_NEXTENTS|XFS_AT_ANEXTENTS|XFS_AT_PROJID|XFS_AT_GENCOUNT)

#define XFS_AT_STAT	(XFS_AT_TYPE|XFS_AT_MODE|XFS_AT_UID|XFS_AT_GID|\
		XFS_AT_FSID|XFS_AT_NODEID|XFS_AT_NLINK|XFS_AT_SIZE|\
		XFS_AT_ATIME|XFS_AT_MTIME|XFS_AT_CTIME|XFS_AT_RDEV|\
		XFS_AT_BLKSIZE|XFS_AT_NBLOCKS|XFS_AT_PROJID)

#define XFS_AT_TIMES	(XFS_AT_ATIME|XFS_AT_MTIME|XFS_AT_CTIME)

#define XFS_AT_UPDTIMES	(XFS_AT_UPDATIME|XFS_AT_UPDMTIME|XFS_AT_UPDCTIME)

#define XFS_AT_NOSET	(XFS_AT_NLINK|XFS_AT_RDEV|XFS_AT_FSID|XFS_AT_NODEID|\
		XFS_AT_TYPE|XFS_AT_BLKSIZE|XFS_AT_NBLOCKS|XFS_AT_VCODE|\
		XFS_AT_NEXTENTS|XFS_AT_ANEXTENTS|XFS_AT_GENCOUNT)

/*
 *  Modes.
 */
#define VSUID	S_ISUID		/* set user id on execution */
#define VSGID	S_ISGID		/* set group id on execution */
#define VSVTX	S_ISVTX		/* save swapped text even after use */
#define VREAD	S_IRUSR		/* read, write, execute permissions */
#define VWRITE	S_IWUSR
#define VEXEC	S_IXUSR

#define MODEMASK S_IALLUGO	/* mode bits plus permission bits */

/*
 * Check whether mandatory file locking is enabled.
 */
#define MANDLOCK(vp, mode)	\
	(VN_ISREG(vp) && ((mode) & (VSGID|(VEXEC>>3))) == VSGID)

extern void	vn_init(void);
extern bhv_vnode_t	*vn_initialize(struct inode *);
extern int	vn_revalidate(struct bhv_vnode *);
extern int	__vn_revalidate(struct bhv_vnode *, bhv_vattr_t *);
extern void	vn_revalidate_core(struct bhv_vnode *, bhv_vattr_t *);

extern void	vn_iowait(struct bhv_vnode *vp);
extern void	vn_iowake(struct bhv_vnode *vp);

extern void	vn_ioerror(struct bhv_vnode *vp, int error, char *f, int l);

static inline int vn_count(struct bhv_vnode *vp)
{
	return atomic_read(&vn_to_inode(vp)->i_count);
}

/*
 * Vnode reference counting functions (and macros for compatibility).
 */
extern bhv_vnode_t	*vn_hold(struct bhv_vnode *);

#if defined(XFS_VNODE_TRACE)
#define VN_HOLD(vp)		\
	((void)vn_hold(vp),	\
	  vn_trace_hold(vp, __FILE__, __LINE__, (inst_t *)__return_address))
#define VN_RELE(vp)		\
	  (vn_trace_rele(vp, __FILE__, __LINE__, (inst_t *)__return_address), \
	   iput(vn_to_inode(vp)))
#else
#define VN_HOLD(vp)		((void)vn_hold(vp))
#define VN_RELE(vp)		(iput(vn_to_inode(vp)))
#endif

static inline struct bhv_vnode *vn_grab(struct bhv_vnode *vp)
{
	struct inode *inode = igrab(vn_to_inode(vp));
	return inode ? vn_from_inode(inode) : NULL;
}

/*
 * Vname handling macros.
 */
#define VNAME(dentry)		((char *) (dentry)->d_name.name)
#define VNAMELEN(dentry)	((dentry)->d_name.len)
#define VNAME_TO_VNODE(dentry)	(vn_from_inode((dentry)->d_inode))

/*
 * Vnode spinlock manipulation.
 */
#define VN_LOCK(vp)		mutex_spinlock(&(vp)->v_lock)
#define VN_UNLOCK(vp, s)	mutex_spinunlock(&(vp)->v_lock, s)

STATIC_INLINE void vn_flagset(struct bhv_vnode *vp, uint flag)
{
	spin_lock(&vp->v_lock);
	vp->v_flag |= flag;
	spin_unlock(&vp->v_lock);
}

STATIC_INLINE uint vn_flagclr(struct bhv_vnode *vp, uint flag)
{
	uint	cleared;

	spin_lock(&vp->v_lock);
	cleared = (vp->v_flag & flag);
	vp->v_flag &= ~flag;
	spin_unlock(&vp->v_lock);
	return cleared;
}

#define VMODIFY(vp)	vn_flagset(vp, VMODIFIED)
#define VUNMODIFY(vp)	vn_flagclr(vp, VMODIFIED)
#define VTRUNCATE(vp)	vn_flagset(vp, VTRUNCATED)
#define VUNTRUNCATE(vp)	vn_flagclr(vp, VTRUNCATED)

/*
 * Dealing with bad inodes
 */
static inline void vn_mark_bad(struct bhv_vnode *vp)
{
	make_bad_inode(vn_to_inode(vp));
}

static inline int VN_BAD(struct bhv_vnode *vp)
{
	return is_bad_inode(vn_to_inode(vp));
}

/*
 * Extracting atime values in various formats
 */
static inline void vn_atime_to_bstime(bhv_vnode_t *vp, xfs_bstime_t *bs_atime)
{
	bs_atime->tv_sec = vp->v_inode.i_atime.tv_sec;
	bs_atime->tv_nsec = vp->v_inode.i_atime.tv_nsec;
}

static inline void vn_atime_to_timespec(bhv_vnode_t *vp, struct timespec *ts)
{
	*ts = vp->v_inode.i_atime;
}

static inline void vn_atime_to_time_t(bhv_vnode_t *vp, time_t *tt)
{
	*tt = vp->v_inode.i_atime.tv_sec;
}

/*
 * Some useful predicates.
 */
#define VN_MAPPED(vp)	mapping_mapped(vn_to_inode(vp)->i_mapping)
#define VN_CACHED(vp)	(vn_to_inode(vp)->i_mapping->nrpages)
#define VN_DIRTY(vp)	mapping_tagged(vn_to_inode(vp)->i_mapping, \
					PAGECACHE_TAG_DIRTY)
#define VN_TRUNC(vp)	((vp)->v_flag & VTRUNCATED)

/*
 * Flags to vop_setattr/getattr.
 */
#define	ATTR_UTIME	0x01	/* non-default utime(2) request */
#define	ATTR_DMI	0x08	/* invocation from a DMI function */
#define	ATTR_LAZY	0x80	/* set/get attributes lazily */
#define	ATTR_NONBLOCK	0x100	/* return EAGAIN if operation would block */
#define ATTR_NOLOCK	0x200	/* Don't grab any conflicting locks */
#define ATTR_NOSIZETOK	0x400	/* Don't get the SIZE token */

/*
 * Flags to vop_fsync/reclaim.
 */
#define FSYNC_NOWAIT	0	/* asynchronous flush */
#define FSYNC_WAIT	0x1	/* synchronous fsync or forced reclaim */
#define FSYNC_INVAL	0x2	/* flush and invalidate cached data */
#define FSYNC_DATA	0x4	/* synchronous fsync of data only */

/*
 * Tracking vnode activity.
 */
#if defined(XFS_VNODE_TRACE)

#define	VNODE_TRACE_SIZE	16		/* number of trace entries */
#define	VNODE_KTRACE_ENTRY	1
#define	VNODE_KTRACE_EXIT	2
#define	VNODE_KTRACE_HOLD	3
#define	VNODE_KTRACE_REF	4
#define	VNODE_KTRACE_RELE	5

extern void vn_trace_entry(struct bhv_vnode *, const char *, inst_t *);
extern void vn_trace_exit(struct bhv_vnode *, const char *, inst_t *);
extern void vn_trace_hold(struct bhv_vnode *, char *, int, inst_t *);
extern void vn_trace_ref(struct bhv_vnode *, char *, int, inst_t *);
extern void vn_trace_rele(struct bhv_vnode *, char *, int, inst_t *);

#define	VN_TRACE(vp)		\
	vn_trace_ref(vp, __FILE__, __LINE__, (inst_t *)__return_address)
#else
#define	vn_trace_entry(a,b,c)
#define	vn_trace_exit(a,b,c)
#define	vn_trace_hold(a,b,c,d)
#define	vn_trace_ref(a,b,c,d)
#define	vn_trace_rele(a,b,c,d)
#define	VN_TRACE(vp)
#endif

#endif	/* __XFS_VNODE_H__ */
