// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * Copyright (c) 2012-2013 Red Hat, Inc.
 * All rights reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"
#include "xfs_symlink_remote.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "xfs_health.h"

/*
 * Each contiguous block has a header, so it is not just a simple pathlen
 * to FSB conversion.
 */
int
xfs_symlink_blocks(
	struct xfs_mount *mp,
	int		pathlen)
{
	int buflen = XFS_SYMLINK_BUF_SPACE(mp, mp->m_sb.sb_blocksize);

	return (pathlen + buflen - 1) / buflen;
}

int
xfs_symlink_hdr_set(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	uint32_t		offset,
	uint32_t		size,
	struct xfs_buf		*bp)
{
	struct xfs_dsymlink_hdr	*dsl = bp->b_addr;

	if (!xfs_has_crc(mp))
		return 0;

	memset(dsl, 0, sizeof(struct xfs_dsymlink_hdr));
	dsl->sl_magic = cpu_to_be32(XFS_SYMLINK_MAGIC);
	dsl->sl_offset = cpu_to_be32(offset);
	dsl->sl_bytes = cpu_to_be32(size);
	uuid_copy(&dsl->sl_uuid, &mp->m_sb.sb_meta_uuid);
	dsl->sl_owner = cpu_to_be64(ino);
	dsl->sl_blkno = cpu_to_be64(xfs_buf_daddr(bp));
	bp->b_ops = &xfs_symlink_buf_ops;

	return sizeof(struct xfs_dsymlink_hdr);
}

/*
 * Checking of the symlink header is split into two parts. the verifier does
 * CRC, location and bounds checking, the unpacking function checks the path
 * parameters and owner.
 */
bool
xfs_symlink_hdr_ok(
	xfs_ino_t		ino,
	uint32_t		offset,
	uint32_t		size,
	struct xfs_buf		*bp)
{
	struct xfs_dsymlink_hdr *dsl = bp->b_addr;

	if (offset != be32_to_cpu(dsl->sl_offset))
		return false;
	if (size != be32_to_cpu(dsl->sl_bytes))
		return false;
	if (ino != be64_to_cpu(dsl->sl_owner))
		return false;

	/* ok */
	return true;
}

static xfs_failaddr_t
xfs_symlink_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_dsymlink_hdr	*dsl = bp->b_addr;

	if (!xfs_has_crc(mp))
		return __this_address;
	if (!xfs_verify_magic(bp, dsl->sl_magic))
		return __this_address;
	if (!uuid_equal(&dsl->sl_uuid, &mp->m_sb.sb_meta_uuid))
		return __this_address;
	if (xfs_buf_daddr(bp) != be64_to_cpu(dsl->sl_blkno))
		return __this_address;
	if (be32_to_cpu(dsl->sl_offset) +
				be32_to_cpu(dsl->sl_bytes) >= XFS_SYMLINK_MAXLEN)
		return __this_address;
	if (dsl->sl_owner == 0)
		return __this_address;
	if (!xfs_log_check_lsn(mp, be64_to_cpu(dsl->sl_lsn)))
		return __this_address;

	return NULL;
}

static void
xfs_symlink_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_mount;
	xfs_failaddr_t	fa;

	/* no verification of non-crc buffers */
	if (!xfs_has_crc(mp))
		return;

	if (!xfs_buf_verify_cksum(bp, XFS_SYMLINK_CRC_OFF))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_symlink_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}
}

static void
xfs_symlink_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_mount;
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	xfs_failaddr_t		fa;

	/* no verification of non-crc buffers */
	if (!xfs_has_crc(mp))
		return;

	fa = xfs_symlink_verify(bp);
	if (fa) {
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}

	if (bip) {
		struct xfs_dsymlink_hdr *dsl = bp->b_addr;
		dsl->sl_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	}
	xfs_buf_update_cksum(bp, XFS_SYMLINK_CRC_OFF);
}

const struct xfs_buf_ops xfs_symlink_buf_ops = {
	.name = "xfs_symlink",
	.magic = { 0, cpu_to_be32(XFS_SYMLINK_MAGIC) },
	.verify_read = xfs_symlink_read_verify,
	.verify_write = xfs_symlink_write_verify,
	.verify_struct = xfs_symlink_verify,
};

void
xfs_symlink_local_to_remote(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	struct xfs_inode	*ip,
	struct xfs_ifork	*ifp,
	void			*priv)
{
	struct xfs_mount	*mp = ip->i_mount;
	char			*buf;

	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_SYMLINK_BUF);

	if (!xfs_has_crc(mp)) {
		bp->b_ops = NULL;
		memcpy(bp->b_addr, ifp->if_data, ifp->if_bytes);
		xfs_trans_log_buf(tp, bp, 0, ifp->if_bytes - 1);
		return;
	}

	/*
	 * As this symlink fits in an inode literal area, it must also fit in
	 * the smallest buffer the filesystem supports.
	 */
	ASSERT(BBTOB(bp->b_length) >=
			ifp->if_bytes + sizeof(struct xfs_dsymlink_hdr));

	bp->b_ops = &xfs_symlink_buf_ops;

	buf = bp->b_addr;
	buf += xfs_symlink_hdr_set(mp, ip->i_ino, 0, ifp->if_bytes, bp);
	memcpy(buf, ifp->if_data, ifp->if_bytes);
	xfs_trans_log_buf(tp, bp, 0, sizeof(struct xfs_dsymlink_hdr) +
					ifp->if_bytes - 1);
}

/*
 * Verify the in-memory consistency of an inline symlink data fork. This
 * does not do on-disk format checks.
 */
xfs_failaddr_t
xfs_symlink_shortform_verify(
	void			*sfp,
	int64_t			size)
{
	char			*endp = sfp + size;

	/*
	 * Zero length symlinks should never occur in memory as they are
	 * never allowed to exist on disk.
	 */
	if (!size)
		return __this_address;

	/* No negative sizes or overly long symlink targets. */
	if (size < 0 || size > XFS_SYMLINK_MAXLEN)
		return __this_address;

	/* No NULLs in the target either. */
	if (memchr(sfp, 0, size - 1))
		return __this_address;

	/* We /did/ null-terminate the buffer, right? */
	if (*endp != 0)
		return __this_address;
	return NULL;
}

/* Read a remote symlink target into the buffer. */
int
xfs_symlink_remote_read(
	struct xfs_inode	*ip,
	char			*link)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_bmbt_irec	mval[XFS_SYMLINK_MAPS];
	struct xfs_buf		*bp;
	xfs_daddr_t		d;
	char			*cur_chunk;
	int			pathlen = ip->i_disk_size;
	int			nmaps = XFS_SYMLINK_MAPS;
	int			byte_cnt;
	int			n;
	int			error = 0;
	int			fsblocks = 0;
	int			offset;

	xfs_assert_ilocked(ip, XFS_ILOCK_SHARED | XFS_ILOCK_EXCL);

	fsblocks = xfs_symlink_blocks(mp, pathlen);
	error = xfs_bmapi_read(ip, 0, fsblocks, mval, &nmaps, 0);
	if (error)
		goto out;

	offset = 0;
	for (n = 0; n < nmaps; n++) {
		d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
		byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);

		error = xfs_buf_read(mp->m_ddev_targp, d, BTOBB(byte_cnt), 0,
				&bp, &xfs_symlink_buf_ops);
		if (xfs_metadata_is_sick(error))
			xfs_inode_mark_sick(ip, XFS_SICK_INO_SYMLINK);
		if (error)
			return error;
		byte_cnt = XFS_SYMLINK_BUF_SPACE(mp, byte_cnt);
		if (pathlen < byte_cnt)
			byte_cnt = pathlen;

		cur_chunk = bp->b_addr;
		if (xfs_has_crc(mp)) {
			if (!xfs_symlink_hdr_ok(ip->i_ino, offset,
							byte_cnt, bp)) {
				xfs_inode_mark_sick(ip, XFS_SICK_INO_SYMLINK);
				error = -EFSCORRUPTED;
				xfs_alert(mp,
"symlink header does not match required off/len/owner (0x%x/0x%x,0x%llx)",
					offset, byte_cnt, ip->i_ino);
				xfs_buf_relse(bp);
				goto out;

			}

			cur_chunk += sizeof(struct xfs_dsymlink_hdr);
		}

		memcpy(link + offset, cur_chunk, byte_cnt);

		pathlen -= byte_cnt;
		offset += byte_cnt;

		xfs_buf_relse(bp);
	}
	ASSERT(pathlen == 0);

	link[ip->i_disk_size] = '\0';
	error = 0;

 out:
	return error;
}

/* Write the symlink target into the inode. */
int
xfs_symlink_write_target(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	xfs_ino_t		owner,
	const char		*target_path,
	int			pathlen,
	xfs_fsblock_t		fs_blocks,
	uint			resblks)
{
	struct xfs_bmbt_irec	mval[XFS_SYMLINK_MAPS];
	struct xfs_mount	*mp = tp->t_mountp;
	const char		*cur_chunk;
	struct xfs_buf		*bp;
	xfs_daddr_t		d;
	int			byte_cnt;
	int			nmaps;
	int			offset = 0;
	int			n;
	int			error;

	/*
	 * If the symlink will fit into the inode, write it inline.
	 */
	if (pathlen <= xfs_inode_data_fork_size(ip)) {
		xfs_init_local_fork(ip, XFS_DATA_FORK, target_path, pathlen);

		ip->i_disk_size = pathlen;
		ip->i_df.if_format = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_DDATA | XFS_ILOG_CORE);
		return 0;
	}

	nmaps = XFS_SYMLINK_MAPS;
	error = xfs_bmapi_write(tp, ip, 0, fs_blocks, XFS_BMAPI_METADATA,
			resblks, mval, &nmaps);
	if (error)
		return error;

	ip->i_disk_size = pathlen;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	cur_chunk = target_path;
	offset = 0;
	for (n = 0; n < nmaps; n++) {
		char	*buf;

		d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
		byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);
		error = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
				BTOBB(byte_cnt), 0, &bp);
		if (error)
			return error;
		bp->b_ops = &xfs_symlink_buf_ops;

		byte_cnt = XFS_SYMLINK_BUF_SPACE(mp, byte_cnt);
		byte_cnt = min(byte_cnt, pathlen);

		buf = bp->b_addr;
		buf += xfs_symlink_hdr_set(mp, owner, offset, byte_cnt, bp);

		memcpy(buf, cur_chunk, byte_cnt);

		cur_chunk += byte_cnt;
		pathlen -= byte_cnt;
		offset += byte_cnt;

		xfs_trans_buf_set_type(tp, bp, XFS_BLFT_SYMLINK_BUF);
		xfs_trans_log_buf(tp, bp, 0, (buf + byte_cnt - 1) -
						(char *)bp->b_addr);
	}
	ASSERT(pathlen == 0);
	return 0;
}

/* Remove all the blocks from a symlink and invalidate buffers. */
int
xfs_symlink_remote_truncate(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	struct xfs_bmbt_irec	mval[XFS_SYMLINK_MAPS];
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_buf		*bp;
	int			nmaps = XFS_SYMLINK_MAPS;
	int			done = 0;
	int			i;
	int			error;

	/* Read mappings and invalidate buffers. */
	error = xfs_bmapi_read(ip, 0, XFS_MAX_FILEOFF, mval, &nmaps, 0);
	if (error)
		return error;

	for (i = 0; i < nmaps; i++) {
		if (!xfs_bmap_is_real_extent(&mval[i]))
			break;

		error = xfs_trans_get_buf(tp, mp->m_ddev_targp,
				XFS_FSB_TO_DADDR(mp, mval[i].br_startblock),
				XFS_FSB_TO_BB(mp, mval[i].br_blockcount), 0,
				&bp);
		if (error)
			return error;

		xfs_trans_binval(tp, bp);
	}

	/* Unmap the remote blocks. */
	error = xfs_bunmapi(tp, ip, 0, XFS_MAX_FILEOFF, 0, nmaps, &done);
	if (error)
		return error;
	if (!done) {
		ASSERT(done);
		xfs_inode_mark_sick(ip, XFS_SICK_INO_SYMLINK);
		return -EFSCORRUPTED;
	}

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return 0;
}
