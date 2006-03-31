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
#define	XFS_LI_5_3_BUF		0x1234	/* v1 bufs, 1-block inode buffers */
#define	XFS_LI_5_3_INODE	0x1235	/* 1-block inode buffers */
#define	XFS_LI_EFI		0x1236
#define	XFS_LI_EFD		0x1237
#define	XFS_LI_IUNLINK		0x1238
#define	XFS_LI_6_1_INODE	0x1239	/* 4K non-aligned inode bufs */
#define	XFS_LI_6_1_BUF		0x123a	/* v1, 4K inode buffers */
#define	XFS_LI_INODE		0x123b	/* aligned ino chunks, var-size ibufs */
#define	XFS_LI_BUF		0x123c	/* v2 bufs, variable sized inode bufs */
#define	XFS_LI_DQUOT		0x123d
#define	XFS_LI_QUOTAOFF		0x123e

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
#define	XFS_TRANS_WRITE_SYNC		17
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
#define	XFS_TRANS_TYPE_MAX		40
/* new transaction types need to be reflected in xfs_logprint(8) */


#ifdef __KERNEL__
struct xfs_buf;
struct xfs_buftarg;
struct xfs_efd_log_item;
struct xfs_efi_log_item;
struct xfs_inode;
struct xfs_item_ops;
struct xfs_log_iovec;
struct xfs_log_item;
struct xfs_log_item_desc;
struct xfs_mount;
struct xfs_trans;
struct xfs_dquot_acct;

typedef struct xfs_ail_entry {
	struct xfs_log_item	*ail_forw;	/* AIL forw pointer */
	struct xfs_log_item	*ail_back;	/* AIL back pointer */
} xfs_ail_entry_t;

typedef struct xfs_log_item {
	xfs_ail_entry_t			li_ail;		/* AIL pointers */
	xfs_lsn_t			li_lsn;		/* last on-disk lsn */
	struct xfs_log_item_desc	*li_desc;	/* ptr to current desc*/
	struct xfs_mount		*li_mountp;	/* ptr to fs mount */
	uint				li_type;	/* item type */
	uint				li_flags;	/* misc flags */
	struct xfs_log_item		*li_bio_list;	/* buffer item list */
	void				(*li_cb)(struct xfs_buf *,
						 struct xfs_log_item *);
							/* buffer item iodone */
							/* callback func */
	struct xfs_item_ops		*li_ops;	/* function list */
} xfs_log_item_t;

#define	XFS_LI_IN_AIL	0x1
#define XFS_LI_ABORTED	0x2

typedef struct xfs_item_ops {
	uint (*iop_size)(xfs_log_item_t *);
	void (*iop_format)(xfs_log_item_t *, struct xfs_log_iovec *);
	void (*iop_pin)(xfs_log_item_t *);
	void (*iop_unpin)(xfs_log_item_t *, int);
	void (*iop_unpin_remove)(xfs_log_item_t *, struct xfs_trans *);
	uint (*iop_trylock)(xfs_log_item_t *);
	void (*iop_unlock)(xfs_log_item_t *);
	xfs_lsn_t (*iop_committed)(xfs_log_item_t *, xfs_lsn_t);
	void (*iop_push)(xfs_log_item_t *);
	void (*iop_abort)(xfs_log_item_t *);
	void (*iop_pushbuf)(xfs_log_item_t *);
	void (*iop_committing)(xfs_log_item_t *, xfs_lsn_t);
} xfs_item_ops_t;

#define IOP_SIZE(ip)		(*(ip)->li_ops->iop_size)(ip)
#define IOP_FORMAT(ip,vp)	(*(ip)->li_ops->iop_format)(ip, vp)
#define IOP_PIN(ip)		(*(ip)->li_ops->iop_pin)(ip)
#define IOP_UNPIN(ip, flags)	(*(ip)->li_ops->iop_unpin)(ip, flags)
#define IOP_UNPIN_REMOVE(ip,tp) (*(ip)->li_ops->iop_unpin_remove)(ip, tp)
#define IOP_TRYLOCK(ip)		(*(ip)->li_ops->iop_trylock)(ip)
#define IOP_UNLOCK(ip)		(*(ip)->li_ops->iop_unlock)(ip)
#define IOP_COMMITTED(ip, lsn)	(*(ip)->li_ops->iop_committed)(ip, lsn)
#define IOP_PUSH(ip)		(*(ip)->li_ops->iop_push)(ip)
#define IOP_ABORT(ip)		(*(ip)->li_ops->iop_abort)(ip)
#define IOP_PUSHBUF(ip)		(*(ip)->li_ops->iop_pushbuf)(ip)
#define IOP_COMMITTING(ip, lsn) (*(ip)->li_ops->iop_committing)(ip, lsn)

/*
 * Return values for the IOP_TRYLOCK() routines.
 */
#define	XFS_ITEM_SUCCESS	0
#define	XFS_ITEM_PINNED		1
#define	XFS_ITEM_LOCKED		2
#define	XFS_ITEM_FLUSHING	3
#define XFS_ITEM_PUSHBUF	4

#endif	/* __KERNEL__ */

/*
 * This structure is used to track log items associated with
 * a transaction.  It points to the log item and keeps some
 * flags to track the state of the log item.  It also tracks
 * the amount of space needed to log the item it describes
 * once we get to commit processing (see xfs_trans_commit()).
 */
typedef struct xfs_log_item_desc {
	xfs_log_item_t	*lid_item;
	ushort		lid_size;
	unsigned char	lid_flags;
	unsigned char	lid_index;
} xfs_log_item_desc_t;

#define XFS_LID_DIRTY		0x1
#define XFS_LID_PINNED		0x2
#define XFS_LID_BUF_STALE	0x8

/*
 * This structure is used to maintain a chunk list of log_item_desc
 * structures. The free field is a bitmask indicating which descriptors
 * in this chunk's array are free.  The unused field is the first value
 * not used since this chunk was allocated.
 */
#define	XFS_LIC_NUM_SLOTS	15
typedef struct xfs_log_item_chunk {
	struct xfs_log_item_chunk	*lic_next;
	ushort				lic_free;
	ushort				lic_unused;
	xfs_log_item_desc_t		lic_descs[XFS_LIC_NUM_SLOTS];
} xfs_log_item_chunk_t;

#define	XFS_LIC_MAX_SLOT	(XFS_LIC_NUM_SLOTS - 1)
#define	XFS_LIC_FREEMASK	((1 << XFS_LIC_NUM_SLOTS) - 1)


/*
 * Initialize the given chunk.  Set the chunk's free descriptor mask
 * to indicate that all descriptors are free.  The caller gets to set
 * lic_unused to the right value (0 matches all free).  The
 * lic_descs.lid_index values are set up as each desc is allocated.
 */
#define	XFS_LIC_INIT(cp)	xfs_lic_init(cp)
static inline void xfs_lic_init(xfs_log_item_chunk_t *cp)
{
	cp->lic_free = XFS_LIC_FREEMASK;
}

#define	XFS_LIC_INIT_SLOT(cp,slot)	xfs_lic_init_slot(cp, slot)
static inline void xfs_lic_init_slot(xfs_log_item_chunk_t *cp, int slot)
{
	cp->lic_descs[slot].lid_index = (unsigned char)(slot);
}

#define	XFS_LIC_VACANCY(cp)		xfs_lic_vacancy(cp)
static inline int xfs_lic_vacancy(xfs_log_item_chunk_t *cp)
{
	return cp->lic_free & XFS_LIC_FREEMASK;
}

#define	XFS_LIC_ALL_FREE(cp)		xfs_lic_all_free(cp)
static inline void xfs_lic_all_free(xfs_log_item_chunk_t *cp)
{
	cp->lic_free = XFS_LIC_FREEMASK;
}

#define	XFS_LIC_ARE_ALL_FREE(cp)	xfs_lic_are_all_free(cp)
static inline int xfs_lic_are_all_free(xfs_log_item_chunk_t *cp)
{
	return ((cp->lic_free & XFS_LIC_FREEMASK) == XFS_LIC_FREEMASK);
}

#define	XFS_LIC_ISFREE(cp,slot)	xfs_lic_isfree(cp,slot)
static inline int xfs_lic_isfree(xfs_log_item_chunk_t *cp, int slot)
{
	return (cp->lic_free & (1 << slot));
}

#define	XFS_LIC_CLAIM(cp,slot)		xfs_lic_claim(cp,slot)
static inline void xfs_lic_claim(xfs_log_item_chunk_t *cp, int slot)
{
	cp->lic_free &= ~(1 << slot);
}

#define	XFS_LIC_RELSE(cp,slot)		xfs_lic_relse(cp,slot)
static inline void xfs_lic_relse(xfs_log_item_chunk_t *cp, int slot)
{
	cp->lic_free |= 1 << slot;
}

#define	XFS_LIC_SLOT(cp,slot)		xfs_lic_slot(cp,slot)
static inline xfs_log_item_desc_t *
xfs_lic_slot(xfs_log_item_chunk_t *cp, int slot)
{
	return &(cp->lic_descs[slot]);
}

#define	XFS_LIC_DESC_TO_SLOT(dp)	xfs_lic_desc_to_slot(dp)
static inline int xfs_lic_desc_to_slot(xfs_log_item_desc_t *dp)
{
	return (uint)dp->lid_index;
}

/*
 * Calculate the address of a chunk given a descriptor pointer:
 * dp - dp->lid_index give the address of the start of the lic_descs array.
 * From this we subtract the offset of the lic_descs field in a chunk.
 * All of this yields the address of the chunk, which is
 * cast to a chunk pointer.
 */
#define	XFS_LIC_DESC_TO_CHUNK(dp)	xfs_lic_desc_to_chunk(dp)
static inline xfs_log_item_chunk_t *
xfs_lic_desc_to_chunk(xfs_log_item_desc_t *dp)
{
	return (xfs_log_item_chunk_t*) \
		(((xfs_caddr_t)((dp) - (dp)->lid_index)) - \
		(xfs_caddr_t)(((xfs_log_item_chunk_t*)0)->lic_descs));
}

#ifdef __KERNEL__
/*
 * This structure is used to maintain a list of block ranges that have been
 * freed in the transaction.  The ranges are listed in the perag[] busy list
 * between when they're freed and the transaction is committed to disk.
 */

typedef struct xfs_log_busy_slot {
	xfs_agnumber_t		lbc_ag;
	ushort			lbc_idx;	/* index in perag.busy[] */
} xfs_log_busy_slot_t;

#define XFS_LBC_NUM_SLOTS	31
typedef struct xfs_log_busy_chunk {
	struct xfs_log_busy_chunk	*lbc_next;
	uint				lbc_free;	/* free slots bitmask */
	ushort				lbc_unused;	/* first unused */
	xfs_log_busy_slot_t		lbc_busy[XFS_LBC_NUM_SLOTS];
} xfs_log_busy_chunk_t;

#define	XFS_LBC_MAX_SLOT	(XFS_LBC_NUM_SLOTS - 1)
#define	XFS_LBC_FREEMASK	((1U << XFS_LBC_NUM_SLOTS) - 1)

#define	XFS_LBC_INIT(cp)	((cp)->lbc_free = XFS_LBC_FREEMASK)
#define	XFS_LBC_CLAIM(cp, slot)	((cp)->lbc_free &= ~(1 << (slot)))
#define	XFS_LBC_SLOT(cp, slot)	(&((cp)->lbc_busy[(slot)]))
#define	XFS_LBC_VACANCY(cp)	(((cp)->lbc_free) & XFS_LBC_FREEMASK)
#define	XFS_LBC_ISFREE(cp, slot) ((cp)->lbc_free & (1 << (slot)))

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
	struct xfs_trans	*t_forw;	/* async list pointers */
	struct xfs_trans	*t_back;	/* async list pointers */
	unsigned int		t_type;		/* transaction type */
	unsigned int		t_log_res;	/* amt of log space resvd */
	unsigned int		t_log_count;	/* count for perm log res */
	unsigned int		t_blk_res;	/* # of blocks resvd */
	unsigned int		t_blk_res_used;	/* # of resvd blocks used */
	unsigned int		t_rtx_res;	/* # of rt extents resvd */
	unsigned int		t_rtx_res_used;	/* # of resvd rt extents used */
	xfs_log_ticket_t	t_ticket;	/* log mgr ticket */
	sema_t			t_sema;		/* sema for commit completion */
	xfs_lsn_t		t_lsn;		/* log seq num of start of
						 * transaction. */
	xfs_lsn_t		t_commit_lsn;	/* log seq num of end of
						 * transaction. */
	struct xfs_mount	*t_mountp;	/* ptr to fs mount struct */
	struct xfs_dquot_acct   *t_dqinfo;	/* acctg info for dquots */
	xfs_trans_callback_t	t_callback;	/* transaction callback */
	void			*t_callarg;	/* callback arg */
	unsigned int		t_flags;	/* misc flags */
	long			t_icount_delta;	/* superblock icount change */
	long			t_ifree_delta;	/* superblock ifree change */
	long			t_fdblocks_delta; /* superblock fdblocks chg */
	long			t_res_fdblocks_delta; /* on-disk only chg */
	long			t_frextents_delta;/* superblock freextents chg*/
	long			t_res_frextents_delta; /* on-disk only chg */
	long			t_ag_freeblks_delta; /* debugging counter */
	long			t_ag_flist_delta; /* debugging counter */
	long			t_ag_btree_delta; /* debugging counter */
	long			t_dblocks_delta;/* superblock dblocks change */
	long			t_agcount_delta;/* superblock agcount change */
	long			t_imaxpct_delta;/* superblock imaxpct change */
	long			t_rextsize_delta;/* superblock rextsize chg */
	long			t_rbmblocks_delta;/* superblock rbmblocks chg */
	long			t_rblocks_delta;/* superblock rblocks change */
	long			t_rextents_delta;/* superblocks rextents chg */
	long			t_rextslog_delta;/* superblocks rextslog chg */
	unsigned int		t_items_free;	/* log item descs free */
	xfs_log_item_chunk_t	t_items;	/* first log item desc chunk */
	xfs_trans_header_t	t_header;	/* header for in-log trans */
	unsigned int		t_busy_free;	/* busy descs free */
	xfs_log_busy_chunk_t	t_busy;		/* busy/async free blocks */
	unsigned long		t_pflags;	/* saved process flags state */
} xfs_trans_t;

#endif	/* __KERNEL__ */


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

/*
 * Values for call flags parameter.
 */
#define	XFS_TRANS_NOSLEEP		0x1
#define	XFS_TRANS_WAIT			0x2
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
 * Various log reservation values.
 * These are based on the size of the file system block
 * because that is what most transactions manipulate.
 * Each adds in an additional 128 bytes per item logged to
 * try to account for the overhead of the transaction mechanism.
 *
 * Note:
 * Most of the reservations underestimate the number of allocation
 * groups into which they could free extents in the xfs_bmap_finish()
 * call.  This is because the number in the worst case is quite high
 * and quite unusual.  In order to fix this we need to change
 * xfs_bmap_finish() to free extents in only a single AG at a time.
 * This will require changes to the EFI code as well, however, so that
 * the EFI for the extents not freed is logged again in each transaction.
 * See bug 261917.
 */

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

/*
 * In a write transaction we can allocate a maximum of 2
 * extents.  This gives:
 *    the inode getting the new extents: inode size
 *    the inode\'s bmap btree: max depth * block size
 *    the agfs of the ags from which the extents are allocated: 2 * sector
 *    the superblock free block counter: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 * And the bmap_finish transaction can free bmap blocks in a join:
 *    the agfs of the ags containing the blocks: 2 * sector size
 *    the agfls of the ags containing the blocks: 2 * sector size
 *    the super block free block counter: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
#define XFS_CALC_WRITE_LOG_RES(mp) \
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK)) + \
	  (2 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_ALLOCFREE_LOG_RES(mp, 2) + \
	  (128 * (4 + XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + XFS_ALLOCFREE_LOG_COUNT(mp, 2)))),\
	 ((2 * (mp)->m_sb.sb_sectsize) + \
	  (2 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_ALLOCFREE_LOG_RES(mp, 2) + \
	  (128 * (5 + XFS_ALLOCFREE_LOG_COUNT(mp, 2))))))

#define	XFS_WRITE_LOG_RES(mp)	((mp)->m_reservations.tr_write)

/*
 * In truncating a file we free up to two extents at once.  We can modify:
 *    the inode being truncated: inode size
 *    the inode\'s bmap btree: (max depth + 1) * block size
 * And the bmap_finish transaction can free the blocks and bmap blocks:
 *    the agf for each of the ags: 4 * sector size
 *    the agfl for each of the ags: 4 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    worst case split in allocation btrees per extent assuming 4 extents:
 *		4 exts * 2 trees * (2 * max depth - 1) * block size
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
#define	XFS_CALC_ITRUNCATE_LOG_RES(mp) \
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + 1) + \
	  (128 * (2 + XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK)))), \
	 ((4 * (mp)->m_sb.sb_sectsize) + \
	  (4 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_ALLOCFREE_LOG_RES(mp, 4) + \
	  (128 * (9 + XFS_ALLOCFREE_LOG_COUNT(mp, 4))) + \
	  (128 * 5) + \
	  XFS_ALLOCFREE_LOG_RES(mp, 1) + \
	   (128 * (2 + XFS_IALLOC_BLOCKS(mp) + XFS_IN_MAXLEVELS(mp) + \
	    XFS_ALLOCFREE_LOG_COUNT(mp, 1))))))

#define	XFS_ITRUNCATE_LOG_RES(mp)   ((mp)->m_reservations.tr_itruncate)

/*
 * In renaming a files we can modify:
 *    the four inodes involved: 4 * inode size
 *    the two directory btrees: 2 * (max depth + v2) * dir block size
 *    the two directory bmap btrees: 2 * max depth * block size
 * And the bmap_finish transaction can free dir and bmap blocks (two sets
 *	of bmap blocks) giving:
 *    the agf for the ags in which the blocks live: 3 * sector size
 *    the agfl for the ags in which the blocks live: 3 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 3 exts * 2 trees * (2 * max depth - 1) * block size
 */
#define	XFS_CALC_RENAME_LOG_RES(mp) \
	(MAX( \
	 ((4 * (mp)->m_sb.sb_inodesize) + \
	  (2 * XFS_DIROP_LOG_RES(mp)) + \
	  (128 * (4 + 2 * XFS_DIROP_LOG_COUNT(mp)))), \
	 ((3 * (mp)->m_sb.sb_sectsize) + \
	  (3 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_ALLOCFREE_LOG_RES(mp, 3) + \
	  (128 * (7 + XFS_ALLOCFREE_LOG_COUNT(mp, 3))))))

#define	XFS_RENAME_LOG_RES(mp)	((mp)->m_reservations.tr_rename)

/*
 * For creating a link to an inode:
 *    the parent directory inode: inode size
 *    the linked inode: inode size
 *    the directory btree could split: (max depth + v2) * dir block size
 *    the directory bmap btree could join or split: (max depth + v2) * blocksize
 * And the bmap_finish transaction can free some bmap blocks giving:
 *    the agf for the ag in which the blocks live: sector size
 *    the agfl for the ag in which the blocks live: sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 trees * (2 * max depth - 1) * block size
 */
#define	XFS_CALC_LINK_LOG_RES(mp) \
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  (mp)->m_sb.sb_inodesize + \
	  XFS_DIROP_LOG_RES(mp) + \
	  (128 * (2 + XFS_DIROP_LOG_COUNT(mp)))), \
	 ((mp)->m_sb.sb_sectsize + \
	  (mp)->m_sb.sb_sectsize + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_ALLOCFREE_LOG_RES(mp, 1) + \
	  (128 * (3 + XFS_ALLOCFREE_LOG_COUNT(mp, 1))))))

#define	XFS_LINK_LOG_RES(mp)	((mp)->m_reservations.tr_link)

/*
 * For removing a directory entry we can modify:
 *    the parent directory inode: inode size
 *    the removed inode: inode size
 *    the directory btree could join: (max depth + v2) * dir block size
 *    the directory bmap btree could join or split: (max depth + v2) * blocksize
 * And the bmap_finish transaction can free the dir and bmap blocks giving:
 *    the agf for the ag in which the blocks live: 2 * sector size
 *    the agfl for the ag in which the blocks live: 2 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
#define	XFS_CALC_REMOVE_LOG_RES(mp)	\
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  (mp)->m_sb.sb_inodesize + \
	  XFS_DIROP_LOG_RES(mp) + \
	  (128 * (2 + XFS_DIROP_LOG_COUNT(mp)))), \
	 ((2 * (mp)->m_sb.sb_sectsize) + \
	  (2 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_ALLOCFREE_LOG_RES(mp, 2) + \
	  (128 * (5 + XFS_ALLOCFREE_LOG_COUNT(mp, 2))))))

#define	XFS_REMOVE_LOG_RES(mp)	((mp)->m_reservations.tr_remove)

/*
 * For symlink we can modify:
 *    the parent directory inode: inode size
 *    the new inode: inode size
 *    the inode btree entry: 1 block
 *    the directory btree: (max depth + v2) * dir block size
 *    the directory inode\'s bmap btree: (max depth + v2) * block size
 *    the blocks for the symlink: 1 KB
 * Or in the first xact we allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the inode blocks allocated: XFS_IALLOC_BLOCKS * blocksize
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (2 * max depth - 1) * block size
 */
#define	XFS_CALC_SYMLINK_LOG_RES(mp)		\
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  (mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B(mp, 1) + \
	  XFS_DIROP_LOG_RES(mp) + \
	  1024 + \
	  (128 * (4 + XFS_DIROP_LOG_COUNT(mp)))), \
	 (2 * (mp)->m_sb.sb_sectsize + \
	  XFS_FSB_TO_B((mp), XFS_IALLOC_BLOCKS((mp))) + \
	  XFS_FSB_TO_B((mp), XFS_IN_MAXLEVELS(mp)) + \
	  XFS_ALLOCFREE_LOG_RES(mp, 1) + \
	  (128 * (2 + XFS_IALLOC_BLOCKS(mp) + XFS_IN_MAXLEVELS(mp) + \
	   XFS_ALLOCFREE_LOG_COUNT(mp, 1))))))

#define	XFS_SYMLINK_LOG_RES(mp)	((mp)->m_reservations.tr_symlink)

/*
 * For create we can modify:
 *    the parent directory inode: inode size
 *    the new inode: inode size
 *    the inode btree entry: block size
 *    the superblock for the nlink flag: sector size
 *    the directory btree: (max depth + v2) * dir block size
 *    the directory inode\'s bmap btree: (max depth + v2) * block size
 * Or in the first xact we allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the superblock for the nlink flag: sector size
 *    the inode blocks allocated: XFS_IALLOC_BLOCKS * blocksize
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
#define	XFS_CALC_CREATE_LOG_RES(mp)		\
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  (mp)->m_sb.sb_inodesize + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_FSB_TO_B(mp, 1) + \
	  XFS_DIROP_LOG_RES(mp) + \
	  (128 * (3 + XFS_DIROP_LOG_COUNT(mp)))), \
	 (3 * (mp)->m_sb.sb_sectsize + \
	  XFS_FSB_TO_B((mp), XFS_IALLOC_BLOCKS((mp))) + \
	  XFS_FSB_TO_B((mp), XFS_IN_MAXLEVELS(mp)) + \
	  XFS_ALLOCFREE_LOG_RES(mp, 1) + \
	  (128 * (2 + XFS_IALLOC_BLOCKS(mp) + XFS_IN_MAXLEVELS(mp) + \
	   XFS_ALLOCFREE_LOG_COUNT(mp, 1))))))

#define	XFS_CREATE_LOG_RES(mp)	((mp)->m_reservations.tr_create)

/*
 * Making a new directory is the same as creating a new file.
 */
#define	XFS_CALC_MKDIR_LOG_RES(mp)	XFS_CALC_CREATE_LOG_RES(mp)

#define	XFS_MKDIR_LOG_RES(mp)	((mp)->m_reservations.tr_mkdir)

/*
 * In freeing an inode we can modify:
 *    the inode being freed: inode size
 *    the super block free inode counter: sector size
 *    the agi hash list and counters: sector size
 *    the inode btree entry: block size
 *    the on disk inode before ours in the agi hash list: inode cluster size
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
#define	XFS_CALC_IFREE_LOG_RES(mp) \
	((mp)->m_sb.sb_inodesize + \
	 (mp)->m_sb.sb_sectsize + \
	 (mp)->m_sb.sb_sectsize + \
	 XFS_FSB_TO_B((mp), 1) + \
	 MAX((__uint16_t)XFS_FSB_TO_B((mp), 1), XFS_INODE_CLUSTER_SIZE(mp)) + \
	 (128 * 5) + \
	  XFS_ALLOCFREE_LOG_RES(mp, 1) + \
	  (128 * (2 + XFS_IALLOC_BLOCKS(mp) + XFS_IN_MAXLEVELS(mp) + \
	   XFS_ALLOCFREE_LOG_COUNT(mp, 1))))


#define	XFS_IFREE_LOG_RES(mp)	((mp)->m_reservations.tr_ifree)

/*
 * When only changing the inode we log the inode and possibly the superblock
 * We also add a bit of slop for the transaction stuff.
 */
#define	XFS_CALC_ICHANGE_LOG_RES(mp)	((mp)->m_sb.sb_inodesize + \
					 (mp)->m_sb.sb_sectsize + 512)

#define	XFS_ICHANGE_LOG_RES(mp)	((mp)->m_reservations.tr_ichange)

/*
 * Growing the data section of the filesystem.
 *	superblock
 *	agi and agf
 *	allocation btrees
 */
#define	XFS_CALC_GROWDATA_LOG_RES(mp) \
	((mp)->m_sb.sb_sectsize * 3 + \
	 XFS_ALLOCFREE_LOG_RES(mp, 1) + \
	 (128 * (3 + XFS_ALLOCFREE_LOG_COUNT(mp, 1))))

#define	XFS_GROWDATA_LOG_RES(mp)    ((mp)->m_reservations.tr_growdata)

/*
 * Growing the rt section of the filesystem.
 * In the first set of transactions (ALLOC) we allocate space to the
 * bitmap or summary files.
 *	superblock: sector size
 *	agf of the ag from which the extent is allocated: sector size
 *	bmap btree for bitmap/summary inode: max depth * blocksize
 *	bitmap/summary inode: inode size
 *	allocation btrees for 1 block alloc: 2 * (2 * maxdepth - 1) * blocksize
 */
#define	XFS_CALC_GROWRTALLOC_LOG_RES(mp) \
	(2 * (mp)->m_sb.sb_sectsize + \
	 XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK)) + \
	 (mp)->m_sb.sb_inodesize + \
	 XFS_ALLOCFREE_LOG_RES(mp, 1) + \
	 (128 * \
	  (3 + XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + \
	   XFS_ALLOCFREE_LOG_COUNT(mp, 1))))

#define	XFS_GROWRTALLOC_LOG_RES(mp)	((mp)->m_reservations.tr_growrtalloc)

/*
 * Growing the rt section of the filesystem.
 * In the second set of transactions (ZERO) we zero the new metadata blocks.
 *	one bitmap/summary block: blocksize
 */
#define	XFS_CALC_GROWRTZERO_LOG_RES(mp) \
	((mp)->m_sb.sb_blocksize + 128)

#define	XFS_GROWRTZERO_LOG_RES(mp)	((mp)->m_reservations.tr_growrtzero)

/*
 * Growing the rt section of the filesystem.
 * In the third set of transactions (FREE) we update metadata without
 * allocating any new blocks.
 *	superblock: sector size
 *	bitmap inode: inode size
 *	summary inode: inode size
 *	one bitmap block: blocksize
 *	summary blocks: new summary size
 */
#define	XFS_CALC_GROWRTFREE_LOG_RES(mp) \
	((mp)->m_sb.sb_sectsize + \
	 2 * (mp)->m_sb.sb_inodesize + \
	 (mp)->m_sb.sb_blocksize + \
	 (mp)->m_rsumsize + \
	 (128 * 5))

#define	XFS_GROWRTFREE_LOG_RES(mp)	((mp)->m_reservations.tr_growrtfree)

/*
 * Logging the inode modification timestamp on a synchronous write.
 *	inode
 */
#define	XFS_CALC_SWRITE_LOG_RES(mp) \
	((mp)->m_sb.sb_inodesize + 128)

#define	XFS_SWRITE_LOG_RES(mp)	((mp)->m_reservations.tr_swrite)

/*
 * Logging the inode timestamps on an fsync -- same as SWRITE
 * as long as SWRITE logs the entire inode core
 */
#define XFS_FSYNC_TS_LOG_RES(mp)        ((mp)->m_reservations.tr_swrite)

/*
 * Logging the inode mode bits when writing a setuid/setgid file
 *	inode
 */
#define	XFS_CALC_WRITEID_LOG_RES(mp) \
	((mp)->m_sb.sb_inodesize + 128)

#define	XFS_WRITEID_LOG_RES(mp)	((mp)->m_reservations.tr_swrite)

/*
 * Converting the inode from non-attributed to attributed.
 *	the inode being converted: inode size
 *	agf block and superblock (for block allocation)
 *	the new block (directory sized)
 *	bmap blocks for the new directory block
 *	allocation btrees
 */
#define	XFS_CALC_ADDAFORK_LOG_RES(mp)	\
	((mp)->m_sb.sb_inodesize + \
	 (mp)->m_sb.sb_sectsize * 2 + \
	 (mp)->m_dirblksize + \
	 (XFS_DIR_IS_V1(mp) ? 0 : \
	    XFS_FSB_TO_B(mp, (XFS_DAENTER_BMAP1B(mp, XFS_DATA_FORK) + 1))) + \
	 XFS_ALLOCFREE_LOG_RES(mp, 1) + \
	 (128 * (4 + \
		 (XFS_DIR_IS_V1(mp) ? 0 : \
			 XFS_DAENTER_BMAP1B(mp, XFS_DATA_FORK) + 1) + \
		 XFS_ALLOCFREE_LOG_COUNT(mp, 1))))

#define	XFS_ADDAFORK_LOG_RES(mp)	((mp)->m_reservations.tr_addafork)

/*
 * Removing the attribute fork of a file
 *    the inode being truncated: inode size
 *    the inode\'s bmap btree: max depth * block size
 * And the bmap_finish transaction can free the blocks and bmap blocks:
 *    the agf for each of the ags: 4 * sector size
 *    the agfl for each of the ags: 4 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    worst case split in allocation btrees per extent assuming 4 extents:
 *		4 exts * 2 trees * (2 * max depth - 1) * block size
 */
#define	XFS_CALC_ATTRINVAL_LOG_RES(mp)	\
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)) + \
	  (128 * (1 + XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)))), \
	 ((4 * (mp)->m_sb.sb_sectsize) + \
	  (4 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_ALLOCFREE_LOG_RES(mp, 4) + \
	  (128 * (9 + XFS_ALLOCFREE_LOG_COUNT(mp, 4))))))

#define	XFS_ATTRINVAL_LOG_RES(mp)	((mp)->m_reservations.tr_attrinval)

/*
 * Setting an attribute.
 *	the inode getting the attribute
 *	the superblock for allocations
 *	the agfs extents are allocated from
 *	the attribute btree * max depth
 *	the inode allocation btree
 * Since attribute transaction space is dependent on the size of the attribute,
 * the calculation is done partially at mount time and partially at runtime.
 */
#define	XFS_CALC_ATTRSET_LOG_RES(mp)	\
	((mp)->m_sb.sb_inodesize + \
	 (mp)->m_sb.sb_sectsize + \
	  XFS_FSB_TO_B((mp), XFS_DA_NODE_MAXDEPTH) + \
	  (128 * (2 + XFS_DA_NODE_MAXDEPTH)))

#define	XFS_ATTRSET_LOG_RES(mp, ext)	\
	((mp)->m_reservations.tr_attrset + \
	 (ext * (mp)->m_sb.sb_sectsize) + \
	 (ext * XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK))) + \
	 (128 * (ext + (ext * XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)))))

/*
 * Removing an attribute.
 *    the inode: inode size
 *    the attribute btree could join: max depth * block size
 *    the inode bmap btree could join or split: max depth * block size
 * And the bmap_finish transaction can free the attr blocks freed giving:
 *    the agf for the ag in which the blocks live: 2 * sector size
 *    the agfl for the ag in which the blocks live: 2 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
#define	XFS_CALC_ATTRRM_LOG_RES(mp)	\
	(MAX( \
	  ((mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B((mp), XFS_DA_NODE_MAXDEPTH) + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)) + \
	  (128 * (1 + XFS_DA_NODE_MAXDEPTH + XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK)))), \
	 ((2 * (mp)->m_sb.sb_sectsize) + \
	  (2 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  XFS_ALLOCFREE_LOG_RES(mp, 2) + \
	  (128 * (5 + XFS_ALLOCFREE_LOG_COUNT(mp, 2))))))

#define	XFS_ATTRRM_LOG_RES(mp)	((mp)->m_reservations.tr_attrrm)

/*
 * Clearing a bad agino number in an agi hash bucket.
 */
#define	XFS_CALC_CLEAR_AGI_BUCKET_LOG_RES(mp) \
	((mp)->m_sb.sb_sectsize + 128)

#define	XFS_CLEAR_AGI_BUCKET_LOG_RES(mp)  ((mp)->m_reservations.tr_clearagi)


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
#define	XFS_ATTR_BTREE_REF	1
#define	XFS_INO_REF		1
#define	XFS_DQUOT_REF		1

#ifdef __KERNEL__
/*
 * XFS transaction mechanism exported interfaces that are
 * actually macros.
 */
#define	xfs_trans_get_log_res(tp)	((tp)->t_log_res)
#define	xfs_trans_get_log_count(tp)	((tp)->t_log_count)
#define	xfs_trans_get_block_res(tp)	((tp)->t_blk_res)
#define	xfs_trans_set_sync(tp)		((tp)->t_flags |= XFS_TRANS_SYNC)

#ifdef DEBUG
#define	xfs_trans_agblocks_delta(tp, d)	((tp)->t_ag_freeblks_delta += (long)d)
#define	xfs_trans_agflist_delta(tp, d)	((tp)->t_ag_flist_delta += (long)d)
#define	xfs_trans_agbtree_delta(tp, d)	((tp)->t_ag_btree_delta += (long)d)
#else
#define	xfs_trans_agblocks_delta(tp, d)
#define	xfs_trans_agflist_delta(tp, d)
#define	xfs_trans_agbtree_delta(tp, d)
#endif

/*
 * XFS transaction mechanism exported interfaces.
 */
void		xfs_trans_init(struct xfs_mount *);
xfs_trans_t	*xfs_trans_alloc(struct xfs_mount *, uint);
xfs_trans_t	*_xfs_trans_alloc(struct xfs_mount *, uint);
xfs_trans_t	*xfs_trans_dup(xfs_trans_t *);
int		xfs_trans_reserve(xfs_trans_t *, uint, uint, uint,
				  uint, uint);
void		xfs_trans_mod_sb(xfs_trans_t *, uint, long);
struct xfs_buf	*xfs_trans_get_buf(xfs_trans_t *, struct xfs_buftarg *, xfs_daddr_t,
				   int, uint);
int		xfs_trans_read_buf(struct xfs_mount *, xfs_trans_t *,
				   struct xfs_buftarg *, xfs_daddr_t, int, uint,
				   struct xfs_buf **);
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
int		xfs_trans_iget(struct xfs_mount *, xfs_trans_t *,
			       xfs_ino_t , uint, uint, struct xfs_inode **);
void		xfs_trans_ijoin(xfs_trans_t *, struct xfs_inode *, uint);
void		xfs_trans_ihold(xfs_trans_t *, struct xfs_inode *);
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
int		_xfs_trans_commit(xfs_trans_t *,
				  uint flags,
				  xfs_lsn_t *,
				  int *);
#define xfs_trans_commit(tp, flags, lsn) \
	_xfs_trans_commit(tp, flags, lsn, NULL)
void		xfs_trans_cancel(xfs_trans_t *, int);
void		xfs_trans_ail_init(struct xfs_mount *);
xfs_lsn_t	xfs_trans_push_ail(struct xfs_mount *, xfs_lsn_t);
xfs_lsn_t	xfs_trans_tail_ail(struct xfs_mount *);
void		xfs_trans_unlocked_item(struct xfs_mount *,
					xfs_log_item_t *);
xfs_log_busy_slot_t *xfs_trans_add_busy(xfs_trans_t *tp,
					xfs_agnumber_t ag,
					xfs_extlen_t idx);

#endif	/* __KERNEL__ */

#endif	/* __XFS_TRANS_H__ */
