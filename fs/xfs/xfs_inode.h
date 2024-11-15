// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_INODE_H__
#define	__XFS_INODE_H__

#include "xfs_inode_buf.h"
#include "xfs_inode_fork.h"
#include "xfs_inode_util.h"

/*
 * Kernel only inode definitions
 */
struct xfs_dinode;
struct xfs_inode;
struct xfs_buf;
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
	struct xfs_ifork	*i_cowfp;	/* copy on write extents */
	struct xfs_ifork	i_df;		/* data fork */
	struct xfs_ifork	i_af;		/* attribute fork */

	/* Transaction and locking information. */
	struct xfs_inode_log_item *i_itemp;	/* logging information */
	struct rw_semaphore	i_lock;		/* inode lock */
	atomic_t		i_pincount;	/* inode pin count */
	struct llist_node	i_gclist;	/* deferred inactivation list */

	/*
	 * Bitsets of inode metadata that have been checked and/or are sick.
	 * Callers must hold i_flags_lock before accessing this field.
	 */
	uint16_t		i_checked;
	uint16_t		i_sick;

	spinlock_t		i_flags_lock;	/* inode i_flags lock */
	/* Miscellaneous state. */
	unsigned long		i_flags;	/* see defined flags below */
	uint64_t		i_delayed_blks;	/* count of delay alloc blks */
	xfs_fsize_t		i_disk_size;	/* number of bytes in file */
	xfs_rfsblock_t		i_nblocks;	/* # of direct & btree blocks */
	prid_t			i_projid;	/* owner's project id */
	xfs_extlen_t		i_extsize;	/* basic/minimum extent size */
	/* cowextsize is only used for v3 inodes, flushiter for v1/2 */
	union {
		xfs_extlen_t	i_cowextsize;	/* basic cow extent size */
		uint16_t	i_flushiter;	/* incremented on flush */
	};
	uint8_t			i_forkoff;	/* attr fork offset >> 3 */
	uint16_t		i_diflags;	/* XFS_DIFLAG_... */
	uint64_t		i_diflags2;	/* XFS_DIFLAG2_... */
	struct timespec64	i_crtime;	/* time created */

	/*
	 * Unlinked list pointers.  These point to the next and previous inodes
	 * in the AGI unlinked bucket list, respectively.  These fields can
	 * only be updated with the AGI locked.
	 *
	 * i_next_unlinked caches di_next_unlinked.
	 */
	xfs_agino_t		i_next_unlinked;

	/*
	 * If the inode is not on an unlinked list, this field is zero.  If the
	 * inode is the first element in an unlinked list, this field is
	 * NULLAGINO.  Otherwise, i_prev_unlinked points to the previous inode
	 * in the unlinked list.
	 */
	xfs_agino_t		i_prev_unlinked;

	/* VFS inode */
	struct inode		i_vnode;	/* embedded VFS inode */

	/* pending io completions */
	spinlock_t		i_ioend_lock;
	struct work_struct	i_ioend_work;
	struct list_head	i_ioend_list;
} xfs_inode_t;

static inline bool xfs_inode_on_unlinked_list(const struct xfs_inode *ip)
{
	return ip->i_prev_unlinked != 0;
}

static inline bool xfs_inode_has_attr_fork(struct xfs_inode *ip)
{
	return ip->i_forkoff > 0;
}

static inline struct xfs_ifork *
xfs_ifork_ptr(
	struct xfs_inode	*ip,
	int			whichfork)
{
	switch (whichfork) {
	case XFS_DATA_FORK:
		return &ip->i_df;
	case XFS_ATTR_FORK:
		if (!xfs_inode_has_attr_fork(ip))
			return NULL;
		return &ip->i_af;
	case XFS_COW_FORK:
		return ip->i_cowfp;
	default:
		ASSERT(0);
		return NULL;
	}
}

static inline unsigned int xfs_inode_fork_boff(struct xfs_inode *ip)
{
	return ip->i_forkoff << 3;
}

static inline unsigned int xfs_inode_data_fork_size(struct xfs_inode *ip)
{
	if (xfs_inode_has_attr_fork(ip))
		return xfs_inode_fork_boff(ip);

	return XFS_LITINO(ip->i_mount);
}

static inline unsigned int xfs_inode_attr_fork_size(struct xfs_inode *ip)
{
	if (xfs_inode_has_attr_fork(ip))
		return XFS_LITINO(ip->i_mount) - xfs_inode_fork_boff(ip);
	return 0;
}

static inline unsigned int
xfs_inode_fork_size(
	struct xfs_inode	*ip,
	int			whichfork)
{
	switch (whichfork) {
	case XFS_DATA_FORK:
		return xfs_inode_data_fork_size(ip);
	case XFS_ATTR_FORK:
		return xfs_inode_attr_fork_size(ip);
	default:
		return 0;
	}
}

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

/* convert from const xfs inode to const vfs inode */
static inline const struct inode *VFS_IC(const struct xfs_inode *ip)
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
	return ip->i_disk_size;
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
	return new_size > ip->i_disk_size ? new_size : 0;
}

/*
 * i_flags helper functions
 */
static inline void
__xfs_iflags_set(xfs_inode_t *ip, unsigned long flags)
{
	ip->i_flags |= flags;
}

static inline void
xfs_iflags_set(xfs_inode_t *ip, unsigned long flags)
{
	spin_lock(&ip->i_flags_lock);
	__xfs_iflags_set(ip, flags);
	spin_unlock(&ip->i_flags_lock);
}

static inline void
xfs_iflags_clear(xfs_inode_t *ip, unsigned long flags)
{
	spin_lock(&ip->i_flags_lock);
	ip->i_flags &= ~flags;
	spin_unlock(&ip->i_flags_lock);
}

static inline int
__xfs_iflags_test(xfs_inode_t *ip, unsigned long flags)
{
	return (ip->i_flags & flags);
}

static inline int
xfs_iflags_test(xfs_inode_t *ip, unsigned long flags)
{
	int ret;
	spin_lock(&ip->i_flags_lock);
	ret = __xfs_iflags_test(ip, flags);
	spin_unlock(&ip->i_flags_lock);
	return ret;
}

static inline int
xfs_iflags_test_and_clear(xfs_inode_t *ip, unsigned long flags)
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
xfs_iflags_test_and_set(xfs_inode_t *ip, unsigned long flags)
{
	int ret;

	spin_lock(&ip->i_flags_lock);
	ret = ip->i_flags & flags;
	if (!ret)
		ip->i_flags |= flags;
	spin_unlock(&ip->i_flags_lock);
	return ret;
}

static inline bool xfs_is_reflink_inode(struct xfs_inode *ip)
{
	return ip->i_diflags2 & XFS_DIFLAG2_REFLINK;
}

static inline bool xfs_is_metadata_inode(const struct xfs_inode *ip)
{
	struct xfs_mount	*mp = ip->i_mount;

	return ip->i_ino == mp->m_sb.sb_rbmino ||
	       ip->i_ino == mp->m_sb.sb_rsumino ||
	       xfs_is_quota_inode(&mp->m_sb, ip->i_ino);
}

bool xfs_is_always_cow_inode(struct xfs_inode *ip);

static inline bool xfs_is_cow_inode(struct xfs_inode *ip)
{
	return xfs_is_reflink_inode(ip) || xfs_is_always_cow_inode(ip);
}

static inline bool xfs_inode_has_filedata(const struct xfs_inode *ip)
{
	return ip->i_df.if_nextents > 0 || ip->i_delayed_blks > 0;
}

/*
 * Check if an inode has any data in the COW fork.  This might be often false
 * even for inodes with the reflink flag when there is no pending COW operation.
 */
static inline bool xfs_inode_has_cow_data(struct xfs_inode *ip)
{
	return ip->i_cowfp && ip->i_cowfp->if_bytes;
}

static inline bool xfs_inode_has_bigtime(struct xfs_inode *ip)
{
	return ip->i_diflags2 & XFS_DIFLAG2_BIGTIME;
}

static inline bool xfs_inode_has_large_extent_counts(struct xfs_inode *ip)
{
	return ip->i_diflags2 & XFS_DIFLAG2_NREXT64;
}

/*
 * Decide if this file is a realtime file whose data allocation unit is larger
 * than a single filesystem block.
 */
static inline bool xfs_inode_has_bigrtalloc(struct xfs_inode *ip)
{
	return XFS_IS_REALTIME_INODE(ip) && ip->i_mount->m_sb.sb_rextsize > 1;
}

/*
 * Return the buftarg used for data allocations on a given inode.
 */
#define xfs_inode_buftarg(ip) \
	(XFS_IS_REALTIME_INODE(ip) ? \
		(ip)->i_mount->m_rtdev_targp : (ip)->i_mount->m_ddev_targp)

/*
 * In-core inode flags.
 */
#define XFS_IRECLAIM		(1 << 0) /* started reclaiming this inode */
#define XFS_ISTALE		(1 << 1) /* inode has been staled */
#define XFS_IRECLAIMABLE	(1 << 2) /* inode can be reclaimed */
#define XFS_INEW		(1 << 3) /* inode has just been allocated */
#define XFS_IPRESERVE_DM_FIELDS	(1 << 4) /* has legacy DMAPI fields set */
#define XFS_ITRUNCATED		(1 << 5) /* truncated down so flush-on-close */
#define XFS_EOFBLOCKS_RELEASED	(1 << 6) /* eofblocks were freed in ->release */
#define XFS_IFLUSHING		(1 << 7) /* inode is being flushed */
#define __XFS_IPINNED_BIT	8	 /* wakeup key for zero pin count */
#define XFS_IPINNED		(1 << __XFS_IPINNED_BIT)
#define XFS_IEOFBLOCKS		(1 << 9) /* has the preallocblocks tag set */
#define XFS_NEED_INACTIVE	(1 << 10) /* see XFS_INACTIVATING below */
/*
 * If this unlinked inode is in the middle of recovery, don't let drop_inode
 * truncate and free the inode.  This can happen if we iget the inode during
 * log recovery to replay a bmap operation on the inode.
 */
#define XFS_IRECOVERY		(1 << 11)
#define XFS_ICOWBLOCKS		(1 << 12)/* has the cowblocks tag set */

/*
 * If we need to update on-disk metadata before this IRECLAIMABLE inode can be
 * freed, then NEED_INACTIVE will be set.  Once we start the updates, the
 * INACTIVATING bit will be set to keep iget away from this inode.  After the
 * inactivation completes, both flags will be cleared and the inode is a
 * plain old IRECLAIMABLE inode.
 */
#define XFS_INACTIVATING	(1 << 13)

/* Quotacheck is running but inode has not been added to quota counts. */
#define XFS_IQUOTAUNCHECKED	(1 << 14)

/*
 * Remap in progress. Callers that wish to update file data while
 * holding a shared IOLOCK or MMAPLOCK must drop the lock and retake
 * the lock in exclusive mode. Relocking the file will block until
 * IREMAPPING is cleared.
 */
#define XFS_IREMAPPING		(1U << 15)

/* All inode state flags related to inode reclaim. */
#define XFS_ALL_IRECLAIM_FLAGS	(XFS_IRECLAIMABLE | \
				 XFS_IRECLAIM | \
				 XFS_NEED_INACTIVE | \
				 XFS_INACTIVATING)

/*
 * Per-lifetime flags need to be reset when re-using a reclaimable inode during
 * inode lookup. This prevents unintended behaviour on the new inode from
 * ocurring.
 */
#define XFS_IRECLAIM_RESET_FLAGS	\
	(XFS_IRECLAIMABLE | XFS_IRECLAIM | \
	 XFS_EOFBLOCKS_RELEASED | XFS_ITRUNCATED | XFS_NEED_INACTIVE | \
	 XFS_INACTIVATING | XFS_IQUOTAUNCHECKED)

/*
 * Flags for inode locking.
 * Bit ranges:	1<<1  - 1<<16-1 -- iolock/ilock modes (bitfield)
 *		1<<16 - 1<<32-1 -- lockdep annotation (integers)
 */
#define	XFS_IOLOCK_EXCL		(1u << 0)
#define	XFS_IOLOCK_SHARED	(1u << 1)
#define	XFS_ILOCK_EXCL		(1u << 2)
#define	XFS_ILOCK_SHARED	(1u << 3)
#define	XFS_MMAPLOCK_EXCL	(1u << 4)
#define	XFS_MMAPLOCK_SHARED	(1u << 5)

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
#define XFS_IOLOCK_DEP_MASK		0x000f0000u

#define XFS_MMAPLOCK_SHIFT		20
#define XFS_MMAPLOCK_NUMORDER		0
#define XFS_MMAPLOCK_MAX_SUBCLASS	3
#define XFS_MMAPLOCK_DEP_MASK		0x00f00000u

#define XFS_ILOCK_SHIFT			24
#define XFS_ILOCK_PARENT_VAL		5u
#define XFS_ILOCK_MAX_SUBCLASS		(XFS_ILOCK_PARENT_VAL - 1)
#define XFS_ILOCK_RTBITMAP_VAL		6u
#define XFS_ILOCK_RTSUM_VAL		7u
#define XFS_ILOCK_DEP_MASK		0xff000000u
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
 * Layouts are broken in the BREAK_WRITE case to ensure that
 * layout-holders do not collide with local writes. Additionally,
 * layouts are broken in the BREAK_UNMAP case to make sure the
 * layout-holder has a consistent view of the file's extent map. While
 * BREAK_WRITE breaks can be satisfied by recalling FL_LAYOUT leases,
 * BREAK_UNMAP breaks additionally require waiting for busy dax-pages to
 * go idle.
 */
enum layout_break_reason {
        BREAK_WRITE,
        BREAK_UNMAP,
};

/*
 * For multiple groups support: if S_ISGID bit is set in the parent
 * directory, group of new file is set to that of the parent, and
 * new subdirectory gets S_ISGID bit from parent.
 */
#define XFS_INHERIT_GID(pip)	\
	(xfs_has_grpid((pip)->i_mount) || (VFS_I(pip)->i_mode & S_ISGID))

int		xfs_inactive(struct xfs_inode *ip);
int		xfs_lookup(struct xfs_inode *dp, const struct xfs_name *name,
			   struct xfs_inode **ipp, struct xfs_name *ci_name);
int		xfs_create(const struct xfs_icreate_args *iargs,
			   struct xfs_name *name, struct xfs_inode **ipp);
int		xfs_create_tmpfile(const struct xfs_icreate_args *iargs,
			   struct xfs_inode **ipp);
int		xfs_remove(struct xfs_inode *dp, struct xfs_name *name,
			   struct xfs_inode *ip);
int		xfs_link(struct xfs_inode *tdp, struct xfs_inode *sip,
			 struct xfs_name *target_name);
int		xfs_rename(struct mnt_idmap *idmap,
			   struct xfs_inode *src_dp, struct xfs_name *src_name,
			   struct xfs_inode *src_ip, struct xfs_inode *target_dp,
			   struct xfs_name *target_name,
			   struct xfs_inode *target_ip, unsigned int flags);

void		xfs_ilock(xfs_inode_t *, uint);
int		xfs_ilock_nowait(xfs_inode_t *, uint);
void		xfs_iunlock(xfs_inode_t *, uint);
void		xfs_ilock_demote(xfs_inode_t *, uint);
void		xfs_assert_ilocked(struct xfs_inode *, uint);
uint		xfs_ilock_data_map_shared(struct xfs_inode *);
uint		xfs_ilock_attr_map_shared(struct xfs_inode *);

int		xfs_ifree(struct xfs_trans *, struct xfs_inode *);
int		xfs_itruncate_extents_flags(struct xfs_trans **,
				struct xfs_inode *, int, xfs_fsize_t, int);
void		xfs_iext_realloc(xfs_inode_t *, int, int);

int		xfs_log_force_inode(struct xfs_inode *ip);
void		xfs_iunpin_wait(xfs_inode_t *);
#define xfs_ipincount(ip)	((unsigned int) atomic_read(&ip->i_pincount))

int		xfs_iflush_cluster(struct xfs_buf *);
void		xfs_lock_two_inodes(struct xfs_inode *ip0, uint ip0_mode,
				struct xfs_inode *ip1, uint ip1_mode);

int xfs_icreate(struct xfs_trans *tp, xfs_ino_t ino,
		const struct xfs_icreate_args *args, struct xfs_inode **ipp);

static inline int
xfs_itruncate_extents(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fsize_t		new_size)
{
	return xfs_itruncate_extents_flags(tpp, ip, whichfork, new_size, 0);
}

int	xfs_break_dax_layouts(struct inode *inode, bool *retry);
int	xfs_break_layouts(struct inode *inode, uint *iolock,
		enum layout_break_reason reason);

static inline void xfs_update_stable_writes(struct xfs_inode *ip)
{
	if (bdev_stable_writes(xfs_inode_buftarg(ip)->bt_bdev))
		mapping_set_stable_writes(VFS_I(ip)->i_mapping);
	else
		mapping_clear_stable_writes(VFS_I(ip)->i_mapping);
}

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
}

static inline void xfs_setup_existing_inode(struct xfs_inode *ip)
{
	xfs_setup_inode(ip);
	xfs_setup_iops(ip);
	xfs_finish_inode_setup(ip);
}

void xfs_irele(struct xfs_inode *ip);

extern struct kmem_cache	*xfs_inode_cache;

/* The default CoW extent size hint. */
#define XFS_DEFAULT_COWEXTSZ_HINT 32

bool xfs_inode_needs_inactive(struct xfs_inode *ip);

struct xfs_inode *xfs_iunlink_lookup(struct xfs_perag *pag, xfs_agino_t agino);
int xfs_iunlink_reload_next(struct xfs_trans *tp, struct xfs_buf *agibp,
		xfs_agino_t prev_agino, xfs_agino_t next_agino);

void xfs_end_io(struct work_struct *work);

int xfs_ilock2_io_mmap(struct xfs_inode *ip1, struct xfs_inode *ip2);
void xfs_iunlock2_io_mmap(struct xfs_inode *ip1, struct xfs_inode *ip2);
void xfs_iunlock2_remapping(struct xfs_inode *ip1, struct xfs_inode *ip2);
void xfs_lock_inodes(struct xfs_inode **ips, int inodes, uint lock_mode);
void xfs_sort_inodes(struct xfs_inode **i_tab, unsigned int num_inodes);

static inline bool
xfs_inode_unlinked_incomplete(
	struct xfs_inode	*ip)
{
	return VFS_I(ip)->i_nlink == 0 && !xfs_inode_on_unlinked_list(ip);
}
int xfs_inode_reload_unlinked_bucket(struct xfs_trans *tp, struct xfs_inode *ip);
int xfs_inode_reload_unlinked(struct xfs_inode *ip);

bool xfs_ifork_zapped(const struct xfs_inode *ip, int whichfork);
void xfs_inode_count_blocks(struct xfs_trans *tp, struct xfs_inode *ip,
		xfs_filblks_t *dblocks, xfs_filblks_t *rblocks);
unsigned int xfs_inode_alloc_unitsize(struct xfs_inode *ip);

int xfs_icreate_dqalloc(const struct xfs_icreate_args *args,
		struct xfs_dquot **udqpp, struct xfs_dquot **gdqpp,
		struct xfs_dquot **pdqpp);

#endif	/* __XFS_INODE_H__ */
