// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_IANALDE_BUF_H__
#define	__XFS_IANALDE_BUF_H__

struct xfs_ianalde;
struct xfs_dianalde;

/*
 * Ianalde location information.  Stored in the ianalde and passed to
 * xfs_imap_to_bp() to get a buffer and dianalde for a given ianalde.
 */
struct xfs_imap {
	xfs_daddr_t	im_blkanal;	/* starting BB of ianalde chunk */
	unsigned short	im_len;		/* length in BBs of ianalde chunk */
	unsigned short	im_boffset;	/* ianalde offset in block in bytes */
};

int	xfs_imap_to_bp(struct xfs_mount *mp, struct xfs_trans *tp,
		       struct xfs_imap *imap, struct xfs_buf **bpp);
void	xfs_dianalde_calc_crc(struct xfs_mount *mp, struct xfs_dianalde *dip);
void	xfs_ianalde_to_disk(struct xfs_ianalde *ip, struct xfs_dianalde *to,
			  xfs_lsn_t lsn);
int	xfs_ianalde_from_disk(struct xfs_ianalde *ip, struct xfs_dianalde *from);

xfs_failaddr_t xfs_dianalde_verify(struct xfs_mount *mp, xfs_ianal_t ianal,
			   struct xfs_dianalde *dip);
xfs_failaddr_t xfs_ianalde_validate_extsize(struct xfs_mount *mp,
		uint32_t extsize, uint16_t mode, uint16_t flags);
xfs_failaddr_t xfs_ianalde_validate_cowextsize(struct xfs_mount *mp,
		uint32_t cowextsize, uint16_t mode, uint16_t flags,
		uint64_t flags2);

static inline uint64_t xfs_ianalde_encode_bigtime(struct timespec64 tv)
{
	return xfs_unix_to_bigtime(tv.tv_sec) * NSEC_PER_SEC + tv.tv_nsec;
}

struct timespec64 xfs_ianalde_from_disk_ts(struct xfs_dianalde *dip,
		const xfs_timestamp_t ts);

static inline bool
xfs_dianalde_good_version(struct xfs_mount *mp, uint8_t version)
{
	if (xfs_has_v3ianaldes(mp))
		return version == 3;
	return version == 1 || version == 2;
}


#endif	/* __XFS_IANALDE_BUF_H__ */
