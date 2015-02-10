/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"

/*
 * Physical superblock buffer manipulations. Shared with libxfs in userspace.
 */

static const struct {
	short offset;
	short type;	/* 0 = integer
			 * 1 = binary / string (no translation)
			 */
} xfs_sb_info[] = {
	{ offsetof(xfs_sb_t, sb_magicnum),	0 },
	{ offsetof(xfs_sb_t, sb_blocksize),	0 },
	{ offsetof(xfs_sb_t, sb_dblocks),	0 },
	{ offsetof(xfs_sb_t, sb_rblocks),	0 },
	{ offsetof(xfs_sb_t, sb_rextents),	0 },
	{ offsetof(xfs_sb_t, sb_uuid),		1 },
	{ offsetof(xfs_sb_t, sb_logstart),	0 },
	{ offsetof(xfs_sb_t, sb_rootino),	0 },
	{ offsetof(xfs_sb_t, sb_rbmino),	0 },
	{ offsetof(xfs_sb_t, sb_rsumino),	0 },
	{ offsetof(xfs_sb_t, sb_rextsize),	0 },
	{ offsetof(xfs_sb_t, sb_agblocks),	0 },
	{ offsetof(xfs_sb_t, sb_agcount),	0 },
	{ offsetof(xfs_sb_t, sb_rbmblocks),	0 },
	{ offsetof(xfs_sb_t, sb_logblocks),	0 },
	{ offsetof(xfs_sb_t, sb_versionnum),	0 },
	{ offsetof(xfs_sb_t, sb_sectsize),	0 },
	{ offsetof(xfs_sb_t, sb_inodesize),	0 },
	{ offsetof(xfs_sb_t, sb_inopblock),	0 },
	{ offsetof(xfs_sb_t, sb_fname[0]),	1 },
	{ offsetof(xfs_sb_t, sb_blocklog),	0 },
	{ offsetof(xfs_sb_t, sb_sectlog),	0 },
	{ offsetof(xfs_sb_t, sb_inodelog),	0 },
	{ offsetof(xfs_sb_t, sb_inopblog),	0 },
	{ offsetof(xfs_sb_t, sb_agblklog),	0 },
	{ offsetof(xfs_sb_t, sb_rextslog),	0 },
	{ offsetof(xfs_sb_t, sb_inprogress),	0 },
	{ offsetof(xfs_sb_t, sb_imax_pct),	0 },
	{ offsetof(xfs_sb_t, sb_icount),	0 },
	{ offsetof(xfs_sb_t, sb_ifree),		0 },
	{ offsetof(xfs_sb_t, sb_fdblocks),	0 },
	{ offsetof(xfs_sb_t, sb_frextents),	0 },
	{ offsetof(xfs_sb_t, sb_uquotino),	0 },
	{ offsetof(xfs_sb_t, sb_gquotino),	0 },
	{ offsetof(xfs_sb_t, sb_qflags),	0 },
	{ offsetof(xfs_sb_t, sb_flags),		0 },
	{ offsetof(xfs_sb_t, sb_shared_vn),	0 },
	{ offsetof(xfs_sb_t, sb_inoalignmt),	0 },
	{ offsetof(xfs_sb_t, sb_unit),		0 },
	{ offsetof(xfs_sb_t, sb_width),		0 },
	{ offsetof(xfs_sb_t, sb_dirblklog),	0 },
	{ offsetof(xfs_sb_t, sb_logsectlog),	0 },
	{ offsetof(xfs_sb_t, sb_logsectsize),	0 },
	{ offsetof(xfs_sb_t, sb_logsunit),	0 },
	{ offsetof(xfs_sb_t, sb_features2),	0 },
	{ offsetof(xfs_sb_t, sb_bad_features2),	0 },
	{ offsetof(xfs_sb_t, sb_features_compat),	0 },
	{ offsetof(xfs_sb_t, sb_features_ro_compat),	0 },
	{ offsetof(xfs_sb_t, sb_features_incompat),	0 },
	{ offsetof(xfs_sb_t, sb_features_log_incompat),	0 },
	{ offsetof(xfs_sb_t, sb_crc),		0 },
	{ offsetof(xfs_sb_t, sb_pad),		0 },
	{ offsetof(xfs_sb_t, sb_pquotino),	0 },
	{ offsetof(xfs_sb_t, sb_lsn),		0 },
	{ sizeof(xfs_sb_t),			0 }
};

/*
 * Reference counting access wrappers to the perag structures.
 * Because we never free per-ag structures, the only thing we
 * have to protect against changes is the tree structure itself.
 */
struct xfs_perag *
xfs_perag_get(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	struct xfs_perag	*pag;
	int			ref = 0;

	rcu_read_lock();
	pag = radix_tree_lookup(&mp->m_perag_tree, agno);
	if (pag) {
		ASSERT(atomic_read(&pag->pag_ref) >= 0);
		ref = atomic_inc_return(&pag->pag_ref);
	}
	rcu_read_unlock();
	trace_xfs_perag_get(mp, agno, ref, _RET_IP_);
	return pag;
}

/*
 * search from @first to find the next perag with the given tag set.
 */
struct xfs_perag *
xfs_perag_get_tag(
	struct xfs_mount	*mp,
	xfs_agnumber_t		first,
	int			tag)
{
	struct xfs_perag	*pag;
	int			found;
	int			ref;

	rcu_read_lock();
	found = radix_tree_gang_lookup_tag(&mp->m_perag_tree,
					(void **)&pag, first, 1, tag);
	if (found <= 0) {
		rcu_read_unlock();
		return NULL;
	}
	ref = atomic_inc_return(&pag->pag_ref);
	rcu_read_unlock();
	trace_xfs_perag_get_tag(mp, pag->pag_agno, ref, _RET_IP_);
	return pag;
}

void
xfs_perag_put(
	struct xfs_perag	*pag)
{
	int	ref;

	ASSERT(atomic_read(&pag->pag_ref) > 0);
	ref = atomic_dec_return(&pag->pag_ref);
	trace_xfs_perag_put(pag->pag_mount, pag->pag_agno, ref, _RET_IP_);
}

/*
 * Check the validity of the SB found.
 */
STATIC int
xfs_mount_validate_sb(
	xfs_mount_t	*mp,
	xfs_sb_t	*sbp,
	bool		check_inprogress,
	bool		check_version)
{

	/*
	 * If the log device and data device have the
	 * same device number, the log is internal.
	 * Consequently, the sb_logstart should be non-zero.  If
	 * we have a zero sb_logstart in this case, we may be trying to mount
	 * a volume filesystem in a non-volume manner.
	 */
	if (sbp->sb_magicnum != XFS_SB_MAGIC) {
		xfs_warn(mp, "bad magic number");
		return -EWRONGFS;
	}


	if (!xfs_sb_good_version(sbp)) {
		xfs_warn(mp, "bad version");
		return -EWRONGFS;
	}

	/*
	 * Version 5 superblock feature mask validation. Reject combinations the
	 * kernel cannot support up front before checking anything else. For
	 * write validation, we don't need to check feature masks.
	 */
	if (check_version && XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5) {
		if (xfs_sb_has_compat_feature(sbp,
					XFS_SB_FEAT_COMPAT_UNKNOWN)) {
			xfs_warn(mp,
"Superblock has unknown compatible features (0x%x) enabled.\n"
"Using a more recent kernel is recommended.",
				(sbp->sb_features_compat &
						XFS_SB_FEAT_COMPAT_UNKNOWN));
		}

		if (xfs_sb_has_ro_compat_feature(sbp,
					XFS_SB_FEAT_RO_COMPAT_UNKNOWN)) {
			xfs_alert(mp,
"Superblock has unknown read-only compatible features (0x%x) enabled.",
				(sbp->sb_features_ro_compat &
						XFS_SB_FEAT_RO_COMPAT_UNKNOWN));
			if (!(mp->m_flags & XFS_MOUNT_RDONLY)) {
				xfs_warn(mp,
"Attempted to mount read-only compatible filesystem read-write.\n"
"Filesystem can only be safely mounted read only.");
				return -EINVAL;
			}
		}
		if (xfs_sb_has_incompat_feature(sbp,
					XFS_SB_FEAT_INCOMPAT_UNKNOWN)) {
			xfs_warn(mp,
"Superblock has unknown incompatible features (0x%x) enabled.\n"
"Filesystem can not be safely mounted by this kernel.",
				(sbp->sb_features_incompat &
						XFS_SB_FEAT_INCOMPAT_UNKNOWN));
			return -EINVAL;
		}
	}

	if (xfs_sb_version_has_pquotino(sbp)) {
		if (sbp->sb_qflags & (XFS_OQUOTA_ENFD | XFS_OQUOTA_CHKD)) {
			xfs_notice(mp,
			   "Version 5 of Super block has XFS_OQUOTA bits.");
			return -EFSCORRUPTED;
		}
	} else if (sbp->sb_qflags & (XFS_PQUOTA_ENFD | XFS_GQUOTA_ENFD |
				XFS_PQUOTA_CHKD | XFS_GQUOTA_CHKD)) {
			xfs_notice(mp,
"Superblock earlier than Version 5 has XFS_[PQ]UOTA_{ENFD|CHKD} bits.");
			return -EFSCORRUPTED;
	}

	if (unlikely(
	    sbp->sb_logstart == 0 && mp->m_logdev_targp == mp->m_ddev_targp)) {
		xfs_warn(mp,
		"filesystem is marked as having an external log; "
		"specify logdev on the mount command line.");
		return -EINVAL;
	}

	if (unlikely(
	    sbp->sb_logstart != 0 && mp->m_logdev_targp != mp->m_ddev_targp)) {
		xfs_warn(mp,
		"filesystem is marked as having an internal log; "
		"do not specify logdev on the mount command line.");
		return -EINVAL;
	}

	/*
	 * More sanity checking.  Most of these were stolen directly from
	 * xfs_repair.
	 */
	if (unlikely(
	    sbp->sb_agcount <= 0					||
	    sbp->sb_sectsize < XFS_MIN_SECTORSIZE			||
	    sbp->sb_sectsize > XFS_MAX_SECTORSIZE			||
	    sbp->sb_sectlog < XFS_MIN_SECTORSIZE_LOG			||
	    sbp->sb_sectlog > XFS_MAX_SECTORSIZE_LOG			||
	    sbp->sb_sectsize != (1 << sbp->sb_sectlog)			||
	    sbp->sb_blocksize < XFS_MIN_BLOCKSIZE			||
	    sbp->sb_blocksize > XFS_MAX_BLOCKSIZE			||
	    sbp->sb_blocklog < XFS_MIN_BLOCKSIZE_LOG			||
	    sbp->sb_blocklog > XFS_MAX_BLOCKSIZE_LOG			||
	    sbp->sb_blocksize != (1 << sbp->sb_blocklog)		||
	    sbp->sb_dirblklog > XFS_MAX_BLOCKSIZE_LOG			||
	    sbp->sb_inodesize < XFS_DINODE_MIN_SIZE			||
	    sbp->sb_inodesize > XFS_DINODE_MAX_SIZE			||
	    sbp->sb_inodelog < XFS_DINODE_MIN_LOG			||
	    sbp->sb_inodelog > XFS_DINODE_MAX_LOG			||
	    sbp->sb_inodesize != (1 << sbp->sb_inodelog)		||
	    sbp->sb_logsunit > XLOG_MAX_RECORD_BSIZE			||
	    sbp->sb_inopblock != howmany(sbp->sb_blocksize,sbp->sb_inodesize) ||
	    (sbp->sb_blocklog - sbp->sb_inodelog != sbp->sb_inopblog)	||
	    (sbp->sb_rextsize * sbp->sb_blocksize > XFS_MAX_RTEXTSIZE)	||
	    (sbp->sb_rextsize * sbp->sb_blocksize < XFS_MIN_RTEXTSIZE)	||
	    (sbp->sb_imax_pct > 100 /* zero sb_imax_pct is valid */)	||
	    sbp->sb_dblocks == 0					||
	    sbp->sb_dblocks > XFS_MAX_DBLOCKS(sbp)			||
	    sbp->sb_dblocks < XFS_MIN_DBLOCKS(sbp)			||
	    sbp->sb_shared_vn != 0)) {
		xfs_notice(mp, "SB sanity check failed");
		return -EFSCORRUPTED;
	}

	/*
	 * Until this is fixed only page-sized or smaller data blocks work.
	 */
	if (unlikely(sbp->sb_blocksize > PAGE_SIZE)) {
		xfs_warn(mp,
		"File system with blocksize %d bytes. "
		"Only pagesize (%ld) or less will currently work.",
				sbp->sb_blocksize, PAGE_SIZE);
		return -ENOSYS;
	}

	/*
	 * Currently only very few inode sizes are supported.
	 */
	switch (sbp->sb_inodesize) {
	case 256:
	case 512:
	case 1024:
	case 2048:
		break;
	default:
		xfs_warn(mp, "inode size of %d bytes not supported",
				sbp->sb_inodesize);
		return -ENOSYS;
	}

	if (xfs_sb_validate_fsb_count(sbp, sbp->sb_dblocks) ||
	    xfs_sb_validate_fsb_count(sbp, sbp->sb_rblocks)) {
		xfs_warn(mp,
		"file system too large to be mounted on this system.");
		return -EFBIG;
	}

	if (check_inprogress && sbp->sb_inprogress) {
		xfs_warn(mp, "Offline file system operation in progress!");
		return -EFSCORRUPTED;
	}
	return 0;
}

void
xfs_sb_quota_from_disk(struct xfs_sb *sbp)
{
	/*
	 * older mkfs doesn't initialize quota inodes to NULLFSINO. This
	 * leads to in-core values having two different values for a quota
	 * inode to be invalid: 0 and NULLFSINO. Change it to a single value
	 * NULLFSINO.
	 *
	 * Note that this change affect only the in-core values. These
	 * values are not written back to disk unless any quota information
	 * is written to the disk. Even in that case, sb_pquotino field is
	 * not written to disk unless the superblock supports pquotino.
	 */
	if (sbp->sb_uquotino == 0)
		sbp->sb_uquotino = NULLFSINO;
	if (sbp->sb_gquotino == 0)
		sbp->sb_gquotino = NULLFSINO;
	if (sbp->sb_pquotino == 0)
		sbp->sb_pquotino = NULLFSINO;

	/*
	 * We need to do these manipilations only if we are working
	 * with an older version of on-disk superblock.
	 */
	if (xfs_sb_version_has_pquotino(sbp))
		return;

	if (sbp->sb_qflags & XFS_OQUOTA_ENFD)
		sbp->sb_qflags |= (sbp->sb_qflags & XFS_PQUOTA_ACCT) ?
					XFS_PQUOTA_ENFD : XFS_GQUOTA_ENFD;
	if (sbp->sb_qflags & XFS_OQUOTA_CHKD)
		sbp->sb_qflags |= (sbp->sb_qflags & XFS_PQUOTA_ACCT) ?
					XFS_PQUOTA_CHKD : XFS_GQUOTA_CHKD;
	sbp->sb_qflags &= ~(XFS_OQUOTA_ENFD | XFS_OQUOTA_CHKD);

	if (sbp->sb_qflags & XFS_PQUOTA_ACCT)  {
		/*
		 * In older version of superblock, on-disk superblock only
		 * has sb_gquotino, and in-core superblock has both sb_gquotino
		 * and sb_pquotino. But, only one of them is supported at any
		 * point of time. So, if PQUOTA is set in disk superblock,
		 * copy over sb_gquotino to sb_pquotino.
		 */
		sbp->sb_pquotino = sbp->sb_gquotino;
		sbp->sb_gquotino = NULLFSINO;
	}
}

static void
__xfs_sb_from_disk(
	struct xfs_sb	*to,
	xfs_dsb_t	*from,
	bool		convert_xquota)
{
	to->sb_magicnum = be32_to_cpu(from->sb_magicnum);
	to->sb_blocksize = be32_to_cpu(from->sb_blocksize);
	to->sb_dblocks = be64_to_cpu(from->sb_dblocks);
	to->sb_rblocks = be64_to_cpu(from->sb_rblocks);
	to->sb_rextents = be64_to_cpu(from->sb_rextents);
	memcpy(&to->sb_uuid, &from->sb_uuid, sizeof(to->sb_uuid));
	to->sb_logstart = be64_to_cpu(from->sb_logstart);
	to->sb_rootino = be64_to_cpu(from->sb_rootino);
	to->sb_rbmino = be64_to_cpu(from->sb_rbmino);
	to->sb_rsumino = be64_to_cpu(from->sb_rsumino);
	to->sb_rextsize = be32_to_cpu(from->sb_rextsize);
	to->sb_agblocks = be32_to_cpu(from->sb_agblocks);
	to->sb_agcount = be32_to_cpu(from->sb_agcount);
	to->sb_rbmblocks = be32_to_cpu(from->sb_rbmblocks);
	to->sb_logblocks = be32_to_cpu(from->sb_logblocks);
	to->sb_versionnum = be16_to_cpu(from->sb_versionnum);
	to->sb_sectsize = be16_to_cpu(from->sb_sectsize);
	to->sb_inodesize = be16_to_cpu(from->sb_inodesize);
	to->sb_inopblock = be16_to_cpu(from->sb_inopblock);
	memcpy(&to->sb_fname, &from->sb_fname, sizeof(to->sb_fname));
	to->sb_blocklog = from->sb_blocklog;
	to->sb_sectlog = from->sb_sectlog;
	to->sb_inodelog = from->sb_inodelog;
	to->sb_inopblog = from->sb_inopblog;
	to->sb_agblklog = from->sb_agblklog;
	to->sb_rextslog = from->sb_rextslog;
	to->sb_inprogress = from->sb_inprogress;
	to->sb_imax_pct = from->sb_imax_pct;
	to->sb_icount = be64_to_cpu(from->sb_icount);
	to->sb_ifree = be64_to_cpu(from->sb_ifree);
	to->sb_fdblocks = be64_to_cpu(from->sb_fdblocks);
	to->sb_frextents = be64_to_cpu(from->sb_frextents);
	to->sb_uquotino = be64_to_cpu(from->sb_uquotino);
	to->sb_gquotino = be64_to_cpu(from->sb_gquotino);
	to->sb_qflags = be16_to_cpu(from->sb_qflags);
	to->sb_flags = from->sb_flags;
	to->sb_shared_vn = from->sb_shared_vn;
	to->sb_inoalignmt = be32_to_cpu(from->sb_inoalignmt);
	to->sb_unit = be32_to_cpu(from->sb_unit);
	to->sb_width = be32_to_cpu(from->sb_width);
	to->sb_dirblklog = from->sb_dirblklog;
	to->sb_logsectlog = from->sb_logsectlog;
	to->sb_logsectsize = be16_to_cpu(from->sb_logsectsize);
	to->sb_logsunit = be32_to_cpu(from->sb_logsunit);
	to->sb_features2 = be32_to_cpu(from->sb_features2);
	to->sb_bad_features2 = be32_to_cpu(from->sb_bad_features2);
	to->sb_features_compat = be32_to_cpu(from->sb_features_compat);
	to->sb_features_ro_compat = be32_to_cpu(from->sb_features_ro_compat);
	to->sb_features_incompat = be32_to_cpu(from->sb_features_incompat);
	to->sb_features_log_incompat =
				be32_to_cpu(from->sb_features_log_incompat);
	/* crc is only used on disk, not in memory; just init to 0 here. */
	to->sb_crc = 0;
	to->sb_pad = 0;
	to->sb_pquotino = be64_to_cpu(from->sb_pquotino);
	to->sb_lsn = be64_to_cpu(from->sb_lsn);
	/* Convert on-disk flags to in-memory flags? */
	if (convert_xquota)
		xfs_sb_quota_from_disk(to);
}

void
xfs_sb_from_disk(
	struct xfs_sb	*to,
	xfs_dsb_t	*from)
{
	__xfs_sb_from_disk(to, from, true);
}

static inline void
xfs_sb_quota_to_disk(
	xfs_dsb_t	*to,
	xfs_sb_t	*from,
	__int64_t	*fields)
{
	__uint16_t	qflags = from->sb_qflags;

	/*
	 * We need to do these manipilations only if we are working
	 * with an older version of on-disk superblock.
	 */
	if (xfs_sb_version_has_pquotino(from))
		return;

	if (*fields & XFS_SB_QFLAGS) {
		/*
		 * The in-core version of sb_qflags do not have
		 * XFS_OQUOTA_* flags, whereas the on-disk version
		 * does.  So, convert incore XFS_{PG}QUOTA_* flags
		 * to on-disk XFS_OQUOTA_* flags.
		 */
		qflags &= ~(XFS_PQUOTA_ENFD | XFS_PQUOTA_CHKD |
				XFS_GQUOTA_ENFD | XFS_GQUOTA_CHKD);

		if (from->sb_qflags &
				(XFS_PQUOTA_ENFD | XFS_GQUOTA_ENFD))
			qflags |= XFS_OQUOTA_ENFD;
		if (from->sb_qflags &
				(XFS_PQUOTA_CHKD | XFS_GQUOTA_CHKD))
			qflags |= XFS_OQUOTA_CHKD;
		to->sb_qflags = cpu_to_be16(qflags);
		*fields &= ~XFS_SB_QFLAGS;
	}

	/*
	 * GQUOTINO and PQUOTINO cannot be used together in versions of
	 * superblock that do not have pquotino. from->sb_flags tells us which
	 * quota is active and should be copied to disk. If neither are active,
	 * make sure we write NULLFSINO to the sb_gquotino field as a quota
	 * inode value of "0" is invalid when the XFS_SB_VERSION_QUOTA feature
	 * bit is set.
	 *
	 * Note that we don't need to handle the sb_uquotino or sb_pquotino here
	 * as they do not require any translation. Hence the main sb field loop
	 * will write them appropriately from the in-core superblock.
	 */
	if ((*fields & XFS_SB_GQUOTINO) &&
				(from->sb_qflags & XFS_GQUOTA_ACCT))
		to->sb_gquotino = cpu_to_be64(from->sb_gquotino);
	else if ((*fields & XFS_SB_PQUOTINO) &&
				(from->sb_qflags & XFS_PQUOTA_ACCT))
		to->sb_gquotino = cpu_to_be64(from->sb_pquotino);
	else {
		/*
		 * We can't rely on just the fields being logged to tell us
		 * that it is safe to write NULLFSINO - we should only do that
		 * if quotas are not actually enabled. Hence only write
		 * NULLFSINO if both in-core quota inodes are NULL.
		 */
		if (from->sb_gquotino == NULLFSINO &&
		    from->sb_pquotino == NULLFSINO)
			to->sb_gquotino = cpu_to_be64(NULLFSINO);
	}

	*fields &= ~(XFS_SB_PQUOTINO | XFS_SB_GQUOTINO);
}

/*
 * Copy in core superblock to ondisk one.
 *
 * The fields argument is mask of superblock fields to copy.
 */
void
xfs_sb_to_disk(
	xfs_dsb_t	*to,
	xfs_sb_t	*from,
	__int64_t	fields)
{
	xfs_caddr_t	to_ptr = (xfs_caddr_t)to;
	xfs_caddr_t	from_ptr = (xfs_caddr_t)from;
	xfs_sb_field_t	f;
	int		first;
	int		size;

	ASSERT(fields);
	if (!fields)
		return;

	/* We should never write the crc here, it's updated in the IO path */
	fields &= ~XFS_SB_CRC;

	xfs_sb_quota_to_disk(to, from, &fields);
	while (fields) {
		f = (xfs_sb_field_t)xfs_lowbit64((__uint64_t)fields);
		first = xfs_sb_info[f].offset;
		size = xfs_sb_info[f + 1].offset - first;

		ASSERT(xfs_sb_info[f].type == 0 || xfs_sb_info[f].type == 1);

		if (size == 1 || xfs_sb_info[f].type == 1) {
			memcpy(to_ptr + first, from_ptr + first, size);
		} else {
			switch (size) {
			case 2:
				*(__be16 *)(to_ptr + first) =
				      cpu_to_be16(*(__u16 *)(from_ptr + first));
				break;
			case 4:
				*(__be32 *)(to_ptr + first) =
				      cpu_to_be32(*(__u32 *)(from_ptr + first));
				break;
			case 8:
				*(__be64 *)(to_ptr + first) =
				      cpu_to_be64(*(__u64 *)(from_ptr + first));
				break;
			default:
				ASSERT(0);
			}
		}

		fields &= ~(1LL << f);
	}
}

static int
xfs_sb_verify(
	struct xfs_buf	*bp,
	bool		check_version)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_sb	sb;

	/*
	 * Use call variant which doesn't convert quota flags from disk 
	 * format, because xfs_mount_validate_sb checks the on-disk flags.
	 */
	__xfs_sb_from_disk(&sb, XFS_BUF_TO_SBP(bp), false);

	/*
	 * Only check the in progress field for the primary superblock as
	 * mkfs.xfs doesn't clear it from secondary superblocks.
	 */
	return xfs_mount_validate_sb(mp, &sb, bp->b_bn == XFS_SB_DADDR,
				     check_version);
}

/*
 * If the superblock has the CRC feature bit set or the CRC field is non-null,
 * check that the CRC is valid.  We check the CRC field is non-null because a
 * single bit error could clear the feature bit and unused parts of the
 * superblock are supposed to be zero. Hence a non-null crc field indicates that
 * we've potentially lost a feature bit and we should check it anyway.
 *
 * However, past bugs (i.e. in growfs) left non-zeroed regions beyond the
 * last field in V4 secondary superblocks.  So for secondary superblocks,
 * we are more forgiving, and ignore CRC failures if the primary doesn't
 * indicate that the fs version is V5.
 */
static void
xfs_sb_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_dsb	*dsb = XFS_BUF_TO_SBP(bp);
	int		error;

	/*
	 * open code the version check to avoid needing to convert the entire
	 * superblock from disk order just to check the version number
	 */
	if (dsb->sb_magicnum == cpu_to_be32(XFS_SB_MAGIC) &&
	    (((be16_to_cpu(dsb->sb_versionnum) & XFS_SB_VERSION_NUMBITS) ==
						XFS_SB_VERSION_5) ||
	     dsb->sb_crc != 0)) {

		if (!xfs_buf_verify_cksum(bp, XFS_SB_CRC_OFF)) {
			/* Only fail bad secondaries on a known V5 filesystem */
			if (bp->b_bn == XFS_SB_DADDR ||
			    xfs_sb_version_hascrc(&mp->m_sb)) {
				error = -EFSBADCRC;
				goto out_error;
			}
		}
	}
	error = xfs_sb_verify(bp, true);

out_error:
	if (error) {
		xfs_buf_ioerror(bp, error);
		if (error == -EFSCORRUPTED || error == -EFSBADCRC)
			xfs_verifier_error(bp);
	}
}

/*
 * We may be probed for a filesystem match, so we may not want to emit
 * messages when the superblock buffer is not actually an XFS superblock.
 * If we find an XFS superblock, then run a normal, noisy mount because we are
 * really going to mount it and want to know about errors.
 */
static void
xfs_sb_quiet_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_dsb	*dsb = XFS_BUF_TO_SBP(bp);

	if (dsb->sb_magicnum == cpu_to_be32(XFS_SB_MAGIC)) {
		/* XFS filesystem, verify noisily! */
		xfs_sb_read_verify(bp);
		return;
	}
	/* quietly fail */
	xfs_buf_ioerror(bp, -EWRONGFS);
}

static void
xfs_sb_write_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_fspriv;
	int			error;

	error = xfs_sb_verify(bp, false);
	if (error) {
		xfs_buf_ioerror(bp, error);
		xfs_verifier_error(bp);
		return;
	}

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (bip)
		XFS_BUF_TO_SBP(bp)->sb_lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_SB_CRC_OFF);
}

const struct xfs_buf_ops xfs_sb_buf_ops = {
	.verify_read = xfs_sb_read_verify,
	.verify_write = xfs_sb_write_verify,
};

const struct xfs_buf_ops xfs_sb_quiet_buf_ops = {
	.verify_read = xfs_sb_quiet_read_verify,
	.verify_write = xfs_sb_write_verify,
};

/*
 * xfs_mount_common
 *
 * Mount initialization code establishing various mount
 * fields from the superblock associated with the given
 * mount structure
 */
void
xfs_sb_mount_common(
	struct xfs_mount *mp,
	struct xfs_sb	*sbp)
{
	mp->m_agfrotor = mp->m_agirotor = 0;
	spin_lock_init(&mp->m_agirotor_lock);
	mp->m_maxagi = mp->m_sb.sb_agcount;
	mp->m_blkbit_log = sbp->sb_blocklog + XFS_NBBYLOG;
	mp->m_blkbb_log = sbp->sb_blocklog - BBSHIFT;
	mp->m_sectbb_log = sbp->sb_sectlog - BBSHIFT;
	mp->m_agno_log = xfs_highbit32(sbp->sb_agcount - 1) + 1;
	mp->m_agino_log = sbp->sb_inopblog + sbp->sb_agblklog;
	mp->m_blockmask = sbp->sb_blocksize - 1;
	mp->m_blockwsize = sbp->sb_blocksize >> XFS_WORDLOG;
	mp->m_blockwmask = mp->m_blockwsize - 1;

	mp->m_alloc_mxr[0] = xfs_allocbt_maxrecs(mp, sbp->sb_blocksize, 1);
	mp->m_alloc_mxr[1] = xfs_allocbt_maxrecs(mp, sbp->sb_blocksize, 0);
	mp->m_alloc_mnr[0] = mp->m_alloc_mxr[0] / 2;
	mp->m_alloc_mnr[1] = mp->m_alloc_mxr[1] / 2;

	mp->m_inobt_mxr[0] = xfs_inobt_maxrecs(mp, sbp->sb_blocksize, 1);
	mp->m_inobt_mxr[1] = xfs_inobt_maxrecs(mp, sbp->sb_blocksize, 0);
	mp->m_inobt_mnr[0] = mp->m_inobt_mxr[0] / 2;
	mp->m_inobt_mnr[1] = mp->m_inobt_mxr[1] / 2;

	mp->m_bmap_dmxr[0] = xfs_bmbt_maxrecs(mp, sbp->sb_blocksize, 1);
	mp->m_bmap_dmxr[1] = xfs_bmbt_maxrecs(mp, sbp->sb_blocksize, 0);
	mp->m_bmap_dmnr[0] = mp->m_bmap_dmxr[0] / 2;
	mp->m_bmap_dmnr[1] = mp->m_bmap_dmxr[1] / 2;

	mp->m_bsize = XFS_FSB_TO_BB(mp, 1);
	mp->m_ialloc_inos = (int)MAX((__uint16_t)XFS_INODES_PER_CHUNK,
					sbp->sb_inopblock);
	mp->m_ialloc_blks = mp->m_ialloc_inos >> sbp->sb_inopblog;
}

/*
 * xfs_initialize_perag_data
 *
 * Read in each per-ag structure so we can count up the number of
 * allocated inodes, free inodes and used filesystem blocks as this
 * information is no longer persistent in the superblock. Once we have
 * this information, write it into the in-core superblock structure.
 */
int
xfs_initialize_perag_data(
	struct xfs_mount *mp,
	xfs_agnumber_t	agcount)
{
	xfs_agnumber_t	index;
	xfs_perag_t	*pag;
	xfs_sb_t	*sbp = &mp->m_sb;
	uint64_t	ifree = 0;
	uint64_t	ialloc = 0;
	uint64_t	bfree = 0;
	uint64_t	bfreelst = 0;
	uint64_t	btree = 0;
	int		error;

	for (index = 0; index < agcount; index++) {
		/*
		 * read the agf, then the agi. This gets us
		 * all the information we need and populates the
		 * per-ag structures for us.
		 */
		error = xfs_alloc_pagf_init(mp, NULL, index, 0);
		if (error)
			return error;

		error = xfs_ialloc_pagi_init(mp, NULL, index);
		if (error)
			return error;
		pag = xfs_perag_get(mp, index);
		ifree += pag->pagi_freecount;
		ialloc += pag->pagi_count;
		bfree += pag->pagf_freeblks;
		bfreelst += pag->pagf_flcount;
		btree += pag->pagf_btreeblks;
		xfs_perag_put(pag);
	}
	/*
	 * Overwrite incore superblock counters with just-read data
	 */
	spin_lock(&mp->m_sb_lock);
	sbp->sb_ifree = ifree;
	sbp->sb_icount = ialloc;
	sbp->sb_fdblocks = bfree + bfreelst + btree;
	spin_unlock(&mp->m_sb_lock);

	/* Fixup the per-cpu counters as well. */
	xfs_icsb_reinit_counters(mp);

	return 0;
}

/*
 * xfs_mod_sb() can be used to copy arbitrary changes to the
 * in-core superblock into the superblock buffer to be logged.
 * It does not provide the higher level of locking that is
 * needed to protect the in-core superblock from concurrent
 * access.
 */
void
xfs_mod_sb(xfs_trans_t *tp, __int64_t fields)
{
	xfs_buf_t	*bp;
	int		first;
	int		last;
	xfs_mount_t	*mp;
	xfs_sb_field_t	f;

	ASSERT(fields);
	if (!fields)
		return;
	mp = tp->t_mountp;
	bp = xfs_trans_getsb(tp, mp, 0);
	first = sizeof(xfs_sb_t);
	last = 0;

	/* translate/copy */

	xfs_sb_to_disk(XFS_BUF_TO_SBP(bp), &mp->m_sb, fields);

	/* find modified range */
	f = (xfs_sb_field_t)xfs_highbit64((__uint64_t)fields);
	ASSERT((1LL << f) & XFS_SB_MOD_BITS);
	last = xfs_sb_info[f + 1].offset - 1;

	f = (xfs_sb_field_t)xfs_lowbit64((__uint64_t)fields);
	ASSERT((1LL << f) & XFS_SB_MOD_BITS);
	first = xfs_sb_info[f].offset;

	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_SB_BUF);
	xfs_trans_log_buf(tp, bp, first, last);
}
