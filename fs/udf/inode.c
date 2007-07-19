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
 *  12/06/98 blf  partition support in udf_iget, udf_block_map and udf_read_inode
 *  12/12/98      rewrote udf_block_map to handle next extents and descs across
 *                block boundaries (which is not actually allowed)
 *  12/20/98      added support for strategy 4096
 *  03/07/99      rewrote udf_block_map (again)
 *                New funcs, inode_bmap, udf_next_aext
 *  04/19/99      Support for writing device EA's for major/minor #
 */

#include "udfdecl.h"
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/slab.h>

#include "udf_i.h"
#include "udf_sb.h"

MODULE_AUTHOR("Ben Fennema");
MODULE_DESCRIPTION("Universal Disk Format Filesystem");
MODULE_LICENSE("GPL");

#define EXTENT_MERGE_SIZE 5

static mode_t udf_convert_permissions(struct fileEntry *);
static int udf_update_inode(struct inode *, int);
static void udf_fill_inode(struct inode *, struct buffer_head *);
static int udf_alloc_i_data(struct inode *inode, size_t size);
static struct buffer_head *inode_getblk(struct inode *, sector_t, int *,
					long *, int *);
static int8_t udf_insert_aext(struct inode *, struct extent_position,
			      kernel_lb_addr, uint32_t);
static void udf_split_extents(struct inode *, int *, int, int,
			      kernel_long_ad[EXTENT_MERGE_SIZE], int *);
static void udf_prealloc_extents(struct inode *, int, int,
				 kernel_long_ad[EXTENT_MERGE_SIZE], int *);
static void udf_merge_extents(struct inode *,
			      kernel_long_ad[EXTENT_MERGE_SIZE], int *);
static void udf_update_extents(struct inode *,
			       kernel_long_ad[EXTENT_MERGE_SIZE], int, int,
			       struct extent_position *);
static int udf_get_block(struct inode *, sector_t, struct buffer_head *, int);

/*
 * udf_delete_inode
 *
 * PURPOSE
 *	Clean-up before the specified inode is destroyed.
 *
 * DESCRIPTION
 *	This routine is called when the kernel destroys an inode structure
 *	ie. when iput() finds i_count == 0.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 *
 *  Called at the last iput() if i_nlink is zero.
 */
void udf_delete_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);

	if (is_bad_inode(inode))
		goto no_delete;

	inode->i_size = 0;
	udf_truncate(inode);
	lock_kernel();

	udf_update_inode(inode, IS_SYNC(inode));
	udf_free_inode(inode);

	unlock_kernel();
	return;
      no_delete:
	clear_inode(inode);
}

/*
 * If we are going to release inode from memory, we discard preallocation and
 * truncate last inode extent to proper length. We could use drop_inode() but
 * it's called under inode_lock and thus we cannot mark inode dirty there.  We
 * use clear_inode() but we have to make sure to write inode as it's not written
 * automatically.
 */
void udf_clear_inode(struct inode *inode)
{
	if (!(inode->i_sb->s_flags & MS_RDONLY)) {
		lock_kernel();
		/* Discard preallocation for directories, symlinks, etc. */
		udf_discard_prealloc(inode);
		udf_truncate_tail_extent(inode);
		unlock_kernel();
		write_inode_now(inode, 1);
	}
	kfree(UDF_I_DATA(inode));
	UDF_I_DATA(inode) = NULL;
}

static int udf_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, udf_get_block, wbc);
}

static int udf_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, udf_get_block);
}

static int udf_prepare_write(struct file *file, struct page *page,
			     unsigned from, unsigned to)
{
	return block_prepare_write(page, from, to, udf_get_block);
}

static sector_t udf_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, udf_get_block);
}

const struct address_space_operations udf_aops = {
	.readpage = udf_readpage,
	.writepage = udf_writepage,
	.sync_page = block_sync_page,
	.prepare_write = udf_prepare_write,
	.commit_write = generic_commit_write,
	.bmap = udf_bmap,
};

void udf_expand_file_adinicb(struct inode *inode, int newsize, int *err)
{
	struct page *page;
	char *kaddr;
	struct writeback_control udf_wbc = {
		.sync_mode = WB_SYNC_NONE,
		.nr_to_write = 1,
	};

	/* from now on we have normal address_space methods */
	inode->i_data.a_ops = &udf_aops;

	if (!UDF_I_LENALLOC(inode)) {
		if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
			UDF_I_ALLOCTYPE(inode) = ICBTAG_FLAG_AD_SHORT;
		else
			UDF_I_ALLOCTYPE(inode) = ICBTAG_FLAG_AD_LONG;
		mark_inode_dirty(inode);
		return;
	}

	page = grab_cache_page(inode->i_mapping, 0);
	BUG_ON(!PageLocked(page));

	if (!PageUptodate(page)) {
		kaddr = kmap(page);
		memset(kaddr + UDF_I_LENALLOC(inode), 0x00,
		       PAGE_CACHE_SIZE - UDF_I_LENALLOC(inode));
		memcpy(kaddr, UDF_I_DATA(inode) + UDF_I_LENEATTR(inode),
		       UDF_I_LENALLOC(inode));
		flush_dcache_page(page);
		SetPageUptodate(page);
		kunmap(page);
	}
	memset(UDF_I_DATA(inode) + UDF_I_LENEATTR(inode), 0x00,
	       UDF_I_LENALLOC(inode));
	UDF_I_LENALLOC(inode) = 0;
	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
		UDF_I_ALLOCTYPE(inode) = ICBTAG_FLAG_AD_SHORT;
	else
		UDF_I_ALLOCTYPE(inode) = ICBTAG_FLAG_AD_LONG;

	inode->i_data.a_ops->writepage(page, &udf_wbc);
	page_cache_release(page);

	mark_inode_dirty(inode);
}

struct buffer_head *udf_expand_dir_adinicb(struct inode *inode, int *block,
					   int *err)
{
	int newblock;
	struct buffer_head *dbh = NULL;
	kernel_lb_addr eloc;
	uint32_t elen;
	uint8_t alloctype;
	struct extent_position epos;

	struct udf_fileident_bh sfibh, dfibh;
	loff_t f_pos = udf_ext0_offset(inode) >> 2;
	int size = (udf_ext0_offset(inode) + inode->i_size) >> 2;
	struct fileIdentDesc cfi, *sfi, *dfi;

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
		alloctype = ICBTAG_FLAG_AD_SHORT;
	else
		alloctype = ICBTAG_FLAG_AD_LONG;

	if (!inode->i_size) {
		UDF_I_ALLOCTYPE(inode) = alloctype;
		mark_inode_dirty(inode);
		return NULL;
	}

	/* alloc block, and copy data to it */
	*block = udf_new_block(inode->i_sb, inode,
			       UDF_I_LOCATION(inode).partitionReferenceNum,
			       UDF_I_LOCATION(inode).logicalBlockNum, err);

	if (!(*block))
		return NULL;
	newblock = udf_get_pblock(inode->i_sb, *block,
				  UDF_I_LOCATION(inode).partitionReferenceNum,
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
	    (f_pos & ((inode->i_sb->s_blocksize - 1) >> 2)) << 2;
	sfibh.sbh = sfibh.ebh = NULL;
	dfibh.soffset = dfibh.eoffset = 0;
	dfibh.sbh = dfibh.ebh = dbh;
	while ((f_pos < size)) {
		UDF_I_ALLOCTYPE(inode) = ICBTAG_FLAG_AD_IN_ICB;
		sfi =
		    udf_fileident_read(inode, &f_pos, &sfibh, &cfi, NULL, NULL,
				       NULL, NULL);
		if (!sfi) {
			brelse(dbh);
			return NULL;
		}
		UDF_I_ALLOCTYPE(inode) = alloctype;
		sfi->descTag.tagLocation = cpu_to_le32(*block);
		dfibh.soffset = dfibh.eoffset;
		dfibh.eoffset += (sfibh.eoffset - sfibh.soffset);
		dfi = (struct fileIdentDesc *)(dbh->b_data + dfibh.soffset);
		if (udf_write_fi(inode, sfi, dfi, &dfibh, sfi->impUse,
				 sfi->fileIdent +
				 le16_to_cpu(sfi->lengthOfImpUse))) {
			UDF_I_ALLOCTYPE(inode) = ICBTAG_FLAG_AD_IN_ICB;
			brelse(dbh);
			return NULL;
		}
	}
	mark_buffer_dirty_inode(dbh, inode);

	memset(UDF_I_DATA(inode) + UDF_I_LENEATTR(inode), 0,
	       UDF_I_LENALLOC(inode));
	UDF_I_LENALLOC(inode) = 0;
	eloc.logicalBlockNum = *block;
	eloc.partitionReferenceNum =
	    UDF_I_LOCATION(inode).partitionReferenceNum;
	elen = inode->i_size;
	UDF_I_LENEXTENTS(inode) = elen;
	epos.bh = NULL;
	epos.block = UDF_I_LOCATION(inode);
	epos.offset = udf_file_entry_alloc_offset(inode);
	udf_add_aext(inode, &epos, eloc, elen, 0);
	/* UniqueID stuff */

	brelse(epos.bh);
	mark_inode_dirty(inode);
	return dbh;
}

static int udf_get_block(struct inode *inode, sector_t block,
			 struct buffer_head *bh_result, int create)
{
	int err, new;
	struct buffer_head *bh;
	unsigned long phys;

	if (!create) {
		phys = udf_block_map(inode, block);
		if (phys)
			map_bh(bh_result, inode->i_sb, phys);
		return 0;
	}

	err = -EIO;
	new = 0;
	bh = NULL;

	lock_kernel();

	if (block < 0)
		goto abort_negative;

	if (block == UDF_I_NEXT_ALLOC_BLOCK(inode) + 1) {
		UDF_I_NEXT_ALLOC_BLOCK(inode)++;
		UDF_I_NEXT_ALLOC_GOAL(inode)++;
	}

	err = 0;

	bh = inode_getblk(inode, block, &err, &phys, &new);
	BUG_ON(bh);
	if (err)
		goto abort;
	BUG_ON(!phys);

	if (new)
		set_buffer_new(bh_result);
	map_bh(bh_result, inode->i_sb, phys);
      abort:
	unlock_kernel();
	return err;

      abort_negative:
	udf_warning(inode->i_sb, "udf_get_block", "block < 0");
	goto abort;
}

static struct buffer_head *udf_getblk(struct inode *inode, long block,
				      int create, int *err)
{
	struct buffer_head dummy;

	dummy.b_state = 0;
	dummy.b_blocknr = -1000;
	*err = udf_get_block(inode, block, &dummy, create);
	if (!*err && buffer_mapped(&dummy)) {
		struct buffer_head *bh;
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
int udf_extend_file(struct inode *inode, struct extent_position *last_pos,
		    kernel_long_ad * last_ext, sector_t blocks)
{
	sector_t add;
	int count = 0, fake = !(last_ext->extLength & UDF_EXTENT_LENGTH_MASK);
	struct super_block *sb = inode->i_sb;
	kernel_lb_addr prealloc_loc = { 0, 0 };
	int prealloc_len = 0;

	/* The previous extent is fake and we should not extend by anything
	 * - there's nothing to do... */
	if (!blocks && fake)
		return 0;
	/* Round the last extent up to a multiple of block size */
	if (last_ext->extLength & (sb->s_blocksize - 1)) {
		last_ext->extLength =
		    (last_ext->extLength & UDF_EXTENT_FLAG_MASK) |
		    (((last_ext->extLength & UDF_EXTENT_LENGTH_MASK) +
		      sb->s_blocksize - 1) & ~(sb->s_blocksize - 1));
		UDF_I_LENEXTENTS(inode) =
		    (UDF_I_LENEXTENTS(inode) + sb->s_blocksize - 1) &
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
		add =
		    ((1 << 30) - sb->s_blocksize -
		     (last_ext->extLength & UDF_EXTENT_LENGTH_MASK)) >> sb->
		    s_blocksize_bits;
		if (add > blocks)
			add = blocks;
		blocks -= add;
		last_ext->extLength += add << sb->s_blocksize_bits;
	}

	if (fake) {
		udf_add_aext(inode, last_pos, last_ext->extLocation,
			     last_ext->extLength, 1);
		count++;
	} else
		udf_write_aext(inode, last_pos, last_ext->extLocation,
			       last_ext->extLength, 1);
	/* Managed to do everything necessary? */
	if (!blocks)
		goto out;

	/* All further extents will be NOT_RECORDED_NOT_ALLOCATED */
	last_ext->extLocation.logicalBlockNum = 0;
	last_ext->extLocation.partitionReferenceNum = 0;
	add = (1 << (30 - sb->s_blocksize_bits)) - 1;
	last_ext->extLength =
	    EXT_NOT_RECORDED_NOT_ALLOCATED | (add << sb->s_blocksize_bits);
	/* Create enough extents to cover the whole hole */
	while (blocks > add) {
		blocks -= add;
		if (udf_add_aext(inode, last_pos, last_ext->extLocation,
				 last_ext->extLength, 1) == -1)
			return -1;
		count++;
	}
	if (blocks) {
		last_ext->extLength = EXT_NOT_RECORDED_NOT_ALLOCATED |
		    (blocks << sb->s_blocksize_bits);
		if (udf_add_aext(inode, last_pos, last_ext->extLocation,
				 last_ext->extLength, 1) == -1)
			return -1;
		count++;
	}
      out:
	/* Do we have some preallocated blocks saved? */
	if (prealloc_len) {
		if (udf_add_aext(inode, last_pos, prealloc_loc, prealloc_len, 1)
		    == -1)
			return -1;
		last_ext->extLocation = prealloc_loc;
		last_ext->extLength = prealloc_len;
		count++;
	}
	/* last_pos should point to the last written extent... */
	if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_SHORT)
		last_pos->offset -= sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_LONG)
		last_pos->offset -= sizeof(long_ad);
	else
		return -1;
	return count;
}

static struct buffer_head *inode_getblk(struct inode *inode, sector_t block,
					int *err, long *phys, int *new)
{
	static sector_t last_block;
	struct buffer_head *result = NULL;
	kernel_long_ad laarr[EXTENT_MERGE_SIZE];
	struct extent_position prev_epos, cur_epos, next_epos;
	int count = 0, startnum = 0, endnum = 0;
	uint32_t elen = 0, tmpelen;
	kernel_lb_addr eloc, tmpeloc;
	int c = 1;
	loff_t lbcount = 0, b_off = 0;
	uint32_t newblocknum, newblock;
	sector_t offset = 0;
	int8_t etype;
	int goal = 0, pgoal = UDF_I_LOCATION(inode).logicalBlockNum;
	int lastblock = 0;

	prev_epos.offset = udf_file_entry_alloc_offset(inode);
	prev_epos.block = UDF_I_LOCATION(inode);
	prev_epos.bh = NULL;
	cur_epos = next_epos = prev_epos;
	b_off = (loff_t) block << inode->i_sb->s_blocksize_bits;

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

		if ((etype =
		     udf_next_aext(inode, &next_epos, &eloc, &elen, 1)) == -1)
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
			etype = udf_write_aext(inode, &cur_epos, eloc, elen, 1);
		}
		brelse(prev_epos.bh);
		brelse(cur_epos.bh);
		brelse(next_epos.bh);
		newblock = udf_get_lb_pblock(inode->i_sb, eloc, offset);
		*phys = newblock;
		return NULL;
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
			       sizeof(kernel_lb_addr));
			laarr[0].extLength = EXT_NOT_RECORDED_NOT_ALLOCATED;
			/* Will udf_extend_file() create real extent from a fake one? */
			startnum = (offset > 0);
		}
		/* Create extents for the hole between EOF and offset */
		ret = udf_extend_file(inode, &prev_epos, laarr, offset);
		if (ret == -1) {
			brelse(prev_epos.bh);
			brelse(cur_epos.bh);
			brelse(next_epos.bh);
			/* We don't really know the error here so we just make
			 * something up */
			*err = -ENOSPC;
			return NULL;
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
			       sizeof(kernel_lb_addr));
			count++;
			endnum++;
		}
		endnum = c + 1;
		lastblock = 1;
	} else {
		endnum = startnum = ((count > 2) ? 2 : count);

		/* if the current extent is in position 0, swap it with the previous */
		if (!c && count != 1) {
			laarr[2] = laarr[0];
			laarr[0] = laarr[1];
			laarr[1] = laarr[2];
			c = 1;
		}

		/* if the current block is located in an extent, read the next extent */
		if ((etype =
		     udf_next_aext(inode, &next_epos, &eloc, &elen, 0)) != -1) {
			laarr[c + 1].extLength = (etype << 30) | elen;
			laarr[c + 1].extLocation = eloc;
			count++;
			startnum++;
			endnum++;
		} else {
			lastblock = 1;
		}
	}

	/* if the current extent is not recorded but allocated, get the
	   block in the extent corresponding to the requested block */
	if ((laarr[c].extLength >> 30) == (EXT_NOT_RECORDED_ALLOCATED >> 30))
		newblocknum = laarr[c].extLocation.logicalBlockNum + offset;
	else {			/* otherwise, allocate a new block */

		if (UDF_I_NEXT_ALLOC_BLOCK(inode) == block)
			goal = UDF_I_NEXT_ALLOC_GOAL(inode);

		if (!goal) {
			if (!(goal = pgoal))
				goal =
				    UDF_I_LOCATION(inode).logicalBlockNum + 1;
		}

		if (!(newblocknum = udf_new_block(inode->i_sb, inode,
						  UDF_I_LOCATION(inode).
						  partitionReferenceNum, goal,
						  err))) {
			brelse(prev_epos.bh);
			*err = -ENOSPC;
			return NULL;
		}
		UDF_I_LENEXTENTS(inode) += inode->i_sb->s_blocksize;
	}

	/* if the extent the requsted block is located in contains multiple blocks,
	   split the extent into at most three extents. blocks prior to requested
	   block, requested block, and blocks after requested block */
	udf_split_extents(inode, &c, offset, newblocknum, laarr, &endnum);

#ifdef UDF_PREALLOCATE
	/* preallocate blocks */
	udf_prealloc_extents(inode, c, lastblock, laarr, &endnum);
#endif

	/* merge any continuous blocks in laarr */
	udf_merge_extents(inode, laarr, &endnum);

	/* write back the new extents, inserting new extents if the new number
	   of extents is greater than the old number, and deleting extents if
	   the new number of extents is less than the old number */
	udf_update_extents(inode, laarr, startnum, endnum, &prev_epos);

	brelse(prev_epos.bh);

	if (!(newblock = udf_get_pblock(inode->i_sb, newblocknum,
					UDF_I_LOCATION(inode).
					partitionReferenceNum, 0))) {
		return NULL;
	}
	*phys = newblock;
	*err = 0;
	*new = 1;
	UDF_I_NEXT_ALLOC_BLOCK(inode) = block;
	UDF_I_NEXT_ALLOC_GOAL(inode) = newblocknum;
	inode->i_ctime = current_fs_time(inode->i_sb);

	if (IS_SYNC(inode))
		udf_sync_inode(inode);
	else
		mark_inode_dirty(inode);
	return result;
}

static void udf_split_extents(struct inode *inode, int *c, int offset,
			      int newblocknum,
			      kernel_long_ad laarr[EXTENT_MERGE_SIZE],
			      int *endnum)
{
	if ((laarr[*c].extLength >> 30) == (EXT_NOT_RECORDED_ALLOCATED >> 30) ||
	    (laarr[*c].extLength >> 30) ==
	    (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30)) {
		int curr = *c;
		int blen = ((laarr[curr].extLength & UDF_EXTENT_LENGTH_MASK) +
			    inode->i_sb->s_blocksize -
			    1) >> inode->i_sb->s_blocksize_bits;
		int8_t etype = (laarr[curr].extLength >> 30);

		if (blen == 1) ;
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
						laarr[curr].extLocation, 0,
						offset);
				laarr[curr].extLength =
				    EXT_NOT_RECORDED_NOT_ALLOCATED | (offset <<
								      inode->
								      i_sb->
								      s_blocksize_bits);
				laarr[curr].extLocation.logicalBlockNum = 0;
				laarr[curr].extLocation.partitionReferenceNum =
				    0;
			} else
				laarr[curr].extLength = (etype << 30) |
				    (offset << inode->i_sb->s_blocksize_bits);
			curr++;
			(*c)++;
			(*endnum)++;
		}

		laarr[curr].extLocation.logicalBlockNum = newblocknum;
		if (etype == (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
			laarr[curr].extLocation.partitionReferenceNum =
			    UDF_I_LOCATION(inode).partitionReferenceNum;
		laarr[curr].extLength = EXT_RECORDED_ALLOCATED |
		    inode->i_sb->s_blocksize;
		curr++;

		if (blen != offset + 1) {
			if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30))
				laarr[curr].extLocation.logicalBlockNum +=
				    (offset + 1);
			laarr[curr].extLength =
			    (etype << 30) | ((blen - (offset + 1)) << inode->
					     i_sb->s_blocksize_bits);
			curr++;
			(*endnum)++;
		}
	}
}

static void udf_prealloc_extents(struct inode *inode, int c, int lastblock,
				 kernel_long_ad laarr[EXTENT_MERGE_SIZE],
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
			    (((laarr[c + 1].
			       extLength & UDF_EXTENT_LENGTH_MASK) +
			      inode->i_sb->s_blocksize -
			      1) >> inode->i_sb->s_blocksize_bits);
		} else
			start = c;
	}

	for (i = start + 1; i <= *endnum; i++) {
		if (i == *endnum) {
			if (lastblock)
				length += UDF_DEFAULT_PREALLOC_BLOCKS;
		} else if ((laarr[i].extLength >> 30) ==
			   (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
			length +=
			    (((laarr[i].extLength & UDF_EXTENT_LENGTH_MASK) +
			      inode->i_sb->s_blocksize -
			      1) >> inode->i_sb->s_blocksize_bits);
		else
			break;
	}

	if (length) {
		int next = laarr[start].extLocation.logicalBlockNum +
		    (((laarr[start].extLength & UDF_EXTENT_LENGTH_MASK) +
		      inode->i_sb->s_blocksize -
		      1) >> inode->i_sb->s_blocksize_bits);
		int numalloc = udf_prealloc_blocks(inode->i_sb, inode,
						   laarr[start].extLocation.
						   partitionReferenceNum,
						   next,
						   (UDF_DEFAULT_PREALLOC_BLOCKS
						    >
						    length ? length :
						    UDF_DEFAULT_PREALLOC_BLOCKS)
						   - currlength);

		if (numalloc) {
			if (start == (c + 1))
				laarr[start].extLength +=
				    (numalloc << inode->i_sb->s_blocksize_bits);
			else {
				memmove(&laarr[c + 2], &laarr[c + 1],
					sizeof(long_ad) * (*endnum - (c + 1)));
				(*endnum)++;
				laarr[c + 1].extLocation.logicalBlockNum = next;
				laarr[c + 1].extLocation.partitionReferenceNum =
				    laarr[c].extLocation.partitionReferenceNum;
				laarr[c + 1].extLength =
				    EXT_NOT_RECORDED_ALLOCATED | (numalloc <<
								  inode->i_sb->
								  s_blocksize_bits);
				start = c + 1;
			}

			for (i = start + 1; numalloc && i < *endnum; i++) {
				int elen =
				    ((laarr[i].
				      extLength & UDF_EXTENT_LENGTH_MASK) +
				     inode->i_sb->s_blocksize -
				     1) >> inode->i_sb->s_blocksize_bits;

				if (elen > numalloc) {
					laarr[i].extLength -=
					    (numalloc << inode->i_sb->
					     s_blocksize_bits);
					numalloc = 0;
				} else {
					numalloc -= elen;
					if (*endnum > (i + 1))
						memmove(&laarr[i],
							&laarr[i + 1],
							sizeof(long_ad) *
							(*endnum - (i + 1)));
					i--;
					(*endnum)--;
				}
			}
			UDF_I_LENEXTENTS(inode) +=
			    numalloc << inode->i_sb->s_blocksize_bits;
		}
	}
}

static void udf_merge_extents(struct inode *inode,
			      kernel_long_ad laarr[EXTENT_MERGE_SIZE],
			      int *endnum)
{
	int i;

	for (i = 0; i < (*endnum - 1); i++) {
		if ((laarr[i].extLength >> 30) ==
		    (laarr[i + 1].extLength >> 30)) {
			if (((laarr[i].extLength >> 30) ==
			     (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
			    ||
			    ((laarr[i + 1].extLocation.logicalBlockNum -
			      laarr[i].extLocation.logicalBlockNum) ==
			     (((laarr[i].extLength & UDF_EXTENT_LENGTH_MASK) +
			       inode->i_sb->s_blocksize -
			       1) >> inode->i_sb->s_blocksize_bits))) {
				if (((laarr[i].
				      extLength & UDF_EXTENT_LENGTH_MASK) +
				     (laarr[i + 1].
				      extLength & UDF_EXTENT_LENGTH_MASK) +
				     inode->i_sb->s_blocksize -
				     1) & ~UDF_EXTENT_LENGTH_MASK) {
					laarr[i + 1].extLength =
					    (laarr[i + 1].extLength -
					     (laarr[i].
					      extLength &
					      UDF_EXTENT_LENGTH_MASK) +
					     UDF_EXTENT_LENGTH_MASK) & ~(inode->
									 i_sb->
									 s_blocksize
									 - 1);
					laarr[i].extLength =
					    (laarr[i].
					     extLength & UDF_EXTENT_FLAG_MASK) +
					    (UDF_EXTENT_LENGTH_MASK + 1) -
					    inode->i_sb->s_blocksize;
					laarr[i +
					      1].extLocation.logicalBlockNum =
					    laarr[i].extLocation.
					    logicalBlockNum +
					    ((laarr[i].
					      extLength &
					      UDF_EXTENT_LENGTH_MASK) >> inode->
					     i_sb->s_blocksize_bits);
				} else {
					laarr[i].extLength =
					    laarr[i + 1].extLength +
					    (((laarr[i].
					       extLength &
					       UDF_EXTENT_LENGTH_MASK) +
					      inode->i_sb->s_blocksize -
					      1) & ~(inode->i_sb->s_blocksize -
						     1));
					if (*endnum > (i + 2))
						memmove(&laarr[i + 1],
							&laarr[i + 2],
							sizeof(long_ad) *
							(*endnum - (i + 2)));
					i--;
					(*endnum)--;
				}
			}
		} else
		    if (((laarr[i].extLength >> 30) ==
			 (EXT_NOT_RECORDED_ALLOCATED >> 30))
			&& ((laarr[i + 1].extLength >> 30) ==
			    (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))) {
			udf_free_blocks(inode->i_sb, inode,
					laarr[i].extLocation, 0,
					((laarr[i].
					  extLength & UDF_EXTENT_LENGTH_MASK) +
					 inode->i_sb->s_blocksize -
					 1) >> inode->i_sb->s_blocksize_bits);
			laarr[i].extLocation.logicalBlockNum = 0;
			laarr[i].extLocation.partitionReferenceNum = 0;

			if (((laarr[i].extLength & UDF_EXTENT_LENGTH_MASK) +
			     (laarr[i + 1].extLength & UDF_EXTENT_LENGTH_MASK) +
			     inode->i_sb->s_blocksize -
			     1) & ~UDF_EXTENT_LENGTH_MASK) {
				laarr[i + 1].extLength =
				    (laarr[i + 1].extLength -
				     (laarr[i].
				      extLength & UDF_EXTENT_LENGTH_MASK) +
				     UDF_EXTENT_LENGTH_MASK) & ~(inode->i_sb->
								 s_blocksize -
								 1);
				laarr[i].extLength =
				    (laarr[i].
				     extLength & UDF_EXTENT_FLAG_MASK) +
				    (UDF_EXTENT_LENGTH_MASK + 1) -
				    inode->i_sb->s_blocksize;
			} else {
				laarr[i].extLength = laarr[i + 1].extLength +
				    (((laarr[i].
				       extLength & UDF_EXTENT_LENGTH_MASK) +
				      inode->i_sb->s_blocksize -
				      1) & ~(inode->i_sb->s_blocksize - 1));
				if (*endnum > (i + 2))
					memmove(&laarr[i + 1], &laarr[i + 2],
						sizeof(long_ad) * (*endnum -
								   (i + 2)));
				i--;
				(*endnum)--;
			}
		} else if ((laarr[i].extLength >> 30) ==
			   (EXT_NOT_RECORDED_ALLOCATED >> 30)) {
			udf_free_blocks(inode->i_sb, inode,
					laarr[i].extLocation, 0,
					((laarr[i].
					  extLength & UDF_EXTENT_LENGTH_MASK) +
					 inode->i_sb->s_blocksize -
					 1) >> inode->i_sb->s_blocksize_bits);
			laarr[i].extLocation.logicalBlockNum = 0;
			laarr[i].extLocation.partitionReferenceNum = 0;
			laarr[i].extLength =
			    (laarr[i].
			     extLength & UDF_EXTENT_LENGTH_MASK) |
			    EXT_NOT_RECORDED_NOT_ALLOCATED;
		}
	}
}

static void udf_update_extents(struct inode *inode,
			       kernel_long_ad laarr[EXTENT_MERGE_SIZE],
			       int startnum, int endnum,
			       struct extent_position *epos)
{
	int start = 0, i;
	kernel_lb_addr tmploc;
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
		udf_write_aext(inode, epos, laarr[i].extLocation,
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

void udf_truncate(struct inode *inode)
{
	int offset;
	int err;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	lock_kernel();
	if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_IN_ICB) {
		if (inode->i_sb->s_blocksize <
		    (udf_file_entry_alloc_offset(inode) + inode->i_size)) {
			udf_expand_file_adinicb(inode, inode->i_size, &err);
			if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_IN_ICB) {
				inode->i_size = UDF_I_LENALLOC(inode);
				unlock_kernel();
				return;
			} else
				udf_truncate_extents(inode);
		} else {
			offset = inode->i_size & (inode->i_sb->s_blocksize - 1);
			memset(UDF_I_DATA(inode) + UDF_I_LENEATTR(inode) +
			       offset, 0x00,
			       inode->i_sb->s_blocksize - offset -
			       udf_file_entry_alloc_offset(inode));
			UDF_I_LENALLOC(inode) = inode->i_size;
		}
	} else {
		block_truncate_page(inode->i_mapping, inode->i_size,
				    udf_get_block);
		udf_truncate_extents(inode);
	}

	inode->i_mtime = inode->i_ctime = current_fs_time(inode->i_sb);
	if (IS_SYNC(inode))
		udf_sync_inode(inode);
	else
		mark_inode_dirty(inode);
	unlock_kernel();
}

static void __udf_read_inode(struct inode *inode)
{
	struct buffer_head *bh = NULL;
	struct fileEntry *fe;
	uint16_t ident;

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
	bh = udf_read_ptagged(inode->i_sb, UDF_I_LOCATION(inode), 0, &ident);

	if (!bh) {
		printk(KERN_ERR "udf: udf_read_inode(ino %ld) failed !bh\n",
		       inode->i_ino);
		make_bad_inode(inode);
		return;
	}

	if (ident != TAG_IDENT_FE && ident != TAG_IDENT_EFE &&
	    ident != TAG_IDENT_USE) {
		printk(KERN_ERR
		       "udf: udf_read_inode(ino %ld) failed ident=%d\n",
		       inode->i_ino, ident);
		brelse(bh);
		make_bad_inode(inode);
		return;
	}

	fe = (struct fileEntry *)bh->b_data;

	if (le16_to_cpu(fe->icbTag.strategyType) == 4096) {
		struct buffer_head *ibh = NULL, *nbh = NULL;
		struct indirectEntry *ie;

		ibh =
		    udf_read_ptagged(inode->i_sb, UDF_I_LOCATION(inode), 1,
				     &ident);
		if (ident == TAG_IDENT_IE) {
			if (ibh) {
				kernel_lb_addr loc;
				ie = (struct indirectEntry *)ibh->b_data;

				loc = lelb_to_cpu(ie->indirectICB.extLocation);

				if (ie->indirectICB.extLength &&
				    (nbh =
				     udf_read_ptagged(inode->i_sb, loc, 0,
						      &ident))) {
					if (ident == TAG_IDENT_FE
					    || ident == TAG_IDENT_EFE) {
						memcpy(&UDF_I_LOCATION(inode),
						       &loc,
						       sizeof(kernel_lb_addr));
						brelse(bh);
						brelse(ibh);
						brelse(nbh);
						__udf_read_inode(inode);
						return;
					} else {
						brelse(nbh);
						brelse(ibh);
					}
				} else
					brelse(ibh);
			}
		} else
			brelse(ibh);
	} else if (le16_to_cpu(fe->icbTag.strategyType) != 4) {
		printk(KERN_ERR "udf: unsupported strategy type: %d\n",
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
	time_t convtime;
	long convtime_usec;
	int offset;

	fe = (struct fileEntry *)bh->b_data;
	efe = (struct extendedFileEntry *)bh->b_data;

	if (le16_to_cpu(fe->icbTag.strategyType) == 4)
		UDF_I_STRAT4096(inode) = 0;
	else			/* if (le16_to_cpu(fe->icbTag.strategyType) == 4096) */
		UDF_I_STRAT4096(inode) = 1;

	UDF_I_ALLOCTYPE(inode) =
	    le16_to_cpu(fe->icbTag.flags) & ICBTAG_FLAG_AD_MASK;
	UDF_I_UNIQUE(inode) = 0;
	UDF_I_LENEATTR(inode) = 0;
	UDF_I_LENEXTENTS(inode) = 0;
	UDF_I_LENALLOC(inode) = 0;
	UDF_I_NEXT_ALLOC_BLOCK(inode) = 0;
	UDF_I_NEXT_ALLOC_GOAL(inode) = 0;
	if (le16_to_cpu(fe->descTag.tagIdent) == TAG_IDENT_EFE) {
		UDF_I_EFE(inode) = 1;
		UDF_I_USE(inode) = 0;
		if (udf_alloc_i_data
		    (inode,
		     inode->i_sb->s_blocksize -
		     sizeof(struct extendedFileEntry))) {
			make_bad_inode(inode);
			return;
		}
		memcpy(UDF_I_DATA(inode),
		       bh->b_data + sizeof(struct extendedFileEntry),
		       inode->i_sb->s_blocksize -
		       sizeof(struct extendedFileEntry));
	} else if (le16_to_cpu(fe->descTag.tagIdent) == TAG_IDENT_FE) {
		UDF_I_EFE(inode) = 0;
		UDF_I_USE(inode) = 0;
		if (udf_alloc_i_data
		    (inode,
		     inode->i_sb->s_blocksize - sizeof(struct fileEntry))) {
			make_bad_inode(inode);
			return;
		}
		memcpy(UDF_I_DATA(inode), bh->b_data + sizeof(struct fileEntry),
		       inode->i_sb->s_blocksize - sizeof(struct fileEntry));
	} else if (le16_to_cpu(fe->descTag.tagIdent) == TAG_IDENT_USE) {
		UDF_I_EFE(inode) = 0;
		UDF_I_USE(inode) = 1;
		UDF_I_LENALLOC(inode) =
		    le32_to_cpu(((struct unallocSpaceEntry *)bh->b_data)->
				lengthAllocDescs);
		if (udf_alloc_i_data
		    (inode,
		     inode->i_sb->s_blocksize -
		     sizeof(struct unallocSpaceEntry))) {
			make_bad_inode(inode);
			return;
		}
		memcpy(UDF_I_DATA(inode),
		       bh->b_data + sizeof(struct unallocSpaceEntry),
		       inode->i_sb->s_blocksize -
		       sizeof(struct unallocSpaceEntry));
		return;
	}

	inode->i_uid = le32_to_cpu(fe->uid);
	if (inode->i_uid == -1 || UDF_QUERY_FLAG(inode->i_sb,
						 UDF_FLAG_UID_IGNORE))
		inode->i_uid = UDF_SB(inode->i_sb)->s_uid;

	inode->i_gid = le32_to_cpu(fe->gid);
	if (inode->i_gid == -1 || UDF_QUERY_FLAG(inode->i_sb,
						 UDF_FLAG_GID_IGNORE))
		inode->i_gid = UDF_SB(inode->i_sb)->s_gid;

	inode->i_nlink = le16_to_cpu(fe->fileLinkCount);
	if (!inode->i_nlink)
		inode->i_nlink = 1;

	inode->i_size = le64_to_cpu(fe->informationLength);
	UDF_I_LENEXTENTS(inode) = inode->i_size;

	inode->i_mode = udf_convert_permissions(fe);
	inode->i_mode &= ~UDF_SB(inode->i_sb)->s_umask;

	if (UDF_I_EFE(inode) == 0) {
		inode->i_blocks = le64_to_cpu(fe->logicalBlocksRecorded) <<
		    (inode->i_sb->s_blocksize_bits - 9);

		if (udf_stamp_to_time(&convtime, &convtime_usec,
				      lets_to_cpu(fe->accessTime))) {
			inode->i_atime.tv_sec = convtime;
			inode->i_atime.tv_nsec = convtime_usec * 1000;
		} else {
			inode->i_atime = UDF_SB_RECORDTIME(inode->i_sb);
		}

		if (udf_stamp_to_time(&convtime, &convtime_usec,
				      lets_to_cpu(fe->modificationTime))) {
			inode->i_mtime.tv_sec = convtime;
			inode->i_mtime.tv_nsec = convtime_usec * 1000;
		} else {
			inode->i_mtime = UDF_SB_RECORDTIME(inode->i_sb);
		}

		if (udf_stamp_to_time(&convtime, &convtime_usec,
				      lets_to_cpu(fe->attrTime))) {
			inode->i_ctime.tv_sec = convtime;
			inode->i_ctime.tv_nsec = convtime_usec * 1000;
		} else {
			inode->i_ctime = UDF_SB_RECORDTIME(inode->i_sb);
		}

		UDF_I_UNIQUE(inode) = le64_to_cpu(fe->uniqueID);
		UDF_I_LENEATTR(inode) = le32_to_cpu(fe->lengthExtendedAttr);
		UDF_I_LENALLOC(inode) = le32_to_cpu(fe->lengthAllocDescs);
		offset = sizeof(struct fileEntry) + UDF_I_LENEATTR(inode);
	} else {
		inode->i_blocks = le64_to_cpu(efe->logicalBlocksRecorded) <<
		    (inode->i_sb->s_blocksize_bits - 9);

		if (udf_stamp_to_time(&convtime, &convtime_usec,
				      lets_to_cpu(efe->accessTime))) {
			inode->i_atime.tv_sec = convtime;
			inode->i_atime.tv_nsec = convtime_usec * 1000;
		} else {
			inode->i_atime = UDF_SB_RECORDTIME(inode->i_sb);
		}

		if (udf_stamp_to_time(&convtime, &convtime_usec,
				      lets_to_cpu(efe->modificationTime))) {
			inode->i_mtime.tv_sec = convtime;
			inode->i_mtime.tv_nsec = convtime_usec * 1000;
		} else {
			inode->i_mtime = UDF_SB_RECORDTIME(inode->i_sb);
		}

		if (udf_stamp_to_time(&convtime, &convtime_usec,
				      lets_to_cpu(efe->createTime))) {
			UDF_I_CRTIME(inode).tv_sec = convtime;
			UDF_I_CRTIME(inode).tv_nsec = convtime_usec * 1000;
		} else {
			UDF_I_CRTIME(inode) = UDF_SB_RECORDTIME(inode->i_sb);
		}

		if (udf_stamp_to_time(&convtime, &convtime_usec,
				      lets_to_cpu(efe->attrTime))) {
			inode->i_ctime.tv_sec = convtime;
			inode->i_ctime.tv_nsec = convtime_usec * 1000;
		} else {
			inode->i_ctime = UDF_SB_RECORDTIME(inode->i_sb);
		}

		UDF_I_UNIQUE(inode) = le64_to_cpu(efe->uniqueID);
		UDF_I_LENEATTR(inode) = le32_to_cpu(efe->lengthExtendedAttr);
		UDF_I_LENALLOC(inode) = le32_to_cpu(efe->lengthAllocDescs);
		offset =
		    sizeof(struct extendedFileEntry) + UDF_I_LENEATTR(inode);
	}

	switch (fe->icbTag.fileType) {
	case ICBTAG_FILE_TYPE_DIRECTORY:
		{
			inode->i_op = &udf_dir_inode_operations;
			inode->i_fop = &udf_dir_operations;
			inode->i_mode |= S_IFDIR;
			inc_nlink(inode);
			break;
		}
	case ICBTAG_FILE_TYPE_REALTIME:
	case ICBTAG_FILE_TYPE_REGULAR:
	case ICBTAG_FILE_TYPE_UNDEF:
		{
			if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_IN_ICB)
				inode->i_data.a_ops = &udf_adinicb_aops;
			else
				inode->i_data.a_ops = &udf_aops;
			inode->i_op = &udf_file_inode_operations;
			inode->i_fop = &udf_file_operations;
			inode->i_mode |= S_IFREG;
			break;
		}
	case ICBTAG_FILE_TYPE_BLOCK:
		{
			inode->i_mode |= S_IFBLK;
			break;
		}
	case ICBTAG_FILE_TYPE_CHAR:
		{
			inode->i_mode |= S_IFCHR;
			break;
		}
	case ICBTAG_FILE_TYPE_FIFO:
		{
			init_special_inode(inode, inode->i_mode | S_IFIFO, 0);
			break;
		}
	case ICBTAG_FILE_TYPE_SOCKET:
		{
			init_special_inode(inode, inode->i_mode | S_IFSOCK, 0);
			break;
		}
	case ICBTAG_FILE_TYPE_SYMLINK:
		{
			inode->i_data.a_ops = &udf_symlink_aops;
			inode->i_op = &page_symlink_inode_operations;
			inode->i_mode = S_IFLNK | S_IRWXUGO;
			break;
		}
	default:
		{
			printk(KERN_ERR
			       "udf: udf_fill_inode(ino %ld) failed unknown file type=%d\n",
			       inode->i_ino, fe->icbTag.fileType);
			make_bad_inode(inode);
			return;
		}
	}
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		struct deviceSpec *dsea = (struct deviceSpec *)
		    udf_get_extendedattr(inode, 12, 1);

		if (dsea) {
			init_special_inode(inode, inode->i_mode,
					   MKDEV(le32_to_cpu
						 (dsea->majorDeviceIdent),
						 le32_to_cpu(dsea->
							     minorDeviceIdent)));
			/* Developer ID ??? */
		} else {
			make_bad_inode(inode);
		}
	}
}

static int udf_alloc_i_data(struct inode *inode, size_t size)
{
	UDF_I_DATA(inode) = kmalloc(size, GFP_KERNEL);

	if (!UDF_I_DATA(inode)) {
		printk(KERN_ERR
		       "udf:udf_alloc_i_data (ino %ld) no free memory\n",
		       inode->i_ino);
		return -ENOMEM;
	}

	return 0;
}

static mode_t udf_convert_permissions(struct fileEntry *fe)
{
	mode_t mode;
	uint32_t permissions;
	uint32_t flags;

	permissions = le32_to_cpu(fe->permissions);
	flags = le16_to_cpu(fe->icbTag.flags);

	mode = ((permissions) & S_IRWXO) |
	    ((permissions >> 2) & S_IRWXG) |
	    ((permissions >> 4) & S_IRWXU) |
	    ((flags & ICBTAG_FLAG_SETUID) ? S_ISUID : 0) |
	    ((flags & ICBTAG_FLAG_SETGID) ? S_ISGID : 0) |
	    ((flags & ICBTAG_FLAG_STICKY) ? S_ISVTX : 0);

	return mode;
}

/*
 * udf_write_inode
 *
 * PURPOSE
 *	Write out the specified inode.
 *
 * DESCRIPTION
 *	This routine is called whenever an inode is synced.
 *	Currently this routine is just a placeholder.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */

int udf_write_inode(struct inode *inode, int sync)
{
	int ret;
	lock_kernel();
	ret = udf_update_inode(inode, sync);
	unlock_kernel();
	return ret;
}

int udf_sync_inode(struct inode *inode)
{
	return udf_update_inode(inode, 1);
}

static int udf_update_inode(struct inode *inode, int do_sync)
{
	struct buffer_head *bh = NULL;
	struct fileEntry *fe;
	struct extendedFileEntry *efe;
	uint32_t udfperms;
	uint16_t icbflags;
	uint16_t crclen;
	int i;
	kernel_timestamp cpu_time;
	int err = 0;

	bh = udf_tread(inode->i_sb,
		       udf_get_lb_pblock(inode->i_sb, UDF_I_LOCATION(inode),
					 0));

	if (!bh) {
		udf_debug("bread failure\n");
		return -EIO;
	}

	memset(bh->b_data, 0x00, inode->i_sb->s_blocksize);

	fe = (struct fileEntry *)bh->b_data;
	efe = (struct extendedFileEntry *)bh->b_data;

	if (le16_to_cpu(fe->descTag.tagIdent) == TAG_IDENT_USE) {
		struct unallocSpaceEntry *use =
		    (struct unallocSpaceEntry *)bh->b_data;

		use->lengthAllocDescs = cpu_to_le32(UDF_I_LENALLOC(inode));
		memcpy(bh->b_data + sizeof(struct unallocSpaceEntry),
		       UDF_I_DATA(inode),
		       inode->i_sb->s_blocksize -
		       sizeof(struct unallocSpaceEntry));
		crclen =
		    sizeof(struct unallocSpaceEntry) + UDF_I_LENALLOC(inode) -
		    sizeof(tag);
		use->descTag.tagLocation =
		    cpu_to_le32(UDF_I_LOCATION(inode).logicalBlockNum);
		use->descTag.descCRCLength = cpu_to_le16(crclen);
		use->descTag.descCRC =
		    cpu_to_le16(udf_crc((char *)use + sizeof(tag), crclen, 0));

		use->descTag.tagChecksum = 0;
		for (i = 0; i < 16; i++)
			if (i != 4)
				use->descTag.tagChecksum +=
				    ((uint8_t *) & (use->descTag))[i];

		mark_buffer_dirty(bh);
		brelse(bh);
		return err;
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
	    ((inode->i_mode & S_IRWXG) << 2) | ((inode->i_mode & S_IRWXU) << 4);

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
		regid *eid;
		struct deviceSpec *dsea = (struct deviceSpec *)
		    udf_get_extendedattr(inode, 12, 1);

		if (!dsea) {
			dsea = (struct deviceSpec *)
			    udf_add_extendedattr(inode,
						 sizeof(struct deviceSpec) +
						 sizeof(regid), 12, 0x3);
			dsea->attrType = cpu_to_le32(12);
			dsea->attrSubtype = 1;
			dsea->attrLength =
			    cpu_to_le32(sizeof(struct deviceSpec) +
					sizeof(regid));
			dsea->impUseLength = cpu_to_le32(sizeof(regid));
		}
		eid = (regid *) dsea->impUse;
		memset(eid, 0, sizeof(regid));
		strcpy(eid->ident, UDF_ID_DEVELOPER);
		eid->identSuffix[0] = UDF_OS_CLASS_UNIX;
		eid->identSuffix[1] = UDF_OS_ID_LINUX;
		dsea->majorDeviceIdent = cpu_to_le32(imajor(inode));
		dsea->minorDeviceIdent = cpu_to_le32(iminor(inode));
	}

	if (UDF_I_EFE(inode) == 0) {
		memcpy(bh->b_data + sizeof(struct fileEntry), UDF_I_DATA(inode),
		       inode->i_sb->s_blocksize - sizeof(struct fileEntry));
		fe->logicalBlocksRecorded =
		    cpu_to_le64((inode->i_blocks +
				 (1 << (inode->i_sb->s_blocksize_bits - 9)) -
				 1) >> (inode->i_sb->s_blocksize_bits - 9));

		if (udf_time_to_stamp(&cpu_time, inode->i_atime))
			fe->accessTime = cpu_to_lets(cpu_time);
		if (udf_time_to_stamp(&cpu_time, inode->i_mtime))
			fe->modificationTime = cpu_to_lets(cpu_time);
		if (udf_time_to_stamp(&cpu_time, inode->i_ctime))
			fe->attrTime = cpu_to_lets(cpu_time);
		memset(&(fe->impIdent), 0, sizeof(regid));
		strcpy(fe->impIdent.ident, UDF_ID_DEVELOPER);
		fe->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
		fe->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
		fe->uniqueID = cpu_to_le64(UDF_I_UNIQUE(inode));
		fe->lengthExtendedAttr = cpu_to_le32(UDF_I_LENEATTR(inode));
		fe->lengthAllocDescs = cpu_to_le32(UDF_I_LENALLOC(inode));
		fe->descTag.tagIdent = cpu_to_le16(TAG_IDENT_FE);
		crclen = sizeof(struct fileEntry);
	} else {
		memcpy(bh->b_data + sizeof(struct extendedFileEntry),
		       UDF_I_DATA(inode),
		       inode->i_sb->s_blocksize -
		       sizeof(struct extendedFileEntry));
		efe->objectSize = cpu_to_le64(inode->i_size);
		efe->logicalBlocksRecorded = cpu_to_le64((inode->i_blocks +
							  (1 <<
							   (inode->i_sb->
							    s_blocksize_bits -
							    9)) -
							  1) >> (inode->i_sb->
								 s_blocksize_bits
								 - 9));

		if (UDF_I_CRTIME(inode).tv_sec > inode->i_atime.tv_sec ||
		    (UDF_I_CRTIME(inode).tv_sec == inode->i_atime.tv_sec &&
		     UDF_I_CRTIME(inode).tv_nsec > inode->i_atime.tv_nsec)) {
			UDF_I_CRTIME(inode) = inode->i_atime;
		}
		if (UDF_I_CRTIME(inode).tv_sec > inode->i_mtime.tv_sec ||
		    (UDF_I_CRTIME(inode).tv_sec == inode->i_mtime.tv_sec &&
		     UDF_I_CRTIME(inode).tv_nsec > inode->i_mtime.tv_nsec)) {
			UDF_I_CRTIME(inode) = inode->i_mtime;
		}
		if (UDF_I_CRTIME(inode).tv_sec > inode->i_ctime.tv_sec ||
		    (UDF_I_CRTIME(inode).tv_sec == inode->i_ctime.tv_sec &&
		     UDF_I_CRTIME(inode).tv_nsec > inode->i_ctime.tv_nsec)) {
			UDF_I_CRTIME(inode) = inode->i_ctime;
		}

		if (udf_time_to_stamp(&cpu_time, inode->i_atime))
			efe->accessTime = cpu_to_lets(cpu_time);
		if (udf_time_to_stamp(&cpu_time, inode->i_mtime))
			efe->modificationTime = cpu_to_lets(cpu_time);
		if (udf_time_to_stamp(&cpu_time, UDF_I_CRTIME(inode)))
			efe->createTime = cpu_to_lets(cpu_time);
		if (udf_time_to_stamp(&cpu_time, inode->i_ctime))
			efe->attrTime = cpu_to_lets(cpu_time);

		memset(&(efe->impIdent), 0, sizeof(regid));
		strcpy(efe->impIdent.ident, UDF_ID_DEVELOPER);
		efe->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
		efe->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
		efe->uniqueID = cpu_to_le64(UDF_I_UNIQUE(inode));
		efe->lengthExtendedAttr = cpu_to_le32(UDF_I_LENEATTR(inode));
		efe->lengthAllocDescs = cpu_to_le32(UDF_I_LENALLOC(inode));
		efe->descTag.tagIdent = cpu_to_le16(TAG_IDENT_EFE);
		crclen = sizeof(struct extendedFileEntry);
	}
	if (UDF_I_STRAT4096(inode)) {
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

	icbflags = UDF_I_ALLOCTYPE(inode) |
	    ((inode->i_mode & S_ISUID) ? ICBTAG_FLAG_SETUID : 0) |
	    ((inode->i_mode & S_ISGID) ? ICBTAG_FLAG_SETGID : 0) |
	    ((inode->i_mode & S_ISVTX) ? ICBTAG_FLAG_STICKY : 0) |
	    (le16_to_cpu(fe->icbTag.flags) &
	     ~(ICBTAG_FLAG_AD_MASK | ICBTAG_FLAG_SETUID |
	       ICBTAG_FLAG_SETGID | ICBTAG_FLAG_STICKY));

	fe->icbTag.flags = cpu_to_le16(icbflags);
	if (UDF_SB_UDFREV(inode->i_sb) >= 0x0200)
		fe->descTag.descVersion = cpu_to_le16(3);
	else
		fe->descTag.descVersion = cpu_to_le16(2);
	fe->descTag.tagSerialNum = cpu_to_le16(UDF_SB_SERIALNUM(inode->i_sb));
	fe->descTag.tagLocation =
	    cpu_to_le32(UDF_I_LOCATION(inode).logicalBlockNum);
	crclen += UDF_I_LENEATTR(inode) + UDF_I_LENALLOC(inode) - sizeof(tag);
	fe->descTag.descCRCLength = cpu_to_le16(crclen);
	fe->descTag.descCRC =
	    cpu_to_le16(udf_crc((char *)fe + sizeof(tag), crclen, 0));

	fe->descTag.tagChecksum = 0;
	for (i = 0; i < 16; i++)
		if (i != 4)
			fe->descTag.tagChecksum +=
			    ((uint8_t *) & (fe->descTag))[i];

	/* write the data blocks */
	mark_buffer_dirty(bh);
	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk("IO error syncing udf inode [%s:%08lx]\n",
			       inode->i_sb->s_id, inode->i_ino);
			err = -EIO;
		}
	}
	brelse(bh);
	return err;
}

struct inode *udf_iget(struct super_block *sb, kernel_lb_addr ino)
{
	unsigned long block = udf_get_lb_pblock(sb, ino, 0);
	struct inode *inode = iget_locked(sb, block);

	if (!inode)
		return NULL;

	if (inode->i_state & I_NEW) {
		memcpy(&UDF_I_LOCATION(inode), &ino, sizeof(kernel_lb_addr));
		__udf_read_inode(inode);
		unlock_new_inode(inode);
	}

	if (is_bad_inode(inode))
		goto out_iput;

	if (ino.logicalBlockNum >=
	    UDF_SB_PARTLEN(sb, ino.partitionReferenceNum)) {
		udf_debug("block=%d, partition=%d out of range\n",
			  ino.logicalBlockNum, ino.partitionReferenceNum);
		make_bad_inode(inode);
		goto out_iput;
	}

	return inode;

      out_iput:
	iput(inode);
	return NULL;
}

int8_t udf_add_aext(struct inode * inode, struct extent_position * epos,
		    kernel_lb_addr eloc, uint32_t elen, int inc)
{
	int adsize;
	short_ad *sad = NULL;
	long_ad *lad = NULL;
	struct allocExtDesc *aed;
	int8_t etype;
	uint8_t *ptr;

	if (!epos->bh)
		ptr =
		    UDF_I_DATA(inode) + epos->offset -
		    udf_file_entry_alloc_offset(inode) + UDF_I_LENEATTR(inode);
	else
		ptr = epos->bh->b_data + epos->offset;

	if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(long_ad);
	else
		return -1;

	if (epos->offset + (2 * adsize) > inode->i_sb->s_blocksize) {
		char *sptr, *dptr;
		struct buffer_head *nbh;
		int err, loffset;
		kernel_lb_addr obloc = epos->block;

		if (!
		    (epos->block.logicalBlockNum =
		     udf_new_block(inode->i_sb, NULL,
				   obloc.partitionReferenceNum,
				   obloc.logicalBlockNum, &err))) {
			return -1;
		}
		if (!
		    (nbh =
		     udf_tgetblk(inode->i_sb,
				 udf_get_lb_pblock(inode->i_sb, epos->block,
						   0)))) {
			return -1;
		}
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
				aed->lengthAllocDescs =
				    cpu_to_le32(le32_to_cpu
						(aed->lengthAllocDescs) +
						adsize);
			} else {
				UDF_I_LENALLOC(inode) += adsize;
				mark_inode_dirty(inode);
			}
		}
		if (UDF_SB_UDFREV(inode->i_sb) >= 0x0200)
			udf_new_tag(nbh->b_data, TAG_IDENT_AED, 3, 1,
				    epos->block.logicalBlockNum, sizeof(tag));
		else
			udf_new_tag(nbh->b_data, TAG_IDENT_AED, 2, 1,
				    epos->block.logicalBlockNum, sizeof(tag));
		switch (UDF_I_ALLOCTYPE(inode)) {
		case ICBTAG_FLAG_AD_SHORT:
			{
				sad = (short_ad *) sptr;
				sad->extLength =
				    cpu_to_le32(EXT_NEXT_EXTENT_ALLOCDECS |
						inode->i_sb->s_blocksize);
				sad->extPosition =
				    cpu_to_le32(epos->block.logicalBlockNum);
				break;
			}
		case ICBTAG_FLAG_AD_LONG:
			{
				lad = (long_ad *) sptr;
				lad->extLength =
				    cpu_to_le32(EXT_NEXT_EXTENT_ALLOCDECS |
						inode->i_sb->s_blocksize);
				lad->extLocation = cpu_to_lelb(epos->block);
				memset(lad->impUse, 0x00, sizeof(lad->impUse));
				break;
			}
		}
		if (epos->bh) {
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT)
			    || UDF_SB_UDFREV(inode->i_sb) >= 0x0201)
				udf_update_tag(epos->bh->b_data, loffset);
			else
				udf_update_tag(epos->bh->b_data,
					       sizeof(struct allocExtDesc));
			mark_buffer_dirty_inode(epos->bh, inode);
			brelse(epos->bh);
		} else
			mark_inode_dirty(inode);
		epos->bh = nbh;
	}

	etype = udf_write_aext(inode, epos, eloc, elen, inc);

	if (!epos->bh) {
		UDF_I_LENALLOC(inode) += adsize;
		mark_inode_dirty(inode);
	} else {
		aed = (struct allocExtDesc *)epos->bh->b_data;
		aed->lengthAllocDescs =
		    cpu_to_le32(le32_to_cpu(aed->lengthAllocDescs) + adsize);
		if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT)
		    || UDF_SB_UDFREV(inode->i_sb) >= 0x0201)
			udf_update_tag(epos->bh->b_data,
				       epos->offset + (inc ? 0 : adsize));
		else
			udf_update_tag(epos->bh->b_data,
				       sizeof(struct allocExtDesc));
		mark_buffer_dirty_inode(epos->bh, inode);
	}

	return etype;
}

int8_t udf_write_aext(struct inode * inode, struct extent_position * epos,
		      kernel_lb_addr eloc, uint32_t elen, int inc)
{
	int adsize;
	uint8_t *ptr;

	if (!epos->bh)
		ptr =
		    UDF_I_DATA(inode) + epos->offset -
		    udf_file_entry_alloc_offset(inode) + UDF_I_LENEATTR(inode);
	else
		ptr = epos->bh->b_data + epos->offset;

	switch (UDF_I_ALLOCTYPE(inode)) {
	case ICBTAG_FLAG_AD_SHORT:
		{
			short_ad *sad = (short_ad *) ptr;
			sad->extLength = cpu_to_le32(elen);
			sad->extPosition = cpu_to_le32(eloc.logicalBlockNum);
			adsize = sizeof(short_ad);
			break;
		}
	case ICBTAG_FLAG_AD_LONG:
		{
			long_ad *lad = (long_ad *) ptr;
			lad->extLength = cpu_to_le32(elen);
			lad->extLocation = cpu_to_lelb(eloc);
			memset(lad->impUse, 0x00, sizeof(lad->impUse));
			adsize = sizeof(long_ad);
			break;
		}
	default:
		return -1;
	}

	if (epos->bh) {
		if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT)
		    || UDF_SB_UDFREV(inode->i_sb) >= 0x0201) {
			struct allocExtDesc *aed =
			    (struct allocExtDesc *)epos->bh->b_data;
			udf_update_tag(epos->bh->b_data,
				       le32_to_cpu(aed->lengthAllocDescs) +
				       sizeof(struct allocExtDesc));
		}
		mark_buffer_dirty_inode(epos->bh, inode);
	} else
		mark_inode_dirty(inode);

	if (inc)
		epos->offset += adsize;
	return (elen >> 30);
}

int8_t udf_next_aext(struct inode * inode, struct extent_position * epos,
		     kernel_lb_addr * eloc, uint32_t * elen, int inc)
{
	int8_t etype;

	while ((etype = udf_current_aext(inode, epos, eloc, elen, inc)) ==
	       (EXT_NEXT_EXTENT_ALLOCDECS >> 30)) {
		epos->block = *eloc;
		epos->offset = sizeof(struct allocExtDesc);
		brelse(epos->bh);
		if (!
		    (epos->bh =
		     udf_tread(inode->i_sb,
			       udf_get_lb_pblock(inode->i_sb, epos->block,
						 0)))) {
			udf_debug("reading block %d failed!\n",
				  udf_get_lb_pblock(inode->i_sb, epos->block,
						    0));
			return -1;
		}
	}

	return etype;
}

int8_t udf_current_aext(struct inode * inode, struct extent_position * epos,
			kernel_lb_addr * eloc, uint32_t * elen, int inc)
{
	int alen;
	int8_t etype;
	uint8_t *ptr;

	if (!epos->bh) {
		if (!epos->offset)
			epos->offset = udf_file_entry_alloc_offset(inode);
		ptr =
		    UDF_I_DATA(inode) + epos->offset -
		    udf_file_entry_alloc_offset(inode) + UDF_I_LENEATTR(inode);
		alen =
		    udf_file_entry_alloc_offset(inode) + UDF_I_LENALLOC(inode);
	} else {
		if (!epos->offset)
			epos->offset = sizeof(struct allocExtDesc);
		ptr = epos->bh->b_data + epos->offset;
		alen =
		    sizeof(struct allocExtDesc) +
		    le32_to_cpu(((struct allocExtDesc *)epos->bh->b_data)->
				lengthAllocDescs);
	}

	switch (UDF_I_ALLOCTYPE(inode)) {
	case ICBTAG_FLAG_AD_SHORT:
		{
			short_ad *sad;

			if (!
			    (sad =
			     udf_get_fileshortad(ptr, alen, &epos->offset,
						 inc)))
				return -1;

			etype = le32_to_cpu(sad->extLength) >> 30;
			eloc->logicalBlockNum = le32_to_cpu(sad->extPosition);
			eloc->partitionReferenceNum =
			    UDF_I_LOCATION(inode).partitionReferenceNum;
			*elen =
			    le32_to_cpu(sad->
					extLength) & UDF_EXTENT_LENGTH_MASK;
			break;
		}
	case ICBTAG_FLAG_AD_LONG:
		{
			long_ad *lad;

			if (!
			    (lad =
			     udf_get_filelongad(ptr, alen, &epos->offset, inc)))
				return -1;

			etype = le32_to_cpu(lad->extLength) >> 30;
			*eloc = lelb_to_cpu(lad->extLocation);
			*elen =
			    le32_to_cpu(lad->
					extLength) & UDF_EXTENT_LENGTH_MASK;
			break;
		}
	default:
		{
			udf_debug("alloc_type = %d unsupported\n",
				  UDF_I_ALLOCTYPE(inode));
			return -1;
		}
	}

	return etype;
}

static int8_t
udf_insert_aext(struct inode *inode, struct extent_position epos,
		kernel_lb_addr neloc, uint32_t nelen)
{
	kernel_lb_addr oeloc;
	uint32_t oelen;
	int8_t etype;

	if (epos.bh)
		get_bh(epos.bh);

	while ((etype = udf_next_aext(inode, &epos, &oeloc, &oelen, 0)) != -1) {
		udf_write_aext(inode, &epos, neloc, nelen, 1);

		neloc = oeloc;
		nelen = (etype << 30) | oelen;
	}
	udf_add_aext(inode, &epos, neloc, nelen, 1);
	brelse(epos.bh);
	return (nelen >> 30);
}

int8_t udf_delete_aext(struct inode * inode, struct extent_position epos,
		       kernel_lb_addr eloc, uint32_t elen)
{
	struct extent_position oepos;
	int adsize;
	int8_t etype;
	struct allocExtDesc *aed;

	if (epos.bh) {
		get_bh(epos.bh);
		get_bh(epos.bh);
	}

	if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(long_ad);
	else
		adsize = 0;

	oepos = epos;
	if (udf_next_aext(inode, &epos, &eloc, &elen, 1) == -1)
		return -1;

	while ((etype = udf_next_aext(inode, &epos, &eloc, &elen, 1)) != -1) {
		udf_write_aext(inode, &oepos, eloc, (etype << 30) | elen, 1);
		if (oepos.bh != epos.bh) {
			oepos.block = epos.block;
			brelse(oepos.bh);
			get_bh(epos.bh);
			oepos.bh = epos.bh;
			oepos.offset = epos.offset - adsize;
		}
	}
	memset(&eloc, 0x00, sizeof(kernel_lb_addr));
	elen = 0;

	if (epos.bh != oepos.bh) {
		udf_free_blocks(inode->i_sb, inode, epos.block, 0, 1);
		udf_write_aext(inode, &oepos, eloc, elen, 1);
		udf_write_aext(inode, &oepos, eloc, elen, 1);
		if (!oepos.bh) {
			UDF_I_LENALLOC(inode) -= (adsize * 2);
			mark_inode_dirty(inode);
		} else {
			aed = (struct allocExtDesc *)oepos.bh->b_data;
			aed->lengthAllocDescs =
			    cpu_to_le32(le32_to_cpu(aed->lengthAllocDescs) -
					(2 * adsize));
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT)
			    || UDF_SB_UDFREV(inode->i_sb) >= 0x0201)
				udf_update_tag(oepos.bh->b_data,
					       oepos.offset - (2 * adsize));
			else
				udf_update_tag(oepos.bh->b_data,
					       sizeof(struct allocExtDesc));
			mark_buffer_dirty_inode(oepos.bh, inode);
		}
	} else {
		udf_write_aext(inode, &oepos, eloc, elen, 1);
		if (!oepos.bh) {
			UDF_I_LENALLOC(inode) -= adsize;
			mark_inode_dirty(inode);
		} else {
			aed = (struct allocExtDesc *)oepos.bh->b_data;
			aed->lengthAllocDescs =
			    cpu_to_le32(le32_to_cpu(aed->lengthAllocDescs) -
					adsize);
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT)
			    || UDF_SB_UDFREV(inode->i_sb) >= 0x0201)
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

int8_t inode_bmap(struct inode * inode, sector_t block,
		  struct extent_position * pos, kernel_lb_addr * eloc,
		  uint32_t * elen, sector_t * offset)
{
	loff_t lbcount = 0, bcount =
	    (loff_t) block << inode->i_sb->s_blocksize_bits;
	int8_t etype;

	if (block < 0) {
		printk(KERN_ERR "udf: inode_bmap: block < 0\n");
		return -1;
	}

	pos->offset = 0;
	pos->block = UDF_I_LOCATION(inode);
	pos->bh = NULL;
	*elen = 0;

	do {
		if ((etype = udf_next_aext(inode, pos, eloc, elen, 1)) == -1) {
			*offset =
			    (bcount - lbcount) >> inode->i_sb->s_blocksize_bits;
			UDF_I_LENEXTENTS(inode) = lbcount;
			return -1;
		}
		lbcount += *elen;
	} while (lbcount <= bcount);

	*offset = (bcount + *elen - lbcount) >> inode->i_sb->s_blocksize_bits;

	return etype;
}

long udf_block_map(struct inode *inode, sector_t block)
{
	kernel_lb_addr eloc;
	uint32_t elen;
	sector_t offset;
	struct extent_position epos = { NULL, 0, {0, 0} };
	int ret;

	lock_kernel();

	if (inode_bmap(inode, block, &epos, &eloc, &elen, &offset) ==
	    (EXT_RECORDED_ALLOCATED >> 30))
		ret = udf_get_lb_pblock(inode->i_sb, eloc, offset);
	else
		ret = 0;

	unlock_kernel();
	brelse(epos.bh);

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_VARCONV))
		return udf_fixed_to_variable(ret);
	else
		return ret;
}
