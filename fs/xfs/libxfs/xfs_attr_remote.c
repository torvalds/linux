// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_attr_remote.h"
#include "xfs_trace.h"
#include "xfs_error.h"
#include "xfs_health.h"

#define ATTR_RMTVALUE_MAPSIZE	1	/* # of map entries at once */

/*
 * Remote Attribute Values
 * =======================
 *
 * Remote extended attribute values are conceptually simple -- they're written
 * to data blocks mapped by an inode's attribute fork, and they have an upper
 * size limit of 64k.  Setting a value does not involve the XFS log.
 *
 * However, on a v5 filesystem, maximally sized remote attr values require one
 * block more than 64k worth of space to hold both the remote attribute value
 * header (64 bytes).  On a 4k block filesystem this results in a 68k buffer;
 * on a 64k block filesystem, this would be a 128k buffer.  Note that the log
 * format can only handle a dirty buffer of XFS_MAX_BLOCKSIZE length (64k).
 * Therefore, we /must/ ensure that remote attribute value buffers never touch
 * the logging system and therefore never have a log item.
 */

/* How many bytes can be stored in a remote value buffer? */
inline unsigned int
xfs_attr3_rmt_buf_space(
	struct xfs_mount	*mp)
{
	unsigned int		blocksize = mp->m_attr_geo->blksize;

	if (xfs_has_crc(mp))
		return blocksize - sizeof(struct xfs_attr3_rmt_hdr);

	return blocksize;
}

/* Compute number of fsblocks needed to store a remote attr value */
unsigned int
xfs_attr3_rmt_blocks(
	struct xfs_mount	*mp,
	unsigned int		attrlen)
{
	/*
	 * Each contiguous block has a header, so it is not just a simple
	 * attribute length to FSB conversion.
	 */
	if (xfs_has_crc(mp))
		return howmany(attrlen, xfs_attr3_rmt_buf_space(mp));

	return XFS_B_TO_FSB(mp, attrlen);
}

/*
 * Checking of the remote attribute header is split into two parts. The verifier
 * does CRC, location and bounds checking, the unpacking function checks the
 * attribute parameters and owner.
 */
static xfs_failaddr_t
xfs_attr3_rmt_hdr_ok(
	void			*ptr,
	xfs_ino_t		ino,
	uint32_t		offset,
	uint32_t		size,
	xfs_daddr_t		bno)
{
	struct xfs_attr3_rmt_hdr *rmt = ptr;

	if (bno != be64_to_cpu(rmt->rm_blkno))
		return __this_address;
	if (offset != be32_to_cpu(rmt->rm_offset))
		return __this_address;
	if (size != be32_to_cpu(rmt->rm_bytes))
		return __this_address;
	if (ino != be64_to_cpu(rmt->rm_owner))
		return __this_address;

	/* ok */
	return NULL;
}

static xfs_failaddr_t
xfs_attr3_rmt_verify(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	void			*ptr,
	xfs_daddr_t		bno)
{
	struct xfs_attr3_rmt_hdr *rmt = ptr;

	if (!xfs_verify_magic(bp, rmt->rm_magic))
		return __this_address;
	if (!uuid_equal(&rmt->rm_uuid, &mp->m_sb.sb_meta_uuid))
		return __this_address;
	if (be64_to_cpu(rmt->rm_blkno) != bno)
		return __this_address;
	if (be32_to_cpu(rmt->rm_bytes) > mp->m_attr_geo->blksize - sizeof(*rmt))
		return __this_address;
	if (be32_to_cpu(rmt->rm_offset) +
				be32_to_cpu(rmt->rm_bytes) > XFS_XATTR_SIZE_MAX)
		return __this_address;
	if (rmt->rm_owner == 0)
		return __this_address;

	return NULL;
}

static int
__xfs_attr3_rmt_read_verify(
	struct xfs_buf	*bp,
	bool		check_crc,
	xfs_failaddr_t	*failaddr)
{
	struct xfs_mount *mp = bp->b_mount;
	char		*ptr;
	unsigned int	len;
	xfs_daddr_t	bno;
	unsigned int	blksize = mp->m_attr_geo->blksize;

	/* no verification of non-crc buffers */
	if (!xfs_has_crc(mp))
		return 0;

	ptr = bp->b_addr;
	bno = xfs_buf_daddr(bp);
	len = BBTOB(bp->b_length);
	ASSERT(len >= blksize);

	while (len > 0) {
		if (check_crc &&
		    !xfs_verify_cksum(ptr, blksize, XFS_ATTR3_RMT_CRC_OFF)) {
			*failaddr = __this_address;
			return -EFSBADCRC;
		}
		*failaddr = xfs_attr3_rmt_verify(mp, bp, ptr, bno);
		if (*failaddr)
			return -EFSCORRUPTED;
		len -= blksize;
		ptr += blksize;
		bno += BTOBB(blksize);
	}

	if (len != 0) {
		*failaddr = __this_address;
		return -EFSCORRUPTED;
	}

	return 0;
}

static void
xfs_attr3_rmt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;
	int		error;

	error = __xfs_attr3_rmt_read_verify(bp, true, &fa);
	if (error)
		xfs_verifier_error(bp, error, fa);
}

static xfs_failaddr_t
xfs_attr3_rmt_verify_struct(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;
	int		error;

	error = __xfs_attr3_rmt_read_verify(bp, false, &fa);
	return error ? fa : NULL;
}

static void
xfs_attr3_rmt_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_mount;
	xfs_failaddr_t	fa;
	unsigned int	blksize = mp->m_attr_geo->blksize;
	char		*ptr;
	int		len;
	xfs_daddr_t	bno;

	/* no verification of non-crc buffers */
	if (!xfs_has_crc(mp))
		return;

	ptr = bp->b_addr;
	bno = xfs_buf_daddr(bp);
	len = BBTOB(bp->b_length);
	ASSERT(len >= blksize);

	while (len > 0) {
		struct xfs_attr3_rmt_hdr *rmt = (struct xfs_attr3_rmt_hdr *)ptr;

		fa = xfs_attr3_rmt_verify(mp, bp, ptr, bno);
		if (fa) {
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
			return;
		}

		/*
		 * Ensure we aren't writing bogus LSNs to disk. See
		 * xfs_attr3_rmt_hdr_set() for the explanation.
		 */
		if (rmt->rm_lsn != cpu_to_be64(NULLCOMMITLSN)) {
			xfs_verifier_error(bp, -EFSCORRUPTED, __this_address);
			return;
		}
		xfs_update_cksum(ptr, blksize, XFS_ATTR3_RMT_CRC_OFF);

		len -= blksize;
		ptr += blksize;
		bno += BTOBB(blksize);
	}

	if (len != 0)
		xfs_verifier_error(bp, -EFSCORRUPTED, __this_address);
}

const struct xfs_buf_ops xfs_attr3_rmt_buf_ops = {
	.name = "xfs_attr3_rmt",
	.magic = { 0, cpu_to_be32(XFS_ATTR3_RMT_MAGIC) },
	.verify_read = xfs_attr3_rmt_read_verify,
	.verify_write = xfs_attr3_rmt_write_verify,
	.verify_struct = xfs_attr3_rmt_verify_struct,
};

STATIC int
xfs_attr3_rmt_hdr_set(
	struct xfs_mount	*mp,
	void			*ptr,
	xfs_ino_t		ino,
	uint32_t		offset,
	uint32_t		size,
	xfs_daddr_t		bno)
{
	struct xfs_attr3_rmt_hdr *rmt = ptr;

	if (!xfs_has_crc(mp))
		return 0;

	rmt->rm_magic = cpu_to_be32(XFS_ATTR3_RMT_MAGIC);
	rmt->rm_offset = cpu_to_be32(offset);
	rmt->rm_bytes = cpu_to_be32(size);
	uuid_copy(&rmt->rm_uuid, &mp->m_sb.sb_meta_uuid);
	rmt->rm_owner = cpu_to_be64(ino);
	rmt->rm_blkno = cpu_to_be64(bno);

	/*
	 * Remote attribute blocks are written synchronously, so we don't
	 * have an LSN that we can stamp in them that makes any sense to log
	 * recovery. To ensure that log recovery handles overwrites of these
	 * blocks sanely (i.e. once they've been freed and reallocated as some
	 * other type of metadata) we need to ensure that the LSN has a value
	 * that tells log recovery to ignore the LSN and overwrite the buffer
	 * with whatever is in it's log. To do this, we use the magic
	 * NULLCOMMITLSN to indicate that the LSN is invalid.
	 */
	rmt->rm_lsn = cpu_to_be64(NULLCOMMITLSN);

	return sizeof(struct xfs_attr3_rmt_hdr);
}

/*
 * Helper functions to copy attribute data in and out of the one disk extents
 */
STATIC int
xfs_attr_rmtval_copyout(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct xfs_inode	*dp,
	xfs_ino_t		owner,
	unsigned int		*offset,
	unsigned int		*valuelen,
	uint8_t			**dst)
{
	char			*src = bp->b_addr;
	xfs_daddr_t		bno = xfs_buf_daddr(bp);
	unsigned int		len = BBTOB(bp->b_length);
	unsigned int		blksize = mp->m_attr_geo->blksize;

	ASSERT(len >= blksize);

	while (len > 0 && *valuelen > 0) {
		unsigned int hdr_size = 0;
		unsigned int byte_cnt = xfs_attr3_rmt_buf_space(mp);

		byte_cnt = min(*valuelen, byte_cnt);

		if (xfs_has_crc(mp)) {
			if (xfs_attr3_rmt_hdr_ok(src, owner, *offset,
						  byte_cnt, bno)) {
				xfs_alert(mp,
"remote attribute header mismatch bno/off/len/owner (0x%llx/0x%x/Ox%x/0x%llx)",
					bno, *offset, byte_cnt, owner);
				xfs_dirattr_mark_sick(dp, XFS_ATTR_FORK);
				return -EFSCORRUPTED;
			}
			hdr_size = sizeof(struct xfs_attr3_rmt_hdr);
		}

		memcpy(*dst, src + hdr_size, byte_cnt);

		/* roll buffer forwards */
		len -= blksize;
		src += blksize;
		bno += BTOBB(blksize);

		/* roll attribute data forwards */
		*valuelen -= byte_cnt;
		*dst += byte_cnt;
		*offset += byte_cnt;
	}
	return 0;
}

STATIC void
xfs_attr_rmtval_copyin(
	struct xfs_mount *mp,
	struct xfs_buf	*bp,
	xfs_ino_t	ino,
	unsigned int	*offset,
	unsigned int	*valuelen,
	uint8_t		**src)
{
	char		*dst = bp->b_addr;
	xfs_daddr_t	bno = xfs_buf_daddr(bp);
	unsigned int	len = BBTOB(bp->b_length);
	unsigned int	blksize = mp->m_attr_geo->blksize;

	ASSERT(len >= blksize);

	while (len > 0 && *valuelen > 0) {
		unsigned int hdr_size;
		unsigned int byte_cnt = xfs_attr3_rmt_buf_space(mp);

		byte_cnt = min(*valuelen, byte_cnt);
		hdr_size = xfs_attr3_rmt_hdr_set(mp, dst, ino, *offset,
						 byte_cnt, bno);

		memcpy(dst + hdr_size, *src, byte_cnt);

		/*
		 * If this is the last block, zero the remainder of it.
		 * Check that we are actually the last block, too.
		 */
		if (byte_cnt + hdr_size < blksize) {
			ASSERT(*valuelen - byte_cnt == 0);
			ASSERT(len == blksize);
			memset(dst + hdr_size + byte_cnt, 0,
					blksize - hdr_size - byte_cnt);
		}

		/* roll buffer forwards */
		len -= blksize;
		dst += blksize;
		bno += BTOBB(blksize);

		/* roll attribute data forwards */
		*valuelen -= byte_cnt;
		*src += byte_cnt;
		*offset += byte_cnt;
	}
}

/*
 * Read the value associated with an attribute from the out-of-line buffer
 * that we stored it in.
 *
 * Returns 0 on successful retrieval, otherwise an error.
 */
int
xfs_attr_rmtval_get(
	struct xfs_da_args	*args)
{
	struct xfs_bmbt_irec	map[ATTR_RMTVALUE_MAPSIZE];
	struct xfs_mount	*mp = args->dp->i_mount;
	struct xfs_buf		*bp;
	xfs_dablk_t		lblkno = args->rmtblkno;
	uint8_t			*dst = args->value;
	unsigned int		valuelen;
	int			nmap;
	int			error;
	unsigned int		blkcnt = args->rmtblkcnt;
	int			i;
	unsigned int		offset = 0;

	trace_xfs_attr_rmtval_get(args);

	ASSERT(args->valuelen != 0);
	ASSERT(args->rmtvaluelen == args->valuelen);

	valuelen = args->rmtvaluelen;
	while (valuelen > 0) {
		nmap = ATTR_RMTVALUE_MAPSIZE;
		error = xfs_bmapi_read(args->dp, (xfs_fileoff_t)lblkno,
				       blkcnt, map, &nmap,
				       XFS_BMAPI_ATTRFORK);
		if (error)
			return error;
		ASSERT(nmap >= 1);

		for (i = 0; (i < nmap) && (valuelen > 0); i++) {
			xfs_daddr_t	dblkno;
			int		dblkcnt;

			ASSERT((map[i].br_startblock != DELAYSTARTBLOCK) &&
			       (map[i].br_startblock != HOLESTARTBLOCK));
			dblkno = XFS_FSB_TO_DADDR(mp, map[i].br_startblock);
			dblkcnt = XFS_FSB_TO_BB(mp, map[i].br_blockcount);
			error = xfs_buf_read(mp->m_ddev_targp, dblkno, dblkcnt,
					0, &bp, &xfs_attr3_rmt_buf_ops);
			if (xfs_metadata_is_sick(error))
				xfs_dirattr_mark_sick(args->dp, XFS_ATTR_FORK);
			if (error)
				return error;

			error = xfs_attr_rmtval_copyout(mp, bp, args->dp,
					args->owner, &offset, &valuelen, &dst);
			xfs_buf_relse(bp);
			if (error)
				return error;

			/* roll attribute extent map forwards */
			lblkno += map[i].br_blockcount;
			blkcnt -= map[i].br_blockcount;
		}
	}
	ASSERT(valuelen == 0);
	return 0;
}

/*
 * Find a "hole" in the attribute address space large enough for us to drop the
 * new attributes value into
 */
int
xfs_attr_rmt_find_hole(
	struct xfs_da_args	*args)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			error;
	unsigned int		blkcnt;
	xfs_fileoff_t		lfileoff = 0;

	/*
	 * Because CRC enable attributes have headers, we can't just do a
	 * straight byte to FSB conversion and have to take the header space
	 * into account.
	 */
	blkcnt = xfs_attr3_rmt_blocks(mp, args->rmtvaluelen);
	error = xfs_bmap_first_unused(args->trans, args->dp, blkcnt, &lfileoff,
						   XFS_ATTR_FORK);
	if (error)
		return error;

	args->rmtblkno = (xfs_dablk_t)lfileoff;
	args->rmtblkcnt = blkcnt;

	return 0;
}

int
xfs_attr_rmtval_set_value(
	struct xfs_da_args	*args)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_bmbt_irec	map;
	xfs_dablk_t		lblkno;
	uint8_t			*src = args->value;
	unsigned int		blkcnt;
	unsigned int		valuelen;
	int			nmap;
	int			error;
	unsigned int		offset = 0;

	/*
	 * Roll through the "value", copying the attribute value to the
	 * already-allocated blocks.  Blocks are written synchronously
	 * so that we can know they are all on disk before we turn off
	 * the INCOMPLETE flag.
	 */
	lblkno = args->rmtblkno;
	blkcnt = args->rmtblkcnt;
	valuelen = args->rmtvaluelen;
	while (valuelen > 0) {
		struct xfs_buf	*bp;
		xfs_daddr_t	dblkno;
		int		dblkcnt;

		ASSERT(blkcnt > 0);

		nmap = 1;
		error = xfs_bmapi_read(dp, (xfs_fileoff_t)lblkno,
				       blkcnt, &map, &nmap,
				       XFS_BMAPI_ATTRFORK);
		if (error)
			return error;
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		dblkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);

		error = xfs_buf_get(mp->m_ddev_targp, dblkno, dblkcnt, &bp);
		if (error)
			return error;
		bp->b_ops = &xfs_attr3_rmt_buf_ops;

		xfs_attr_rmtval_copyin(mp, bp, args->owner, &offset, &valuelen,
				&src);

		error = xfs_bwrite(bp);	/* GROT: NOTE: synchronous write */
		xfs_buf_relse(bp);
		if (error)
			return error;


		/* roll attribute extent map forwards */
		lblkno += map.br_blockcount;
		blkcnt -= map.br_blockcount;
	}
	ASSERT(valuelen == 0);
	return 0;
}

/* Mark stale any incore buffers for the remote value. */
int
xfs_attr_rmtval_stale(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*map,
	xfs_buf_flags_t		incore_flags)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_buf		*bp;
	int			error;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);

	if (XFS_IS_CORRUPT(mp, map->br_startblock == DELAYSTARTBLOCK) ||
	    XFS_IS_CORRUPT(mp, map->br_startblock == HOLESTARTBLOCK)) {
		xfs_bmap_mark_sick(ip, XFS_ATTR_FORK);
		return -EFSCORRUPTED;
	}

	error = xfs_buf_incore(mp->m_ddev_targp,
			XFS_FSB_TO_DADDR(mp, map->br_startblock),
			XFS_FSB_TO_BB(mp, map->br_blockcount),
			incore_flags, &bp);
	if (error) {
		if (error == -ENOENT)
			return 0;
		return error;
	}

	xfs_buf_stale(bp);
	xfs_buf_relse(bp);
	return 0;
}

/*
 * Find a hole for the attr and store it in the delayed attr context.  This
 * initializes the context to roll through allocating an attr extent for a
 * delayed attr operation
 */
int
xfs_attr_rmtval_find_space(
	struct xfs_attr_intent		*attr)
{
	struct xfs_da_args		*args = attr->xattri_da_args;
	struct xfs_bmbt_irec		*map = &attr->xattri_map;
	int				error;

	attr->xattri_lblkno = 0;
	attr->xattri_blkcnt = 0;
	args->rmtblkcnt = 0;
	args->rmtblkno = 0;
	memset(map, 0, sizeof(struct xfs_bmbt_irec));

	error = xfs_attr_rmt_find_hole(args);
	if (error)
		return error;

	attr->xattri_blkcnt = args->rmtblkcnt;
	attr->xattri_lblkno = args->rmtblkno;

	return 0;
}

/*
 * Write one block of the value associated with an attribute into the
 * out-of-line buffer that we have defined for it. This is similar to a subset
 * of xfs_attr_rmtval_set, but records the current block to the delayed attr
 * context, and leaves transaction handling to the caller.
 */
int
xfs_attr_rmtval_set_blk(
	struct xfs_attr_intent		*attr)
{
	struct xfs_da_args		*args = attr->xattri_da_args;
	struct xfs_inode		*dp = args->dp;
	struct xfs_bmbt_irec		*map = &attr->xattri_map;
	int nmap;
	int error;

	nmap = 1;
	error = xfs_bmapi_write(args->trans, dp,
			(xfs_fileoff_t)attr->xattri_lblkno,
			attr->xattri_blkcnt, XFS_BMAPI_ATTRFORK, args->total,
			map, &nmap);
	if (error)
		return error;

	ASSERT((map->br_startblock != DELAYSTARTBLOCK) &&
	       (map->br_startblock != HOLESTARTBLOCK));

	/* roll attribute extent map forwards */
	attr->xattri_lblkno += map->br_blockcount;
	attr->xattri_blkcnt -= map->br_blockcount;

	return 0;
}

/*
 * Remove the value associated with an attribute by deleting the
 * out-of-line buffer that it is stored on.
 */
int
xfs_attr_rmtval_invalidate(
	struct xfs_da_args	*args)
{
	xfs_dablk_t		lblkno;
	unsigned int		blkcnt;
	int			error;

	/*
	 * Roll through the "value", invalidating the attribute value's blocks.
	 */
	lblkno = args->rmtblkno;
	blkcnt = args->rmtblkcnt;
	while (blkcnt > 0) {
		struct xfs_bmbt_irec	map;
		int			nmap;

		/*
		 * Try to remember where we decided to put the value.
		 */
		nmap = 1;
		error = xfs_bmapi_read(args->dp, (xfs_fileoff_t)lblkno,
				       blkcnt, &map, &nmap, XFS_BMAPI_ATTRFORK);
		if (error)
			return error;
		if (XFS_IS_CORRUPT(args->dp->i_mount, nmap != 1)) {
			xfs_bmap_mark_sick(args->dp, XFS_ATTR_FORK);
			return -EFSCORRUPTED;
		}
		error = xfs_attr_rmtval_stale(args->dp, &map, XBF_TRYLOCK);
		if (error)
			return error;

		lblkno += map.br_blockcount;
		blkcnt -= map.br_blockcount;
	}
	return 0;
}

/*
 * Remove the value associated with an attribute by deleting the out-of-line
 * buffer that it is stored on. Returns -EAGAIN for the caller to refresh the
 * transaction and re-call the function.  Callers should keep calling this
 * routine until it returns something other than -EAGAIN.
 */
int
xfs_attr_rmtval_remove(
	struct xfs_attr_intent		*attr)
{
	struct xfs_da_args		*args = attr->xattri_da_args;
	int				error, done;

	/*
	 * Unmap value blocks for this attr.
	 */
	error = xfs_bunmapi(args->trans, args->dp, args->rmtblkno,
			    args->rmtblkcnt, XFS_BMAPI_ATTRFORK, 1, &done);
	if (error)
		return error;

	/*
	 * We don't need an explicit state here to pick up where we left off. We
	 * can figure it out using the !done return code. The actual value of
	 * attr->xattri_dela_state may be some value reminiscent of the calling
	 * function, but it's value is irrelevant with in the context of this
	 * function. Once we are done here, the next state is set as needed by
	 * the parent
	 */
	if (!done) {
		trace_xfs_attr_rmtval_remove_return(attr->xattri_dela_state,
						    args->dp);
		return -EAGAIN;
	}

	args->rmtblkno = 0;
	args->rmtblkcnt = 0;
	return 0;
}
