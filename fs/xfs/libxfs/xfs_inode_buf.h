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
 * Inode location information.  Stored in the inode and passed to
 * xfs_imap_to_bp() to get a buffer and dinode for a given inode.
 */
struct xfs_imap {
	xfs_daddr_t	im_blkno;	/* starting BB of inode chunk */
	unsigned short	im_len;		/* length in BBs of inode chunk */
	unsigned short	im_boffset;	/* inode offset in block in bytes */
};

int	xfs_imap_to_bp(struct xfs_mount *mp, struct xfs_trans *tp,
		       struct xfs_imap *imap, struct xfs_buf **bpp);
void	xfs_dinode_calc_crc(struct xfs_mount *mp, struct xfs_dinode *dip);
void	xfs_inode_to_disk(struct xfs_inode *ip, struct xfs_dinode *to,
			  xfs_lsn_t lsn);
int	xfs_inode_from_disk(struct xfs_inode *ip, struct xfs_dinode *from);

xfs_failaddr_t xfs_dinode_verify(struct xfs_mount *mp, xfs_ino_t ino,
			   struct xfs_dinode *dip);
xfs_failaddr_t xfs_dinode_verify_metadir(struct xfs_mount *mp,
		struct xfs_dinode *dip, uint16_t mode, uint16_t flags,
		uint64_t flags2);
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

static inline bool
xfs_dinode_good_version(struct xfs_mount *mp, uint8_t version)
{
	if (xfs_has_v3inodes(mp))
		return version == 3;
	return version == 1 || version == 2;
}


#endif	/* __XFS_INODE_BUF_H__ */
