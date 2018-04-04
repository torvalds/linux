/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * Copyright (c) 2012-2013 Red Hat, Inc.
 * All rights reserved.
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
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_symlink.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"


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

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return 0;

	memset(dsl, 0, sizeof(struct xfs_dsymlink_hdr));
	dsl->sl_magic = cpu_to_be32(XFS_SYMLINK_MAGIC);
	dsl->sl_offset = cpu_to_be32(offset);
	dsl->sl_bytes = cpu_to_be32(size);
	uuid_copy(&dsl->sl_uuid, &mp->m_sb.sb_meta_uuid);
	dsl->sl_owner = cpu_to_be64(ino);
	dsl->sl_blkno = cpu_to_be64(bp->b_bn);
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
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_dsymlink_hdr	*dsl = bp->b_addr;

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return __this_address;
	if (dsl->sl_magic != cpu_to_be32(XFS_SYMLINK_MAGIC))
		return __this_address;
	if (!uuid_equal(&dsl->sl_uuid, &mp->m_sb.sb_meta_uuid))
		return __this_address;
	if (bp->b_bn != be64_to_cpu(dsl->sl_blkno))
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
	struct xfs_mount *mp = bp->b_target->bt_mount;
	xfs_failaddr_t	fa;

	/* no verification of non-crc buffers */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
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
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	xfs_failaddr_t		fa;

	/* no verification of non-crc buffers */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
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
	.verify_read = xfs_symlink_read_verify,
	.verify_write = xfs_symlink_write_verify,
	.verify_struct = xfs_symlink_verify,
};

void
xfs_symlink_local_to_remote(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	struct xfs_inode	*ip,
	struct xfs_ifork	*ifp)
{
	struct xfs_mount	*mp = ip->i_mount;
	char			*buf;

	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_SYMLINK_BUF);

	if (!xfs_sb_version_hascrc(&mp->m_sb)) {
		bp->b_ops = NULL;
		memcpy(bp->b_addr, ifp->if_u1.if_data, ifp->if_bytes);
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
	memcpy(buf, ifp->if_u1.if_data, ifp->if_bytes);
	xfs_trans_log_buf(tp, bp, 0, sizeof(struct xfs_dsymlink_hdr) +
					ifp->if_bytes - 1);
}

/* Verify the consistency of an inline symlink. */
xfs_failaddr_t
xfs_symlink_shortform_verify(
	struct xfs_inode	*ip)
{
	char			*sfp;
	char			*endp;
	struct xfs_ifork	*ifp;
	int			size;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
	sfp = (char *)ifp->if_u1.if_data;
	size = ifp->if_bytes;
	endp = sfp + size;

	/* Zero length symlinks can exist while we're deleting a remote one. */
	if (size == 0)
		return NULL;

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
