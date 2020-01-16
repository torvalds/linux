// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_INODE_H__
#define	__XFS_INODE_H__

#include "xfs_iyesde_buf.h"
#include "xfs_iyesde_fork.h"

/*
 * Kernel only iyesde definitions
 */
struct xfs_diyesde;
struct xfs_iyesde;
struct xfs_buf;
struct xfs_bmbt_irec;
struct xfs_iyesde_log_item;
struct xfs_mount;
struct xfs_trans;
struct xfs_dquot;

typedef struct xfs_iyesde {
	/* Iyesde linking and identification information. */
	struct xfs_mount	*i_mount;	/* fs mount struct ptr */
	struct xfs_dquot	*i_udquot;	/* user dquot */
	struct xfs_dquot	*i_gdquot;	/* group dquot */
	struct xfs_dquot	*i_pdquot;	/* project dquot */

	/* Iyesde location stuff */
	xfs_iyes_t		i_iyes;		/* iyesde number (agyes/agiyes)*/
	struct xfs_imap		i_imap;		/* location for xfs_imap() */

	/* Extent information. */
	struct xfs_ifork	*i_afp;		/* attribute fork pointer */
	struct xfs_ifork	*i_cowfp;	/* copy on write extents */
	struct xfs_ifork	i_df;		/* data fork */

	/* Transaction and locking information. */
	struct xfs_iyesde_log_item *i_itemp;	/* logging information */
	mrlock_t		i_lock;		/* iyesde lock */
	mrlock_t		i_mmaplock;	/* iyesde mmap IO lock */
	atomic_t		i_pincount;	/* iyesde pin count */

	/*
	 * Bitsets of iyesde metadata that have been checked and/or are sick.
	 * Callers must hold i_flags_lock before accessing this field.
	 */
	uint16_t		i_checked;
	uint16_t		i_sick;

	spinlock_t		i_flags_lock;	/* iyesde i_flags lock */
	/* Miscellaneous state. */
	unsigned long		i_flags;	/* see defined flags below */
	uint64_t		i_delayed_blks;	/* count of delay alloc blks */

	struct xfs_icdiyesde	i_d;		/* most of ondisk iyesde */

	xfs_extnum_t		i_cnextents;	/* # of extents in cow fork */
	unsigned int		i_cformat;	/* format of cow fork */

	/* VFS iyesde */
	struct iyesde		i_vyesde;	/* embedded VFS iyesde */

	/* pending io completions */
	spinlock_t		i_ioend_lock;
	struct work_struct	i_ioend_work;
	struct list_head	i_ioend_list;
} xfs_iyesde_t;

/* Convert from vfs iyesde to xfs iyesde */
static inline struct xfs_iyesde *XFS_I(struct iyesde *iyesde)
{
	return container_of(iyesde, struct xfs_iyesde, i_vyesde);
}

/* convert from xfs iyesde to vfs iyesde */
static inline struct iyesde *VFS_I(struct xfs_iyesde *ip)
{
	return &ip->i_vyesde;
}

/*
 * For regular files we only update the on-disk filesize when actually
 * writing data back to disk.  Until then only the copy in the VFS iyesde
 * is uptodate.
 */
static inline xfs_fsize_t XFS_ISIZE(struct xfs_iyesde *ip)
{
	if (S_ISREG(VFS_I(ip)->i_mode))
		return i_size_read(VFS_I(ip));
	return ip->i_d.di_size;
}

/*
 * If this I/O goes past the on-disk iyesde size update it unless it would
 * be past the current in-core iyesde size.
 */
static inline xfs_fsize_t
xfs_new_eof(struct xfs_iyesde *ip, xfs_fsize_t new_size)
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
__xfs_iflags_set(xfs_iyesde_t *ip, unsigned short flags)
{
	ip->i_flags |= flags;
}

static inline void
xfs_iflags_set(xfs_iyesde_t *ip, unsigned short flags)
{
	spin_lock(&ip->i_flags_lock);
	__xfs_iflags_set(ip, flags);
	spin_unlock(&ip->i_flags_lock);
}

static inline void
xfs_iflags_clear(xfs_iyesde_t *ip, unsigned short flags)
{
	spin_lock(&ip->i_flags_lock);
	ip->i_flags &= ~flags;
	spin_unlock(&ip->i_flags_lock);
}

static inline int
__xfs_iflags_test(xfs_iyesde_t *ip, unsigned short flags)
{
	return (ip->i_flags & flags);
}

static inline int
xfs_iflags_test(xfs_iyesde_t *ip, unsigned short flags)
{
	int ret;
	spin_lock(&ip->i_flags_lock);
	ret = __xfs_iflags_test(ip, flags);
	spin_unlock(&ip->i_flags_lock);
	return ret;
}

static inline int
xfs_iflags_test_and_clear(xfs_iyesde_t *ip, unsigned short flags)
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
xfs_iflags_test_and_set(xfs_iyesde_t *ip, unsigned short flags)
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
xfs_get_initial_prid(struct xfs_iyesde *dp)
{
	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		return dp->i_d.di_projid;

	return XFS_PROJID_DEFAULT;
}

static inline bool xfs_is_reflink_iyesde(struct xfs_iyesde *ip)
{
	return ip->i_d.di_flags2 & XFS_DIFLAG2_REFLINK;
}

/*
 * Check if an iyesde has any data in the COW fork.  This might be often false
 * even for iyesdes with the reflink flag when there is yes pending COW operation.
 */
static inline bool xfs_iyesde_has_cow_data(struct xfs_iyesde *ip)
{
	return ip->i_cowfp && ip->i_cowfp->if_bytes;
}

/*
 * Return the buftarg used for data allocations on a given iyesde.
 */
#define xfs_iyesde_buftarg(ip) \
	(XFS_IS_REALTIME_INODE(ip) ? \
		(ip)->i_mount->m_rtdev_targp : (ip)->i_mount->m_ddev_targp)

/*
 * In-core iyesde flags.
 */
#define XFS_IRECLAIM		(1 << 0) /* started reclaiming this iyesde */
#define XFS_ISTALE		(1 << 1) /* iyesde has been staled */
#define XFS_IRECLAIMABLE	(1 << 2) /* iyesde can be reclaimed */
#define __XFS_INEW_BIT		3	 /* iyesde has just been allocated */
#define XFS_INEW		(1 << __XFS_INEW_BIT)
#define XFS_ITRUNCATED		(1 << 5) /* truncated down so flush-on-close */
#define XFS_IDIRTY_RELEASE	(1 << 6) /* dirty release already seen */
#define __XFS_IFLOCK_BIT	7	 /* iyesde is being flushed right yesw */
#define XFS_IFLOCK		(1 << __XFS_IFLOCK_BIT)
#define __XFS_IPINNED_BIT	8	 /* wakeup key for zero pin count */
#define XFS_IPINNED		(1 << __XFS_IPINNED_BIT)
#define XFS_IDONTCACHE		(1 << 9) /* don't cache the iyesde long term */
#define XFS_IEOFBLOCKS		(1 << 10)/* has the preallocblocks tag set */
/*
 * If this unlinked iyesde is in the middle of recovery, don't let drop_iyesde
 * truncate and free the iyesde.  This can happen if we iget the iyesde during
 * log recovery to replay a bmap operation on the iyesde.
 */
#define XFS_IRECOVERY		(1 << 11)
#define XFS_ICOWBLOCKS		(1 << 12)/* has the cowblocks tag set */

/*
 * Per-lifetime flags need to be reset when re-using a reclaimable iyesde during
 * iyesde lookup. This prevents unintended behaviour on the new iyesde from
 * ocurring.
 */
#define XFS_IRECLAIM_RESET_FLAGS	\
	(XFS_IRECLAIMABLE | XFS_IRECLAIM | \
	 XFS_IDIRTY_RELEASE | XFS_ITRUNCATED)

/*
 * Synchronize processes attempting to flush the in-core iyesde back to disk.
 */

static inline int xfs_isiflocked(struct xfs_iyesde *ip)
{
	return xfs_iflags_test(ip, XFS_IFLOCK);
}

extern void __xfs_iflock(struct xfs_iyesde *ip);

static inline int xfs_iflock_yeswait(struct xfs_iyesde *ip)
{
	return !xfs_iflags_test_and_set(ip, XFS_IFLOCK);
}

static inline void xfs_iflock(struct xfs_iyesde *ip)
{
	if (!xfs_iflock_yeswait(ip))
		__xfs_iflock(ip);
}

static inline void xfs_ifunlock(struct xfs_iyesde *ip)
{
	ASSERT(xfs_isiflocked(ip));
	xfs_iflags_clear(ip, XFS_IFLOCK);
	smp_mb();
	wake_up_bit(&ip->i_flags, __XFS_IFLOCK_BIT);
}

/*
 * Flags for iyesde locking.
 * Bit ranges:	1<<1  - 1<<16-1 -- iolock/ilock modes (bitfield)
 *		1<<16 - 1<<32-1 -- lockdep anyestation (integers)
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
 * Flags for lockdep anyestations.
 *
 * XFS_LOCK_PARENT - for directory operations that require locking a
 * parent directory iyesde and a child entry iyesde. IOLOCK requires nesting,
 * MMAPLOCK does yest support this class, ILOCK requires a single subclass
 * to differentiate parent from child.
 *
 * XFS_LOCK_RTBITMAP/XFS_LOCK_RTSUM - the realtime device bitmap and summary
 * iyesdes do yest participate in the yesrmal lock order, and thus have their
 * own subclasses.
 *
 * XFS_LOCK_INUMORDER - for locking several iyesdes at the some time
 * with xfs_lock_iyesdes().  This flag is used as the starting subclass
 * and each subsequent lock acquired will increment the subclass by one.
 * However, MAX_LOCKDEP_SUBCLASSES == 8, which means we are greatly
 * limited to the subclasses we can represent via nesting. We need at least
 * 5 iyesdes nest depth for the ILOCK through rename, and we also have to support
 * XFS_ILOCK_PARENT, which gives 6 subclasses. Then we have XFS_ILOCK_RTBITMAP
 * and XFS_ILOCK_RTSUM, which are ayesther 2 unique subclasses, so that's all
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
 * 5		PARENT subclass (yest nestable)
 * 6		RTBITMAP subclass (yest nestable)
 * 7		RTSUM subclass (yest nestable)
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
 * Layouts are broken in the BREAK_WRITE case to ensure that
 * layout-holders do yest collide with local writes. Additionally,
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
	(((pip)->i_mount->m_flags & XFS_MOUNT_GRPID) || \
	 (VFS_I(pip)->i_mode & S_ISGID))

int		xfs_release(struct xfs_iyesde *ip);
void		xfs_inactive(struct xfs_iyesde *ip);
int		xfs_lookup(struct xfs_iyesde *dp, struct xfs_name *name,
			   struct xfs_iyesde **ipp, struct xfs_name *ci_name);
int		xfs_create(struct xfs_iyesde *dp, struct xfs_name *name,
			   umode_t mode, dev_t rdev, struct xfs_iyesde **ipp);
int		xfs_create_tmpfile(struct xfs_iyesde *dp, umode_t mode,
			   struct xfs_iyesde **ipp);
int		xfs_remove(struct xfs_iyesde *dp, struct xfs_name *name,
			   struct xfs_iyesde *ip);
int		xfs_link(struct xfs_iyesde *tdp, struct xfs_iyesde *sip,
			 struct xfs_name *target_name);
int		xfs_rename(struct xfs_iyesde *src_dp, struct xfs_name *src_name,
			   struct xfs_iyesde *src_ip, struct xfs_iyesde *target_dp,
			   struct xfs_name *target_name,
			   struct xfs_iyesde *target_ip, unsigned int flags);

void		xfs_ilock(xfs_iyesde_t *, uint);
int		xfs_ilock_yeswait(xfs_iyesde_t *, uint);
void		xfs_iunlock(xfs_iyesde_t *, uint);
void		xfs_ilock_demote(xfs_iyesde_t *, uint);
int		xfs_isilocked(xfs_iyesde_t *, uint);
uint		xfs_ilock_data_map_shared(struct xfs_iyesde *);
uint		xfs_ilock_attr_map_shared(struct xfs_iyesde *);

uint		xfs_ip2xflags(struct xfs_iyesde *);
int		xfs_ifree(struct xfs_trans *, struct xfs_iyesde *);
int		xfs_itruncate_extents_flags(struct xfs_trans **,
				struct xfs_iyesde *, int, xfs_fsize_t, int);
void		xfs_iext_realloc(xfs_iyesde_t *, int, int);

void		xfs_iunpin_wait(xfs_iyesde_t *);
#define xfs_ipincount(ip)	((unsigned int) atomic_read(&ip->i_pincount))

int		xfs_iflush(struct xfs_iyesde *, struct xfs_buf **);
void		xfs_lock_two_iyesdes(struct xfs_iyesde *ip0, uint ip0_mode,
				struct xfs_iyesde *ip1, uint ip1_mode);

xfs_extlen_t	xfs_get_extsz_hint(struct xfs_iyesde *ip);
xfs_extlen_t	xfs_get_cowextsz_hint(struct xfs_iyesde *ip);

int		xfs_dir_ialloc(struct xfs_trans **, struct xfs_iyesde *, umode_t,
			       xfs_nlink_t, dev_t, prid_t,
			       struct xfs_iyesde **);

static inline int
xfs_itruncate_extents(
	struct xfs_trans	**tpp,
	struct xfs_iyesde	*ip,
	int			whichfork,
	xfs_fsize_t		new_size)
{
	return xfs_itruncate_extents_flags(tpp, ip, whichfork, new_size, 0);
}

/* from xfs_file.c */
enum xfs_prealloc_flags {
	XFS_PREALLOC_SET	= (1 << 1),
	XFS_PREALLOC_CLEAR	= (1 << 2),
	XFS_PREALLOC_SYNC	= (1 << 3),
	XFS_PREALLOC_INVISIBLE	= (1 << 4),
};

int	xfs_update_prealloc_flags(struct xfs_iyesde *ip,
				  enum xfs_prealloc_flags flags);
int	xfs_break_layouts(struct iyesde *iyesde, uint *iolock,
		enum layout_break_reason reason);

/* from xfs_iops.c */
extern void xfs_setup_iyesde(struct xfs_iyesde *ip);
extern void xfs_setup_iops(struct xfs_iyesde *ip);

/*
 * When setting up a newly allocated iyesde, we need to call
 * xfs_finish_iyesde_setup() once the iyesde is fully instantiated at
 * the VFS level to prevent the rest of the world seeing the iyesde
 * before we've completed instantiation. Otherwise we can do it
 * the moment the iyesde lookup is complete.
 */
static inline void xfs_finish_iyesde_setup(struct xfs_iyesde *ip)
{
	xfs_iflags_clear(ip, XFS_INEW);
	barrier();
	unlock_new_iyesde(VFS_I(ip));
	wake_up_bit(&ip->i_flags, __XFS_INEW_BIT);
}

static inline void xfs_setup_existing_iyesde(struct xfs_iyesde *ip)
{
	xfs_setup_iyesde(ip);
	xfs_setup_iops(ip);
	xfs_finish_iyesde_setup(ip);
}

void xfs_irele(struct xfs_iyesde *ip);

extern struct kmem_zone	*xfs_iyesde_zone;

/* The default CoW extent size hint. */
#define XFS_DEFAULT_COWEXTSZ_HINT 32

bool xfs_iyesde_verify_forks(struct xfs_iyesde *ip);

int xfs_iunlink_init(struct xfs_perag *pag);
void xfs_iunlink_destroy(struct xfs_perag *pag);

void xfs_end_io(struct work_struct *work);

#endif	/* __XFS_INODE_H__ */
