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
#ifndef __XFS_MOUNT_H__
#define	__XFS_MOUNT_H__

typedef struct xfs_trans_reservations {
	uint	tr_write;	/* extent alloc trans */
	uint	tr_itruncate;	/* truncate trans */
	uint	tr_rename;	/* rename trans */
	uint	tr_link;	/* link trans */
	uint	tr_remove;	/* unlink trans */
	uint	tr_symlink;	/* symlink trans */
	uint	tr_create;	/* create trans */
	uint	tr_mkdir;	/* mkdir trans */
	uint	tr_ifree;	/* inode free trans */
	uint	tr_ichange;	/* inode update trans */
	uint	tr_growdata;	/* fs data section grow trans */
	uint	tr_swrite;	/* sync write inode trans */
	uint	tr_addafork;	/* cvt inode to attributed trans */
	uint	tr_writeid;	/* write setuid/setgid file */
	uint	tr_attrinval;	/* attr fork buffer invalidation */
	uint	tr_attrset;	/* set/create an attribute */
	uint	tr_attrrm;	/* remove an attribute */
	uint	tr_clearagi;	/* clear bad agi unlinked ino bucket */
	uint	tr_growrtalloc;	/* grow realtime allocations */
	uint	tr_growrtzero;	/* grow realtime zeroing */
	uint	tr_growrtfree;	/* grow realtime freeing */
} xfs_trans_reservations_t;

#ifndef __KERNEL__

#define xfs_daddr_to_agno(mp,d) \
	((xfs_agnumber_t)(XFS_BB_TO_FSBT(mp, d) / (mp)->m_sb.sb_agblocks))
#define xfs_daddr_to_agbno(mp,d) \
	((xfs_agblock_t)(XFS_BB_TO_FSBT(mp, d) % (mp)->m_sb.sb_agblocks))

#else /* __KERNEL__ */

#include "xfs_sync.h"

struct log;
struct xfs_mount_args;
struct xfs_inode;
struct xfs_bmbt_irec;
struct xfs_bmap_free;
struct xfs_extdelta;
struct xfs_swapext;
struct xfs_mru_cache;
struct xfs_nameops;
struct xfs_ail;
struct xfs_quotainfo;

#ifdef HAVE_PERCPU_SB

/*
 * Valid per-cpu incore superblock counters. Note that if you add new counters,
 * you may need to define new counter disabled bit field descriptors as there
 * are more possible fields in the superblock that can fit in a bitfield on a
 * 32 bit platform. The XFS_SBS_* values for the current current counters just
 * fit.
 */
typedef struct xfs_icsb_cnts {
	uint64_t	icsb_fdblocks;
	uint64_t	icsb_ifree;
	uint64_t	icsb_icount;
	unsigned long	icsb_flags;
} xfs_icsb_cnts_t;

#define XFS_ICSB_FLAG_LOCK	(1 << 0)	/* counter lock bit */

#define XFS_ICSB_LAZY_COUNT	(1 << 1)	/* accuracy not needed */

extern int	xfs_icsb_init_counters(struct xfs_mount *);
extern void	xfs_icsb_reinit_counters(struct xfs_mount *);
extern void	xfs_icsb_destroy_counters(struct xfs_mount *);
extern void	xfs_icsb_sync_counters(struct xfs_mount *, int);
extern void	xfs_icsb_sync_counters_locked(struct xfs_mount *, int);
extern int	xfs_icsb_modify_counters(struct xfs_mount *, xfs_sb_field_t,
						int64_t, int);

#else
#define xfs_icsb_init_counters(mp)		(0)
#define xfs_icsb_destroy_counters(mp)		do { } while (0)
#define xfs_icsb_reinit_counters(mp)		do { } while (0)
#define xfs_icsb_sync_counters(mp, flags)	do { } while (0)
#define xfs_icsb_sync_counters_locked(mp, flags) do { } while (0)
#define xfs_icsb_modify_counters(mp, field, delta, rsvd) \
	xfs_mod_incore_sb(mp, field, delta, rsvd)
#endif

/* dynamic preallocation free space thresholds, 5% down to 1% */
enum {
	XFS_LOWSP_1_PCNT = 0,
	XFS_LOWSP_2_PCNT,
	XFS_LOWSP_3_PCNT,
	XFS_LOWSP_4_PCNT,
	XFS_LOWSP_5_PCNT,
	XFS_LOWSP_MAX,
};

typedef struct xfs_mount {
	struct super_block	*m_super;
	xfs_tid_t		m_tid;		/* next unused tid for fs */
	struct xfs_ail		*m_ail;		/* fs active log item list */
	xfs_sb_t		m_sb;		/* copy of fs superblock */
	spinlock_t		m_sb_lock;	/* sb counter lock */
	struct xfs_buf		*m_sb_bp;	/* buffer for superblock */
	char			*m_fsname;	/* filesystem name */
	int			m_fsname_len;	/* strlen of fs name */
	char			*m_rtname;	/* realtime device name */
	char			*m_logname;	/* external log device name */
	int			m_bsize;	/* fs logical block size */
	xfs_agnumber_t		m_agfrotor;	/* last ag where space found */
	xfs_agnumber_t		m_agirotor;	/* last ag dir inode alloced */
	spinlock_t		m_agirotor_lock;/* .. and lock protecting it */
	xfs_agnumber_t		m_maxagi;	/* highest inode alloc group */
	uint			m_readio_log;	/* min read size log bytes */
	uint			m_readio_blocks; /* min read size blocks */
	uint			m_writeio_log;	/* min write size log bytes */
	uint			m_writeio_blocks; /* min write size blocks */
	struct log		*m_log;		/* log specific stuff */
	int			m_logbufs;	/* number of log buffers */
	int			m_logbsize;	/* size of each log buffer */
	uint			m_rsumlevels;	/* rt summary levels */
	uint			m_rsumsize;	/* size of rt summary, bytes */
	struct xfs_inode	*m_rbmip;	/* pointer to bitmap inode */
	struct xfs_inode	*m_rsumip;	/* pointer to summary inode */
	struct xfs_inode	*m_rootip;	/* pointer to root directory */
	struct xfs_quotainfo	*m_quotainfo;	/* disk quota information */
	xfs_buftarg_t		*m_ddev_targp;	/* saves taking the address */
	xfs_buftarg_t		*m_logdev_targp;/* ptr to log device */
	xfs_buftarg_t		*m_rtdev_targp;	/* ptr to rt device */
	__uint8_t		m_blkbit_log;	/* blocklog + NBBY */
	__uint8_t		m_blkbb_log;	/* blocklog - BBSHIFT */
	__uint8_t		m_agno_log;	/* log #ag's */
	__uint8_t		m_agino_log;	/* #bits for agino in inum */
	__uint16_t		m_inode_cluster_size;/* min inode buf size */
	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	uint			m_blockwmask;	/* blockwsize-1 */
	uint			m_alloc_mxr[2];	/* max alloc btree records */
	uint			m_alloc_mnr[2];	/* min alloc btree records */
	uint			m_bmap_dmxr[2];	/* max bmap btree records */
	uint			m_bmap_dmnr[2];	/* min bmap btree records */
	uint			m_inobt_mxr[2];	/* max inobt btree records */
	uint			m_inobt_mnr[2];	/* min inobt btree records */
	uint			m_ag_maxlevels;	/* XFS_AG_MAXLEVELS */
	uint			m_bm_maxlevels[2]; /* XFS_BM_MAXLEVELS */
	uint			m_in_maxlevels;	/* max inobt btree levels. */
	struct radix_tree_root	m_perag_tree;	/* per-ag accounting info */
	spinlock_t		m_perag_lock;	/* lock for m_perag_tree */
	struct mutex		m_growlock;	/* growfs mutex */
	int			m_fixedfsid[2];	/* unchanged for life of FS */
	uint			m_dmevmask;	/* DMI events for this FS */
	__uint64_t		m_flags;	/* global mount flags */
	uint			m_dir_node_ents; /* #entries in a dir danode */
	uint			m_attr_node_ents; /* #entries in attr danode */
	int			m_ialloc_inos;	/* inodes in inode allocation */
	int			m_ialloc_blks;	/* blocks in inode allocation */
	int			m_inoalign_mask;/* mask sb_inoalignmt if used */
	uint			m_qflags;	/* quota status flags */
	xfs_trans_reservations_t m_reservations;/* precomputed res values */
	__uint64_t		m_maxicount;	/* maximum inode count */
	__uint64_t		m_maxioffset;	/* maximum inode offset */
	__uint64_t		m_resblks;	/* total reserved blocks */
	__uint64_t		m_resblks_avail;/* available reserved blocks */
	__uint64_t		m_resblks_save;	/* reserved blks @ remount,ro */
	int			m_dalign;	/* stripe unit */
	int			m_swidth;	/* stripe width */
	int			m_sinoalign;	/* stripe unit inode alignment */
	int			m_attr_magicpct;/* 37% of the blocksize */
	int			m_dir_magicpct;	/* 37% of the dir blocksize */
	__uint8_t		m_sectbb_log;	/* sectlog - BBSHIFT */
	const struct xfs_nameops *m_dirnameops;	/* vector of dir name ops */
	int			m_dirblksize;	/* directory block sz--bytes */
	int			m_dirblkfsbs;	/* directory block sz--fsbs */
	xfs_dablk_t		m_dirdatablk;	/* blockno of dir data v2 */
	xfs_dablk_t		m_dirleafblk;	/* blockno of dir non-data v2 */
	xfs_dablk_t		m_dirfreeblk;	/* blockno of dirfreeindex v2 */
	uint			m_chsize;	/* size of next field */
	struct xfs_chash	*m_chash;	/* fs private inode per-cluster
						 * hash table */
	atomic_t		m_active_trans;	/* number trans frozen */
#ifdef HAVE_PERCPU_SB
	xfs_icsb_cnts_t __percpu *m_sb_cnts;	/* per-cpu superblock counters */
	unsigned long		m_icsb_counters; /* disabled per-cpu counters */
	struct notifier_block	m_icsb_notifier; /* hotplug cpu notifier */
	struct mutex		m_icsb_mutex;	/* balancer sync lock */
#endif
	struct xfs_mru_cache	*m_filestream;  /* per-mount filestream data */
	struct delayed_work	m_sync_work;	/* background sync work */
	struct delayed_work	m_reclaim_work;	/* background inode reclaim */
	struct work_struct	m_flush_work;	/* background inode flush */
	__int64_t		m_update_flags;	/* sb flags we need to update
						   on the next remount,rw */
	struct shrinker		m_inode_shrink;	/* inode reclaim shrinker */
	int64_t			m_low_space[XFS_LOWSP_MAX];
						/* low free space thresholds */

	struct workqueue_struct	*m_data_workqueue;
	struct workqueue_struct	*m_unwritten_workqueue;
} xfs_mount_t;

/*
 * Flags for m_flags.
 */
#define XFS_MOUNT_WSYNC		(1ULL << 0)	/* for nfs - all metadata ops
						   must be synchronous except
						   for space allocations */
#define XFS_MOUNT_WAS_CLEAN	(1ULL << 3)
#define XFS_MOUNT_FS_SHUTDOWN	(1ULL << 4)	/* atomic stop of all filesystem
						   operations, typically for
						   disk errors in metadata */
#define XFS_MOUNT_DISCARD	(1ULL << 5)	/* discard unused blocks */
#define XFS_MOUNT_RETERR	(1ULL << 6)     /* return alignment errors to
						   user */
#define XFS_MOUNT_NOALIGN	(1ULL << 7)	/* turn off stripe alignment
						   allocations */
#define XFS_MOUNT_ATTR2		(1ULL << 8)	/* allow use of attr2 format */
#define XFS_MOUNT_GRPID		(1ULL << 9)	/* group-ID assigned from directory */
#define XFS_MOUNT_NORECOVERY	(1ULL << 10)	/* no recovery - dirty fs */
#define XFS_MOUNT_DFLT_IOSIZE	(1ULL << 12)	/* set default i/o size */
#define XFS_MOUNT_32BITINODES	(1ULL << 14)	/* do not create inodes above
						 * 32 bits in size */
#define XFS_MOUNT_SMALL_INUMS	(1ULL << 15)	/* users wants 32bit inodes */
#define XFS_MOUNT_NOUUID	(1ULL << 16)	/* ignore uuid during mount */
#define XFS_MOUNT_BARRIER	(1ULL << 17)
#define XFS_MOUNT_IKEEP		(1ULL << 18)	/* keep empty inode clusters*/
#define XFS_MOUNT_SWALLOC	(1ULL << 19)	/* turn on stripe width
						 * allocation */
#define XFS_MOUNT_RDONLY	(1ULL << 20)	/* read-only fs */
#define XFS_MOUNT_DIRSYNC	(1ULL << 21)	/* synchronous directory ops */
#define XFS_MOUNT_COMPAT_IOSIZE	(1ULL << 22)	/* don't report large preferred
						 * I/O size in stat() */
#define XFS_MOUNT_FILESTREAMS	(1ULL << 24)	/* enable the filestreams
						   allocator */
#define XFS_MOUNT_NOATTR2	(1ULL << 25)	/* disable use of attr2 format */


/*
 * Default minimum read and write sizes.
 */
#define XFS_READIO_LOG_LARGE	16
#define XFS_WRITEIO_LOG_LARGE	16

/*
 * Max and min values for mount-option defined I/O
 * preallocation sizes.
 */
#define XFS_MAX_IO_LOG		30	/* 1G */
#define XFS_MIN_IO_LOG		PAGE_SHIFT

/*
 * Synchronous read and write sizes.  This should be
 * better for NFSv2 wsync filesystems.
 */
#define	XFS_WSYNC_READIO_LOG	15	/* 32k */
#define	XFS_WSYNC_WRITEIO_LOG	14	/* 16k */

/*
 * Allow large block sizes to be reported to userspace programs if the
 * "largeio" mount option is used.
 *
 * If compatibility mode is specified, simply return the basic unit of caching
 * so that we don't get inefficient read/modify/write I/O from user apps.
 * Otherwise....
 *
 * If the underlying volume is a stripe, then return the stripe width in bytes
 * as the recommended I/O size. It is not a stripe and we've set a default
 * buffered I/O size, return that, otherwise return the compat default.
 */
static inline unsigned long
xfs_preferred_iosize(xfs_mount_t *mp)
{
	if (mp->m_flags & XFS_MOUNT_COMPAT_IOSIZE)
		return PAGE_CACHE_SIZE;
	return (mp->m_swidth ?
		(mp->m_swidth << mp->m_sb.sb_blocklog) :
		((mp->m_flags & XFS_MOUNT_DFLT_IOSIZE) ?
			(1 << (int)MAX(mp->m_readio_log, mp->m_writeio_log)) :
			PAGE_CACHE_SIZE));
}

#define XFS_MAXIOFFSET(mp)	((mp)->m_maxioffset)

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
#define SHUTDOWN_REMOTE_REQ	0x0010	/* shutdown came from remote cell */
#define SHUTDOWN_DEVICE_REQ	0x0020	/* failed all paths to the device */

#define xfs_test_for_freeze(mp)		((mp)->m_super->s_frozen)
#define xfs_wait_for_freeze(mp,l)	vfs_check_frozen((mp)->m_super, (l))

/*
 * Flags for xfs_mountfs
 */
#define XFS_MFSI_QUIET		0x40	/* Be silent if mount errors found */

static inline xfs_agnumber_t
xfs_daddr_to_agno(struct xfs_mount *mp, xfs_daddr_t d)
{
	xfs_daddr_t ld = XFS_BB_TO_FSBT(mp, d);
	do_div(ld, mp->m_sb.sb_agblocks);
	return (xfs_agnumber_t) ld;
}

static inline xfs_agblock_t
xfs_daddr_to_agbno(struct xfs_mount *mp, xfs_daddr_t d)
{
	xfs_daddr_t ld = XFS_BB_TO_FSBT(mp, d);
	return (xfs_agblock_t) do_div(ld, mp->m_sb.sb_agblocks);
}

/*
 * perag get/put wrappers for ref counting
 */
struct xfs_perag *xfs_perag_get(struct xfs_mount *mp, xfs_agnumber_t agno);
struct xfs_perag *xfs_perag_get_tag(struct xfs_mount *mp, xfs_agnumber_t agno,
					int tag);
void	xfs_perag_put(struct xfs_perag *pag);

/*
 * Per-cpu superblock locking functions
 */
#ifdef HAVE_PERCPU_SB
static inline void
xfs_icsb_lock(xfs_mount_t *mp)
{
	mutex_lock(&mp->m_icsb_mutex);
}

static inline void
xfs_icsb_unlock(xfs_mount_t *mp)
{
	mutex_unlock(&mp->m_icsb_mutex);
}
#else
#define xfs_icsb_lock(mp)
#define xfs_icsb_unlock(mp)
#endif

/*
 * This structure is for use by the xfs_mod_incore_sb_batch() routine.
 * xfs_growfs can specify a few fields which are more than int limit
 */
typedef struct xfs_mod_sb {
	xfs_sb_field_t	msb_field;	/* Field to modify, see below */
	int64_t		msb_delta;	/* Change to make to specified field */
} xfs_mod_sb_t;

extern int	xfs_log_sbcount(xfs_mount_t *);
extern __uint64_t xfs_default_resblks(xfs_mount_t *mp);
extern int	xfs_mountfs(xfs_mount_t *mp);

extern void	xfs_unmountfs(xfs_mount_t *);
extern int	xfs_mod_incore_sb(xfs_mount_t *, xfs_sb_field_t, int64_t, int);
extern int	xfs_mod_incore_sb_batch(xfs_mount_t *, xfs_mod_sb_t *,
			uint, int);
extern int	xfs_mount_log_sb(xfs_mount_t *, __int64_t);
extern struct xfs_buf *xfs_getsb(xfs_mount_t *, int);
extern int	xfs_readsb(xfs_mount_t *, int);
extern void	xfs_freesb(xfs_mount_t *);
extern int	xfs_fs_writable(xfs_mount_t *);
extern int	xfs_sb_validate_fsb_count(struct xfs_sb *, __uint64_t);

extern int	xfs_dev_is_read_only(struct xfs_mount *, char *);

extern void	xfs_set_low_space_thresholds(struct xfs_mount *);

#endif	/* __KERNEL__ */

extern void	xfs_mod_sb(struct xfs_trans *, __int64_t);
extern int	xfs_initialize_perag(struct xfs_mount *, xfs_agnumber_t,
					xfs_agnumber_t *);
extern void	xfs_sb_from_disk(struct xfs_mount *, struct xfs_dsb *);
extern void	xfs_sb_to_disk(struct xfs_dsb *, struct xfs_sb *, __int64_t);

#endif	/* __XFS_MOUNT_H__ */
