// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
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
	uint16_t	di_flushiter;	/* incremented on flush */
	prid_t		di_projid;	/* owner's project id */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_rfsblock_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	uint8_t		di_forkoff;	/* attr fork offs, <<3 for 64b align */
	uint32_t	di_dmevmask;	/* DMIG event mask */
	uint16_t	di_dmstate;	/* DMIG state info */
	uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */

	uint64_t	di_flags2;	/* more random flags */
	uint32_t	di_cowextsize;	/* basic cow extent size for file */

	struct timespec64 di_crtime;	/* time created */
};

static inline bool xfs_icdinode_has_bigtime(const struct xfs_icdinode *icd)
{
	return icd->di_flags2 & XFS_DIFLAG2_BIGTIME;
}

/*
 * Inode location information.  Stored in the inode and passed to
 * xfs_imap_to_bp() to get a buffer and dinode for a given inode.
 */
struct xfs_imap {
	xfs_daddr_t	im_blkno;	/* starting BB of inode chunk */
	unsigned short	im_len;		/* length in BBs of inode chunk */
	unsigned short	im_boffset;	/* inode offset in block in bytes */
};

int	xfs_imap_to_bp(struct xfs_mount *, struct xfs_trans *,
		       struct xfs_imap *, struct xfs_dinode **,
		       struct xfs_buf **, uint);
void	xfs_dinode_calc_crc(struct xfs_mount *, struct xfs_dinode *);
void	xfs_inode_to_disk(struct xfs_inode *ip, struct xfs_dinode *to,
			  xfs_lsn_t lsn);
int	xfs_inode_from_disk(struct xfs_inode *ip, struct xfs_dinode *from);

xfs_failaddr_t xfs_dinode_verify(struct xfs_mount *mp, xfs_ino_t ino,
			   struct xfs_dinode *dip);
xfs_failaddr_t xfs_inode_validate_extsize(struct xfs_mount *mp,
		uint32_t extsize, uint16_t mode, uint16_t flags);
xfs_failaddr_t xfs_inode_validate_cowextsize(struct xfs_mount *mp,
		uint32_t cowextsize, uint16_t mode, uint16_t flags,
		uint64_t flags2);

static inline uint64_t xfs_inode_encode_bigtime(struct timespec64 tv)
{
	return xfs_unix_to_bigtime(tv.tv_sec) * NSEC_PER_SEC + tv.tv_nsec;
}

struct timespec64 xfs_inode_from_disk_ts(struct xfs_dinode *dip,
		const xfs_timestamp_t ts);

#endif	/* __XFS_INODE_BUF_H__ */
