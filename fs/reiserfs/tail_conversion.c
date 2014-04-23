/*
 * Copyright 1999 Hans Reiser, see reiserfs/README for licensing and copyright
 * details
 */

#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include "reiserfs.h"

/*
 * access to tail : when one is going to read tail it must make sure, that is
 * not running.  direct2indirect and indirect2direct can not run concurrently
 */

/*
 * Converts direct items to an unformatted node. Panics if file has no
 * tail. -ENOSPC if no disk space for conversion
 */
/*
 * path points to first direct item of the file regardless of how many of
 * them are there
 */
int direct2indirect(struct reiserfs_transaction_handle *th, struct inode *inode,
		    struct treepath *path, struct buffer_head *unbh,
		    loff_t tail_offset)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *up_to_date_bh;
	struct item_head *p_le_ih = tp_item_head(path);
	unsigned long total_tail = 0;

	/* Key to search for the last byte of the converted item. */
	struct cpu_key end_key;

	/*
	 * new indirect item to be inserted or key
	 * of unfm pointer to be pasted
	 */
	struct item_head ind_ih;
	int blk_size;
	/* returned value for reiserfs_insert_item and clones */
	int  retval;
	/* Handle on an unformatted node that will be inserted in the tree. */
	unp_t unfm_ptr;

	BUG_ON(!th->t_trans_id);

	REISERFS_SB(sb)->s_direct2indirect++;

	blk_size = sb->s_blocksize;

	/*
	 * and key to search for append or insert pointer to the new
	 * unformatted node.
	 */
	copy_item_head(&ind_ih, p_le_ih);
	set_le_ih_k_offset(&ind_ih, tail_offset);
	set_le_ih_k_type(&ind_ih, TYPE_INDIRECT);

	/* Set the key to search for the place for new unfm pointer */
	make_cpu_key(&end_key, inode, tail_offset, TYPE_INDIRECT, 4);

	/* FIXME: we could avoid this */
	if (search_for_position_by_key(sb, &end_key, path) == POSITION_FOUND) {
		reiserfs_error(sb, "PAP-14030",
			       "pasted or inserted byte exists in "
			       "the tree %K. Use fsck to repair.", &end_key);
		pathrelse(path);
		return -EIO;
	}

	p_le_ih = tp_item_head(path);

	unfm_ptr = cpu_to_le32(unbh->b_blocknr);

	if (is_statdata_le_ih(p_le_ih)) {
		/* Insert new indirect item. */
		set_ih_free_space(&ind_ih, 0);	/* delete at nearest future */
		put_ih_item_len(&ind_ih, UNFM_P_SIZE);
		PATH_LAST_POSITION(path)++;
		retval =
		    reiserfs_insert_item(th, path, &end_key, &ind_ih, inode,
					 (char *)&unfm_ptr);
	} else {
		/* Paste into last indirect item of an object. */
		retval = reiserfs_paste_into_item(th, path, &end_key, inode,
						    (char *)&unfm_ptr,
						    UNFM_P_SIZE);
	}
	if (retval) {
		return retval;
	}
	/*
	 * note: from here there are two keys which have matching first
	 *  three key components. They only differ by the fourth one.
	 */

	/* Set the key to search for the direct items of the file */
	make_cpu_key(&end_key, inode, max_reiserfs_offset(inode), TYPE_DIRECT,
		     4);

	/*
	 * Move bytes from the direct items to the new unformatted node
	 * and delete them.
	 */
	while (1) {
		int tail_size;

		/*
		 * end_key.k_offset is set so, that we will always have found
		 * last item of the file
		 */
		if (search_for_position_by_key(sb, &end_key, path) ==
		    POSITION_FOUND)
			reiserfs_panic(sb, "PAP-14050",
				       "direct item (%K) not found", &end_key);
		p_le_ih = tp_item_head(path);
		RFALSE(!is_direct_le_ih(p_le_ih),
		       "vs-14055: direct item expected(%K), found %h",
		       &end_key, p_le_ih);
		tail_size = (le_ih_k_offset(p_le_ih) & (blk_size - 1))
		    + ih_item_len(p_le_ih) - 1;

		/*
		 * we only send the unbh pointer if the buffer is not
		 * up to date.  this avoids overwriting good data from
		 * writepage() with old data from the disk or buffer cache
		 * Special case: unbh->b_page will be NULL if we are coming
		 * through DIRECT_IO handler here.
		 */
		if (!unbh->b_page || buffer_uptodate(unbh)
		    || PageUptodate(unbh->b_page)) {
			up_to_date_bh = NULL;
		} else {
			up_to_date_bh = unbh;
		}
		retval = reiserfs_delete_item(th, path, &end_key, inode,
						up_to_date_bh);

		total_tail += retval;

		/* done: file does not have direct items anymore */
		if (tail_size == retval)
			break;

	}
	/*
	 * if we've copied bytes from disk into the page, we need to zero
	 * out the unused part of the block (it was not up to date before)
	 */
	if (up_to_date_bh) {
		unsigned pgoff =
		    (tail_offset + total_tail - 1) & (PAGE_CACHE_SIZE - 1);
		char *kaddr = kmap_atomic(up_to_date_bh->b_page);
		memset(kaddr + pgoff, 0, blk_size - total_tail);
		kunmap_atomic(kaddr);
	}

	REISERFS_I(inode)->i_first_direct_byte = U32_MAX;

	return 0;
}

/* stolen from fs/buffer.c */
void reiserfs_unmap_buffer(struct buffer_head *bh)
{
	lock_buffer(bh);
	if (buffer_journaled(bh) || buffer_journal_dirty(bh)) {
		BUG();
	}
	clear_buffer_dirty(bh);
	/*
	 * Remove the buffer from whatever list it belongs to. We are mostly
	 * interested in removing it from per-sb j_dirty_buffers list, to avoid
	 * BUG() on attempt to write not mapped buffer
	 */
	if ((!list_empty(&bh->b_assoc_buffers) || bh->b_private) && bh->b_page) {
		struct inode *inode = bh->b_page->mapping->host;
		struct reiserfs_journal *j = SB_JOURNAL(inode->i_sb);
		spin_lock(&j->j_dirty_buffers_lock);
		list_del_init(&bh->b_assoc_buffers);
		reiserfs_free_jh(bh);
		spin_unlock(&j->j_dirty_buffers_lock);
	}
	clear_buffer_mapped(bh);
	clear_buffer_req(bh);
	clear_buffer_new(bh);
	bh->b_bdev = NULL;
	unlock_buffer(bh);
}

/*
 * this first locks inode (neither reads nor sync are permitted),
 * reads tail through page cache, insert direct item. When direct item
 * inserted successfully inode is left locked. Return value is always
 * what we expect from it (number of cut bytes). But when tail remains
 * in the unformatted node, we set mode to SKIP_BALANCING and unlock
 * inode
 */
int indirect2direct(struct reiserfs_transaction_handle *th,
		    struct inode *inode, struct page *page,
		    struct treepath *path,	/* path to the indirect item. */
		    const struct cpu_key *item_key,	/* Key to look for
							 * unformatted node
							 * pointer to be cut. */
		    loff_t n_new_file_size,	/* New file size. */
		    char *mode)
{
	struct super_block *sb = inode->i_sb;
	struct item_head s_ih;
	unsigned long block_size = sb->s_blocksize;
	char *tail;
	int tail_len, round_tail_len;
	loff_t pos, pos1;	/* position of first byte of the tail */
	struct cpu_key key;

	BUG_ON(!th->t_trans_id);

	REISERFS_SB(sb)->s_indirect2direct++;

	*mode = M_SKIP_BALANCING;

	/* store item head path points to. */
	copy_item_head(&s_ih, tp_item_head(path));

	tail_len = (n_new_file_size & (block_size - 1));
	if (get_inode_sd_version(inode) == STAT_DATA_V2)
		round_tail_len = ROUND_UP(tail_len);
	else
		round_tail_len = tail_len;

	pos =
	    le_ih_k_offset(&s_ih) - 1 + (ih_item_len(&s_ih) / UNFM_P_SIZE -
					 1) * sb->s_blocksize;
	pos1 = pos;

	/*
	 * we are protected by i_mutex. The tail can not disapper, not
	 * append can be done either
	 * we are in truncate or packing tail in file_release
	 */

	tail = (char *)kmap(page);	/* this can schedule */

	if (path_changed(&s_ih, path)) {
		/* re-search indirect item */
		if (search_for_position_by_key(sb, item_key, path)
		    == POSITION_NOT_FOUND)
			reiserfs_panic(sb, "PAP-5520",
				       "item to be converted %K does not exist",
				       item_key);
		copy_item_head(&s_ih, tp_item_head(path));
#ifdef CONFIG_REISERFS_CHECK
		pos = le_ih_k_offset(&s_ih) - 1 +
		    (ih_item_len(&s_ih) / UNFM_P_SIZE -
		     1) * sb->s_blocksize;
		if (pos != pos1)
			reiserfs_panic(sb, "vs-5530", "tail position "
				       "changed while we were reading it");
#endif
	}

	/* Set direct item header to insert. */
	make_le_item_head(&s_ih, NULL, get_inode_item_key_version(inode),
			  pos1 + 1, TYPE_DIRECT, round_tail_len,
			  0xffff /*ih_free_space */ );

	/*
	 * we want a pointer to the first byte of the tail in the page.
	 * the page was locked and this part of the page was up to date when
	 * indirect2direct was called, so we know the bytes are still valid
	 */
	tail = tail + (pos & (PAGE_CACHE_SIZE - 1));

	PATH_LAST_POSITION(path)++;

	key = *item_key;
	set_cpu_key_k_type(&key, TYPE_DIRECT);
	key.key_length = 4;
	/* Insert tail as new direct item in the tree */
	if (reiserfs_insert_item(th, path, &key, &s_ih, inode,
				 tail ? tail : NULL) < 0) {
		/*
		 * No disk memory. So we can not convert last unformatted node
		 * to the direct item.  In this case we used to adjust
		 * indirect items's ih_free_space. Now ih_free_space is not
		 * used, it would be ideal to write zeros to corresponding
		 * unformatted node. For now i_size is considered as guard for
		 * going out of file size
		 */
		kunmap(page);
		return block_size - round_tail_len;
	}
	kunmap(page);

	/* make sure to get the i_blocks changes from reiserfs_insert_item */
	reiserfs_update_sd(th, inode);

	/*
	 * note: we have now the same as in above direct2indirect
	 * conversion: there are two keys which have matching first three
	 * key components. They only differ by the fourth one.
	 */

	/*
	 * We have inserted new direct item and must remove last
	 * unformatted node.
	 */
	*mode = M_CUT;

	/* we store position of first direct item in the in-core inode */
	/* mark_file_with_tail (inode, pos1 + 1); */
	REISERFS_I(inode)->i_first_direct_byte = pos1 + 1;

	return block_size - round_tail_len;
}
