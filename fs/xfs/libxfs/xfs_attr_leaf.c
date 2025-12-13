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
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_attr_sf.h"
#include "xfs_attr.h"
#include "xfs_attr_remote.h"
#include "xfs_attr_leaf.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_buf_item.h"
#include "xfs_dir2.h"
#include "xfs_log.h"
#include "xfs_ag.h"
#include "xfs_errortag.h"
#include "xfs_health.h"


/*
 * xfs_attr_leaf.c
 *
 * Routines to implement leaf blocks of attributes as Btrees of hashed names.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
STATIC int xfs_attr3_leaf_create(struct xfs_da_args *args,
				 xfs_dablk_t which_block, struct xfs_buf **bpp);
STATIC void xfs_attr3_leaf_add_work(struct xfs_buf *leaf_buffer,
				   struct xfs_attr3_icleaf_hdr *ichdr,
				   struct xfs_da_args *args, int freemap_index);
STATIC void xfs_attr3_leaf_compact(struct xfs_da_args *args,
				   struct xfs_attr3_icleaf_hdr *ichdr,
				   struct xfs_buf *leaf_buffer);
STATIC void xfs_attr3_leaf_rebalance(xfs_da_state_t *state,
						   xfs_da_state_blk_t *blk1,
						   xfs_da_state_blk_t *blk2);
STATIC int xfs_attr3_leaf_figure_balance(xfs_da_state_t *state,
			xfs_da_state_blk_t *leaf_blk_1,
			struct xfs_attr3_icleaf_hdr *ichdr1,
			xfs_da_state_blk_t *leaf_blk_2,
			struct xfs_attr3_icleaf_hdr *ichdr2,
			int *number_entries_in_blk1,
			int *number_usedbytes_in_blk1);

/*
 * Utility routines.
 */
STATIC void xfs_attr3_leaf_moveents(struct xfs_da_args *args,
			struct xfs_attr_leafblock *src_leaf,
			struct xfs_attr3_icleaf_hdr *src_ichdr, int src_start,
			struct xfs_attr_leafblock *dst_leaf,
			struct xfs_attr3_icleaf_hdr *dst_ichdr, int dst_start,
			int move_count);
STATIC int xfs_attr_leaf_entsize(xfs_attr_leafblock_t *leaf, int index);

/*
 * attr3 block 'firstused' conversion helpers.
 *
 * firstused refers to the offset of the first used byte of the nameval region
 * of an attr leaf block. The region starts at the tail of the block and expands
 * backwards towards the middle. As such, firstused is initialized to the block
 * size for an empty leaf block and is reduced from there.
 *
 * The attr3 block size is pegged to the fsb size and the maximum fsb is 64k.
 * The in-core firstused field is 32-bit and thus supports the maximum fsb size.
 * The on-disk field is only 16-bit, however, and overflows at 64k. Since this
 * only occurs at exactly 64k, we use zero as a magic on-disk value to represent
 * the attr block size. The following helpers manage the conversion between the
 * in-core and on-disk formats.
 */

static void
xfs_attr3_leaf_firstused_from_disk(
	struct xfs_da_geometry		*geo,
	struct xfs_attr3_icleaf_hdr	*to,
	struct xfs_attr_leafblock	*from)
{
	struct xfs_attr3_leaf_hdr	*hdr3;

	if (from->hdr.info.magic == cpu_to_be16(XFS_ATTR3_LEAF_MAGIC)) {
		hdr3 = (struct xfs_attr3_leaf_hdr *) from;
		to->firstused = be16_to_cpu(hdr3->firstused);
	} else {
		to->firstused = be16_to_cpu(from->hdr.firstused);
	}

	/*
	 * Convert from the magic fsb size value to actual blocksize. This
	 * should only occur for empty blocks when the block size overflows
	 * 16-bits.
	 */
	if (to->firstused == XFS_ATTR3_LEAF_NULLOFF) {
		ASSERT(!to->count && !to->usedbytes);
		ASSERT(geo->blksize > USHRT_MAX);
		to->firstused = geo->blksize;
	}
}

static void
xfs_attr3_leaf_firstused_to_disk(
	struct xfs_da_geometry		*geo,
	struct xfs_attr_leafblock	*to,
	struct xfs_attr3_icleaf_hdr	*from)
{
	struct xfs_attr3_leaf_hdr	*hdr3;
	uint32_t			firstused;

	/* magic value should only be seen on disk */
	ASSERT(from->firstused != XFS_ATTR3_LEAF_NULLOFF);

	/*
	 * Scale down the 32-bit in-core firstused value to the 16-bit on-disk
	 * value. This only overflows at the max supported value of 64k. Use the
	 * magic on-disk value to represent block size in this case.
	 */
	firstused = from->firstused;
	if (firstused > USHRT_MAX) {
		ASSERT(from->firstused == geo->blksize);
		firstused = XFS_ATTR3_LEAF_NULLOFF;
	}

	if (from->magic == XFS_ATTR3_LEAF_MAGIC) {
		hdr3 = (struct xfs_attr3_leaf_hdr *) to;
		hdr3->firstused = cpu_to_be16(firstused);
	} else {
		to->hdr.firstused = cpu_to_be16(firstused);
	}
}

void
xfs_attr3_leaf_hdr_from_disk(
	struct xfs_da_geometry		*geo,
	struct xfs_attr3_icleaf_hdr	*to,
	struct xfs_attr_leafblock	*from)
{
	int	i;

	ASSERT(from->hdr.info.magic == cpu_to_be16(XFS_ATTR_LEAF_MAGIC) ||
	       from->hdr.info.magic == cpu_to_be16(XFS_ATTR3_LEAF_MAGIC));

	if (from->hdr.info.magic == cpu_to_be16(XFS_ATTR3_LEAF_MAGIC)) {
		struct xfs_attr3_leaf_hdr *hdr3 = (struct xfs_attr3_leaf_hdr *)from;

		to->forw = be32_to_cpu(hdr3->info.hdr.forw);
		to->back = be32_to_cpu(hdr3->info.hdr.back);
		to->magic = be16_to_cpu(hdr3->info.hdr.magic);
		to->count = be16_to_cpu(hdr3->count);
		to->usedbytes = be16_to_cpu(hdr3->usedbytes);
		xfs_attr3_leaf_firstused_from_disk(geo, to, from);
		to->holes = hdr3->holes;

		for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
			to->freemap[i].base = be16_to_cpu(hdr3->freemap[i].base);
			to->freemap[i].size = be16_to_cpu(hdr3->freemap[i].size);
		}
		return;
	}
	to->forw = be32_to_cpu(from->hdr.info.forw);
	to->back = be32_to_cpu(from->hdr.info.back);
	to->magic = be16_to_cpu(from->hdr.info.magic);
	to->count = be16_to_cpu(from->hdr.count);
	to->usedbytes = be16_to_cpu(from->hdr.usedbytes);
	xfs_attr3_leaf_firstused_from_disk(geo, to, from);
	to->holes = from->hdr.holes;

	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
		to->freemap[i].base = be16_to_cpu(from->hdr.freemap[i].base);
		to->freemap[i].size = be16_to_cpu(from->hdr.freemap[i].size);
	}
}

void
xfs_attr3_leaf_hdr_to_disk(
	struct xfs_da_geometry		*geo,
	struct xfs_attr_leafblock	*to,
	struct xfs_attr3_icleaf_hdr	*from)
{
	int				i;

	ASSERT(from->magic == XFS_ATTR_LEAF_MAGIC ||
	       from->magic == XFS_ATTR3_LEAF_MAGIC);

	if (from->magic == XFS_ATTR3_LEAF_MAGIC) {
		struct xfs_attr3_leaf_hdr *hdr3 = (struct xfs_attr3_leaf_hdr *)to;

		hdr3->info.hdr.forw = cpu_to_be32(from->forw);
		hdr3->info.hdr.back = cpu_to_be32(from->back);
		hdr3->info.hdr.magic = cpu_to_be16(from->magic);
		hdr3->count = cpu_to_be16(from->count);
		hdr3->usedbytes = cpu_to_be16(from->usedbytes);
		xfs_attr3_leaf_firstused_to_disk(geo, to, from);
		hdr3->holes = from->holes;
		hdr3->pad1 = 0;

		for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
			hdr3->freemap[i].base = cpu_to_be16(from->freemap[i].base);
			hdr3->freemap[i].size = cpu_to_be16(from->freemap[i].size);
		}
		return;
	}
	to->hdr.info.forw = cpu_to_be32(from->forw);
	to->hdr.info.back = cpu_to_be32(from->back);
	to->hdr.info.magic = cpu_to_be16(from->magic);
	to->hdr.count = cpu_to_be16(from->count);
	to->hdr.usedbytes = cpu_to_be16(from->usedbytes);
	xfs_attr3_leaf_firstused_to_disk(geo, to, from);
	to->hdr.holes = from->holes;
	to->hdr.pad1 = 0;

	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
		to->hdr.freemap[i].base = cpu_to_be16(from->freemap[i].base);
		to->hdr.freemap[i].size = cpu_to_be16(from->freemap[i].size);
	}
}

static xfs_failaddr_t
xfs_attr3_leaf_verify_entry(
	struct xfs_mount			*mp,
	char					*buf_end,
	struct xfs_attr_leafblock		*leaf,
	struct xfs_attr3_icleaf_hdr		*leafhdr,
	struct xfs_attr_leaf_entry		*ent,
	int					idx,
	__u32					*last_hashval)
{
	struct xfs_attr_leaf_name_local		*lentry;
	struct xfs_attr_leaf_name_remote	*rentry;
	char					*name_end;
	unsigned int				nameidx;
	unsigned int				namesize;
	__u32					hashval;

	/* hash order check */
	hashval = be32_to_cpu(ent->hashval);
	if (hashval < *last_hashval)
		return __this_address;
	*last_hashval = hashval;

	nameidx = be16_to_cpu(ent->nameidx);
	if (nameidx < leafhdr->firstused || nameidx >= mp->m_attr_geo->blksize)
		return __this_address;

	/*
	 * Check the name information.  The namelen fields are u8 so we can't
	 * possibly exceed the maximum name length of 255 bytes.
	 */
	if (ent->flags & XFS_ATTR_LOCAL) {
		lentry = xfs_attr3_leaf_name_local(leaf, idx);
		namesize = xfs_attr_leaf_entsize_local(lentry->namelen,
				be16_to_cpu(lentry->valuelen));
		name_end = (char *)lentry + namesize;
		if (lentry->namelen == 0)
			return __this_address;
	} else {
		rentry = xfs_attr3_leaf_name_remote(leaf, idx);
		namesize = xfs_attr_leaf_entsize_remote(rentry->namelen);
		name_end = (char *)rentry + namesize;
		if (rentry->namelen == 0)
			return __this_address;
		if (!(ent->flags & XFS_ATTR_INCOMPLETE) &&
		    rentry->valueblk == 0)
			return __this_address;
	}

	if (name_end > buf_end)
		return __this_address;

	return NULL;
}

/*
 * Validate an attribute leaf block.
 *
 * Empty leaf blocks can occur under the following circumstances:
 *
 * 1. setxattr adds a new extended attribute to a file;
 * 2. The file has zero existing attributes;
 * 3. The attribute is too large to fit in the attribute fork;
 * 4. The attribute is small enough to fit in a leaf block;
 * 5. A log flush occurs after committing the transaction that creates
 *    the (empty) leaf block; and
 * 6. The filesystem goes down after the log flush but before the new
 *    attribute can be committed to the leaf block.
 *
 * Hence we need to ensure that we don't fail the validation purely
 * because the leaf is empty.
 */
static xfs_failaddr_t
xfs_attr3_leaf_verify(
	struct xfs_buf			*bp)
{
	struct xfs_attr3_icleaf_hdr	ichdr;
	struct xfs_mount		*mp = bp->b_mount;
	struct xfs_attr_leafblock	*leaf = bp->b_addr;
	struct xfs_attr_leaf_entry	*entries;
	struct xfs_attr_leaf_entry	*ent;
	char				*buf_end;
	uint32_t			end;	/* must be 32bit - see below */
	__u32				last_hashval = 0;
	int				i;
	xfs_failaddr_t			fa;

	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &ichdr, leaf);

	fa = xfs_da3_blkinfo_verify(bp, bp->b_addr);
	if (fa)
		return fa;

	/*
	 * firstused is the block offset of the first name info structure.
	 * Make sure it doesn't go off the block or crash into the header.
	 */
	if (ichdr.firstused > mp->m_attr_geo->blksize)
		return __this_address;
	if (ichdr.firstused < xfs_attr3_leaf_hdr_size(leaf))
		return __this_address;

	/* Make sure the entries array doesn't crash into the name info. */
	entries = xfs_attr3_leaf_entryp(bp->b_addr);
	if ((char *)&entries[ichdr.count] >
	    (char *)bp->b_addr + ichdr.firstused)
		return __this_address;

	/*
	 * NOTE: This verifier historically failed empty leaf buffers because
	 * we expect the fork to be in another format. Empty attr fork format
	 * conversions are possible during xattr set, however, and format
	 * conversion is not atomic with the xattr set that triggers it. We
	 * cannot assume leaf blocks are non-empty until that is addressed.
	*/
	buf_end = (char *)bp->b_addr + mp->m_attr_geo->blksize;
	for (i = 0, ent = entries; i < ichdr.count; ent++, i++) {
		fa = xfs_attr3_leaf_verify_entry(mp, buf_end, leaf, &ichdr,
				ent, i, &last_hashval);
		if (fa)
			return fa;
	}

	/*
	 * Quickly check the freemap information.  Attribute data has to be
	 * aligned to 4-byte boundaries, and likewise for the free space.
	 *
	 * Note that for 64k block size filesystems, the freemap entries cannot
	 * overflow as they are only be16 fields. However, when checking end
	 * pointer of the freemap, we have to be careful to detect overflows and
	 * so use uint32_t for those checks.
	 */
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
		if (ichdr.freemap[i].base > mp->m_attr_geo->blksize)
			return __this_address;
		if (ichdr.freemap[i].base & 0x3)
			return __this_address;
		if (ichdr.freemap[i].size > mp->m_attr_geo->blksize)
			return __this_address;
		if (ichdr.freemap[i].size & 0x3)
			return __this_address;

		/* be care of 16 bit overflows here */
		end = (uint32_t)ichdr.freemap[i].base + ichdr.freemap[i].size;
		if (end < ichdr.freemap[i].base)
			return __this_address;
		if (end > mp->m_attr_geo->blksize)
			return __this_address;
	}

	return NULL;
}

xfs_failaddr_t
xfs_attr3_leaf_header_check(
	struct xfs_buf		*bp,
	xfs_ino_t		owner)
{
	struct xfs_mount	*mp = bp->b_mount;

	if (xfs_has_crc(mp)) {
		struct xfs_attr3_leafblock *hdr3 = bp->b_addr;

		if (hdr3->hdr.info.hdr.magic !=
				cpu_to_be16(XFS_ATTR3_LEAF_MAGIC))
			return __this_address;

		if (be64_to_cpu(hdr3->hdr.info.owner) != owner)
			return __this_address;
	}

	return NULL;
}

static void
xfs_attr3_leaf_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	struct xfs_attr3_leaf_hdr *hdr3 = bp->b_addr;
	xfs_failaddr_t		fa;

	fa = xfs_attr3_leaf_verify(bp);
	if (fa) {
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}

	if (!xfs_has_crc(mp))
		return;

	if (bip)
		hdr3->info.lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_ATTR3_LEAF_CRC_OFF);
}

/*
 * leaf/node format detection on trees is sketchy, so a node read can be done on
 * leaf level blocks when detection identifies the tree as a node format tree
 * incorrectly. In this case, we need to swap the verifier to match the correct
 * format of the block being read.
 */
static void
xfs_attr3_leaf_read_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	xfs_failaddr_t		fa;

	if (xfs_has_crc(mp) &&
	     !xfs_buf_verify_cksum(bp, XFS_ATTR3_LEAF_CRC_OFF))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_attr3_leaf_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}
}

const struct xfs_buf_ops xfs_attr3_leaf_buf_ops = {
	.name = "xfs_attr3_leaf",
	.magic16 = { cpu_to_be16(XFS_ATTR_LEAF_MAGIC),
		     cpu_to_be16(XFS_ATTR3_LEAF_MAGIC) },
	.verify_read = xfs_attr3_leaf_read_verify,
	.verify_write = xfs_attr3_leaf_write_verify,
	.verify_struct = xfs_attr3_leaf_verify,
};

int
xfs_attr3_leaf_read(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	xfs_ino_t		owner,
	xfs_dablk_t		bno,
	struct xfs_buf		**bpp)
{
	xfs_failaddr_t		fa;
	int			err;

	err = xfs_da_read_buf(tp, dp, bno, 0, bpp, XFS_ATTR_FORK,
			&xfs_attr3_leaf_buf_ops);
	if (err || !(*bpp))
		return err;

	fa = xfs_attr3_leaf_header_check(*bpp, owner);
	if (fa) {
		__xfs_buf_mark_corrupt(*bpp, fa);
		xfs_trans_brelse(tp, *bpp);
		*bpp = NULL;
		xfs_dirattr_mark_sick(dp, XFS_ATTR_FORK);
		return -EFSCORRUPTED;
	}

	if (tp)
		xfs_trans_buf_set_type(tp, *bpp, XFS_BLFT_ATTR_LEAF_BUF);
	return 0;
}

/*========================================================================
 * Namespace helper routines
 *========================================================================*/

/*
 * If we are in log recovery, then we want the lookup to ignore the INCOMPLETE
 * flag on disk - if there's an incomplete attr then recovery needs to tear it
 * down. If there's no incomplete attr, then recovery needs to tear that attr
 * down to replace it with the attr that has been logged. In this case, the
 * INCOMPLETE flag will not be set in attr->attr_filter, but rather
 * XFS_DA_OP_RECOVERY will be set in args->op_flags.
 */
static inline unsigned int xfs_attr_match_mask(const struct xfs_da_args *args)
{
	if (args->op_flags & XFS_DA_OP_RECOVERY)
		return XFS_ATTR_NSP_ONDISK_MASK;
	return XFS_ATTR_NSP_ONDISK_MASK | XFS_ATTR_INCOMPLETE;
}

static inline bool
xfs_attr_parent_match(
	const struct xfs_da_args	*args,
	const void			*value,
	unsigned int			valuelen)
{
	ASSERT(args->value != NULL);

	/* Parent pointers do not use remote values */
	if (!value)
		return false;

	/*
	 * The only value we support is a parent rec.  However, we'll accept
	 * any valuelen so that offline repair can delete ATTR_PARENT values
	 * that are not parent pointers.
	 */
	if (valuelen != args->valuelen)
		return false;

	return memcmp(args->value, value, valuelen) == 0;
}

static bool
xfs_attr_match(
	struct xfs_da_args	*args,
	unsigned int		attr_flags,
	const unsigned char	*name,
	unsigned int		namelen,
	const void		*value,
	unsigned int		valuelen)
{
	unsigned int		mask = xfs_attr_match_mask(args);

	if (args->namelen != namelen)
		return false;
	if ((args->attr_filter & mask) != (attr_flags & mask))
		return false;
	if (memcmp(args->name, name, namelen) != 0)
		return false;

	if (attr_flags & XFS_ATTR_PARENT)
		return xfs_attr_parent_match(args, value, valuelen);

	return true;
}

static int
xfs_attr_copy_value(
	struct xfs_da_args	*args,
	unsigned char		*value,
	int			valuelen)
{
	/*
	 * Parent pointer lookups require the caller to specify the name and
	 * value, so don't copy anything.
	 */
	if (args->attr_filter & XFS_ATTR_PARENT)
		return 0;

	/*
	 * No copy if all we have to do is get the length
	 */
	if (!args->valuelen) {
		args->valuelen = valuelen;
		return 0;
	}

	/*
	 * No copy if the length of the existing buffer is too small
	 */
	if (args->valuelen < valuelen) {
		args->valuelen = valuelen;
		return -ERANGE;
	}

	if (!args->value) {
		args->value = kvmalloc(valuelen, GFP_KERNEL | __GFP_NOLOCKDEP);
		if (!args->value)
			return -ENOMEM;
	}
	args->valuelen = valuelen;

	/* remote block xattr requires IO for copy-in */
	if (args->rmtblkno)
		return xfs_attr_rmtval_get(args);

	/*
	 * This is to prevent a GCC warning because the remote xattr case
	 * doesn't have a value to pass in. In that case, we never reach here,
	 * but GCC can't work that out and so throws a "passing NULL to
	 * memcpy" warning.
	 */
	if (!value)
		return -EINVAL;
	memcpy(args->value, value, valuelen);
	return 0;
}

/*========================================================================
 * External routines when attribute fork size < XFS_LITINO(mp).
 *========================================================================*/

/*
 * Query whether the total requested number of attr fork bytes of extended
 * attribute space will be able to fit inline.
 *
 * Returns zero if not, else the i_forkoff fork offset to be used in the
 * literal area for attribute data once the new bytes have been added.
 *
 * i_forkoff must be 8 byte aligned, hence is stored as a >>3 value;
 * special case for dev/uuid inodes, they have fixed size data forks.
 */
int
xfs_attr_shortform_bytesfit(
	struct xfs_inode	*dp,
	int			bytes)
{
	struct xfs_mount	*mp = dp->i_mount;
	int64_t			dsize;
	int			minforkoff;
	int			maxforkoff;
	int			offset;

	/*
	 * Check if the new size could fit at all first:
	 */
	if (bytes > XFS_LITINO(mp))
		return 0;

	/* rounded down */
	offset = (XFS_LITINO(mp) - bytes) >> 3;

	if (dp->i_df.if_format == XFS_DINODE_FMT_DEV) {
		minforkoff = roundup(sizeof(xfs_dev_t), 8) >> 3;
		return (offset >= minforkoff) ? minforkoff : 0;
	}

	/*
	 * If the requested numbers of bytes is smaller or equal to the
	 * current attribute fork size we can always proceed.
	 *
	 * Note that if_bytes in the data fork might actually be larger than
	 * the current data fork size is due to delalloc extents. In that
	 * case either the extent count will go down when they are converted
	 * to real extents, or the delalloc conversion will take care of the
	 * literal area rebalancing.
	 */
	if (bytes <= xfs_inode_attr_fork_size(dp))
		return dp->i_forkoff;

	/*
	 * For attr2 we can try to move the forkoff if there is space in the
	 * literal area
	 */
	dsize = dp->i_df.if_bytes;

	switch (dp->i_df.if_format) {
	case XFS_DINODE_FMT_EXTENTS:
		/*
		 * If there is no attr fork and the data fork is extents,
		 * determine if creating the default attr fork will result
		 * in the extents form migrating to btree. If so, the
		 * minimum offset only needs to be the space required for
		 * the btree root.
		 */
		if (!dp->i_forkoff && dp->i_df.if_bytes >
		    xfs_default_attroffset(dp))
			dsize = xfs_bmdr_space_calc(MINDBTPTRS);
		break;
	case XFS_DINODE_FMT_BTREE:
		/*
		 * If we have a data btree then keep forkoff if we have one,
		 * otherwise we are adding a new attr, so then we set
		 * minforkoff to where the btree root can finish so we have
		 * plenty of room for attrs
		 */
		if (dp->i_forkoff) {
			if (offset < dp->i_forkoff)
				return 0;
			return dp->i_forkoff;
		}
		dsize = xfs_bmap_bmdr_space(dp->i_df.if_broot);
		break;
	}

	/*
	 * A data fork btree root must have space for at least
	 * MINDBTPTRS key/ptr pairs if the data fork is small or empty.
	 */
	minforkoff = max_t(int64_t, dsize, xfs_bmdr_space_calc(MINDBTPTRS));
	minforkoff = roundup(minforkoff, 8) >> 3;

	/* attr fork btree root can have at least this many key/ptr pairs */
	maxforkoff = XFS_LITINO(mp) - xfs_bmdr_space_calc(MINABTPTRS);
	maxforkoff = maxforkoff >> 3;	/* rounded down */

	if (offset >= maxforkoff)
		return maxforkoff;
	if (offset >= minforkoff)
		return offset;
	return 0;
}

/*
 * Switch on the ATTR2 superblock bit (implies also FEATURES2) unless
 * on-disk version bit says it is already set
 */
STATIC void
xfs_sbversion_add_attr2(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp)
{
	if (mp->m_sb.sb_features2 & XFS_SB_VERSION2_ATTR2BIT)
		return;

	spin_lock(&mp->m_sb_lock);
	xfs_add_attr2(mp);
	spin_unlock(&mp->m_sb_lock);
	xfs_log_sb(tp);
}

/*
 * Create the initial contents of a shortform attribute list.
 */
void
xfs_attr_shortform_create(
	struct xfs_da_args	*args)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_ifork	*ifp = &dp->i_af;
	struct xfs_attr_sf_hdr	*hdr;

	trace_xfs_attr_sf_create(args);

	ASSERT(ifp->if_bytes == 0);
	if (ifp->if_format == XFS_DINODE_FMT_EXTENTS)
		ifp->if_format = XFS_DINODE_FMT_LOCAL;

	hdr = xfs_idata_realloc(dp, sizeof(*hdr), XFS_ATTR_FORK);
	memset(hdr, 0, sizeof(*hdr));
	hdr->totsize = cpu_to_be16(sizeof(*hdr));
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_ADATA);
}

/*
 * Return the entry if the attr in args is found, or NULL if not.
 */
struct xfs_attr_sf_entry *
xfs_attr_sf_findname(
	struct xfs_da_args		*args)
{
	struct xfs_attr_sf_hdr		*sf = args->dp->i_af.if_data;
	struct xfs_attr_sf_entry	*sfe;

	for (sfe = xfs_attr_sf_firstentry(sf);
	     sfe < xfs_attr_sf_endptr(sf);
	     sfe = xfs_attr_sf_nextentry(sfe)) {
		if (xfs_attr_match(args, sfe->flags, sfe->nameval,
				sfe->namelen, &sfe->nameval[sfe->namelen],
				sfe->valuelen))
			return sfe;
	}

	return NULL;
}

/*
 * Add a name/value pair to the shortform attribute list.
 * Overflow from the inode has already been checked for.
 */
void
xfs_attr_shortform_add(
	struct xfs_da_args		*args,
	int				forkoff)
{
	struct xfs_inode		*dp = args->dp;
	struct xfs_mount		*mp = dp->i_mount;
	struct xfs_ifork		*ifp = &dp->i_af;
	struct xfs_attr_sf_hdr		*sf = ifp->if_data;
	struct xfs_attr_sf_entry	*sfe;
	int				size;

	trace_xfs_attr_sf_add(args);

	dp->i_forkoff = forkoff;

	ASSERT(ifp->if_format == XFS_DINODE_FMT_LOCAL);
	ASSERT(!xfs_attr_sf_findname(args));

	size = xfs_attr_sf_entsize_byname(args->namelen, args->valuelen);
	sf = xfs_idata_realloc(dp, size, XFS_ATTR_FORK);

	sfe = xfs_attr_sf_endptr(sf);
	sfe->namelen = args->namelen;
	sfe->valuelen = args->valuelen;
	sfe->flags = args->attr_filter;
	memcpy(sfe->nameval, args->name, args->namelen);
	memcpy(&sfe->nameval[args->namelen], args->value, args->valuelen);
	sf->count++;
	be16_add_cpu(&sf->totsize, size);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_ADATA);

	xfs_sbversion_add_attr2(mp, args->trans);
}

/*
 * After the last attribute is removed revert to original inode format,
 * making all literal area available to the data fork once more.
 */
void
xfs_attr_fork_remove(
	struct xfs_inode	*ip,
	struct xfs_trans	*tp)
{
	ASSERT(ip->i_af.if_nextents == 0);

	xfs_ifork_zap_attr(ip);
	ip->i_forkoff = 0;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}

/*
 * Remove an attribute from the shortform attribute list structure.
 */
int
xfs_attr_sf_removename(
	struct xfs_da_args		*args)
{
	struct xfs_inode		*dp = args->dp;
	struct xfs_mount		*mp = dp->i_mount;
	struct xfs_attr_sf_hdr		*sf = dp->i_af.if_data;
	struct xfs_attr_sf_entry	*sfe;
	uint16_t			totsize = be16_to_cpu(sf->totsize);
	void				*next, *end;
	int				size = 0;

	trace_xfs_attr_sf_remove(args);

	sfe = xfs_attr_sf_findname(args);
	if (!sfe) {
		/*
		 * If we are recovering an operation, finding nothing to remove
		 * is not an error, it just means there was nothing to clean up.
		 */
		if (args->op_flags & XFS_DA_OP_RECOVERY)
			return 0;
		return -ENOATTR;
	}

	/*
	 * Fix up the attribute fork data, covering the hole
	 */
	size = xfs_attr_sf_entsize(sfe);
	next = xfs_attr_sf_nextentry(sfe);
	end = xfs_attr_sf_endptr(sf);
	if (next < end)
		memmove(sfe, next, end - next);
	sf->count--;
	totsize -= size;
	sf->totsize = cpu_to_be16(totsize);

	/*
	 * Fix up the start offset of the attribute fork
	 */
	if (totsize == sizeof(struct xfs_attr_sf_hdr) &&
	    (dp->i_df.if_format != XFS_DINODE_FMT_BTREE) &&
	    !(args->op_flags & (XFS_DA_OP_ADDNAME | XFS_DA_OP_REPLACE)) &&
	    !xfs_has_parent(mp)) {
		xfs_attr_fork_remove(dp, args->trans);
	} else {
		xfs_idata_realloc(dp, -size, XFS_ATTR_FORK);
		dp->i_forkoff = xfs_attr_shortform_bytesfit(dp, totsize);
		ASSERT(dp->i_forkoff);
		ASSERT(totsize > sizeof(struct xfs_attr_sf_hdr) ||
				(args->op_flags & XFS_DA_OP_ADDNAME) ||
				dp->i_df.if_format == XFS_DINODE_FMT_BTREE ||
				xfs_has_parent(mp));
		xfs_trans_log_inode(args->trans, dp,
					XFS_ILOG_CORE | XFS_ILOG_ADATA);
	}

	xfs_sbversion_add_attr2(mp, args->trans);

	return 0;
}

/*
 * Retrieve the attribute value and length.
 *
 * If args->valuelen is zero, only the length needs to be returned.  Unlike a
 * lookup, we only return an error if the attribute does not exist or we can't
 * retrieve the value.
 */
int
xfs_attr_shortform_getvalue(
	struct xfs_da_args		*args)
{
	struct xfs_attr_sf_entry	*sfe;

	ASSERT(args->dp->i_af.if_format == XFS_DINODE_FMT_LOCAL);

	trace_xfs_attr_sf_lookup(args);

	sfe = xfs_attr_sf_findname(args);
	if (!sfe)
		return -ENOATTR;
	return xfs_attr_copy_value(args, &sfe->nameval[args->namelen],
			sfe->valuelen);
}

/* Convert from using the shortform to the leaf format. */
int
xfs_attr_shortform_to_leaf(
	struct xfs_da_args		*args)
{
	struct xfs_inode		*dp = args->dp;
	struct xfs_ifork		*ifp = &dp->i_af;
	struct xfs_attr_sf_hdr		*sf = ifp->if_data;
	struct xfs_attr_sf_entry	*sfe;
	int				size = be16_to_cpu(sf->totsize);
	struct xfs_da_args		nargs;
	char				*tmpbuffer;
	int				error, i;
	xfs_dablk_t			blkno;
	struct xfs_buf			*bp;

	trace_xfs_attr_sf_to_leaf(args);

	tmpbuffer = kmalloc(size, GFP_KERNEL | __GFP_NOFAIL);
	memcpy(tmpbuffer, ifp->if_data, size);
	sf = (struct xfs_attr_sf_hdr *)tmpbuffer;

	xfs_idata_realloc(dp, -size, XFS_ATTR_FORK);
	xfs_bmap_local_to_extents_empty(args->trans, dp, XFS_ATTR_FORK);

	bp = NULL;
	error = xfs_da_grow_inode(args, &blkno);
	if (error)
		goto out;

	ASSERT(blkno == 0);
	error = xfs_attr3_leaf_create(args, blkno, &bp);
	if (error)
		goto out;

	memset((char *)&nargs, 0, sizeof(nargs));
	nargs.dp = dp;
	nargs.geo = args->geo;
	nargs.total = args->total;
	nargs.whichfork = XFS_ATTR_FORK;
	nargs.trans = args->trans;
	nargs.op_flags = XFS_DA_OP_OKNOENT;
	nargs.owner = args->owner;

	sfe = xfs_attr_sf_firstentry(sf);
	for (i = 0; i < sf->count; i++) {
		nargs.name = sfe->nameval;
		nargs.namelen = sfe->namelen;
		nargs.value = &sfe->nameval[nargs.namelen];
		nargs.valuelen = sfe->valuelen;
		nargs.attr_filter = sfe->flags & XFS_ATTR_NSP_ONDISK_MASK;
		if (!xfs_attr_check_namespace(sfe->flags)) {
			xfs_da_mark_sick(args);
			error = -EFSCORRUPTED;
			goto out;
		}
		xfs_attr_sethash(&nargs);
		error = xfs_attr3_leaf_lookup_int(bp, &nargs); /* set a->index */
		ASSERT(error == -ENOATTR);
		if (!xfs_attr3_leaf_add(bp, &nargs))
			ASSERT(0);
		sfe = xfs_attr_sf_nextentry(sfe);
	}
	error = 0;
out:
	kfree(tmpbuffer);
	return error;
}

/*
 * Check a leaf attribute block to see if all the entries would fit into
 * a shortform attribute list.
 */
int
xfs_attr_shortform_allfit(
	struct xfs_buf		*bp,
	struct xfs_inode	*dp)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr_leaf_entry *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	struct xfs_attr3_icleaf_hdr leafhdr;
	int			bytes;
	int			i;
	struct xfs_mount	*mp = bp->b_mount;

	leaf = bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &leafhdr, leaf);
	entry = xfs_attr3_leaf_entryp(leaf);

	bytes = sizeof(struct xfs_attr_sf_hdr);
	for (i = 0; i < leafhdr.count; entry++, i++) {
		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;		/* don't copy partial entries */
		if (!(entry->flags & XFS_ATTR_LOCAL))
			return 0;
		name_loc = xfs_attr3_leaf_name_local(leaf, i);
		if (name_loc->namelen >= XFS_ATTR_SF_ENTSIZE_MAX)
			return 0;
		if (be16_to_cpu(name_loc->valuelen) >= XFS_ATTR_SF_ENTSIZE_MAX)
			return 0;
		bytes += xfs_attr_sf_entsize_byname(name_loc->namelen,
					be16_to_cpu(name_loc->valuelen));
	}
	if ((dp->i_df.if_format != XFS_DINODE_FMT_BTREE) &&
	    (bytes == sizeof(struct xfs_attr_sf_hdr)))
		return -1;
	return xfs_attr_shortform_bytesfit(dp, bytes);
}

/* Verify the consistency of a raw inline attribute fork. */
xfs_failaddr_t
xfs_attr_shortform_verify(
	struct xfs_attr_sf_hdr		*sfp,
	size_t				size)
{
	struct xfs_attr_sf_entry	*sfep = xfs_attr_sf_firstentry(sfp);
	struct xfs_attr_sf_entry	*next_sfep;
	char				*endp;
	int				i;

	/*
	 * Give up if the attribute is way too short.
	 */
	if (size < sizeof(struct xfs_attr_sf_hdr))
		return __this_address;

	endp = (char *)sfp + size;

	/* Check all reported entries */
	for (i = 0; i < sfp->count; i++) {
		/*
		 * struct xfs_attr_sf_entry has a variable length.
		 * Check the fixed-offset parts of the structure are
		 * within the data buffer.
		 * xfs_attr_sf_entry is defined with a 1-byte variable
		 * array at the end, so we must subtract that off.
		 */
		if (((char *)sfep + sizeof(*sfep)) >= endp)
			return __this_address;

		/* Don't allow names with known bad length. */
		if (sfep->namelen == 0)
			return __this_address;

		/*
		 * Check that the variable-length part of the structure is
		 * within the data buffer.  The next entry starts after the
		 * name component, so nextentry is an acceptable test.
		 */
		next_sfep = xfs_attr_sf_nextentry(sfep);
		if ((char *)next_sfep > endp)
			return __this_address;

		/*
		 * Check for unknown flags.  Short form doesn't support
		 * the incomplete or local bits, so we can use the namespace
		 * mask here.
		 */
		if (sfep->flags & ~XFS_ATTR_NSP_ONDISK_MASK)
			return __this_address;

		/*
		 * Check for invalid namespace combinations.  We only allow
		 * one namespace flag per xattr, so we can just count the
		 * bits (i.e. hweight) here.
		 */
		if (!xfs_attr_check_namespace(sfep->flags))
			return __this_address;

		sfep = next_sfep;
	}
	if ((void *)sfep != (void *)endp)
		return __this_address;

	return NULL;
}

/*
 * Convert a leaf attribute list to shortform attribute list
 */
int
xfs_attr3_leaf_to_shortform(
	struct xfs_buf		*bp,
	struct xfs_da_args	*args,
	int			forkoff)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr3_icleaf_hdr ichdr;
	struct xfs_attr_leaf_entry *entry;
	struct xfs_attr_leaf_name_local *name_loc;
	struct xfs_da_args	nargs;
	struct xfs_inode	*dp = args->dp;
	char			*tmpbuffer;
	int			error;
	int			i;

	trace_xfs_attr_leaf_to_sf(args);

	tmpbuffer = kvmalloc(args->geo->blksize, GFP_KERNEL | __GFP_NOFAIL);
	memcpy(tmpbuffer, bp->b_addr, args->geo->blksize);

	leaf = (xfs_attr_leafblock_t *)tmpbuffer;
	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr, leaf);
	entry = xfs_attr3_leaf_entryp(leaf);

	/* XXX (dgc): buffer is about to be marked stale - why zero it? */
	memset(bp->b_addr, 0, args->geo->blksize);

	/*
	 * Clean out the prior contents of the attribute list.
	 */
	error = xfs_da_shrink_inode(args, 0, bp);
	if (error)
		goto out;

	if (forkoff == -1) {
		/*
		 * Don't remove the attr fork if this operation is the first
		 * part of a attr replace operations. We're going to add a new
		 * attr immediately, so we need to keep the attr fork around in
		 * this case.
		 */
		if (!(args->op_flags & XFS_DA_OP_REPLACE)) {
			ASSERT(dp->i_df.if_format != XFS_DINODE_FMT_BTREE);
			xfs_attr_fork_remove(dp, args->trans);
		}
		goto out;
	}

	xfs_attr_shortform_create(args);

	/*
	 * Copy the attributes
	 */
	memset((char *)&nargs, 0, sizeof(nargs));
	nargs.geo = args->geo;
	nargs.dp = dp;
	nargs.total = args->total;
	nargs.whichfork = XFS_ATTR_FORK;
	nargs.trans = args->trans;
	nargs.op_flags = XFS_DA_OP_OKNOENT;
	nargs.owner = args->owner;

	for (i = 0; i < ichdr.count; entry++, i++) {
		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;	/* don't copy partial entries */
		if (!entry->nameidx)
			continue;
		ASSERT(entry->flags & XFS_ATTR_LOCAL);
		name_loc = xfs_attr3_leaf_name_local(leaf, i);
		nargs.name = name_loc->nameval;
		nargs.namelen = name_loc->namelen;
		nargs.value = &name_loc->nameval[nargs.namelen];
		nargs.valuelen = be16_to_cpu(name_loc->valuelen);
		nargs.hashval = be32_to_cpu(entry->hashval);
		nargs.attr_filter = entry->flags & XFS_ATTR_NSP_ONDISK_MASK;
		xfs_attr_shortform_add(&nargs, forkoff);
	}
	error = 0;

out:
	kvfree(tmpbuffer);
	return error;
}

/*
 * Convert from using a single leaf to a root node and a leaf.
 */
int
xfs_attr3_leaf_to_node(
	struct xfs_da_args	*args)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr3_icleaf_hdr icleafhdr;
	struct xfs_attr_leaf_entry *entries;
	struct xfs_da3_icnode_hdr icnodehdr;
	struct xfs_da_intnode	*node;
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_buf		*bp1 = NULL;
	struct xfs_buf		*bp2 = NULL;
	xfs_dablk_t		blkno;
	int			error;

	trace_xfs_attr_leaf_to_node(args);

	if (XFS_TEST_ERROR(mp, XFS_ERRTAG_ATTR_LEAF_TO_NODE)) {
		error = -EIO;
		goto out;
	}

	error = xfs_da_grow_inode(args, &blkno);
	if (error)
		goto out;
	error = xfs_attr3_leaf_read(args->trans, dp, args->owner, 0, &bp1);
	if (error)
		goto out;

	error = xfs_da_get_buf(args->trans, dp, blkno, &bp2, XFS_ATTR_FORK);
	if (error)
		goto out;

	/*
	 * Copy leaf to new buffer and log it.
	 */
	xfs_da_buf_copy(bp2, bp1, args->geo->blksize);
	xfs_trans_log_buf(args->trans, bp2, 0, args->geo->blksize - 1);

	/*
	 * Set up the new root node.
	 */
	error = xfs_da3_node_create(args, 0, 1, &bp1, XFS_ATTR_FORK);
	if (error)
		goto out;
	node = bp1->b_addr;
	xfs_da3_node_hdr_from_disk(mp, &icnodehdr, node);

	leaf = bp2->b_addr;
	xfs_attr3_leaf_hdr_from_disk(args->geo, &icleafhdr, leaf);
	entries = xfs_attr3_leaf_entryp(leaf);

	/* both on-disk, don't endian-flip twice */
	icnodehdr.btree[0].hashval = entries[icleafhdr.count - 1].hashval;
	icnodehdr.btree[0].before = cpu_to_be32(blkno);
	icnodehdr.count = 1;
	xfs_da3_node_hdr_to_disk(dp->i_mount, node, &icnodehdr);
	xfs_trans_log_buf(args->trans, bp1, 0, args->geo->blksize - 1);
	error = 0;
out:
	return error;
}

/*========================================================================
 * Routines used for growing the Btree.
 *========================================================================*/

/*
 * Create the initial contents of a leaf attribute list
 * or a leaf in a node attribute list.
 */
STATIC int
xfs_attr3_leaf_create(
	struct xfs_da_args	*args,
	xfs_dablk_t		blkno,
	struct xfs_buf		**bpp)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr3_icleaf_hdr ichdr;
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_buf		*bp;
	int			error;

	trace_xfs_attr_leaf_create(args);

	error = xfs_da_get_buf(args->trans, args->dp, blkno, &bp,
					    XFS_ATTR_FORK);
	if (error)
		return error;
	bp->b_ops = &xfs_attr3_leaf_buf_ops;
	xfs_trans_buf_set_type(args->trans, bp, XFS_BLFT_ATTR_LEAF_BUF);
	leaf = bp->b_addr;
	memset(leaf, 0, args->geo->blksize);

	memset(&ichdr, 0, sizeof(ichdr));
	ichdr.firstused = args->geo->blksize;

	if (xfs_has_crc(mp)) {
		struct xfs_da3_blkinfo *hdr3 = bp->b_addr;

		ichdr.magic = XFS_ATTR3_LEAF_MAGIC;

		hdr3->blkno = cpu_to_be64(xfs_buf_daddr(bp));
		hdr3->owner = cpu_to_be64(args->owner);
		uuid_copy(&hdr3->uuid, &mp->m_sb.sb_meta_uuid);

		ichdr.freemap[0].base = sizeof(struct xfs_attr3_leaf_hdr);
	} else {
		ichdr.magic = XFS_ATTR_LEAF_MAGIC;
		ichdr.freemap[0].base = sizeof(struct xfs_attr_leaf_hdr);
	}
	ichdr.freemap[0].size = ichdr.firstused - ichdr.freemap[0].base;

	xfs_attr3_leaf_hdr_to_disk(args->geo, leaf, &ichdr);
	xfs_trans_log_buf(args->trans, bp, 0, args->geo->blksize - 1);

	*bpp = bp;
	return 0;
}

/*
 * Split the leaf node, rebalance, then add the new entry.
 *
 * Returns 0 if the entry was added, 1 if a further split is needed or a
 * negative error number otherwise.
 */
int
xfs_attr3_leaf_split(
	struct xfs_da_state	*state,
	struct xfs_da_state_blk	*oldblk,
	struct xfs_da_state_blk	*newblk)
{
	bool			added;
	xfs_dablk_t		blkno;
	int			error;

	trace_xfs_attr_leaf_split(state->args);

	/*
	 * Allocate space for a new leaf node.
	 */
	ASSERT(oldblk->magic == XFS_ATTR_LEAF_MAGIC);
	error = xfs_da_grow_inode(state->args, &blkno);
	if (error)
		return error;
	error = xfs_attr3_leaf_create(state->args, blkno, &newblk->bp);
	if (error)
		return error;
	newblk->blkno = blkno;
	newblk->magic = XFS_ATTR_LEAF_MAGIC;

	/*
	 * Rebalance the entries across the two leaves.
	 * NOTE: rebalance() currently depends on the 2nd block being empty.
	 */
	xfs_attr3_leaf_rebalance(state, oldblk, newblk);
	error = xfs_da3_blk_link(state, oldblk, newblk);
	if (error)
		return error;

	/*
	 * Save info on "old" attribute for "atomic rename" ops, leaf_add()
	 * modifies the index/blkno/rmtblk/rmtblkcnt fields to show the
	 * "new" attrs info.  Will need the "old" info to remove it later.
	 *
	 * Insert the "new" entry in the correct block.
	 */
	if (state->inleaf) {
		trace_xfs_attr_leaf_add_old(state->args);
		added = xfs_attr3_leaf_add(oldblk->bp, state->args);
	} else {
		trace_xfs_attr_leaf_add_new(state->args);
		added = xfs_attr3_leaf_add(newblk->bp, state->args);
	}

	/*
	 * Update last hashval in each block since we added the name.
	 */
	oldblk->hashval = xfs_attr_leaf_lasthash(oldblk->bp, NULL);
	newblk->hashval = xfs_attr_leaf_lasthash(newblk->bp, NULL);
	if (!added)
		return 1;
	return 0;
}

/*
 * Add a name to the leaf attribute list structure.
 */
bool
xfs_attr3_leaf_add(
	struct xfs_buf		*bp,
	struct xfs_da_args	*args)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr3_icleaf_hdr ichdr;
	int			tablesize;
	int			entsize;
	bool			added = true;
	int			sum;
	int			tmp;
	int			i;

	trace_xfs_attr_leaf_add(args);

	leaf = bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr, leaf);
	ASSERT(args->index >= 0 && args->index <= ichdr.count);
	entsize = xfs_attr_leaf_newentsize(args, NULL);

	/*
	 * Search through freemap for first-fit on new name length.
	 * (may need to figure in size of entry struct too)
	 */
	tablesize = (ichdr.count + 1) * sizeof(xfs_attr_leaf_entry_t)
					+ xfs_attr3_leaf_hdr_size(leaf);
	for (sum = 0, i = XFS_ATTR_LEAF_MAPSIZE - 1; i >= 0; i--) {
		if (tablesize > ichdr.firstused) {
			sum += ichdr.freemap[i].size;
			continue;
		}
		if (!ichdr.freemap[i].size)
			continue;	/* no space in this map */
		tmp = entsize;
		if (ichdr.freemap[i].base < ichdr.firstused)
			tmp += sizeof(xfs_attr_leaf_entry_t);
		if (ichdr.freemap[i].size >= tmp) {
			xfs_attr3_leaf_add_work(bp, &ichdr, args, i);
			goto out_log_hdr;
		}
		sum += ichdr.freemap[i].size;
	}

	/*
	 * If there are no holes in the address space of the block,
	 * and we don't have enough freespace, then compaction will do us
	 * no good and we should just give up.
	 */
	if (!ichdr.holes && sum < entsize)
		return false;

	/*
	 * Compact the entries to coalesce free space.
	 * This may change the hdr->count via dropping INCOMPLETE entries.
	 */
	xfs_attr3_leaf_compact(args, &ichdr, bp);

	/*
	 * After compaction, the block is guaranteed to have only one
	 * free region, in freemap[0].  If it is not big enough, give up.
	 */
	if (ichdr.freemap[0].size < (entsize + sizeof(xfs_attr_leaf_entry_t))) {
		added = false;
		goto out_log_hdr;
	}

	xfs_attr3_leaf_add_work(bp, &ichdr, args, 0);

out_log_hdr:
	xfs_attr3_leaf_hdr_to_disk(args->geo, leaf, &ichdr);
	xfs_trans_log_buf(args->trans, bp,
		XFS_DA_LOGRANGE(leaf, &leaf->hdr,
				xfs_attr3_leaf_hdr_size(leaf)));
	return added;
}

/*
 * Add a name to a leaf attribute list structure.
 */
STATIC void
xfs_attr3_leaf_add_work(
	struct xfs_buf		*bp,
	struct xfs_attr3_icleaf_hdr *ichdr,
	struct xfs_da_args	*args,
	int			mapindex)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr_leaf_entry *entry;
	struct xfs_attr_leaf_name_local *name_loc;
	struct xfs_attr_leaf_name_remote *name_rmt;
	struct xfs_mount	*mp;
	int			tmp;
	int			i;

	trace_xfs_attr_leaf_add_work(args);

	leaf = bp->b_addr;
	ASSERT(mapindex >= 0 && mapindex < XFS_ATTR_LEAF_MAPSIZE);
	ASSERT(args->index >= 0 && args->index <= ichdr->count);

	/*
	 * Force open some space in the entry array and fill it in.
	 */
	entry = &xfs_attr3_leaf_entryp(leaf)[args->index];
	if (args->index < ichdr->count) {
		tmp  = ichdr->count - args->index;
		tmp *= sizeof(xfs_attr_leaf_entry_t);
		memmove(entry + 1, entry, tmp);
		xfs_trans_log_buf(args->trans, bp,
		    XFS_DA_LOGRANGE(leaf, entry, tmp + sizeof(*entry)));
	}
	ichdr->count++;

	/*
	 * Allocate space for the new string (at the end of the run).
	 */
	mp = args->trans->t_mountp;
	ASSERT(ichdr->freemap[mapindex].base < args->geo->blksize);
	ASSERT((ichdr->freemap[mapindex].base & 0x3) == 0);
	ASSERT(ichdr->freemap[mapindex].size >=
		xfs_attr_leaf_newentsize(args, NULL));
	ASSERT(ichdr->freemap[mapindex].size < args->geo->blksize);
	ASSERT((ichdr->freemap[mapindex].size & 0x3) == 0);

	ichdr->freemap[mapindex].size -= xfs_attr_leaf_newentsize(args, &tmp);

	entry->nameidx = cpu_to_be16(ichdr->freemap[mapindex].base +
				     ichdr->freemap[mapindex].size);
	entry->hashval = cpu_to_be32(args->hashval);
	entry->flags = args->attr_filter;
	if (tmp)
		entry->flags |= XFS_ATTR_LOCAL;
	if (args->op_flags & XFS_DA_OP_REPLACE) {
		if (!(args->op_flags & XFS_DA_OP_LOGGED))
			entry->flags |= XFS_ATTR_INCOMPLETE;
		if ((args->blkno2 == args->blkno) &&
		    (args->index2 <= args->index)) {
			args->index2++;
		}
	}
	xfs_trans_log_buf(args->trans, bp,
			  XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));
	ASSERT((args->index == 0) ||
	       (be32_to_cpu(entry->hashval) >= be32_to_cpu((entry-1)->hashval)));
	ASSERT((args->index == ichdr->count - 1) ||
	       (be32_to_cpu(entry->hashval) <= be32_to_cpu((entry+1)->hashval)));

	/*
	 * For "remote" attribute values, simply note that we need to
	 * allocate space for the "remote" value.  We can't actually
	 * allocate the extents in this transaction, and we can't decide
	 * which blocks they should be as we might allocate more blocks
	 * as part of this transaction (a split operation for example).
	 */
	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = xfs_attr3_leaf_name_local(leaf, args->index);
		name_loc->namelen = args->namelen;
		name_loc->valuelen = cpu_to_be16(args->valuelen);
		memcpy((char *)name_loc->nameval, args->name, args->namelen);
		memcpy((char *)&name_loc->nameval[args->namelen], args->value,
				   be16_to_cpu(name_loc->valuelen));
	} else {
		name_rmt = xfs_attr3_leaf_name_remote(leaf, args->index);
		name_rmt->namelen = args->namelen;
		memcpy((char *)name_rmt->name, args->name, args->namelen);
		entry->flags |= XFS_ATTR_INCOMPLETE;
		/* just in case */
		name_rmt->valuelen = 0;
		name_rmt->valueblk = 0;
		args->rmtblkno = 1;
		args->rmtblkcnt = xfs_attr3_rmt_blocks(mp, args->valuelen);
		args->rmtvaluelen = args->valuelen;
	}
	xfs_trans_log_buf(args->trans, bp,
	     XFS_DA_LOGRANGE(leaf, xfs_attr3_leaf_name(leaf, args->index),
				   xfs_attr_leaf_entsize(leaf, args->index)));

	/*
	 * Update the control info for this leaf node
	 */
	if (be16_to_cpu(entry->nameidx) < ichdr->firstused)
		ichdr->firstused = be16_to_cpu(entry->nameidx);

	ASSERT(ichdr->firstused >= ichdr->count * sizeof(xfs_attr_leaf_entry_t)
					+ xfs_attr3_leaf_hdr_size(leaf));
	tmp = (ichdr->count - 1) * sizeof(xfs_attr_leaf_entry_t)
					+ xfs_attr3_leaf_hdr_size(leaf);

	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
		if (ichdr->freemap[i].base == tmp) {
			ichdr->freemap[i].base += sizeof(xfs_attr_leaf_entry_t);
			ichdr->freemap[i].size -=
				min_t(uint16_t, ichdr->freemap[i].size,
						sizeof(xfs_attr_leaf_entry_t));
		}
	}
	ichdr->usedbytes += xfs_attr_leaf_entsize(leaf, args->index);
}

/*
 * Garbage collect a leaf attribute list block by copying it to a new buffer.
 */
STATIC void
xfs_attr3_leaf_compact(
	struct xfs_da_args	*args,
	struct xfs_attr3_icleaf_hdr *ichdr_dst,
	struct xfs_buf		*bp)
{
	struct xfs_attr_leafblock *leaf_src;
	struct xfs_attr_leafblock *leaf_dst;
	struct xfs_attr3_icleaf_hdr ichdr_src;
	struct xfs_trans	*trans = args->trans;
	char			*tmpbuffer;

	trace_xfs_attr_leaf_compact(args);

	tmpbuffer = kvmalloc(args->geo->blksize, GFP_KERNEL | __GFP_NOFAIL);
	memcpy(tmpbuffer, bp->b_addr, args->geo->blksize);
	memset(bp->b_addr, 0, args->geo->blksize);
	leaf_src = (xfs_attr_leafblock_t *)tmpbuffer;
	leaf_dst = bp->b_addr;

	/*
	 * Copy the on-disk header back into the destination buffer to ensure
	 * all the information in the header that is not part of the incore
	 * header structure is preserved.
	 */
	memcpy(bp->b_addr, tmpbuffer, xfs_attr3_leaf_hdr_size(leaf_src));

	/* Initialise the incore headers */
	ichdr_src = *ichdr_dst;	/* struct copy */
	ichdr_dst->firstused = args->geo->blksize;
	ichdr_dst->usedbytes = 0;
	ichdr_dst->count = 0;
	ichdr_dst->holes = 0;
	ichdr_dst->freemap[0].base = xfs_attr3_leaf_hdr_size(leaf_src);
	ichdr_dst->freemap[0].size = ichdr_dst->firstused -
						ichdr_dst->freemap[0].base;

	/* write the header back to initialise the underlying buffer */
	xfs_attr3_leaf_hdr_to_disk(args->geo, leaf_dst, ichdr_dst);

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate name/value pairs packed and in sequence.
	 */
	xfs_attr3_leaf_moveents(args, leaf_src, &ichdr_src, 0,
				leaf_dst, ichdr_dst, 0, ichdr_src.count);
	/*
	 * this logs the entire buffer, but the caller must write the header
	 * back to the buffer when it is finished modifying it.
	 */
	xfs_trans_log_buf(trans, bp, 0, args->geo->blksize - 1);

	kvfree(tmpbuffer);
}

/*
 * Compare two leaf blocks "order".
 * Return 0 unless leaf2 should go before leaf1.
 */
static int
xfs_attr3_leaf_order(
	struct xfs_buf	*leaf1_bp,
	struct xfs_attr3_icleaf_hdr *leaf1hdr,
	struct xfs_buf	*leaf2_bp,
	struct xfs_attr3_icleaf_hdr *leaf2hdr)
{
	struct xfs_attr_leaf_entry *entries1;
	struct xfs_attr_leaf_entry *entries2;

	entries1 = xfs_attr3_leaf_entryp(leaf1_bp->b_addr);
	entries2 = xfs_attr3_leaf_entryp(leaf2_bp->b_addr);
	if (leaf1hdr->count > 0 && leaf2hdr->count > 0 &&
	    ((be32_to_cpu(entries2[0].hashval) <
	      be32_to_cpu(entries1[0].hashval)) ||
	     (be32_to_cpu(entries2[leaf2hdr->count - 1].hashval) <
	      be32_to_cpu(entries1[leaf1hdr->count - 1].hashval)))) {
		return 1;
	}
	return 0;
}

int
xfs_attr_leaf_order(
	struct xfs_buf	*leaf1_bp,
	struct xfs_buf	*leaf2_bp)
{
	struct xfs_attr3_icleaf_hdr ichdr1;
	struct xfs_attr3_icleaf_hdr ichdr2;
	struct xfs_mount *mp = leaf1_bp->b_mount;

	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &ichdr1, leaf1_bp->b_addr);
	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &ichdr2, leaf2_bp->b_addr);
	return xfs_attr3_leaf_order(leaf1_bp, &ichdr1, leaf2_bp, &ichdr2);
}

/*
 * Redistribute the attribute list entries between two leaf nodes,
 * taking into account the size of the new entry.
 *
 * NOTE: if new block is empty, then it will get the upper half of the
 * old block.  At present, all (one) callers pass in an empty second block.
 *
 * This code adjusts the args->index/blkno and args->index2/blkno2 fields
 * to match what it is doing in splitting the attribute leaf block.  Those
 * values are used in "atomic rename" operations on attributes.  Note that
 * the "new" and "old" values can end up in different blocks.
 */
STATIC void
xfs_attr3_leaf_rebalance(
	struct xfs_da_state	*state,
	struct xfs_da_state_blk	*blk1,
	struct xfs_da_state_blk	*blk2)
{
	struct xfs_da_args	*args;
	struct xfs_attr_leafblock *leaf1;
	struct xfs_attr_leafblock *leaf2;
	struct xfs_attr3_icleaf_hdr ichdr1;
	struct xfs_attr3_icleaf_hdr ichdr2;
	struct xfs_attr_leaf_entry *entries1;
	struct xfs_attr_leaf_entry *entries2;
	int			count;
	int			totallen;
	int			max;
	int			space;
	int			swap;

	/*
	 * Set up environment.
	 */
	ASSERT(blk1->magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(blk2->magic == XFS_ATTR_LEAF_MAGIC);
	leaf1 = blk1->bp->b_addr;
	leaf2 = blk2->bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(state->args->geo, &ichdr1, leaf1);
	xfs_attr3_leaf_hdr_from_disk(state->args->geo, &ichdr2, leaf2);
	ASSERT(ichdr2.count == 0);
	args = state->args;

	trace_xfs_attr_leaf_rebalance(args);

	/*
	 * Check ordering of blocks, reverse if it makes things simpler.
	 *
	 * NOTE: Given that all (current) callers pass in an empty
	 * second block, this code should never set "swap".
	 */
	swap = 0;
	if (xfs_attr3_leaf_order(blk1->bp, &ichdr1, blk2->bp, &ichdr2)) {
		swap(blk1, blk2);

		/* swap structures rather than reconverting them */
		swap(ichdr1, ichdr2);

		leaf1 = blk1->bp->b_addr;
		leaf2 = blk2->bp->b_addr;
		swap = 1;
	}

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.  Then get
	 * the direction to copy and the number of elements to move.
	 *
	 * "inleaf" is true if the new entry should be inserted into blk1.
	 * If "swap" is also true, then reverse the sense of "inleaf".
	 */
	state->inleaf = xfs_attr3_leaf_figure_balance(state, blk1, &ichdr1,
						      blk2, &ichdr2,
						      &count, &totallen);
	if (swap)
		state->inleaf = !state->inleaf;

	/*
	 * Move any entries required from leaf to leaf:
	 */
	if (count < ichdr1.count) {
		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		/* number entries being moved */
		count = ichdr1.count - count;
		space  = ichdr1.usedbytes - totallen;
		space += count * sizeof(xfs_attr_leaf_entry_t);

		/*
		 * leaf2 is the destination, compact it if it looks tight.
		 */
		max  = ichdr2.firstused - xfs_attr3_leaf_hdr_size(leaf1);
		max -= ichdr2.count * sizeof(xfs_attr_leaf_entry_t);
		if (space > max)
			xfs_attr3_leaf_compact(args, &ichdr2, blk2->bp);

		/*
		 * Move high entries from leaf1 to low end of leaf2.
		 */
		xfs_attr3_leaf_moveents(args, leaf1, &ichdr1,
				ichdr1.count - count, leaf2, &ichdr2, 0, count);

	} else if (count > ichdr1.count) {
		/*
		 * I assert that since all callers pass in an empty
		 * second buffer, this code should never execute.
		 */
		ASSERT(0);

		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		/* number entries being moved */
		count -= ichdr1.count;
		space  = totallen - ichdr1.usedbytes;
		space += count * sizeof(xfs_attr_leaf_entry_t);

		/*
		 * leaf1 is the destination, compact it if it looks tight.
		 */
		max  = ichdr1.firstused - xfs_attr3_leaf_hdr_size(leaf1);
		max -= ichdr1.count * sizeof(xfs_attr_leaf_entry_t);
		if (space > max)
			xfs_attr3_leaf_compact(args, &ichdr1, blk1->bp);

		/*
		 * Move low entries from leaf2 to high end of leaf1.
		 */
		xfs_attr3_leaf_moveents(args, leaf2, &ichdr2, 0, leaf1, &ichdr1,
					ichdr1.count, count);
	}

	xfs_attr3_leaf_hdr_to_disk(state->args->geo, leaf1, &ichdr1);
	xfs_attr3_leaf_hdr_to_disk(state->args->geo, leaf2, &ichdr2);
	xfs_trans_log_buf(args->trans, blk1->bp, 0, args->geo->blksize - 1);
	xfs_trans_log_buf(args->trans, blk2->bp, 0, args->geo->blksize - 1);

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	entries1 = xfs_attr3_leaf_entryp(leaf1);
	entries2 = xfs_attr3_leaf_entryp(leaf2);
	blk1->hashval = be32_to_cpu(entries1[ichdr1.count - 1].hashval);
	blk2->hashval = be32_to_cpu(entries2[ichdr2.count - 1].hashval);

	/*
	 * Adjust the expected index for insertion.
	 * NOTE: this code depends on the (current) situation that the
	 * second block was originally empty.
	 *
	 * If the insertion point moved to the 2nd block, we must adjust
	 * the index.  We must also track the entry just following the
	 * new entry for use in an "atomic rename" operation, that entry
	 * is always the "old" entry and the "new" entry is what we are
	 * inserting.  The index/blkno fields refer to the "old" entry,
	 * while the index2/blkno2 fields refer to the "new" entry.
	 */
	if (blk1->index > ichdr1.count) {
		ASSERT(state->inleaf == 0);
		blk2->index = blk1->index - ichdr1.count;
		args->index = args->index2 = blk2->index;
		args->blkno = args->blkno2 = blk2->blkno;
	} else if (blk1->index == ichdr1.count) {
		if (state->inleaf) {
			args->index = blk1->index;
			args->blkno = blk1->blkno;
			args->index2 = 0;
			args->blkno2 = blk2->blkno;
		} else {
			/*
			 * On a double leaf split, the original attr location
			 * is already stored in blkno2/index2, so don't
			 * overwrite it overwise we corrupt the tree.
			 */
			blk2->index = blk1->index - ichdr1.count;
			args->index = blk2->index;
			args->blkno = blk2->blkno;
			if (!state->extravalid) {
				/*
				 * set the new attr location to match the old
				 * one and let the higher level split code
				 * decide where in the leaf to place it.
				 */
				args->index2 = blk2->index;
				args->blkno2 = blk2->blkno;
			}
		}
	} else {
		ASSERT(state->inleaf == 1);
		args->index = args->index2 = blk1->index;
		args->blkno = args->blkno2 = blk1->blkno;
	}
}

/*
 * Examine entries until we reduce the absolute difference in
 * byte usage between the two blocks to a minimum.
 * GROT: Is this really necessary?  With other than a 512 byte blocksize,
 * GROT: there will always be enough room in either block for a new entry.
 * GROT: Do a double-split for this case?
 */
STATIC int
xfs_attr3_leaf_figure_balance(
	struct xfs_da_state		*state,
	struct xfs_da_state_blk		*blk1,
	struct xfs_attr3_icleaf_hdr	*ichdr1,
	struct xfs_da_state_blk		*blk2,
	struct xfs_attr3_icleaf_hdr	*ichdr2,
	int				*countarg,
	int				*usedbytesarg)
{
	struct xfs_attr_leafblock	*leaf1 = blk1->bp->b_addr;
	struct xfs_attr_leafblock	*leaf2 = blk2->bp->b_addr;
	struct xfs_attr_leaf_entry	*entry;
	int				count;
	int				max;
	int				index;
	int				totallen = 0;
	int				half;
	int				lastdelta;
	int				foundit = 0;
	int				tmp;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.
	 */
	max = ichdr1->count + ichdr2->count;
	half = (max + 1) * sizeof(*entry);
	half += ichdr1->usedbytes + ichdr2->usedbytes +
			xfs_attr_leaf_newentsize(state->args, NULL);
	half /= 2;
	lastdelta = state->args->geo->blksize;
	entry = xfs_attr3_leaf_entryp(leaf1);
	for (count = index = 0; count < max; entry++, index++, count++) {

#define XFS_ATTR_ABS(A)	(((A) < 0) ? -(A) : (A))
		/*
		 * The new entry is in the first block, account for it.
		 */
		if (count == blk1->index) {
			tmp = totallen + sizeof(*entry) +
				xfs_attr_leaf_newentsize(state->args, NULL);
			if (XFS_ATTR_ABS(half - tmp) > lastdelta)
				break;
			lastdelta = XFS_ATTR_ABS(half - tmp);
			totallen = tmp;
			foundit = 1;
		}

		/*
		 * Wrap around into the second block if necessary.
		 */
		if (count == ichdr1->count) {
			leaf1 = leaf2;
			entry = xfs_attr3_leaf_entryp(leaf1);
			index = 0;
		}

		/*
		 * Figure out if next leaf entry would be too much.
		 */
		tmp = totallen + sizeof(*entry) + xfs_attr_leaf_entsize(leaf1,
									index);
		if (XFS_ATTR_ABS(half - tmp) > lastdelta)
			break;
		lastdelta = XFS_ATTR_ABS(half - tmp);
		totallen = tmp;
#undef XFS_ATTR_ABS
	}

	/*
	 * Calculate the number of usedbytes that will end up in lower block.
	 * If new entry not in lower block, fix up the count.
	 */
	totallen -= count * sizeof(*entry);
	if (foundit) {
		totallen -= sizeof(*entry) +
				xfs_attr_leaf_newentsize(state->args, NULL);
	}

	*countarg = count;
	*usedbytesarg = totallen;
	return foundit;
}

/*========================================================================
 * Routines used for shrinking the Btree.
 *========================================================================*/

/*
 * Check a leaf block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  Always keep the block
 * with the smaller block number.
 * If the current block is over 50% full, don't try to join it, return 0.
 * If the block is empty, fill in the state structure and return 2.
 * If it can be collapsed, fill in the state structure and return 1.
 * If nothing can be done, return 0.
 *
 * GROT: allow for INCOMPLETE entries in calculation.
 */
int
xfs_attr3_leaf_toosmall(
	struct xfs_da_state	*state,
	int			*action)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_da_state_blk	*blk;
	struct xfs_attr3_icleaf_hdr ichdr;
	struct xfs_buf		*bp;
	xfs_dablk_t		blkno;
	int			bytes;
	int			forward;
	int			error;
	int			retval;
	int			i;

	trace_xfs_attr_leaf_toosmall(state->args);

	/*
	 * Check for the degenerate case of the block being over 50% full.
	 * If so, it's not worth even looking to see if we might be able
	 * to coalesce with a sibling.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	leaf = blk->bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(state->args->geo, &ichdr, leaf);
	bytes = xfs_attr3_leaf_hdr_size(leaf) +
		ichdr.count * sizeof(xfs_attr_leaf_entry_t) +
		ichdr.usedbytes;
	if (bytes > (state->args->geo->blksize >> 1)) {
		*action = 0;	/* blk over 50%, don't try to join */
		return 0;
	}

	/*
	 * Check for the degenerate case of the block being empty.
	 * If the block is empty, we'll simply delete it, no need to
	 * coalesce it with a sibling block.  We choose (arbitrarily)
	 * to merge with the forward block unless it is NULL.
	 */
	if (ichdr.count == 0) {
		/*
		 * Make altpath point to the block we want to keep and
		 * path point to the block we want to drop (this one).
		 */
		forward = (ichdr.forw != 0);
		memcpy(&state->altpath, &state->path, sizeof(state->path));
		error = xfs_da3_path_shift(state, &state->altpath, forward,
						 0, &retval);
		if (error)
			return error;
		if (retval) {
			*action = 0;
		} else {
			*action = 2;
		}
		return 0;
	}

	/*
	 * Examine each sibling block to see if we can coalesce with
	 * at least 25% free space to spare.  We need to figure out
	 * whether to merge with the forward or the backward block.
	 * We prefer coalescing with the lower numbered sibling so as
	 * to shrink an attribute list over time.
	 */
	/* start with smaller blk num */
	forward = ichdr.forw < ichdr.back;
	for (i = 0; i < 2; forward = !forward, i++) {
		struct xfs_attr3_icleaf_hdr ichdr2;
		if (forward)
			blkno = ichdr.forw;
		else
			blkno = ichdr.back;
		if (blkno == 0)
			continue;
		error = xfs_attr3_leaf_read(state->args->trans, state->args->dp,
					state->args->owner, blkno, &bp);
		if (error)
			return error;

		xfs_attr3_leaf_hdr_from_disk(state->args->geo, &ichdr2, bp->b_addr);

		bytes = state->args->geo->blksize -
			(state->args->geo->blksize >> 2) -
			ichdr.usedbytes - ichdr2.usedbytes -
			((ichdr.count + ichdr2.count) *
					sizeof(xfs_attr_leaf_entry_t)) -
			xfs_attr3_leaf_hdr_size(leaf);

		xfs_trans_brelse(state->args->trans, bp);
		if (bytes >= 0)
			break;	/* fits with at least 25% to spare */
	}
	if (i >= 2) {
		*action = 0;
		return 0;
	}

	/*
	 * Make altpath point to the block we want to keep (the lower
	 * numbered block) and path point to the block we want to drop.
	 */
	memcpy(&state->altpath, &state->path, sizeof(state->path));
	if (blkno < blk->blkno) {
		error = xfs_da3_path_shift(state, &state->altpath, forward,
						 0, &retval);
	} else {
		error = xfs_da3_path_shift(state, &state->path, forward,
						 0, &retval);
	}
	if (error)
		return error;
	if (retval) {
		*action = 0;
	} else {
		*action = 1;
	}
	return 0;
}

/*
 * Remove a name from the leaf attribute list structure.
 *
 * Return 1 if leaf is less than 37% full, 0 if >= 37% full.
 * If two leaves are 37% full, when combined they will leave 25% free.
 */
int
xfs_attr3_leaf_remove(
	struct xfs_buf		*bp,
	struct xfs_da_args	*args)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr3_icleaf_hdr ichdr;
	struct xfs_attr_leaf_entry *entry;
	int			before;
	int			after;
	int			smallest;
	int			entsize;
	int			tablesize;
	int			tmp;
	int			i;

	trace_xfs_attr_leaf_remove(args);

	leaf = bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr, leaf);

	ASSERT(ichdr.count > 0 && ichdr.count < args->geo->blksize / 8);
	ASSERT(args->index >= 0 && args->index < ichdr.count);
	ASSERT(ichdr.firstused >= ichdr.count * sizeof(*entry) +
					xfs_attr3_leaf_hdr_size(leaf));

	entry = &xfs_attr3_leaf_entryp(leaf)[args->index];

	ASSERT(be16_to_cpu(entry->nameidx) >= ichdr.firstused);
	ASSERT(be16_to_cpu(entry->nameidx) < args->geo->blksize);

	/*
	 * Scan through free region table:
	 *    check for adjacency of free'd entry with an existing one,
	 *    find smallest free region in case we need to replace it,
	 *    adjust any map that borders the entry table,
	 */
	tablesize = ichdr.count * sizeof(xfs_attr_leaf_entry_t)
					+ xfs_attr3_leaf_hdr_size(leaf);
	tmp = ichdr.freemap[0].size;
	before = after = -1;
	smallest = XFS_ATTR_LEAF_MAPSIZE - 1;
	entsize = xfs_attr_leaf_entsize(leaf, args->index);
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
		ASSERT(ichdr.freemap[i].base < args->geo->blksize);
		ASSERT(ichdr.freemap[i].size < args->geo->blksize);
		if (ichdr.freemap[i].base == tablesize) {
			ichdr.freemap[i].base -= sizeof(xfs_attr_leaf_entry_t);
			ichdr.freemap[i].size += sizeof(xfs_attr_leaf_entry_t);
		}

		if (ichdr.freemap[i].base + ichdr.freemap[i].size ==
				be16_to_cpu(entry->nameidx)) {
			before = i;
		} else if (ichdr.freemap[i].base ==
				(be16_to_cpu(entry->nameidx) + entsize)) {
			after = i;
		} else if (ichdr.freemap[i].size < tmp) {
			tmp = ichdr.freemap[i].size;
			smallest = i;
		}
	}

	/*
	 * Coalesce adjacent freemap regions,
	 * or replace the smallest region.
	 */
	if ((before >= 0) || (after >= 0)) {
		if ((before >= 0) && (after >= 0)) {
			ichdr.freemap[before].size += entsize;
			ichdr.freemap[before].size += ichdr.freemap[after].size;
			ichdr.freemap[after].base = 0;
			ichdr.freemap[after].size = 0;
		} else if (before >= 0) {
			ichdr.freemap[before].size += entsize;
		} else {
			ichdr.freemap[after].base = be16_to_cpu(entry->nameidx);
			ichdr.freemap[after].size += entsize;
		}
	} else {
		/*
		 * Replace smallest region (if it is smaller than free'd entry)
		 */
		if (ichdr.freemap[smallest].size < entsize) {
			ichdr.freemap[smallest].base = be16_to_cpu(entry->nameidx);
			ichdr.freemap[smallest].size = entsize;
		}
	}

	/*
	 * Did we remove the first entry?
	 */
	if (be16_to_cpu(entry->nameidx) == ichdr.firstused)
		smallest = 1;
	else
		smallest = 0;

	/*
	 * Compress the remaining entries and zero out the removed stuff.
	 */
	memset(xfs_attr3_leaf_name(leaf, args->index), 0, entsize);
	ichdr.usedbytes -= entsize;
	xfs_trans_log_buf(args->trans, bp,
	     XFS_DA_LOGRANGE(leaf, xfs_attr3_leaf_name(leaf, args->index),
				   entsize));

	tmp = (ichdr.count - args->index) * sizeof(xfs_attr_leaf_entry_t);
	memmove(entry, entry + 1, tmp);
	ichdr.count--;
	xfs_trans_log_buf(args->trans, bp,
	    XFS_DA_LOGRANGE(leaf, entry, tmp + sizeof(xfs_attr_leaf_entry_t)));

	entry = &xfs_attr3_leaf_entryp(leaf)[ichdr.count];
	memset(entry, 0, sizeof(xfs_attr_leaf_entry_t));

	/*
	 * If we removed the first entry, re-find the first used byte
	 * in the name area.  Note that if the entry was the "firstused",
	 * then we don't have a "hole" in our block resulting from
	 * removing the name.
	 */
	if (smallest) {
		tmp = args->geo->blksize;
		entry = xfs_attr3_leaf_entryp(leaf);
		for (i = ichdr.count - 1; i >= 0; entry++, i--) {
			ASSERT(be16_to_cpu(entry->nameidx) >= ichdr.firstused);
			ASSERT(be16_to_cpu(entry->nameidx) < args->geo->blksize);

			if (be16_to_cpu(entry->nameidx) < tmp)
				tmp = be16_to_cpu(entry->nameidx);
		}
		ichdr.firstused = tmp;
		ASSERT(ichdr.firstused != 0);
	} else {
		ichdr.holes = 1;	/* mark as needing compaction */
	}
	xfs_attr3_leaf_hdr_to_disk(args->geo, leaf, &ichdr);
	xfs_trans_log_buf(args->trans, bp,
			  XFS_DA_LOGRANGE(leaf, &leaf->hdr,
					  xfs_attr3_leaf_hdr_size(leaf)));

	/*
	 * Check if leaf is less than 50% full, caller may want to
	 * "join" the leaf with a sibling if so.
	 */
	tmp = ichdr.usedbytes + xfs_attr3_leaf_hdr_size(leaf) +
	      ichdr.count * sizeof(xfs_attr_leaf_entry_t);

	return tmp < args->geo->magicpct; /* leaf is < 37% full */
}

/*
 * Move all the attribute list entries from drop_leaf into save_leaf.
 */
void
xfs_attr3_leaf_unbalance(
	struct xfs_da_state	*state,
	struct xfs_da_state_blk	*drop_blk,
	struct xfs_da_state_blk	*save_blk)
{
	struct xfs_attr_leafblock *drop_leaf = drop_blk->bp->b_addr;
	struct xfs_attr_leafblock *save_leaf = save_blk->bp->b_addr;
	struct xfs_attr3_icleaf_hdr drophdr;
	struct xfs_attr3_icleaf_hdr savehdr;
	struct xfs_attr_leaf_entry *entry;

	trace_xfs_attr_leaf_unbalance(state->args);

	xfs_attr3_leaf_hdr_from_disk(state->args->geo, &drophdr, drop_leaf);
	xfs_attr3_leaf_hdr_from_disk(state->args->geo, &savehdr, save_leaf);
	entry = xfs_attr3_leaf_entryp(drop_leaf);

	/*
	 * Save last hashval from dying block for later Btree fixup.
	 */
	drop_blk->hashval = be32_to_cpu(entry[drophdr.count - 1].hashval);

	/*
	 * Check if we need a temp buffer, or can we do it in place.
	 * Note that we don't check "leaf" for holes because we will
	 * always be dropping it, toosmall() decided that for us already.
	 */
	if (savehdr.holes == 0) {
		/*
		 * dest leaf has no holes, so we add there.  May need
		 * to make some room in the entry array.
		 */
		if (xfs_attr3_leaf_order(save_blk->bp, &savehdr,
					 drop_blk->bp, &drophdr)) {
			xfs_attr3_leaf_moveents(state->args,
						drop_leaf, &drophdr, 0,
						save_leaf, &savehdr, 0,
						drophdr.count);
		} else {
			xfs_attr3_leaf_moveents(state->args,
						drop_leaf, &drophdr, 0,
						save_leaf, &savehdr,
						savehdr.count, drophdr.count);
		}
	} else {
		/*
		 * Destination has holes, so we make a temporary copy
		 * of the leaf and add them both to that.
		 */
		struct xfs_attr_leafblock *tmp_leaf;
		struct xfs_attr3_icleaf_hdr tmphdr;

		tmp_leaf = kvzalloc(state->args->geo->blksize,
				GFP_KERNEL | __GFP_NOFAIL);

		/*
		 * Copy the header into the temp leaf so that all the stuff
		 * not in the incore header is present and gets copied back in
		 * once we've moved all the entries.
		 */
		memcpy(tmp_leaf, save_leaf, xfs_attr3_leaf_hdr_size(save_leaf));

		memset(&tmphdr, 0, sizeof(tmphdr));
		tmphdr.magic = savehdr.magic;
		tmphdr.forw = savehdr.forw;
		tmphdr.back = savehdr.back;
		tmphdr.firstused = state->args->geo->blksize;

		/* write the header to the temp buffer to initialise it */
		xfs_attr3_leaf_hdr_to_disk(state->args->geo, tmp_leaf, &tmphdr);

		if (xfs_attr3_leaf_order(save_blk->bp, &savehdr,
					 drop_blk->bp, &drophdr)) {
			xfs_attr3_leaf_moveents(state->args,
						drop_leaf, &drophdr, 0,
						tmp_leaf, &tmphdr, 0,
						drophdr.count);
			xfs_attr3_leaf_moveents(state->args,
						save_leaf, &savehdr, 0,
						tmp_leaf, &tmphdr, tmphdr.count,
						savehdr.count);
		} else {
			xfs_attr3_leaf_moveents(state->args,
						save_leaf, &savehdr, 0,
						tmp_leaf, &tmphdr, 0,
						savehdr.count);
			xfs_attr3_leaf_moveents(state->args,
						drop_leaf, &drophdr, 0,
						tmp_leaf, &tmphdr, tmphdr.count,
						drophdr.count);
		}
		memcpy(save_leaf, tmp_leaf, state->args->geo->blksize);
		savehdr = tmphdr; /* struct copy */
		kvfree(tmp_leaf);
	}

	xfs_attr3_leaf_hdr_to_disk(state->args->geo, save_leaf, &savehdr);
	xfs_trans_log_buf(state->args->trans, save_blk->bp, 0,
					   state->args->geo->blksize - 1);

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	entry = xfs_attr3_leaf_entryp(save_leaf);
	save_blk->hashval = be32_to_cpu(entry[savehdr.count - 1].hashval);
}

/*========================================================================
 * Routines used for finding things in the Btree.
 *========================================================================*/

/*
 * Look up a name in a leaf attribute list structure.
 * This is the internal routine, it uses the caller's buffer.
 *
 * Note that duplicate keys are allowed, but only check within the
 * current leaf node.  The Btree code must check in adjacent leaf nodes.
 *
 * Return in args->index the index into the entry[] array of either
 * the found entry, or where the entry should have been (insert before
 * that entry).
 *
 * Don't change the args->value unless we find the attribute.
 */
int
xfs_attr3_leaf_lookup_int(
	struct xfs_buf		*bp,
	struct xfs_da_args	*args)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr3_icleaf_hdr ichdr;
	struct xfs_attr_leaf_entry *entry;
	struct xfs_attr_leaf_entry *entries;
	struct xfs_attr_leaf_name_local *name_loc;
	struct xfs_attr_leaf_name_remote *name_rmt;
	xfs_dahash_t		hashval;
	int			probe;
	int			span;

	trace_xfs_attr_leaf_lookup(args);

	leaf = bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr, leaf);
	entries = xfs_attr3_leaf_entryp(leaf);
	if (ichdr.count >= args->geo->blksize / 8) {
		xfs_buf_mark_corrupt(bp);
		xfs_da_mark_sick(args);
		return -EFSCORRUPTED;
	}

	/*
	 * Binary search.  (note: small blocks will skip this loop)
	 */
	hashval = args->hashval;
	probe = span = ichdr.count / 2;
	for (entry = &entries[probe]; span > 4; entry = &entries[probe]) {
		span /= 2;
		if (be32_to_cpu(entry->hashval) < hashval)
			probe += span;
		else if (be32_to_cpu(entry->hashval) > hashval)
			probe -= span;
		else
			break;
	}
	if (!(probe >= 0 && (!ichdr.count || probe < ichdr.count))) {
		xfs_buf_mark_corrupt(bp);
		xfs_da_mark_sick(args);
		return -EFSCORRUPTED;
	}
	if (!(span <= 4 || be32_to_cpu(entry->hashval) == hashval)) {
		xfs_buf_mark_corrupt(bp);
		xfs_da_mark_sick(args);
		return -EFSCORRUPTED;
	}

	/*
	 * Since we may have duplicate hashval's, find the first matching
	 * hashval in the leaf.
	 */
	while (probe > 0 && be32_to_cpu(entry->hashval) >= hashval) {
		entry--;
		probe--;
	}
	while (probe < ichdr.count &&
	       be32_to_cpu(entry->hashval) < hashval) {
		entry++;
		probe++;
	}
	if (probe == ichdr.count || be32_to_cpu(entry->hashval) != hashval) {
		args->index = probe;
		return -ENOATTR;
	}

	/*
	 * Duplicate keys may be present, so search all of them for a match.
	 */
	for (; probe < ichdr.count && (be32_to_cpu(entry->hashval) == hashval);
			entry++, probe++) {
/*
 * GROT: Add code to remove incomplete entries.
 */
		if (entry->flags & XFS_ATTR_LOCAL) {
			name_loc = xfs_attr3_leaf_name_local(leaf, probe);
			if (!xfs_attr_match(args, entry->flags,
					name_loc->nameval, name_loc->namelen,
					&name_loc->nameval[name_loc->namelen],
					be16_to_cpu(name_loc->valuelen)))
				continue;
			args->index = probe;
			return -EEXIST;
		} else {
			unsigned int	valuelen;

			name_rmt = xfs_attr3_leaf_name_remote(leaf, probe);
			valuelen = be32_to_cpu(name_rmt->valuelen);
			if (!xfs_attr_match(args, entry->flags, name_rmt->name,
					name_rmt->namelen, NULL, valuelen))
				continue;
			args->index = probe;
			args->rmtvaluelen = valuelen;
			args->rmtblkno = be32_to_cpu(name_rmt->valueblk);
			args->rmtblkcnt = xfs_attr3_rmt_blocks(
							args->dp->i_mount,
							args->rmtvaluelen);
			return -EEXIST;
		}
	}
	args->index = probe;
	return -ENOATTR;
}

/*
 * Get the value associated with an attribute name from a leaf attribute
 * list structure.
 *
 * If args->valuelen is zero, only the length needs to be returned.  Unlike a
 * lookup, we only return an error if the attribute does not exist or we can't
 * retrieve the value.
 */
int
xfs_attr3_leaf_getvalue(
	struct xfs_buf		*bp,
	struct xfs_da_args	*args)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr3_icleaf_hdr ichdr;
	struct xfs_attr_leaf_entry *entry;
	struct xfs_attr_leaf_name_local *name_loc;
	struct xfs_attr_leaf_name_remote *name_rmt;

	leaf = bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr, leaf);
	ASSERT(ichdr.count < args->geo->blksize / 8);
	ASSERT(args->index < ichdr.count);

	entry = &xfs_attr3_leaf_entryp(leaf)[args->index];
	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = xfs_attr3_leaf_name_local(leaf, args->index);
		ASSERT(name_loc->namelen == args->namelen);
		ASSERT(memcmp(args->name, name_loc->nameval, args->namelen) == 0);
		return xfs_attr_copy_value(args,
					&name_loc->nameval[args->namelen],
					be16_to_cpu(name_loc->valuelen));
	}

	name_rmt = xfs_attr3_leaf_name_remote(leaf, args->index);
	ASSERT(name_rmt->namelen == args->namelen);
	ASSERT(memcmp(args->name, name_rmt->name, args->namelen) == 0);
	args->rmtvaluelen = be32_to_cpu(name_rmt->valuelen);
	args->rmtblkno = be32_to_cpu(name_rmt->valueblk);
	args->rmtblkcnt = xfs_attr3_rmt_blocks(args->dp->i_mount,
					       args->rmtvaluelen);
	return xfs_attr_copy_value(args, NULL, args->rmtvaluelen);
}

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Move the indicated entries from one leaf to another.
 * NOTE: this routine modifies both source and destination leaves.
 */
/*ARGSUSED*/
STATIC void
xfs_attr3_leaf_moveents(
	struct xfs_da_args		*args,
	struct xfs_attr_leafblock	*leaf_s,
	struct xfs_attr3_icleaf_hdr	*ichdr_s,
	int				start_s,
	struct xfs_attr_leafblock	*leaf_d,
	struct xfs_attr3_icleaf_hdr	*ichdr_d,
	int				start_d,
	int				count)
{
	struct xfs_attr_leaf_entry	*entry_s;
	struct xfs_attr_leaf_entry	*entry_d;
	int				desti;
	int				tmp;
	int				i;

	/*
	 * Check for nothing to do.
	 */
	if (count == 0)
		return;

	/*
	 * Set up environment.
	 */
	ASSERT(ichdr_s->magic == XFS_ATTR_LEAF_MAGIC ||
	       ichdr_s->magic == XFS_ATTR3_LEAF_MAGIC);
	ASSERT(ichdr_s->magic == ichdr_d->magic);
	ASSERT(ichdr_s->count > 0 && ichdr_s->count < args->geo->blksize / 8);
	ASSERT(ichdr_s->firstused >= (ichdr_s->count * sizeof(*entry_s))
					+ xfs_attr3_leaf_hdr_size(leaf_s));
	ASSERT(ichdr_d->count < args->geo->blksize / 8);
	ASSERT(ichdr_d->firstused >= (ichdr_d->count * sizeof(*entry_d))
					+ xfs_attr3_leaf_hdr_size(leaf_d));

	ASSERT(start_s < ichdr_s->count);
	ASSERT(start_d <= ichdr_d->count);
	ASSERT(count <= ichdr_s->count);


	/*
	 * Move the entries in the destination leaf up to make a hole?
	 */
	if (start_d < ichdr_d->count) {
		tmp  = ichdr_d->count - start_d;
		tmp *= sizeof(xfs_attr_leaf_entry_t);
		entry_s = &xfs_attr3_leaf_entryp(leaf_d)[start_d];
		entry_d = &xfs_attr3_leaf_entryp(leaf_d)[start_d + count];
		memmove(entry_d, entry_s, tmp);
	}

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate attribute info packed and in sequence.
	 */
	entry_s = &xfs_attr3_leaf_entryp(leaf_s)[start_s];
	entry_d = &xfs_attr3_leaf_entryp(leaf_d)[start_d];
	desti = start_d;
	for (i = 0; i < count; entry_s++, entry_d++, desti++, i++) {
		ASSERT(be16_to_cpu(entry_s->nameidx) >= ichdr_s->firstused);
		tmp = xfs_attr_leaf_entsize(leaf_s, start_s + i);
#ifdef GROT
		/*
		 * Code to drop INCOMPLETE entries.  Difficult to use as we
		 * may also need to change the insertion index.  Code turned
		 * off for 6.2, should be revisited later.
		 */
		if (entry_s->flags & XFS_ATTR_INCOMPLETE) { /* skip partials? */
			memset(xfs_attr3_leaf_name(leaf_s, start_s + i), 0, tmp);
			ichdr_s->usedbytes -= tmp;
			ichdr_s->count -= 1;
			entry_d--;	/* to compensate for ++ in loop hdr */
			desti--;
			if ((start_s + i) < offset)
				result++;	/* insertion index adjustment */
		} else {
#endif /* GROT */
			ichdr_d->firstused -= tmp;
			/* both on-disk, don't endian flip twice */
			entry_d->hashval = entry_s->hashval;
			entry_d->nameidx = cpu_to_be16(ichdr_d->firstused);
			entry_d->flags = entry_s->flags;
			ASSERT(be16_to_cpu(entry_d->nameidx) + tmp
							<= args->geo->blksize);
			memmove(xfs_attr3_leaf_name(leaf_d, desti),
				xfs_attr3_leaf_name(leaf_s, start_s + i), tmp);
			ASSERT(be16_to_cpu(entry_s->nameidx) + tmp
							<= args->geo->blksize);
			memset(xfs_attr3_leaf_name(leaf_s, start_s + i), 0, tmp);
			ichdr_s->usedbytes -= tmp;
			ichdr_d->usedbytes += tmp;
			ichdr_s->count -= 1;
			ichdr_d->count += 1;
			tmp = ichdr_d->count * sizeof(xfs_attr_leaf_entry_t)
					+ xfs_attr3_leaf_hdr_size(leaf_d);
			ASSERT(ichdr_d->firstused >= tmp);
#ifdef GROT
		}
#endif /* GROT */
	}

	/*
	 * Zero out the entries we just copied.
	 */
	if (start_s == ichdr_s->count) {
		tmp = count * sizeof(xfs_attr_leaf_entry_t);
		entry_s = &xfs_attr3_leaf_entryp(leaf_s)[start_s];
		ASSERT(((char *)entry_s + tmp) <=
		       ((char *)leaf_s + args->geo->blksize));
		memset(entry_s, 0, tmp);
	} else {
		/*
		 * Move the remaining entries down to fill the hole,
		 * then zero the entries at the top.
		 */
		tmp  = (ichdr_s->count - count) * sizeof(xfs_attr_leaf_entry_t);
		entry_s = &xfs_attr3_leaf_entryp(leaf_s)[start_s + count];
		entry_d = &xfs_attr3_leaf_entryp(leaf_s)[start_s];
		memmove(entry_d, entry_s, tmp);

		tmp = count * sizeof(xfs_attr_leaf_entry_t);
		entry_s = &xfs_attr3_leaf_entryp(leaf_s)[ichdr_s->count];
		ASSERT(((char *)entry_s + tmp) <=
		       ((char *)leaf_s + args->geo->blksize));
		memset(entry_s, 0, tmp);
	}

	/*
	 * Fill in the freemap information
	 */
	ichdr_d->freemap[0].base = xfs_attr3_leaf_hdr_size(leaf_d);
	ichdr_d->freemap[0].base += ichdr_d->count * sizeof(xfs_attr_leaf_entry_t);
	ichdr_d->freemap[0].size = ichdr_d->firstused - ichdr_d->freemap[0].base;
	ichdr_d->freemap[1].base = 0;
	ichdr_d->freemap[2].base = 0;
	ichdr_d->freemap[1].size = 0;
	ichdr_d->freemap[2].size = 0;
	ichdr_s->holes = 1;	/* leaf may not be compact */
}

/*
 * Pick up the last hashvalue from a leaf block.
 */
xfs_dahash_t
xfs_attr_leaf_lasthash(
	struct xfs_buf	*bp,
	int		*count)
{
	struct xfs_attr3_icleaf_hdr ichdr;
	struct xfs_attr_leaf_entry *entries;
	struct xfs_mount *mp = bp->b_mount;

	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &ichdr, bp->b_addr);
	entries = xfs_attr3_leaf_entryp(bp->b_addr);
	if (count)
		*count = ichdr.count;
	if (!ichdr.count)
		return 0;
	return be32_to_cpu(entries[ichdr.count - 1].hashval);
}

/*
 * Calculate the number of bytes used to store the indicated attribute
 * (whether local or remote only calculate bytes in this block).
 */
STATIC int
xfs_attr_leaf_entsize(xfs_attr_leafblock_t *leaf, int index)
{
	struct xfs_attr_leaf_entry *entries;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	int size;

	entries = xfs_attr3_leaf_entryp(leaf);
	if (entries[index].flags & XFS_ATTR_LOCAL) {
		name_loc = xfs_attr3_leaf_name_local(leaf, index);
		size = xfs_attr_leaf_entsize_local(name_loc->namelen,
						   be16_to_cpu(name_loc->valuelen));
	} else {
		name_rmt = xfs_attr3_leaf_name_remote(leaf, index);
		size = xfs_attr_leaf_entsize_remote(name_rmt->namelen);
	}
	return size;
}

/*
 * Calculate the number of bytes that would be required to store the new
 * attribute (whether local or remote only calculate bytes in this block).
 * This routine decides as a side effect whether the attribute will be
 * a "local" or a "remote" attribute.
 */
int
xfs_attr_leaf_newentsize(
	struct xfs_da_args	*args,
	int			*local)
{
	int			size;

	size = xfs_attr_leaf_entsize_local(args->namelen, args->valuelen);
	if (size < xfs_attr_leaf_entsize_local_max(args->geo->blksize)) {
		if (local)
			*local = 1;
		return size;
	}
	if (local)
		*local = 0;
	return xfs_attr_leaf_entsize_remote(args->namelen);
}


/*========================================================================
 * Manage the INCOMPLETE flag in a leaf entry
 *========================================================================*/

/*
 * Clear the INCOMPLETE flag on an entry in a leaf block.
 */
int
xfs_attr3_leaf_clearflag(
	struct xfs_da_args	*args)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr_leaf_entry *entry;
	struct xfs_attr_leaf_name_remote *name_rmt;
	struct xfs_buf		*bp;
	int			error;
#ifdef DEBUG
	struct xfs_attr3_icleaf_hdr ichdr;
	xfs_attr_leaf_name_local_t *name_loc;
	int namelen;
	char *name;
#endif /* DEBUG */

	trace_xfs_attr_leaf_clearflag(args);
	/*
	 * Set up the operation.
	 */
	error = xfs_attr3_leaf_read(args->trans, args->dp, args->owner,
			args->blkno, &bp);
	if (error)
		return error;

	leaf = bp->b_addr;
	entry = &xfs_attr3_leaf_entryp(leaf)[args->index];
	ASSERT(entry->flags & XFS_ATTR_INCOMPLETE);

#ifdef DEBUG
	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr, leaf);
	ASSERT(args->index < ichdr.count);
	ASSERT(args->index >= 0);

	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = xfs_attr3_leaf_name_local(leaf, args->index);
		namelen = name_loc->namelen;
		name = (char *)name_loc->nameval;
	} else {
		name_rmt = xfs_attr3_leaf_name_remote(leaf, args->index);
		namelen = name_rmt->namelen;
		name = (char *)name_rmt->name;
	}
	ASSERT(be32_to_cpu(entry->hashval) == args->hashval);
	ASSERT(namelen == args->namelen);
	ASSERT(memcmp(name, args->name, namelen) == 0);
#endif /* DEBUG */

	entry->flags &= ~XFS_ATTR_INCOMPLETE;
	xfs_trans_log_buf(args->trans, bp,
			 XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));

	if (args->rmtblkno) {
		ASSERT((entry->flags & XFS_ATTR_LOCAL) == 0);
		name_rmt = xfs_attr3_leaf_name_remote(leaf, args->index);
		name_rmt->valueblk = cpu_to_be32(args->rmtblkno);
		name_rmt->valuelen = cpu_to_be32(args->rmtvaluelen);
		xfs_trans_log_buf(args->trans, bp,
			 XFS_DA_LOGRANGE(leaf, name_rmt, sizeof(*name_rmt)));
	}

	return 0;
}

/*
 * Set the INCOMPLETE flag on an entry in a leaf block.
 */
int
xfs_attr3_leaf_setflag(
	struct xfs_da_args	*args)
{
	struct xfs_attr_leafblock *leaf;
	struct xfs_attr_leaf_entry *entry;
	struct xfs_attr_leaf_name_remote *name_rmt;
	struct xfs_buf		*bp;
	int error;
#ifdef DEBUG
	struct xfs_attr3_icleaf_hdr ichdr;
#endif

	trace_xfs_attr_leaf_setflag(args);

	/*
	 * Set up the operation.
	 */
	error = xfs_attr3_leaf_read(args->trans, args->dp, args->owner,
			args->blkno, &bp);
	if (error)
		return error;

	leaf = bp->b_addr;
#ifdef DEBUG
	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr, leaf);
	ASSERT(args->index < ichdr.count);
	ASSERT(args->index >= 0);
#endif
	entry = &xfs_attr3_leaf_entryp(leaf)[args->index];

	ASSERT((entry->flags & XFS_ATTR_INCOMPLETE) == 0);
	entry->flags |= XFS_ATTR_INCOMPLETE;
	xfs_trans_log_buf(args->trans, bp,
			XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));
	if ((entry->flags & XFS_ATTR_LOCAL) == 0) {
		name_rmt = xfs_attr3_leaf_name_remote(leaf, args->index);
		name_rmt->valueblk = 0;
		name_rmt->valuelen = 0;
		xfs_trans_log_buf(args->trans, bp,
			 XFS_DA_LOGRANGE(leaf, name_rmt, sizeof(*name_rmt)));
	}

	return 0;
}

/*
 * In a single transaction, clear the INCOMPLETE flag on the leaf entry
 * given by args->blkno/index and set the INCOMPLETE flag on the leaf
 * entry given by args->blkno2/index2.
 *
 * Note that they could be in different blocks, or in the same block.
 */
int
xfs_attr3_leaf_flipflags(
	struct xfs_da_args	*args)
{
	struct xfs_attr_leafblock *leaf1;
	struct xfs_attr_leafblock *leaf2;
	struct xfs_attr_leaf_entry *entry1;
	struct xfs_attr_leaf_entry *entry2;
	struct xfs_attr_leaf_name_remote *name_rmt;
	struct xfs_buf		*bp1;
	struct xfs_buf		*bp2;
	int error;
#ifdef DEBUG
	struct xfs_attr3_icleaf_hdr ichdr1;
	struct xfs_attr3_icleaf_hdr ichdr2;
	xfs_attr_leaf_name_local_t *name_loc;
	int namelen1, namelen2;
	char *name1, *name2;
#endif /* DEBUG */

	trace_xfs_attr_leaf_flipflags(args);

	/*
	 * Read the block containing the "old" attr
	 */
	error = xfs_attr3_leaf_read(args->trans, args->dp, args->owner,
			args->blkno, &bp1);
	if (error)
		return error;

	/*
	 * Read the block containing the "new" attr, if it is different
	 */
	if (args->blkno2 != args->blkno) {
		error = xfs_attr3_leaf_read(args->trans, args->dp, args->owner,
				args->blkno2, &bp2);
		if (error)
			return error;
	} else {
		bp2 = bp1;
	}

	leaf1 = bp1->b_addr;
	entry1 = &xfs_attr3_leaf_entryp(leaf1)[args->index];

	leaf2 = bp2->b_addr;
	entry2 = &xfs_attr3_leaf_entryp(leaf2)[args->index2];

#ifdef DEBUG
	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr1, leaf1);
	ASSERT(args->index < ichdr1.count);
	ASSERT(args->index >= 0);

	xfs_attr3_leaf_hdr_from_disk(args->geo, &ichdr2, leaf2);
	ASSERT(args->index2 < ichdr2.count);
	ASSERT(args->index2 >= 0);

	if (entry1->flags & XFS_ATTR_LOCAL) {
		name_loc = xfs_attr3_leaf_name_local(leaf1, args->index);
		namelen1 = name_loc->namelen;
		name1 = (char *)name_loc->nameval;
	} else {
		name_rmt = xfs_attr3_leaf_name_remote(leaf1, args->index);
		namelen1 = name_rmt->namelen;
		name1 = (char *)name_rmt->name;
	}
	if (entry2->flags & XFS_ATTR_LOCAL) {
		name_loc = xfs_attr3_leaf_name_local(leaf2, args->index2);
		namelen2 = name_loc->namelen;
		name2 = (char *)name_loc->nameval;
	} else {
		name_rmt = xfs_attr3_leaf_name_remote(leaf2, args->index2);
		namelen2 = name_rmt->namelen;
		name2 = (char *)name_rmt->name;
	}
	ASSERT(be32_to_cpu(entry1->hashval) == be32_to_cpu(entry2->hashval));
	ASSERT(namelen1 == namelen2);
	ASSERT(memcmp(name1, name2, namelen1) == 0);
#endif /* DEBUG */

	ASSERT(entry1->flags & XFS_ATTR_INCOMPLETE);
	ASSERT((entry2->flags & XFS_ATTR_INCOMPLETE) == 0);

	entry1->flags &= ~XFS_ATTR_INCOMPLETE;
	xfs_trans_log_buf(args->trans, bp1,
			  XFS_DA_LOGRANGE(leaf1, entry1, sizeof(*entry1)));
	if (args->rmtblkno) {
		ASSERT((entry1->flags & XFS_ATTR_LOCAL) == 0);
		name_rmt = xfs_attr3_leaf_name_remote(leaf1, args->index);
		name_rmt->valueblk = cpu_to_be32(args->rmtblkno);
		name_rmt->valuelen = cpu_to_be32(args->rmtvaluelen);
		xfs_trans_log_buf(args->trans, bp1,
			 XFS_DA_LOGRANGE(leaf1, name_rmt, sizeof(*name_rmt)));
	}

	entry2->flags |= XFS_ATTR_INCOMPLETE;
	xfs_trans_log_buf(args->trans, bp2,
			  XFS_DA_LOGRANGE(leaf2, entry2, sizeof(*entry2)));
	if ((entry2->flags & XFS_ATTR_LOCAL) == 0) {
		name_rmt = xfs_attr3_leaf_name_remote(leaf2, args->index2);
		name_rmt->valueblk = 0;
		name_rmt->valuelen = 0;
		xfs_trans_log_buf(args->trans, bp2,
			 XFS_DA_LOGRANGE(leaf2, name_rmt, sizeof(*name_rmt)));
	}

	return 0;
}
