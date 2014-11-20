/*
 *  linux/fs/ext3/inode.c
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
 *  Assorted race fixes, rewrite of ext3_get_block() by Al Viro, 2000
 */

#include <linux/highuid.h>
#include <linux/quotaops.h>
#include <linux/writeback.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/aio.h>
#include "ext3.h"
#include "xattr.h"
#include "acl.h"

static int ext3_writepage_trans_blocks(struct inode *inode);
static int ext3_block_truncate_page(struct inode *inode, loff_t from);

/*
 * Test whether an inode is a fast symlink.
 */
static int ext3_inode_is_fast_symlink(struct inode *inode)
{
	int ea_blocks = EXT3_I(inode)->i_file_acl ?
		(inode->i_sb->s_blocksize >> 9) : 0;

	return (S_ISLNK(inode->i_mode) && inode->i_blocks - ea_blocks == 0);
}

/*
 * The ext3 forget function must perform a revoke if we are freeing data
 * which has been journaled.  Metadata (eg. indirect blocks) must be
 * revoked in all cases.
 *
 * "bh" may be NULL: a metadata block may have been freed from memory
 * but there may still be a record of it in the journal, and that record
 * still needs to be revoked.
 */
int ext3_forget(handle_t *handle, int is_metadata, struct inode *inode,
			struct buffer_head *bh, ext3_fsblk_t blocknr)
{
	int err;

	might_sleep();

	trace_ext3_forget(inode, is_metadata, blocknr);
	BUFFER_TRACE(bh, "enter");

	jbd_debug(4, "forgetting bh %p: is_metadata = %d, mode %o, "
		  "data mode %lx\n",
		  bh, is_metadata, inode->i_mode,
		  test_opt(inode->i_sb, DATA_FLAGS));

	/* Never use the revoke function if we are doing full data
	 * journaling: there is no need to, and a V1 superblock won't
	 * support it.  Otherwise, only skip the revoke on un-journaled
	 * data blocks. */

	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT3_MOUNT_JOURNAL_DATA ||
	    (!is_metadata && !ext3_should_journal_data(inode))) {
		if (bh) {
			BUFFER_TRACE(bh, "call journal_forget");
			return ext3_journal_forget(handle, bh);
		}
		return 0;
	}

	/*
	 * data!=journal && (is_metadata || should_journal_data(inode))
	 */
	BUFFER_TRACE(bh, "call ext3_journal_revoke");
	err = ext3_journal_revoke(handle, blocknr, bh);
	if (err)
		ext3_abort(inode->i_sb, __func__,
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
	unsigned long needed;

	needed = inode->i_blocks >> (inode->i_sb->s_blocksize_bits - 9);

	/* Give ourselves just enough room to cope with inodes in which
	 * i_blocks is corrupt: we've seen disk corruptions in the past
	 * which resulted in random data in an inode which looked enough
	 * like a regular file for ext3 to try to delete it.  Things
	 * will go a bit crazy if that happens, but at least we should
	 * try not to panic the whole kernel. */
	if (needed < 2)
		needed = 2;

	/* But we need to bound the transaction so we don't overflow the
	 * journal. */
	if (needed > EXT3_MAX_TRANS_DATA)
		needed = EXT3_MAX_TRANS_DATA;

	return EXT3_DATA_TRANS_BLOCKS(inode->i_sb) + needed;
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

	result = ext3_journal_start(inode, blocks_for_truncate(inode));
	if (!IS_ERR(result))
		return result;

	ext3_std_error(inode->i_sb, PTR_ERR(result));
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
	if (handle->h_buffer_credits > EXT3_RESERVE_TRANS_BLOCKS)
		return 0;
	if (!ext3_journal_extend(handle, blocks_for_truncate(inode)))
		return 0;
	return 1;
}

/*
 * Restart the transaction associated with *handle.  This does a commit,
 * so before we call here everything must be consistently dirtied against
 * this transaction.
 */
static int truncate_restart_transaction(handle_t *handle, struct inode *inode)
{
	int ret;

	jbd_debug(2, "restarting handle %p\n", handle);
	/*
	 * Drop truncate_mutex to avoid deadlock with ext3_get_blocks_handle
	 * At this moment, get_block can be called only for blocks inside
	 * i_size since page cache has been already dropped and writes are
	 * blocked by i_mutex. So we can safely drop the truncate_mutex.
	 */
	mutex_unlock(&EXT3_I(inode)->truncate_mutex);
	ret = ext3_journal_restart(handle, blocks_for_truncate(inode));
	mutex_lock(&EXT3_I(inode)->truncate_mutex);
	return ret;
}

/*
 * Called at inode eviction from icache
 */
void ext3_evict_inode (struct inode *inode)
{
	struct ext3_inode_info *ei = EXT3_I(inode);
	struct ext3_block_alloc_info *rsv;
	handle_t *handle;
	int want_delete = 0;

	trace_ext3_evict_inode(inode);
	if (!inode->i_nlink && !is_bad_inode(inode)) {
		dquot_initialize(inode);
		want_delete = 1;
	}

	/*
	 * When journalling data dirty buffers are tracked only in the journal.
	 * So although mm thinks everything is clean and ready for reaping the
	 * inode might still have some pages to write in the running
	 * transaction or waiting to be checkpointed. Thus calling
	 * journal_invalidatepage() (via truncate_inode_pages()) to discard
	 * these buffers can cause data loss. Also even if we did not discard
	 * these buffers, we would have no way to find them after the inode
	 * is reaped and thus user could see stale data if he tries to read
	 * them before the transaction is checkpointed. So be careful and
	 * force everything to disk here... We use ei->i_datasync_tid to
	 * store the newest transaction containing inode's data.
	 *
	 * Note that directories do not have this problem because they don't
	 * use page cache.
	 *
	 * The s_journal check handles the case when ext3_get_journal() fails
	 * and puts the journal inode.
	 */
	if (inode->i_nlink && ext3_should_journal_data(inode) &&
	    EXT3_SB(inode->i_sb)->s_journal &&
	    (S_ISLNK(inode->i_mode) || S_ISREG(inode->i_mode)) &&
	    inode->i_ino != EXT3_JOURNAL_INO) {
		tid_t commit_tid = atomic_read(&ei->i_datasync_tid);
		journal_t *journal = EXT3_SB(inode->i_sb)->s_journal;

		log_start_commit(journal, commit_tid);
		log_wait_commit(journal, commit_tid);
		filemap_write_and_wait(&inode->i_data);
	}
	truncate_inode_pages_final(&inode->i_data);

	ext3_discard_reservation(inode);
	rsv = ei->i_block_alloc_info;
	ei->i_block_alloc_info = NULL;
	if (unlikely(rsv))
		kfree(rsv);

	if (!want_delete)
		goto no_delete;

	handle = start_transaction(inode);
	if (IS_ERR(handle)) {
		/*
		 * If we're going to skip the normal cleanup, we still need to
		 * make sure that the in-core orphan linked list is properly
		 * cleaned up.
		 */
		ext3_orphan_del(NULL, inode);
		goto no_delete;
	}

	if (IS_SYNC(inode))
		handle->h_sync = 1;
	inode->i_size = 0;
	if (inode->i_blocks)
		ext3_truncate(inode);
	/*
	 * Kill off the orphan record created when the inode lost the last
	 * link.  Note that ext3_orphan_del() has to be able to cope with the
	 * deletion of a non-existent orphan - ext3_truncate() could
	 * have removed the record.
	 */
	ext3_orphan_del(handle, inode);
	ei->i_dtime = get_seconds();

	/*
	 * One subtle ordering requirement: if anything has gone wrong
	 * (transaction abort, IO errors, whatever), then we can still
	 * do these next steps (the fs will already have been marked as
	 * having errors), but we can't free the inode if the mark_dirty
	 * fails.
	 */
	if (ext3_mark_inode_dirty(handle, inode)) {
		/* If that failed, just dquot_drop() and be done with that */
		dquot_drop(inode);
		clear_inode(inode);
	} else {
		ext3_xattr_delete_inode(handle, inode);
		dquot_free_inode(inode);
		dquot_drop(inode);
		clear_inode(inode);
		ext3_free_inode(handle, inode);
	}
	ext3_journal_stop(handle);
	return;
no_delete:
	clear_inode(inode);
	dquot_drop(inode);
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

static int verify_chain(Indirect *from, Indirect *to)
{
	while (from <= to && from->key == *from->p)
		from++;
	return (from > to);
}

/**
 *	ext3_block_to_path - parse the block number into array of offsets
 *	@inode: inode in question (we are only interested in its superblock)
 *	@i_block: block number to be parsed
 *	@offsets: array to store the offsets in
 *      @boundary: set this non-zero if the referred-to block is likely to be
 *             followed (on disk) by an indirect block.
 *
 *	To store the locations of file's data ext3 uses a data structure common
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

static int ext3_block_to_path(struct inode *inode,
			long i_block, int offsets[4], int *boundary)
{
	int ptrs = EXT3_ADDR_PER_BLOCK(inode->i_sb);
	int ptrs_bits = EXT3_ADDR_PER_BLOCK_BITS(inode->i_sb);
	const long direct_blocks = EXT3_NDIR_BLOCKS,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2));
	int n = 0;
	int final = 0;

	if (i_block < 0) {
		ext3_warning (inode->i_sb, "ext3_block_to_path", "block < 0");
	} else if (i_block < direct_blocks) {
		offsets[n++] = i_block;
		final = direct_blocks;
	} else if ( (i_block -= direct_blocks) < indirect_blocks) {
		offsets[n++] = EXT3_IND_BLOCK;
		offsets[n++] = i_block;
		final = ptrs;
	} else if ((i_block -= indirect_blocks) < double_blocks) {
		offsets[n++] = EXT3_DIND_BLOCK;
		offsets[n++] = i_block >> ptrs_bits;
		offsets[n++] = i_block & (ptrs - 1);
		final = ptrs;
	} else if (((i_block -= double_blocks) >> (ptrs_bits * 2)) < ptrs) {
		offsets[n++] = EXT3_TIND_BLOCK;
		offsets[n++] = i_block >> (ptrs_bits * 2);
		offsets[n++] = (i_block >> ptrs_bits) & (ptrs - 1);
		offsets[n++] = i_block & (ptrs - 1);
		final = ptrs;
	} else {
		ext3_warning(inode->i_sb, "ext3_block_to_path", "block > big");
	}
	if (boundary)
		*boundary = final - 1 - (i_block & (ptrs - 1));
	return n;
}

/**
 *	ext3_get_branch - read the chain of indirect blocks leading to data
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
static Indirect *ext3_get_branch(struct inode *inode, int depth, int *offsets,
				 Indirect chain[4], int *err)
{
	struct super_block *sb = inode->i_sb;
	Indirect *p = chain;
	struct buffer_head *bh;

	*err = 0;
	/* i_data is not going away, no lock needed */
	add_chain (chain, NULL, EXT3_I(inode)->i_data + *offsets);
	if (!p->key)
		goto no_block;
	while (--depth) {
		bh = sb_bread(sb, le32_to_cpu(p->key));
		if (!bh)
			goto failure;
		/* Reader: pointers */
		if (!verify_chain(chain, p))
			goto changed;
		add_chain(++p, bh, (__le32*)bh->b_data + *++offsets);
		/* Reader: end */
		if (!p->key)
			goto no_block;
	}
	return NULL;

changed:
	brelse(bh);
	*err = -EAGAIN;
	goto no_block;
failure:
	*err = -EIO;
no_block:
	return p;
}

/**
 *	ext3_find_near - find a place for allocation with sufficient locality
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
static ext3_fsblk_t ext3_find_near(struct inode *inode, Indirect *ind)
{
	struct ext3_inode_info *ei = EXT3_I(inode);
	__le32 *start = ind->bh ? (__le32*) ind->bh->b_data : ei->i_data;
	__le32 *p;
	ext3_fsblk_t bg_start;
	ext3_grpblk_t colour;

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
	bg_start = ext3_group_first_block_no(inode->i_sb, ei->i_block_group);
	colour = (current->pid % 16) *
			(EXT3_BLOCKS_PER_GROUP(inode->i_sb) / 16);
	return bg_start + colour;
}

/**
 *	ext3_find_goal - find a preferred place for allocation.
 *	@inode: owner
 *	@block:  block we want
 *	@partial: pointer to the last triple within a chain
 *
 *	Normally this function find the preferred place for block allocation,
 *	returns it.
 */

static ext3_fsblk_t ext3_find_goal(struct inode *inode, long block,
				   Indirect *partial)
{
	struct ext3_block_alloc_info *block_i;

	block_i =  EXT3_I(inode)->i_block_alloc_info;

	/*
	 * try the heuristic for sequential allocation,
	 * failing that at least try to get decent locality.
	 */
	if (block_i && (block == block_i->last_alloc_logical_block + 1)
		&& (block_i->last_alloc_physical_block != 0)) {
		return block_i->last_alloc_physical_block + 1;
	}

	return ext3_find_near(inode, partial);
}

/**
 *	ext3_blks_to_allocate - Look up the block map and count the number
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
static int ext3_blks_to_allocate(Indirect *branch, int k, unsigned long blks,
		int blocks_to_boundary)
{
	unsigned long count = 0;

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
 *	ext3_alloc_blocks - multiple allocate blocks needed for a branch
 *	@handle: handle for this transaction
 *	@inode: owner
 *	@goal: preferred place for allocation
 *	@indirect_blks: the number of blocks need to allocate for indirect
 *			blocks
 *	@blks:	number of blocks need to allocated for direct blocks
 *	@new_blocks: on return it will store the new block numbers for
 *	the indirect blocks(if needed) and the first direct block,
 *	@err: here we store the error value
 *
 *	return the number of direct blocks allocated
 */
static int ext3_alloc_blocks(handle_t *handle, struct inode *inode,
			ext3_fsblk_t goal, int indirect_blks, int blks,
			ext3_fsblk_t new_blocks[4], int *err)
{
	int target, i;
	unsigned long count = 0;
	int index = 0;
	ext3_fsblk_t current_block = 0;
	int ret = 0;

	/*
	 * Here we try to allocate the requested multiple blocks at once,
	 * on a best-effort basis.
	 * To build a branch, we should allocate blocks for
	 * the indirect blocks(if not allocated yet), and at least
	 * the first direct block of this branch.  That's the
	 * minimum number of blocks need to allocate(required)
	 */
	target = blks + indirect_blks;

	while (1) {
		count = target;
		/* allocating blocks for indirect blocks and direct blocks */
		current_block = ext3_new_blocks(handle,inode,goal,&count,err);
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
		ext3_free_blocks(handle, inode, new_blocks[i], 1);
	return ret;
}

/**
 *	ext3_alloc_branch - allocate and set up a chain of blocks.
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
 *	the same format as ext3_get_branch() would do. We are calling it after
 *	we had read the existing part of chain and partial points to the last
 *	triple of that (one with zero ->key). Upon the exit we have the same
 *	picture as after the successful ext3_get_block(), except that in one
 *	place chain is disconnected - *branch->p is still zero (we did not
 *	set the last link), but branch->key contains the number that should
 *	be placed into *branch->p to fill that gap.
 *
 *	If allocation fails we free all blocks we've allocated (and forget
 *	their buffer_heads) and return the error value the from failed
 *	ext3_alloc_block() (normally -ENOSPC). Otherwise we set the chain
 *	as described above and return 0.
 */
static int ext3_alloc_branch(handle_t *handle, struct inode *inode,
			int indirect_blks, int *blks, ext3_fsblk_t goal,
			int *offsets, Indirect *branch)
{
	int blocksize = inode->i_sb->s_blocksize;
	int i, n = 0;
	int err = 0;
	struct buffer_head *bh;
	int num;
	ext3_fsblk_t new_blocks[4];
	ext3_fsblk_t current_block;

	num = ext3_alloc_blocks(handle, inode, goal, indirect_blks,
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
			err = -ENOMEM;
			goto failed;
		}
		branch[n].bh = bh;
		lock_buffer(bh);
		BUFFER_TRACE(bh, "call get_create_access");
		err = ext3_journal_get_create_access(handle, bh);
		if (err) {
			unlock_buffer(bh);
			brelse(bh);
			goto failed;
		}

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
		BUFFER_TRACE(bh, "marking uptodate");
		set_buffer_uptodate(bh);
		unlock_buffer(bh);

		BUFFER_TRACE(bh, "call ext3_journal_dirty_metadata");
		err = ext3_journal_dirty_metadata(handle, bh);
		if (err)
			goto failed;
	}
	*blks = num;
	return err;
failed:
	/* Allocation failed, free what we already allocated */
	for (i = 1; i <= n ; i++) {
		BUFFER_TRACE(branch[i].bh, "call journal_forget");
		ext3_journal_forget(handle, branch[i].bh);
	}
	for (i = 0; i < indirect_blks; i++)
		ext3_free_blocks(handle, inode, new_blocks[i], 1);

	ext3_free_blocks(handle, inode, new_blocks[i], num);

	return err;
}

/**
 * ext3_splice_branch - splice the allocated branch onto inode.
 * @handle: handle for this transaction
 * @inode: owner
 * @block: (logical) number of block we are adding
 * @where: location of missing link
 * @num:   number of indirect blocks we are adding
 * @blks:  number of direct blocks we are adding
 *
 * This function fills the missing link and does all housekeeping needed in
 * inode (->i_blocks, etc.). In case of success we end up with the full
 * chain to new block and return 0.
 */
static int ext3_splice_branch(handle_t *handle, struct inode *inode,
			long block, Indirect *where, int num, int blks)
{
	int i;
	int err = 0;
	struct ext3_block_alloc_info *block_i;
	ext3_fsblk_t current_block;
	struct ext3_inode_info *ei = EXT3_I(inode);
	struct timespec now;

	block_i = ei->i_block_alloc_info;
	/*
	 * If we're splicing into a [td]indirect block (as opposed to the
	 * inode) then we need to get write access to the [td]indirect block
	 * before the splice.
	 */
	if (where->bh) {
		BUFFER_TRACE(where->bh, "get_write_access");
		err = ext3_journal_get_write_access(handle, where->bh);
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

	/* We are done with atomic stuff, now do the rest of housekeeping */
	now = CURRENT_TIME_SEC;
	if (!timespec_equal(&inode->i_ctime, &now) || !where->bh) {
		inode->i_ctime = now;
		ext3_mark_inode_dirty(handle, inode);
	}
	/* ext3_mark_inode_dirty already updated i_sync_tid */
	atomic_set(&ei->i_datasync_tid, handle->h_transaction->t_tid);

	/* had we spliced it onto indirect block? */
	if (where->bh) {
		/*
		 * If we spliced it onto an indirect block, we haven't
		 * altered the inode.  Note however that if it is being spliced
		 * onto an indirect block at the very end of the file (the
		 * file is growing) then we *will* alter the inode to reflect
		 * the new i_size.  But that is not done here - it is done in
		 * generic_commit_write->__mark_inode_dirty->ext3_dirty_inode.
		 */
		jbd_debug(5, "splicing indirect only\n");
		BUFFER_TRACE(where->bh, "call ext3_journal_dirty_metadata");
		err = ext3_journal_dirty_metadata(handle, where->bh);
		if (err)
			goto err_out;
	} else {
		/*
		 * OK, we spliced it into the inode itself on a direct block.
		 * Inode was dirtied above.
		 */
		jbd_debug(5, "splicing direct\n");
	}
	return err;

err_out:
	for (i = 1; i <= num; i++) {
		BUFFER_TRACE(where[i].bh, "call journal_forget");
		ext3_journal_forget(handle, where[i].bh);
		ext3_free_blocks(handle,inode,le32_to_cpu(where[i-1].key),1);
	}
	ext3_free_blocks(handle, inode, le32_to_cpu(where[num].key), blks);

	return err;
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
 *
 * `handle' can be NULL if create == 0.
 *
 * The BKL may not be held on entry here.  Be sure to take it early.
 * return > 0, # of blocks mapped or allocated.
 * return = 0, if plain lookup failed.
 * return < 0, error case.
 */
int ext3_get_blocks_handle(handle_t *handle, struct inode *inode,
		sector_t iblock, unsigned long maxblocks,
		struct buffer_head *bh_result,
		int create)
{
	int err = -EIO;
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
	ext3_fsblk_t goal;
	int indirect_blks;
	int blocks_to_boundary = 0;
	int depth;
	struct ext3_inode_info *ei = EXT3_I(inode);
	int count = 0;
	ext3_fsblk_t first_block = 0;


	trace_ext3_get_blocks_enter(inode, iblock, maxblocks, create);
	J_ASSERT(handle != NULL || create == 0);
	depth = ext3_block_to_path(inode,iblock,offsets,&blocks_to_boundary);

	if (depth == 0)
		goto out;

	partial = ext3_get_branch(inode, depth, offsets, chain, &err);

	/* Simplest case - block found, no allocation needed */
	if (!partial) {
		first_block = le32_to_cpu(chain[depth - 1].key);
		clear_buffer_new(bh_result);
		count++;
		/*map more blocks*/
		while (count < maxblocks && count <= blocks_to_boundary) {
			ext3_fsblk_t blk;

			if (!verify_chain(chain, chain + depth - 1)) {
				/*
				 * Indirect block might be removed by
				 * truncate while we were reading it.
				 * Handling of that case: forget what we've
				 * got now. Flag the err as EAGAIN, so it
				 * will reread.
				 */
				err = -EAGAIN;
				count = 0;
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

	/*
	 * Block out ext3_truncate while we alter the tree
	 */
	mutex_lock(&ei->truncate_mutex);

	/*
	 * If the indirect block is missing while we are reading
	 * the chain(ext3_get_branch() returns -EAGAIN err), or
	 * if the chain has been changed after we grab the semaphore,
	 * (either because another process truncated this branch, or
	 * another get_block allocated this branch) re-grab the chain to see if
	 * the request block has been allocated or not.
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
		partial = ext3_get_branch(inode, depth, offsets, chain, &err);
		if (!partial) {
			count++;
			mutex_unlock(&ei->truncate_mutex);
			if (err)
				goto cleanup;
			clear_buffer_new(bh_result);
			goto got_it;
		}
	}

	/*
	 * Okay, we need to do block allocation.  Lazily initialize the block
	 * allocation info here if necessary
	*/
	if (S_ISREG(inode->i_mode) && (!ei->i_block_alloc_info))
		ext3_init_block_alloc_info(inode);

	goal = ext3_find_goal(inode, iblock, partial);

	/* the number of blocks need to allocate for [d,t]indirect blocks */
	indirect_blks = (chain + depth) - partial - 1;

	/*
	 * Next look up the indirect map to count the totoal number of
	 * direct blocks to allocate for this branch.
	 */
	count = ext3_blks_to_allocate(partial, indirect_blks,
					maxblocks, blocks_to_boundary);
	err = ext3_alloc_branch(handle, inode, indirect_blks, &count, goal,
				offsets + (partial - chain), partial);

	/*
	 * The ext3_splice_branch call will free and forget any buffers
	 * on the new chain if there is a failure, but that risks using
	 * up transaction credits, especially for bitmaps where the
	 * credits cannot be returned.  Can we handle this somehow?  We
	 * may need to return -EAGAIN upwards in the worst case.  --sct
	 */
	if (!err)
		err = ext3_splice_branch(handle, inode, iblock,
					partial, indirect_blks, count);
	mutex_unlock(&ei->truncate_mutex);
	if (err)
		goto cleanup;

	set_buffer_new(bh_result);
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
	trace_ext3_get_blocks_exit(inode, iblock,
				   depth ? le32_to_cpu(chain[depth-1].key) : 0,
				   count, err);
	return err;
}

/* Maximum number of blocks we map for direct IO at once. */
#define DIO_MAX_BLOCKS 4096
/*
 * Number of credits we need for writing DIO_MAX_BLOCKS:
 * We need sb + group descriptor + bitmap + inode -> 4
 * For B blocks with A block pointers per block we need:
 * 1 (triple ind.) + (B/A/A + 2) (doubly ind.) + (B/A + 2) (indirect).
 * If we plug in 4096 for B and 256 for A (for 1KB block size), we get 25.
 */
#define DIO_CREDITS 25

static int ext3_get_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	handle_t *handle = ext3_journal_current_handle();
	int ret = 0, started = 0;
	unsigned max_blocks = bh_result->b_size >> inode->i_blkbits;

	if (create && !handle) {	/* Direct IO write... */
		if (max_blocks > DIO_MAX_BLOCKS)
			max_blocks = DIO_MAX_BLOCKS;
		handle = ext3_journal_start(inode, DIO_CREDITS +
				EXT3_MAXQUOTAS_TRANS_BLOCKS(inode->i_sb));
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			goto out;
		}
		started = 1;
	}

	ret = ext3_get_blocks_handle(handle, inode, iblock,
					max_blocks, bh_result, create);
	if (ret > 0) {
		bh_result->b_size = (ret << inode->i_blkbits);
		ret = 0;
	}
	if (started)
		ext3_journal_stop(handle);
out:
	return ret;
}

int ext3_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	return generic_block_fiemap(inode, fieinfo, start, len,
				    ext3_get_block);
}

/*
 * `handle' can be NULL if create is zero
 */
struct buffer_head *ext3_getblk(handle_t *handle, struct inode *inode,
				long block, int create, int *errp)
{
	struct buffer_head dummy;
	int fatal = 0, err;

	J_ASSERT(handle != NULL || create == 0);

	dummy.b_state = 0;
	dummy.b_blocknr = -1000;
	buffer_trace_init(&dummy.b_history);
	err = ext3_get_blocks_handle(handle, inode, block, 1,
					&dummy, create);
	/*
	 * ext3_get_blocks_handle() returns number of blocks
	 * mapped. 0 in case of a HOLE.
	 */
	if (err > 0) {
		WARN_ON(err > 1);
		err = 0;
	}
	*errp = err;
	if (!err && buffer_mapped(&dummy)) {
		struct buffer_head *bh;
		bh = sb_getblk(inode->i_sb, dummy.b_blocknr);
		if (unlikely(!bh)) {
			*errp = -ENOMEM;
			goto err;
		}
		if (buffer_new(&dummy)) {
			J_ASSERT(create != 0);
			J_ASSERT(handle != NULL);

			/*
			 * Now that we do not always journal data, we should
			 * keep in mind whether this should always journal the
			 * new buffer as metadata.  For now, regular file
			 * writes use ext3_get_block instead, so it's not a
			 * problem.
			 */
			lock_buffer(bh);
			BUFFER_TRACE(bh, "call get_create_access");
			fatal = ext3_journal_get_create_access(handle, bh);
			if (!fatal && !buffer_uptodate(bh)) {
				memset(bh->b_data,0,inode->i_sb->s_blocksize);
				set_buffer_uptodate(bh);
			}
			unlock_buffer(bh);
			BUFFER_TRACE(bh, "call ext3_journal_dirty_metadata");
			err = ext3_journal_dirty_metadata(handle, bh);
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

struct buffer_head *ext3_bread(handle_t *handle, struct inode *inode,
			       int block, int create, int *err)
{
	struct buffer_head * bh;

	bh = ext3_getblk(handle, inode, block, create, err);
	if (!bh)
		return bh;
	if (bh_uptodate_or_lock(bh))
		return bh;
	get_bh(bh);
	bh->b_end_io = end_buffer_read_sync;
	submit_bh(READ | REQ_META | REQ_PRIO, bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	put_bh(bh);
	*err = -EIO;
	return NULL;
}

static int walk_page_buffers(	handle_t *handle,
				struct buffer_head *head,
				unsigned from,
				unsigned to,
				int *partial,
				int (*fn)(	handle_t *handle,
						struct buffer_head *bh))
{
	struct buffer_head *bh;
	unsigned block_start, block_end;
	unsigned blocksize = head->b_size;
	int err, ret = 0;
	struct buffer_head *next;

	for (	bh = head, block_start = 0;
		ret == 0 && (bh != head || !block_start);
		block_start = block_end, bh = next)
	{
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
 * close off a transaction and start a new one between the ext3_get_block()
 * and the commit_write().  So doing the journal_start at the start of
 * prepare_write() is the right place.
 *
 * Also, this function can nest inside ext3_writepage() ->
 * block_write_full_page(). In that case, we *know* that ext3_writepage()
 * has generated enough buffer credits to do the whole page.  So we won't
 * block on the journal in that case, which is good, because the caller may
 * be PF_MEMALLOC.
 *
 * By accident, ext3 can be reentered when a transaction is open via
 * quota file writes.  If we were to commit the transaction while thus
 * reentered, there can be a deadlock - we would be holding a quota
 * lock, and the commit would never complete if another thread had a
 * transaction open and was blocking on the quota lock - a ranking
 * violation.
 *
 * So what we do is to rely on the fact that journal_stop/journal_start
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
	 * __block_prepare_write() could have dirtied some buffers. Clean
	 * the dirty bit as jbd2_journal_get_write_access() could complain
	 * otherwise about fs integrity issues. Setting of the dirty bit
	 * by __block_prepare_write() isn't a real problem here as we clear
	 * the bit before releasing a page lock and thus writeback cannot
	 * ever write the buffer.
	 */
	if (dirty)
		clear_buffer_dirty(bh);
	ret = ext3_journal_get_write_access(handle, bh);
	if (!ret && dirty)
		ret = ext3_journal_dirty_metadata(handle, bh);
	return ret;
}

/*
 * Truncate blocks that were not used by write. We have to truncate the
 * pagecache as well so that corresponding buffers get properly unmapped.
 */
static void ext3_truncate_failed_write(struct inode *inode)
{
	truncate_inode_pages(inode->i_mapping, inode->i_size);
	ext3_truncate(inode);
}

/*
 * Truncate blocks that were not used by direct IO write. We have to zero out
 * the last file block as well because direct IO might have written to it.
 */
static void ext3_truncate_failed_direct_write(struct inode *inode)
{
	ext3_block_truncate_page(inode, inode->i_size);
	ext3_truncate(inode);
}

static int ext3_write_begin(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	int ret;
	handle_t *handle;
	int retries = 0;
	struct page *page;
	pgoff_t index;
	unsigned from, to;
	/* Reserve one block more for addition to orphan list in case
	 * we allocate blocks but write fails for some reason */
	int needed_blocks = ext3_writepage_trans_blocks(inode) + 1;

	trace_ext3_write_begin(inode, pos, len, flags);

	index = pos >> PAGE_CACHE_SHIFT;
	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + len;

retry:
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	*pagep = page;

	handle = ext3_journal_start(inode, needed_blocks);
	if (IS_ERR(handle)) {
		unlock_page(page);
		page_cache_release(page);
		ret = PTR_ERR(handle);
		goto out;
	}
	ret = __block_write_begin(page, pos, len, ext3_get_block);
	if (ret)
		goto write_begin_failed;

	if (ext3_should_journal_data(inode)) {
		ret = walk_page_buffers(handle, page_buffers(page),
				from, to, NULL, do_journal_get_write_access);
	}
write_begin_failed:
	if (ret) {
		/*
		 * block_write_begin may have instantiated a few blocks
		 * outside i_size.  Trim these off again. Don't need
		 * i_size_read because we hold i_mutex.
		 *
		 * Add inode to orphan list in case we crash before truncate
		 * finishes. Do this only if ext3_can_truncate() agrees so
		 * that orphan processing code is happy.
		 */
		if (pos + len > inode->i_size && ext3_can_truncate(inode))
			ext3_orphan_add(handle, inode);
		ext3_journal_stop(handle);
		unlock_page(page);
		page_cache_release(page);
		if (pos + len > inode->i_size)
			ext3_truncate_failed_write(inode);
	}
	if (ret == -ENOSPC && ext3_should_retry_alloc(inode->i_sb, &retries))
		goto retry;
out:
	return ret;
}


int ext3_journal_dirty_data(handle_t *handle, struct buffer_head *bh)
{
	int err = journal_dirty_data(handle, bh);
	if (err)
		ext3_journal_abort_handle(__func__, __func__,
						bh, handle, err);
	return err;
}

/* For ordered writepage and write_end functions */
static int journal_dirty_data_fn(handle_t *handle, struct buffer_head *bh)
{
	/*
	 * Write could have mapped the buffer but it didn't copy the data in
	 * yet. So avoid filing such buffer into a transaction.
	 */
	if (buffer_mapped(bh) && buffer_uptodate(bh))
		return ext3_journal_dirty_data(handle, bh);
	return 0;
}

/* For write_end() in data=journal mode */
static int write_end_fn(handle_t *handle, struct buffer_head *bh)
{
	if (!buffer_mapped(bh) || buffer_freed(bh))
		return 0;
	set_buffer_uptodate(bh);
	return ext3_journal_dirty_metadata(handle, bh);
}

/*
 * This is nasty and subtle: ext3_write_begin() could have allocated blocks
 * for the whole page but later we failed to copy the data in. Update inode
 * size according to what we managed to copy. The rest is going to be
 * truncated in write_end function.
 */
static void update_file_sizes(struct inode *inode, loff_t pos, unsigned copied)
{
	/* What matters to us is i_disksize. We don't write i_size anywhere */
	if (pos + copied > inode->i_size)
		i_size_write(inode, pos + copied);
	if (pos + copied > EXT3_I(inode)->i_disksize) {
		EXT3_I(inode)->i_disksize = pos + copied;
		mark_inode_dirty(inode);
	}
}

/*
 * We need to pick up the new inode size which generic_commit_write gave us
 * `file' can be NULL - eg, when called from page_symlink().
 *
 * ext3 never places buffers on inode->i_mapping->private_list.  metadata
 * buffers are managed internally.
 */
static int ext3_ordered_write_end(struct file *file,
				struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata)
{
	handle_t *handle = ext3_journal_current_handle();
	struct inode *inode = file->f_mapping->host;
	unsigned from, to;
	int ret = 0, ret2;

	trace_ext3_ordered_write_end(inode, pos, len, copied);
	copied = block_write_end(file, mapping, pos, len, copied, page, fsdata);

	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + copied;
	ret = walk_page_buffers(handle, page_buffers(page),
		from, to, NULL, journal_dirty_data_fn);

	if (ret == 0)
		update_file_sizes(inode, pos, copied);
	/*
	 * There may be allocated blocks outside of i_size because
	 * we failed to copy some data. Prepare for truncate.
	 */
	if (pos + len > inode->i_size && ext3_can_truncate(inode))
		ext3_orphan_add(handle, inode);
	ret2 = ext3_journal_stop(handle);
	if (!ret)
		ret = ret2;
	unlock_page(page);
	page_cache_release(page);

	if (pos + len > inode->i_size)
		ext3_truncate_failed_write(inode);
	return ret ? ret : copied;
}

static int ext3_writeback_write_end(struct file *file,
				struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata)
{
	handle_t *handle = ext3_journal_current_handle();
	struct inode *inode = file->f_mapping->host;
	int ret;

	trace_ext3_writeback_write_end(inode, pos, len, copied);
	copied = block_write_end(file, mapping, pos, len, copied, page, fsdata);
	update_file_sizes(inode, pos, copied);
	/*
	 * There may be allocated blocks outside of i_size because
	 * we failed to copy some data. Prepare for truncate.
	 */
	if (pos + len > inode->i_size && ext3_can_truncate(inode))
		ext3_orphan_add(handle, inode);
	ret = ext3_journal_stop(handle);
	unlock_page(page);
	page_cache_release(page);

	if (pos + len > inode->i_size)
		ext3_truncate_failed_write(inode);
	return ret ? ret : copied;
}

static int ext3_journalled_write_end(struct file *file,
				struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata)
{
	handle_t *handle = ext3_journal_current_handle();
	struct inode *inode = mapping->host;
	struct ext3_inode_info *ei = EXT3_I(inode);
	int ret = 0, ret2;
	int partial = 0;
	unsigned from, to;

	trace_ext3_journalled_write_end(inode, pos, len, copied);
	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + len;

	if (copied < len) {
		if (!PageUptodate(page))
			copied = 0;
		page_zero_new_buffers(page, from + copied, to);
		to = from + copied;
	}

	ret = walk_page_buffers(handle, page_buffers(page), from,
				to, &partial, write_end_fn);
	if (!partial)
		SetPageUptodate(page);

	if (pos + copied > inode->i_size)
		i_size_write(inode, pos + copied);
	/*
	 * There may be allocated blocks outside of i_size because
	 * we failed to copy some data. Prepare for truncate.
	 */
	if (pos + len > inode->i_size && ext3_can_truncate(inode))
		ext3_orphan_add(handle, inode);
	ext3_set_inode_state(inode, EXT3_STATE_JDATA);
	atomic_set(&ei->i_datasync_tid, handle->h_transaction->t_tid);
	if (inode->i_size > ei->i_disksize) {
		ei->i_disksize = inode->i_size;
		ret2 = ext3_mark_inode_dirty(handle, inode);
		if (!ret)
			ret = ret2;
	}

	ret2 = ext3_journal_stop(handle);
	if (!ret)
		ret = ret2;
	unlock_page(page);
	page_cache_release(page);

	if (pos + len > inode->i_size)
		ext3_truncate_failed_write(inode);
	return ret ? ret : copied;
}

/*
 * bmap() is special.  It gets used by applications such as lilo and by
 * the swapper to find the on-disk block of a specific piece of data.
 *
 * Naturally, this is dangerous if the block concerned is still in the
 * journal.  If somebody makes a swapfile on an ext3 data-journaling
 * filesystem and enables swap, then they may get a nasty shock when the
 * data getting swapped to that swapfile suddenly gets overwritten by
 * the original zero's written out previously to the journal and
 * awaiting writeback in the kernel's buffer cache.
 *
 * So, if we see any bmap calls here on a modified, data-journaled file,
 * take extra steps to flush any blocks which might be in the cache.
 */
static sector_t ext3_bmap(struct address_space *mapping, sector_t block)
{
	struct inode *inode = mapping->host;
	journal_t *journal;
	int err;

	if (ext3_test_inode_state(inode, EXT3_STATE_JDATA)) {
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
		 * NB. EXT3_STATE_JDATA is not set on files other than
		 * regular files.  If somebody wants to bmap a directory
		 * or symlink and gets confused because the buffer
		 * hasn't yet been flushed to disk, they deserve
		 * everything they get.
		 */

		ext3_clear_inode_state(inode, EXT3_STATE_JDATA);
		journal = EXT3_JOURNAL(inode);
		journal_lock_updates(journal);
		err = journal_flush(journal);
		journal_unlock_updates(journal);

		if (err)
			return 0;
	}

	return generic_block_bmap(mapping,block,ext3_get_block);
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

static int buffer_unmapped(handle_t *handle, struct buffer_head *bh)
{
	return !buffer_mapped(bh);
}

/*
 * Note that whenever we need to map blocks we start a transaction even if
 * we're not journalling data.  This is to preserve ordering: any hole
 * instantiation within __block_write_full_page -> ext3_get_block() should be
 * journalled along with the data so we don't crash and then get metadata which
 * refers to old data.
 *
 * In all journalling modes block_write_full_page() will start the I/O.
 *
 * We don't honour synchronous mounts for writepage().  That would be
 * disastrous.  Any write() or metadata operation will sync the fs for
 * us.
 */
static int ext3_ordered_writepage(struct page *page,
				struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct buffer_head *page_bufs;
	handle_t *handle = NULL;
	int ret = 0;
	int err;

	J_ASSERT(PageLocked(page));
	/*
	 * We don't want to warn for emergency remount. The condition is
	 * ordered to avoid dereferencing inode->i_sb in non-error case to
	 * avoid slow-downs.
	 */
	WARN_ON_ONCE(IS_RDONLY(inode) &&
		     !(EXT3_SB(inode->i_sb)->s_mount_state & EXT3_ERROR_FS));

	/*
	 * We give up here if we're reentered, because it might be for a
	 * different filesystem.
	 */
	if (ext3_journal_current_handle())
		goto out_fail;

	trace_ext3_ordered_writepage(page);
	if (!page_has_buffers(page)) {
		create_empty_buffers(page, inode->i_sb->s_blocksize,
				(1 << BH_Dirty)|(1 << BH_Uptodate));
		page_bufs = page_buffers(page);
	} else {
		page_bufs = page_buffers(page);
		if (!walk_page_buffers(NULL, page_bufs, 0, PAGE_CACHE_SIZE,
				       NULL, buffer_unmapped)) {
			/* Provide NULL get_block() to catch bugs if buffers
			 * weren't really mapped */
			return block_write_full_page(page, NULL, wbc);
		}
	}
	handle = ext3_journal_start(inode, ext3_writepage_trans_blocks(inode));

	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out_fail;
	}

	walk_page_buffers(handle, page_bufs, 0,
			PAGE_CACHE_SIZE, NULL, bget_one);

	ret = block_write_full_page(page, ext3_get_block, wbc);

	/*
	 * The page can become unlocked at any point now, and
	 * truncate can then come in and change things.  So we
	 * can't touch *page from now on.  But *page_bufs is
	 * safe due to elevated refcount.
	 */

	/*
	 * And attach them to the current transaction.  But only if
	 * block_write_full_page() succeeded.  Otherwise they are unmapped,
	 * and generally junk.
	 */
	if (ret == 0)
		ret = walk_page_buffers(handle, page_bufs, 0, PAGE_CACHE_SIZE,
					NULL, journal_dirty_data_fn);
	walk_page_buffers(handle, page_bufs, 0,
			PAGE_CACHE_SIZE, NULL, bput_one);
	err = ext3_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;

out_fail:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return ret;
}

static int ext3_writeback_writepage(struct page *page,
				struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	handle_t *handle = NULL;
	int ret = 0;
	int err;

	J_ASSERT(PageLocked(page));
	/*
	 * We don't want to warn for emergency remount. The condition is
	 * ordered to avoid dereferencing inode->i_sb in non-error case to
	 * avoid slow-downs.
	 */
	WARN_ON_ONCE(IS_RDONLY(inode) &&
		     !(EXT3_SB(inode->i_sb)->s_mount_state & EXT3_ERROR_FS));

	if (ext3_journal_current_handle())
		goto out_fail;

	trace_ext3_writeback_writepage(page);
	if (page_has_buffers(page)) {
		if (!walk_page_buffers(NULL, page_buffers(page), 0,
				      PAGE_CACHE_SIZE, NULL, buffer_unmapped)) {
			/* Provide NULL get_block() to catch bugs if buffers
			 * weren't really mapped */
			return block_write_full_page(page, NULL, wbc);
		}
	}

	handle = ext3_journal_start(inode, ext3_writepage_trans_blocks(inode));
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out_fail;
	}

	ret = block_write_full_page(page, ext3_get_block, wbc);

	err = ext3_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;

out_fail:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return ret;
}

static int ext3_journalled_writepage(struct page *page,
				struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	handle_t *handle = NULL;
	int ret = 0;
	int err;

	J_ASSERT(PageLocked(page));
	/*
	 * We don't want to warn for emergency remount. The condition is
	 * ordered to avoid dereferencing inode->i_sb in non-error case to
	 * avoid slow-downs.
	 */
	WARN_ON_ONCE(IS_RDONLY(inode) &&
		     !(EXT3_SB(inode->i_sb)->s_mount_state & EXT3_ERROR_FS));

	trace_ext3_journalled_writepage(page);
	if (!page_has_buffers(page) || PageChecked(page)) {
		if (ext3_journal_current_handle())
			goto no_write;

		handle = ext3_journal_start(inode,
					    ext3_writepage_trans_blocks(inode));
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			goto no_write;
		}
		/*
		 * It's mmapped pagecache.  Add buffers and journal it.  There
		 * doesn't seem much point in redirtying the page here.
		 */
		ClearPageChecked(page);
		ret = __block_write_begin(page, 0, PAGE_CACHE_SIZE,
					  ext3_get_block);
		if (ret != 0) {
			ext3_journal_stop(handle);
			goto out_unlock;
		}
		ret = walk_page_buffers(handle, page_buffers(page), 0,
			PAGE_CACHE_SIZE, NULL, do_journal_get_write_access);

		err = walk_page_buffers(handle, page_buffers(page), 0,
				PAGE_CACHE_SIZE, NULL, write_end_fn);
		if (ret == 0)
			ret = err;
		ext3_set_inode_state(inode, EXT3_STATE_JDATA);
		atomic_set(&EXT3_I(inode)->i_datasync_tid,
			   handle->h_transaction->t_tid);
		unlock_page(page);
		err = ext3_journal_stop(handle);
		if (!ret)
			ret = err;
	} else {
		/*
		 * It is a page full of checkpoint-mode buffers. Go and write
		 * them. They should have been already mapped when they went
		 * to the journal so provide NULL get_block function to catch
		 * errors.
		 */
		ret = block_write_full_page(page, NULL, wbc);
	}
out:
	return ret;

no_write:
	redirty_page_for_writepage(wbc, page);
out_unlock:
	unlock_page(page);
	goto out;
}

static int ext3_readpage(struct file *file, struct page *page)
{
	trace_ext3_readpage(page);
	return mpage_readpage(page, ext3_get_block);
}

static int
ext3_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, ext3_get_block);
}

static void ext3_invalidatepage(struct page *page, unsigned int offset,
				unsigned int length)
{
	journal_t *journal = EXT3_JOURNAL(page->mapping->host);

	trace_ext3_invalidatepage(page, offset, length);

	/*
	 * If it's a full truncate we just forget about the pending dirtying
	 */
	if (offset == 0 && length == PAGE_CACHE_SIZE)
		ClearPageChecked(page);

	journal_invalidatepage(journal, page, offset, length);
}

static int ext3_releasepage(struct page *page, gfp_t wait)
{
	journal_t *journal = EXT3_JOURNAL(page->mapping->host);

	trace_ext3_releasepage(page);
	WARN_ON(PageChecked(page));
	if (!page_has_buffers(page))
		return 0;
	return journal_try_to_free_buffers(journal, page, wait);
}

/*
 * If the O_DIRECT write will extend the file then add this inode to the
 * orphan list.  So recovery will truncate it back to the original size
 * if the machine crashes during the write.
 *
 * If the O_DIRECT write is intantiating holes inside i_size and the machine
 * crashes then stale disk data _may_ be exposed inside the file. But current
 * VFS code falls back into buffered path in that case so we are safe.
 */
static ssize_t ext3_direct_IO(int rw, struct kiocb *iocb,
			struct iov_iter *iter, loff_t offset)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct ext3_inode_info *ei = EXT3_I(inode);
	handle_t *handle;
	ssize_t ret;
	int orphan = 0;
	size_t count = iov_iter_count(iter);
	int retries = 0;

	trace_ext3_direct_IO_enter(inode, offset, count, rw);

	if (rw == WRITE) {
		loff_t final_size = offset + count;

		if (final_size > inode->i_size) {
			/* Credits for sb + inode write */
			handle = ext3_journal_start(inode, 2);
			if (IS_ERR(handle)) {
				ret = PTR_ERR(handle);
				goto out;
			}
			ret = ext3_orphan_add(handle, inode);
			if (ret) {
				ext3_journal_stop(handle);
				goto out;
			}
			orphan = 1;
			ei->i_disksize = inode->i_size;
			ext3_journal_stop(handle);
		}
	}

retry:
	ret = blockdev_direct_IO(rw, iocb, inode, iter, offset, ext3_get_block);
	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely((rw & WRITE) && ret < 0)) {
		loff_t isize = i_size_read(inode);
		loff_t end = offset + count;

		if (end > isize)
			ext3_truncate_failed_direct_write(inode);
	}
	if (ret == -ENOSPC && ext3_should_retry_alloc(inode->i_sb, &retries))
		goto retry;

	if (orphan) {
		int err;

		/* Credits for sb + inode write */
		handle = ext3_journal_start(inode, 2);
		if (IS_ERR(handle)) {
			/* This is really bad luck. We've written the data
			 * but cannot extend i_size. Truncate allocated blocks
			 * and pretend the write failed... */
			ext3_truncate_failed_direct_write(inode);
			ret = PTR_ERR(handle);
			if (inode->i_nlink)
				ext3_orphan_del(NULL, inode);
			goto out;
		}
		if (inode->i_nlink)
			ext3_orphan_del(handle, inode);
		if (ret > 0) {
			loff_t end = offset + ret;
			if (end > inode->i_size) {
				ei->i_disksize = end;
				i_size_write(inode, end);
				/*
				 * We're going to return a positive `ret'
				 * here due to non-zero-length I/O, so there's
				 * no way of reporting error returns from
				 * ext3_mark_inode_dirty() to userspace.  So
				 * ignore it.
				 */
				ext3_mark_inode_dirty(handle, inode);
			}
		}
		err = ext3_journal_stop(handle);
		if (ret == 0)
			ret = err;
	}
out:
	trace_ext3_direct_IO_exit(inode, offset, count, rw, ret);
	return ret;
}

/*
 * Pages can be marked dirty completely asynchronously from ext3's journalling
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
static int ext3_journalled_set_page_dirty(struct page *page)
{
	SetPageChecked(page);
	return __set_page_dirty_nobuffers(page);
}

static const struct address_space_operations ext3_ordered_aops = {
	.readpage		= ext3_readpage,
	.readpages		= ext3_readpages,
	.writepage		= ext3_ordered_writepage,
	.write_begin		= ext3_write_begin,
	.write_end		= ext3_ordered_write_end,
	.bmap			= ext3_bmap,
	.invalidatepage		= ext3_invalidatepage,
	.releasepage		= ext3_releasepage,
	.direct_IO		= ext3_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.is_dirty_writeback	= buffer_check_dirty_writeback,
	.error_remove_page	= generic_error_remove_page,
};

static const struct address_space_operations ext3_writeback_aops = {
	.readpage		= ext3_readpage,
	.readpages		= ext3_readpages,
	.writepage		= ext3_writeback_writepage,
	.write_begin		= ext3_write_begin,
	.write_end		= ext3_writeback_write_end,
	.bmap			= ext3_bmap,
	.invalidatepage		= ext3_invalidatepage,
	.releasepage		= ext3_releasepage,
	.direct_IO		= ext3_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

static const struct address_space_operations ext3_journalled_aops = {
	.readpage		= ext3_readpage,
	.readpages		= ext3_readpages,
	.writepage		= ext3_journalled_writepage,
	.write_begin		= ext3_write_begin,
	.write_end		= ext3_journalled_write_end,
	.set_page_dirty		= ext3_journalled_set_page_dirty,
	.bmap			= ext3_bmap,
	.invalidatepage		= ext3_invalidatepage,
	.releasepage		= ext3_releasepage,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

void ext3_set_aops(struct inode *inode)
{
	if (ext3_should_order_data(inode))
		inode->i_mapping->a_ops = &ext3_ordered_aops;
	else if (ext3_should_writeback_data(inode))
		inode->i_mapping->a_ops = &ext3_writeback_aops;
	else
		inode->i_mapping->a_ops = &ext3_journalled_aops;
}

/*
 * ext3_block_truncate_page() zeroes out a mapping from file offset `from'
 * up to the end of the block which corresponds to `from'.
 * This required during truncate. We need to physically zero the tail end
 * of that block so it doesn't yield old data if the file is later grown.
 */
static int ext3_block_truncate_page(struct inode *inode, loff_t from)
{
	ext3_fsblk_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE - 1);
	unsigned blocksize, iblock, length, pos;
	struct page *page;
	handle_t *handle = NULL;
	struct buffer_head *bh;
	int err = 0;

	/* Truncated on block boundary - nothing to do */
	blocksize = inode->i_sb->s_blocksize;
	if ((from & (blocksize - 1)) == 0)
		return 0;

	page = grab_cache_page(inode->i_mapping, index);
	if (!page)
		return -ENOMEM;
	length = blocksize - (offset & (blocksize - 1));
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
		ext3_get_block(inode, iblock, bh, 0);
		/* unmapped? It's a hole - nothing to do */
		if (!buffer_mapped(bh)) {
			BUFFER_TRACE(bh, "still unmapped");
			goto unlock;
		}
	}

	/* Ok, it's mapped. Make sure it's up-to-date */
	if (PageUptodate(page))
		set_buffer_uptodate(bh);

	if (!bh_uptodate_or_lock(bh)) {
		err = bh_submit_read(bh);
		/* Uhhuh. Read error. Complain and punt. */
		if (err)
			goto unlock;
	}

	/* data=writeback mode doesn't need transaction to zero-out data */
	if (!ext3_should_writeback_data(inode)) {
		/* We journal at most one block */
		handle = ext3_journal_start(inode, 1);
		if (IS_ERR(handle)) {
			clear_highpage(page);
			flush_dcache_page(page);
			err = PTR_ERR(handle);
			goto unlock;
		}
	}

	if (ext3_should_journal_data(inode)) {
		BUFFER_TRACE(bh, "get write access");
		err = ext3_journal_get_write_access(handle, bh);
		if (err)
			goto stop;
	}

	zero_user(page, offset, length);
	BUFFER_TRACE(bh, "zeroed end of block");

	err = 0;
	if (ext3_should_journal_data(inode)) {
		err = ext3_journal_dirty_metadata(handle, bh);
	} else {
		if (ext3_should_order_data(inode))
			err = ext3_journal_dirty_data(handle, bh);
		mark_buffer_dirty(bh);
	}
stop:
	if (handle)
		ext3_journal_stop(handle);

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
 *	ext3_find_shared - find the indirect blocks for partial truncation.
 *	@inode:	  inode in question
 *	@depth:	  depth of the affected branch
 *	@offsets: offsets of pointers in that branch (see ext3_block_to_path)
 *	@chain:	  place to store the pointers to partial indirect blocks
 *	@top:	  place to the (detached) top of branch
 *
 *	This is a helper function used by ext3_truncate().
 *
 *	When we do truncate() we may have to clean the ends of several
 *	indirect blocks but leave the blocks themselves alive. Block is
 *	partially truncated if some data below the new i_size is referred
 *	from it (and it is on the path to the first completely truncated
 *	data block, indeed).  We have to free the top of that path along
 *	with everything to the right of the path. Since no allocation
 *	past the truncation point is possible until ext3_truncate()
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

static Indirect *ext3_find_shared(struct inode *inode, int depth,
			int offsets[4], Indirect chain[4], __le32 *top)
{
	Indirect *partial, *p;
	int k, err;

	*top = 0;
	/* Make k index the deepest non-null offset + 1 */
	for (k = depth; k > 1 && !offsets[k-1]; k--)
		;
	partial = ext3_get_branch(inode, k, offsets, chain, &err);
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
		/* Nope, don't do this in ext3.  Must leave the tree intact */
#if 0
		*p->p = 0;
#endif
	}
	/* Writer: end */

	while(partial > p) {
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
static void ext3_clear_blocks(handle_t *handle, struct inode *inode,
		struct buffer_head *bh, ext3_fsblk_t block_to_free,
		unsigned long count, __le32 *first, __le32 *last)
{
	__le32 *p;
	if (try_to_extend_transaction(handle, inode)) {
		if (bh) {
			BUFFER_TRACE(bh, "call ext3_journal_dirty_metadata");
			if (ext3_journal_dirty_metadata(handle, bh))
				return;
		}
		ext3_mark_inode_dirty(handle, inode);
		truncate_restart_transaction(handle, inode);
		if (bh) {
			BUFFER_TRACE(bh, "retaking write access");
			if (ext3_journal_get_write_access(handle, bh))
				return;
		}
	}

	/*
	 * Any buffers which are on the journal will be in memory. We find
	 * them on the hash table so journal_revoke() will run journal_forget()
	 * on them.  We've already detached each block from the file, so
	 * bforget() in journal_forget() should be safe.
	 *
	 * AKPM: turn on bforget in journal_forget()!!!
	 */
	for (p = first; p < last; p++) {
		u32 nr = le32_to_cpu(*p);
		if (nr) {
			struct buffer_head *bh;

			*p = 0;
			bh = sb_find_get_block(inode->i_sb, nr);
			ext3_forget(handle, 0, inode, bh, nr);
		}
	}

	ext3_free_blocks(handle, inode, block_to_free, count);
}

/**
 * ext3_free_data - free a list of data blocks
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
static void ext3_free_data(handle_t *handle, struct inode *inode,
			   struct buffer_head *this_bh,
			   __le32 *first, __le32 *last)
{
	ext3_fsblk_t block_to_free = 0;    /* Starting block # of a run */
	unsigned long count = 0;	    /* Number of blocks in the run */
	__le32 *block_to_free_p = NULL;	    /* Pointer into inode/ind
					       corresponding to
					       block_to_free */
	ext3_fsblk_t nr;		    /* Current block # */
	__le32 *p;			    /* Pointer into inode/ind
					       for current block */
	int err;

	if (this_bh) {				/* For indirect block */
		BUFFER_TRACE(this_bh, "get_write_access");
		err = ext3_journal_get_write_access(handle, this_bh);
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
				ext3_clear_blocks(handle, inode, this_bh,
						  block_to_free,
						  count, block_to_free_p, p);
				block_to_free = nr;
				block_to_free_p = p;
				count = 1;
			}
		}
	}

	if (count > 0)
		ext3_clear_blocks(handle, inode, this_bh, block_to_free,
				  count, block_to_free_p, p);

	if (this_bh) {
		BUFFER_TRACE(this_bh, "call ext3_journal_dirty_metadata");

		/*
		 * The buffer head should have an attached journal head at this
		 * point. However, if the data is corrupted and an indirect
		 * block pointed to itself, it would have been detached when
		 * the block was cleared. Check for this instead of OOPSing.
		 */
		if (bh2jh(this_bh))
			ext3_journal_dirty_metadata(handle, this_bh);
		else
			ext3_error(inode->i_sb, "ext3_free_data",
				   "circular indirect block detected, "
				   "inode=%lu, block=%llu",
				   inode->i_ino,
				   (unsigned long long)this_bh->b_blocknr);
	}
}

/**
 *	ext3_free_branches - free an array of branches
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
static void ext3_free_branches(handle_t *handle, struct inode *inode,
			       struct buffer_head *parent_bh,
			       __le32 *first, __le32 *last, int depth)
{
	ext3_fsblk_t nr;
	__le32 *p;

	if (is_handle_aborted(handle))
		return;

	if (depth--) {
		struct buffer_head *bh;
		int addr_per_block = EXT3_ADDR_PER_BLOCK(inode->i_sb);
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
				ext3_error(inode->i_sb, "ext3_free_branches",
					   "Read failure, inode=%lu, block="E3FSBLK,
					   inode->i_ino, nr);
				continue;
			}

			/* This zaps the entire block.  Bottom up. */
			BUFFER_TRACE(bh, "free child branches");
			ext3_free_branches(handle, inode, bh,
					   (__le32*)bh->b_data,
					   (__le32*)bh->b_data + addr_per_block,
					   depth);

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
			if (is_handle_aborted(handle))
				return;
			if (try_to_extend_transaction(handle, inode)) {
				ext3_mark_inode_dirty(handle, inode);
				truncate_restart_transaction(handle, inode);
			}

			/*
			 * We've probably journalled the indirect block several
			 * times during the truncate.  But it's no longer
			 * needed and we now drop it from the transaction via
			 * journal_revoke().
			 *
			 * That's easy if it's exclusively part of this
			 * transaction.  But if it's part of the committing
			 * transaction then journal_forget() will simply
			 * brelse() it.  That means that if the underlying
			 * block is reallocated in ext3_get_block(),
			 * unmap_underlying_metadata() will find this block
			 * and will try to get rid of it.  damn, damn. Thus
			 * we don't allow a block to be reallocated until
			 * a transaction freeing it has fully committed.
			 *
			 * We also have to make sure journal replay after a
			 * crash does not overwrite non-journaled data blocks
			 * with old metadata when the block got reallocated for
			 * data.  Thus we have to store a revoke record for a
			 * block in the same transaction in which we free the
			 * block.
			 */
			ext3_forget(handle, 1, inode, bh, bh->b_blocknr);

			ext3_free_blocks(handle, inode, nr, 1);

			if (parent_bh) {
				/*
				 * The block which we have just freed is
				 * pointed to by an indirect block: journal it
				 */
				BUFFER_TRACE(parent_bh, "get_write_access");
				if (!ext3_journal_get_write_access(handle,
								   parent_bh)){
					*p = 0;
					BUFFER_TRACE(parent_bh,
					"call ext3_journal_dirty_metadata");
					ext3_journal_dirty_metadata(handle,
								    parent_bh);
				}
			}
		}
	} else {
		/* We have reached the bottom of the tree. */
		BUFFER_TRACE(parent_bh, "free data blocks");
		ext3_free_data(handle, inode, parent_bh, first, last);
	}
}

int ext3_can_truncate(struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		return 1;
	if (S_ISDIR(inode->i_mode))
		return 1;
	if (S_ISLNK(inode->i_mode))
		return !ext3_inode_is_fast_symlink(inode);
	return 0;
}

/*
 * ext3_truncate()
 *
 * We block out ext3_get_block() block instantiations across the entire
 * transaction, and VFS/VM ensures that ext3_truncate() cannot run
 * simultaneously on behalf of the same inode.
 *
 * As we work through the truncate and commit bits of it to the journal there
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
 * i_disksize in this case).  After a crash, ext3_orphan_cleanup() will see
 * that this inode's truncate did not complete and it will again call
 * ext3_truncate() to have another go.  So there will be instantiated blocks
 * to the right of the truncation point in a crashed ext3 filesystem.  But
 * that's fine - as long as they are linked from the inode, the post-crash
 * ext3_truncate() run will find them and release them.
 */
void ext3_truncate(struct inode *inode)
{
	handle_t *handle;
	struct ext3_inode_info *ei = EXT3_I(inode);
	__le32 *i_data = ei->i_data;
	int addr_per_block = EXT3_ADDR_PER_BLOCK(inode->i_sb);
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
	__le32 nr = 0;
	int n;
	long last_block;
	unsigned blocksize = inode->i_sb->s_blocksize;

	trace_ext3_truncate_enter(inode);

	if (!ext3_can_truncate(inode))
		goto out_notrans;

	if (inode->i_size == 0 && ext3_should_writeback_data(inode))
		ext3_set_inode_state(inode, EXT3_STATE_FLUSH_ON_CLOSE);

	handle = start_transaction(inode);
	if (IS_ERR(handle))
		goto out_notrans;

	last_block = (inode->i_size + blocksize-1)
					>> EXT3_BLOCK_SIZE_BITS(inode->i_sb);
	n = ext3_block_to_path(inode, last_block, offsets, NULL);
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
	if (ext3_orphan_add(handle, inode))
		goto out_stop;

	/*
	 * The orphan list entry will now protect us from any crash which
	 * occurs before the truncate completes, so it is now safe to propagate
	 * the new, shorter inode size (held for now in i_size) into the
	 * on-disk inode. We do this via i_disksize, which is the value which
	 * ext3 *really* writes onto the disk inode.
	 */
	ei->i_disksize = inode->i_size;

	/*
	 * From here we block out all ext3_get_block() callers who want to
	 * modify the block allocation tree.
	 */
	mutex_lock(&ei->truncate_mutex);

	if (n == 1) {		/* direct blocks */
		ext3_free_data(handle, inode, NULL, i_data+offsets[0],
			       i_data + EXT3_NDIR_BLOCKS);
		goto do_indirects;
	}

	partial = ext3_find_shared(inode, n, offsets, chain, &nr);
	/* Kill the top of shared branch (not detached) */
	if (nr) {
		if (partial == chain) {
			/* Shared branch grows from the inode */
			ext3_free_branches(handle, inode, NULL,
					   &nr, &nr+1, (chain+n-1) - partial);
			*partial->p = 0;
			/*
			 * We mark the inode dirty prior to restart,
			 * and prior to stop.  No need for it here.
			 */
		} else {
			/* Shared branch grows from an indirect block */
			ext3_free_branches(handle, inode, partial->bh,
					partial->p,
					partial->p+1, (chain+n-1) - partial);
		}
	}
	/* Clear the ends of indirect blocks on the shared branch */
	while (partial > chain) {
		ext3_free_branches(handle, inode, partial->bh, partial->p + 1,
				   (__le32*)partial->bh->b_data+addr_per_block,
				   (chain+n-1) - partial);
		BUFFER_TRACE(partial->bh, "call brelse");
		brelse (partial->bh);
		partial--;
	}
do_indirects:
	/* Kill the remaining (whole) subtrees */
	switch (offsets[0]) {
	default:
		nr = i_data[EXT3_IND_BLOCK];
		if (nr) {
			ext3_free_branches(handle, inode, NULL, &nr, &nr+1, 1);
			i_data[EXT3_IND_BLOCK] = 0;
		}
	case EXT3_IND_BLOCK:
		nr = i_data[EXT3_DIND_BLOCK];
		if (nr) {
			ext3_free_branches(handle, inode, NULL, &nr, &nr+1, 2);
			i_data[EXT3_DIND_BLOCK] = 0;
		}
	case EXT3_DIND_BLOCK:
		nr = i_data[EXT3_TIND_BLOCK];
		if (nr) {
			ext3_free_branches(handle, inode, NULL, &nr, &nr+1, 3);
			i_data[EXT3_TIND_BLOCK] = 0;
		}
	case EXT3_TIND_BLOCK:
		;
	}

	ext3_discard_reservation(inode);

	mutex_unlock(&ei->truncate_mutex);
	inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
	ext3_mark_inode_dirty(handle, inode);

	/*
	 * In a multi-transaction truncate, we only make the final transaction
	 * synchronous
	 */
	if (IS_SYNC(inode))
		handle->h_sync = 1;
out_stop:
	/*
	 * If this was a simple ftruncate(), and the file will remain alive
	 * then we need to clear up the orphan record which we created above.
	 * However, if this was a real unlink then we were called by
	 * ext3_evict_inode(), and we allow that function to clean up the
	 * orphan info for us.
	 */
	if (inode->i_nlink)
		ext3_orphan_del(handle, inode);

	ext3_journal_stop(handle);
	trace_ext3_truncate_exit(inode);
	return;
out_notrans:
	/*
	 * Delete the inode from orphan list so that it doesn't stay there
	 * forever and trigger assertion on umount.
	 */
	if (inode->i_nlink)
		ext3_orphan_del(NULL, inode);
	trace_ext3_truncate_exit(inode);
}

static ext3_fsblk_t ext3_get_inode_block(struct super_block *sb,
		unsigned long ino, struct ext3_iloc *iloc)
{
	unsigned long block_group;
	unsigned long offset;
	ext3_fsblk_t block;
	struct ext3_group_desc *gdp;

	if (!ext3_valid_inum(sb, ino)) {
		/*
		 * This error is already checked for in namei.c unless we are
		 * looking at an NFS filehandle, in which case no error
		 * report is needed
		 */
		return 0;
	}

	block_group = (ino - 1) / EXT3_INODES_PER_GROUP(sb);
	gdp = ext3_get_group_desc(sb, block_group, NULL);
	if (!gdp)
		return 0;
	/*
	 * Figure out the offset within the block group inode table
	 */
	offset = ((ino - 1) % EXT3_INODES_PER_GROUP(sb)) *
		EXT3_INODE_SIZE(sb);
	block = le32_to_cpu(gdp->bg_inode_table) +
		(offset >> EXT3_BLOCK_SIZE_BITS(sb));

	iloc->block_group = block_group;
	iloc->offset = offset & (EXT3_BLOCK_SIZE(sb) - 1);
	return block;
}

/*
 * ext3_get_inode_loc returns with an extra refcount against the inode's
 * underlying buffer_head on success. If 'in_mem' is true, we have all
 * data in memory that is needed to recreate the on-disk version of this
 * inode.
 */
static int __ext3_get_inode_loc(struct inode *inode,
				struct ext3_iloc *iloc, int in_mem)
{
	ext3_fsblk_t block;
	struct buffer_head *bh;

	block = ext3_get_inode_block(inode->i_sb, inode->i_ino, iloc);
	if (!block)
		return -EIO;

	bh = sb_getblk(inode->i_sb, block);
	if (unlikely(!bh)) {
		ext3_error (inode->i_sb, "ext3_get_inode_loc",
				"unable to read inode block - "
				"inode=%lu, block="E3FSBLK,
				 inode->i_ino, block);
		return -ENOMEM;
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
			struct ext3_group_desc *desc;
			int inodes_per_buffer;
			int inode_offset, i;
			int block_group;
			int start;

			block_group = (inode->i_ino - 1) /
					EXT3_INODES_PER_GROUP(inode->i_sb);
			inodes_per_buffer = bh->b_size /
				EXT3_INODE_SIZE(inode->i_sb);
			inode_offset = ((inode->i_ino - 1) %
					EXT3_INODES_PER_GROUP(inode->i_sb));
			start = inode_offset & ~(inodes_per_buffer - 1);

			/* Is the inode bitmap in cache? */
			desc = ext3_get_group_desc(inode->i_sb,
						block_group, NULL);
			if (!desc)
				goto make_io;

			bitmap_bh = sb_getblk(inode->i_sb,
					le32_to_cpu(desc->bg_inode_bitmap));
			if (unlikely(!bitmap_bh))
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
			for (i = start; i < start + inodes_per_buffer; i++) {
				if (i == inode_offset)
					continue;
				if (ext3_test_bit(i, bitmap_bh->b_data))
					break;
			}
			brelse(bitmap_bh);
			if (i == start + inodes_per_buffer) {
				/* all other inodes are free, so skip I/O */
				memset(bh->b_data, 0, bh->b_size);
				set_buffer_uptodate(bh);
				unlock_buffer(bh);
				goto has_buffer;
			}
		}

make_io:
		/*
		 * There are other valid inodes in the buffer, this inode
		 * has in-inode xattrs, or we don't have this inode in memory.
		 * Read the block from disk.
		 */
		trace_ext3_load_inode(inode);
		get_bh(bh);
		bh->b_end_io = end_buffer_read_sync;
		submit_bh(READ | REQ_META | REQ_PRIO, bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			ext3_error(inode->i_sb, "ext3_get_inode_loc",
					"unable to read inode block - "
					"inode=%lu, block="E3FSBLK,
					inode->i_ino, block);
			brelse(bh);
			return -EIO;
		}
	}
has_buffer:
	iloc->bh = bh;
	return 0;
}

int ext3_get_inode_loc(struct inode *inode, struct ext3_iloc *iloc)
{
	/* We have all inode data except xattrs in memory here. */
	return __ext3_get_inode_loc(inode, iloc,
		!ext3_test_inode_state(inode, EXT3_STATE_XATTR));
}

void ext3_set_inode_flags(struct inode *inode)
{
	unsigned int flags = EXT3_I(inode)->i_flags;

	inode->i_flags &= ~(S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);
	if (flags & EXT3_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & EXT3_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & EXT3_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & EXT3_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & EXT3_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
}

/* Propagate flags from i_flags to EXT3_I(inode)->i_flags */
void ext3_get_inode_flags(struct ext3_inode_info *ei)
{
	unsigned int flags = ei->vfs_inode.i_flags;

	ei->i_flags &= ~(EXT3_SYNC_FL|EXT3_APPEND_FL|
			EXT3_IMMUTABLE_FL|EXT3_NOATIME_FL|EXT3_DIRSYNC_FL);
	if (flags & S_SYNC)
		ei->i_flags |= EXT3_SYNC_FL;
	if (flags & S_APPEND)
		ei->i_flags |= EXT3_APPEND_FL;
	if (flags & S_IMMUTABLE)
		ei->i_flags |= EXT3_IMMUTABLE_FL;
	if (flags & S_NOATIME)
		ei->i_flags |= EXT3_NOATIME_FL;
	if (flags & S_DIRSYNC)
		ei->i_flags |= EXT3_DIRSYNC_FL;
}

struct inode *ext3_iget(struct super_block *sb, unsigned long ino)
{
	struct ext3_iloc iloc;
	struct ext3_inode *raw_inode;
	struct ext3_inode_info *ei;
	struct buffer_head *bh;
	struct inode *inode;
	journal_t *journal = EXT3_SB(sb)->s_journal;
	transaction_t *transaction;
	long ret;
	int block;
	uid_t i_uid;
	gid_t i_gid;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	ei = EXT3_I(inode);
	ei->i_block_alloc_info = NULL;

	ret = __ext3_get_inode_loc(inode, &iloc, 0);
	if (ret < 0)
		goto bad_inode;
	bh = iloc.bh;
	raw_inode = ext3_raw_inode(&iloc);
	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	i_uid = (uid_t)le16_to_cpu(raw_inode->i_uid_low);
	i_gid = (gid_t)le16_to_cpu(raw_inode->i_gid_low);
	if(!(test_opt (inode->i_sb, NO_UID32))) {
		i_uid |= le16_to_cpu(raw_inode->i_uid_high) << 16;
		i_gid |= le16_to_cpu(raw_inode->i_gid_high) << 16;
	}
	i_uid_write(inode, i_uid);
	i_gid_write(inode, i_gid);
	set_nlink(inode, le16_to_cpu(raw_inode->i_links_count));
	inode->i_size = le32_to_cpu(raw_inode->i_size);
	inode->i_atime.tv_sec = (signed)le32_to_cpu(raw_inode->i_atime);
	inode->i_ctime.tv_sec = (signed)le32_to_cpu(raw_inode->i_ctime);
	inode->i_mtime.tv_sec = (signed)le32_to_cpu(raw_inode->i_mtime);
	inode->i_atime.tv_nsec = inode->i_ctime.tv_nsec = inode->i_mtime.tv_nsec = 0;

	ei->i_state_flags = 0;
	ei->i_dir_start_lookup = 0;
	ei->i_dtime = le32_to_cpu(raw_inode->i_dtime);
	/* We now have enough fields to check if the inode was active or not.
	 * This is needed because nfsd might try to access dead inodes
	 * the test is that same one that e2fsck uses
	 * NeilBrown 1999oct15
	 */
	if (inode->i_nlink == 0) {
		if (inode->i_mode == 0 ||
		    !(EXT3_SB(inode->i_sb)->s_mount_state & EXT3_ORPHAN_FS)) {
			/* this inode is deleted */
			brelse (bh);
			ret = -ESTALE;
			goto bad_inode;
		}
		/* The only unlinked inodes we let through here have
		 * valid i_mode and are being read by the orphan
		 * recovery code: that's fine, we're about to complete
		 * the process of deleting those. */
	}
	inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);
	ei->i_flags = le32_to_cpu(raw_inode->i_flags);
#ifdef EXT3_FRAGMENTS
	ei->i_faddr = le32_to_cpu(raw_inode->i_faddr);
	ei->i_frag_no = raw_inode->i_frag;
	ei->i_frag_size = raw_inode->i_fsize;
#endif
	ei->i_file_acl = le32_to_cpu(raw_inode->i_file_acl);
	if (!S_ISREG(inode->i_mode)) {
		ei->i_dir_acl = le32_to_cpu(raw_inode->i_dir_acl);
	} else {
		inode->i_size |=
			((__u64)le32_to_cpu(raw_inode->i_size_high)) << 32;
	}
	ei->i_disksize = inode->i_size;
	inode->i_generation = le32_to_cpu(raw_inode->i_generation);
	ei->i_block_group = iloc.block_group;
	/*
	 * NOTE! The in-memory inode i_data array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (block = 0; block < EXT3_N_BLOCKS; block++)
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
		atomic_set(&ei->i_sync_tid, tid);
		atomic_set(&ei->i_datasync_tid, tid);
	}

	if (inode->i_ino >= EXT3_FIRST_INO(inode->i_sb) + 1 &&
	    EXT3_INODE_SIZE(inode->i_sb) > EXT3_GOOD_OLD_INODE_SIZE) {
		/*
		 * When mke2fs creates big inodes it does not zero out
		 * the unused bytes above EXT3_GOOD_OLD_INODE_SIZE,
		 * so ignore those first few inodes.
		 */
		ei->i_extra_isize = le16_to_cpu(raw_inode->i_extra_isize);
		if (EXT3_GOOD_OLD_INODE_SIZE + ei->i_extra_isize >
		    EXT3_INODE_SIZE(inode->i_sb)) {
			brelse (bh);
			ret = -EIO;
			goto bad_inode;
		}
		if (ei->i_extra_isize == 0) {
			/* The extra space is currently unused. Use it. */
			ei->i_extra_isize = sizeof(struct ext3_inode) -
					    EXT3_GOOD_OLD_INODE_SIZE;
		} else {
			__le32 *magic = (void *)raw_inode +
					EXT3_GOOD_OLD_INODE_SIZE +
					ei->i_extra_isize;
			if (*magic == cpu_to_le32(EXT3_XATTR_MAGIC))
				 ext3_set_inode_state(inode, EXT3_STATE_XATTR);
		}
	} else
		ei->i_extra_isize = 0;

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &ext3_file_inode_operations;
		inode->i_fop = &ext3_file_operations;
		ext3_set_aops(inode);
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ext3_dir_inode_operations;
		inode->i_fop = &ext3_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		if (ext3_inode_is_fast_symlink(inode)) {
			inode->i_op = &ext3_fast_symlink_inode_operations;
			nd_terminate_link(ei->i_data, inode->i_size,
				sizeof(ei->i_data) - 1);
		} else {
			inode->i_op = &ext3_symlink_inode_operations;
			ext3_set_aops(inode);
		}
	} else {
		inode->i_op = &ext3_special_inode_operations;
		if (raw_inode->i_block[0])
			init_special_inode(inode, inode->i_mode,
			   old_decode_dev(le32_to_cpu(raw_inode->i_block[0])));
		else
			init_special_inode(inode, inode->i_mode,
			   new_decode_dev(le32_to_cpu(raw_inode->i_block[1])));
	}
	brelse (iloc.bh);
	ext3_set_inode_flags(inode);
	unlock_new_inode(inode);
	return inode;

bad_inode:
	iget_failed(inode);
	return ERR_PTR(ret);
}

/*
 * Post the struct inode info into an on-disk inode location in the
 * buffer-cache.  This gobbles the caller's reference to the
 * buffer_head in the inode location struct.
 *
 * The caller must have write access to iloc->bh.
 */
static int ext3_do_update_inode(handle_t *handle,
				struct inode *inode,
				struct ext3_iloc *iloc)
{
	struct ext3_inode *raw_inode = ext3_raw_inode(iloc);
	struct ext3_inode_info *ei = EXT3_I(inode);
	struct buffer_head *bh = iloc->bh;
	int err = 0, rc, block;
	int need_datasync = 0;
	__le32 disksize;
	uid_t i_uid;
	gid_t i_gid;

again:
	/* we can't allow multiple procs in here at once, its a bit racey */
	lock_buffer(bh);

	/* For fields not not tracking in the in-memory inode,
	 * initialise them to zero for new inodes. */
	if (ext3_test_inode_state(inode, EXT3_STATE_NEW))
		memset(raw_inode, 0, EXT3_SB(inode->i_sb)->s_inode_size);

	ext3_get_inode_flags(ei);
	raw_inode->i_mode = cpu_to_le16(inode->i_mode);
	i_uid = i_uid_read(inode);
	i_gid = i_gid_read(inode);
	if(!(test_opt(inode->i_sb, NO_UID32))) {
		raw_inode->i_uid_low = cpu_to_le16(low_16_bits(i_uid));
		raw_inode->i_gid_low = cpu_to_le16(low_16_bits(i_gid));
/*
 * Fix up interoperability with old kernels. Otherwise, old inodes get
 * re-used with the upper 16 bits of the uid/gid intact
 */
		if(!ei->i_dtime) {
			raw_inode->i_uid_high =
				cpu_to_le16(high_16_bits(i_uid));
			raw_inode->i_gid_high =
				cpu_to_le16(high_16_bits(i_gid));
		} else {
			raw_inode->i_uid_high = 0;
			raw_inode->i_gid_high = 0;
		}
	} else {
		raw_inode->i_uid_low =
			cpu_to_le16(fs_high2lowuid(i_uid));
		raw_inode->i_gid_low =
			cpu_to_le16(fs_high2lowgid(i_gid));
		raw_inode->i_uid_high = 0;
		raw_inode->i_gid_high = 0;
	}
	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);
	disksize = cpu_to_le32(ei->i_disksize);
	if (disksize != raw_inode->i_size) {
		need_datasync = 1;
		raw_inode->i_size = disksize;
	}
	raw_inode->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	raw_inode->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	raw_inode->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	raw_inode->i_blocks = cpu_to_le32(inode->i_blocks);
	raw_inode->i_dtime = cpu_to_le32(ei->i_dtime);
	raw_inode->i_flags = cpu_to_le32(ei->i_flags);
#ifdef EXT3_FRAGMENTS
	raw_inode->i_faddr = cpu_to_le32(ei->i_faddr);
	raw_inode->i_frag = ei->i_frag_no;
	raw_inode->i_fsize = ei->i_frag_size;
#endif
	raw_inode->i_file_acl = cpu_to_le32(ei->i_file_acl);
	if (!S_ISREG(inode->i_mode)) {
		raw_inode->i_dir_acl = cpu_to_le32(ei->i_dir_acl);
	} else {
		disksize = cpu_to_le32(ei->i_disksize >> 32);
		if (disksize != raw_inode->i_size_high) {
			raw_inode->i_size_high = disksize;
			need_datasync = 1;
		}
		if (ei->i_disksize > 0x7fffffffULL) {
			struct super_block *sb = inode->i_sb;
			if (!EXT3_HAS_RO_COMPAT_FEATURE(sb,
					EXT3_FEATURE_RO_COMPAT_LARGE_FILE) ||
			    EXT3_SB(sb)->s_es->s_rev_level ==
					cpu_to_le32(EXT3_GOOD_OLD_REV)) {
			       /* If this is the first large file
				* created, add a flag to the superblock.
				*/
				unlock_buffer(bh);
				err = ext3_journal_get_write_access(handle,
						EXT3_SB(sb)->s_sbh);
				if (err)
					goto out_brelse;

				ext3_update_dynamic_rev(sb);
				EXT3_SET_RO_COMPAT_FEATURE(sb,
					EXT3_FEATURE_RO_COMPAT_LARGE_FILE);
				handle->h_sync = 1;
				err = ext3_journal_dirty_metadata(handle,
						EXT3_SB(sb)->s_sbh);
				/* get our lock and start over */
				goto again;
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
	} else for (block = 0; block < EXT3_N_BLOCKS; block++)
		raw_inode->i_block[block] = ei->i_data[block];

	if (ei->i_extra_isize)
		raw_inode->i_extra_isize = cpu_to_le16(ei->i_extra_isize);

	BUFFER_TRACE(bh, "call ext3_journal_dirty_metadata");
	unlock_buffer(bh);
	rc = ext3_journal_dirty_metadata(handle, bh);
	if (!err)
		err = rc;
	ext3_clear_inode_state(inode, EXT3_STATE_NEW);

	atomic_set(&ei->i_sync_tid, handle->h_transaction->t_tid);
	if (need_datasync)
		atomic_set(&ei->i_datasync_tid, handle->h_transaction->t_tid);
out_brelse:
	brelse (bh);
	ext3_std_error(inode->i_sb, err);
	return err;
}

/*
 * ext3_write_inode()
 *
 * We are called from a few places:
 *
 * - Within generic_file_aio_write() -> generic_write_sync() for O_SYNC files.
 *   Here, there will be no transaction running. We wait for any running
 *   transaction to commit.
 *
 * - Within flush work (for sys_sync(), kupdate and such).
 *   We wait on commit, if told to.
 *
 * - Within iput_final() -> write_inode_now()
 *   We wait on commit, if told to.
 *
 * In all cases it is actually safe for us to return without doing anything,
 * because the inode has been copied into a raw inode buffer in
 * ext3_mark_inode_dirty().  This is a correctness thing for WB_SYNC_ALL
 * writeback.
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
 * is in error because write_inode() could occur while `stuff()' is running,
 * and the new i_size will be lost.  Plus the inode will no longer be on the
 * superblock's dirty inode list.
 */
int ext3_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	if (WARN_ON_ONCE(current->flags & PF_MEMALLOC))
		return 0;

	if (ext3_journal_current_handle()) {
		jbd_debug(1, "called recursively, non-PF_MEMALLOC!\n");
		dump_stack();
		return -EIO;
	}

	/*
	 * No need to force transaction in WB_SYNC_NONE mode. Also
	 * ext3_sync_fs() will force the commit after everything is
	 * written.
	 */
	if (wbc->sync_mode != WB_SYNC_ALL || wbc->for_sync)
		return 0;

	return ext3_force_commit(inode->i_sb);
}

/*
 * ext3_setattr()
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
 * Called with inode->sem down.
 */
int ext3_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error, rc = 0;
	const unsigned int ia_valid = attr->ia_valid;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if (is_quota_modification(inode, attr))
		dquot_initialize(inode);
	if ((ia_valid & ATTR_UID && !uid_eq(attr->ia_uid, inode->i_uid)) ||
	    (ia_valid & ATTR_GID && !gid_eq(attr->ia_gid, inode->i_gid))) {
		handle_t *handle;

		/* (user+group)*(old+new) structure, inode write (sb,
		 * inode block, ? - but truncate inode update has it) */
		handle = ext3_journal_start(inode, EXT3_MAXQUOTAS_INIT_BLOCKS(inode->i_sb)+
					EXT3_MAXQUOTAS_DEL_BLOCKS(inode->i_sb)+3);
		if (IS_ERR(handle)) {
			error = PTR_ERR(handle);
			goto err_out;
		}
		error = dquot_transfer(inode, attr);
		if (error) {
			ext3_journal_stop(handle);
			return error;
		}
		/* Update corresponding info in inode so that everything is in
		 * one transaction */
		if (attr->ia_valid & ATTR_UID)
			inode->i_uid = attr->ia_uid;
		if (attr->ia_valid & ATTR_GID)
			inode->i_gid = attr->ia_gid;
		error = ext3_mark_inode_dirty(handle, inode);
		ext3_journal_stop(handle);
	}

	if (attr->ia_valid & ATTR_SIZE)
		inode_dio_wait(inode);

	if (S_ISREG(inode->i_mode) &&
	    attr->ia_valid & ATTR_SIZE && attr->ia_size < inode->i_size) {
		handle_t *handle;

		handle = ext3_journal_start(inode, 3);
		if (IS_ERR(handle)) {
			error = PTR_ERR(handle);
			goto err_out;
		}

		error = ext3_orphan_add(handle, inode);
		if (error) {
			ext3_journal_stop(handle);
			goto err_out;
		}
		EXT3_I(inode)->i_disksize = attr->ia_size;
		error = ext3_mark_inode_dirty(handle, inode);
		ext3_journal_stop(handle);
		if (error) {
			/* Some hard fs error must have happened. Bail out. */
			ext3_orphan_del(NULL, inode);
			goto err_out;
		}
		rc = ext3_block_truncate_page(inode, attr->ia_size);
		if (rc) {
			/* Cleanup orphan list and exit */
			handle = ext3_journal_start(inode, 3);
			if (IS_ERR(handle)) {
				ext3_orphan_del(NULL, inode);
				goto err_out;
			}
			ext3_orphan_del(handle, inode);
			ext3_journal_stop(handle);
			goto err_out;
		}
	}

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		truncate_setsize(inode, attr->ia_size);
		ext3_truncate(inode);
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);

	if (ia_valid & ATTR_MODE)
		rc = posix_acl_chmod(inode, inode->i_mode);

err_out:
	ext3_std_error(inode->i_sb, error);
	if (!error)
		error = rc;
	return error;
}


/*
 * How many blocks doth make a writepage()?
 *
 * With N blocks per page, it may be:
 * N data blocks
 * 2 indirect block
 * 2 dindirect
 * 1 tindirect
 * N+5 bitmap blocks (from the above)
 * N+5 group descriptor summary blocks
 * 1 inode block
 * 1 superblock.
 * 2 * EXT3_SINGLEDATA_TRANS_BLOCKS for the quote files
 *
 * 3 * (N + 5) + 2 + 2 * EXT3_SINGLEDATA_TRANS_BLOCKS
 *
 * With ordered or writeback data it's the same, less the N data blocks.
 *
 * If the inode's direct blocks can hold an integral number of pages then a
 * page cannot straddle two indirect blocks, and we can only touch one indirect
 * and dindirect block, and the "5" above becomes "3".
 *
 * This still overestimates under most circumstances.  If we were to pass the
 * start and end offsets in here as well we could do block_to_path() on each
 * block and work out the exact number of indirects which are touched.  Pah.
 */

static int ext3_writepage_trans_blocks(struct inode *inode)
{
	int bpp = ext3_journal_blocks_per_page(inode);
	int indirects = (EXT3_NDIR_BLOCKS % bpp) ? 5 : 3;
	int ret;

	if (ext3_should_journal_data(inode))
		ret = 3 * (bpp + indirects) + 2;
	else
		ret = 2 * (bpp + indirects) + indirects + 2;

#ifdef CONFIG_QUOTA
	/* We know that structure was already allocated during dquot_initialize so
	 * we will be updating only the data blocks + inodes */
	ret += EXT3_MAXQUOTAS_TRANS_BLOCKS(inode->i_sb);
#endif

	return ret;
}

/*
 * The caller must have previously called ext3_reserve_inode_write().
 * Give this, we know that the caller already has write access to iloc->bh.
 */
int ext3_mark_iloc_dirty(handle_t *handle,
		struct inode *inode, struct ext3_iloc *iloc)
{
	int err = 0;

	/* the do_update_inode consumes one bh->b_count */
	get_bh(iloc->bh);

	/* ext3_do_update_inode() does journal_dirty_metadata */
	err = ext3_do_update_inode(handle, inode, iloc);
	put_bh(iloc->bh);
	return err;
}

/*
 * On success, We end up with an outstanding reference count against
 * iloc->bh.  This _must_ be cleaned up later.
 */

int
ext3_reserve_inode_write(handle_t *handle, struct inode *inode,
			 struct ext3_iloc *iloc)
{
	int err = 0;
	if (handle) {
		err = ext3_get_inode_loc(inode, iloc);
		if (!err) {
			BUFFER_TRACE(iloc->bh, "get_write_access");
			err = ext3_journal_get_write_access(handle, iloc->bh);
			if (err) {
				brelse(iloc->bh);
				iloc->bh = NULL;
			}
		}
	}
	ext3_std_error(inode->i_sb, err);
	return err;
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
 */
int ext3_mark_inode_dirty(handle_t *handle, struct inode *inode)
{
	struct ext3_iloc iloc;
	int err;

	might_sleep();
	trace_ext3_mark_inode_dirty(inode, _RET_IP_);
	err = ext3_reserve_inode_write(handle, inode, &iloc);
	if (!err)
		err = ext3_mark_iloc_dirty(handle, inode, &iloc);
	return err;
}

/*
 * ext3_dirty_inode() is called from __mark_inode_dirty()
 *
 * We're really interested in the case where a file is being extended.
 * i_size has been changed by generic_commit_write() and we thus need
 * to include the updated inode in the current transaction.
 *
 * Also, dquot_alloc_space() will always dirty the inode when blocks
 * are allocated to the file.
 *
 * If the inode is marked synchronous, we don't honour that here - doing
 * so would cause a commit on atime updates, which we don't bother doing.
 * We handle synchronous inodes at the highest possible level.
 */
void ext3_dirty_inode(struct inode *inode, int flags)
{
	handle_t *current_handle = ext3_journal_current_handle();
	handle_t *handle;

	handle = ext3_journal_start(inode, 2);
	if (IS_ERR(handle))
		goto out;
	if (current_handle &&
		current_handle->h_transaction != handle->h_transaction) {
		/* This task has a transaction open against a different fs */
		printk(KERN_EMERG "%s: transactions do not match!\n",
		       __func__);
	} else {
		jbd_debug(5, "marking dirty.  outer handle=%p\n",
				current_handle);
		ext3_mark_inode_dirty(handle, inode);
	}
	ext3_journal_stop(handle);
out:
	return;
}

#if 0
/*
 * Bind an inode's backing buffer_head into this transaction, to prevent
 * it from being flushed to disk early.  Unlike
 * ext3_reserve_inode_write, this leaves behind no bh reference and
 * returns no iloc structure, so the caller needs to repeat the iloc
 * lookup to mark the inode dirty later.
 */
static int ext3_pin_inode(handle_t *handle, struct inode *inode)
{
	struct ext3_iloc iloc;

	int err = 0;
	if (handle) {
		err = ext3_get_inode_loc(inode, &iloc);
		if (!err) {
			BUFFER_TRACE(iloc.bh, "get_write_access");
			err = journal_get_write_access(handle, iloc.bh);
			if (!err)
				err = ext3_journal_dirty_metadata(handle,
								  iloc.bh);
			brelse(iloc.bh);
		}
	}
	ext3_std_error(inode->i_sb, err);
	return err;
}
#endif

int ext3_change_inode_journal_flag(struct inode *inode, int val)
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

	journal = EXT3_JOURNAL(inode);
	if (is_journal_aborted(journal))
		return -EROFS;

	journal_lock_updates(journal);
	journal_flush(journal);

	/*
	 * OK, there are no updates running now, and all cached data is
	 * synced to disk.  We are now in a completely consistent state
	 * which doesn't have anything in the journal, and we know that
	 * no filesystem updates are running, so it is safe to modify
	 * the inode's in-core data-journaling state flag now.
	 */

	if (val)
		EXT3_I(inode)->i_flags |= EXT3_JOURNAL_DATA_FL;
	else
		EXT3_I(inode)->i_flags &= ~EXT3_JOURNAL_DATA_FL;
	ext3_set_aops(inode);

	journal_unlock_updates(journal);

	/* Finally we can mark the inode as dirty. */

	handle = ext3_journal_start(inode, 1);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	err = ext3_mark_inode_dirty(handle, inode);
	handle->h_sync = 1;
	ext3_journal_stop(handle);
	ext3_std_error(inode->i_sb, err);

	return err;
}
