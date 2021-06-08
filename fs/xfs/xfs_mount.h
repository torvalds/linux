// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_MOUNT_H__
#define	__XFS_MOUNT_H__

struct xlog;
struct xfs_inode;
struct xfs_mru_cache;
struct xfs_ail;
struct xfs_quotainfo;
struct xfs_da_geometry;
struct xfs_perag;

/* dynamic preallocation free space thresholds, 5% down to 1% */
enum {
	XFS_LOWSP_1_PCNT = 0,
	XFS_LOWSP_2_PCNT,
	XFS_LOWSP_3_PCNT,
	XFS_LOWSP_4_PCNT,
	XFS_LOWSP_5_PCNT,
	XFS_LOWSP_MAX,
};

/*
 * Error Configuration
 *
 * Error classes define the subsystem the configuration belongs to.
 * Error numbers define the errors that are configurable.
 */
enum {
	XFS_ERR_METADATA,
	XFS_ERR_CLASS_MAX,
};
enum {
	XFS_ERR_DEFAULT,
	XFS_ERR_EIO,
	XFS_ERR_ENOSPC,
	XFS_ERR_ENODEV,
	XFS_ERR_ERRNO_MAX,
};

#define XFS_ERR_RETRY_FOREVER	-1

/*
 * Although retry_timeout is in jiffies which is normally an unsigned long,
 * we limit the retry timeout to 86400 seconds, or one day.  So even a
 * signed 32-bit long is sufficient for a HZ value up to 24855.  Making it
 * signed lets us store the special "-1" value, meaning retry forever.
 */
struct xfs_error_cfg {
	struct xfs_kobj	kobj;
	int		max_retries;
	long		retry_timeout;	/* in jiffies, -1 = infinite */
};

/*
 * The struct xfsmount layout is optimised to separate read-mostly variables
 * from variables that are frequently modified. We put the read-mostly variables
 * first, then place all the other variables at the end.
 *
 * Typically, read-mostly variables are those that are set at mount time and
 * never changed again, or only change rarely as a result of things like sysfs
 * knobs being tweaked.
 */
typedef struct xfs_mount {
	struct xfs_sb		m_sb;		/* copy of fs superblock */
	struct super_block	*m_super;
	struct xfs_ail		*m_ail;		/* fs active log item list */
	struct xfs_buf		*m_sb_bp;	/* buffer for superblock */
	char			*m_rtname;	/* realtime device name */
	char			*m_logname;	/* external log device name */
	struct xfs_da_geometry	*m_dir_geo;	/* directory block geometry */
	struct xfs_da_geometry	*m_attr_geo;	/* attribute block geometry */
	struct xlog		*m_log;		/* log specific stuff */
	struct xfs_inode	*m_rbmip;	/* pointer to bitmap inode */
	struct xfs_inode	*m_rsumip;	/* pointer to summary inode */
	struct xfs_inode	*m_rootip;	/* pointer to root directory */
	struct xfs_quotainfo	*m_quotainfo;	/* disk quota information */
	xfs_buftarg_t		*m_ddev_targp;	/* saves taking the address */
	xfs_buftarg_t		*m_logdev_targp;/* ptr to log device */
	xfs_buftarg_t		*m_rtdev_targp;	/* ptr to rt device */
	/*
	 * Optional cache of rt summary level per bitmap block with the
	 * invariant that m_rsum_cache[bbno] <= the minimum i for which
	 * rsum[i][bbno] != 0. Reads and writes are serialized by the rsumip
	 * inode lock.
	 */
	uint8_t			*m_rsum_cache;
	struct xfs_mru_cache	*m_filestream;  /* per-mount filestream data */
	struct workqueue_struct *m_buf_workqueue;
	struct workqueue_struct	*m_unwritten_workqueue;
	struct workqueue_struct	*m_cil_workqueue;
	struct workqueue_struct	*m_reclaim_workqueue;
	struct workqueue_struct *m_gc_workqueue;
	struct workqueue_struct	*m_sync_workqueue;

	int			m_bsize;	/* fs logical block size */
	uint8_t			m_blkbit_log;	/* blocklog + NBBY */
	uint8_t			m_blkbb_log;	/* blocklog - BBSHIFT */
	uint8_t			m_agno_log;	/* log #ag's */
	uint8_t			m_sectbb_log;	/* sectlog - BBSHIFT */
	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	uint			m_blockwmask;	/* blockwsize-1 */
	uint			m_alloc_mxr[2];	/* max alloc btree records */
	uint			m_alloc_mnr[2];	/* min alloc btree records */
	uint			m_bmap_dmxr[2];	/* max bmap btree records */
	uint			m_bmap_dmnr[2];	/* min bmap btree records */
	uint			m_rmap_mxr[2];	/* max rmap btree records */
	uint			m_rmap_mnr[2];	/* min rmap btree records */
	uint			m_refc_mxr[2];	/* max refc btree records */
	uint			m_refc_mnr[2];	/* min refc btree records */
	uint			m_ag_maxlevels;	/* XFS_AG_MAXLEVELS */
	uint			m_bm_maxlevels[2]; /* XFS_BM_MAXLEVELS */
	uint			m_rmap_maxlevels; /* max rmap btree levels */
	uint			m_refc_maxlevels; /* max refcount btree level */
	xfs_extlen_t		m_ag_prealloc_blocks; /* reserved ag blocks */
	uint			m_alloc_set_aside; /* space we can't use */
	uint			m_ag_max_usable; /* max space per AG */
	int			m_dalign;	/* stripe unit */
	int			m_swidth;	/* stripe width */
	xfs_agnumber_t		m_maxagi;	/* highest inode alloc group */
	uint			m_allocsize_log;/* min write size log bytes */
	uint			m_allocsize_blocks; /* min write size blocks */
	int			m_logbufs;	/* number of log buffers */
	int			m_logbsize;	/* size of each log buffer */
	uint			m_rsumlevels;	/* rt summary levels */
	uint			m_rsumsize;	/* size of rt summary, bytes */
	int			m_fixedfsid[2];	/* unchanged for life of FS */
	uint			m_qflags;	/* quota status flags */
	uint64_t		m_flags;	/* global mount flags */
	int64_t			m_low_space[XFS_LOWSP_MAX];
	struct xfs_ino_geometry	m_ino_geo;	/* inode geometry */
	struct xfs_trans_resv	m_resv;		/* precomputed res values */
						/* low free space thresholds */
	bool			m_always_cow;
	bool			m_fail_unmount;
	bool			m_finobt_nores; /* no per-AG finobt resv. */
	bool			m_update_sb;	/* sb needs update in mount */

	/*
	 * Bitsets of per-fs metadata that have been checked and/or are sick.
	 * Callers must hold m_sb_lock to access these two fields.
	 */
	uint8_t			m_fs_checked;
	uint8_t			m_fs_sick;
	/*
	 * Bitsets of rt metadata that have been checked and/or are sick.
	 * Callers must hold m_sb_lock to access this field.
	 */
	uint8_t			m_rt_checked;
	uint8_t			m_rt_sick;

	/*
	 * End of read-mostly variables. Frequently written variables and locks
	 * should be placed below this comment from now on. The first variable
	 * here is marked as cacheline aligned so they it is separated from
	 * the read-mostly variables.
	 */

	spinlock_t ____cacheline_aligned m_sb_lock; /* sb counter lock */
	struct percpu_counter	m_icount;	/* allocated inodes counter */
	struct percpu_counter	m_ifree;	/* free inodes counter */
	struct percpu_counter	m_fdblocks;	/* free block counter */
	/*
	 * Count of data device blocks reserved for delayed allocations,
	 * including indlen blocks.  Does not include allocated CoW staging
	 * extents or anything related to the rt device.
	 */
	struct percpu_counter	m_delalloc_blks;
	/*
	 * Global count of allocation btree blocks in use across all AGs. Only
	 * used when perag reservation is enabled. Helps prevent block
	 * reservation from attempting to reserve allocation btree blocks.
	 */
	atomic64_t		m_allocbt_blks;

	struct radix_tree_root	m_perag_tree;	/* per-ag accounting info */
	spinlock_t		m_perag_lock;	/* lock for m_perag_tree */
	uint64_t		m_resblks;	/* total reserved blocks */
	uint64_t		m_resblks_avail;/* available reserved blocks */
	uint64_t		m_resblks_save;	/* reserved blks @ remount,ro */
	struct delayed_work	m_reclaim_work;	/* background inode reclaim */
	struct xfs_kobj		m_kobj;
	struct xfs_kobj		m_error_kobj;
	struct xfs_kobj		m_error_meta_kobj;
	struct xfs_error_cfg	m_error_cfg[XFS_ERR_CLASS_MAX][XFS_ERR_ERRNO_MAX];
	struct xstats		m_stats;	/* per-fs stats */
	xfs_agnumber_t		m_agfrotor;	/* last ag where space found */
	xfs_agnumber_t		m_agirotor;	/* last ag dir inode alloced */
	spinlock_t		m_agirotor_lock;/* .. and lock protecting it */

	/*
	 * Workqueue item so that we can coalesce multiple inode flush attempts
	 * into a single flush.
	 */
	struct work_struct	m_flush_inodes_work;

	/*
	 * Generation of the filesysyem layout.  This is incremented by each
	 * growfs, and used by the pNFS server to ensure the client updates
	 * its view of the block device once it gets a layout that might
	 * reference the newly added blocks.  Does not need to be persistent
	 * as long as we only allow file system size increments, but if we
	 * ever support shrinks it would have to be persisted in addition
	 * to various other kinds of pain inflicted on the pNFS server.
	 */
	uint32_t		m_generation;
	struct mutex		m_growlock;	/* growfs mutex */

#ifdef DEBUG
	/*
	 * Frequency with which errors are injected.  Replaces xfs_etest; the
	 * value stored in here is the inverse of the frequency with which the
	 * error triggers.  1 = always, 2 = half the time, etc.
	 */
	unsigned int		*m_errortag;
	struct xfs_kobj		m_errortag_kobj;
#endif
} xfs_mount_t;

#define M_IGEO(mp)		(&(mp)->m_ino_geo)

/*
 * Flags for m_flags.
 */
#define XFS_MOUNT_WSYNC		(1ULL << 0)	/* for nfs - all metadata ops
						   must be synchronous except
						   for space allocations */
#define XFS_MOUNT_UNMOUNTING	(1ULL << 1)	/* filesystem is unmounting */
#define XFS_MOUNT_WAS_CLEAN	(1ULL << 3)
#define XFS_MOUNT_FS_SHUTDOWN	(1ULL << 4)	/* atomic stop of all filesystem
						   operations, typically for
						   disk errors in metadata */
#define XFS_MOUNT_DISCARD	(1ULL << 5)	/* discard unused blocks */
#define XFS_MOUNT_NOALIGN	(1ULL << 7)	/* turn off stripe alignment
						   allocations */
#define XFS_MOUNT_ATTR2		(1ULL << 8)	/* allow use of attr2 format */
#define XFS_MOUNT_GRPID		(1ULL << 9)	/* group-ID assigned from directory */
#define XFS_MOUNT_NORECOVERY	(1ULL << 10)	/* no recovery - dirty fs */
#define XFS_MOUNT_ALLOCSIZE	(1ULL << 12)	/* specified allocation size */
#define XFS_MOUNT_SMALL_INUMS	(1ULL << 14)	/* user wants 32bit inodes */
#define XFS_MOUNT_32BITINODES	(1ULL << 15)	/* inode32 allocator active */
#define XFS_MOUNT_NOUUID	(1ULL << 16)	/* ignore uuid during mount */
#define XFS_MOUNT_IKEEP		(1ULL << 18)	/* keep empty inode clusters*/
#define XFS_MOUNT_SWALLOC	(1ULL << 19)	/* turn on stripe width
						 * allocation */
#define XFS_MOUNT_RDONLY	(1ULL << 20)	/* read-only fs */
#define XFS_MOUNT_DIRSYNC	(1ULL << 21)	/* synchronous directory ops */
#define XFS_MOUNT_LARGEIO	(1ULL << 22)	/* report large preferred
						 * I/O size in stat() */
#define XFS_MOUNT_FILESTREAMS	(1ULL << 24)	/* enable the filestreams
						   allocator */
#define XFS_MOUNT_NOATTR2	(1ULL << 25)	/* disable use of attr2 format */
#define XFS_MOUNT_DAX_ALWAYS	(1ULL << 26)
#define XFS_MOUNT_DAX_NEVER	(1ULL << 27)

/*
 * Max and min values for mount-option defined I/O
 * preallocation sizes.
 */
#define XFS_MAX_IO_LOG		30	/* 1G */
#define XFS_MIN_IO_LOG		PAGE_SHIFT

#define XFS_LAST_UNMOUNT_WAS_CLEAN(mp)	\
				((mp)->m_flags & XFS_MOUNT_WAS_CLEAN)
#define XFS_FORCED_SHUTDOWN(mp)	((mp)->m_flags & XFS_MOUNT_FS_SHUTDOWN)
void xfs_do_force_shutdown(struct xfs_mount *mp, int flags, char *fname,
		int lnnum);
#define xfs_force_shutdown(m,f)	\
	xfs_do_force_shutdown(m, f, __FILE__, __LINE__)

#define SHUTDOWN_META_IO_ERROR	0x0001	/* write attempt to metadata failed */
#define SHUTDOWN_LOG_IO_ERROR	0x0002	/* write attempt to the log failed */
#define SHUTDOWN_FORCE_UMOUNT	0x0004	/* shutdown from a forced unmount */
#define SHUTDOWN_CORRUPT_INCORE	0x0008	/* corrupt in-memory data structures */

/*
 * Flags for xfs_mountfs
 */
#define XFS_MFSI_QUIET		0x40	/* Be silent if mount errors found */

static inline xfs_agnumber_t
xfs_daddr_to_agno(struct xfs_mount *mp, xfs_daddr_t d)
{
	xfs_rfsblock_t ld = XFS_BB_TO_FSBT(mp, d);
	do_div(ld, mp->m_sb.sb_agblocks);
	return (xfs_agnumber_t) ld;
}

static inline xfs_agblock_t
xfs_daddr_to_agbno(struct xfs_mount *mp, xfs_daddr_t d)
{
	xfs_rfsblock_t ld = XFS_BB_TO_FSBT(mp, d);
	return (xfs_agblock_t) do_div(ld, mp->m_sb.sb_agblocks);
}

int xfs_buf_hash_init(struct xfs_perag *pag);
void xfs_buf_hash_destroy(struct xfs_perag *pag);

extern void	xfs_uuid_table_free(void);
extern uint64_t xfs_default_resblks(xfs_mount_t *mp);
extern int	xfs_mountfs(xfs_mount_t *mp);
extern void	xfs_unmountfs(xfs_mount_t *);

extern int	xfs_mod_fdblocks(struct xfs_mount *mp, int64_t delta,
				 bool reserved);
extern int	xfs_mod_frextents(struct xfs_mount *mp, int64_t delta);

extern int	xfs_readsb(xfs_mount_t *, int);
extern void	xfs_freesb(xfs_mount_t *);
extern bool	xfs_fs_writable(struct xfs_mount *mp, int level);
extern int	xfs_sb_validate_fsb_count(struct xfs_sb *, uint64_t);

extern int	xfs_dev_is_read_only(struct xfs_mount *, char *);

extern void	xfs_set_low_space_thresholds(struct xfs_mount *);

int	xfs_zero_extent(struct xfs_inode *ip, xfs_fsblock_t start_fsb,
			xfs_off_t count_fsb);

struct xfs_error_cfg * xfs_error_get_cfg(struct xfs_mount *mp,
		int error_class, int error);
void xfs_force_summary_recalc(struct xfs_mount *mp);
void xfs_mod_delalloc(struct xfs_mount *mp, int64_t delta);

#endif	/* __XFS_MOUNT_H__ */
