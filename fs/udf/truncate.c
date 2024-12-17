// SPDX-License-Identifier: GPL-2.0-only
/*
 * truncate.c
 *
 * PURPOSE
 *	Truncate handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *  (C) 1999-2004 Ben Fennema
 *  (C) 1999 Stelias Computing Inc
 *
 * HISTORY
 *
 *  02/24/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <linux/fs.h>
#include <linux/mm.h>

#include "udf_i.h"
#include "udf_sb.h"

static void extent_trunc(struct inode *inode, struct extent_position *epos,
			 struct kernel_lb_addr *eloc, int8_t etype, uint32_t elen,
			 uint32_t nelen)
{
	struct kernel_lb_addr neloc = {};
	int last_block = (elen + inode->i_sb->s_blocksize - 1) >>
		inode->i_sb->s_blocksize_bits;
	int first_block = (nelen + inode->i_sb->s_blocksize - 1) >>
		inode->i_sb->s_blocksize_bits;

	if (nelen) {
		if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30)) {
			udf_free_blocks(inode->i_sb, inode, eloc, 0,
					last_block);
			etype = (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30);
		} else
			neloc = *eloc;
		nelen = (etype << 30) | nelen;
	}

	if (elen != nelen) {
		udf_write_aext(inode, epos, &neloc, nelen, 0);
		if (last_block > first_block) {
			if (etype == (EXT_RECORDED_ALLOCATED >> 30))
				mark_inode_dirty(inode);

			if (etype != (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
				udf_free_blocks(inode->i_sb, inode, eloc,
						first_block,
						last_block - first_block);
		}
	}
}

/*
 * Truncate the last extent to match i_size. This function assumes
 * that preallocation extent is already truncated.
 */
void udf_truncate_tail_extent(struct inode *inode)
{
	struct extent_position epos = {};
	struct kernel_lb_addr eloc;
	uint32_t elen, nelen;
	uint64_t lbcount = 0;
	int8_t etype = -1, netype;
	int adsize;
	struct udf_inode_info *iinfo = UDF_I(inode);
	int ret;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB ||
	    inode->i_size == iinfo->i_lenExtents)
		return;
	/* Are we going to delete the file anyway? */
	if (inode->i_nlink == 0)
		return;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		BUG();

	/* Find the last extent in the file */
	while (1) {
		ret = udf_next_aext(inode, &epos, &eloc, &elen, &netype, 1);
		if (ret <= 0)
			break;
		etype = netype;
		lbcount += elen;
		if (lbcount > inode->i_size) {
			if (lbcount - inode->i_size >= inode->i_sb->s_blocksize)
				udf_warn(inode->i_sb,
					 "Too long extent after EOF in inode %u: i_size: %lld lbcount: %lld extent %u+%u\n",
					 (unsigned)inode->i_ino,
					 (long long)inode->i_size,
					 (long long)lbcount,
					 (unsigned)eloc.logicalBlockNum,
					 (unsigned)elen);
			nelen = elen - (lbcount - inode->i_size);
			epos.offset -= adsize;
			extent_trunc(inode, &epos, &eloc, etype, elen, nelen);
			epos.offset += adsize;
			if (udf_next_aext(inode, &epos, &eloc, &elen,
					  &netype, 1) > 0)
				udf_err(inode->i_sb,
					"Extent after EOF in inode %u\n",
					(unsigned)inode->i_ino);
			break;
		}
	}
	/* This inode entry is in-memory only and thus we don't have to mark
	 * the inode dirty */
	if (ret == 0)
		iinfo->i_lenExtents = inode->i_size;
	brelse(epos.bh);
}

void udf_discard_prealloc(struct inode *inode)
{
	struct extent_position epos = {};
	struct extent_position prev_epos = {};
	struct kernel_lb_addr eloc;
	uint32_t elen;
	uint64_t lbcount = 0;
	int8_t etype = -1;
	struct udf_inode_info *iinfo = UDF_I(inode);
	int bsize = i_blocksize(inode);
	int8_t tmpetype = -1;
	int ret;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB ||
	    ALIGN(inode->i_size, bsize) == ALIGN(iinfo->i_lenExtents, bsize))
		return;

	epos.block = iinfo->i_location;

	/* Find the last extent in the file */
	while (1) {
		ret = udf_next_aext(inode, &epos, &eloc, &elen, &tmpetype, 0);
		if (ret < 0)
			goto out;
		if (ret == 0)
			break;
		brelse(prev_epos.bh);
		prev_epos = epos;
		if (prev_epos.bh)
			get_bh(prev_epos.bh);

		ret = udf_next_aext(inode, &epos, &eloc, &elen, &etype, 1);
		if (ret < 0)
			goto out;
		lbcount += elen;
	}

	if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30)) {
		lbcount -= elen;
		udf_delete_aext(inode, prev_epos);
		udf_free_blocks(inode->i_sb, inode, &eloc, 0,
				DIV_ROUND_UP(elen, bsize));
	}
	/* This inode entry is in-memory only and thus we don't have to mark
	 * the inode dirty */
	iinfo->i_lenExtents = lbcount;
out:
	brelse(epos.bh);
	brelse(prev_epos.bh);
}

static void udf_update_alloc_ext_desc(struct inode *inode,
				      struct extent_position *epos,
				      u32 lenalloc)
{
	struct super_block *sb = inode->i_sb;
	struct udf_sb_info *sbi = UDF_SB(sb);

	struct allocExtDesc *aed = (struct allocExtDesc *) (epos->bh->b_data);
	int len = sizeof(struct allocExtDesc);

	aed->lengthAllocDescs =	cpu_to_le32(lenalloc);
	if (!UDF_QUERY_FLAG(sb, UDF_FLAG_STRICT) || sbi->s_udfrev >= 0x0201)
		len += lenalloc;

	udf_update_tag(epos->bh->b_data, len);
	mark_buffer_dirty_inode(epos->bh, inode);
}

/*
 * Truncate extents of inode to inode->i_size. This function can be used only
 * for making file shorter. For making file longer, udf_extend_file() has to
 * be used.
 */
int udf_truncate_extents(struct inode *inode)
{
	struct extent_position epos;
	struct kernel_lb_addr eloc, neloc = {};
	uint32_t elen, nelen = 0, indirect_ext_len = 0, lenalloc;
	int8_t etype;
	struct super_block *sb = inode->i_sb;
	sector_t first_block = inode->i_size >> sb->s_blocksize_bits, offset;
	loff_t byte_offset;
	int adsize;
	struct udf_inode_info *iinfo = UDF_I(inode);
	int ret = 0;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		BUG();

	ret = inode_bmap(inode, first_block, &epos, &eloc, &elen, &offset, &etype);
	if (ret < 0)
		return ret;
	byte_offset = (offset << sb->s_blocksize_bits) +
		(inode->i_size & (sb->s_blocksize - 1));
	if (ret == 0) {
		/* We should extend the file? */
		WARN_ON(byte_offset);
		return 0;
	}
	epos.offset -= adsize;
	extent_trunc(inode, &epos, &eloc, etype, elen, byte_offset);
	epos.offset += adsize;
	if (byte_offset)
		lenalloc = epos.offset;
	else
		lenalloc = epos.offset - adsize;

	if (!epos.bh)
		lenalloc -= udf_file_entry_alloc_offset(inode);
	else
		lenalloc -= sizeof(struct allocExtDesc);

	while ((ret = udf_current_aext(inode, &epos, &eloc,
				       &elen, &etype, 0)) > 0) {
		if (etype == (EXT_NEXT_EXTENT_ALLOCDESCS >> 30)) {
			udf_write_aext(inode, &epos, &neloc, nelen, 0);
			if (indirect_ext_len) {
				/* We managed to free all extents in the
				 * indirect extent - free it too */
				BUG_ON(!epos.bh);
				udf_free_blocks(sb, NULL, &epos.block,
						0, indirect_ext_len);
			} else if (!epos.bh) {
				iinfo->i_lenAlloc = lenalloc;
				mark_inode_dirty(inode);
			} else
				udf_update_alloc_ext_desc(inode,
						&epos, lenalloc);
			brelse(epos.bh);
			epos.offset = sizeof(struct allocExtDesc);
			epos.block = eloc;
			epos.bh = sb_bread(sb,
					udf_get_lb_pblock(sb, &eloc, 0));
			/* Error reading indirect block? */
			if (!epos.bh)
				return -EIO;
			if (elen)
				indirect_ext_len =
					(elen + sb->s_blocksize - 1) >>
					sb->s_blocksize_bits;
			else
				indirect_ext_len = 1;
		} else {
			extent_trunc(inode, &epos, &eloc, etype, elen, 0);
			epos.offset += adsize;
		}
	}

	if (ret < 0) {
		brelse(epos.bh);
		return ret;
	}

	if (indirect_ext_len) {
		BUG_ON(!epos.bh);
		udf_free_blocks(sb, NULL, &epos.block, 0, indirect_ext_len);
	} else if (!epos.bh) {
		iinfo->i_lenAlloc = lenalloc;
		mark_inode_dirty(inode);
	} else
		udf_update_alloc_ext_desc(inode, &epos, lenalloc);
	iinfo->i_lenExtents = inode->i_size;

	brelse(epos.bh);
	return 0;
}
