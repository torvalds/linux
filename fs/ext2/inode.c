/*
 *  linux/fs/ext2/inode.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
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

#include <linux/smp_lock.h>
#include <linux/time.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include "ext2.h"
#include "acl.h"
#include "xip.h"

MODULE_AUTHOR("Remy Card and others");
MODULE_DESCRIPTION("Second Extended Filesystem");
MODULE_LICENSE("GPL");

static int ext2_update_inode(struct inode * inode, int do_sync);

/*
 * Test whether an inode is a fast symlink.
 */
static inline int ext2_inode_is_fast_symlink(struct inode *inode)
{
	int ea_blocks = EXT2_I(inode)->i_file_acl ?
		(inode->i_sb->s_blocksize >> 9) : 0;

	return (S_ISLNK(inode->i_mode) &&
		inode->i_blocks - ea_blocks == 0);
}

/*
 * Called at each iput().
 *
 * The inode may be "bad" if ext2_read_inode() saw an error from
 * ext2_get_inode(), so we need to check that to avoid freeing random disk
 * blocks.
 */
void ext2_put_inode(struct inode *inode)
{
	if (!is_bad_inode(inode))
		ext2_discard_prealloc(inode);
}

/*
 * Called at the last iput() if i_nlink is zero.
 */
void ext2_delete_inode (struct inode * inode)
{
	truncate_inode_pages(&inode->i_data, 0);

	if (is_bad_inode(inode))
		goto no_delete;
	EXT2_I(inode)->i_dtime	= get_seconds();
	mark_inode_dirty(inode);
	ext2_update_inode(inode, inode_needs_sync(inode));

	inode->i_size = 0;
	if (inode->i_blocks)
		ext2_truncate (inode);
	ext2_free_inode (inode);

	return;
no_delete:
	clear_inode(inode);	/* We must guarantee clearing of inode... */
}

void ext2_discard_prealloc (struct inode * inode)
{
#ifdef EXT2_PREALLOCATE
	struct ext2_inode_info *ei = EXT2_I(inode);
	write_lock(&ei->i_meta_lock);
	if (ei->i_prealloc_count) {
		unsigned short total = ei->i_prealloc_count;
		unsigned long block = ei->i_prealloc_block;
		ei->i_prealloc_count = 0;
		ei->i_prealloc_block = 0;
		write_unlock(&ei->i_meta_lock);
		ext2_free_blocks (inode, block, total);
		return;
	} else
		write_unlock(&ei->i_meta_lock);
#endif
}

static int ext2_alloc_block (struct inode * inode, unsigned long goal, int *err)
{
#ifdef EXT2FS_DEBUG
	static unsigned long alloc_hits, alloc_attempts;
#endif
	unsigned long result;


#ifdef EXT2_PREALLOCATE
	struct ext2_inode_info *ei = EXT2_I(inode);
	write_lock(&ei->i_meta_lock);
	if (ei->i_prealloc_count &&
	    (goal == ei->i_prealloc_block || goal + 1 == ei->i_prealloc_block))
	{
		result = ei->i_prealloc_block++;
		ei->i_prealloc_count--;
		write_unlock(&ei->i_meta_lock);
		ext2_debug ("preallocation hit (%lu/%lu).\n",
			    ++alloc_hits, ++alloc_attempts);
	} else {
		write_unlock(&ei->i_meta_lock);
		ext2_discard_prealloc (inode);
		ext2_debug ("preallocation miss (%lu/%lu).\n",
			    alloc_hits, ++alloc_attempts);
		if (S_ISREG(inode->i_mode))
			result = ext2_new_block (inode, goal, 
				 &ei->i_prealloc_count,
				 &ei->i_prealloc_block, err);
		else
			result = ext2_new_block(inode, goal, NULL, NULL, err);
	}
#else
	result = ext2_new_block (inode, goal, 0, 0, err);
#endif
	return result;
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
 *	@inode: inode in question (we are only interested in its superblock)
 *	@i_block: block number to be parsed
 *	@offsets: array to store the offsets in
 *      @boundary: set this non-zero if the referred-to block is likely to be
 *             followed (on disk) by an indirect block.
 *	To store the locations of file's data ext2 uses a data structure common
 *	for UNIX filesystems - tree of pointers anchored in the inode, with
 *	data blocks at leaves and indirect blocks in intermediate nodes.
 *	This function translates the block number into path in that tree -
 *	return value is the path length and @offsets[n] is the offset of
 *	pointer to (n+1)th node in the nth one. If @block is out of range
 *	(negative or too large) warning is printed and zero returned.
 *
 *	Note: function doesn't find node addresses, so no IO is needed. All
 *	we need to know is the capacity of indirect blocks (taken from the
 *	inode->i_sb).
 */

/*
 * Portability note: the last comparison (check that we fit into triple
 * indirect block) is spelled differently, because otherwise on an
 * architecture with 32-bit longs and 8Kb pages we might get into trouble
 * if our filesystem had 8Kb blocks. We might use long long, but that would
 * kill us on x86. Oh, well, at least the sign propagation does not matter -
 * i_block would have to be negative in the very beginning, so we would not
 * get there at all.
 */

static int ext2_block_to_path(struct inode *inode,
			long i_block, int offsets[4], int *boundary)
{
	int ptrs = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	int ptrs_bits = EXT2_ADDR_PER_BLOCK_BITS(inode->i_sb);
	const long direct_blocks = EXT2_NDIR_BLOCKS,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2));
	int n = 0;
	int final = 0;

	if (i_block < 0) {
		ext2_warning (inode->i_sb, "ext2_block_to_path", "block < 0");
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
		ext2_warning (inode->i_sb, "ext2_block_to_path", "block > big");
	}
	if (boundary)
		*boundary = (i_block & (ptrs - 1)) == (final - 1);
	return n;
}

/**
 *	ext2_get_branch - read the chain of indirect blocks leading to data
 *	@inode: inode in question
 *	@depth: depth of the chain (1 - direct pointer, etc.)
 *	@offsets: offsets of pointers in inode/indirect blocks
 *	@chain: place to store the result
 *	@err: here we store the error value
 *
 *	Function fills the array of triples <key, p, bh> and returns %NULL
 *	if everything went OK or the pointer to the last filled triple
 *	(incomplete one) otherwise. Upon the return chain[i].key contains
 *	the number of (i+1)-th block in the chain (as it is stored in memory,
 *	i.e. little-endian 32-bit), chain[i].p contains the address of that
 *	number (it points into struct inode for i==0 and into the bh->b_data
 *	for i>0) and chain[i].bh points to the buffer_head of i-th indirect
 *	block for i>0 and NULL for i==0. In other words, it holds the block
 *	numbers of the chain, addresses they were taken from (and where we can
 *	verify that chain did not change) and buffer_heads hosting these
 *	numbers.
 *
 *	Function stops when it stumbles upon zero pointer (absent block)
 *		(pointer to last triple returned, *@err == 0)
 *	or when it gets an IO error reading an indirect block
 *		(ditto, *@err == -EIO)
 *	or when it notices that chain had been changed while it was reading
 *		(ditto, *@err == -EAGAIN)
 *	or when it reads all @depth-1 indirect blocks successfully and finds
 *	the whole chain, all way to the data (returns %NULL, *err == 0).
 */
static Indirect *ext2_get_branch(struct inode *inode,
				 int depth,
				 int *offsets,
				 Indirect chain[4],
				 int *err)
{
	struct super_block *sb = inode->i_sb;
	Indirect *p = chain;
	struct buffer_head *bh;

	*err = 0;
	/* i_data is not going away, no lock needed */
	add_chain (chain, NULL, EXT2_I(inode)->i_data + *offsets);
	if (!p->key)
		goto no_block;
	while (--depth) {
		bh = sb_bread(sb, le32_to_cpu(p->key));
		if (!bh)
			goto failure;
		read_lock(&EXT2_I(inode)->i_meta_lock);
		if (!verify_chain(chain, p))
			goto changed;
		add_chain(++p, bh, (__le32*)bh->b_data + *++offsets);
		read_unlock(&EXT2_I(inode)->i_meta_lock);
		if (!p->key)
			goto no_block;
	}
	return NULL;

changed:
	read_unlock(&EXT2_I(inode)->i_meta_lock);
	brelse(bh);
	*err = -EAGAIN;
	goto no_block;
failure:
	*err = -EIO;
no_block:
	return p;
}

/**
 *	ext2_find_near - find a place for allocation with sufficient locality
 *	@inode: owner
 *	@ind: descriptor of indirect block.
 *
 *	This function returns the prefered place for block allocation.
 *	It is used when heuristic for sequential allocation fails.
 *	Rules are:
 *	  + if there is a block to the left of our position - allocate near it.
 *	  + if pointer will live in indirect block - allocate near that block.
 *	  + if pointer will live in inode - allocate in the same cylinder group.
 *
 * In the latter case we colour the starting block by the callers PID to
 * prevent it from clashing with concurrent allocations for a different inode
 * in the same block group.   The PID is used here so that functionally related
 * files will be close-by on-disk.
 *
 *	Caller must make sure that @ind is valid and will stay that way.
 */

static unsigned long ext2_find_near(struct inode *inode, Indirect *ind)
{
	struct ext2_inode_info *ei = EXT2_I(inode);
	__le32 *start = ind->bh ? (__le32 *) ind->bh->b_data : ei->i_data;
	__le32 *p;
	unsigned long bg_start;
	unsigned long colour;

	/* Try to find previous block */
	for (p = ind->p - 1; p >= start; p--)
		if (*p)
			return le32_to_cpu(*p);

	/* No such thing, so let's try location of indirect block */
	if (ind->bh)
		return ind->bh->b_blocknr;

	/*
	 * It is going to be refered from inode itself? OK, just put it into
	 * the same cylinder group then.
	 */
	bg_start = (ei->i_block_group * EXT2_BLOCKS_PER_GROUP(inode->i_sb)) +
		le32_to_cpu(EXT2_SB(inode->i_sb)->s_es->s_first_data_block);
	colour = (current->pid % 16) *
			(EXT2_BLOCKS_PER_GROUP(inode->i_sb) / 16);
	return bg_start + colour;
}

/**
 *	ext2_find_goal - find a prefered place for allocation.
 *	@inode: owner
 *	@block:  block we want
 *	@chain:  chain of indirect blocks
 *	@partial: pointer to the last triple within a chain
 *	@goal:	place to store the result.
 *
 *	Normally this function find the prefered place for block allocation,
 *	stores it in *@goal and returns zero. If the branch had been changed
 *	under us we return -EAGAIN.
 */

static inline int ext2_find_goal(struct inode *inode,
				 long block,
				 Indirect chain[4],
				 Indirect *partial,
				 unsigned long *goal)
{
	struct ext2_inode_info *ei = EXT2_I(inode);
	write_lock(&ei->i_meta_lock);
	if ((block == ei->i_next_alloc_block + 1) && ei->i_next_alloc_goal) {
		ei->i_next_alloc_block++;
		ei->i_next_alloc_goal++;
	} 
	if (verify_chain(chain, partial)) {
		/*
		 * try the heuristic for sequential allocation,
		 * failing that at least try to get decent locality.
		 */
		if (block == ei->i_next_alloc_block)
			*goal = ei->i_next_alloc_goal;
		if (!*goal)
			*goal = ext2_find_near(inode, partial);
		write_unlock(&ei->i_meta_lock);
		return 0;
	}
	write_unlock(&ei->i_meta_lock);
	return -EAGAIN;
}

/**
 *	ext2_alloc_branch - allocate and set up a chain of blocks.
 *	@inode: owner
 *	@num: depth of the chain (number of blocks to allocate)
 *	@offsets: offsets (in the blocks) to store the pointers to next.
 *	@branch: place to store the chain in.
 *
 *	This function allocates @num blocks, zeroes out all but the last one,
 *	links them into chain and (if we are synchronous) writes them to disk.
 *	In other words, it prepares a branch that can be spliced onto the
 *	inode. It stores the information about that chain in the branch[], in
 *	the same format as ext2_get_branch() would do. We are calling it after
 *	we had read the existing part of chain and partial points to the last
 *	triple of that (one with zero ->key). Upon the exit we have the same
 *	picture as after the successful ext2_get_block(), excpet that in one
 *	place chain is disconnected - *branch->p is still zero (we did not
 *	set the last link), but branch->key contains the number that should
 *	be placed into *branch->p to fill that gap.
 *
 *	If allocation fails we free all blocks we've allocated (and forget
 *	their buffer_heads) and return the error value the from failed
 *	ext2_alloc_block() (normally -ENOSPC). Otherwise we set the chain
 *	as described above and return 0.
 */

static int ext2_alloc_branch(struct inode *inode,
			     int num,
			     unsigned long goal,
			     int *offsets,
			     Indirect *branch)
{
	int blocksize = inode->i_sb->s_blocksize;
	int n = 0;
	int err;
	int i;
	int parent = ext2_alloc_block(inode, goal, &err);

	branch[0].key = cpu_to_le32(parent);
	if (parent) for (n = 1; n < num; n++) {
		struct buffer_head *bh;
		/* Allocate the next block */
		int nr = ext2_alloc_block(inode, parent, &err);
		if (!nr)
			break;
		branch[n].key = cpu_to_le32(nr);
		/*
		 * Get buffer_head for parent block, zero it out and set 
		 * the pointer to new one, then send parent to disk.
		 */
		bh = sb_getblk(inode->i_sb, parent);
		if (!bh) {
			err = -EIO;
			break;
		}
		lock_buffer(bh);
		memset(bh->b_data, 0, blocksize);
		branch[n].bh = bh;
		branch[n].p = (__le32 *) bh->b_data + offsets[n];
		*branch[n].p = branch[n].key;
		set_buffer_uptodate(bh);
		unlock_buffer(bh);
		mark_buffer_dirty_inode(bh, inode);
		/* We used to sync bh here if IS_SYNC(inode).
		 * But we now rely upon generic_osync_inode()
		 * and b_inode_buffers.  But not for directories.
		 */
		if (S_ISDIR(inode->i_mode) && IS_DIRSYNC(inode))
			sync_dirty_buffer(bh);
		parent = nr;
	}
	if (n == num)
		return 0;

	/* Allocation failed, free what we already allocated */
	for (i = 1; i < n; i++)
		bforget(branch[i].bh);
	for (i = 0; i < n; i++)
		ext2_free_blocks(inode, le32_to_cpu(branch[i].key), 1);
	return err;
}

/**
 *	ext2_splice_branch - splice the allocated branch onto inode.
 *	@inode: owner
 *	@block: (logical) number of block we are adding
 *	@chain: chain of indirect blocks (with a missing link - see
 *		ext2_alloc_branch)
 *	@where: location of missing link
 *	@num:   number of blocks we are adding
 *
 *	This function verifies that chain (up to the missing link) had not
 *	changed, fills the missing link and does all housekeeping needed in
 *	inode (->i_blocks, etc.). In case of success we end up with the full
 *	chain to new block and return 0. Otherwise (== chain had been changed)
 *	we free the new blocks (forgetting their buffer_heads, indeed) and
 *	return -EAGAIN.
 */

static inline int ext2_splice_branch(struct inode *inode,
				     long block,
				     Indirect chain[4],
				     Indirect *where,
				     int num)
{
	struct ext2_inode_info *ei = EXT2_I(inode);
	int i;

	/* Verify that place we are splicing to is still there and vacant */

	write_lock(&ei->i_meta_lock);
	if (!verify_chain(chain, where-1) || *where->p)
		goto changed;

	/* That's it */

	*where->p = where->key;
	ei->i_next_alloc_block = block;
	ei->i_next_alloc_goal = le32_to_cpu(where[num-1].key);

	write_unlock(&ei->i_meta_lock);

	/* We are done with atomic stuff, now do the rest of housekeeping */

	inode->i_ctime = CURRENT_TIME_SEC;

	/* had we spliced it onto indirect block? */
	if (where->bh)
		mark_buffer_dirty_inode(where->bh, inode);

	mark_inode_dirty(inode);
	return 0;

changed:
	write_unlock(&ei->i_meta_lock);
	for (i = 1; i < num; i++)
		bforget(where[i].bh);
	for (i = 0; i < num; i++)
		ext2_free_blocks(inode, le32_to_cpu(where[i].key), 1);
	return -EAGAIN;
}

/*
 * Allocation strategy is simple: if we have to allocate something, we will
 * have to go the whole way to leaf. So let's do it before attaching anything
 * to tree, set linkage between the newborn blocks, write them if sync is
 * required, recheck the path, free and repeat if check fails, otherwise
 * set the last missing link (that will protect us from any truncate-generated
 * removals - all blocks on the path are immune now) and possibly force the
 * write on the parent block.
 * That has a nice additional property: no special recovery from the failed
 * allocations is needed - we simply release blocks and do not touch anything
 * reachable from inode.
 */

int ext2_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
{
	int err = -EIO;
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
	unsigned long goal;
	int left;
	int boundary = 0;
	int depth = ext2_block_to_path(inode, iblock, offsets, &boundary);

	if (depth == 0)
		goto out;

reread:
	partial = ext2_get_branch(inode, depth, offsets, chain, &err);

	/* Simplest case - block found, no allocation needed */
	if (!partial) {
got_it:
		map_bh(bh_result, inode->i_sb, le32_to_cpu(chain[depth-1].key));
		if (boundary)
			set_buffer_boundary(bh_result);
		/* Clean up and exit */
		partial = chain+depth-1; /* the whole chain */
		goto cleanup;
	}

	/* Next simple case - plain lookup or failed read of indirect block */
	if (!create || err == -EIO) {
cleanup:
		while (partial > chain) {
			brelse(partial->bh);
			partial--;
		}
out:
		return err;
	}

	/*
	 * Indirect block might be removed by truncate while we were
	 * reading it. Handling of that case (forget what we've got and
	 * reread) is taken out of the main path.
	 */
	if (err == -EAGAIN)
		goto changed;

	goal = 0;
	if (ext2_find_goal(inode, iblock, chain, partial, &goal) < 0)
		goto changed;

	left = (chain + depth) - partial;
	err = ext2_alloc_branch(inode, left, goal,
					offsets+(partial-chain), partial);
	if (err)
		goto cleanup;

	if (ext2_use_xip(inode->i_sb)) {
		/*
		 * we need to clear the block
		 */
		err = ext2_clear_xip_target (inode,
			le32_to_cpu(chain[depth-1].key));
		if (err)
			goto cleanup;
	}

	if (ext2_splice_branch(inode, iblock, chain, partial, left) < 0)
		goto changed;

	set_buffer_new(bh_result);
	goto got_it;

changed:
	while (partial > chain) {
		brelse(partial->bh);
		partial--;
	}
	goto reread;
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
ext2_prepare_write(struct file *file, struct page *page,
			unsigned from, unsigned to)
{
	return block_prepare_write(page,from,to,ext2_get_block);
}

static int
ext2_nobh_prepare_write(struct file *file, struct page *page,
			unsigned from, unsigned to)
{
	return nobh_prepare_write(page,from,to,ext2_get_block);
}

static int ext2_nobh_writepage(struct page *page,
			struct writeback_control *wbc)
{
	return nobh_writepage(page, ext2_get_block, wbc);
}

static sector_t ext2_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,ext2_get_block);
}

static ssize_t
ext2_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
			loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;

	return blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
				offset, nr_segs, ext2_get_block, NULL);
}

static int
ext2_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, ext2_get_block);
}

struct address_space_operations ext2_aops = {
	.readpage		= ext2_readpage,
	.readpages		= ext2_readpages,
	.writepage		= ext2_writepage,
	.sync_page		= block_sync_page,
	.prepare_write		= ext2_prepare_write,
	.commit_write		= generic_commit_write,
	.bmap			= ext2_bmap,
	.direct_IO		= ext2_direct_IO,
	.writepages		= ext2_writepages,
	.migratepage		= buffer_migrate_page,
};

struct address_space_operations ext2_aops_xip = {
	.bmap			= ext2_bmap,
	.get_xip_page		= ext2_get_xip_page,
};

struct address_space_operations ext2_nobh_aops = {
	.readpage		= ext2_readpage,
	.readpages		= ext2_readpages,
	.writepage		= ext2_nobh_writepage,
	.sync_page		= block_sync_page,
	.prepare_write		= ext2_nobh_prepare_write,
	.commit_write		= nobh_commit_write,
	.bmap			= ext2_bmap,
	.direct_IO		= ext2_direct_IO,
	.writepages		= ext2_writepages,
	.migratepage		= buffer_migrate_page,
};

/*
 * Probably it should be a library function... search for first non-zero word
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
 *	@inode:	  inode in question
 *	@depth:	  depth of the affected branch
 *	@offsets: offsets of pointers in that branch (see ext2_block_to_path)
 *	@chain:	  place to store the pointers to partial indirect blocks
 *	@top:	  place to the (detached) top of branch
 *
 *	This is a helper function used by ext2_truncate().
 *
 *	When we do truncate() we may have to clean the ends of several indirect
 *	blocks but leave the blocks themselves alive. Block is partially
 *	truncated if some data below the new i_size is refered from it (and
 *	it is on the path to the first completely truncated data block, indeed).
 *	We have to free the top of that path along with everything to the right
 *	of the path. Since no allocation past the truncation point is possible
 *	until ext2_truncate() finishes, we may safely do the latter, but top
 *	of branch may require special attention - pageout below the truncation
 *	point might try to populate it.
 *
 *	We atomically detach the top of branch from the tree, store the block
 *	number of its root in *@top, pointers to buffer_heads of partially
 *	truncated blocks - in @chain[].bh and pointers to their last elements
 *	that should not be removed - in @chain[].p. Return value is the pointer
 *	to last filled element of @chain.
 *
 *	The work left to caller to do the actual freeing of subtrees:
 *		a) free the subtree starting from *@top
 *		b) free the subtrees whose roots are stored in
 *			(@chain[i].p+1 .. end of @chain[i].bh->b_data)
 *		c) free the subtrees growing from the inode past the @chain[0].p
 *			(no partially truncated stuff there).
 */

static Indirect *ext2_find_shared(struct inode *inode,
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
	partial = ext2_get_branch(inode, k, offsets, chain, &err);
	if (!partial)
		partial = chain + k-1;
	/*
	 * If the branch acquired continuation since we've looked at it -
	 * fine, it should all survive and (new) top doesn't belong to us.
	 */
	write_lock(&EXT2_I(inode)->i_meta_lock);
	if (!partial->key && *partial->p) {
		write_unlock(&EXT2_I(inode)->i_meta_lock);
		goto no_top;
	}
	for (p=partial; p>chain && all_zeroes((__le32*)p->bh->b_data,p->p); p--)
		;
	/*
	 * OK, we've found the last block that must survive. The rest of our
	 * branch should be detached before unlocking. However, if that rest
	 * of branch is all ours and does not grow immediately from the inode
	 * it's easier to cheat and just decrement partial->p.
	 */
	if (p == chain + k - 1 && p > chain) {
		p->p--;
	} else {
		*top = *p->p;
		*p->p = 0;
	}
	write_unlock(&EXT2_I(inode)->i_meta_lock);

	while(partial > p)
	{
		brelse(partial->bh);
		partial--;
	}
no_top:
	return partial;
}

/**
 *	ext2_free_data - free a list of data blocks
 *	@inode:	inode we are dealing with
 *	@p:	array of block numbers
 *	@q:	points immediately past the end of array
 *
 *	We are freeing all blocks refered from that array (numbers are
 *	stored as little-endian 32-bit) and updating @inode->i_blocks
 *	appropriately.
 */
static inline void ext2_free_data(struct inode *inode, __le32 *p, __le32 *q)
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
				mark_inode_dirty(inode);
				ext2_free_blocks (inode, block_to_free, count);
			free_this:
				block_to_free = nr;
				count = 1;
			}
		}
	}
	if (count > 0) {
		mark_inode_dirty(inode);
		ext2_free_blocks (inode, block_to_free, count);
	}
}

/**
 *	ext2_free_branches - free an array of branches
 *	@inode:	inode we are dealing with
 *	@p:	array of block numbers
 *	@q:	pointer immediately past the end of array
 *	@depth:	depth of the branches to free
 *
 *	We are freeing all blocks refered from these branches (numbers are
 *	stored as little-endian 32-bit) and updating @inode->i_blocks
 *	appropriately.
 */
static void ext2_free_branches(struct inode *inode, __le32 *p, __le32 *q, int depth)
{
	struct buffer_head * bh;
	unsigned long nr;

	if (depth--) {
		int addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
		for ( ; p < q ; p++) {
			nr = le32_to_cpu(*p);
			if (!nr)
				continue;
			*p = 0;
			bh = sb_bread(inode->i_sb, nr);
			/*
			 * A read failure? Report error and clear slot
			 * (should be rare).
			 */ 
			if (!bh) {
				ext2_error(inode->i_sb, "ext2_free_branches",
					"Read failure, inode=%ld, block=%ld",
					inode->i_ino, nr);
				continue;
			}
			ext2_free_branches(inode,
					   (__le32*)bh->b_data,
					   (__le32*)bh->b_data + addr_per_block,
					   depth);
			bforget(bh);
			ext2_free_blocks(inode, nr, 1);
			mark_inode_dirty(inode);
		}
	} else
		ext2_free_data(inode, p, q);
}

void ext2_truncate (struct inode * inode)
{
	__le32 *i_data = EXT2_I(inode)->i_data;
	int addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
	__le32 nr = 0;
	int n;
	long iblock;
	unsigned blocksize;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	    S_ISLNK(inode->i_mode)))
		return;
	if (ext2_inode_is_fast_symlink(inode))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	ext2_discard_prealloc(inode);

	blocksize = inode->i_sb->s_blocksize;
	iblock = (inode->i_size + blocksize-1)
					>> EXT2_BLOCK_SIZE_BITS(inode->i_sb);

	if (mapping_is_xip(inode->i_mapping))
		xip_truncate_page(inode->i_mapping, inode->i_size);
	else if (test_opt(inode->i_sb, NOBH))
		nobh_truncate_page(inode->i_mapping, inode->i_size);
	else
		block_truncate_page(inode->i_mapping,
				inode->i_size, ext2_get_block);

	n = ext2_block_to_path(inode, iblock, offsets, NULL);
	if (n == 0)
		return;

	if (n == 1) {
		ext2_free_data(inode, i_data+offsets[0],
					i_data + EXT2_NDIR_BLOCKS);
		goto do_indirects;
	}

	partial = ext2_find_shared(inode, n, offsets, chain, &nr);
	/* Kill the top of shared branch (already detached) */
	if (nr) {
		if (partial == chain)
			mark_inode_dirty(inode);
		else
			mark_buffer_dirty_inode(partial->bh, inode);
		ext2_free_branches(inode, &nr, &nr+1, (chain+n-1) - partial);
	}
	/* Clear the ends of indirect blocks on the shared branch */
	while (partial > chain) {
		ext2_free_branches(inode,
				   partial->p + 1,
				   (__le32*)partial->bh->b_data+addr_per_block,
				   (chain+n-1) - partial);
		mark_buffer_dirty_inode(partial->bh, inode);
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
				mark_inode_dirty(inode);
				ext2_free_branches(inode, &nr, &nr+1, 1);
			}
		case EXT2_IND_BLOCK:
			nr = i_data[EXT2_DIND_BLOCK];
			if (nr) {
				i_data[EXT2_DIND_BLOCK] = 0;
				mark_inode_dirty(inode);
				ext2_free_branches(inode, &nr, &nr+1, 2);
			}
		case EXT2_DIND_BLOCK:
			nr = i_data[EXT2_TIND_BLOCK];
			if (nr) {
				i_data[EXT2_TIND_BLOCK] = 0;
				mark_inode_dirty(inode);
				ext2_free_branches(inode, &nr, &nr+1, 3);
			}
		case EXT2_TIND_BLOCK:
			;
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
	if (inode_needs_sync(inode)) {
		sync_mapping_buffers(inode->i_mapping);
		ext2_sync_inode (inode);
	} else {
		mark_inode_dirty(inode);
	}
}

static struct ext2_inode *ext2_get_inode(struct super_block *sb, ino_t ino,
					struct buffer_head **p)
{
	struct buffer_head * bh;
	unsigned long block_group;
	unsigned long block;
	unsigned long offset;
	struct ext2_group_desc * gdp;

	*p = NULL;
	if ((ino != EXT2_ROOT_INO && ino < EXT2_FIRST_INO(sb)) ||
	    ino > le32_to_cpu(EXT2_SB(sb)->s_es->s_inodes_count))
		goto Einval;

	block_group = (ino - 1) / EXT2_INODES_PER_GROUP(sb);
	gdp = ext2_get_group_desc(sb, block_group, &bh);
	if (!gdp)
		goto Egdp;
	/*
	 * Figure out the offset within the block group inode table
	 */
	offset = ((ino - 1) % EXT2_INODES_PER_GROUP(sb)) * EXT2_INODE_SIZE(sb);
	block = le32_to_cpu(gdp->bg_inode_table) +
		(offset >> EXT2_BLOCK_SIZE_BITS(sb));
	if (!(bh = sb_bread(sb, block)))
		goto Eio;

	*p = bh;
	offset &= (EXT2_BLOCK_SIZE(sb) - 1);
	return (struct ext2_inode *) (bh->b_data + offset);

Einval:
	ext2_error(sb, "ext2_get_inode", "bad inode number: %lu",
		   (unsigned long) ino);
	return ERR_PTR(-EINVAL);
Eio:
	ext2_error(sb, "ext2_get_inode",
		   "unable to read inode block - inode=%lu, block=%lu",
		   (unsigned long) ino, block);
Egdp:
	return ERR_PTR(-EIO);
}

void ext2_set_inode_flags(struct inode *inode)
{
	unsigned int flags = EXT2_I(inode)->i_flags;

	inode->i_flags &= ~(S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);
	if (flags & EXT2_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & EXT2_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & EXT2_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & EXT2_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & EXT2_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
}

void ext2_read_inode (struct inode * inode)
{
	struct ext2_inode_info *ei = EXT2_I(inode);
	ino_t ino = inode->i_ino;
	struct buffer_head * bh;
	struct ext2_inode * raw_inode = ext2_get_inode(inode->i_sb, ino, &bh);
	int n;

#ifdef CONFIG_EXT2_FS_POSIX_ACL
	ei->i_acl = EXT2_ACL_NOT_CACHED;
	ei->i_default_acl = EXT2_ACL_NOT_CACHED;
#endif
	if (IS_ERR(raw_inode))
 		goto bad_inode;

	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	inode->i_uid = (uid_t)le16_to_cpu(raw_inode->i_uid_low);
	inode->i_gid = (gid_t)le16_to_cpu(raw_inode->i_gid_low);
	if (!(test_opt (inode->i_sb, NO_UID32))) {
		inode->i_uid |= le16_to_cpu(raw_inode->i_uid_high) << 16;
		inode->i_gid |= le16_to_cpu(raw_inode->i_gid_high) << 16;
	}
	inode->i_nlink = le16_to_cpu(raw_inode->i_links_count);
	inode->i_size = le32_to_cpu(raw_inode->i_size);
	inode->i_atime.tv_sec = le32_to_cpu(raw_inode->i_atime);
	inode->i_ctime.tv_sec = le32_to_cpu(raw_inode->i_ctime);
	inode->i_mtime.tv_sec = le32_to_cpu(raw_inode->i_mtime);
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = 0;
	ei->i_dtime = le32_to_cpu(raw_inode->i_dtime);
	/* We now have enough fields to check if the inode was active or not.
	 * This is needed because nfsd might try to access dead inodes
	 * the test is that same one that e2fsck uses
	 * NeilBrown 1999oct15
	 */
	if (inode->i_nlink == 0 && (inode->i_mode == 0 || ei->i_dtime)) {
		/* this inode is deleted */
		brelse (bh);
		goto bad_inode;
	}
	inode->i_blksize = PAGE_SIZE;	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);
	ei->i_flags = le32_to_cpu(raw_inode->i_flags);
	ei->i_faddr = le32_to_cpu(raw_inode->i_faddr);
	ei->i_frag_no = raw_inode->i_frag;
	ei->i_frag_size = raw_inode->i_fsize;
	ei->i_file_acl = le32_to_cpu(raw_inode->i_file_acl);
	ei->i_dir_acl = 0;
	if (S_ISREG(inode->i_mode))
		inode->i_size |= ((__u64)le32_to_cpu(raw_inode->i_size_high)) << 32;
	else
		ei->i_dir_acl = le32_to_cpu(raw_inode->i_dir_acl);
	ei->i_dtime = 0;
	inode->i_generation = le32_to_cpu(raw_inode->i_generation);
	ei->i_state = 0;
	ei->i_next_alloc_block = 0;
	ei->i_next_alloc_goal = 0;
	ei->i_prealloc_count = 0;
	ei->i_block_group = (ino - 1) / EXT2_INODES_PER_GROUP(inode->i_sb);
	ei->i_dir_start_lookup = 0;

	/*
	 * NOTE! The in-memory inode i_data array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (n = 0; n < EXT2_N_BLOCKS; n++)
		ei->i_data[n] = raw_inode->i_block[n];

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &ext2_file_inode_operations;
		if (ext2_use_xip(inode->i_sb)) {
			inode->i_mapping->a_ops = &ext2_aops_xip;
			inode->i_fop = &ext2_xip_file_operations;
		} else if (test_opt(inode->i_sb, NOBH)) {
			inode->i_mapping->a_ops = &ext2_nobh_aops;
			inode->i_fop = &ext2_file_operations;
		} else {
			inode->i_mapping->a_ops = &ext2_aops;
			inode->i_fop = &ext2_file_operations;
		}
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ext2_dir_inode_operations;
		inode->i_fop = &ext2_dir_operations;
		if (test_opt(inode->i_sb, NOBH))
			inode->i_mapping->a_ops = &ext2_nobh_aops;
		else
			inode->i_mapping->a_ops = &ext2_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		if (ext2_inode_is_fast_symlink(inode))
			inode->i_op = &ext2_fast_symlink_inode_operations;
		else {
			inode->i_op = &ext2_symlink_inode_operations;
			if (test_opt(inode->i_sb, NOBH))
				inode->i_mapping->a_ops = &ext2_nobh_aops;
			else
				inode->i_mapping->a_ops = &ext2_aops;
		}
	} else {
		inode->i_op = &ext2_special_inode_operations;
		if (raw_inode->i_block[0])
			init_special_inode(inode, inode->i_mode,
			   old_decode_dev(le32_to_cpu(raw_inode->i_block[0])));
		else 
			init_special_inode(inode, inode->i_mode,
			   new_decode_dev(le32_to_cpu(raw_inode->i_block[1])));
	}
	brelse (bh);
	ext2_set_inode_flags(inode);
	return;
	
bad_inode:
	make_bad_inode(inode);
	return;
}

static int ext2_update_inode(struct inode * inode, int do_sync)
{
	struct ext2_inode_info *ei = EXT2_I(inode);
	struct super_block *sb = inode->i_sb;
	ino_t ino = inode->i_ino;
	uid_t uid = inode->i_uid;
	gid_t gid = inode->i_gid;
	struct buffer_head * bh;
	struct ext2_inode * raw_inode = ext2_get_inode(sb, ino, &bh);
	int n;
	int err = 0;

	if (IS_ERR(raw_inode))
 		return -EIO;

	/* For fields not not tracking in the in-memory inode,
	 * initialise them to zero for new inodes. */
	if (ei->i_state & EXT2_STATE_NEW)
		memset(raw_inode, 0, EXT2_SB(sb)->s_inode_size);

	raw_inode->i_mode = cpu_to_le16(inode->i_mode);
	if (!(test_opt(sb, NO_UID32))) {
		raw_inode->i_uid_low = cpu_to_le16(low_16_bits(uid));
		raw_inode->i_gid_low = cpu_to_le16(low_16_bits(gid));
/*
 * Fix up interoperability with old kernels. Otherwise, old inodes get
 * re-used with the upper 16 bits of the uid/gid intact
 */
		if (!ei->i_dtime) {
			raw_inode->i_uid_high = cpu_to_le16(high_16_bits(uid));
			raw_inode->i_gid_high = cpu_to_le16(high_16_bits(gid));
		} else {
			raw_inode->i_uid_high = 0;
			raw_inode->i_gid_high = 0;
		}
	} else {
		raw_inode->i_uid_low = cpu_to_le16(fs_high2lowuid(uid));
		raw_inode->i_gid_low = cpu_to_le16(fs_high2lowgid(gid));
		raw_inode->i_uid_high = 0;
		raw_inode->i_gid_high = 0;
	}
	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);
	raw_inode->i_size = cpu_to_le32(inode->i_size);
	raw_inode->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	raw_inode->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	raw_inode->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);

	raw_inode->i_blocks = cpu_to_le32(inode->i_blocks);
	raw_inode->i_dtime = cpu_to_le32(ei->i_dtime);
	raw_inode->i_flags = cpu_to_le32(ei->i_flags);
	raw_inode->i_faddr = cpu_to_le32(ei->i_faddr);
	raw_inode->i_frag = ei->i_frag_no;
	raw_inode->i_fsize = ei->i_frag_size;
	raw_inode->i_file_acl = cpu_to_le32(ei->i_file_acl);
	if (!S_ISREG(inode->i_mode))
		raw_inode->i_dir_acl = cpu_to_le32(ei->i_dir_acl);
	else {
		raw_inode->i_size_high = cpu_to_le32(inode->i_size >> 32);
		if (inode->i_size > 0x7fffffffULL) {
			if (!EXT2_HAS_RO_COMPAT_FEATURE(sb,
					EXT2_FEATURE_RO_COMPAT_LARGE_FILE) ||
			    EXT2_SB(sb)->s_es->s_rev_level ==
					cpu_to_le32(EXT2_GOOD_OLD_REV)) {
			       /* If this is the first large file
				* created, add a flag to the superblock.
				*/
				lock_kernel();
				ext2_update_dynamic_rev(sb);
				EXT2_SET_RO_COMPAT_FEATURE(sb,
					EXT2_FEATURE_RO_COMPAT_LARGE_FILE);
				unlock_kernel();
				ext2_write_super(sb);
			}
		}
	}
	
	raw_inode->i_generation = cpu_to_le32(inode->i_generation);
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		if (old_valid_dev(inode->i_rdev)) {
			raw_inode->i_block[0] =
				cpu_to_le32(old_encode_dev(inode->i_rdev));
			raw_inode->i_block[1] = 0;
		} else {
			raw_inode->i_block[0] = 0;
			raw_inode->i_block[1] =
				cpu_to_le32(new_encode_dev(inode->i_rdev));
			raw_inode->i_block[2] = 0;
		}
	} else for (n = 0; n < EXT2_N_BLOCKS; n++)
		raw_inode->i_block[n] = ei->i_data[n];
	mark_buffer_dirty(bh);
	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk ("IO error syncing ext2 inode [%s:%08lx]\n",
				sb->s_id, (unsigned long) ino);
			err = -EIO;
		}
	}
	ei->i_state &= ~EXT2_STATE_NEW;
	brelse (bh);
	return err;
}

int ext2_write_inode(struct inode *inode, int wait)
{
	return ext2_update_inode(inode, wait);
}

int ext2_sync_inode(struct inode *inode)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,	/* sys_fsync did this */
	};
	return sync_inode(inode, &wbc);
}

int ext2_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = inode_change_ok(inode, iattr);
	if (error)
		return error;
	if ((iattr->ia_valid & ATTR_UID && iattr->ia_uid != inode->i_uid) ||
	    (iattr->ia_valid & ATTR_GID && iattr->ia_gid != inode->i_gid)) {
		error = DQUOT_TRANSFER(inode, iattr) ? -EDQUOT : 0;
		if (error)
			return error;
	}
	error = inode_setattr(inode, iattr);
	if (!error && (iattr->ia_valid & ATTR_MODE))
		error = ext2_acl_chmod(inode);
	return error;
}
