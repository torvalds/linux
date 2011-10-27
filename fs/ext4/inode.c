/*
 *  linux/fs/ext4/inode.c
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
 *	(sct@redhat.com), 1993, 1998
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 *	(jj@sunsite.ms.mff.cuni.cz)
 *
 *  Assorted race fixes, rewrite of ext4_get_block() by Al Viro, 2000
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/jbd2.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/uio.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>

#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"
#include "ext4_extents.h"

#include <trace/events/ext4.h>

#define MPAGE_DA_EXTENT_TAIL 0x01

static inline int ext4_begin_ordered_truncate(struct inode *inode,
					      loff_t new_size)
{
	trace_ext4_begin_ordered_truncate(inode, new_size);
	/*
	 * If jinode is zero, then we never opened the file for
	 * writing, so there's no need to call
	 * jbd2_journal_begin_ordered_truncate() since there's no
	 * outstanding writes we need to flush.
	 */
	if (!EXT4_I(inode)->jinode)
		return 0;
	return jbd2_journal_begin_ordered_truncate(EXT4_JOURNAL(inode),
						   EXT4_I(inode)->jinode,
						   new_size);
}

static void ext4_invalidatepage(struct page *page, unsigned long offset);
static int noalloc_get_block_write(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create);
static int ext4_set_bh_endio(struct buffer_head *bh, struct inode *inode);
static void ext4_end_io_buffer_write(struct buffer_head *bh, int uptodate);
static int __ext4_journalled_writepage(struct page *page, unsigned int len);
static int ext4_bh_delay_or_unwritten(handle_t *handle, struct buffer_head *bh);

/*
 * Test whether an inode is a fast symlink.
 */
static int ext4_inode_is_fast_symlink(struct inode *inode)
{
	int ea_blocks = EXT4_I(inode)->i_file_acl ?
		(inode->i_sb->s_blocksize >> 9) : 0;

	return (S_ISLNK(inode->i_mode) && inode->i_blocks - ea_blocks == 0);
}

/*
 * Work out how many blocks we need to proceed with the next chunk of a
 * truncate transaction.
 */
static unsigned long blocks_for_truncate(struct inode *inode)
{
	ext4_lblk_t needed;

	needed = inode->i_blocks >> (inode->i_sb->s_blocksize_bits - 9);

	/* Give ourselves just enough room to cope with inodes in which
	 * i_blocks is corrupt: we've seen disk corruptions in the past
	 * which resulted in random data in an inode which looked enough
	 * like a regular file for ext4 to try to delete it.  Things
	 * will go a bit crazy if that happens, but at least we should
	 * try not to panic the whole kernel. */
	if (needed < 2)
		needed = 2;

	/* But we need to bound the transaction so we don't overflow the
	 * journal. */
	if (needed > EXT4_MAX_TRANS_DATA)
		needed = EXT4_MAX_TRANS_DATA;

	return EXT4_DATA_TRANS_BLOCKS(inode->i_sb) + needed;
}

/*
 * Truncate transactions can be complex and absolutely huge.  So we need to
 * be able to restart the transaction at a conventient checkpoint to make
 * sure we don't overflow the journal.
 *
 * start_transaction gets us a new handle for a truncate transaction,
 * and extend_transaction tries to extend the existing one a bit.  If
 * extend fails, we need to propagate the failure up and restart the
 * transaction in the top-level truncate loop. --sct
 */
static handle_t *start_transaction(struct inode *inode)
{
	handle_t *result;

	result = ext4_journal_start(inode, blocks_for_truncate(inode));
	if (!IS_ERR(result))
		return result;

	ext4_std_error(inode->i_sb, PTR_ERR(result));
	return result;
}

/*
 * Try to extend this transaction for the purposes of truncation.
 *
 * Returns 0 if we managed to create more room.  If we can't create more
 * room, and the transaction must be restarted we return 1.
 */
static int try_to_extend_transaction(handle_t *handle, struct inode *inode)
{
	if (!ext4_handle_valid(handle))
		return 0;
	if (ext4_handle_has_enough_credits(handle, EXT4_RESERVE_TRANS_BLOCKS+1))
		return 0;
	if (!ext4_journal_extend(handle, blocks_for_truncate(inode)))
		return 0;
	return 1;
}

/*
 * Restart the transaction associated with *handle.  This does a commit,
 * so before we call here everything must be consistently dirtied against
 * this transaction.
 */
int ext4_truncate_restart_trans(handle_t *handle, struct inode *inode,
				 int nblocks)
{
	int ret;

	/*
	 * Drop i_data_sem to avoid deadlock with ext4_map_blocks.  At this
	 * moment, get_block can be called only for blocks inside i_size since
	 * page cache has been already dropped and writes are blocked by
	 * i_mutex. So we can safely drop the i_data_sem here.
	 */
	BUG_ON(EXT4_JOURNAL(inode) == NULL);
	jbd_debug(2, "restarting handle %p\n", handle);
	up_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_journal_restart(handle, nblocks);
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_discard_preallocations(inode);

	return ret;
}

/*
 * Called at the last iput() if i_nlink is zero.
 */
void ext4_evict_inode(struct inode *inode)
{
	handle_t *handle;
	int err;

	trace_ext4_evict_inode(inode);

	mutex_lock(&inode->i_mutex);
	ext4_flush_completed_IO(inode);
	mutex_unlock(&inode->i_mutex);
	ext4_ioend_wait(inode);

	if (inode->i_nlink) {
		truncate_inode_pages(&inode->i_data, 0);
		goto no_delete;
	}

	if (!is_bad_inode(inode))
		dquot_initialize(inode);

	if (ext4_should_order_data(inode))
		ext4_begin_ordered_truncate(inode, 0);
	truncate_inode_pages(&inode->i_data, 0);

	if (is_bad_inode(inode))
		goto no_delete;

	handle = ext4_journal_start(inode, blocks_for_truncate(inode)+3);
	if (IS_ERR(handle)) {
		ext4_std_error(inode->i_sb, PTR_ERR(handle));
		/*
		 * If we're going to skip the normal cleanup, we still need to
		 * make sure that the in-core orphan linked list is properly
		 * cleaned up.
		 */
		ext4_orphan_del(NULL, inode);
		goto no_delete;
	}

	if (IS_SYNC(inode))
		ext4_handle_sync(handle);
	inode->i_size = 0;
	err = ext4_mark_inode_dirty(handle, inode);
	if (err) {
		ext4_warning(inode->i_sb,
			     "couldn't mark inode dirty (err %d)", err);
		goto stop_handle;
	}
	if (inode->i_blocks)
		ext4_truncate(inode);

	/*
	 * ext4_ext_truncate() doesn't reserve any slop when it
	 * restarts journal transactions; therefore there may not be
	 * enough credits left in the handle to remove the inode from
	 * the orphan list and set the dtime field.
	 */
	if (!ext4_handle_has_enough_credits(handle, 3)) {
		err = ext4_journal_extend(handle, 3);
		if (err > 0)
			err = ext4_journal_restart(handle, 3);
		if (err != 0) {
			ext4_warning(inode->i_sb,
				     "couldn't extend journal (err %d)", err);
		stop_handle:
			ext4_journal_stop(handle);
			ext4_orphan_del(NULL, inode);
			goto no_delete;
		}
	}

	/*
	 * Kill off the orphan record which ext4_truncate created.
	 * AKPM: I think this can be inside the above `if'.
	 * Note that ext4_orphan_del() has to be able to cope with the
	 * deletion of a non-existent orphan - this is because we don't
	 * know if ext4_truncate() actually created an orphan record.
	 * (Well, we could do this if we need to, but heck - it works)
	 */
	ext4_orphan_del(handle, inode);
	EXT4_I(inode)->i_dtime	= get_seconds();

	/*
	 * One subtle ordering requirement: if anything has gone wrong
	 * (transaction abort, IO errors, whatever), then we can still
	 * do these next steps (the fs will already have been marked as
	 * having errors), but we can't free the inode if the mark_dirty
	 * fails.
	 */
	if (ext4_mark_inode_dirty(handle, inode))
		/* If that failed, just do the required in-core inode clear. */
		ext4_clear_inode(inode);
	else
		ext4_free_inode(handle, inode);
	ext4_journal_stop(handle);
	return;
no_delete:
	ext4_clear_inode(inode);	/* We must guarantee clearing of inode... */
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

/**
 *	ext4_block_to_path - parse the block number into array of offsets
 *	@inode: inode in question (we are only interested in its superblock)
 *	@i_block: block number to be parsed
 *	@offsets: array to store the offsets in
 *	@boundary: set this non-zero if the referred-to block is likely to be
 *	       followed (on disk) by an indirect block.
 *
 *	To store the locations of file's data ext4 uses a data structure common
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

static int ext4_block_to_path(struct inode *inode,
			      ext4_lblk_t i_block,
			      ext4_lblk_t offsets[4], int *boundary)
{
	int ptrs = EXT4_ADDR_PER_BLOCK(inode->i_sb);
	int ptrs_bits = EXT4_ADDR_PER_BLOCK_BITS(inode->i_sb);
	const long direct_blocks = EXT4_NDIR_BLOCKS,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2));
	int n = 0;
	int final = 0;

	if (i_block < direct_blocks) {
		offsets[n++] = i_block;
		final = direct_blocks;
	} else if ((i_block -= direct_blocks) < indirect_blocks) {
		offsets[n++] = EXT4_IND_BLOCK;
		offsets[n++] = i_block;
		final = ptrs;
	} else if ((i_block -= indirect_blocks) < double_blocks) {
		offsets[n++] = EXT4_DIND_BLOCK;
		offsets[n++] = i_block >> ptrs_bits;
		offsets[n++] = i_block & (ptrs - 1);
		final = ptrs;
	} else if (((i_block -= double_blocks) >> (ptrs_bits * 2)) < ptrs) {
		offsets[n++] = EXT4_TIND_BLOCK;
		offsets[n++] = i_block >> (ptrs_bits * 2);
		offsets[n++] = (i_block >> ptrs_bits) & (ptrs - 1);
		offsets[n++] = i_block & (ptrs - 1);
		final = ptrs;
	} else {
		ext4_warning(inode->i_sb, "block %lu > max in inode %lu",
			     i_block + direct_blocks +
			     indirect_blocks + double_blocks, inode->i_ino);
	}
	if (boundary)
		*boundary = final - 1 - (i_block & (ptrs - 1));
	return n;
}

static int __ext4_check_blockref(const char *function, unsigned int line,
				 struct inode *inode,
				 __le32 *p, unsigned int max)
{
	struct ext4_super_block *es = EXT4_SB(inode->i_sb)->s_es;
	__le32 *bref = p;
	unsigned int blk;

	while (bref < p+max) {
		blk = le32_to_cpu(*bref++);
		if (blk &&
		    unlikely(!ext4_data_block_valid(EXT4_SB(inode->i_sb),
						    blk, 1))) {
			es->s_last_error_block = cpu_to_le64(blk);
			ext4_error_inode(inode, function, line, blk,
					 "invalid block");
			return -EIO;
		}
	}
	return 0;
}


#define ext4_check_indirect_blockref(inode, bh)                         \
	__ext4_check_blockref(__func__, __LINE__, inode,		\
			      (__le32 *)(bh)->b_data,			\
			      EXT4_ADDR_PER_BLOCK((inode)->i_sb))

#define ext4_check_inode_blockref(inode)                                \
	__ext4_check_blockref(__func__, __LINE__, inode,		\
			      EXT4_I(inode)->i_data,			\
			      EXT4_NDIR_BLOCKS)

/**
 *	ext4_get_branch - read the chain of indirect blocks leading to data
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
 *	or when it reads all @depth-1 indirect blocks successfully and finds
 *	the whole chain, all way to the data (returns %NULL, *err == 0).
 *
 *      Need to be called with
 *      down_read(&EXT4_I(inode)->i_data_sem)
 */
static Indirect *ext4_get_branch(struct inode *inode, int depth,
				 ext4_lblk_t  *offsets,
				 Indirect chain[4], int *err)
{
	struct super_block *sb = inode->i_sb;
	Indirect *p = chain;
	struct buffer_head *bh;

	*err = 0;
	/* i_data is not going away, no lock needed */
	add_chain(chain, NULL, EXT4_I(inode)->i_data + *offsets);
	if (!p->key)
		goto no_block;
	while (--depth) {
		bh = sb_getblk(sb, le32_to_cpu(p->key));
		if (unlikely(!bh))
			goto failure;

		if (!bh_uptodate_or_lock(bh)) {
			if (bh_submit_read(bh) < 0) {
				put_bh(bh);
				goto failure;
			}
			/* validate block references */
			if (ext4_check_indirect_blockref(inode, bh)) {
				put_bh(bh);
				goto failure;
			}
		}

		add_chain(++p, bh, (__le32 *)bh->b_data + *++offsets);
		/* Reader: end */
		if (!p->key)
			goto no_block;
	}
	return NULL;

failure:
	*err = -EIO;
no_block:
	return p;
}

/**
 *	ext4_find_near - find a place for allocation with sufficient locality
 *	@inode: owner
 *	@ind: descriptor of indirect block.
 *
 *	This function returns the preferred place for block allocation.
 *	It is used when heuristic for sequential allocation fails.
 *	Rules are:
 *	  + if there is a block to the left of our position - allocate near it.
 *	  + if pointer will live in indirect block - allocate near that block.
 *	  + if pointer will live in inode - allocate in the same
 *	    cylinder group.
 *
 * In the latter case we colour the starting block by the callers PID to
 * prevent it from clashing with concurrent allocations for a different inode
 * in the same block group.   The PID is used here so that functionally related
 * files will be close-by on-disk.
 *
 *	Caller must make sure that @ind is valid and will stay that way.
 */
static ext4_fsblk_t ext4_find_near(struct inode *inode, Indirect *ind)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	__le32 *start = ind->bh ? (__le32 *) ind->bh->b_data : ei->i_data;
	__le32 *p;
	ext4_fsblk_t bg_start;
	ext4_fsblk_t last_block;
	ext4_grpblk_t colour;
	ext4_group_t block_group;
	int flex_size = ext4_flex_bg_size(EXT4_SB(inode->i_sb));

	/* Try to find previous block */
	for (p = ind->p - 1; p >= start; p--) {
		if (*p)
			return le32_to_cpu(*p);
	}

	/* No such thing, so let's try location of indirect block */
	if (ind->bh)
		return ind->bh->b_blocknr;

	/*
	 * It is going to be referred to from the inode itself? OK, just put it
	 * into the same cylinder group then.
	 */
	block_group = ei->i_block_group;
	if (flex_size >= EXT4_FLEX_SIZE_DIR_ALLOC_SCHEME) {
		block_group &= ~(flex_size-1);
		if (S_ISREG(inode->i_mode))
			block_group++;
	}
	bg_start = ext4_group_first_block_no(inode->i_sb, block_group);
	last_block = ext4_blocks_count(EXT4_SB(inode->i_sb)->s_es) - 1;

	/*
	 * If we are doing delayed allocation, we don't need take
	 * colour into account.
	 */
	if (test_opt(inode->i_sb, DELALLOC))
		return bg_start;

	if (bg_start + EXT4_BLOCKS_PER_GROUP(inode->i_sb) <= last_block)
		colour = (current->pid % 16) *
			(EXT4_BLOCKS_PER_GROUP(inode->i_sb) / 16);
	else
		colour = (current->pid % 16) * ((last_block - bg_start) / 16);
	return bg_start + colour;
}

/**
 *	ext4_find_goal - find a preferred place for allocation.
 *	@inode: owner
 *	@block:  block we want
 *	@partial: pointer to the last triple within a chain
 *
 *	Normally this function find the preferred place for block allocation,
 *	returns it.
 *	Because this is only used for non-extent files, we limit the block nr
 *	to 32 bits.
 */
static ext4_fsblk_t ext4_find_goal(struct inode *inode, ext4_lblk_t block,
				   Indirect *partial)
{
	ext4_fsblk_t goal;

	/*
	 * XXX need to get goal block from mballoc's data structures
	 */

	goal = ext4_find_near(inode, partial);
	goal = goal & EXT4_MAX_BLOCK_FILE_PHYS;
	return goal;
}

/**
 *	ext4_blks_to_allocate - Look up the block map and count the number
 *	of direct blocks need to be allocated for the given branch.
 *
 *	@branch: chain of indirect blocks
 *	@k: number of blocks need for indirect blocks
 *	@blks: number of data blocks to be mapped.
 *	@blocks_to_boundary:  the offset in the indirect block
 *
 *	return the total number of blocks to be allocate, including the
 *	direct and indirect blocks.
 */
static int ext4_blks_to_allocate(Indirect *branch, int k, unsigned int blks,
				 int blocks_to_boundary)
{
	unsigned int count = 0;

	/*
	 * Simple case, [t,d]Indirect block(s) has not allocated yet
	 * then it's clear blocks on that path have not allocated
	 */
	if (k > 0) {
		/* right now we don't handle cross boundary allocation */
		if (blks < blocks_to_boundary + 1)
			count += blks;
		else
			count += blocks_to_boundary + 1;
		return count;
	}

	count++;
	while (count < blks && count <= blocks_to_boundary &&
		le32_to_cpu(*(branch[0].p + count)) == 0) {
		count++;
	}
	return count;
}

/**
 *	ext4_alloc_blocks: multiple allocate blocks needed for a branch
 *	@handle: handle for this transaction
 *	@inode: inode which needs allocated blocks
 *	@iblock: the logical block to start allocated at
 *	@goal: preferred physical block of allocation
 *	@indirect_blks: the number of blocks need to allocate for indirect
 *			blocks
 *	@blks: number of desired blocks
 *	@new_blocks: on return it will store the new block numbers for
 *	the indirect blocks(if needed) and the first direct block,
 *	@err: on return it will store the error code
 *
 *	This function will return the number of blocks allocated as
 *	requested by the passed-in parameters.
 */
static int ext4_alloc_blocks(handle_t *handle, struct inode *inode,
			     ext4_lblk_t iblock, ext4_fsblk_t goal,
			     int indirect_blks, int blks,
			     ext4_fsblk_t new_blocks[4], int *err)
{
	struct ext4_allocation_request ar;
	int target, i;
	unsigned long count = 0, blk_allocated = 0;
	int index = 0;
	ext4_fsblk_t current_block = 0;
	int ret = 0;

	/*
	 * Here we try to allocate the requested multiple blocks at once,
	 * on a best-effort basis.
	 * To build a branch, we should allocate blocks for
	 * the indirect blocks(if not allocated yet), and at least
	 * the first direct block of this branch.  That's the
	 * minimum number of blocks need to allocate(required)
	 */
	/* first we try to allocate the indirect blocks */
	target = indirect_blks;
	while (target > 0) {
		count = target;
		/* allocating blocks for indirect blocks and direct blocks */
		current_block = ext4_new_meta_blocks(handle, inode, goal,
						     0, &count, err);
		if (*err)
			goto failed_out;

		if (unlikely(current_block + count > EXT4_MAX_BLOCK_FILE_PHYS)) {
			EXT4_ERROR_INODE(inode,
					 "current_block %llu + count %lu > %d!",
					 current_block, count,
					 EXT4_MAX_BLOCK_FILE_PHYS);
			*err = -EIO;
			goto failed_out;
		}

		target -= count;
		/* allocate blocks for indirect blocks */
		while (index < indirect_blks && count) {
			new_blocks[index++] = current_block++;
			count--;
		}
		if (count > 0) {
			/*
			 * save the new block number
			 * for the first direct block
			 */
			new_blocks[index] = current_block;
			printk(KERN_INFO "%s returned more blocks than "
						"requested\n", __func__);
			WARN_ON(1);
			break;
		}
	}

	target = blks - count ;
	blk_allocated = count;
	if (!target)
		goto allocated;
	/* Now allocate data blocks */
	memset(&ar, 0, sizeof(ar));
	ar.inode = inode;
	ar.goal = goal;
	ar.len = target;
	ar.logical = iblock;
	if (S_ISREG(inode->i_mode))
		/* enable in-core preallocation only for regular files */
		ar.flags = EXT4_MB_HINT_DATA;

	current_block = ext4_mb_new_blocks(handle, &ar, err);
	if (unlikely(current_block + ar.len > EXT4_MAX_BLOCK_FILE_PHYS)) {
		EXT4_ERROR_INODE(inode,
				 "current_block %llu + ar.len %d > %d!",
				 current_block, ar.len,
				 EXT4_MAX_BLOCK_FILE_PHYS);
		*err = -EIO;
		goto failed_out;
	}

	if (*err && (target == blks)) {
		/*
		 * if the allocation failed and we didn't allocate
		 * any blocks before
		 */
		goto failed_out;
	}
	if (!*err) {
		if (target == blks) {
			/*
			 * save the new block number
			 * for the first direct block
			 */
			new_blocks[index] = current_block;
		}
		blk_allocated += ar.len;
	}
allocated:
	/* total number of blocks allocated for direct blocks */
	ret = blk_allocated;
	*err = 0;
	return ret;
failed_out:
	for (i = 0; i < index; i++)
		ext4_free_blocks(handle, inode, NULL, new_blocks[i], 1, 0);
	return ret;
}

/**
 *	ext4_alloc_branch - allocate and set up a chain of blocks.
 *	@handle: handle for this transaction
 *	@inode: owner
 *	@indirect_blks: number of allocated indirect blocks
 *	@blks: number of allocated direct blocks
 *	@goal: preferred place for allocation
 *	@offsets: offsets (in the blocks) to store the pointers to next.
 *	@branch: place to store the chain in.
 *
 *	This function allocates blocks, zeroes out all but the last one,
 *	links them into chain and (if we are synchronous) writes them to disk.
 *	In other words, it prepares a branch that can be spliced onto the
 *	inode. It stores the information about that chain in the branch[], in
 *	the same format as ext4_get_branch() would do. We are calling it after
 *	we had read the existing part of chain and partial points to the last
 *	triple of that (one with zero ->key). Upon the exit we have the same
 *	picture as after the successful ext4_get_block(), except that in one
 *	place chain is disconnected - *branch->p is still zero (we did not
 *	set the last link), but branch->key contains the number that should
 *	be placed into *branch->p to fill that gap.
 *
 *	If allocation fails we free all blocks we've allocated (and forget
 *	their buffer_heads) and return the error value the from failed
 *	ext4_alloc_block() (normally -ENOSPC). Otherwise we set the chain
 *	as described above and return 0.
 */
static int ext4_alloc_branch(handle_t *handle, struct inode *inode,
			     ext4_lblk_t iblock, int indirect_blks,
			     int *blks, ext4_fsblk_t goal,
			     ext4_lblk_t *offsets, Indirect *branch)
{
	int blocksize = inode->i_sb->s_blocksize;
	int i, n = 0;
	int err = 0;
	struct buffer_head *bh;
	int num;
	ext4_fsblk_t new_blocks[4];
	ext4_fsblk_t current_block;

	num = ext4_alloc_blocks(handle, inode, iblock, goal, indirect_blks,
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
		bh = sb_getblk(inode->i_sb, new_blocks[n-1]);
		if (unlikely(!bh)) {
			err = -EIO;
			goto failed;
		}

		branch[n].bh = bh;
		lock_buffer(bh);
		BUFFER_TRACE(bh, "call get_create_access");
		err = ext4_journal_get_create_access(handle, bh);
		if (err) {
			/* Don't brelse(bh) here; it's done in
			 * ext4_journal_forget() below */
			unlock_buffer(bh);
			goto failed;
		}

		memset(bh->b_data, 0, blocksize);
		branch[n].p = (__le32 *) bh->b_data + offsets[n];
		branch[n].key = cpu_to_le32(new_blocks[n]);
		*branch[n].p = branch[n].key;
		if (n == indirect_blks) {
			current_block = new_blocks[n];
			/*
			 * End of chain, update the last new metablock of
			 * the chain to point to the new allocated
			 * data blocks numbers
			 */
			for (i = 1; i < num; i++)
				*(branch[n].p + i) = cpu_to_le32(++current_block);
		}
		BUFFER_TRACE(bh, "marking uptodate");
		set_buffer_uptodate(bh);
		unlock_buffer(bh);

		BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
		err = ext4_handle_dirty_metadata(handle, inode, bh);
		if (err)
			goto failed;
	}
	*blks = num;
	return err;
failed:
	/* Allocation failed, free what we already allocated */
	ext4_free_blocks(handle, inode, NULL, new_blocks[0], 1, 0);
	for (i = 1; i <= n ; i++) {
		/*
		 * branch[i].bh is newly allocated, so there is no
		 * need to revoke the block, which is why we don't
		 * need to set EXT4_FREE_BLOCKS_METADATA.
		 */
		ext4_free_blocks(handle, inode, NULL, new_blocks[i], 1,
				 EXT4_FREE_BLOCKS_FORGET);
	}
	for (i = n+1; i < indirect_blks; i++)
		ext4_free_blocks(handle, inode, NULL, new_blocks[i], 1, 0);

	ext4_free_blocks(handle, inode, NULL, new_blocks[i], num, 0);

	return err;
}

/**
 * ext4_splice_branch - splice the allocated branch onto inode.
 * @handle: handle for this transaction
 * @inode: owner
 * @block: (logical) number of block we are adding
 * @chain: chain of indirect blocks (with a missing link - see
 *	ext4_alloc_branch)
 * @where: location of missing link
 * @num:   number of indirect blocks we are adding
 * @blks:  number of direct blocks we are adding
 *
 * This function fills the missing link and does all housekeeping needed in
 * inode (->i_blocks, etc.). In case of success we end up with the full
 * chain to new block and return 0.
 */
static int ext4_splice_branch(handle_t *handle, struct inode *inode,
			      ext4_lblk_t block, Indirect *where, int num,
			      int blks)
{
	int i;
	int err = 0;
	ext4_fsblk_t current_block;

	/*
	 * If we're splicing into a [td]indirect block (as opposed to the
	 * inode) then we need to get write access to the [td]indirect block
	 * before the splice.
	 */
	if (where->bh) {
		BUFFER_TRACE(where->bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, where->bh);
		if (err)
			goto err_out;
	}
	/* That's it */

	*where->p = where->key;

	/*
	 * Update the host buffer_head or inode to point to more just allocated
	 * direct blocks blocks
	 */
	if (num == 0 && blks > 1) {
		current_block = le32_to_cpu(where->key) + 1;
		for (i = 1; i < blks; i++)
			*(where->p + i) = cpu_to_le32(current_block++);
	}

	/* We are done with atomic stuff, now do the rest of housekeeping */
	/* had we spliced it onto indirect block? */
	if (where->bh) {
		/*
		 * If we spliced it onto an indirect block, we haven't
		 * altered the inode.  Note however that if it is being spliced
		 * onto an indirect block at the very end of the file (the
		 * file is growing) then we *will* alter the inode to reflect
		 * the new i_size.  But that is not done here - it is done in
		 * generic_commit_write->__mark_inode_dirty->ext4_dirty_inode.
		 */
		jbd_debug(5, "splicing indirect only\n");
		BUFFER_TRACE(where->bh, "call ext4_handle_dirty_metadata");
		err = ext4_handle_dirty_metadata(handle, inode, where->bh);
		if (err)
			goto err_out;
	} else {
		/*
		 * OK, we spliced it into the inode itself on a direct block.
		 */
		ext4_mark_inode_dirty(handle, inode);
		jbd_debug(5, "splicing direct\n");
	}
	return err;

err_out:
	for (i = 1; i <= num; i++) {
		/*
		 * branch[i].bh is newly allocated, so there is no
		 * need to revoke the block, which is why we don't
		 * need to set EXT4_FREE_BLOCKS_METADATA.
		 */
		ext4_free_blocks(handle, inode, where[i].bh, 0, 1,
				 EXT4_FREE_BLOCKS_FORGET);
	}
	ext4_free_blocks(handle, inode, NULL, le32_to_cpu(where[num].key),
			 blks, 0);

	return err;
}

/*
 * The ext4_ind_map_blocks() function handles non-extents inodes
 * (i.e., using the traditional indirect/double-indirect i_blocks
 * scheme) for ext4_map_blocks().
 *
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
 *
 * `handle' can be NULL if create == 0.
 *
 * return > 0, # of blocks mapped or allocated.
 * return = 0, if plain lookup failed.
 * return < 0, error case.
 *
 * The ext4_ind_get_blocks() function should be called with
 * down_write(&EXT4_I(inode)->i_data_sem) if allocating filesystem
 * blocks (i.e., flags has EXT4_GET_BLOCKS_CREATE set) or
 * down_read(&EXT4_I(inode)->i_data_sem) if not allocating file system
 * blocks.
 */
static int ext4_ind_map_blocks(handle_t *handle, struct inode *inode,
			       struct ext4_map_blocks *map,
			       int flags)
{
	int err = -EIO;
	ext4_lblk_t offsets[4];
	Indirect chain[4];
	Indirect *partial;
	ext4_fsblk_t goal;
	int indirect_blks;
	int blocks_to_boundary = 0;
	int depth;
	int count = 0;
	ext4_fsblk_t first_block = 0;

	trace_ext4_ind_map_blocks_enter(inode, map->m_lblk, map->m_len, flags);
	J_ASSERT(!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)));
	J_ASSERT(handle != NULL || (flags & EXT4_GET_BLOCKS_CREATE) == 0);
	depth = ext4_block_to_path(inode, map->m_lblk, offsets,
				   &blocks_to_boundary);

	if (depth == 0)
		goto out;

	partial = ext4_get_branch(inode, depth, offsets, chain, &err);

	/* Simplest case - block found, no allocation needed */
	if (!partial) {
		first_block = le32_to_cpu(chain[depth - 1].key);
		count++;
		/*map more blocks*/
		while (count < map->m_len && count <= blocks_to_boundary) {
			ext4_fsblk_t blk;

			blk = le32_to_cpu(*(chain[depth-1].p + count));

			if (blk == first_block + count)
				count++;
			else
				break;
		}
		goto got_it;
	}

	/* Next simple case - plain lookup or failed read of indirect block */
	if ((flags & EXT4_GET_BLOCKS_CREATE) == 0 || err == -EIO)
		goto cleanup;

	/*
	 * Okay, we need to do block allocation.
	*/
	goal = ext4_find_goal(inode, map->m_lblk, partial);

	/* the number of blocks need to allocate for [d,t]indirect blocks */
	indirect_blks = (chain + depth) - partial - 1;

	/*
	 * Next look up the indirect map to count the totoal number of
	 * direct blocks to allocate for this branch.
	 */
	count = ext4_blks_to_allocate(partial, indirect_blks,
				      map->m_len, blocks_to_boundary);
	/*
	 * Block out ext4_truncate while we alter the tree
	 */
	err = ext4_alloc_branch(handle, inode, map->m_lblk, indirect_blks,
				&count, goal,
				offsets + (partial - chain), partial);

	/*
	 * The ext4_splice_branch call will free and forget any buffers
	 * on the new chain if there is a failure, but that risks using
	 * up transaction credits, especially for bitmaps where the
	 * credits cannot be returned.  Can we handle this somehow?  We
	 * may need to return -EAGAIN upwards in the worst case.  --sct
	 */
	if (!err)
		err = ext4_splice_branch(handle, inode, map->m_lblk,
					 partial, indirect_blks, count);
	if (err)
		goto cleanup;

	map->m_flags |= EXT4_MAP_NEW;

	ext4_update_inode_fsync_trans(handle, inode, 1);
got_it:
	map->m_flags |= EXT4_MAP_MAPPED;
	map->m_pblk = le32_to_cpu(chain[depth-1].key);
	map->m_len = count;
	if (count > blocks_to_boundary)
		map->m_flags |= EXT4_MAP_BOUNDARY;
	err = count;
	/* Clean up and exit */
	partial = chain + depth - 1;	/* the whole chain */
cleanup:
	while (partial > chain) {
		BUFFER_TRACE(partial->bh, "call brelse");
		brelse(partial->bh);
		partial--;
	}
out:
	trace_ext4_ind_map_blocks_exit(inode, map->m_lblk,
				map->m_pblk, map->m_len, err);
	return err;
}

#ifdef CONFIG_QUOTA
qsize_t *ext4_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}
#endif

/*
 * Calculate the number of metadata blocks need to reserve
 * to allocate a new block at @lblocks for non extent file based file
 */
static int ext4_indirect_calc_metadata_amount(struct inode *inode,
					      sector_t lblock)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	sector_t dind_mask = ~((sector_t)EXT4_ADDR_PER_BLOCK(inode->i_sb) - 1);
	int blk_bits;

	if (lblock < EXT4_NDIR_BLOCKS)
		return 0;

	lblock -= EXT4_NDIR_BLOCKS;

	if (ei->i_da_metadata_calc_len &&
	    (lblock & dind_mask) == ei->i_da_metadata_calc_last_lblock) {
		ei->i_da_metadata_calc_len++;
		return 0;
	}
	ei->i_da_metadata_calc_last_lblock = lblock & dind_mask;
	ei->i_da_metadata_calc_len = 1;
	blk_bits = order_base_2(lblock);
	return (blk_bits / EXT4_ADDR_PER_BLOCK_BITS(inode->i_sb)) + 1;
}

/*
 * Calculate the number of metadata blocks need to reserve
 * to allocate a block located at @lblock
 */
static int ext4_calc_metadata_amount(struct inode *inode, ext4_lblk_t lblock)
{
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		return ext4_ext_calc_metadata_amount(inode, lblock);

	return ext4_indirect_calc_metadata_amount(inode, lblock);
}

/*
 * Called with i_data_sem down, which is important since we can call
 * ext4_discard_preallocations() from here.
 */
void ext4_da_update_reserve_space(struct inode *inode,
					int used, int quota_claim)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);

	spin_lock(&ei->i_block_reservation_lock);
	trace_ext4_da_update_reserve_space(inode, used);
	if (unlikely(used > ei->i_reserved_data_blocks)) {
		ext4_msg(inode->i_sb, KERN_NOTICE, "%s: ino %lu, used %d "
			 "with only %d reserved data blocks\n",
			 __func__, inode->i_ino, used,
			 ei->i_reserved_data_blocks);
		WARN_ON(1);
		used = ei->i_reserved_data_blocks;
	}

	/* Update per-inode reservations */
	ei->i_reserved_data_blocks -= used;
	ei->i_reserved_meta_blocks -= ei->i_allocated_meta_blocks;
	percpu_counter_sub(&sbi->s_dirtyblocks_counter,
			   used + ei->i_allocated_meta_blocks);
	ei->i_allocated_meta_blocks = 0;

	if (ei->i_reserved_data_blocks == 0) {
		/*
		 * We can release all of the reserved metadata blocks
		 * only when we have written all of the delayed
		 * allocation blocks.
		 */
		percpu_counter_sub(&sbi->s_dirtyblocks_counter,
				   ei->i_reserved_meta_blocks);
		ei->i_reserved_meta_blocks = 0;
		ei->i_da_metadata_calc_len = 0;
	}
	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

	/* Update quota subsystem for data blocks */
	if (quota_claim)
		dquot_claim_block(inode, used);
	else {
		/*
		 * We did fallocate with an offset that is already delayed
		 * allocated. So on delayed allocated writeback we should
		 * not re-claim the quota for fallocated blocks.
		 */
		dquot_release_reservation_block(inode, used);
	}

	/*
	 * If we have done all the pending block allocations and if
	 * there aren't any writers on the inode, we can discard the
	 * inode's preallocations.
	 */
	if ((ei->i_reserved_data_blocks == 0) &&
	    (atomic_read(&inode->i_writecount) == 0))
		ext4_discard_preallocations(inode);
}

static int __check_block_validity(struct inode *inode, const char *func,
				unsigned int line,
				struct ext4_map_blocks *map)
{
	if (!ext4_data_block_valid(EXT4_SB(inode->i_sb), map->m_pblk,
				   map->m_len)) {
		ext4_error_inode(inode, func, line, map->m_pblk,
				 "lblock %lu mapped to illegal pblock "
				 "(length %d)", (unsigned long) map->m_lblk,
				 map->m_len);
		return -EIO;
	}
	return 0;
}

#define check_block_validity(inode, map)	\
	__check_block_validity((inode), __func__, __LINE__, (map))

/*
 * Return the number of contiguous dirty pages in a given inode
 * starting at page frame idx.
 */
static pgoff_t ext4_num_dirty_pages(struct inode *inode, pgoff_t idx,
				    unsigned int max_pages)
{
	struct address_space *mapping = inode->i_mapping;
	pgoff_t	index;
	struct pagevec pvec;
	pgoff_t num = 0;
	int i, nr_pages, done = 0;

	if (max_pages == 0)
		return 0;
	pagevec_init(&pvec, 0);
	while (!done) {
		index = idx;
		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
					      PAGECACHE_TAG_DIRTY,
					      (pgoff_t)PAGEVEC_SIZE);
		if (nr_pages == 0)
			break;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			struct buffer_head *bh, *head;

			lock_page(page);
			if (unlikely(page->mapping != mapping) ||
			    !PageDirty(page) ||
			    PageWriteback(page) ||
			    page->index != idx) {
				done = 1;
				unlock_page(page);
				break;
			}
			if (page_has_buffers(page)) {
				bh = head = page_buffers(page);
				do {
					if (!buffer_delay(bh) &&
					    !buffer_unwritten(bh))
						done = 1;
					bh = bh->b_this_page;
				} while (!done && (bh != head));
			}
			unlock_page(page);
			if (done)
				break;
			idx++;
			num++;
			if (num >= max_pages) {
				done = 1;
				break;
			}
		}
		pagevec_release(&pvec);
	}
	return num;
}

/*
 * The ext4_map_blocks() function tries to look up the requested blocks,
 * and returns if the blocks are already mapped.
 *
 * Otherwise it takes the write lock of the i_data_sem and allocate blocks
 * and store the allocated blocks in the result buffer head and mark it
 * mapped.
 *
 * If file type is extents based, it will call ext4_ext_map_blocks(),
 * Otherwise, call with ext4_ind_map_blocks() to handle indirect mapping
 * based files
 *
 * On success, it returns the number of blocks being mapped or allocate.
 * if create==0 and the blocks are pre-allocated and uninitialized block,
 * the result buffer head is unmapped. If the create ==1, it will make sure
 * the buffer head is mapped.
 *
 * It returns 0 if plain look up failed (blocks have not been allocated), in
 * that casem, buffer head is unmapped
 *
 * It returns the error in case of allocation failure.
 */
int ext4_map_blocks(handle_t *handle, struct inode *inode,
		    struct ext4_map_blocks *map, int flags)
{
	int retval;

	map->m_flags = 0;
	ext_debug("ext4_map_blocks(): inode %lu, flag %d, max_blocks %u,"
		  "logical block %lu\n", inode->i_ino, flags, map->m_len,
		  (unsigned long) map->m_lblk);
	/*
	 * Try to see if we can get the block without requesting a new
	 * file system block.
	 */
	down_read((&EXT4_I(inode)->i_data_sem));
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		retval = ext4_ext_map_blocks(handle, inode, map, 0);
	} else {
		retval = ext4_ind_map_blocks(handle, inode, map, 0);
	}
	up_read((&EXT4_I(inode)->i_data_sem));

	if (retval > 0 && map->m_flags & EXT4_MAP_MAPPED) {
		int ret = check_block_validity(inode, map);
		if (ret != 0)
			return ret;
	}

	/* If it is only a block(s) look up */
	if ((flags & EXT4_GET_BLOCKS_CREATE) == 0)
		return retval;

	/*
	 * Returns if the blocks have already allocated
	 *
	 * Note that if blocks have been preallocated
	 * ext4_ext_get_block() returns th create = 0
	 * with buffer head unmapped.
	 */
	if (retval > 0 && map->m_flags & EXT4_MAP_MAPPED)
		return retval;

	/*
	 * When we call get_blocks without the create flag, the
	 * BH_Unwritten flag could have gotten set if the blocks
	 * requested were part of a uninitialized extent.  We need to
	 * clear this flag now that we are committed to convert all or
	 * part of the uninitialized extent to be an initialized
	 * extent.  This is because we need to avoid the combination
	 * of BH_Unwritten and BH_Mapped flags being simultaneously
	 * set on the buffer_head.
	 */
	map->m_flags &= ~EXT4_MAP_UNWRITTEN;

	/*
	 * New blocks allocate and/or writing to uninitialized extent
	 * will possibly result in updating i_data, so we take
	 * the write lock of i_data_sem, and call get_blocks()
	 * with create == 1 flag.
	 */
	down_write((&EXT4_I(inode)->i_data_sem));

	/*
	 * if the caller is from delayed allocation writeout path
	 * we have already reserved fs blocks for allocation
	 * let the underlying get_block() function know to
	 * avoid double accounting
	 */
	if (flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE)
		ext4_set_inode_state(inode, EXT4_STATE_DELALLOC_RESERVED);
	/*
	 * We need to check for EXT4 here because migrate
	 * could have changed the inode type in between
	 */
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		retval = ext4_ext_map_blocks(handle, inode, map, flags);
	} else {
		retval = ext4_ind_map_blocks(handle, inode, map, flags);

		if (retval > 0 && map->m_flags & EXT4_MAP_NEW) {
			/*
			 * We allocated new blocks which will result in
			 * i_data's format changing.  Force the migrate
			 * to fail by clearing migrate flags
			 */
			ext4_clear_inode_state(inode, EXT4_STATE_EXT_MIGRATE);
		}

		/*
		 * Update reserved blocks/metadata blocks after successful
		 * block allocation which had been deferred till now. We don't
		 * support fallocate for non extent files. So we can update
		 * reserve space here.
		 */
		if ((retval > 0) &&
			(flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE))
			ext4_da_update_reserve_space(inode, retval, 1);
	}
	if (flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE)
		ext4_clear_inode_state(inode, EXT4_STATE_DELALLOC_RESERVED);

	up_write((&EXT4_I(inode)->i_data_sem));
	if (retval > 0 && map->m_flags & EXT4_MAP_MAPPED) {
		int ret = check_block_validity(inode, map);
		if (ret != 0)
			return ret;
	}
	return retval;
}

/* Maximum number of blocks we map for direct IO at once. */
#define DIO_MAX_BLOCKS 4096

static int _ext4_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int flags)
{
	handle_t *handle = ext4_journal_current_handle();
	struct ext4_map_blocks map;
	int ret = 0, started = 0;
	int dio_credits;

	map.m_lblk = iblock;
	map.m_len = bh->b_size >> inode->i_blkbits;

	if (flags && !handle) {
		/* Direct IO write... */
		if (map.m_len > DIO_MAX_BLOCKS)
			map.m_len = DIO_MAX_BLOCKS;
		dio_credits = ext4_chunk_trans_blocks(inode, map.m_len);
		handle = ext4_journal_start(inode, dio_credits);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			return ret;
		}
		started = 1;
	}

	ret = ext4_map_blocks(handle, inode, &map, flags);
	if (ret > 0) {
		map_bh(bh, inode->i_sb, map.m_pblk);
		bh->b_state = (bh->b_state & ~EXT4_MAP_FLAGS) | map.m_flags;
		bh->b_size = inode->i_sb->s_blocksize * map.m_len;
		ret = 0;
	}
	if (started)
		ext4_journal_stop(handle);
	return ret;
}

int ext4_get_block(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh, int create)
{
	return _ext4_get_block(inode, iblock, bh,
			       create ? EXT4_GET_BLOCKS_CREATE : 0);
}

/*
 * `handle' can be NULL if create is zero
 */
struct buffer_head *ext4_getblk(handle_t *handle, struct inode *inode,
				ext4_lblk_t block, int create, int *errp)
{
	struct ext4_map_blocks map;
	struct buffer_head *bh;
	int fatal = 0, err;

	J_ASSERT(handle != NULL || create == 0);

	map.m_lblk = block;
	map.m_len = 1;
	err = ext4_map_blocks(handle, inode, &map,
			      create ? EXT4_GET_BLOCKS_CREATE : 0);

	if (err < 0)
		*errp = err;
	if (err <= 0)
		return NULL;
	*errp = 0;

	bh = sb_getblk(inode->i_sb, map.m_pblk);
	if (!bh) {
		*errp = -EIO;
		return NULL;
	}
	if (map.m_flags & EXT4_MAP_NEW) {
		J_ASSERT(create != 0);
		J_ASSERT(handle != NULL);

		/*
		 * Now that we do not always journal data, we should
		 * keep in mind whether this should always journal the
		 * new buffer as metadata.  For now, regular file
		 * writes use ext4_get_block instead, so it's not a
		 * problem.
		 */
		lock_buffer(bh);
		BUFFER_TRACE(bh, "call get_create_access");
		fatal = ext4_journal_get_create_access(handle, bh);
		if (!fatal && !buffer_uptodate(bh)) {
			memset(bh->b_data, 0, inode->i_sb->s_blocksize);
			set_buffer_uptodate(bh);
		}
		unlock_buffer(bh);
		BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
		err = ext4_handle_dirty_metadata(handle, inode, bh);
		if (!fatal)
			fatal = err;
	} else {
		BUFFER_TRACE(bh, "not a new buffer");
	}
	if (fatal) {
		*errp = fatal;
		brelse(bh);
		bh = NULL;
	}
	return bh;
}

struct buffer_head *ext4_bread(handle_t *handle, struct inode *inode,
			       ext4_lblk_t block, int create, int *err)
{
	struct buffer_head *bh;

	bh = ext4_getblk(handle, inode, block, create, err);
	if (!bh)
		return bh;
	if (buffer_uptodate(bh))
		return bh;
	ll_rw_block(READ_META, 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	put_bh(bh);
	*err = -EIO;
	return NULL;
}

static int walk_page_buffers(handle_t *handle,
			     struct buffer_head *head,
			     unsigned from,
			     unsigned to,
			     int *partial,
			     int (*fn)(handle_t *handle,
				       struct buffer_head *bh))
{
	struct buffer_head *bh;
	unsigned block_start, block_end;
	unsigned blocksize = head->b_size;
	int err, ret = 0;
	struct buffer_head *next;

	for (bh = head, block_start = 0;
	     ret == 0 && (bh != head || !block_start);
	     block_start = block_end, bh = next) {
		next = bh->b_this_page;
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (partial && !buffer_uptodate(bh))
				*partial = 1;
			continue;
		}
		err = (*fn)(handle, bh);
		if (!ret)
			ret = err;
	}
	return ret;
}

/*
 * To preserve ordering, it is essential that the hole instantiation and
 * the data write be encapsulated in a single transaction.  We cannot
 * close off a transaction and start a new one between the ext4_get_block()
 * and the commit_write().  So doing the jbd2_journal_start at the start of
 * prepare_write() is the right place.
 *
 * Also, this function can nest inside ext4_writepage() ->
 * block_write_full_page(). In that case, we *know* that ext4_writepage()
 * has generated enough buffer credits to do the whole page.  So we won't
 * block on the journal in that case, which is good, because the caller may
 * be PF_MEMALLOC.
 *
 * By accident, ext4 can be reentered when a transaction is open via
 * quota file writes.  If we were to commit the transaction while thus
 * reentered, there can be a deadlock - we would be holding a quota
 * lock, and the commit would never complete if another thread had a
 * transaction open and was blocking on the quota lock - a ranking
 * violation.
 *
 * So what we do is to rely on the fact that jbd2_journal_stop/journal_start
 * will _not_ run commit under these circumstances because handle->h_ref
 * is elevated.  We'll still have enough credits for the tiny quotafile
 * write.
 */
static int do_journal_get_write_access(handle_t *handle,
				       struct buffer_head *bh)
{
	int dirty = buffer_dirty(bh);
	int ret;

	if (!buffer_mapped(bh) || buffer_freed(bh))
		return 0;
	/*
	 * __block_write_begin() could have dirtied some buffers. Clean
	 * the dirty bit as jbd2_journal_get_write_access() could complain
	 * otherwise about fs integrity issues. Setting of the dirty bit
	 * by __block_write_begin() isn't a real problem here as we clear
	 * the bit before releasing a page lock and thus writeback cannot
	 * ever write the buffer.
	 */
	if (dirty)
		clear_buffer_dirty(bh);
	ret = ext4_journal_get_write_access(handle, bh);
	if (!ret && dirty)
		ret = ext4_handle_dirty_metadata(handle, NULL, bh);
	return ret;
}

/*
 * Truncate blocks that were not used by write. We have to truncate the
 * pagecache as well so that corresponding buffers get properly unmapped.
 */
static void ext4_truncate_failed_write(struct inode *inode)
{
	truncate_inode_pages(inode->i_mapping, inode->i_size);
	ext4_truncate(inode);
}

static int ext4_get_block_write(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create);
static int ext4_write_begin(struct file *file, struct address_space *mapping,
			    loff_t pos, unsigned len, unsigned flags,
			    struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	int ret, needed_blocks;
	handle_t *handle;
	int retries = 0;
	struct page *page;
	pgoff_t index;
	unsigned from, to;

	trace_ext4_write_begin(inode, pos, len, flags);
	/*
	 * Reserve one block more for addition to orphan list in case
	 * we allocate blocks but write fails for some reason
	 */
	needed_blocks = ext4_writepage_trans_blocks(inode) + 1;
	index = pos >> PAGE_CACHE_SHIFT;
	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + len;

retry:
	handle = ext4_journal_start(inode, needed_blocks);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}

	/* We cannot recurse into the filesystem as the transaction is already
	 * started */
	flags |= AOP_FLAG_NOFS;

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		ext4_journal_stop(handle);
		ret = -ENOMEM;
		goto out;
	}
	*pagep = page;

	if (ext4_should_dioread_nolock(inode))
		ret = __block_write_begin(page, pos, len, ext4_get_block_write);
	else
		ret = __block_write_begin(page, pos, len, ext4_get_block);

	if (!ret && ext4_should_journal_data(inode)) {
		ret = walk_page_buffers(handle, page_buffers(page),
				from, to, NULL, do_journal_get_write_access);
	}

	if (ret) {
		unlock_page(page);
		page_cache_release(page);
		/*
		 * __block_write_begin may have instantiated a few blocks
		 * outside i_size.  Trim these off again. Don't need
		 * i_size_read because we hold i_mutex.
		 *
		 * Add inode to orphan list in case we crash before
		 * truncate finishes
		 */
		if (pos + len > inode->i_size && ext4_can_truncate(inode))
			ext4_orphan_add(handle, inode);

		ext4_journal_stop(handle);
		if (pos + len > inode->i_size) {
			ext4_truncate_failed_write(inode);
			/*
			 * If truncate failed early the inode might
			 * still be on the orphan list; we need to
			 * make sure the inode is removed from the
			 * orphan list in that case.
			 */
			if (inode->i_nlink)
				ext4_orphan_del(NULL, inode);
		}
	}

	if (ret == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
		goto retry;
out:
	return ret;
}

/* For write_end() in data=journal mode */
static int write_end_fn(handle_t *handle, struct buffer_head *bh)
{
	if (!buffer_mapped(bh) || buffer_freed(bh))
		return 0;
	set_buffer_uptodate(bh);
	return ext4_handle_dirty_metadata(handle, NULL, bh);
}

static int ext4_generic_write_end(struct file *file,
				  struct address_space *mapping,
				  loff_t pos, unsigned len, unsigned copied,
				  struct page *page, void *fsdata)
{
	int i_size_changed = 0;
	struct inode *inode = mapping->host;
	handle_t *handle = ext4_journal_current_handle();

	copied = block_write_end(file, mapping, pos, len, copied, page, fsdata);

	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold i_mutex.
	 *
	 * But it's important to update i_size while still holding page lock:
	 * page writeout could otherwise come in and zero beyond i_size.
	 */
	if (pos + copied > inode->i_size) {
		i_size_write(inode, pos + copied);
		i_size_changed = 1;
	}

	if (pos + copied >  EXT4_I(inode)->i_disksize) {
		/* We need to mark inode dirty even if
		 * new_i_size is less that inode->i_size
		 * bu greater than i_disksize.(hint delalloc)
		 */
		ext4_update_i_disksize(inode, (pos + copied));
		i_size_changed = 1;
	}
	unlock_page(page);
	page_cache_release(page);

	/*
	 * Don't mark the inode dirty under page lock. First, it unnecessarily
	 * makes the holding time of page lock longer. Second, it forces lock
	 * ordering of page lock and transaction start for journaling
	 * filesystems.
	 */
	if (i_size_changed)
		ext4_mark_inode_dirty(handle, inode);

	return copied;
}

/*
 * We need to pick up the new inode size which generic_commit_write gave us
 * `file' can be NULL - eg, when called from page_symlink().
 *
 * ext4 never places buffers on inode->i_mapping->private_list.  metadata
 * buffers are managed internally.
 */
static int ext4_ordered_write_end(struct file *file,
				  struct address_space *mapping,
				  loff_t pos, unsigned len, unsigned copied,
				  struct page *page, void *fsdata)
{
	handle_t *handle = ext4_journal_current_handle();
	struct inode *inode = mapping->host;
	int ret = 0, ret2;

	trace_ext4_ordered_write_end(inode, pos, len, copied);
	ret = ext4_jbd2_file_inode(handle, inode);

	if (ret == 0) {
		ret2 = ext4_generic_write_end(file, mapping, pos, len, copied,
							page, fsdata);
		copied = ret2;
		if (pos + len > inode->i_size && ext4_can_truncate(inode))
			/* if we have allocated more blocks and copied
			 * less. We will have blocks allocated outside
			 * inode->i_size. So truncate them
			 */
			ext4_orphan_add(handle, inode);
		if (ret2 < 0)
			ret = ret2;
	}
	ret2 = ext4_journal_stop(handle);
	if (!ret)
		ret = ret2;

	if (pos + len > inode->i_size) {
		ext4_truncate_failed_write(inode);
		/*
		 * If truncate failed early the inode might still be
		 * on the orphan list; we need to make sure the inode
		 * is removed from the orphan list in that case.
		 */
		if (inode->i_nlink)
			ext4_orphan_del(NULL, inode);
	}


	return ret ? ret : copied;
}

static int ext4_writeback_write_end(struct file *file,
				    struct address_space *mapping,
				    loff_t pos, unsigned len, unsigned copied,
				    struct page *page, void *fsdata)
{
	handle_t *handle = ext4_journal_current_handle();
	struct inode *inode = mapping->host;
	int ret = 0, ret2;

	trace_ext4_writeback_write_end(inode, pos, len, copied);
	ret2 = ext4_generic_write_end(file, mapping, pos, len, copied,
							page, fsdata);
	copied = ret2;
	if (pos + len > inode->i_size && ext4_can_truncate(inode))
		/* if we have allocated more blocks and copied
		 * less. We will have blocks allocated outside
		 * inode->i_size. So truncate them
		 */
		ext4_orphan_add(handle, inode);

	if (ret2 < 0)
		ret = ret2;

	ret2 = ext4_journal_stop(handle);
	if (!ret)
		ret = ret2;

	if (pos + len > inode->i_size) {
		ext4_truncate_failed_write(inode);
		/*
		 * If truncate failed early the inode might still be
		 * on the orphan list; we need to make sure the inode
		 * is removed from the orphan list in that case.
		 */
		if (inode->i_nlink)
			ext4_orphan_del(NULL, inode);
	}

	return ret ? ret : copied;
}

static int ext4_journalled_write_end(struct file *file,
				     struct address_space *mapping,
				     loff_t pos, unsigned len, unsigned copied,
				     struct page *page, void *fsdata)
{
	handle_t *handle = ext4_journal_current_handle();
	struct inode *inode = mapping->host;
	int ret = 0, ret2;
	int partial = 0;
	unsigned from, to;
	loff_t new_i_size;

	trace_ext4_journalled_write_end(inode, pos, len, copied);
	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + len;

	BUG_ON(!ext4_handle_valid(handle));

	if (copied < len) {
		if (!PageUptodate(page))
			copied = 0;
		page_zero_new_buffers(page, from+copied, to);
	}

	ret = walk_page_buffers(handle, page_buffers(page), from,
				to, &partial, write_end_fn);
	if (!partial)
		SetPageUptodate(page);
	new_i_size = pos + copied;
	if (new_i_size > inode->i_size)
		i_size_write(inode, pos+copied);
	ext4_set_inode_state(inode, EXT4_STATE_JDATA);
	if (new_i_size > EXT4_I(inode)->i_disksize) {
		ext4_update_i_disksize(inode, new_i_size);
		ret2 = ext4_mark_inode_dirty(handle, inode);
		if (!ret)
			ret = ret2;
	}

	unlock_page(page);
	page_cache_release(page);
	if (pos + len > inode->i_size && ext4_can_truncate(inode))
		/* if we have allocated more blocks and copied
		 * less. We will have blocks allocated outside
		 * inode->i_size. So truncate them
		 */
		ext4_orphan_add(handle, inode);

	ret2 = ext4_journal_stop(handle);
	if (!ret)
		ret = ret2;
	if (pos + len > inode->i_size) {
		ext4_truncate_failed_write(inode);
		/*
		 * If truncate failed early the inode might still be
		 * on the orphan list; we need to make sure the inode
		 * is removed from the orphan list in that case.
		 */
		if (inode->i_nlink)
			ext4_orphan_del(NULL, inode);
	}

	return ret ? ret : copied;
}

/*
 * Reserve a single block located at lblock
 */
static int ext4_da_reserve_space(struct inode *inode, ext4_lblk_t lblock)
{
	int retries = 0;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);
	unsigned long md_needed;
	int ret;

	/*
	 * recalculate the amount of metadata blocks to reserve
	 * in order to allocate nrblocks
	 * worse case is one extent per block
	 */
repeat:
	spin_lock(&ei->i_block_reservation_lock);
	md_needed = ext4_calc_metadata_amount(inode, lblock);
	trace_ext4_da_reserve_space(inode, md_needed);
	spin_unlock(&ei->i_block_reservation_lock);

	/*
	 * We will charge metadata quota at writeout time; this saves
	 * us from metadata over-estimation, though we may go over by
	 * a small amount in the end.  Here we just reserve for data.
	 */
	ret = dquot_reserve_block(inode, 1);
	if (ret)
		return ret;
	/*
	 * We do still charge estimated metadata to the sb though;
	 * we cannot afford to run out of free blocks.
	 */
	if (ext4_claim_free_blocks(sbi, md_needed + 1, 0)) {
		dquot_release_reservation_block(inode, 1);
		if (ext4_should_retry_alloc(inode->i_sb, &retries)) {
			yield();
			goto repeat;
		}
		return -ENOSPC;
	}
	spin_lock(&ei->i_block_reservation_lock);
	ei->i_reserved_data_blocks++;
	ei->i_reserved_meta_blocks += md_needed;
	spin_unlock(&ei->i_block_reservation_lock);

	return 0;       /* success */
}

static void ext4_da_release_space(struct inode *inode, int to_free)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);

	if (!to_free)
		return;		/* Nothing to release, exit */

	spin_lock(&EXT4_I(inode)->i_block_reservation_lock);

	trace_ext4_da_release_space(inode, to_free);
	if (unlikely(to_free > ei->i_reserved_data_blocks)) {
		/*
		 * if there aren't enough reserved blocks, then the
		 * counter is messed up somewhere.  Since this
		 * function is called from invalidate page, it's
		 * harmless to return without any action.
		 */
		ext4_msg(inode->i_sb, KERN_NOTICE, "ext4_da_release_space: "
			 "ino %lu, to_free %d with only %d reserved "
			 "data blocks\n", inode->i_ino, to_free,
			 ei->i_reserved_data_blocks);
		WARN_ON(1);
		to_free = ei->i_reserved_data_blocks;
	}
	ei->i_reserved_data_blocks -= to_free;

	if (ei->i_reserved_data_blocks == 0) {
		/*
		 * We can release all of the reserved metadata blocks
		 * only when we have written all of the delayed
		 * allocation blocks.
		 */
		percpu_counter_sub(&sbi->s_dirtyblocks_counter,
				   ei->i_reserved_meta_blocks);
		ei->i_reserved_meta_blocks = 0;
		ei->i_da_metadata_calc_len = 0;
	}

	/* update fs dirty data blocks counter */
	percpu_counter_sub(&sbi->s_dirtyblocks_counter, to_free);

	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

	dquot_release_reservation_block(inode, to_free);
}

static void ext4_da_page_release_reservation(struct page *page,
					     unsigned long offset)
{
	int to_release = 0;
	struct buffer_head *head, *bh;
	unsigned int curr_off = 0;

	head = page_buffers(page);
	bh = head;
	do {
		unsigned int next_off = curr_off + bh->b_size;

		if ((offset <= curr_off) && (buffer_delay(bh))) {
			to_release++;
			clear_buffer_delay(bh);
		}
		curr_off = next_off;
	} while ((bh = bh->b_this_page) != head);
	ext4_da_release_space(page->mapping->host, to_release);
}

/*
 * Delayed allocation stuff
 */

/*
 * mpage_da_submit_io - walks through extent of pages and try to write
 * them with writepage() call back
 *
 * @mpd->inode: inode
 * @mpd->first_page: first page of the extent
 * @mpd->next_page: page after the last page of the extent
 *
 * By the time mpage_da_submit_io() is called we expect all blocks
 * to be allocated. this may be wrong if allocation failed.
 *
 * As pages are already locked by write_cache_pages(), we can't use it
 */
static int mpage_da_submit_io(struct mpage_da_data *mpd,
			      struct ext4_map_blocks *map)
{
	struct pagevec pvec;
	unsigned long index, end;
	int ret = 0, err, nr_pages, i;
	struct inode *inode = mpd->inode;
	struct address_space *mapping = inode->i_mapping;
	loff_t size = i_size_read(inode);
	unsigned int len, block_start;
	struct buffer_head *bh, *page_bufs = NULL;
	int journal_data = ext4_should_journal_data(inode);
	sector_t pblock = 0, cur_logical = 0;
	struct ext4_io_submit io_submit;

	BUG_ON(mpd->next_page <= mpd->first_page);
	memset(&io_submit, 0, sizeof(io_submit));
	/*
	 * We need to start from the first_page to the next_page - 1
	 * to make sure we also write the mapped dirty buffer_heads.
	 * If we look at mpd->b_blocknr we would only be looking
	 * at the currently mapped buffer_heads.
	 */
	index = mpd->first_page;
	end = mpd->next_page - 1;

	pagevec_init(&pvec, 0);
	while (index <= end) {
		nr_pages = pagevec_lookup(&pvec, mapping, index, PAGEVEC_SIZE);
		if (nr_pages == 0)
			break;
		for (i = 0; i < nr_pages; i++) {
			int commit_write = 0, skip_page = 0;
			struct page *page = pvec.pages[i];

			index = page->index;
			if (index > end)
				break;

			if (index == size >> PAGE_CACHE_SHIFT)
				len = size & ~PAGE_CACHE_MASK;
			else
				len = PAGE_CACHE_SIZE;
			if (map) {
				cur_logical = index << (PAGE_CACHE_SHIFT -
							inode->i_blkbits);
				pblock = map->m_pblk + (cur_logical -
							map->m_lblk);
			}
			index++;

			BUG_ON(!PageLocked(page));
			BUG_ON(PageWriteback(page));

			/*
			 * If the page does not have buffers (for
			 * whatever reason), try to create them using
			 * __block_write_begin.  If this fails,
			 * skip the page and move on.
			 */
			if (!page_has_buffers(page)) {
				if (__block_write_begin(page, 0, len,
						noalloc_get_block_write)) {
				skip_page:
					unlock_page(page);
					continue;
				}
				commit_write = 1;
			}

			bh = page_bufs = page_buffers(page);
			block_start = 0;
			do {
				if (!bh)
					goto skip_page;
				if (map && (cur_logical >= map->m_lblk) &&
				    (cur_logical <= (map->m_lblk +
						     (map->m_len - 1)))) {
					if (buffer_delay(bh)) {
						clear_buffer_delay(bh);
						bh->b_blocknr = pblock;
					}
					if (buffer_unwritten(bh) ||
					    buffer_mapped(bh))
						BUG_ON(bh->b_blocknr != pblock);
					if (map->m_flags & EXT4_MAP_UNINIT)
						set_buffer_uninit(bh);
					clear_buffer_unwritten(bh);
				}

				/* skip page if block allocation undone */
				if (buffer_delay(bh) || buffer_unwritten(bh))
					skip_page = 1;
				bh = bh->b_this_page;
				block_start += bh->b_size;
				cur_logical++;
				pblock++;
			} while (bh != page_bufs);

			if (skip_page)
				goto skip_page;

			if (commit_write)
				/* mark the buffer_heads as dirty & uptodate */
				block_commit_write(page, 0, len);

			clear_page_dirty_for_io(page);
			/*
			 * Delalloc doesn't support data journalling,
			 * but eventually maybe we'll lift this
			 * restriction.
			 */
			if (unlikely(journal_data && PageChecked(page)))
				err = __ext4_journalled_writepage(page, len);
			else if (test_opt(inode->i_sb, MBLK_IO_SUBMIT))
				err = ext4_bio_write_page(&io_submit, page,
							  len, mpd->wbc);
			else if (buffer_uninit(page_bufs)) {
				ext4_set_bh_endio(page_bufs, inode);
				err = block_write_full_page_endio(page,
					noalloc_get_block_write,
					mpd->wbc, ext4_end_io_buffer_write);
			} else
				err = block_write_full_page(page,
					noalloc_get_block_write, mpd->wbc);

			if (!err)
				mpd->pages_written++;
			/*
			 * In error case, we have to continue because
			 * remaining pages are still locked
			 */
			if (ret == 0)
				ret = err;
		}
		pagevec_release(&pvec);
	}
	ext4_io_submit(&io_submit);
	return ret;
}

static void ext4_da_block_invalidatepages(struct mpage_da_data *mpd)
{
	int nr_pages, i;
	pgoff_t index, end;
	struct pagevec pvec;
	struct inode *inode = mpd->inode;
	struct address_space *mapping = inode->i_mapping;

	index = mpd->first_page;
	end   = mpd->next_page - 1;
	while (index <= end) {
		nr_pages = pagevec_lookup(&pvec, mapping, index, PAGEVEC_SIZE);
		if (nr_pages == 0)
			break;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			if (page->index > end)
				break;
			BUG_ON(!PageLocked(page));
			BUG_ON(PageWriteback(page));
			block_invalidatepage(page, 0);
			ClearPageUptodate(page);
			unlock_page(page);
		}
		index = pvec.pages[nr_pages - 1]->index + 1;
		pagevec_release(&pvec);
	}
	return;
}

static void ext4_print_free_blocks(struct inode *inode)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	printk(KERN_CRIT "Total free blocks count %lld\n",
	       ext4_count_free_blocks(inode->i_sb));
	printk(KERN_CRIT "Free/Dirty block details\n");
	printk(KERN_CRIT "free_blocks=%lld\n",
	       (long long) percpu_counter_sum(&sbi->s_freeblocks_counter));
	printk(KERN_CRIT "dirty_blocks=%lld\n",
	       (long long) percpu_counter_sum(&sbi->s_dirtyblocks_counter));
	printk(KERN_CRIT "Block reservation details\n");
	printk(KERN_CRIT "i_reserved_data_blocks=%u\n",
	       EXT4_I(inode)->i_reserved_data_blocks);
	printk(KERN_CRIT "i_reserved_meta_blocks=%u\n",
	       EXT4_I(inode)->i_reserved_meta_blocks);
	return;
}

/*
 * mpage_da_map_and_submit - go through given space, map them
 *       if necessary, and then submit them for I/O
 *
 * @mpd - bh describing space
 *
 * The function skips space we know is already mapped to disk blocks.
 *
 */
static void mpage_da_map_and_submit(struct mpage_da_data *mpd)
{
	int err, blks, get_blocks_flags;
	struct ext4_map_blocks map, *mapp = NULL;
	sector_t next = mpd->b_blocknr;
	unsigned max_blocks = mpd->b_size >> mpd->inode->i_blkbits;
	loff_t disksize = EXT4_I(mpd->inode)->i_disksize;
	handle_t *handle = NULL;

	/*
	 * If the blocks are mapped already, or we couldn't accumulate
	 * any blocks, then proceed immediately to the submission stage.
	 */
	if ((mpd->b_size == 0) ||
	    ((mpd->b_state  & (1 << BH_Mapped)) &&
	     !(mpd->b_state & (1 << BH_Delay)) &&
	     !(mpd->b_state & (1 << BH_Unwritten))))
		goto submit_io;

	handle = ext4_journal_current_handle();
	BUG_ON(!handle);

	/*
	 * Call ext4_map_blocks() to allocate any delayed allocation
	 * blocks, or to convert an uninitialized extent to be
	 * initialized (in the case where we have written into
	 * one or more preallocated blocks).
	 *
	 * We pass in the magic EXT4_GET_BLOCKS_DELALLOC_RESERVE to
	 * indicate that we are on the delayed allocation path.  This
	 * affects functions in many different parts of the allocation
	 * call path.  This flag exists primarily because we don't
	 * want to change *many* call functions, so ext4_map_blocks()
	 * will set the EXT4_STATE_DELALLOC_RESERVED flag once the
	 * inode's allocation semaphore is taken.
	 *
	 * If the blocks in questions were delalloc blocks, set
	 * EXT4_GET_BLOCKS_DELALLOC_RESERVE so the delalloc accounting
	 * variables are updated after the blocks have been allocated.
	 */
	map.m_lblk = next;
	map.m_len = max_blocks;
	get_blocks_flags = EXT4_GET_BLOCKS_CREATE;
	if (ext4_should_dioread_nolock(mpd->inode))
		get_blocks_flags |= EXT4_GET_BLOCKS_IO_CREATE_EXT;
	if (mpd->b_state & (1 << BH_Delay))
		get_blocks_flags |= EXT4_GET_BLOCKS_DELALLOC_RESERVE;

	blks = ext4_map_blocks(handle, mpd->inode, &map, get_blocks_flags);
	if (blks < 0) {
		struct super_block *sb = mpd->inode->i_sb;

		err = blks;
		/*
		 * If get block returns EAGAIN or ENOSPC and there
		 * appears to be free blocks we will just let
		 * mpage_da_submit_io() unlock all of the pages.
		 */
		if (err == -EAGAIN)
			goto submit_io;

		if (err == -ENOSPC &&
		    ext4_count_free_blocks(sb)) {
			mpd->retval = err;
			goto submit_io;
		}

		/*
		 * get block failure will cause us to loop in
		 * writepages, because a_ops->writepage won't be able
		 * to make progress. The page will be redirtied by
		 * writepage and writepages will again try to write
		 * the same.
		 */
		if (!(EXT4_SB(sb)->s_mount_flags & EXT4_MF_FS_ABORTED)) {
			ext4_msg(sb, KERN_CRIT,
				 "delayed block allocation failed for inode %lu "
				 "at logical offset %llu with max blocks %zd "
				 "with error %d", mpd->inode->i_ino,
				 (unsigned long long) next,
				 mpd->b_size >> mpd->inode->i_blkbits, err);
			ext4_msg(sb, KERN_CRIT,
				"This should not happen!! Data will be lost\n");
			if (err == -ENOSPC)
				ext4_print_free_blocks(mpd->inode);
		}
		/* invalidate all the pages */
		ext4_da_block_invalidatepages(mpd);

		/* Mark this page range as having been completed */
		mpd->io_done = 1;
		return;
	}
	BUG_ON(blks == 0);

	mapp = &map;
	if (map.m_flags & EXT4_MAP_NEW) {
		struct block_device *bdev = mpd->inode->i_sb->s_bdev;
		int i;

		for (i = 0; i < map.m_len; i++)
			unmap_underlying_metadata(bdev, map.m_pblk + i);
	}

	if (ext4_should_order_data(mpd->inode)) {
		err = ext4_jbd2_file_inode(handle, mpd->inode);
		if (err)
			/* This only happens if the journal is aborted */
			return;
	}

	/*
	 * Update on-disk size along with block allocation.
	 */
	disksize = ((loff_t) next + blks) << mpd->inode->i_blkbits;
	if (disksize > i_size_read(mpd->inode))
		disksize = i_size_read(mpd->inode);
	if (disksize > EXT4_I(mpd->inode)->i_disksize) {
		ext4_update_i_disksize(mpd->inode, disksize);
		err = ext4_mark_inode_dirty(handle, mpd->inode);
		if (err)
			ext4_error(mpd->inode->i_sb,
				   "Failed to mark inode %lu dirty",
				   mpd->inode->i_ino);
	}

submit_io:
	mpage_da_submit_io(mpd, mapp);
	mpd->io_done = 1;
}

#define BH_FLAGS ((1 << BH_Uptodate) | (1 << BH_Mapped) | \
		(1 << BH_Delay) | (1 << BH_Unwritten))

/*
 * mpage_add_bh_to_extent - try to add one more block to extent of blocks
 *
 * @mpd->lbh - extent of blocks
 * @logical - logical number of the block in the file
 * @bh - bh of the block (used to access block's state)
 *
 * the function is used to collect contig. blocks in same state
 */
static void mpage_add_bh_to_extent(struct mpage_da_data *mpd,
				   sector_t logical, size_t b_size,
				   unsigned long b_state)
{
	sector_t next;
	int nrblocks = mpd->b_size >> mpd->inode->i_blkbits;

	/*
	 * XXX Don't go larger than mballoc is willing to allocate
	 * This is a stopgap solution.  We eventually need to fold
	 * mpage_da_submit_io() into this function and then call
	 * ext4_map_blocks() multiple times in a loop
	 */
	if (nrblocks >= 8*1024*1024/mpd->inode->i_sb->s_blocksize)
		goto flush_it;

	/* check if thereserved journal credits might overflow */
	if (!(ext4_test_inode_flag(mpd->inode, EXT4_INODE_EXTENTS))) {
		if (nrblocks >= EXT4_MAX_TRANS_DATA) {
			/*
			 * With non-extent format we are limited by the journal
			 * credit available.  Total credit needed to insert
			 * nrblocks contiguous blocks is dependent on the
			 * nrblocks.  So limit nrblocks.
			 */
			goto flush_it;
		} else if ((nrblocks + (b_size >> mpd->inode->i_blkbits)) >
				EXT4_MAX_TRANS_DATA) {
			/*
			 * Adding the new buffer_head would make it cross the
			 * allowed limit for which we have journal credit
			 * reserved. So limit the new bh->b_size
			 */
			b_size = (EXT4_MAX_TRANS_DATA - nrblocks) <<
						mpd->inode->i_blkbits;
			/* we will do mpage_da_submit_io in the next loop */
		}
	}
	/*
	 * First block in the extent
	 */
	if (mpd->b_size == 0) {
		mpd->b_blocknr = logical;
		mpd->b_size = b_size;
		mpd->b_state = b_state & BH_FLAGS;
		return;
	}

	next = mpd->b_blocknr + nrblocks;
	/*
	 * Can we merge the block to our big extent?
	 */
	if (logical == next && (b_state & BH_FLAGS) == mpd->b_state) {
		mpd->b_size += b_size;
		return;
	}

flush_it:
	/*
	 * We couldn't merge the block to our extent, so we
	 * need to flush current  extent and start new one
	 */
	mpage_da_map_and_submit(mpd);
	return;
}

static int ext4_bh_delay_or_unwritten(handle_t *handle, struct buffer_head *bh)
{
	return (buffer_delay(bh) || buffer_unwritten(bh)) && buffer_dirty(bh);
}

/*
 * This is a special get_blocks_t callback which is used by
 * ext4_da_write_begin().  It will either return mapped block or
 * reserve space for a single block.
 *
 * For delayed buffer_head we have BH_Mapped, BH_New, BH_Delay set.
 * We also have b_blocknr = -1 and b_bdev initialized properly
 *
 * For unwritten buffer_head we have BH_Mapped, BH_New, BH_Unwritten set.
 * We also have b_blocknr = physicalblock mapping unwritten extent and b_bdev
 * initialized properly.
 */
static int ext4_da_get_block_prep(struct inode *inode, sector_t iblock,
				  struct buffer_head *bh, int create)
{
	struct ext4_map_blocks map;
	int ret = 0;
	sector_t invalid_block = ~((sector_t) 0xffff);

	if (invalid_block < ext4_blocks_count(EXT4_SB(inode->i_sb)->s_es))
		invalid_block = ~0;

	BUG_ON(create == 0);
	BUG_ON(bh->b_size != inode->i_sb->s_blocksize);

	map.m_lblk = iblock;
	map.m_len = 1;

	/*
	 * first, we need to know whether the block is allocated already
	 * preallocated blocks are unmapped but should treated
	 * the same as allocated blocks.
	 */
	ret = ext4_map_blocks(NULL, inode, &map, 0);
	if (ret < 0)
		return ret;
	if (ret == 0) {
		if (buffer_delay(bh))
			return 0; /* Not sure this could or should happen */
		/*
		 * XXX: __block_write_begin() unmaps passed block, is it OK?
		 */
		ret = ext4_da_reserve_space(inode, iblock);
		if (ret)
			/* not enough space to reserve */
			return ret;

		map_bh(bh, inode->i_sb, invalid_block);
		set_buffer_new(bh);
		set_buffer_delay(bh);
		return 0;
	}

	map_bh(bh, inode->i_sb, map.m_pblk);
	bh->b_state = (bh->b_state & ~EXT4_MAP_FLAGS) | map.m_flags;

	if (buffer_unwritten(bh)) {
		/* A delayed write to unwritten bh should be marked
		 * new and mapped.  Mapped ensures that we don't do
		 * get_block multiple times when we write to the same
		 * offset and new ensures that we do proper zero out
		 * for partial write.
		 */
		set_buffer_new(bh);
		set_buffer_mapped(bh);
	}
	return 0;
}

/*
 * This function is used as a standard get_block_t calback function
 * when there is no desire to allocate any blocks.  It is used as a
 * callback function for block_write_begin() and block_write_full_page().
 * These functions should only try to map a single block at a time.
 *
 * Since this function doesn't do block allocations even if the caller
 * requests it by passing in create=1, it is critically important that
 * any caller checks to make sure that any buffer heads are returned
 * by this function are either all already mapped or marked for
 * delayed allocation before calling  block_write_full_page().  Otherwise,
 * b_blocknr could be left unitialized, and the page write functions will
 * be taken by surprise.
 */
static int noalloc_get_block_write(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	BUG_ON(bh_result->b_size != inode->i_sb->s_blocksize);
	return _ext4_get_block(inode, iblock, bh_result, 0);
}

static int bget_one(handle_t *handle, struct buffer_head *bh)
{
	get_bh(bh);
	return 0;
}

static int bput_one(handle_t *handle, struct buffer_head *bh)
{
	put_bh(bh);
	return 0;
}

static int __ext4_journalled_writepage(struct page *page,
				       unsigned int len)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	struct buffer_head *page_bufs;
	handle_t *handle = NULL;
	int ret = 0;
	int err;

	ClearPageChecked(page);
	page_bufs = page_buffers(page);
	BUG_ON(!page_bufs);
	walk_page_buffers(handle, page_bufs, 0, len, NULL, bget_one);
	/* As soon as we unlock the page, it can go away, but we have
	 * references to buffers so we are safe */
	unlock_page(page);

	handle = ext4_journal_start(inode, ext4_writepage_trans_blocks(inode));
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}

	BUG_ON(!ext4_handle_valid(handle));

	ret = walk_page_buffers(handle, page_bufs, 0, len, NULL,
				do_journal_get_write_access);

	err = walk_page_buffers(handle, page_bufs, 0, len, NULL,
				write_end_fn);
	if (ret == 0)
		ret = err;
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;

	walk_page_buffers(handle, page_bufs, 0, len, NULL, bput_one);
	ext4_set_inode_state(inode, EXT4_STATE_JDATA);
out:
	return ret;
}

static int ext4_set_bh_endio(struct buffer_head *bh, struct inode *inode);
static void ext4_end_io_buffer_write(struct buffer_head *bh, int uptodate);

/*
 * Note that we don't need to start a transaction unless we're journaling data
 * because we should have holes filled from ext4_page_mkwrite(). We even don't
 * need to file the inode to the transaction's list in ordered mode because if
 * we are writing back data added by write(), the inode is already there and if
 * we are writing back data modified via mmap(), no one guarantees in which
 * transaction the data will hit the disk. In case we are journaling data, we
 * cannot start transaction directly because transaction start ranks above page
 * lock so we have to do some magic.
 *
 * This function can get called via...
 *   - ext4_da_writepages after taking page lock (have journal handle)
 *   - journal_submit_inode_data_buffers (no journal handle)
 *   - shrink_page_list via pdflush (no journal handle)
 *   - grab_page_cache when doing write_begin (have journal handle)
 *
 * We don't do any block allocation in this function. If we have page with
 * multiple blocks we need to write those buffer_heads that are mapped. This
 * is important for mmaped based write. So if we do with blocksize 1K
 * truncate(f, 1024);
 * a = mmap(f, 0, 4096);
 * a[0] = 'a';
 * truncate(f, 4096);
 * we have in the page first buffer_head mapped via page_mkwrite call back
 * but other bufer_heads would be unmapped but dirty(dirty done via the
 * do_wp_page). So writepage should write the first block. If we modify
 * the mmap area beyond 1024 we will again get a page_fault and the
 * page_mkwrite callback will do the block allocation and mark the
 * buffer_heads mapped.
 *
 * We redirty the page if we have any buffer_heads that is either delay or
 * unwritten in the page.
 *
 * We can get recursively called as show below.
 *
 *	ext4_writepage() -> kmalloc() -> __alloc_pages() -> page_launder() ->
 *		ext4_writepage()
 *
 * But since we don't do any block allocation we should not deadlock.
 * Page also have the dirty flag cleared so we don't get recurive page_lock.
 */
static int ext4_writepage(struct page *page,
			  struct writeback_control *wbc)
{
	int ret = 0, commit_write = 0;
	loff_t size;
	unsigned int len;
	struct buffer_head *page_bufs = NULL;
	struct inode *inode = page->mapping->host;

	trace_ext4_writepage(page);
	size = i_size_read(inode);
	if (page->index == size >> PAGE_CACHE_SHIFT)
		len = size & ~PAGE_CACHE_MASK;
	else
		len = PAGE_CACHE_SIZE;

	/*
	 * If the page does not have buffers (for whatever reason),
	 * try to create them using __block_write_begin.  If this
	 * fails, redirty the page and move on.
	 */
	if (!page_has_buffers(page)) {
		if (__block_write_begin(page, 0, len,
					noalloc_get_block_write)) {
		redirty_page:
			redirty_page_for_writepage(wbc, page);
			unlock_page(page);
			return 0;
		}
		commit_write = 1;
	}
	page_bufs = page_buffers(page);
	if (walk_page_buffers(NULL, page_bufs, 0, len, NULL,
			      ext4_bh_delay_or_unwritten)) {
		/*
		 * We don't want to do block allocation, so redirty
		 * the page and return.  We may reach here when we do
		 * a journal commit via journal_submit_inode_data_buffers.
		 * We can also reach here via shrink_page_list
		 */
		goto redirty_page;
	}
	if (commit_write)
		/* now mark the buffer_heads as dirty and uptodate */
		block_commit_write(page, 0, len);

	if (PageChecked(page) && ext4_should_journal_data(inode))
		/*
		 * It's mmapped pagecache.  Add buffers and journal it.  There
		 * doesn't seem much point in redirtying the page here.
		 */
		return __ext4_journalled_writepage(page, len);

	if (buffer_uninit(page_bufs)) {
		ext4_set_bh_endio(page_bufs, inode);
		ret = block_write_full_page_endio(page, noalloc_get_block_write,
					    wbc, ext4_end_io_buffer_write);
	} else
		ret = block_write_full_page(page, noalloc_get_block_write,
					    wbc);

	return ret;
}

/*
 * This is called via ext4_da_writepages() to
 * calculate the total number of credits to reserve to fit
 * a single extent allocation into a single transaction,
 * ext4_da_writpeages() will loop calling this before
 * the block allocation.
 */

static int ext4_da_writepages_trans_blocks(struct inode *inode)
{
	int max_blocks = EXT4_I(inode)->i_reserved_data_blocks;

	/*
	 * With non-extent format the journal credit needed to
	 * insert nrblocks contiguous block is dependent on
	 * number of contiguous block. So we will limit
	 * number of contiguous block to a sane value
	 */
	if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) &&
	    (max_blocks > EXT4_MAX_TRANS_DATA))
		max_blocks = EXT4_MAX_TRANS_DATA;

	return ext4_chunk_trans_blocks(inode, max_blocks);
}

/*
 * write_cache_pages_da - walk the list of dirty pages of the given
 * address space and accumulate pages that need writing, and call
 * mpage_da_map_and_submit to map a single contiguous memory region
 * and then write them.
 */
static int write_cache_pages_da(struct address_space *mapping,
				struct writeback_control *wbc,
				struct mpage_da_data *mpd,
				pgoff_t *done_index)
{
	struct buffer_head	*bh, *head;
	struct inode		*inode = mapping->host;
	struct pagevec		pvec;
	unsigned int		nr_pages;
	sector_t		logical;
	pgoff_t			index, end;
	long			nr_to_write = wbc->nr_to_write;
	int			i, tag, ret = 0;

	memset(mpd, 0, sizeof(struct mpage_da_data));
	mpd->wbc = wbc;
	mpd->inode = inode;
	pagevec_init(&pvec, 0);
	index = wbc->range_start >> PAGE_CACHE_SHIFT;
	end = wbc->range_end >> PAGE_CACHE_SHIFT;

	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;

	*done_index = index;
	while (index <= end) {
		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index, tag,
			      min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
		if (nr_pages == 0)
			return 0;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			/*
			 * At this point, the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or
			 * even swizzled back from swapper_space to tmpfs file
			 * mapping. However, page->index will not change
			 * because we have a reference on the page.
			 */
			if (page->index > end)
				goto out;

			*done_index = page->index + 1;

			/*
			 * If we can't merge this page, and we have
			 * accumulated an contiguous region, write it
			 */
			if ((mpd->next_page != page->index) &&
			    (mpd->next_page != mpd->first_page)) {
				mpage_da_map_and_submit(mpd);
				goto ret_extent_tail;
			}

			lock_page(page);

			/*
			 * If the page is no longer dirty, or its
			 * mapping no longer corresponds to inode we
			 * are writing (which means it has been
			 * truncated or invalidated), or the page is
			 * already under writeback and we are not
			 * doing a data integrity writeback, skip the page
			 */
			if (!PageDirty(page) ||
			    (PageWriteback(page) &&
			     (wbc->sync_mode == WB_SYNC_NONE)) ||
			    unlikely(page->mapping != mapping)) {
				unlock_page(page);
				continue;
			}

			wait_on_page_writeback(page);
			BUG_ON(PageWriteback(page));

			if (mpd->next_page != page->index)
				mpd->first_page = page->index;
			mpd->next_page = page->index + 1;
			logical = (sector_t) page->index <<
				(PAGE_CACHE_SHIFT - inode->i_blkbits);

			if (!page_has_buffers(page)) {
				mpage_add_bh_to_extent(mpd, logical,
						       PAGE_CACHE_SIZE,
						       (1 << BH_Dirty) | (1 << BH_Uptodate));
				if (mpd->io_done)
					goto ret_extent_tail;
			} else {
				/*
				 * Page with regular buffer heads,
				 * just add all dirty ones
				 */
				head = page_buffers(page);
				bh = head;
				do {
					BUG_ON(buffer_locked(bh));
					/*
					 * We need to try to allocate
					 * unmapped blocks in the same page.
					 * Otherwise we won't make progress
					 * with the page in ext4_writepage
					 */
					if (ext4_bh_delay_or_unwritten(NULL, bh)) {
						mpage_add_bh_to_extent(mpd, logical,
								       bh->b_size,
								       bh->b_state);
						if (mpd->io_done)
							goto ret_extent_tail;
					} else if (buffer_dirty(bh) && (buffer_mapped(bh))) {
						/*
						 * mapped dirty buffer. We need
						 * to update the b_state
						 * because we look at b_state
						 * in mpage_da_map_blocks.  We
						 * don't update b_size because
						 * if we find an unmapped
						 * buffer_head later we need to
						 * use the b_state flag of that
						 * buffer_head.
						 */
						if (mpd->b_size == 0)
							mpd->b_state = bh->b_state & BH_FLAGS;
					}
					logical++;
				} while ((bh = bh->b_this_page) != head);
			}

			if (nr_to_write > 0) {
				nr_to_write--;
				if (nr_to_write == 0 &&
				    wbc->sync_mode == WB_SYNC_NONE)
					/*
					 * We stop writing back only if we are
					 * not doing integrity sync. In case of
					 * integrity sync we have to keep going
					 * because someone may be concurrently
					 * dirtying pages, and we might have
					 * synced a lot of newly appeared dirty
					 * pages, but have not synced all of the
					 * old dirty pages.
					 */
					goto out;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}
	return 0;
ret_extent_tail:
	ret = MPAGE_DA_EXTENT_TAIL;
out:
	pagevec_release(&pvec);
	cond_resched();
	return ret;
}


static int ext4_da_writepages(struct address_space *mapping,
			      struct writeback_control *wbc)
{
	pgoff_t	index;
	int range_whole = 0;
	handle_t *handle = NULL;
	struct mpage_da_data mpd;
	struct inode *inode = mapping->host;
	int pages_written = 0;
	unsigned int max_pages;
	int range_cyclic, cycled = 1, io_done = 0;
	int needed_blocks, ret = 0;
	long desired_nr_to_write, nr_to_writebump = 0;
	loff_t range_start = wbc->range_start;
	struct ext4_sb_info *sbi = EXT4_SB(mapping->host->i_sb);
	pgoff_t done_index = 0;
	pgoff_t end;

	trace_ext4_da_writepages(inode, wbc);

	/*
	 * No pages to write? This is mainly a kludge to avoid starting
	 * a transaction for special inodes like journal inode on last iput()
	 * because that could violate lock ordering on umount
	 */
	if (!mapping->nrpages || !mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		return 0;

	/*
	 * If the filesystem has aborted, it is read-only, so return
	 * right away instead of dumping stack traces later on that
	 * will obscure the real source of the problem.  We test
	 * EXT4_MF_FS_ABORTED instead of sb->s_flag's MS_RDONLY because
	 * the latter could be true if the filesystem is mounted
	 * read-only, and in that case, ext4_da_writepages should
	 * *never* be called, so if that ever happens, we would want
	 * the stack trace.
	 */
	if (unlikely(sbi->s_mount_flags & EXT4_MF_FS_ABORTED))
		return -EROFS;

	if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
		range_whole = 1;

	range_cyclic = wbc->range_cyclic;
	if (wbc->range_cyclic) {
		index = mapping->writeback_index;
		if (index)
			cycled = 0;
		wbc->range_start = index << PAGE_CACHE_SHIFT;
		wbc->range_end  = LLONG_MAX;
		wbc->range_cyclic = 0;
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
	}

	/*
	 * This works around two forms of stupidity.  The first is in
	 * the writeback code, which caps the maximum number of pages
	 * written to be 1024 pages.  This is wrong on multiple
	 * levels; different architectues have a different page size,
	 * which changes the maximum amount of data which gets
	 * written.  Secondly, 4 megabytes is way too small.  XFS
	 * forces this value to be 16 megabytes by multiplying
	 * nr_to_write parameter by four, and then relies on its
	 * allocator to allocate larger extents to make them
	 * contiguous.  Unfortunately this brings us to the second
	 * stupidity, which is that ext4's mballoc code only allocates
	 * at most 2048 blocks.  So we force contiguous writes up to
	 * the number of dirty blocks in the inode, or
	 * sbi->max_writeback_mb_bump whichever is smaller.
	 */
	max_pages = sbi->s_max_writeback_mb_bump << (20 - PAGE_CACHE_SHIFT);
	if (!range_cyclic && range_whole) {
		if (wbc->nr_to_write == LONG_MAX)
			desired_nr_to_write = wbc->nr_to_write;
		else
			desired_nr_to_write = wbc->nr_to_write * 8;
	} else
		desired_nr_to_write = ext4_num_dirty_pages(inode, index,
							   max_pages);
	if (desired_nr_to_write > max_pages)
		desired_nr_to_write = max_pages;

	if (wbc->nr_to_write < desired_nr_to_write) {
		nr_to_writebump = desired_nr_to_write - wbc->nr_to_write;
		wbc->nr_to_write = desired_nr_to_write;
	}

retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);

	while (!ret && wbc->nr_to_write > 0) {

		/*
		 * we  insert one extent at a time. So we need
		 * credit needed for single extent allocation.
		 * journalled mode is currently not supported
		 * by delalloc
		 */
		BUG_ON(ext4_should_journal_data(inode));
		needed_blocks = ext4_da_writepages_trans_blocks(inode);

		/* start a new transaction*/
		handle = ext4_journal_start(inode, needed_blocks);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			ext4_msg(inode->i_sb, KERN_CRIT, "%s: jbd2_start: "
			       "%ld pages, ino %lu; err %d", __func__,
				wbc->nr_to_write, inode->i_ino, ret);
			goto out_writepages;
		}

		/*
		 * Now call write_cache_pages_da() to find the next
		 * contiguous region of logical blocks that need
		 * blocks to be allocated by ext4 and submit them.
		 */
		ret = write_cache_pages_da(mapping, wbc, &mpd, &done_index);
		/*
		 * If we have a contiguous extent of pages and we
		 * haven't done the I/O yet, map the blocks and submit
		 * them for I/O.
		 */
		if (!mpd.io_done && mpd.next_page != mpd.first_page) {
			mpage_da_map_and_submit(&mpd);
			ret = MPAGE_DA_EXTENT_TAIL;
		}
		trace_ext4_da_write_pages(inode, &mpd);
		wbc->nr_to_write -= mpd.pages_written;

		ext4_journal_stop(handle);

		if ((mpd.retval == -ENOSPC) && sbi->s_journal) {
			/* commit the transaction which would
			 * free blocks released in the transaction
			 * and try again
			 */
			jbd2_journal_force_commit_nested(sbi->s_journal);
			ret = 0;
		} else if (ret == MPAGE_DA_EXTENT_TAIL) {
			/*
			 * got one extent now try with
			 * rest of the pages
			 */
			pages_written += mpd.pages_written;
			ret = 0;
			io_done = 1;
		} else if (wbc->nr_to_write)
			/*
			 * There is no more writeout needed
			 * or we requested for a noblocking writeout
			 * and we found the device congested
			 */
			break;
	}
	if (!io_done && !cycled) {
		cycled = 1;
		index = 0;
		wbc->range_start = index << PAGE_CACHE_SHIFT;
		wbc->range_end  = mapping->writeback_index - 1;
		goto retry;
	}

	/* Update index */
	wbc->range_cyclic = range_cyclic;
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		/*
		 * set the writeback_index so that range_cyclic
		 * mode will write it back later
		 */
		mapping->writeback_index = done_index;

out_writepages:
	wbc->nr_to_write -= nr_to_writebump;
	wbc->range_start = range_start;
	trace_ext4_da_writepages_result(inode, wbc, ret, pages_written);
	return ret;
}

#define FALL_BACK_TO_NONDELALLOC 1
static int ext4_nonda_switch(struct super_block *sb)
{
	s64 free_blocks, dirty_blocks;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	/*
	 * switch to non delalloc mode if we are running low
	 * on free block. The free block accounting via percpu
	 * counters can get slightly wrong with percpu_counter_batch getting
	 * accumulated on each CPU without updating global counters
	 * Delalloc need an accurate free block accounting. So switch
	 * to non delalloc when we are near to error range.
	 */
	free_blocks  = percpu_counter_read_positive(&sbi->s_freeblocks_counter);
	dirty_blocks = percpu_counter_read_positive(&sbi->s_dirtyblocks_counter);
	if (2 * free_blocks < 3 * dirty_blocks ||
		free_blocks < (dirty_blocks + EXT4_FREEBLOCKS_WATERMARK)) {
		/*
		 * free block count is less than 150% of dirty blocks
		 * or free blocks is less than watermark
		 */
		return 1;
	}
	/*
	 * Even if we don't switch but are nearing capacity,
	 * start pushing delalloc when 1/2 of free blocks are dirty.
	 */
	if (free_blocks < 2 * dirty_blocks)
		writeback_inodes_sb_if_idle(sb);

	return 0;
}

static int ext4_da_write_begin(struct file *file, struct address_space *mapping,
			       loff_t pos, unsigned len, unsigned flags,
			       struct page **pagep, void **fsdata)
{
	int ret, retries = 0;
	struct page *page;
	pgoff_t index;
	struct inode *inode = mapping->host;
	handle_t *handle;

	index = pos >> PAGE_CACHE_SHIFT;

	if (ext4_nonda_switch(inode->i_sb)) {
		*fsdata = (void *)FALL_BACK_TO_NONDELALLOC;
		return ext4_write_begin(file, mapping, pos,
					len, flags, pagep, fsdata);
	}
	*fsdata = (void *)0;
	trace_ext4_da_write_begin(inode, pos, len, flags);
retry:
	/*
	 * With delayed allocation, we don't log the i_disksize update
	 * if there is delayed block allocation. But we still need
	 * to journalling the i_disksize update if writes to the end
	 * of file which has an already mapped buffer.
	 */
	handle = ext4_journal_start(inode, 1);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}
	/* We cannot recurse into the filesystem as the transaction is already
	 * started */
	flags |= AOP_FLAG_NOFS;

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		ext4_journal_stop(handle);
		ret = -ENOMEM;
		goto out;
	}
	*pagep = page;

	ret = __block_write_begin(page, pos, len, ext4_da_get_block_prep);
	if (ret < 0) {
		unlock_page(page);
		ext4_journal_stop(handle);
		page_cache_release(page);
		/*
		 * block_write_begin may have instantiated a few blocks
		 * outside i_size.  Trim these off again. Don't need
		 * i_size_read because we hold i_mutex.
		 */
		if (pos + len > inode->i_size)
			ext4_truncate_failed_write(inode);
	}

	if (ret == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
		goto retry;
out:
	return ret;
}

/*
 * Check if we should update i_disksize
 * when write to the end of file but not require block allocation
 */
static int ext4_da_should_update_i_disksize(struct page *page,
					    unsigned long offset)
{
	struct buffer_head *bh;
	struct inode *inode = page->mapping->host;
	unsigned int idx;
	int i;

	bh = page_buffers(page);
	idx = offset >> inode->i_blkbits;

	for (i = 0; i < idx; i++)
		bh = bh->b_this_page;

	if (!buffer_mapped(bh) || (buffer_delay(bh)) || buffer_unwritten(bh))
		return 0;
	return 1;
}

static int ext4_da_write_end(struct file *file,
			     struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned copied,
			     struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	int ret = 0, ret2;
	handle_t *handle = ext4_journal_current_handle();
	loff_t new_i_size;
	unsigned long start, end;
	int write_mode = (int)(unsigned long)fsdata;

	if (write_mode == FALL_BACK_TO_NONDELALLOC) {
		if (ext4_should_order_data(inode)) {
			return ext4_ordered_write_end(file, mapping, pos,
					len, copied, page, fsdata);
		} else if (ext4_should_writeback_data(inode)) {
			return ext4_writeback_write_end(file, mapping, pos,
					len, copied, page, fsdata);
		} else {
			BUG();
		}
	}

	trace_ext4_da_write_end(inode, pos, len, copied);
	start = pos & (PAGE_CACHE_SIZE - 1);
	end = start + copied - 1;

	/*
	 * generic_write_end() will run mark_inode_dirty() if i_size
	 * changes.  So let's piggyback the i_disksize mark_inode_dirty
	 * into that.
	 */

	new_i_size = pos + copied;
	if (new_i_size > EXT4_I(inode)->i_disksize) {
		if (ext4_da_should_update_i_disksize(page, end)) {
			down_write(&EXT4_I(inode)->i_data_sem);
			if (new_i_size > EXT4_I(inode)->i_disksize) {
				/*
				 * Updating i_disksize when extending file
				 * without needing block allocation
				 */
				if (ext4_should_order_data(inode))
					ret = ext4_jbd2_file_inode(handle,
								   inode);

				EXT4_I(inode)->i_disksize = new_i_size;
			}
			up_write(&EXT4_I(inode)->i_data_sem);
			/* We need to mark inode dirty even if
			 * new_i_size is less that inode->i_size
			 * bu greater than i_disksize.(hint delalloc)
			 */
			ext4_mark_inode_dirty(handle, inode);
		}
	}
	ret2 = generic_write_end(file, mapping, pos, len, copied,
							page, fsdata);
	copied = ret2;
	if (ret2 < 0)
		ret = ret2;
	ret2 = ext4_journal_stop(handle);
	if (!ret)
		ret = ret2;

	return ret ? ret : copied;
}

static void ext4_da_invalidatepage(struct page *page, unsigned long offset)
{
	/*
	 * Drop reserved blocks
	 */
	BUG_ON(!PageLocked(page));
	if (!page_has_buffers(page))
		goto out;

	ext4_da_page_release_reservation(page, offset);

out:
	ext4_invalidatepage(page, offset);

	return;
}

/*
 * Force all delayed allocation blocks to be allocated for a given inode.
 */
int ext4_alloc_da_blocks(struct inode *inode)
{
	trace_ext4_alloc_da_blocks(inode);

	if (!EXT4_I(inode)->i_reserved_data_blocks &&
	    !EXT4_I(inode)->i_reserved_meta_blocks)
		return 0;

	/*
	 * We do something simple for now.  The filemap_flush() will
	 * also start triggering a write of the data blocks, which is
	 * not strictly speaking necessary (and for users of
	 * laptop_mode, not even desirable).  However, to do otherwise
	 * would require replicating code paths in:
	 *
	 * ext4_da_writepages() ->
	 *    write_cache_pages() ---> (via passed in callback function)
	 *        __mpage_da_writepage() -->
	 *           mpage_add_bh_to_extent()
	 *           mpage_da_map_blocks()
	 *
	 * The problem is that write_cache_pages(), located in
	 * mm/page-writeback.c, marks pages clean in preparation for
	 * doing I/O, which is not desirable if we're not planning on
	 * doing I/O at all.
	 *
	 * We could call write_cache_pages(), and then redirty all of
	 * the pages by calling redirty_page_for_writepage() but that
	 * would be ugly in the extreme.  So instead we would need to
	 * replicate parts of the code in the above functions,
	 * simplifying them because we wouldn't actually intend to
	 * write out the pages, but rather only collect contiguous
	 * logical block extents, call the multi-block allocator, and
	 * then update the buffer heads with the block allocations.
	 *
	 * For now, though, we'll cheat by calling filemap_flush(),
	 * which will map the blocks, and start the I/O, but not
	 * actually wait for the I/O to complete.
	 */
	return filemap_flush(inode->i_mapping);
}

/*
 * bmap() is special.  It gets used by applications such as lilo and by
 * the swapper to find the on-disk block of a specific piece of data.
 *
 * Naturally, this is dangerous if the block concerned is still in the
 * journal.  If somebody makes a swapfile on an ext4 data-journaling
 * filesystem and enables swap, then they may get a nasty shock when the
 * data getting swapped to that swapfile suddenly gets overwritten by
 * the original zero's written out previously to the journal and
 * awaiting writeback in the kernel's buffer cache.
 *
 * So, if we see any bmap calls here on a modified, data-journaled file,
 * take extra steps to flush any blocks which might be in the cache.
 */
static sector_t ext4_bmap(struct address_space *mapping, sector_t block)
{
	struct inode *inode = mapping->host;
	journal_t *journal;
	int err;

	if (mapping_tagged(mapping, PAGECACHE_TAG_DIRTY) &&
			test_opt(inode->i_sb, DELALLOC)) {
		/*
		 * With delalloc we want to sync the file
		 * so that we can make sure we allocate
		 * blocks for file
		 */
		filemap_write_and_wait(mapping);
	}

	if (EXT4_JOURNAL(inode) &&
	    ext4_test_inode_state(inode, EXT4_STATE_JDATA)) {
		/*
		 * This is a REALLY heavyweight approach, but the use of
		 * bmap on dirty files is expected to be extremely rare:
		 * only if we run lilo or swapon on a freshly made file
		 * do we expect this to happen.
		 *
		 * (bmap requires CAP_SYS_RAWIO so this does not
		 * represent an unprivileged user DOS attack --- we'd be
		 * in trouble if mortal users could trigger this path at
		 * will.)
		 *
		 * NB. EXT4_STATE_JDATA is not set on files other than
		 * regular files.  If somebody wants to bmap a directory
		 * or symlink and gets confused because the buffer
		 * hasn't yet been flushed to disk, they deserve
		 * everything they get.
		 */

		ext4_clear_inode_state(inode, EXT4_STATE_JDATA);
		journal = EXT4_JOURNAL(inode);
		jbd2_journal_lock_updates(journal);
		err = jbd2_journal_flush(journal);
		jbd2_journal_unlock_updates(journal);

		if (err)
			return 0;
	}

	return generic_block_bmap(mapping, block, ext4_get_block);
}

static int ext4_readpage(struct file *file, struct page *page)
{
	trace_ext4_readpage(page);
	return mpage_readpage(page, ext4_get_block);
}

static int
ext4_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, ext4_get_block);
}

static void ext4_invalidatepage_free_endio(struct page *page, unsigned long offset)
{
	struct buffer_head *head, *bh;
	unsigned int curr_off = 0;

	if (!page_has_buffers(page))
		return;
	head = bh = page_buffers(page);
	do {
		if (offset <= curr_off && test_clear_buffer_uninit(bh)
					&& bh->b_private) {
			ext4_free_io_end(bh->b_private);
			bh->b_private = NULL;
			bh->b_end_io = NULL;
		}
		curr_off = curr_off + bh->b_size;
		bh = bh->b_this_page;
	} while (bh != head);
}

static void ext4_invalidatepage(struct page *page, unsigned long offset)
{
	journal_t *journal = EXT4_JOURNAL(page->mapping->host);

	trace_ext4_invalidatepage(page, offset);

	/*
	 * free any io_end structure allocated for buffers to be discarded
	 */
	if (ext4_should_dioread_nolock(page->mapping->host))
		ext4_invalidatepage_free_endio(page, offset);
	/*
	 * If it's a full truncate we just forget about the pending dirtying
	 */
	if (offset == 0)
		ClearPageChecked(page);

	if (journal)
		jbd2_journal_invalidatepage(journal, page, offset);
	else
		block_invalidatepage(page, offset);
}

static int ext4_releasepage(struct page *page, gfp_t wait)
{
	journal_t *journal = EXT4_JOURNAL(page->mapping->host);

	trace_ext4_releasepage(page);

	WARN_ON(PageChecked(page));
	if (!page_has_buffers(page))
		return 0;
	if (journal)
		return jbd2_journal_try_to_free_buffers(journal, page, wait);
	else
		return try_to_free_buffers(page);
}

/*
 * O_DIRECT for ext3 (or indirect map) based files
 *
 * If the O_DIRECT write will extend the file then add this inode to the
 * orphan list.  So recovery will truncate it back to the original size
 * if the machine crashes during the write.
 *
 * If the O_DIRECT write is intantiating holes inside i_size and the machine
 * crashes then stale disk data _may_ be exposed inside the file. But current
 * VFS code falls back into buffered path in that case so we are safe.
 */
static ssize_t ext4_ind_direct_IO(int rw, struct kiocb *iocb,
			      const struct iovec *iov, loff_t offset,
			      unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct ext4_inode_info *ei = EXT4_I(inode);
	handle_t *handle;
	ssize_t ret;
	int orphan = 0;
	size_t count = iov_length(iov, nr_segs);
	int retries = 0;

	if (rw == WRITE) {
		loff_t final_size = offset + count;

		if (final_size > inode->i_size) {
			/* Credits for sb + inode write */
			handle = ext4_journal_start(inode, 2);
			if (IS_ERR(handle)) {
				ret = PTR_ERR(handle);
				goto out;
			}
			ret = ext4_orphan_add(handle, inode);
			if (ret) {
				ext4_journal_stop(handle);
				goto out;
			}
			orphan = 1;
			ei->i_disksize = inode->i_size;
			ext4_journal_stop(handle);
		}
	}

retry:
	if (rw == READ && ext4_should_dioread_nolock(inode))
		ret = __blockdev_direct_IO(rw, iocb, inode,
				 inode->i_sb->s_bdev, iov,
				 offset, nr_segs,
				 ext4_get_block, NULL, NULL, 0);
	else {
		ret = blockdev_direct_IO(rw, iocb, inode,
				 inode->i_sb->s_bdev, iov,
				 offset, nr_segs,
				 ext4_get_block, NULL);

		if (unlikely((rw & WRITE) && ret < 0)) {
			loff_t isize = i_size_read(inode);
			loff_t end = offset + iov_length(iov, nr_segs);

			if (end > isize)
				ext4_truncate_failed_write(inode);
		}
	}
	if (ret == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
		goto retry;

	if (orphan) {
		int err;

		/* Credits for sb + inode write */
		handle = ext4_journal_start(inode, 2);
		if (IS_ERR(handle)) {
			/* This is really bad luck. We've written the data
			 * but cannot extend i_size. Bail out and pretend
			 * the write failed... */
			ret = PTR_ERR(handle);
			if (inode->i_nlink)
				ext4_orphan_del(NULL, inode);

			goto out;
		}
		if (inode->i_nlink)
			ext4_orphan_del(handle, inode);
		if (ret > 0) {
			loff_t end = offset + ret;
			if (end > inode->i_size) {
				ei->i_disksize = end;
				i_size_write(inode, end);
				/*
				 * We're going to return a positive `ret'
				 * here due to non-zero-length I/O, so there's
				 * no way of reporting error returns from
				 * ext4_mark_inode_dirty() to userspace.  So
				 * ignore it.
				 */
				ext4_mark_inode_dirty(handle, inode);
			}
		}
		err = ext4_journal_stop(handle);
		if (ret == 0)
			ret = err;
	}
out:
	return ret;
}

/*
 * ext4_get_block used when preparing for a DIO write or buffer write.
 * We allocate an uinitialized extent if blocks haven't been allocated.
 * The extent will be converted to initialized after the IO is complete.
 */
static int ext4_get_block_write(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create)
{
	ext4_debug("ext4_get_block_write: inode %lu, create flag %d\n",
		   inode->i_ino, create);
	return _ext4_get_block(inode, iblock, bh_result,
			       EXT4_GET_BLOCKS_IO_CREATE_EXT);
}

static void ext4_end_io_dio(struct kiocb *iocb, loff_t offset,
			    ssize_t size, void *private, int ret,
			    bool is_async)
{
        ext4_io_end_t *io_end = iocb->private;
	struct workqueue_struct *wq;
	unsigned long flags;
	struct ext4_inode_info *ei;

	/* if not async direct IO or dio with 0 bytes write, just return */
	if (!io_end || !size)
		goto out;

	ext_debug("ext4_end_io_dio(): io_end 0x%p"
		  "for inode %lu, iocb 0x%p, offset %llu, size %llu\n",
 		  iocb->private, io_end->inode->i_ino, iocb, offset,
		  size);

	/* if not aio dio with unwritten extents, just free io and return */
	if (!(io_end->flag & EXT4_IO_END_UNWRITTEN)) {
		ext4_free_io_end(io_end);
		iocb->private = NULL;
out:
		if (is_async)
			aio_complete(iocb, ret, 0);
		return;
	}

	io_end->offset = offset;
	io_end->size = size;
	if (is_async) {
		io_end->iocb = iocb;
		io_end->result = ret;
	}
	wq = EXT4_SB(io_end->inode->i_sb)->dio_unwritten_wq;

	/* Add the io_end to per-inode completed aio dio list*/
	ei = EXT4_I(io_end->inode);
	spin_lock_irqsave(&ei->i_completed_io_lock, flags);
	list_add_tail(&io_end->list, &ei->i_completed_io_list);
	spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);

	/* queue the work to convert unwritten extents to written */
	queue_work(wq, &io_end->work);
	iocb->private = NULL;
}

static void ext4_end_io_buffer_write(struct buffer_head *bh, int uptodate)
{
	ext4_io_end_t *io_end = bh->b_private;
	struct workqueue_struct *wq;
	struct inode *inode;
	unsigned long flags;

	if (!test_clear_buffer_uninit(bh) || !io_end)
		goto out;

	if (!(io_end->inode->i_sb->s_flags & MS_ACTIVE)) {
		printk("sb umounted, discard end_io request for inode %lu\n",
			io_end->inode->i_ino);
		ext4_free_io_end(io_end);
		goto out;
	}

	/*
	 * It may be over-defensive here to check EXT4_IO_END_UNWRITTEN now,
	 * but being more careful is always safe for the future change.
	 */
	inode = io_end->inode;
	if (!(io_end->flag & EXT4_IO_END_UNWRITTEN)) {
		io_end->flag |= EXT4_IO_END_UNWRITTEN;
		atomic_inc(&EXT4_I(inode)->i_aiodio_unwritten);
	}

	/* Add the io_end to per-inode completed io list*/
	spin_lock_irqsave(&EXT4_I(inode)->i_completed_io_lock, flags);
	list_add_tail(&io_end->list, &EXT4_I(inode)->i_completed_io_list);
	spin_unlock_irqrestore(&EXT4_I(inode)->i_completed_io_lock, flags);

	wq = EXT4_SB(inode->i_sb)->dio_unwritten_wq;
	/* queue the work to convert unwritten extents to written */
	queue_work(wq, &io_end->work);
out:
	bh->b_private = NULL;
	bh->b_end_io = NULL;
	clear_buffer_uninit(bh);
	end_buffer_async_write(bh, uptodate);
}

static int ext4_set_bh_endio(struct buffer_head *bh, struct inode *inode)
{
	ext4_io_end_t *io_end;
	struct page *page = bh->b_page;
	loff_t offset = (sector_t)page->index << PAGE_CACHE_SHIFT;
	size_t size = bh->b_size;

retry:
	io_end = ext4_init_io_end(inode, GFP_ATOMIC);
	if (!io_end) {
		pr_warn_ratelimited("%s: allocation fail\n", __func__);
		schedule();
		goto retry;
	}
	io_end->offset = offset;
	io_end->size = size;
	/*
	 * We need to hold a reference to the page to make sure it
	 * doesn't get evicted before ext4_end_io_work() has a chance
	 * to convert the extent from written to unwritten.
	 */
	io_end->page = page;
	get_page(io_end->page);

	bh->b_private = io_end;
	bh->b_end_io = ext4_end_io_buffer_write;
	return 0;
}

/*
 * For ext4 extent files, ext4 will do direct-io write to holes,
 * preallocated extents, and those write extend the file, no need to
 * fall back to buffered IO.
 *
 * For holes, we fallocate those blocks, mark them as uninitialized
 * If those blocks were preallocated, we mark sure they are splited, but
 * still keep the range to write as uninitialized.
 *
 * The unwrritten extents will be converted to written when DIO is completed.
 * For async direct IO, since the IO may still pending when return, we
 * set up an end_io call back function, which will do the conversion
 * when async direct IO completed.
 *
 * If the O_DIRECT write will extend the file then add this inode to the
 * orphan list.  So recovery will truncate it back to the original size
 * if the machine crashes during the write.
 *
 */
static ssize_t ext4_ext_direct_IO(int rw, struct kiocb *iocb,
			      const struct iovec *iov, loff_t offset,
			      unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;
	size_t count = iov_length(iov, nr_segs);

	loff_t final_size = offset + count;
	if (rw == WRITE && final_size <= inode->i_size) {
		/*
 		 * We could direct write to holes and fallocate.
		 *
 		 * Allocated blocks to fill the hole are marked as uninitialized
 		 * to prevent parallel buffered read to expose the stale data
 		 * before DIO complete the data IO.
		 *
 		 * As to previously fallocated extents, ext4 get_block
 		 * will just simply mark the buffer mapped but still
 		 * keep the extents uninitialized.
 		 *
		 * for non AIO case, we will convert those unwritten extents
		 * to written after return back from blockdev_direct_IO.
		 *
		 * for async DIO, the conversion needs to be defered when
		 * the IO is completed. The ext4 end_io callback function
		 * will be called to take care of the conversion work.
		 * Here for async case, we allocate an io_end structure to
		 * hook to the iocb.
 		 */
		iocb->private = NULL;
		EXT4_I(inode)->cur_aio_dio = NULL;
		if (!is_sync_kiocb(iocb)) {
			iocb->private = ext4_init_io_end(inode, GFP_NOFS);
			if (!iocb->private)
				return -ENOMEM;
			/*
			 * we save the io structure for current async
			 * direct IO, so that later ext4_map_blocks()
			 * could flag the io structure whether there
			 * is a unwritten extents needs to be converted
			 * when IO is completed.
			 */
			EXT4_I(inode)->cur_aio_dio = iocb->private;
		}

		ret = blockdev_direct_IO(rw, iocb, inode,
					 inode->i_sb->s_bdev, iov,
					 offset, nr_segs,
					 ext4_get_block_write,
					 ext4_end_io_dio);
		if (iocb->private)
			EXT4_I(inode)->cur_aio_dio = NULL;
		/*
		 * The io_end structure takes a reference to the inode,
		 * that structure needs to be destroyed and the
		 * reference to the inode need to be dropped, when IO is
		 * complete, even with 0 byte write, or failed.
		 *
		 * In the successful AIO DIO case, the io_end structure will be
		 * desctroyed and the reference to the inode will be dropped
		 * after the end_io call back function is called.
		 *
		 * In the case there is 0 byte write, or error case, since
		 * VFS direct IO won't invoke the end_io call back function,
		 * we need to free the end_io structure here.
		 */
		if (ret != -EIOCBQUEUED && ret <= 0 && iocb->private) {
			ext4_free_io_end(iocb->private);
			iocb->private = NULL;
		} else if (ret > 0 && ext4_test_inode_state(inode,
						EXT4_STATE_DIO_UNWRITTEN)) {
			int err;
			/*
			 * for non AIO case, since the IO is already
			 * completed, we could do the conversion right here
			 */
			err = ext4_convert_unwritten_extents(inode,
							     offset, ret);
			if (err < 0)
				ret = err;
			ext4_clear_inode_state(inode, EXT4_STATE_DIO_UNWRITTEN);
		}
		return ret;
	}

	/* for write the the end of file case, we fall back to old way */
	return ext4_ind_direct_IO(rw, iocb, iov, offset, nr_segs);
}

static ssize_t ext4_direct_IO(int rw, struct kiocb *iocb,
			      const struct iovec *iov, loff_t offset,
			      unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;

	trace_ext4_direct_IO_enter(inode, offset, iov_length(iov, nr_segs), rw);
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		ret = ext4_ext_direct_IO(rw, iocb, iov, offset, nr_segs);
	else
		ret = ext4_ind_direct_IO(rw, iocb, iov, offset, nr_segs);
	trace_ext4_direct_IO_exit(inode, offset,
				iov_length(iov, nr_segs), rw, ret);
	return ret;
}

/*
 * Pages can be marked dirty completely asynchronously from ext4's journalling
 * activity.  By filemap_sync_pte(), try_to_unmap_one(), etc.  We cannot do
 * much here because ->set_page_dirty is called under VFS locks.  The page is
 * not necessarily locked.
 *
 * We cannot just dirty the page and leave attached buffers clean, because the
 * buffers' dirty state is "definitive".  We cannot just set the buffers dirty
 * or jbddirty because all the journalling code will explode.
 *
 * So what we do is to mark the page "pending dirty" and next time writepage
 * is called, propagate that into the buffers appropriately.
 */
static int ext4_journalled_set_page_dirty(struct page *page)
{
	SetPageChecked(page);
	return __set_page_dirty_nobuffers(page);
}

static const struct address_space_operations ext4_ordered_aops = {
	.readpage		= ext4_readpage,
	.readpages		= ext4_readpages,
	.writepage		= ext4_writepage,
	.write_begin		= ext4_write_begin,
	.write_end		= ext4_ordered_write_end,
	.bmap			= ext4_bmap,
	.invalidatepage		= ext4_invalidatepage,
	.releasepage		= ext4_releasepage,
	.direct_IO		= ext4_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

static const struct address_space_operations ext4_writeback_aops = {
	.readpage		= ext4_readpage,
	.readpages		= ext4_readpages,
	.writepage		= ext4_writepage,
	.write_begin		= ext4_write_begin,
	.write_end		= ext4_writeback_write_end,
	.bmap			= ext4_bmap,
	.invalidatepage		= ext4_invalidatepage,
	.releasepage		= ext4_releasepage,
	.direct_IO		= ext4_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

static const struct address_space_operations ext4_journalled_aops = {
	.readpage		= ext4_readpage,
	.readpages		= ext4_readpages,
	.writepage		= ext4_writepage,
	.write_begin		= ext4_write_begin,
	.write_end		= ext4_journalled_write_end,
	.set_page_dirty		= ext4_journalled_set_page_dirty,
	.bmap			= ext4_bmap,
	.invalidatepage		= ext4_invalidatepage,
	.releasepage		= ext4_releasepage,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

static const struct address_space_operations ext4_da_aops = {
	.readpage		= ext4_readpage,
	.readpages		= ext4_readpages,
	.writepage		= ext4_writepage,
	.writepages		= ext4_da_writepages,
	.write_begin		= ext4_da_write_begin,
	.write_end		= ext4_da_write_end,
	.bmap			= ext4_bmap,
	.invalidatepage		= ext4_da_invalidatepage,
	.releasepage		= ext4_releasepage,
	.direct_IO		= ext4_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

void ext4_set_aops(struct inode *inode)
{
	if (ext4_should_order_data(inode) &&
		test_opt(inode->i_sb, DELALLOC))
		inode->i_mapping->a_ops = &ext4_da_aops;
	else if (ext4_should_order_data(inode))
		inode->i_mapping->a_ops = &ext4_ordered_aops;
	else if (ext4_should_writeback_data(inode) &&
		 test_opt(inode->i_sb, DELALLOC))
		inode->i_mapping->a_ops = &ext4_da_aops;
	else if (ext4_should_writeback_data(inode))
		inode->i_mapping->a_ops = &ext4_writeback_aops;
	else
		inode->i_mapping->a_ops = &ext4_journalled_aops;
}

/*
 * ext4_block_truncate_page() zeroes out a mapping from file offset `from'
 * up to the end of the block which corresponds to `from'.
 * This required during truncate. We need to physically zero the tail end
 * of that block so it doesn't yield old data if the file is later grown.
 */
int ext4_block_truncate_page(handle_t *handle,
		struct address_space *mapping, loff_t from)
{
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned length;
	unsigned blocksize;
	struct inode *inode = mapping->host;

	blocksize = inode->i_sb->s_blocksize;
	length = blocksize - (offset & (blocksize - 1));

	return ext4_block_zero_page_range(handle, mapping, from, length);
}

/*
 * ext4_block_zero_page_range() zeros out a mapping of length 'length'
 * starting from file offset 'from'.  The range to be zero'd must
 * be contained with in one block.  If the specified range exceeds
 * the end of the block it will be shortened to end of the block
 * that cooresponds to 'from'
 */
int ext4_block_zero_page_range(handle_t *handle,
		struct address_space *mapping, loff_t from, loff_t length)
{
	ext4_fsblk_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned blocksize, max, pos;
	ext4_lblk_t iblock;
	struct inode *inode = mapping->host;
	struct buffer_head *bh;
	struct page *page;
	int err = 0;

	page = find_or_create_page(mapping, from >> PAGE_CACHE_SHIFT,
				   mapping_gfp_mask(mapping) & ~__GFP_FS);
	if (!page)
		return -EINVAL;

	blocksize = inode->i_sb->s_blocksize;
	max = blocksize - (offset & (blocksize - 1));

	/*
	 * correct length if it does not fall between
	 * 'from' and the end of the block
	 */
	if (length > max || length < 0)
		length = max;

	iblock = index << (PAGE_CACHE_SHIFT - inode->i_sb->s_blocksize_bits);

	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);

	/* Find the buffer that contains "offset" */
	bh = page_buffers(page);
	pos = blocksize;
	while (offset >= pos) {
		bh = bh->b_this_page;
		iblock++;
		pos += blocksize;
	}

	err = 0;
	if (buffer_freed(bh)) {
		BUFFER_TRACE(bh, "freed: skip");
		goto unlock;
	}

	if (!buffer_mapped(bh)) {
		BUFFER_TRACE(bh, "unmapped");
		ext4_get_block(inode, iblock, bh, 0);
		/* unmapped? It's a hole - nothing to do */
		if (!buffer_mapped(bh)) {
			BUFFER_TRACE(bh, "still unmapped");
			goto unlock;
		}
	}

	/* Ok, it's mapped. Make sure it's up-to-date */
	if (PageUptodate(page))
		set_buffer_uptodate(bh);

	if (!buffer_uptodate(bh)) {
		err = -EIO;
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		/* Uhhuh. Read error. Complain and punt. */
		if (!buffer_uptodate(bh))
			goto unlock;
	}

	if (ext4_should_journal_data(inode)) {
		BUFFER_TRACE(bh, "get write access");
		err = ext4_journal_get_write_access(handle, bh);
		if (err)
			goto unlock;
	}

	zero_user(page, offset, length);

	BUFFER_TRACE(bh, "zeroed end of block");

	err = 0;
	if (ext4_should_journal_data(inode)) {
		err = ext4_handle_dirty_metadata(handle, inode, bh);
	} else {
		if (ext4_should_order_data(inode) && EXT4_I(inode)->jinode)
			err = ext4_jbd2_file_inode(handle, inode);
		mark_buffer_dirty(bh);
	}

unlock:
	unlock_page(page);
	page_cache_release(page);
	return err;
}

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
 *	ext4_find_shared - find the indirect blocks for partial truncation.
 *	@inode:	  inode in question
 *	@depth:	  depth of the affected branch
 *	@offsets: offsets of pointers in that branch (see ext4_block_to_path)
 *	@chain:	  place to store the pointers to partial indirect blocks
 *	@top:	  place to the (detached) top of branch
 *
 *	This is a helper function used by ext4_truncate().
 *
 *	When we do truncate() we may have to clean the ends of several
 *	indirect blocks but leave the blocks themselves alive. Block is
 *	partially truncated if some data below the new i_size is referred
 *	from it (and it is on the path to the first completely truncated
 *	data block, indeed).  We have to free the top of that path along
 *	with everything to the right of the path. Since no allocation
 *	past the truncation point is possible until ext4_truncate()
 *	finishes, we may safely do the latter, but top of branch may
 *	require special attention - pageout below the truncation point
 *	might try to populate it.
 *
 *	We atomically detach the top of branch from the tree, store the
 *	block number of its root in *@top, pointers to buffer_heads of
 *	partially truncated blocks - in @chain[].bh and pointers to
 *	their last elements that should not be removed - in
 *	@chain[].p. Return value is the pointer to last filled element
 *	of @chain.
 *
 *	The work left to caller to do the actual freeing of subtrees:
 *		a) free the subtree starting from *@top
 *		b) free the subtrees whose roots are stored in
 *			(@chain[i].p+1 .. end of @chain[i].bh->b_data)
 *		c) free the subtrees growing from the inode past the @chain[0].
 *			(no partially truncated stuff there).  */

static Indirect *ext4_find_shared(struct inode *inode, int depth,
				  ext4_lblk_t offsets[4], Indirect chain[4],
				  __le32 *top)
{
	Indirect *partial, *p;
	int k, err;

	*top = 0;
	/* Make k index the deepest non-null offset + 1 */
	for (k = depth; k > 1 && !offsets[k-1]; k--)
		;
	partial = ext4_get_branch(inode, k, offsets, chain, &err);
	/* Writer: pointers */
	if (!partial)
		partial = chain + k-1;
	/*
	 * If the branch acquired continuation since we've looked at it -
	 * fine, it should all survive and (new) top doesn't belong to us.
	 */
	if (!partial->key && *partial->p)
		/* Writer: end */
		goto no_top;
	for (p = partial; (p > chain) && all_zeroes((__le32 *) p->bh->b_data, p->p); p--)
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
		/* Nope, don't do this in ext4.  Must leave the tree intact */
#if 0
		*p->p = 0;
#endif
	}
	/* Writer: end */

	while (partial > p) {
		brelse(partial->bh);
		partial--;
	}
no_top:
	return partial;
}

/*
 * Zero a number of block pointers in either an inode or an indirect block.
 * If we restart the transaction we must again get write access to the
 * indirect block for further modification.
 *
 * We release `count' blocks on disk, but (last - first) may be greater
 * than `count' because there can be holes in there.
 *
 * Return 0 on success, 1 on invalid block range
 * and < 0 on fatal error.
 */
static int ext4_clear_blocks(handle_t *handle, struct inode *inode,
			     struct buffer_head *bh,
			     ext4_fsblk_t block_to_free,
			     unsigned long count, __le32 *first,
			     __le32 *last)
{
	__le32 *p;
	int	flags = EXT4_FREE_BLOCKS_FORGET | EXT4_FREE_BLOCKS_VALIDATED;
	int	err;

	if (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode))
		flags |= EXT4_FREE_BLOCKS_METADATA;

	if (!ext4_data_block_valid(EXT4_SB(inode->i_sb), block_to_free,
				   count)) {
		EXT4_ERROR_INODE(inode, "attempt to clear invalid "
				 "blocks %llu len %lu",
				 (unsigned long long) block_to_free, count);
		return 1;
	}

	if (try_to_extend_transaction(handle, inode)) {
		if (bh) {
			BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
			err = ext4_handle_dirty_metadata(handle, inode, bh);
			if (unlikely(err))
				goto out_err;
		}
		err = ext4_mark_inode_dirty(handle, inode);
		if (unlikely(err))
			goto out_err;
		err = ext4_truncate_restart_trans(handle, inode,
						  blocks_for_truncate(inode));
		if (unlikely(err))
			goto out_err;
		if (bh) {
			BUFFER_TRACE(bh, "retaking write access");
			err = ext4_journal_get_write_access(handle, bh);
			if (unlikely(err))
				goto out_err;
		}
	}

	for (p = first; p < last; p++)
		*p = 0;

	ext4_free_blocks(handle, inode, NULL, block_to_free, count, flags);
	return 0;
out_err:
	ext4_std_error(inode->i_sb, err);
	return err;
}

/**
 * ext4_free_data - free a list of data blocks
 * @handle:	handle for this transaction
 * @inode:	inode we are dealing with
 * @this_bh:	indirect buffer_head which contains *@first and *@last
 * @first:	array of block numbers
 * @last:	points immediately past the end of array
 *
 * We are freeing all blocks referred from that array (numbers are stored as
 * little-endian 32-bit) and updating @inode->i_blocks appropriately.
 *
 * We accumulate contiguous runs of blocks to free.  Conveniently, if these
 * blocks are contiguous then releasing them at one time will only affect one
 * or two bitmap blocks (+ group descriptor(s) and superblock) and we won't
 * actually use a lot of journal space.
 *
 * @this_bh will be %NULL if @first and @last point into the inode's direct
 * block pointers.
 */
static void ext4_free_data(handle_t *handle, struct inode *inode,
			   struct buffer_head *this_bh,
			   __le32 *first, __le32 *last)
{
	ext4_fsblk_t block_to_free = 0;    /* Starting block # of a run */
	unsigned long count = 0;	    /* Number of blocks in the run */
	__le32 *block_to_free_p = NULL;	    /* Pointer into inode/ind
					       corresponding to
					       block_to_free */
	ext4_fsblk_t nr;		    /* Current block # */
	__le32 *p;			    /* Pointer into inode/ind
					       for current block */
	int err = 0;

	if (this_bh) {				/* For indirect block */
		BUFFER_TRACE(this_bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, this_bh);
		/* Important: if we can't update the indirect pointers
		 * to the blocks, we can't free them. */
		if (err)
			return;
	}

	for (p = first; p < last; p++) {
		nr = le32_to_cpu(*p);
		if (nr) {
			/* accumulate blocks to free if they're contiguous */
			if (count == 0) {
				block_to_free = nr;
				block_to_free_p = p;
				count = 1;
			} else if (nr == block_to_free + count) {
				count++;
			} else {
				err = ext4_clear_blocks(handle, inode, this_bh,
						        block_to_free, count,
						        block_to_free_p, p);
				if (err)
					break;
				block_to_free = nr;
				block_to_free_p = p;
				count = 1;
			}
		}
	}

	if (!err && count > 0)
		err = ext4_clear_blocks(handle, inode, this_bh, block_to_free,
					count, block_to_free_p, p);
	if (err < 0)
		/* fatal error */
		return;

	if (this_bh) {
		BUFFER_TRACE(this_bh, "call ext4_handle_dirty_metadata");

		/*
		 * The buffer head should have an attached journal head at this
		 * point. However, if the data is corrupted and an indirect
		 * block pointed to itself, it would have been detached when
		 * the block was cleared. Check for this instead of OOPSing.
		 */
		if ((EXT4_JOURNAL(inode) == NULL) || bh2jh(this_bh))
			ext4_handle_dirty_metadata(handle, inode, this_bh);
		else
			EXT4_ERROR_INODE(inode,
					 "circular indirect block detected at "
					 "block %llu",
				(unsigned long long) this_bh->b_blocknr);
	}
}

/**
 *	ext4_free_branches - free an array of branches
 *	@handle: JBD handle for this transaction
 *	@inode:	inode we are dealing with
 *	@parent_bh: the buffer_head which contains *@first and *@last
 *	@first:	array of block numbers
 *	@last:	pointer immediately past the end of array
 *	@depth:	depth of the branches to free
 *
 *	We are freeing all blocks referred from these branches (numbers are
 *	stored as little-endian 32-bit) and updating @inode->i_blocks
 *	appropriately.
 */
static void ext4_free_branches(handle_t *handle, struct inode *inode,
			       struct buffer_head *parent_bh,
			       __le32 *first, __le32 *last, int depth)
{
	ext4_fsblk_t nr;
	__le32 *p;

	if (ext4_handle_is_aborted(handle))
		return;

	if (depth--) {
		struct buffer_head *bh;
		int addr_per_block = EXT4_ADDR_PER_BLOCK(inode->i_sb);
		p = last;
		while (--p >= first) {
			nr = le32_to_cpu(*p);
			if (!nr)
				continue;		/* A hole */

			if (!ext4_data_block_valid(EXT4_SB(inode->i_sb),
						   nr, 1)) {
				EXT4_ERROR_INODE(inode,
						 "invalid indirect mapped "
						 "block %lu (level %d)",
						 (unsigned long) nr, depth);
				break;
			}

			/* Go read the buffer for the next level down */
			bh = sb_bread(inode->i_sb, nr);

			/*
			 * A read failure? Report error and clear slot
			 * (should be rare).
			 */
			if (!bh) {
				EXT4_ERROR_INODE_BLOCK(inode, nr,
						       "Read failure");
				continue;
			}

			/* This zaps the entire block.  Bottom up. */
			BUFFER_TRACE(bh, "free child branches");
			ext4_free_branches(handle, inode, bh,
					(__le32 *) bh->b_data,
					(__le32 *) bh->b_data + addr_per_block,
					depth);
			brelse(bh);

			/*
			 * Everything below this this pointer has been
			 * released.  Now let this top-of-subtree go.
			 *
			 * We want the freeing of this indirect block to be
			 * atomic in the journal with the updating of the
			 * bitmap block which owns it.  So make some room in
			 * the journal.
			 *
			 * We zero the parent pointer *after* freeing its
			 * pointee in the bitmaps, so if extend_transaction()
			 * for some reason fails to put the bitmap changes and
			 * the release into the same transaction, recovery
			 * will merely complain about releasing a free block,
			 * rather than leaking blocks.
			 */
			if (ext4_handle_is_aborted(handle))
				return;
			if (try_to_extend_transaction(handle, inode)) {
				ext4_mark_inode_dirty(handle, inode);
				ext4_truncate_restart_trans(handle, inode,
					    blocks_for_truncate(inode));
			}

			/*
			 * The forget flag here is critical because if
			 * we are journaling (and not doing data
			 * journaling), we have to make sure a revoke
			 * record is written to prevent the journal
			 * replay from overwriting the (former)
			 * indirect block if it gets reallocated as a
			 * data block.  This must happen in the same
			 * transaction where the data blocks are
			 * actually freed.
			 */
			ext4_free_blocks(handle, inode, NULL, nr, 1,
					 EXT4_FREE_BLOCKS_METADATA|
					 EXT4_FREE_BLOCKS_FORGET);

			if (parent_bh) {
				/*
				 * The block which we have just freed is
				 * pointed to by an indirect block: journal it
				 */
				BUFFER_TRACE(parent_bh, "get_write_access");
				if (!ext4_journal_get_write_access(handle,
								   parent_bh)){
					*p = 0;
					BUFFER_TRACE(parent_bh,
					"call ext4_handle_dirty_metadata");
					ext4_handle_dirty_metadata(handle,
								   inode,
								   parent_bh);
				}
			}
		}
	} else {
		/* We have reached the bottom of the tree. */
		BUFFER_TRACE(parent_bh, "free data blocks");
		ext4_free_data(handle, inode, parent_bh, first, last);
	}
}

int ext4_can_truncate(struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		return 1;
	if (S_ISDIR(inode->i_mode))
		return 1;
	if (S_ISLNK(inode->i_mode))
		return !ext4_inode_is_fast_symlink(inode);
	return 0;
}

/*
 * ext4_punch_hole: punches a hole in a file by releaseing the blocks
 * associated with the given offset and length
 *
 * @inode:  File inode
 * @offset: The offset where the hole will begin
 * @len:    The length of the hole
 *
 * Returns: 0 on sucess or negative on failure
 */

int ext4_punch_hole(struct file *file, loff_t offset, loff_t length)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	if (!S_ISREG(inode->i_mode))
		return -ENOTSUPP;

	if (!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		/* TODO: Add support for non extent hole punching */
		return -ENOTSUPP;
	}

	return ext4_ext_punch_hole(file, offset, length);
}

/*
 * ext4_truncate()
 *
 * We block out ext4_get_block() block instantiations across the entire
 * transaction, and VFS/VM ensures that ext4_truncate() cannot run
 * simultaneously on behalf of the same inode.
 *
 * As we work through the truncate and commmit bits of it to the journal there
 * is one core, guiding principle: the file's tree must always be consistent on
 * disk.  We must be able to restart the truncate after a crash.
 *
 * The file's tree may be transiently inconsistent in memory (although it
 * probably isn't), but whenever we close off and commit a journal transaction,
 * the contents of (the filesystem + the journal) must be consistent and
 * restartable.  It's pretty simple, really: bottom up, right to left (although
 * left-to-right works OK too).
 *
 * Note that at recovery time, journal replay occurs *before* the restart of
 * truncate against the orphan inode list.
 *
 * The committed inode has the new, desired i_size (which is the same as
 * i_disksize in this case).  After a crash, ext4_orphan_cleanup() will see
 * that this inode's truncate did not complete and it will again call
 * ext4_truncate() to have another go.  So there will be instantiated blocks
 * to the right of the truncation point in a crashed ext4 filesystem.  But
 * that's fine - as long as they are linked from the inode, the post-crash
 * ext4_truncate() run will find them and release them.
 */
void ext4_truncate(struct inode *inode)
{
	handle_t *handle;
	struct ext4_inode_info *ei = EXT4_I(inode);
	__le32 *i_data = ei->i_data;
	int addr_per_block = EXT4_ADDR_PER_BLOCK(inode->i_sb);
	struct address_space *mapping = inode->i_mapping;
	ext4_lblk_t offsets[4];
	Indirect chain[4];
	Indirect *partial;
	__le32 nr = 0;
	int n = 0;
	ext4_lblk_t last_block, max_block;
	unsigned blocksize = inode->i_sb->s_blocksize;

	trace_ext4_truncate_enter(inode);

	if (!ext4_can_truncate(inode))
		return;

	ext4_clear_inode_flag(inode, EXT4_INODE_EOFBLOCKS);

	if (inode->i_size == 0 && !test_opt(inode->i_sb, NO_AUTO_DA_ALLOC))
		ext4_set_inode_state(inode, EXT4_STATE_DA_ALLOC_CLOSE);

	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		ext4_ext_truncate(inode);
		trace_ext4_truncate_exit(inode);
		return;
	}

	handle = start_transaction(inode);
	if (IS_ERR(handle))
		return;		/* AKPM: return what? */

	last_block = (inode->i_size + blocksize-1)
					>> EXT4_BLOCK_SIZE_BITS(inode->i_sb);
	max_block = (EXT4_SB(inode->i_sb)->s_bitmap_maxbytes + blocksize-1)
					>> EXT4_BLOCK_SIZE_BITS(inode->i_sb);

	if (inode->i_size & (blocksize - 1))
		if (ext4_block_truncate_page(handle, mapping, inode->i_size))
			goto out_stop;

	if (last_block != max_block) {
		n = ext4_block_to_path(inode, last_block, offsets, NULL);
		if (n == 0)
			goto out_stop;	/* error */
	}

	/*
	 * OK.  This truncate is going to happen.  We add the inode to the
	 * orphan list, so that if this truncate spans multiple transactions,
	 * and we crash, we will resume the truncate when the filesystem
	 * recovers.  It also marks the inode dirty, to catch the new size.
	 *
	 * Implication: the file must always be in a sane, consistent
	 * truncatable state while each transaction commits.
	 */
	if (ext4_orphan_add(handle, inode))
		goto out_stop;

	/*
	 * From here we block out all ext4_get_block() callers who want to
	 * modify the block allocation tree.
	 */
	down_write(&ei->i_data_sem);

	ext4_discard_preallocations(inode);

	/*
	 * The orphan list entry will now protect us from any crash which
	 * occurs before the truncate completes, so it is now safe to propagate
	 * the new, shorter inode size (held for now in i_size) into the
	 * on-disk inode. We do this via i_disksize, which is the value which
	 * ext4 *really* writes onto the disk inode.
	 */
	ei->i_disksize = inode->i_size;

	if (last_block == max_block) {
		/*
		 * It is unnecessary to free any data blocks if last_block is
		 * equal to the indirect block limit.
		 */
		goto out_unlock;
	} else if (n == 1) {		/* direct blocks */
		ext4_free_data(handle, inode, NULL, i_data+offsets[0],
			       i_data + EXT4_NDIR_BLOCKS);
		goto do_indirects;
	}

	partial = ext4_find_shared(inode, n, offsets, chain, &nr);
	/* Kill the top of shared branch (not detached) */
	if (nr) {
		if (partial == chain) {
			/* Shared branch grows from the inode */
			ext4_free_branches(handle, inode, NULL,
					   &nr, &nr+1, (chain+n-1) - partial);
			*partial->p = 0;
			/*
			 * We mark the inode dirty prior to restart,
			 * and prior to stop.  No need for it here.
			 */
		} else {
			/* Shared branch grows from an indirect block */
			BUFFER_TRACE(partial->bh, "get_write_access");
			ext4_free_branches(handle, inode, partial->bh,
					partial->p,
					partial->p+1, (chain+n-1) - partial);
		}
	}
	/* Clear the ends of indirect blocks on the shared branch */
	while (partial > chain) {
		ext4_free_branches(handle, inode, partial->bh, partial->p + 1,
				   (__le32*)partial->bh->b_data+addr_per_block,
				   (chain+n-1) - partial);
		BUFFER_TRACE(partial->bh, "call brelse");
		brelse(partial->bh);
		partial--;
	}
do_indirects:
	/* Kill the remaining (whole) subtrees */
	switch (offsets[0]) {
	default:
		nr = i_data[EXT4_IND_BLOCK];
		if (nr) {
			ext4_free_branches(handle, inode, NULL, &nr, &nr+1, 1);
			i_data[EXT4_IND_BLOCK] = 0;
		}
	case EXT4_IND_BLOCK:
		nr = i_data[EXT4_DIND_BLOCK];
		if (nr) {
			ext4_free_branches(handle, inode, NULL, &nr, &nr+1, 2);
			i_data[EXT4_DIND_BLOCK] = 0;
		}
	case EXT4_DIND_BLOCK:
		nr = i_data[EXT4_TIND_BLOCK];
		if (nr) {
			ext4_free_branches(handle, inode, NULL, &nr, &nr+1, 3);
			i_data[EXT4_TIND_BLOCK] = 0;
		}
	case EXT4_TIND_BLOCK:
		;
	}

out_unlock:
	up_write(&ei->i_data_sem);
	inode->i_mtime = inode->i_ctime = ext4_current_time(inode);
	ext4_mark_inode_dirty(handle, inode);

	/*
	 * In a multi-transaction truncate, we only make the final transaction
	 * synchronous
	 */
	if (IS_SYNC(inode))
		ext4_handle_sync(handle);
out_stop:
	/*
	 * If this was a simple ftruncate(), and the file will remain alive
	 * then we need to clear up the orphan record which we created above.
	 * However, if this was a real unlink then we were called by
	 * ext4_delete_inode(), and we allow that function to clean up the
	 * orphan info for us.
	 */
	if (inode->i_nlink)
		ext4_orphan_del(handle, inode);

	ext4_journal_stop(handle);
	trace_ext4_truncate_exit(inode);
}

/*
 * ext4_get_inode_loc returns with an extra refcount against the inode's
 * underlying buffer_head on success. If 'in_mem' is true, we have all
 * data in memory that is needed to recreate the on-disk version of this
 * inode.
 */
static int __ext4_get_inode_loc(struct inode *inode,
				struct ext4_iloc *iloc, int in_mem)
{
	struct ext4_group_desc	*gdp;
	struct buffer_head	*bh;
	struct super_block	*sb = inode->i_sb;
	ext4_fsblk_t		block;
	int			inodes_per_block, inode_offset;

	iloc->bh = NULL;
	if (!ext4_valid_inum(sb, inode->i_ino))
		return -EIO;

	iloc->block_group = (inode->i_ino - 1) / EXT4_INODES_PER_GROUP(sb);
	gdp = ext4_get_group_desc(sb, iloc->block_group, NULL);
	if (!gdp)
		return -EIO;

	/*
	 * Figure out the offset within the block group inode table
	 */
	inodes_per_block = EXT4_SB(sb)->s_inodes_per_block;
	inode_offset = ((inode->i_ino - 1) %
			EXT4_INODES_PER_GROUP(sb));
	block = ext4_inode_table(sb, gdp) + (inode_offset / inodes_per_block);
	iloc->offset = (inode_offset % inodes_per_block) * EXT4_INODE_SIZE(sb);

	bh = sb_getblk(sb, block);
	if (!bh) {
		EXT4_ERROR_INODE_BLOCK(inode, block,
				       "unable to read itable block");
		return -EIO;
	}
	if (!buffer_uptodate(bh)) {
		lock_buffer(bh);

		/*
		 * If the buffer has the write error flag, we have failed
		 * to write out another inode in the same block.  In this
		 * case, we don't have to read the block because we may
		 * read the old inode data successfully.
		 */
		if (buffer_write_io_error(bh) && !buffer_uptodate(bh))
			set_buffer_uptodate(bh);

		if (buffer_uptodate(bh)) {
			/* someone brought it uptodate while we waited */
			unlock_buffer(bh);
			goto has_buffer;
		}

		/*
		 * If we have all information of the inode in memory and this
		 * is the only valid inode in the block, we need not read the
		 * block.
		 */
		if (in_mem) {
			struct buffer_head *bitmap_bh;
			int i, start;

			start = inode_offset & ~(inodes_per_block - 1);

			/* Is the inode bitmap in cache? */
			bitmap_bh = sb_getblk(sb, ext4_inode_bitmap(sb, gdp));
			if (!bitmap_bh)
				goto make_io;

			/*
			 * If the inode bitmap isn't in cache then the
			 * optimisation may end up performing two reads instead
			 * of one, so skip it.
			 */
			if (!buffer_uptodate(bitmap_bh)) {
				brelse(bitmap_bh);
				goto make_io;
			}
			for (i = start; i < start + inodes_per_block; i++) {
				if (i == inode_offset)
					continue;
				if (ext4_test_bit(i, bitmap_bh->b_data))
					break;
			}
			brelse(bitmap_bh);
			if (i == start + inodes_per_block) {
				/* all other inodes are free, so skip I/O */
				memset(bh->b_data, 0, bh->b_size);
				set_buffer_uptodate(bh);
				unlock_buffer(bh);
				goto has_buffer;
			}
		}

make_io:
		/*
		 * If we need to do any I/O, try to pre-readahead extra
		 * blocks from the inode table.
		 */
		if (EXT4_SB(sb)->s_inode_readahead_blks) {
			ext4_fsblk_t b, end, table;
			unsigned num;

			table = ext4_inode_table(sb, gdp);
			/* s_inode_readahead_blks is always a power of 2 */
			b = block & ~(EXT4_SB(sb)->s_inode_readahead_blks-1);
			if (table > b)
				b = table;
			end = b + EXT4_SB(sb)->s_inode_readahead_blks;
			num = EXT4_INODES_PER_GROUP(sb);
			if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
				       EXT4_FEATURE_RO_COMPAT_GDT_CSUM))
				num -= ext4_itable_unused_count(sb, gdp);
			table += num / inodes_per_block;
			if (end > table)
				end = table;
			while (b <= end)
				sb_breadahead(sb, b++);
		}

		/*
		 * There are other valid inodes in the buffer, this inode
		 * has in-inode xattrs, or we don't have this inode in memory.
		 * Read the block from disk.
		 */
		trace_ext4_load_inode(inode);
		get_bh(bh);
		bh->b_end_io = end_buffer_read_sync;
		submit_bh(READ_META, bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			EXT4_ERROR_INODE_BLOCK(inode, block,
					       "unable to read itable block");
			brelse(bh);
			return -EIO;
		}
	}
has_buffer:
	iloc->bh = bh;
	return 0;
}

int ext4_get_inode_loc(struct inode *inode, struct ext4_iloc *iloc)
{
	/* We have all inode data except xattrs in memory here. */
	return __ext4_get_inode_loc(inode, iloc,
		!ext4_test_inode_state(inode, EXT4_STATE_XATTR));
}

void ext4_set_inode_flags(struct inode *inode)
{
	unsigned int flags = EXT4_I(inode)->i_flags;

	inode->i_flags &= ~(S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);
	if (flags & EXT4_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & EXT4_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & EXT4_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & EXT4_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & EXT4_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
}

/* Propagate flags from i_flags to EXT4_I(inode)->i_flags */
void ext4_get_inode_flags(struct ext4_inode_info *ei)
{
	unsigned int vfs_fl;
	unsigned long old_fl, new_fl;

	do {
		vfs_fl = ei->vfs_inode.i_flags;
		old_fl = ei->i_flags;
		new_fl = old_fl & ~(EXT4_SYNC_FL|EXT4_APPEND_FL|
				EXT4_IMMUTABLE_FL|EXT4_NOATIME_FL|
				EXT4_DIRSYNC_FL);
		if (vfs_fl & S_SYNC)
			new_fl |= EXT4_SYNC_FL;
		if (vfs_fl & S_APPEND)
			new_fl |= EXT4_APPEND_FL;
		if (vfs_fl & S_IMMUTABLE)
			new_fl |= EXT4_IMMUTABLE_FL;
		if (vfs_fl & S_NOATIME)
			new_fl |= EXT4_NOATIME_FL;
		if (vfs_fl & S_DIRSYNC)
			new_fl |= EXT4_DIRSYNC_FL;
	} while (cmpxchg(&ei->i_flags, old_fl, new_fl) != old_fl);
}

static blkcnt_t ext4_inode_blocks(struct ext4_inode *raw_inode,
				  struct ext4_inode_info *ei)
{
	blkcnt_t i_blocks ;
	struct inode *inode = &(ei->vfs_inode);
	struct super_block *sb = inode->i_sb;

	if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
				EXT4_FEATURE_RO_COMPAT_HUGE_FILE)) {
		/* we are using combined 48 bit field */
		i_blocks = ((u64)le16_to_cpu(raw_inode->i_blocks_high)) << 32 |
					le32_to_cpu(raw_inode->i_blocks_lo);
		if (ext4_test_inode_flag(inode, EXT4_INODE_HUGE_FILE)) {
			/* i_blocks represent file system block size */
			return i_blocks  << (inode->i_blkbits - 9);
		} else {
			return i_blocks;
		}
	} else {
		return le32_to_cpu(raw_inode->i_blocks_lo);
	}
}

struct inode *ext4_iget(struct super_block *sb, unsigned long ino)
{
	struct ext4_iloc iloc;
	struct ext4_inode *raw_inode;
	struct ext4_inode_info *ei;
	struct inode *inode;
	journal_t *journal = EXT4_SB(sb)->s_journal;
	long ret;
	int block;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	ei = EXT4_I(inode);
	iloc.bh = NULL;

	ret = __ext4_get_inode_loc(inode, &iloc, 0);
	if (ret < 0)
		goto bad_inode;
	raw_inode = ext4_raw_inode(&iloc);
	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	inode->i_uid = (uid_t)le16_to_cpu(raw_inode->i_uid_low);
	inode->i_gid = (gid_t)le16_to_cpu(raw_inode->i_gid_low);
	if (!(test_opt(inode->i_sb, NO_UID32))) {
		inode->i_uid |= le16_to_cpu(raw_inode->i_uid_high) << 16;
		inode->i_gid |= le16_to_cpu(raw_inode->i_gid_high) << 16;
	}
	inode->i_nlink = le16_to_cpu(raw_inode->i_links_count);

	ext4_clear_state_flags(ei);	/* Only relevant on 32-bit archs */
	ei->i_dir_start_lookup = 0;
	ei->i_dtime = le32_to_cpu(raw_inode->i_dtime);
	/* We now have enough fields to check if the inode was active or not.
	 * This is needed because nfsd might try to access dead inodes
	 * the test is that same one that e2fsck uses
	 * NeilBrown 1999oct15
	 */
	if (inode->i_nlink == 0) {
		if (inode->i_mode == 0 ||
		    !(EXT4_SB(inode->i_sb)->s_mount_state & EXT4_ORPHAN_FS)) {
			/* this inode is deleted */
			ret = -ESTALE;
			goto bad_inode;
		}
		/* The only unlinked inodes we let through here have
		 * valid i_mode and are being read by the orphan
		 * recovery code: that's fine, we're about to complete
		 * the process of deleting those. */
	}
	ei->i_flags = le32_to_cpu(raw_inode->i_flags);
	inode->i_blocks = ext4_inode_blocks(raw_inode, ei);
	ei->i_file_acl = le32_to_cpu(raw_inode->i_file_acl_lo);
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_64BIT))
		ei->i_file_acl |=
			((__u64)le16_to_cpu(raw_inode->i_file_acl_high)) << 32;
	inode->i_size = ext4_isize(raw_inode);
	ei->i_disksize = inode->i_size;
#ifdef CONFIG_QUOTA
	ei->i_reserved_quota = 0;
#endif
	inode->i_generation = le32_to_cpu(raw_inode->i_generation);
	ei->i_block_group = iloc.block_group;
	ei->i_last_alloc_group = ~0;
	/*
	 * NOTE! The in-memory inode i_data array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (block = 0; block < EXT4_N_BLOCKS; block++)
		ei->i_data[block] = raw_inode->i_block[block];
	INIT_LIST_HEAD(&ei->i_orphan);

	/*
	 * Set transaction id's of transactions that have to be committed
	 * to finish f[data]sync. We set them to currently running transaction
	 * as we cannot be sure that the inode or some of its metadata isn't
	 * part of the transaction - the inode could have been reclaimed and
	 * now it is reread from disk.
	 */
	if (journal) {
		transaction_t *transaction;
		tid_t tid;

		read_lock(&journal->j_state_lock);
		if (journal->j_running_transaction)
			transaction = journal->j_running_transaction;
		else
			transaction = journal->j_committing_transaction;
		if (transaction)
			tid = transaction->t_tid;
		else
			tid = journal->j_commit_sequence;
		read_unlock(&journal->j_state_lock);
		ei->i_sync_tid = tid;
		ei->i_datasync_tid = tid;
	}

	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE) {
		ei->i_extra_isize = le16_to_cpu(raw_inode->i_extra_isize);
		if (EXT4_GOOD_OLD_INODE_SIZE + ei->i_extra_isize >
		    EXT4_INODE_SIZE(inode->i_sb)) {
			ret = -EIO;
			goto bad_inode;
		}
		if (ei->i_extra_isize == 0) {
			/* The extra space is currently unused. Use it. */
			ei->i_extra_isize = sizeof(struct ext4_inode) -
					    EXT4_GOOD_OLD_INODE_SIZE;
		} else {
			__le32 *magic = (void *)raw_inode +
					EXT4_GOOD_OLD_INODE_SIZE +
					ei->i_extra_isize;
			if (*magic == cpu_to_le32(EXT4_XATTR_MAGIC))
				ext4_set_inode_state(inode, EXT4_STATE_XATTR);
		}
	} else
		ei->i_extra_isize = 0;

	EXT4_INODE_GET_XTIME(i_ctime, inode, raw_inode);
	EXT4_INODE_GET_XTIME(i_mtime, inode, raw_inode);
	EXT4_INODE_GET_XTIME(i_atime, inode, raw_inode);
	EXT4_EINODE_GET_XTIME(i_crtime, ei, raw_inode);

	inode->i_version = le32_to_cpu(raw_inode->i_disk_version);
	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE) {
		if (EXT4_FITS_IN_INODE(raw_inode, ei, i_version_hi))
			inode->i_version |=
			(__u64)(le32_to_cpu(raw_inode->i_version_hi)) << 32;
	}

	ret = 0;
	if (ei->i_file_acl &&
	    !ext4_data_block_valid(EXT4_SB(sb), ei->i_file_acl, 1)) {
		EXT4_ERROR_INODE(inode, "bad extended attribute block %llu",
				 ei->i_file_acl);
		ret = -EIO;
		goto bad_inode;
	} else if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
		    (S_ISLNK(inode->i_mode) &&
		     !ext4_inode_is_fast_symlink(inode)))
			/* Validate extent which is part of inode */
			ret = ext4_ext_check_inode(inode);
	} else if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
		   (S_ISLNK(inode->i_mode) &&
		    !ext4_inode_is_fast_symlink(inode))) {
		/* Validate block references which are part of inode */
		ret = ext4_check_inode_blockref(inode);
	}
	if (ret)
		goto bad_inode;

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &ext4_file_inode_operations;
		inode->i_fop = &ext4_file_operations;
		ext4_set_aops(inode);
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ext4_dir_inode_operations;
		inode->i_fop = &ext4_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		if (ext4_inode_is_fast_symlink(inode)) {
			inode->i_op = &ext4_fast_symlink_inode_operations;
			nd_terminate_link(ei->i_data, inode->i_size,
				sizeof(ei->i_data) - 1);
		} else {
			inode->i_op = &ext4_symlink_inode_operations;
			ext4_set_aops(inode);
		}
	} else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
	      S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		inode->i_op = &ext4_special_inode_operations;
		if (raw_inode->i_block[0])
			init_special_inode(inode, inode->i_mode,
			   old_decode_dev(le32_to_cpu(raw_inode->i_block[0])));
		else
			init_special_inode(inode, inode->i_mode,
			   new_decode_dev(le32_to_cpu(raw_inode->i_block[1])));
	} else {
		ret = -EIO;
		EXT4_ERROR_INODE(inode, "bogus i_mode (%o)", inode->i_mode);
		goto bad_inode;
	}
	brelse(iloc.bh);
	ext4_set_inode_flags(inode);
	unlock_new_inode(inode);
	return inode;

bad_inode:
	brelse(iloc.bh);
	iget_failed(inode);
	return ERR_PTR(ret);
}

static int ext4_inode_blocks_set(handle_t *handle,
				struct ext4_inode *raw_inode,
				struct ext4_inode_info *ei)
{
	struct inode *inode = &(ei->vfs_inode);
	u64 i_blocks = inode->i_blocks;
	struct super_block *sb = inode->i_sb;

	if (i_blocks <= ~0U) {
		/*
		 * i_blocks can be represnted in a 32 bit variable
		 * as multiple of 512 bytes
		 */
		raw_inode->i_blocks_lo   = cpu_to_le32(i_blocks);
		raw_inode->i_blocks_high = 0;
		ext4_clear_inode_flag(inode, EXT4_INODE_HUGE_FILE);
		return 0;
	}
	if (!EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_HUGE_FILE))
		return -EFBIG;

	if (i_blocks <= 0xffffffffffffULL) {
		/*
		 * i_blocks can be represented in a 48 bit variable
		 * as multiple of 512 bytes
		 */
		raw_inode->i_blocks_lo   = cpu_to_le32(i_blocks);
		raw_inode->i_blocks_high = cpu_to_le16(i_blocks >> 32);
		ext4_clear_inode_flag(inode, EXT4_INODE_HUGE_FILE);
	} else {
		ext4_set_inode_flag(inode, EXT4_INODE_HUGE_FILE);
		/* i_block is stored in file system block size */
		i_blocks = i_blocks >> (inode->i_blkbits - 9);
		raw_inode->i_blocks_lo   = cpu_to_le32(i_blocks);
		raw_inode->i_blocks_high = cpu_to_le16(i_blocks >> 32);
	}
	return 0;
}

/*
 * Post the struct inode info into an on-disk inode location in the
 * buffer-cache.  This gobbles the caller's reference to the
 * buffer_head in the inode location struct.
 *
 * The caller must have write access to iloc->bh.
 */
static int ext4_do_update_inode(handle_t *handle,
				struct inode *inode,
				struct ext4_iloc *iloc)
{
	struct ext4_inode *raw_inode = ext4_raw_inode(iloc);
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct buffer_head *bh = iloc->bh;
	int err = 0, rc, block;

	/* For fields not not tracking in the in-memory inode,
	 * initialise them to zero for new inodes. */
	if (ext4_test_inode_state(inode, EXT4_STATE_NEW))
		memset(raw_inode, 0, EXT4_SB(inode->i_sb)->s_inode_size);

	ext4_get_inode_flags(ei);
	raw_inode->i_mode = cpu_to_le16(inode->i_mode);
	if (!(test_opt(inode->i_sb, NO_UID32))) {
		raw_inode->i_uid_low = cpu_to_le16(low_16_bits(inode->i_uid));
		raw_inode->i_gid_low = cpu_to_le16(low_16_bits(inode->i_gid));
/*
 * Fix up interoperability with old kernels. Otherwise, old inodes get
 * re-used with the upper 16 bits of the uid/gid intact
 */
		if (!ei->i_dtime) {
			raw_inode->i_uid_high =
				cpu_to_le16(high_16_bits(inode->i_uid));
			raw_inode->i_gid_high =
				cpu_to_le16(high_16_bits(inode->i_gid));
		} else {
			raw_inode->i_uid_high = 0;
			raw_inode->i_gid_high = 0;
		}
	} else {
		raw_inode->i_uid_low =
			cpu_to_le16(fs_high2lowuid(inode->i_uid));
		raw_inode->i_gid_low =
			cpu_to_le16(fs_high2lowgid(inode->i_gid));
		raw_inode->i_uid_high = 0;
		raw_inode->i_gid_high = 0;
	}
	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);

	EXT4_INODE_SET_XTIME(i_ctime, inode, raw_inode);
	EXT4_INODE_SET_XTIME(i_mtime, inode, raw_inode);
	EXT4_INODE_SET_XTIME(i_atime, inode, raw_inode);
	EXT4_EINODE_SET_XTIME(i_crtime, ei, raw_inode);

	if (ext4_inode_blocks_set(handle, raw_inode, ei))
		goto out_brelse;
	raw_inode->i_dtime = cpu_to_le32(ei->i_dtime);
	raw_inode->i_flags = cpu_to_le32(ei->i_flags & 0xFFFFFFFF);
	if (EXT4_SB(inode->i_sb)->s_es->s_creator_os !=
	    cpu_to_le32(EXT4_OS_HURD))
		raw_inode->i_file_acl_high =
			cpu_to_le16(ei->i_file_acl >> 32);
	raw_inode->i_file_acl_lo = cpu_to_le32(ei->i_file_acl);
	ext4_isize_set(raw_inode, ei->i_disksize);
	if (ei->i_disksize > 0x7fffffffULL) {
		struct super_block *sb = inode->i_sb;
		if (!EXT4_HAS_RO_COMPAT_FEATURE(sb,
				EXT4_FEATURE_RO_COMPAT_LARGE_FILE) ||
				EXT4_SB(sb)->s_es->s_rev_level ==
				cpu_to_le32(EXT4_GOOD_OLD_REV)) {
			/* If this is the first large file
			 * created, add a flag to the superblock.
			 */
			err = ext4_journal_get_write_access(handle,
					EXT4_SB(sb)->s_sbh);
			if (err)
				goto out_brelse;
			ext4_update_dynamic_rev(sb);
			EXT4_SET_RO_COMPAT_FEATURE(sb,
					EXT4_FEATURE_RO_COMPAT_LARGE_FILE);
			sb->s_dirt = 1;
			ext4_handle_sync(handle);
			err = ext4_handle_dirty_metadata(handle, NULL,
					EXT4_SB(sb)->s_sbh);
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
	} else
		for (block = 0; block < EXT4_N_BLOCKS; block++)
			raw_inode->i_block[block] = ei->i_data[block];

	raw_inode->i_disk_version = cpu_to_le32(inode->i_version);
	if (ei->i_extra_isize) {
		if (EXT4_FITS_IN_INODE(raw_inode, ei, i_version_hi))
			raw_inode->i_version_hi =
			cpu_to_le32(inode->i_version >> 32);
		raw_inode->i_extra_isize = cpu_to_le16(ei->i_extra_isize);
	}

	BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
	rc = ext4_handle_dirty_metadata(handle, NULL, bh);
	if (!err)
		err = rc;
	ext4_clear_inode_state(inode, EXT4_STATE_NEW);

	ext4_update_inode_fsync_trans(handle, inode, 0);
out_brelse:
	brelse(bh);
	ext4_std_error(inode->i_sb, err);
	return err;
}

/*
 * ext4_write_inode()
 *
 * We are called from a few places:
 *
 * - Within generic_file_write() for O_SYNC files.
 *   Here, there will be no transaction running. We wait for any running
 *   trasnaction to commit.
 *
 * - Within sys_sync(), kupdate and such.
 *   We wait on commit, if tol to.
 *
 * - Within prune_icache() (PF_MEMALLOC == true)
 *   Here we simply return.  We can't afford to block kswapd on the
 *   journal commit.
 *
 * In all cases it is actually safe for us to return without doing anything,
 * because the inode has been copied into a raw inode buffer in
 * ext4_mark_inode_dirty().  This is a correctness thing for O_SYNC and for
 * knfsd.
 *
 * Note that we are absolutely dependent upon all inode dirtiers doing the
 * right thing: they *must* call mark_inode_dirty() after dirtying info in
 * which we are interested.
 *
 * It would be a bug for them to not do this.  The code:
 *
 *	mark_inode_dirty(inode)
 *	stuff();
 *	inode->i_size = expr;
 *
 * is in error because a kswapd-driven write_inode() could occur while
 * `stuff()' is running, and the new i_size will be lost.  Plus the inode
 * will no longer be on the superblock's dirty inode list.
 */
int ext4_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int err;

	if (current->flags & PF_MEMALLOC)
		return 0;

	if (EXT4_SB(inode->i_sb)->s_journal) {
		if (ext4_journal_current_handle()) {
			jbd_debug(1, "called recursively, non-PF_MEMALLOC!\n");
			dump_stack();
			return -EIO;
		}

		if (wbc->sync_mode != WB_SYNC_ALL)
			return 0;

		err = ext4_force_commit(inode->i_sb);
	} else {
		struct ext4_iloc iloc;

		err = __ext4_get_inode_loc(inode, &iloc, 0);
		if (err)
			return err;
		if (wbc->sync_mode == WB_SYNC_ALL)
			sync_dirty_buffer(iloc.bh);
		if (buffer_req(iloc.bh) && !buffer_uptodate(iloc.bh)) {
			EXT4_ERROR_INODE_BLOCK(inode, iloc.bh->b_blocknr,
					 "IO error syncing inode");
			err = -EIO;
		}
		brelse(iloc.bh);
	}
	return err;
}

/*
 * ext4_setattr()
 *
 * Called from notify_change.
 *
 * We want to trap VFS attempts to truncate the file as soon as
 * possible.  In particular, we want to make sure that when the VFS
 * shrinks i_size, we put the inode on the orphan list and modify
 * i_disksize immediately, so that during the subsequent flushing of
 * dirty pages and freeing of disk blocks, we can guarantee that any
 * commit will leave the blocks being flushed in an unused state on
 * disk.  (On recovery, the inode will get truncated and the blocks will
 * be freed, so we have a strong guarantee that no future commit will
 * leave these blocks visible to the user.)
 *
 * Another thing we have to assure is that if we are in ordered mode
 * and inode is still attached to the committing transaction, we must
 * we start writeout of all the dirty pages which are being truncated.
 * This way we are sure that all the data written in the previous
 * transaction are already on disk (truncate waits for pages under
 * writeback).
 *
 * Called with inode->i_mutex down.
 */
int ext4_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error, rc = 0;
	int orphan = 0;
	const unsigned int ia_valid = attr->ia_valid;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if (is_quota_modification(inode, attr))
		dquot_initialize(inode);
	if ((ia_valid & ATTR_UID && attr->ia_uid != inode->i_uid) ||
		(ia_valid & ATTR_GID && attr->ia_gid != inode->i_gid)) {
		handle_t *handle;

		/* (user+group)*(old+new) structure, inode write (sb,
		 * inode block, ? - but truncate inode update has it) */
		handle = ext4_journal_start(inode, (EXT4_MAXQUOTAS_INIT_BLOCKS(inode->i_sb)+
					EXT4_MAXQUOTAS_DEL_BLOCKS(inode->i_sb))+3);
		if (IS_ERR(handle)) {
			error = PTR_ERR(handle);
			goto err_out;
		}
		error = dquot_transfer(inode, attr);
		if (error) {
			ext4_journal_stop(handle);
			return error;
		}
		/* Update corresponding info in inode so that everything is in
		 * one transaction */
		if (attr->ia_valid & ATTR_UID)
			inode->i_uid = attr->ia_uid;
		if (attr->ia_valid & ATTR_GID)
			inode->i_gid = attr->ia_gid;
		error = ext4_mark_inode_dirty(handle, inode);
		ext4_journal_stop(handle);
	}

	if (attr->ia_valid & ATTR_SIZE) {
		if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))) {
			struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

			if (attr->ia_size > sbi->s_bitmap_maxbytes)
				return -EFBIG;
		}
	}

	if (S_ISREG(inode->i_mode) &&
	    attr->ia_valid & ATTR_SIZE &&
	    (attr->ia_size < inode->i_size)) {
		handle_t *handle;

		handle = ext4_journal_start(inode, 3);
		if (IS_ERR(handle)) {
			error = PTR_ERR(handle);
			goto err_out;
		}
		if (ext4_handle_valid(handle)) {
			error = ext4_orphan_add(handle, inode);
			orphan = 1;
		}
		EXT4_I(inode)->i_disksize = attr->ia_size;
		rc = ext4_mark_inode_dirty(handle, inode);
		if (!error)
			error = rc;
		ext4_journal_stop(handle);

		if (ext4_should_order_data(inode)) {
			error = ext4_begin_ordered_truncate(inode,
							    attr->ia_size);
			if (error) {
				/* Do as much error cleanup as possible */
				handle = ext4_journal_start(inode, 3);
				if (IS_ERR(handle)) {
					ext4_orphan_del(NULL, inode);
					goto err_out;
				}
				ext4_orphan_del(handle, inode);
				orphan = 0;
				ext4_journal_stop(handle);
				goto err_out;
			}
		}
	}

	if (attr->ia_valid & ATTR_SIZE) {
		if (attr->ia_size != i_size_read(inode)) {
			truncate_setsize(inode, attr->ia_size);
			ext4_truncate(inode);
		} else if (ext4_test_inode_flag(inode, EXT4_INODE_EOFBLOCKS))
			ext4_truncate(inode);
	}

	if (!rc) {
		setattr_copy(inode, attr);
		mark_inode_dirty(inode);
	}

	/*
	 * If the call to ext4_truncate failed to get a transaction handle at
	 * all, we need to clean up the in-core orphan list manually.
	 */
	if (orphan && inode->i_nlink)
		ext4_orphan_del(NULL, inode);

	if (!rc && (ia_valid & ATTR_MODE))
		rc = ext4_acl_chmod(inode);

err_out:
	ext4_std_error(inode->i_sb, error);
	if (!error)
		error = rc;
	return error;
}

int ext4_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct inode *inode;
	unsigned long delalloc_blocks;

	inode = dentry->d_inode;
	generic_fillattr(inode, stat);

	/*
	 * We can't update i_blocks if the block allocation is delayed
	 * otherwise in the case of system crash before the real block
	 * allocation is done, we will have i_blocks inconsistent with
	 * on-disk file blocks.
	 * We always keep i_blocks updated together with real
	 * allocation. But to not confuse with user, stat
	 * will return the blocks that include the delayed allocation
	 * blocks for this file.
	 */
	delalloc_blocks = EXT4_I(inode)->i_reserved_data_blocks;

	stat->blocks += (delalloc_blocks << inode->i_sb->s_blocksize_bits)>>9;
	return 0;
}

static int ext4_indirect_trans_blocks(struct inode *inode, int nrblocks,
				      int chunk)
{
	int indirects;

	/* if nrblocks are contiguous */
	if (chunk) {
		/*
		 * With N contiguous data blocks, we need at most
		 * N/EXT4_ADDR_PER_BLOCK(inode->i_sb) + 1 indirect blocks,
		 * 2 dindirect blocks, and 1 tindirect block
		 */
		return DIV_ROUND_UP(nrblocks,
				    EXT4_ADDR_PER_BLOCK(inode->i_sb)) + 4;
	}
	/*
	 * if nrblocks are not contiguous, worse case, each block touch
	 * a indirect block, and each indirect block touch a double indirect
	 * block, plus a triple indirect block
	 */
	indirects = nrblocks * 2 + 1;
	return indirects;
}

static int ext4_index_trans_blocks(struct inode *inode, int nrblocks, int chunk)
{
	if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)))
		return ext4_indirect_trans_blocks(inode, nrblocks, chunk);
	return ext4_ext_index_trans_blocks(inode, nrblocks, chunk);
}

/*
 * Account for index blocks, block groups bitmaps and block group
 * descriptor blocks if modify datablocks and index blocks
 * worse case, the indexs blocks spread over different block groups
 *
 * If datablocks are discontiguous, they are possible to spread over
 * different block groups too. If they are contiuguous, with flexbg,
 * they could still across block group boundary.
 *
 * Also account for superblock, inode, quota and xattr blocks
 */
static int ext4_meta_trans_blocks(struct inode *inode, int nrblocks, int chunk)
{
	ext4_group_t groups, ngroups = ext4_get_groups_count(inode->i_sb);
	int gdpblocks;
	int idxblocks;
	int ret = 0;

	/*
	 * How many index blocks need to touch to modify nrblocks?
	 * The "Chunk" flag indicating whether the nrblocks is
	 * physically contiguous on disk
	 *
	 * For Direct IO and fallocate, they calls get_block to allocate
	 * one single extent at a time, so they could set the "Chunk" flag
	 */
	idxblocks = ext4_index_trans_blocks(inode, nrblocks, chunk);

	ret = idxblocks;

	/*
	 * Now let's see how many group bitmaps and group descriptors need
	 * to account
	 */
	groups = idxblocks;
	if (chunk)
		groups += 1;
	else
		groups += nrblocks;

	gdpblocks = groups;
	if (groups > ngroups)
		groups = ngroups;
	if (groups > EXT4_SB(inode->i_sb)->s_gdb_count)
		gdpblocks = EXT4_SB(inode->i_sb)->s_gdb_count;

	/* bitmaps and block group descriptor blocks */
	ret += groups + gdpblocks;

	/* Blocks for super block, inode, quota and xattr blocks */
	ret += EXT4_META_TRANS_BLOCKS(inode->i_sb);

	return ret;
}

/*
 * Calculate the total number of credits to reserve to fit
 * the modification of a single pages into a single transaction,
 * which may include multiple chunks of block allocations.
 *
 * This could be called via ext4_write_begin()
 *
 * We need to consider the worse case, when
 * one new block per extent.
 */
int ext4_writepage_trans_blocks(struct inode *inode)
{
	int bpp = ext4_journal_blocks_per_page(inode);
	int ret;

	ret = ext4_meta_trans_blocks(inode, bpp, 0);

	/* Account for data blocks for journalled mode */
	if (ext4_should_journal_data(inode))
		ret += bpp;
	return ret;
}

/*
 * Calculate the journal credits for a chunk of data modification.
 *
 * This is called from DIO, fallocate or whoever calling
 * ext4_map_blocks() to map/allocate a chunk of contiguous disk blocks.
 *
 * journal buffers for data blocks are not included here, as DIO
 * and fallocate do no need to journal data buffers.
 */
int ext4_chunk_trans_blocks(struct inode *inode, int nrblocks)
{
	return ext4_meta_trans_blocks(inode, nrblocks, 1);
}

/*
 * The caller must have previously called ext4_reserve_inode_write().
 * Give this, we know that the caller already has write access to iloc->bh.
 */
int ext4_mark_iloc_dirty(handle_t *handle,
			 struct inode *inode, struct ext4_iloc *iloc)
{
	int err = 0;

	if (test_opt(inode->i_sb, I_VERSION))
		inode_inc_iversion(inode);

	/* the do_update_inode consumes one bh->b_count */
	get_bh(iloc->bh);

	/* ext4_do_update_inode() does jbd2_journal_dirty_metadata */
	err = ext4_do_update_inode(handle, inode, iloc);
	put_bh(iloc->bh);
	return err;
}

/*
 * On success, We end up with an outstanding reference count against
 * iloc->bh.  This _must_ be cleaned up later.
 */

int
ext4_reserve_inode_write(handle_t *handle, struct inode *inode,
			 struct ext4_iloc *iloc)
{
	int err;

	err = ext4_get_inode_loc(inode, iloc);
	if (!err) {
		BUFFER_TRACE(iloc->bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, iloc->bh);
		if (err) {
			brelse(iloc->bh);
			iloc->bh = NULL;
		}
	}
	ext4_std_error(inode->i_sb, err);
	return err;
}

/*
 * Expand an inode by new_extra_isize bytes.
 * Returns 0 on success or negative error number on failure.
 */
static int ext4_expand_extra_isize(struct inode *inode,
				   unsigned int new_extra_isize,
				   struct ext4_iloc iloc,
				   handle_t *handle)
{
	struct ext4_inode *raw_inode;
	struct ext4_xattr_ibody_header *header;

	if (EXT4_I(inode)->i_extra_isize >= new_extra_isize)
		return 0;

	raw_inode = ext4_raw_inode(&iloc);

	header = IHDR(inode, raw_inode);

	/* No extended attributes present */
	if (!ext4_test_inode_state(inode, EXT4_STATE_XATTR) ||
	    header->h_magic != cpu_to_le32(EXT4_XATTR_MAGIC)) {
		memset((void *)raw_inode + EXT4_GOOD_OLD_INODE_SIZE, 0,
			new_extra_isize);
		EXT4_I(inode)->i_extra_isize = new_extra_isize;
		return 0;
	}

	/* try to expand with EAs present */
	return ext4_expand_extra_isize_ea(inode, new_extra_isize,
					  raw_inode, handle);
}

/*
 * What we do here is to mark the in-core inode as clean with respect to inode
 * dirtiness (it may still be data-dirty).
 * This means that the in-core inode may be reaped by prune_icache
 * without having to perform any I/O.  This is a very good thing,
 * because *any* task may call prune_icache - even ones which
 * have a transaction open against a different journal.
 *
 * Is this cheating?  Not really.  Sure, we haven't written the
 * inode out, but prune_icache isn't a user-visible syncing function.
 * Whenever the user wants stuff synced (sys_sync, sys_msync, sys_fsync)
 * we start and wait on commits.
 *
 * Is this efficient/effective?  Well, we're being nice to the system
 * by cleaning up our inodes proactively so they can be reaped
 * without I/O.  But we are potentially leaving up to five seconds'
 * worth of inodes floating about which prune_icache wants us to
 * write out.  One way to fix that would be to get prune_icache()
 * to do a write_super() to free up some memory.  It has the desired
 * effect.
 */
int ext4_mark_inode_dirty(handle_t *handle, struct inode *inode)
{
	struct ext4_iloc iloc;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	static unsigned int mnt_count;
	int err, ret;

	might_sleep();
	trace_ext4_mark_inode_dirty(inode, _RET_IP_);
	err = ext4_reserve_inode_write(handle, inode, &iloc);
	if (ext4_handle_valid(handle) &&
	    EXT4_I(inode)->i_extra_isize < sbi->s_want_extra_isize &&
	    !ext4_test_inode_state(inode, EXT4_STATE_NO_EXPAND)) {
		/*
		 * We need extra buffer credits since we may write into EA block
		 * with this same handle. If journal_extend fails, then it will
		 * only result in a minor loss of functionality for that inode.
		 * If this is felt to be critical, then e2fsck should be run to
		 * force a large enough s_min_extra_isize.
		 */
		if ((jbd2_journal_extend(handle,
			     EXT4_DATA_TRANS_BLOCKS(inode->i_sb))) == 0) {
			ret = ext4_expand_extra_isize(inode,
						      sbi->s_want_extra_isize,
						      iloc, handle);
			if (ret) {
				ext4_set_inode_state(inode,
						     EXT4_STATE_NO_EXPAND);
				if (mnt_count !=
					le16_to_cpu(sbi->s_es->s_mnt_count)) {
					ext4_warning(inode->i_sb,
					"Unable to expand inode %lu. Delete"
					" some EAs or run e2fsck.",
					inode->i_ino);
					mnt_count =
					  le16_to_cpu(sbi->s_es->s_mnt_count);
				}
			}
		}
	}
	if (!err)
		err = ext4_mark_iloc_dirty(handle, inode, &iloc);
	return err;
}

/*
 * ext4_dirty_inode() is called from __mark_inode_dirty()
 *
 * We're really interested in the case where a file is being extended.
 * i_size has been changed by generic_commit_write() and we thus need
 * to include the updated inode in the current transaction.
 *
 * Also, dquot_alloc_block() will always dirty the inode when blocks
 * are allocated to the file.
 *
 * If the inode is marked synchronous, we don't honour that here - doing
 * so would cause a commit on atime updates, which we don't bother doing.
 * We handle synchronous inodes at the highest possible level.
 */
void ext4_dirty_inode(struct inode *inode, int flags)
{
	handle_t *handle;

	handle = ext4_journal_start(inode, 2);
	if (IS_ERR(handle))
		goto out;

	ext4_mark_inode_dirty(handle, inode);

	ext4_journal_stop(handle);
out:
	return;
}

#if 0
/*
 * Bind an inode's backing buffer_head into this transaction, to prevent
 * it from being flushed to disk early.  Unlike
 * ext4_reserve_inode_write, this leaves behind no bh reference and
 * returns no iloc structure, so the caller needs to repeat the iloc
 * lookup to mark the inode dirty later.
 */
static int ext4_pin_inode(handle_t *handle, struct inode *inode)
{
	struct ext4_iloc iloc;

	int err = 0;
	if (handle) {
		err = ext4_get_inode_loc(inode, &iloc);
		if (!err) {
			BUFFER_TRACE(iloc.bh, "get_write_access");
			err = jbd2_journal_get_write_access(handle, iloc.bh);
			if (!err)
				err = ext4_handle_dirty_metadata(handle,
								 NULL,
								 iloc.bh);
			brelse(iloc.bh);
		}
	}
	ext4_std_error(inode->i_sb, err);
	return err;
}
#endif

int ext4_change_inode_journal_flag(struct inode *inode, int val)
{
	journal_t *journal;
	handle_t *handle;
	int err;

	/*
	 * We have to be very careful here: changing a data block's
	 * journaling status dynamically is dangerous.  If we write a
	 * data block to the journal, change the status and then delete
	 * that block, we risk forgetting to revoke the old log record
	 * from the journal and so a subsequent replay can corrupt data.
	 * So, first we make sure that the journal is empty and that
	 * nobody is changing anything.
	 */

	journal = EXT4_JOURNAL(inode);
	if (!journal)
		return 0;
	if (is_journal_aborted(journal))
		return -EROFS;

	jbd2_journal_lock_updates(journal);
	jbd2_journal_flush(journal);

	/*
	 * OK, there are no updates running now, and all cached data is
	 * synced to disk.  We are now in a completely consistent state
	 * which doesn't have anything in the journal, and we know that
	 * no filesystem updates are running, so it is safe to modify
	 * the inode's in-core data-journaling state flag now.
	 */

	if (val)
		ext4_set_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	else
		ext4_clear_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	ext4_set_aops(inode);

	jbd2_journal_unlock_updates(journal);

	/* Finally we can mark the inode as dirty. */

	handle = ext4_journal_start(inode, 1);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	err = ext4_mark_inode_dirty(handle, inode);
	ext4_handle_sync(handle);
	ext4_journal_stop(handle);
	ext4_std_error(inode->i_sb, err);

	return err;
}

static int ext4_bh_unmapped(handle_t *handle, struct buffer_head *bh)
{
	return !buffer_mapped(bh);
}

int ext4_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	loff_t size;
	unsigned long len;
	int ret = -EINVAL;
	void *fsdata;
	struct file *file = vma->vm_file;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;

	/*
	 * Get i_alloc_sem to stop truncates messing with the inode. We cannot
	 * get i_mutex because we are already holding mmap_sem.
	 */
	down_read(&inode->i_alloc_sem);
	size = i_size_read(inode);
	if (page->mapping != mapping || size <= page_offset(page)
	    || !PageUptodate(page)) {
		/* page got truncated from under us? */
		goto out_unlock;
	}
	ret = 0;

	lock_page(page);
	wait_on_page_writeback(page);
	if (PageMappedToDisk(page)) {
		up_read(&inode->i_alloc_sem);
		return VM_FAULT_LOCKED;
	}

	if (page->index == size >> PAGE_CACHE_SHIFT)
		len = size & ~PAGE_CACHE_MASK;
	else
		len = PAGE_CACHE_SIZE;

	/*
	 * return if we have all the buffers mapped. This avoid
	 * the need to call write_begin/write_end which does a
	 * journal_start/journal_stop which can block and take
	 * long time
	 */
	if (page_has_buffers(page)) {
		if (!walk_page_buffers(NULL, page_buffers(page), 0, len, NULL,
					ext4_bh_unmapped)) {
			up_read(&inode->i_alloc_sem);
			return VM_FAULT_LOCKED;
		}
	}
	unlock_page(page);
	/*
	 * OK, we need to fill the hole... Do write_begin write_end
	 * to do block allocation/reservation.We are not holding
	 * inode.i__mutex here. That allow * parallel write_begin,
	 * write_end call. lock_page prevent this from happening
	 * on the same page though
	 */
	ret = mapping->a_ops->write_begin(file, mapping, page_offset(page),
			len, AOP_FLAG_UNINTERRUPTIBLE, &page, &fsdata);
	if (ret < 0)
		goto out_unlock;
	ret = mapping->a_ops->write_end(file, mapping, page_offset(page),
			len, len, page, fsdata);
	if (ret < 0)
		goto out_unlock;
	ret = 0;

	/*
	 * write_begin/end might have created a dirty page and someone
	 * could wander in and start the IO.  Make sure that hasn't
	 * happened.
	 */
	lock_page(page);
	wait_on_page_writeback(page);
	up_read(&inode->i_alloc_sem);
	return VM_FAULT_LOCKED;
out_unlock:
	if (ret)
		ret = VM_FAULT_SIGBUS;
	up_read(&inode->i_alloc_sem);
	return ret;
}
