/*
 * truncate.c
 *
 * PURPOSE
 *	Truncate handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
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

static void extent_trunc(struct iyesde *iyesde, struct extent_position *epos,
			 struct kernel_lb_addr *eloc, int8_t etype, uint32_t elen,
			 uint32_t nelen)
{
	struct kernel_lb_addr neloc = {};
	int last_block = (elen + iyesde->i_sb->s_blocksize - 1) >>
		iyesde->i_sb->s_blocksize_bits;
	int first_block = (nelen + iyesde->i_sb->s_blocksize - 1) >>
		iyesde->i_sb->s_blocksize_bits;

	if (nelen) {
		if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30)) {
			udf_free_blocks(iyesde->i_sb, iyesde, eloc, 0,
					last_block);
			etype = (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30);
		} else
			neloc = *eloc;
		nelen = (etype << 30) | nelen;
	}

	if (elen != nelen) {
		udf_write_aext(iyesde, epos, &neloc, nelen, 0);
		if (last_block > first_block) {
			if (etype == (EXT_RECORDED_ALLOCATED >> 30))
				mark_iyesde_dirty(iyesde);

			if (etype != (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
				udf_free_blocks(iyesde->i_sb, iyesde, eloc,
						first_block,
						last_block - first_block);
		}
	}
}

/*
 * Truncate the last extent to match i_size. This function assumes
 * that preallocation extent is already truncated.
 */
void udf_truncate_tail_extent(struct iyesde *iyesde)
{
	struct extent_position epos = {};
	struct kernel_lb_addr eloc;
	uint32_t elen, nelen;
	uint64_t lbcount = 0;
	int8_t etype = -1, netype;
	int adsize;
	struct udf_iyesde_info *iinfo = UDF_I(iyesde);

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB ||
	    iyesde->i_size == iinfo->i_lenExtents)
		return;
	/* Are we going to delete the file anyway? */
	if (iyesde->i_nlink == 0)
		return;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		BUG();

	/* Find the last extent in the file */
	while ((netype = udf_next_aext(iyesde, &epos, &eloc, &elen, 1)) != -1) {
		etype = netype;
		lbcount += elen;
		if (lbcount > iyesde->i_size) {
			if (lbcount - iyesde->i_size >= iyesde->i_sb->s_blocksize)
				udf_warn(iyesde->i_sb,
					 "Too long extent after EOF in iyesde %u: i_size: %lld lbcount: %lld extent %u+%u\n",
					 (unsigned)iyesde->i_iyes,
					 (long long)iyesde->i_size,
					 (long long)lbcount,
					 (unsigned)eloc.logicalBlockNum,
					 (unsigned)elen);
			nelen = elen - (lbcount - iyesde->i_size);
			epos.offset -= adsize;
			extent_trunc(iyesde, &epos, &eloc, etype, elen, nelen);
			epos.offset += adsize;
			if (udf_next_aext(iyesde, &epos, &eloc, &elen, 1) != -1)
				udf_err(iyesde->i_sb,
					"Extent after EOF in iyesde %u\n",
					(unsigned)iyesde->i_iyes);
			break;
		}
	}
	/* This iyesde entry is in-memory only and thus we don't have to mark
	 * the iyesde dirty */
	iinfo->i_lenExtents = iyesde->i_size;
	brelse(epos.bh);
}

void udf_discard_prealloc(struct iyesde *iyesde)
{
	struct extent_position epos = { NULL, 0, {0, 0} };
	struct kernel_lb_addr eloc;
	uint32_t elen;
	uint64_t lbcount = 0;
	int8_t etype = -1, netype;
	int adsize;
	struct udf_iyesde_info *iinfo = UDF_I(iyesde);

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB ||
	    iyesde->i_size == iinfo->i_lenExtents)
		return;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		adsize = 0;

	epos.block = iinfo->i_location;

	/* Find the last extent in the file */
	while ((netype = udf_next_aext(iyesde, &epos, &eloc, &elen, 1)) != -1) {
		etype = netype;
		lbcount += elen;
	}
	if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30)) {
		epos.offset -= adsize;
		lbcount -= elen;
		extent_trunc(iyesde, &epos, &eloc, etype, elen, 0);
		if (!epos.bh) {
			iinfo->i_lenAlloc =
				epos.offset -
				udf_file_entry_alloc_offset(iyesde);
			mark_iyesde_dirty(iyesde);
		} else {
			struct allocExtDesc *aed =
				(struct allocExtDesc *)(epos.bh->b_data);
			aed->lengthAllocDescs =
				cpu_to_le32(epos.offset -
					    sizeof(struct allocExtDesc));
			if (!UDF_QUERY_FLAG(iyesde->i_sb, UDF_FLAG_STRICT) ||
			    UDF_SB(iyesde->i_sb)->s_udfrev >= 0x0201)
				udf_update_tag(epos.bh->b_data, epos.offset);
			else
				udf_update_tag(epos.bh->b_data,
					       sizeof(struct allocExtDesc));
			mark_buffer_dirty_iyesde(epos.bh, iyesde);
		}
	}
	/* This iyesde entry is in-memory only and thus we don't have to mark
	 * the iyesde dirty */
	iinfo->i_lenExtents = lbcount;
	brelse(epos.bh);
}

static void udf_update_alloc_ext_desc(struct iyesde *iyesde,
				      struct extent_position *epos,
				      u32 lenalloc)
{
	struct super_block *sb = iyesde->i_sb;
	struct udf_sb_info *sbi = UDF_SB(sb);

	struct allocExtDesc *aed = (struct allocExtDesc *) (epos->bh->b_data);
	int len = sizeof(struct allocExtDesc);

	aed->lengthAllocDescs =	cpu_to_le32(lenalloc);
	if (!UDF_QUERY_FLAG(sb, UDF_FLAG_STRICT) || sbi->s_udfrev >= 0x0201)
		len += lenalloc;

	udf_update_tag(epos->bh->b_data, len);
	mark_buffer_dirty_iyesde(epos->bh, iyesde);
}

/*
 * Truncate extents of iyesde to iyesde->i_size. This function can be used only
 * for making file shorter. For making file longer, udf_extend_file() has to
 * be used.
 */
int udf_truncate_extents(struct iyesde *iyesde)
{
	struct extent_position epos;
	struct kernel_lb_addr eloc, neloc = {};
	uint32_t elen, nelen = 0, indirect_ext_len = 0, lenalloc;
	int8_t etype;
	struct super_block *sb = iyesde->i_sb;
	sector_t first_block = iyesde->i_size >> sb->s_blocksize_bits, offset;
	loff_t byte_offset;
	int adsize;
	struct udf_iyesde_info *iinfo = UDF_I(iyesde);

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		BUG();

	etype = iyesde_bmap(iyesde, first_block, &epos, &eloc, &elen, &offset);
	byte_offset = (offset << sb->s_blocksize_bits) +
		(iyesde->i_size & (sb->s_blocksize - 1));
	if (etype == -1) {
		/* We should extend the file? */
		WARN_ON(byte_offset);
		return 0;
	}
	epos.offset -= adsize;
	extent_trunc(iyesde, &epos, &eloc, etype, elen, byte_offset);
	epos.offset += adsize;
	if (byte_offset)
		lenalloc = epos.offset;
	else
		lenalloc = epos.offset - adsize;

	if (!epos.bh)
		lenalloc -= udf_file_entry_alloc_offset(iyesde);
	else
		lenalloc -= sizeof(struct allocExtDesc);

	while ((etype = udf_current_aext(iyesde, &epos, &eloc,
					 &elen, 0)) != -1) {
		if (etype == (EXT_NEXT_EXTENT_ALLOCDECS >> 30)) {
			udf_write_aext(iyesde, &epos, &neloc, nelen, 0);
			if (indirect_ext_len) {
				/* We managed to free all extents in the
				 * indirect extent - free it too */
				BUG_ON(!epos.bh);
				udf_free_blocks(sb, NULL, &epos.block,
						0, indirect_ext_len);
			} else if (!epos.bh) {
				iinfo->i_lenAlloc = lenalloc;
				mark_iyesde_dirty(iyesde);
			} else
				udf_update_alloc_ext_desc(iyesde,
						&epos, lenalloc);
			brelse(epos.bh);
			epos.offset = sizeof(struct allocExtDesc);
			epos.block = eloc;
			epos.bh = udf_tread(sb,
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
			extent_trunc(iyesde, &epos, &eloc, etype, elen, 0);
			epos.offset += adsize;
		}
	}

	if (indirect_ext_len) {
		BUG_ON(!epos.bh);
		udf_free_blocks(sb, NULL, &epos.block, 0, indirect_ext_len);
	} else if (!epos.bh) {
		iinfo->i_lenAlloc = lenalloc;
		mark_iyesde_dirty(iyesde);
	} else
		udf_update_alloc_ext_desc(iyesde, &epos, lenalloc);
	iinfo->i_lenExtents = iyesde->i_size;

	brelse(epos.bh);
	return 0;
}
