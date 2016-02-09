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
 * In memory representation of the XFS inode. This is held in the in-core
 * struct xfs_inode to represent the on disk values, but no longer needs to be
 * identical to the on-disk structure as it is always translated to on-disk
 * format specific structures at the appropriate time.
 */
struct xfs_icdinode {
	__uint16_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__uint16_t	di_mode;	/* mode and type of file */
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__uint16_t	di_onlink;	/* old number of links to file */
	__uint32_t	di_uid;		/* owner's user id */
	__uint32_t	di_gid;		/* owner's group id */
	__uint32_t	di_nlink;	/* number of links to file */
	__uint16_t	di_projid_lo;	/* lower part of owner's project id */
	__uint16_t	di_projid_hi;	/* higher part of owner's project id */
	__uint8_t	di_pad[6];	/* unused, zeroed space */
	__uint16_t	di_flushiter;	/* incremented on flush */
	xfs_ictimestamp_t di_atime;	/* time last accessed */
	xfs_ictimestamp_t di_mtime;	/* time last modified */
	xfs_ictimestamp_t di_ctime;	/* time created/inode modified */
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
	__uint32_t	di_gen;		/* generation number */

	/* di_next_unlinked is the only non-core field in the old dinode */
	xfs_agino_t	di_next_unlinked;/* agi unlinked list ptr */

	/* start of the extended dinode, writable fields */
	__uint32_t	di_crc;		/* CRC of the inode */
	__uint64_t	di_changecount;	/* number of attribute changes */
	xfs_lsn_t	di_lsn;		/* flush sequence */
	__uint64_t	di_flags2;	/* more random flags */
	__uint8_t	di_pad2[16];	/* more padding for future expansion */

	/* fields only written to during inode creation */
	xfs_ictimestamp_t di_crtime;	/* time created */
	xfs_ino_t	di_ino;		/* inode number */
	uuid_t		di_uuid;	/* UUID of the filesystem */

	/* structure must be padded to 64 bit alignment */
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
void	xfs_dinode_to_disk(struct xfs_dinode *to, struct xfs_icdinode *from);
void	xfs_dinode_from_disk(struct xfs_icdinode *to, struct xfs_dinode *from);

#if defined(DEBUG)
void	xfs_inobp_check(struct xfs_mount *, struct xfs_buf *);
#else
#define	xfs_inobp_check(mp, bp)
#endif /* DEBUG */

#endif	/* __XFS_INODE_BUF_H__ */
