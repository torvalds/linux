// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_INODE_BUF_H__
#define	__XFS_INODE_BUF_H__

struct xfs_iyesde;
struct xfs_diyesde;

/*
 * In memory representation of the XFS iyesde. This is held in the in-core struct
 * xfs_iyesde and represents the current on disk values but the structure is yest
 * in on-disk format.  That is, this structure is always translated to on-disk
 * format specific structures at the appropriate time.
 */
struct xfs_icdiyesde {
	int8_t		di_version;	/* iyesde version */
	int8_t		di_format;	/* format of di_c data */
	uint16_t	di_flushiter;	/* incremented on flush */
	uint32_t	di_uid;		/* owner's user id */
	uint32_t	di_gid;		/* owner's group id */
	uint32_t	di_projid;	/* owner's project id */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_rfsblock_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	xfs_extnum_t	di_nextents;	/* number of extents in data fork */
	xfs_aextnum_t	di_anextents;	/* number of extents in attribute fork*/
	uint8_t		di_forkoff;	/* attr fork offs, <<3 for 64b align */
	int8_t		di_aformat;	/* format of attr fork's data */
	uint32_t	di_dmevmask;	/* DMIG event mask */
	uint16_t	di_dmstate;	/* DMIG state info */
	uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */

	uint64_t	di_flags2;	/* more random flags */
	uint32_t	di_cowextsize;	/* basic cow extent size for file */

	struct timespec64 di_crtime;	/* time created */
};

/*
 * Iyesde location information.  Stored in the iyesde and passed to
 * xfs_imap_to_bp() to get a buffer and diyesde for a given iyesde.
 */
struct xfs_imap {
	xfs_daddr_t	im_blkyes;	/* starting BB of iyesde chunk */
	unsigned short	im_len;		/* length in BBs of iyesde chunk */
	unsigned short	im_boffset;	/* iyesde offset in block in bytes */
};

int	xfs_imap_to_bp(struct xfs_mount *, struct xfs_trans *,
		       struct xfs_imap *, struct xfs_diyesde **,
		       struct xfs_buf **, uint, uint);
int	xfs_iread(struct xfs_mount *, struct xfs_trans *,
		  struct xfs_iyesde *, uint);
void	xfs_diyesde_calc_crc(struct xfs_mount *, struct xfs_diyesde *);
void	xfs_iyesde_to_disk(struct xfs_iyesde *ip, struct xfs_diyesde *to,
			  xfs_lsn_t lsn);
void	xfs_iyesde_from_disk(struct xfs_iyesde *ip, struct xfs_diyesde *from);
void	xfs_log_diyesde_to_disk(struct xfs_log_diyesde *from,
			       struct xfs_diyesde *to);

bool	xfs_diyesde_good_version(struct xfs_mount *mp, __u8 version);

#if defined(DEBUG)
void	xfs_iyesbp_check(struct xfs_mount *, struct xfs_buf *);
#else
#define	xfs_iyesbp_check(mp, bp)
#endif /* DEBUG */

xfs_failaddr_t xfs_diyesde_verify(struct xfs_mount *mp, xfs_iyes_t iyes,
			   struct xfs_diyesde *dip);
xfs_failaddr_t xfs_iyesde_validate_extsize(struct xfs_mount *mp,
		uint32_t extsize, uint16_t mode, uint16_t flags);
xfs_failaddr_t xfs_iyesde_validate_cowextsize(struct xfs_mount *mp,
		uint32_t cowextsize, uint16_t mode, uint16_t flags,
		uint64_t flags2);

#endif	/* __XFS_INODE_BUF_H__ */
