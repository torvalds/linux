/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/time.h>
#include <linux/reiserfs_fs.h>
#include <linux/reiserfs_acl.h>
#include <linux/reiserfs_xattr.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/quotaops.h>

/*
** We pack the tails of files on file close, not at the time they are written.
** This implies an unnecessary copy of the tail and an unnecessary indirect item
** insertion/balancing, for files that are written in one write.
** It avoids unnecessary tail packings (balances) for files that are written in
** multiple writes and are small enough to have tails.
** 
** file_release is called by the VFS layer when the file is closed.  If
** this is the last open file descriptor, and the file
** small enough to have a tail, and the tail is currently in an
** unformatted node, the tail is converted back into a direct item.
** 
** We use reiserfs_truncate_file to pack the tail, since it already has
** all the conditions coded.  
*/
static int reiserfs_file_release(struct inode *inode, struct file *filp)
{

	struct reiserfs_transaction_handle th;
	int err;
	int jbegin_failure = 0;

	BUG_ON(!S_ISREG(inode->i_mode));

	/* fast out for when nothing needs to be done */
	if ((atomic_read(&inode->i_count) > 1 ||
	     !(REISERFS_I(inode)->i_flags & i_pack_on_close_mask) ||
	     !tail_has_to_be_packed(inode)) &&
	    REISERFS_I(inode)->i_prealloc_count <= 0) {
		return 0;
	}

	mutex_lock(&inode->i_mutex);
	reiserfs_write_lock(inode->i_sb);
	/* freeing preallocation only involves relogging blocks that
	 * are already in the current transaction.  preallocation gets
	 * freed at the end of each transaction, so it is impossible for
	 * us to log any additional blocks (including quota blocks)
	 */
	err = journal_begin(&th, inode->i_sb, 1);
	if (err) {
		/* uh oh, we can't allow the inode to go away while there
		 * is still preallocation blocks pending.  Try to join the
		 * aborted transaction
		 */
		jbegin_failure = err;
		err = journal_join_abort(&th, inode->i_sb, 1);

		if (err) {
			/* hmpf, our choices here aren't good.  We can pin the inode
			 * which will disallow unmount from every happening, we can
			 * do nothing, which will corrupt random memory on unmount,
			 * or we can forcibly remove the file from the preallocation
			 * list, which will leak blocks on disk.  Lets pin the inode
			 * and let the admin know what is going on.
			 */
			igrab(inode);
			reiserfs_warning(inode->i_sb,
					 "pinning inode %lu because the "
					 "preallocation can't be freed",
					 inode->i_ino);
			goto out;
		}
	}
	reiserfs_update_inode_transaction(inode);

#ifdef REISERFS_PREALLOCATE
	reiserfs_discard_prealloc(&th, inode);
#endif
	err = journal_end(&th, inode->i_sb, 1);

	/* copy back the error code from journal_begin */
	if (!err)
		err = jbegin_failure;

	if (!err && atomic_read(&inode->i_count) <= 1 &&
	    (REISERFS_I(inode)->i_flags & i_pack_on_close_mask) &&
	    tail_has_to_be_packed(inode)) {
		/* if regular file is released by last holder and it has been
		   appended (we append by unformatted node only) or its direct
		   item(s) had to be converted, then it may have to be
		   indirect2direct converted */
		err = reiserfs_truncate_file(inode, 0);
	}
      out:
	mutex_unlock(&inode->i_mutex);
	reiserfs_write_unlock(inode->i_sb);
	return err;
}

static void reiserfs_vfs_truncate_file(struct inode *inode)
{
	reiserfs_truncate_file(inode, 1);
}

/* Sync a reiserfs file. */

/*
 * FIXME: sync_mapping_buffers() never has anything to sync.  Can
 * be removed...
 */

static int reiserfs_sync_file(struct file *p_s_filp,
			      struct dentry *p_s_dentry, int datasync)
{
	struct inode *p_s_inode = p_s_dentry->d_inode;
	int n_err;
	int barrier_done;

	BUG_ON(!S_ISREG(p_s_inode->i_mode));
	n_err = sync_mapping_buffers(p_s_inode->i_mapping);
	reiserfs_write_lock(p_s_inode->i_sb);
	barrier_done = reiserfs_commit_for_inode(p_s_inode);
	reiserfs_write_unlock(p_s_inode->i_sb);
	if (barrier_done != 1 && reiserfs_barrier_flush(p_s_inode->i_sb))
		blkdev_issue_flush(p_s_inode->i_sb->s_bdev, NULL);
	if (barrier_done < 0)
		return barrier_done;
	return (n_err < 0) ? -EIO : 0;
}

/* I really do not want to play with memory shortage right now, so
   to simplify the code, we are not going to write more than this much pages at
   a time. This still should considerably improve performance compared to 4k
   at a time case. This is 32 pages of 4k size. */
#define REISERFS_WRITE_PAGES_AT_A_TIME (128 * 1024) / PAGE_CACHE_SIZE

/* Allocates blocks for a file to fulfil write request.
   Maps all unmapped but prepared pages from the list.
   Updates metadata with newly allocated blocknumbers as needed */
static int reiserfs_allocate_blocks_for_region(struct reiserfs_transaction_handle *th, struct inode *inode,	/* Inode we work with */
					       loff_t pos,	/* Writing position */
					       int num_pages,	/* number of pages write going
								   to touch */
					       int write_bytes,	/* amount of bytes to write */
					       struct page **prepared_pages,	/* array of
										   prepared pages
										 */
					       int blocks_to_allocate	/* Amount of blocks we
									   need to allocate to
									   fit the data into file
									 */
    )
{
	struct cpu_key key;	// cpu key of item that we are going to deal with
	struct item_head *ih;	// pointer to item head that we are going to deal with
	struct buffer_head *bh;	// Buffer head that contains items that we are going to deal with
	__le32 *item;		// pointer to item we are going to deal with
	INITIALIZE_PATH(path);	// path to item, that we are going to deal with.
	b_blocknr_t *allocated_blocks;	// Pointer to a place where allocated blocknumbers would be stored.
	reiserfs_blocknr_hint_t hint;	// hint structure for block allocator.
	size_t res;		// return value of various functions that we call.
	int curr_block;		// current block used to keep track of unmapped blocks.
	int i;			// loop counter
	int itempos;		// position in item
	unsigned int from = (pos & (PAGE_CACHE_SIZE - 1));	// writing position in
	// first page
	unsigned int to = ((pos + write_bytes - 1) & (PAGE_CACHE_SIZE - 1)) + 1;	/* last modified byte offset in last page */
	__u64 hole_size;	// amount of blocks for a file hole, if it needed to be created.
	int modifying_this_item = 0;	// Flag for items traversal code to keep track
	// of the fact that we already prepared
	// current block for journal
	int will_prealloc = 0;
	RFALSE(!blocks_to_allocate,
	       "green-9004: tried to allocate zero blocks?");

	/* only preallocate if this is a small write */
	if (REISERFS_I(inode)->i_prealloc_count ||
	    (!(write_bytes & (inode->i_sb->s_blocksize - 1)) &&
	     blocks_to_allocate <
	     REISERFS_SB(inode->i_sb)->s_alloc_options.preallocsize))
		will_prealloc =
		    REISERFS_SB(inode->i_sb)->s_alloc_options.preallocsize;

	allocated_blocks = kmalloc((blocks_to_allocate + will_prealloc) *
				   sizeof(b_blocknr_t), GFP_NOFS);
	if (!allocated_blocks)
		return -ENOMEM;

	/* First we compose a key to point at the writing position, we want to do
	   that outside of any locking region. */
	make_cpu_key(&key, inode, pos + 1, TYPE_ANY, 3 /*key length */ );

	/* If we came here, it means we absolutely need to open a transaction,
	   since we need to allocate some blocks */
	reiserfs_write_lock(inode->i_sb);	// Journaling stuff and we need that.
	res = journal_begin(th, inode->i_sb, JOURNAL_PER_BALANCE_CNT * 3 + 1 + 2 * REISERFS_QUOTA_TRANS_BLOCKS(inode->i_sb));	// Wish I know if this number enough
	if (res)
		goto error_exit;
	reiserfs_update_inode_transaction(inode);

	/* Look for the in-tree position of our write, need path for block allocator */
	res = search_for_position_by_key(inode->i_sb, &key, &path);
	if (res == IO_ERROR) {
		res = -EIO;
		goto error_exit;
	}

	/* Allocate blocks */
	/* First fill in "hint" structure for block allocator */
	hint.th = th;		// transaction handle.
	hint.path = &path;	// Path, so that block allocator can determine packing locality or whatever it needs to determine.
	hint.inode = inode;	// Inode is needed by block allocator too.
	hint.search_start = 0;	// We have no hint on where to search free blocks for block allocator.
	hint.key = key.on_disk_key;	// on disk key of file.
	hint.block = inode->i_blocks >> (inode->i_sb->s_blocksize_bits - 9);	// Number of disk blocks this file occupies already.
	hint.formatted_node = 0;	// We are allocating blocks for unformatted node.
	hint.preallocate = will_prealloc;

	/* Call block allocator to allocate blocks */
	res =
	    reiserfs_allocate_blocknrs(&hint, allocated_blocks,
				       blocks_to_allocate, blocks_to_allocate);
	if (res != CARRY_ON) {
		if (res == NO_DISK_SPACE) {
			/* We flush the transaction in case of no space. This way some
			   blocks might become free */
			SB_JOURNAL(inode->i_sb)->j_must_wait = 1;
			res = restart_transaction(th, inode, &path);
			if (res)
				goto error_exit;

			/* We might have scheduled, so search again */
			res =
			    search_for_position_by_key(inode->i_sb, &key,
						       &path);
			if (res == IO_ERROR) {
				res = -EIO;
				goto error_exit;
			}

			/* update changed info for hint structure. */
			res =
			    reiserfs_allocate_blocknrs(&hint, allocated_blocks,
						       blocks_to_allocate,
						       blocks_to_allocate);
			if (res != CARRY_ON) {
				res = res == QUOTA_EXCEEDED ? -EDQUOT : -ENOSPC;
				pathrelse(&path);
				goto error_exit;
			}
		} else {
			res = res == QUOTA_EXCEEDED ? -EDQUOT : -ENOSPC;
			pathrelse(&path);
			goto error_exit;
		}
	}
#ifdef __BIG_ENDIAN
	// Too bad, I have not found any way to convert a given region from
	// cpu format to little endian format
	{
		int i;
		for (i = 0; i < blocks_to_allocate; i++)
			allocated_blocks[i] = cpu_to_le32(allocated_blocks[i]);
	}
#endif

	/* Blocks allocating well might have scheduled and tree might have changed,
	   let's search the tree again */
	/* find where in the tree our write should go */
	res = search_for_position_by_key(inode->i_sb, &key, &path);
	if (res == IO_ERROR) {
		res = -EIO;
		goto error_exit_free_blocks;
	}

	bh = get_last_bh(&path);	// Get a bufferhead for last element in path.
	ih = get_ih(&path);	// Get a pointer to last item head in path.
	item = get_item(&path);	// Get a pointer to last item in path

	/* Let's see what we have found */
	if (res != POSITION_FOUND) {	/* position not found, this means that we
					   might need to append file with holes
					   first */
		// Since we are writing past the file's end, we need to find out if
		// there is a hole that needs to be inserted before our writing
		// position, and how many blocks it is going to cover (we need to
		//  populate pointers to file blocks representing the hole with zeros)

		{
			int item_offset = 1;
			/*
			 * if ih is stat data, its offset is 0 and we don't want to
			 * add 1 to pos in the hole_size calculation
			 */
			if (is_statdata_le_ih(ih))
				item_offset = 0;
			hole_size = (pos + item_offset -
				     (le_key_k_offset
				      (get_inode_item_key_version(inode),
				       &(ih->ih_key)) + op_bytes_number(ih,
									inode->
									i_sb->
									s_blocksize)))
			    >> inode->i_sb->s_blocksize_bits;
		}

		if (hole_size > 0) {
			int to_paste = min_t(__u64, hole_size, MAX_ITEM_LEN(inode->i_sb->s_blocksize) / UNFM_P_SIZE);	// How much data to insert first time.
			/* area filled with zeroes, to supply as list of zero blocknumbers
			   We allocate it outside of loop just in case loop would spin for
			   several iterations. */
			char *zeros = kzalloc(to_paste * UNFM_P_SIZE, GFP_ATOMIC);	// We cannot insert more than MAX_ITEM_LEN bytes anyway.
			if (!zeros) {
				res = -ENOMEM;
				goto error_exit_free_blocks;
			}
			do {
				to_paste =
				    min_t(__u64, hole_size,
					  MAX_ITEM_LEN(inode->i_sb->
						       s_blocksize) /
					  UNFM_P_SIZE);
				if (is_indirect_le_ih(ih)) {
					/* Ok, there is existing indirect item already. Need to append it */
					/* Calculate position past inserted item */
					make_cpu_key(&key, inode,
						     le_key_k_offset
						     (get_inode_item_key_version
						      (inode),
						      &(ih->ih_key)) +
						     op_bytes_number(ih,
								     inode->
								     i_sb->
								     s_blocksize),
						     TYPE_INDIRECT, 3);
					res =
					    reiserfs_paste_into_item(th, &path,
								     &key,
								     inode,
								     (char *)
								     zeros,
								     UNFM_P_SIZE
								     *
								     to_paste);
					if (res) {
						kfree(zeros);
						goto error_exit_free_blocks;
					}
				} else if (is_statdata_le_ih(ih)) {
					/* No existing item, create it */
					/* item head for new item */
					struct item_head ins_ih;

					/* create a key for our new item */
					make_cpu_key(&key, inode, 1,
						     TYPE_INDIRECT, 3);

					/* Create new item head for our new item */
					make_le_item_head(&ins_ih, &key,
							  key.version, 1,
							  TYPE_INDIRECT,
							  to_paste *
							  UNFM_P_SIZE,
							  0 /* free space */ );

					/* Find where such item should live in the tree */
					res =
					    search_item(inode->i_sb, &key,
							&path);
					if (res != ITEM_NOT_FOUND) {
						/* item should not exist, otherwise we have error */
						if (res != -ENOSPC) {
							reiserfs_warning(inode->
									 i_sb,
									 "green-9008: search_by_key (%K) returned %d",
									 &key,
									 res);
						}
						res = -EIO;
						kfree(zeros);
						goto error_exit_free_blocks;
					}
					res =
					    reiserfs_insert_item(th, &path,
								 &key, &ins_ih,
								 inode,
								 (char *)zeros);
				} else {
					reiserfs_panic(inode->i_sb,
						       "green-9011: Unexpected key type %K\n",
						       &key);
				}
				if (res) {
					kfree(zeros);
					goto error_exit_free_blocks;
				}
				/* Now we want to check if transaction is too full, and if it is
				   we restart it. This will also free the path. */
				if (journal_transaction_should_end
				    (th, th->t_blocks_allocated)) {
					inode->i_size = cpu_key_k_offset(&key) +
						(to_paste << inode->i_blkbits);
					res =
					    restart_transaction(th, inode,
								&path);
					if (res) {
						pathrelse(&path);
						kfree(zeros);
						goto error_exit;
					}
				}

				/* Well, need to recalculate path and stuff */
				set_cpu_key_k_offset(&key,
						     cpu_key_k_offset(&key) +
						     (to_paste << inode->
						      i_blkbits));
				res =
				    search_for_position_by_key(inode->i_sb,
							       &key, &path);
				if (res == IO_ERROR) {
					res = -EIO;
					kfree(zeros);
					goto error_exit_free_blocks;
				}
				bh = get_last_bh(&path);
				ih = get_ih(&path);
				item = get_item(&path);
				hole_size -= to_paste;
			} while (hole_size);
			kfree(zeros);
		}
	}
	// Go through existing indirect items first
	// replace all zeroes with blocknumbers from list
	// Note that if no corresponding item was found, by previous search,
	// it means there are no existing in-tree representation for file area
	// we are going to overwrite, so there is nothing to scan through for holes.
	for (curr_block = 0, itempos = path.pos_in_item;
	     curr_block < blocks_to_allocate && res == POSITION_FOUND;) {
	      retry:

		if (itempos >= ih_item_len(ih) / UNFM_P_SIZE) {
			/* We run out of data in this indirect item, let's look for another
			   one. */
			/* First if we are already modifying current item, log it */
			if (modifying_this_item) {
				journal_mark_dirty(th, inode->i_sb, bh);
				modifying_this_item = 0;
			}
			/* Then set the key to look for a new indirect item (offset of old
			   item is added to old item length */
			set_cpu_key_k_offset(&key,
					     le_key_k_offset
					     (get_inode_item_key_version(inode),
					      &(ih->ih_key)) +
					     op_bytes_number(ih,
							     inode->i_sb->
							     s_blocksize));
			/* Search ofor position of new key in the tree. */
			res =
			    search_for_position_by_key(inode->i_sb, &key,
						       &path);
			if (res == IO_ERROR) {
				res = -EIO;
				goto error_exit_free_blocks;
			}
			bh = get_last_bh(&path);
			ih = get_ih(&path);
			item = get_item(&path);
			itempos = path.pos_in_item;
			continue;	// loop to check all kinds of conditions and so on.
		}
		/* Ok, we have correct position in item now, so let's see if it is
		   representing file hole (blocknumber is zero) and fill it if needed */
		if (!item[itempos]) {
			/* Ok, a hole. Now we need to check if we already prepared this
			   block to be journaled */
			while (!modifying_this_item) {	// loop until succeed
				/* Well, this item is not journaled yet, so we must prepare
				   it for journal first, before we can change it */
				struct item_head tmp_ih;	// We copy item head of found item,
				// here to detect if fs changed under
				// us while we were preparing for
				// journal.
				int fs_gen;	// We store fs generation here to find if someone
				// changes fs under our feet

				copy_item_head(&tmp_ih, ih);	// Remember itemhead
				fs_gen = get_generation(inode->i_sb);	// remember fs generation
				reiserfs_prepare_for_journal(inode->i_sb, bh, 1);	// Prepare a buffer within which indirect item is stored for changing.
				if (fs_changed(fs_gen, inode->i_sb)
				    && item_moved(&tmp_ih, &path)) {
					// Sigh, fs was changed under us, we need to look for new
					// location of item we are working with

					/* unmark prepaerd area as journaled and search for it's
					   new position */
					reiserfs_restore_prepared_buffer(inode->
									 i_sb,
									 bh);
					res =
					    search_for_position_by_key(inode->
								       i_sb,
								       &key,
								       &path);
					if (res == IO_ERROR) {
						res = -EIO;
						goto error_exit_free_blocks;
					}
					bh = get_last_bh(&path);
					ih = get_ih(&path);
					item = get_item(&path);
					itempos = path.pos_in_item;
					goto retry;
				}
				modifying_this_item = 1;
			}
			item[itempos] = allocated_blocks[curr_block];	// Assign new block
			curr_block++;
		}
		itempos++;
	}

	if (modifying_this_item) {	// We need to log last-accessed block, if it
		// was modified, but not logged yet.
		journal_mark_dirty(th, inode->i_sb, bh);
	}

	if (curr_block < blocks_to_allocate) {
		// Oh, well need to append to indirect item, or to create indirect item
		// if there weren't any
		if (is_indirect_le_ih(ih)) {
			// Existing indirect item - append. First calculate key for append
			// position. We do not need to recalculate path as it should
			// already point to correct place.
			make_cpu_key(&key, inode,
				     le_key_k_offset(get_inode_item_key_version
						     (inode),
						     &(ih->ih_key)) +
				     op_bytes_number(ih,
						     inode->i_sb->s_blocksize),
				     TYPE_INDIRECT, 3);
			res =
			    reiserfs_paste_into_item(th, &path, &key, inode,
						     (char *)(allocated_blocks +
							      curr_block),
						     UNFM_P_SIZE *
						     (blocks_to_allocate -
						      curr_block));
			if (res) {
				goto error_exit_free_blocks;
			}
		} else if (is_statdata_le_ih(ih)) {
			// Last found item was statdata. That means we need to create indirect item.
			struct item_head ins_ih;	/* itemhead for new item */

			/* create a key for our new item */
			make_cpu_key(&key, inode, 1, TYPE_INDIRECT, 3);	// Position one,
			// because that's
			// where first
			// indirect item
			// begins
			/* Create new item head for our new item */
			make_le_item_head(&ins_ih, &key, key.version, 1,
					  TYPE_INDIRECT,
					  (blocks_to_allocate -
					   curr_block) * UNFM_P_SIZE,
					  0 /* free space */ );
			/* Find where such item should live in the tree */
			res = search_item(inode->i_sb, &key, &path);
			if (res != ITEM_NOT_FOUND) {
				/* Well, if we have found such item already, or some error
				   occured, we need to warn user and return error */
				if (res != -ENOSPC) {
					reiserfs_warning(inode->i_sb,
							 "green-9009: search_by_key (%K) "
							 "returned %d", &key,
							 res);
				}
				res = -EIO;
				goto error_exit_free_blocks;
			}
			/* Insert item into the tree with the data as its body */
			res =
			    reiserfs_insert_item(th, &path, &key, &ins_ih,
						 inode,
						 (char *)(allocated_blocks +
							  curr_block));
		} else {
			reiserfs_panic(inode->i_sb,
				       "green-9010: unexpected item type for key %K\n",
				       &key);
		}
	}
	// the caller is responsible for closing the transaction
	// unless we return an error, they are also responsible for logging
	// the inode.
	//
	pathrelse(&path);
	/*
	 * cleanup prellocation from previous writes
	 * if this is a partial block write
	 */
	if (write_bytes & (inode->i_sb->s_blocksize - 1))
		reiserfs_discard_prealloc(th, inode);
	reiserfs_write_unlock(inode->i_sb);

	// go through all the pages/buffers and map the buffers to newly allocated
	// blocks (so that system knows where to write these pages later).
	curr_block = 0;
	for (i = 0; i < num_pages; i++) {
		struct page *page = prepared_pages[i];	//current page
		struct buffer_head *head = page_buffers(page);	// first buffer for a page
		int block_start, block_end;	// in-page offsets for buffers.

		if (!page_buffers(page))
			reiserfs_panic(inode->i_sb,
				       "green-9005: No buffers for prepared page???");

		/* For each buffer in page */
		for (bh = head, block_start = 0; bh != head || !block_start;
		     block_start = block_end, bh = bh->b_this_page) {
			if (!bh)
				reiserfs_panic(inode->i_sb,
					       "green-9006: Allocated but absent buffer for a page?");
			block_end = block_start + inode->i_sb->s_blocksize;
			if (i == 0 && block_end <= from)
				/* if this buffer is before requested data to map, skip it */
				continue;
			if (i == num_pages - 1 && block_start >= to)
				/* If this buffer is after requested data to map, abort
				   processing of current page */
				break;

			if (!buffer_mapped(bh)) {	// Ok, unmapped buffer, need to map it
				map_bh(bh, inode->i_sb,
				       le32_to_cpu(allocated_blocks
						   [curr_block]));
				curr_block++;
				set_buffer_new(bh);
			}
		}
	}

	RFALSE(curr_block > blocks_to_allocate,
	       "green-9007: Used too many blocks? weird");

	kfree(allocated_blocks);
	return 0;

// Need to deal with transaction here.
      error_exit_free_blocks:
	pathrelse(&path);
	// free blocks
	for (i = 0; i < blocks_to_allocate; i++)
		reiserfs_free_block(th, inode, le32_to_cpu(allocated_blocks[i]),
				    1);

      error_exit:
	if (th->t_trans_id) {
		int err;
		// update any changes we made to blk count
		mark_inode_dirty(inode);
		err =
		    journal_end(th, inode->i_sb,
				JOURNAL_PER_BALANCE_CNT * 3 + 1 +
				2 * REISERFS_QUOTA_TRANS_BLOCKS(inode->i_sb));
		if (err)
			res = err;
	}
	reiserfs_write_unlock(inode->i_sb);
	kfree(allocated_blocks);

	return res;
}

/* Unlock pages prepared by reiserfs_prepare_file_region_for_write */
static void reiserfs_unprepare_pages(struct page **prepared_pages,	/* list of locked pages */
				     size_t num_pages /* amount of pages */ )
{
	int i;			// loop counter

	for (i = 0; i < num_pages; i++) {
		struct page *page = prepared_pages[i];

		try_to_free_buffers(page);
		unlock_page(page);
		page_cache_release(page);
	}
}

/* This function will copy data from userspace to specified pages within
   supplied byte range */
static int reiserfs_copy_from_user_to_file_region(loff_t pos,	/* In-file position */
						  int num_pages,	/* Number of pages affected */
						  int write_bytes,	/* Amount of bytes to write */
						  struct page **prepared_pages,	/* pointer to 
										   array to
										   prepared pages
										 */
						  const char __user * buf	/* Pointer to user-supplied
										   data */
    )
{
	long page_fault = 0;	// status of copy_from_user.
	int i;			// loop counter.
	int offset;		// offset in page

	for (i = 0, offset = (pos & (PAGE_CACHE_SIZE - 1)); i < num_pages;
	     i++, offset = 0) {
		size_t count = min_t(size_t, PAGE_CACHE_SIZE - offset, write_bytes);	// How much of bytes to write to this page
		struct page *page = prepared_pages[i];	// Current page we process.

		fault_in_pages_readable(buf, count);

		/* Copy data from userspace to the current page */
		kmap(page);
		page_fault = __copy_from_user(page_address(page) + offset, buf, count);	// Copy the data.
		/* Flush processor's dcache for this page */
		flush_dcache_page(page);
		kunmap(page);
		buf += count;
		write_bytes -= count;

		if (page_fault)
			break;	// Was there a fault? abort.
	}

	return page_fault ? -EFAULT : 0;
}

/* taken fs/buffer.c:__block_commit_write */
int reiserfs_commit_page(struct inode *inode, struct page *page,
			 unsigned from, unsigned to)
{
	unsigned block_start, block_end;
	int partial = 0;
	unsigned blocksize;
	struct buffer_head *bh, *head;
	unsigned long i_size_index = inode->i_size >> PAGE_CACHE_SHIFT;
	int new;
	int logit = reiserfs_file_data_log(inode);
	struct super_block *s = inode->i_sb;
	int bh_per_page = PAGE_CACHE_SIZE / s->s_blocksize;
	struct reiserfs_transaction_handle th;
	int ret = 0;

	th.t_trans_id = 0;
	blocksize = 1 << inode->i_blkbits;

	if (logit) {
		reiserfs_write_lock(s);
		ret = journal_begin(&th, s, bh_per_page + 1);
		if (ret)
			goto drop_write_lock;
		reiserfs_update_inode_transaction(inode);
	}
	for (bh = head = page_buffers(page), block_start = 0;
	     bh != head || !block_start;
	     block_start = block_end, bh = bh->b_this_page) {

		new = buffer_new(bh);
		clear_buffer_new(bh);
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = 1;
		} else {
			set_buffer_uptodate(bh);
			if (logit) {
				reiserfs_prepare_for_journal(s, bh, 1);
				journal_mark_dirty(&th, s, bh);
			} else if (!buffer_dirty(bh)) {
				mark_buffer_dirty(bh);
				/* do data=ordered on any page past the end
				 * of file and any buffer marked BH_New.
				 */
				if (reiserfs_data_ordered(inode->i_sb) &&
				    (new || page->index >= i_size_index)) {
					reiserfs_add_ordered_list(inode, bh);
				}
			}
		}
	}
	if (logit) {
		ret = journal_end(&th, s, bh_per_page + 1);
	      drop_write_lock:
		reiserfs_write_unlock(s);
	}
	/*
	 * If this is a partial write which happened to make all buffers
	 * uptodate then we can optimize away a bogus readpage() for
	 * the next read(). Here we 'discover' whether the page went
	 * uptodate as a result of this (potentially partial) write.
	 */
	if (!partial)
		SetPageUptodate(page);
	return ret;
}

/* Submit pages for write. This was separated from actual file copying
   because we might want to allocate block numbers in-between.
   This function assumes that caller will adjust file size to correct value. */
static int reiserfs_submit_file_region_for_write(struct reiserfs_transaction_handle *th, struct inode *inode, loff_t pos,	/* Writing position offset */
						 size_t num_pages,	/* Number of pages to write */
						 size_t write_bytes,	/* number of bytes to write */
						 struct page **prepared_pages	/* list of pages */
    )
{
	int status;		// return status of block_commit_write.
	int retval = 0;		// Return value we are going to return.
	int i;			// loop counter
	int offset;		// Writing offset in page.
	int orig_write_bytes = write_bytes;
	int sd_update = 0;

	for (i = 0, offset = (pos & (PAGE_CACHE_SIZE - 1)); i < num_pages;
	     i++, offset = 0) {
		int count = min_t(int, PAGE_CACHE_SIZE - offset, write_bytes);	// How much of bytes to write to this page
		struct page *page = prepared_pages[i];	// Current page we process.

		status =
		    reiserfs_commit_page(inode, page, offset, offset + count);
		if (status)
			retval = status;	// To not overcomplicate matters We are going to
		// submit all the pages even if there was error.
		// we only remember error status to report it on
		// exit.
		write_bytes -= count;
	}
	/* now that we've gotten all the ordered buffers marked dirty,
	 * we can safely update i_size and close any running transaction
	 */
	if (pos + orig_write_bytes > inode->i_size) {
		inode->i_size = pos + orig_write_bytes;	// Set new size
		/* If the file have grown so much that tail packing is no
		 * longer possible, reset "need to pack" flag */
		if ((have_large_tails(inode->i_sb) &&
		     inode->i_size > i_block_size(inode) * 4) ||
		    (have_small_tails(inode->i_sb) &&
		     inode->i_size > i_block_size(inode)))
			REISERFS_I(inode)->i_flags &= ~i_pack_on_close_mask;
		else if ((have_large_tails(inode->i_sb) &&
			  inode->i_size < i_block_size(inode) * 4) ||
			 (have_small_tails(inode->i_sb) &&
			  inode->i_size < i_block_size(inode)))
			REISERFS_I(inode)->i_flags |= i_pack_on_close_mask;

		if (th->t_trans_id) {
			reiserfs_write_lock(inode->i_sb);
			// this sets the proper flags for O_SYNC to trigger a commit
			mark_inode_dirty(inode);
			reiserfs_write_unlock(inode->i_sb);
		} else {
			reiserfs_write_lock(inode->i_sb);
			reiserfs_update_inode_transaction(inode);
			mark_inode_dirty(inode);
			reiserfs_write_unlock(inode->i_sb);
		}

		sd_update = 1;
	}
	if (th->t_trans_id) {
		reiserfs_write_lock(inode->i_sb);
		if (!sd_update)
			mark_inode_dirty(inode);
		status = journal_end(th, th->t_super, th->t_blocks_allocated);
		if (status)
			retval = status;
		reiserfs_write_unlock(inode->i_sb);
	}
	th->t_trans_id = 0;

	/* 
	 * we have to unlock the pages after updating i_size, otherwise
	 * we race with writepage
	 */
	for (i = 0; i < num_pages; i++) {
		struct page *page = prepared_pages[i];
		unlock_page(page);
		mark_page_accessed(page);
		page_cache_release(page);
	}
	return retval;
}

/* Look if passed writing region is going to touch file's tail
   (if it is present). And if it is, convert the tail to unformatted node */
static int reiserfs_check_for_tail_and_convert(struct inode *inode,	/* inode to deal with */
					       loff_t pos,	/* Writing position */
					       int write_bytes	/* amount of bytes to write */
    )
{
	INITIALIZE_PATH(path);	// needed for search_for_position
	struct cpu_key key;	// Key that would represent last touched writing byte.
	struct item_head *ih;	// item header of found block;
	int res;		// Return value of various functions we call.
	int cont_expand_offset;	// We will put offset for generic_cont_expand here
	// This can be int just because tails are created
	// only for small files.

/* this embodies a dependency on a particular tail policy */
	if (inode->i_size >= inode->i_sb->s_blocksize * 4) {
		/* such a big files do not have tails, so we won't bother ourselves
		   to look for tails, simply return */
		return 0;
	}

	reiserfs_write_lock(inode->i_sb);
	/* find the item containing the last byte to be written, or if
	 * writing past the end of the file then the last item of the
	 * file (and then we check its type). */
	make_cpu_key(&key, inode, pos + write_bytes + 1, TYPE_ANY,
		     3 /*key length */ );
	res = search_for_position_by_key(inode->i_sb, &key, &path);
	if (res == IO_ERROR) {
		reiserfs_write_unlock(inode->i_sb);
		return -EIO;
	}
	ih = get_ih(&path);
	res = 0;
	if (is_direct_le_ih(ih)) {
		/* Ok, closest item is file tail (tails are stored in "direct"
		 * items), so we need to unpack it. */
		/* To not overcomplicate matters, we just call generic_cont_expand
		   which will in turn call other stuff and finally will boil down to
		   reiserfs_get_block() that would do necessary conversion. */
		cont_expand_offset =
		    le_key_k_offset(get_inode_item_key_version(inode),
				    &(ih->ih_key));
		pathrelse(&path);
		res = generic_cont_expand(inode, cont_expand_offset);
	} else
		pathrelse(&path);

	reiserfs_write_unlock(inode->i_sb);
	return res;
}

/* This function locks pages starting from @pos for @inode.
   @num_pages pages are locked and stored in
   @prepared_pages array. Also buffers are allocated for these pages.
   First and last page of the region is read if it is overwritten only
   partially. If last page did not exist before write (file hole or file
   append), it is zeroed, then. 
   Returns number of unallocated blocks that should be allocated to cover
   new file data.*/
static int reiserfs_prepare_file_region_for_write(struct inode *inode
						  /* Inode of the file */ ,
						  loff_t pos,	/* position in the file */
						  size_t num_pages,	/* number of pages to
									   prepare */
						  size_t write_bytes,	/* Amount of bytes to be
									   overwritten from
									   @pos */
						  struct page **prepared_pages	/* pointer to array
										   where to store
										   prepared pages */
    )
{
	int res = 0;		// Return values of different functions we call.
	unsigned long index = pos >> PAGE_CACHE_SHIFT;	// Offset in file in pages.
	int from = (pos & (PAGE_CACHE_SIZE - 1));	// Writing offset in first page
	int to = ((pos + write_bytes - 1) & (PAGE_CACHE_SIZE - 1)) + 1;
	/* offset of last modified byte in last
	   page */
	struct address_space *mapping = inode->i_mapping;	// Pages are mapped here.
	int i;			// Simple counter
	int blocks = 0;		/* Return value (blocks that should be allocated) */
	struct buffer_head *bh, *head;	// Current bufferhead and first bufferhead
	// of a page.
	unsigned block_start, block_end;	// Starting and ending offsets of current
	// buffer in the page.
	struct buffer_head *wait[2], **wait_bh = wait;	// Buffers for page, if
	// Page appeared to be not up
	// to date. Note how we have
	// at most 2 buffers, this is
	// because we at most may
	// partially overwrite two
	// buffers for one page. One at                                                 // the beginning of write area
	// and one at the end.
	// Everything inthe middle gets                                                 // overwritten totally.

	struct cpu_key key;	// cpu key of item that we are going to deal with
	struct item_head *ih = NULL;	// pointer to item head that we are going to deal with
	struct buffer_head *itembuf = NULL;	// Buffer head that contains items that we are going to deal with
	INITIALIZE_PATH(path);	// path to item, that we are going to deal with.
	__le32 *item = NULL;	// pointer to item we are going to deal with
	int item_pos = -1;	/* Position in indirect item */

	if (num_pages < 1) {
		reiserfs_warning(inode->i_sb,
				 "green-9001: reiserfs_prepare_file_region_for_write "
				 "called with zero number of pages to process");
		return -EFAULT;
	}

	/* We have 2 loops for pages. In first loop we grab and lock the pages, so
	   that nobody would touch these until we release the pages. Then
	   we'd start to deal with mapping buffers to blocks. */
	for (i = 0; i < num_pages; i++) {
		prepared_pages[i] = grab_cache_page(mapping, index + i);	// locks the page
		if (!prepared_pages[i]) {
			res = -ENOMEM;
			goto failed_page_grabbing;
		}
		if (!page_has_buffers(prepared_pages[i]))
			create_empty_buffers(prepared_pages[i],
					     inode->i_sb->s_blocksize, 0);
	}

	/* Let's count amount of blocks for a case where all the blocks
	   overwritten are new (we will substract already allocated blocks later) */
	if (num_pages > 2)
		/* These are full-overwritten pages so we count all the blocks in
		   these pages are counted as needed to be allocated */
		blocks =
		    (num_pages - 2) << (PAGE_CACHE_SHIFT - inode->i_blkbits);

	/* count blocks needed for first page (possibly partially written) */
	blocks += ((PAGE_CACHE_SIZE - from) >> inode->i_blkbits) + !!(from & (inode->i_sb->s_blocksize - 1));	/* roundup */

	/* Now we account for last page. If last page == first page (we
	   overwrite only one page), we substract all the blocks past the
	   last writing position in a page out of already calculated number
	   of blocks */
	blocks += ((num_pages > 1) << (PAGE_CACHE_SHIFT - inode->i_blkbits)) -
	    ((PAGE_CACHE_SIZE - to) >> inode->i_blkbits);
	/* Note how we do not roundup here since partial blocks still
	   should be allocated */

	/* Now if all the write area lies past the file end, no point in
	   maping blocks, since there is none, so we just zero out remaining
	   parts of first and last pages in write area (if needed) */
	if ((pos & ~((loff_t) PAGE_CACHE_SIZE - 1)) > inode->i_size) {
		if (from != 0) {	/* First page needs to be partially zeroed */
			char *kaddr = kmap_atomic(prepared_pages[0], KM_USER0);
			memset(kaddr, 0, from);
			kunmap_atomic(kaddr, KM_USER0);
			flush_dcache_page(prepared_pages[0]);
		}
		if (to != PAGE_CACHE_SIZE) {	/* Last page needs to be partially zeroed */
			char *kaddr =
			    kmap_atomic(prepared_pages[num_pages - 1],
					KM_USER0);
			memset(kaddr + to, 0, PAGE_CACHE_SIZE - to);
			kunmap_atomic(kaddr, KM_USER0);
			flush_dcache_page(prepared_pages[num_pages - 1]);
		}

		/* Since all blocks are new - use already calculated value */
		return blocks;
	}

	/* Well, since we write somewhere into the middle of a file, there is
	   possibility we are writing over some already allocated blocks, so
	   let's map these blocks and substract number of such blocks out of blocks
	   we need to allocate (calculated above) */
	/* Mask write position to start on blocksize, we do it out of the
	   loop for performance reasons */
	pos &= ~((loff_t) inode->i_sb->s_blocksize - 1);
	/* Set cpu key to the starting position in a file (on left block boundary) */
	make_cpu_key(&key, inode,
		     1 + ((pos) & ~((loff_t) inode->i_sb->s_blocksize - 1)),
		     TYPE_ANY, 3 /*key length */ );

	reiserfs_write_lock(inode->i_sb);	// We need that for at least search_by_key()
	for (i = 0; i < num_pages; i++) {

		head = page_buffers(prepared_pages[i]);
		/* For each buffer in the page */
		for (bh = head, block_start = 0; bh != head || !block_start;
		     block_start = block_end, bh = bh->b_this_page) {
			if (!bh)
				reiserfs_panic(inode->i_sb,
					       "green-9002: Allocated but absent buffer for a page?");
			/* Find where this buffer ends */
			block_end = block_start + inode->i_sb->s_blocksize;
			if (i == 0 && block_end <= from)
				/* if this buffer is before requested data to map, skip it */
				continue;

			if (i == num_pages - 1 && block_start >= to) {
				/* If this buffer is after requested data to map, abort
				   processing of current page */
				break;
			}

			if (buffer_mapped(bh) && bh->b_blocknr != 0) {
				/* This is optimisation for a case where buffer is mapped
				   and have blocknumber assigned. In case significant amount
				   of such buffers are present, we may avoid some amount
				   of search_by_key calls.
				   Probably it would be possible to move parts of this code
				   out of BKL, but I afraid that would overcomplicate code
				   without any noticeable benefit.
				 */
				item_pos++;
				/* Update the key */
				set_cpu_key_k_offset(&key,
						     cpu_key_k_offset(&key) +
						     inode->i_sb->s_blocksize);
				blocks--;	// Decrease the amount of blocks that need to be
				// allocated
				continue;	// Go to the next buffer
			}

			if (!itembuf ||	/* if first iteration */
			    item_pos >= ih_item_len(ih) / UNFM_P_SIZE) {	/* or if we progressed past the
										   current unformatted_item */
				/* Try to find next item */
				res =
				    search_for_position_by_key(inode->i_sb,
							       &key, &path);
				/* Abort if no more items */
				if (res != POSITION_FOUND) {
					/* make sure later loops don't use this item */
					itembuf = NULL;
					item = NULL;
					break;
				}

				/* Update information about current indirect item */
				itembuf = get_last_bh(&path);
				ih = get_ih(&path);
				item = get_item(&path);
				item_pos = path.pos_in_item;

				RFALSE(!is_indirect_le_ih(ih),
				       "green-9003: indirect item expected");
			}

			/* See if there is some block associated with the file
			   at that position, map the buffer to this block */
			if (get_block_num(item, item_pos)) {
				map_bh(bh, inode->i_sb,
				       get_block_num(item, item_pos));
				blocks--;	// Decrease the amount of blocks that need to be
				// allocated
			}
			item_pos++;
			/* Update the key */
			set_cpu_key_k_offset(&key,
					     cpu_key_k_offset(&key) +
					     inode->i_sb->s_blocksize);
		}
	}
	pathrelse(&path);	// Free the path
	reiserfs_write_unlock(inode->i_sb);

	/* Now zero out unmappend buffers for the first and last pages of
	   write area or issue read requests if page is mapped. */
	/* First page, see if it is not uptodate */
	if (!PageUptodate(prepared_pages[0])) {
		head = page_buffers(prepared_pages[0]);

		/* For each buffer in page */
		for (bh = head, block_start = 0; bh != head || !block_start;
		     block_start = block_end, bh = bh->b_this_page) {

			if (!bh)
				reiserfs_panic(inode->i_sb,
					       "green-9002: Allocated but absent buffer for a page?");
			/* Find where this buffer ends */
			block_end = block_start + inode->i_sb->s_blocksize;
			if (block_end <= from)
				/* if this buffer is before requested data to map, skip it */
				continue;
			if (block_start < from) {	/* Aha, our partial buffer */
				if (buffer_mapped(bh)) {	/* If it is mapped, we need to
								   issue READ request for it to
								   not loose data */
					ll_rw_block(READ, 1, &bh);
					*wait_bh++ = bh;
				} else {	/* Not mapped, zero it */
					char *kaddr =
					    kmap_atomic(prepared_pages[0],
							KM_USER0);
					memset(kaddr + block_start, 0,
					       from - block_start);
					kunmap_atomic(kaddr, KM_USER0);
					flush_dcache_page(prepared_pages[0]);
					set_buffer_uptodate(bh);
				}
			}
		}
	}

	/* Last page, see if it is not uptodate, or if the last page is past the end of the file. */
	if (!PageUptodate(prepared_pages[num_pages - 1]) ||
	    ((pos + write_bytes) >> PAGE_CACHE_SHIFT) >
	    (inode->i_size >> PAGE_CACHE_SHIFT)) {
		head = page_buffers(prepared_pages[num_pages - 1]);

		/* for each buffer in page */
		for (bh = head, block_start = 0; bh != head || !block_start;
		     block_start = block_end, bh = bh->b_this_page) {

			if (!bh)
				reiserfs_panic(inode->i_sb,
					       "green-9002: Allocated but absent buffer for a page?");
			/* Find where this buffer ends */
			block_end = block_start + inode->i_sb->s_blocksize;
			if (block_start >= to)
				/* if this buffer is after requested data to map, skip it */
				break;
			if (block_end > to) {	/* Aha, our partial buffer */
				if (buffer_mapped(bh)) {	/* If it is mapped, we need to
								   issue READ request for it to
								   not loose data */
					ll_rw_block(READ, 1, &bh);
					*wait_bh++ = bh;
				} else {	/* Not mapped, zero it */
					char *kaddr =
					    kmap_atomic(prepared_pages
							[num_pages - 1],
							KM_USER0);
					memset(kaddr + to, 0, block_end - to);
					kunmap_atomic(kaddr, KM_USER0);
					flush_dcache_page(prepared_pages[num_pages - 1]);
					set_buffer_uptodate(bh);
				}
			}
		}
	}

	/* Wait for read requests we made to happen, if necessary */
	while (wait_bh > wait) {
		wait_on_buffer(*--wait_bh);
		if (!buffer_uptodate(*wait_bh)) {
			res = -EIO;
			goto failed_read;
		}
	}

	return blocks;
      failed_page_grabbing:
	num_pages = i;
      failed_read:
	reiserfs_unprepare_pages(prepared_pages, num_pages);
	return res;
}

/* Write @count bytes at position @ppos in a file indicated by @file
   from the buffer @buf.  

   generic_file_write() is only appropriate for filesystems that are not seeking to optimize performance and want
   something simple that works.  It is not for serious use by general purpose filesystems, excepting the one that it was
   written for (ext2/3).  This is for several reasons:

   * It has no understanding of any filesystem specific optimizations.

   * It enters the filesystem repeatedly for each page that is written.

   * It depends on reiserfs_get_block() function which if implemented by reiserfs performs costly search_by_key
   * operation for each page it is supplied with. By contrast reiserfs_file_write() feeds as much as possible at a time
   * to reiserfs which allows for fewer tree traversals.

   * Each indirect pointer insertion takes a lot of cpu, because it involves memory moves inside of blocks.

   * Asking the block allocation code for blocks one at a time is slightly less efficient.

   All of these reasons for not using only generic file write were understood back when reiserfs was first miscoded to
   use it, but we were in a hurry to make code freeze, and so it couldn't be revised then.  This new code should make
   things right finally.

   Future Features: providing search_by_key with hints.

*/
static ssize_t reiserfs_file_write(struct file *file,	/* the file we are going to write into */
				   const char __user * buf,	/*  pointer to user supplied data
								   (in userspace) */
				   size_t count,	/* amount of bytes to write */
				   loff_t * ppos	/* pointer to position in file that we start writing at. Should be updated to
							 * new current position before returning. */
				   )
{
	size_t already_written = 0;	// Number of bytes already written to the file.
	loff_t pos;		// Current position in the file.
	ssize_t res;		// return value of various functions that we call.
	int err = 0;
	struct inode *inode = file->f_dentry->d_inode;	// Inode of the file that we are writing to.
	/* To simplify coding at this time, we store
	   locked pages in array for now */
	struct page *prepared_pages[REISERFS_WRITE_PAGES_AT_A_TIME];
	struct reiserfs_transaction_handle th;
	th.t_trans_id = 0;

	/* If a filesystem is converted from 3.5 to 3.6, we'll have v3.5 items
	* lying around (most of the disk, in fact). Despite the filesystem
	* now being a v3.6 format, the old items still can't support large
	* file sizes. Catch this case here, as the rest of the VFS layer is
	* oblivious to the different limitations between old and new items.
	* reiserfs_setattr catches this for truncates. This chunk is lifted
	* from generic_write_checks. */
	if (get_inode_item_key_version (inode) == KEY_FORMAT_3_5 &&
	    *ppos + count > MAX_NON_LFS) {
		if (*ppos >= MAX_NON_LFS) {
			send_sig(SIGXFSZ, current, 0);
			return -EFBIG;
		}
		if (count > MAX_NON_LFS - (unsigned long)*ppos)
			count = MAX_NON_LFS - (unsigned long)*ppos;
	}

	if (file->f_flags & O_DIRECT)
		return do_sync_write(file, buf, count, ppos);

	if (unlikely((ssize_t) count < 0))
		return -EINVAL;

	if (unlikely(!access_ok(VERIFY_READ, buf, count)))
		return -EFAULT;

	mutex_lock(&inode->i_mutex);	// locks the entire file for just us

	pos = *ppos;

	/* Check if we can write to specified region of file, file
	   is not overly big and this kind of stuff. Adjust pos and
	   count, if needed */
	res = generic_write_checks(file, &pos, &count, 0);
	if (res)
		goto out;

	if (count == 0)
		goto out;

	res = remove_suid(file->f_dentry);
	if (res)
		goto out;

	file_update_time(file);

	// Ok, we are done with all the checks.

	// Now we should start real work

	/* If we are going to write past the file's packed tail or if we are going
	   to overwrite part of the tail, we need that tail to be converted into
	   unformatted node */
	res = reiserfs_check_for_tail_and_convert(inode, pos, count);
	if (res)
		goto out;

	while (count > 0) {
		/* This is the main loop in which we running until some error occures
		   or until we write all of the data. */
		size_t num_pages;	/* amount of pages we are going to write this iteration */
		size_t write_bytes;	/* amount of bytes to write during this iteration */
		size_t blocks_to_allocate;	/* how much blocks we need to allocate for this iteration */

		/*  (pos & (PAGE_CACHE_SIZE-1)) is an idiom for offset into a page of pos */
		num_pages = !!((pos + count) & (PAGE_CACHE_SIZE - 1)) +	/* round up partial
									   pages */
		    ((count +
		      (pos & (PAGE_CACHE_SIZE - 1))) >> PAGE_CACHE_SHIFT);
		/* convert size to amount of
		   pages */
		reiserfs_write_lock(inode->i_sb);
		if (num_pages > REISERFS_WRITE_PAGES_AT_A_TIME
		    || num_pages > reiserfs_can_fit_pages(inode->i_sb)) {
			/* If we were asked to write more data than we want to or if there
			   is not that much space, then we shorten amount of data to write
			   for this iteration. */
			num_pages =
			    min_t(size_t, REISERFS_WRITE_PAGES_AT_A_TIME,
				  reiserfs_can_fit_pages(inode->i_sb));
			/* Also we should not forget to set size in bytes accordingly */
			write_bytes = (num_pages << PAGE_CACHE_SHIFT) -
			    (pos & (PAGE_CACHE_SIZE - 1));
			/* If position is not on the
			   start of the page, we need
			   to substract the offset
			   within page */
		} else
			write_bytes = count;

		/* reserve the blocks to be allocated later, so that later on
		   we still have the space to write the blocks to */
		reiserfs_claim_blocks_to_be_allocated(inode->i_sb,
						      num_pages <<
						      (PAGE_CACHE_SHIFT -
						       inode->i_blkbits));
		reiserfs_write_unlock(inode->i_sb);

		if (!num_pages) {	/* If we do not have enough space even for a single page... */
			if (pos >
			    inode->i_size + inode->i_sb->s_blocksize -
			    (pos & (inode->i_sb->s_blocksize - 1))) {
				res = -ENOSPC;
				break;	// In case we are writing past the end of the last file block, break.
			}
			// Otherwise we are possibly overwriting the file, so
			// let's set write size to be equal or less than blocksize.
			// This way we get it correctly for file holes.
			// But overwriting files on absolutelly full volumes would not
			// be very efficient. Well, people are not supposed to fill
			// 100% of disk space anyway.
			write_bytes =
			    min_t(size_t, count,
				  inode->i_sb->s_blocksize -
				  (pos & (inode->i_sb->s_blocksize - 1)));
			num_pages = 1;
			// No blocks were claimed before, so do it now.
			reiserfs_claim_blocks_to_be_allocated(inode->i_sb,
							      1 <<
							      (PAGE_CACHE_SHIFT
							       -
							       inode->
							       i_blkbits));
		}

		/* Prepare for writing into the region, read in all the
		   partially overwritten pages, if needed. And lock the pages,
		   so that nobody else can access these until we are done.
		   We get number of actual blocks needed as a result. */
		res = reiserfs_prepare_file_region_for_write(inode, pos,
							     num_pages,
							     write_bytes,
							     prepared_pages);
		if (res < 0) {
			reiserfs_release_claimed_blocks(inode->i_sb,
							num_pages <<
							(PAGE_CACHE_SHIFT -
							 inode->i_blkbits));
			break;
		}

		blocks_to_allocate = res;

		/* First we correct our estimate of how many blocks we need */
		reiserfs_release_claimed_blocks(inode->i_sb,
						(num_pages <<
						 (PAGE_CACHE_SHIFT -
						  inode->i_sb->
						  s_blocksize_bits)) -
						blocks_to_allocate);

		if (blocks_to_allocate > 0) {	/*We only allocate blocks if we need to */
			/* Fill in all the possible holes and append the file if needed */
			res =
			    reiserfs_allocate_blocks_for_region(&th, inode, pos,
								num_pages,
								write_bytes,
								prepared_pages,
								blocks_to_allocate);
		}

		/* well, we have allocated the blocks, so it is time to free
		   the reservation we made earlier. */
		reiserfs_release_claimed_blocks(inode->i_sb,
						blocks_to_allocate);
		if (res) {
			reiserfs_unprepare_pages(prepared_pages, num_pages);
			break;
		}

/* NOTE that allocating blocks and filling blocks can be done in reverse order
   and probably we would do that just to get rid of garbage in files after a
   crash */

		/* Copy data from user-supplied buffer to file's pages */
		res =
		    reiserfs_copy_from_user_to_file_region(pos, num_pages,
							   write_bytes,
							   prepared_pages, buf);
		if (res) {
			reiserfs_unprepare_pages(prepared_pages, num_pages);
			break;
		}

		/* Send the pages to disk and unlock them. */
		res =
		    reiserfs_submit_file_region_for_write(&th, inode, pos,
							  num_pages,
							  write_bytes,
							  prepared_pages);
		if (res)
			break;

		already_written += write_bytes;
		buf += write_bytes;
		*ppos = pos += write_bytes;
		count -= write_bytes;
		balance_dirty_pages_ratelimited_nr(inode->i_mapping, num_pages);
	}

	/* this is only true on error */
	if (th.t_trans_id) {
		reiserfs_write_lock(inode->i_sb);
		err = journal_end(&th, th.t_super, th.t_blocks_allocated);
		reiserfs_write_unlock(inode->i_sb);
		if (err) {
			res = err;
			goto out;
		}
	}

	if (likely(res >= 0) &&
	    (unlikely((file->f_flags & O_SYNC) || IS_SYNC(inode))))
		res = generic_osync_inode(inode, file->f_mapping,
		                          OSYNC_METADATA | OSYNC_DATA);

	mutex_unlock(&inode->i_mutex);
	reiserfs_async_progress_wait(inode->i_sb);
	return (already_written != 0) ? already_written : res;

      out:
	mutex_unlock(&inode->i_mutex);	// unlock the file on exit.
	return res;
}

const struct file_operations reiserfs_file_operations = {
	.read = do_sync_read,
	.write = reiserfs_file_write,
	.ioctl = reiserfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = reiserfs_compat_ioctl,
#endif
	.mmap = generic_file_mmap,
	.open = generic_file_open,
	.release = reiserfs_file_release,
	.fsync = reiserfs_sync_file,
	.sendfile = generic_file_sendfile,
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
	.splice_read = generic_file_splice_read,
	.splice_write = generic_file_splice_write,
};

struct inode_operations reiserfs_file_inode_operations = {
	.truncate = reiserfs_vfs_truncate_file,
	.setattr = reiserfs_setattr,
	.setxattr = reiserfs_setxattr,
	.getxattr = reiserfs_getxattr,
	.listxattr = reiserfs_listxattr,
	.removexattr = reiserfs_removexattr,
	.permission = reiserfs_permission,
};
