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

#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/bitops.h>

#include "udf_i.h"
#include "udf_sb.h"

#define udf_clear_bit(nr, addr) ext2_clear_bit(nr, addr)
#define udf_set_bit(nr, addr) ext2_set_bit(nr, addr)
#define udf_test_bit(nr, addr) ext2_test_bit(nr, addr)
#define udf_find_first_one_bit(addr, size) find_first_one_bit(addr, size)
#define udf_find_next_one_bit(addr, size, offset) \
		find_next_one_bit(addr, size, offset)

#define leBPL_to_cpup(x) leNUM_to_cpup(BITS_PER_LONG, x)
#define leNUM_to_cpup(x, y) xleNUM_to_cpup(x, y)
#define xleNUM_to_cpup(x, y) (le ## x ## _to_cpup(y))
#define uintBPL_t uint(BITS_PER_LONG)
#define uint(x) xuint(x)
#define xuint(x) __le ## x

static inline int find_next_one_bit(void *addr, int size, int offset)
{
	uintBPL_t *p = ((uintBPL_t *) addr) + (offset / BITS_PER_LONG);
	int result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= (BITS_PER_LONG - 1);
	if (offset) {
		tmp = leBPL_to_cpup(p++);
		tmp &= ~0UL << offset;
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG - 1)) {
		tmp = leBPL_to_cpup(p++);
		if (tmp)
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = leBPL_to_cpup(p);
found_first:
	tmp &= ~0UL >> (BITS_PER_LONG - size);
found_middle:
	return result + ffz(~tmp);
}

#define find_first_one_bit(addr, size)\
	find_next_one_bit((addr), (size), 0)

static int read_block_bitmap(struct super_block *sb,
			     struct udf_bitmap *bitmap, unsigned int block,
			     unsigned long bitmap_nr)
{
	struct buffer_head *bh = NULL;
	int retval = 0;
	kernel_lb_addr loc;

	loc.logicalBlockNum = bitmap->s_extPosition;
	loc.partitionReferenceNum = UDF_SB(sb)->s_partition;

	bh = udf_tread(sb, udf_get_lb_pblock(sb, loc, block));
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
		udf_debug("block_group (%d) > nr_groups (%d)\n", block_group,
			  nr_groups);
	}

	if (bitmap->s_block_bitmap[block_group]) {
		return block_group;
	} else {
		retval = read_block_bitmap(sb, bitmap, block_group,
					   block_group);
		if (retval < 0)
			return retval;
		return block_group;
	}
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

static void udf_bitmap_free_blocks(struct super_block *sb,
				   struct inode *inode,
				   struct udf_bitmap *bitmap,
				   kernel_lb_addr bloc, uint32_t offset,
				   uint32_t count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct buffer_head *bh = NULL;
	unsigned long block;
	unsigned long block_group;
	unsigned long bit;
	unsigned long i;
	int bitmap_nr;
	unsigned long overflow;

	mutex_lock(&sbi->s_alloc_mutex);
	if (bloc.logicalBlockNum < 0 ||
	    (bloc.logicalBlockNum + count) >
		sbi->s_partmaps[bloc.partitionReferenceNum].s_partition_len) {
		udf_debug("%d < %d || %d + %d > %d\n",
			  bloc.logicalBlockNum, 0, bloc.logicalBlockNum, count,
			  sbi->s_partmaps[bloc.partitionReferenceNum].
							s_partition_len);
		goto error_return;
	}

	block = bloc.logicalBlockNum + offset +
		(sizeof(struct spaceBitmapDesc) << 3);

do_more:
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
			udf_debug("bit %ld already set\n", bit + i);
			udf_debug("byte=%2x\n",
				  ((char *)bh->b_data)[(bit + i) >> 3]);
		} else {
			if (inode)
				DQUOT_FREE_BLOCK(inode, 1);
			if (sbi->s_lvid_bh) {
				struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
				lvid->freeSpaceTable[sbi->s_partition] =
					cpu_to_le32(le32_to_cpu(lvid->freeSpaceTable[sbi->s_partition]) + 1);
			}
		}
	}
	mark_buffer_dirty(bh);
	if (overflow) {
		block += count;
		count = overflow;
		goto do_more;
	}
error_return:
	sb->s_dirt = 1;
	if (sbi->s_lvid_bh)
		mark_buffer_dirty(sbi->s_lvid_bh);
	mutex_unlock(&sbi->s_alloc_mutex);
	return;
}

static int udf_bitmap_prealloc_blocks(struct super_block *sb,
				      struct inode *inode,
				      struct udf_bitmap *bitmap,
				      uint16_t partition, uint32_t first_block,
				      uint32_t block_count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	int alloc_count = 0;
	int bit, block, block_group, group_start;
	int nr_groups, bitmap_nr;
	struct buffer_head *bh;
	__u32 part_len;

	mutex_lock(&sbi->s_alloc_mutex);
	part_len = sbi->s_partmaps[partition].s_partition_len;
	if (first_block < 0 || first_block >= part_len)
		goto out;

	if (first_block + block_count > part_len)
		block_count = part_len - first_block;

repeat:
	nr_groups = udf_compute_nr_groups(sb, partition);
	block = first_block + (sizeof(struct spaceBitmapDesc) << 3);
	block_group = block >> (sb->s_blocksize_bits + 3);
	group_start = block_group ? 0 : sizeof(struct spaceBitmapDesc);

	bitmap_nr = load_block_bitmap(sb, bitmap, block_group);
	if (bitmap_nr < 0)
		goto out;
	bh = bitmap->s_block_bitmap[bitmap_nr];

	bit = block % (sb->s_blocksize << 3);

	while (bit < (sb->s_blocksize << 3) && block_count > 0) {
		if (!udf_test_bit(bit, bh->b_data)) {
			goto out;
		} else if (DQUOT_PREALLOC_BLOCK(inode, 1)) {
			goto out;
		} else if (!udf_clear_bit(bit, bh->b_data)) {
			udf_debug("bit already cleared for block %d\n", bit);
			DQUOT_FREE_BLOCK(inode, 1);
			goto out;
		}
		block_count--;
		alloc_count++;
		bit++;
		block++;
	}
	mark_buffer_dirty(bh);
	if (block_count > 0)
		goto repeat;
out:
	if (sbi->s_lvid_bh) {
		struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
		lvid->freeSpaceTable[partition] =
			cpu_to_le32(le32_to_cpu(lvid->freeSpaceTable[partition]) - alloc_count);
		mark_buffer_dirty(sbi->s_lvid_bh);
	}
	sb->s_dirt = 1;
	mutex_unlock(&sbi->s_alloc_mutex);
	return alloc_count;
}

static int udf_bitmap_new_block(struct super_block *sb,
				struct inode *inode,
				struct udf_bitmap *bitmap, uint16_t partition,
				uint32_t goal, int *err)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	int newbit, bit = 0, block, block_group, group_start;
	int end_goal, nr_groups, bitmap_nr, i;
	struct buffer_head *bh = NULL;
	char *ptr;
	int newblock = 0;

	*err = -ENOSPC;
	mutex_lock(&sbi->s_alloc_mutex);

repeat:
	if (goal < 0 || goal >= sbi->s_partmaps[partition].s_partition_len)
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
			bit = udf_find_next_one_bit((char *)bh->b_data,
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

	/*
	 * Check quota for allocation of this block.
	 */
	if (inode && DQUOT_ALLOC_BLOCK(inode, 1)) {
		mutex_unlock(&sbi->s_alloc_mutex);
		*err = -EDQUOT;
		return 0;
	}

	newblock = bit + (block_group << (sb->s_blocksize_bits + 3)) -
		(sizeof(struct spaceBitmapDesc) << 3);

	if (!udf_clear_bit(bit, bh->b_data)) {
		udf_debug("bit already cleared for block %d\n", bit);
		goto repeat;
	}

	mark_buffer_dirty(bh);

	if (sbi->s_lvid_bh) {
		struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
		lvid->freeSpaceTable[partition] =
			cpu_to_le32(le32_to_cpu(lvid->freeSpaceTable[partition]) - 1);
		mark_buffer_dirty(sbi->s_lvid_bh);
	}
	sb->s_dirt = 1;
	mutex_unlock(&sbi->s_alloc_mutex);
	*err = 0;
	return newblock;

error_return:
	*err = -EIO;
	mutex_unlock(&sbi->s_alloc_mutex);
	return 0;
}

static void udf_table_free_blocks(struct super_block *sb,
				  struct inode *inode,
				  struct inode *table,
				  kernel_lb_addr bloc, uint32_t offset,
				  uint32_t count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	uint32_t start, end;
	uint32_t elen;
	kernel_lb_addr eloc;
	struct extent_position oepos, epos;
	int8_t etype;
	int i;

	mutex_lock(&sbi->s_alloc_mutex);
	if (bloc.logicalBlockNum < 0 ||
	    (bloc.logicalBlockNum + count) >
		sbi->s_partmaps[bloc.partitionReferenceNum].s_partition_len) {
		udf_debug("%d < %d || %d + %d > %d\n",
			  bloc.logicalBlockNum, 0, bloc.logicalBlockNum, count,
			  sbi->s_partmaps[bloc.partitionReferenceNum].
							s_partition_len);
		goto error_return;
	}

	/* We do this up front - There are some error conditions that
	   could occure, but.. oh well */
	if (inode)
		DQUOT_FREE_BLOCK(inode, count);
	if (sbi->s_lvid_bh) {
		struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
		lvid->freeSpaceTable[sbi->s_partition] =
			cpu_to_le32(le32_to_cpu(lvid->freeSpaceTable[sbi->s_partition]) + count);
		mark_buffer_dirty(sbi->s_lvid_bh);
	}

	start = bloc.logicalBlockNum + offset;
	end = bloc.logicalBlockNum + offset + count - 1;

	epos.offset = oepos.offset = sizeof(struct unallocSpaceEntry);
	elen = 0;
	epos.block = oepos.block = UDF_I_LOCATION(table);
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
			udf_write_aext(table, &oepos, eloc, elen, 1);
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
			udf_write_aext(table, &oepos, eloc, elen, 1);
		}

		if (epos.bh != oepos.bh) {
			i = -1;
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
		short_ad *sad = NULL;
		long_ad *lad = NULL;
		struct allocExtDesc *aed;

		eloc.logicalBlockNum = start;
		elen = EXT_RECORDED_ALLOCATED |
			(count << sb->s_blocksize_bits);

		if (UDF_I_ALLOCTYPE(table) == ICBTAG_FLAG_AD_SHORT) {
			adsize = sizeof(short_ad);
		} else if (UDF_I_ALLOCTYPE(table) == ICBTAG_FLAG_AD_LONG) {
			adsize = sizeof(long_ad);
		} else {
			brelse(oepos.bh);
			brelse(epos.bh);
			goto error_return;
		}

		if (epos.offset + (2 * adsize) > sb->s_blocksize) {
			char *sptr, *dptr;
			int loffset;

			brelse(oepos.bh);
			oepos = epos;

			/* Steal a block from the extent being free'd */
			epos.block.logicalBlockNum = eloc.logicalBlockNum;
			eloc.logicalBlockNum++;
			elen -= sb->s_blocksize;

			epos.bh = udf_tread(sb,
					udf_get_lb_pblock(sb, epos.block, 0));
			if (!epos.bh) {
				brelse(oepos.bh);
				goto error_return;
			}
			aed = (struct allocExtDesc *)(epos.bh->b_data);
			aed->previousAllocExtLocation =
				cpu_to_le32(oepos.block.logicalBlockNum);
			if (epos.offset + adsize > sb->s_blocksize) {
				loffset = epos.offset;
				aed->lengthAllocDescs = cpu_to_le32(adsize);
				sptr = UDF_I_DATA(table) + epos.offset - adsize;
				dptr = epos.bh->b_data +
					sizeof(struct allocExtDesc);
				memcpy(dptr, sptr, adsize);
				epos.offset = sizeof(struct allocExtDesc) +
						adsize;
			} else {
				loffset = epos.offset + adsize;
				aed->lengthAllocDescs = cpu_to_le32(0);
				if (oepos.bh) {
					sptr = oepos.bh->b_data + epos.offset;
					aed = (struct allocExtDesc *)
						oepos.bh->b_data;
					aed->lengthAllocDescs =
						cpu_to_le32(le32_to_cpu(
							aed->lengthAllocDescs) +
								adsize);
				} else {
					sptr = UDF_I_DATA(table) + epos.offset;
					UDF_I_LENALLOC(table) += adsize;
					mark_inode_dirty(table);
				}
				epos.offset = sizeof(struct allocExtDesc);
			}
			if (sbi->s_udfrev >= 0x0200)
				udf_new_tag(epos.bh->b_data, TAG_IDENT_AED,
					    3, 1, epos.block.logicalBlockNum,
					    sizeof(tag));
			else
				udf_new_tag(epos.bh->b_data, TAG_IDENT_AED,
					    2, 1, epos.block.logicalBlockNum,
					    sizeof(tag));

			switch (UDF_I_ALLOCTYPE(table)) {
			case ICBTAG_FLAG_AD_SHORT:
				sad = (short_ad *)sptr;
				sad->extLength = cpu_to_le32(
					EXT_NEXT_EXTENT_ALLOCDECS |
					sb->s_blocksize);
				sad->extPosition =
					cpu_to_le32(epos.block.logicalBlockNum);
				break;
			case ICBTAG_FLAG_AD_LONG:
				lad = (long_ad *)sptr;
				lad->extLength = cpu_to_le32(
					EXT_NEXT_EXTENT_ALLOCDECS |
					sb->s_blocksize);
				lad->extLocation =
					cpu_to_lelb(epos.block);
				break;
			}
			if (oepos.bh) {
				udf_update_tag(oepos.bh->b_data, loffset);
				mark_buffer_dirty(oepos.bh);
			} else {
				mark_inode_dirty(table);
			}
		}

		/* It's possible that stealing the block emptied the extent */
		if (elen) {
			udf_write_aext(table, &epos, eloc, elen, 1);

			if (!epos.bh) {
				UDF_I_LENALLOC(table) += adsize;
				mark_inode_dirty(table);
			} else {
				aed = (struct allocExtDesc *)epos.bh->b_data;
				aed->lengthAllocDescs =
					cpu_to_le32(le32_to_cpu(
					    aed->lengthAllocDescs) + adsize);
				udf_update_tag(epos.bh->b_data, epos.offset);
				mark_buffer_dirty(epos.bh);
			}
		}
	}

	brelse(epos.bh);
	brelse(oepos.bh);

error_return:
	sb->s_dirt = 1;
	mutex_unlock(&sbi->s_alloc_mutex);
	return;
}

static int udf_table_prealloc_blocks(struct super_block *sb,
				     struct inode *inode,
				     struct inode *table, uint16_t partition,
				     uint32_t first_block, uint32_t block_count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	int alloc_count = 0;
	uint32_t elen, adsize;
	kernel_lb_addr eloc;
	struct extent_position epos;
	int8_t etype = -1;

	if (first_block < 0 ||
		first_block >= sbi->s_partmaps[partition].s_partition_len)
		return 0;

	if (UDF_I_ALLOCTYPE(table) == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(table) == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(long_ad);
	else
		return 0;

	mutex_lock(&sbi->s_alloc_mutex);
	epos.offset = sizeof(struct unallocSpaceEntry);
	epos.block = UDF_I_LOCATION(table);
	epos.bh = NULL;
	eloc.logicalBlockNum = 0xFFFFFFFF;

	while (first_block != eloc.logicalBlockNum &&
	       (etype = udf_next_aext(table, &epos, &eloc, &elen, 1)) != -1) {
		udf_debug("eloc=%d, elen=%d, first_block=%d\n",
			  eloc.logicalBlockNum, elen, first_block);
		; /* empty loop body */
	}

	if (first_block == eloc.logicalBlockNum) {
		epos.offset -= adsize;

		alloc_count = (elen >> sb->s_blocksize_bits);
		if (inode && DQUOT_PREALLOC_BLOCK(inode,
			alloc_count > block_count ? block_count : alloc_count))
			alloc_count = 0;
		else if (alloc_count > block_count) {
			alloc_count = block_count;
			eloc.logicalBlockNum += alloc_count;
			elen -= (alloc_count << sb->s_blocksize_bits);
			udf_write_aext(table, &epos, eloc,
					(etype << 30) | elen, 1);
		} else
			udf_delete_aext(table, epos, eloc,
					(etype << 30) | elen);
	} else {
		alloc_count = 0;
	}

	brelse(epos.bh);

	if (alloc_count && sbi->s_lvid_bh) {
		struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
		lvid->freeSpaceTable[partition] =
			cpu_to_le32(le32_to_cpu(lvid->freeSpaceTable[partition]) - alloc_count);
		mark_buffer_dirty(sbi->s_lvid_bh);
		sb->s_dirt = 1;
	}
	mutex_unlock(&sbi->s_alloc_mutex);
	return alloc_count;
}

static int udf_table_new_block(struct super_block *sb,
			       struct inode *inode,
			       struct inode *table, uint16_t partition,
			       uint32_t goal, int *err)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	uint32_t spread = 0xFFFFFFFF, nspread = 0xFFFFFFFF;
	uint32_t newblock = 0, adsize;
	uint32_t elen, goal_elen = 0;
	kernel_lb_addr eloc, uninitialized_var(goal_eloc);
	struct extent_position epos, goal_epos;
	int8_t etype;

	*err = -ENOSPC;

	if (UDF_I_ALLOCTYPE(table) == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(table) == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(long_ad);
	else
		return newblock;

	mutex_lock(&sbi->s_alloc_mutex);
	if (goal < 0 || goal >= sbi->s_partmaps[partition].s_partition_len)
		goal = 0;

	/* We search for the closest matching block to goal. If we find
	   a exact hit, we stop. Otherwise we keep going till we run out
	   of extents. We store the buffer_head, bloc, and extoffset
	   of the current closest match and use that when we are done.
	 */
	epos.offset = sizeof(struct unallocSpaceEntry);
	epos.block = UDF_I_LOCATION(table);
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

	if (inode && DQUOT_ALLOC_BLOCK(inode, 1)) {
		brelse(goal_epos.bh);
		mutex_unlock(&sbi->s_alloc_mutex);
		*err = -EDQUOT;
		return 0;
	}

	if (goal_elen)
		udf_write_aext(table, &goal_epos, goal_eloc, goal_elen, 1);
	else
		udf_delete_aext(table, goal_epos, goal_eloc, goal_elen);
	brelse(goal_epos.bh);

	if (sbi->s_lvid_bh) {
		struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
		lvid->freeSpaceTable[partition] =
			cpu_to_le32(le32_to_cpu(lvid->freeSpaceTable[partition]) - 1);
		mark_buffer_dirty(sbi->s_lvid_bh);
	}

	sb->s_dirt = 1;
	mutex_unlock(&sbi->s_alloc_mutex);
	*err = 0;
	return newblock;
}

inline void udf_free_blocks(struct super_block *sb,
			    struct inode *inode,
			    kernel_lb_addr bloc, uint32_t offset,
			    uint32_t count)
{
	uint16_t partition = bloc.partitionReferenceNum;
	struct udf_part_map *map = &UDF_SB(sb)->s_partmaps[partition];

	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_BITMAP) {
		return udf_bitmap_free_blocks(sb, inode,
					      map->s_uspace.s_bitmap,
					      bloc, offset, count);
	} else if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_TABLE) {
		return udf_table_free_blocks(sb, inode,
					     map->s_uspace.s_table,
					     bloc, offset, count);
	} else if (map->s_partition_flags & UDF_PART_FLAG_FREED_BITMAP) {
		return udf_bitmap_free_blocks(sb, inode,
					      map->s_fspace.s_bitmap,
					      bloc, offset, count);
	} else if (map->s_partition_flags & UDF_PART_FLAG_FREED_TABLE) {
		return udf_table_free_blocks(sb, inode,
					     map->s_fspace.s_table,
					     bloc, offset, count);
	} else {
		return;
	}
}

inline int udf_prealloc_blocks(struct super_block *sb,
			       struct inode *inode,
			       uint16_t partition, uint32_t first_block,
			       uint32_t block_count)
{
	struct udf_part_map *map = &UDF_SB(sb)->s_partmaps[partition];

	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_BITMAP)
		return udf_bitmap_prealloc_blocks(sb, inode,
						  map->s_uspace.s_bitmap,
						  partition, first_block,
						  block_count);
	else if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_TABLE)
		return udf_table_prealloc_blocks(sb, inode,
						 map->s_uspace.s_table,
						 partition, first_block,
						 block_count);
	else if (map->s_partition_flags & UDF_PART_FLAG_FREED_BITMAP)
		return udf_bitmap_prealloc_blocks(sb, inode,
						  map->s_fspace.s_bitmap,
						  partition, first_block,
						  block_count);
	else if (map->s_partition_flags & UDF_PART_FLAG_FREED_TABLE)
		return udf_table_prealloc_blocks(sb, inode,
						 map->s_fspace.s_table,
						 partition, first_block,
						 block_count);
	else
		return 0;
}

inline int udf_new_block(struct super_block *sb,
			 struct inode *inode,
			 uint16_t partition, uint32_t goal, int *err)
{
	struct udf_part_map *map = &UDF_SB(sb)->s_partmaps[partition];

	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_BITMAP)
		return udf_bitmap_new_block(sb, inode,
					   map->s_uspace.s_bitmap,
					   partition, goal, err);
	else if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_TABLE)
		return udf_table_new_block(sb, inode,
					   map->s_uspace.s_table,
					   partition, goal, err);
	else if (map->s_partition_flags & UDF_PART_FLAG_FREED_BITMAP)
		return udf_bitmap_new_block(sb, inode,
					    map->s_fspace.s_bitmap,
					    partition, goal, err);
	else if (map->s_partition_flags & UDF_PART_FLAG_FREED_TABLE)
		return udf_table_new_block(sb, inode,
					   map->s_fspace.s_table,
					   partition, goal, err);
	else {
		*err = -EIO;
		return 0;
	}
}
