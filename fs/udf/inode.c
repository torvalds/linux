/*
 * inode.c
 *
 * PURPOSE
 *  Inode handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *  This file is distributed under the terms of the GNU General Public
 *  License (GPL). Copies of the GPL can be obtained from:
 *    ftp://prep.ai.mit.edu/pub/gnu/GPL
 *  Each contributing author retains all rights to their own work.
 *
 *  (C) 1998 Dave Boynton
 *  (C) 1998-2004 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  10/04/98 dgb  Added rudimentary directory functions
 *  10/07/98      Fully working udf_block_map! It works!
 *  11/25/98      bmap altered to better support extents
 *  12/06/98 blf  partition support in udf_iget, udf_block_map
 *                and udf_read_inode
 *  12/12/98      rewrote udf_block_map to handle next extents and descs across
 *                block boundaries (which is not actually allowed)
 *  12/20/98      added support for strategy 4096
 *  03/07/99      rewrote udf_block_map (again)
 *                New funcs, inode_bmap, udf_next_aext
 *  04/19/99      Support for writing device EA's for major/minor #
 */

#include "udfdecl.h"
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/slab.h>
#include <linux/crc-itu-t.h>
#include <linux/mpage.h>

#include "udf_i.h"
#include "udf_sb.h"

MODULE_AUTHOR("Ben Fennema");
MODULE_DESCRIPTION("Universal Disk Format Filesystem");
MODULE_LICENSE("GPL");

#define EXTENT_MERGE_SIZE 5

static umode_t udf_convert_permissions(struct fileEntry *);
static int udf_update_inode(struct inode *, int);
static void udf_fill_inode(struct inode *, struct buffer_head *);
static int udf_sync_inode(struct inode *inode);
static int udf_alloc_i_data(struct inode *inode, size_t size);
static sector_t inode_getblk(struct inode *, sector_t, int *, int *);
static int8_t udf_insert_aext(struct inode *, struct extent_position,
			      struct kernel_lb_addr, uint32_t);
static void udf_split_extents(struct inode *, int *, int, int,
			      struct kernel_long_ad[EXTENT_MERGE_SIZE], int *);
static void udf_prealloc_extents(struct inode *, int, int,
				 struct kernel_long_ad[EXTENT_MERGE_SIZE], int *);
static void udf_merge_extents(struct inode *,
			      struct kernel_long_ad[EXTENT_MERGE_SIZE], int *);
static void udf_update_extents(struct inode *,
			       struct kernel_long_ad[EXTENT_MERGE_SIZE], int, int,
			       struct extent_position *);
static int udf_get_block(struct inode *, sector_t, struct buffer_head *, int);


void udf_evict_inode(struct inode *inode)
{
	struct udf_inode_info *iinfo = UDF_I(inode);
	int want_delete = 0;

	if (!inode->i_nlink && !is_bad_inode(inode)) {
		want_delete = 1;
		udf_setsize(inode, 0);
		udf_update_inode(inode, IS_SYNC(inode));
	} else
		truncate_inode_pages(&inode->i_data, 0);
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB &&
	    inode->i_size != iinfo->i_lenExtents) {
		udf_warn(inode->i_sb, "Inode %lu (mode %o) has inode size %llu different from extent length %llu. Filesystem need not be standards compliant.\n",
			 inode->i_ino, inode->i_mode,
			 (unsigned long long)inode->i_size,
			 (unsigned long long)iinfo->i_lenExtents);
	}
	kfree(iinfo->i_ext.i_data);
	iinfo->i_ext.i_data = NULL;
	if (want_delete) {
		udf_free_inode(inode);
	}
}

static void udf_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;
	struct udf_inode_info *iinfo = UDF_I(inode);
	loff_t isize = inode->i_size;

	if (to > isize) {
		truncate_pagecache(inode, to, isize);
		if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB) {
			down_write(&iinfo->i_data_sem);
			udf_truncate_extents(inode);
			up_write(&iinfo->i_data_sem);
		}
	}
}

static int udf_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, udf_get_block, wbc);
}

static int udf_writepages(struct address_space *mapping,
			struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, udf_get_block);
}

static int udf_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, udf_get_block);
}

static int udf_readpages(struct file *file, struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, udf_get_block);
}

static int udf_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, flags, pagep, udf_get_block);
	if (unlikely(ret))
		udf_write_failed(mapping, pos + len);
	return ret;
}

static ssize_t udf_direct_IO(int rw, struct kiocb *iocb,
			     const struct iovec *iov,
			     loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t ret;

	ret = blockdev_direct_IO(rw, iocb, inode, iov, offset, nr_segs,
				  udf_get_block);
	if (unlikely(ret < 0 && (rw & WRITE)))
		udf_write_failed(mapping, offset + iov_length(iov, nr_segs));
	return ret;
}

static sector_t udf_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, udf_get_block);
}

const struct address_space_operations udf_aops = {
	.readpage	= udf_readpage,
	.readpages	= udf_readpages,
	.writepage	= udf_writepage,
	.writepages	= udf_writepages,
	.write_begin	= udf_write_begin,
	.write_end	= generic_write_end,
	.direct_IO	= udf_direct_IO,
	.bmap		= udf_bmap,
};

/*
 * Expand file stored in ICB to a normal one-block-file
 *
 * This function requires i_data_sem for writing and releases it.
 * This function requires i_mutex held
 */
int udf_expand_file_adinicb(struct inode *inode)
{
	struct page *page;
	char *kaddr;
	struct udf_inode_info *iinfo = UDF_I(inode);
	int err;
	struct writeback_control udf_wbc = {
		.sync_mode = WB_SYNC_NONE,
		.nr_to_write = 1,
	};

	if (!iinfo->i_lenAlloc) {
		if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
			iinfo->i_alloc_type = ICBTAG_FLAG_AD_SHORT;
		else
			iinfo->i_alloc_type = ICBTAG_FLAG_AD_LONG;
		/* from now on we have normal address_space methods */
		inode->i_data.a_ops = &udf_aops;
		up_write(&iinfo->i_data_sem);
		mark_inode_dirty(inode);
		return 0;
	}
	/*
	 * Release i_data_sem so that we can lock a page - page lock ranks
	 * above i_data_sem. i_mutex still protects us against file changes.
	 */
	up_write(&iinfo->i_data_sem);

	page = find_or_create_page(inode->i_mapping, 0, GFP_NOFS);
	if (!page)
		return -ENOMEM;

	if (!PageUptodate(page)) {
		kaddr = kmap(page);
		memset(kaddr + iinfo->i_lenAlloc, 0x00,
		       PAGE_CACHE_SIZE - iinfo->i_lenAlloc);
		memcpy(kaddr, iinfo->i_ext.i_data + iinfo->i_lenEAttr,
			iinfo->i_lenAlloc);
		flush_dcache_page(page);
		SetPageUptodate(page);
		kunmap(page);
	}
	down_write(&iinfo->i_data_sem);
	memset(iinfo->i_ext.i_data + iinfo->i_lenEAttr, 0x00,
	       iinfo->i_lenAlloc);
	iinfo->i_lenAlloc = 0;
	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_SHORT;
	else
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_LONG;
	/* from now on we have normal address_space methods */
	inode->i_data.a_ops = &udf_aops;
	up_write(&iinfo->i_data_sem);
	err = inode->i_data.a_ops->writepage(page, &udf_wbc);
	if (err) {
		/* Restore everything back so that we don't lose data... */
		lock_page(page);
		kaddr = kmap(page);
		down_write(&iinfo->i_data_sem);
		memcpy(iinfo->i_ext.i_data + iinfo->i_lenEAttr, kaddr,
		       inode->i_size);
		kunmap(page);
		unlock_page(page);
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_IN_ICB;
		inode->i_data.a_ops = &udf_adinicb_aops;
		up_write(&iinfo->i_data_sem);
	}
	page_cache_release(page);
	mark_inode_dirty(inode);

	return err;
}

struct buffer_head *udf_expand_dir_adinicb(struct inode *inode, int *block,
					   int *err)
{
	int newblock;
	struct buffer_head *dbh = NULL;
	struct kernel_lb_addr eloc;
	uint8_t alloctype;
	struct extent_position epos;

	struct udf_fileident_bh sfibh, dfibh;
	loff_t f_pos = udf_ext0_offset(inode);
	int size = udf_ext0_offset(inode) + inode->i_size;
	struct fileIdentDesc cfi, *sfi, *dfi;
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
		alloctype = ICBTAG_FLAG_AD_SHORT;
	else
		alloctype = ICBTAG_FLAG_AD_LONG;

	if (!inode->i_size) {
		iinfo->i_alloc_type = alloctype;
		mark_inode_dirty(inode);
		return NULL;
	}

	/* alloc block, and copy data to it */
	*block = udf_new_block(inode->i_sb, inode,
			       iinfo->i_location.partitionReferenceNum,
			       iinfo->i_location.logicalBlockNum, err);
	if (!(*block))
		return NULL;
	newblock = udf_get_pblock(inode->i_sb, *block,
				  iinfo->i_location.partitionReferenceNum,
				0);
	if (!newblock)
		return NULL;
	dbh = udf_tgetblk(inode->i_sb, newblock);
	if (!dbh)
		return NULL;
	lock_buffer(dbh);
	memset(dbh->b_data, 0x00, inode->i_sb->s_blocksize);
	set_buffer_uptodate(dbh);
	unlock_buffer(dbh);
	mark_buffer_dirty_inode(dbh, inode);

	sfibh.soffset = sfibh.eoffset =
			f_pos & (inode->i_sb->s_blocksize - 1);
	sfibh.sbh = sfibh.ebh = NULL;
	dfibh.soffset = dfibh.eoffset = 0;
	dfibh.sbh = dfibh.ebh = dbh;
	while (f_pos < size) {
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_IN_ICB;
		sfi = udf_fileident_read(inode, &f_pos, &sfibh, &cfi, NULL,
					 NULL, NULL, NULL);
		if (!sfi) {
			brelse(dbh);
			return NULL;
		}
		iinfo->i_alloc_type = alloctype;
		sfi->descTag.tagLocation = cpu_to_le32(*block);
		dfibh.soffset = dfibh.eoffset;
		dfibh.eoffset += (sfibh.eoffset - sfibh.soffset);
		dfi = (struct fileIdentDesc *)(dbh->b_data + dfibh.soffset);
		if (udf_write_fi(inode, sfi, dfi, &dfibh, sfi->impUse,
				 sfi->fileIdent +
					le16_to_cpu(sfi->lengthOfImpUse))) {
			iinfo->i_alloc_type = ICBTAG_FLAG_AD_IN_ICB;
			brelse(dbh);
			return NULL;
		}
	}
	mark_buffer_dirty_inode(dbh, inode);

	memset(iinfo->i_ext.i_data + iinfo->i_lenEAttr, 0,
		iinfo->i_lenAlloc);
	iinfo->i_lenAlloc = 0;
	eloc.logicalBlockNum = *block;
	eloc.partitionReferenceNum =
				iinfo->i_location.partitionReferenceNum;
	iinfo->i_lenExtents = inode->i_size;
	epos.bh = NULL;
	epos.block = iinfo->i_location;
	epos.offset = udf_file_entry_alloc_offset(inode);
	udf_add_aext(inode, &epos, &eloc, inode->i_size, 0);
	/* UniqueID stuff */

	brelse(epos.bh);
	mark_inode_dirty(inode);
	return dbh;
}

static int udf_get_block(struct inode *inode, sector_t block,
			 struct buffer_head *bh_result, int create)
{
	int err, new;
	sector_t phys = 0;
	struct udf_inode_info *iinfo;

	if (!create) {
		phys = udf_block_map(inode, block);
		if (phys)
			map_bh(bh_result, inode->i_sb, phys);
		return 0;
	}

	err = -EIO;
	new = 0;
	iinfo = UDF_I(inode);

	down_write(&iinfo->i_data_sem);
	if (block == iinfo->i_next_alloc_block + 1) {
		iinfo->i_next_alloc_block++;
		iinfo->i_next_alloc_goal++;
	}


	phys = inode_getblk(inode, block, &err, &new);
	if (!phys)
		goto abort;

	if (new)
		set_buffer_new(bh_result);
	map_bh(bh_result, inode->i_sb, phys);

abort:
	up_write(&iinfo->i_data_sem);
	return err;
}

static struct buffer_head *udf_getblk(struct inode *inode, long block,
				      int create, int *err)
{
	struct buffer_head *bh;
	struct buffer_head dummy;

	dummy.b_state = 0;
	dummy.b_blocknr = -1000;
	*err = udf_get_block(inode, block, &dummy, create);
	if (!*err && buffer_mapped(&dummy)) {
		bh = sb_getblk(inode->i_sb, dummy.b_blocknr);
		if (buffer_new(&dummy)) {
			lock_buffer(bh);
			memset(bh->b_data, 0x00, inode->i_sb->s_blocksize);
			set_buffer_uptodate(bh);
			unlock_buffer(bh);
			mark_buffer_dirty_inode(bh, inode);
		}
		return bh;
	}

	return NULL;
}

/* Extend the file by 'blocks' blocks, return the number of extents added */
static int udf_do_extend_file(struct inode *inode,
			      struct extent_position *last_pos,
			      struct kernel_long_ad *last_ext,
			      sector_t blocks)
{
	sector_t add;
	int count = 0, fake = !(last_ext->extLength & UDF_EXTENT_LENGTH_MASK);
	struct super_block *sb = inode->i_sb;
	struct kernel_lb_addr prealloc_loc = {};
	int prealloc_len = 0;
	struct udf_inode_info *iinfo;
	int err;

	/* The previous extent is fake and we should not extend by anything
	 * - there's nothing to do... */
	if (!blocks && fake)
		return 0;

	iinfo = UDF_I(inode);
	/* Round the last extent up to a multiple of block size */
	if (last_ext->extLength & (sb->s_blocksize - 1)) {
		last_ext->extLength =
			(last_ext->extLength & UDF_EXTENT_FLAG_MASK) |
			(((last_ext->extLength & UDF_EXTENT_LENGTH_MASK) +
			  sb->s_blocksize - 1) & ~(sb->s_blocksize - 1));
		iinfo->i_lenExtents =
			(iinfo->i_lenExtents + sb->s_blocksize - 1) &
			~(sb->s_blocksize - 1);
	}

	/* Last extent are just preallocated blocks? */
	if ((last_ext->extLength & UDF_EXTENT_FLAG_MASK) ==
						EXT_NOT_RECORDED_ALLOCATED) {
		/* Save the extent so that we can reattach it to the end */
		prealloc_loc = last_ext->extLocation;
		prealloc_len = last_ext->extLength;
		/* Mark the extent as a hole */
		last_ext->extLength = EXT_NOT_RECORDED_NOT_ALLOCATED |
			(last_ext->extLength & UDF_EXTENT_LENGTH_MASK);
		last_ext->extLocation.logicalBlockNum = 0;
		last_ext->extLocation.partitionReferenceNum = 0;
	}

	/* Can we merge with the previous extent? */
	if ((last_ext->extLength & UDF_EXTENT_FLAG_MASK) ==
					EXT_NOT_RECORDED_NOT_ALLOCATED) {
		add = ((1 << 30) - sb->s_blocksize -
			(last_ext->extLength & UDF_EXTENT_LENGTH_MASK)) >>
			sb->s_blocksize_bits;
		if (add > blocks)
			add = blocks;
		blocks -= add;
		last_ext->extLength += add << sb->s_blocksize_bits;
	}

	if (fake) {
		udf_add_aext(inode, last_pos, &last_ext->extLocation,
			     last_ext->extLength, 1);
		count++;
	} else
		udf_write_aext(inode, last_pos, &last_ext->extLocation,
				last_ext->extLength, 1);

	/* Managed to do everything necessary? */
	if (!blocks)
		goto out;

	/* All further extents will be NOT_RECORDED_NOT_ALLOCATED */
	last_ext->extLocation.logicalBlockNum = 0;
	last_ext->extLocation.partitionReferenceNum = 0;
	add = (1 << (30-sb->s_blocksize_bits)) - 1;
	last_ext->extLength = EXT_NOT_RECORDED_NOT_ALLOCATED |
				(add << sb->s_blocksize_bits);

	/* Create enough extents to cover the whole hole */
	while (blocks > add) {
		blocks -= add;
		err = udf_add_aext(inode, last_pos, &last_ext->extLocation,
				   last_ext->extLength, 1);
		if (err)
			return err;
		count++;
	}
	if (blocks) {
		last_ext->extLength = EXT_NOT_RECORDED_NOT_ALLOCATED |
			(blocks << sb->s_blocksize_bits);
		err = udf_add_aext(inode, last_pos, &last_ext->extLocation,
				   last_ext->extLength, 1);
		if (err)
			return err;
		count++;
	}

out:
	/* Do we have some preallocated blocks saved? */
	if (prealloc_len) {
		err = udf_add_aext(inode, last_pos, &prealloc_loc,
				   prealloc_len, 1);
		if (err)
			return err;
		last_ext->extLocation = prealloc_loc;
		last_ext->extLength = prealloc_len;
		count++;
	}

	/* last_pos should point to the last written extent... */
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		last_pos->offset -= sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		last_pos->offset -= sizeof(struct long_ad);
	else
		return -EIO;

	return count;
}

static int udf_extend_file(struct inode *inode, loff_t newsize)
{

	struct extent_position epos;
	struct kernel_lb_addr eloc;
	uint32_t elen;
	int8_t etype;
	struct super_block *sb = inode->i_sb;
	sector_t first_block = newsize >> sb->s_blocksize_bits, offset;
	int adsize;
	struct udf_inode_info *iinfo = UDF_I(inode);
	struct kernel_long_ad extent;
	int err;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		BUG();

	etype = inode_bmap(inode, first_block, &epos, &eloc, &elen, &offset);

	/* File has extent covering the new size (could happen when extending
	 * inside a block)? */
	if (etype != -1)
		return 0;
	if (newsize & (sb->s_blocksize - 1))
		offset++;
	/* Extended file just to the boundary of the last file block? */
	if (offset == 0)
		return 0;

	/* Truncate is extending the file by 'offset' blocks */
	if ((!epos.bh && epos.offset == udf_file_entry_alloc_offset(inode)) ||
	    (epos.bh && epos.offset == sizeof(struct allocExtDesc))) {
		/* File has no extents at all or has empty last
		 * indirect extent! Create a fake extent... */
		extent.extLocation.logicalBlockNum = 0;
		extent.extLocation.partitionReferenceNum = 0;
		extent.extLength = EXT_NOT_RECORDED_NOT_ALLOCATED;
	} else {
		epos.offset -= adsize;
		etype = udf_next_aext(inode, &epos, &extent.extLocation,
				      &extent.extLength, 0);
		extent.extLength |= etype << 30;
	}
	err = udf_do_extend_file(inode, &epos, &extent, offset);
	if (err < 0)
		goto out;
	err = 0;
	iinfo->i_lenExtents = newsize;
out:
	brelse(epos.bh);
	return err;
}

static sector_t inode_getblk(struct inode *inode, sector_t block,
			     int *err, int *new)
{
	static sector_t last_block;
	struct kernel_long_ad laarr[EXTENT_MERGE_SIZE];
	struct extent_position prev_epos, cur_epos, next_epos;
	int count = 0, startnum = 0, endnum = 0;
	uint32_t elen = 0, tmpelen;
	struct kernel_lb_addr eloc, tmpeloc;
	int c = 1;
	loff_t lbcount = 0, b_off = 0;
	uint32_t newblocknum, newblock;
	sector_t offset = 0;
	int8_t etype;
	struct udf_inode_info *iinfo = UDF_I(inode);
	int goal = 0, pgoal = iinfo->i_location.logicalBlockNum;
	int lastblock = 0;

	*err = 0;
	*new = 0;
	prev_epos.offset = udf_file_entry_alloc_offset(inode);
	prev_epos.block = iinfo->i_location;
	prev_epos.bh = NULL;
	cur_epos = next_epos = prev_epos;
	b_off = (loff_t)block << inode->i_sb->s_blocksize_bits;

	/* find the extent which contains the block we are looking for.
	   alternate between laarr[0] and laarr[1] for locations of the
	   current extent, and the previous extent */
	do {
		if (prev_epos.bh != cur_epos.bh) {
			brelse(prev_epos.bh);
			get_bh(cur_epos.bh);
			prev_epos.bh = cur_epos.bh;
		}
		if (cur_epos.bh != next_epos.bh) {
			brelse(cur_epos.bh);
			get_bh(next_epos.bh);
			cur_epos.bh = next_epos.bh;
		}

		lbcount += elen;

		prev_epos.block = cur_epos.block;
		cur_epos.block = next_epos.block;

		prev_epos.offset = cur_epos.offset;
		cur_epos.offset = next_epos.offset;

		etype = udf_next_aext(inode, &next_epos, &eloc, &elen, 1);
		if (etype == -1)
			break;

		c = !c;

		laarr[c].extLength = (etype << 30) | elen;
		laarr[c].extLocation = eloc;

		if (etype != (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
			pgoal = eloc.logicalBlockNum +
				((elen + inode->i_sb->s_blocksize - 1) >>
				 inode->i_sb->s_blocksize_bits);

		count++;
	} while (lbcount + elen <= b_off);

	b_off -= lbcount;
	offset = b_off >> inode->i_sb->s_blocksize_bits;
	/*
	 * Move prev_epos and cur_epos into indirect extent if we are at
	 * the pointer to it
	 */
	udf_next_aext(inode, &prev_epos, &tmpeloc, &tmpelen, 0);
	udf_next_aext(inode, &cur_epos, &tmpeloc, &tmpelen, 0);

	/* if the extent is allocated and recorded, return the block
	   if the extent is not a multiple of the blocksize, round up */

	if (etype == (EXT_RECORDED_ALLOCATED >> 30)) {
		if (elen & (inode->i_sb->s_blocksize - 1)) {
			elen = EXT_RECORDED_ALLOCATED |
				((elen + inode->i_sb->s_blocksize - 1) &
				 ~(inode->i_sb->s_blocksize - 1));
			udf_write_aext(inode, &cur_epos, &eloc, elen, 1);
		}
		brelse(prev_epos.bh);
		brelse(cur_epos.bh);
		brelse(next_epos.bh);
		newblock = udf_get_lb_pblock(inode->i_sb, &eloc, offset);
		return newblock;
	}

	last_block = block;
	/* Are we beyond EOF? */
	if (etype == -1) {
		int ret;

		if (count) {
			if (c)
				laarr[0] = laarr[1];
			startnum = 1;
		} else {
			/* Create a fake extent when there's not one */
			memset(&laarr[0].extLocation, 0x00,
				sizeof(struct kernel_lb_addr));
			laarr[0].extLength = EXT_NOT_RECORDED_NOT_ALLOCATED;
			/* Will udf_do_extend_file() create real extent from
			   a fake one? */
			startnum = (offset > 0);
		}
		/* Create extents for the hole between EOF and offset */
		ret = udf_do_extend_file(inode, &prev_epos, laarr, offset);
		if (ret < 0) {
			brelse(prev_epos.bh);
			brelse(cur_epos.bh);
			brelse(next_epos.bh);
			*err = ret;
			return 0;
		}
		c = 0;
		offset = 0;
		count += ret;
		/* We are not covered by a preallocated extent? */
		if ((laarr[0].extLength & UDF_EXTENT_FLAG_MASK) !=
						EXT_NOT_RECORDED_ALLOCATED) {
			/* Is there any real extent? - otherwise we overwrite
			 * the fake one... */
			if (count)
				c = !c;
			laarr[c].extLength = EXT_NOT_RECORDED_NOT_ALLOCATED |
				inode->i_sb->s_blocksize;
			memset(&laarr[c].extLocation, 0x00,
				sizeof(struct kernel_lb_addr));
			count++;
			endnum++;
		}
		endnum = c + 1;
		lastblock = 1;
	} else {
		endnum = startnum = ((count > 2) ? 2 : count);

		/* if the current extent is in position 0,
		   swap it with the previous */
		if (!c && count != 1) {
			laarr[2] = laarr[0];
			laarr[0] = laarr[1];
			laarr[1] = laarr[2];
			c = 1;
		}

		/* if the current block is located in an extent,
		   read the next extent */
		etype = udf_next_aext(inode, &next_epos, &eloc, &elen, 0);
		if (etype != -1) {
			laarr[c + 1].extLength = (etype << 30) | elen;
			laarr[c + 1].extLocation = eloc;
			count++;
			startnum++;
			endnum++;
		} else
			lastblock = 1;
	}

	/* if the current extent is not recorded but allocated, get the
	 * block in the extent corresponding to the requested block */
	if ((laarr[c].extLength >> 30) == (EXT_NOT_RECORDED_ALLOCATED >> 30))
		newblocknum = laarr[c].extLocation.logicalBlockNum + offset;
	else { /* otherwise, allocate a new block */
		if (iinfo->i_next_alloc_block == block)
			goal = iinfo->i_next_alloc_goal;

		if (!goal) {
			if (!(goal = pgoal)) /* XXX: what was intended here? */
				goal = iinfo->i_location.logicalBlockNum + 1;
		}

		newblocknum = udf_new_block(inode->i_sb, inode,
				iinfo->i_location.partitionReferenceNum,
				goal, err);
		if (!newblocknum) {
			brelse(prev_epos.bh);
			*err = -ENOSPC;
			return 0;
		}
		iinfo->i_lenExtents += inode->i_sb->s_blocksize;
	}

	/* if the extent the requsted block is located in contains multiple
	 * blocks, split the extent into at most three extents. blocks prior
	 * to requested block, requested block, and blocks after requested
	 * block */
	udf_split_extents(inode, &c, offset, newblocknum, laarr, &endnum);

#ifdef UDF_PREALLOCATE
	/* We preallocate blocks only for regular files. It also makes sense
	 * for directories but there's a problem when to drop the
	 * preallocation. We might use some delayed work for that but I feel
	 * it's overengineering for a filesystem like UDF. */
	if (S_ISREG(inode->i_mode))
		udf_prealloc_extents(inode, c, lastblock, laarr, &endnum);
#endif

	/* merge any continuous blocks in laarr */
	udf_merge_extents(inode, laarr, &endnum);

	/* write back the new extents, inserting new extents if the new number
	 * of extents is greater than the old number, and deleting extents if
	 * the new number of extents is less than the old number */
	udf_update_extents(inode, laarr, startnum, endnum, &prev_epos);

	brelse(prev_epos.bh);

	newblock = udf_get_pblock(inode->i_sb, newblocknum,
				iinfo->i_location.partitionReferenceNum, 0);
	if (!newblock) {
		*err = -EIO;
		return 0;
	}
	*new = 1;
	iinfo->i_next_alloc_block = block;
	iinfo->i_next_alloc_goal = newblocknum;
	inode->i_ctime = current_fs_time(inode->i_sb);

	if (IS_SYNC(inode))
		udf_sync_inode(inode);
	else
		mark_inode_dirty(inode);

	return newblock;
}

static void udf_split_extents(struct inode *inode, int *c, int offset,
			      int newblocknum,
			      struct kernel_long_ad laarr[EXTENT_MERGE_SIZE],
			      int *endnum)
{
	unsigned long blocksize = inode->i_sb->s_blocksize;
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;

	if ((laarr[*c].extLength >> 30) == (EXT_NOT_RECORDED_ALLOCATED >> 30) ||
	    (laarr[*c].extLength >> 30) ==
				(EXT_NOT_RECORDED_NOT_ALLOCATED >> 30)) {
		int curr = *c;
		int blen = ((laarr[curr].extLength & UDF_EXTENT_LENGTH_MASK) +
			    blocksize - 1) >> blocksize_bits;
		int8_t etype = (laarr[curr].extLength >> 30);

		if (blen == 1)
			;
		else if (!offset || blen == offset + 1) {
			laarr[curr + 2] = laarr[curr + 1];
			laarr[curr + 1] = laarr[curr];
		} else {
			laarr[curr + 3] = laarr[curr + 1];
			laarr[curr + 2] = laarr[curr + 1] = laarr[curr];
		}

		if (offset) {
			if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30)) {
				udf_free_blocks(inode->i_sb, inode,
						&laarr[curr].extLocation,
						0, offset);
				laarr[curr].extLength =
					EXT_NOT_RECORDED_NOT_ALLOCATED |
					(offset << blocksize_bits);
				laarr[curr].extLocation.logicalBlockNum = 0;
				laarr[curr].extLocation.
						partitionReferenceNum = 0;
			} else
				laarr[curr].extLength = (etype << 30) |
					(offset << blocksize_bits);
			curr++;
			(*c)++;
			(*endnum)++;
		}

		laarr[curr].extLocation.logicalBlockNum = newblocknum;
		if (etype == (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
			laarr[curr].extLocation.partitionReferenceNum =
				UDF_I(inode)->i_location.partitionReferenceNum;
		laarr[curr].extLength = EXT_RECORDED_ALLOCATED |
			blocksize;
		curr++;

		if (blen != offset + 1) {
			if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30))
				laarr[curr].extLocation.logicalBlockNum +=
								offset + 1;
			laarr[curr].extLength = (etype << 30) |
				((blen - (offset + 1)) << blocksize_bits);
			curr++;
			(*endnum)++;
		}
	}
}

static void udf_prealloc_extents(struct inode *inode, int c, int lastblock,
				 struct kernel_long_ad laarr[EXTENT_MERGE_SIZE],
				 int *endnum)
{
	int start, length = 0, currlength = 0, i;

	if (*endnum >= (c + 1)) {
		if (!lastblock)
			return;
		else
			start = c;
	} else {
		if ((laarr[c + 1].extLength >> 30) ==
					(EXT_NOT_RECORDED_ALLOCATED >> 30)) {
			start = c + 1;
			length = currlength =
				(((laarr[c + 1].extLength &
					UDF_EXTENT_LENGTH_MASK) +
				inode->i_sb->s_blocksize - 1) >>
				inode->i_sb->s_blocksize_bits);
		} else
			start = c;
	}

	for (i = start + 1; i <= *endnum; i++) {
		if (i == *endnum) {
			if (lastblock)
				length += UDF_DEFAULT_PREALLOC_BLOCKS;
		} else if ((laarr[i].extLength >> 30) ==
				(EXT_NOT_RECORDED_NOT_ALLOCATED >> 30)) {
			length += (((laarr[i].extLength &
						UDF_EXTENT_LENGTH_MASK) +
				    inode->i_sb->s_blocksize - 1) >>
				    inode->i_sb->s_blocksize_bits);
		} else
			break;
	}

	if (length) {
		int next = laarr[start].extLocation.logicalBlockNum +
			(((laarr[start].extLength & UDF_EXTENT_LENGTH_MASK) +
			  inode->i_sb->s_blocksize - 1) >>
			  inode->i_sb->s_blocksize_bits);
		int numalloc = udf_prealloc_blocks(inode->i_sb, inode,
				laarr[start].extLocation.partitionReferenceNum,
				next, (UDF_DEFAULT_PREALLOC_BLOCKS > length ?
				length : UDF_DEFAULT_PREALLOC_BLOCKS) -
				currlength);
		if (numalloc) 	{
			if (start == (c + 1))
				laarr[start].extLength +=
					(numalloc <<
					 inode->i_sb->s_blocksize_bits);
			else {
				memmove(&laarr[c + 2], &laarr[c + 1],
					sizeof(struct long_ad) * (*endnum - (c + 1)));
				(*endnum)++;
				laarr[c + 1].extLocation.logicalBlockNum = next;
				laarr[c + 1].extLocation.partitionReferenceNum =
					laarr[c].extLocation.
							partitionReferenceNum;
				laarr[c + 1].extLength =
					EXT_NOT_RECORDED_ALLOCATED |
					(numalloc <<
					 inode->i_sb->s_blocksize_bits);
				start = c + 1;
			}

			for (i = start + 1; numalloc && i < *endnum; i++) {
				int elen = ((laarr[i].extLength &
						UDF_EXTENT_LENGTH_MASK) +
					    inode->i_sb->s_blocksize - 1) >>
					    inode->i_sb->s_blocksize_bits;

				if (elen > numalloc) {
					laarr[i].extLength -=
						(numalloc <<
						 inode->i_sb->s_blocksize_bits);
					numalloc = 0;
				} else {
					numalloc -= elen;
					if (*endnum > (i + 1))
						memmove(&laarr[i],
							&laarr[i + 1],
							sizeof(struct long_ad) *
							(*endnum - (i + 1)));
					i--;
					(*endnum)--;
				}
			}
			UDF_I(inode)->i_lenExtents +=
				numalloc << inode->i_sb->s_blocksize_bits;
		}
	}
}

static void udf_merge_extents(struct inode *inode,
			      struct kernel_long_ad laarr[EXTENT_MERGE_SIZE],
			      int *endnum)
{
	int i;
	unsigned long blocksize = inode->i_sb->s_blocksize;
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;

	for (i = 0; i < (*endnum - 1); i++) {
		struct kernel_long_ad *li /*l[i]*/ = &laarr[i];
		struct kernel_long_ad *lip1 /*l[i plus 1]*/ = &laarr[i + 1];

		if (((li->extLength >> 30) == (lip1->extLength >> 30)) &&
			(((li->extLength >> 30) ==
				(EXT_NOT_RECORDED_NOT_ALLOCATED >> 30)) ||
			((lip1->extLocation.logicalBlockNum -
			  li->extLocation.logicalBlockNum) ==
			(((li->extLength & UDF_EXTENT_LENGTH_MASK) +
			blocksize - 1) >> blocksize_bits)))) {

			if (((li->extLength & UDF_EXTENT_LENGTH_MASK) +
				(lip1->extLength & UDF_EXTENT_LENGTH_MASK) +
				blocksize - 1) & ~UDF_EXTENT_LENGTH_MASK) {
				lip1->extLength = (lip1->extLength -
						  (li->extLength &
						   UDF_EXTENT_LENGTH_MASK) +
						   UDF_EXTENT_LENGTH_MASK) &
							~(blocksize - 1);
				li->extLength = (li->extLength &
						 UDF_EXTENT_FLAG_MASK) +
						(UDF_EXTENT_LENGTH_MASK + 1) -
						blocksize;
				lip1->extLocation.logicalBlockNum =
					li->extLocation.logicalBlockNum +
					((li->extLength &
						UDF_EXTENT_LENGTH_MASK) >>
						blocksize_bits);
			} else {
				li->extLength = lip1->extLength +
					(((li->extLength &
						UDF_EXTENT_LENGTH_MASK) +
					 blocksize - 1) & ~(blocksize - 1));
				if (*endnum > (i + 2))
					memmove(&laarr[i + 1], &laarr[i + 2],
						sizeof(struct long_ad) *
						(*endnum - (i + 2)));
				i--;
				(*endnum)--;
			}
		} else if (((li->extLength >> 30) ==
				(EXT_NOT_RECORDED_ALLOCATED >> 30)) &&
			   ((lip1->extLength >> 30) ==
				(EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))) {
			udf_free_blocks(inode->i_sb, inode, &li->extLocation, 0,
					((li->extLength &
					  UDF_EXTENT_LENGTH_MASK) +
					 blocksize - 1) >> blocksize_bits);
			li->extLocation.logicalBlockNum = 0;
			li->extLocation.partitionReferenceNum = 0;

			if (((li->extLength & UDF_EXTENT_LENGTH_MASK) +
			     (lip1->extLength & UDF_EXTENT_LENGTH_MASK) +
			     blocksize - 1) & ~UDF_EXTENT_LENGTH_MASK) {
				lip1->extLength = (lip1->extLength -
						   (li->extLength &
						   UDF_EXTENT_LENGTH_MASK) +
						   UDF_EXTENT_LENGTH_MASK) &
						   ~(blocksize - 1);
				li->extLength = (li->extLength &
						 UDF_EXTENT_FLAG_MASK) +
						(UDF_EXTENT_LENGTH_MASK + 1) -
						blocksize;
			} else {
				li->extLength = lip1->extLength +
					(((li->extLength &
						UDF_EXTENT_LENGTH_MASK) +
					  blocksize - 1) & ~(blocksize - 1));
				if (*endnum > (i + 2))
					memmove(&laarr[i + 1], &laarr[i + 2],
						sizeof(struct long_ad) *
						(*endnum - (i + 2)));
				i--;
				(*endnum)--;
			}
		} else if ((li->extLength >> 30) ==
					(EXT_NOT_RECORDED_ALLOCATED >> 30)) {
			udf_free_blocks(inode->i_sb, inode,
					&li->extLocation, 0,
					((li->extLength &
						UDF_EXTENT_LENGTH_MASK) +
					 blocksize - 1) >> blocksize_bits);
			li->extLocation.logicalBlockNum = 0;
			li->extLocation.partitionReferenceNum = 0;
			li->extLength = (li->extLength &
						UDF_EXTENT_LENGTH_MASK) |
						EXT_NOT_RECORDED_NOT_ALLOCATED;
		}
	}
}

static void udf_update_extents(struct inode *inode,
			       struct kernel_long_ad laarr[EXTENT_MERGE_SIZE],
			       int startnum, int endnum,
			       struct extent_position *epos)
{
	int start = 0, i;
	struct kernel_lb_addr tmploc;
	uint32_t tmplen;

	if (startnum > endnum) {
		for (i = 0; i < (startnum - endnum); i++)
			udf_delete_aext(inode, *epos, laarr[i].extLocation,
					laarr[i].extLength);
	} else if (startnum < endnum) {
		for (i = 0; i < (endnum - startnum); i++) {
			udf_insert_aext(inode, *epos, laarr[i].extLocation,
					laarr[i].extLength);
			udf_next_aext(inode, epos, &laarr[i].extLocation,
				      &laarr[i].extLength, 1);
			start++;
		}
	}

	for (i = start; i < endnum; i++) {
		udf_next_aext(inode, epos, &tmploc, &tmplen, 0);
		udf_write_aext(inode, epos, &laarr[i].extLocation,
			       laarr[i].extLength, 1);
	}
}

struct buffer_head *udf_bread(struct inode *inode, int block,
			      int create, int *err)
{
	struct buffer_head *bh = NULL;

	bh = udf_getblk(inode, block, create, err);
	if (!bh)
		return NULL;

	if (buffer_uptodate(bh))
		return bh;

	ll_rw_block(READ, 1, &bh);

	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;

	brelse(bh);
	*err = -EIO;
	return NULL;
}

int udf_setsize(struct inode *inode, loff_t newsize)
{
	int err;
	struct udf_inode_info *iinfo;
	int bsize = 1 << inode->i_blkbits;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;

	iinfo = UDF_I(inode);
	if (newsize > inode->i_size) {
		down_write(&iinfo->i_data_sem);
		if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
			if (bsize <
			    (udf_file_entry_alloc_offset(inode) + newsize)) {
				err = udf_expand_file_adinicb(inode);
				if (err)
					return err;
				down_write(&iinfo->i_data_sem);
			} else {
				iinfo->i_lenAlloc = newsize;
				goto set_size;
			}
		}
		err = udf_extend_file(inode, newsize);
		if (err) {
			up_write(&iinfo->i_data_sem);
			return err;
		}
set_size:
		truncate_setsize(inode, newsize);
		up_write(&iinfo->i_data_sem);
	} else {
		if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
			down_write(&iinfo->i_data_sem);
			memset(iinfo->i_ext.i_data + iinfo->i_lenEAttr + newsize,
			       0x00, bsize - newsize -
			       udf_file_entry_alloc_offset(inode));
			iinfo->i_lenAlloc = newsize;
			truncate_setsize(inode, newsize);
			up_write(&iinfo->i_data_sem);
			goto update_time;
		}
		err = block_truncate_page(inode->i_mapping, newsize,
					  udf_get_block);
		if (err)
			return err;
		down_write(&iinfo->i_data_sem);
		truncate_setsize(inode, newsize);
		udf_truncate_extents(inode);
		up_write(&iinfo->i_data_sem);
	}
update_time:
	inode->i_mtime = inode->i_ctime = current_fs_time(inode->i_sb);
	if (IS_SYNC(inode))
		udf_sync_inode(inode);
	else
		mark_inode_dirty(inode);
	return 0;
}

static void __udf_read_inode(struct inode *inode)
{
	struct buffer_head *bh = NULL;
	struct fileEntry *fe;
	uint16_t ident;
	struct udf_inode_info *iinfo = UDF_I(inode);

	/*
	 * Set defaults, but the inode is still incomplete!
	 * Note: get_new_inode() sets the following on a new inode:
	 *      i_sb = sb
	 *      i_no = ino
	 *      i_flags = sb->s_flags
	 *      i_state = 0
	 * clean_inode(): zero fills and sets
	 *      i_count = 1
	 *      i_nlink = 1
	 *      i_op = NULL;
	 */
	bh = udf_read_ptagged(inode->i_sb, &iinfo->i_location, 0, &ident);
	if (!bh) {
		udf_err(inode->i_sb, "(ino %ld) failed !bh\n", inode->i_ino);
		make_bad_inode(inode);
		return;
	}

	if (ident != TAG_IDENT_FE && ident != TAG_IDENT_EFE &&
	    ident != TAG_IDENT_USE) {
		udf_err(inode->i_sb, "(ino %ld) failed ident=%d\n",
			inode->i_ino, ident);
		brelse(bh);
		make_bad_inode(inode);
		return;
	}

	fe = (struct fileEntry *)bh->b_data;

	if (fe->icbTag.strategyType == cpu_to_le16(4096)) {
		struct buffer_head *ibh;

		ibh = udf_read_ptagged(inode->i_sb, &iinfo->i_location, 1,
					&ident);
		if (ident == TAG_IDENT_IE && ibh) {
			struct buffer_head *nbh = NULL;
			struct kernel_lb_addr loc;
			struct indirectEntry *ie;

			ie = (struct indirectEntry *)ibh->b_data;
			loc = lelb_to_cpu(ie->indirectICB.extLocation);

			if (ie->indirectICB.extLength &&
				(nbh = udf_read_ptagged(inode->i_sb, &loc, 0,
							&ident))) {
				if (ident == TAG_IDENT_FE ||
					ident == TAG_IDENT_EFE) {
					memcpy(&iinfo->i_location,
						&loc,
						sizeof(struct kernel_lb_addr));
					brelse(bh);
					brelse(ibh);
					brelse(nbh);
					__udf_read_inode(inode);
					return;
				}
				brelse(nbh);
			}
		}
		brelse(ibh);
	} else if (fe->icbTag.strategyType != cpu_to_le16(4)) {
		udf_err(inode->i_sb, "unsupported strategy type: %d\n",
			le16_to_cpu(fe->icbTag.strategyType));
		brelse(bh);
		make_bad_inode(inode);
		return;
	}
	udf_fill_inode(inode, bh);

	brelse(bh);
}

static void udf_fill_inode(struct inode *inode, struct buffer_head *bh)
{
	struct fileEntry *fe;
	struct extendedFileEntry *efe;
	struct udf_sb_info *sbi = UDF_SB(inode->i_sb);
	struct udf_inode_info *iinfo = UDF_I(inode);
	unsigned int link_count;

	fe = (struct fileEntry *)bh->b_data;
	efe = (struct extendedFileEntry *)bh->b_data;

	if (fe->icbTag.strategyType == cpu_to_le16(4))
		iinfo->i_strat4096 = 0;
	else /* if (fe->icbTag.strategyType == cpu_to_le16(4096)) */
		iinfo->i_strat4096 = 1;

	iinfo->i_alloc_type = le16_to_cpu(fe->icbTag.flags) &
							ICBTAG_FLAG_AD_MASK;
	iinfo->i_unique = 0;
	iinfo->i_lenEAttr = 0;
	iinfo->i_lenExtents = 0;
	iinfo->i_lenAlloc = 0;
	iinfo->i_next_alloc_block = 0;
	iinfo->i_next_alloc_goal = 0;
	if (fe->descTag.tagIdent == cpu_to_le16(TAG_IDENT_EFE)) {
		iinfo->i_efe = 1;
		iinfo->i_use = 0;
		if (udf_alloc_i_data(inode, inode->i_sb->s_blocksize -
					sizeof(struct extendedFileEntry))) {
			make_bad_inode(inode);
			return;
		}
		memcpy(iinfo->i_ext.i_data,
		       bh->b_data + sizeof(struct extendedFileEntry),
		       inode->i_sb->s_blocksize -
					sizeof(struct extendedFileEntry));
	} else if (fe->descTag.tagIdent == cpu_to_le16(TAG_IDENT_FE)) {
		iinfo->i_efe = 0;
		iinfo->i_use = 0;
		if (udf_alloc_i_data(inode, inode->i_sb->s_blocksize -
						sizeof(struct fileEntry))) {
			make_bad_inode(inode);
			return;
		}
		memcpy(iinfo->i_ext.i_data,
		       bh->b_data + sizeof(struct fileEntry),
		       inode->i_sb->s_blocksize - sizeof(struct fileEntry));
	} else if (fe->descTag.tagIdent == cpu_to_le16(TAG_IDENT_USE)) {
		iinfo->i_efe = 0;
		iinfo->i_use = 1;
		iinfo->i_lenAlloc = le32_to_cpu(
				((struct unallocSpaceEntry *)bh->b_data)->
				 lengthAllocDescs);
		if (udf_alloc_i_data(inode, inode->i_sb->s_blocksize -
					sizeof(struct unallocSpaceEntry))) {
			make_bad_inode(inode);
			return;
		}
		memcpy(iinfo->i_ext.i_data,
		       bh->b_data + sizeof(struct unallocSpaceEntry),
		       inode->i_sb->s_blocksize -
					sizeof(struct unallocSpaceEntry));
		return;
	}

	read_lock(&sbi->s_cred_lock);
	inode->i_uid = le32_to_cpu(fe->uid);
	if (inode->i_uid == -1 ||
	    UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_UID_IGNORE) ||
	    UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_UID_SET))
		inode->i_uid = UDF_SB(inode->i_sb)->s_uid;

	inode->i_gid = le32_to_cpu(fe->gid);
	if (inode->i_gid == -1 ||
	    UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_GID_IGNORE) ||
	    UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_GID_SET))
		inode->i_gid = UDF_SB(inode->i_sb)->s_gid;

	if (fe->icbTag.fileType != ICBTAG_FILE_TYPE_DIRECTORY &&
			sbi->s_fmode != UDF_INVALID_MODE)
		inode->i_mode = sbi->s_fmode;
	else if (fe->icbTag.fileType == ICBTAG_FILE_TYPE_DIRECTORY &&
			sbi->s_dmode != UDF_INVALID_MODE)
		inode->i_mode = sbi->s_dmode;
	else
		inode->i_mode = udf_convert_permissions(fe);
	inode->i_mode &= ~sbi->s_umask;
	read_unlock(&sbi->s_cred_lock);

	link_count = le16_to_cpu(fe->fileLinkCount);
	if (!link_count)
		link_count = 1;
	set_nlink(inode, link_count);

	inode->i_size = le64_to_cpu(fe->informationLength);
	iinfo->i_lenExtents = inode->i_size;

	if (iinfo->i_efe == 0) {
		inode->i_blocks = le64_to_cpu(fe->logicalBlocksRecorded) <<
			(inode->i_sb->s_blocksize_bits - 9);

		if (!udf_disk_stamp_to_time(&inode->i_atime, fe->accessTime))
			inode->i_atime = sbi->s_record_time;

		if (!udf_disk_stamp_to_time(&inode->i_mtime,
					    fe->modificationTime))
			inode->i_mtime = sbi->s_record_time;

		if (!udf_disk_stamp_to_time(&inode->i_ctime, fe->attrTime))
			inode->i_ctime = sbi->s_record_time;

		iinfo->i_unique = le64_to_cpu(fe->uniqueID);
		iinfo->i_lenEAttr = le32_to_cpu(fe->lengthExtendedAttr);
		iinfo->i_lenAlloc = le32_to_cpu(fe->lengthAllocDescs);
		iinfo->i_checkpoint = le32_to_cpu(fe->checkpoint);
	} else {
		inode->i_blocks = le64_to_cpu(efe->logicalBlocksRecorded) <<
		    (inode->i_sb->s_blocksize_bits - 9);

		if (!udf_disk_stamp_to_time(&inode->i_atime, efe->accessTime))
			inode->i_atime = sbi->s_record_time;

		if (!udf_disk_stamp_to_time(&inode->i_mtime,
					    efe->modificationTime))
			inode->i_mtime = sbi->s_record_time;

		if (!udf_disk_stamp_to_time(&iinfo->i_crtime, efe->createTime))
			iinfo->i_crtime = sbi->s_record_time;

		if (!udf_disk_stamp_to_time(&inode->i_ctime, efe->attrTime))
			inode->i_ctime = sbi->s_record_time;

		iinfo->i_unique = le64_to_cpu(efe->uniqueID);
		iinfo->i_lenEAttr = le32_to_cpu(efe->lengthExtendedAttr);
		iinfo->i_lenAlloc = le32_to_cpu(efe->lengthAllocDescs);
		iinfo->i_checkpoint = le32_to_cpu(efe->checkpoint);
	}

	switch (fe->icbTag.fileType) {
	case ICBTAG_FILE_TYPE_DIRECTORY:
		inode->i_op = &udf_dir_inode_operations;
		inode->i_fop = &udf_dir_operations;
		inode->i_mode |= S_IFDIR;
		inc_nlink(inode);
		break;
	case ICBTAG_FILE_TYPE_REALTIME:
	case ICBTAG_FILE_TYPE_REGULAR:
	case ICBTAG_FILE_TYPE_UNDEF:
	case ICBTAG_FILE_TYPE_VAT20:
		if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
			inode->i_data.a_ops = &udf_adinicb_aops;
		else
			inode->i_data.a_ops = &udf_aops;
		inode->i_op = &udf_file_inode_operations;
		inode->i_fop = &udf_file_operations;
		inode->i_mode |= S_IFREG;
		break;
	case ICBTAG_FILE_TYPE_BLOCK:
		inode->i_mode |= S_IFBLK;
		break;
	case ICBTAG_FILE_TYPE_CHAR:
		inode->i_mode |= S_IFCHR;
		break;
	case ICBTAG_FILE_TYPE_FIFO:
		init_special_inode(inode, inode->i_mode | S_IFIFO, 0);
		break;
	case ICBTAG_FILE_TYPE_SOCKET:
		init_special_inode(inode, inode->i_mode | S_IFSOCK, 0);
		break;
	case ICBTAG_FILE_TYPE_SYMLINK:
		inode->i_data.a_ops = &udf_symlink_aops;
		inode->i_op = &udf_symlink_inode_operations;
		inode->i_mode = S_IFLNK | S_IRWXUGO;
		break;
	case ICBTAG_FILE_TYPE_MAIN:
		udf_debug("METADATA FILE-----\n");
		break;
	case ICBTAG_FILE_TYPE_MIRROR:
		udf_debug("METADATA MIRROR FILE-----\n");
		break;
	case ICBTAG_FILE_TYPE_BITMAP:
		udf_debug("METADATA BITMAP FILE-----\n");
		break;
	default:
		udf_err(inode->i_sb, "(ino %ld) failed unknown file type=%d\n",
			inode->i_ino, fe->icbTag.fileType);
		make_bad_inode(inode);
		return;
	}
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		struct deviceSpec *dsea =
			(struct deviceSpec *)udf_get_extendedattr(inode, 12, 1);
		if (dsea) {
			init_special_inode(inode, inode->i_mode,
				MKDEV(le32_to_cpu(dsea->majorDeviceIdent),
				      le32_to_cpu(dsea->minorDeviceIdent)));
			/* Developer ID ??? */
		} else
			make_bad_inode(inode);
	}
}

static int udf_alloc_i_data(struct inode *inode, size_t size)
{
	struct udf_inode_info *iinfo = UDF_I(inode);
	iinfo->i_ext.i_data = kmalloc(size, GFP_KERNEL);

	if (!iinfo->i_ext.i_data) {
		udf_err(inode->i_sb, "(ino %ld) no free memory\n",
			inode->i_ino);
		return -ENOMEM;
	}

	return 0;
}

static umode_t udf_convert_permissions(struct fileEntry *fe)
{
	umode_t mode;
	uint32_t permissions;
	uint32_t flags;

	permissions = le32_to_cpu(fe->permissions);
	flags = le16_to_cpu(fe->icbTag.flags);

	mode =	((permissions) & S_IRWXO) |
		((permissions >> 2) & S_IRWXG) |
		((permissions >> 4) & S_IRWXU) |
		((flags & ICBTAG_FLAG_SETUID) ? S_ISUID : 0) |
		((flags & ICBTAG_FLAG_SETGID) ? S_ISGID : 0) |
		((flags & ICBTAG_FLAG_STICKY) ? S_ISVTX : 0);

	return mode;
}

int udf_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return udf_update_inode(inode, wbc->sync_mode == WB_SYNC_ALL);
}

static int udf_sync_inode(struct inode *inode)
{
	return udf_update_inode(inode, 1);
}

static int udf_update_inode(struct inode *inode, int do_sync)
{
	struct buffer_head *bh = NULL;
	struct fileEntry *fe;
	struct extendedFileEntry *efe;
	uint64_t lb_recorded;
	uint32_t udfperms;
	uint16_t icbflags;
	uint16_t crclen;
	int err = 0;
	struct udf_sb_info *sbi = UDF_SB(inode->i_sb);
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;
	struct udf_inode_info *iinfo = UDF_I(inode);

	bh = udf_tgetblk(inode->i_sb,
			udf_get_lb_pblock(inode->i_sb, &iinfo->i_location, 0));
	if (!bh) {
		udf_debug("getblk failure\n");
		return -ENOMEM;
	}

	lock_buffer(bh);
	memset(bh->b_data, 0, inode->i_sb->s_blocksize);
	fe = (struct fileEntry *)bh->b_data;
	efe = (struct extendedFileEntry *)bh->b_data;

	if (iinfo->i_use) {
		struct unallocSpaceEntry *use =
			(struct unallocSpaceEntry *)bh->b_data;

		use->lengthAllocDescs = cpu_to_le32(iinfo->i_lenAlloc);
		memcpy(bh->b_data + sizeof(struct unallocSpaceEntry),
		       iinfo->i_ext.i_data, inode->i_sb->s_blocksize -
					sizeof(struct unallocSpaceEntry));
		use->descTag.tagIdent = cpu_to_le16(TAG_IDENT_USE);
		use->descTag.tagLocation =
				cpu_to_le32(iinfo->i_location.logicalBlockNum);
		crclen = sizeof(struct unallocSpaceEntry) +
				iinfo->i_lenAlloc - sizeof(struct tag);
		use->descTag.descCRCLength = cpu_to_le16(crclen);
		use->descTag.descCRC = cpu_to_le16(crc_itu_t(0, (char *)use +
							   sizeof(struct tag),
							   crclen));
		use->descTag.tagChecksum = udf_tag_checksum(&use->descTag);

		goto out;
	}

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_UID_FORGET))
		fe->uid = cpu_to_le32(-1);
	else
		fe->uid = cpu_to_le32(inode->i_uid);

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_GID_FORGET))
		fe->gid = cpu_to_le32(-1);
	else
		fe->gid = cpu_to_le32(inode->i_gid);

	udfperms = ((inode->i_mode & S_IRWXO)) |
		   ((inode->i_mode & S_IRWXG) << 2) |
		   ((inode->i_mode & S_IRWXU) << 4);

	udfperms |= (le32_to_cpu(fe->permissions) &
		    (FE_PERM_O_DELETE | FE_PERM_O_CHATTR |
		     FE_PERM_G_DELETE | FE_PERM_G_CHATTR |
		     FE_PERM_U_DELETE | FE_PERM_U_CHATTR));
	fe->permissions = cpu_to_le32(udfperms);

	if (S_ISDIR(inode->i_mode))
		fe->fileLinkCount = cpu_to_le16(inode->i_nlink - 1);
	else
		fe->fileLinkCount = cpu_to_le16(inode->i_nlink);

	fe->informationLength = cpu_to_le64(inode->i_size);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		struct regid *eid;
		struct deviceSpec *dsea =
			(struct deviceSpec *)udf_get_extendedattr(inode, 12, 1);
		if (!dsea) {
			dsea = (struct deviceSpec *)
				udf_add_extendedattr(inode,
						     sizeof(struct deviceSpec) +
						     sizeof(struct regid), 12, 0x3);
			dsea->attrType = cpu_to_le32(12);
			dsea->attrSubtype = 1;
			dsea->attrLength = cpu_to_le32(
						sizeof(struct deviceSpec) +
						sizeof(struct regid));
			dsea->impUseLength = cpu_to_le32(sizeof(struct regid));
		}
		eid = (struct regid *)dsea->impUse;
		memset(eid, 0, sizeof(struct regid));
		strcpy(eid->ident, UDF_ID_DEVELOPER);
		eid->identSuffix[0] = UDF_OS_CLASS_UNIX;
		eid->identSuffix[1] = UDF_OS_ID_LINUX;
		dsea->majorDeviceIdent = cpu_to_le32(imajor(inode));
		dsea->minorDeviceIdent = cpu_to_le32(iminor(inode));
	}

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
		lb_recorded = 0; /* No extents => no blocks! */
	else
		lb_recorded =
			(inode->i_blocks + (1 << (blocksize_bits - 9)) - 1) >>
			(blocksize_bits - 9);

	if (iinfo->i_efe == 0) {
		memcpy(bh->b_data + sizeof(struct fileEntry),
		       iinfo->i_ext.i_data,
		       inode->i_sb->s_blocksize - sizeof(struct fileEntry));
		fe->logicalBlocksRecorded = cpu_to_le64(lb_recorded);

		udf_time_to_disk_stamp(&fe->accessTime, inode->i_atime);
		udf_time_to_disk_stamp(&fe->modificationTime, inode->i_mtime);
		udf_time_to_disk_stamp(&fe->attrTime, inode->i_ctime);
		memset(&(fe->impIdent), 0, sizeof(struct regid));
		strcpy(fe->impIdent.ident, UDF_ID_DEVELOPER);
		fe->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
		fe->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
		fe->uniqueID = cpu_to_le64(iinfo->i_unique);
		fe->lengthExtendedAttr = cpu_to_le32(iinfo->i_lenEAttr);
		fe->lengthAllocDescs = cpu_to_le32(iinfo->i_lenAlloc);
		fe->checkpoint = cpu_to_le32(iinfo->i_checkpoint);
		fe->descTag.tagIdent = cpu_to_le16(TAG_IDENT_FE);
		crclen = sizeof(struct fileEntry);
	} else {
		memcpy(bh->b_data + sizeof(struct extendedFileEntry),
		       iinfo->i_ext.i_data,
		       inode->i_sb->s_blocksize -
					sizeof(struct extendedFileEntry));
		efe->objectSize = cpu_to_le64(inode->i_size);
		efe->logicalBlocksRecorded = cpu_to_le64(lb_recorded);

		if (iinfo->i_crtime.tv_sec > inode->i_atime.tv_sec ||
		    (iinfo->i_crtime.tv_sec == inode->i_atime.tv_sec &&
		     iinfo->i_crtime.tv_nsec > inode->i_atime.tv_nsec))
			iinfo->i_crtime = inode->i_atime;

		if (iinfo->i_crtime.tv_sec > inode->i_mtime.tv_sec ||
		    (iinfo->i_crtime.tv_sec == inode->i_mtime.tv_sec &&
		     iinfo->i_crtime.tv_nsec > inode->i_mtime.tv_nsec))
			iinfo->i_crtime = inode->i_mtime;

		if (iinfo->i_crtime.tv_sec > inode->i_ctime.tv_sec ||
		    (iinfo->i_crtime.tv_sec == inode->i_ctime.tv_sec &&
		     iinfo->i_crtime.tv_nsec > inode->i_ctime.tv_nsec))
			iinfo->i_crtime = inode->i_ctime;

		udf_time_to_disk_stamp(&efe->accessTime, inode->i_atime);
		udf_time_to_disk_stamp(&efe->modificationTime, inode->i_mtime);
		udf_time_to_disk_stamp(&efe->createTime, iinfo->i_crtime);
		udf_time_to_disk_stamp(&efe->attrTime, inode->i_ctime);

		memset(&(efe->impIdent), 0, sizeof(struct regid));
		strcpy(efe->impIdent.ident, UDF_ID_DEVELOPER);
		efe->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
		efe->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
		efe->uniqueID = cpu_to_le64(iinfo->i_unique);
		efe->lengthExtendedAttr = cpu_to_le32(iinfo->i_lenEAttr);
		efe->lengthAllocDescs = cpu_to_le32(iinfo->i_lenAlloc);
		efe->checkpoint = cpu_to_le32(iinfo->i_checkpoint);
		efe->descTag.tagIdent = cpu_to_le16(TAG_IDENT_EFE);
		crclen = sizeof(struct extendedFileEntry);
	}
	if (iinfo->i_strat4096) {
		fe->icbTag.strategyType = cpu_to_le16(4096);
		fe->icbTag.strategyParameter = cpu_to_le16(1);
		fe->icbTag.numEntries = cpu_to_le16(2);
	} else {
		fe->icbTag.strategyType = cpu_to_le16(4);
		fe->icbTag.numEntries = cpu_to_le16(1);
	}

	if (S_ISDIR(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_DIRECTORY;
	else if (S_ISREG(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_REGULAR;
	else if (S_ISLNK(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_SYMLINK;
	else if (S_ISBLK(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_BLOCK;
	else if (S_ISCHR(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_CHAR;
	else if (S_ISFIFO(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_FIFO;
	else if (S_ISSOCK(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_SOCKET;

	icbflags =	iinfo->i_alloc_type |
			((inode->i_mode & S_ISUID) ? ICBTAG_FLAG_SETUID : 0) |
			((inode->i_mode & S_ISGID) ? ICBTAG_FLAG_SETGID : 0) |
			((inode->i_mode & S_ISVTX) ? ICBTAG_FLAG_STICKY : 0) |
			(le16_to_cpu(fe->icbTag.flags) &
				~(ICBTAG_FLAG_AD_MASK | ICBTAG_FLAG_SETUID |
				ICBTAG_FLAG_SETGID | ICBTAG_FLAG_STICKY));

	fe->icbTag.flags = cpu_to_le16(icbflags);
	if (sbi->s_udfrev >= 0x0200)
		fe->descTag.descVersion = cpu_to_le16(3);
	else
		fe->descTag.descVersion = cpu_to_le16(2);
	fe->descTag.tagSerialNum = cpu_to_le16(sbi->s_serial_number);
	fe->descTag.tagLocation = cpu_to_le32(
					iinfo->i_location.logicalBlockNum);
	crclen += iinfo->i_lenEAttr + iinfo->i_lenAlloc - sizeof(struct tag);
	fe->descTag.descCRCLength = cpu_to_le16(crclen);
	fe->descTag.descCRC = cpu_to_le16(crc_itu_t(0, (char *)fe + sizeof(struct tag),
						  crclen));
	fe->descTag.tagChecksum = udf_tag_checksum(&fe->descTag);

out:
	set_buffer_uptodate(bh);
	unlock_buffer(bh);

	/* write the data blocks */
	mark_buffer_dirty(bh);
	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_write_io_error(bh)) {
			udf_warn(inode->i_sb, "IO error syncing udf inode [%08lx]\n",
				 inode->i_ino);
			err = -EIO;
		}
	}
	brelse(bh);

	return err;
}

struct inode *udf_iget(struct super_block *sb, struct kernel_lb_addr *ino)
{
	unsigned long block = udf_get_lb_pblock(sb, ino, 0);
	struct inode *inode = iget_locked(sb, block);

	if (!inode)
		return NULL;

	if (inode->i_state & I_NEW) {
		memcpy(&UDF_I(inode)->i_location, ino, sizeof(struct kernel_lb_addr));
		__udf_read_inode(inode);
		unlock_new_inode(inode);
	}

	if (is_bad_inode(inode))
		goto out_iput;

	if (ino->logicalBlockNum >= UDF_SB(sb)->
			s_partmaps[ino->partitionReferenceNum].s_partition_len) {
		udf_debug("block=%d, partition=%d out of range\n",
			  ino->logicalBlockNum, ino->partitionReferenceNum);
		make_bad_inode(inode);
		goto out_iput;
	}

	return inode;

 out_iput:
	iput(inode);
	return NULL;
}

int udf_add_aext(struct inode *inode, struct extent_position *epos,
		 struct kernel_lb_addr *eloc, uint32_t elen, int inc)
{
	int adsize;
	struct short_ad *sad = NULL;
	struct long_ad *lad = NULL;
	struct allocExtDesc *aed;
	uint8_t *ptr;
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (!epos->bh)
		ptr = iinfo->i_ext.i_data + epos->offset -
			udf_file_entry_alloc_offset(inode) +
			iinfo->i_lenEAttr;
	else
		ptr = epos->bh->b_data + epos->offset;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		return -EIO;

	if (epos->offset + (2 * adsize) > inode->i_sb->s_blocksize) {
		unsigned char *sptr, *dptr;
		struct buffer_head *nbh;
		int err, loffset;
		struct kernel_lb_addr obloc = epos->block;

		epos->block.logicalBlockNum = udf_new_block(inode->i_sb, NULL,
						obloc.partitionReferenceNum,
						obloc.logicalBlockNum, &err);
		if (!epos->block.logicalBlockNum)
			return -ENOSPC;
		nbh = udf_tgetblk(inode->i_sb, udf_get_lb_pblock(inode->i_sb,
								 &epos->block,
								 0));
		if (!nbh)
			return -EIO;
		lock_buffer(nbh);
		memset(nbh->b_data, 0x00, inode->i_sb->s_blocksize);
		set_buffer_uptodate(nbh);
		unlock_buffer(nbh);
		mark_buffer_dirty_inode(nbh, inode);

		aed = (struct allocExtDesc *)(nbh->b_data);
		if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT))
			aed->previousAllocExtLocation =
					cpu_to_le32(obloc.logicalBlockNum);
		if (epos->offset + adsize > inode->i_sb->s_blocksize) {
			loffset = epos->offset;
			aed->lengthAllocDescs = cpu_to_le32(adsize);
			sptr = ptr - adsize;
			dptr = nbh->b_data + sizeof(struct allocExtDesc);
			memcpy(dptr, sptr, adsize);
			epos->offset = sizeof(struct allocExtDesc) + adsize;
		} else {
			loffset = epos->offset + adsize;
			aed->lengthAllocDescs = cpu_to_le32(0);
			sptr = ptr;
			epos->offset = sizeof(struct allocExtDesc);

			if (epos->bh) {
				aed = (struct allocExtDesc *)epos->bh->b_data;
				le32_add_cpu(&aed->lengthAllocDescs, adsize);
			} else {
				iinfo->i_lenAlloc += adsize;
				mark_inode_dirty(inode);
			}
		}
		if (UDF_SB(inode->i_sb)->s_udfrev >= 0x0200)
			udf_new_tag(nbh->b_data, TAG_IDENT_AED, 3, 1,
				    epos->block.logicalBlockNum, sizeof(struct tag));
		else
			udf_new_tag(nbh->b_data, TAG_IDENT_AED, 2, 1,
				    epos->block.logicalBlockNum, sizeof(struct tag));
		switch (iinfo->i_alloc_type) {
		case ICBTAG_FLAG_AD_SHORT:
			sad = (struct short_ad *)sptr;
			sad->extLength = cpu_to_le32(EXT_NEXT_EXTENT_ALLOCDECS |
						     inode->i_sb->s_blocksize);
			sad->extPosition =
				cpu_to_le32(epos->block.logicalBlockNum);
			break;
		case ICBTAG_FLAG_AD_LONG:
			lad = (struct long_ad *)sptr;
			lad->extLength = cpu_to_le32(EXT_NEXT_EXTENT_ALLOCDECS |
						     inode->i_sb->s_blocksize);
			lad->extLocation = cpu_to_lelb(epos->block);
			memset(lad->impUse, 0x00, sizeof(lad->impUse));
			break;
		}
		if (epos->bh) {
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
			    UDF_SB(inode->i_sb)->s_udfrev >= 0x0201)
				udf_update_tag(epos->bh->b_data, loffset);
			else
				udf_update_tag(epos->bh->b_data,
						sizeof(struct allocExtDesc));
			mark_buffer_dirty_inode(epos->bh, inode);
			brelse(epos->bh);
		} else {
			mark_inode_dirty(inode);
		}
		epos->bh = nbh;
	}

	udf_write_aext(inode, epos, eloc, elen, inc);

	if (!epos->bh) {
		iinfo->i_lenAlloc += adsize;
		mark_inode_dirty(inode);
	} else {
		aed = (struct allocExtDesc *)epos->bh->b_data;
		le32_add_cpu(&aed->lengthAllocDescs, adsize);
		if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
				UDF_SB(inode->i_sb)->s_udfrev >= 0x0201)
			udf_update_tag(epos->bh->b_data,
					epos->offset + (inc ? 0 : adsize));
		else
			udf_update_tag(epos->bh->b_data,
					sizeof(struct allocExtDesc));
		mark_buffer_dirty_inode(epos->bh, inode);
	}

	return 0;
}

void udf_write_aext(struct inode *inode, struct extent_position *epos,
		    struct kernel_lb_addr *eloc, uint32_t elen, int inc)
{
	int adsize;
	uint8_t *ptr;
	struct short_ad *sad;
	struct long_ad *lad;
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (!epos->bh)
		ptr = iinfo->i_ext.i_data + epos->offset -
			udf_file_entry_alloc_offset(inode) +
			iinfo->i_lenEAttr;
	else
		ptr = epos->bh->b_data + epos->offset;

	switch (iinfo->i_alloc_type) {
	case ICBTAG_FLAG_AD_SHORT:
		sad = (struct short_ad *)ptr;
		sad->extLength = cpu_to_le32(elen);
		sad->extPosition = cpu_to_le32(eloc->logicalBlockNum);
		adsize = sizeof(struct short_ad);
		break;
	case ICBTAG_FLAG_AD_LONG:
		lad = (struct long_ad *)ptr;
		lad->extLength = cpu_to_le32(elen);
		lad->extLocation = cpu_to_lelb(*eloc);
		memset(lad->impUse, 0x00, sizeof(lad->impUse));
		adsize = sizeof(struct long_ad);
		break;
	default:
		return;
	}

	if (epos->bh) {
		if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
		    UDF_SB(inode->i_sb)->s_udfrev >= 0x0201) {
			struct allocExtDesc *aed =
				(struct allocExtDesc *)epos->bh->b_data;
			udf_update_tag(epos->bh->b_data,
				       le32_to_cpu(aed->lengthAllocDescs) +
				       sizeof(struct allocExtDesc));
		}
		mark_buffer_dirty_inode(epos->bh, inode);
	} else {
		mark_inode_dirty(inode);
	}

	if (inc)
		epos->offset += adsize;
}

int8_t udf_next_aext(struct inode *inode, struct extent_position *epos,
		     struct kernel_lb_addr *eloc, uint32_t *elen, int inc)
{
	int8_t etype;

	while ((etype = udf_current_aext(inode, epos, eloc, elen, inc)) ==
	       (EXT_NEXT_EXTENT_ALLOCDECS >> 30)) {
		int block;
		epos->block = *eloc;
		epos->offset = sizeof(struct allocExtDesc);
		brelse(epos->bh);
		block = udf_get_lb_pblock(inode->i_sb, &epos->block, 0);
		epos->bh = udf_tread(inode->i_sb, block);
		if (!epos->bh) {
			udf_debug("reading block %d failed!\n", block);
			return -1;
		}
	}

	return etype;
}

int8_t udf_current_aext(struct inode *inode, struct extent_position *epos,
			struct kernel_lb_addr *eloc, uint32_t *elen, int inc)
{
	int alen;
	int8_t etype;
	uint8_t *ptr;
	struct short_ad *sad;
	struct long_ad *lad;
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (!epos->bh) {
		if (!epos->offset)
			epos->offset = udf_file_entry_alloc_offset(inode);
		ptr = iinfo->i_ext.i_data + epos->offset -
			udf_file_entry_alloc_offset(inode) +
			iinfo->i_lenEAttr;
		alen = udf_file_entry_alloc_offset(inode) +
							iinfo->i_lenAlloc;
	} else {
		if (!epos->offset)
			epos->offset = sizeof(struct allocExtDesc);
		ptr = epos->bh->b_data + epos->offset;
		alen = sizeof(struct allocExtDesc) +
			le32_to_cpu(((struct allocExtDesc *)epos->bh->b_data)->
							lengthAllocDescs);
	}

	switch (iinfo->i_alloc_type) {
	case ICBTAG_FLAG_AD_SHORT:
		sad = udf_get_fileshortad(ptr, alen, &epos->offset, inc);
		if (!sad)
			return -1;
		etype = le32_to_cpu(sad->extLength) >> 30;
		eloc->logicalBlockNum = le32_to_cpu(sad->extPosition);
		eloc->partitionReferenceNum =
				iinfo->i_location.partitionReferenceNum;
		*elen = le32_to_cpu(sad->extLength) & UDF_EXTENT_LENGTH_MASK;
		break;
	case ICBTAG_FLAG_AD_LONG:
		lad = udf_get_filelongad(ptr, alen, &epos->offset, inc);
		if (!lad)
			return -1;
		etype = le32_to_cpu(lad->extLength) >> 30;
		*eloc = lelb_to_cpu(lad->extLocation);
		*elen = le32_to_cpu(lad->extLength) & UDF_EXTENT_LENGTH_MASK;
		break;
	default:
		udf_debug("alloc_type = %d unsupported\n", iinfo->i_alloc_type);
		return -1;
	}

	return etype;
}

static int8_t udf_insert_aext(struct inode *inode, struct extent_position epos,
			      struct kernel_lb_addr neloc, uint32_t nelen)
{
	struct kernel_lb_addr oeloc;
	uint32_t oelen;
	int8_t etype;

	if (epos.bh)
		get_bh(epos.bh);

	while ((etype = udf_next_aext(inode, &epos, &oeloc, &oelen, 0)) != -1) {
		udf_write_aext(inode, &epos, &neloc, nelen, 1);
		neloc = oeloc;
		nelen = (etype << 30) | oelen;
	}
	udf_add_aext(inode, &epos, &neloc, nelen, 1);
	brelse(epos.bh);

	return (nelen >> 30);
}

int8_t udf_delete_aext(struct inode *inode, struct extent_position epos,
		       struct kernel_lb_addr eloc, uint32_t elen)
{
	struct extent_position oepos;
	int adsize;
	int8_t etype;
	struct allocExtDesc *aed;
	struct udf_inode_info *iinfo;

	if (epos.bh) {
		get_bh(epos.bh);
		get_bh(epos.bh);
	}

	iinfo = UDF_I(inode);
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		adsize = 0;

	oepos = epos;
	if (udf_next_aext(inode, &epos, &eloc, &elen, 1) == -1)
		return -1;

	while ((etype = udf_next_aext(inode, &epos, &eloc, &elen, 1)) != -1) {
		udf_write_aext(inode, &oepos, &eloc, (etype << 30) | elen, 1);
		if (oepos.bh != epos.bh) {
			oepos.block = epos.block;
			brelse(oepos.bh);
			get_bh(epos.bh);
			oepos.bh = epos.bh;
			oepos.offset = epos.offset - adsize;
		}
	}
	memset(&eloc, 0x00, sizeof(struct kernel_lb_addr));
	elen = 0;

	if (epos.bh != oepos.bh) {
		udf_free_blocks(inode->i_sb, inode, &epos.block, 0, 1);
		udf_write_aext(inode, &oepos, &eloc, elen, 1);
		udf_write_aext(inode, &oepos, &eloc, elen, 1);
		if (!oepos.bh) {
			iinfo->i_lenAlloc -= (adsize * 2);
			mark_inode_dirty(inode);
		} else {
			aed = (struct allocExtDesc *)oepos.bh->b_data;
			le32_add_cpu(&aed->lengthAllocDescs, -(2 * adsize));
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
			    UDF_SB(inode->i_sb)->s_udfrev >= 0x0201)
				udf_update_tag(oepos.bh->b_data,
						oepos.offset - (2 * adsize));
			else
				udf_update_tag(oepos.bh->b_data,
						sizeof(struct allocExtDesc));
			mark_buffer_dirty_inode(oepos.bh, inode);
		}
	} else {
		udf_write_aext(inode, &oepos, &eloc, elen, 1);
		if (!oepos.bh) {
			iinfo->i_lenAlloc -= adsize;
			mark_inode_dirty(inode);
		} else {
			aed = (struct allocExtDesc *)oepos.bh->b_data;
			le32_add_cpu(&aed->lengthAllocDescs, -adsize);
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
			    UDF_SB(inode->i_sb)->s_udfrev >= 0x0201)
				udf_update_tag(oepos.bh->b_data,
						epos.offset - adsize);
			else
				udf_update_tag(oepos.bh->b_data,
						sizeof(struct allocExtDesc));
			mark_buffer_dirty_inode(oepos.bh, inode);
		}
	}

	brelse(epos.bh);
	brelse(oepos.bh);

	return (elen >> 30);
}

int8_t inode_bmap(struct inode *inode, sector_t block,
		  struct extent_position *pos, struct kernel_lb_addr *eloc,
		  uint32_t *elen, sector_t *offset)
{
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;
	loff_t lbcount = 0, bcount =
	    (loff_t) block << blocksize_bits;
	int8_t etype;
	struct udf_inode_info *iinfo;

	iinfo = UDF_I(inode);
	pos->offset = 0;
	pos->block = iinfo->i_location;
	pos->bh = NULL;
	*elen = 0;

	do {
		etype = udf_next_aext(inode, pos, eloc, elen, 1);
		if (etype == -1) {
			*offset = (bcount - lbcount) >> blocksize_bits;
			iinfo->i_lenExtents = lbcount;
			return -1;
		}
		lbcount += *elen;
	} while (lbcount <= bcount);

	*offset = (bcount + *elen - lbcount) >> blocksize_bits;

	return etype;
}

long udf_block_map(struct inode *inode, sector_t block)
{
	struct kernel_lb_addr eloc;
	uint32_t elen;
	sector_t offset;
	struct extent_position epos = {};
	int ret;

	down_read(&UDF_I(inode)->i_data_sem);

	if (inode_bmap(inode, block, &epos, &eloc, &elen, &offset) ==
						(EXT_RECORDED_ALLOCATED >> 30))
		ret = udf_get_lb_pblock(inode->i_sb, &eloc, offset);
	else
		ret = 0;

	up_read(&UDF_I(inode)->i_data_sem);
	brelse(epos.bh);

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_VARCONV))
		return udf_fixed_to_variable(ret);
	else
		return ret;
}
