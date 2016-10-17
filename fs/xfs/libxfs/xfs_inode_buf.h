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

/*
 * In memory representation of the XFS inode. This is held in the in-core struct
 * xfs_inode and represents the current on disk values but the structure is not
 * in on-disk format.  That is, this structure is always translated to on-disk
 * format specific structures at the appropriate time.
 */
struct xfs_icdinode {
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__uint16_t	di_flushiter;	/* incremented on flush */
	__uint32_t	di_uid;		/* owner's user id */
	__uint32_t	di_gid;		/* owner's group id */
	__uint16_t	di_projid_lo;	/* lower part of owner's project id */
	__uint16_t	di_projid_hi;	/* higher part of owner's project id */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_rfsblock_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	xfs_extnum_t	di_nextents;	/* number of extents in data fork */
	xfs_aextnum_t	di_anextents;	/* number of extents in attribute fork*/
	__uint8_t	di_forkoff;	/* attr fork offs, <<3 for 64b align */
	__int8_t	di_aformat;	/* format of attr fork's data */
	__uint32_t	di_dmevmask;	/* DMIG event mask */
	__uint16_t	di_dmstate;	/* DMIG state info */
	__uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */

	__uint64_t	di_flags2;	/* more random flags */
	__uint32_t	di_cowextsize;	/* basic cow extent size for file */

	xfs_ictimestamp_t di_crtime;	/* time created */
};

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
void	xfs_inode_to_disk(struct xfs_inode *ip, struct xfs_dinode *to,
			  xfs_lsn_t lsn);
void	xfs_inode_from_disk(struct xfs_inode *ip, struct xfs_dinode *from);
void	xfs_log_dinode_to_disk(struct xfs_log_dinode *from,
			       struct xfs_dinode *to);

#if defined(DEBUG)
void	xfs_inobp_check(struct xfs_mount *, struct xfs_buf *);
#else
#define	xfs_inobp_check(mp, bp)
#endif /* DEBUG */

#endif	/* __XFS_INODE_BUF_H__ */
