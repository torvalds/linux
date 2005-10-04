/*
 *  linux/fs/ext3/balloc.c
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

#include <linux/config.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/ext3_fs.h>
#include <linux/ext3_jbd.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>

/*
 * balloc.c contains the blocks allocation and deallocation routines
 */

/*
 * The free blocks are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.  The descriptors are loaded in memory
 * when a file system is mounted (see ext3_read_super).
 */


#define in_range(b, first, len)	((b) >= (first) && (b) <= (first) + (len) - 1)

struct ext3_group_desc * ext3_get_group_desc(struct super_block * sb,
					     unsigned int block_group,
					     struct buffer_head ** bh)
{
	unsigned long group_desc;
	unsigned long offset;
	struct ext3_group_desc * desc;
	struct ext3_sb_info *sbi = EXT3_SB(sb);

	if (block_group >= sbi->s_groups_count) {
		ext3_error (sb, "ext3_get_group_desc",
			    "block_group >= groups_count - "
			    "block_group = %d, groups_count = %lu",
			    block_group, sbi->s_groups_count);

		return NULL;
	}
	smp_rmb();

	group_desc = block_group >> EXT3_DESC_PER_BLOCK_BITS(sb);
	offset = block_group & (EXT3_DESC_PER_BLOCK(sb) - 1);
	if (!sbi->s_group_desc[group_desc]) {
		ext3_error (sb, "ext3_get_group_desc",
			    "Group descriptor not loaded - "
			    "block_group = %d, group_desc = %lu, desc = %lu",
			     block_group, group_desc, offset);
		return NULL;
	}

	desc = (struct ext3_group_desc *) sbi->s_group_desc[group_desc]->b_data;
	if (bh)
		*bh = sbi->s_group_desc[group_desc];
	return desc + offset;
}

/*
 * Read the bitmap for a given block_group, reading into the specified 
 * slot in the superblock's bitmap cache.
 *
 * Return buffer_head on success or NULL in case of failure.
 */
static struct buffer_head *
read_block_bitmap(struct super_block *sb, unsigned int block_group)
{
	struct ext3_group_desc * desc;
	struct buffer_head * bh = NULL;

	desc = ext3_get_group_desc (sb, block_group, NULL);
	if (!desc)
		goto error_out;
	bh = sb_bread(sb, le32_to_cpu(desc->bg_block_bitmap));
	if (!bh)
		ext3_error (sb, "read_block_bitmap",
			    "Cannot read block bitmap - "
			    "block_group = %d, block_bitmap = %u",
			    block_group, le32_to_cpu(desc->bg_block_bitmap));
error_out:
	return bh;
}
/*
 * The reservation window structure operations
 * --------------------------------------------
 * Operations include:
 * dump, find, add, remove, is_empty, find_next_reservable_window, etc.
 *
 * We use sorted double linked list for the per-filesystem reservation
 * window list. (like in vm_region).
 *
 * Initially, we keep those small operations in the abstract functions,
 * so later if we need a better searching tree than double linked-list,
 * we could easily switch to that without changing too much
 * code.
 */
#if 0
static void __rsv_window_dump(struct rb_root *root, int verbose,
			      const char *fn)
{
	struct rb_node *n;
	struct ext3_reserve_window_node *rsv, *prev;
	int bad;

restart:
	n = rb_first(root);
	bad = 0;
	prev = NULL;

	printk("Block Allocation Reservation Windows Map (%s):\n", fn);
	while (n) {
		rsv = list_entry(n, struct ext3_reserve_window_node, rsv_node);
		if (verbose)
			printk("reservation window 0x%p "
			       "start:  %d, end:  %d\n",
			       rsv, rsv->rsv_start, rsv->rsv_end);
		if (rsv->rsv_start && rsv->rsv_start >= rsv->rsv_end) {
			printk("Bad reservation %p (start >= end)\n",
			       rsv);
			bad = 1;
		}
		if (prev && prev->rsv_end >= rsv->rsv_start) {
			printk("Bad reservation %p (prev->end >= start)\n",
			       rsv);
			bad = 1;
		}
		if (bad) {
			if (!verbose) {
				printk("Restarting reservation walk in verbose mode\n");
				verbose = 1;
				goto restart;
			}
		}
		n = rb_next(n);
		prev = rsv;
	}
	printk("Window map complete.\n");
	if (bad)
		BUG();
}
#define rsv_window_dump(root, verbose) \
	__rsv_window_dump((root), (verbose), __FUNCTION__)
#else
#define rsv_window_dump(root, verbose) do {} while (0)
#endif

static int
goal_in_my_reservation(struct ext3_reserve_window *rsv, int goal,
			unsigned int group, struct super_block * sb)
{
	unsigned long group_first_block, group_last_block;

	group_first_block = le32_to_cpu(EXT3_SB(sb)->s_es->s_first_data_block) +
				group * EXT3_BLOCKS_PER_GROUP(sb);
	group_last_block = group_first_block + EXT3_BLOCKS_PER_GROUP(sb) - 1;

	if ((rsv->_rsv_start > group_last_block) ||
	    (rsv->_rsv_end < group_first_block))
		return 0;
	if ((goal >= 0) && ((goal + group_first_block < rsv->_rsv_start)
		|| (goal + group_first_block > rsv->_rsv_end)))
		return 0;
	return 1;
}

/*
 * Find the reserved window which includes the goal, or the previous one
 * if the goal is not in any window.
 * Returns NULL if there are no windows or if all windows start after the goal.
 */
static struct ext3_reserve_window_node *
search_reserve_window(struct rb_root *root, unsigned long goal)
{
	struct rb_node *n = root->rb_node;
	struct ext3_reserve_window_node *rsv;

	if (!n)
		return NULL;

	do {
		rsv = rb_entry(n, struct ext3_reserve_window_node, rsv_node);

		if (goal < rsv->rsv_start)
			n = n->rb_left;
		else if (goal > rsv->rsv_end)
			n = n->rb_right;
		else
			return rsv;
	} while (n);
	/*
	 * We've fallen off the end of the tree: the goal wasn't inside
	 * any particular node.  OK, the previous node must be to one
	 * side of the interval containing the goal.  If it's the RHS,
	 * we need to back up one.
	 */
	if (rsv->rsv_start > goal) {
		n = rb_prev(&rsv->rsv_node);
		rsv = rb_entry(n, struct ext3_reserve_window_node, rsv_node);
	}
	return rsv;
}

void ext3_rsv_window_add(struct super_block *sb,
		    struct ext3_reserve_window_node *rsv)
{
	struct rb_root *root = &EXT3_SB(sb)->s_rsv_window_root;
	struct rb_node *node = &rsv->rsv_node;
	unsigned int start = rsv->rsv_start;

	struct rb_node ** p = &root->rb_node;
	struct rb_node * parent = NULL;
	struct ext3_reserve_window_node *this;

	while (*p)
	{
		parent = *p;
		this = rb_entry(parent, struct ext3_reserve_window_node, rsv_node);

		if (start < this->rsv_start)
			p = &(*p)->rb_left;
		else if (start > this->rsv_end)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(node, parent, p);
	rb_insert_color(node, root);
}

static void rsv_window_remove(struct super_block *sb,
			      struct ext3_reserve_window_node *rsv)
{
	rsv->rsv_start = EXT3_RESERVE_WINDOW_NOT_ALLOCATED;
	rsv->rsv_end = EXT3_RESERVE_WINDOW_NOT_ALLOCATED;
	rsv->rsv_alloc_hit = 0;
	rb_erase(&rsv->rsv_node, &EXT3_SB(sb)->s_rsv_window_root);
}

static inline int rsv_is_empty(struct ext3_reserve_window *rsv)
{
	/* a valid reservation end block could not be 0 */
	return (rsv->_rsv_end == EXT3_RESERVE_WINDOW_NOT_ALLOCATED);
}
void ext3_init_block_alloc_info(struct inode *inode)
{
	struct ext3_inode_info *ei = EXT3_I(inode);
	struct ext3_block_alloc_info *block_i = ei->i_block_alloc_info;
	struct super_block *sb = inode->i_sb;

	block_i = kmalloc(sizeof(*block_i), GFP_NOFS);
	if (block_i) {
		struct ext3_reserve_window_node *rsv = &block_i->rsv_window_node;

		rsv->rsv_start = EXT3_RESERVE_WINDOW_NOT_ALLOCATED;
		rsv->rsv_end = EXT3_RESERVE_WINDOW_NOT_ALLOCATED;

	 	/*
		 * if filesystem is mounted with NORESERVATION, the goal
		 * reservation window size is set to zero to indicate
		 * block reservation is off
		 */
		if (!test_opt(sb, RESERVATION))
			rsv->rsv_goal_size = 0;
		else
			rsv->rsv_goal_size = EXT3_DEFAULT_RESERVE_BLOCKS;
		rsv->rsv_alloc_hit = 0;
		block_i->last_alloc_logical_block = 0;
		block_i->last_alloc_physical_block = 0;
	}
	ei->i_block_alloc_info = block_i;
}

void ext3_discard_reservation(struct inode *inode)
{
	struct ext3_inode_info *ei = EXT3_I(inode);
	struct ext3_block_alloc_info *block_i = ei->i_block_alloc_info;
	struct ext3_reserve_window_node *rsv;
	spinlock_t *rsv_lock = &EXT3_SB(inode->i_sb)->s_rsv_window_lock;

	if (!block_i)
		return;

	rsv = &block_i->rsv_window_node;
	if (!rsv_is_empty(&rsv->rsv_window)) {
		spin_lock(rsv_lock);
		if (!rsv_is_empty(&rsv->rsv_window))
			rsv_window_remove(inode->i_sb, rsv);
		spin_unlock(rsv_lock);
	}
}

/* Free given blocks, update quota and i_blocks field */
void ext3_free_blocks_sb(handle_t *handle, struct super_block *sb,
			 unsigned long block, unsigned long count,
			 int *pdquot_freed_blocks)
{
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *gd_bh;
	unsigned long block_group;
	unsigned long bit;
	unsigned long i;
	unsigned long overflow;
	struct ext3_group_desc * desc;
	struct ext3_super_block * es;
	struct ext3_sb_info *sbi;
	int err = 0, ret;
	unsigned group_freed;

	*pdquot_freed_blocks = 0;
	sbi = EXT3_SB(sb);
	es = sbi->s_es;
	if (block < le32_to_cpu(es->s_first_data_block) ||
	    block + count < block ||
	    block + count > le32_to_cpu(es->s_blocks_count)) {
		ext3_error (sb, "ext3_free_blocks",
			    "Freeing blocks not in datazone - "
			    "block = %lu, count = %lu", block, count);
		goto error_return;
	}

	ext3_debug ("freeing block(s) %lu-%lu\n", block, block + count - 1);

do_more:
	overflow = 0;
	block_group = (block - le32_to_cpu(es->s_first_data_block)) /
		      EXT3_BLOCKS_PER_GROUP(sb);
	bit = (block - le32_to_cpu(es->s_first_data_block)) %
		      EXT3_BLOCKS_PER_GROUP(sb);
	/*
	 * Check to see if we are freeing blocks across a group
	 * boundary.
	 */
	if (bit + count > EXT3_BLOCKS_PER_GROUP(sb)) {
		overflow = bit + count - EXT3_BLOCKS_PER_GROUP(sb);
		count -= overflow;
	}
	brelse(bitmap_bh);
	bitmap_bh = read_block_bitmap(sb, block_group);
	if (!bitmap_bh)
		goto error_return;
	desc = ext3_get_group_desc (sb, block_group, &gd_bh);
	if (!desc)
		goto error_return;

	if (in_range (le32_to_cpu(desc->bg_block_bitmap), block, count) ||
	    in_range (le32_to_cpu(desc->bg_inode_bitmap), block, count) ||
	    in_range (block, le32_to_cpu(desc->bg_inode_table),
		      sbi->s_itb_per_group) ||
	    in_range (block + count - 1, le32_to_cpu(desc->bg_inode_table),
		      sbi->s_itb_per_group))
		ext3_error (sb, "ext3_free_blocks",
			    "Freeing blocks in system zones - "
			    "Block = %lu, count = %lu",
			    block, count);

	/*
	 * We are about to start releasing blocks in the bitmap,
	 * so we need undo access.
	 */
	/* @@@ check errors */
	BUFFER_TRACE(bitmap_bh, "getting undo access");
	err = ext3_journal_get_undo_access(handle, bitmap_bh);
	if (err)
		goto error_return;

	/*
	 * We are about to modify some metadata.  Call the journal APIs
	 * to unshare ->b_data if a currently-committing transaction is
	 * using it
	 */
	BUFFER_TRACE(gd_bh, "get_write_access");
	err = ext3_journal_get_write_access(handle, gd_bh);
	if (err)
		goto error_return;

	jbd_lock_bh_state(bitmap_bh);

	for (i = 0, group_freed = 0; i < count; i++) {
		/*
		 * An HJ special.  This is expensive...
		 */
#ifdef CONFIG_JBD_DEBUG
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
		 * do so requires making journal_forget() capable of
		 * revoking the queued write of a data block, which
		 * implies blocking on the journal lock.  *forget()
		 * cannot block due to truncate races.
		 *
		 * Eventually we can fix this by making journal_forget()
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
		ext3_set_bit_atomic(sb_bgl_lock(sbi, block_group), bit + i,
				bh2jh(bitmap_bh)->b_committed_data);

		/*
		 * We clear the bit in the bitmap after setting the committed
		 * data bit, because this is the reverse order to that which
		 * the allocator uses.
		 */
		BUFFER_TRACE(bitmap_bh, "clear bit");
		if (!ext3_clear_bit_atomic(sb_bgl_lock(sbi, block_group),
						bit + i, bitmap_bh->b_data)) {
			jbd_unlock_bh_state(bitmap_bh);
			ext3_error(sb, __FUNCTION__,
				"bit already cleared for block %lu", block + i);
			jbd_lock_bh_state(bitmap_bh);
			BUFFER_TRACE(bitmap_bh, "bit already cleared");
		} else {
			group_freed++;
		}
	}
	jbd_unlock_bh_state(bitmap_bh);

	spin_lock(sb_bgl_lock(sbi, block_group));
	desc->bg_free_blocks_count =
		cpu_to_le16(le16_to_cpu(desc->bg_free_blocks_count) +
			group_freed);
	spin_unlock(sb_bgl_lock(sbi, block_group));
	percpu_counter_mod(&sbi->s_freeblocks_counter, count);

	/* We dirtied the bitmap block */
	BUFFER_TRACE(bitmap_bh, "dirtied bitmap block");
	err = ext3_journal_dirty_metadata(handle, bitmap_bh);

	/* And the group descriptor block */
	BUFFER_TRACE(gd_bh, "dirtied group descriptor block");
	ret = ext3_journal_dirty_metadata(handle, gd_bh);
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
	ext3_std_error(sb, err);
	return;
}

/* Free given blocks, update quota and i_blocks field */
void ext3_free_blocks(handle_t *handle, struct inode *inode,
			unsigned long block, unsigned long count)
{
	struct super_block * sb;
	int dquot_freed_blocks;

	sb = inode->i_sb;
	if (!sb) {
		printk ("ext3_free_blocks: nonexistent device");
		return;
	}
	ext3_free_blocks_sb(handle, sb, block, count, &dquot_freed_blocks);
	if (dquot_freed_blocks)
		DQUOT_FREE_BLOCK(inode, dquot_freed_blocks);
	return;
}

/*
 * For ext3 allocations, we must not reuse any blocks which are
 * allocated in the bitmap buffer's "last committed data" copy.  This
 * prevents deletes from freeing up the page for reuse until we have
 * committed the delete transaction.
 *
 * If we didn't do this, then deleting something and reallocating it as
 * data would allow the old block to be overwritten before the
 * transaction committed (because we force data to disk before commit).
 * This would lead to corruption if we crashed between overwriting the
 * data and committing the delete. 
 *
 * @@@ We may want to make this allocation behaviour conditional on
 * data-writes at some point, and disable it for metadata allocations or
 * sync-data inodes.
 */
static int ext3_test_allocatable(int nr, struct buffer_head *bh)
{
	int ret;
	struct journal_head *jh = bh2jh(bh);

	if (ext3_test_bit(nr, bh->b_data))
		return 0;

	jbd_lock_bh_state(bh);
	if (!jh->b_committed_data)
		ret = 1;
	else
		ret = !ext3_test_bit(nr, jh->b_committed_data);
	jbd_unlock_bh_state(bh);
	return ret;
}

static int
bitmap_search_next_usable_block(int start, struct buffer_head *bh,
					int maxblocks)
{
	int next;
	struct journal_head *jh = bh2jh(bh);

	/*
	 * The bitmap search --- search forward alternately through the actual
	 * bitmap and the last-committed copy until we find a bit free in
	 * both
	 */
	while (start < maxblocks) {
		next = ext3_find_next_zero_bit(bh->b_data, maxblocks, start);
		if (next >= maxblocks)
			return -1;
		if (ext3_test_allocatable(next, bh))
			return next;
		jbd_lock_bh_state(bh);
		if (jh->b_committed_data)
			start = ext3_find_next_zero_bit(jh->b_committed_data,
						 	maxblocks, next);
		jbd_unlock_bh_state(bh);
	}
	return -1;
}

/*
 * Find an allocatable block in a bitmap.  We honour both the bitmap and
 * its last-committed copy (if that exists), and perform the "most
 * appropriate allocation" algorithm of looking for a free block near
 * the initial goal; then for a free byte somewhere in the bitmap; then
 * for any free bit in the bitmap.
 */
static int
find_next_usable_block(int start, struct buffer_head *bh, int maxblocks)
{
	int here, next;
	char *p, *r;

	if (start > 0) {
		/*
		 * The goal was occupied; search forward for a free 
		 * block within the next XX blocks.
		 *
		 * end_goal is more or less random, but it has to be
		 * less than EXT3_BLOCKS_PER_GROUP. Aligning up to the
		 * next 64-bit boundary is simple..
		 */
		int end_goal = (start + 63) & ~63;
		if (end_goal > maxblocks)
			end_goal = maxblocks;
		here = ext3_find_next_zero_bit(bh->b_data, end_goal, start);
		if (here < end_goal && ext3_test_allocatable(here, bh))
			return here;
		ext3_debug("Bit not found near goal\n");
	}

	here = start;
	if (here < 0)
		here = 0;

	p = ((char *)bh->b_data) + (here >> 3);
	r = memscan(p, 0, (maxblocks - here + 7) >> 3);
	next = (r - ((char *)bh->b_data)) << 3;

	if (next < maxblocks && next >= start && ext3_test_allocatable(next, bh))
		return next;

	/*
	 * The bitmap search --- search forward alternately through the actual
	 * bitmap and the last-committed copy until we find a bit free in
	 * both
	 */
	here = bitmap_search_next_usable_block(here, bh, maxblocks);
	return here;
}

/*
 * We think we can allocate this block in this bitmap.  Try to set the bit.
 * If that succeeds then check that nobody has allocated and then freed the
 * block since we saw that is was not marked in b_committed_data.  If it _was_
 * allocated and freed then clear the bit in the bitmap again and return
 * zero (failure).
 */
static inline int
claim_block(spinlock_t *lock, int block, struct buffer_head *bh)
{
	struct journal_head *jh = bh2jh(bh);
	int ret;

	if (ext3_set_bit_atomic(lock, block, bh->b_data))
		return 0;
	jbd_lock_bh_state(bh);
	if (jh->b_committed_data && ext3_test_bit(block,jh->b_committed_data)) {
		ext3_clear_bit_atomic(lock, block, bh->b_data);
		ret = 0;
	} else {
		ret = 1;
	}
	jbd_unlock_bh_state(bh);
	return ret;
}

/*
 * If we failed to allocate the desired block then we may end up crossing to a
 * new bitmap.  In that case we must release write access to the old one via
 * ext3_journal_release_buffer(), else we'll run out of credits.
 */
static int
ext3_try_to_allocate(struct super_block *sb, handle_t *handle, int group,
	struct buffer_head *bitmap_bh, int goal, struct ext3_reserve_window *my_rsv)
{
	int group_first_block, start, end;

	/* we do allocation within the reservation window if we have a window */
	if (my_rsv) {
		group_first_block =
			le32_to_cpu(EXT3_SB(sb)->s_es->s_first_data_block) +
			group * EXT3_BLOCKS_PER_GROUP(sb);
		if (my_rsv->_rsv_start >= group_first_block)
			start = my_rsv->_rsv_start - group_first_block;
		else
			/* reservation window cross group boundary */
			start = 0;
		end = my_rsv->_rsv_end - group_first_block + 1;
		if (end > EXT3_BLOCKS_PER_GROUP(sb))
			/* reservation window crosses group boundary */
			end = EXT3_BLOCKS_PER_GROUP(sb);
		if ((start <= goal) && (goal < end))
			start = goal;
		else
			goal = -1;
	} else {
		if (goal > 0)
			start = goal;
		else
			start = 0;
		end = EXT3_BLOCKS_PER_GROUP(sb);
	}

	BUG_ON(start > EXT3_BLOCKS_PER_GROUP(sb));

repeat:
	if (goal < 0 || !ext3_test_allocatable(goal, bitmap_bh)) {
		goal = find_next_usable_block(start, bitmap_bh, end);
		if (goal < 0)
			goto fail_access;
		if (!my_rsv) {
			int i;

			for (i = 0; i < 7 && goal > start &&
					ext3_test_allocatable(goal - 1,
								bitmap_bh);
					i++, goal--)
				;
		}
	}
	start = goal;

	if (!claim_block(sb_bgl_lock(EXT3_SB(sb), group), goal, bitmap_bh)) {
		/*
		 * The block was allocated by another thread, or it was
		 * allocated and then freed by another thread
		 */
		start++;
		goal++;
		if (start >= end)
			goto fail_access;
		goto repeat;
	}
	return goal;
fail_access:
	return -1;
}

/**
 * 	find_next_reservable_window():
 *		find a reservable space within the given range.
 *		It does not allocate the reservation window for now:
 *		alloc_new_reservation() will do the work later.
 *
 * 	@search_head: the head of the searching list;
 *		This is not necessarily the list head of the whole filesystem
 *
 *		We have both head and start_block to assist the search
 *		for the reservable space. The list starts from head,
 *		but we will shift to the place where start_block is,
 *		then start from there, when looking for a reservable space.
 *
 * 	@size: the target new reservation window size
 *
 * 	@group_first_block: the first block we consider to start
 *			the real search from
 *
 * 	@last_block:
 *		the maximum block number that our goal reservable space
 *		could start from. This is normally the last block in this
 *		group. The search will end when we found the start of next
 *		possible reservable space is out of this boundary.
 *		This could handle the cross boundary reservation window
 *		request.
 *
 * 	basically we search from the given range, rather than the whole
 * 	reservation double linked list, (start_block, last_block)
 * 	to find a free region that is of my size and has not
 * 	been reserved.
 *
 */
static int find_next_reservable_window(
				struct ext3_reserve_window_node *search_head,
				struct ext3_reserve_window_node *my_rsv,
				struct super_block * sb, int start_block,
				int last_block)
{
	struct rb_node *next;
	struct ext3_reserve_window_node *rsv, *prev;
	int cur;
	int size = my_rsv->rsv_goal_size;

	/* TODO: make the start of the reservation window byte-aligned */
	/* cur = *start_block & ~7;*/
	cur = start_block;
	rsv = search_head;
	if (!rsv)
		return -1;

	while (1) {
		if (cur <= rsv->rsv_end)
			cur = rsv->rsv_end + 1;

		/* TODO?
		 * in the case we could not find a reservable space
		 * that is what is expected, during the re-search, we could
		 * remember what's the largest reservable space we could have
		 * and return that one.
		 *
		 * For now it will fail if we could not find the reservable
		 * space with expected-size (or more)...
		 */
		if (cur > last_block)
			return -1;		/* fail */

		prev = rsv;
		next = rb_next(&rsv->rsv_node);
		rsv = list_entry(next,struct ext3_reserve_window_node,rsv_node);

		/*
		 * Reached the last reservation, we can just append to the
		 * previous one.
		 */
		if (!next)
			break;

		if (cur + size <= rsv->rsv_start) {
			/*
			 * Found a reserveable space big enough.  We could
			 * have a reservation across the group boundary here
		 	 */
			break;
		}
	}
	/*
	 * we come here either :
	 * when we reach the end of the whole list,
	 * and there is empty reservable space after last entry in the list.
	 * append it to the end of the list.
	 *
	 * or we found one reservable space in the middle of the list,
	 * return the reservation window that we could append to.
	 * succeed.
	 */

	if ((prev != my_rsv) && (!rsv_is_empty(&my_rsv->rsv_window)))
		rsv_window_remove(sb, my_rsv);

	/*
	 * Let's book the whole avaliable window for now.  We will check the
	 * disk bitmap later and then, if there are free blocks then we adjust
	 * the window size if it's larger than requested.
	 * Otherwise, we will remove this node from the tree next time
	 * call find_next_reservable_window.
	 */
	my_rsv->rsv_start = cur;
	my_rsv->rsv_end = cur + size - 1;
	my_rsv->rsv_alloc_hit = 0;

	if (prev != my_rsv)
		ext3_rsv_window_add(sb, my_rsv);

	return 0;
}

/**
 * 	alloc_new_reservation()--allocate a new reservation window
 *
 *		To make a new reservation, we search part of the filesystem
 *		reservation list (the list that inside the group). We try to
 *		allocate a new reservation window near the allocation goal,
 *		or the beginning of the group, if there is no goal.
 *
 *		We first find a reservable space after the goal, then from
 *		there, we check the bitmap for the first free block after
 *		it. If there is no free block until the end of group, then the
 *		whole group is full, we failed. Otherwise, check if the free
 *		block is inside the expected reservable space, if so, we
 *		succeed.
 *		If the first free block is outside the reservable space, then
 *		start from the first free block, we search for next available
 *		space, and go on.
 *
 *	on succeed, a new reservation will be found and inserted into the list
 *	It contains at least one free block, and it does not overlap with other
 *	reservation windows.
 *
 *	failed: we failed to find a reservation window in this group
 *
 *	@rsv: the reservation
 *
 *	@goal: The goal (group-relative).  It is where the search for a
 *		free reservable space should start from.
 *		if we have a goal(goal >0 ), then start from there,
 *		no goal(goal = -1), we start from the first block
 *		of the group.
 *
 *	@sb: the super block
 *	@group: the group we are trying to allocate in
 *	@bitmap_bh: the block group block bitmap
 *
 */
static int alloc_new_reservation(struct ext3_reserve_window_node *my_rsv,
		int goal, struct super_block *sb,
		unsigned int group, struct buffer_head *bitmap_bh)
{
	struct ext3_reserve_window_node *search_head;
	int group_first_block, group_end_block, start_block;
	int first_free_block;
	struct rb_root *fs_rsv_root = &EXT3_SB(sb)->s_rsv_window_root;
	unsigned long size;
	int ret;
	spinlock_t *rsv_lock = &EXT3_SB(sb)->s_rsv_window_lock;

	group_first_block = le32_to_cpu(EXT3_SB(sb)->s_es->s_first_data_block) +
				group * EXT3_BLOCKS_PER_GROUP(sb);
	group_end_block = group_first_block + EXT3_BLOCKS_PER_GROUP(sb) - 1;

	if (goal < 0)
		start_block = group_first_block;
	else
		start_block = goal + group_first_block;

	size = my_rsv->rsv_goal_size;

	if (!rsv_is_empty(&my_rsv->rsv_window)) {
		/*
		 * if the old reservation is cross group boundary
		 * and if the goal is inside the old reservation window,
		 * we will come here when we just failed to allocate from
		 * the first part of the window. We still have another part
		 * that belongs to the next group. In this case, there is no
		 * point to discard our window and try to allocate a new one
		 * in this group(which will fail). we should
		 * keep the reservation window, just simply move on.
		 *
		 * Maybe we could shift the start block of the reservation
		 * window to the first block of next group.
		 */

		if ((my_rsv->rsv_start <= group_end_block) &&
				(my_rsv->rsv_end > group_end_block) &&
				(start_block >= my_rsv->rsv_start))
			return -1;

		if ((my_rsv->rsv_alloc_hit >
		     (my_rsv->rsv_end - my_rsv->rsv_start + 1) / 2)) {
			/*
			 * if we previously allocation hit ration is greater than half
			 * we double the size of reservation window next time
			 * otherwise keep the same
			 */
			size = size * 2;
			if (size > EXT3_MAX_RESERVE_BLOCKS)
				size = EXT3_MAX_RESERVE_BLOCKS;
			my_rsv->rsv_goal_size= size;
		}
	}

	spin_lock(rsv_lock);
	/*
	 * shift the search start to the window near the goal block
	 */
	search_head = search_reserve_window(fs_rsv_root, start_block);

	/*
	 * find_next_reservable_window() simply finds a reservable window
	 * inside the given range(start_block, group_end_block).
	 *
	 * To make sure the reservation window has a free bit inside it, we
	 * need to check the bitmap after we found a reservable window.
	 */
retry:
	ret = find_next_reservable_window(search_head, my_rsv, sb,
						start_block, group_end_block);

	if (ret == -1) {
		if (!rsv_is_empty(&my_rsv->rsv_window))
			rsv_window_remove(sb, my_rsv);
		spin_unlock(rsv_lock);
		return -1;
	}

	/*
	 * On success, find_next_reservable_window() returns the
	 * reservation window where there is a reservable space after it.
	 * Before we reserve this reservable space, we need
	 * to make sure there is at least a free block inside this region.
	 *
	 * searching the first free bit on the block bitmap and copy of
	 * last committed bitmap alternatively, until we found a allocatable
	 * block. Search start from the start block of the reservable space
	 * we just found.
	 */
	spin_unlock(rsv_lock);
	first_free_block = bitmap_search_next_usable_block(
			my_rsv->rsv_start - group_first_block,
			bitmap_bh, group_end_block - group_first_block + 1);

	if (first_free_block < 0) {
		/*
		 * no free block left on the bitmap, no point
		 * to reserve the space. return failed.
		 */
		spin_lock(rsv_lock);
		if (!rsv_is_empty(&my_rsv->rsv_window))
			rsv_window_remove(sb, my_rsv);
		spin_unlock(rsv_lock);
		return -1;		/* failed */
	}

	start_block = first_free_block + group_first_block;
	/*
	 * check if the first free block is within the
	 * free space we just reserved
	 */
	if (start_block >= my_rsv->rsv_start && start_block < my_rsv->rsv_end)
		return 0;		/* success */
	/*
	 * if the first free bit we found is out of the reservable space
	 * continue search for next reservable space,
	 * start from where the free block is,
	 * we also shift the list head to where we stopped last time
	 */
	search_head = my_rsv;
	spin_lock(rsv_lock);
	goto retry;
}

/*
 * This is the main function used to allocate a new block and its reservation
 * window.
 *
 * Each time when a new block allocation is need, first try to allocate from
 * its own reservation.  If it does not have a reservation window, instead of
 * looking for a free bit on bitmap first, then look up the reservation list to
 * see if it is inside somebody else's reservation window, we try to allocate a
 * reservation window for it starting from the goal first. Then do the block
 * allocation within the reservation window.
 *
 * This will avoid keeping on searching the reservation list again and
 * again when someboday is looking for a free block (without
 * reservation), and there are lots of free blocks, but they are all
 * being reserved.
 *
 * We use a sorted double linked list for the per-filesystem reservation list.
 * The insert, remove and find a free space(non-reserved) operations for the
 * sorted double linked list should be fast.
 *
 */
static int
ext3_try_to_allocate_with_rsv(struct super_block *sb, handle_t *handle,
			unsigned int group, struct buffer_head *bitmap_bh,
			int goal, struct ext3_reserve_window_node * my_rsv,
			int *errp)
{
	unsigned long group_first_block;
	int ret = 0;
	int fatal;

	*errp = 0;

	/*
	 * Make sure we use undo access for the bitmap, because it is critical
	 * that we do the frozen_data COW on bitmap buffers in all cases even
	 * if the buffer is in BJ_Forget state in the committing transaction.
	 */
	BUFFER_TRACE(bitmap_bh, "get undo access for new block");
	fatal = ext3_journal_get_undo_access(handle, bitmap_bh);
	if (fatal) {
		*errp = fatal;
		return -1;
	}

	/*
	 * we don't deal with reservation when
	 * filesystem is mounted without reservation
	 * or the file is not a regular file
	 * or last attempt to allocate a block with reservation turned on failed
	 */
	if (my_rsv == NULL ) {
		ret = ext3_try_to_allocate(sb, handle, group, bitmap_bh, goal, NULL);
		goto out;
	}
	/*
	 * goal is a group relative block number (if there is a goal)
	 * 0 < goal < EXT3_BLOCKS_PER_GROUP(sb)
	 * first block is a filesystem wide block number
	 * first block is the block number of the first block in this group
	 */
	group_first_block = le32_to_cpu(EXT3_SB(sb)->s_es->s_first_data_block) +
			group * EXT3_BLOCKS_PER_GROUP(sb);

	/*
	 * Basically we will allocate a new block from inode's reservation
	 * window.
	 *
	 * We need to allocate a new reservation window, if:
	 * a) inode does not have a reservation window; or
	 * b) last attempt to allocate a block from existing reservation
	 *    failed; or
	 * c) we come here with a goal and with a reservation window
	 *
	 * We do not need to allocate a new reservation window if we come here
	 * at the beginning with a goal and the goal is inside the window, or
	 * we don't have a goal but already have a reservation window.
	 * then we could go to allocate from the reservation window directly.
	 */
	while (1) {
		if (rsv_is_empty(&my_rsv->rsv_window) || (ret < 0) ||
			!goal_in_my_reservation(&my_rsv->rsv_window, goal, group, sb)) {
			ret = alloc_new_reservation(my_rsv, goal, sb,
							group, bitmap_bh);
			if (ret < 0)
				break;			/* failed */

			if (!goal_in_my_reservation(&my_rsv->rsv_window, goal, group, sb))
				goal = -1;
		}
		if ((my_rsv->rsv_start >= group_first_block + EXT3_BLOCKS_PER_GROUP(sb))
		    || (my_rsv->rsv_end < group_first_block))
			BUG();
		ret = ext3_try_to_allocate(sb, handle, group, bitmap_bh, goal,
					   &my_rsv->rsv_window);
		if (ret >= 0) {
			my_rsv->rsv_alloc_hit++;
			break;				/* succeed */
		}
	}
out:
	if (ret >= 0) {
		BUFFER_TRACE(bitmap_bh, "journal_dirty_metadata for "
					"bitmap block");
		fatal = ext3_journal_dirty_metadata(handle, bitmap_bh);
		if (fatal) {
			*errp = fatal;
			return -1;
		}
		return ret;
	}

	BUFFER_TRACE(bitmap_bh, "journal_release_buffer");
	ext3_journal_release_buffer(handle, bitmap_bh);
	return ret;
}

static int ext3_has_free_blocks(struct ext3_sb_info *sbi)
{
	int free_blocks, root_blocks;

	free_blocks = percpu_counter_read_positive(&sbi->s_freeblocks_counter);
	root_blocks = le32_to_cpu(sbi->s_es->s_r_blocks_count);
	if (free_blocks < root_blocks + 1 && !capable(CAP_SYS_RESOURCE) &&
		sbi->s_resuid != current->fsuid &&
		(sbi->s_resgid == 0 || !in_group_p (sbi->s_resgid))) {
		return 0;
	}
	return 1;
}

/*
 * ext3_should_retry_alloc() is called when ENOSPC is returned, and if
 * it is profitable to retry the operation, this function will wait
 * for the current or commiting transaction to complete, and then
 * return TRUE.
 */
int ext3_should_retry_alloc(struct super_block *sb, int *retries)
{
	if (!ext3_has_free_blocks(EXT3_SB(sb)) || (*retries)++ > 3)
		return 0;

	jbd_debug(1, "%s: retrying operation after ENOSPC\n", sb->s_id);

	return journal_force_commit_nested(EXT3_SB(sb)->s_journal);
}

/*
 * ext3_new_block uses a goal block to assist allocation.  If the goal is
 * free, or there is a free block within 32 blocks of the goal, that block
 * is allocated.  Otherwise a forward search is made for a free block; within 
 * each block group the search first looks for an entire free byte in the block
 * bitmap, and then for any free bit if that fails.
 * This function also updates quota and i_blocks field.
 */
int ext3_new_block(handle_t *handle, struct inode *inode,
			unsigned long goal, int *errp)
{
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *gdp_bh;
	int group_no;
	int goal_group;
	int ret_block;
	int bgi;			/* blockgroup iteration index */
	int target_block;
	int fatal = 0, err;
	int performed_allocation = 0;
	int free_blocks;
	struct super_block *sb;
	struct ext3_group_desc *gdp;
	struct ext3_super_block *es;
	struct ext3_sb_info *sbi;
	struct ext3_reserve_window_node *my_rsv = NULL;
	struct ext3_block_alloc_info *block_i;
	unsigned short windowsz = 0;
#ifdef EXT3FS_DEBUG
	static int goal_hits, goal_attempts;
#endif
	unsigned long ngroups;

	*errp = -ENOSPC;
	sb = inode->i_sb;
	if (!sb) {
		printk("ext3_new_block: nonexistent device");
		return 0;
	}

	/*
	 * Check quota for allocation of this block.
	 */
	if (DQUOT_ALLOC_BLOCK(inode, 1)) {
		*errp = -EDQUOT;
		return 0;
	}

	sbi = EXT3_SB(sb);
	es = EXT3_SB(sb)->s_es;
	ext3_debug("goal=%lu.\n", goal);
	/*
	 * Allocate a block from reservation only when
	 * filesystem is mounted with reservation(default,-o reservation), and
	 * it's a regular file, and
	 * the desired window size is greater than 0 (One could use ioctl
	 * command EXT3_IOC_SETRSVSZ to set the window size to 0 to turn off
	 * reservation on that particular file)
	 */
	block_i = EXT3_I(inode)->i_block_alloc_info;
	if (block_i && ((windowsz = block_i->rsv_window_node.rsv_goal_size) > 0))
		my_rsv = &block_i->rsv_window_node;

	if (!ext3_has_free_blocks(sbi)) {
		*errp = -ENOSPC;
		goto out;
	}

	/*
	 * First, test whether the goal block is free.
	 */
	if (goal < le32_to_cpu(es->s_first_data_block) ||
	    goal >= le32_to_cpu(es->s_blocks_count))
		goal = le32_to_cpu(es->s_first_data_block);
	group_no = (goal - le32_to_cpu(es->s_first_data_block)) /
			EXT3_BLOCKS_PER_GROUP(sb);
	gdp = ext3_get_group_desc(sb, group_no, &gdp_bh);
	if (!gdp)
		goto io_error;

	goal_group = group_no;
retry:
	free_blocks = le16_to_cpu(gdp->bg_free_blocks_count);
	/*
	 * if there is not enough free blocks to make a new resevation
	 * turn off reservation for this allocation
	 */
	if (my_rsv && (free_blocks < windowsz)
		&& (rsv_is_empty(&my_rsv->rsv_window)))
		my_rsv = NULL;

	if (free_blocks > 0) {
		ret_block = ((goal - le32_to_cpu(es->s_first_data_block)) %
				EXT3_BLOCKS_PER_GROUP(sb));
		bitmap_bh = read_block_bitmap(sb, group_no);
		if (!bitmap_bh)
			goto io_error;
		ret_block = ext3_try_to_allocate_with_rsv(sb, handle, group_no,
					bitmap_bh, ret_block, my_rsv, &fatal);
		if (fatal)
			goto out;
		if (ret_block >= 0)
			goto allocated;
	}

	ngroups = EXT3_SB(sb)->s_groups_count;
	smp_rmb();

	/*
	 * Now search the rest of the groups.  We assume that 
	 * i and gdp correctly point to the last group visited.
	 */
	for (bgi = 0; bgi < ngroups; bgi++) {
		group_no++;
		if (group_no >= ngroups)
			group_no = 0;
		gdp = ext3_get_group_desc(sb, group_no, &gdp_bh);
		if (!gdp) {
			*errp = -EIO;
			goto out;
		}
		free_blocks = le16_to_cpu(gdp->bg_free_blocks_count);
		/*
		 * skip this group if the number of
		 * free blocks is less than half of the reservation
		 * window size.
		 */
		if (free_blocks <= (windowsz/2))
			continue;

		brelse(bitmap_bh);
		bitmap_bh = read_block_bitmap(sb, group_no);
		if (!bitmap_bh)
			goto io_error;
		ret_block = ext3_try_to_allocate_with_rsv(sb, handle, group_no,
					bitmap_bh, -1, my_rsv, &fatal);
		if (fatal)
			goto out;
		if (ret_block >= 0) 
			goto allocated;
	}
	/*
	 * We may end up a bogus ealier ENOSPC error due to
	 * filesystem is "full" of reservations, but
	 * there maybe indeed free blocks avaliable on disk
	 * In this case, we just forget about the reservations
	 * just do block allocation as without reservations.
	 */
	if (my_rsv) {
		my_rsv = NULL;
		group_no = goal_group;
		goto retry;
	}
	/* No space left on the device */
	*errp = -ENOSPC;
	goto out;

allocated:

	ext3_debug("using block group %d(%d)\n",
			group_no, gdp->bg_free_blocks_count);

	BUFFER_TRACE(gdp_bh, "get_write_access");
	fatal = ext3_journal_get_write_access(handle, gdp_bh);
	if (fatal)
		goto out;

	target_block = ret_block + group_no * EXT3_BLOCKS_PER_GROUP(sb)
				+ le32_to_cpu(es->s_first_data_block);

	if (target_block == le32_to_cpu(gdp->bg_block_bitmap) ||
	    target_block == le32_to_cpu(gdp->bg_inode_bitmap) ||
	    in_range(target_block, le32_to_cpu(gdp->bg_inode_table),
		      EXT3_SB(sb)->s_itb_per_group))
		ext3_error(sb, "ext3_new_block",
			    "Allocating block in system zone - "
			    "block = %u", target_block);

	performed_allocation = 1;

#ifdef CONFIG_JBD_DEBUG
	{
		struct buffer_head *debug_bh;

		/* Record bitmap buffer state in the newly allocated block */
		debug_bh = sb_find_get_block(sb, target_block);
		if (debug_bh) {
			BUFFER_TRACE(debug_bh, "state when allocated");
			BUFFER_TRACE2(debug_bh, bitmap_bh, "bitmap state");
			brelse(debug_bh);
		}
	}
	jbd_lock_bh_state(bitmap_bh);
	spin_lock(sb_bgl_lock(sbi, group_no));
	if (buffer_jbd(bitmap_bh) && bh2jh(bitmap_bh)->b_committed_data) {
		if (ext3_test_bit(ret_block,
				bh2jh(bitmap_bh)->b_committed_data)) {
			printk("%s: block was unexpectedly set in "
				"b_committed_data\n", __FUNCTION__);
		}
	}
	ext3_debug("found bit %d\n", ret_block);
	spin_unlock(sb_bgl_lock(sbi, group_no));
	jbd_unlock_bh_state(bitmap_bh);
#endif

	/* ret_block was blockgroup-relative.  Now it becomes fs-relative */
	ret_block = target_block;

	if (ret_block >= le32_to_cpu(es->s_blocks_count)) {
		ext3_error(sb, "ext3_new_block",
			    "block(%d) >= blocks count(%d) - "
			    "block_group = %d, es == %p ", ret_block,
			le32_to_cpu(es->s_blocks_count), group_no, es);
		goto out;
	}

	/*
	 * It is up to the caller to add the new buffer to a journal
	 * list of some description.  We don't know in advance whether
	 * the caller wants to use it as metadata or data.
	 */
	ext3_debug("allocating block %d. Goal hits %d of %d.\n",
			ret_block, goal_hits, goal_attempts);

	spin_lock(sb_bgl_lock(sbi, group_no));
	gdp->bg_free_blocks_count =
			cpu_to_le16(le16_to_cpu(gdp->bg_free_blocks_count) - 1);
	spin_unlock(sb_bgl_lock(sbi, group_no));
	percpu_counter_mod(&sbi->s_freeblocks_counter, -1);

	BUFFER_TRACE(gdp_bh, "journal_dirty_metadata for group descriptor");
	err = ext3_journal_dirty_metadata(handle, gdp_bh);
	if (!fatal)
		fatal = err;

	sb->s_dirt = 1;
	if (fatal)
		goto out;

	*errp = 0;
	brelse(bitmap_bh);
	return ret_block;

io_error:
	*errp = -EIO;
out:
	if (fatal) {
		*errp = fatal;
		ext3_std_error(sb, fatal);
	}
	/*
	 * Undo the block allocation
	 */
	if (!performed_allocation)
		DQUOT_FREE_BLOCK(inode, 1);
	brelse(bitmap_bh);
	return 0;
}

unsigned long ext3_count_free_blocks(struct super_block *sb)
{
	unsigned long desc_count;
	struct ext3_group_desc *gdp;
	int i;
	unsigned long ngroups = EXT3_SB(sb)->s_groups_count;
#ifdef EXT3FS_DEBUG
	struct ext3_super_block *es;
	unsigned long bitmap_count, x;
	struct buffer_head *bitmap_bh = NULL;

	lock_super(sb);
	es = EXT3_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;

	for (i = 0; i < ngroups; i++) {
		gdp = ext3_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_blocks_count);
		brelse(bitmap_bh);
		bitmap_bh = read_block_bitmap(sb, i);
		if (bitmap_bh == NULL)
			continue;

		x = ext3_count_free(bitmap_bh, sb->s_blocksize);
		printk("group %d: stored = %d, counted = %lu\n",
			i, le16_to_cpu(gdp->bg_free_blocks_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk("ext3_count_free_blocks: stored = %u, computed = %lu, %lu\n",
	       le32_to_cpu(es->s_free_blocks_count), desc_count, bitmap_count);
	unlock_super(sb);
	return bitmap_count;
#else
	desc_count = 0;
	smp_rmb();
	for (i = 0; i < ngroups; i++) {
		gdp = ext3_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_blocks_count);
	}

	return desc_count;
#endif
}

static inline int
block_in_use(unsigned long block, struct super_block *sb, unsigned char *map)
{
	return ext3_test_bit ((block -
		le32_to_cpu(EXT3_SB(sb)->s_es->s_first_data_block)) %
			 EXT3_BLOCKS_PER_GROUP(sb), map);
}

static inline int test_root(int a, int b)
{
	int num = b;

	while (a > num)
		num *= b;
	return num == a;
}

static int ext3_group_sparse(int group)
{
	if (group <= 1)
		return 1;
	if (!(group & 1))
		return 0;
	return (test_root(group, 7) || test_root(group, 5) ||
		test_root(group, 3));
}

/**
 *	ext3_bg_has_super - number of blocks used by the superblock in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the superblock (primary or backup)
 *	in this group.  Currently this will be only 0 or 1.
 */
int ext3_bg_has_super(struct super_block *sb, int group)
{
	if (EXT3_HAS_RO_COMPAT_FEATURE(sb,EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
	    !ext3_group_sparse(group))
		return 0;
	return 1;
}

/**
 *	ext3_bg_num_gdb - number of blocks used by the group table in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the group descriptor table
 *	(primary or backup) in this group.  In the future there may be a
 *	different number of descriptor blocks in each group.
 */
unsigned long ext3_bg_num_gdb(struct super_block *sb, int group)
{
	if (EXT3_HAS_RO_COMPAT_FEATURE(sb,EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
	    !ext3_group_sparse(group))
		return 0;
	return EXT3_SB(sb)->s_gdb_count;
}

#ifdef CONFIG_EXT3_CHECK
/* Called at mount-time, super-block is locked */
void ext3_check_blocks_bitmap (struct super_block * sb)
{
	struct ext3_super_block *es;
	unsigned long desc_count, bitmap_count, x, j;
	unsigned long desc_blocks;
	struct buffer_head *bitmap_bh = NULL;
	struct ext3_group_desc *gdp;
	int i;

	es = EXT3_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < EXT3_SB(sb)->s_groups_count; i++) {
		gdp = ext3_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_blocks_count);
		brelse(bitmap_bh);
		bitmap_bh = read_block_bitmap(sb, i);
		if (bitmap_bh == NULL)
			continue;

		if (ext3_bg_has_super(sb, i) &&
				!ext3_test_bit(0, bitmap_bh->b_data))
			ext3_error(sb, __FUNCTION__,
				   "Superblock in group %d is marked free", i);

		desc_blocks = ext3_bg_num_gdb(sb, i);
		for (j = 0; j < desc_blocks; j++)
			if (!ext3_test_bit(j + 1, bitmap_bh->b_data))
				ext3_error(sb, __FUNCTION__,
					   "Descriptor block #%ld in group "
					   "%d is marked free", j, i);

		if (!block_in_use (le32_to_cpu(gdp->bg_block_bitmap),
						sb, bitmap_bh->b_data))
			ext3_error (sb, "ext3_check_blocks_bitmap",
				    "Block bitmap for group %d is marked free",
				    i);

		if (!block_in_use (le32_to_cpu(gdp->bg_inode_bitmap),
						sb, bitmap_bh->b_data))
			ext3_error (sb, "ext3_check_blocks_bitmap",
				    "Inode bitmap for group %d is marked free",
				    i);

		for (j = 0; j < EXT3_SB(sb)->s_itb_per_group; j++)
			if (!block_in_use (le32_to_cpu(gdp->bg_inode_table) + j,
							sb, bitmap_bh->b_data))
				ext3_error (sb, "ext3_check_blocks_bitmap",
					    "Block #%d of the inode table in "
					    "group %d is marked free", j, i);

		x = ext3_count_free(bitmap_bh, sb->s_blocksize);
		if (le16_to_cpu(gdp->bg_free_blocks_count) != x)
			ext3_error (sb, "ext3_check_blocks_bitmap",
				    "Wrong free blocks count for group %d, "
				    "stored = %d, counted = %lu", i,
				    le16_to_cpu(gdp->bg_free_blocks_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	if (le32_to_cpu(es->s_free_blocks_count) != bitmap_count)
		ext3_error (sb, "ext3_check_blocks_bitmap",
			"Wrong free blocks count in super block, "
			"stored = %lu, counted = %lu",
			(unsigned long)le32_to_cpu(es->s_free_blocks_count),
			bitmap_count);
}
#endif
