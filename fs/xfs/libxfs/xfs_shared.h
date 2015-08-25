/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
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
#ifndef __XFS_SHARED_H__
#define __XFS_SHARED_H__

/*
 * Definitions shared between kernel and userspace that don't fit into any other
 * header file that is shared with userspace.
 */
struct xfs_ifork;
struct xfs_buf;
struct xfs_buf_ops;
struct xfs_mount;
struct xfs_trans;
struct xfs_inode;

/*
 * Buffer verifier operations are widely used, including userspace tools
 */
extern const struct xfs_buf_ops xfs_agf_buf_ops;
extern const struct xfs_buf_ops xfs_agi_buf_ops;
extern const struct xfs_buf_ops xfs_agf_buf_ops;
extern const struct xfs_buf_ops xfs_agfl_buf_ops;
extern const struct xfs_buf_ops xfs_allocbt_buf_ops;
extern const struct xfs_buf_ops xfs_attr3_leaf_buf_ops;
extern const struct xfs_buf_ops xfs_attr3_rmt_buf_ops;
extern const struct xfs_buf_ops xfs_bmbt_buf_ops;
extern const struct xfs_buf_ops xfs_da3_node_buf_ops;
extern const struct xfs_buf_ops xfs_dquot_buf_ops;
extern const struct xfs_buf_ops xfs_symlink_buf_ops;
extern const struct xfs_buf_ops xfs_agi_buf_ops;
extern const struct xfs_buf_ops xfs_inobt_buf_ops;
extern const struct xfs_buf_ops xfs_inode_buf_ops;
extern const struct xfs_buf_ops xfs_inode_buf_ra_ops;
extern const struct xfs_buf_ops xfs_dquot_buf_ops;
extern const struct xfs_buf_ops xfs_sb_buf_ops;
extern const struct xfs_buf_ops xfs_sb_quiet_buf_ops;
extern const struct xfs_buf_ops xfs_symlink_buf_ops;

/*
 * Transaction types.  Used to distinguish types of buffers. These never reach
 * the log.
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
#define XFS_TRANS_SB_CHANGE		26
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
#define XFS_TRANS_FSYNC_TS		35
#define	XFS_TRANS_GROWFSRT_ALLOC	36
#define	XFS_TRANS_GROWFSRT_ZERO		37
#define	XFS_TRANS_GROWFSRT_FREE		38
#define	XFS_TRANS_SWAPEXT		39
#define	XFS_TRANS_CHECKPOINT		40
#define	XFS_TRANS_ICREATE		41
#define	XFS_TRANS_CREATE_TMPFILE	42
#define	XFS_TRANS_TYPE_MAX		43
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
	{ XFS_TRANS_SB_CHANGE,		"SBCHANGE" }, \
	{ XFS_TRANS_DUMMY1,		"DUMMY1" }, \
	{ XFS_TRANS_DUMMY2,		"DUMMY2" }, \
	{ XFS_TRANS_QM_QUOTAOFF,	"QM_QUOTAOFF" }, \
	{ XFS_TRANS_QM_DQALLOC,		"QM_DQALLOC" }, \
	{ XFS_TRANS_QM_SETQLIM,		"QM_SETQLIM" }, \
	{ XFS_TRANS_QM_DQCLUSTER,	"QM_DQCLUSTER" }, \
	{ XFS_TRANS_QM_QINOCREATE,	"QM_QINOCREATE" }, \
	{ XFS_TRANS_QM_QUOTAOFF_END,	"QM_QOFF_END" }, \
	{ XFS_TRANS_FSYNC_TS,		"FSYNC_TS" }, \
	{ XFS_TRANS_GROWFSRT_ALLOC,	"GROWFSRT_ALLOC" }, \
	{ XFS_TRANS_GROWFSRT_ZERO,	"GROWFSRT_ZERO" }, \
	{ XFS_TRANS_GROWFSRT_FREE,	"GROWFSRT_FREE" }, \
	{ XFS_TRANS_SWAPEXT,		"SWAPEXT" }, \
	{ XFS_TRANS_CHECKPOINT,		"CHECKPOINT" }, \
	{ XFS_TRANS_ICREATE,		"ICREATE" }, \
	{ XFS_TRANS_CREATE_TMPFILE,	"CREATE_TMPFILE" }, \
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

/* log size calculation functions */
int	xfs_log_calc_unit_res(struct xfs_mount *mp, int unit_bytes);
int	xfs_log_calc_minimum_size(struct xfs_mount *);


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
 * Here we centralize the specification of XFS meta-data buffer reference count
 * values.  This determines how hard the buffer cache tries to hold onto the
 * buffer.
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

/*
 * Flags for xfs_trans_ichgtime().
 */
#define	XFS_ICHGTIME_MOD	0x1	/* data fork modification timestamp */
#define	XFS_ICHGTIME_CHG	0x2	/* inode field change timestamp */
#define	XFS_ICHGTIME_CREATE	0x4	/* inode create timestamp */


/*
 * Symlink decoding/encoding functions
 */
int xfs_symlink_blocks(struct xfs_mount *mp, int pathlen);
int xfs_symlink_hdr_set(struct xfs_mount *mp, xfs_ino_t ino, uint32_t offset,
			uint32_t size, struct xfs_buf *bp);
bool xfs_symlink_hdr_ok(xfs_ino_t ino, uint32_t offset,
			uint32_t size, struct xfs_buf *bp);
void xfs_symlink_local_to_remote(struct xfs_trans *tp, struct xfs_buf *bp,
				 struct xfs_inode *ip, struct xfs_ifork *ifp);

#endif /* __XFS_SHARED_H__ */
