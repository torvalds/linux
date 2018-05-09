/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#ifndef	__XFS_INODE_H__
#define	__XFS_INODE_H__

#include "xfs_inode_buf.h"
#include "xfs_inode_fork.h"

/*
 * Kernel only inode definitions
 */
struct xfs_dinode;
struct xfs_inode;
struct xfs_buf;
struct xfs_defer_ops;
struct xfs_bmbt_irec;
struct xfs_inode_log_item;
struct xfs_mount;
struct xfs_trans;
struct xfs_dquot;

typedef struct xfs_inode {
	/* Inode linking and identification information. */
	struct xfs_mount	*i_mount;	/* fs mount struct ptr */
	struct xfs_dquot	*i_udquot;	/* user dquot */
	struct xfs_dquot	*i_gdquot;	/* group dquot */
	struct xfs_dquot	*i_pdquot;	/* project dquot */

	/* Inode location stuff */
	xfs_ino_t		i_ino;		/* inode number (agno/agino)*/
	struct xfs_imap		i_imap;		/* location for xfs_imap() */

	/* Extent information. */
	xfs_ifork_t		*i_afp;		/* attribute fork pointer */
	xfs_ifork_t		*i_cowfp;	/* copy on write extents */
	xfs_ifork_t		i_df;		/* data fork */

	/* operations vectors */
	const struct xfs_dir_ops *d_ops;		/* directory ops vector */

	/* Transaction and locking information. */
	struct xfs_inode_log_item *i_itemp;	/* logging information */
	mrlock_t		i_lock;		/* inode lock */
	mrlock_t		i_mmaplock;	/* inode mmap IO lock */
	atomic_t		i_pincount;	/* inode pin count */
	spinlock_t		i_flags_lock;	/* inode i_flags lock */
	/* Miscellaneous state. */
	unsigned long		i_flags;	/* see defined flags below */
	unsigned int		i_delayed_blks;	/* count of delay alloc blks */

	struct xfs_icdinode	i_d;		/* most of ondisk inode */

	xfs_extnum_t		i_cnextents;	/* # of extents in cow fork */
	unsigned int		i_cformat;	/* format of cow fork */

	/* VFS inode */
	struct inode		i_vnode;	/* embedded VFS inode */
} xfs_inode_t;

/* Convert from vfs inode to xfs inode */
static inline struct xfs_inode *XFS_I(struct inode *inode)
{
	return container_of(inode, struct xfs_inode, i_vnode);
}

/* convert from xfs inode to vfs inode */
static inline struct inode *VFS_I(struct xfs_inode *ip)
{
	return &ip->i_vnode;
}

/*
 * For regular files we only update the on-disk filesize when actually
 * writing data back to disk.  Until then only the copy in the VFS inode
 * is uptodate.
 */
static inline xfs_fsize_t XFS_ISIZE(struct xfs_inode *ip)
{
	if (S_ISREG(VFS_I(ip)->i_mode))
		return i_size_read(VFS_I(ip));
	return ip->i_d.di_size;
}

/*
 * If this I/O goes past the on-disk inode size update it unless it would
 * be past the current in-core inode size.
 */
static inline xfs_fsize_t
xfs_new_eof(struct xfs_inode *ip, xfs_fsize_t new_size)
{
	xfs_fsize_t i_size = i_size_read(VFS_I(ip));

	if (new_size > i_size || new_size < 0)
		new_size = i_size;
	return new_size > ip->i_d.di_size ? new_size : 0;
}

/*
 * i_flags helper functions
 */
static inline void
__xfs_iflags_set(xfs_inode_t *ip, unsigned short flags)
{
	ip->i_flags |= flags;
}

static inline void
xfs_iflags_set(xfs_inode_t *ip, unsigned short flags)
{
	spin_lock(&ip->i_flags_lock);
	__xfs_iflags_set(ip, flags);
	spin_unlock(&ip->i_flags_lock);
}

static inline void
xfs_iflags_clear(xfs_inode_t *ip, unsigned short flags)
{
	spin_lock(&ip->i_flags_lock);
	ip->i_flags &= ~flags;
	spin_unlock(&ip->i_flags_lock);
}

static inline int
__xfs_iflags_test(xfs_inode_t *ip, unsigned short flags)
{
	return (ip->i_flags & flags);
}

static inline int
xfs_iflags_test(xfs_inode_t *ip, unsigned short flags)
{
	int ret;
	spin_lock(&ip->i_flags_lock);
	ret = __xfs_iflags_test(ip, flags);
	spin_unlock(&ip->i_flags_lock);
	return ret;
}

static inline int
xfs_iflags_test_and_clear(xfs_inode_t *ip, unsigned short flags)
{
	int ret;

	spin_lock(&ip->i_flags_lock);
	ret = ip->i_flags & flags;
	if (ret)
		ip->i_flags &= ~flags;
	spin_unlock(&ip->i_flags_lock);
	return ret;
}

static inline int
xfs_iflags_test_and_set(xfs_inode_t *ip, unsigned short flags)
{
	int ret;

	spin_lock(&ip->i_flags_lock);
	ret = ip->i_flags & flags;
	if (!ret)
		ip->i_flags |= flags;
	spin_unlock(&ip->i_flags_lock);
	return ret;
}

/*
 * Project quota id helpers (previously projid was 16bit only
 * and using two 16bit values to hold new 32bit projid was chosen
 * to retain compatibility with "old" filesystems).
 */
static inline prid_t
xfs_get_projid(struct xfs_inode *ip)
{
	return (prid_t)ip->i_d.di_projid_hi << 16 | ip->i_d.di_projid_lo;
}

static inline void
xfs_set_projid(struct xfs_inode *ip,
		prid_t projid)
{
	ip->i_d.di_projid_hi = (uint16_t) (projid >> 16);
	ip->i_d.di_projid_lo = (uint16_t) (projid & 0xffff);
}

static inline prid_t
xfs_get_initial_prid(struct xfs_inode *dp)
{
	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		return xfs_get_projid(dp);

	return XFS_PROJID_DEFAULT;
}

static inline bool xfs_is_reflink_inode(struct xfs_inode *ip)
{
	return ip->i_d.di_flags2 & XFS_DIFLAG2_REFLINK;
}

/*
 * In-core inode flags.
 */
#define XFS_IRECLAIM		(1 << 0) /* started reclaiming this inode */
#define XFS_ISTALE		(1 << 1) /* inode has been staled */
#define XFS_IRECLAIMABLE	(1 << 2) /* inode can be reclaimed */
#define __XFS_INEW_BIT		3	 /* inode has just been allocated */
#define XFS_INEW		(1 << __XFS_INEW_BIT)
#define XFS_ITRUNCATED		(1 << 5) /* truncated down so flush-on-close */
#define XFS_IDIRTY_RELEASE	(1 << 6) /* dirty release already seen */
#define __XFS_IFLOCK_BIT	7	 /* inode is being flushed right now */
#define XFS_IFLOCK		(1 << __XFS_IFLOCK_BIT)
#define __XFS_IPINNED_BIT	8	 /* wakeup key for zero pin count */
#define XFS_IPINNED		(1 << __XFS_IPINNED_BIT)
#define XFS_IDONTCACHE		(1 << 9) /* don't cache the inode long term */
#define XFS_IEOFBLOCKS		(1 << 10)/* has the preallocblocks tag set */
/*
 * If this unlinked inode is in the middle of recovery, don't let drop_inode
 * truncate and free the inode.  This can happen if we iget the inode during
 * log recovery to replay a bmap operation on the inode.
 */
#define XFS_IRECOVERY		(1 << 11)
#define XFS_ICOWBLOCKS		(1 << 12)/* has the cowblocks tag set */

/*
 * Per-lifetime flags need to be reset when re-using a reclaimable inode during
 * inode lookup. This prevents unintended behaviour on the new inode from
 * ocurring.
 */
#define XFS_IRECLAIM_RESET_FLAGS	\
	(XFS_IRECLAIMABLE | XFS_IRECLAIM | \
	 XFS_IDIRTY_RELEASE | XFS_ITRUNCATED)

/*
 * Synchronize processes attempting to flush the in-core inode back to disk.
 */

static inline int xfs_isiflocked(struct xfs_inode *ip)
{
	return xfs_iflags_test(ip, XFS_IFLOCK);
}

extern void __xfs_iflock(struct xfs_inode *ip);

static inline int xfs_iflock_nowait(struct xfs_inode *ip)
{
	return !xfs_iflags_test_and_set(ip, XFS_IFLOCK);
}

static inline void xfs_iflock(struct xfs_inode *ip)
{
	if (!xfs_iflock_nowait(ip))
		__xfs_iflock(ip);
}

static inline void xfs_ifunlock(struct xfs_inode *ip)
{
	ASSERT(xfs_isiflocked(ip));
	xfs_iflags_clear(ip, XFS_IFLOCK);
	smp_mb();
	wake_up_bit(&ip->i_flags, __XFS_IFLOCK_BIT);
}

/*
 * Flags for inode locking.
 * Bit ranges:	1<<1  - 1<<16-1 -- iolock/ilock modes (bitfield)
 *		1<<16 - 1<<32-1 -- lockdep annotation (integers)
 */
#define	XFS_IOLOCK_EXCL		(1<<0)
#define	XFS_IOLOCK_SHARED	(1<<1)
#define	XFS_ILOCK_EXCL		(1<<2)
#define	XFS_ILOCK_SHARED	(1<<3)
#define	XFS_MMAPLOCK_EXCL	(1<<4)
#define	XFS_MMAPLOCK_SHARED	(1<<5)

#define XFS_LOCK_MASK		(XFS_IOLOCK_EXCL | XFS_IOLOCK_SHARED \
				| XFS_ILOCK_EXCL | XFS_ILOCK_SHARED \
				| XFS_MMAPLOCK_EXCL | XFS_MMAPLOCK_SHARED)

#define XFS_LOCK_FLAGS \
	{ XFS_IOLOCK_EXCL,	"IOLOCK_EXCL" }, \
	{ XFS_IOLOCK_SHARED,	"IOLOCK_SHARED" }, \
	{ XFS_ILOCK_EXCL,	"ILOCK_EXCL" }, \
	{ XFS_ILOCK_SHARED,	"ILOCK_SHARED" }, \
	{ XFS_MMAPLOCK_EXCL,	"MMAPLOCK_EXCL" }, \
	{ XFS_MMAPLOCK_SHARED,	"MMAPLOCK_SHARED" }


/*
 * Flags for lockdep annotations.
 *
 * XFS_LOCK_PARENT - for directory operations that require locking a
 * parent directory inode and a child entry inode. IOLOCK requires nesting,
 * MMAPLOCK does not support this class, ILOCK requires a single subclass
 * to differentiate parent from child.
 *
 * XFS_LOCK_RTBITMAP/XFS_LOCK_RTSUM - the realtime device bitmap and summary
 * inodes do not participate in the normal lock order, and thus have their
 * own subclasses.
 *
 * XFS_LOCK_INUMORDER - for locking several inodes at the some time
 * with xfs_lock_inodes().  This flag is used as the starting subclass
 * and each subsequent lock acquired will increment the subclass by one.
 * However, MAX_LOCKDEP_SUBCLASSES == 8, which means we are greatly
 * limited to the subclasses we can represent via nesting. We need at least
 * 5 inodes nest depth for the ILOCK through rename, and we also have to support
 * XFS_ILOCK_PARENT, which gives 6 subclasses. Then we have XFS_ILOCK_RTBITMAP
 * and XFS_ILOCK_RTSUM, which are another 2 unique subclasses, so that's all
 * 8 subclasses supported by lockdep.
 *
 * This also means we have to number the sub-classes in the lowest bits of
 * the mask we keep, and we have to ensure we never exceed 3 bits of lockdep
 * mask and we can't use bit-masking to build the subclasses. What a mess.
 *
 * Bit layout:
 *
 * Bit		Lock Region
 * 16-19	XFS_IOLOCK_SHIFT dependencies
 * 20-23	XFS_MMAPLOCK_SHIFT dependencies
 * 24-31	XFS_ILOCK_SHIFT dependencies
 *
 * IOLOCK values
 *
 * 0-3		subclass value
 * 4-7		unused
 *
 * MMAPLOCK values
 *
 * 0-3		subclass value
 * 4-7		unused
 *
 * ILOCK values
 * 0-4		subclass values
 * 5		PARENT subclass (not nestable)
 * 6		RTBITMAP subclass (not nestable)
 * 7		RTSUM subclass (not nestable)
 * 
 */
#define XFS_IOLOCK_SHIFT		16
#define XFS_IOLOCK_MAX_SUBCLASS		3
#define XFS_IOLOCK_DEP_MASK		0x000f0000

#define XFS_MMAPLOCK_SHIFT		20
#define XFS_MMAPLOCK_NUMORDER		0
#define XFS_MMAPLOCK_MAX_SUBCLASS	3
#define XFS_MMAPLOCK_DEP_MASK		0x00f00000

#define XFS_ILOCK_SHIFT			24
#define XFS_ILOCK_PARENT_VAL		5
#define XFS_ILOCK_MAX_SUBCLASS		(XFS_ILOCK_PARENT_VAL - 1)
#define XFS_ILOCK_RTBITMAP_VAL		6
#define XFS_ILOCK_RTSUM_VAL		7
#define XFS_ILOCK_DEP_MASK		0xff000000
#define	XFS_ILOCK_PARENT		(XFS_ILOCK_PARENT_VAL << XFS_ILOCK_SHIFT)
#define	XFS_ILOCK_RTBITMAP		(XFS_ILOCK_RTBITMAP_VAL << XFS_ILOCK_SHIFT)
#define	XFS_ILOCK_RTSUM			(XFS_ILOCK_RTSUM_VAL << XFS_ILOCK_SHIFT)

#define XFS_LOCK_SUBCLASS_MASK	(XFS_IOLOCK_DEP_MASK | \
				 XFS_MMAPLOCK_DEP_MASK | \
				 XFS_ILOCK_DEP_MASK)

#define XFS_IOLOCK_DEP(flags)	(((flags) & XFS_IOLOCK_DEP_MASK) \
					>> XFS_IOLOCK_SHIFT)
#define XFS_MMAPLOCK_DEP(flags)	(((flags) & XFS_MMAPLOCK_DEP_MASK) \
					>> XFS_MMAPLOCK_SHIFT)
#define XFS_ILOCK_DEP(flags)	(((flags) & XFS_ILOCK_DEP_MASK) \
					>> XFS_ILOCK_SHIFT)

/*
 * For multiple groups support: if S_ISGID bit is set in the parent
 * directory, group of new file is set to that of the parent, and
 * new subdirectory gets S_ISGID bit from parent.
 */
#define XFS_INHERIT_GID(pip)	\
	(((pip)->i_mount->m_flags & XFS_MOUNT_GRPID) || \
	 (VFS_I(pip)->i_mode & S_ISGID))

int		xfs_release(struct xfs_inode *ip);
void		xfs_inactive(struct xfs_inode *ip);
int		xfs_lookup(struct xfs_inode *dp, struct xfs_name *name,
			   struct xfs_inode **ipp, struct xfs_name *ci_name);
int		xfs_create(struct xfs_inode *dp, struct xfs_name *name,
			   umode_t mode, dev_t rdev, struct xfs_inode **ipp);
int		xfs_create_tmpfile(struct xfs_inode *dp, umode_t mode,
			   struct xfs_inode **ipp);
int		xfs_remove(struct xfs_inode *dp, struct xfs_name *name,
			   struct xfs_inode *ip);
int		xfs_link(struct xfs_inode *tdp, struct xfs_inode *sip,
			 struct xfs_name *target_name);
int		xfs_rename(struct xfs_inode *src_dp, struct xfs_name *src_name,
			   struct xfs_inode *src_ip, struct xfs_inode *target_dp,
			   struct xfs_name *target_name,
			   struct xfs_inode *target_ip, unsigned int flags);

void		xfs_ilock(xfs_inode_t *, uint);
int		xfs_ilock_nowait(xfs_inode_t *, uint);
void		xfs_iunlock(xfs_inode_t *, uint);
void		xfs_ilock_demote(xfs_inode_t *, uint);
int		xfs_isilocked(xfs_inode_t *, uint);
uint		xfs_ilock_data_map_shared(struct xfs_inode *);
uint		xfs_ilock_attr_map_shared(struct xfs_inode *);

uint		xfs_ip2xflags(struct xfs_inode *);
int		xfs_ifree(struct xfs_trans *, xfs_inode_t *,
			   struct xfs_defer_ops *);
int		__xfs_itruncate_extents(struct xfs_trans **, struct xfs_inode *,
				        int, xfs_fsize_t, bool);
void		xfs_iext_realloc(xfs_inode_t *, int, int);

void		xfs_iunpin_wait(xfs_inode_t *);
#define xfs_ipincount(ip)	((unsigned int) atomic_read(&ip->i_pincount))

int		xfs_iflush(struct xfs_inode *, struct xfs_buf **);
void		xfs_lock_two_inodes(struct xfs_inode *ip0, uint ip0_mode,
				struct xfs_inode *ip1, uint ip1_mode);

xfs_extlen_t	xfs_get_extsz_hint(struct xfs_inode *ip);
xfs_extlen_t	xfs_get_cowextsz_hint(struct xfs_inode *ip);

int		xfs_dir_ialloc(struct xfs_trans **, struct xfs_inode *, umode_t,
			       xfs_nlink_t, dev_t, prid_t,
			       struct xfs_inode **);

static inline int
xfs_itruncate_extents(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fsize_t		new_size)
{
	return __xfs_itruncate_extents(tpp, ip, whichfork, new_size, false);
}

static inline int
xfs_itruncate_extents_nodiscard(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fsize_t		new_size)
{
	return __xfs_itruncate_extents(tpp, ip, whichfork, new_size, true);
}

/* from xfs_file.c */
enum xfs_prealloc_flags {
	XFS_PREALLOC_SET	= (1 << 1),
	XFS_PREALLOC_CLEAR	= (1 << 2),
	XFS_PREALLOC_SYNC	= (1 << 3),
	XFS_PREALLOC_INVISIBLE	= (1 << 4),
};

int	xfs_update_prealloc_flags(struct xfs_inode *ip,
				  enum xfs_prealloc_flags flags);

/* from xfs_iops.c */
extern void xfs_setup_inode(struct xfs_inode *ip);
extern void xfs_setup_iops(struct xfs_inode *ip);

/*
 * When setting up a newly allocated inode, we need to call
 * xfs_finish_inode_setup() once the inode is fully instantiated at
 * the VFS level to prevent the rest of the world seeing the inode
 * before we've completed instantiation. Otherwise we can do it
 * the moment the inode lookup is complete.
 */
static inline void xfs_finish_inode_setup(struct xfs_inode *ip)
{
	xfs_iflags_clear(ip, XFS_INEW);
	barrier();
	unlock_new_inode(VFS_I(ip));
	wake_up_bit(&ip->i_flags, __XFS_INEW_BIT);
}

static inline void xfs_setup_existing_inode(struct xfs_inode *ip)
{
	xfs_setup_inode(ip);
	xfs_setup_iops(ip);
	xfs_finish_inode_setup(ip);
}

#define IHOLD(ip) \
do { \
	ASSERT(atomic_read(&VFS_I(ip)->i_count) > 0) ; \
	ihold(VFS_I(ip)); \
	trace_xfs_ihold(ip, _THIS_IP_); \
} while (0)

#define IRELE(ip) \
do { \
	trace_xfs_irele(ip, _THIS_IP_); \
	iput(VFS_I(ip)); \
} while (0)

extern struct kmem_zone	*xfs_inode_zone;

/* The default CoW extent size hint. */
#define XFS_DEFAULT_COWEXTSZ_HINT 32

bool xfs_inode_verify_forks(struct xfs_inode *ip);

#endif	/* __XFS_INODE_H__ */
