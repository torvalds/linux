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
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_trace.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_icache.h"
#include "xfs_bmap_btree.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_rtrefcount_btree.h"

STATIC void
xlog_recover_inode_ra_pass2(
	struct xlog                     *log,
	struct xlog_recover_item        *item)
{
	if (item->ri_buf[0].i_len == sizeof(struct xfs_inode_log_format)) {
		struct xfs_inode_log_format	*ilfp = item->ri_buf[0].i_addr;

		xlog_buf_readahead(log, ilfp->ilf_blkno, ilfp->ilf_len,
				   &xfs_inode_buf_ra_ops);
	} else {
		struct xfs_inode_log_format_32	*ilfp = item->ri_buf[0].i_addr;

		xlog_buf_readahead(log, ilfp->ilf_blkno, ilfp->ilf_len,
				   &xfs_inode_buf_ra_ops);
	}
}

/*
 * Inode fork owner changes
 *
 * If we have been told that we have to reparent the inode fork, it's because an
 * extent swap operation on a CRC enabled filesystem has been done and we are
 * replaying it. We need to walk the BMBT of the appropriate fork and change the
 * owners of it.
 *
 * The complexity here is that we don't have an inode context to work with, so
 * after we've replayed the inode we need to instantiate one.  This is where the
 * fun begins.
 *
 * We are in the middle of log recovery, so we can't run transactions. That
 * means we cannot use cache coherent inode instantiation via xfs_iget(), as
 * that will result in the corresponding iput() running the inode through
 * xfs_inactive(). If we've just replayed an inode core that changes the link
 * count to zero (i.e. it's been unlinked), then xfs_inactive() will run
 * transactions (bad!).
 *
 * So, to avoid this, we instantiate an inode directly from the inode core we've
 * just recovered. We have the buffer still locked, and all we really need to
 * instantiate is the inode core and the forks being modified. We can do this
 * manually, then run the inode btree owner change, and then tear down the
 * xfs_inode without having to run any transactions at all.
 *
 * Also, because we don't have a transaction context available here but need to
 * gather all the buffers we modify for writeback so we pass the buffer_list
 * instead for the operation to use.
 */

STATIC int
xfs_recover_inode_owner_change(
	struct xfs_mount	*mp,
	struct xfs_dinode	*dip,
	struct xfs_inode_log_format *in_f,
	struct list_head	*buffer_list)
{
	struct xfs_inode	*ip;
	int			error;

	ASSERT(in_f->ilf_fields & (XFS_ILOG_DOWNER|XFS_ILOG_AOWNER));

	ip = xfs_inode_alloc(mp, in_f->ilf_ino);
	if (!ip)
		return -ENOMEM;

	/* instantiate the inode */
	ASSERT(dip->di_version >= 3);

	error = xfs_inode_from_disk(ip, dip);
	if (error)
		goto out_free_ip;

	if (in_f->ilf_fields & XFS_ILOG_DOWNER) {
		ASSERT(in_f->ilf_fields & XFS_ILOG_DBROOT);
		error = xfs_bmbt_change_owner(NULL, ip, XFS_DATA_FORK,
					      ip->i_ino, buffer_list);
		if (error)
			goto out_free_ip;
	}

	if (in_f->ilf_fields & XFS_ILOG_AOWNER) {
		ASSERT(in_f->ilf_fields & XFS_ILOG_ABROOT);
		error = xfs_bmbt_change_owner(NULL, ip, XFS_ATTR_FORK,
					      ip->i_ino, buffer_list);
		if (error)
			goto out_free_ip;
	}

out_free_ip:
	xfs_inode_free(ip);
	return error;
}

static inline bool xfs_log_dinode_has_bigtime(const struct xfs_log_dinode *ld)
{
	return ld->di_version >= 3 &&
	       (ld->di_flags2 & XFS_DIFLAG2_BIGTIME);
}

/* Convert a log timestamp to an ondisk timestamp. */
static inline xfs_timestamp_t
xfs_log_dinode_to_disk_ts(
	struct xfs_log_dinode		*from,
	const xfs_log_timestamp_t	its)
{
	struct xfs_legacy_timestamp	*lts;
	struct xfs_log_legacy_timestamp	*lits;
	xfs_timestamp_t			ts;

	if (xfs_log_dinode_has_bigtime(from))
		return cpu_to_be64(its);

	lts = (struct xfs_legacy_timestamp *)&ts;
	lits = (struct xfs_log_legacy_timestamp *)&its;
	lts->t_sec = cpu_to_be32(lits->t_sec);
	lts->t_nsec = cpu_to_be32(lits->t_nsec);

	return ts;
}

static inline bool xfs_log_dinode_has_large_extent_counts(
		const struct xfs_log_dinode *ld)
{
	return ld->di_version >= 3 &&
	       (ld->di_flags2 & XFS_DIFLAG2_NREXT64);
}

static inline void
xfs_log_dinode_to_disk_iext_counters(
	struct xfs_log_dinode	*from,
	struct xfs_dinode	*to)
{
	if (xfs_log_dinode_has_large_extent_counts(from)) {
		to->di_big_nextents = cpu_to_be64(from->di_big_nextents);
		to->di_big_anextents = cpu_to_be32(from->di_big_anextents);
		to->di_nrext64_pad = cpu_to_be16(from->di_nrext64_pad);
	} else {
		to->di_nextents = cpu_to_be32(from->di_nextents);
		to->di_anextents = cpu_to_be16(from->di_anextents);
	}

}

STATIC void
xfs_log_dinode_to_disk(
	struct xfs_log_dinode	*from,
	struct xfs_dinode	*to,
	xfs_lsn_t		lsn)
{
	to->di_magic = cpu_to_be16(from->di_magic);
	to->di_mode = cpu_to_be16(from->di_mode);
	to->di_version = from->di_version;
	to->di_format = from->di_format;
	to->di_metatype = cpu_to_be16(from->di_metatype);
	to->di_uid = cpu_to_be32(from->di_uid);
	to->di_gid = cpu_to_be32(from->di_gid);
	to->di_nlink = cpu_to_be32(from->di_nlink);
	to->di_projid_lo = cpu_to_be16(from->di_projid_lo);
	to->di_projid_hi = cpu_to_be16(from->di_projid_hi);

	to->di_atime = xfs_log_dinode_to_disk_ts(from, from->di_atime);
	to->di_mtime = xfs_log_dinode_to_disk_ts(from, from->di_mtime);
	to->di_ctime = xfs_log_dinode_to_disk_ts(from, from->di_ctime);

	to->di_size = cpu_to_be64(from->di_size);
	to->di_nblocks = cpu_to_be64(from->di_nblocks);
	to->di_extsize = cpu_to_be32(from->di_extsize);
	to->di_forkoff = from->di_forkoff;
	to->di_aformat = from->di_aformat;
	to->di_dmevmask = cpu_to_be32(from->di_dmevmask);
	to->di_dmstate = cpu_to_be16(from->di_dmstate);
	to->di_flags = cpu_to_be16(from->di_flags);
	to->di_gen = cpu_to_be32(from->di_gen);

	if (from->di_version == 3) {
		to->di_changecount = cpu_to_be64(from->di_changecount);
		to->di_crtime = xfs_log_dinode_to_disk_ts(from,
							  from->di_crtime);
		to->di_flags2 = cpu_to_be64(from->di_flags2);
		/* also covers the di_used_blocks union arm: */
		to->di_cowextsize = cpu_to_be32(from->di_cowextsize);
		to->di_ino = cpu_to_be64(from->di_ino);
		to->di_lsn = cpu_to_be64(lsn);
		memset(to->di_pad2, 0, sizeof(to->di_pad2));
		uuid_copy(&to->di_uuid, &from->di_uuid);
		to->di_v3_pad = 0;
	} else {
		to->di_flushiter = cpu_to_be16(from->di_flushiter);
		memset(to->di_v2_pad, 0, sizeof(to->di_v2_pad));
	}

	xfs_log_dinode_to_disk_iext_counters(from, to);
}

STATIC int
xlog_dinode_verify_extent_counts(
	struct xfs_mount	*mp,
	struct xfs_log_dinode	*ldip)
{
	xfs_extnum_t		nextents;
	xfs_aextnum_t		anextents;

	if (xfs_log_dinode_has_large_extent_counts(ldip)) {
		if (!xfs_has_large_extent_counts(mp) ||
		    (ldip->di_nrext64_pad != 0)) {
			XFS_CORRUPTION_ERROR(
				"Bad log dinode large extent count format",
				XFS_ERRLEVEL_LOW, mp, ldip, sizeof(*ldip));
			xfs_alert(mp,
				"Bad inode 0x%llx, large extent counts %d, padding 0x%x",
				ldip->di_ino, xfs_has_large_extent_counts(mp),
				ldip->di_nrext64_pad);
			return -EFSCORRUPTED;
		}

		nextents = ldip->di_big_nextents;
		anextents = ldip->di_big_anextents;
	} else {
		if (ldip->di_version == 3 && ldip->di_v3_pad != 0) {
			XFS_CORRUPTION_ERROR(
				"Bad log dinode di_v3_pad",
				XFS_ERRLEVEL_LOW, mp, ldip, sizeof(*ldip));
			xfs_alert(mp,
				"Bad inode 0x%llx, di_v3_pad 0x%llx",
				ldip->di_ino, ldip->di_v3_pad);
			return -EFSCORRUPTED;
		}

		nextents = ldip->di_nextents;
		anextents = ldip->di_anextents;
	}

	if (unlikely(nextents + anextents > ldip->di_nblocks)) {
		XFS_CORRUPTION_ERROR("Bad log dinode extent counts",
				XFS_ERRLEVEL_LOW, mp, ldip, sizeof(*ldip));
		xfs_alert(mp,
			"Bad inode 0x%llx, large extent counts %d, nextents 0x%llx, anextents 0x%x, nblocks 0x%llx",
			ldip->di_ino, xfs_has_large_extent_counts(mp), nextents,
			anextents, ldip->di_nblocks);
		return -EFSCORRUPTED;
	}

	return 0;
}

static inline int
xlog_recover_inode_dbroot(
	struct xfs_mount	*mp,
	void			*src,
	unsigned int		len,
	struct xfs_dinode	*dip)
{
	void			*dfork = XFS_DFORK_DPTR(dip);
	unsigned int		dsize = XFS_DFORK_DSIZE(dip, mp);

	switch (dip->di_format) {
	case XFS_DINODE_FMT_BTREE:
		xfs_bmbt_to_bmdr(mp, src, len, dfork, dsize);
		break;
	case XFS_DINODE_FMT_META_BTREE:
		switch (be16_to_cpu(dip->di_metatype)) {
		case XFS_METAFILE_RTRMAP:
			xfs_rtrmapbt_to_disk(mp, src, len, dfork, dsize);
			return 0;
		case XFS_METAFILE_RTREFCOUNT:
			xfs_rtrefcountbt_to_disk(mp, src, len, dfork, dsize);
			return 0;
		default:
			ASSERT(0);
			return -EFSCORRUPTED;
		}
		break;
	default:
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	return 0;
}

STATIC int
xlog_recover_inode_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			current_lsn)
{
	struct xfs_inode_log_format	*in_f;
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_buf			*bp;
	struct xfs_dinode		*dip;
	int				len;
	char				*src;
	char				*dest;
	int				error;
	int				attr_index;
	uint				fields;
	struct xfs_log_dinode		*ldip;
	uint				isize;
	int				need_free = 0;
	xfs_failaddr_t			fa;

	if (item->ri_buf[0].i_len == sizeof(struct xfs_inode_log_format)) {
		in_f = item->ri_buf[0].i_addr;
	} else {
		in_f = kmalloc(sizeof(struct xfs_inode_log_format),
				GFP_KERNEL | __GFP_NOFAIL);
		need_free = 1;
		error = xfs_inode_item_format_convert(&item->ri_buf[0], in_f);
		if (error)
			goto error;
	}

	/*
	 * Inode buffers can be freed, look out for it,
	 * and do not replay the inode.
	 */
	if (xlog_is_buffer_cancelled(log, in_f->ilf_blkno, in_f->ilf_len)) {
		error = 0;
		trace_xfs_log_recover_inode_cancel(log, in_f);
		goto error;
	}
	trace_xfs_log_recover_inode_recover(log, in_f);

	error = xfs_buf_read(mp->m_ddev_targp, in_f->ilf_blkno, in_f->ilf_len,
			0, &bp, &xfs_inode_buf_ops);
	if (error)
		goto error;
	ASSERT(in_f->ilf_fields & XFS_ILOG_CORE);
	dip = xfs_buf_offset(bp, in_f->ilf_boffset);

	/*
	 * Make sure the place we're flushing out to really looks
	 * like an inode!
	 */
	if (XFS_IS_CORRUPT(mp, !xfs_verify_magic16(bp, dip->di_magic))) {
		xfs_alert(mp,
	"%s: Bad inode magic number, dip = "PTR_FMT", dino bp = "PTR_FMT", ino = %lld",
			__func__, dip, bp, in_f->ilf_ino);
		error = -EFSCORRUPTED;
		goto out_release;
	}
	ldip = item->ri_buf[1].i_addr;
	if (XFS_IS_CORRUPT(mp, ldip->di_magic != XFS_DINODE_MAGIC)) {
		xfs_alert(mp,
			"%s: Bad inode log record, rec ptr "PTR_FMT", ino %lld",
			__func__, item, in_f->ilf_ino);
		error = -EFSCORRUPTED;
		goto out_release;
	}

	/*
	 * If the inode has an LSN in it, recover the inode only if the on-disk
	 * inode's LSN is older than the lsn of the transaction we are
	 * replaying. We can have multiple checkpoints with the same start LSN,
	 * so the current LSN being equal to the on-disk LSN doesn't necessarily
	 * mean that the on-disk inode is more recent than the change being
	 * replayed.
	 *
	 * We must check the current_lsn against the on-disk inode
	 * here because the we can't trust the log dinode to contain a valid LSN
	 * (see comment below before replaying the log dinode for details).
	 *
	 * Note: we still need to replay an owner change even though the inode
	 * is more recent than the transaction as there is no guarantee that all
	 * the btree blocks are more recent than this transaction, too.
	 */
	if (dip->di_version >= 3) {
		xfs_lsn_t	lsn = be64_to_cpu(dip->di_lsn);

		if (lsn && lsn != -1 && XFS_LSN_CMP(lsn, current_lsn) > 0) {
			trace_xfs_log_recover_inode_skip(log, in_f);
			error = 0;
			goto out_owner_change;
		}
	}

	/*
	 * di_flushiter is only valid for v1/2 inodes. All changes for v3 inodes
	 * are transactional and if ordering is necessary we can determine that
	 * more accurately by the LSN field in the V3 inode core. Don't trust
	 * the inode versions we might be changing them here - use the
	 * superblock flag to determine whether we need to look at di_flushiter
	 * to skip replay when the on disk inode is newer than the log one
	 */
	if (!xfs_has_v3inodes(mp)) {
		if (ldip->di_flushiter < be16_to_cpu(dip->di_flushiter)) {
			/*
			 * Deal with the wrap case, DI_MAX_FLUSH is less
			 * than smaller numbers
			 */
			if (be16_to_cpu(dip->di_flushiter) == DI_MAX_FLUSH &&
			    ldip->di_flushiter < (DI_MAX_FLUSH >> 1)) {
				/* do nothing */
			} else {
				trace_xfs_log_recover_inode_skip(log, in_f);
				error = 0;
				goto out_release;
			}
		}

		/* Take the opportunity to reset the flush iteration count */
		ldip->di_flushiter = 0;
	}


	if (unlikely(S_ISREG(ldip->di_mode))) {
		if (ldip->di_format != XFS_DINODE_FMT_EXTENTS &&
		    ldip->di_format != XFS_DINODE_FMT_BTREE &&
		    ldip->di_format != XFS_DINODE_FMT_META_BTREE) {
			XFS_CORRUPTION_ERROR(
				"Bad log dinode data fork format for regular file",
				XFS_ERRLEVEL_LOW, mp, ldip, sizeof(*ldip));
			xfs_alert(mp,
				"Bad inode 0x%llx, data fork format 0x%x",
				in_f->ilf_ino, ldip->di_format);
			error = -EFSCORRUPTED;
			goto out_release;
		}
	} else if (unlikely(S_ISDIR(ldip->di_mode))) {
		if ((ldip->di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ldip->di_format != XFS_DINODE_FMT_BTREE) &&
		    (ldip->di_format != XFS_DINODE_FMT_LOCAL)) {
			XFS_CORRUPTION_ERROR(
				"Bad log dinode data fork format for directory",
				XFS_ERRLEVEL_LOW, mp, ldip, sizeof(*ldip));
			xfs_alert(mp,
				"Bad inode 0x%llx, data fork format 0x%x",
				in_f->ilf_ino, ldip->di_format);
			error = -EFSCORRUPTED;
			goto out_release;
		}
	}

	error = xlog_dinode_verify_extent_counts(mp, ldip);
	if (error)
		goto out_release;

	if (unlikely(ldip->di_forkoff > mp->m_sb.sb_inodesize)) {
		XFS_CORRUPTION_ERROR("Bad log dinode fork offset",
				XFS_ERRLEVEL_LOW, mp, ldip, sizeof(*ldip));
		xfs_alert(mp,
			"Bad inode 0x%llx, di_forkoff 0x%x",
			in_f->ilf_ino, ldip->di_forkoff);
		error = -EFSCORRUPTED;
		goto out_release;
	}
	isize = xfs_log_dinode_size(mp);
	if (unlikely(item->ri_buf[1].i_len > isize)) {
		XFS_CORRUPTION_ERROR("Bad log dinode size", XFS_ERRLEVEL_LOW,
				     mp, ldip, sizeof(*ldip));
		xfs_alert(mp,
			"Bad inode 0x%llx log dinode size 0x%x",
			in_f->ilf_ino, item->ri_buf[1].i_len);
		error = -EFSCORRUPTED;
		goto out_release;
	}

	/*
	 * Recover the log dinode inode into the on disk inode.
	 *
	 * The LSN in the log dinode is garbage - it can be zero or reflect
	 * stale in-memory runtime state that isn't coherent with the changes
	 * logged in this transaction or the changes written to the on-disk
	 * inode.  Hence we write the current lSN into the inode because that
	 * matches what xfs_iflush() would write inode the inode when flushing
	 * the changes in this transaction.
	 */
	xfs_log_dinode_to_disk(ldip, dip, current_lsn);

	fields = in_f->ilf_fields;
	if (fields & XFS_ILOG_DEV)
		xfs_dinode_put_rdev(dip, in_f->ilf_u.ilfu_rdev);

	if (in_f->ilf_size == 2)
		goto out_owner_change;
	len = item->ri_buf[2].i_len;
	src = item->ri_buf[2].i_addr;
	ASSERT(in_f->ilf_size <= 4);
	ASSERT((in_f->ilf_size == 3) || (fields & XFS_ILOG_AFORK));
	ASSERT(!(fields & XFS_ILOG_DFORK) ||
	       (len == xlog_calc_iovec_len(in_f->ilf_dsize)));

	switch (fields & XFS_ILOG_DFORK) {
	case XFS_ILOG_DDATA:
	case XFS_ILOG_DEXT:
		memcpy(XFS_DFORK_DPTR(dip), src, len);
		break;

	case XFS_ILOG_DBROOT:
		error = xlog_recover_inode_dbroot(mp, src, len, dip);
		if (error)
			goto out_release;
		break;

	default:
		/*
		 * There are no data fork flags set.
		 */
		ASSERT((fields & XFS_ILOG_DFORK) == 0);
		break;
	}

	/*
	 * If we logged any attribute data, recover it.  There may or
	 * may not have been any other non-core data logged in this
	 * transaction.
	 */
	if (in_f->ilf_fields & XFS_ILOG_AFORK) {
		if (in_f->ilf_fields & XFS_ILOG_DFORK) {
			attr_index = 3;
		} else {
			attr_index = 2;
		}
		len = item->ri_buf[attr_index].i_len;
		src = item->ri_buf[attr_index].i_addr;
		ASSERT(len == xlog_calc_iovec_len(in_f->ilf_asize));

		switch (in_f->ilf_fields & XFS_ILOG_AFORK) {
		case XFS_ILOG_ADATA:
		case XFS_ILOG_AEXT:
			dest = XFS_DFORK_APTR(dip);
			ASSERT(len <= XFS_DFORK_ASIZE(dip, mp));
			memcpy(dest, src, len);
			break;

		case XFS_ILOG_ABROOT:
			dest = XFS_DFORK_APTR(dip);
			xfs_bmbt_to_bmdr(mp, (struct xfs_btree_block *)src,
					 len, (struct xfs_bmdr_block *)dest,
					 XFS_DFORK_ASIZE(dip, mp));
			break;

		default:
			xfs_warn(log->l_mp, "%s: Invalid flag", __func__);
			ASSERT(0);
			error = -EFSCORRUPTED;
			goto out_release;
		}
	}

out_owner_change:
	/* Recover the swapext owner change unless inode has been deleted */
	if ((in_f->ilf_fields & (XFS_ILOG_DOWNER|XFS_ILOG_AOWNER)) &&
	    (dip->di_mode != 0))
		error = xfs_recover_inode_owner_change(mp, dip, in_f,
						       buffer_list);
	/* re-generate the checksum and validate the recovered inode. */
	xfs_dinode_calc_crc(log->l_mp, dip);
	fa = xfs_dinode_verify(log->l_mp, in_f->ilf_ino, dip);
	if (fa) {
		XFS_CORRUPTION_ERROR(
			"Bad dinode after recovery",
				XFS_ERRLEVEL_LOW, mp, dip, sizeof(*dip));
		xfs_alert(mp,
			"Metadata corruption detected at %pS, inode 0x%llx",
			fa, in_f->ilf_ino);
		error = -EFSCORRUPTED;
		goto out_release;
	}

	ASSERT(bp->b_mount == mp);
	bp->b_flags |= _XBF_LOGRECOVERY;
	xfs_buf_delwri_queue(bp, buffer_list);

out_release:
	xfs_buf_relse(bp);
error:
	if (need_free)
		kfree(in_f);
	return error;
}

const struct xlog_recover_item_ops xlog_inode_item_ops = {
	.item_type		= XFS_LI_INODE,
	.ra_pass2		= xlog_recover_inode_ra_pass2,
	.commit_pass2		= xlog_recover_inode_commit_pass2,
};
