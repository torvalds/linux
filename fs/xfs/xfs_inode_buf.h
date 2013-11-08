/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#ifndef	__XFS_INODE_BUF_H__
#define	__XFS_INODE_BUF_H__

struct xfs_inode;
struct xfs_dinode;
struct xfs_icdinode;

/*
 * Inode location information.  Stored in the inode and passed to
 * xfs_imap_to_bp() to get a buffer and dinode for a given inode.
 */
struct xfs_imap {
	xfs_daddr_t	im_blkno;	/* starting BB of inode chunk */
	ushort		im_len;		/* length in BBs of inode chunk */
	ushort		im_boffset;	/* inode offset in block in bytes */
};

int	xfs_imap_to_bp(struct xfs_mount *, struct xfs_trans *,
		       struct xfs_imap *, struct xfs_dinode **,
		       struct xfs_buf **, uint, uint);
int	xfs_iread(struct xfs_mount *, struct xfs_trans *,
		  struct xfs_inode *, uint);
void	xfs_dinode_calc_crc(struct xfs_mount *, struct xfs_dinode *);
void	xfs_dinode_to_disk(struct xfs_dinode *to, struct xfs_icdinode *from);
void	xfs_dinode_from_disk(struct xfs_icdinode *to, struct xfs_dinode *from);

#if defined(DEBUG)
void	xfs_inobp_check(struct xfs_mount *, struct xfs_buf *);
#else
#define	xfs_inobp_check(mp, bp)
#endif /* DEBUG */

extern const struct xfs_buf_ops xfs_inode_buf_ops;
extern const struct xfs_buf_ops xfs_inode_buf_ra_ops;

#endif	/* __XFS_INODE_BUF_H__ */
