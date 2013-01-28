/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#ifndef	__XFS_TRANS_H__
#define	__XFS_TRANS_H__

struct xfs_log_item;

/*
 * This is the structure written in the log at the head of
 * every transaction. It identifies the type and id of the
 * transaction, and contains the number of items logged by
 * the transaction so we know how many to expect during recovery.
 *
 * Do not change the below structure without redoing the code in
 * xlog_recover_add_to_trans() and xlog_recover_add_to_cont_trans().
 */
typedef struct xfs_trans_header {
	uint		th_magic;		/* magic number */
	uint		th_type;		/* transaction type */
	__int32_t	th_tid;			/* transaction id (unused) */
	uint		th_num_items;		/* num items logged by trans */
} xfs_trans_header_t;

#define	XFS_TRANS_HEADER_MAGIC	0x5452414e	/* TRAN */

/*
 * Log item types.
 */
#define	XFS_LI_EFI		0x1236
#define	XFS_LI_EFD		0x1237
#define	XFS_LI_IUNLINK		0x1238
#define	XFS_LI_INODE		0x123b	/* aligned ino chunks, var-size ibufs */
#define	XFS_LI_BUF		0x123c	/* v2 bufs, variable sized inode bufs */
#define	XFS_LI_DQUOT		0x123d
#define	XFS_LI_QUOTAOFF		0x123e

#define XFS_LI_TYPE_DESC \
	{ XFS_LI_EFI,		"XFS_LI_EFI" }, \
	{ XFS_LI_EFD,		"XFS_LI_EFD" }, \
	{ XFS_LI_IUNLINK,	"XFS_LI_IUNLINK" }, \
	{ XFS_LI_INODE,		"XFS_LI_INODE" }, \
	{ XFS_LI_BUF,		"XFS_LI_BUF" }, \
	{ XFS_LI_DQUOT,		"XFS_LI_DQUOT" }, \
	{ XFS_LI_QUOTAOFF,	"XFS_LI_QUOTAOFF" }

/*
 * Transaction types.  Used to distinguish types of buffers.
 */
#define XFS_TRANS_SETATTR_NOT_SIZE	1
#define XFS_TRANS_SETATTR_SIZE		2
#define XFS_TRANS_INACTIVE		3
#define XFS_TRANS_CREATE		4
#define XFS_TRANS_CREATE_TRUNC		5
#define XFS_TRANS_TRUNCATE_FILE		6
#define XFS_TRANS_REMOVE		7
#define XFS_TRANS_LINK			8
#define XFS_TRANS_RENAME		9
#define XFS_TRANS_MKDIR			10
#define XFS_TRANS_RMDIR			11
#define XFS_TRANS_SYMLINK		12
#define XFS_TRANS_SET_DMATTRS		13
#define XFS_TRANS_GROWFS		14
#define XFS_TRANS_STRAT_WRITE		15
#define XFS_TRANS_DIOSTRAT		16
/* 17 was XFS_TRANS_WRITE_SYNC */
#define	XFS_TRANS_WRITEID		18
#define	XFS_TRANS_ADDAFORK		19
#define	XFS_TRANS_ATTRINVAL		20
#define	XFS_TRANS_ATRUNCATE		21
#define	XFS_TRANS_ATTR_SET		22
#define	XFS_TRANS_ATTR_RM		23
#define	XFS_TRANS_ATTR_FLAG		24
#define	XFS_TRANS_CLEAR_AGI_BUCKET	25
#define XFS_TRANS_QM_SBCHANGE		26
/*
 * Dummy entries since we use the transaction type to index into the
 * trans_type[] in xlog_recover_print_trans_head()
 */
#define XFS_TRANS_DUMMY1		27
#define XFS_TRANS_DUMMY2		28
#define XFS_TRANS_QM_QUOTAOFF		29
#define XFS_TRANS_QM_DQALLOC		30
#define XFS_TRANS_QM_SETQLIM		31
#define XFS_TRANS_QM_DQCLUSTER		32
#define XFS_TRANS_QM_QINOCREATE		33
#define XFS_TRANS_QM_QUOTAOFF_END	34
#define XFS_TRANS_SB_UNIT		35
#define XFS_TRANS_FSYNC_TS		36
#define	XFS_TRANS_GROWFSRT_ALLOC	37
#define	XFS_TRANS_GROWFSRT_ZERO		38
#define	XFS_TRANS_GROWFSRT_FREE		39
#define	XFS_TRANS_SWAPEXT		40
#define	XFS_TRANS_SB_COUNT		41
#define	XFS_TRANS_CHECKPOINT		42
#define	XFS_TRANS_TYPE_MAX		42
/* new transaction types need to be reflected in xfs_logprint(8) */

#define XFS_TRANS_TYPES \
	{ XFS_TRANS_SETATTR_NOT_SIZE,	"SETATTR_NOT_SIZE" }, \
	{ XFS_TRANS_SETATTR_SIZE,	"SETATTR_SIZE" }, \
	{ XFS_TRANS_INACTIVE,		"INACTIVE" }, \
	{ XFS_TRANS_CREATE,		"CREATE" }, \
	{ XFS_TRANS_CREATE_TRUNC,	"CREATE_TRUNC" }, \
	{ XFS_TRANS_TRUNCATE_FILE,	"TRUNCATE_FILE" }, \
	{ XFS_TRANS_REMOVE,		"REMOVE" }, \
	{ XFS_TRANS_LINK,		"LINK" }, \
	{ XFS_TRANS_RENAME,		"RENAME" }, \
	{ XFS_TRANS_MKDIR,		"MKDIR" }, \
	{ XFS_TRANS_RMDIR,		"RMDIR" }, \
	{ XFS_TRANS_SYMLINK,		"SYMLINK" }, \
	{ XFS_TRANS_SET_DMATTRS,	"SET_DMATTRS" }, \
	{ XFS_TRANS_GROWFS,		"GROWFS" }, \
	{ XFS_TRANS_STRAT_WRITE,	"STRAT_WRITE" }, \
	{ XFS_TRANS_DIOSTRAT,		"DIOSTRAT" }, \
	{ XFS_TRANS_WRITEID,		"WRITEID" }, \
	{ XFS_TRANS_ADDAFORK,		"ADDAFORK" }, \
	{ XFS_TRANS_ATTRINVAL,		"ATTRINVAL" }, \
	{ XFS_TRANS_ATRUNCATE,		"ATRUNCATE" }, \
	{ XFS_TRANS_ATTR_SET,		"ATTR_SET" }, \
	{ XFS_TRANS_ATTR_RM,		"ATTR_RM" }, \
	{ XFS_TRANS_ATTR_FLAG,		"ATTR_FLAG" }, \
	{ XFS_TRANS_CLEAR_AGI_BUCKET,	"CLEAR_AGI_BUCKET" }, \
	{ XFS_TRANS_QM_SBCHANGE,	"QM_SBCHANGE" }, \
	{ XFS_TRANS_QM_QUOTAOFF,	"QM_QUOTAOFF" }, \
	{ XFS_TRANS_QM_DQALLOC,		"QM_DQALLOC" }, \
	{ XFS_TRANS_QM_SETQLIM,		"QM_SETQLIM" }, \
	{ XFS_TRANS_QM_DQCLUSTER,	"QM_DQCLUSTER" }, \
	{ XFS_TRANS_QM_QINOCREATE,	"QM_QINOCREATE" }, \
	{ XFS_TRANS_QM_QUOTAOFF_END,	"QM_QOFF_END" }, \
	{ XFS_TRANS_SB_UNIT,		"SB_UNIT" }, \
	{ XFS_TRANS_FSYNC_TS,		"FSYNC_TS" }, \
	{ XFS_TRANS_GROWFSRT_ALLOC,	"GROWFSRT_ALLOC" }, \
	{ XFS_TRANS_GROWFSRT_ZERO,	"GROWFSRT_ZERO" }, \
	{ XFS_TRANS_GROWFSRT_FREE,	"GROWFSRT_FREE" }, \
	{ XFS_TRANS_SWAPEXT,		"SWAPEXT" }, \
	{ XFS_TRANS_SB_COUNT,		"SB_COUNT" }, \
	{ XFS_TRANS_CHECKPOINT,		"CHECKPOINT" }, \
	{ XFS_TRANS_DUMMY1,		"DUMMY1" }, \
	{ XFS_TRANS_DUMMY2,		"DUMMY2" }, \
	{ XLOG_UNMOUNT_REC_TYPE,	"UNMOUNT" }

/*
 * This structure is used to track log items associated with
 * a transaction.  It points to the log item and keeps some
 * flags to track the state of the log item.  It also tracks
 * the amount of space needed to log the item it describes
 * once we get to commit processing (see xfs_trans_commit()).
 */
struct xfs_log_item_desc {
	struct xfs_log_item	*lid_item;
	struct list_head	lid_trans;
	unsigned char		lid_flags;
};

#define XFS_LID_DIRTY		0x1

#define	XFS_TRANS_MAGIC		0x5452414E	/* 'TRAN' */
/*
 * Values for t_flags.
 */
#define	XFS_TRANS_DIRTY		0x01	/* something needs to be logged */
#define	XFS_TRANS_SB_DIRTY	0x02	/* superblock is modified */
#define	XFS_TRANS_PERM_LOG_RES	0x04	/* xact took a permanent log res */
#define	XFS_TRANS_SYNC		0x08	/* make commit synchronous */
#define XFS_TRANS_DQ_DIRTY	0x10	/* at least one dquot in trx dirty */
#define XFS_TRANS_RESERVE	0x20    /* OK to use reserved data blocks */
#define XFS_TRANS_FREEZE_PROT	0x40	/* Transaction has elevated writer
					   count in superblock */

/*
 * Values for call flags parameter.
 */
#define	XFS_TRANS_RELEASE_LOG_RES	0x4
#define	XFS_TRANS_ABORT			0x8

/*
 * Field values for xfs_trans_mod_sb.
 */
#define	XFS_TRANS_SB_ICOUNT		0x00000001
#define	XFS_TRANS_SB_IFREE		0x00000002
#define	XFS_TRANS_SB_FDBLOCKS		0x00000004
#define	XFS_TRANS_SB_RES_FDBLOCKS	0x00000008
#define	XFS_TRANS_SB_FREXTENTS		0x00000010
#define	XFS_TRANS_SB_RES_FREXTENTS	0x00000020
#define	XFS_TRANS_SB_DBLOCKS		0x00000040
#define	XFS_TRANS_SB_AGCOUNT		0x00000080
#define	XFS_TRANS_SB_IMAXPCT		0x00000100
#define	XFS_TRANS_SB_REXTSIZE		0x00000200
#define	XFS_TRANS_SB_RBMBLOCKS		0x00000400
#define	XFS_TRANS_SB_RBLOCKS		0x00000800
#define	XFS_TRANS_SB_REXTENTS		0x00001000
#define	XFS_TRANS_SB_REXTSLOG		0x00002000


/*
 * Per-extent log reservation for the allocation btree changes
 * involved in freeing or allocating an extent.
 * 2 trees * (2 blocks/level * max depth - 1) * block size
 */
#define	XFS_ALLOCFREE_LOG_RES(mp,nx) \
	((nx) * (2 * XFS_FSB_TO_B((mp), 2 * XFS_AG_MAXLEVELS(mp) - 1)))
#define	XFS_ALLOCFREE_LOG_COUNT(mp,nx) \
	((nx) * (2 * (2 * XFS_AG_MAXLEVELS(mp) - 1)))

/*
 * Per-directory log reservation for any directory change.
 * dir blocks: (1 btree block per level + data block + free block) * dblock size
 * bmap btree: (levels + 2) * max depth * block size
 * v2 directory blocks can be fragmented below the dirblksize down to the fsb
 * size, so account for that in the DAENTER macros.
 */
#define	XFS_DIROP_LOG_RES(mp)	\
	(XFS_FSB_TO_B(mp, XFS_DAENTER_BLOCKS(mp, XFS_DATA_FORK)) + \
	 (XFS_FSB_TO_B(mp, XFS_DAENTER_BMAPS(mp, XFS_DATA_FORK) + 1)))
#define	XFS_DIROP_LOG_COUNT(mp)	\
	(XFS_DAENTER_BLOCKS(mp, XFS_DATA_FORK) + \
	 XFS_DAENTER_BMAPS(mp, XFS_DATA_FORK) + 1)


#define	XFS_WRITE_LOG_RES(mp)	((mp)->m_reservations.tr_write)
#define	XFS_ITRUNCATE_LOG_RES(mp)   ((mp)->m_reservations.tr_itruncate)
#define	XFS_RENAME_LOG_RES(mp)	((mp)->m_reservations.tr_rename)
#define	XFS_LINK_LOG_RES(mp)	((mp)->m_reservations.tr_link)
#define	XFS_REMOVE_LOG_RES(mp)	((mp)->m_reservations.tr_remove)
#define	XFS_SYMLINK_LOG_RES(mp)	((mp)->m_reservations.tr_symlink)
#define	XFS_CREATE_LOG_RES(mp)	((mp)->m_reservations.tr_create)
#define	XFS_MKDIR_LOG_RES(mp)	((mp)->m_reservations.tr_mkdir)
#define	XFS_IFREE_LOG_RES(mp)	((mp)->m_reservations.tr_ifree)
#define	XFS_ICHANGE_LOG_RES(mp)	((mp)->m_reservations.tr_ichange)
#define	XFS_GROWDATA_LOG_RES(mp)    ((mp)->m_reservations.tr_growdata)
#define	XFS_GROWRTALLOC_LOG_RES(mp)	((mp)->m_reservations.tr_growrtalloc)
#define	XFS_GROWRTZERO_LOG_RES(mp)	((mp)->m_reservations.tr_growrtzero)
#define	XFS_GROWRTFREE_LOG_RES(mp)	((mp)->m_reservations.tr_growrtfree)
#define	XFS_SWRITE_LOG_RES(mp)	((mp)->m_reservations.tr_swrite)
/*
 * Logging the inode timestamps on an fsync -- same as SWRITE
 * as long as SWRITE logs the entire inode core
 */
#define XFS_FSYNC_TS_LOG_RES(mp)        ((mp)->m_reservations.tr_swrite)
#define	XFS_WRITEID_LOG_RES(mp)	((mp)->m_reservations.tr_swrite)
#define	XFS_ADDAFORK_LOG_RES(mp)	((mp)->m_reservations.tr_addafork)
#define	XFS_ATTRINVAL_LOG_RES(mp)	((mp)->m_reservations.tr_attrinval)
#define	XFS_ATTRSET_LOG_RES(mp, ext)	\
	((mp)->m_reservations.tr_attrset + \
	 (ext * (mp)->m_sb.sb_sectsize) + \
	 (ext * XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK))) + \
	 (128 * (ext + (ext * XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)))))
#define	XFS_ATTRRM_LOG_RES(mp)	((mp)->m_reservations.tr_attrrm)
#define	XFS_CLEAR_AGI_BUCKET_LOG_RES(mp)  ((mp)->m_reservations.tr_clearagi)
#define XFS_QM_SBCHANGE_LOG_RES(mp)	((mp)->m_reservations.tr_qm_sbchange)
#define XFS_QM_SETQLIM_LOG_RES(mp)	((mp)->m_reservations.tr_qm_setqlim)
#define XFS_QM_DQALLOC_LOG_RES(mp)	((mp)->m_reservations.tr_qm_dqalloc)
#define XFS_QM_QUOTAOFF_LOG_RES(mp)	((mp)->m_reservations.tr_qm_quotaoff)
#define XFS_QM_QUOTAOFF_END_LOG_RES(mp)	((mp)->m_reservations.tr_qm_equotaoff)
#define XFS_SB_LOG_RES(mp)		((mp)->m_reservations.tr_sb)

/*
 * Various log count values.
 */
#define	XFS_DEFAULT_LOG_COUNT		1
#define	XFS_DEFAULT_PERM_LOG_COUNT	2
#define	XFS_ITRUNCATE_LOG_COUNT		2
#define XFS_INACTIVE_LOG_COUNT		2
#define	XFS_CREATE_LOG_COUNT		2
#define	XFS_MKDIR_LOG_COUNT		3
#define	XFS_SYMLINK_LOG_COUNT		3
#define	XFS_REMOVE_LOG_COUNT		2
#define	XFS_LINK_LOG_COUNT		2
#define	XFS_RENAME_LOG_COUNT		2
#define	XFS_WRITE_LOG_COUNT		2
#define	XFS_ADDAFORK_LOG_COUNT		2
#define	XFS_ATTRINVAL_LOG_COUNT		1
#define	XFS_ATTRSET_LOG_COUNT		3
#define	XFS_ATTRRM_LOG_COUNT		3

/*
 * Here we centralize the specification of XFS meta-data buffer
 * reference count values.  This determine how hard the buffer
 * cache tries to hold onto the buffer.
 */
#define	XFS_AGF_REF		4
#define	XFS_AGI_REF		4
#define	XFS_AGFL_REF		3
#define	XFS_INO_BTREE_REF	3
#define	XFS_ALLOC_BTREE_REF	2
#define	XFS_BMAP_BTREE_REF	2
#define	XFS_DIR_BTREE_REF	2
#define	XFS_INO_REF		2
#define	XFS_ATTR_BTREE_REF	1
#define	XFS_DQUOT_REF		1

#ifdef __KERNEL__

struct xfs_buf;
struct xfs_buftarg;
struct xfs_efd_log_item;
struct xfs_efi_log_item;
struct xfs_inode;
struct xfs_item_ops;
struct xfs_log_iovec;
struct xfs_log_item_desc;
struct xfs_mount;
struct xfs_trans;
struct xfs_dquot_acct;
struct xfs_busy_extent;

typedef struct xfs_log_item {
	struct list_head		li_ail;		/* AIL pointers */
	xfs_lsn_t			li_lsn;		/* last on-disk lsn */
	struct xfs_log_item_desc	*li_desc;	/* ptr to current desc*/
	struct xfs_mount		*li_mountp;	/* ptr to fs mount */
	struct xfs_ail			*li_ailp;	/* ptr to AIL */
	uint				li_type;	/* item type */
	uint				li_flags;	/* misc flags */
	struct xfs_log_item		*li_bio_list;	/* buffer item list */
	void				(*li_cb)(struct xfs_buf *,
						 struct xfs_log_item *);
							/* buffer item iodone */
							/* callback func */
	const struct xfs_item_ops	*li_ops;	/* function list */

	/* delayed logging */
	struct list_head		li_cil;		/* CIL pointers */
	struct xfs_log_vec		*li_lv;		/* active log vector */
	xfs_lsn_t			li_seq;		/* CIL commit seq */
} xfs_log_item_t;

#define	XFS_LI_IN_AIL	0x1
#define XFS_LI_ABORTED	0x2

#define XFS_LI_FLAGS \
	{ XFS_LI_IN_AIL,	"IN_AIL" }, \
	{ XFS_LI_ABORTED,	"ABORTED" }

struct xfs_item_ops {
	uint (*iop_size)(xfs_log_item_t *);
	void (*iop_format)(xfs_log_item_t *, struct xfs_log_iovec *);
	void (*iop_pin)(xfs_log_item_t *);
	void (*iop_unpin)(xfs_log_item_t *, int remove);
	uint (*iop_push)(struct xfs_log_item *, struct list_head *);
	void (*iop_unlock)(xfs_log_item_t *);
	xfs_lsn_t (*iop_committed)(xfs_log_item_t *, xfs_lsn_t);
	void (*iop_committing)(xfs_log_item_t *, xfs_lsn_t);
};

#define IOP_SIZE(ip)		(*(ip)->li_ops->iop_size)(ip)
#define IOP_FORMAT(ip,vp)	(*(ip)->li_ops->iop_format)(ip, vp)
#define IOP_PIN(ip)		(*(ip)->li_ops->iop_pin)(ip)
#define IOP_UNPIN(ip, remove)	(*(ip)->li_ops->iop_unpin)(ip, remove)
#define IOP_PUSH(ip, list)	(*(ip)->li_ops->iop_push)(ip, list)
#define IOP_UNLOCK(ip)		(*(ip)->li_ops->iop_unlock)(ip)
#define IOP_COMMITTED(ip, lsn)	(*(ip)->li_ops->iop_committed)(ip, lsn)
#define IOP_COMMITTING(ip, lsn) (*(ip)->li_ops->iop_committing)(ip, lsn)

/*
 * Return values for the IOP_PUSH() routines.
 */
#define XFS_ITEM_SUCCESS	0
#define XFS_ITEM_PINNED		1
#define XFS_ITEM_LOCKED		2
#define XFS_ITEM_FLUSHING	3

/*
 * This is the type of function which can be given to xfs_trans_callback()
 * to be called upon the transaction's commit to disk.
 */
typedef void (*xfs_trans_callback_t)(struct xfs_trans *, void *);

/*
 * This is the structure maintained for every active transaction.
 */
typedef struct xfs_trans {
	unsigned int		t_magic;	/* magic number */
	xfs_log_callback_t	t_logcb;	/* log callback struct */
	unsigned int		t_type;		/* transaction type */
	unsigned int		t_log_res;	/* amt of log space resvd */
	unsigned int		t_log_count;	/* count for perm log res */
	unsigned int		t_blk_res;	/* # of blocks resvd */
	unsigned int		t_blk_res_used;	/* # of resvd blocks used */
	unsigned int		t_rtx_res;	/* # of rt extents resvd */
	unsigned int		t_rtx_res_used;	/* # of resvd rt extents used */
	struct xlog_ticket	*t_ticket;	/* log mgr ticket */
	xfs_lsn_t		t_lsn;		/* log seq num of start of
						 * transaction. */
	xfs_lsn_t		t_commit_lsn;	/* log seq num of end of
						 * transaction. */
	struct xfs_mount	*t_mountp;	/* ptr to fs mount struct */
	struct xfs_dquot_acct   *t_dqinfo;	/* acctg info for dquots */
	unsigned int		t_flags;	/* misc flags */
	int64_t			t_icount_delta;	/* superblock icount change */
	int64_t			t_ifree_delta;	/* superblock ifree change */
	int64_t			t_fdblocks_delta; /* superblock fdblocks chg */
	int64_t			t_res_fdblocks_delta; /* on-disk only chg */
	int64_t			t_frextents_delta;/* superblock freextents chg*/
	int64_t			t_res_frextents_delta; /* on-disk only chg */
#ifdef DEBUG
	int64_t			t_ag_freeblks_delta; /* debugging counter */
	int64_t			t_ag_flist_delta; /* debugging counter */
	int64_t			t_ag_btree_delta; /* debugging counter */
#endif
	int64_t			t_dblocks_delta;/* superblock dblocks change */
	int64_t			t_agcount_delta;/* superblock agcount change */
	int64_t			t_imaxpct_delta;/* superblock imaxpct change */
	int64_t			t_rextsize_delta;/* superblock rextsize chg */
	int64_t			t_rbmblocks_delta;/* superblock rbmblocks chg */
	int64_t			t_rblocks_delta;/* superblock rblocks change */
	int64_t			t_rextents_delta;/* superblocks rextents chg */
	int64_t			t_rextslog_delta;/* superblocks rextslog chg */
	struct list_head	t_items;	/* log item descriptors */
	xfs_trans_header_t	t_header;	/* header for in-log trans */
	struct list_head	t_busy;		/* list of busy extents */
	unsigned long		t_pflags;	/* saved process flags state */
} xfs_trans_t;

/*
 * XFS transaction mechanism exported interfaces that are
 * actually macros.
 */
#define	xfs_trans_get_log_res(tp)	((tp)->t_log_res)
#define	xfs_trans_get_log_count(tp)	((tp)->t_log_count)
#define	xfs_trans_get_block_res(tp)	((tp)->t_blk_res)
#define	xfs_trans_set_sync(tp)		((tp)->t_flags |= XFS_TRANS_SYNC)

#ifdef DEBUG
#define	xfs_trans_agblocks_delta(tp, d)	((tp)->t_ag_freeblks_delta += (int64_t)d)
#define	xfs_trans_agflist_delta(tp, d)	((tp)->t_ag_flist_delta += (int64_t)d)
#define	xfs_trans_agbtree_delta(tp, d)	((tp)->t_ag_btree_delta += (int64_t)d)
#else
#define	xfs_trans_agblocks_delta(tp, d)
#define	xfs_trans_agflist_delta(tp, d)
#define	xfs_trans_agbtree_delta(tp, d)
#endif

/*
 * XFS transaction mechanism exported interfaces.
 */
xfs_trans_t	*xfs_trans_alloc(struct xfs_mount *, uint);
xfs_trans_t	*_xfs_trans_alloc(struct xfs_mount *, uint, xfs_km_flags_t);
xfs_trans_t	*xfs_trans_dup(xfs_trans_t *);
int		xfs_trans_reserve(xfs_trans_t *, uint, uint, uint,
				  uint, uint);
void		xfs_trans_mod_sb(xfs_trans_t *, uint, int64_t);

struct xfs_buf	*xfs_trans_get_buf_map(struct xfs_trans *tp,
				       struct xfs_buftarg *target,
				       struct xfs_buf_map *map, int nmaps,
				       uint flags);

static inline struct xfs_buf *
xfs_trans_get_buf(
	struct xfs_trans	*tp,
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	int			numblks,
	uint			flags)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return xfs_trans_get_buf_map(tp, target, &map, 1, flags);
}

int		xfs_trans_read_buf_map(struct xfs_mount *mp,
				       struct xfs_trans *tp,
				       struct xfs_buftarg *target,
				       struct xfs_buf_map *map, int nmaps,
				       xfs_buf_flags_t flags,
				       struct xfs_buf **bpp,
				       const struct xfs_buf_ops *ops);

static inline int
xfs_trans_read_buf(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	int			numblks,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp,
	const struct xfs_buf_ops *ops)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return xfs_trans_read_buf_map(mp, tp, target, &map, 1,
				      flags, bpp, ops);
}

struct xfs_buf	*xfs_trans_getsb(xfs_trans_t *, struct xfs_mount *, int);

void		xfs_trans_brelse(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_bjoin(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_bhold(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_bhold_release(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_binval(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_inode_buf(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_stale_inode_buf(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_dquot_buf(xfs_trans_t *, struct xfs_buf *, uint);
void		xfs_trans_inode_alloc_buf(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_ichgtime(struct xfs_trans *, struct xfs_inode *, int);
void		xfs_trans_ijoin(struct xfs_trans *, struct xfs_inode *, uint);
void		xfs_trans_log_buf(xfs_trans_t *, struct xfs_buf *, uint, uint);
void		xfs_trans_log_inode(xfs_trans_t *, struct xfs_inode *, uint);
struct xfs_efi_log_item	*xfs_trans_get_efi(xfs_trans_t *, uint);
void		xfs_efi_release(struct xfs_efi_log_item *, uint);
void		xfs_trans_log_efi_extent(xfs_trans_t *,
					 struct xfs_efi_log_item *,
					 xfs_fsblock_t,
					 xfs_extlen_t);
struct xfs_efd_log_item	*xfs_trans_get_efd(xfs_trans_t *,
				  struct xfs_efi_log_item *,
				  uint);
void		xfs_trans_log_efd_extent(xfs_trans_t *,
					 struct xfs_efd_log_item *,
					 xfs_fsblock_t,
					 xfs_extlen_t);
int		xfs_trans_commit(xfs_trans_t *, uint flags);
void		xfs_trans_cancel(xfs_trans_t *, int);
int		xfs_trans_ail_init(struct xfs_mount *);
void		xfs_trans_ail_destroy(struct xfs_mount *);

extern kmem_zone_t	*xfs_trans_zone;
extern kmem_zone_t	*xfs_log_item_desc_zone;

#endif	/* __KERNEL__ */

void		xfs_trans_init(struct xfs_mount *);
int		xfs_trans_roll(struct xfs_trans **, struct xfs_inode *);

#endif	/* __XFS_TRANS_H__ */
