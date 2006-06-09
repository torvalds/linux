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
/*
 * Moved here from xfs_ag.h to avoid reordering header files
 */
#define XFS_DADDR_TO_AGNO(mp,d) \
	((xfs_agnumber_t)(XFS_BB_TO_FSBT(mp, d) / (mp)->m_sb.sb_agblocks))
#define XFS_DADDR_TO_AGBNO(mp,d) \
	((xfs_agblock_t)(XFS_BB_TO_FSBT(mp, d) % (mp)->m_sb.sb_agblocks))
#else
struct cred;
struct log;
struct vfs;
struct vnode;
struct xfs_mount_args;
struct xfs_ihash;
struct xfs_chash;
struct xfs_inode;
struct xfs_perag;
struct xfs_iocore;
struct xfs_bmbt_irec;
struct xfs_bmap_free;
struct xfs_extdelta;
struct xfs_swapext;

extern struct vfsops xfs_vfsops;
extern struct vnodeops xfs_vnodeops;

#define	AIL_LOCK_T		lock_t
#define	AIL_LOCKINIT(x,y)	spinlock_init(x,y)
#define	AIL_LOCK_DESTROY(x)	spinlock_destroy(x)
#define	AIL_LOCK(mp,s)		s=mutex_spinlock(&(mp)->m_ail_lock)
#define	AIL_UNLOCK(mp,s)	mutex_spinunlock(&(mp)->m_ail_lock, s)


/*
 * Prototypes and functions for the Data Migration subsystem.
 */

typedef int	(*xfs_send_data_t)(int, struct vnode *,
			xfs_off_t, size_t, int, vrwlock_t *);
typedef int	(*xfs_send_mmap_t)(struct vm_area_struct *, uint);
typedef int	(*xfs_send_destroy_t)(struct vnode *, dm_right_t);
typedef int	(*xfs_send_namesp_t)(dm_eventtype_t, struct vfs *,
			struct vnode *,
			dm_right_t, struct vnode *, dm_right_t,
			char *, char *, mode_t, int, int);
typedef void	(*xfs_send_unmount_t)(struct vfs *, struct vnode *,
			dm_right_t, mode_t, int, int);

typedef struct xfs_dmops {
	xfs_send_data_t		xfs_send_data;
	xfs_send_mmap_t		xfs_send_mmap;
	xfs_send_destroy_t	xfs_send_destroy;
	xfs_send_namesp_t	xfs_send_namesp;
	xfs_send_unmount_t	xfs_send_unmount;
} xfs_dmops_t;

#define XFS_SEND_DATA(mp, ev,vp,off,len,fl,lock) \
	(*(mp)->m_dm_ops.xfs_send_data)(ev,vp,off,len,fl,lock)
#define XFS_SEND_MMAP(mp, vma,fl) \
	(*(mp)->m_dm_ops.xfs_send_mmap)(vma,fl)
#define XFS_SEND_DESTROY(mp, vp,right) \
	(*(mp)->m_dm_ops.xfs_send_destroy)(vp,right)
#define XFS_SEND_NAMESP(mp, ev,b1,r1,b2,r2,n1,n2,mode,rval,fl) \
	(*(mp)->m_dm_ops.xfs_send_namesp)(ev,NULL,b1,r1,b2,r2,n1,n2,mode,rval,fl)
#define XFS_SEND_PREUNMOUNT(mp, vfs,b1,r1,b2,r2,n1,n2,mode,rval,fl) \
	(*(mp)->m_dm_ops.xfs_send_namesp)(DM_EVENT_PREUNMOUNT,vfs,b1,r1,b2,r2,n1,n2,mode,rval,fl)
#define XFS_SEND_UNMOUNT(mp, vfsp,vp,right,mode,rval,fl) \
	(*(mp)->m_dm_ops.xfs_send_unmount)(vfsp,vp,right,mode,rval,fl)


/*
 * Prototypes and functions for the Quota Management subsystem.
 */

struct xfs_dquot;
struct xfs_dqtrxops;
struct xfs_quotainfo;

typedef int	(*xfs_qminit_t)(struct xfs_mount *, uint *, uint *);
typedef int	(*xfs_qmmount_t)(struct xfs_mount *, uint, uint, int);
typedef int	(*xfs_qmunmount_t)(struct xfs_mount *);
typedef void	(*xfs_qmdone_t)(struct xfs_mount *);
typedef void	(*xfs_dqrele_t)(struct xfs_dquot *);
typedef int	(*xfs_dqattach_t)(struct xfs_inode *, uint);
typedef void	(*xfs_dqdetach_t)(struct xfs_inode *);
typedef int	(*xfs_dqpurgeall_t)(struct xfs_mount *, uint);
typedef int	(*xfs_dqvopalloc_t)(struct xfs_mount *,
			struct xfs_inode *, uid_t, gid_t, prid_t, uint,
			struct xfs_dquot **, struct xfs_dquot **);
typedef void	(*xfs_dqvopcreate_t)(struct xfs_trans *, struct xfs_inode *,
			struct xfs_dquot *, struct xfs_dquot *);
typedef int	(*xfs_dqvoprename_t)(struct xfs_inode **);
typedef struct xfs_dquot * (*xfs_dqvopchown_t)(
			struct xfs_trans *, struct xfs_inode *,
			struct xfs_dquot **, struct xfs_dquot *);
typedef int	(*xfs_dqvopchownresv_t)(struct xfs_trans *, struct xfs_inode *,
			struct xfs_dquot *, struct xfs_dquot *, uint);

typedef struct xfs_qmops {
	xfs_qminit_t		xfs_qminit;
	xfs_qmdone_t		xfs_qmdone;
	xfs_qmmount_t		xfs_qmmount;
	xfs_qmunmount_t		xfs_qmunmount;
	xfs_dqrele_t		xfs_dqrele;
	xfs_dqattach_t		xfs_dqattach;
	xfs_dqdetach_t		xfs_dqdetach;
	xfs_dqpurgeall_t	xfs_dqpurgeall;
	xfs_dqvopalloc_t	xfs_dqvopalloc;
	xfs_dqvopcreate_t	xfs_dqvopcreate;
	xfs_dqvoprename_t	xfs_dqvoprename;
	xfs_dqvopchown_t	xfs_dqvopchown;
	xfs_dqvopchownresv_t	xfs_dqvopchownresv;
	struct xfs_dqtrxops	*xfs_dqtrxops;
} xfs_qmops_t;

#define XFS_QM_INIT(mp, mnt, fl) \
	(*(mp)->m_qm_ops.xfs_qminit)(mp, mnt, fl)
#define XFS_QM_MOUNT(mp, mnt, fl, mfsi_flags) \
	(*(mp)->m_qm_ops.xfs_qmmount)(mp, mnt, fl, mfsi_flags)
#define XFS_QM_UNMOUNT(mp) \
	(*(mp)->m_qm_ops.xfs_qmunmount)(mp)
#define XFS_QM_DONE(mp) \
	(*(mp)->m_qm_ops.xfs_qmdone)(mp)
#define XFS_QM_DQRELE(mp, dq) \
	(*(mp)->m_qm_ops.xfs_dqrele)(dq)
#define XFS_QM_DQATTACH(mp, ip, fl) \
	(*(mp)->m_qm_ops.xfs_dqattach)(ip, fl)
#define XFS_QM_DQDETACH(mp, ip) \
	(*(mp)->m_qm_ops.xfs_dqdetach)(ip)
#define XFS_QM_DQPURGEALL(mp, fl) \
	(*(mp)->m_qm_ops.xfs_dqpurgeall)(mp, fl)
#define XFS_QM_DQVOPALLOC(mp, ip, uid, gid, prid, fl, dq1, dq2) \
	(*(mp)->m_qm_ops.xfs_dqvopalloc)(mp, ip, uid, gid, prid, fl, dq1, dq2)
#define XFS_QM_DQVOPCREATE(mp, tp, ip, dq1, dq2) \
	(*(mp)->m_qm_ops.xfs_dqvopcreate)(tp, ip, dq1, dq2)
#define XFS_QM_DQVOPRENAME(mp, ip) \
	(*(mp)->m_qm_ops.xfs_dqvoprename)(ip)
#define XFS_QM_DQVOPCHOWN(mp, tp, ip, dqp, dq) \
	(*(mp)->m_qm_ops.xfs_dqvopchown)(tp, ip, dqp, dq)
#define XFS_QM_DQVOPCHOWNRESV(mp, tp, ip, dq1, dq2, fl) \
	(*(mp)->m_qm_ops.xfs_dqvopchownresv)(tp, ip, dq1, dq2, fl)


/*
 * Prototypes and functions for I/O core modularization.
 */

typedef int		(*xfs_ioinit_t)(struct vfs *,
				struct xfs_mount_args *, int);
typedef int		(*xfs_bmapi_t)(struct xfs_trans *, void *,
				xfs_fileoff_t, xfs_filblks_t, int,
				xfs_fsblock_t *, xfs_extlen_t,
				struct xfs_bmbt_irec *, int *,
				struct xfs_bmap_free *, struct xfs_extdelta *);
typedef int		(*xfs_bunmapi_t)(struct xfs_trans *,
				void *, xfs_fileoff_t,
				xfs_filblks_t, int, xfs_extnum_t,
				xfs_fsblock_t *, struct xfs_bmap_free *,
				struct xfs_extdelta *, int *);
typedef int		(*xfs_bmap_eof_t)(void *, xfs_fileoff_t, int, int *);
typedef int		(*xfs_iomap_write_direct_t)(
				void *, xfs_off_t, size_t, int,
				struct xfs_bmbt_irec *, int *, int);
typedef int		(*xfs_iomap_write_delay_t)(
				void *, xfs_off_t, size_t, int,
				struct xfs_bmbt_irec *, int *);
typedef int		(*xfs_iomap_write_allocate_t)(
				void *, xfs_off_t, size_t,
				struct xfs_bmbt_irec *, int *);
typedef int		(*xfs_iomap_write_unwritten_t)(
				void *, xfs_off_t, size_t);
typedef uint		(*xfs_lck_map_shared_t)(void *);
typedef void		(*xfs_lock_t)(void *, uint);
typedef void		(*xfs_lock_demote_t)(void *, uint);
typedef int		(*xfs_lock_nowait_t)(void *, uint);
typedef void		(*xfs_unlk_t)(void *, unsigned int);
typedef xfs_fsize_t	(*xfs_size_t)(void *);
typedef xfs_fsize_t	(*xfs_iodone_t)(struct vfs *);
typedef int		(*xfs_swap_extents_t)(void *, void *,
				struct xfs_swapext*);

typedef struct xfs_ioops {
	xfs_ioinit_t			xfs_ioinit;
	xfs_bmapi_t			xfs_bmapi_func;
	xfs_bunmapi_t			xfs_bunmapi_func;
	xfs_bmap_eof_t			xfs_bmap_eof_func;
	xfs_iomap_write_direct_t	xfs_iomap_write_direct;
	xfs_iomap_write_delay_t		xfs_iomap_write_delay;
	xfs_iomap_write_allocate_t	xfs_iomap_write_allocate;
	xfs_iomap_write_unwritten_t	xfs_iomap_write_unwritten;
	xfs_lock_t			xfs_ilock;
	xfs_lck_map_shared_t		xfs_lck_map_shared;
	xfs_lock_demote_t		xfs_ilock_demote;
	xfs_lock_nowait_t		xfs_ilock_nowait;
	xfs_unlk_t			xfs_unlock;
	xfs_size_t			xfs_size_func;
	xfs_iodone_t			xfs_iodone;
	xfs_swap_extents_t		xfs_swap_extents_func;
} xfs_ioops_t;

#define XFS_IOINIT(vfsp, args, flags) \
	(*(mp)->m_io_ops.xfs_ioinit)(vfsp, args, flags)
#define XFS_BMAPI(mp, trans,io,bno,len,f,first,tot,mval,nmap,flist,delta) \
	(*(mp)->m_io_ops.xfs_bmapi_func) \
		(trans,(io)->io_obj,bno,len,f,first,tot,mval,nmap,flist,delta)
#define XFS_BUNMAPI(mp, trans,io,bno,len,f,nexts,first,flist,delta,done) \
	(*(mp)->m_io_ops.xfs_bunmapi_func) \
		(trans,(io)->io_obj,bno,len,f,nexts,first,flist,delta,done)
#define XFS_BMAP_EOF(mp, io, endoff, whichfork, eof) \
	(*(mp)->m_io_ops.xfs_bmap_eof_func) \
		((io)->io_obj, endoff, whichfork, eof)
#define XFS_IOMAP_WRITE_DIRECT(mp, io, offset, count, flags, mval, nmap, found)\
	(*(mp)->m_io_ops.xfs_iomap_write_direct) \
		((io)->io_obj, offset, count, flags, mval, nmap, found)
#define XFS_IOMAP_WRITE_DELAY(mp, io, offset, count, flags, mval, nmap) \
	(*(mp)->m_io_ops.xfs_iomap_write_delay) \
		((io)->io_obj, offset, count, flags, mval, nmap)
#define XFS_IOMAP_WRITE_ALLOCATE(mp, io, offset, count, mval, nmap) \
	(*(mp)->m_io_ops.xfs_iomap_write_allocate) \
		((io)->io_obj, offset, count, mval, nmap)
#define XFS_IOMAP_WRITE_UNWRITTEN(mp, io, offset, count) \
	(*(mp)->m_io_ops.xfs_iomap_write_unwritten) \
		((io)->io_obj, offset, count)
#define XFS_LCK_MAP_SHARED(mp, io) \
	(*(mp)->m_io_ops.xfs_lck_map_shared)((io)->io_obj)
#define XFS_ILOCK(mp, io, mode) \
	(*(mp)->m_io_ops.xfs_ilock)((io)->io_obj, mode)
#define XFS_ILOCK_NOWAIT(mp, io, mode) \
	(*(mp)->m_io_ops.xfs_ilock_nowait)((io)->io_obj, mode)
#define XFS_IUNLOCK(mp, io, mode) \
	(*(mp)->m_io_ops.xfs_unlock)((io)->io_obj, mode)
#define XFS_ILOCK_DEMOTE(mp, io, mode) \
	(*(mp)->m_io_ops.xfs_ilock_demote)((io)->io_obj, mode)
#define XFS_SIZE(mp, io) \
	(*(mp)->m_io_ops.xfs_size_func)((io)->io_obj)
#define XFS_IODONE(vfsp) \
	(*(mp)->m_io_ops.xfs_iodone)(vfsp)
#define XFS_SWAP_EXTENTS(mp, io, tio, sxp) \
	(*(mp)->m_io_ops.xfs_swap_extents_func) \
		((io)->io_obj, (tio)->io_obj, sxp)

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

#define XFS_ICSB_SB_LOCKED	(1 << 0)	/* sb already locked */
#define XFS_ICSB_LAZY_COUNT	(1 << 1)	/* accuracy not needed */

extern int	xfs_icsb_init_counters(struct xfs_mount *);
extern void	xfs_icsb_sync_counters_lazy(struct xfs_mount *);

#else
#define xfs_icsb_init_counters(mp)	(0)
#define xfs_icsb_sync_counters_lazy(mp)	do { } while (0)
#endif

typedef struct xfs_mount {
	bhv_desc_t		m_bhv;		/* vfs xfs behavior */
	xfs_tid_t		m_tid;		/* next unused tid for fs */
	AIL_LOCK_T		m_ail_lock;	/* fs AIL mutex */
	xfs_ail_entry_t		m_ail;		/* fs active log item list */
	uint			m_ail_gen;	/* fs AIL generation count */
	xfs_sb_t		m_sb;		/* copy of fs superblock */
	lock_t			m_sb_lock;	/* sb counter mutex */
	struct xfs_buf		*m_sb_bp;	/* buffer for superblock */
	char			*m_fsname;	/* filesystem name */
	int			m_fsname_len;	/* strlen of fs name */
	char			*m_rtname;	/* realtime device name */
	char			*m_logname;	/* external log device name */
	int			m_bsize;	/* fs logical block size */
	xfs_agnumber_t		m_agfrotor;	/* last ag where space found */
	xfs_agnumber_t		m_agirotor;	/* last ag dir inode alloced */
	lock_t			m_agirotor_lock;/* .. and lock protecting it */
	xfs_agnumber_t		m_maxagi;	/* highest inode alloc group */
	uint			m_ihsize;	/* size of next field */
	struct xfs_ihash	*m_ihash;	/* fs private inode hash table*/
	struct xfs_inode	*m_inodes;	/* active inode list */
	struct list_head	m_del_inodes;	/* inodes to reclaim */
	mutex_t			m_ilock;	/* inode list mutex */
	uint			m_ireclaims;	/* count of calls to reclaim*/
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
	__uint8_t		m_dircook_elog;	/* log d-cookie entry bits */
	__uint8_t		m_blkbit_log;	/* blocklog + NBBY */
	__uint8_t		m_blkbb_log;	/* blocklog - BBSHIFT */
	__uint8_t		m_agno_log;	/* log #ag's */
	__uint8_t		m_agino_log;	/* #bits for agino in inum */
	__uint8_t		m_nreadaheads;	/* #readahead buffers */
	__uint16_t		m_inode_cluster_size;/* min inode buf size */
	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	uint			m_blockwmask;	/* blockwsize-1 */
	uint			m_alloc_mxr[2];	/* XFS_ALLOC_BLOCK_MAXRECS */
	uint			m_alloc_mnr[2];	/* XFS_ALLOC_BLOCK_MINRECS */
	uint			m_bmap_dmxr[2];	/* XFS_BMAP_BLOCK_DMAXRECS */
	uint			m_bmap_dmnr[2];	/* XFS_BMAP_BLOCK_DMINRECS */
	uint			m_inobt_mxr[2];	/* XFS_INOBT_BLOCK_MAXRECS */
	uint			m_inobt_mnr[2];	/* XFS_INOBT_BLOCK_MINRECS */
	uint			m_ag_maxlevels;	/* XFS_AG_MAXLEVELS */
	uint			m_bm_maxlevels[2]; /* XFS_BM_MAXLEVELS */
	uint			m_in_maxlevels;	/* XFS_IN_MAXLEVELS */
	struct xfs_perag	*m_perag;	/* per-ag accounting info */
	struct rw_semaphore	m_peraglock;	/* lock for m_perag (pointer) */
	sema_t			m_growlock;	/* growfs mutex */
	int			m_fixedfsid[2];	/* unchanged for life of FS */
	uint			m_dmevmask;	/* DMI events for this FS */
	__uint64_t		m_flags;	/* global mount flags */
	uint			m_attroffset;	/* inode attribute offset */
	uint			m_dir_node_ents; /* #entries in a dir danode */
	uint			m_attr_node_ents; /* #entries in attr danode */
	int			m_ialloc_inos;	/* inodes in inode allocation */
	int			m_ialloc_blks;	/* blocks in inode allocation */
	int			m_litino;	/* size of inode union area */
	int			m_inoalign_mask;/* mask sb_inoalignmt if used */
	uint			m_qflags;	/* quota status flags */
	xfs_trans_reservations_t m_reservations;/* precomputed res values */
	__uint64_t		m_maxicount;	/* maximum inode count */
	__uint64_t		m_maxioffset;	/* maximum inode offset */
	__uint64_t		m_resblks;	/* total reserved blocks */
	__uint64_t		m_resblks_avail;/* available reserved blocks */
#if XFS_BIG_INUMS
	xfs_ino_t		m_inoadd;	/* add value for ino64_offset */
#endif
	int			m_dalign;	/* stripe unit */
	int			m_swidth;	/* stripe width */
	int			m_sinoalign;	/* stripe unit inode alignment */
	int			m_attr_magicpct;/* 37% of the blocksize */
	int			m_dir_magicpct;	/* 37% of the dir blocksize */
	__uint8_t		m_mk_sharedro;	/* mark shared ro on unmount */
	__uint8_t		m_inode_quiesce;/* call quiesce on new inodes.
						   field governed by m_ilock */
	__uint8_t		m_sectbb_log;	/* sectlog - BBSHIFT */
	__uint8_t		m_dirversion;	/* 1 or 2 */
	xfs_dirops_t		m_dirops;	/* table of dir funcs */
	int			m_dirblksize;	/* directory block sz--bytes */
	int			m_dirblkfsbs;	/* directory block sz--fsbs */
	xfs_dablk_t		m_dirdatablk;	/* blockno of dir data v2 */
	xfs_dablk_t		m_dirleafblk;	/* blockno of dir non-data v2 */
	xfs_dablk_t		m_dirfreeblk;	/* blockno of dirfreeindex v2 */
	uint			m_chsize;	/* size of next field */
	struct xfs_chash	*m_chash;	/* fs private inode per-cluster
						 * hash table */
	struct xfs_dmops	m_dm_ops;	/* vector of DMI ops */
	struct xfs_qmops	m_qm_ops;	/* vector of XQM ops */
	struct xfs_ioops	m_io_ops;	/* vector of I/O ops */
	atomic_t		m_active_trans;	/* number trans frozen */
#ifdef HAVE_PERCPU_SB
	xfs_icsb_cnts_t		*m_sb_cnts;	/* per-cpu superblock counters */
	unsigned long		m_icsb_counters; /* disabled per-cpu counters */
	struct notifier_block	m_icsb_notifier; /* hotplug cpu notifier */
#endif
} xfs_mount_t;

/*
 * Flags for m_flags.
 */
#define	XFS_MOUNT_WSYNC		(1ULL << 0)	/* for nfs - all metadata ops
						   must be synchronous except
						   for space allocations */
#define	XFS_MOUNT_INO64		(1ULL << 1)
			     /* (1ULL << 2)	-- currently unused */
			     /* (1ULL << 3)	-- currently unused */
#define XFS_MOUNT_FS_SHUTDOWN	(1ULL << 4)	/* atomic stop of all filesystem
						   operations, typically for
						   disk errors in metadata */
#define XFS_MOUNT_RETERR	(1ULL << 6)     /* return alignment errors to
						   user */
#define XFS_MOUNT_NOALIGN	(1ULL << 7)	/* turn off stripe alignment
						   allocations */
#define XFS_MOUNT_ATTR2		(1ULL << 8)	/* allow use of attr2 format */
			     /*	(1ULL << 9)	-- currently unused */
#define XFS_MOUNT_NORECOVERY	(1ULL << 10)	/* no recovery - dirty fs */
#define XFS_MOUNT_SHARED	(1ULL << 11)	/* shared mount */
#define XFS_MOUNT_DFLT_IOSIZE	(1ULL << 12)	/* set default i/o size */
#define XFS_MOUNT_OSYNCISOSYNC	(1ULL << 13)	/* o_sync is REALLY o_sync */
						/* osyncisdsync is now default*/
#define XFS_MOUNT_32BITINODES	(1ULL << 14)	/* do not create inodes above
						 * 32 bits in size */
			     /* (1ULL << 15)	-- currently unused */
#define XFS_MOUNT_NOUUID	(1ULL << 16)	/* ignore uuid during mount */
#define XFS_MOUNT_BARRIER	(1ULL << 17)
#define XFS_MOUNT_IDELETE	(1ULL << 18)	/* delete empty inode clusters*/
#define XFS_MOUNT_SWALLOC	(1ULL << 19)	/* turn on stripe width
						 * allocation */
#define XFS_MOUNT_IHASHSIZE	(1ULL << 20)	/* inode hash table size */
#define XFS_MOUNT_DIRSYNC	(1ULL << 21)	/* synchronous directory ops */
#define XFS_MOUNT_COMPAT_IOSIZE	(1ULL << 22)	/* don't report large preferred
						 * I/O size in stat() */
#define XFS_MOUNT_NO_PERCPU_SB	(1ULL << 23)	/* don't use per-cpu superblock
						   counters */


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
#define	XFS_WSYNC_READIO_LOG	15	/* 32K */
#define	XFS_WSYNC_WRITEIO_LOG	14	/* 16K */

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

#define XFS_FORCED_SHUTDOWN(mp)	((mp)->m_flags & XFS_MOUNT_FS_SHUTDOWN)
#define xfs_force_shutdown(m,f)	\
	VFS_FORCE_SHUTDOWN((XFS_MTOVFS(m)), f, __FILE__, __LINE__)

/*
 * Flags for xfs_mountfs
 */
#define XFS_MFSI_SECOND		0x01	/* Secondary mount -- skip stuff */
#define XFS_MFSI_CLIENT		0x02	/* Is a client -- skip lots of stuff */
/*	XFS_MFSI_RRINODES	*/
#define XFS_MFSI_NOUNLINK	0x08	/* Skip unlinked inode processing in */
					/* log recovery */
#define XFS_MFSI_NO_QUOTACHECK	0x10	/* Skip quotacheck processing */
/*	XFS_MFSI_CONVERT_SUNIT	*/
#define XFS_MFSI_QUIET		0x40	/* Be silent if mount errors found */

/*
 * Macros for getting from mount to vfs and back.
 */
#define	XFS_MTOVFS(mp)		xfs_mtovfs(mp)
static inline struct vfs *xfs_mtovfs(xfs_mount_t *mp)
{
	return bhvtovfs(&mp->m_bhv);
}

#define	XFS_BHVTOM(bdp)	xfs_bhvtom(bdp)
static inline xfs_mount_t *xfs_bhvtom(bhv_desc_t *bdp)
{
	return (xfs_mount_t *)BHV_PDATA(bdp);
}

#define XFS_VFSTOM(vfs) xfs_vfstom(vfs)
static inline xfs_mount_t *xfs_vfstom(vfs_t *vfs)
{
	return XFS_BHVTOM(bhv_lookup(VFS_BHVHEAD(vfs), &xfs_vfsops));
}

#define XFS_DADDR_TO_AGNO(mp,d)         xfs_daddr_to_agno(mp,d)
static inline xfs_agnumber_t
xfs_daddr_to_agno(struct xfs_mount *mp, xfs_daddr_t d)
{
	xfs_daddr_t ld = XFS_BB_TO_FSBT(mp, d);
	do_div(ld, mp->m_sb.sb_agblocks);
	return (xfs_agnumber_t) ld;
}

#define XFS_DADDR_TO_AGBNO(mp,d)        xfs_daddr_to_agbno(mp,d)
static inline xfs_agblock_t
xfs_daddr_to_agbno(struct xfs_mount *mp, xfs_daddr_t d)
{
	xfs_daddr_t ld = XFS_BB_TO_FSBT(mp, d);
	return (xfs_agblock_t) do_div(ld, mp->m_sb.sb_agblocks);
}

/*
 * This structure is for use by the xfs_mod_incore_sb_batch() routine.
 */
typedef struct xfs_mod_sb {
	xfs_sb_field_t	msb_field;	/* Field to modify, see below */
	int		msb_delta;	/* Change to make to specified field */
} xfs_mod_sb_t;

#define	XFS_MOUNT_ILOCK(mp)	mutex_lock(&((mp)->m_ilock))
#define	XFS_MOUNT_IUNLOCK(mp)	mutex_unlock(&((mp)->m_ilock))
#define	XFS_SB_LOCK(mp)		mutex_spinlock(&(mp)->m_sb_lock)
#define	XFS_SB_UNLOCK(mp,s)	mutex_spinunlock(&(mp)->m_sb_lock,(s))

extern xfs_mount_t *xfs_mount_init(void);
extern void	xfs_mod_sb(xfs_trans_t *, __int64_t);
extern void	xfs_mount_free(xfs_mount_t *mp, int remove_bhv);
extern int	xfs_mountfs(struct vfs *, xfs_mount_t *mp, int);
extern void	xfs_mountfs_check_barriers(xfs_mount_t *mp);

extern int	xfs_unmountfs(xfs_mount_t *, struct cred *);
extern void	xfs_unmountfs_close(xfs_mount_t *, struct cred *);
extern int	xfs_unmountfs_writesb(xfs_mount_t *);
extern int	xfs_unmount_flush(xfs_mount_t *, int);
extern int	xfs_mod_incore_sb(xfs_mount_t *, xfs_sb_field_t, int, int);
extern int	xfs_mod_incore_sb_unlocked(xfs_mount_t *, xfs_sb_field_t,
			int, int);
extern int	xfs_mod_incore_sb_batch(xfs_mount_t *, xfs_mod_sb_t *,
			uint, int);
extern struct xfs_buf *xfs_getsb(xfs_mount_t *, int);
extern int	xfs_readsb(xfs_mount_t *, int);
extern void	xfs_freesb(xfs_mount_t *);
extern void	xfs_do_force_shutdown(bhv_desc_t *, int, char *, int);
extern int	xfs_syncsub(xfs_mount_t *, int, int, int *);
extern int	xfs_sync_inodes(xfs_mount_t *, int, int, int *);
extern xfs_agnumber_t	xfs_initialize_perag(struct vfs *, xfs_mount_t *,
						xfs_agnumber_t);
extern void	xfs_xlatesb(void *, struct xfs_sb *, int, __int64_t);

extern struct xfs_dmops xfs_dmcore_stub;
extern struct xfs_qmops xfs_qmcore_stub;
extern struct xfs_ioops xfs_iocore_xfs;

extern int	xfs_init(void);
extern void	xfs_cleanup(void);

#endif	/* __KERNEL__ */

#endif	/* __XFS_MOUNT_H__ */
