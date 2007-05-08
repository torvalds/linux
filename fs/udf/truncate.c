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
#include <linux/udf_fs.h>
#include <linux/buffer_head.h>

#include "udf_i.h"
#include "udf_sb.h"

static void extent_trunc(struct inode * inode, struct extent_position *epos,
	kernel_lb_addr eloc, int8_t etype, uint32_t elen, uint32_t nelen)
{
	kernel_lb_addr neloc = { 0, 0 };
	int last_block = (elen + inode->i_sb->s_blocksize - 1) >> inode->i_sb->s_blocksize_bits;
	int first_block = (nelen + inode->i_sb->s_blocksize - 1) >> inode->i_sb->s_blocksize_bits;

	if (nelen)
	{
		if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30))
		{
			udf_free_blocks(inode->i_sb, inode, eloc, 0, last_block);
			etype = (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30);
		}
		else
			neloc = eloc;
		nelen = (etype << 30) | nelen;
	}

	if (elen != nelen)
	{
		udf_write_aext(inode, epos, neloc, nelen, 0);
		if (last_block - first_block > 0)
		{
			if (etype == (EXT_RECORDED_ALLOCATED >> 30))
				mark_inode_dirty(inode);

			if (etype != (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
				udf_free_blocks(inode->i_sb, inode, eloc, first_block, last_block - first_block);
		}
	}
}

void udf_discard_prealloc(struct inode * inode)
{
	struct extent_position epos = { NULL, 0, {0, 0}};
	kernel_lb_addr eloc;
	uint32_t elen, nelen;
	uint64_t lbcount = 0;
	int8_t etype = -1, netype;
	int adsize;

	if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_IN_ICB ||
		inode->i_size == UDF_I_LENEXTENTS(inode))
		return;

	if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(long_ad);
	else
		adsize = 0;

	epos.block = UDF_I_LOCATION(inode);

	/* Find the last extent in the file */
	while ((netype = udf_next_aext(inode, &epos, &eloc, &elen, 1)) != -1)
	{
		etype = netype;
		lbcount += elen;
		if (lbcount > inode->i_size && lbcount - elen < inode->i_size)
		{
			WARN_ON(lbcount - inode->i_size >= inode->i_sb->s_blocksize);
			nelen = elen - (lbcount - inode->i_size);
			epos.offset -= adsize;
			extent_trunc(inode, &epos, eloc, etype, elen, nelen);
			epos.offset += adsize;
			lbcount = inode->i_size;
		}
	}
	if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30)) {
		epos.offset -= adsize;
		lbcount -= elen;
		extent_trunc(inode, &epos, eloc, etype, elen, 0);
		if (!epos.bh)
		{
			UDF_I_LENALLOC(inode) = epos.offset - udf_file_entry_alloc_offset(inode);
			mark_inode_dirty(inode);
		}
		else
		{
			struct allocExtDesc *aed = (struct allocExtDesc *)(epos.bh->b_data);
			aed->lengthAllocDescs = cpu_to_le32(epos.offset - sizeof(struct allocExtDesc));
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) || UDF_SB_UDFREV(inode->i_sb) >= 0x0201)
				udf_update_tag(epos.bh->b_data, epos.offset);
			else
				udf_update_tag(epos.bh->b_data, sizeof(struct allocExtDesc));
			mark_buffer_dirty_inode(epos.bh, inode);
		}
	}
	UDF_I_LENEXTENTS(inode) = lbcount;

	WARN_ON(lbcount != inode->i_size);
	brelse(epos.bh);
}

void udf_truncate_extents(struct inode * inode)
{
	struct extent_position epos;
	kernel_lb_addr eloc, neloc = { 0, 0 };
	uint32_t elen, nelen = 0, indirect_ext_len = 0, lenalloc;
	int8_t etype;
	sector_t first_block = inode->i_size >> inode->i_sb->s_blocksize_bits, offset;
	loff_t byte_offset;
	int adsize;

	if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(long_ad);
	else
		BUG();

	etype = inode_bmap(inode, first_block, &epos, &eloc, &elen, &offset);
	byte_offset = (offset << inode->i_sb->s_blocksize_bits) + (inode->i_size & (inode->i_sb->s_blocksize-1));
	if (etype != -1)
	{
		epos.offset -= adsize;
		extent_trunc(inode, &epos, eloc, etype, elen, byte_offset);
		epos.offset += adsize;
		if (byte_offset)
			lenalloc = epos.offset;
		else
			lenalloc = epos.offset - adsize;

		if (!epos.bh)
			lenalloc -= udf_file_entry_alloc_offset(inode);
		else
			lenalloc -= sizeof(struct allocExtDesc);

		while ((etype = udf_current_aext(inode, &epos, &eloc, &elen, 0)) != -1)
		{
			if (etype == (EXT_NEXT_EXTENT_ALLOCDECS >> 30))
			{
				udf_write_aext(inode, &epos, neloc, nelen, 0);
				if (indirect_ext_len)
				{
					/* We managed to free all extents in the
					 * indirect extent - free it too */
					if (!epos.bh)
						BUG();
					udf_free_blocks(inode->i_sb, inode, epos.block, 0, indirect_ext_len);
				}
				else
				{
					if (!epos.bh)
					{
						UDF_I_LENALLOC(inode) = lenalloc;
						mark_inode_dirty(inode);
					}
					else
					{
						struct allocExtDesc *aed = (struct allocExtDesc *)(epos.bh->b_data);
						aed->lengthAllocDescs = cpu_to_le32(lenalloc);
						if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) || UDF_SB_UDFREV(inode->i_sb) >= 0x0201)
							udf_update_tag(epos.bh->b_data, lenalloc +
								sizeof(struct allocExtDesc));
						else
							udf_update_tag(epos.bh->b_data, sizeof(struct allocExtDesc));
						mark_buffer_dirty_inode(epos.bh, inode);
					}
				}
				brelse(epos.bh);
				epos.offset = sizeof(struct allocExtDesc);
				epos.block = eloc;
				epos.bh = udf_tread(inode->i_sb, udf_get_lb_pblock(inode->i_sb, eloc, 0));
				if (elen)
					indirect_ext_len = (elen +
						inode->i_sb->s_blocksize - 1) >>
						inode->i_sb->s_blocksize_bits;
				else
					indirect_ext_len = 1;
			}
			else
			{
				extent_trunc(inode, &epos, eloc, etype, elen, 0);
				epos.offset += adsize;
			}
		}

		if (indirect_ext_len)
		{
			if (!epos.bh)
				BUG();
			udf_free_blocks(inode->i_sb, inode, epos.block, 0, indirect_ext_len);
		}
		else
		{
			if (!epos.bh)
			{
				UDF_I_LENALLOC(inode) = lenalloc;
				mark_inode_dirty(inode);
			}
			else
			{
				struct allocExtDesc *aed = (struct allocExtDesc *)(epos.bh->b_data);
				aed->lengthAllocDescs = cpu_to_le32(lenalloc);
				if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) || UDF_SB_UDFREV(inode->i_sb) >= 0x0201)
					udf_update_tag(epos.bh->b_data, lenalloc +
						sizeof(struct allocExtDesc));
				else
					udf_update_tag(epos.bh->b_data, sizeof(struct allocExtDesc));
				mark_buffer_dirty_inode(epos.bh, inode);
			}
		}
	}
	else if (inode->i_size)
	{
		if (byte_offset)
		{
			/*
			 *  OK, there is not extent covering inode->i_size and
			 *  no extent above inode->i_size => truncate is
			 *  extending the file by 'offset'.
			 */
			if ((!epos.bh && epos.offset == udf_file_entry_alloc_offset(inode)) ||
			    (epos.bh && epos.offset == sizeof(struct allocExtDesc))) {
				/* File has no extents at all! */
				memset(&eloc, 0x00, sizeof(kernel_lb_addr));
				elen = EXT_NOT_RECORDED_NOT_ALLOCATED | byte_offset;
				udf_add_aext(inode, &epos, eloc, elen, 1);
			}
			else {
				epos.offset -= adsize;
				etype = udf_next_aext(inode, &epos, &eloc, &elen, 1);

				if (etype == (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
				{
					epos.offset -= adsize;
					elen = EXT_NOT_RECORDED_NOT_ALLOCATED | (elen + byte_offset);
					udf_write_aext(inode, &epos, eloc, elen, 0);
				}
				else if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30))
				{
					kernel_lb_addr neloc = { 0, 0 };
					epos.offset -= adsize;
					nelen = EXT_NOT_RECORDED_NOT_ALLOCATED |
						((elen + byte_offset + inode->i_sb->s_blocksize - 1) &
						~(inode->i_sb->s_blocksize - 1));
					udf_write_aext(inode, &epos, neloc, nelen, 1);
					udf_add_aext(inode, &epos, eloc, (etype << 30) | elen, 1);
				}
				else
				{
					if (elen & (inode->i_sb->s_blocksize - 1))
					{
						epos.offset -= adsize;
						elen = EXT_RECORDED_ALLOCATED |
							((elen + inode->i_sb->s_blocksize - 1) &
							~(inode->i_sb->s_blocksize - 1));
						udf_write_aext(inode, &epos, eloc, elen, 1);
					}
					memset(&eloc, 0x00, sizeof(kernel_lb_addr));
					elen = EXT_NOT_RECORDED_NOT_ALLOCATED | byte_offset;
					udf_add_aext(inode, &epos, eloc, elen, 1);
				}
			}
		}
	}
	UDF_I_LENEXTENTS(inode) = inode->i_size;

	brelse(epos.bh);
}
