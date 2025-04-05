// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_ag.h"
#include "xfs_inode.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_icache.h"
#include "xfs_trans.h"
#include "xfs_ialloc.h"
#include "xfs_dir2.h"
#include "xfs_health.h"
#include "xfs_metafile.h"

#include <linux/iversion.h>

/*
 * If we are doing readahead on an inode buffer, we might be in log recovery
 * reading an inode allocation buffer that hasn't yet been replayed, and hence
 * has not had the inode cores stamped into it. Hence for readahead, the buffer
 * may be potentially invalid.
 *
 * If the readahead buffer is invalid, we need to mark it with an error and
 * clear the DONE status of the buffer so that a followup read will re-read it
 * from disk. We don't report the error otherwise to avoid warnings during log
 * recovery and we don't get unnecessary panics on debug kernels. We use EIO here
 * because all we want to do is say readahead failed; there is no-one to report
 * the error to, so this will distinguish it from a non-ra verifier failure.
 * Changes to this readahead error behaviour also need to be reflected in
 * xfs_dquot_buf_readahead_verify().
 */
static void
xfs_inode_buf_verify(
	struct xfs_buf	*bp,
	bool		readahead)
{
	struct xfs_mount *mp = bp->b_mount;
	int		i;
	int		ni;

	/*
	 * Validate the magic number and version of every inode in the buffer
	 */
	ni = XFS_BB_TO_FSB(mp, bp->b_length) * mp->m_sb.sb_inopblock;
	for (i = 0; i < ni; i++) {
		struct xfs_dinode	*dip;
		xfs_agino_t		unlinked_ino;
		int			di_ok;

		dip = xfs_buf_offset(bp, (i << mp->m_sb.sb_inodelog));
		unlinked_ino = be32_to_cpu(dip->di_next_unlinked);
		di_ok = xfs_verify_magic16(bp, dip->di_magic) &&
			xfs_dinode_good_version(mp, dip->di_version) &&
			xfs_verify_agino_or_null(bp->b_pag, unlinked_ino);
		if (unlikely(XFS_TEST_ERROR(!di_ok, mp,
						XFS_ERRTAG_ITOBP_INOTOBP))) {
			if (readahead) {
				bp->b_flags &= ~XBF_DONE;
				xfs_buf_ioerror(bp, -EIO);
				return;
			}

#ifdef DEBUG
			xfs_alert(mp,
				"bad inode magic/vsn daddr %lld #%d (magic=%x)",
				(unsigned long long)xfs_buf_daddr(bp), i,
				be16_to_cpu(dip->di_magic));
#endif
			xfs_buf_verifier_error(bp, -EFSCORRUPTED,
					__func__, dip, sizeof(*dip),
					NULL);
			return;
		}
	}
}


static void
xfs_inode_buf_read_verify(
	struct xfs_buf	*bp)
{
	xfs_inode_buf_verify(bp, false);
}

static void
xfs_inode_buf_readahead_verify(
	struct xfs_buf	*bp)
{
	xfs_inode_buf_verify(bp, true);
}

static void
xfs_inode_buf_write_verify(
	struct xfs_buf	*bp)
{
	xfs_inode_buf_verify(bp, false);
}

const struct xfs_buf_ops xfs_inode_buf_ops = {
	.name = "xfs_inode",
	.magic16 = { cpu_to_be16(XFS_DINODE_MAGIC),
		     cpu_to_be16(XFS_DINODE_MAGIC) },
	.verify_read = xfs_inode_buf_read_verify,
	.verify_write = xfs_inode_buf_write_verify,
};

const struct xfs_buf_ops xfs_inode_buf_ra_ops = {
	.name = "xfs_inode_ra",
	.magic16 = { cpu_to_be16(XFS_DINODE_MAGIC),
		     cpu_to_be16(XFS_DINODE_MAGIC) },
	.verify_read = xfs_inode_buf_readahead_verify,
	.verify_write = xfs_inode_buf_write_verify,
};


/*
 * This routine is called to map an inode to the buffer containing the on-disk
 * version of the inode.  It returns a pointer to the buffer containing the
 * on-disk inode in the bpp parameter.
 */
int
xfs_imap_to_bp(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_imap		*imap,
	struct xfs_buf		**bpp)
{
	int			error;

	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp, imap->im_blkno,
			imap->im_len, XBF_UNMAPPED, bpp, &xfs_inode_buf_ops);
	if (xfs_metadata_is_sick(error))
		xfs_agno_mark_sick(mp, xfs_daddr_to_agno(mp, imap->im_blkno),
				XFS_SICK_AG_INODES);
	return error;
}

static inline struct timespec64 xfs_inode_decode_bigtime(uint64_t ts)
{
	struct timespec64	tv;
	uint32_t		n;

	tv.tv_sec = xfs_bigtime_to_unix(div_u64_rem(ts, NSEC_PER_SEC, &n));
	tv.tv_nsec = n;

	return tv;
}

/* Convert an ondisk timestamp to an incore timestamp. */
struct timespec64
xfs_inode_from_disk_ts(
	struct xfs_dinode		*dip,
	const xfs_timestamp_t		ts)
{
	struct timespec64		tv;
	struct xfs_legacy_timestamp	*lts;

	if (xfs_dinode_has_bigtime(dip))
		return xfs_inode_decode_bigtime(be64_to_cpu(ts));

	lts = (struct xfs_legacy_timestamp *)&ts;
	tv.tv_sec = (int)be32_to_cpu(lts->t_sec);
	tv.tv_nsec = (int)be32_to_cpu(lts->t_nsec);

	return tv;
}

int
xfs_inode_from_disk(
	struct xfs_inode	*ip,
	struct xfs_dinode	*from)
{
	struct inode		*inode = VFS_I(ip);
	int			error;
	xfs_failaddr_t		fa;

	ASSERT(ip->i_cowfp == NULL);

	fa = xfs_dinode_verify(ip->i_mount, ip->i_ino, from);
	if (fa) {
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, "dinode", from,
				sizeof(*from), fa);
		return -EFSCORRUPTED;
	}

	/*
	 * First get the permanent information that is needed to allocate an
	 * inode. If the inode is unused, mode is zero and we shouldn't mess
	 * with the uninitialized part of it.
	 */
	if (!xfs_has_v3inodes(ip->i_mount))
		ip->i_flushiter = be16_to_cpu(from->di_flushiter);
	inode->i_generation = be32_to_cpu(from->di_gen);
	inode->i_mode = be16_to_cpu(from->di_mode);
	if (!inode->i_mode)
		return 0;

	/*
	 * Convert v1 inodes immediately to v2 inode format as this is the
	 * minimum inode version format we support in the rest of the code.
	 * They will also be unconditionally written back to disk as v2 inodes.
	 */
	if (unlikely(from->di_version == 1)) {
		/* di_metatype used to be di_onlink */
		set_nlink(inode, be16_to_cpu(from->di_metatype));
		ip->i_projid = 0;
	} else {
		set_nlink(inode, be32_to_cpu(from->di_nlink));
		ip->i_projid = (prid_t)be16_to_cpu(from->di_projid_hi) << 16 |
					be16_to_cpu(from->di_projid_lo);
		if (xfs_dinode_is_metadir(from))
			ip->i_metatype = be16_to_cpu(from->di_metatype);
	}

	i_uid_write(inode, be32_to_cpu(from->di_uid));
	i_gid_write(inode, be32_to_cpu(from->di_gid));

	/*
	 * Time is signed, so need to convert to signed 32 bit before
	 * storing in inode timestamp which may be 64 bit. Otherwise
	 * a time before epoch is converted to a time long after epoch
	 * on 64 bit systems.
	 */
	inode_set_atime_to_ts(inode,
			      xfs_inode_from_disk_ts(from, from->di_atime));
	inode_set_mtime_to_ts(inode,
			      xfs_inode_from_disk_ts(from, from->di_mtime));
	inode_set_ctime_to_ts(inode,
			      xfs_inode_from_disk_ts(from, from->di_ctime));

	ip->i_disk_size = be64_to_cpu(from->di_size);
	ip->i_nblocks = be64_to_cpu(from->di_nblocks);
	ip->i_extsize = be32_to_cpu(from->di_extsize);
	ip->i_forkoff = from->di_forkoff;
	ip->i_diflags = be16_to_cpu(from->di_flags);
	ip->i_next_unlinked = be32_to_cpu(from->di_next_unlinked);

	if (from->di_dmevmask || from->di_dmstate)
		xfs_iflags_set(ip, XFS_IPRESERVE_DM_FIELDS);

	if (xfs_has_v3inodes(ip->i_mount)) {
		inode_set_iversion_queried(inode,
					   be64_to_cpu(from->di_changecount));
		ip->i_crtime = xfs_inode_from_disk_ts(from, from->di_crtime);
		ip->i_diflags2 = be64_to_cpu(from->di_flags2);
		ip->i_cowextsize = be32_to_cpu(from->di_cowextsize);
	}

	error = xfs_iformat_data_fork(ip, from);
	if (error)
		return error;
	if (from->di_forkoff) {
		error = xfs_iformat_attr_fork(ip, from);
		if (error)
			goto out_destroy_data_fork;
	}
	if (xfs_is_reflink_inode(ip))
		xfs_ifork_init_cow(ip);
	return 0;

out_destroy_data_fork:
	xfs_idestroy_fork(&ip->i_df);
	return error;
}

/* Convert an incore timestamp to an ondisk timestamp. */
static inline xfs_timestamp_t
xfs_inode_to_disk_ts(
	struct xfs_inode		*ip,
	const struct timespec64		tv)
{
	struct xfs_legacy_timestamp	*lts;
	xfs_timestamp_t			ts;

	if (xfs_inode_has_bigtime(ip))
		return cpu_to_be64(xfs_inode_encode_bigtime(tv));

	lts = (struct xfs_legacy_timestamp *)&ts;
	lts->t_sec = cpu_to_be32(tv.tv_sec);
	lts->t_nsec = cpu_to_be32(tv.tv_nsec);

	return ts;
}

static inline void
xfs_inode_to_disk_iext_counters(
	struct xfs_inode	*ip,
	struct xfs_dinode	*to)
{
	if (xfs_inode_has_large_extent_counts(ip)) {
		to->di_big_nextents = cpu_to_be64(xfs_ifork_nextents(&ip->i_df));
		to->di_big_anextents = cpu_to_be32(xfs_ifork_nextents(&ip->i_af));
		/*
		 * We might be upgrading the inode to use larger extent counters
		 * than was previously used. Hence zero the unused field.
		 */
		to->di_nrext64_pad = cpu_to_be16(0);
	} else {
		to->di_nextents = cpu_to_be32(xfs_ifork_nextents(&ip->i_df));
		to->di_anextents = cpu_to_be16(xfs_ifork_nextents(&ip->i_af));
	}
}

void
xfs_inode_to_disk(
	struct xfs_inode	*ip,
	struct xfs_dinode	*to,
	xfs_lsn_t		lsn)
{
	struct inode		*inode = VFS_I(ip);

	to->di_magic = cpu_to_be16(XFS_DINODE_MAGIC);
	if (xfs_is_metadir_inode(ip))
		to->di_metatype = cpu_to_be16(ip->i_metatype);
	else
		to->di_metatype = 0;

	to->di_format = xfs_ifork_format(&ip->i_df);
	to->di_uid = cpu_to_be32(i_uid_read(inode));
	to->di_gid = cpu_to_be32(i_gid_read(inode));
	to->di_projid_lo = cpu_to_be16(ip->i_projid & 0xffff);
	to->di_projid_hi = cpu_to_be16(ip->i_projid >> 16);

	to->di_atime = xfs_inode_to_disk_ts(ip, inode_get_atime(inode));
	to->di_mtime = xfs_inode_to_disk_ts(ip, inode_get_mtime(inode));
	to->di_ctime = xfs_inode_to_disk_ts(ip, inode_get_ctime(inode));
	to->di_nlink = cpu_to_be32(inode->i_nlink);
	to->di_gen = cpu_to_be32(inode->i_generation);
	to->di_mode = cpu_to_be16(inode->i_mode);

	to->di_size = cpu_to_be64(ip->i_disk_size);
	to->di_nblocks = cpu_to_be64(ip->i_nblocks);
	to->di_extsize = cpu_to_be32(ip->i_extsize);
	to->di_forkoff = ip->i_forkoff;
	to->di_aformat = xfs_ifork_format(&ip->i_af);
	to->di_flags = cpu_to_be16(ip->i_diflags);

	if (xfs_has_v3inodes(ip->i_mount)) {
		to->di_version = 3;
		to->di_changecount = cpu_to_be64(inode_peek_iversion(inode));
		to->di_crtime = xfs_inode_to_disk_ts(ip, ip->i_crtime);
		to->di_flags2 = cpu_to_be64(ip->i_diflags2);
		to->di_cowextsize = cpu_to_be32(ip->i_cowextsize);
		to->di_ino = cpu_to_be64(ip->i_ino);
		to->di_lsn = cpu_to_be64(lsn);
		memset(to->di_pad2, 0, sizeof(to->di_pad2));
		uuid_copy(&to->di_uuid, &ip->i_mount->m_sb.sb_meta_uuid);
		to->di_v3_pad = 0;
	} else {
		to->di_version = 2;
		to->di_flushiter = cpu_to_be16(ip->i_flushiter);
		memset(to->di_v2_pad, 0, sizeof(to->di_v2_pad));
	}

	xfs_inode_to_disk_iext_counters(ip, to);
}

static xfs_failaddr_t
xfs_dinode_verify_fork(
	struct xfs_dinode	*dip,
	struct xfs_mount	*mp,
	int			whichfork)
{
	xfs_extnum_t		di_nextents;
	xfs_extnum_t		max_extents;
	mode_t			mode = be16_to_cpu(dip->di_mode);
	uint32_t		fork_size = XFS_DFORK_SIZE(dip, mp, whichfork);
	uint32_t		fork_format = XFS_DFORK_FORMAT(dip, whichfork);

	di_nextents = xfs_dfork_nextents(dip, whichfork);

	/*
	 * For fork types that can contain local data, check that the fork
	 * format matches the size of local data contained within the fork.
	 */
	if (whichfork == XFS_DATA_FORK) {
		/*
		 * A directory small enough to fit in the inode must be stored
		 * in local format.  The directory sf <-> extents conversion
		 * code updates the directory size accordingly.  Directories
		 * being truncated have zero size and are not subject to this
		 * check.
		 */
		if (S_ISDIR(mode)) {
			if (dip->di_size &&
			    be64_to_cpu(dip->di_size) <= fork_size &&
			    fork_format != XFS_DINODE_FMT_LOCAL)
				return __this_address;
		}

		/*
		 * A symlink with a target small enough to fit in the inode can
		 * be stored in extents format if xattrs were added (thus
		 * converting the data fork from shortform to remote format)
		 * and then removed.
		 */
		if (S_ISLNK(mode)) {
			if (be64_to_cpu(dip->di_size) <= fork_size &&
			    fork_format != XFS_DINODE_FMT_EXTENTS &&
			    fork_format != XFS_DINODE_FMT_LOCAL)
				return __this_address;
		}

		/*
		 * For all types, check that when the size says the fork should
		 * be in extent or btree format, the inode isn't claiming to be
		 * in local format.
		 */
		if (be64_to_cpu(dip->di_size) > fork_size &&
		    fork_format == XFS_DINODE_FMT_LOCAL)
			return __this_address;
	}

	switch (fork_format) {
	case XFS_DINODE_FMT_LOCAL:
		/*
		 * No local regular files yet.
		 */
		if (S_ISREG(mode) && whichfork == XFS_DATA_FORK)
			return __this_address;
		if (di_nextents)
			return __this_address;
		break;
	case XFS_DINODE_FMT_EXTENTS:
		if (di_nextents > XFS_DFORK_MAXEXT(dip, mp, whichfork))
			return __this_address;
		break;
	case XFS_DINODE_FMT_BTREE:
		max_extents = xfs_iext_max_nextents(
					xfs_dinode_has_large_extent_counts(dip),
					whichfork);
		if (di_nextents > max_extents)
			return __this_address;
		break;
	case XFS_DINODE_FMT_META_BTREE:
		if (!xfs_has_metadir(mp))
			return __this_address;
		if (!(dip->di_flags2 & cpu_to_be64(XFS_DIFLAG2_METADATA)))
			return __this_address;
		switch (be16_to_cpu(dip->di_metatype)) {
		case XFS_METAFILE_RTRMAP:
			/*
			 * growfs must create the rtrmap inodes before adding a
			 * realtime volume to the filesystem, so we cannot use
			 * the rtrmapbt predicate here.
			 */
			if (!xfs_has_rmapbt(mp))
				return __this_address;
			break;
		case XFS_METAFILE_RTREFCOUNT:
			/* same comment about growfs and rmap inodes applies */
			if (!xfs_has_reflink(mp))
				return __this_address;
			break;
		default:
			return __this_address;
		}
		break;
	default:
		return __this_address;
	}
	return NULL;
}

static xfs_failaddr_t
xfs_dinode_verify_forkoff(
	struct xfs_dinode	*dip,
	struct xfs_mount	*mp)
{
	if (!dip->di_forkoff)
		return NULL;

	switch (dip->di_format)  {
	case XFS_DINODE_FMT_DEV:
		if (dip->di_forkoff != (roundup(sizeof(xfs_dev_t), 8) >> 3))
			return __this_address;
		break;
	case XFS_DINODE_FMT_META_BTREE:
		if (!xfs_has_metadir(mp) || !xfs_has_parent(mp))
			return __this_address;
		fallthrough;
	case XFS_DINODE_FMT_LOCAL:	/* fall through ... */
	case XFS_DINODE_FMT_EXTENTS:    /* fall through ... */
	case XFS_DINODE_FMT_BTREE:
		if (dip->di_forkoff >= (XFS_LITINO(mp) >> 3))
			return __this_address;
		break;
	default:
		return __this_address;
	}
	return NULL;
}

static xfs_failaddr_t
xfs_dinode_verify_nrext64(
	struct xfs_mount	*mp,
	struct xfs_dinode	*dip)
{
	if (xfs_dinode_has_large_extent_counts(dip)) {
		if (!xfs_has_large_extent_counts(mp))
			return __this_address;
		if (dip->di_nrext64_pad != 0)
			return __this_address;
	} else if (dip->di_version >= 3) {
		if (dip->di_v3_pad != 0)
			return __this_address;
	}

	return NULL;
}

/*
 * Validate all the picky requirements we have for a file that claims to be
 * filesystem metadata.
 */
xfs_failaddr_t
xfs_dinode_verify_metadir(
	struct xfs_mount	*mp,
	struct xfs_dinode	*dip,
	uint16_t		mode,
	uint16_t		flags,
	uint64_t		flags2)
{
	if (!xfs_has_metadir(mp))
		return __this_address;

	/* V5 filesystem only */
	if (dip->di_version < 3)
		return __this_address;

	if (be16_to_cpu(dip->di_metatype) >= XFS_METAFILE_MAX)
		return __this_address;

	/* V3 inode fields that are always zero */
	if ((flags2 & XFS_DIFLAG2_NREXT64) && dip->di_nrext64_pad)
		return __this_address;
	if (!(flags2 & XFS_DIFLAG2_NREXT64) && dip->di_flushiter)
		return __this_address;

	/* Metadata files can only be directories or regular files */
	if (!S_ISDIR(mode) && !S_ISREG(mode))
		return __this_address;

	/* They must have zero access permissions */
	if (mode & 0777)
		return __this_address;

	/* DMAPI event and state masks are zero */
	if (dip->di_dmevmask || dip->di_dmstate)
		return __this_address;

	/*
	 * User and group IDs must be zero.  The project ID is used for
	 * grouping inodes.  Metadata inodes are never accounted to quotas.
	 */
	if (dip->di_uid || dip->di_gid)
		return __this_address;

	/* Mandatory inode flags must be set */
	if (S_ISDIR(mode)) {
		if ((flags & XFS_METADIR_DIFLAGS) != XFS_METADIR_DIFLAGS)
			return __this_address;
	} else {
		if ((flags & XFS_METAFILE_DIFLAGS) != XFS_METAFILE_DIFLAGS)
			return __this_address;
	}

	/* dax flags2 must not be set */
	if (flags2 & XFS_DIFLAG2_DAX)
		return __this_address;

	return NULL;
}

xfs_failaddr_t
xfs_dinode_verify(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	struct xfs_dinode	*dip)
{
	xfs_failaddr_t		fa;
	uint16_t		mode;
	uint16_t		flags;
	uint64_t		flags2;
	uint64_t		di_size;
	xfs_extnum_t		nextents;
	xfs_extnum_t		naextents;
	xfs_filblks_t		nblocks;

	if (dip->di_magic != cpu_to_be16(XFS_DINODE_MAGIC))
		return __this_address;

	/* Verify v3 integrity information first */
	if (dip->di_version >= 3) {
		if (!xfs_has_v3inodes(mp))
			return __this_address;
		if (!xfs_verify_cksum((char *)dip, mp->m_sb.sb_inodesize,
				      XFS_DINODE_CRC_OFF))
			return __this_address;
		if (be64_to_cpu(dip->di_ino) != ino)
			return __this_address;
		if (!uuid_equal(&dip->di_uuid, &mp->m_sb.sb_meta_uuid))
			return __this_address;
	}

	/*
	 * Historical note: xfsprogs in the 3.2 era set up its incore inodes to
	 * have di_nlink track the link count, even if the actual filesystem
	 * only supported V1 inodes (i.e. di_onlink).  When writing out the
	 * ondisk inode, it would set both the ondisk di_nlink and di_onlink to
	 * the the incore di_nlink value, which is why we cannot check for
	 * di_nlink==0 on a V1 inode.  V2/3 inodes would get written out with
	 * di_onlink==0, so we can check that.
	 */
	if (dip->di_version == 2) {
		if (dip->di_metatype)
			return __this_address;
	} else if (dip->di_version >= 3) {
		if (!xfs_dinode_is_metadir(dip) && dip->di_metatype)
			return __this_address;
	}

	/* don't allow invalid i_size */
	di_size = be64_to_cpu(dip->di_size);
	if (di_size & (1ULL << 63))
		return __this_address;

	mode = be16_to_cpu(dip->di_mode);
	if (mode && xfs_mode_to_ftype(mode) == XFS_DIR3_FT_UNKNOWN)
		return __this_address;

	/*
	 * No zero-length symlinks/dirs unless they're unlinked and hence being
	 * inactivated.
	 */
	if ((S_ISLNK(mode) || S_ISDIR(mode)) && di_size == 0) {
		if (dip->di_version > 1) {
			if (dip->di_nlink)
				return __this_address;
		} else {
			/* di_metatype used to be di_onlink */
			if (dip->di_metatype)
				return __this_address;
		}
	}

	fa = xfs_dinode_verify_nrext64(mp, dip);
	if (fa)
		return fa;

	nextents = xfs_dfork_data_extents(dip);
	naextents = xfs_dfork_attr_extents(dip);
	nblocks = be64_to_cpu(dip->di_nblocks);

	/* Fork checks carried over from xfs_iformat_fork */
	if (mode && nextents + naextents > nblocks)
		return __this_address;

	if (S_ISDIR(mode) && nextents > mp->m_dir_geo->max_extents)
		return __this_address;

	if (mode && XFS_DFORK_BOFF(dip) > mp->m_sb.sb_inodesize)
		return __this_address;

	flags = be16_to_cpu(dip->di_flags);

	if (mode && (flags & XFS_DIFLAG_REALTIME) && !mp->m_rtdev_targp)
		return __this_address;

	/* check for illegal values of forkoff */
	fa = xfs_dinode_verify_forkoff(dip, mp);
	if (fa)
		return fa;

	/* Do we have appropriate data fork formats for the mode? */
	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		if (dip->di_format != XFS_DINODE_FMT_DEV)
			return __this_address;
		break;
	case S_IFREG:
	case S_IFLNK:
	case S_IFDIR:
		fa = xfs_dinode_verify_fork(dip, mp, XFS_DATA_FORK);
		if (fa)
			return fa;
		break;
	case 0:
		/* Uninitialized inode ok. */
		break;
	default:
		return __this_address;
	}

	if (dip->di_forkoff) {
		fa = xfs_dinode_verify_fork(dip, mp, XFS_ATTR_FORK);
		if (fa)
			return fa;
	} else {
		/*
		 * If there is no fork offset, this may be a freshly-made inode
		 * in a new disk cluster, in which case di_aformat is zeroed.
		 * Otherwise, such an inode must be in EXTENTS format; this goes
		 * for freed inodes as well.
		 */
		switch (dip->di_aformat) {
		case 0:
		case XFS_DINODE_FMT_EXTENTS:
			break;
		default:
			return __this_address;
		}
		if (naextents)
			return __this_address;
	}

	/* extent size hint validation */
	fa = xfs_inode_validate_extsize(mp, be32_to_cpu(dip->di_extsize),
			mode, flags);
	if (fa)
		return fa;

	/* only version 3 or greater inodes are extensively verified here */
	if (dip->di_version < 3)
		return NULL;

	flags2 = be64_to_cpu(dip->di_flags2);

	/* don't allow reflink/cowextsize if we don't have reflink */
	if ((flags2 & (XFS_DIFLAG2_REFLINK | XFS_DIFLAG2_COWEXTSIZE)) &&
	     !xfs_has_reflink(mp))
		return __this_address;

	/* only regular files get reflink */
	if ((flags2 & XFS_DIFLAG2_REFLINK) && (mode & S_IFMT) != S_IFREG)
		return __this_address;

	/* don't let reflink and realtime mix */
	if ((flags2 & XFS_DIFLAG2_REFLINK) && (flags & XFS_DIFLAG_REALTIME) &&
	    !xfs_has_rtreflink(mp))
		return __this_address;

	/* COW extent size hint validation */
	fa = xfs_inode_validate_cowextsize(mp, be32_to_cpu(dip->di_cowextsize),
			mode, flags, flags2);
	if (fa)
		return fa;

	/* bigtime iflag can only happen on bigtime filesystems */
	if (xfs_dinode_has_bigtime(dip) &&
	    !xfs_has_bigtime(mp))
		return __this_address;

	if (flags2 & XFS_DIFLAG2_METADATA) {
		fa = xfs_dinode_verify_metadir(mp, dip, mode, flags, flags2);
		if (fa)
			return fa;
	}

	/* metadata inodes containing btrees always have zero extent count */
	if (XFS_DFORK_FORMAT(dip, XFS_DATA_FORK) != XFS_DINODE_FMT_META_BTREE) {
		if (nextents + naextents == 0 && nblocks != 0)
			return __this_address;
	}

	return NULL;
}

void
xfs_dinode_calc_crc(
	struct xfs_mount	*mp,
	struct xfs_dinode	*dip)
{
	uint32_t		crc;

	if (dip->di_version < 3)
		return;

	ASSERT(xfs_has_crc(mp));
	crc = xfs_start_cksum_update((char *)dip, mp->m_sb.sb_inodesize,
			      XFS_DINODE_CRC_OFF);
	dip->di_crc = xfs_end_cksum(crc);
}

/*
 * Validate di_extsize hint.
 *
 * 1. Extent size hint is only valid for directories and regular files.
 * 2. FS_XFLAG_EXTSIZE is only valid for regular files.
 * 3. FS_XFLAG_EXTSZINHERIT is only valid for directories.
 * 4. Hint cannot be larger than MAXTEXTLEN.
 * 5. Can be changed on directories at any time.
 * 6. Hint value of 0 turns off hints, clears inode flags.
 * 7. Extent size must be a multiple of the appropriate block size.
 *    For realtime files, this is the rt extent size.
 * 8. For non-realtime files, the extent size hint must be limited
 *    to half the AG size to avoid alignment extending the extent beyond the
 *    limits of the AG.
 */
xfs_failaddr_t
xfs_inode_validate_extsize(
	struct xfs_mount		*mp,
	uint32_t			extsize,
	uint16_t			mode,
	uint16_t			flags)
{
	bool				rt_flag;
	bool				hint_flag;
	bool				inherit_flag;
	uint32_t			extsize_bytes;
	uint32_t			blocksize_bytes;

	rt_flag = (flags & XFS_DIFLAG_REALTIME);
	hint_flag = (flags & XFS_DIFLAG_EXTSIZE);
	inherit_flag = (flags & XFS_DIFLAG_EXTSZINHERIT);
	extsize_bytes = XFS_FSB_TO_B(mp, extsize);

	/*
	 * This comment describes a historic gap in this verifier function.
	 *
	 * For a directory with both RTINHERIT and EXTSZINHERIT flags set, this
	 * function has never checked that the extent size hint is an integer
	 * multiple of the realtime extent size.  Since we allow users to set
	 * this combination  on non-rt filesystems /and/ to change the rt
	 * extent size when adding a rt device to a filesystem, the net effect
	 * is that users can configure a filesystem anticipating one rt
	 * geometry and change their minds later.  Directories do not use the
	 * extent size hint, so this is harmless for them.
	 *
	 * If a directory with a misaligned extent size hint is allowed to
	 * propagate that hint into a new regular realtime file, the result
	 * is that the inode cluster buffer verifier will trigger a corruption
	 * shutdown the next time it is run, because the verifier has always
	 * enforced the alignment rule for regular files.
	 *
	 * Because we allow administrators to set a new rt extent size when
	 * adding a rt section, we cannot add a check to this verifier because
	 * that will result a new source of directory corruption errors when
	 * reading an existing filesystem.  Instead, we rely on callers to
	 * decide when alignment checks are appropriate, and fix things up as
	 * needed.
	 */

	if (rt_flag)
		blocksize_bytes = XFS_FSB_TO_B(mp, mp->m_sb.sb_rextsize);
	else
		blocksize_bytes = mp->m_sb.sb_blocksize;

	if ((hint_flag || inherit_flag) && !(S_ISDIR(mode) || S_ISREG(mode)))
		return __this_address;

	if (hint_flag && !S_ISREG(mode))
		return __this_address;

	if (inherit_flag && !S_ISDIR(mode))
		return __this_address;

	if ((hint_flag || inherit_flag) && extsize == 0)
		return __this_address;

	/* free inodes get flags set to zero but extsize remains */
	if (mode && !(hint_flag || inherit_flag) && extsize != 0)
		return __this_address;

	if (extsize_bytes % blocksize_bytes)
		return __this_address;

	if (extsize > XFS_MAX_BMBT_EXTLEN)
		return __this_address;

	if (!rt_flag && extsize > mp->m_sb.sb_agblocks / 2)
		return __this_address;

	return NULL;
}

/*
 * Validate di_cowextsize hint.
 *
 * 1. CoW extent size hint can only be set if reflink is enabled on the fs.
 *    The inode does not have to have any shared blocks, but it must be a v3.
 * 2. FS_XFLAG_COWEXTSIZE is only valid for directories and regular files;
 *    for a directory, the hint is propagated to new files.
 * 3. Can be changed on files & directories at any time.
 * 4. Hint value of 0 turns off hints, clears inode flags.
 * 5. Extent size must be a multiple of the appropriate block size.
 * 6. The extent size hint must be limited to half the AG size to avoid
 *    alignment extending the extent beyond the limits of the AG.
 */
xfs_failaddr_t
xfs_inode_validate_cowextsize(
	struct xfs_mount		*mp,
	uint32_t			cowextsize,
	uint16_t			mode,
	uint16_t			flags,
	uint64_t			flags2)
{
	bool				rt_flag;
	bool				hint_flag;
	uint32_t			cowextsize_bytes;
	uint32_t			blocksize_bytes;

	rt_flag = (flags & XFS_DIFLAG_REALTIME);
	hint_flag = (flags2 & XFS_DIFLAG2_COWEXTSIZE);
	cowextsize_bytes = XFS_FSB_TO_B(mp, cowextsize);

	/*
	 * Similar to extent size hints, a directory can be configured to
	 * propagate realtime status and a CoW extent size hint to newly
	 * created files even if there is no realtime device, and the hints on
	 * disk can become misaligned if the sysadmin changes the rt extent
	 * size while adding the realtime device.
	 *
	 * Therefore, we can only enforce the rextsize alignment check against
	 * regular realtime files, and rely on callers to decide when alignment
	 * checks are appropriate, and fix things up as needed.
	 */

	if (rt_flag)
		blocksize_bytes = XFS_FSB_TO_B(mp, mp->m_sb.sb_rextsize);
	else
		blocksize_bytes = mp->m_sb.sb_blocksize;

	if (hint_flag && !xfs_has_reflink(mp))
		return __this_address;

	if (hint_flag && !(S_ISDIR(mode) || S_ISREG(mode)))
		return __this_address;

	if (hint_flag && cowextsize == 0)
		return __this_address;

	/* free inodes get flags set to zero but cowextsize remains */
	if (mode && !hint_flag && cowextsize != 0)
		return __this_address;

	if (cowextsize_bytes % blocksize_bytes)
		return __this_address;

	if (cowextsize > XFS_MAX_BMBT_EXTLEN)
		return __this_address;

	if (!rt_flag && cowextsize > mp->m_sb.sb_agblocks / 2)
		return __this_address;

	return NULL;
}
