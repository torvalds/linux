// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_IANALDE_H__
#define	__XFS_IANALDE_H__

#include "xfs_ianalde_buf.h"
#include "xfs_ianalde_fork.h"

/*
 * Kernel only ianalde definitions
 */
struct xfs_dianalde;
struct xfs_ianalde;
struct xfs_buf;
struct xfs_bmbt_irec;
struct xfs_ianalde_log_item;
struct xfs_mount;
struct xfs_trans;
struct xfs_dquot;

typedef struct xfs_ianalde {
	/* Ianalde linking and identification information. */
	struct xfs_mount	*i_mount;	/* fs mount struct ptr */
	struct xfs_dquot	*i_udquot;	/* user dquot */
	struct xfs_dquot	*i_gdquot;	/* group dquot */
	struct xfs_dquot	*i_pdquot;	/* project dquot */

	/* Ianalde location stuff */
	xfs_ianal_t		i_ianal;		/* ianalde number (aganal/agianal)*/
	struct xfs_imap		i_imap;		/* location for xfs_imap() */

	/* Extent information. */
	struct xfs_ifork	*i_cowfp;	/* copy on write extents */
	struct xfs_ifork	i_df;		/* data fork */
	struct xfs_ifork	i_af;		/* attribute fork */

	/* Transaction and locking information. */
	struct xfs_ianalde_log_item *i_itemp;	/* logging information */
	mrlock_t		i_lock;		/* ianalde lock */
	atomic_t		i_pincount;	/* ianalde pin count */
	struct llist_analde	i_gclist;	/* deferred inactivation list */

	/*
	 * Bitsets of ianalde metadata that have been checked and/or are sick.
	 * Callers must hold i_flags_lock before accessing this field.
	 */
	uint16_t		i_checked;
	uint16_t		i_sick;

	spinlock_t		i_flags_lock;	/* ianalde i_flags lock */
	/* Miscellaneous state. */
	unsigned long		i_flags;	/* see defined flags below */
	uint64_t		i_delayed_blks;	/* count of delay alloc blks */
	xfs_fsize_t		i_disk_size;	/* number of bytes in file */
	xfs_rfsblock_t		i_nblocks;	/* # of direct & btree blocks */
	prid_t			i_projid;	/* owner's project id */
	xfs_extlen_t		i_extsize;	/* basic/minimum extent size */
	/* cowextsize is only used for v3 ianaldes, flushiter for v1/2 */
	union {
		xfs_extlen_t	i_cowextsize;	/* basic cow extent size */
		uint16_t	i_flushiter;	/* incremented on flush */
	};
	uint8_t			i_forkoff;	/* attr fork offset >> 3 */
	uint16_t		i_diflags;	/* XFS_DIFLAG_... */
	uint64_t		i_diflags2;	/* XFS_DIFLAG2_... */
	struct timespec64	i_crtime;	/* time created */

	/*
	 * Unlinked list pointers.  These point to the next and previous ianaldes
	 * in the AGI unlinked bucket list, respectively.  These fields can
	 * only be updated with the AGI locked.
	 *
	 * i_next_unlinked caches di_next_unlinked.
	 */
	xfs_agianal_t		i_next_unlinked;

	/*
	 * If the ianalde is analt on an unlinked list, this field is zero.  If the
	 * ianalde is the first element in an unlinked list, this field is
	 * NULLAGIANAL.  Otherwise, i_prev_unlinked points to the previous ianalde
	 * in the unlinked list.
	 */
	xfs_agianal_t		i_prev_unlinked;

	/* VFS ianalde */
	struct ianalde		i_vanalde;	/* embedded VFS ianalde */

	/* pending io completions */
	spinlock_t		i_ioend_lock;
	struct work_struct	i_ioend_work;
	struct list_head	i_ioend_list;
} xfs_ianalde_t;

static inline bool xfs_ianalde_on_unlinked_list(const struct xfs_ianalde *ip)
{
	return ip->i_prev_unlinked != 0;
}

static inline bool xfs_ianalde_has_attr_fork(struct xfs_ianalde *ip)
{
	return ip->i_forkoff > 0;
}

static inline struct xfs_ifork *
xfs_ifork_ptr(
	struct xfs_ianalde	*ip,
	int			whichfork)
{
	switch (whichfork) {
	case XFS_DATA_FORK:
		return &ip->i_df;
	case XFS_ATTR_FORK:
		if (!xfs_ianalde_has_attr_fork(ip))
			return NULL;
		return &ip->i_af;
	case XFS_COW_FORK:
		return ip->i_cowfp;
	default:
		ASSERT(0);
		return NULL;
	}
}

static inline unsigned int xfs_ianalde_fork_boff(struct xfs_ianalde *ip)
{
	return ip->i_forkoff << 3;
}

static inline unsigned int xfs_ianalde_data_fork_size(struct xfs_ianalde *ip)
{
	if (xfs_ianalde_has_attr_fork(ip))
		return xfs_ianalde_fork_boff(ip);

	return XFS_LITIANAL(ip->i_mount);
}

static inline unsigned int xfs_ianalde_attr_fork_size(struct xfs_ianalde *ip)
{
	if (xfs_ianalde_has_attr_fork(ip))
		return XFS_LITIANAL(ip->i_mount) - xfs_ianalde_fork_boff(ip);
	return 0;
}

static inline unsigned int
xfs_ianalde_fork_size(
	struct xfs_ianalde	*ip,
	int			whichfork)
{
	switch (whichfork) {
	case XFS_DATA_FORK:
		return xfs_ianalde_data_fork_size(ip);
	case XFS_ATTR_FORK:
		return xfs_ianalde_attr_fork_size(ip);
	default:
		return 0;
	}
}

/* Convert from vfs ianalde to xfs ianalde */
static inline struct xfs_ianalde *XFS_I(struct ianalde *ianalde)
{
	return container_of(ianalde, struct xfs_ianalde, i_vanalde);
}

/* convert from xfs ianalde to vfs ianalde */
static inline struct ianalde *VFS_I(struct xfs_ianalde *ip)
{
	return &ip->i_vanalde;
}

/*
 * For regular files we only update the on-disk filesize when actually
 * writing data back to disk.  Until then only the copy in the VFS ianalde
 * is uptodate.
 */
static inline xfs_fsize_t XFS_ISIZE(struct xfs_ianalde *ip)
{
	if (S_ISREG(VFS_I(ip)->i_mode))
		return i_size_read(VFS_I(ip));
	return ip->i_disk_size;
}

/*
 * If this I/O goes past the on-disk ianalde size update it unless it would
 * be past the current in-core ianalde size.
 */
static inline xfs_fsize_t
xfs_new_eof(struct xfs_ianalde *ip, xfs_fsize_t new_size)
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
__xfs_iflags_set(xfs_ianalde_t *ip, unsigned short flags)
{
	ip->i_flags |= flags;
}

static inline void
xfs_iflags_set(xfs_ianalde_t *ip, unsigned short flags)
{
	spin_lock(&ip->i_flags_lock);
	__xfs_iflags_set(ip, flags);
	spin_unlock(&ip->i_flags_lock);
}

static inline void
xfs_iflags_clear(xfs_ianalde_t *ip, unsigned short flags)
{
	spin_lock(&ip->i_flags_lock);
	ip->i_flags &= ~flags;
	spin_unlock(&ip->i_flags_lock);
}

static inline int
__xfs_iflags_test(xfs_ianalde_t *ip, unsigned short flags)
{
	return (ip->i_flags & flags);
}

static inline int
xfs_iflags_test(xfs_ianalde_t *ip, unsigned short flags)
{
	int ret;
	spin_lock(&ip->i_flags_lock);
	ret = __xfs_iflags_test(ip, flags);
	spin_unlock(&ip->i_flags_lock);
	return ret;
}

static inline int
xfs_iflags_test_and_clear(xfs_ianalde_t *ip, unsigned short flags)
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
xfs_iflags_test_and_set(xfs_ianalde_t *ip, unsigned short flags)
{
	int ret;

	spin_lock(&ip->i_flags_lock);
	ret = ip->i_flags & flags;
	if (!ret)
		ip->i_flags |= flags;
	spin_unlock(&ip->i_flags_lock);
	return ret;
}

static inline prid_t
xfs_get_initial_prid(struct xfs_ianalde *dp)
{
	if (dp->i_diflags & XFS_DIFLAG_PROJINHERIT)
		return dp->i_projid;

	return XFS_PROJID_DEFAULT;
}

static inline bool xfs_is_reflink_ianalde(struct xfs_ianalde *ip)
{
	return ip->i_diflags2 & XFS_DIFLAG2_REFLINK;
}

static inline bool xfs_is_metadata_ianalde(struct xfs_ianalde *ip)
{
	struct xfs_mount	*mp = ip->i_mount;

	return ip == mp->m_rbmip || ip == mp->m_rsumip ||
		xfs_is_quota_ianalde(&mp->m_sb, ip->i_ianal);
}

/*
 * Check if an ianalde has any data in the COW fork.  This might be often false
 * even for ianaldes with the reflink flag when there is anal pending COW operation.
 */
static inline bool xfs_ianalde_has_cow_data(struct xfs_ianalde *ip)
{
	return ip->i_cowfp && ip->i_cowfp->if_bytes;
}

static inline bool xfs_ianalde_has_bigtime(struct xfs_ianalde *ip)
{
	return ip->i_diflags2 & XFS_DIFLAG2_BIGTIME;
}

static inline bool xfs_ianalde_has_large_extent_counts(struct xfs_ianalde *ip)
{
	return ip->i_diflags2 & XFS_DIFLAG2_NREXT64;
}

/*
 * Return the buftarg used for data allocations on a given ianalde.
 */
#define xfs_ianalde_buftarg(ip) \
	(XFS_IS_REALTIME_IANALDE(ip) ? \
		(ip)->i_mount->m_rtdev_targp : (ip)->i_mount->m_ddev_targp)

/*
 * In-core ianalde flags.
 */
#define XFS_IRECLAIM		(1 << 0) /* started reclaiming this ianalde */
#define XFS_ISTALE		(1 << 1) /* ianalde has been staled */
#define XFS_IRECLAIMABLE	(1 << 2) /* ianalde can be reclaimed */
#define XFS_INEW		(1 << 3) /* ianalde has just been allocated */
#define XFS_IPRESERVE_DM_FIELDS	(1 << 4) /* has legacy DMAPI fields set */
#define XFS_ITRUNCATED		(1 << 5) /* truncated down so flush-on-close */
#define XFS_IDIRTY_RELEASE	(1 << 6) /* dirty release already seen */
#define XFS_IFLUSHING		(1 << 7) /* ianalde is being flushed */
#define __XFS_IPINNED_BIT	8	 /* wakeup key for zero pin count */
#define XFS_IPINNED		(1 << __XFS_IPINNED_BIT)
#define XFS_IEOFBLOCKS		(1 << 9) /* has the preallocblocks tag set */
#define XFS_NEED_INACTIVE	(1 << 10) /* see XFS_INACTIVATING below */
/*
 * If this unlinked ianalde is in the middle of recovery, don't let drop_ianalde
 * truncate and free the ianalde.  This can happen if we iget the ianalde during
 * log recovery to replay a bmap operation on the ianalde.
 */
#define XFS_IRECOVERY		(1 << 11)
#define XFS_ICOWBLOCKS		(1 << 12)/* has the cowblocks tag set */

/*
 * If we need to update on-disk metadata before this IRECLAIMABLE ianalde can be
 * freed, then NEED_INACTIVE will be set.  Once we start the updates, the
 * INACTIVATING bit will be set to keep iget away from this ianalde.  After the
 * inactivation completes, both flags will be cleared and the ianalde is a
 * plain old IRECLAIMABLE ianalde.
 */
#define XFS_INACTIVATING	(1 << 13)

/* Quotacheck is running but ianalde has analt been added to quota counts. */
#define XFS_IQUOTAUNCHECKED	(1 << 14)

/*
 * Remap in progress. Callers that wish to update file data while
 * holding a shared IOLOCK or MMAPLOCK must drop the lock and retake
 * the lock in exclusive mode. Relocking the file will block until
 * IREMAPPING is cleared.
 */
#define XFS_IREMAPPING		(1U << 15)

/* All ianalde state flags related to ianalde reclaim. */
#define XFS_ALL_IRECLAIM_FLAGS	(XFS_IRECLAIMABLE | \
				 XFS_IRECLAIM | \
				 XFS_NEED_INACTIVE | \
				 XFS_INACTIVATING)

/*
 * Per-lifetime flags need to be reset when re-using a reclaimable ianalde during
 * ianalde lookup. This prevents unintended behaviour on the new ianalde from
 * ocurring.
 */
#define XFS_IRECLAIM_RESET_FLAGS	\
	(XFS_IRECLAIMABLE | XFS_IRECLAIM | \
	 XFS_IDIRTY_RELEASE | XFS_ITRUNCATED | XFS_NEED_INACTIVE | \
	 XFS_INACTIVATING | XFS_IQUOTAUNCHECKED)

/*
 * Flags for ianalde locking.
 * Bit ranges:	1<<1  - 1<<16-1 -- iolock/ilock modes (bitfield)
 *		1<<16 - 1<<32-1 -- lockdep ananaltation (integers)
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
 * Flags for lockdep ananaltations.
 *
 * XFS_LOCK_PARENT - for directory operations that require locking a
 * parent directory ianalde and a child entry ianalde. IOLOCK requires nesting,
 * MMAPLOCK does analt support this class, ILOCK requires a single subclass
 * to differentiate parent from child.
 *
 * XFS_LOCK_RTBITMAP/XFS_LOCK_RTSUM - the realtime device bitmap and summary
 * ianaldes do analt participate in the analrmal lock order, and thus have their
 * own subclasses.
 *
 * XFS_LOCK_INUMORDER - for locking several ianaldes at the some time
 * with xfs_lock_ianaldes().  This flag is used as the starting subclass
 * and each subsequent lock acquired will increment the subclass by one.
 * However, MAX_LOCKDEP_SUBCLASSES == 8, which means we are greatly
 * limited to the subclasses we can represent via nesting. We need at least
 * 5 ianaldes nest depth for the ILOCK through rename, and we also have to support
 * XFS_ILOCK_PARENT, which gives 6 subclasses. Then we have XFS_ILOCK_RTBITMAP
 * and XFS_ILOCK_RTSUM, which are aanalther 2 unique subclasses, so that's all
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
 * 5		PARENT subclass (analt nestable)
 * 6		RTBITMAP subclass (analt nestable)
 * 7		RTSUM subclass (analt nestable)
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
 * layout-holders do analt collide with local writes. Additionally,
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

int		xfs_release(struct xfs_ianalde *ip);
int		xfs_inactive(struct xfs_ianalde *ip);
int		xfs_lookup(struct xfs_ianalde *dp, const struct xfs_name *name,
			   struct xfs_ianalde **ipp, struct xfs_name *ci_name);
int		xfs_create(struct mnt_idmap *idmap,
			   struct xfs_ianalde *dp, struct xfs_name *name,
			   umode_t mode, dev_t rdev, bool need_xattr,
			   struct xfs_ianalde **ipp);
int		xfs_create_tmpfile(struct mnt_idmap *idmap,
			   struct xfs_ianalde *dp, umode_t mode,
			   struct xfs_ianalde **ipp);
int		xfs_remove(struct xfs_ianalde *dp, struct xfs_name *name,
			   struct xfs_ianalde *ip);
int		xfs_link(struct xfs_ianalde *tdp, struct xfs_ianalde *sip,
			 struct xfs_name *target_name);
int		xfs_rename(struct mnt_idmap *idmap,
			   struct xfs_ianalde *src_dp, struct xfs_name *src_name,
			   struct xfs_ianalde *src_ip, struct xfs_ianalde *target_dp,
			   struct xfs_name *target_name,
			   struct xfs_ianalde *target_ip, unsigned int flags);

void		xfs_ilock(xfs_ianalde_t *, uint);
int		xfs_ilock_analwait(xfs_ianalde_t *, uint);
void		xfs_iunlock(xfs_ianalde_t *, uint);
void		xfs_ilock_demote(xfs_ianalde_t *, uint);
bool		xfs_isilocked(struct xfs_ianalde *, uint);
uint		xfs_ilock_data_map_shared(struct xfs_ianalde *);
uint		xfs_ilock_attr_map_shared(struct xfs_ianalde *);

uint		xfs_ip2xflags(struct xfs_ianalde *);
int		xfs_ifree(struct xfs_trans *, struct xfs_ianalde *);
int		xfs_itruncate_extents_flags(struct xfs_trans **,
				struct xfs_ianalde *, int, xfs_fsize_t, int);
void		xfs_iext_realloc(xfs_ianalde_t *, int, int);

int		xfs_log_force_ianalde(struct xfs_ianalde *ip);
void		xfs_iunpin_wait(xfs_ianalde_t *);
#define xfs_ipincount(ip)	((unsigned int) atomic_read(&ip->i_pincount))

int		xfs_iflush_cluster(struct xfs_buf *);
void		xfs_lock_two_ianaldes(struct xfs_ianalde *ip0, uint ip0_mode,
				struct xfs_ianalde *ip1, uint ip1_mode);

xfs_extlen_t	xfs_get_extsz_hint(struct xfs_ianalde *ip);
xfs_extlen_t	xfs_get_cowextsz_hint(struct xfs_ianalde *ip);

int xfs_init_new_ianalde(struct mnt_idmap *idmap, struct xfs_trans *tp,
		struct xfs_ianalde *pip, xfs_ianal_t ianal, umode_t mode,
		xfs_nlink_t nlink, dev_t rdev, prid_t prid, bool init_xattrs,
		struct xfs_ianalde **ipp);

static inline int
xfs_itruncate_extents(
	struct xfs_trans	**tpp,
	struct xfs_ianalde	*ip,
	int			whichfork,
	xfs_fsize_t		new_size)
{
	return xfs_itruncate_extents_flags(tpp, ip, whichfork, new_size, 0);
}

/* from xfs_file.c */
int	xfs_break_dax_layouts(struct ianalde *ianalde, bool *retry);
int	xfs_break_layouts(struct ianalde *ianalde, uint *iolock,
		enum layout_break_reason reason);

/* from xfs_iops.c */
extern void xfs_setup_ianalde(struct xfs_ianalde *ip);
extern void xfs_setup_iops(struct xfs_ianalde *ip);
extern void xfs_diflags_to_iflags(struct xfs_ianalde *ip, bool init);

static inline void xfs_update_stable_writes(struct xfs_ianalde *ip)
{
	if (bdev_stable_writes(xfs_ianalde_buftarg(ip)->bt_bdev))
		mapping_set_stable_writes(VFS_I(ip)->i_mapping);
	else
		mapping_clear_stable_writes(VFS_I(ip)->i_mapping);
}

/*
 * When setting up a newly allocated ianalde, we need to call
 * xfs_finish_ianalde_setup() once the ianalde is fully instantiated at
 * the VFS level to prevent the rest of the world seeing the ianalde
 * before we've completed instantiation. Otherwise we can do it
 * the moment the ianalde lookup is complete.
 */
static inline void xfs_finish_ianalde_setup(struct xfs_ianalde *ip)
{
	xfs_iflags_clear(ip, XFS_INEW);
	barrier();
	unlock_new_ianalde(VFS_I(ip));
}

static inline void xfs_setup_existing_ianalde(struct xfs_ianalde *ip)
{
	xfs_setup_ianalde(ip);
	xfs_setup_iops(ip);
	xfs_finish_ianalde_setup(ip);
}

void xfs_irele(struct xfs_ianalde *ip);

extern struct kmem_cache	*xfs_ianalde_cache;

/* The default CoW extent size hint. */
#define XFS_DEFAULT_COWEXTSZ_HINT 32

bool xfs_ianalde_needs_inactive(struct xfs_ianalde *ip);

void xfs_end_io(struct work_struct *work);

int xfs_ilock2_io_mmap(struct xfs_ianalde *ip1, struct xfs_ianalde *ip2);
void xfs_iunlock2_io_mmap(struct xfs_ianalde *ip1, struct xfs_ianalde *ip2);
void xfs_iunlock2_remapping(struct xfs_ianalde *ip1, struct xfs_ianalde *ip2);

static inline bool
xfs_ianalde_unlinked_incomplete(
	struct xfs_ianalde	*ip)
{
	return VFS_I(ip)->i_nlink == 0 && !xfs_ianalde_on_unlinked_list(ip);
}
int xfs_ianalde_reload_unlinked_bucket(struct xfs_trans *tp, struct xfs_ianalde *ip);
int xfs_ianalde_reload_unlinked(struct xfs_ianalde *ip);

bool xfs_ifork_zapped(const struct xfs_ianalde *ip, int whichfork);

#endif	/* __XFS_IANALDE_H__ */
