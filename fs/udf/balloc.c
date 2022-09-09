/*
 * balloc.c
 *
 * PURPOSE
 *	Block allocation handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1999-2001 Ben Fennema
 *  (C) 1999 Stelias Computing Inc
 *
 * HISTORY
 *
 *  02/24/99 blf  Created.
 *
 */

#include "udfdecl.h"

#include <linux/bitops.h>

#include "udf_i.h"
#include "udf_sb.h"

#define udf_clear_bit	__test_and_clear_bit_le
#define udf_set_bit	__test_and_set_bit_le
#define udf_test_bit	test_bit_le
#define udf_find_next_one_bit	find_next_bit_le

static int read_block_bitmap(struct super_block *sb,
			     struct udf_bitmap *bitmap, unsigned int block,
			     unsigned long bitmap_nr)
{
	struct buffer_head *bh = NULL;
	int retval = 0;
	struct kernel_lb_addr loc;

	loc.logicalBlockNum = bitmap->s_extPosition;
	loc.partitionReferenceNum = UDF_SB(sb)->s_partition;

	bh = udf_tread(sb, udf_get_lb_pblock(sb, &loc, block));
	if (!bh)
		retval = -EIO;

	bitmap->s_block_bitmap[bitmap_nr] = bh;
	return retval;
}

static int __load_block_bitmap(struct super_block *sb,
			       struct udf_bitmap *bitmap,
			       unsigned int block_group)
{
	int retval = 0;
	int nr_groups = bitmap->s_nr_groups;

	if (block_group >= nr_groups) {
		udf_debug("block_group (%u) > nr_groups (%d)\n",
			  block_group, nr_groups);
	}

	if (bitmap->s_block_bitmap[block_group])
		return block_group;

	retval = read_block_bitmap(sb, bitmap, block_group, block_group);
	if (retval < 0)
		return retval;

	return block_group;
}

static inline int load_block_bitmap(struct super_block *sb,
				    struct udf_bitmap *bitmap,
				    unsigned int block_group)
{
	int slot;

	slot = __load_block_bitmap(sb, bitmap, block_group);

	if (slot < 0)
		return slot;

	if (!bitmap->s_block_bitmap[slot])
		return -EIO;

	return slot;
}

static void udf_add_free_space(struct super_block *sb, u16 partition, u32 cnt)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct logicalVolIntegrityDesc *lvid;

	if (!sbi->s_lvid_bh)
		return;

	lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
	le32_add_cpu(&lvid->freeSpaceTable[partition], cnt);
	udf_updated_lvid(sb);
}

static void udf_bitmap_free_blocks(struct super_block *sb,
				   struct udf_bitmap *bitmap,
				   struct kernel_lb_addr *bloc,
				   uint32_t offset,
				   uint32_t count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct buffer_head *bh = NULL;
	struct udf_part_map *partmap;
	unsigned long block;
	unsigned long block_group;
	unsigned long bit;
	unsigned long i;
	int bitmap_nr;
	unsigned long overflow;

	mutex_lock(&sbi->s_alloc_mutex);
	partmap = &sbi->s_partmaps[bloc->partitionReferenceNum];
	if (bloc->logicalBlockNum + count < count ||
	    (bloc->logicalBlockNum + count) > partmap->s_partition_len) {
		udf_debug("%u < %d || %u + %u > %u\n",
			  bloc->logicalBlockNum, 0,
			  bloc->logicalBlockNum, count,
			  partmap->s_partition_len);
		goto error_return;
	}

	block = bloc->logicalBlockNum + offset +
		(sizeof(struct spaceBitmapDesc) << 3);

	do {
		overflow = 0;
		block_group = block >> (sb->s_blocksize_bits + 3);
		bit = block % (sb->s_blocksize << 3);

		/*
		* Check to see if we are freeing blocks across a group boundary.
		*/
		if (bit + count > (sb->s_blocksize << 3)) {
			overflow = bit + count - (sb->s_blocksize << 3);
			count -= overflow;
		}
		bitmap_nr = load_block_bitmap(sb, bitmap, block_group);
		if (bitmap_nr < 0)
			goto error_return;

		bh = bitmap->s_block_bitmap[bitmap_nr];
		for (i = 0; i < count; i++) {
			if (udf_set_bit(bit + i, bh->b_data)) {
				udf_debug("bit %lu already set\n", bit + i);
				udf_debug("byte=%2x\n",
					  ((__u8 *)bh->b_data)[(bit + i) >> 3]);
			}
		}
		udf_add_free_space(sb, sbi->s_partition, count);
		mark_buffer_dirty(bh);
		if (overflow) {
			block += count;
			count = overflow;
		}
	} while (overflow);

error_return:
	mutex_unlock(&sbi->s_alloc_mutex);
}

static int udf_bitmap_prealloc_blocks(struct super_block *sb,
				      struct udf_bitmap *bitmap,
				      uint16_t partition, uint32_t first_block,
				      uint32_t block_count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	int alloc_count = 0;
	int bit, block, block_group;
	int bitmap_nr;
	struct buffer_head *bh;
	__u32 part_len;

	mutex_lock(&sbi->s_alloc_mutex);
	part_len = sbi->s_partmaps[partition].s_partition_len;
	if (first_block >= part_len)
		goto out;

	if (first_block + block_count > part_len)
		block_count = part_len - first_block;

	do {
		block = first_block + (sizeof(struct spaceBitmapDesc) << 3);
		block_group = block >> (sb->s_blocksize_bits + 3);

		bitmap_nr = load_block_bitmap(sb, bitmap, block_group);
		if (bitmap_nr < 0)
			goto out;
		bh = bitmap->s_block_bitmap[bitmap_nr];

		bit = block % (sb->s_blocksize << 3);

		while (bit < (sb->s_blocksize << 3) && block_count > 0) {
			if (!udf_clear_bit(bit, bh->b_data))
				goto out;
			block_count--;
			alloc_count++;
			bit++;
			block++;
		}
		mark_buffer_dirty(bh);
	} while (block_count > 0);

out:
	udf_add_free_space(sb, partition, -alloc_count);
	mutex_unlock(&sbi->s_alloc_mutex);
	return alloc_count;
}

static udf_pblk_t udf_bitmap_new_block(struct super_block *sb,
				struct udf_bitmap *bitmap, uint16_t partition,
				uint32_t goal, int *err)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	int newbit, bit = 0;
	udf_pblk_t block;
	int block_group, group_start;
	int end_goal, nr_groups, bitmap_nr, i;
	struct buffer_head *bh = NULL;
	char *ptr;
	udf_pblk_t newblock = 0;

	*err = -ENOSPC;
	mutex_lock(&sbi->s_alloc_mutex);

repeat:
	if (goal >= sbi->s_partmaps[partition].s_partition_len)
		goal = 0;

	nr_groups = bitmap->s_nr_groups;
	block = goal + (sizeof(struct spaceBitmapDesc) << 3);
	block_group = block >> (sb->s_blocksize_bits + 3);
	group_start = block_group ? 0 : sizeof(struct spaceBitmapDesc);

	bitmap_nr = load_block_bitmap(sb, bitmap, block_group);
	if (bitmap_nr < 0)
		goto error_return;
	bh = bitmap->s_block_bitmap[bitmap_nr];
	ptr = memscan((char *)bh->b_data + group_start, 0xFF,
		      sb->s_blocksize - group_start);

	if ((ptr - ((char *)bh->b_data)) < sb->s_blocksize) {
		bit = block % (sb->s_blocksize << 3);
		if (udf_test_bit(bit, bh->b_data))
			goto got_block;

		end_goal = (bit + 63) & ~63;
		bit = udf_find_next_one_bit(bh->b_data, end_goal, bit);
		if (bit < end_goal)
			goto got_block;

		ptr = memscan((char *)bh->b_data + (bit >> 3), 0xFF,
			      sb->s_blocksize - ((bit + 7) >> 3));
		newbit = (ptr - ((char *)bh->b_data)) << 3;
		if (newbit < sb->s_blocksize << 3) {
			bit = newbit;
			goto search_back;
		}

		newbit = udf_find_next_one_bit(bh->b_data,
					       sb->s_blocksize << 3, bit);
		if (newbit < sb->s_blocksize << 3) {
			bit = newbit;
			goto got_block;
		}
	}

	for (i = 0; i < (nr_groups * 2); i++) {
		block_group++;
		if (block_group >= nr_groups)
			block_group = 0;
		group_start = block_group ? 0 : sizeof(struct spaceBitmapDesc);

		bitmap_nr = load_block_bitmap(sb, bitmap, block_group);
		if (bitmap_nr < 0)
			goto error_return;
		bh = bitmap->s_block_bitmap[bitmap_nr];
		if (i < nr_groups) {
			ptr = memscan((char *)bh->b_data + group_start, 0xFF,
				      sb->s_blocksize - group_start);
			if ((ptr - ((char *)bh->b_data)) < sb->s_blocksize) {
				bit = (ptr - ((char *)bh->b_data)) << 3;
				break;
			}
		} else {
			bit = udf_find_next_one_bit(bh->b_data,
						    sb->s_blocksize << 3,
						    group_start << 3);
			if (bit < sb->s_blocksize << 3)
				break;
		}
	}
	if (i >= (nr_groups * 2)) {
		mutex_unlock(&sbi->s_alloc_mutex);
		return newblock;
	}
	if (bit < sb->s_blocksize << 3)
		goto search_back;
	else
		bit = udf_find_next_one_bit(bh->b_data, sb->s_blocksize << 3,
					    group_start << 3);
	if (bit >= sb->s_blocksize << 3) {
		mutex_unlock(&sbi->s_alloc_mutex);
		return 0;
	}

search_back:
	i = 0;
	while (i < 7 && bit > (group_start << 3) &&
	       udf_test_bit(bit - 1, bh->b_data)) {
		++i;
		--bit;
	}

got_block:
	newblock = bit + (block_group << (sb->s_blocksize_bits + 3)) -
		(sizeof(struct spaceBitmapDesc) << 3);

	if (newblock >= sbi->s_partmaps[partition].s_partition_len) {
		/*
		 * Ran off the end of the bitmap, and bits following are
		 * non-compliant (not all zero)
		 */
		udf_err(sb, "bitmap for partition %d corrupted (block %u marked"
			" as free, partition length is %u)\n", partition,
			newblock, sbi->s_partmaps[partition].s_partition_len);
		goto error_return;
	}

	if (!udf_clear_bit(bit, bh->b_data)) {
		udf_debug("bit already cleared for block %d\n", bit);
		goto repeat;
	}

	mark_buffer_dirty(bh);

	udf_add_free_space(sb, partition, -1);
	mutex_unlock(&sbi->s_alloc_mutex);
	*err = 0;
	return newblock;

error_return:
	*err = -EIO;
	mutex_unlock(&sbi->s_alloc_mutex);
	return 0;
}

static void udf_table_free_blocks(struct super_block *sb,
				  struct inode *table,
				  struct kernel_lb_addr *bloc,
				  uint32_t offset,
				  uint32_t count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct udf_part_map *partmap;
	uint32_t start, end;
	uint32_t elen;
	struct kernel_lb_addr eloc;
	struct extent_position oepos, epos;
	int8_t etype;
	struct udf_inode_info *iinfo;

	mutex_lock(&sbi->s_alloc_mutex);
	partmap = &sbi->s_partmaps[bloc->partitionReferenceNum];
	if (bloc->logicalBlockNum + count < count ||
	    (bloc->logicalBlockNum + count) > partmap->s_partition_len) {
		udf_debug("%u < %d || %u + %u > %u\n",
			  bloc->logicalBlockNum, 0,
			  bloc->logicalBlockNum, count,
			  partmap->s_partition_len);
		goto error_return;
	}

	iinfo = UDF_I(table);
	udf_add_free_space(sb, sbi->s_partition, count);

	start = bloc->logicalBlockNum + offset;
	end = bloc->logicalBlockNum + offset + count - 1;

	epos.offset = oepos.offset = sizeof(struct unallocSpaceEntry);
	elen = 0;
	epos.block = oepos.block = iinfo->i_location;
	epos.bh = oepos.bh = NULL;

	while (count &&
	       (etype = udf_next_aext(table, &epos, &eloc, &elen, 1)) != -1) {
		if (((eloc.logicalBlockNum +
			(elen >> sb->s_blocksize_bits)) == start)) {
			if ((0x3FFFFFFF - elen) <
					(count << sb->s_blocksize_bits)) {
				uint32_t tmp = ((0x3FFFFFFF - elen) >>
							sb->s_blocksize_bits);
				count -= tmp;
				start += tmp;
				elen = (etype << 30) |
					(0x40000000 - sb->s_blocksize);
			} else {
				elen = (etype << 30) |
					(elen +
					(count << sb->s_blocksize_bits));
				start += count;
				count = 0;
			}
			udf_write_aext(table, &oepos, &eloc, elen, 1);
		} else if (eloc.logicalBlockNum == (end + 1)) {
			if ((0x3FFFFFFF - elen) <
					(count << sb->s_blocksize_bits)) {
				uint32_t tmp = ((0x3FFFFFFF - elen) >>
						sb->s_blocksize_bits);
				count -= tmp;
				end -= tmp;
				eloc.logicalBlockNum -= tmp;
				elen = (etype << 30) |
					(0x40000000 - sb->s_blocksize);
			} else {
				eloc.logicalBlockNum = start;
				elen = (etype << 30) |
					(elen +
					(count << sb->s_blocksize_bits));
				end -= count;
				count = 0;
			}
			udf_write_aext(table, &oepos, &eloc, elen, 1);
		}

		if (epos.bh != oepos.bh) {
			oepos.block = epos.block;
			brelse(oepos.bh);
			get_bh(epos.bh);
			oepos.bh = epos.bh;
			oepos.offset = 0;
		} else {
			oepos.offset = epos.offset;
		}
	}

	if (count) {
		/*
		 * NOTE: we CANNOT use udf_add_aext here, as it can try to
		 * allocate a new block, and since we hold the super block
		 * lock already very bad things would happen :)
		 *
		 * We copy the behavior of udf_add_aext, but instead of
		 * trying to allocate a new block close to the existing one,
		 * we just steal a block from the extent we are trying to add.
		 *
		 * It would be nice if the blocks were close together, but it
		 * isn't required.
		 */

		int adsize;

		eloc.logicalBlockNum = start;
		elen = EXT_RECORDED_ALLOCATED |
			(count << sb->s_blocksize_bits);

		if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
			adsize = sizeof(struct short_ad);
		else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
			adsize = sizeof(struct long_ad);
		else {
			brelse(oepos.bh);
			brelse(epos.bh);
			goto error_return;
		}

		if (epos.offset + (2 * adsize) > sb->s_blocksize) {
			/* Steal a block from the extent being free'd */
			udf_setup_indirect_aext(table, eloc.logicalBlockNum,
						&epos);

			eloc.logicalBlockNum++;
			elen -= sb->s_blocksize;
		}

		/* It's possible that stealing the block emptied the extent */
		if (elen)
			__udf_add_aext(table, &epos, &eloc, elen, 1);
	}

	brelse(epos.bh);
	brelse(oepos.bh);

error_return:
	mutex_unlock(&sbi->s_alloc_mutex);
	return;
}

static int udf_table_prealloc_blocks(struct super_block *sb,
				     struct inode *table, uint16_t partition,
				     uint32_t first_block, uint32_t block_count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	int alloc_count = 0;
	uint32_t elen, adsize;
	struct kernel_lb_addr eloc;
	struct extent_position epos;
	int8_t etype = -1;
	struct udf_inode_info *iinfo;

	if (first_block >= sbi->s_partmaps[partition].s_partition_len)
		return 0;

	iinfo = UDF_I(table);
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		return 0;

	mutex_lock(&sbi->s_alloc_mutex);
	epos.offset = sizeof(struct unallocSpaceEntry);
	epos.block = iinfo->i_location;
	epos.bh = NULL;
	eloc.logicalBlockNum = 0xFFFFFFFF;

	while (first_block != eloc.logicalBlockNum &&
	       (etype = udf_next_aext(table, &epos, &eloc, &elen, 1)) != -1) {
		udf_debug("eloc=%u, elen=%u, first_block=%u\n",
			  eloc.logicalBlockNum, elen, first_block);
		; /* empty loop body */
	}

	if (first_block == eloc.logicalBlockNum) {
		epos.offset -= adsize;

		alloc_count = (elen >> sb->s_blocksize_bits);
		if (alloc_count > block_count) {
			alloc_count = block_count;
			eloc.logicalBlockNum += alloc_count;
			elen -= (alloc_count << sb->s_blocksize_bits);
			udf_write_aext(table, &epos, &eloc,
					(etype << 30) | elen, 1);
		} else
			udf_delete_aext(table, epos);
	} else {
		alloc_count = 0;
	}

	brelse(epos.bh);

	if (alloc_count)
		udf_add_free_space(sb, partition, -alloc_count);
	mutex_unlock(&sbi->s_alloc_mutex);
	return alloc_count;
}

static udf_pblk_t udf_table_new_block(struct super_block *sb,
			       struct inode *table, uint16_t partition,
			       uint32_t goal, int *err)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	uint32_t spread = 0xFFFFFFFF, nspread = 0xFFFFFFFF;
	udf_pblk_t newblock = 0;
	uint32_t adsize;
	uint32_t elen, goal_elen = 0;
	struct kernel_lb_addr eloc, goal_eloc;
	struct extent_position epos, goal_epos;
	int8_t etype;
	struct udf_inode_info *iinfo = UDF_I(table);

	*err = -ENOSPC;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		return newblock;

	mutex_lock(&sbi->s_alloc_mutex);
	if (goal >= sbi->s_partmaps[partition].s_partition_len)
		goal = 0;

	/* We search for the closest matching block to goal. If we find
	   a exact hit, we stop. Otherwise we keep going till we run out
	   of extents. We store the buffer_head, bloc, and extoffset
	   of the current closest match and use that when we are done.
	 */
	epos.offset = sizeof(struct unallocSpaceEntry);
	epos.block = iinfo->i_location;
	epos.bh = goal_epos.bh = NULL;

	while (spread &&
	       (etype = udf_next_aext(table, &epos, &eloc, &elen, 1)) != -1) {
		if (goal >= eloc.logicalBlockNum) {
			if (goal < eloc.logicalBlockNum +
					(elen >> sb->s_blocksize_bits))
				nspread = 0;
			else
				nspread = goal - eloc.logicalBlockNum -
					(elen >> sb->s_blocksize_bits);
		} else {
			nspread = eloc.logicalBlockNum - goal;
		}

		if (nspread < spread) {
			spread = nspread;
			if (goal_epos.bh != epos.bh) {
				brelse(goal_epos.bh);
				goal_epos.bh = epos.bh;
				get_bh(goal_epos.bh);
			}
			goal_epos.block = epos.block;
			goal_epos.offset = epos.offset - adsize;
			goal_eloc = eloc;
			goal_elen = (etype << 30) | elen;
		}
	}

	brelse(epos.bh);

	if (spread == 0xFFFFFFFF) {
		brelse(goal_epos.bh);
		mutex_unlock(&sbi->s_alloc_mutex);
		return 0;
	}

	/* Only allocate blocks from the beginning of the extent.
	   That way, we only delete (empty) extents, never have to insert an
	   extent because of splitting */
	/* This works, but very poorly.... */

	newblock = goal_eloc.logicalBlockNum;
	goal_eloc.logicalBlockNum++;
	goal_elen -= sb->s_blocksize;

	if (goal_elen)
		udf_write_aext(table, &goal_epos, &goal_eloc, goal_elen, 1);
	else
		udf_delete_aext(table, goal_epos);
	brelse(goal_epos.bh);

	udf_add_free_space(sb, partition, -1);

	mutex_unlock(&sbi->s_alloc_mutex);
	*err = 0;
	return newblock;
}

void udf_free_blocks(struct super_block *sb, struct inode *inode,
		     struct kernel_lb_addr *bloc, uint32_t offset,
		     uint32_t count)
{
	uint16_t partition = bloc->partitionReferenceNum;
	struct udf_part_map *map = &UDF_SB(sb)->s_partmaps[partition];

	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_BITMAP) {
		udf_bitmap_free_blocks(sb, map->s_uspace.s_bitmap,
				       bloc, offset, count);
	} else if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_TABLE) {
		udf_table_free_blocks(sb, map->s_uspace.s_table,
				      bloc, offset, count);
	}

	if (inode) {
		inode_sub_bytes(inode,
				((sector_t)count) << sb->s_blocksize_bits);
	}
}

inline int udf_prealloc_blocks(struct super_block *sb,
			       struct inode *inode,
			       uint16_t partition, uint32_t first_block,
			       uint32_t block_count)
{
	struct udf_part_map *map = &UDF_SB(sb)->s_partmaps[partition];
	int allocated;

	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_BITMAP)
		allocated = udf_bitmap_prealloc_blocks(sb,
						       map->s_uspace.s_bitmap,
						       partition, first_block,
						       block_count);
	else if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_TABLE)
		allocated = udf_table_prealloc_blocks(sb,
						      map->s_uspace.s_table,
						      partition, first_block,
						      block_count);
	else
		return 0;

	if (inode && allocated > 0)
		inode_add_bytes(inode, allocated << sb->s_blocksize_bits);
	return allocated;
}

inline udf_pblk_t udf_new_block(struct super_block *sb,
			 struct inode *inode,
			 uint16_t partition, uint32_t goal, int *err)
{
	struct udf_part_map *map = &UDF_SB(sb)->s_partmaps[partition];
	udf_pblk_t block;

	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_BITMAP)
		block = udf_bitmap_new_block(sb,
					     map->s_uspace.s_bitmap,
					     partition, goal, err);
	else if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_TABLE)
		block = udf_table_new_block(sb,
					    map->s_uspace.s_table,
					    partition, goal, err);
	else {
		*err = -EIO;
		return 0;
	}
	if (inode && block)
		inode_add_bytes(inode, sb->s_blocksize);
	return block;
}
