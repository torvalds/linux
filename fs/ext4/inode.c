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

#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"
#include "ext4_extents.h"

#include <trace/events/ext4.h>

#define MPAGE_DA_EXTENT_TAIL 0x01

static inline int ext4_begin_ordered_truncate(struct inode *inode,
					      loff_t new_size)
{
	return jbd2_journal_begin_ordered_truncate(
					EXT4_SB(inode->i_sb)->s_journal,
					&EXT4_I(inode)->jinode,
					new_size);
}

static void ext4_invalidatepage(struct page *page, unsigned long offset);

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
 * The ext4 forget function must perform a revoke if we are freeing data
 * which has been journaled.  Metadata (eg. indirect blocks) must be
 * revoked in all cases.
 *
 * "bh" may be NULL: a metadata block may have been freed from memory
 * but there may still be a record of it in the journal, and that record
 * still needs to be revoked.
 *
 * If the handle isn't valid we're not journaling, but we still need to
 * call into ext4_journal_revoke() to put the buffer head.
 */
int ext4_forget(handle_t *handle, int is_metadata, struct inode *inode,
		struct buffer_head *bh, ext4_fsblk_t blocknr)
{
	int err;

	might_sleep();

	BUFFER_TRACE(bh, "enter");

	jbd_debug(4, "forgetting bh %p: is_metadata = %d, mode %o, "
		  "data mode %x\n",
		  bh, is_metadata, inode->i_mode,
		  test_opt(inode->i_sb, DATA_FLAGS));

	/* Never use the revoke function if we are doing full data
	 * journaling: there is no need to, and a V1 superblock won't
	 * support it.  Otherwise, only skip the revoke on un-journaled
	 * data blocks. */

	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA ||
	    (!is_metadata && !ext4_should_journal_data(inode))) {
		if (bh) {
			BUFFER_TRACE(bh, "call jbd2_journal_forget");
			return ext4_journal_forget(handle, bh);
		}
		return 0;
	}

	/*
	 * data!=journal && (is_metadata || should_journal_data(inode))
	 */
	BUFFER_TRACE(bh, "call ext4_journal_revoke");
	err = ext4_journal_revoke(handle, blocknr, bh);
	if (err)
		ext4_abort(inode->i_sb, __func__,
			   "error %d when attempting revoke", err);
	BUFFER_TRACE(bh, "exit");
	return err;
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
	 * Drop i_data_sem to avoid deadlock with ext4_get_blocks At this
	 * moment, get_block can be called only for blocks inside i_size since
	 * page cache has been already dropped and writes are blocked by
	 * i_mutex. So we can safely drop the i_data_sem here.
	 */
	BUG_ON(EXT4_JOURNAL(inode) == NULL);
	jbd_debug(2, "restarting handle %p\n", handle);
	up_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_journal_restart(handle, blocks_for_truncate(inode));
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_discard_preallocations(inode);

	return ret;
}

/*
 * Called at the last iput() if i_nlink is zero.
 */
void ext4_delete_inode(struct inode *inode)
{
	handle_t *handle;
	int err;

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
		ext4_warning(inode->i_sb, __func__,
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
			ext4_warning(inode->i_sb, __func__,
				     "couldn't extend journal (err %d)", err);
		stop_handle:
			ext4_journal_stop(handle);
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
		clear_inode(inode);
	else
		ext4_free_inode(handle, inode);
	ext4_journal_stop(handle);
	return;
no_delete:
	clear_inode(inode);	/* We must guarantee clearing of inode... */
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
		ext4_warning(inode->i_sb, "ext4_block_to_path",
			     "block %lu > max in inode %lu",
			     i_block + direct_blocks +
			     indirect_blocks + double_blocks, inode->i_ino);
	}
	if (boundary)
		*boundary = final - 1 - (i_block & (ptrs - 1));
	return n;
}

static int __ext4_check_blockref(const char *function, struct inode *inode,
				 __le32 *p, unsigned int max)
{
	__le32 *bref = p;
	unsigned int blk;

	while (bref < p+max) {
		blk = le32_to_cpu(*bref++);
		if (blk &&
		    unlikely(!ext4_data_block_valid(EXT4_SB(inode->i_sb),
						    blk, 1))) {
			ext4_error(inode->i_sb, function,
				   "invalid block reference %u "
				   "in inode #%lu", blk, inode->i_ino);
			return -EIO;
		}
	}
	return 0;
}


#define ext4_check_indirect_blockref(inode, bh)                         \
	__ext4_check_blockref(__func__, inode, (__le32 *)(bh)->b_data,  \
			      EXT4_ADDR_PER_BLOCK((inode)->i_sb))

#define ext4_check_inode_blockref(inode)                                \
	__ext4_check_blockref(__func__, inode, EXT4_I(inode)->i_data,   \
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
 *	ext4_blks_to_allocate: Look up the block map and count the number
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
 *	@indirect_blks: the number of blocks need to allocate for indirect
 *			blocks
 *
 *	@new_blocks: on return it will store the new block numbers for
 *	the indirect blocks(if needed) and the first direct block,
 *	@blks:	on return it will store the total number of allocated
 *		direct blocks
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
		current_block = ext4_new_meta_blocks(handle, inode,
							goal, &count, err);
		if (*err)
			goto failed_out;

		BUG_ON(current_block + count > EXT4_MAX_BLOCK_FILE_PHYS);

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
	BUG_ON(current_block + ar.len > EXT4_MAX_BLOCK_FILE_PHYS);

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
		ext4_free_blocks(handle, inode, new_blocks[i], 1, 0);
	return ret;
}

/**
 *	ext4_alloc_branch - allocate and set up a chain of blocks.
 *	@inode: owner
 *	@indirect_blks: number of allocated indirect blocks
 *	@blks: number of allocated direct blocks
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
	for (i = 1; i <= n ; i++) {
		BUFFER_TRACE(branch[i].bh, "call jbd2_journal_forget");
		ext4_journal_forget(handle, branch[i].bh);
	}
	for (i = 0; i < indirect_blks; i++)
		ext4_free_blocks(handle, inode, new_blocks[i], 1, 0);

	ext4_free_blocks(handle, inode, new_blocks[i], num, 0);

	return err;
}

/**
 * ext4_splice_branch - splice the allocated branch onto inode.
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
		BUFFER_TRACE(where[i].bh, "call jbd2_journal_forget");
		ext4_journal_forget(handle, where[i].bh);
		ext4_free_blocks(handle, inode,
					le32_to_cpu(where[i-1].key), 1, 0);
	}
	ext4_free_blocks(handle, inode, le32_to_cpu(where[num].key), blks, 0);

	return err;
}

/*
 * The ext4_ind_get_blocks() function handles non-extents inodes
 * (i.e., using the traditional indirect/double-indirect i_blocks
 * scheme) for ext4_get_blocks().
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
static int ext4_ind_get_blocks(handle_t *handle, struct inode *inode,
			       ext4_lblk_t iblock, unsigned int maxblocks,
			       struct buffer_head *bh_result,
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

	J_ASSERT(!(EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL));
	J_ASSERT(handle != NULL || (flags & EXT4_GET_BLOCKS_CREATE) == 0);
	depth = ext4_block_to_path(inode, iblock, offsets,
				   &blocks_to_boundary);

	if (depth == 0)
		goto out;

	partial = ext4_get_branch(inode, depth, offsets, chain, &err);

	/* Simplest case - block found, no allocation needed */
	if (!partial) {
		first_block = le32_to_cpu(chain[depth - 1].key);
		clear_buffer_new(bh_result);
		count++;
		/*map more blocks*/
		while (count < maxblocks && count <= blocks_to_boundary) {
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
	goal = ext4_find_goal(inode, iblock, partial);

	/* the number of blocks need to allocate for [d,t]indirect blocks */
	indirect_blks = (chain + depth) - partial - 1;

	/*
	 * Next look up the indirect map to count the totoal number of
	 * direct blocks to allocate for this branch.
	 */
	count = ext4_blks_to_allocate(partial, indirect_blks,
					maxblocks, blocks_to_boundary);
	/*
	 * Block out ext4_truncate while we alter the tree
	 */
	err = ext4_alloc_branch(handle, inode, iblock, indirect_blks,
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
		err = ext4_splice_branch(handle, inode, iblock,
					 partial, indirect_blks, count);
	if (err)
		goto cleanup;

	set_buffer_new(bh_result);

	ext4_update_inode_fsync_trans(handle, inode, 1);
got_it:
	map_bh(bh_result, inode->i_sb, le32_to_cpu(chain[depth-1].key));
	if (count > blocks_to_boundary)
		set_buffer_boundary(bh_result);
	err = count;
	/* Clean up and exit */
	partial = chain + depth - 1;	/* the whole chain */
cleanup:
	while (partial > chain) {
		BUFFER_TRACE(partial->bh, "call brelse");
		brelse(partial->bh);
		partial--;
	}
	BUFFER_TRACE(bh_result, "returned");
out:
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
 * to allocate @blocks for non extent file based file
 */
static int ext4_indirect_calc_metadata_amount(struct inode *inode, int blocks)
{
	int icap = EXT4_ADDR_PER_BLOCK(inode->i_sb);
	int ind_blks, dind_blks, tind_blks;

	/* number of new indirect blocks needed */
	ind_blks = (blocks + icap - 1) / icap;

	dind_blks = (ind_blks + icap - 1) / icap;

	tind_blks = 1;

	return ind_blks + dind_blks + tind_blks;
}

/*
 * Calculate the number of metadata blocks need to reserve
 * to allocate given number of blocks
 */
static int ext4_calc_metadata_amount(struct inode *inode, int blocks)
{
	if (!blocks)
		return 0;

	if (EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL)
		return ext4_ext_calc_metadata_amount(inode, blocks);

	return ext4_indirect_calc_metadata_amount(inode, blocks);
}

static void ext4_da_update_reserve_space(struct inode *inode, int used)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	int total, mdb, mdb_free;

	spin_lock(&EXT4_I(inode)->i_block_reservation_lock);
	/* recalculate the number of metablocks still need to be reserved */
	total = EXT4_I(inode)->i_reserved_data_blocks - used;
	mdb = ext4_calc_metadata_amount(inode, total);

	/* figure out how many metablocks to release */
	BUG_ON(mdb > EXT4_I(inode)->i_reserved_meta_blocks);
	mdb_free = EXT4_I(inode)->i_reserved_meta_blocks - mdb;

	if (mdb_free) {
		/* Account for allocated meta_blocks */
		mdb_free -= EXT4_I(inode)->i_allocated_meta_blocks;

		/* update fs dirty blocks counter */
		percpu_counter_sub(&sbi->s_dirtyblocks_counter, mdb_free);
		EXT4_I(inode)->i_allocated_meta_blocks = 0;
		EXT4_I(inode)->i_reserved_meta_blocks = mdb;
	}

	/* update per-inode reservations */
	BUG_ON(used  > EXT4_I(inode)->i_reserved_data_blocks);
	EXT4_I(inode)->i_reserved_data_blocks -= used;
	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

	/*
	 * free those over-booking quota for metadata blocks
	 */
	if (mdb_free)
		vfs_dq_release_reservation_block(inode, mdb_free);

	/*
	 * If we have done all the pending block allocations and if
	 * there aren't any writers on the inode, we can discard the
	 * inode's preallocations.
	 */
	if (!total && (atomic_read(&inode->i_writecount) == 0))
		ext4_discard_preallocations(inode);
}

static int check_block_validity(struct inode *inode, const char *msg,
				sector_t logical, sector_t phys, int len)
{
	if (!ext4_data_block_valid(EXT4_SB(inode->i_sb), phys, len)) {
		ext4_error(inode->i_sb, msg,
			   "inode #%lu logical block %llu mapped to %llu "
			   "(size %d)", inode->i_ino,
			   (unsigned long long) logical,
			   (unsigned long long) phys, len);
		return -EIO;
	}
	return 0;
}

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
			if (num >= max_pages)
				break;
		}
		pagevec_release(&pvec);
	}
	return num;
}

/*
 * The ext4_get_blocks() function tries to look up the requested blocks,
 * and returns if the blocks are already mapped.
 *
 * Otherwise it takes the write lock of the i_data_sem and allocate blocks
 * and store the allocated blocks in the result buffer head and mark it
 * mapped.
 *
 * If file type is extents based, it will call ext4_ext_get_blocks(),
 * Otherwise, call with ext4_ind_get_blocks() to handle indirect mapping
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
int ext4_get_blocks(handle_t *handle, struct inode *inode, sector_t block,
		    unsigned int max_blocks, struct buffer_head *bh,
		    int flags)
{
	int retval;

	clear_buffer_mapped(bh);
	clear_buffer_unwritten(bh);

	ext_debug("ext4_get_blocks(): inode %lu, flag %d, max_blocks %u,"
		  "logical block %lu\n", inode->i_ino, flags, max_blocks,
		  (unsigned long)block);
	/*
	 * Try to see if we can get the block without requesting a new
	 * file system block.
	 */
	down_read((&EXT4_I(inode)->i_data_sem));
	if (EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL) {
		retval =  ext4_ext_get_blocks(handle, inode, block, max_blocks,
				bh, 0);
	} else {
		retval = ext4_ind_get_blocks(handle, inode, block, max_blocks,
					     bh, 0);
	}
	up_read((&EXT4_I(inode)->i_data_sem));

	if (retval > 0 && buffer_mapped(bh)) {
		int ret = check_block_validity(inode, "file system corruption",
					       block, bh->b_blocknr, retval);
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
	if (retval > 0 && buffer_mapped(bh))
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
	clear_buffer_unwritten(bh);

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
		EXT4_I(inode)->i_delalloc_reserved_flag = 1;
	/*
	 * We need to check for EXT4 here because migrate
	 * could have changed the inode type in between
	 */
	if (EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL) {
		retval =  ext4_ext_get_blocks(handle, inode, block, max_blocks,
					      bh, flags);
	} else {
		retval = ext4_ind_get_blocks(handle, inode, block,
					     max_blocks, bh, flags);

		if (retval > 0 && buffer_new(bh)) {
			/*
			 * We allocated new blocks which will result in
			 * i_data's format changing.  Force the migrate
			 * to fail by clearing migrate flags
			 */
			EXT4_I(inode)->i_state &= ~EXT4_STATE_EXT_MIGRATE;
		}
	}

	if (flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE)
		EXT4_I(inode)->i_delalloc_reserved_flag = 0;

	/*
	 * Update reserved blocks/metadata blocks after successful
	 * block allocation which had been deferred till now.
	 */
	if ((retval > 0) && (flags & EXT4_GET_BLOCKS_UPDATE_RESERVE_SPACE))
		ext4_da_update_reserve_space(inode, retval);

	up_write((&EXT4_I(inode)->i_data_sem));
	if (retval > 0 && buffer_mapped(bh)) {
		int ret = check_block_validity(inode, "file system "
					       "corruption after allocation",
					       block, bh->b_blocknr, retval);
		if (ret != 0)
			return ret;
	}
	return retval;
}

/* Maximum number of blocks we map for direct IO at once. */
#define DIO_MAX_BLOCKS 4096

int ext4_get_block(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create)
{
	handle_t *handle = ext4_journal_current_handle();
	int ret = 0, started = 0;
	unsigned max_blocks = bh_result->b_size >> inode->i_blkbits;
	int dio_credits;

	if (create && !handle) {
		/* Direct IO write... */
		if (max_blocks > DIO_MAX_BLOCKS)
			max_blocks = DIO_MAX_BLOCKS;
		dio_credits = ext4_chunk_trans_blocks(inode, max_blocks);
		handle = ext4_journal_start(inode, dio_credits);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			goto out;
		}
		started = 1;
	}

	ret = ext4_get_blocks(handle, inode, iblock, max_blocks, bh_result,
			      create ? EXT4_GET_BLOCKS_CREATE : 0);
	if (ret > 0) {
		bh_result->b_size = (ret << inode->i_blkbits);
		ret = 0;
	}
	if (started)
		ext4_journal_stop(handle);
out:
	return ret;
}

/*
 * `handle' can be NULL if create is zero
 */
struct buffer_head *ext4_getblk(handle_t *handle, struct inode *inode,
				ext4_lblk_t block, int create, int *errp)
{
	struct buffer_head dummy;
	int fatal = 0, err;
	int flags = 0;

	J_ASSERT(handle != NULL || create == 0);

	dummy.b_state = 0;
	dummy.b_blocknr = -1000;
	buffer_trace_init(&dummy.b_history);
	if (create)
		flags |= EXT4_GET_BLOCKS_CREATE;
	err = ext4_get_blocks(handle, inode, block, 1, &dummy, flags);
	/*
	 * ext4_get_blocks() returns number of blocks mapped. 0 in
	 * case of a HOLE.
	 */
	if (err > 0) {
		if (err > 1)
			WARN_ON(1);
		err = 0;
	}
	*errp = err;
	if (!err && buffer_mapped(&dummy)) {
		struct buffer_head *bh;
		bh = sb_getblk(inode->i_sb, dummy.b_blocknr);
		if (!bh) {
			*errp = -EIO;
			goto err;
		}
		if (buffer_new(&dummy)) {
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
err:
	return NULL;
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
	if (!buffer_mapped(bh) || buffer_freed(bh))
		return 0;
	return ext4_journal_get_write_access(handle, bh);
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

	ret = block_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				ext4_get_block);

	if (!ret && ext4_should_journal_data(inode)) {
		ret = walk_page_buffers(handle, page_buffers(page),
				from, to, NULL, do_journal_get_write_access);
	}

	if (ret) {
		unlock_page(page);
		page_cache_release(page);
		/*
		 * block_write_begin may have instantiated a few blocks
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
	EXT4_I(inode)->i_state |= EXT4_STATE_JDATA;
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

static int ext4_da_reserve_space(struct inode *inode, int nrblocks)
{
	int retries = 0;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	unsigned long md_needed, mdblocks, total = 0;

	/*
	 * recalculate the amount of metadata blocks to reserve
	 * in order to allocate nrblocks
	 * worse case is one extent per block
	 */
repeat:
	spin_lock(&EXT4_I(inode)->i_block_reservation_lock);
	total = EXT4_I(inode)->i_reserved_data_blocks + nrblocks;
	mdblocks = ext4_calc_metadata_amount(inode, total);
	BUG_ON(mdblocks < EXT4_I(inode)->i_reserved_meta_blocks);

	md_needed = mdblocks - EXT4_I(inode)->i_reserved_meta_blocks;
	total = md_needed + nrblocks;
	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

	/*
	 * Make quota reservation here to prevent quota overflow
	 * later. Real quota accounting is done at pages writeout
	 * time.
	 */
	if (vfs_dq_reserve_block(inode, total))
		return -EDQUOT;

	if (ext4_claim_free_blocks(sbi, total)) {
		vfs_dq_release_reservation_block(inode, total);
		if (ext4_should_retry_alloc(inode->i_sb, &retries)) {
			yield();
			goto repeat;
		}
		return -ENOSPC;
	}
	spin_lock(&EXT4_I(inode)->i_block_reservation_lock);
	EXT4_I(inode)->i_reserved_data_blocks += nrblocks;
	EXT4_I(inode)->i_reserved_meta_blocks += md_needed;
	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

	return 0;       /* success */
}

static void ext4_da_release_space(struct inode *inode, int to_free)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	int total, mdb, mdb_free, release;

	if (!to_free)
		return;		/* Nothing to release, exit */

	spin_lock(&EXT4_I(inode)->i_block_reservation_lock);

	if (!EXT4_I(inode)->i_reserved_data_blocks) {
		/*
		 * if there is no reserved blocks, but we try to free some
		 * then the counter is messed up somewhere.
		 * but since this function is called from invalidate
		 * page, it's harmless to return without any action
		 */
		printk(KERN_INFO "ext4 delalloc try to release %d reserved "
			    "blocks for inode %lu, but there is no reserved "
			    "data blocks\n", to_free, inode->i_ino);
		spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);
		return;
	}

	/* recalculate the number of metablocks still need to be reserved */
	total = EXT4_I(inode)->i_reserved_data_blocks - to_free;
	mdb = ext4_calc_metadata_amount(inode, total);

	/* figure out how many metablocks to release */
	BUG_ON(mdb > EXT4_I(inode)->i_reserved_meta_blocks);
	mdb_free = EXT4_I(inode)->i_reserved_meta_blocks - mdb;

	release = to_free + mdb_free;

	/* update fs dirty blocks counter for truncate case */
	percpu_counter_sub(&sbi->s_dirtyblocks_counter, release);

	/* update per-inode reservations */
	BUG_ON(to_free > EXT4_I(inode)->i_reserved_data_blocks);
	EXT4_I(inode)->i_reserved_data_blocks -= to_free;

	BUG_ON(mdb > EXT4_I(inode)->i_reserved_meta_blocks);
	EXT4_I(inode)->i_reserved_meta_blocks = mdb;
	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

	vfs_dq_release_reservation_block(inode, release);
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
static int mpage_da_submit_io(struct mpage_da_data *mpd)
{
	long pages_skipped;
	struct pagevec pvec;
	unsigned long index, end;
	int ret = 0, err, nr_pages, i;
	struct inode *inode = mpd->inode;
	struct address_space *mapping = inode->i_mapping;

	BUG_ON(mpd->next_page <= mpd->first_page);
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
			struct page *page = pvec.pages[i];

			index = page->index;
			if (index > end)
				break;
			index++;

			BUG_ON(!PageLocked(page));
			BUG_ON(PageWriteback(page));

			pages_skipped = mpd->wbc->pages_skipped;
			err = mapping->a_ops->writepage(page, mpd->wbc);
			if (!err && (pages_skipped == mpd->wbc->pages_skipped))
				/*
				 * have successfully written the page
				 * without skipping the same
				 */
				mpd->pages_written++;
			/*
			 * In error case, we have to continue because
			 * remaining pages are still locked
			 * XXX: unlock and re-dirty them?
			 */
			if (ret == 0)
				ret = err;
		}
		pagevec_release(&pvec);
	}
	return ret;
}

/*
 * mpage_put_bnr_to_bhs - walk blocks and assign them actual numbers
 *
 * @mpd->inode - inode to walk through
 * @exbh->b_blocknr - first block on a disk
 * @exbh->b_size - amount of space in bytes
 * @logical - first logical block to start assignment with
 *
 * the function goes through all passed space and put actual disk
 * block numbers into buffer heads, dropping BH_Delay and BH_Unwritten
 */
static void mpage_put_bnr_to_bhs(struct mpage_da_data *mpd, sector_t logical,
				 struct buffer_head *exbh)
{
	struct inode *inode = mpd->inode;
	struct address_space *mapping = inode->i_mapping;
	int blocks = exbh->b_size >> inode->i_blkbits;
	sector_t pblock = exbh->b_blocknr, cur_logical;
	struct buffer_head *head, *bh;
	pgoff_t index, end;
	struct pagevec pvec;
	int nr_pages, i;

	index = logical >> (PAGE_CACHE_SHIFT - inode->i_blkbits);
	end = (logical + blocks - 1) >> (PAGE_CACHE_SHIFT - inode->i_blkbits);
	cur_logical = index << (PAGE_CACHE_SHIFT - inode->i_blkbits);

	pagevec_init(&pvec, 0);

	while (index <= end) {
		/* XXX: optimize tail */
		nr_pages = pagevec_lookup(&pvec, mapping, index, PAGEVEC_SIZE);
		if (nr_pages == 0)
			break;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			index = page->index;
			if (index > end)
				break;
			index++;

			BUG_ON(!PageLocked(page));
			BUG_ON(PageWriteback(page));
			BUG_ON(!page_has_buffers(page));

			bh = page_buffers(page);
			head = bh;

			/* skip blocks out of the range */
			do {
				if (cur_logical >= logical)
					break;
				cur_logical++;
			} while ((bh = bh->b_this_page) != head);

			do {
				if (cur_logical >= logical + blocks)
					break;

				if (buffer_delay(bh) ||
						buffer_unwritten(bh)) {

					BUG_ON(bh->b_bdev != inode->i_sb->s_bdev);

					if (buffer_delay(bh)) {
						clear_buffer_delay(bh);
						bh->b_blocknr = pblock;
					} else {
						/*
						 * unwritten already should have
						 * blocknr assigned. Verify that
						 */
						clear_buffer_unwritten(bh);
						BUG_ON(bh->b_blocknr != pblock);
					}

				} else if (buffer_mapped(bh))
					BUG_ON(bh->b_blocknr != pblock);

				cur_logical++;
				pblock++;
			} while ((bh = bh->b_this_page) != head);
		}
		pagevec_release(&pvec);
	}
}


/*
 * __unmap_underlying_blocks - just a helper function to unmap
 * set of blocks described by @bh
 */
static inline void __unmap_underlying_blocks(struct inode *inode,
					     struct buffer_head *bh)
{
	struct block_device *bdev = inode->i_sb->s_bdev;
	int blocks, i;

	blocks = bh->b_size >> inode->i_blkbits;
	for (i = 0; i < blocks; i++)
		unmap_underlying_metadata(bdev, bh->b_blocknr + i);
}

static void ext4_da_block_invalidatepages(struct mpage_da_data *mpd,
					sector_t logical, long blk_cnt)
{
	int nr_pages, i;
	pgoff_t index, end;
	struct pagevec pvec;
	struct inode *inode = mpd->inode;
	struct address_space *mapping = inode->i_mapping;

	index = logical >> (PAGE_CACHE_SHIFT - inode->i_blkbits);
	end   = (logical + blk_cnt - 1) >>
				(PAGE_CACHE_SHIFT - inode->i_blkbits);
	while (index <= end) {
		nr_pages = pagevec_lookup(&pvec, mapping, index, PAGEVEC_SIZE);
		if (nr_pages == 0)
			break;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			index = page->index;
			if (index > end)
				break;
			index++;

			BUG_ON(!PageLocked(page));
			BUG_ON(PageWriteback(page));
			block_invalidatepage(page, 0);
			ClearPageUptodate(page);
			unlock_page(page);
		}
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
 * mpage_da_map_blocks - go through given space
 *
 * @mpd - bh describing space
 *
 * The function skips space we know is already mapped to disk blocks.
 *
 */
static int mpage_da_map_blocks(struct mpage_da_data *mpd)
{
	int err, blks, get_blocks_flags;
	struct buffer_head new;
	sector_t next = mpd->b_blocknr;
	unsigned max_blocks = mpd->b_size >> mpd->inode->i_blkbits;
	loff_t disksize = EXT4_I(mpd->inode)->i_disksize;
	handle_t *handle = NULL;

	/*
	 * We consider only non-mapped and non-allocated blocks
	 */
	if ((mpd->b_state  & (1 << BH_Mapped)) &&
		!(mpd->b_state & (1 << BH_Delay)) &&
		!(mpd->b_state & (1 << BH_Unwritten)))
		return 0;

	/*
	 * If we didn't accumulate anything to write simply return
	 */
	if (!mpd->b_size)
		return 0;

	handle = ext4_journal_current_handle();
	BUG_ON(!handle);

	/*
	 * Call ext4_get_blocks() to allocate any delayed allocation
	 * blocks, or to convert an uninitialized extent to be
	 * initialized (in the case where we have written into
	 * one or more preallocated blocks).
	 *
	 * We pass in the magic EXT4_GET_BLOCKS_DELALLOC_RESERVE to
	 * indicate that we are on the delayed allocation path.  This
	 * affects functions in many different parts of the allocation
	 * call path.  This flag exists primarily because we don't
	 * want to change *many* call functions, so ext4_get_blocks()
	 * will set the magic i_delalloc_reserved_flag once the
	 * inode's allocation semaphore is taken.
	 *
	 * If the blocks in questions were delalloc blocks, set
	 * EXT4_GET_BLOCKS_DELALLOC_RESERVE so the delalloc accounting
	 * variables are updated after the blocks have been allocated.
	 */
	new.b_state = 0;
	get_blocks_flags = (EXT4_GET_BLOCKS_CREATE |
			    EXT4_GET_BLOCKS_DELALLOC_RESERVE);
	if (mpd->b_state & (1 << BH_Delay))
		get_blocks_flags |= EXT4_GET_BLOCKS_UPDATE_RESERVE_SPACE;
	blks = ext4_get_blocks(handle, mpd->inode, next, max_blocks,
			       &new, get_blocks_flags);
	if (blks < 0) {
		err = blks;
		/*
		 * If get block returns with error we simply
		 * return. Later writepage will redirty the page and
		 * writepages will find the dirty page again
		 */
		if (err == -EAGAIN)
			return 0;

		if (err == -ENOSPC &&
		    ext4_count_free_blocks(mpd->inode->i_sb)) {
			mpd->retval = err;
			return 0;
		}

		/*
		 * get block failure will cause us to loop in
		 * writepages, because a_ops->writepage won't be able
		 * to make progress. The page will be redirtied by
		 * writepage and writepages will again try to write
		 * the same.
		 */
		ext4_msg(mpd->inode->i_sb, KERN_CRIT,
			 "delayed block allocation failed for inode %lu at "
			 "logical offset %llu with max blocks %zd with "
			 "error %d\n", mpd->inode->i_ino,
			 (unsigned long long) next,
			 mpd->b_size >> mpd->inode->i_blkbits, err);
		printk(KERN_CRIT "This should not happen!!  "
		       "Data will be lost\n");
		if (err == -ENOSPC) {
			ext4_print_free_blocks(mpd->inode);
		}
		/* invalidate all the pages */
		ext4_da_block_invalidatepages(mpd, next,
				mpd->b_size >> mpd->inode->i_blkbits);
		return err;
	}
	BUG_ON(blks == 0);

	new.b_size = (blks << mpd->inode->i_blkbits);

	if (buffer_new(&new))
		__unmap_underlying_blocks(mpd->inode, &new);

	/*
	 * If blocks are delayed marked, we need to
	 * put actual blocknr and drop delayed bit
	 */
	if ((mpd->b_state & (1 << BH_Delay)) ||
	    (mpd->b_state & (1 << BH_Unwritten)))
		mpage_put_bnr_to_bhs(mpd, next, &new);

	if (ext4_should_order_data(mpd->inode)) {
		err = ext4_jbd2_file_inode(handle, mpd->inode);
		if (err)
			return err;
	}

	/*
	 * Update on-disk size along with block allocation.
	 */
	disksize = ((loff_t) next + blks) << mpd->inode->i_blkbits;
	if (disksize > i_size_read(mpd->inode))
		disksize = i_size_read(mpd->inode);
	if (disksize > EXT4_I(mpd->inode)->i_disksize) {
		ext4_update_i_disksize(mpd->inode, disksize);
		return ext4_mark_inode_dirty(handle, mpd->inode);
	}

	return 0;
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

	/* check if thereserved journal credits might overflow */
	if (!(EXT4_I(mpd->inode)->i_flags & EXT4_EXTENTS_FL)) {
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
	if (mpage_da_map_blocks(mpd) == 0)
		mpage_da_submit_io(mpd);
	mpd->io_done = 1;
	return;
}

static int ext4_bh_delay_or_unwritten(handle_t *handle, struct buffer_head *bh)
{
	return (buffer_delay(bh) || buffer_unwritten(bh)) && buffer_dirty(bh);
}

/*
 * __mpage_da_writepage - finds extent of pages and blocks
 *
 * @page: page to consider
 * @wbc: not used, we just follow rules
 * @data: context
 *
 * The function finds extents of pages and scan them for all blocks.
 */
static int __mpage_da_writepage(struct page *page,
				struct writeback_control *wbc, void *data)
{
	struct mpage_da_data *mpd = data;
	struct inode *inode = mpd->inode;
	struct buffer_head *bh, *head;
	sector_t logical;

	if (mpd->io_done) {
		/*
		 * Rest of the page in the page_vec
		 * redirty then and skip then. We will
		 * try to write them again after
		 * starting a new transaction
		 */
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return MPAGE_DA_EXTENT_TAIL;
	}
	/*
	 * Can we merge this page to current extent?
	 */
	if (mpd->next_page != page->index) {
		/*
		 * Nope, we can't. So, we map non-allocated blocks
		 * and start IO on them using writepage()
		 */
		if (mpd->next_page != mpd->first_page) {
			if (mpage_da_map_blocks(mpd) == 0)
				mpage_da_submit_io(mpd);
			/*
			 * skip rest of the page in the page_vec
			 */
			mpd->io_done = 1;
			redirty_page_for_writepage(wbc, page);
			unlock_page(page);
			return MPAGE_DA_EXTENT_TAIL;
		}

		/*
		 * Start next extent of pages ...
		 */
		mpd->first_page = page->index;

		/*
		 * ... and blocks
		 */
		mpd->b_size = 0;
		mpd->b_state = 0;
		mpd->b_blocknr = 0;
	}

	mpd->next_page = page->index + 1;
	logical = (sector_t) page->index <<
		  (PAGE_CACHE_SHIFT - inode->i_blkbits);

	if (!page_has_buffers(page)) {
		mpage_add_bh_to_extent(mpd, logical, PAGE_CACHE_SIZE,
				       (1 << BH_Dirty) | (1 << BH_Uptodate));
		if (mpd->io_done)
			return MPAGE_DA_EXTENT_TAIL;
	} else {
		/*
		 * Page with regular buffer heads, just add all dirty ones
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
					return MPAGE_DA_EXTENT_TAIL;
			} else if (buffer_dirty(bh) && (buffer_mapped(bh))) {
				/*
				 * mapped dirty buffer. We need to update
				 * the b_state because we look at
				 * b_state in mpage_da_map_blocks. We don't
				 * update b_size because if we find an
				 * unmapped buffer_head later we need to
				 * use the b_state flag of that buffer_head.
				 */
				if (mpd->b_size == 0)
					mpd->b_state = bh->b_state & BH_FLAGS;
			}
			logical++;
		} while ((bh = bh->b_this_page) != head);
	}

	return 0;
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
				  struct buffer_head *bh_result, int create)
{
	int ret = 0;
	sector_t invalid_block = ~((sector_t) 0xffff);

	if (invalid_block < ext4_blocks_count(EXT4_SB(inode->i_sb)->s_es))
		invalid_block = ~0;

	BUG_ON(create == 0);
	BUG_ON(bh_result->b_size != inode->i_sb->s_blocksize);

	/*
	 * first, we need to know whether the block is allocated already
	 * preallocated blocks are unmapped but should treated
	 * the same as allocated blocks.
	 */
	ret = ext4_get_blocks(NULL, inode, iblock, 1,  bh_result, 0);
	if ((ret == 0) && !buffer_delay(bh_result)) {
		/* the block isn't (pre)allocated yet, let's reserve space */
		/*
		 * XXX: __block_prepare_write() unmaps passed block,
		 * is it OK?
		 */
		ret = ext4_da_reserve_space(inode, 1);
		if (ret)
			/* not enough space to reserve */
			return ret;

		map_bh(bh_result, inode->i_sb, invalid_block);
		set_buffer_new(bh_result);
		set_buffer_delay(bh_result);
	} else if (ret > 0) {
		bh_result->b_size = (ret << inode->i_blkbits);
		if (buffer_unwritten(bh_result)) {
			/* A delayed write to unwritten bh should
			 * be marked new and mapped.  Mapped ensures
			 * that we don't do get_block multiple times
			 * when we write to the same offset and new
			 * ensures that we do proper zero out for
			 * partial write.
			 */
			set_buffer_new(bh_result);
			set_buffer_mapped(bh_result);
		}
		ret = 0;
	}

	return ret;
}

/*
 * This function is used as a standard get_block_t calback function
 * when there is no desire to allocate any blocks.  It is used as a
 * callback function for block_prepare_write(), nobh_writepage(), and
 * block_write_full_page().  These functions should only try to map a
 * single block at a time.
 *
 * Since this function doesn't do block allocations even if the caller
 * requests it by passing in create=1, it is critically important that
 * any caller checks to make sure that any buffer heads are returned
 * by this function are either all already mapped or marked for
 * delayed allocation before calling nobh_writepage() or
 * block_write_full_page().  Otherwise, b_blocknr could be left
 * unitialized, and the page write functions will be taken by
 * surprise.
 */
static int noalloc_get_block_write(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	int ret = 0;
	unsigned max_blocks = bh_result->b_size >> inode->i_blkbits;

	BUG_ON(bh_result->b_size != inode->i_sb->s_blocksize);

	/*
	 * we don't want to do block allocation in writepage
	 * so call get_block_wrap with create = 0
	 */
	ret = ext4_get_blocks(NULL, inode, iblock, max_blocks, bh_result, 0);
	if (ret > 0) {
		bh_result->b_size = (ret << inode->i_blkbits);
		ret = 0;
	}
	return ret;
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
				       struct writeback_control *wbc,
				       unsigned int len)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	struct buffer_head *page_bufs;
	handle_t *handle = NULL;
	int ret = 0;
	int err;

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
	EXT4_I(inode)->i_state |= EXT4_STATE_JDATA;
out:
	return ret;
}

/*
 * Note that we don't need to start a transaction unless we're journaling data
 * because we should have holes filled from ext4_page_mkwrite(). We even don't
 * need to file the inode to the transaction's list in ordered mode because if
 * we are writing back data added by write(), the inode is already there and if
 * we are writing back data modified via mmap(), noone guarantees in which
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
	int ret = 0;
	loff_t size;
	unsigned int len;
	struct buffer_head *page_bufs;
	struct inode *inode = page->mapping->host;

	trace_ext4_writepage(inode, page);
	size = i_size_read(inode);
	if (page->index == size >> PAGE_CACHE_SHIFT)
		len = size & ~PAGE_CACHE_MASK;
	else
		len = PAGE_CACHE_SIZE;

	if (page_has_buffers(page)) {
		page_bufs = page_buffers(page);
		if (walk_page_buffers(NULL, page_bufs, 0, len, NULL,
					ext4_bh_delay_or_unwritten)) {
			/*
			 * We don't want to do  block allocation
			 * So redirty the page and return
			 * We may reach here when we do a journal commit
			 * via journal_submit_inode_data_buffers.
			 * If we don't have mapping block we just ignore
			 * them. We can also reach here via shrink_page_list
			 */
			redirty_page_for_writepage(wbc, page);
			unlock_page(page);
			return 0;
		}
	} else {
		/*
		 * The test for page_has_buffers() is subtle:
		 * We know the page is dirty but it lost buffers. That means
		 * that at some moment in time after write_begin()/write_end()
		 * has been called all buffers have been clean and thus they
		 * must have been written at least once. So they are all
		 * mapped and we can happily proceed with mapping them
		 * and writing the page.
		 *
		 * Try to initialize the buffer_heads and check whether
		 * all are mapped and non delay. We don't want to
		 * do block allocation here.
		 */
		ret = block_prepare_write(page, 0, len,
					  noalloc_get_block_write);
		if (!ret) {
			page_bufs = page_buffers(page);
			/* check whether all are mapped and non delay */
			if (walk_page_buffers(NULL, page_bufs, 0, len, NULL,
						ext4_bh_delay_or_unwritten)) {
				redirty_page_for_writepage(wbc, page);
				unlock_page(page);
				return 0;
			}
		} else {
			/*
			 * We can't do block allocation here
			 * so just redity the page and unlock
			 * and return
			 */
			redirty_page_for_writepage(wbc, page);
			unlock_page(page);
			return 0;
		}
		/* now mark the buffer_heads as dirty and uptodate */
		block_commit_write(page, 0, len);
	}

	if (PageChecked(page) && ext4_should_journal_data(inode)) {
		/*
		 * It's mmapped pagecache.  Add buffers and journal it.  There
		 * doesn't seem much point in redirtying the page here.
		 */
		ClearPageChecked(page);
		return __ext4_journalled_writepage(page, wbc, len);
	}

	if (test_opt(inode->i_sb, NOBH) && ext4_should_writeback_data(inode))
		ret = nobh_writepage(page, noalloc_get_block_write, wbc);
	else
		ret = block_write_full_page(page, noalloc_get_block_write,
					    wbc);

	return ret;
}

/*
 * This is called via ext4_da_writepages() to
 * calulate the total number of credits to reserve to fit
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
	if (!(EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL) &&
	    (max_blocks > EXT4_MAX_TRANS_DATA))
		max_blocks = EXT4_MAX_TRANS_DATA;

	return ext4_chunk_trans_blocks(inode, max_blocks);
}

static int ext4_da_writepages(struct address_space *mapping,
			      struct writeback_control *wbc)
{
	pgoff_t	index;
	int range_whole = 0;
	handle_t *handle = NULL;
	struct mpage_da_data mpd;
	struct inode *inode = mapping->host;
	int no_nrwrite_index_update;
	int pages_written = 0;
	long pages_skipped;
	unsigned int max_pages;
	int range_cyclic, cycled = 1, io_done = 0;
	int needed_blocks, ret = 0;
	long desired_nr_to_write, nr_to_writebump = 0;
	loff_t range_start = wbc->range_start;
	struct ext4_sb_info *sbi = EXT4_SB(mapping->host->i_sb);

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
	} else
		index = wbc->range_start >> PAGE_CACHE_SHIFT;

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
	if (!range_cyclic && range_whole)
		desired_nr_to_write = wbc->nr_to_write * 8;
	else
		desired_nr_to_write = ext4_num_dirty_pages(inode, index,
							   max_pages);
	if (desired_nr_to_write > max_pages)
		desired_nr_to_write = max_pages;

	if (wbc->nr_to_write < desired_nr_to_write) {
		nr_to_writebump = desired_nr_to_write - wbc->nr_to_write;
		wbc->nr_to_write = desired_nr_to_write;
	}

	mpd.wbc = wbc;
	mpd.inode = mapping->host;

	/*
	 * we don't want write_cache_pages to update
	 * nr_to_write and writeback_index
	 */
	no_nrwrite_index_update = wbc->no_nrwrite_index_update;
	wbc->no_nrwrite_index_update = 1;
	pages_skipped = wbc->pages_skipped;

retry:
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
			       "%ld pages, ino %lu; err %d\n", __func__,
				wbc->nr_to_write, inode->i_ino, ret);
			goto out_writepages;
		}

		/*
		 * Now call __mpage_da_writepage to find the next
		 * contiguous region of logical blocks that need
		 * blocks to be allocated by ext4.  We don't actually
		 * submit the blocks for I/O here, even though
		 * write_cache_pages thinks it will, and will set the
		 * pages as clean for write before calling
		 * __mpage_da_writepage().
		 */
		mpd.b_size = 0;
		mpd.b_state = 0;
		mpd.b_blocknr = 0;
		mpd.first_page = 0;
		mpd.next_page = 0;
		mpd.io_done = 0;
		mpd.pages_written = 0;
		mpd.retval = 0;
		ret = write_cache_pages(mapping, wbc, __mpage_da_writepage,
					&mpd);
		/*
		 * If we have a contigous extent of pages and we
		 * haven't done the I/O yet, map the blocks and submit
		 * them for I/O.
		 */
		if (!mpd.io_done && mpd.next_page != mpd.first_page) {
			if (mpage_da_map_blocks(&mpd) == 0)
				mpage_da_submit_io(&mpd);
			mpd.io_done = 1;
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
			wbc->pages_skipped = pages_skipped;
			ret = 0;
		} else if (ret == MPAGE_DA_EXTENT_TAIL) {
			/*
			 * got one extent now try with
			 * rest of the pages
			 */
			pages_written += mpd.pages_written;
			wbc->pages_skipped = pages_skipped;
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
	if (pages_skipped != wbc->pages_skipped)
		ext4_msg(inode->i_sb, KERN_CRIT,
			 "This should not happen leaving %s "
			 "with nr_to_write = %ld ret = %d\n",
			 __func__, wbc->nr_to_write, ret);

	/* Update index */
	index += pages_written;
	wbc->range_cyclic = range_cyclic;
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		/*
		 * set the writeback_index so that range_cyclic
		 * mode will write it back later
		 */
		mapping->writeback_index = index;

out_writepages:
	if (!no_nrwrite_index_update)
		wbc->no_nrwrite_index_update = 0;
	if (wbc->nr_to_write > nr_to_writebump)
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
	unsigned from, to;
	struct inode *inode = mapping->host;
	handle_t *handle;

	index = pos >> PAGE_CACHE_SHIFT;
	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + len;

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

	ret = block_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				ext4_da_get_block_prep);
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
	 * the pages by calling redirty_page_for_writeback() but that
	 * would be ugly in the extreme.  So instead we would need to
	 * replicate parts of the code in the above functions,
	 * simplifying them becuase we wouldn't actually intend to
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

	if (EXT4_JOURNAL(inode) && EXT4_I(inode)->i_state & EXT4_STATE_JDATA) {
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

		EXT4_I(inode)->i_state &= ~EXT4_STATE_JDATA;
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
	return mpage_readpage(page, ext4_get_block);
}

static int
ext4_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, ext4_get_block);
}

static void ext4_invalidatepage(struct page *page, unsigned long offset)
{
	journal_t *journal = EXT4_JOURNAL(page->mapping->host);

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
	ret = blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
				 offset, nr_segs,
				 ext4_get_block, NULL);
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

static int ext4_get_block_dio_write(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create)
{
	handle_t *handle = NULL;
	int ret = 0;
	unsigned max_blocks = bh_result->b_size >> inode->i_blkbits;
	int dio_credits;

	ext4_debug("ext4_get_block_dio_write: inode %lu, create flag %d\n",
		   inode->i_ino, create);
	/*
	 * DIO VFS code passes create = 0 flag for write to
	 * the middle of file. It does this to avoid block
	 * allocation for holes, to prevent expose stale data
	 * out when there is parallel buffered read (which does
	 * not hold the i_mutex lock) while direct IO write has
	 * not completed. DIO request on holes finally falls back
	 * to buffered IO for this reason.
	 *
	 * For ext4 extent based file, since we support fallocate,
	 * new allocated extent as uninitialized, for holes, we
	 * could fallocate blocks for holes, thus parallel
	 * buffered IO read will zero out the page when read on
	 * a hole while parallel DIO write to the hole has not completed.
	 *
	 * when we come here, we know it's a direct IO write to
	 * to the middle of file (<i_size)
	 * so it's safe to override the create flag from VFS.
	 */
	create = EXT4_GET_BLOCKS_DIO_CREATE_EXT;

	if (max_blocks > DIO_MAX_BLOCKS)
		max_blocks = DIO_MAX_BLOCKS;
	dio_credits = ext4_chunk_trans_blocks(inode, max_blocks);
	handle = ext4_journal_start(inode, dio_credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}
	ret = ext4_get_blocks(handle, inode, iblock, max_blocks, bh_result,
			      create);
	if (ret > 0) {
		bh_result->b_size = (ret << inode->i_blkbits);
		ret = 0;
	}
	ext4_journal_stop(handle);
out:
	return ret;
}

static void ext4_free_io_end(ext4_io_end_t *io)
{
	BUG_ON(!io);
	iput(io->inode);
	kfree(io);
}
static void dump_aio_dio_list(struct inode * inode)
{
#ifdef	EXT4_DEBUG
	struct list_head *cur, *before, *after;
	ext4_io_end_t *io, *io0, *io1;

	if (list_empty(&EXT4_I(inode)->i_aio_dio_complete_list)){
		ext4_debug("inode %lu aio dio list is empty\n", inode->i_ino);
		return;
	}

	ext4_debug("Dump inode %lu aio_dio_completed_IO list \n", inode->i_ino);
	list_for_each_entry(io, &EXT4_I(inode)->i_aio_dio_complete_list, list){
		cur = &io->list;
		before = cur->prev;
		io0 = container_of(before, ext4_io_end_t, list);
		after = cur->next;
		io1 = container_of(after, ext4_io_end_t, list);

		ext4_debug("io 0x%p from inode %lu,prev 0x%p,next 0x%p\n",
			    io, inode->i_ino, io0, io1);
	}
#endif
}

/*
 * check a range of space and convert unwritten extents to written.
 */
static int ext4_end_aio_dio_nolock(ext4_io_end_t *io)
{
	struct inode *inode = io->inode;
	loff_t offset = io->offset;
	ssize_t size = io->size;
	int ret = 0;

	ext4_debug("end_aio_dio_onlock: io 0x%p from inode %lu,list->next 0x%p,"
		   "list->prev 0x%p\n",
	           io, inode->i_ino, io->list.next, io->list.prev);

	if (list_empty(&io->list))
		return ret;

	if (io->flag != DIO_AIO_UNWRITTEN)
		return ret;

	if (offset + size <= i_size_read(inode))
		ret = ext4_convert_unwritten_extents(inode, offset, size);

	if (ret < 0) {
		printk(KERN_EMERG "%s: failed to convert unwritten"
			"extents to written extents, error is %d"
			" io is still on inode %lu aio dio list\n",
                       __func__, ret, inode->i_ino);
		return ret;
	}

	/* clear the DIO AIO unwritten flag */
	io->flag = 0;
	return ret;
}
/*
 * work on completed aio dio IO, to convert unwritten extents to extents
 */
static void ext4_end_aio_dio_work(struct work_struct *work)
{
	ext4_io_end_t *io  = container_of(work, ext4_io_end_t, work);
	struct inode *inode = io->inode;
	int ret = 0;

	mutex_lock(&inode->i_mutex);
	ret = ext4_end_aio_dio_nolock(io);
	if (ret >= 0) {
		if (!list_empty(&io->list))
			list_del_init(&io->list);
		ext4_free_io_end(io);
	}
	mutex_unlock(&inode->i_mutex);
}
/*
 * This function is called from ext4_sync_file().
 *
 * When AIO DIO IO is completed, the work to convert unwritten
 * extents to written is queued on workqueue but may not get immediately
 * scheduled. When fsync is called, we need to ensure the
 * conversion is complete before fsync returns.
 * The inode keeps track of a list of completed AIO from DIO path
 * that might needs to do the conversion. This function walks through
 * the list and convert the related unwritten extents to written.
 */
int flush_aio_dio_completed_IO(struct inode *inode)
{
	ext4_io_end_t *io;
	int ret = 0;
	int ret2 = 0;

	if (list_empty(&EXT4_I(inode)->i_aio_dio_complete_list))
		return ret;

	dump_aio_dio_list(inode);
	while (!list_empty(&EXT4_I(inode)->i_aio_dio_complete_list)){
		io = list_entry(EXT4_I(inode)->i_aio_dio_complete_list.next,
				ext4_io_end_t, list);
		/*
		 * Calling ext4_end_aio_dio_nolock() to convert completed
		 * IO to written.
		 *
		 * When ext4_sync_file() is called, run_queue() may already
		 * about to flush the work corresponding to this io structure.
		 * It will be upset if it founds the io structure related
		 * to the work-to-be schedule is freed.
		 *
		 * Thus we need to keep the io structure still valid here after
		 * convertion finished. The io structure has a flag to
		 * avoid double converting from both fsync and background work
		 * queue work.
		 */
		ret = ext4_end_aio_dio_nolock(io);
		if (ret < 0)
			ret2 = ret;
		else
			list_del_init(&io->list);
	}
	return (ret2 < 0) ? ret2 : 0;
}

static ext4_io_end_t *ext4_init_io_end (struct inode *inode)
{
	ext4_io_end_t *io = NULL;

	io = kmalloc(sizeof(*io), GFP_NOFS);

	if (io) {
		igrab(inode);
		io->inode = inode;
		io->flag = 0;
		io->offset = 0;
		io->size = 0;
		io->error = 0;
		INIT_WORK(&io->work, ext4_end_aio_dio_work);
		INIT_LIST_HEAD(&io->list);
	}

	return io;
}

static void ext4_end_io_dio(struct kiocb *iocb, loff_t offset,
			    ssize_t size, void *private)
{
        ext4_io_end_t *io_end = iocb->private;
	struct workqueue_struct *wq;

	/* if not async direct IO or dio with 0 bytes write, just return */
	if (!io_end || !size)
		return;

	ext_debug("ext4_end_io_dio(): io_end 0x%p"
		  "for inode %lu, iocb 0x%p, offset %llu, size %llu\n",
 		  iocb->private, io_end->inode->i_ino, iocb, offset,
		  size);

	/* if not aio dio with unwritten extents, just free io and return */
	if (io_end->flag != DIO_AIO_UNWRITTEN){
		ext4_free_io_end(io_end);
		iocb->private = NULL;
		return;
	}

	io_end->offset = offset;
	io_end->size = size;
	wq = EXT4_SB(io_end->inode->i_sb)->dio_unwritten_wq;

	/* queue the work to convert unwritten extents to written */
	queue_work(wq, &io_end->work);

	/* Add the io_end to per-inode completed aio dio list*/
	list_add_tail(&io_end->list,
		 &EXT4_I(io_end->inode)->i_aio_dio_complete_list);
	iocb->private = NULL;
}
/*
 * For ext4 extent files, ext4 will do direct-io write to holes,
 * preallocated extents, and those write extend the file, no need to
 * fall back to buffered IO.
 *
 * For holes, we fallocate those blocks, mark them as unintialized
 * If those blocks were preallocated, we mark sure they are splited, but
 * still keep the range to write as unintialized.
 *
 * The unwrritten extents will be converted to written when DIO is completed.
 * For async direct IO, since the IO may still pending when return, we
 * set up an end_io call back function, which will do the convertion
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
 		 * to prevent paralel buffered read to expose the stale data
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
			iocb->private = ext4_init_io_end(inode);
			if (!iocb->private)
				return -ENOMEM;
			/*
			 * we save the io structure for current async
			 * direct IO, so that later ext4_get_blocks()
			 * could flag the io structure whether there
			 * is a unwritten extents needs to be converted
			 * when IO is completed.
			 */
			EXT4_I(inode)->cur_aio_dio = iocb->private;
		}

		ret = blockdev_direct_IO(rw, iocb, inode,
					 inode->i_sb->s_bdev, iov,
					 offset, nr_segs,
					 ext4_get_block_dio_write,
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
		} else if (ret > 0 && (EXT4_I(inode)->i_state &
				       EXT4_STATE_DIO_UNWRITTEN)) {
			int err;
			/*
			 * for non AIO case, since the IO is already
			 * completed, we could do the convertion right here
			 */
			err = ext4_convert_unwritten_extents(inode,
							     offset, ret);
			if (err < 0)
				ret = err;
			EXT4_I(inode)->i_state &= ~EXT4_STATE_DIO_UNWRITTEN;
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

	if (EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL)
		return ext4_ext_direct_IO(rw, iocb, iov, offset, nr_segs);

	return ext4_ind_direct_IO(rw, iocb, iov, offset, nr_segs);
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
	.sync_page		= block_sync_page,
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
	.sync_page		= block_sync_page,
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
	.sync_page		= block_sync_page,
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
	.sync_page		= block_sync_page,
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
	ext4_fsblk_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned blocksize, length, pos;
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
	length = blocksize - (offset & (blocksize - 1));
	iblock = index << (PAGE_CACHE_SHIFT - inode->i_sb->s_blocksize_bits);

	/*
	 * For "nobh" option,  we can only work if we don't need to
	 * read-in the page - otherwise we create buffers to do the IO.
	 */
	if (!page_has_buffers(page) && test_opt(inode->i_sb, NOBH) &&
	     ext4_should_writeback_data(inode) && PageUptodate(page)) {
		zero_user(page, offset, length);
		set_page_dirty(page);
		goto unlock;
	}

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
		if (ext4_should_order_data(inode))
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
 *	partially truncated if some data below the new i_size is refered
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
	/* Make k index the deepest non-null offest + 1 */
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
 */
static void ext4_clear_blocks(handle_t *handle, struct inode *inode,
			      struct buffer_head *bh,
			      ext4_fsblk_t block_to_free,
			      unsigned long count, __le32 *first,
			      __le32 *last)
{
	__le32 *p;
	int	is_metadata = S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode);

	if (try_to_extend_transaction(handle, inode)) {
		if (bh) {
			BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
			ext4_handle_dirty_metadata(handle, inode, bh);
		}
		ext4_mark_inode_dirty(handle, inode);
		ext4_truncate_restart_trans(handle, inode,
					    blocks_for_truncate(inode));
		if (bh) {
			BUFFER_TRACE(bh, "retaking write access");
			ext4_journal_get_write_access(handle, bh);
		}
	}

	/*
	 * Any buffers which are on the journal will be in memory. We
	 * find them on the hash table so jbd2_journal_revoke() will
	 * run jbd2_journal_forget() on them.  We've already detached
	 * each block from the file, so bforget() in
	 * jbd2_journal_forget() should be safe.
	 *
	 * AKPM: turn on bforget in jbd2_journal_forget()!!!
	 */
	for (p = first; p < last; p++) {
		u32 nr = le32_to_cpu(*p);
		if (nr) {
			struct buffer_head *tbh;

			*p = 0;
			tbh = sb_find_get_block(inode->i_sb, nr);
			ext4_forget(handle, is_metadata, inode, tbh, nr);
		}
	}

	ext4_free_blocks(handle, inode, block_to_free, count, is_metadata);
}

/**
 * ext4_free_data - free a list of data blocks
 * @handle:	handle for this transaction
 * @inode:	inode we are dealing with
 * @this_bh:	indirect buffer_head which contains *@first and *@last
 * @first:	array of block numbers
 * @last:	points immediately past the end of array
 *
 * We are freeing all blocks refered from that array (numbers are stored as
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
	int err;

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
				ext4_clear_blocks(handle, inode, this_bh,
						  block_to_free,
						  count, block_to_free_p, p);
				block_to_free = nr;
				block_to_free_p = p;
				count = 1;
			}
		}
	}

	if (count > 0)
		ext4_clear_blocks(handle, inode, this_bh, block_to_free,
				  count, block_to_free_p, p);

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
			ext4_error(inode->i_sb, __func__,
				   "circular indirect block detected, "
				   "inode=%lu, block=%llu",
				   inode->i_ino,
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
 *	We are freeing all blocks refered from these branches (numbers are
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

			/* Go read the buffer for the next level down */
			bh = sb_bread(inode->i_sb, nr);

			/*
			 * A read failure? Report error and clear slot
			 * (should be rare).
			 */
			if (!bh) {
				ext4_error(inode->i_sb, "ext4_free_branches",
					   "Read failure, inode=%lu, block=%llu",
					   inode->i_ino, nr);
				continue;
			}

			/* This zaps the entire block.  Bottom up. */
			BUFFER_TRACE(bh, "free child branches");
			ext4_free_branches(handle, inode, bh,
					(__le32 *) bh->b_data,
					(__le32 *) bh->b_data + addr_per_block,
					depth);

			/*
			 * We've probably journalled the indirect block several
			 * times during the truncate.  But it's no longer
			 * needed and we now drop it from the transaction via
			 * jbd2_journal_revoke().
			 *
			 * That's easy if it's exclusively part of this
			 * transaction.  But if it's part of the committing
			 * transaction then jbd2_journal_forget() will simply
			 * brelse() it.  That means that if the underlying
			 * block is reallocated in ext4_get_block(),
			 * unmap_underlying_metadata() will find this block
			 * and will try to get rid of it.  damn, damn.
			 *
			 * If this block has already been committed to the
			 * journal, a revoke record will be written.  And
			 * revoke records must be emitted *before* clearing
			 * this block's bit in the bitmaps.
			 */
			ext4_forget(handle, 1, inode, bh, bh->b_blocknr);

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

			ext4_free_blocks(handle, inode, nr, 1, 1);

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
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return 0;
	if (S_ISREG(inode->i_mode))
		return 1;
	if (S_ISDIR(inode->i_mode))
		return 1;
	if (S_ISLNK(inode->i_mode))
		return !ext4_inode_is_fast_symlink(inode);
	return 0;
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
	int n;
	ext4_lblk_t last_block;
	unsigned blocksize = inode->i_sb->s_blocksize;

	if (!ext4_can_truncate(inode))
		return;

	if (inode->i_size == 0 && !test_opt(inode->i_sb, NO_AUTO_DA_ALLOC))
		ei->i_state |= EXT4_STATE_DA_ALLOC_CLOSE;

	if (EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL) {
		ext4_ext_truncate(inode);
		return;
	}

	handle = start_transaction(inode);
	if (IS_ERR(handle))
		return;		/* AKPM: return what? */

	last_block = (inode->i_size + blocksize-1)
					>> EXT4_BLOCK_SIZE_BITS(inode->i_sb);

	if (inode->i_size & (blocksize - 1))
		if (ext4_block_truncate_page(handle, mapping, inode->i_size))
			goto out_stop;

	n = ext4_block_to_path(inode, last_block, offsets, NULL);
	if (n == 0)
		goto out_stop;	/* error */

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

	if (n == 1) {		/* direct blocks */
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
	inodes_per_block = (EXT4_BLOCK_SIZE(sb) / EXT4_INODE_SIZE(sb));
	inode_offset = ((inode->i_ino - 1) %
			EXT4_INODES_PER_GROUP(sb));
	block = ext4_inode_table(sb, gdp) + (inode_offset / inodes_per_block);
	iloc->offset = (inode_offset % inodes_per_block) * EXT4_INODE_SIZE(sb);

	bh = sb_getblk(sb, block);
	if (!bh) {
		ext4_error(sb, "ext4_get_inode_loc", "unable to read "
			   "inode block - inode=%lu, block=%llu",
			   inode->i_ino, block);
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
		get_bh(bh);
		bh->b_end_io = end_buffer_read_sync;
		submit_bh(READ_META, bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			ext4_error(sb, __func__,
				   "unable to read inode block - inode=%lu, "
				   "block=%llu", inode->i_ino, block);
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
		!(EXT4_I(inode)->i_state & EXT4_STATE_XATTR));
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
	unsigned int flags = ei->vfs_inode.i_flags;

	ei->i_flags &= ~(EXT4_SYNC_FL|EXT4_APPEND_FL|
			EXT4_IMMUTABLE_FL|EXT4_NOATIME_FL|EXT4_DIRSYNC_FL);
	if (flags & S_SYNC)
		ei->i_flags |= EXT4_SYNC_FL;
	if (flags & S_APPEND)
		ei->i_flags |= EXT4_APPEND_FL;
	if (flags & S_IMMUTABLE)
		ei->i_flags |= EXT4_IMMUTABLE_FL;
	if (flags & S_NOATIME)
		ei->i_flags |= EXT4_NOATIME_FL;
	if (flags & S_DIRSYNC)
		ei->i_flags |= EXT4_DIRSYNC_FL;
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
		if (ei->i_flags & EXT4_HUGE_FILE_FL) {
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
	iloc.bh = 0;

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

	ei->i_state = 0;
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

		spin_lock(&journal->j_state_lock);
		if (journal->j_running_transaction)
			transaction = journal->j_running_transaction;
		else
			transaction = journal->j_committing_transaction;
		if (transaction)
			tid = transaction->t_tid;
		else
			tid = journal->j_commit_sequence;
		spin_unlock(&journal->j_state_lock);
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
				ei->i_state |= EXT4_STATE_XATTR;
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
		ext4_error(sb, __func__,
			   "bad extended attribute block %llu in inode #%lu",
			   ei->i_file_acl, inode->i_ino);
		ret = -EIO;
		goto bad_inode;
	} else if (ei->i_flags & EXT4_EXTENTS_FL) {
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
		ext4_error(inode->i_sb, __func__,
			   "bogus i_mode (%o) for inode=%lu",
			   inode->i_mode, inode->i_ino);
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
		ei->i_flags &= ~EXT4_HUGE_FILE_FL;
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
		ei->i_flags &= ~EXT4_HUGE_FILE_FL;
	} else {
		ei->i_flags |= EXT4_HUGE_FILE_FL;
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
	if (ei->i_state & EXT4_STATE_NEW)
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
	raw_inode->i_flags = cpu_to_le32(ei->i_flags);
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
			err = ext4_handle_dirty_metadata(handle, inode,
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
	rc = ext4_handle_dirty_metadata(handle, inode, bh);
	if (!err)
		err = rc;
	ei->i_state &= ~EXT4_STATE_NEW;

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
int ext4_write_inode(struct inode *inode, int wait)
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

		if (!wait)
			return 0;

		err = ext4_force_commit(inode->i_sb);
	} else {
		struct ext4_iloc iloc;

		err = ext4_get_inode_loc(inode, &iloc);
		if (err)
			return err;
		if (wait)
			sync_dirty_buffer(iloc.bh);
		if (buffer_req(iloc.bh) && !buffer_uptodate(iloc.bh)) {
			ext4_error(inode->i_sb, __func__,
				   "IO error syncing inode, "
				   "inode=%lu, block=%llu",
				   inode->i_ino,
				   (unsigned long long)iloc.bh->b_blocknr);
			err = -EIO;
		}
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
	const unsigned int ia_valid = attr->ia_valid;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

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
		error = vfs_dq_transfer(inode, attr) ? -EDQUOT : 0;
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
		if (!(EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL)) {
			struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

			if (attr->ia_size > sbi->s_bitmap_maxbytes) {
				error = -EFBIG;
				goto err_out;
			}
		}
	}

	if (S_ISREG(inode->i_mode) &&
	    attr->ia_valid & ATTR_SIZE && attr->ia_size < inode->i_size) {
		handle_t *handle;

		handle = ext4_journal_start(inode, 3);
		if (IS_ERR(handle)) {
			error = PTR_ERR(handle);
			goto err_out;
		}

		error = ext4_orphan_add(handle, inode);
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
				ext4_journal_stop(handle);
				goto err_out;
			}
		}
	}

	rc = inode_setattr(inode, attr);

	/* If inode_setattr's call to ext4_truncate failed to get a
	 * transaction handle at all, we need to clean up the in-core
	 * orphan list manually. */
	if (inode->i_nlink)
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
	spin_lock(&EXT4_I(inode)->i_block_reservation_lock);
	delalloc_blocks = EXT4_I(inode)->i_reserved_data_blocks;
	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

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
		 * With N contiguous data blocks, it need at most
		 * N/EXT4_ADDR_PER_BLOCK(inode->i_sb) indirect blocks
		 * 2 dindirect blocks
		 * 1 tindirect block
		 */
		indirects = nrblocks / EXT4_ADDR_PER_BLOCK(inode->i_sb);
		return indirects + 3;
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
	if (!(EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL))
		return ext4_indirect_trans_blocks(inode, nrblocks, chunk);
	return ext4_ext_index_trans_blocks(inode, nrblocks, chunk);
}

/*
 * Account for index blocks, block groups bitmaps and block group
 * descriptor blocks if modify datablocks and index blocks
 * worse case, the indexs blocks spread over different block groups
 *
 * If datablocks are discontiguous, they are possible to spread over
 * different block groups too. If they are contiugous, with flexbg,
 * they could still across block group boundary.
 *
 * Also account for superblock, inode, quota and xattr blocks
 */
int ext4_meta_trans_blocks(struct inode *inode, int nrblocks, int chunk)
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
 * Calulate the total number of credits to reserve to fit
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
 * ext4_get_blocks() to map/allocate a chunk of contigous disk blocks.
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
	struct ext4_xattr_entry *entry;

	if (EXT4_I(inode)->i_extra_isize >= new_extra_isize)
		return 0;

	raw_inode = ext4_raw_inode(&iloc);

	header = IHDR(inode, raw_inode);
	entry = IFIRST(header);

	/* No extended attributes present */
	if (!(EXT4_I(inode)->i_state & EXT4_STATE_XATTR) ||
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
	err = ext4_reserve_inode_write(handle, inode, &iloc);
	if (ext4_handle_valid(handle) &&
	    EXT4_I(inode)->i_extra_isize < sbi->s_want_extra_isize &&
	    !(EXT4_I(inode)->i_state & EXT4_STATE_NO_EXPAND)) {
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
				EXT4_I(inode)->i_state |= EXT4_STATE_NO_EXPAND;
				if (mnt_count !=
					le16_to_cpu(sbi->s_es->s_mnt_count)) {
					ext4_warning(inode->i_sb, __func__,
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
 * Also, vfs_dq_alloc_block() will always dirty the inode when blocks
 * are allocated to the file.
 *
 * If the inode is marked synchronous, we don't honour that here - doing
 * so would cause a commit on atime updates, which we don't bother doing.
 * We handle synchronous inodes at the highest possible level.
 */
void ext4_dirty_inode(struct inode *inode)
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
								 inode,
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
		EXT4_I(inode)->i_flags |= EXT4_JOURNAL_DATA_FL;
	else
		EXT4_I(inode)->i_flags &= ~EXT4_JOURNAL_DATA_FL;
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
	if (PageMappedToDisk(page))
		goto out_unlock;

	if (page->index == size >> PAGE_CACHE_SHIFT)
		len = size & ~PAGE_CACHE_MASK;
	else
		len = PAGE_CACHE_SIZE;

	lock_page(page);
	/*
	 * return if we have all the buffers mapped. This avoid
	 * the need to call write_begin/write_end which does a
	 * journal_start/journal_stop which can block and take
	 * long time
	 */
	if (page_has_buffers(page)) {
		if (!walk_page_buffers(NULL, page_buffers(page), 0, len, NULL,
					ext4_bh_unmapped)) {
			unlock_page(page);
			goto out_unlock;
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
out_unlock:
	if (ret)
		ret = VM_FAULT_SIGBUS;
	up_read(&inode->i_alloc_sem);
	return ret;
}
