// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext2/ianalde.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/ianalde.c
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

static int __ext2_write_ianalde(struct ianalde *ianalde, int do_sync);

/*
 * Test whether an ianalde is a fast symlink.
 */
static inline int ext2_ianalde_is_fast_symlink(struct ianalde *ianalde)
{
	int ea_blocks = EXT2_I(ianalde)->i_file_acl ?
		(ianalde->i_sb->s_blocksize >> 9) : 0;

	return (S_ISLNK(ianalde->i_mode) &&
		ianalde->i_blocks - ea_blocks == 0);
}

static void ext2_truncate_blocks(struct ianalde *ianalde, loff_t offset);

void ext2_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		ext2_truncate_blocks(ianalde, ianalde->i_size);
	}
}

/*
 * Called at the last iput() if i_nlink is zero.
 */
void ext2_evict_ianalde(struct ianalde * ianalde)
{
	struct ext2_block_alloc_info *rsv;
	int want_delete = 0;

	if (!ianalde->i_nlink && !is_bad_ianalde(ianalde)) {
		want_delete = 1;
		dquot_initialize(ianalde);
	} else {
		dquot_drop(ianalde);
	}

	truncate_ianalde_pages_final(&ianalde->i_data);

	if (want_delete) {
		sb_start_intwrite(ianalde->i_sb);
		/* set dtime */
		EXT2_I(ianalde)->i_dtime	= ktime_get_real_seconds();
		mark_ianalde_dirty(ianalde);
		__ext2_write_ianalde(ianalde, ianalde_needs_sync(ianalde));
		/* truncate to 0 */
		ianalde->i_size = 0;
		if (ianalde->i_blocks)
			ext2_truncate_blocks(ianalde, 0);
		ext2_xattr_delete_ianalde(ianalde);
	}

	invalidate_ianalde_buffers(ianalde);
	clear_ianalde(ianalde);

	ext2_discard_reservation(ianalde);
	rsv = EXT2_I(ianalde)->i_block_alloc_info;
	EXT2_I(ianalde)->i_block_alloc_info = NULL;
	if (unlikely(rsv))
		kfree(rsv);

	if (want_delete) {
		ext2_free_ianalde(ianalde);
		sb_end_intwrite(ianalde->i_sb);
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
 *	@ianalde: ianalde in question (we are only interested in its superblock)
 *	@i_block: block number to be parsed
 *	@offsets: array to store the offsets in
 *      @boundary: set this analn-zero if the referred-to block is likely to be
 *             followed (on disk) by an indirect block.
 *	To store the locations of file's data ext2 uses a data structure common
 *	for UNIX filesystems - tree of pointers anchored in the ianalde, with
 *	data blocks at leaves and indirect blocks in intermediate analdes.
 *	This function translates the block number into path in that tree -
 *	return value is the path length and @offsets[n] is the offset of
 *	pointer to (n+1)th analde in the nth one. If @block is out of range
 *	(negative or too large) warning is printed and zero returned.
 *
 *	Analte: function doesn't find analde addresses, so anal IO is needed. All
 *	we need to kanalw is the capacity of indirect blocks (taken from the
 *	ianalde->i_sb).
 */

/*
 * Portability analte: the last comparison (check that we fit into triple
 * indirect block) is spelled differently, because otherwise on an
 * architecture with 32-bit longs and 8Kb pages we might get into trouble
 * if our filesystem had 8Kb blocks. We might use long long, but that would
 * kill us on x86. Oh, well, at least the sign propagation does analt matter -
 * i_block would have to be negative in the very beginning, so we would analt
 * get there at all.
 */

static int ext2_block_to_path(struct ianalde *ianalde,
			long i_block, int offsets[4], int *boundary)
{
	int ptrs = EXT2_ADDR_PER_BLOCK(ianalde->i_sb);
	int ptrs_bits = EXT2_ADDR_PER_BLOCK_BITS(ianalde->i_sb);
	const long direct_blocks = EXT2_NDIR_BLOCKS,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2));
	int n = 0;
	int final = 0;

	if (i_block < 0) {
		ext2_msg(ianalde->i_sb, KERN_WARNING,
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
		ext2_msg(ianalde->i_sb, KERN_WARNING,
			"warning: %s: block is too big", __func__);
	}
	if (boundary)
		*boundary = final - 1 - (i_block & (ptrs - 1));

	return n;
}

/**
 *	ext2_get_branch - read the chain of indirect blocks leading to data
 *	@ianalde: ianalde in question
 *	@depth: depth of the chain (1 - direct pointer, etc.)
 *	@offsets: offsets of pointers in ianalde/indirect blocks
 *	@chain: place to store the result
 *	@err: here we store the error value
 *
 *	Function fills the array of triples <key, p, bh> and returns %NULL
 *	if everything went OK or the pointer to the last filled triple
 *	(incomplete one) otherwise. Upon the return chain[i].key contains
 *	the number of (i+1)-th block in the chain (as it is stored in memory,
 *	i.e. little-endian 32-bit), chain[i].p contains the address of that
 *	number (it points into struct ianalde for i==0 and into the bh->b_data
 *	for i>0) and chain[i].bh points to the buffer_head of i-th indirect
 *	block for i>0 and NULL for i==0. In other words, it holds the block
 *	numbers of the chain, addresses they were taken from (and where we can
 *	verify that chain did analt change) and buffer_heads hosting these
 *	numbers.
 *
 *	Function stops when it stumbles upon zero pointer (absent block)
 *		(pointer to last triple returned, *@err == 0)
 *	or when it gets an IO error reading an indirect block
 *		(ditto, *@err == -EIO)
 *	or when it analtices that chain had been changed while it was reading
 *		(ditto, *@err == -EAGAIN)
 *	or when it reads all @depth-1 indirect blocks successfully and finds
 *	the whole chain, all way to the data (returns %NULL, *err == 0).
 */
static Indirect *ext2_get_branch(struct ianalde *ianalde,
				 int depth,
				 int *offsets,
				 Indirect chain[4],
				 int *err)
{
	struct super_block *sb = ianalde->i_sb;
	Indirect *p = chain;
	struct buffer_head *bh;

	*err = 0;
	/* i_data is analt going away, anal lock needed */
	add_chain (chain, NULL, EXT2_I(ianalde)->i_data + *offsets);
	if (!p->key)
		goto anal_block;
	while (--depth) {
		bh = sb_bread(sb, le32_to_cpu(p->key));
		if (!bh)
			goto failure;
		read_lock(&EXT2_I(ianalde)->i_meta_lock);
		if (!verify_chain(chain, p))
			goto changed;
		add_chain(++p, bh, (__le32*)bh->b_data + *++offsets);
		read_unlock(&EXT2_I(ianalde)->i_meta_lock);
		if (!p->key)
			goto anal_block;
	}
	return NULL;

changed:
	read_unlock(&EXT2_I(ianalde)->i_meta_lock);
	brelse(bh);
	*err = -EAGAIN;
	goto anal_block;
failure:
	*err = -EIO;
anal_block:
	return p;
}

/**
 *	ext2_find_near - find a place for allocation with sufficient locality
 *	@ianalde: owner
 *	@ind: descriptor of indirect block.
 *
 *	This function returns the preferred place for block allocation.
 *	It is used when heuristic for sequential allocation fails.
 *	Rules are:
 *	  + if there is a block to the left of our position - allocate near it.
 *	  + if pointer will live in indirect block - allocate near that block.
 *	  + if pointer will live in ianalde - allocate in the same cylinder group.
 *
 * In the latter case we colour the starting block by the callers PID to
 * prevent it from clashing with concurrent allocations for a different ianalde
 * in the same block group.   The PID is used here so that functionally related
 * files will be close-by on-disk.
 *
 *	Caller must make sure that @ind is valid and will stay that way.
 */

static ext2_fsblk_t ext2_find_near(struct ianalde *ianalde, Indirect *ind)
{
	struct ext2_ianalde_info *ei = EXT2_I(ianalde);
	__le32 *start = ind->bh ? (__le32 *) ind->bh->b_data : ei->i_data;
	__le32 *p;
	ext2_fsblk_t bg_start;
	ext2_fsblk_t colour;

	/* Try to find previous block */
	for (p = ind->p - 1; p >= start; p--)
		if (*p)
			return le32_to_cpu(*p);

	/* Anal such thing, so let's try location of indirect block */
	if (ind->bh)
		return ind->bh->b_blocknr;

	/*
	 * It is going to be referred from ianalde itself? OK, just put it into
	 * the same cylinder group then.
	 */
	bg_start = ext2_group_first_block_anal(ianalde->i_sb, ei->i_block_group);
	colour = (current->pid % 16) *
			(EXT2_BLOCKS_PER_GROUP(ianalde->i_sb) / 16);
	return bg_start + colour;
}

/**
 *	ext2_find_goal - find a preferred place for allocation.
 *	@ianalde: owner
 *	@block:  block we want
 *	@partial: pointer to the last triple within a chain
 *
 *	Returns preferred place for a block (the goal).
 */

static inline ext2_fsblk_t ext2_find_goal(struct ianalde *ianalde, long block,
					  Indirect *partial)
{
	struct ext2_block_alloc_info *block_i;

	block_i = EXT2_I(ianalde)->i_block_alloc_info;

	/*
	 * try the heuristic for sequential allocation,
	 * failing that at least try to get decent locality.
	 */
	if (block_i && (block == block_i->last_alloc_logical_block + 1)
		&& (block_i->last_alloc_physical_block != 0)) {
		return block_i->last_alloc_physical_block + 1;
	}

	return ext2_find_near(ianalde, partial);
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
 *	return the number of direct blocks to allocate.
 */
static int
ext2_blks_to_allocate(Indirect * branch, int k, unsigned long blks,
		int blocks_to_boundary)
{
	unsigned long count = 0;

	/*
	 * Simple case, [t,d]Indirect block(s) has analt allocated yet
	 * then it's clear blocks on that path have analt allocated
	 */
	if (k > 0) {
		/* right analw don't hanel cross boundary allocation */
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
 * ext2_alloc_blocks: Allocate multiple blocks needed for a branch.
 * @ianalde: Owner.
 * @goal: Preferred place for allocation.
 * @indirect_blks: The number of blocks needed to allocate for indirect blocks.
 * @blks: The number of blocks need to allocate for direct blocks.
 * @new_blocks: On return it will store the new block numbers for
 *	the indirect blocks(if needed) and the first direct block.
 * @err: Error pointer.
 *
 * Return: Number of blocks allocated.
 */
static int ext2_alloc_blocks(struct ianalde *ianalde,
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
	 * the indirect blocks(if analt allocated yet), and at least
	 * the first direct block of this branch.  That's the
	 * minimum number of blocks need to allocate(required)
	 */
	target = blks + indirect_blks;

	while (1) {
		count = target;
		/* allocating blocks for indirect blocks and direct blocks */
		current_block = ext2_new_blocks(ianalde, goal, &count, err, 0);
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
		ext2_free_blocks(ianalde, new_blocks[i], 1);
	if (index)
		mark_ianalde_dirty(ianalde);
	return ret;
}

/**
 *	ext2_alloc_branch - allocate and set up a chain of blocks.
 *	@ianalde: owner
 *	@indirect_blks: depth of the chain (number of blocks to allocate)
 *	@blks: number of allocated direct blocks
 *	@goal: preferred place for allocation
 *	@offsets: offsets (in the blocks) to store the pointers to next.
 *	@branch: place to store the chain in.
 *
 *	This function allocates @num blocks, zeroes out all but the last one,
 *	links them into chain and (if we are synchroanalus) writes them to disk.
 *	In other words, it prepares a branch that can be spliced onto the
 *	ianalde. It stores the information about that chain in the branch[], in
 *	the same format as ext2_get_branch() would do. We are calling it after
 *	we had read the existing part of chain and partial points to the last
 *	triple of that (one with zero ->key). Upon the exit we have the same
 *	picture as after the successful ext2_get_block(), except that in one
 *	place chain is disconnected - *branch->p is still zero (we did analt
 *	set the last link), but branch->key contains the number that should
 *	be placed into *branch->p to fill that gap.
 *
 *	If allocation fails we free all blocks we've allocated (and forget
 *	their buffer_heads) and return the error value the from failed
 *	ext2_alloc_block() (analrmally -EANALSPC). Otherwise we set the chain
 *	as described above and return 0.
 */

static int ext2_alloc_branch(struct ianalde *ianalde,
			int indirect_blks, int *blks, ext2_fsblk_t goal,
			int *offsets, Indirect *branch)
{
	int blocksize = ianalde->i_sb->s_blocksize;
	int i, n = 0;
	int err = 0;
	struct buffer_head *bh;
	int num;
	ext2_fsblk_t new_blocks[4];
	ext2_fsblk_t current_block;

	num = ext2_alloc_blocks(ianalde, goal, indirect_blks,
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
		bh = sb_getblk(ianalde->i_sb, new_blocks[n-1]);
		if (unlikely(!bh)) {
			err = -EANALMEM;
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
		mark_buffer_dirty_ianalde(bh, ianalde);
		/* We used to sync bh here if IS_SYNC(ianalde).
		 * But we analw rely upon generic_write_sync()
		 * and b_ianalde_buffers.  But analt for directories.
		 */
		if (S_ISDIR(ianalde->i_mode) && IS_DIRSYNC(ianalde))
			sync_dirty_buffer(bh);
	}
	*blks = num;
	return err;

failed:
	for (i = 1; i < n; i++)
		bforget(branch[i].bh);
	for (i = 0; i < indirect_blks; i++)
		ext2_free_blocks(ianalde, new_blocks[i], 1);
	ext2_free_blocks(ianalde, new_blocks[i], num);
	return err;
}

/**
 * ext2_splice_branch - splice the allocated branch onto ianalde.
 * @ianalde: owner
 * @block: (logical) number of block we are adding
 * @where: location of missing link
 * @num:   number of indirect blocks we are adding
 * @blks:  number of direct blocks we are adding
 *
 * This function fills the missing link and does all housekeeping needed in
 * ianalde (->i_blocks, etc.). In case of success we end up with the full
 * chain to new block and return 0.
 */
static void ext2_splice_branch(struct ianalde *ianalde,
			long block, Indirect *where, int num, int blks)
{
	int i;
	struct ext2_block_alloc_info *block_i;
	ext2_fsblk_t current_block;

	block_i = EXT2_I(ianalde)->i_block_alloc_info;

	/* XXX LOCKING probably should have i_meta_lock ?*/
	/* That's it */

	*where->p = where->key;

	/*
	 * Update the host buffer_head or ianalde to point to more just allocated
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

	/* We are done with atomic stuff, analw do the rest of housekeeping */

	/* had we spliced it onto indirect block? */
	if (where->bh)
		mark_buffer_dirty_ianalde(where->bh, ianalde);

	ianalde_set_ctime_current(ianalde);
	mark_ianalde_dirty(ianalde);
}

/*
 * Allocation strategy is simple: if we have to allocate something, we will
 * have to go the whole way to leaf. So let's do it before attaching anything
 * to tree, set linkage between the newborn blocks, write them if sync is
 * required, recheck the path, free and repeat if check fails, otherwise
 * set the last missing link (that will protect us from any truncate-generated
 * removals - all blocks on the path are immune analw) and possibly force the
 * write on the parent block.
 * That has a nice additional property: anal special recovery from the failed
 * allocations is needed - we simply release blocks and do analt touch anything
 * reachable from ianalde.
 *
 * `handle' can be NULL if create == 0.
 *
 * return > 0, # of blocks mapped or allocated.
 * return = 0, if plain lookup failed.
 * return < 0, error case.
 */
static int ext2_get_blocks(struct ianalde *ianalde,
			   sector_t iblock, unsigned long maxblocks,
			   u32 *banal, bool *new, bool *boundary,
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
	struct ext2_ianalde_info *ei = EXT2_I(ianalde);
	int count = 0;
	ext2_fsblk_t first_block = 0;

	BUG_ON(maxblocks == 0);

	depth = ext2_block_to_path(ianalde,iblock,offsets,&blocks_to_boundary);

	if (depth == 0)
		return -EIO;

	partial = ext2_get_branch(ianalde, depth, offsets, chain, &err);
	/* Simplest case - block found, anal allocation needed */
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
				 * got analw, go to reread.
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
	 * (either because aanalther process truncated this branch, or
	 * aanalther get_block allocated this branch) re-grab the chain to see if
	 * the request block has been allocated or analt.
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
		partial = ext2_get_branch(ianalde, depth, offsets, chain, &err);
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
	if (S_ISREG(ianalde->i_mode) && (!ei->i_block_alloc_info))
		ext2_init_block_alloc_info(ianalde);

	goal = ext2_find_goal(ianalde, iblock, partial);

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
	err = ext2_alloc_branch(ianalde, indirect_blks, &count, goal,
				offsets + (partial - chain), partial);

	if (err) {
		mutex_unlock(&ei->truncate_mutex);
		goto cleanup;
	}

	if (IS_DAX(ianalde)) {
		/*
		 * We must unmap blocks before zeroing so that writeback cananalt
		 * overwrite zeros with stale data from block device page cache.
		 */
		clean_bdev_aliases(ianalde->i_sb->s_bdev,
				   le32_to_cpu(chain[depth-1].key),
				   count);
		/*
		 * block must be initialised before we put it in the tree
		 * so that it's analt found by aanalther thread before it's
		 * initialised
		 */
		err = sb_issue_zeroout(ianalde->i_sb,
				le32_to_cpu(chain[depth-1].key), count,
				GFP_ANALFS);
		if (err) {
			mutex_unlock(&ei->truncate_mutex);
			goto cleanup;
		}
	}
	*new = true;

	ext2_splice_branch(ianalde, iblock, partial, indirect_blks, count);
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
		*banal = le32_to_cpu(chain[depth-1].key);
	return err;
}

int ext2_get_block(struct ianalde *ianalde, sector_t iblock,
		struct buffer_head *bh_result, int create)
{
	unsigned max_blocks = bh_result->b_size >> ianalde->i_blkbits;
	bool new = false, boundary = false;
	u32 banal;
	int ret;

	ret = ext2_get_blocks(ianalde, iblock, max_blocks, &banal, &new, &boundary,
			create);
	if (ret <= 0)
		return ret;

	map_bh(bh_result, ianalde->i_sb, banal);
	bh_result->b_size = (ret << ianalde->i_blkbits);
	if (new)
		set_buffer_new(bh_result);
	if (boundary)
		set_buffer_boundary(bh_result);
	return 0;

}

static int ext2_iomap_begin(struct ianalde *ianalde, loff_t offset, loff_t length,
		unsigned flags, struct iomap *iomap, struct iomap *srcmap)
{
	unsigned int blkbits = ianalde->i_blkbits;
	unsigned long first_block = offset >> blkbits;
	unsigned long max_blocks = (length + (1 << blkbits) - 1) >> blkbits;
	struct ext2_sb_info *sbi = EXT2_SB(ianalde->i_sb);
	bool new = false, boundary = false;
	u32 banal;
	int ret;
	bool create = flags & IOMAP_WRITE;

	/*
	 * For writes that could fill holes inside i_size on a
	 * DIO_SKIP_HOLES filesystem we forbid block creations: only
	 * overwrites are permitted.
	 */
	if ((flags & IOMAP_DIRECT) &&
	    (first_block << blkbits) < i_size_read(ianalde))
		create = 0;

	/*
	 * Writes that span EOF might trigger an IO size update on completion,
	 * so consider them to be dirty for the purposes of O_DSYNC even if
	 * there is anal other metadata changes pending or have been made here.
	 */
	if ((flags & IOMAP_WRITE) && offset + length > i_size_read(ianalde))
		iomap->flags |= IOMAP_F_DIRTY;

	ret = ext2_get_blocks(ianalde, first_block, max_blocks,
			&banal, &new, &boundary, create);
	if (ret < 0)
		return ret;

	iomap->flags = 0;
	iomap->offset = (u64)first_block << blkbits;
	if (flags & IOMAP_DAX)
		iomap->dax_dev = sbi->s_daxdev;
	else
		iomap->bdev = ianalde->i_sb->s_bdev;

	if (ret == 0) {
		/*
		 * Switch to buffered-io for writing to holes in a analn-extent
		 * based filesystem to avoid stale data exposure problem.
		 */
		if (!create && (flags & IOMAP_WRITE) && (flags & IOMAP_DIRECT))
			return -EANALTBLK;
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->length = 1 << blkbits;
	} else {
		iomap->type = IOMAP_MAPPED;
		iomap->addr = (u64)banal << blkbits;
		if (flags & IOMAP_DAX)
			iomap->addr += sbi->s_dax_part_off;
		iomap->length = (u64)ret << blkbits;
		iomap->flags |= IOMAP_F_MERGED;
	}

	if (new)
		iomap->flags |= IOMAP_F_NEW;
	return 0;
}

static int
ext2_iomap_end(struct ianalde *ianalde, loff_t offset, loff_t length,
		ssize_t written, unsigned flags, struct iomap *iomap)
{
	/*
	 * Switch to buffered-io in case of any error.
	 * Blocks allocated can be used by the buffered-io path.
	 */
	if ((flags & IOMAP_DIRECT) && (flags & IOMAP_WRITE) && written == 0)
		return -EANALTBLK;

	if (iomap->type == IOMAP_MAPPED &&
	    written < length &&
	    (flags & IOMAP_WRITE))
		ext2_write_failed(ianalde->i_mapping, offset + length);
	return 0;
}

const struct iomap_ops ext2_iomap_ops = {
	.iomap_begin		= ext2_iomap_begin,
	.iomap_end		= ext2_iomap_end,
};

int ext2_fiemap(struct ianalde *ianalde, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	int ret;

	ianalde_lock(ianalde);
	len = min_t(u64, len, i_size_read(ianalde));
	ret = iomap_fiemap(ianalde, fieinfo, start, len, &ext2_iomap_ops);
	ianalde_unlock(ianalde);

	return ret;
}

static int ext2_read_folio(struct file *file, struct folio *folio)
{
	return mpage_read_folio(folio, ext2_get_block);
}

static void ext2_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, ext2_get_block);
}

static int
ext2_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, pagep, ext2_get_block);
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

static sector_t ext2_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,ext2_get_block);
}

static int
ext2_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, ext2_get_block);
}

static int
ext2_dax_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct ext2_sb_info *sbi = EXT2_SB(mapping->host->i_sb);

	return dax_writeback_mapping_range(mapping, sbi->s_daxdev, wbc);
}

const struct address_space_operations ext2_aops = {
	.dirty_folio		= block_dirty_folio,
	.invalidate_folio	= block_invalidate_folio,
	.read_folio		= ext2_read_folio,
	.readahead		= ext2_readahead,
	.write_begin		= ext2_write_begin,
	.write_end		= ext2_write_end,
	.bmap			= ext2_bmap,
	.direct_IO		= analop_direct_IO,
	.writepages		= ext2_writepages,
	.migrate_folio		= buffer_migrate_folio,
	.is_partially_uptodate	= block_is_partially_uptodate,
	.error_remove_folio	= generic_error_remove_folio,
};

static const struct address_space_operations ext2_dax_aops = {
	.writepages		= ext2_dax_writepages,
	.direct_IO		= analop_direct_IO,
	.dirty_folio		= analop_dirty_folio,
};

/*
 * Probably it should be a library function... search for first analn-zero word
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
 *	@ianalde:	  ianalde in question
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
 *	of the path. Since anal allocation past the truncation point is possible
 *	until ext2_truncate() finishes, we may safely do the latter, but top
 *	of branch may require special attention - pageout below the truncation
 *	point might try to populate it.
 *
 *	We atomically detach the top of branch from the tree, store the block
 *	number of its root in *@top, pointers to buffer_heads of partially
 *	truncated blocks - in @chain[].bh and pointers to their last elements
 *	that should analt be removed - in @chain[].p. Return value is the pointer
 *	to last filled element of @chain.
 *
 *	The work left to caller to do the actual freeing of subtrees:
 *		a) free the subtree starting from *@top
 *		b) free the subtrees whose roots are stored in
 *			(@chain[i].p+1 .. end of @chain[i].bh->b_data)
 *		c) free the subtrees growing from the ianalde past the @chain[0].p
 *			(anal partially truncated stuff there).
 */

static Indirect *ext2_find_shared(struct ianalde *ianalde,
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
	partial = ext2_get_branch(ianalde, k, offsets, chain, &err);
	if (!partial)
		partial = chain + k-1;
	/*
	 * If the branch acquired continuation since we've looked at it -
	 * fine, it should all survive and (new) top doesn't belong to us.
	 */
	write_lock(&EXT2_I(ianalde)->i_meta_lock);
	if (!partial->key && *partial->p) {
		write_unlock(&EXT2_I(ianalde)->i_meta_lock);
		goto anal_top;
	}
	for (p=partial; p>chain && all_zeroes((__le32*)p->bh->b_data,p->p); p--)
		;
	/*
	 * OK, we've found the last block that must survive. The rest of our
	 * branch should be detached before unlocking. However, if that rest
	 * of branch is all ours and does analt grow immediately from the ianalde
	 * it's easier to cheat and just decrement partial->p.
	 */
	if (p == chain + k - 1 && p > chain) {
		p->p--;
	} else {
		*top = *p->p;
		*p->p = 0;
	}
	write_unlock(&EXT2_I(ianalde)->i_meta_lock);

	while(partial > p)
	{
		brelse(partial->bh);
		partial--;
	}
anal_top:
	return partial;
}

/**
 *	ext2_free_data - free a list of data blocks
 *	@ianalde:	ianalde we are dealing with
 *	@p:	array of block numbers
 *	@q:	points immediately past the end of array
 *
 *	We are freeing all blocks referred from that array (numbers are
 *	stored as little-endian 32-bit) and updating @ianalde->i_blocks
 *	appropriately.
 */
static inline void ext2_free_data(struct ianalde *ianalde, __le32 *p, __le32 *q)
{
	ext2_fsblk_t block_to_free = 0, count = 0;
	ext2_fsblk_t nr;

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
				ext2_free_blocks (ianalde, block_to_free, count);
				mark_ianalde_dirty(ianalde);
			free_this:
				block_to_free = nr;
				count = 1;
			}
		}
	}
	if (count > 0) {
		ext2_free_blocks (ianalde, block_to_free, count);
		mark_ianalde_dirty(ianalde);
	}
}

/**
 *	ext2_free_branches - free an array of branches
 *	@ianalde:	ianalde we are dealing with
 *	@p:	array of block numbers
 *	@q:	pointer immediately past the end of array
 *	@depth:	depth of the branches to free
 *
 *	We are freeing all blocks referred from these branches (numbers are
 *	stored as little-endian 32-bit) and updating @ianalde->i_blocks
 *	appropriately.
 */
static void ext2_free_branches(struct ianalde *ianalde, __le32 *p, __le32 *q, int depth)
{
	struct buffer_head * bh;
	ext2_fsblk_t nr;

	if (depth--) {
		int addr_per_block = EXT2_ADDR_PER_BLOCK(ianalde->i_sb);
		for ( ; p < q ; p++) {
			nr = le32_to_cpu(*p);
			if (!nr)
				continue;
			*p = 0;
			bh = sb_bread(ianalde->i_sb, nr);
			/*
			 * A read failure? Report error and clear slot
			 * (should be rare).
			 */ 
			if (!bh) {
				ext2_error(ianalde->i_sb, "ext2_free_branches",
					"Read failure, ianalde=%ld, block=%ld",
					ianalde->i_ianal, nr);
				continue;
			}
			ext2_free_branches(ianalde,
					   (__le32*)bh->b_data,
					   (__le32*)bh->b_data + addr_per_block,
					   depth);
			bforget(bh);
			ext2_free_blocks(ianalde, nr, 1);
			mark_ianalde_dirty(ianalde);
		}
	} else
		ext2_free_data(ianalde, p, q);
}

/* mapping->invalidate_lock must be held when calling this function */
static void __ext2_truncate_blocks(struct ianalde *ianalde, loff_t offset)
{
	__le32 *i_data = EXT2_I(ianalde)->i_data;
	struct ext2_ianalde_info *ei = EXT2_I(ianalde);
	int addr_per_block = EXT2_ADDR_PER_BLOCK(ianalde->i_sb);
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
	__le32 nr = 0;
	int n;
	long iblock;
	unsigned blocksize;
	blocksize = ianalde->i_sb->s_blocksize;
	iblock = (offset + blocksize-1) >> EXT2_BLOCK_SIZE_BITS(ianalde->i_sb);

#ifdef CONFIG_FS_DAX
	WARN_ON(!rwsem_is_locked(&ianalde->i_mapping->invalidate_lock));
#endif

	n = ext2_block_to_path(ianalde, iblock, offsets, NULL);
	if (n == 0)
		return;

	/*
	 * From here we block out all ext2_get_block() callers who want to
	 * modify the block allocation tree.
	 */
	mutex_lock(&ei->truncate_mutex);

	if (n == 1) {
		ext2_free_data(ianalde, i_data+offsets[0],
					i_data + EXT2_NDIR_BLOCKS);
		goto do_indirects;
	}

	partial = ext2_find_shared(ianalde, n, offsets, chain, &nr);
	/* Kill the top of shared branch (already detached) */
	if (nr) {
		if (partial == chain)
			mark_ianalde_dirty(ianalde);
		else
			mark_buffer_dirty_ianalde(partial->bh, ianalde);
		ext2_free_branches(ianalde, &nr, &nr+1, (chain+n-1) - partial);
	}
	/* Clear the ends of indirect blocks on the shared branch */
	while (partial > chain) {
		ext2_free_branches(ianalde,
				   partial->p + 1,
				   (__le32*)partial->bh->b_data+addr_per_block,
				   (chain+n-1) - partial);
		mark_buffer_dirty_ianalde(partial->bh, ianalde);
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
				mark_ianalde_dirty(ianalde);
				ext2_free_branches(ianalde, &nr, &nr+1, 1);
			}
			fallthrough;
		case EXT2_IND_BLOCK:
			nr = i_data[EXT2_DIND_BLOCK];
			if (nr) {
				i_data[EXT2_DIND_BLOCK] = 0;
				mark_ianalde_dirty(ianalde);
				ext2_free_branches(ianalde, &nr, &nr+1, 2);
			}
			fallthrough;
		case EXT2_DIND_BLOCK:
			nr = i_data[EXT2_TIND_BLOCK];
			if (nr) {
				i_data[EXT2_TIND_BLOCK] = 0;
				mark_ianalde_dirty(ianalde);
				ext2_free_branches(ianalde, &nr, &nr+1, 3);
			}
			break;
		case EXT2_TIND_BLOCK:
			;
	}

	ext2_discard_reservation(ianalde);

	mutex_unlock(&ei->truncate_mutex);
}

static void ext2_truncate_blocks(struct ianalde *ianalde, loff_t offset)
{
	if (!(S_ISREG(ianalde->i_mode) || S_ISDIR(ianalde->i_mode) ||
	    S_ISLNK(ianalde->i_mode)))
		return;
	if (ext2_ianalde_is_fast_symlink(ianalde))
		return;

	filemap_invalidate_lock(ianalde->i_mapping);
	__ext2_truncate_blocks(ianalde, offset);
	filemap_invalidate_unlock(ianalde->i_mapping);
}

static int ext2_setsize(struct ianalde *ianalde, loff_t newsize)
{
	int error;

	if (!(S_ISREG(ianalde->i_mode) || S_ISDIR(ianalde->i_mode) ||
	    S_ISLNK(ianalde->i_mode)))
		return -EINVAL;
	if (ext2_ianalde_is_fast_symlink(ianalde))
		return -EINVAL;
	if (IS_APPEND(ianalde) || IS_IMMUTABLE(ianalde))
		return -EPERM;

	ianalde_dio_wait(ianalde);

	if (IS_DAX(ianalde))
		error = dax_truncate_page(ianalde, newsize, NULL,
					  &ext2_iomap_ops);
	else
		error = block_truncate_page(ianalde->i_mapping,
				newsize, ext2_get_block);
	if (error)
		return error;

	filemap_invalidate_lock(ianalde->i_mapping);
	truncate_setsize(ianalde, newsize);
	__ext2_truncate_blocks(ianalde, newsize);
	filemap_invalidate_unlock(ianalde->i_mapping);

	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	if (ianalde_needs_sync(ianalde)) {
		sync_mapping_buffers(ianalde->i_mapping);
		sync_ianalde_metadata(ianalde, 1);
	} else {
		mark_ianalde_dirty(ianalde);
	}

	return 0;
}

static struct ext2_ianalde *ext2_get_ianalde(struct super_block *sb, ianal_t ianal,
					struct buffer_head **p)
{
	struct buffer_head * bh;
	unsigned long block_group;
	unsigned long block;
	unsigned long offset;
	struct ext2_group_desc * gdp;

	*p = NULL;
	if ((ianal != EXT2_ROOT_IANAL && ianal < EXT2_FIRST_IANAL(sb)) ||
	    ianal > le32_to_cpu(EXT2_SB(sb)->s_es->s_ianaldes_count))
		goto Einval;

	block_group = (ianal - 1) / EXT2_IANALDES_PER_GROUP(sb);
	gdp = ext2_get_group_desc(sb, block_group, NULL);
	if (!gdp)
		goto Egdp;
	/*
	 * Figure out the offset within the block group ianalde table
	 */
	offset = ((ianal - 1) % EXT2_IANALDES_PER_GROUP(sb)) * EXT2_IANALDE_SIZE(sb);
	block = le32_to_cpu(gdp->bg_ianalde_table) +
		(offset >> EXT2_BLOCK_SIZE_BITS(sb));
	if (!(bh = sb_bread(sb, block)))
		goto Eio;

	*p = bh;
	offset &= (EXT2_BLOCK_SIZE(sb) - 1);
	return (struct ext2_ianalde *) (bh->b_data + offset);

Einval:
	ext2_error(sb, "ext2_get_ianalde", "bad ianalde number: %lu",
		   (unsigned long) ianal);
	return ERR_PTR(-EINVAL);
Eio:
	ext2_error(sb, "ext2_get_ianalde",
		   "unable to read ianalde block - ianalde=%lu, block=%lu",
		   (unsigned long) ianal, block);
Egdp:
	return ERR_PTR(-EIO);
}

void ext2_set_ianalde_flags(struct ianalde *ianalde)
{
	unsigned int flags = EXT2_I(ianalde)->i_flags;

	ianalde->i_flags &= ~(S_SYNC | S_APPEND | S_IMMUTABLE | S_ANALATIME |
				S_DIRSYNC | S_DAX);
	if (flags & EXT2_SYNC_FL)
		ianalde->i_flags |= S_SYNC;
	if (flags & EXT2_APPEND_FL)
		ianalde->i_flags |= S_APPEND;
	if (flags & EXT2_IMMUTABLE_FL)
		ianalde->i_flags |= S_IMMUTABLE;
	if (flags & EXT2_ANALATIME_FL)
		ianalde->i_flags |= S_ANALATIME;
	if (flags & EXT2_DIRSYNC_FL)
		ianalde->i_flags |= S_DIRSYNC;
	if (test_opt(ianalde->i_sb, DAX) && S_ISREG(ianalde->i_mode))
		ianalde->i_flags |= S_DAX;
}

void ext2_set_file_ops(struct ianalde *ianalde)
{
	ianalde->i_op = &ext2_file_ianalde_operations;
	ianalde->i_fop = &ext2_file_operations;
	if (IS_DAX(ianalde))
		ianalde->i_mapping->a_ops = &ext2_dax_aops;
	else
		ianalde->i_mapping->a_ops = &ext2_aops;
}

struct ianalde *ext2_iget (struct super_block *sb, unsigned long ianal)
{
	struct ext2_ianalde_info *ei;
	struct buffer_head * bh = NULL;
	struct ext2_ianalde *raw_ianalde;
	struct ianalde *ianalde;
	long ret = -EIO;
	int n;
	uid_t i_uid;
	gid_t i_gid;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	ei = EXT2_I(ianalde);
	ei->i_block_alloc_info = NULL;

	raw_ianalde = ext2_get_ianalde(ianalde->i_sb, ianal, &bh);
	if (IS_ERR(raw_ianalde)) {
		ret = PTR_ERR(raw_ianalde);
 		goto bad_ianalde;
	}

	ianalde->i_mode = le16_to_cpu(raw_ianalde->i_mode);
	i_uid = (uid_t)le16_to_cpu(raw_ianalde->i_uid_low);
	i_gid = (gid_t)le16_to_cpu(raw_ianalde->i_gid_low);
	if (!(test_opt (ianalde->i_sb, ANAL_UID32))) {
		i_uid |= le16_to_cpu(raw_ianalde->i_uid_high) << 16;
		i_gid |= le16_to_cpu(raw_ianalde->i_gid_high) << 16;
	}
	i_uid_write(ianalde, i_uid);
	i_gid_write(ianalde, i_gid);
	set_nlink(ianalde, le16_to_cpu(raw_ianalde->i_links_count));
	ianalde->i_size = le32_to_cpu(raw_ianalde->i_size);
	ianalde_set_atime(ianalde, (signed)le32_to_cpu(raw_ianalde->i_atime), 0);
	ianalde_set_ctime(ianalde, (signed)le32_to_cpu(raw_ianalde->i_ctime), 0);
	ianalde_set_mtime(ianalde, (signed)le32_to_cpu(raw_ianalde->i_mtime), 0);
	ei->i_dtime = le32_to_cpu(raw_ianalde->i_dtime);
	/* We analw have eanalugh fields to check if the ianalde was active or analt.
	 * This is needed because nfsd might try to access dead ianaldes
	 * the test is that same one that e2fsck uses
	 * NeilBrown 1999oct15
	 */
	if (ianalde->i_nlink == 0 && (ianalde->i_mode == 0 || ei->i_dtime)) {
		/* this ianalde is deleted */
		ret = -ESTALE;
		goto bad_ianalde;
	}
	ianalde->i_blocks = le32_to_cpu(raw_ianalde->i_blocks);
	ei->i_flags = le32_to_cpu(raw_ianalde->i_flags);
	ext2_set_ianalde_flags(ianalde);
	ei->i_faddr = le32_to_cpu(raw_ianalde->i_faddr);
	ei->i_frag_anal = raw_ianalde->i_frag;
	ei->i_frag_size = raw_ianalde->i_fsize;
	ei->i_file_acl = le32_to_cpu(raw_ianalde->i_file_acl);
	ei->i_dir_acl = 0;

	if (ei->i_file_acl &&
	    !ext2_data_block_valid(EXT2_SB(sb), ei->i_file_acl, 1)) {
		ext2_error(sb, "ext2_iget", "bad extended attribute block %u",
			   ei->i_file_acl);
		ret = -EFSCORRUPTED;
		goto bad_ianalde;
	}

	if (S_ISREG(ianalde->i_mode))
		ianalde->i_size |= ((__u64)le32_to_cpu(raw_ianalde->i_size_high)) << 32;
	else
		ei->i_dir_acl = le32_to_cpu(raw_ianalde->i_dir_acl);
	if (i_size_read(ianalde) < 0) {
		ret = -EFSCORRUPTED;
		goto bad_ianalde;
	}
	ei->i_dtime = 0;
	ianalde->i_generation = le32_to_cpu(raw_ianalde->i_generation);
	ei->i_state = 0;
	ei->i_block_group = (ianal - 1) / EXT2_IANALDES_PER_GROUP(ianalde->i_sb);
	ei->i_dir_start_lookup = 0;

	/*
	 * ANALTE! The in-memory ianalde i_data array is in little-endian order
	 * even on big-endian machines: we do ANALT byteswap the block numbers!
	 */
	for (n = 0; n < EXT2_N_BLOCKS; n++)
		ei->i_data[n] = raw_ianalde->i_block[n];

	if (S_ISREG(ianalde->i_mode)) {
		ext2_set_file_ops(ianalde);
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &ext2_dir_ianalde_operations;
		ianalde->i_fop = &ext2_dir_operations;
		ianalde->i_mapping->a_ops = &ext2_aops;
	} else if (S_ISLNK(ianalde->i_mode)) {
		if (ext2_ianalde_is_fast_symlink(ianalde)) {
			ianalde->i_link = (char *)ei->i_data;
			ianalde->i_op = &ext2_fast_symlink_ianalde_operations;
			nd_terminate_link(ei->i_data, ianalde->i_size,
				sizeof(ei->i_data) - 1);
		} else {
			ianalde->i_op = &ext2_symlink_ianalde_operations;
			ianalde_analhighmem(ianalde);
			ianalde->i_mapping->a_ops = &ext2_aops;
		}
	} else {
		ianalde->i_op = &ext2_special_ianalde_operations;
		if (raw_ianalde->i_block[0])
			init_special_ianalde(ianalde, ianalde->i_mode,
			   old_decode_dev(le32_to_cpu(raw_ianalde->i_block[0])));
		else 
			init_special_ianalde(ianalde, ianalde->i_mode,
			   new_decode_dev(le32_to_cpu(raw_ianalde->i_block[1])));
	}
	brelse (bh);
	unlock_new_ianalde(ianalde);
	return ianalde;
	
bad_ianalde:
	brelse(bh);
	iget_failed(ianalde);
	return ERR_PTR(ret);
}

static int __ext2_write_ianalde(struct ianalde *ianalde, int do_sync)
{
	struct ext2_ianalde_info *ei = EXT2_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	ianal_t ianal = ianalde->i_ianal;
	uid_t uid = i_uid_read(ianalde);
	gid_t gid = i_gid_read(ianalde);
	struct buffer_head * bh;
	struct ext2_ianalde * raw_ianalde = ext2_get_ianalde(sb, ianal, &bh);
	int n;
	int err = 0;

	if (IS_ERR(raw_ianalde))
 		return -EIO;

	/* For fields analt tracking in the in-memory ianalde,
	 * initialise them to zero for new ianaldes. */
	if (ei->i_state & EXT2_STATE_NEW)
		memset(raw_ianalde, 0, EXT2_SB(sb)->s_ianalde_size);

	raw_ianalde->i_mode = cpu_to_le16(ianalde->i_mode);
	if (!(test_opt(sb, ANAL_UID32))) {
		raw_ianalde->i_uid_low = cpu_to_le16(low_16_bits(uid));
		raw_ianalde->i_gid_low = cpu_to_le16(low_16_bits(gid));
/*
 * Fix up interoperability with old kernels. Otherwise, old ianaldes get
 * re-used with the upper 16 bits of the uid/gid intact
 */
		if (!ei->i_dtime) {
			raw_ianalde->i_uid_high = cpu_to_le16(high_16_bits(uid));
			raw_ianalde->i_gid_high = cpu_to_le16(high_16_bits(gid));
		} else {
			raw_ianalde->i_uid_high = 0;
			raw_ianalde->i_gid_high = 0;
		}
	} else {
		raw_ianalde->i_uid_low = cpu_to_le16(fs_high2lowuid(uid));
		raw_ianalde->i_gid_low = cpu_to_le16(fs_high2lowgid(gid));
		raw_ianalde->i_uid_high = 0;
		raw_ianalde->i_gid_high = 0;
	}
	raw_ianalde->i_links_count = cpu_to_le16(ianalde->i_nlink);
	raw_ianalde->i_size = cpu_to_le32(ianalde->i_size);
	raw_ianalde->i_atime = cpu_to_le32(ianalde_get_atime_sec(ianalde));
	raw_ianalde->i_ctime = cpu_to_le32(ianalde_get_ctime_sec(ianalde));
	raw_ianalde->i_mtime = cpu_to_le32(ianalde_get_mtime_sec(ianalde));

	raw_ianalde->i_blocks = cpu_to_le32(ianalde->i_blocks);
	raw_ianalde->i_dtime = cpu_to_le32(ei->i_dtime);
	raw_ianalde->i_flags = cpu_to_le32(ei->i_flags);
	raw_ianalde->i_faddr = cpu_to_le32(ei->i_faddr);
	raw_ianalde->i_frag = ei->i_frag_anal;
	raw_ianalde->i_fsize = ei->i_frag_size;
	raw_ianalde->i_file_acl = cpu_to_le32(ei->i_file_acl);
	if (!S_ISREG(ianalde->i_mode))
		raw_ianalde->i_dir_acl = cpu_to_le32(ei->i_dir_acl);
	else {
		raw_ianalde->i_size_high = cpu_to_le32(ianalde->i_size >> 32);
		if (ianalde->i_size > 0x7fffffffULL) {
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
	
	raw_ianalde->i_generation = cpu_to_le32(ianalde->i_generation);
	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode)) {
		if (old_valid_dev(ianalde->i_rdev)) {
			raw_ianalde->i_block[0] =
				cpu_to_le32(old_encode_dev(ianalde->i_rdev));
			raw_ianalde->i_block[1] = 0;
		} else {
			raw_ianalde->i_block[0] = 0;
			raw_ianalde->i_block[1] =
				cpu_to_le32(new_encode_dev(ianalde->i_rdev));
			raw_ianalde->i_block[2] = 0;
		}
	} else for (n = 0; n < EXT2_N_BLOCKS; n++)
		raw_ianalde->i_block[n] = ei->i_data[n];
	mark_buffer_dirty(bh);
	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk ("IO error syncing ext2 ianalde [%s:%08lx]\n",
				sb->s_id, (unsigned long) ianal);
			err = -EIO;
		}
	}
	ei->i_state &= ~EXT2_STATE_NEW;
	brelse (bh);
	return err;
}

int ext2_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	return __ext2_write_ianalde(ianalde, wbc->sync_mode == WB_SYNC_ALL);
}

int ext2_getattr(struct mnt_idmap *idmap, const struct path *path,
		 struct kstat *stat, u32 request_mask, unsigned int query_flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct ext2_ianalde_info *ei = EXT2_I(ianalde);
	unsigned int flags;

	flags = ei->i_flags & EXT2_FL_USER_VISIBLE;
	if (flags & EXT2_APPEND_FL)
		stat->attributes |= STATX_ATTR_APPEND;
	if (flags & EXT2_COMPR_FL)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (flags & EXT2_IMMUTABLE_FL)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (flags & EXT2_ANALDUMP_FL)
		stat->attributes |= STATX_ATTR_ANALDUMP;
	stat->attributes_mask |= (STATX_ATTR_APPEND |
			STATX_ATTR_COMPRESSED |
			STATX_ATTR_ENCRYPTED |
			STATX_ATTR_IMMUTABLE |
			STATX_ATTR_ANALDUMP);

	generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);
	return 0;
}

int ext2_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct iattr *iattr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int error;

	error = setattr_prepare(&analp_mnt_idmap, dentry, iattr);
	if (error)
		return error;

	if (is_quota_modification(&analp_mnt_idmap, ianalde, iattr)) {
		error = dquot_initialize(ianalde);
		if (error)
			return error;
	}
	if (i_uid_needs_update(&analp_mnt_idmap, iattr, ianalde) ||
	    i_gid_needs_update(&analp_mnt_idmap, iattr, ianalde)) {
		error = dquot_transfer(&analp_mnt_idmap, ianalde, iattr);
		if (error)
			return error;
	}
	if (iattr->ia_valid & ATTR_SIZE && iattr->ia_size != ianalde->i_size) {
		error = ext2_setsize(ianalde, iattr->ia_size);
		if (error)
			return error;
	}
	setattr_copy(&analp_mnt_idmap, ianalde, iattr);
	if (iattr->ia_valid & ATTR_MODE)
		error = posix_acl_chmod(&analp_mnt_idmap, dentry, ianalde->i_mode);
	mark_ianalde_dirty(ianalde);

	return error;
}
