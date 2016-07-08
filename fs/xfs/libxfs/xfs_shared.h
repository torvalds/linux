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
extern const struct xfs_buf_ops xfs_dquot_buf_ra_ops;
extern const struct xfs_buf_ops xfs_sb_buf_ops;
extern const struct xfs_buf_ops xfs_sb_quiet_buf_ops;
extern const struct xfs_buf_ops xfs_symlink_buf_ops;
extern const struct xfs_buf_ops xfs_rtbuf_ops;

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
#define XFS_TRANS_NO_WRITECOUNT 0x40	/* do not elevate SB writecount */
#define XFS_TRANS_NOFS		0x80	/* pass KM_NOFS to kmem_alloc */

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
