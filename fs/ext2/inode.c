// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext2/iyesde.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/iyesde.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Goal-directed block allocation by Stephen Tweedie
 * 	(sct@dcs.ed.ac.uk), 1993, 1998
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 *
 *  Assorted race fixes, rewrite of ext2_get_block() by Al Viro, 2000
 */

#include <linux/time.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/dax.h>
#include <linux/blkdev.h>
#include <linux/quotaops.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/fiemap.h>
#include <linux/iomap.h>
#include <linux/namei.h>
#include <linux/uio.h>
#include "ext2.h"
#include "acl.h"
#include "xattr.h"

static int __ext2_write_iyesde(struct iyesde *iyesde, int do_sync);

/*
 * Test whether an iyesde is a fast symlink.
 */
static inline int ext2_iyesde_is_fast_symlink(struct iyesde *iyesde)
{
	int ea_blocks = EXT2_I(iyesde)->i_file_acl ?
		(iyesde->i_sb->s_blocksize >> 9) : 0;

	return (S_ISLNK(iyesde->i_mode) &&
		iyesde->i_blocks - ea_blocks == 0);
}

static void ext2_truncate_blocks(struct iyesde *iyesde, loff_t offset);

static void ext2_write_failed(struct address_space *mapping, loff_t to)
{
	struct iyesde *iyesde = mapping->host;

	if (to > iyesde->i_size) {
		truncate_pagecache(iyesde, iyesde->i_size);
		ext2_truncate_blocks(iyesde, iyesde->i_size);
	}
}

/*
 * Called at the last iput() if i_nlink is zero.
 */
void ext2_evict_iyesde(struct iyesde * iyesde)
{
	struct ext2_block_alloc_info *rsv;
	int want_delete = 0;

	if (!iyesde->i_nlink && !is_bad_iyesde(iyesde)) {
		want_delete = 1;
		dquot_initialize(iyesde);
	} else {
		dquot_drop(iyesde);
	}

	truncate_iyesde_pages_final(&iyesde->i_data);

	if (want_delete) {
		sb_start_intwrite(iyesde->i_sb);
		/* set dtime */
		EXT2_I(iyesde)->i_dtime	= ktime_get_real_seconds();
		mark_iyesde_dirty(iyesde);
		__ext2_write_iyesde(iyesde, iyesde_needs_sync(iyesde));
		/* truncate to 0 */
		iyesde->i_size = 0;
		if (iyesde->i_blocks)
			ext2_truncate_blocks(iyesde, 0);
		ext2_xattr_delete_iyesde(iyesde);
	}

	invalidate_iyesde_buffers(iyesde);
	clear_iyesde(iyesde);

	ext2_discard_reservation(iyesde);
	rsv = EXT2_I(iyesde)->i_block_alloc_info;
	EXT2_I(iyesde)->i_block_alloc_info = NULL;
	if (unlikely(rsv))
		kfree(rsv);

	if (want_delete) {
		ext2_free_iyesde(iyesde);
		sb_end_intwrite(iyesde->i_sb);
	}
}

typedef struct {
	__le32	*p;
	__le32	key;
	struct buffer_head *bh;
} Indirect;

static inline void add_chain(Indirect *p, struct buffer_head *bh, __le32 *v)
{
	p->key = *(p->p = v);
	p->bh = bh;
}

static inline int verify_chain(Indirect *from, Indirect *to)
{
	while (from <= to && from->key == *from->p)
		from++;
	return (from > to);
}

/**
 *	ext2_block_to_path - parse the block number into array of offsets
 *	@iyesde: iyesde in question (we are only interested in its superblock)
 *	@i_block: block number to be parsed
 *	@offsets: array to store the offsets in
 *      @boundary: set this yesn-zero if the referred-to block is likely to be
 *             followed (on disk) by an indirect block.
 *	To store the locations of file's data ext2 uses a data structure common
 *	for UNIX filesystems - tree of pointers anchored in the iyesde, with
 *	data blocks at leaves and indirect blocks in intermediate yesdes.
 *	This function translates the block number into path in that tree -
 *	return value is the path length and @offsets[n] is the offset of
 *	pointer to (n+1)th yesde in the nth one. If @block is out of range
 *	(negative or too large) warning is printed and zero returned.
 *
 *	Note: function doesn't find yesde addresses, so yes IO is needed. All
 *	we need to kyesw is the capacity of indirect blocks (taken from the
 *	iyesde->i_sb).
 */

/*
 * Portability yeste: the last comparison (check that we fit into triple
 * indirect block) is spelled differently, because otherwise on an
 * architecture with 32-bit longs and 8Kb pages we might get into trouble
 * if our filesystem had 8Kb blocks. We might use long long, but that would
 * kill us on x86. Oh, well, at least the sign propagation does yest matter -
 * i_block would have to be negative in the very beginning, so we would yest
 * get there at all.
 */

static int ext2_block_to_path(struct iyesde *iyesde,
			long i_block, int offsets[4], int *boundary)
{
	int ptrs = EXT2_ADDR_PER_BLOCK(iyesde->i_sb);
	int ptrs_bits = EXT2_ADDR_PER_BLOCK_BITS(iyesde->i_sb);
	const long direct_blocks = EXT2_NDIR_BLOCKS,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2));
	int n = 0;
	int final = 0;

	if (i_block < 0) {
		ext2_msg(iyesde->i_sb, KERN_WARNING,
			"warning: %s: block < 0", __func__);
	} else if (i_block < direct_blocks) {
		offsets[n++] = i_block;
		final = direct_blocks;
	} else if ( (i_block -= direct_blocks) < indirect_blocks) {
		offsets[n++] = EXT2_IND_BLOCK;
		offsets[n++] = i_block;
		final = ptrs;
	} else if ((i_block -= indirect_blocks) < double_blocks) {
		offsets[n++] = EXT2_DIND_BLOCK;
		offsets[n++] = i_block >> ptrs_bits;
		offsets[n++] = i_block & (ptrs - 1);
		final = ptrs;
	} else if (((i_block -= double_blocks) >> (ptrs_bits * 2)) < ptrs) {
		offsets[n++] = EXT2_TIND_BLOCK;
		offsets[n++] = i_block >> (ptrs_bits * 2);
		offsets[n++] = (i_block >> ptrs_bits) & (ptrs - 1);
		offsets[n++] = i_block & (ptrs - 1);
		final = ptrs;
	} else {
		ext2_msg(iyesde->i_sb, KERN_WARNING,
			"warning: %s: block is too big", __func__);
	}
	if (boundary)
		*boundary = final - 1 - (i_block & (ptrs - 1));

	return n;
}

/**
 *	ext2_get_branch - read the chain of indirect blocks leading to data
 *	@iyesde: iyesde in question
 *	@depth: depth of the chain (1 - direct pointer, etc.)
 *	@offsets: offsets of pointers in iyesde/indirect blocks
 *	@chain: place to store the result
 *	@err: here we store the error value
 *
 *	Function fills the array of triples <key, p, bh> and returns %NULL
 *	if everything went OK or the pointer to the last filled triple
 *	(incomplete one) otherwise. Upon the return chain[i].key contains
 *	the number of (i+1)-th block in the chain (as it is stored in memory,
 *	i.e. little-endian 32-bit), chain[i].p contains the address of that
 *	number (it points into struct iyesde for i==0 and into the bh->b_data
 *	for i>0) and chain[i].bh points to the buffer_head of i-th indirect
 *	block for i>0 and NULL for i==0. In other words, it holds the block
 *	numbers of the chain, addresses they were taken from (and where we can
 *	verify that chain did yest change) and buffer_heads hosting these
 *	numbers.
 *
 *	Function stops when it stumbles upon zero pointer (absent block)
 *		(pointer to last triple returned, *@err == 0)
 *	or when it gets an IO error reading an indirect block
 *		(ditto, *@err == -EIO)
 *	or when it yestices that chain had been changed while it was reading
 *		(ditto, *@err == -EAGAIN)
 *	or when it reads all @depth-1 indirect blocks successfully and finds
 *	the whole chain, all way to the data (returns %NULL, *err == 0).
 */
static Indirect *ext2_get_branch(struct iyesde *iyesde,
				 int depth,
				 int *offsets,
				 Indirect chain[4],
				 int *err)
{
	struct super_block *sb = iyesde->i_sb;
	Indirect *p = chain;
	struct buffer_head *bh;

	*err = 0;
	/* i_data is yest going away, yes lock needed */
	add_chain (chain, NULL, EXT2_I(iyesde)->i_data + *offsets);
	if (!p->key)
		goto yes_block;
	while (--depth) {
		bh = sb_bread(sb, le32_to_cpu(p->key));
		if (!bh)
			goto failure;
		read_lock(&EXT2_I(iyesde)->i_meta_lock);
		if (!verify_chain(chain, p))
			goto changed;
		add_chain(++p, bh, (__le32*)bh->b_data + *++offsets);
		read_unlock(&EXT2_I(iyesde)->i_meta_lock);
		if (!p->key)
			goto yes_block;
	}
	return NULL;

changed:
	read_unlock(&EXT2_I(iyesde)->i_meta_lock);
	brelse(bh);
	*err = -EAGAIN;
	goto yes_block;
failure:
	*err = -EIO;
yes_block:
	return p;
}

/**
 *	ext2_find_near - find a place for allocation with sufficient locality
 *	@iyesde: owner
 *	@ind: descriptor of indirect block.
 *
 *	This function returns the preferred place for block allocation.
 *	It is used when heuristic for sequential allocation fails.
 *	Rules are:
 *	  + if there is a block to the left of our position - allocate near it.
 *	  + if pointer will live in indirect block - allocate near that block.
 *	  + if pointer will live in iyesde - allocate in the same cylinder group.
 *
 * In the latter case we colour the starting block by the callers PID to
 * prevent it from clashing with concurrent allocations for a different iyesde
 * in the same block group.   The PID is used here so that functionally related
 * files will be close-by on-disk.
 *
 *	Caller must make sure that @ind is valid and will stay that way.
 */

static ext2_fsblk_t ext2_find_near(struct iyesde *iyesde, Indirect *ind)
{
	struct ext2_iyesde_info *ei = EXT2_I(iyesde);
	__le32 *start = ind->bh ? (__le32 *) ind->bh->b_data : ei->i_data;
	__le32 *p;
	ext2_fsblk_t bg_start;
	ext2_fsblk_t colour;

	/* Try to find previous block */
	for (p = ind->p - 1; p >= start; p--)
		if (*p)
			return le32_to_cpu(*p);

	/* No such thing, so let's try location of indirect block */
	if (ind->bh)
		return ind->bh->b_blocknr;

	/*
	 * It is going to be referred from iyesde itself? OK, just put it into
	 * the same cylinder group then.
	 */
	bg_start = ext2_group_first_block_yes(iyesde->i_sb, ei->i_block_group);
	colour = (current->pid % 16) *
			(EXT2_BLOCKS_PER_GROUP(iyesde->i_sb) / 16);
	return bg_start + colour;
}

/**
 *	ext2_find_goal - find a preferred place for allocation.
 *	@iyesde: owner
 *	@block:  block we want
 *	@partial: pointer to the last triple within a chain
 *
 *	Returns preferred place for a block (the goal).
 */

static inline ext2_fsblk_t ext2_find_goal(struct iyesde *iyesde, long block,
					  Indirect *partial)
{
	struct ext2_block_alloc_info *block_i;

	block_i = EXT2_I(iyesde)->i_block_alloc_info;

	/*
	 * try the heuristic for sequential allocation,
	 * failing that at least try to get decent locality.
	 */
	if (block_i && (block == block_i->last_alloc_logical_block + 1)
		&& (block_i->last_alloc_physical_block != 0)) {
		return block_i->last_alloc_physical_block + 1;
	}

	return ext2_find_near(iyesde, partial);
}

/**
 *	ext2_blks_to_allocate: Look up the block map and count the number
 *	of direct blocks need to be allocated for the given branch.
 *
 * 	@branch: chain of indirect blocks
 *	@k: number of blocks need for indirect blocks
 *	@blks: number of data blocks to be mapped.
 *	@blocks_to_boundary:  the offset in the indirect block
 *
 *	return the total number of blocks to be allocate, including the
 *	direct and indirect blocks.
 */
static int
ext2_blks_to_allocate(Indirect * branch, int k, unsigned long blks,
		int blocks_to_boundary)
{
	unsigned long count = 0;

	/*
	 * Simple case, [t,d]Indirect block(s) has yest allocated yet
	 * then it's clear blocks on that path have yest allocated
	 */
	if (k > 0) {
		/* right yesw don't hanel cross boundary allocation */
		if (blks < blocks_to_boundary + 1)
			count += blks;
		else
			count += blocks_to_boundary + 1;
		return count;
	}

	count++;
	while (count < blks && count <= blocks_to_boundary
		&& le32_to_cpu(*(branch[0].p + count)) == 0) {
		count++;
	}
	return count;
}

/**
 *	ext2_alloc_blocks: multiple allocate blocks needed for a branch
 *	@indirect_blks: the number of blocks need to allocate for indirect
 *			blocks
 *
 *	@new_blocks: on return it will store the new block numbers for
 *	the indirect blocks(if needed) and the first direct block,
 *	@blks:	on return it will store the total number of allocated
 *		direct blocks
 */
static int ext2_alloc_blocks(struct iyesde *iyesde,
			ext2_fsblk_t goal, int indirect_blks, int blks,
			ext2_fsblk_t new_blocks[4], int *err)
{
	int target, i;
	unsigned long count = 0;
	int index = 0;
	ext2_fsblk_t current_block = 0;
	int ret = 0;

	/*
	 * Here we try to allocate the requested multiple blocks at once,
	 * on a best-effort basis.
	 * To build a branch, we should allocate blocks for
	 * the indirect blocks(if yest allocated yet), and at least
	 * the first direct block of this branch.  That's the
	 * minimum number of blocks need to allocate(required)
	 */
	target = blks + indirect_blks;

	while (1) {
		count = target;
		/* allocating blocks for indirect blocks and direct blocks */
		current_block = ext2_new_blocks(iyesde,goal,&count,err);
		if (*err)
			goto failed_out;

		target -= count;
		/* allocate blocks for indirect blocks */
		while (index < indirect_blks && count) {
			new_blocks[index++] = current_block++;
			count--;
		}

		if (count > 0)
			break;
	}

	/* save the new block number for the first direct block */
	new_blocks[index] = current_block;

	/* total number of blocks allocated for direct blocks */
	ret = count;
	*err = 0;
	return ret;
failed_out:
	for (i = 0; i <index; i++)
		ext2_free_blocks(iyesde, new_blocks[i], 1);
	if (index)
		mark_iyesde_dirty(iyesde);
	return ret;
}

/**
 *	ext2_alloc_branch - allocate and set up a chain of blocks.
 *	@iyesde: owner
 *	@indirect_blks: depth of the chain (number of blocks to allocate)
 *	@blks: number of allocated direct blocks
 *	@goal: preferred place for allocation
 *	@offsets: offsets (in the blocks) to store the pointers to next.
 *	@branch: place to store the chain in.
 *
 *	This function allocates @num blocks, zeroes out all but the last one,
 *	links them into chain and (if we are synchroyesus) writes them to disk.
 *	In other words, it prepares a branch that can be spliced onto the
 *	iyesde. It stores the information about that chain in the branch[], in
 *	the same format as ext2_get_branch() would do. We are calling it after
 *	we had read the existing part of chain and partial points to the last
 *	triple of that (one with zero ->key). Upon the exit we have the same
 *	picture as after the successful ext2_get_block(), except that in one
 *	place chain is disconnected - *branch->p is still zero (we did yest
 *	set the last link), but branch->key contains the number that should
 *	be placed into *branch->p to fill that gap.
 *
 *	If allocation fails we free all blocks we've allocated (and forget
 *	their buffer_heads) and return the error value the from failed
 *	ext2_alloc_block() (yesrmally -ENOSPC). Otherwise we set the chain
 *	as described above and return 0.
 */

static int ext2_alloc_branch(struct iyesde *iyesde,
			int indirect_blks, int *blks, ext2_fsblk_t goal,
			int *offsets, Indirect *branch)
{
	int blocksize = iyesde->i_sb->s_blocksize;
	int i, n = 0;
	int err = 0;
	struct buffer_head *bh;
	int num;
	ext2_fsblk_t new_blocks[4];
	ext2_fsblk_t current_block;

	num = ext2_alloc_blocks(iyesde, goal, indirect_blks,
				*blks, new_blocks, &err);
	if (err)
		return err;

	branch[0].key = cpu_to_le32(new_blocks[0]);
	/*
	 * metadata blocks and data blocks are allocated.
	 */
	for (n = 1; n <= indirect_blks;  n++) {
		/*
		 * Get buffer_head for parent block, zero it out
		 * and set the pointer to new one, then send
		 * parent to disk.
		 */
		bh = sb_getblk(iyesde->i_sb, new_blocks[n-1]);
		if (unlikely(!bh)) {
			err = -ENOMEM;
			goto failed;
		}
		branch[n].bh = bh;
		lock_buffer(bh);
		memset(bh->b_data, 0, blocksize);
		branch[n].p = (__le32 *) bh->b_data + offsets[n];
		branch[n].key = cpu_to_le32(new_blocks[n]);
		*branch[n].p = branch[n].key;
		if ( n == indirect_blks) {
			current_block = new_blocks[n];
			/*
			 * End of chain, update the last new metablock of
			 * the chain to point to the new allocated
			 * data blocks numbers
			 */
			for (i=1; i < num; i++)
				*(branch[n].p + i) = cpu_to_le32(++current_block);
		}
		set_buffer_uptodate(bh);
		unlock_buffer(bh);
		mark_buffer_dirty_iyesde(bh, iyesde);
		/* We used to sync bh here if IS_SYNC(iyesde).
		 * But we yesw rely upon generic_write_sync()
		 * and b_iyesde_buffers.  But yest for directories.
		 */
		if (S_ISDIR(iyesde->i_mode) && IS_DIRSYNC(iyesde))
			sync_dirty_buffer(bh);
	}
	*blks = num;
	return err;

failed:
	for (i = 1; i < n; i++)
		bforget(branch[i].bh);
	for (i = 0; i < indirect_blks; i++)
		ext2_free_blocks(iyesde, new_blocks[i], 1);
	ext2_free_blocks(iyesde, new_blocks[i], num);
	return err;
}

/**
 * ext2_splice_branch - splice the allocated branch onto iyesde.
 * @iyesde: owner
 * @block: (logical) number of block we are adding
 * @where: location of missing link
 * @num:   number of indirect blocks we are adding
 * @blks:  number of direct blocks we are adding
 *
 * This function fills the missing link and does all housekeeping needed in
 * iyesde (->i_blocks, etc.). In case of success we end up with the full
 * chain to new block and return 0.
 */
static void ext2_splice_branch(struct iyesde *iyesde,
			long block, Indirect *where, int num, int blks)
{
	int i;
	struct ext2_block_alloc_info *block_i;
	ext2_fsblk_t current_block;

	block_i = EXT2_I(iyesde)->i_block_alloc_info;

	/* XXX LOCKING probably should have i_meta_lock ?*/
	/* That's it */

	*where->p = where->key;

	/*
	 * Update the host buffer_head or iyesde to point to more just allocated
	 * direct blocks blocks
	 */
	if (num == 0 && blks > 1) {
		current_block = le32_to_cpu(where->key) + 1;
		for (i = 1; i < blks; i++)
			*(where->p + i ) = cpu_to_le32(current_block++);
	}

	/*
	 * update the most recently allocated logical & physical block
	 * in i_block_alloc_info, to assist find the proper goal block for next
	 * allocation
	 */
	if (block_i) {
		block_i->last_alloc_logical_block = block + blks - 1;
		block_i->last_alloc_physical_block =
				le32_to_cpu(where[num].key) + blks - 1;
	}

	/* We are done with atomic stuff, yesw do the rest of housekeeping */

	/* had we spliced it onto indirect block? */
	if (where->bh)
		mark_buffer_dirty_iyesde(where->bh, iyesde);

	iyesde->i_ctime = current_time(iyesde);
	mark_iyesde_dirty(iyesde);
}

/*
 * Allocation strategy is simple: if we have to allocate something, we will
 * have to go the whole way to leaf. So let's do it before attaching anything
 * to tree, set linkage between the newborn blocks, write them if sync is
 * required, recheck the path, free and repeat if check fails, otherwise
 * set the last missing link (that will protect us from any truncate-generated
 * removals - all blocks on the path are immune yesw) and possibly force the
 * write on the parent block.
 * That has a nice additional property: yes special recovery from the failed
 * allocations is needed - we simply release blocks and do yest touch anything
 * reachable from iyesde.
 *
 * `handle' can be NULL if create == 0.
 *
 * return > 0, # of blocks mapped or allocated.
 * return = 0, if plain lookup failed.
 * return < 0, error case.
 */
static int ext2_get_blocks(struct iyesde *iyesde,
			   sector_t iblock, unsigned long maxblocks,
			   u32 *byes, bool *new, bool *boundary,
			   int create)
{
	int err;
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
	ext2_fsblk_t goal;
	int indirect_blks;
	int blocks_to_boundary = 0;
	int depth;
	struct ext2_iyesde_info *ei = EXT2_I(iyesde);
	int count = 0;
	ext2_fsblk_t first_block = 0;

	BUG_ON(maxblocks == 0);

	depth = ext2_block_to_path(iyesde,iblock,offsets,&blocks_to_boundary);

	if (depth == 0)
		return -EIO;

	partial = ext2_get_branch(iyesde, depth, offsets, chain, &err);
	/* Simplest case - block found, yes allocation needed */
	if (!partial) {
		first_block = le32_to_cpu(chain[depth - 1].key);
		count++;
		/*map more blocks*/
		while (count < maxblocks && count <= blocks_to_boundary) {
			ext2_fsblk_t blk;

			if (!verify_chain(chain, chain + depth - 1)) {
				/*
				 * Indirect block might be removed by
				 * truncate while we were reading it.
				 * Handling of that case: forget what we've
				 * got yesw, go to reread.
				 */
				err = -EAGAIN;
				count = 0;
				partial = chain + depth - 1;
				break;
			}
			blk = le32_to_cpu(*(chain[depth-1].p + count));
			if (blk == first_block + count)
				count++;
			else
				break;
		}
		if (err != -EAGAIN)
			goto got_it;
	}

	/* Next simple case - plain lookup or failed read of indirect block */
	if (!create || err == -EIO)
		goto cleanup;

	mutex_lock(&ei->truncate_mutex);
	/*
	 * If the indirect block is missing while we are reading
	 * the chain(ext2_get_branch() returns -EAGAIN err), or
	 * if the chain has been changed after we grab the semaphore,
	 * (either because ayesther process truncated this branch, or
	 * ayesther get_block allocated this branch) re-grab the chain to see if
	 * the request block has been allocated or yest.
	 *
	 * Since we already block the truncate/other get_block
	 * at this point, we will have the current copy of the chain when we
	 * splice the branch into the tree.
	 */
	if (err == -EAGAIN || !verify_chain(chain, partial)) {
		while (partial > chain) {
			brelse(partial->bh);
			partial--;
		}
		partial = ext2_get_branch(iyesde, depth, offsets, chain, &err);
		if (!partial) {
			count++;
			mutex_unlock(&ei->truncate_mutex);
			goto got_it;
		}

		if (err) {
			mutex_unlock(&ei->truncate_mutex);
			goto cleanup;
		}
	}

	/*
	 * Okay, we need to do block allocation.  Lazily initialize the block
	 * allocation info here if necessary
	*/
	if (S_ISREG(iyesde->i_mode) && (!ei->i_block_alloc_info))
		ext2_init_block_alloc_info(iyesde);

	goal = ext2_find_goal(iyesde, iblock, partial);

	/* the number of blocks need to allocate for [d,t]indirect blocks */
	indirect_blks = (chain + depth) - partial - 1;
	/*
	 * Next look up the indirect map to count the total number of
	 * direct blocks to allocate for this branch.
	 */
	count = ext2_blks_to_allocate(partial, indirect_blks,
					maxblocks, blocks_to_boundary);
	/*
	 * XXX ???? Block out ext2_truncate while we alter the tree
	 */
	err = ext2_alloc_branch(iyesde, indirect_blks, &count, goal,
				offsets + (partial - chain), partial);

	if (err) {
		mutex_unlock(&ei->truncate_mutex);
		goto cleanup;
	}

	if (IS_DAX(iyesde)) {
		/*
		 * We must unmap blocks before zeroing so that writeback canyest
		 * overwrite zeros with stale data from block device page cache.
		 */
		clean_bdev_aliases(iyesde->i_sb->s_bdev,
				   le32_to_cpu(chain[depth-1].key),
				   count);
		/*
		 * block must be initialised before we put it in the tree
		 * so that it's yest found by ayesther thread before it's
		 * initialised
		 */
		err = sb_issue_zeroout(iyesde->i_sb,
				le32_to_cpu(chain[depth-1].key), count,
				GFP_NOFS);
		if (err) {
			mutex_unlock(&ei->truncate_mutex);
			goto cleanup;
		}
	}
	*new = true;

	ext2_splice_branch(iyesde, iblock, partial, indirect_blks, count);
	mutex_unlock(&ei->truncate_mutex);
got_it:
	if (count > blocks_to_boundary)
		*boundary = true;
	err = count;
	/* Clean up and exit */
	partial = chain + depth - 1;	/* the whole chain */
cleanup:
	while (partial > chain) {
		brelse(partial->bh);
		partial--;
	}
	if (err > 0)
		*byes = le32_to_cpu(chain[depth-1].key);
	return err;
}

int ext2_get_block(struct iyesde *iyesde, sector_t iblock,
		struct buffer_head *bh_result, int create)
{
	unsigned max_blocks = bh_result->b_size >> iyesde->i_blkbits;
	bool new = false, boundary = false;
	u32 byes;
	int ret;

	ret = ext2_get_blocks(iyesde, iblock, max_blocks, &byes, &new, &boundary,
			create);
	if (ret <= 0)
		return ret;

	map_bh(bh_result, iyesde->i_sb, byes);
	bh_result->b_size = (ret << iyesde->i_blkbits);
	if (new)
		set_buffer_new(bh_result);
	if (boundary)
		set_buffer_boundary(bh_result);
	return 0;

}

#ifdef CONFIG_FS_DAX
static int ext2_iomap_begin(struct iyesde *iyesde, loff_t offset, loff_t length,
		unsigned flags, struct iomap *iomap, struct iomap *srcmap)
{
	unsigned int blkbits = iyesde->i_blkbits;
	unsigned long first_block = offset >> blkbits;
	unsigned long max_blocks = (length + (1 << blkbits) - 1) >> blkbits;
	struct ext2_sb_info *sbi = EXT2_SB(iyesde->i_sb);
	bool new = false, boundary = false;
	u32 byes;
	int ret;

	ret = ext2_get_blocks(iyesde, first_block, max_blocks,
			&byes, &new, &boundary, flags & IOMAP_WRITE);
	if (ret < 0)
		return ret;

	iomap->flags = 0;
	iomap->bdev = iyesde->i_sb->s_bdev;
	iomap->offset = (u64)first_block << blkbits;
	iomap->dax_dev = sbi->s_daxdev;

	if (ret == 0) {
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->length = 1 << blkbits;
	} else {
		iomap->type = IOMAP_MAPPED;
		iomap->addr = (u64)byes << blkbits;
		iomap->length = (u64)ret << blkbits;
		iomap->flags |= IOMAP_F_MERGED;
	}

	if (new)
		iomap->flags |= IOMAP_F_NEW;
	return 0;
}

static int
ext2_iomap_end(struct iyesde *iyesde, loff_t offset, loff_t length,
		ssize_t written, unsigned flags, struct iomap *iomap)
{
	if (iomap->type == IOMAP_MAPPED &&
	    written < length &&
	    (flags & IOMAP_WRITE))
		ext2_write_failed(iyesde->i_mapping, offset + length);
	return 0;
}

const struct iomap_ops ext2_iomap_ops = {
	.iomap_begin		= ext2_iomap_begin,
	.iomap_end		= ext2_iomap_end,
};
#else
/* Define empty ops for !CONFIG_FS_DAX case to avoid ugly ifdefs */
const struct iomap_ops ext2_iomap_ops;
#endif /* CONFIG_FS_DAX */

int ext2_fiemap(struct iyesde *iyesde, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	return generic_block_fiemap(iyesde, fieinfo, start, len,
				    ext2_get_block);
}

static int ext2_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ext2_get_block, wbc);
}

static int ext2_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, ext2_get_block);
}

static int
ext2_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, ext2_get_block);
}

static int
ext2_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, flags, pagep,
				ext2_get_block);
	if (ret < 0)
		ext2_write_failed(mapping, pos + len);
	return ret;
}

static int ext2_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int ret;

	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (ret < len)
		ext2_write_failed(mapping, pos + len);
	return ret;
}

static int
ext2_yesbh_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	int ret;

	ret = yesbh_write_begin(mapping, pos, len, flags, pagep, fsdata,
			       ext2_get_block);
	if (ret < 0)
		ext2_write_failed(mapping, pos + len);
	return ret;
}

static int ext2_yesbh_writepage(struct page *page,
			struct writeback_control *wbc)
{
	return yesbh_writepage(page, ext2_get_block, wbc);
}

static sector_t ext2_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,ext2_get_block);
}

static ssize_t
ext2_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct iyesde *iyesde = mapping->host;
	size_t count = iov_iter_count(iter);
	loff_t offset = iocb->ki_pos;
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, iyesde, iter, ext2_get_block);
	if (ret < 0 && iov_iter_rw(iter) == WRITE)
		ext2_write_failed(mapping, offset + count);
	return ret;
}

static int
ext2_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, ext2_get_block);
}

static int
ext2_dax_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	return dax_writeback_mapping_range(mapping,
			mapping->host->i_sb->s_bdev, wbc);
}

const struct address_space_operations ext2_aops = {
	.readpage		= ext2_readpage,
	.readpages		= ext2_readpages,
	.writepage		= ext2_writepage,
	.write_begin		= ext2_write_begin,
	.write_end		= ext2_write_end,
	.bmap			= ext2_bmap,
	.direct_IO		= ext2_direct_IO,
	.writepages		= ext2_writepages,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate	= block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

const struct address_space_operations ext2_yesbh_aops = {
	.readpage		= ext2_readpage,
	.readpages		= ext2_readpages,
	.writepage		= ext2_yesbh_writepage,
	.write_begin		= ext2_yesbh_write_begin,
	.write_end		= yesbh_write_end,
	.bmap			= ext2_bmap,
	.direct_IO		= ext2_direct_IO,
	.writepages		= ext2_writepages,
	.migratepage		= buffer_migrate_page,
	.error_remove_page	= generic_error_remove_page,
};

static const struct address_space_operations ext2_dax_aops = {
	.writepages		= ext2_dax_writepages,
	.direct_IO		= yesop_direct_IO,
	.set_page_dirty		= yesop_set_page_dirty,
	.invalidatepage		= yesop_invalidatepage,
};

/*
 * Probably it should be a library function... search for first yesn-zero word
 * or memcmp with zero_page, whatever is better for particular architecture.
 * Linus?
 */
static inline int all_zeroes(__le32 *p, __le32 *q)
{
	while (p < q)
		if (*p++)
			return 0;
	return 1;
}

/**
 *	ext2_find_shared - find the indirect blocks for partial truncation.
 *	@iyesde:	  iyesde in question
 *	@depth:	  depth of the affected branch
 *	@offsets: offsets of pointers in that branch (see ext2_block_to_path)
 *	@chain:	  place to store the pointers to partial indirect blocks
 *	@top:	  place to the (detached) top of branch
 *
 *	This is a helper function used by ext2_truncate().
 *
 *	When we do truncate() we may have to clean the ends of several indirect
 *	blocks but leave the blocks themselves alive. Block is partially
 *	truncated if some data below the new i_size is referred from it (and
 *	it is on the path to the first completely truncated data block, indeed).
 *	We have to free the top of that path along with everything to the right
 *	of the path. Since yes allocation past the truncation point is possible
 *	until ext2_truncate() finishes, we may safely do the latter, but top
 *	of branch may require special attention - pageout below the truncation
 *	point might try to populate it.
 *
 *	We atomically detach the top of branch from the tree, store the block
 *	number of its root in *@top, pointers to buffer_heads of partially
 *	truncated blocks - in @chain[].bh and pointers to their last elements
 *	that should yest be removed - in @chain[].p. Return value is the pointer
 *	to last filled element of @chain.
 *
 *	The work left to caller to do the actual freeing of subtrees:
 *		a) free the subtree starting from *@top
 *		b) free the subtrees whose roots are stored in
 *			(@chain[i].p+1 .. end of @chain[i].bh->b_data)
 *		c) free the subtrees growing from the iyesde past the @chain[0].p
 *			(yes partially truncated stuff there).
 */

static Indirect *ext2_find_shared(struct iyesde *iyesde,
				int depth,
				int offsets[4],
				Indirect chain[4],
				__le32 *top)
{
	Indirect *partial, *p;
	int k, err;

	*top = 0;
	for (k = depth; k > 1 && !offsets[k-1]; k--)
		;
	partial = ext2_get_branch(iyesde, k, offsets, chain, &err);
	if (!partial)
		partial = chain + k-1;
	/*
	 * If the branch acquired continuation since we've looked at it -
	 * fine, it should all survive and (new) top doesn't belong to us.
	 */
	write_lock(&EXT2_I(iyesde)->i_meta_lock);
	if (!partial->key && *partial->p) {
		write_unlock(&EXT2_I(iyesde)->i_meta_lock);
		goto yes_top;
	}
	for (p=partial; p>chain && all_zeroes((__le32*)p->bh->b_data,p->p); p--)
		;
	/*
	 * OK, we've found the last block that must survive. The rest of our
	 * branch should be detached before unlocking. However, if that rest
	 * of branch is all ours and does yest grow immediately from the iyesde
	 * it's easier to cheat and just decrement partial->p.
	 */
	if (p == chain + k - 1 && p > chain) {
		p->p--;
	} else {
		*top = *p->p;
		*p->p = 0;
	}
	write_unlock(&EXT2_I(iyesde)->i_meta_lock);

	while(partial > p)
	{
		brelse(partial->bh);
		partial--;
	}
yes_top:
	return partial;
}

/**
 *	ext2_free_data - free a list of data blocks
 *	@iyesde:	iyesde we are dealing with
 *	@p:	array of block numbers
 *	@q:	points immediately past the end of array
 *
 *	We are freeing all blocks referred from that array (numbers are
 *	stored as little-endian 32-bit) and updating @iyesde->i_blocks
 *	appropriately.
 */
static inline void ext2_free_data(struct iyesde *iyesde, __le32 *p, __le32 *q)
{
	unsigned long block_to_free = 0, count = 0;
	unsigned long nr;

	for ( ; p < q ; p++) {
		nr = le32_to_cpu(*p);
		if (nr) {
			*p = 0;
			/* accumulate blocks to free if they're contiguous */
			if (count == 0)
				goto free_this;
			else if (block_to_free == nr - count)
				count++;
			else {
				ext2_free_blocks (iyesde, block_to_free, count);
				mark_iyesde_dirty(iyesde);
			free_this:
				block_to_free = nr;
				count = 1;
			}
		}
	}
	if (count > 0) {
		ext2_free_blocks (iyesde, block_to_free, count);
		mark_iyesde_dirty(iyesde);
	}
}

/**
 *	ext2_free_branches - free an array of branches
 *	@iyesde:	iyesde we are dealing with
 *	@p:	array of block numbers
 *	@q:	pointer immediately past the end of array
 *	@depth:	depth of the branches to free
 *
 *	We are freeing all blocks referred from these branches (numbers are
 *	stored as little-endian 32-bit) and updating @iyesde->i_blocks
 *	appropriately.
 */
static void ext2_free_branches(struct iyesde *iyesde, __le32 *p, __le32 *q, int depth)
{
	struct buffer_head * bh;
	unsigned long nr;

	if (depth--) {
		int addr_per_block = EXT2_ADDR_PER_BLOCK(iyesde->i_sb);
		for ( ; p < q ; p++) {
			nr = le32_to_cpu(*p);
			if (!nr)
				continue;
			*p = 0;
			bh = sb_bread(iyesde->i_sb, nr);
			/*
			 * A read failure? Report error and clear slot
			 * (should be rare).
			 */ 
			if (!bh) {
				ext2_error(iyesde->i_sb, "ext2_free_branches",
					"Read failure, iyesde=%ld, block=%ld",
					iyesde->i_iyes, nr);
				continue;
			}
			ext2_free_branches(iyesde,
					   (__le32*)bh->b_data,
					   (__le32*)bh->b_data + addr_per_block,
					   depth);
			bforget(bh);
			ext2_free_blocks(iyesde, nr, 1);
			mark_iyesde_dirty(iyesde);
		}
	} else
		ext2_free_data(iyesde, p, q);
}

/* dax_sem must be held when calling this function */
static void __ext2_truncate_blocks(struct iyesde *iyesde, loff_t offset)
{
	__le32 *i_data = EXT2_I(iyesde)->i_data;
	struct ext2_iyesde_info *ei = EXT2_I(iyesde);
	int addr_per_block = EXT2_ADDR_PER_BLOCK(iyesde->i_sb);
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
	__le32 nr = 0;
	int n;
	long iblock;
	unsigned blocksize;
	blocksize = iyesde->i_sb->s_blocksize;
	iblock = (offset + blocksize-1) >> EXT2_BLOCK_SIZE_BITS(iyesde->i_sb);

#ifdef CONFIG_FS_DAX
	WARN_ON(!rwsem_is_locked(&ei->dax_sem));
#endif

	n = ext2_block_to_path(iyesde, iblock, offsets, NULL);
	if (n == 0)
		return;

	/*
	 * From here we block out all ext2_get_block() callers who want to
	 * modify the block allocation tree.
	 */
	mutex_lock(&ei->truncate_mutex);

	if (n == 1) {
		ext2_free_data(iyesde, i_data+offsets[0],
					i_data + EXT2_NDIR_BLOCKS);
		goto do_indirects;
	}

	partial = ext2_find_shared(iyesde, n, offsets, chain, &nr);
	/* Kill the top of shared branch (already detached) */
	if (nr) {
		if (partial == chain)
			mark_iyesde_dirty(iyesde);
		else
			mark_buffer_dirty_iyesde(partial->bh, iyesde);
		ext2_free_branches(iyesde, &nr, &nr+1, (chain+n-1) - partial);
	}
	/* Clear the ends of indirect blocks on the shared branch */
	while (partial > chain) {
		ext2_free_branches(iyesde,
				   partial->p + 1,
				   (__le32*)partial->bh->b_data+addr_per_block,
				   (chain+n-1) - partial);
		mark_buffer_dirty_iyesde(partial->bh, iyesde);
		brelse (partial->bh);
		partial--;
	}
do_indirects:
	/* Kill the remaining (whole) subtrees */
	switch (offsets[0]) {
		default:
			nr = i_data[EXT2_IND_BLOCK];
			if (nr) {
				i_data[EXT2_IND_BLOCK] = 0;
				mark_iyesde_dirty(iyesde);
				ext2_free_branches(iyesde, &nr, &nr+1, 1);
			}
			/* fall through */
		case EXT2_IND_BLOCK:
			nr = i_data[EXT2_DIND_BLOCK];
			if (nr) {
				i_data[EXT2_DIND_BLOCK] = 0;
				mark_iyesde_dirty(iyesde);
				ext2_free_branches(iyesde, &nr, &nr+1, 2);
			}
			/* fall through */
		case EXT2_DIND_BLOCK:
			nr = i_data[EXT2_TIND_BLOCK];
			if (nr) {
				i_data[EXT2_TIND_BLOCK] = 0;
				mark_iyesde_dirty(iyesde);
				ext2_free_branches(iyesde, &nr, &nr+1, 3);
			}
		case EXT2_TIND_BLOCK:
			;
	}

	ext2_discard_reservation(iyesde);

	mutex_unlock(&ei->truncate_mutex);
}

static void ext2_truncate_blocks(struct iyesde *iyesde, loff_t offset)
{
	if (!(S_ISREG(iyesde->i_mode) || S_ISDIR(iyesde->i_mode) ||
	    S_ISLNK(iyesde->i_mode)))
		return;
	if (ext2_iyesde_is_fast_symlink(iyesde))
		return;

	dax_sem_down_write(EXT2_I(iyesde));
	__ext2_truncate_blocks(iyesde, offset);
	dax_sem_up_write(EXT2_I(iyesde));
}

static int ext2_setsize(struct iyesde *iyesde, loff_t newsize)
{
	int error;

	if (!(S_ISREG(iyesde->i_mode) || S_ISDIR(iyesde->i_mode) ||
	    S_ISLNK(iyesde->i_mode)))
		return -EINVAL;
	if (ext2_iyesde_is_fast_symlink(iyesde))
		return -EINVAL;
	if (IS_APPEND(iyesde) || IS_IMMUTABLE(iyesde))
		return -EPERM;

	iyesde_dio_wait(iyesde);

	if (IS_DAX(iyesde)) {
		error = iomap_zero_range(iyesde, newsize,
					 PAGE_ALIGN(newsize) - newsize, NULL,
					 &ext2_iomap_ops);
	} else if (test_opt(iyesde->i_sb, NOBH))
		error = yesbh_truncate_page(iyesde->i_mapping,
				newsize, ext2_get_block);
	else
		error = block_truncate_page(iyesde->i_mapping,
				newsize, ext2_get_block);
	if (error)
		return error;

	dax_sem_down_write(EXT2_I(iyesde));
	truncate_setsize(iyesde, newsize);
	__ext2_truncate_blocks(iyesde, newsize);
	dax_sem_up_write(EXT2_I(iyesde));

	iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	if (iyesde_needs_sync(iyesde)) {
		sync_mapping_buffers(iyesde->i_mapping);
		sync_iyesde_metadata(iyesde, 1);
	} else {
		mark_iyesde_dirty(iyesde);
	}

	return 0;
}

static struct ext2_iyesde *ext2_get_iyesde(struct super_block *sb, iyes_t iyes,
					struct buffer_head **p)
{
	struct buffer_head * bh;
	unsigned long block_group;
	unsigned long block;
	unsigned long offset;
	struct ext2_group_desc * gdp;

	*p = NULL;
	if ((iyes != EXT2_ROOT_INO && iyes < EXT2_FIRST_INO(sb)) ||
	    iyes > le32_to_cpu(EXT2_SB(sb)->s_es->s_iyesdes_count))
		goto Einval;

	block_group = (iyes - 1) / EXT2_INODES_PER_GROUP(sb);
	gdp = ext2_get_group_desc(sb, block_group, NULL);
	if (!gdp)
		goto Egdp;
	/*
	 * Figure out the offset within the block group iyesde table
	 */
	offset = ((iyes - 1) % EXT2_INODES_PER_GROUP(sb)) * EXT2_INODE_SIZE(sb);
	block = le32_to_cpu(gdp->bg_iyesde_table) +
		(offset >> EXT2_BLOCK_SIZE_BITS(sb));
	if (!(bh = sb_bread(sb, block)))
		goto Eio;

	*p = bh;
	offset &= (EXT2_BLOCK_SIZE(sb) - 1);
	return (struct ext2_iyesde *) (bh->b_data + offset);

Einval:
	ext2_error(sb, "ext2_get_iyesde", "bad iyesde number: %lu",
		   (unsigned long) iyes);
	return ERR_PTR(-EINVAL);
Eio:
	ext2_error(sb, "ext2_get_iyesde",
		   "unable to read iyesde block - iyesde=%lu, block=%lu",
		   (unsigned long) iyes, block);
Egdp:
	return ERR_PTR(-EIO);
}

void ext2_set_iyesde_flags(struct iyesde *iyesde)
{
	unsigned int flags = EXT2_I(iyesde)->i_flags;

	iyesde->i_flags &= ~(S_SYNC | S_APPEND | S_IMMUTABLE | S_NOATIME |
				S_DIRSYNC | S_DAX);
	if (flags & EXT2_SYNC_FL)
		iyesde->i_flags |= S_SYNC;
	if (flags & EXT2_APPEND_FL)
		iyesde->i_flags |= S_APPEND;
	if (flags & EXT2_IMMUTABLE_FL)
		iyesde->i_flags |= S_IMMUTABLE;
	if (flags & EXT2_NOATIME_FL)
		iyesde->i_flags |= S_NOATIME;
	if (flags & EXT2_DIRSYNC_FL)
		iyesde->i_flags |= S_DIRSYNC;
	if (test_opt(iyesde->i_sb, DAX) && S_ISREG(iyesde->i_mode))
		iyesde->i_flags |= S_DAX;
}

void ext2_set_file_ops(struct iyesde *iyesde)
{
	iyesde->i_op = &ext2_file_iyesde_operations;
	iyesde->i_fop = &ext2_file_operations;
	if (IS_DAX(iyesde))
		iyesde->i_mapping->a_ops = &ext2_dax_aops;
	else if (test_opt(iyesde->i_sb, NOBH))
		iyesde->i_mapping->a_ops = &ext2_yesbh_aops;
	else
		iyesde->i_mapping->a_ops = &ext2_aops;
}

struct iyesde *ext2_iget (struct super_block *sb, unsigned long iyes)
{
	struct ext2_iyesde_info *ei;
	struct buffer_head * bh = NULL;
	struct ext2_iyesde *raw_iyesde;
	struct iyesde *iyesde;
	long ret = -EIO;
	int n;
	uid_t i_uid;
	gid_t i_gid;

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	ei = EXT2_I(iyesde);
	ei->i_block_alloc_info = NULL;

	raw_iyesde = ext2_get_iyesde(iyesde->i_sb, iyes, &bh);
	if (IS_ERR(raw_iyesde)) {
		ret = PTR_ERR(raw_iyesde);
 		goto bad_iyesde;
	}

	iyesde->i_mode = le16_to_cpu(raw_iyesde->i_mode);
	i_uid = (uid_t)le16_to_cpu(raw_iyesde->i_uid_low);
	i_gid = (gid_t)le16_to_cpu(raw_iyesde->i_gid_low);
	if (!(test_opt (iyesde->i_sb, NO_UID32))) {
		i_uid |= le16_to_cpu(raw_iyesde->i_uid_high) << 16;
		i_gid |= le16_to_cpu(raw_iyesde->i_gid_high) << 16;
	}
	i_uid_write(iyesde, i_uid);
	i_gid_write(iyesde, i_gid);
	set_nlink(iyesde, le16_to_cpu(raw_iyesde->i_links_count));
	iyesde->i_size = le32_to_cpu(raw_iyesde->i_size);
	iyesde->i_atime.tv_sec = (signed)le32_to_cpu(raw_iyesde->i_atime);
	iyesde->i_ctime.tv_sec = (signed)le32_to_cpu(raw_iyesde->i_ctime);
	iyesde->i_mtime.tv_sec = (signed)le32_to_cpu(raw_iyesde->i_mtime);
	iyesde->i_atime.tv_nsec = iyesde->i_mtime.tv_nsec = iyesde->i_ctime.tv_nsec = 0;
	ei->i_dtime = le32_to_cpu(raw_iyesde->i_dtime);
	/* We yesw have eyesugh fields to check if the iyesde was active or yest.
	 * This is needed because nfsd might try to access dead iyesdes
	 * the test is that same one that e2fsck uses
	 * NeilBrown 1999oct15
	 */
	if (iyesde->i_nlink == 0 && (iyesde->i_mode == 0 || ei->i_dtime)) {
		/* this iyesde is deleted */
		ret = -ESTALE;
		goto bad_iyesde;
	}
	iyesde->i_blocks = le32_to_cpu(raw_iyesde->i_blocks);
	ei->i_flags = le32_to_cpu(raw_iyesde->i_flags);
	ext2_set_iyesde_flags(iyesde);
	ei->i_faddr = le32_to_cpu(raw_iyesde->i_faddr);
	ei->i_frag_yes = raw_iyesde->i_frag;
	ei->i_frag_size = raw_iyesde->i_fsize;
	ei->i_file_acl = le32_to_cpu(raw_iyesde->i_file_acl);
	ei->i_dir_acl = 0;

	if (ei->i_file_acl &&
	    !ext2_data_block_valid(EXT2_SB(sb), ei->i_file_acl, 1)) {
		ext2_error(sb, "ext2_iget", "bad extended attribute block %u",
			   ei->i_file_acl);
		ret = -EFSCORRUPTED;
		goto bad_iyesde;
	}

	if (S_ISREG(iyesde->i_mode))
		iyesde->i_size |= ((__u64)le32_to_cpu(raw_iyesde->i_size_high)) << 32;
	else
		ei->i_dir_acl = le32_to_cpu(raw_iyesde->i_dir_acl);
	if (i_size_read(iyesde) < 0) {
		ret = -EFSCORRUPTED;
		goto bad_iyesde;
	}
	ei->i_dtime = 0;
	iyesde->i_generation = le32_to_cpu(raw_iyesde->i_generation);
	ei->i_state = 0;
	ei->i_block_group = (iyes - 1) / EXT2_INODES_PER_GROUP(iyesde->i_sb);
	ei->i_dir_start_lookup = 0;

	/*
	 * NOTE! The in-memory iyesde i_data array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (n = 0; n < EXT2_N_BLOCKS; n++)
		ei->i_data[n] = raw_iyesde->i_block[n];

	if (S_ISREG(iyesde->i_mode)) {
		ext2_set_file_ops(iyesde);
	} else if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_op = &ext2_dir_iyesde_operations;
		iyesde->i_fop = &ext2_dir_operations;
		if (test_opt(iyesde->i_sb, NOBH))
			iyesde->i_mapping->a_ops = &ext2_yesbh_aops;
		else
			iyesde->i_mapping->a_ops = &ext2_aops;
	} else if (S_ISLNK(iyesde->i_mode)) {
		if (ext2_iyesde_is_fast_symlink(iyesde)) {
			iyesde->i_link = (char *)ei->i_data;
			iyesde->i_op = &ext2_fast_symlink_iyesde_operations;
			nd_terminate_link(ei->i_data, iyesde->i_size,
				sizeof(ei->i_data) - 1);
		} else {
			iyesde->i_op = &ext2_symlink_iyesde_operations;
			iyesde_yeshighmem(iyesde);
			if (test_opt(iyesde->i_sb, NOBH))
				iyesde->i_mapping->a_ops = &ext2_yesbh_aops;
			else
				iyesde->i_mapping->a_ops = &ext2_aops;
		}
	} else {
		iyesde->i_op = &ext2_special_iyesde_operations;
		if (raw_iyesde->i_block[0])
			init_special_iyesde(iyesde, iyesde->i_mode,
			   old_decode_dev(le32_to_cpu(raw_iyesde->i_block[0])));
		else 
			init_special_iyesde(iyesde, iyesde->i_mode,
			   new_decode_dev(le32_to_cpu(raw_iyesde->i_block[1])));
	}
	brelse (bh);
	unlock_new_iyesde(iyesde);
	return iyesde;
	
bad_iyesde:
	brelse(bh);
	iget_failed(iyesde);
	return ERR_PTR(ret);
}

static int __ext2_write_iyesde(struct iyesde *iyesde, int do_sync)
{
	struct ext2_iyesde_info *ei = EXT2_I(iyesde);
	struct super_block *sb = iyesde->i_sb;
	iyes_t iyes = iyesde->i_iyes;
	uid_t uid = i_uid_read(iyesde);
	gid_t gid = i_gid_read(iyesde);
	struct buffer_head * bh;
	struct ext2_iyesde * raw_iyesde = ext2_get_iyesde(sb, iyes, &bh);
	int n;
	int err = 0;

	if (IS_ERR(raw_iyesde))
 		return -EIO;

	/* For fields yest yest tracking in the in-memory iyesde,
	 * initialise them to zero for new iyesdes. */
	if (ei->i_state & EXT2_STATE_NEW)
		memset(raw_iyesde, 0, EXT2_SB(sb)->s_iyesde_size);

	raw_iyesde->i_mode = cpu_to_le16(iyesde->i_mode);
	if (!(test_opt(sb, NO_UID32))) {
		raw_iyesde->i_uid_low = cpu_to_le16(low_16_bits(uid));
		raw_iyesde->i_gid_low = cpu_to_le16(low_16_bits(gid));
/*
 * Fix up interoperability with old kernels. Otherwise, old iyesdes get
 * re-used with the upper 16 bits of the uid/gid intact
 */
		if (!ei->i_dtime) {
			raw_iyesde->i_uid_high = cpu_to_le16(high_16_bits(uid));
			raw_iyesde->i_gid_high = cpu_to_le16(high_16_bits(gid));
		} else {
			raw_iyesde->i_uid_high = 0;
			raw_iyesde->i_gid_high = 0;
		}
	} else {
		raw_iyesde->i_uid_low = cpu_to_le16(fs_high2lowuid(uid));
		raw_iyesde->i_gid_low = cpu_to_le16(fs_high2lowgid(gid));
		raw_iyesde->i_uid_high = 0;
		raw_iyesde->i_gid_high = 0;
	}
	raw_iyesde->i_links_count = cpu_to_le16(iyesde->i_nlink);
	raw_iyesde->i_size = cpu_to_le32(iyesde->i_size);
	raw_iyesde->i_atime = cpu_to_le32(iyesde->i_atime.tv_sec);
	raw_iyesde->i_ctime = cpu_to_le32(iyesde->i_ctime.tv_sec);
	raw_iyesde->i_mtime = cpu_to_le32(iyesde->i_mtime.tv_sec);

	raw_iyesde->i_blocks = cpu_to_le32(iyesde->i_blocks);
	raw_iyesde->i_dtime = cpu_to_le32(ei->i_dtime);
	raw_iyesde->i_flags = cpu_to_le32(ei->i_flags);
	raw_iyesde->i_faddr = cpu_to_le32(ei->i_faddr);
	raw_iyesde->i_frag = ei->i_frag_yes;
	raw_iyesde->i_fsize = ei->i_frag_size;
	raw_iyesde->i_file_acl = cpu_to_le32(ei->i_file_acl);
	if (!S_ISREG(iyesde->i_mode))
		raw_iyesde->i_dir_acl = cpu_to_le32(ei->i_dir_acl);
	else {
		raw_iyesde->i_size_high = cpu_to_le32(iyesde->i_size >> 32);
		if (iyesde->i_size > 0x7fffffffULL) {
			if (!EXT2_HAS_RO_COMPAT_FEATURE(sb,
					EXT2_FEATURE_RO_COMPAT_LARGE_FILE) ||
			    EXT2_SB(sb)->s_es->s_rev_level ==
					cpu_to_le32(EXT2_GOOD_OLD_REV)) {
			       /* If this is the first large file
				* created, add a flag to the superblock.
				*/
				spin_lock(&EXT2_SB(sb)->s_lock);
				ext2_update_dynamic_rev(sb);
				EXT2_SET_RO_COMPAT_FEATURE(sb,
					EXT2_FEATURE_RO_COMPAT_LARGE_FILE);
				spin_unlock(&EXT2_SB(sb)->s_lock);
				ext2_sync_super(sb, EXT2_SB(sb)->s_es, 1);
			}
		}
	}
	
	raw_iyesde->i_generation = cpu_to_le32(iyesde->i_generation);
	if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode)) {
		if (old_valid_dev(iyesde->i_rdev)) {
			raw_iyesde->i_block[0] =
				cpu_to_le32(old_encode_dev(iyesde->i_rdev));
			raw_iyesde->i_block[1] = 0;
		} else {
			raw_iyesde->i_block[0] = 0;
			raw_iyesde->i_block[1] =
				cpu_to_le32(new_encode_dev(iyesde->i_rdev));
			raw_iyesde->i_block[2] = 0;
		}
	} else for (n = 0; n < EXT2_N_BLOCKS; n++)
		raw_iyesde->i_block[n] = ei->i_data[n];
	mark_buffer_dirty(bh);
	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk ("IO error syncing ext2 iyesde [%s:%08lx]\n",
				sb->s_id, (unsigned long) iyes);
			err = -EIO;
		}
	}
	ei->i_state &= ~EXT2_STATE_NEW;
	brelse (bh);
	return err;
}

int ext2_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	return __ext2_write_iyesde(iyesde, wbc->sync_mode == WB_SYNC_ALL);
}

int ext2_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int query_flags)
{
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct ext2_iyesde_info *ei = EXT2_I(iyesde);
	unsigned int flags;

	flags = ei->i_flags & EXT2_FL_USER_VISIBLE;
	if (flags & EXT2_APPEND_FL)
		stat->attributes |= STATX_ATTR_APPEND;
	if (flags & EXT2_COMPR_FL)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (flags & EXT2_IMMUTABLE_FL)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (flags & EXT2_NODUMP_FL)
		stat->attributes |= STATX_ATTR_NODUMP;
	stat->attributes_mask |= (STATX_ATTR_APPEND |
			STATX_ATTR_COMPRESSED |
			STATX_ATTR_ENCRYPTED |
			STATX_ATTR_IMMUTABLE |
			STATX_ATTR_NODUMP);

	generic_fillattr(iyesde, stat);
	return 0;
}

int ext2_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int error;

	error = setattr_prepare(dentry, iattr);
	if (error)
		return error;

	if (is_quota_modification(iyesde, iattr)) {
		error = dquot_initialize(iyesde);
		if (error)
			return error;
	}
	if ((iattr->ia_valid & ATTR_UID && !uid_eq(iattr->ia_uid, iyesde->i_uid)) ||
	    (iattr->ia_valid & ATTR_GID && !gid_eq(iattr->ia_gid, iyesde->i_gid))) {
		error = dquot_transfer(iyesde, iattr);
		if (error)
			return error;
	}
	if (iattr->ia_valid & ATTR_SIZE && iattr->ia_size != iyesde->i_size) {
		error = ext2_setsize(iyesde, iattr->ia_size);
		if (error)
			return error;
	}
	setattr_copy(iyesde, iattr);
	if (iattr->ia_valid & ATTR_MODE)
		error = posix_acl_chmod(iyesde, iyesde->i_mode);
	mark_iyesde_dirty(iyesde);

	return error;
}
