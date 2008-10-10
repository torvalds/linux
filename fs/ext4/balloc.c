/*
 *  linux/fs/ext4/balloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  Enhanced block allocation by Stephen Tweedie (sct@redhat.com), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/time.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/jbd2.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include "ext4.h"
#include "ext4_jbd2.h"
#include "group.h"

/*
 * balloc.c contains the blocks allocation and deallocation routines
 */

/*
 * Calculate the block group number and offset, given a block number
 */
void ext4_get_group_no_and_offset(struct super_block *sb, ext4_fsblk_t blocknr,
		ext4_group_t *blockgrpp, ext4_grpblk_t *offsetp)
{
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;
	ext4_grpblk_t offset;

	blocknr = blocknr - le32_to_cpu(es->s_first_data_block);
	offset = do_div(blocknr, EXT4_BLOCKS_PER_GROUP(sb));
	if (offsetp)
		*offsetp = offset;
	if (blockgrpp)
		*blockgrpp = blocknr;

}

static int ext4_block_in_group(struct super_block *sb, ext4_fsblk_t block,
			ext4_group_t block_group)
{
	ext4_group_t actual_group;
	ext4_get_group_no_and_offset(sb, block, &actual_group, NULL);
	if (actual_group == block_group)
		return 1;
	return 0;
}

static int ext4_group_used_meta_blocks(struct super_block *sb,
				ext4_group_t block_group)
{
	ext4_fsblk_t tmp;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	/* block bitmap, inode bitmap, and inode table blocks */
	int used_blocks = sbi->s_itb_per_group + 2;

	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_FLEX_BG)) {
		struct ext4_group_desc *gdp;
		struct buffer_head *bh;

		gdp = ext4_get_group_desc(sb, block_group, &bh);
		if (!ext4_block_in_group(sb, ext4_block_bitmap(sb, gdp),
					block_group))
			used_blocks--;

		if (!ext4_block_in_group(sb, ext4_inode_bitmap(sb, gdp),
					block_group))
			used_blocks--;

		tmp = ext4_inode_table(sb, gdp);
		for (; tmp < ext4_inode_table(sb, gdp) +
				sbi->s_itb_per_group; tmp++) {
			if (!ext4_block_in_group(sb, tmp, block_group))
				used_blocks -= 1;
		}
	}
	return used_blocks;
}

/* Initializes an uninitialized block bitmap if given, and returns the
 * number of blocks free in the group. */
unsigned ext4_init_block_bitmap(struct super_block *sb, struct buffer_head *bh,
		 ext4_group_t block_group, struct ext4_group_desc *gdp)
{
	int bit, bit_max;
	unsigned free_blocks, group_blocks;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (bh) {
		J_ASSERT_BH(bh, buffer_locked(bh));

		/* If checksum is bad mark all blocks used to prevent allocation
		 * essentially implementing a per-group read-only flag. */
		if (!ext4_group_desc_csum_verify(sbi, block_group, gdp)) {
			ext4_error(sb, __func__,
				  "Checksum bad for group %lu\n", block_group);
			gdp->bg_free_blocks_count = 0;
			gdp->bg_free_inodes_count = 0;
			gdp->bg_itable_unused = 0;
			memset(bh->b_data, 0xff, sb->s_blocksize);
			return 0;
		}
		memset(bh->b_data, 0, sb->s_blocksize);
	}

	/* Check for superblock and gdt backups in this group */
	bit_max = ext4_bg_has_super(sb, block_group);

	if (!EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_META_BG) ||
	    block_group < le32_to_cpu(sbi->s_es->s_first_meta_bg) *
			  sbi->s_desc_per_block) {
		if (bit_max) {
			bit_max += ext4_bg_num_gdb(sb, block_group);
			bit_max +=
				le16_to_cpu(sbi->s_es->s_reserved_gdt_blocks);
		}
	} else { /* For META_BG_BLOCK_GROUPS */
		bit_max += ext4_bg_num_gdb(sb, block_group);
	}

	if (block_group == sbi->s_groups_count - 1) {
		/*
		 * Even though mke2fs always initialize first and last group
		 * if some other tool enabled the EXT4_BG_BLOCK_UNINIT we need
		 * to make sure we calculate the right free blocks
		 */
		group_blocks = ext4_blocks_count(sbi->s_es) -
			le32_to_cpu(sbi->s_es->s_first_data_block) -
			(EXT4_BLOCKS_PER_GROUP(sb) * (sbi->s_groups_count - 1));
	} else {
		group_blocks = EXT4_BLOCKS_PER_GROUP(sb);
	}

	free_blocks = group_blocks - bit_max;

	if (bh) {
		ext4_fsblk_t start, tmp;
		int flex_bg = 0;

		for (bit = 0; bit < bit_max; bit++)
			ext4_set_bit(bit, bh->b_data);

		start = ext4_group_first_block_no(sb, block_group);

		if (EXT4_HAS_INCOMPAT_FEATURE(sb,
					      EXT4_FEATURE_INCOMPAT_FLEX_BG))
			flex_bg = 1;

		/* Set bits for block and inode bitmaps, and inode table */
		tmp = ext4_block_bitmap(sb, gdp);
		if (!flex_bg || ext4_block_in_group(sb, tmp, block_group))
			ext4_set_bit(tmp - start, bh->b_data);

		tmp = ext4_inode_bitmap(sb, gdp);
		if (!flex_bg || ext4_block_in_group(sb, tmp, block_group))
			ext4_set_bit(tmp - start, bh->b_data);

		tmp = ext4_inode_table(sb, gdp);
		for (; tmp < ext4_inode_table(sb, gdp) +
				sbi->s_itb_per_group; tmp++) {
			if (!flex_bg ||
				ext4_block_in_group(sb, tmp, block_group))
				ext4_set_bit(tmp - start, bh->b_data);
		}
		/*
		 * Also if the number of blocks within the group is
		 * less than the blocksize * 8 ( which is the size
		 * of bitmap ), set rest of the block bitmap to 1
		 */
		mark_bitmap_end(group_blocks, sb->s_blocksize * 8, bh->b_data);
	}
	return free_blocks - ext4_group_used_meta_blocks(sb, block_group);
}


/*
 * The free blocks are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.  The descriptors are loaded in memory
 * when a file system is mounted (see ext4_fill_super).
 */


#define in_range(b, first, len)	((b) >= (first) && (b) <= (first) + (len) - 1)

/**
 * ext4_get_group_desc() -- load group descriptor from disk
 * @sb:			super block
 * @block_group:	given block group
 * @bh:			pointer to the buffer head to store the block
 *			group descriptor
 */
struct ext4_group_desc * ext4_get_group_desc(struct super_block *sb,
					     ext4_group_t block_group,
					     struct buffer_head **bh)
{
	unsigned long group_desc;
	unsigned long offset;
	struct ext4_group_desc *desc;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (block_group >= sbi->s_groups_count) {
		ext4_error(sb, "ext4_get_group_desc",
			   "block_group >= groups_count - "
			   "block_group = %lu, groups_count = %lu",
			   block_group, sbi->s_groups_count);

		return NULL;
	}
	smp_rmb();

	group_desc = block_group >> EXT4_DESC_PER_BLOCK_BITS(sb);
	offset = block_group & (EXT4_DESC_PER_BLOCK(sb) - 1);
	if (!sbi->s_group_desc[group_desc]) {
		ext4_error(sb, "ext4_get_group_desc",
			   "Group descriptor not loaded - "
			   "block_group = %lu, group_desc = %lu, desc = %lu",
			   block_group, group_desc, offset);
		return NULL;
	}

	desc = (struct ext4_group_desc *)(
		(__u8 *)sbi->s_group_desc[group_desc]->b_data +
		offset * EXT4_DESC_SIZE(sb));
	if (bh)
		*bh = sbi->s_group_desc[group_desc];
	return desc;
}

static int ext4_valid_block_bitmap(struct super_block *sb,
					struct ext4_group_desc *desc,
					unsigned int block_group,
					struct buffer_head *bh)
{
	ext4_grpblk_t offset;
	ext4_grpblk_t next_zero_bit;
	ext4_fsblk_t bitmap_blk;
	ext4_fsblk_t group_first_block;

	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_FLEX_BG)) {
		/* with FLEX_BG, the inode/block bitmaps and itable
		 * blocks may not be in the group at all
		 * so the bitmap validation will be skipped for those groups
		 * or it has to also read the block group where the bitmaps
		 * are located to verify they are set.
		 */
		return 1;
	}
	group_first_block = ext4_group_first_block_no(sb, block_group);

	/* check whether block bitmap block number is set */
	bitmap_blk = ext4_block_bitmap(sb, desc);
	offset = bitmap_blk - group_first_block;
	if (!ext4_test_bit(offset, bh->b_data))
		/* bad block bitmap */
		goto err_out;

	/* check whether the inode bitmap block number is set */
	bitmap_blk = ext4_inode_bitmap(sb, desc);
	offset = bitmap_blk - group_first_block;
	if (!ext4_test_bit(offset, bh->b_data))
		/* bad block bitmap */
		goto err_out;

	/* check whether the inode table block number is set */
	bitmap_blk = ext4_inode_table(sb, desc);
	offset = bitmap_blk - group_first_block;
	next_zero_bit = ext4_find_next_zero_bit(bh->b_data,
				offset + EXT4_SB(sb)->s_itb_per_group,
				offset);
	if (next_zero_bit >= offset + EXT4_SB(sb)->s_itb_per_group)
		/* good bitmap for inode tables */
		return 1;

err_out:
	ext4_error(sb, __func__,
			"Invalid block bitmap - "
			"block_group = %d, block = %llu",
			block_group, bitmap_blk);
	return 0;
}
/**
 * ext4_read_block_bitmap()
 * @sb:			super block
 * @block_group:	given block group
 *
 * Read the bitmap for a given block_group,and validate the
 * bits for block/inode/inode tables are set in the bitmaps
 *
 * Return buffer_head on success or NULL in case of failure.
 */
struct buffer_head *
ext4_read_block_bitmap(struct super_block *sb, ext4_group_t block_group)
{
	struct ext4_group_desc *desc;
	struct buffer_head *bh = NULL;
	ext4_fsblk_t bitmap_blk;

	desc = ext4_get_group_desc(sb, block_group, NULL);
	if (!desc)
		return NULL;
	bitmap_blk = ext4_block_bitmap(sb, desc);
	bh = sb_getblk(sb, bitmap_blk);
	if (unlikely(!bh)) {
		ext4_error(sb, __func__,
			    "Cannot read block bitmap - "
			    "block_group = %lu, block_bitmap = %llu",
			    block_group, bitmap_blk);
		return NULL;
	}
	if (bh_uptodate_or_lock(bh))
		return bh;

	spin_lock(sb_bgl_lock(EXT4_SB(sb), block_group));
	if (desc->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
		ext4_init_block_bitmap(sb, bh, block_group, desc);
		set_buffer_uptodate(bh);
		unlock_buffer(bh);
		spin_unlock(sb_bgl_lock(EXT4_SB(sb), block_group));
		return bh;
	}
	spin_unlock(sb_bgl_lock(EXT4_SB(sb), block_group));
	if (bh_submit_read(bh) < 0) {
		put_bh(bh);
		ext4_error(sb, __func__,
			    "Cannot read block bitmap - "
			    "block_group = %lu, block_bitmap = %llu",
			    block_group, bitmap_blk);
		return NULL;
	}
	ext4_valid_block_bitmap(sb, desc, block_group, bh);
	/*
	 * file system mounted not to panic on error,
	 * continue with corrupt bitmap
	 */
	return bh;
}

/**
 * ext4_free_blocks_sb() -- Free given blocks and update quota
 * @handle:			handle to this transaction
 * @sb:				super block
 * @block:			start physcial block to free
 * @count:			number of blocks to free
 * @pdquot_freed_blocks:	pointer to quota
 *
 * XXX This function is only used by the on-line resizing code, which
 * should probably be fixed up to call the mballoc variant.  There
 * this needs to be cleaned up later; in fact, I'm not convinced this
 * is 100% correct in the face of the mballoc code.  The online resizing
 * code needs to be fixed up to more tightly (and correctly) interlock
 * with the mballoc code.
 */
void ext4_free_blocks_sb(handle_t *handle, struct super_block *sb,
			 ext4_fsblk_t block, unsigned long count,
			 unsigned long *pdquot_freed_blocks)
{
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *gd_bh;
	ext4_group_t block_group;
	ext4_grpblk_t bit;
	unsigned long i;
	unsigned long overflow;
	struct ext4_group_desc *desc;
	struct ext4_super_block *es;
	struct ext4_sb_info *sbi;
	int err = 0, ret;
	ext4_grpblk_t group_freed;

	*pdquot_freed_blocks = 0;
	sbi = EXT4_SB(sb);
	es = sbi->s_es;
	if (block < le32_to_cpu(es->s_first_data_block) ||
	    block + count < block ||
	    block + count > ext4_blocks_count(es)) {
		ext4_error(sb, "ext4_free_blocks",
			   "Freeing blocks not in datazone - "
			   "block = %llu, count = %lu", block, count);
		goto error_return;
	}

	ext4_debug("freeing block(s) %llu-%llu\n", block, block + count - 1);

do_more:
	overflow = 0;
	ext4_get_group_no_and_offset(sb, block, &block_group, &bit);
	/*
	 * Check to see if we are freeing blocks across a group
	 * boundary.
	 */
	if (bit + count > EXT4_BLOCKS_PER_GROUP(sb)) {
		overflow = bit + count - EXT4_BLOCKS_PER_GROUP(sb);
		count -= overflow;
	}
	brelse(bitmap_bh);
	bitmap_bh = ext4_read_block_bitmap(sb, block_group);
	if (!bitmap_bh)
		goto error_return;
	desc = ext4_get_group_desc(sb, block_group, &gd_bh);
	if (!desc)
		goto error_return;

	if (in_range(ext4_block_bitmap(sb, desc), block, count) ||
	    in_range(ext4_inode_bitmap(sb, desc), block, count) ||
	    in_range(block, ext4_inode_table(sb, desc), sbi->s_itb_per_group) ||
	    in_range(block + count - 1, ext4_inode_table(sb, desc),
		     sbi->s_itb_per_group)) {
		ext4_error(sb, "ext4_free_blocks",
			   "Freeing blocks in system zones - "
			   "Block = %llu, count = %lu",
			   block, count);
		goto error_return;
	}

	/*
	 * We are about to start releasing blocks in the bitmap,
	 * so we need undo access.
	 */
	/* @@@ check errors */
	BUFFER_TRACE(bitmap_bh, "getting undo access");
	err = ext4_journal_get_undo_access(handle, bitmap_bh);
	if (err)
		goto error_return;

	/*
	 * We are about to modify some metadata.  Call the journal APIs
	 * to unshare ->b_data if a currently-committing transaction is
	 * using it
	 */
	BUFFER_TRACE(gd_bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, gd_bh);
	if (err)
		goto error_return;

	jbd_lock_bh_state(bitmap_bh);

	for (i = 0, group_freed = 0; i < count; i++) {
		/*
		 * An HJ special.  This is expensive...
		 */
#ifdef CONFIG_JBD2_DEBUG
		jbd_unlock_bh_state(bitmap_bh);
		{
			struct buffer_head *debug_bh;
			debug_bh = sb_find_get_block(sb, block + i);
			if (debug_bh) {
				BUFFER_TRACE(debug_bh, "Deleted!");
				if (!bh2jh(bitmap_bh)->b_committed_data)
					BUFFER_TRACE(debug_bh,
						"No commited data in bitmap");
				BUFFER_TRACE2(debug_bh, bitmap_bh, "bitmap");
				__brelse(debug_bh);
			}
		}
		jbd_lock_bh_state(bitmap_bh);
#endif
		if (need_resched()) {
			jbd_unlock_bh_state(bitmap_bh);
			cond_resched();
			jbd_lock_bh_state(bitmap_bh);
		}
		/* @@@ This prevents newly-allocated data from being
		 * freed and then reallocated within the same
		 * transaction.
		 *
		 * Ideally we would want to allow that to happen, but to
		 * do so requires making jbd2_journal_forget() capable of
		 * revoking the queued write of a data block, which
		 * implies blocking on the journal lock.  *forget()
		 * cannot block due to truncate races.
		 *
		 * Eventually we can fix this by making jbd2_journal_forget()
		 * return a status indicating whether or not it was able
		 * to revoke the buffer.  On successful revoke, it is
		 * safe not to set the allocation bit in the committed
		 * bitmap, because we know that there is no outstanding
		 * activity on the buffer any more and so it is safe to
		 * reallocate it.
		 */
		BUFFER_TRACE(bitmap_bh, "set in b_committed_data");
		J_ASSERT_BH(bitmap_bh,
				bh2jh(bitmap_bh)->b_committed_data != NULL);
		ext4_set_bit_atomic(sb_bgl_lock(sbi, block_group), bit + i,
				bh2jh(bitmap_bh)->b_committed_data);

		/*
		 * We clear the bit in the bitmap after setting the committed
		 * data bit, because this is the reverse order to that which
		 * the allocator uses.
		 */
		BUFFER_TRACE(bitmap_bh, "clear bit");
		if (!ext4_clear_bit_atomic(sb_bgl_lock(sbi, block_group),
						bit + i, bitmap_bh->b_data)) {
			jbd_unlock_bh_state(bitmap_bh);
			ext4_error(sb, __func__,
				   "bit already cleared for block %llu",
				   (ext4_fsblk_t)(block + i));
			jbd_lock_bh_state(bitmap_bh);
			BUFFER_TRACE(bitmap_bh, "bit already cleared");
		} else {
			group_freed++;
		}
	}
	jbd_unlock_bh_state(bitmap_bh);

	spin_lock(sb_bgl_lock(sbi, block_group));
	le16_add_cpu(&desc->bg_free_blocks_count, group_freed);
	desc->bg_checksum = ext4_group_desc_csum(sbi, block_group, desc);
	spin_unlock(sb_bgl_lock(sbi, block_group));
	percpu_counter_add(&sbi->s_freeblocks_counter, count);

	if (sbi->s_log_groups_per_flex) {
		ext4_group_t flex_group = ext4_flex_group(sbi, block_group);
		spin_lock(sb_bgl_lock(sbi, flex_group));
		sbi->s_flex_groups[flex_group].free_blocks += count;
		spin_unlock(sb_bgl_lock(sbi, flex_group));
	}

	/* We dirtied the bitmap block */
	BUFFER_TRACE(bitmap_bh, "dirtied bitmap block");
	err = ext4_journal_dirty_metadata(handle, bitmap_bh);

	/* And the group descriptor block */
	BUFFER_TRACE(gd_bh, "dirtied group descriptor block");
	ret = ext4_journal_dirty_metadata(handle, gd_bh);
	if (!err) err = ret;
	*pdquot_freed_blocks += group_freed;

	if (overflow && !err) {
		block += count;
		count = overflow;
		goto do_more;
	}
	sb->s_dirt = 1;
error_return:
	brelse(bitmap_bh);
	ext4_std_error(sb, err);
	return;
}

/**
 * ext4_free_blocks() -- Free given blocks and update quota
 * @handle:		handle for this transaction
 * @inode:		inode
 * @block:		start physical block to free
 * @count:		number of blocks to count
 * @metadata: 		Are these metadata blocks
 */
void ext4_free_blocks(handle_t *handle, struct inode *inode,
			ext4_fsblk_t block, unsigned long count,
			int metadata)
{
	struct super_block *sb;
	unsigned long dquot_freed_blocks;

	/* this isn't the right place to decide whether block is metadata
	 * inode.c/extents.c knows better, but for safety ... */
	if (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode) ||
			ext4_should_journal_data(inode))
		metadata = 1;

	sb = inode->i_sb;

	ext4_mb_free_blocks(handle, inode, block, count,
			    metadata, &dquot_freed_blocks);
	if (dquot_freed_blocks)
		DQUOT_FREE_BLOCK(inode, dquot_freed_blocks);
	return;
}

int ext4_claim_free_blocks(struct ext4_sb_info *sbi,
						s64 nblocks)
{
	s64 free_blocks, dirty_blocks;
	s64 root_blocks = 0;
	struct percpu_counter *fbc = &sbi->s_freeblocks_counter;
	struct percpu_counter *dbc = &sbi->s_dirtyblocks_counter;

	free_blocks  = percpu_counter_read_positive(fbc);
	dirty_blocks = percpu_counter_read_positive(dbc);

	if (!capable(CAP_SYS_RESOURCE) &&
		sbi->s_resuid != current->fsuid &&
		(sbi->s_resgid == 0 || !in_group_p(sbi->s_resgid)))
		root_blocks = ext4_r_blocks_count(sbi->s_es);

	if (free_blocks - (nblocks + root_blocks + dirty_blocks) <
						EXT4_FREEBLOCKS_WATERMARK) {
		free_blocks  = percpu_counter_sum(fbc);
		dirty_blocks = percpu_counter_sum(dbc);
		if (dirty_blocks < 0) {
			printk(KERN_CRIT "Dirty block accounting "
					"went wrong %lld\n",
					dirty_blocks);
		}
	}
	/* Check whether we have space after
	 * accounting for current dirty blocks
	 */
	if (free_blocks < ((root_blocks + nblocks) + dirty_blocks))
		/* we don't have free space */
		return -ENOSPC;

	/* Add the blocks to nblocks */
	percpu_counter_add(dbc, nblocks);
	return 0;
}

/**
 * ext4_has_free_blocks()
 * @sbi:	in-core super block structure.
 * @nblocks:	number of neeed blocks
 *
 * Check if filesystem has free blocks available for allocation.
 * Return the number of blocks avaible for allocation for this request
 * On success, return nblocks
 */
ext4_fsblk_t ext4_has_free_blocks(struct ext4_sb_info *sbi,
						s64 nblocks)
{
	s64 free_blocks, dirty_blocks;
	s64 root_blocks = 0;
	struct percpu_counter *fbc = &sbi->s_freeblocks_counter;
	struct percpu_counter *dbc = &sbi->s_dirtyblocks_counter;

	free_blocks  = percpu_counter_read_positive(fbc);
	dirty_blocks = percpu_counter_read_positive(dbc);

	if (!capable(CAP_SYS_RESOURCE) &&
		sbi->s_resuid != current->fsuid &&
		(sbi->s_resgid == 0 || !in_group_p(sbi->s_resgid)))
		root_blocks = ext4_r_blocks_count(sbi->s_es);

	if (free_blocks - (nblocks + root_blocks + dirty_blocks) <
						EXT4_FREEBLOCKS_WATERMARK) {
		free_blocks  = percpu_counter_sum(fbc);
		dirty_blocks = percpu_counter_sum(dbc);
	}
	if (free_blocks <= (root_blocks + dirty_blocks))
		/* we don't have free space */
		return 0;

	if (free_blocks - (root_blocks + dirty_blocks) < nblocks)
		return free_blocks - (root_blocks + dirty_blocks);
	return nblocks;
}


/**
 * ext4_should_retry_alloc()
 * @sb:			super block
 * @retries		number of attemps has been made
 *
 * ext4_should_retry_alloc() is called when ENOSPC is returned, and if
 * it is profitable to retry the operation, this function will wait
 * for the current or commiting transaction to complete, and then
 * return TRUE.
 *
 * if the total number of retries exceed three times, return FALSE.
 */
int ext4_should_retry_alloc(struct super_block *sb, int *retries)
{
	if (!ext4_has_free_blocks(EXT4_SB(sb), 1) || (*retries)++ > 3)
		return 0;

	jbd_debug(1, "%s: retrying operation after ENOSPC\n", sb->s_id);

	return jbd2_journal_force_commit_nested(EXT4_SB(sb)->s_journal);
}

#define EXT4_META_BLOCK 0x1

static ext4_fsblk_t do_blk_alloc(handle_t *handle, struct inode *inode,
				ext4_lblk_t iblock, ext4_fsblk_t goal,
				unsigned long *count, int *errp, int flags)
{
	struct ext4_allocation_request ar;
	ext4_fsblk_t ret;

	memset(&ar, 0, sizeof(ar));
	/* Fill with neighbour allocated blocks */

	ar.inode = inode;
	ar.goal = goal;
	ar.len = *count;
	ar.logical = iblock;

	if (S_ISREG(inode->i_mode) && !(flags & EXT4_META_BLOCK))
		/* enable in-core preallocation for data block allocation */
		ar.flags = EXT4_MB_HINT_DATA;
	else
		/* disable in-core preallocation for non-regular files */
		ar.flags = 0;

	ret = ext4_mb_new_blocks(handle, &ar, errp);
	*count = ar.len;
	return ret;
}

/*
 * ext4_new_meta_blocks() -- allocate block for meta data (indexing) blocks
 *
 * @handle:             handle to this transaction
 * @inode:              file inode
 * @goal:               given target block(filesystem wide)
 * @count:		total number of blocks need
 * @errp:               error code
 *
 * Return 1st allocated block numberon success, *count stores total account
 * error stores in errp pointer
 */
ext4_fsblk_t ext4_new_meta_blocks(handle_t *handle, struct inode *inode,
		ext4_fsblk_t goal, unsigned long *count, int *errp)
{
	ext4_fsblk_t ret;
	ret = do_blk_alloc(handle, inode, 0, goal,
				count, errp, EXT4_META_BLOCK);
	/*
	 * Account for the allocated meta blocks
	 */
	if (!(*errp) && EXT4_I(inode)->i_delalloc_reserved_flag) {
		spin_lock(&EXT4_I(inode)->i_block_reservation_lock);
		EXT4_I(inode)->i_allocated_meta_blocks += *count;
		spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);
	}
	return ret;
}

/*
 * ext4_new_meta_block() -- allocate block for meta data (indexing) blocks
 *
 * @handle:             handle to this transaction
 * @inode:              file inode
 * @goal:               given target block(filesystem wide)
 * @errp:               error code
 *
 * Return allocated block number on success
 */
ext4_fsblk_t ext4_new_meta_block(handle_t *handle, struct inode *inode,
		ext4_fsblk_t goal, int *errp)
{
	unsigned long count = 1;
	return ext4_new_meta_blocks(handle, inode, goal, &count, errp);
}

/*
 * ext4_new_blocks() -- allocate data blocks
 *
 * @handle:             handle to this transaction
 * @inode:              file inode
 * @goal:               given target block(filesystem wide)
 * @count:		total number of blocks need
 * @errp:               error code
 *
 * Return 1st allocated block numberon success, *count stores total account
 * error stores in errp pointer
 */

ext4_fsblk_t ext4_new_blocks(handle_t *handle, struct inode *inode,
				ext4_lblk_t iblock, ext4_fsblk_t goal,
				unsigned long *count, int *errp)
{
	return do_blk_alloc(handle, inode, iblock, goal, count, errp, 0);
}

/**
 * ext4_count_free_blocks() -- count filesystem free blocks
 * @sb:		superblock
 *
 * Adds up the number of free blocks from each block group.
 */
ext4_fsblk_t ext4_count_free_blocks(struct super_block *sb)
{
	ext4_fsblk_t desc_count;
	struct ext4_group_desc *gdp;
	ext4_group_t i;
	ext4_group_t ngroups = EXT4_SB(sb)->s_groups_count;
#ifdef EXT4FS_DEBUG
	struct ext4_super_block *es;
	ext4_fsblk_t bitmap_count;
	unsigned long x;
	struct buffer_head *bitmap_bh = NULL;

	es = EXT4_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;

	smp_rmb();
	for (i = 0; i < ngroups; i++) {
		gdp = ext4_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_blocks_count);
		brelse(bitmap_bh);
		bitmap_bh = ext4_read_block_bitmap(sb, i);
		if (bitmap_bh == NULL)
			continue;

		x = ext4_count_free(bitmap_bh, sb->s_blocksize);
		printk(KERN_DEBUG "group %lu: stored = %d, counted = %lu\n",
			i, le16_to_cpu(gdp->bg_free_blocks_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk(KERN_DEBUG "ext4_count_free_blocks: stored = %llu"
		", computed = %llu, %llu\n", ext4_free_blocks_count(es),
	       desc_count, bitmap_count);
	return bitmap_count;
#else
	desc_count = 0;
	smp_rmb();
	for (i = 0; i < ngroups; i++) {
		gdp = ext4_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_blocks_count);
	}

	return desc_count;
#endif
}

static inline int test_root(ext4_group_t a, int b)
{
	int num = b;

	while (a > num)
		num *= b;
	return num == a;
}

static int ext4_group_sparse(ext4_group_t group)
{
	if (group <= 1)
		return 1;
	if (!(group & 1))
		return 0;
	return (test_root(group, 7) || test_root(group, 5) ||
		test_root(group, 3));
}

/**
 *	ext4_bg_has_super - number of blocks used by the superblock in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the superblock (primary or backup)
 *	in this group.  Currently this will be only 0 or 1.
 */
int ext4_bg_has_super(struct super_block *sb, ext4_group_t group)
{
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
				EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER) &&
			!ext4_group_sparse(group))
		return 0;
	return 1;
}

static unsigned long ext4_bg_num_gdb_meta(struct super_block *sb,
					ext4_group_t group)
{
	unsigned long metagroup = group / EXT4_DESC_PER_BLOCK(sb);
	ext4_group_t first = metagroup * EXT4_DESC_PER_BLOCK(sb);
	ext4_group_t last = first + EXT4_DESC_PER_BLOCK(sb) - 1;

	if (group == first || group == first + 1 || group == last)
		return 1;
	return 0;
}

static unsigned long ext4_bg_num_gdb_nometa(struct super_block *sb,
					ext4_group_t group)
{
	return ext4_bg_has_super(sb, group) ? EXT4_SB(sb)->s_gdb_count : 0;
}

/**
 *	ext4_bg_num_gdb - number of blocks used by the group table in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the group descriptor table
 *	(primary or backup) in this group.  In the future there may be a
 *	different number of descriptor blocks in each group.
 */
unsigned long ext4_bg_num_gdb(struct super_block *sb, ext4_group_t group)
{
	unsigned long first_meta_bg =
			le32_to_cpu(EXT4_SB(sb)->s_es->s_first_meta_bg);
	unsigned long metagroup = group / EXT4_DESC_PER_BLOCK(sb);

	if (!EXT4_HAS_INCOMPAT_FEATURE(sb,EXT4_FEATURE_INCOMPAT_META_BG) ||
			metagroup < first_meta_bg)
		return ext4_bg_num_gdb_nometa(sb, group);

	return ext4_bg_num_gdb_meta(sb,group);

}

