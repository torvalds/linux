/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
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
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_attr_remote.h"
#include "xfs_trans_space.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_buf_item.h"

#define ATTR_RMTVALUE_MAPSIZE	1	/* # of map entries at once */

/*
 * Each contiguous block has a header, so it is not just a simple attribute
 * length to FSB conversion.
 */
static int
xfs_attr3_rmt_blocks(
	struct xfs_mount *mp,
	int		attrlen)
{
	int		buflen = XFS_ATTR3_RMT_BUF_SPACE(mp,
							 mp->m_sb.sb_blocksize);
	return (attrlen + buflen - 1) / buflen;
}

static bool
xfs_attr3_rmt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_attr3_rmt_hdr *rmt = bp->b_addr;

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return false;
	if (rmt->rm_magic != cpu_to_be32(XFS_ATTR3_RMT_MAGIC))
		return false;
	if (!uuid_equal(&rmt->rm_uuid, &mp->m_sb.sb_uuid))
		return false;
	if (bp->b_bn != be64_to_cpu(rmt->rm_blkno))
		return false;
	if (be32_to_cpu(rmt->rm_offset) +
				be32_to_cpu(rmt->rm_bytes) >= XATTR_SIZE_MAX)
		return false;
	if (rmt->rm_owner == 0)
		return false;

	return true;
}

static void
xfs_attr3_rmt_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;

	/* no verification of non-crc buffers */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (!xfs_verify_cksum(bp->b_addr, BBTOB(bp->b_length),
			      XFS_ATTR3_RMT_CRC_OFF) ||
	    !xfs_attr3_rmt_verify(bp)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, bp->b_addr);
		xfs_buf_ioerror(bp, EFSCORRUPTED);
	}
}

static void
xfs_attr3_rmt_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_fspriv;

	/* no verification of non-crc buffers */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (!xfs_attr3_rmt_verify(bp)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, bp->b_addr);
		xfs_buf_ioerror(bp, EFSCORRUPTED);
		return;
	}

	if (bip) {
		struct xfs_attr3_rmt_hdr *rmt = bp->b_addr;
		rmt->rm_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	}
	xfs_update_cksum(bp->b_addr, BBTOB(bp->b_length),
			 XFS_ATTR3_RMT_CRC_OFF);
}

const struct xfs_buf_ops xfs_attr3_rmt_buf_ops = {
	.verify_read = xfs_attr3_rmt_read_verify,
	.verify_write = xfs_attr3_rmt_write_verify,
};

static int
xfs_attr3_rmt_hdr_set(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	uint32_t		offset,
	uint32_t		size,
	struct xfs_buf		*bp)
{
	struct xfs_attr3_rmt_hdr *rmt = bp->b_addr;

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return 0;

	rmt->rm_magic = cpu_to_be32(XFS_ATTR3_RMT_MAGIC);
	rmt->rm_offset = cpu_to_be32(offset);
	rmt->rm_bytes = cpu_to_be32(size);
	uuid_copy(&rmt->rm_uuid, &mp->m_sb.sb_uuid);
	rmt->rm_owner = cpu_to_be64(ino);
	rmt->rm_blkno = cpu_to_be64(bp->b_bn);
	bp->b_ops = &xfs_attr3_rmt_buf_ops;

	return sizeof(struct xfs_attr3_rmt_hdr);
}

/*
 * Checking of the remote attribute header is split into two parts. the verifier
 * does CRC, location and bounds checking, the unpacking function checks the
 * attribute parameters and owner.
 */
static bool
xfs_attr3_rmt_hdr_ok(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	uint32_t		offset,
	uint32_t		size,
	struct xfs_buf		*bp)
{
	struct xfs_attr3_rmt_hdr *rmt = bp->b_addr;

	if (offset != be32_to_cpu(rmt->rm_offset))
		return false;
	if (size != be32_to_cpu(rmt->rm_bytes))
		return false;
	if (ino != be64_to_cpu(rmt->rm_owner))
		return false;

	/* ok */
	return true;
}

/*
 * Read the value associated with an attribute from the out-of-line buffer
 * that we stored it in.
 */
int
xfs_attr_rmtval_get(
	struct xfs_da_args	*args)
{
	struct xfs_bmbt_irec	map[ATTR_RMTVALUE_MAPSIZE];
	struct xfs_mount	*mp = args->dp->i_mount;
	struct xfs_buf		*bp;
	xfs_daddr_t		dblkno;
	xfs_dablk_t		lblkno = args->rmtblkno;
	void			*dst = args->value;
	int			valuelen = args->valuelen;
	int			nmap;
	int			error;
	int			blkcnt;
	int			i;
	int			offset = 0;

	trace_xfs_attr_rmtval_get(args);

	ASSERT(!(args->flags & ATTR_KERNOVAL));

	while (valuelen > 0) {
		nmap = ATTR_RMTVALUE_MAPSIZE;
		error = xfs_bmapi_read(args->dp, (xfs_fileoff_t)lblkno,
				       args->rmtblkcnt, map, &nmap,
				       XFS_BMAPI_ATTRFORK);
		if (error)
			return error;
		ASSERT(nmap >= 1);

		for (i = 0; (i < nmap) && (valuelen > 0); i++) {
			int	byte_cnt;
			char	*src;

			ASSERT((map[i].br_startblock != DELAYSTARTBLOCK) &&
			       (map[i].br_startblock != HOLESTARTBLOCK));
			dblkno = XFS_FSB_TO_DADDR(mp, map[i].br_startblock);
			blkcnt = XFS_FSB_TO_BB(mp, map[i].br_blockcount);
			error = xfs_trans_read_buf(mp, NULL, mp->m_ddev_targp,
						   dblkno, blkcnt, 0, &bp,
						   &xfs_attr3_rmt_buf_ops);
			if (error)
				return error;

			byte_cnt = min_t(int, valuelen, BBTOB(bp->b_length));
			byte_cnt = XFS_ATTR3_RMT_BUF_SPACE(mp, byte_cnt);

			src = bp->b_addr;
			if (xfs_sb_version_hascrc(&mp->m_sb)) {
				if (!xfs_attr3_rmt_hdr_ok(mp, args->dp->i_ino,
							offset, byte_cnt, bp)) {
					xfs_alert(mp,
"remote attribute header does not match required off/len/owner (0x%x/Ox%x,0x%llx)",
						offset, byte_cnt, args->dp->i_ino);
					xfs_buf_relse(bp);
					return EFSCORRUPTED;

				}

				src += sizeof(struct xfs_attr3_rmt_hdr);
			}

			memcpy(dst, src, byte_cnt);
			xfs_buf_relse(bp);

			offset += byte_cnt;
			dst += byte_cnt;
			valuelen -= byte_cnt;

			lblkno += map[i].br_blockcount;
		}
	}
	ASSERT(valuelen == 0);
	return 0;
}

/*
 * Write the value associated with an attribute into the out-of-line buffer
 * that we have defined for it.
 */
int
xfs_attr_rmtval_set(
	struct xfs_da_args	*args)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_bmbt_irec	map;
	struct xfs_buf		*bp;
	xfs_daddr_t		dblkno;
	xfs_dablk_t		lblkno;
	xfs_fileoff_t		lfileoff = 0;
	void			*src = args->value;
	int			blkcnt;
	int			valuelen;
	int			nmap;
	int			error;
	int			hdrcnt = 0;
	bool			crcs = xfs_sb_version_hascrc(&mp->m_sb);
	int			offset = 0;

	trace_xfs_attr_rmtval_set(args);

	/*
	 * Find a "hole" in the attribute address space large enough for
	 * us to drop the new attribute's value into. Because CRC enable
	 * attributes have headers, we can't just do a straight byte to FSB
	 * conversion. We calculate the worst case block count in this case
	 * and we may not need that many, so we have to handle this when
	 * allocating the blocks below. 
	 */
	if (!crcs)
		blkcnt = XFS_B_TO_FSB(mp, args->valuelen);
	else
		blkcnt = xfs_attr3_rmt_blocks(mp, args->valuelen);

	error = xfs_bmap_first_unused(args->trans, args->dp, blkcnt, &lfileoff,
						   XFS_ATTR_FORK);
	if (error)
		return error;

	/* Start with the attribute data. We'll allocate the rest afterwards. */
	if (crcs)
		blkcnt = XFS_B_TO_FSB(mp, args->valuelen);

	args->rmtblkno = lblkno = (xfs_dablk_t)lfileoff;
	args->rmtblkcnt = blkcnt;

	/*
	 * Roll through the "value", allocating blocks on disk as required.
	 */
	while (blkcnt > 0) {
		int	committed;

		/*
		 * Allocate a single extent, up to the size of the value.
		 */
		xfs_bmap_init(args->flist, args->firstblock);
		nmap = 1;
		error = xfs_bmapi_write(args->trans, dp, (xfs_fileoff_t)lblkno,
				  blkcnt,
				  XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				  args->firstblock, args->total, &map, &nmap,
				  args->flist);
		if (!error) {
			error = xfs_bmap_finish(&args->trans, args->flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args->trans = NULL;
			xfs_bmap_cancel(args->flist);
			return(error);
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args->trans, dp, 0);

		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));
		lblkno += map.br_blockcount;
		blkcnt -= map.br_blockcount;
		hdrcnt++;

		/*
		 * If we have enough blocks for the attribute data, calculate
		 * how many extra blocks we need for headers. We might run
		 * through this multiple times in the case that the additional
		 * headers in the blocks needed for the data fragments spills
		 * into requiring more blocks. e.g. for 512 byte blocks, we'll
		 * spill for another block every 9 headers we require in this
		 * loop.
		 *
		 * Note that this can result in contiguous allocation of blocks,
		 * so we don't use all the space we allocate for headers as we
		 * have one less header for each contiguous allocation that
		 * occurs in the map/write loop below.
		 */
		if (crcs && blkcnt == 0) {
			int total_len;

			total_len = args->valuelen +
				    hdrcnt * sizeof(struct xfs_attr3_rmt_hdr);
			blkcnt = XFS_B_TO_FSB(mp, total_len);
			blkcnt -= args->rmtblkcnt;
			args->rmtblkcnt += blkcnt;
		}

		/*
		 * Start the next trans in the chain.
		 */
		error = xfs_trans_roll(&args->trans, dp);
		if (error)
			return (error);
	}

	/*
	 * Roll through the "value", copying the attribute value to the
	 * already-allocated blocks.  Blocks are written synchronously
	 * so that we can know they are all on disk before we turn off
	 * the INCOMPLETE flag.
	 */
	lblkno = args->rmtblkno;
	valuelen = args->valuelen;
	while (valuelen > 0) {
		int	byte_cnt;
		char	*buf;

		/*
		 * Try to remember where we decided to put the value.
		 */
		xfs_bmap_init(args->flist, args->firstblock);
		nmap = 1;
		error = xfs_bmapi_read(dp, (xfs_fileoff_t)lblkno,
				       args->rmtblkcnt, &map, &nmap,
				       XFS_BMAPI_ATTRFORK);
		if (error)
			return(error);
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		blkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);

		bp = xfs_buf_get(mp->m_ddev_targp, dblkno, blkcnt, 0);
		if (!bp)
			return ENOMEM;
		bp->b_ops = &xfs_attr3_rmt_buf_ops;

		byte_cnt = BBTOB(bp->b_length);
		byte_cnt = XFS_ATTR3_RMT_BUF_SPACE(mp, byte_cnt);
		if (valuelen < byte_cnt)
			byte_cnt = valuelen;

		buf = bp->b_addr;
		buf += xfs_attr3_rmt_hdr_set(mp, dp->i_ino, offset,
					     byte_cnt, bp);
		memcpy(buf, src, byte_cnt);

		if (byte_cnt < BBTOB(bp->b_length))
			xfs_buf_zero(bp, byte_cnt,
				     BBTOB(bp->b_length) - byte_cnt);

		error = xfs_bwrite(bp);	/* GROT: NOTE: synchronous write */
		xfs_buf_relse(bp);
		if (error)
			return error;

		src += byte_cnt;
		valuelen -= byte_cnt;
		offset += byte_cnt;
		hdrcnt--;

		lblkno += map.br_blockcount;
	}
	ASSERT(valuelen == 0);
	return 0;
}

/*
 * Remove the value associated with an attribute by deleting the
 * out-of-line buffer that it is stored on.
 */
int
xfs_attr_rmtval_remove(xfs_da_args_t *args)
{
	xfs_mount_t *mp;
	xfs_bmbt_irec_t map;
	xfs_buf_t *bp;
	xfs_daddr_t dblkno;
	xfs_dablk_t lblkno;
	int valuelen, blkcnt, nmap, error, done, committed;

	trace_xfs_attr_rmtval_remove(args);

	mp = args->dp->i_mount;

	/*
	 * Roll through the "value", invalidating the attribute value's
	 * blocks.
	 */
	lblkno = args->rmtblkno;
	valuelen = args->rmtblkcnt;
	while (valuelen > 0) {
		/*
		 * Try to remember where we decided to put the value.
		 */
		nmap = 1;
		error = xfs_bmapi_read(args->dp, (xfs_fileoff_t)lblkno,
				       args->rmtblkcnt, &map, &nmap,
				       XFS_BMAPI_ATTRFORK);
		if (error)
			return(error);
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		blkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);

		/*
		 * If the "remote" value is in the cache, remove it.
		 */
		bp = xfs_incore(mp->m_ddev_targp, dblkno, blkcnt, XBF_TRYLOCK);
		if (bp) {
			xfs_buf_stale(bp);
			xfs_buf_relse(bp);
			bp = NULL;
		}

		valuelen -= map.br_blockcount;

		lblkno += map.br_blockcount;
	}

	/*
	 * Keep de-allocating extents until the remote-value region is gone.
	 */
	lblkno = args->rmtblkno;
	blkcnt = args->rmtblkcnt;
	done = 0;
	while (!done) {
		xfs_bmap_init(args->flist, args->firstblock);
		error = xfs_bunmapi(args->trans, args->dp, lblkno, blkcnt,
				    XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				    1, args->firstblock, args->flist,
				    &done);
		if (!error) {
			error = xfs_bmap_finish(&args->trans, args->flist,
						&committed);
		}
		if (error) {
			ASSERT(committed);
			args->trans = NULL;
			xfs_bmap_cancel(args->flist);
			return error;
		}

		/*
		 * bmap_finish() may have committed the last trans and started
		 * a new one.  We need the inode to be in all transactions.
		 */
		if (committed)
			xfs_trans_ijoin(args->trans, args->dp, 0);

		/*
		 * Close out trans and start the next one in the chain.
		 */
		error = xfs_trans_roll(&args->trans, args->dp);
		if (error)
			return (error);
	}
	return(0);
}

