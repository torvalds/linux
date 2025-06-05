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
 * Per-cpu deferred inode inactivation GC lists.
 */
struct xfs_inodegc {
	struct xfs_mount	*mp;
	struct llist_head	list;
	struct delayed_work	work;
	int			error;

	/* approximate count of inodes in the list */
	unsigned int		items;
	unsigned int		shrinker_hits;
	unsigned int		cpu;
};

/*
 * Container for each type of groups, used to look up individual groups and
 * describes the geometry.
 */
struct xfs_groups {
	struct xarray		xa;

	/*
	 * Maximum capacity of the group in FSBs.
	 *
	 * Each group is laid out densely in the daddr space.  For the
	 * degenerate case of a pre-rtgroups filesystem, the incore rtgroup
	 * pretends to have a zero-block and zero-blklog rtgroup.
	 */
	uint32_t		blocks;

	/*
	 * Log(2) of the logical size of each group.
	 *
	 * Compared to the blocks field above this is rounded up to the next
	 * power of two, and thus lays out the xfs_fsblock_t/xfs_rtblock_t
	 * space sparsely with a hole from blocks to (1 << blklog) at the end
	 * of each group.
	 */
	uint8_t			blklog;

	/*
	 * Zoned devices can have gaps beyond the usable capacity of a zone and
	 * the end in the LBA/daddr address space.  In other words, the hardware
	 * equivalent to the RT groups already takes care of the power of 2
	 * alignment for us.  In this case the sparse FSB/RTB address space maps
	 * 1:1 to the device address space.
	 */
	bool			has_daddr_gaps;

	/*
	 * Mask to extract the group-relative block number from a FSB.
	 * For a pre-rtgroups filesystem we pretend to have one very large
	 * rtgroup, so this mask must be 64-bit.
	 */
	uint64_t		blkmask;

	/*
	 * Start of the first group in the device.  This is used to support a
	 * RT device following the data device on the same block device for
	 * SMR hard drives.
	 */
	xfs_fsblock_t		start_fsb;
};

struct xfs_freecounter {
	/* free blocks for general use: */
	struct percpu_counter	count;

	/* total reserved blocks: */
	uint64_t		res_total;

	/* available reserved blocks: */
	uint64_t		res_avail;

	/* reserved blks @ remount,ro: */
	uint64_t		res_saved;
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
	struct xfs_buf		*m_rtsb_bp;	/* realtime superblock */
	char			*m_rtname;	/* realtime device name */
	char			*m_logname;	/* external log device name */
	struct xfs_da_geometry	*m_dir_geo;	/* directory block geometry */
	struct xfs_da_geometry	*m_attr_geo;	/* attribute block geometry */
	struct xlog		*m_log;		/* log specific stuff */
	struct xfs_inode	*m_rootip;	/* pointer to root directory */
	struct xfs_inode	*m_metadirip;	/* ptr to metadata directory */
	struct xfs_inode	*m_rtdirip;	/* ptr to realtime metadir */
	struct xfs_quotainfo	*m_quotainfo;	/* disk quota information */
	struct xfs_buftarg	*m_ddev_targp;	/* data device */
	struct xfs_buftarg	*m_logdev_targp;/* log device */
	struct xfs_buftarg	*m_rtdev_targp;	/* rt device */
	void __percpu		*m_inodegc;	/* percpu inodegc structures */
	struct xfs_mru_cache	*m_filestream;  /* per-mount filestream data */
	struct workqueue_struct *m_buf_workqueue;
	struct workqueue_struct	*m_unwritten_workqueue;
	struct workqueue_struct	*m_reclaim_workqueue;
	struct workqueue_struct	*m_sync_workqueue;
	struct workqueue_struct *m_blockgc_wq;
	struct workqueue_struct *m_inodegc_wq;

	int			m_bsize;	/* fs logical block size */
	uint8_t			m_blkbit_log;	/* blocklog + NBBY */
	uint8_t			m_blkbb_log;	/* blocklog - BBSHIFT */
	uint8_t			m_agno_log;	/* log #ag's */
	uint8_t			m_sectbb_log;	/* sectlog - BBSHIFT */
	int8_t			m_rtxblklog;	/* log2 of rextsize, if possible */

	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	/* number of rt extents per rt bitmap block if rtgroups enabled */
	unsigned int		m_rtx_per_rbmblock;
	uint			m_alloc_mxr[2];	/* max alloc btree records */
	uint			m_alloc_mnr[2];	/* min alloc btree records */
	uint			m_bmap_dmxr[2];	/* max bmap btree records */
	uint			m_bmap_dmnr[2];	/* min bmap btree records */
	uint			m_rmap_mxr[2];	/* max rmap btree records */
	uint			m_rmap_mnr[2];	/* min rmap btree records */
	uint			m_rtrmap_mxr[2]; /* max rtrmap btree records */
	uint			m_rtrmap_mnr[2]; /* min rtrmap btree records */
	uint			m_refc_mxr[2];	/* max refc btree records */
	uint			m_refc_mnr[2];	/* min refc btree records */
	uint			m_rtrefc_mxr[2]; /* max rtrefc btree records */
	uint			m_rtrefc_mnr[2]; /* min rtrefc btree records */
	uint			m_alloc_maxlevels; /* max alloc btree levels */
	uint			m_bm_maxlevels[2]; /* max bmap btree levels */
	uint			m_rmap_maxlevels; /* max rmap btree levels */
	uint			m_rtrmap_maxlevels; /* max rtrmap btree level */
	uint			m_refc_maxlevels; /* max refcount btree level */
	uint			m_rtrefc_maxlevels; /* max rtrefc btree level */
	unsigned int		m_agbtree_maxlevels; /* max level of all AG btrees */
	unsigned int		m_rtbtree_maxlevels; /* max level of all rt btrees */
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
	unsigned int		m_rsumlevels;	/* rt summary levels */
	xfs_filblks_t		m_rsumblocks;	/* size of rt summary, FSBs */
	int			m_fixedfsid[2];	/* unchanged for life of FS */
	uint			m_qflags;	/* quota status flags */
	uint64_t		m_features;	/* active filesystem features */
	uint64_t		m_low_space[XFS_LOWSP_MAX];
	uint64_t		m_low_rtexts[XFS_LOWSP_MAX];
	uint64_t		m_rtxblkmask;	/* rt extent block mask */
	struct xfs_ino_geometry	m_ino_geo;	/* inode geometry */
	struct xfs_trans_resv	m_resv;		/* precomputed res values */
						/* low free space thresholds */
	unsigned long		m_opstate;	/* dynamic state flags */
	bool			m_always_cow;
	bool			m_fail_unmount;
	bool			m_finobt_nores; /* no per-AG finobt resv. */
	bool			m_update_sb;	/* sb needs update in mount */
	unsigned int		m_max_open_zones;
	unsigned int		m_zonegc_low_space;

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

	struct xfs_freecounter	m_free[XC_FREE_NR];

	/*
	 * Count of data device blocks reserved for delayed allocations,
	 * including indlen blocks.  Does not include allocated CoW staging
	 * extents or anything related to the rt device.
	 */
	struct percpu_counter	m_delalloc_blks;

	/*
	 * RT version of the above.
	 */
	struct percpu_counter	m_delalloc_rtextents;

	/*
	 * Global count of allocation btree blocks in use across all AGs. Only
	 * used when perag reservation is enabled. Helps prevent block
	 * reservation from attempting to reserve allocation btree blocks.
	 */
	atomic64_t		m_allocbt_blks;

	struct xfs_groups	m_groups[XG_TYPE_MAX];
	struct delayed_work	m_reclaim_work;	/* background inode reclaim */
	struct xfs_zone_info	*m_zone_info;	/* zone allocator information */
	struct dentry		*m_debugfs;	/* debugfs parent */
	struct xfs_kobj		m_kobj;
	struct xfs_kobj		m_error_kobj;
	struct xfs_kobj		m_error_meta_kobj;
	struct xfs_error_cfg	m_error_cfg[XFS_ERR_CLASS_MAX][XFS_ERR_ERRNO_MAX];
	struct xstats		m_stats;	/* per-fs stats */
#ifdef CONFIG_XFS_ONLINE_SCRUB_STATS
	struct xchk_stats	*m_scrub_stats;
#endif
	struct xfs_kobj		m_zoned_kobj;
	xfs_agnumber_t		m_agfrotor;	/* last ag where space found */
	atomic_t		m_agirotor;	/* last ag dir inode alloced */
	atomic_t		m_rtgrotor;	/* last rtgroup rtpicked */

	struct mutex		m_metafile_resv_lock;
	uint64_t		m_metafile_resv_target;
	uint64_t		m_metafile_resv_used;
	uint64_t		m_metafile_resv_avail;

	/* Memory shrinker to throttle and reprioritize inodegc */
	struct shrinker		*m_inodegc_shrinker;
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

	/* cpus that have inodes queued for inactivation */
	struct cpumask		m_inodegc_cpumask;

	/* Hook to feed dirent updates to an active online repair. */
	struct xfs_hooks	m_dir_update_hooks;
} xfs_mount_t;

#define M_IGEO(mp)		(&(mp)->m_ino_geo)

/*
 * Flags for m_features.
 *
 * These are all the active features in the filesystem, regardless of how
 * they are configured.
 */
#define XFS_FEAT_ATTR		(1ULL << 0)	/* xattrs present in fs */
#define XFS_FEAT_NLINK		(1ULL << 1)	/* 32 bit link counts */
#define XFS_FEAT_QUOTA		(1ULL << 2)	/* quota active */
#define XFS_FEAT_ALIGN		(1ULL << 3)	/* inode alignment */
#define XFS_FEAT_DALIGN		(1ULL << 4)	/* data alignment */
#define XFS_FEAT_LOGV2		(1ULL << 5)	/* version 2 logs */
#define XFS_FEAT_SECTOR		(1ULL << 6)	/* sector size > 512 bytes */
#define XFS_FEAT_EXTFLG		(1ULL << 7)	/* unwritten extents */
#define XFS_FEAT_ASCIICI	(1ULL << 8)	/* ASCII only case-insens. */
#define XFS_FEAT_LAZYSBCOUNT	(1ULL << 9)	/* Superblk counters */
#define XFS_FEAT_ATTR2		(1ULL << 10)	/* dynamic attr fork */
#define XFS_FEAT_PARENT		(1ULL << 11)	/* parent pointers */
#define XFS_FEAT_PROJID32	(1ULL << 12)	/* 32 bit project id */
#define XFS_FEAT_CRC		(1ULL << 13)	/* metadata CRCs */
#define XFS_FEAT_V3INODES	(1ULL << 14)	/* Version 3 inodes */
#define XFS_FEAT_PQUOTINO	(1ULL << 15)	/* non-shared proj/grp quotas */
#define XFS_FEAT_FTYPE		(1ULL << 16)	/* inode type in dir */
#define XFS_FEAT_FINOBT		(1ULL << 17)	/* free inode btree */
#define XFS_FEAT_RMAPBT		(1ULL << 18)	/* reverse map btree */
#define XFS_FEAT_REFLINK	(1ULL << 19)	/* reflinked files */
#define XFS_FEAT_SPINODES	(1ULL << 20)	/* sparse inode chunks */
#define XFS_FEAT_META_UUID	(1ULL << 21)	/* metadata UUID */
#define XFS_FEAT_REALTIME	(1ULL << 22)	/* realtime device present */
#define XFS_FEAT_INOBTCNT	(1ULL << 23)	/* inobt block counts */
#define XFS_FEAT_BIGTIME	(1ULL << 24)	/* large timestamps */
#define XFS_FEAT_NEEDSREPAIR	(1ULL << 25)	/* needs xfs_repair */
#define XFS_FEAT_NREXT64	(1ULL << 26)	/* large extent counters */
#define XFS_FEAT_EXCHANGE_RANGE	(1ULL << 27)	/* exchange range */
#define XFS_FEAT_METADIR	(1ULL << 28)	/* metadata directory tree */
#define XFS_FEAT_ZONED		(1ULL << 29)	/* zoned RT device */

/* Mount features */
#define XFS_FEAT_NOLIFETIME	(1ULL << 47)	/* disable lifetime hints */
#define XFS_FEAT_NOATTR2	(1ULL << 48)	/* disable attr2 creation */
#define XFS_FEAT_NOALIGN	(1ULL << 49)	/* ignore alignment */
#define XFS_FEAT_ALLOCSIZE	(1ULL << 50)	/* user specified allocation size */
#define XFS_FEAT_LARGE_IOSIZE	(1ULL << 51)	/* report large preferred
						 * I/O size in stat() */
#define XFS_FEAT_WSYNC		(1ULL << 52)	/* synchronous metadata ops */
#define XFS_FEAT_DIRSYNC	(1ULL << 53)	/* synchronous directory ops */
#define XFS_FEAT_DISCARD	(1ULL << 54)	/* discard unused blocks */
#define XFS_FEAT_GRPID		(1ULL << 55)	/* group-ID assigned from directory */
#define XFS_FEAT_SMALL_INUMS	(1ULL << 56)	/* user wants 32bit inodes */
#define XFS_FEAT_IKEEP		(1ULL << 57)	/* keep empty inode clusters*/
#define XFS_FEAT_SWALLOC	(1ULL << 58)	/* stripe width allocation */
#define XFS_FEAT_FILESTREAMS	(1ULL << 59)	/* use filestreams allocator */
#define XFS_FEAT_DAX_ALWAYS	(1ULL << 60)	/* DAX always enabled */
#define XFS_FEAT_DAX_NEVER	(1ULL << 61)	/* DAX never enabled */
#define XFS_FEAT_NORECOVERY	(1ULL << 62)	/* no recovery - dirty fs */
#define XFS_FEAT_NOUUID		(1ULL << 63)	/* ignore uuid during mount */

#define __XFS_HAS_FEAT(name, NAME) \
static inline bool xfs_has_ ## name (const struct xfs_mount *mp) \
{ \
	return mp->m_features & XFS_FEAT_ ## NAME; \
}

/* Some features can be added dynamically so they need a set wrapper, too. */
#define __XFS_ADD_FEAT(name, NAME) \
	__XFS_HAS_FEAT(name, NAME); \
static inline void xfs_add_ ## name (struct xfs_mount *mp) \
{ \
	mp->m_features |= XFS_FEAT_ ## NAME; \
	xfs_sb_version_add ## name(&mp->m_sb); \
}

/* Superblock features */
__XFS_ADD_FEAT(attr, ATTR)
__XFS_HAS_FEAT(nlink, NLINK)
__XFS_ADD_FEAT(quota, QUOTA)
__XFS_HAS_FEAT(dalign, DALIGN)
__XFS_HAS_FEAT(sector, SECTOR)
__XFS_HAS_FEAT(asciici, ASCIICI)
__XFS_HAS_FEAT(parent, PARENT)
__XFS_HAS_FEAT(ftype, FTYPE)
__XFS_HAS_FEAT(finobt, FINOBT)
__XFS_HAS_FEAT(rmapbt, RMAPBT)
__XFS_HAS_FEAT(reflink, REFLINK)
__XFS_HAS_FEAT(sparseinodes, SPINODES)
__XFS_HAS_FEAT(metauuid, META_UUID)
__XFS_HAS_FEAT(realtime, REALTIME)
__XFS_HAS_FEAT(inobtcounts, INOBTCNT)
__XFS_HAS_FEAT(bigtime, BIGTIME)
__XFS_HAS_FEAT(needsrepair, NEEDSREPAIR)
__XFS_HAS_FEAT(large_extent_counts, NREXT64)
__XFS_HAS_FEAT(exchange_range, EXCHANGE_RANGE)
__XFS_HAS_FEAT(metadir, METADIR)
__XFS_HAS_FEAT(zoned, ZONED)
__XFS_HAS_FEAT(nolifetime, NOLIFETIME)

static inline bool xfs_has_rtgroups(const struct xfs_mount *mp)
{
	/* all metadir file systems also allow rtgroups */
	return xfs_has_metadir(mp);
}

static inline bool xfs_has_rtsb(const struct xfs_mount *mp)
{
	/* all rtgroups filesystems with an rt section have an rtsb */
	return xfs_has_rtgroups(mp) &&
		xfs_has_realtime(mp) &&
		!xfs_has_zoned(mp);
}

static inline bool xfs_has_rtrmapbt(const struct xfs_mount *mp)
{
	return xfs_has_rtgroups(mp) && xfs_has_realtime(mp) &&
	       xfs_has_rmapbt(mp);
}

static inline bool xfs_has_rtreflink(const struct xfs_mount *mp)
{
	return xfs_has_metadir(mp) && xfs_has_realtime(mp) &&
	       xfs_has_reflink(mp);
}

static inline bool xfs_has_nonzoned(const struct xfs_mount *mp)
{
	return !xfs_has_zoned(mp);
}

/*
 * Some features are always on for v5 file systems, allow the compiler to
 * eliminiate dead code when building without v4 support.
 */
#define __XFS_HAS_V4_FEAT(name, NAME) \
static inline bool xfs_has_ ## name (struct xfs_mount *mp) \
{ \
	return !IS_ENABLED(CONFIG_XFS_SUPPORT_V4) || \
		(mp->m_features & XFS_FEAT_ ## NAME); \
}

#define __XFS_ADD_V4_FEAT(name, NAME) \
	__XFS_HAS_V4_FEAT(name, NAME); \
static inline void xfs_add_ ## name (struct xfs_mount *mp) \
{ \
	if (IS_ENABLED(CONFIG_XFS_SUPPORT_V4)) { \
		mp->m_features |= XFS_FEAT_ ## NAME; \
		xfs_sb_version_add ## name(&mp->m_sb); \
	} \
}

__XFS_HAS_V4_FEAT(align, ALIGN)
__XFS_HAS_V4_FEAT(logv2, LOGV2)
__XFS_HAS_V4_FEAT(extflg, EXTFLG)
__XFS_HAS_V4_FEAT(lazysbcount, LAZYSBCOUNT)
__XFS_ADD_V4_FEAT(attr2, ATTR2)
__XFS_ADD_V4_FEAT(projid32, PROJID32)
__XFS_HAS_V4_FEAT(v3inodes, V3INODES)
__XFS_HAS_V4_FEAT(crc, CRC)
__XFS_HAS_V4_FEAT(pquotino, PQUOTINO)

/*
 * Mount features
 *
 * These do not change dynamically - features that can come and go, such as 32
 * bit inodes and read-only state, are kept as operational state rather than
 * features.
 */
__XFS_HAS_FEAT(noattr2, NOATTR2)
__XFS_HAS_FEAT(noalign, NOALIGN)
__XFS_HAS_FEAT(allocsize, ALLOCSIZE)
__XFS_HAS_FEAT(large_iosize, LARGE_IOSIZE)
__XFS_HAS_FEAT(wsync, WSYNC)
__XFS_HAS_FEAT(dirsync, DIRSYNC)
__XFS_HAS_FEAT(discard, DISCARD)
__XFS_HAS_FEAT(grpid, GRPID)
__XFS_HAS_FEAT(small_inums, SMALL_INUMS)
__XFS_HAS_FEAT(ikeep, IKEEP)
__XFS_HAS_FEAT(swalloc, SWALLOC)
__XFS_HAS_FEAT(filestreams, FILESTREAMS)
__XFS_HAS_FEAT(dax_always, DAX_ALWAYS)
__XFS_HAS_FEAT(dax_never, DAX_NEVER)
__XFS_HAS_FEAT(norecovery, NORECOVERY)
__XFS_HAS_FEAT(nouuid, NOUUID)

/*
 * Operational mount state flags
 *
 * Use these with atomic bit ops only!
 */
#define XFS_OPSTATE_UNMOUNTING		0	/* filesystem is unmounting */
#define XFS_OPSTATE_CLEAN		1	/* mount was clean */
#define XFS_OPSTATE_SHUTDOWN		2	/* stop all fs operations */
#define XFS_OPSTATE_INODE32		3	/* inode32 allocator active */
#define XFS_OPSTATE_READONLY		4	/* read-only fs */

/*
 * If set, inactivation worker threads will be scheduled to process queued
 * inodegc work.  If not, queued inodes remain in memory waiting to be
 * processed.
 */
#define XFS_OPSTATE_INODEGC_ENABLED	5
/*
 * If set, background speculative prealloc gc worker threads will be scheduled
 * to process queued blockgc work.  If not, inodes retain their preallocations
 * until explicitly deleted.
 */
#define XFS_OPSTATE_BLOCKGC_ENABLED	6

/* Kernel has logged a warning about pNFS being used on this fs. */
#define XFS_OPSTATE_WARNED_PNFS		7
/* Kernel has logged a warning about online fsck being used on this fs. */
#define XFS_OPSTATE_WARNED_SCRUB	8
/* Kernel has logged a warning about shrink being used on this fs. */
#define XFS_OPSTATE_WARNED_SHRINK	9
/* Kernel has logged a warning about logged xattr updates being used. */
#define XFS_OPSTATE_WARNED_LARP		10
/* Mount time quotacheck is running */
#define XFS_OPSTATE_QUOTACHECK_RUNNING	11
/* Do we want to clear log incompat flags? */
#define XFS_OPSTATE_UNSET_LOG_INCOMPAT	12
/* Filesystem can use logged extended attributes */
#define XFS_OPSTATE_USE_LARP		13
/* Kernel has logged a warning about blocksize > pagesize on this fs. */
#define XFS_OPSTATE_WARNED_LBS		14
/* Kernel has logged a warning about exchange-range being used on this fs. */
#define XFS_OPSTATE_WARNED_EXCHRANGE	15
/* Kernel has logged a warning about parent pointers being used on this fs. */
#define XFS_OPSTATE_WARNED_PPTR		16
/* Kernel has logged a warning about metadata dirs being used on this fs. */
#define XFS_OPSTATE_WARNED_METADIR	17
/* Filesystem should use qflags to determine quotaon status */
#define XFS_OPSTATE_RESUMING_QUOTAON	18
/* Kernel has logged a warning about zoned RT device being used on this fs. */
#define XFS_OPSTATE_WARNED_ZONED	19
/* (Zoned) GC is in progress */
#define XFS_OPSTATE_ZONEGC_RUNNING	20

#define __XFS_IS_OPSTATE(name, NAME) \
static inline bool xfs_is_ ## name (struct xfs_mount *mp) \
{ \
	return test_bit(XFS_OPSTATE_ ## NAME, &mp->m_opstate); \
} \
static inline bool xfs_clear_ ## name (struct xfs_mount *mp) \
{ \
	return test_and_clear_bit(XFS_OPSTATE_ ## NAME, &mp->m_opstate); \
} \
static inline bool xfs_set_ ## name (struct xfs_mount *mp) \
{ \
	return test_and_set_bit(XFS_OPSTATE_ ## NAME, &mp->m_opstate); \
}

__XFS_IS_OPSTATE(unmounting, UNMOUNTING)
__XFS_IS_OPSTATE(clean, CLEAN)
__XFS_IS_OPSTATE(shutdown, SHUTDOWN)
__XFS_IS_OPSTATE(inode32, INODE32)
__XFS_IS_OPSTATE(readonly, READONLY)
__XFS_IS_OPSTATE(inodegc_enabled, INODEGC_ENABLED)
__XFS_IS_OPSTATE(blockgc_enabled, BLOCKGC_ENABLED)
#ifdef CONFIG_XFS_QUOTA
__XFS_IS_OPSTATE(quotacheck_running, QUOTACHECK_RUNNING)
__XFS_IS_OPSTATE(resuming_quotaon, RESUMING_QUOTAON)
#else
static inline bool xfs_is_quotacheck_running(struct xfs_mount *mp)
{
	return false;
}
static inline bool xfs_is_resuming_quotaon(struct xfs_mount *mp)
{
	return false;
}
static inline void xfs_set_resuming_quotaon(struct xfs_mount *m)
{
}
static inline bool xfs_clear_resuming_quotaon(struct xfs_mount *mp)
{
	return false;
}
#endif /* CONFIG_XFS_QUOTA */
__XFS_IS_OPSTATE(done_with_log_incompat, UNSET_LOG_INCOMPAT)
__XFS_IS_OPSTATE(using_logged_xattrs, USE_LARP)
__XFS_IS_OPSTATE(zonegc_running, ZONEGC_RUNNING)

static inline bool
xfs_should_warn(struct xfs_mount *mp, long nr)
{
	return !test_and_set_bit(nr, &mp->m_opstate);
}

#define XFS_OPSTATE_STRINGS \
	{ (1UL << XFS_OPSTATE_UNMOUNTING),		"unmounting" }, \
	{ (1UL << XFS_OPSTATE_CLEAN),			"clean" }, \
	{ (1UL << XFS_OPSTATE_SHUTDOWN),		"shutdown" }, \
	{ (1UL << XFS_OPSTATE_INODE32),			"inode32" }, \
	{ (1UL << XFS_OPSTATE_READONLY),		"read_only" }, \
	{ (1UL << XFS_OPSTATE_INODEGC_ENABLED),		"inodegc" }, \
	{ (1UL << XFS_OPSTATE_BLOCKGC_ENABLED),		"blockgc" }, \
	{ (1UL << XFS_OPSTATE_WARNED_SCRUB),		"wscrub" }, \
	{ (1UL << XFS_OPSTATE_WARNED_SHRINK),		"wshrink" }, \
	{ (1UL << XFS_OPSTATE_WARNED_LARP),		"wlarp" }, \
	{ (1UL << XFS_OPSTATE_QUOTACHECK_RUNNING),	"quotacheck" }, \
	{ (1UL << XFS_OPSTATE_UNSET_LOG_INCOMPAT),	"unset_log_incompat" }, \
	{ (1UL << XFS_OPSTATE_USE_LARP),		"logged_xattrs" }

/*
 * Max and min values for mount-option defined I/O
 * preallocation sizes.
 */
#define XFS_MAX_IO_LOG		30	/* 1G */
#define XFS_MIN_IO_LOG		PAGE_SHIFT

void xfs_do_force_shutdown(struct xfs_mount *mp, uint32_t flags, char *fname,
		int lnnum);
#define xfs_force_shutdown(m,f)	\
	xfs_do_force_shutdown(m, f, __FILE__, __LINE__)

#define SHUTDOWN_META_IO_ERROR	(1u << 0) /* write attempt to metadata failed */
#define SHUTDOWN_LOG_IO_ERROR	(1u << 1) /* write attempt to the log failed */
#define SHUTDOWN_FORCE_UMOUNT	(1u << 2) /* shutdown from a forced unmount */
#define SHUTDOWN_CORRUPT_INCORE	(1u << 3) /* corrupt in-memory structures */
#define SHUTDOWN_CORRUPT_ONDISK	(1u << 4)  /* corrupt metadata on device */
#define SHUTDOWN_DEVICE_REMOVED	(1u << 5) /* device removed underneath us */

#define XFS_SHUTDOWN_STRINGS \
	{ SHUTDOWN_META_IO_ERROR,	"metadata_io" }, \
	{ SHUTDOWN_LOG_IO_ERROR,	"log_io" }, \
	{ SHUTDOWN_FORCE_UMOUNT,	"force_umount" }, \
	{ SHUTDOWN_CORRUPT_INCORE,	"corruption" }, \
	{ SHUTDOWN_DEVICE_REMOVED,	"device_removed" }

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

extern void	xfs_uuid_table_free(void);
uint64_t	xfs_default_resblks(struct xfs_mount *mp,
			enum xfs_free_counter ctr);
extern int	xfs_mountfs(xfs_mount_t *mp);
extern void	xfs_unmountfs(xfs_mount_t *);

/*
 * Deltas for the block count can vary from 1 to very large, but lock contention
 * only occurs on frequent small block count updates such as in the delayed
 * allocation path for buffered writes (page a time updates). Hence we set
 * a large batch count (1024) to minimise global counter updates except when
 * we get near to ENOSPC and we have to be very accurate with our updates.
 */
#define XFS_FDBLOCKS_BATCH	1024

uint64_t xfs_freecounter_unavailable(struct xfs_mount *mp,
		enum xfs_free_counter ctr);

/*
 * Sum up the freecount, but never return negative values.
 */
static inline s64 xfs_sum_freecounter(struct xfs_mount *mp,
		enum xfs_free_counter ctr)
{
	return percpu_counter_sum_positive(&mp->m_free[ctr].count);
}

/*
 * Same as above, but does return negative values.  Mostly useful for
 * special cases like repair and tracing.
 */
static inline s64 xfs_sum_freecounter_raw(struct xfs_mount *mp,
		enum xfs_free_counter ctr)
{
	return percpu_counter_sum(&mp->m_free[ctr].count);
}

/*
 * This just provides and estimate without the cpu-local updates, use
 * xfs_sum_freecounter for the exact value.
 */
static inline s64 xfs_estimate_freecounter(struct xfs_mount *mp,
		enum xfs_free_counter ctr)
{
	return percpu_counter_read_positive(&mp->m_free[ctr].count);
}

static inline int xfs_compare_freecounter(struct xfs_mount *mp,
		enum xfs_free_counter ctr, s64 rhs, s32 batch)
{
	return __percpu_counter_compare(&mp->m_free[ctr].count, rhs, batch);
}

static inline void xfs_set_freecounter(struct xfs_mount *mp,
		enum xfs_free_counter ctr, uint64_t val)
{
	percpu_counter_set(&mp->m_free[ctr].count, val);
}

int xfs_dec_freecounter(struct xfs_mount *mp, enum xfs_free_counter ctr,
		uint64_t delta, bool rsvd);
void xfs_add_freecounter(struct xfs_mount *mp, enum xfs_free_counter ctr,
		uint64_t delta);

static inline int xfs_dec_fdblocks(struct xfs_mount *mp, uint64_t delta,
		bool reserved)
{
	return xfs_dec_freecounter(mp, XC_FREE_BLOCKS, delta, reserved);
}

static inline void xfs_add_fdblocks(struct xfs_mount *mp, uint64_t delta)
{
	xfs_add_freecounter(mp, XC_FREE_BLOCKS, delta);
}

static inline int xfs_dec_frextents(struct xfs_mount *mp, uint64_t delta)
{
	return xfs_dec_freecounter(mp, XC_FREE_RTEXTENTS, delta, false);
}

static inline void xfs_add_frextents(struct xfs_mount *mp, uint64_t delta)
{
	xfs_add_freecounter(mp, XC_FREE_RTEXTENTS, delta);
}

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
int xfs_add_incompat_log_feature(struct xfs_mount *mp, uint32_t feature);
bool xfs_clear_incompat_log_features(struct xfs_mount *mp);
void xfs_mod_delalloc(struct xfs_inode *ip, int64_t data_delta,
		int64_t ind_delta);
static inline void xfs_mod_sb_delalloc(struct xfs_mount *mp, int64_t delta)
{
	percpu_counter_add(&mp->m_delalloc_blks, delta);
}

#endif	/* __XFS_MOUNT_H__ */
