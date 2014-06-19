/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/time.h>
#include <linux/fs.h>
#include "reiserfs.h"
#include "acl.h"
#include "xattr.h"
#include <linux/exportfs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/quotaops.h>
#include <linux/swap.h>
#include <linux/aio.h>

int reiserfs_commit_write(struct file *f, struct page *page,
			  unsigned from, unsigned to);

void reiserfs_evict_inode(struct inode *inode)
{
	/*
	 * We need blocks for transaction + (user+group) quota
	 * update (possibly delete)
	 */
	int jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 2 +
	    2 * REISERFS_QUOTA_INIT_BLOCKS(inode->i_sb);
	struct reiserfs_transaction_handle th;
	int err;

	if (!inode->i_nlink && !is_bad_inode(inode))
		dquot_initialize(inode);

	truncate_inode_pages_final(&inode->i_data);
	if (inode->i_nlink)
		goto no_delete;

	/*
	 * The = 0 happens when we abort creating a new inode
	 * for some reason like lack of space..
	 * also handles bad_inode case
	 */
	if (!(inode->i_state & I_NEW) && INODE_PKEY(inode)->k_objectid != 0) {

		reiserfs_delete_xattrs(inode);

		reiserfs_write_lock(inode->i_sb);

		if (journal_begin(&th, inode->i_sb, jbegin_count))
			goto out;
		reiserfs_update_inode_transaction(inode);

		reiserfs_discard_prealloc(&th, inode);

		err = reiserfs_delete_object(&th, inode);

		/*
		 * Do quota update inside a transaction for journaled quotas.
		 * We must do that after delete_object so that quota updates
		 * go into the same transaction as stat data deletion
		 */
		if (!err) {
			int depth = reiserfs_write_unlock_nested(inode->i_sb);
			dquot_free_inode(inode);
			reiserfs_write_lock_nested(inode->i_sb, depth);
		}

		if (journal_end(&th))
			goto out;

		/*
		 * check return value from reiserfs_delete_object after
		 * ending the transaction
		 */
		if (err)
		    goto out;

		/*
		 * all items of file are deleted, so we can remove
		 * "save" link
		 * we can't do anything about an error here
		 */
		remove_save_link(inode, 0 /* not truncate */);
out:
		reiserfs_write_unlock(inode->i_sb);
	} else {
		/* no object items are in the tree */
		;
	}

	/* note this must go after the journal_end to prevent deadlock */
	clear_inode(inode);

	dquot_drop(inode);
	inode->i_blocks = 0;
	return;

no_delete:
	clear_inode(inode);
	dquot_drop(inode);
}

static void _make_cpu_key(struct cpu_key *key, int version, __u32 dirid,
			  __u32 objectid, loff_t offset, int type, int length)
{
	key->version = version;

	key->on_disk_key.k_dir_id = dirid;
	key->on_disk_key.k_objectid = objectid;
	set_cpu_key_k_offset(key, offset);
	set_cpu_key_k_type(key, type);
	key->key_length = length;
}

/*
 * take base of inode_key (it comes from inode always) (dirid, objectid)
 * and version from an inode, set offset and type of key
 */
void make_cpu_key(struct cpu_key *key, struct inode *inode, loff_t offset,
		  int type, int length)
{
	_make_cpu_key(key, get_inode_item_key_version(inode),
		      le32_to_cpu(INODE_PKEY(inode)->k_dir_id),
		      le32_to_cpu(INODE_PKEY(inode)->k_objectid), offset, type,
		      length);
}

/* when key is 0, do not set version and short key */
inline void make_le_item_head(struct item_head *ih, const struct cpu_key *key,
			      int version,
			      loff_t offset, int type, int length,
			      int entry_count /*or ih_free_space */ )
{
	if (key) {
		ih->ih_key.k_dir_id = cpu_to_le32(key->on_disk_key.k_dir_id);
		ih->ih_key.k_objectid =
		    cpu_to_le32(key->on_disk_key.k_objectid);
	}
	put_ih_version(ih, version);
	set_le_ih_k_offset(ih, offset);
	set_le_ih_k_type(ih, type);
	put_ih_item_len(ih, length);
	/*    set_ih_free_space (ih, 0); */
	/*
	 * for directory items it is entry count, for directs and stat
	 * datas - 0xffff, for indirects - 0
	 */
	put_ih_entry_count(ih, entry_count);
}

/*
 * FIXME: we might cache recently accessed indirect item
 * Ugh.  Not too eager for that....
 * I cut the code until such time as I see a convincing argument (benchmark).
 * I don't want a bloated inode struct..., and I don't like code complexity....
 */

/*
 * cutting the code is fine, since it really isn't in use yet and is easy
 * to add back in.  But, Vladimir has a really good idea here.  Think
 * about what happens for reading a file.  For each page,
 * The VFS layer calls reiserfs_readpage, who searches the tree to find
 * an indirect item.  This indirect item has X number of pointers, where
 * X is a big number if we've done the block allocation right.  But,
 * we only use one or two of these pointers during each call to readpage,
 * needlessly researching again later on.
 *
 * The size of the cache could be dynamic based on the size of the file.
 *
 * I'd also like to see us cache the location the stat data item, since
 * we are needlessly researching for that frequently.
 *
 * --chris
 */

/*
 * If this page has a file tail in it, and
 * it was read in by get_block_create_0, the page data is valid,
 * but tail is still sitting in a direct item, and we can't write to
 * it.  So, look through this page, and check all the mapped buffers
 * to make sure they have valid block numbers.  Any that don't need
 * to be unmapped, so that __block_write_begin will correctly call
 * reiserfs_get_block to convert the tail into an unformatted node
 */
static inline void fix_tail_page_for_writing(struct page *page)
{
	struct buffer_head *head, *next, *bh;

	if (page && page_has_buffers(page)) {
		head = page_buffers(page);
		bh = head;
		do {
			next = bh->b_this_page;
			if (buffer_mapped(bh) && bh->b_blocknr == 0) {
				reiserfs_unmap_buffer(bh);
			}
			bh = next;
		} while (bh != head);
	}
}

/*
 * reiserfs_get_block does not need to allocate a block only if it has been
 * done already or non-hole position has been found in the indirect item
 */
static inline int allocation_needed(int retval, b_blocknr_t allocated,
				    struct item_head *ih,
				    __le32 * item, int pos_in_item)
{
	if (allocated)
		return 0;
	if (retval == POSITION_FOUND && is_indirect_le_ih(ih) &&
	    get_block_num(item, pos_in_item))
		return 0;
	return 1;
}

static inline int indirect_item_found(int retval, struct item_head *ih)
{
	return (retval == POSITION_FOUND) && is_indirect_le_ih(ih);
}

static inline void set_block_dev_mapped(struct buffer_head *bh,
					b_blocknr_t block, struct inode *inode)
{
	map_bh(bh, inode->i_sb, block);
}

/*
 * files which were created in the earlier version can not be longer,
 * than 2 gb
 */
static int file_capable(struct inode *inode, sector_t block)
{
	/* it is new file. */
	if (get_inode_item_key_version(inode) != KEY_FORMAT_3_5 ||
	    /* old file, but 'block' is inside of 2gb */
	    block < (1 << (31 - inode->i_sb->s_blocksize_bits)))
		return 1;

	return 0;
}

static int restart_transaction(struct reiserfs_transaction_handle *th,
			       struct inode *inode, struct treepath *path)
{
	struct super_block *s = th->t_super;
	int err;

	BUG_ON(!th->t_trans_id);
	BUG_ON(!th->t_refcount);

	pathrelse(path);

	/* we cannot restart while nested */
	if (th->t_refcount > 1) {
		return 0;
	}
	reiserfs_update_sd(th, inode);
	err = journal_end(th);
	if (!err) {
		err = journal_begin(th, s, JOURNAL_PER_BALANCE_CNT * 6);
		if (!err)
			reiserfs_update_inode_transaction(inode);
	}
	return err;
}

/*
 * it is called by get_block when create == 0. Returns block number
 * for 'block'-th logical block of file. When it hits direct item it
 * returns 0 (being called from bmap) or read direct item into piece
 * of page (bh_result)
 * Please improve the english/clarity in the comment above, as it is
 * hard to understand.
 */
static int _get_block_create_0(struct inode *inode, sector_t block,
			       struct buffer_head *bh_result, int args)
{
	INITIALIZE_PATH(path);
	struct cpu_key key;
	struct buffer_head *bh;
	struct item_head *ih, tmp_ih;
	b_blocknr_t blocknr;
	char *p = NULL;
	int chars;
	int ret;
	int result;
	int done = 0;
	unsigned long offset;

	/* prepare the key to look for the 'block'-th block of file */
	make_cpu_key(&key, inode,
		     (loff_t) block * inode->i_sb->s_blocksize + 1, TYPE_ANY,
		     3);

	result = search_for_position_by_key(inode->i_sb, &key, &path);
	if (result != POSITION_FOUND) {
		pathrelse(&path);
		if (p)
			kunmap(bh_result->b_page);
		if (result == IO_ERROR)
			return -EIO;
		/*
		 * We do not return -ENOENT if there is a hole but page is
		 * uptodate, because it means that there is some MMAPED data
		 * associated with it that is yet to be written to disk.
		 */
		if ((args & GET_BLOCK_NO_HOLE)
		    && !PageUptodate(bh_result->b_page)) {
			return -ENOENT;
		}
		return 0;
	}

	bh = get_last_bh(&path);
	ih = tp_item_head(&path);
	if (is_indirect_le_ih(ih)) {
		__le32 *ind_item = (__le32 *) ih_item_body(bh, ih);

		/*
		 * FIXME: here we could cache indirect item or part of it in
		 * the inode to avoid search_by_key in case of subsequent
		 * access to file
		 */
		blocknr = get_block_num(ind_item, path.pos_in_item);
		ret = 0;
		if (blocknr) {
			map_bh(bh_result, inode->i_sb, blocknr);
			if (path.pos_in_item ==
			    ((ih_item_len(ih) / UNFM_P_SIZE) - 1)) {
				set_buffer_boundary(bh_result);
			}
		} else
			/*
			 * We do not return -ENOENT if there is a hole but
			 * page is uptodate, because it means that there is
			 * some MMAPED data associated with it that is
			 * yet to be written to disk.
			 */
		if ((args & GET_BLOCK_NO_HOLE)
			    && !PageUptodate(bh_result->b_page)) {
			ret = -ENOENT;
		}

		pathrelse(&path);
		if (p)
			kunmap(bh_result->b_page);
		return ret;
	}
	/* requested data are in direct item(s) */
	if (!(args & GET_BLOCK_READ_DIRECT)) {
		/*
		 * we are called by bmap. FIXME: we can not map block of file
		 * when it is stored in direct item(s)
		 */
		pathrelse(&path);
		if (p)
			kunmap(bh_result->b_page);
		return -ENOENT;
	}

	/*
	 * if we've got a direct item, and the buffer or page was uptodate,
	 * we don't want to pull data off disk again.  skip to the
	 * end, where we map the buffer and return
	 */
	if (buffer_uptodate(bh_result)) {
		goto finished;
	} else
		/*
		 * grab_tail_page can trigger calls to reiserfs_get_block on
		 * up to date pages without any buffers.  If the page is up
		 * to date, we don't want read old data off disk.  Set the up
		 * to date bit on the buffer instead and jump to the end
		 */
	if (!bh_result->b_page || PageUptodate(bh_result->b_page)) {
		set_buffer_uptodate(bh_result);
		goto finished;
	}
	/* read file tail into part of page */
	offset = (cpu_key_k_offset(&key) - 1) & (PAGE_CACHE_SIZE - 1);
	copy_item_head(&tmp_ih, ih);

	/*
	 * we only want to kmap if we are reading the tail into the page.
	 * this is not the common case, so we don't kmap until we are
	 * sure we need to.  But, this means the item might move if
	 * kmap schedules
	 */
	if (!p)
		p = (char *)kmap(bh_result->b_page);

	p += offset;
	memset(p, 0, inode->i_sb->s_blocksize);
	do {
		if (!is_direct_le_ih(ih)) {
			BUG();
		}
		/*
		 * make sure we don't read more bytes than actually exist in
		 * the file.  This can happen in odd cases where i_size isn't
		 * correct, and when direct item padding results in a few
		 * extra bytes at the end of the direct item
		 */
		if ((le_ih_k_offset(ih) + path.pos_in_item) > inode->i_size)
			break;
		if ((le_ih_k_offset(ih) - 1 + ih_item_len(ih)) > inode->i_size) {
			chars =
			    inode->i_size - (le_ih_k_offset(ih) - 1) -
			    path.pos_in_item;
			done = 1;
		} else {
			chars = ih_item_len(ih) - path.pos_in_item;
		}
		memcpy(p, ih_item_body(bh, ih) + path.pos_in_item, chars);

		if (done)
			break;

		p += chars;

		/*
		 * we done, if read direct item is not the last item of
		 * node FIXME: we could try to check right delimiting key
		 * to see whether direct item continues in the right
		 * neighbor or rely on i_size
		 */
		if (PATH_LAST_POSITION(&path) != (B_NR_ITEMS(bh) - 1))
			break;

		/* update key to look for the next piece */
		set_cpu_key_k_offset(&key, cpu_key_k_offset(&key) + chars);
		result = search_for_position_by_key(inode->i_sb, &key, &path);
		if (result != POSITION_FOUND)
			/* i/o error most likely */
			break;
		bh = get_last_bh(&path);
		ih = tp_item_head(&path);
	} while (1);

	flush_dcache_page(bh_result->b_page);
	kunmap(bh_result->b_page);

finished:
	pathrelse(&path);

	if (result == IO_ERROR)
		return -EIO;

	/*
	 * this buffer has valid data, but isn't valid for io.  mapping it to
	 * block #0 tells the rest of reiserfs it just has a tail in it
	 */
	map_bh(bh_result, inode->i_sb, 0);
	set_buffer_uptodate(bh_result);
	return 0;
}

/*
 * this is called to create file map. So, _get_block_create_0 will not
 * read direct item
 */
static int reiserfs_bmap(struct inode *inode, sector_t block,
			 struct buffer_head *bh_result, int create)
{
	if (!file_capable(inode, block))
		return -EFBIG;

	reiserfs_write_lock(inode->i_sb);
	/* do not read the direct item */
	_get_block_create_0(inode, block, bh_result, 0);
	reiserfs_write_unlock(inode->i_sb);
	return 0;
}

/*
 * special version of get_block that is only used by grab_tail_page right
 * now.  It is sent to __block_write_begin, and when you try to get a
 * block past the end of the file (or a block from a hole) it returns
 * -ENOENT instead of a valid buffer.  __block_write_begin expects to
 * be able to do i/o on the buffers returned, unless an error value
 * is also returned.
 *
 * So, this allows __block_write_begin to be used for reading a single block
 * in a page.  Where it does not produce a valid page for holes, or past the
 * end of the file.  This turns out to be exactly what we need for reading
 * tails for conversion.
 *
 * The point of the wrapper is forcing a certain value for create, even
 * though the VFS layer is calling this function with create==1.  If you
 * don't want to send create == GET_BLOCK_NO_HOLE to reiserfs_get_block,
 * don't use this function.
*/
static int reiserfs_get_block_create_0(struct inode *inode, sector_t block,
				       struct buffer_head *bh_result,
				       int create)
{
	return reiserfs_get_block(inode, block, bh_result, GET_BLOCK_NO_HOLE);
}

/*
 * This is special helper for reiserfs_get_block in case we are executing
 * direct_IO request.
 */
static int reiserfs_get_blocks_direct_io(struct inode *inode,
					 sector_t iblock,
					 struct buffer_head *bh_result,
					 int create)
{
	int ret;

	bh_result->b_page = NULL;

	/*
	 * We set the b_size before reiserfs_get_block call since it is
	 * referenced in convert_tail_for_hole() that may be called from
	 * reiserfs_get_block()
	 */
	bh_result->b_size = (1 << inode->i_blkbits);

	ret = reiserfs_get_block(inode, iblock, bh_result,
				 create | GET_BLOCK_NO_DANGLE);
	if (ret)
		goto out;

	/* don't allow direct io onto tail pages */
	if (buffer_mapped(bh_result) && bh_result->b_blocknr == 0) {
		/*
		 * make sure future calls to the direct io funcs for this
		 * offset in the file fail by unmapping the buffer
		 */
		clear_buffer_mapped(bh_result);
		ret = -EINVAL;
	}

	/*
	 * Possible unpacked tail. Flush the data before pages have
	 * disappeared
	 */
	if (REISERFS_I(inode)->i_flags & i_pack_on_close_mask) {
		int err;

		reiserfs_write_lock(inode->i_sb);

		err = reiserfs_commit_for_inode(inode);
		REISERFS_I(inode)->i_flags &= ~i_pack_on_close_mask;

		reiserfs_write_unlock(inode->i_sb);

		if (err < 0)
			ret = err;
	}
out:
	return ret;
}

/*
 * helper function for when reiserfs_get_block is called for a hole
 * but the file tail is still in a direct item
 * bh_result is the buffer head for the hole
 * tail_offset is the offset of the start of the tail in the file
 *
 * This calls prepare_write, which will start a new transaction
 * you should not be in a transaction, or have any paths held when you
 * call this.
 */
static int convert_tail_for_hole(struct inode *inode,
				 struct buffer_head *bh_result,
				 loff_t tail_offset)
{
	unsigned long index;
	unsigned long tail_end;
	unsigned long tail_start;
	struct page *tail_page;
	struct page *hole_page = bh_result->b_page;
	int retval = 0;

	if ((tail_offset & (bh_result->b_size - 1)) != 1)
		return -EIO;

	/* always try to read until the end of the block */
	tail_start = tail_offset & (PAGE_CACHE_SIZE - 1);
	tail_end = (tail_start | (bh_result->b_size - 1)) + 1;

	index = tail_offset >> PAGE_CACHE_SHIFT;
	/*
	 * hole_page can be zero in case of direct_io, we are sure
	 * that we cannot get here if we write with O_DIRECT into tail page
	 */
	if (!hole_page || index != hole_page->index) {
		tail_page = grab_cache_page(inode->i_mapping, index);
		retval = -ENOMEM;
		if (!tail_page) {
			goto out;
		}
	} else {
		tail_page = hole_page;
	}

	/*
	 * we don't have to make sure the conversion did not happen while
	 * we were locking the page because anyone that could convert
	 * must first take i_mutex.
	 *
	 * We must fix the tail page for writing because it might have buffers
	 * that are mapped, but have a block number of 0.  This indicates tail
	 * data that has been read directly into the page, and
	 * __block_write_begin won't trigger a get_block in this case.
	 */
	fix_tail_page_for_writing(tail_page);
	retval = __reiserfs_write_begin(tail_page, tail_start,
				      tail_end - tail_start);
	if (retval)
		goto unlock;

	/* tail conversion might change the data in the page */
	flush_dcache_page(tail_page);

	retval = reiserfs_commit_write(NULL, tail_page, tail_start, tail_end);

unlock:
	if (tail_page != hole_page) {
		unlock_page(tail_page);
		page_cache_release(tail_page);
	}
out:
	return retval;
}

static inline int _allocate_block(struct reiserfs_transaction_handle *th,
				  sector_t block,
				  struct inode *inode,
				  b_blocknr_t * allocated_block_nr,
				  struct treepath *path, int flags)
{
	BUG_ON(!th->t_trans_id);

#ifdef REISERFS_PREALLOCATE
	if (!(flags & GET_BLOCK_NO_IMUX)) {
		return reiserfs_new_unf_blocknrs2(th, inode, allocated_block_nr,
						  path, block);
	}
#endif
	return reiserfs_new_unf_blocknrs(th, inode, allocated_block_nr, path,
					 block);
}

int reiserfs_get_block(struct inode *inode, sector_t block,
		       struct buffer_head *bh_result, int create)
{
	int repeat, retval = 0;
	/* b_blocknr_t is (unsigned) 32 bit int*/
	b_blocknr_t allocated_block_nr = 0;
	INITIALIZE_PATH(path);
	int pos_in_item;
	struct cpu_key key;
	struct buffer_head *bh, *unbh = NULL;
	struct item_head *ih, tmp_ih;
	__le32 *item;
	int done;
	int fs_gen;
	struct reiserfs_transaction_handle *th = NULL;
	/*
	 * space reserved in transaction batch:
	 * . 3 balancings in direct->indirect conversion
	 * . 1 block involved into reiserfs_update_sd()
	 * XXX in practically impossible worst case direct2indirect()
	 * can incur (much) more than 3 balancings.
	 * quota update for user, group
	 */
	int jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 3 + 1 +
	    2 * REISERFS_QUOTA_TRANS_BLOCKS(inode->i_sb);
	int version;
	int dangle = 1;
	loff_t new_offset =
	    (((loff_t) block) << inode->i_sb->s_blocksize_bits) + 1;

	reiserfs_write_lock(inode->i_sb);
	version = get_inode_item_key_version(inode);

	if (!file_capable(inode, block)) {
		reiserfs_write_unlock(inode->i_sb);
		return -EFBIG;
	}

	/*
	 * if !create, we aren't changing the FS, so we don't need to
	 * log anything, so we don't need to start a transaction
	 */
	if (!(create & GET_BLOCK_CREATE)) {
		int ret;
		/* find number of block-th logical block of the file */
		ret = _get_block_create_0(inode, block, bh_result,
					  create | GET_BLOCK_READ_DIRECT);
		reiserfs_write_unlock(inode->i_sb);
		return ret;
	}

	/*
	 * if we're already in a transaction, make sure to close
	 * any new transactions we start in this func
	 */
	if ((create & GET_BLOCK_NO_DANGLE) ||
	    reiserfs_transaction_running(inode->i_sb))
		dangle = 0;

	/*
	 * If file is of such a size, that it might have a tail and
	 * tails are enabled  we should mark it as possibly needing
	 * tail packing on close
	 */
	if ((have_large_tails(inode->i_sb)
	     && inode->i_size < i_block_size(inode) * 4)
	    || (have_small_tails(inode->i_sb)
		&& inode->i_size < i_block_size(inode)))
		REISERFS_I(inode)->i_flags |= i_pack_on_close_mask;

	/* set the key of the first byte in the 'block'-th block of file */
	make_cpu_key(&key, inode, new_offset, TYPE_ANY, 3 /*key length */ );
	if ((new_offset + inode->i_sb->s_blocksize - 1) > inode->i_size) {
start_trans:
		th = reiserfs_persistent_transaction(inode->i_sb, jbegin_count);
		if (!th) {
			retval = -ENOMEM;
			goto failure;
		}
		reiserfs_update_inode_transaction(inode);
	}
research:

	retval = search_for_position_by_key(inode->i_sb, &key, &path);
	if (retval == IO_ERROR) {
		retval = -EIO;
		goto failure;
	}

	bh = get_last_bh(&path);
	ih = tp_item_head(&path);
	item = tp_item_body(&path);
	pos_in_item = path.pos_in_item;

	fs_gen = get_generation(inode->i_sb);
	copy_item_head(&tmp_ih, ih);

	if (allocation_needed
	    (retval, allocated_block_nr, ih, item, pos_in_item)) {
		/* we have to allocate block for the unformatted node */
		if (!th) {
			pathrelse(&path);
			goto start_trans;
		}

		repeat =
		    _allocate_block(th, block, inode, &allocated_block_nr,
				    &path, create);

		/*
		 * restart the transaction to give the journal a chance to free
		 * some blocks.  releases the path, so we have to go back to
		 * research if we succeed on the second try
		 */
		if (repeat == NO_DISK_SPACE || repeat == QUOTA_EXCEEDED) {
			SB_JOURNAL(inode->i_sb)->j_next_async_flush = 1;
			retval = restart_transaction(th, inode, &path);
			if (retval)
				goto failure;
			repeat =
			    _allocate_block(th, block, inode,
					    &allocated_block_nr, NULL, create);

			if (repeat != NO_DISK_SPACE && repeat != QUOTA_EXCEEDED) {
				goto research;
			}
			if (repeat == QUOTA_EXCEEDED)
				retval = -EDQUOT;
			else
				retval = -ENOSPC;
			goto failure;
		}

		if (fs_changed(fs_gen, inode->i_sb)
		    && item_moved(&tmp_ih, &path)) {
			goto research;
		}
	}

	if (indirect_item_found(retval, ih)) {
		b_blocknr_t unfm_ptr;
		/*
		 * 'block'-th block is in the file already (there is
		 * corresponding cell in some indirect item). But it may be
		 * zero unformatted node pointer (hole)
		 */
		unfm_ptr = get_block_num(item, pos_in_item);
		if (unfm_ptr == 0) {
			/* use allocated block to plug the hole */
			reiserfs_prepare_for_journal(inode->i_sb, bh, 1);
			if (fs_changed(fs_gen, inode->i_sb)
			    && item_moved(&tmp_ih, &path)) {
				reiserfs_restore_prepared_buffer(inode->i_sb,
								 bh);
				goto research;
			}
			set_buffer_new(bh_result);
			if (buffer_dirty(bh_result)
			    && reiserfs_data_ordered(inode->i_sb))
				reiserfs_add_ordered_list(inode, bh_result);
			put_block_num(item, pos_in_item, allocated_block_nr);
			unfm_ptr = allocated_block_nr;
			journal_mark_dirty(th, bh);
			reiserfs_update_sd(th, inode);
		}
		set_block_dev_mapped(bh_result, unfm_ptr, inode);
		pathrelse(&path);
		retval = 0;
		if (!dangle && th)
			retval = reiserfs_end_persistent_transaction(th);

		reiserfs_write_unlock(inode->i_sb);

		/*
		 * the item was found, so new blocks were not added to the file
		 * there is no need to make sure the inode is updated with this
		 * transaction
		 */
		return retval;
	}

	if (!th) {
		pathrelse(&path);
		goto start_trans;
	}

	/*
	 * desired position is not found or is in the direct item. We have
	 * to append file with holes up to 'block'-th block converting
	 * direct items to indirect one if necessary
	 */
	done = 0;
	do {
		if (is_statdata_le_ih(ih)) {
			__le32 unp = 0;
			struct cpu_key tmp_key;

			/* indirect item has to be inserted */
			make_le_item_head(&tmp_ih, &key, version, 1,
					  TYPE_INDIRECT, UNFM_P_SIZE,
					  0 /* free_space */ );

			/*
			 * we are going to add 'block'-th block to the file.
			 * Use allocated block for that
			 */
			if (cpu_key_k_offset(&key) == 1) {
				unp = cpu_to_le32(allocated_block_nr);
				set_block_dev_mapped(bh_result,
						     allocated_block_nr, inode);
				set_buffer_new(bh_result);
				done = 1;
			}
			tmp_key = key;	/* ;) */
			set_cpu_key_k_offset(&tmp_key, 1);
			PATH_LAST_POSITION(&path)++;

			retval =
			    reiserfs_insert_item(th, &path, &tmp_key, &tmp_ih,
						 inode, (char *)&unp);
			if (retval) {
				reiserfs_free_block(th, inode,
						    allocated_block_nr, 1);
				/*
				 * retval == -ENOSPC, -EDQUOT or -EIO
				 * or -EEXIST
				 */
				goto failure;
			}
		} else if (is_direct_le_ih(ih)) {
			/* direct item has to be converted */
			loff_t tail_offset;

			tail_offset =
			    ((le_ih_k_offset(ih) -
			      1) & ~(inode->i_sb->s_blocksize - 1)) + 1;

			/*
			 * direct item we just found fits into block we have
			 * to map. Convert it into unformatted node: use
			 * bh_result for the conversion
			 */
			if (tail_offset == cpu_key_k_offset(&key)) {
				set_block_dev_mapped(bh_result,
						     allocated_block_nr, inode);
				unbh = bh_result;
				done = 1;
			} else {
				/*
				 * we have to pad file tail stored in direct
				 * item(s) up to block size and convert it
				 * to unformatted node. FIXME: this should
				 * also get into page cache
				 */

				pathrelse(&path);
				/*
				 * ugly, but we can only end the transaction if
				 * we aren't nested
				 */
				BUG_ON(!th->t_refcount);
				if (th->t_refcount == 1) {
					retval =
					    reiserfs_end_persistent_transaction
					    (th);
					th = NULL;
					if (retval)
						goto failure;
				}

				retval =
				    convert_tail_for_hole(inode, bh_result,
							  tail_offset);
				if (retval) {
					if (retval != -ENOSPC)
						reiserfs_error(inode->i_sb,
							"clm-6004",
							"convert tail failed "
							"inode %lu, error %d",
							inode->i_ino,
							retval);
					if (allocated_block_nr) {
						/*
						 * the bitmap, the super,
						 * and the stat data == 3
						 */
						if (!th)
							th = reiserfs_persistent_transaction(inode->i_sb, 3);
						if (th)
							reiserfs_free_block(th,
									    inode,
									    allocated_block_nr,
									    1);
					}
					goto failure;
				}
				goto research;
			}
			retval =
			    direct2indirect(th, inode, &path, unbh,
					    tail_offset);
			if (retval) {
				reiserfs_unmap_buffer(unbh);
				reiserfs_free_block(th, inode,
						    allocated_block_nr, 1);
				goto failure;
			}
			/*
			 * it is important the set_buffer_uptodate is done
			 * after the direct2indirect.  The buffer might
			 * contain valid data newer than the data on disk
			 * (read by readpage, changed, and then sent here by
			 * writepage).  direct2indirect needs to know if unbh
			 * was already up to date, so it can decide if the
			 * data in unbh needs to be replaced with data from
			 * the disk
			 */
			set_buffer_uptodate(unbh);

			/*
			 * unbh->b_page == NULL in case of DIRECT_IO request,
			 * this means buffer will disappear shortly, so it
			 * should not be added to
			 */
			if (unbh->b_page) {
				/*
				 * we've converted the tail, so we must
				 * flush unbh before the transaction commits
				 */
				reiserfs_add_tail_list(inode, unbh);

				/*
				 * mark it dirty now to prevent commit_write
				 * from adding this buffer to the inode's
				 * dirty buffer list
				 */
				/*
				 * AKPM: changed __mark_buffer_dirty to
				 * mark_buffer_dirty().  It's still atomic,
				 * but it sets the page dirty too, which makes
				 * it eligible for writeback at any time by the
				 * VM (which was also the case with
				 * __mark_buffer_dirty())
				 */
				mark_buffer_dirty(unbh);
			}
		} else {
			/*
			 * append indirect item with holes if needed, when
			 * appending pointer to 'block'-th block use block,
			 * which is already allocated
			 */
			struct cpu_key tmp_key;
			/*
			 * We use this in case we need to allocate
			 * only one block which is a fastpath
			 */
			unp_t unf_single = 0;
			unp_t *un;
			__u64 max_to_insert =
			    MAX_ITEM_LEN(inode->i_sb->s_blocksize) /
			    UNFM_P_SIZE;
			__u64 blocks_needed;

			RFALSE(pos_in_item != ih_item_len(ih) / UNFM_P_SIZE,
			       "vs-804: invalid position for append");
			/*
			 * indirect item has to be appended,
			 * set up key of that position
			 * (key type is unimportant)
			 */
			make_cpu_key(&tmp_key, inode,
				     le_key_k_offset(version,
						     &ih->ih_key) +
				     op_bytes_number(ih,
						     inode->i_sb->s_blocksize),
				     TYPE_INDIRECT, 3);

			RFALSE(cpu_key_k_offset(&tmp_key) > cpu_key_k_offset(&key),
			       "green-805: invalid offset");
			blocks_needed =
			    1 +
			    ((cpu_key_k_offset(&key) -
			      cpu_key_k_offset(&tmp_key)) >> inode->i_sb->
			     s_blocksize_bits);

			if (blocks_needed == 1) {
				un = &unf_single;
			} else {
				un = kzalloc(min(blocks_needed, max_to_insert) * UNFM_P_SIZE, GFP_NOFS);
				if (!un) {
					un = &unf_single;
					blocks_needed = 1;
					max_to_insert = 0;
				}
			}
			if (blocks_needed <= max_to_insert) {
				/*
				 * we are going to add target block to
				 * the file. Use allocated block for that
				 */
				un[blocks_needed - 1] =
				    cpu_to_le32(allocated_block_nr);
				set_block_dev_mapped(bh_result,
						     allocated_block_nr, inode);
				set_buffer_new(bh_result);
				done = 1;
			} else {
				/* paste hole to the indirect item */
				/*
				 * If kmalloc failed, max_to_insert becomes
				 * zero and it means we only have space for
				 * one block
				 */
				blocks_needed =
				    max_to_insert ? max_to_insert : 1;
			}
			retval =
			    reiserfs_paste_into_item(th, &path, &tmp_key, inode,
						     (char *)un,
						     UNFM_P_SIZE *
						     blocks_needed);

			if (blocks_needed != 1)
				kfree(un);

			if (retval) {
				reiserfs_free_block(th, inode,
						    allocated_block_nr, 1);
				goto failure;
			}
			if (!done) {
				/*
				 * We need to mark new file size in case
				 * this function will be interrupted/aborted
				 * later on. And we may do this only for
				 * holes.
				 */
				inode->i_size +=
				    inode->i_sb->s_blocksize * blocks_needed;
			}
		}

		if (done == 1)
			break;

		/*
		 * this loop could log more blocks than we had originally
		 * asked for.  So, we have to allow the transaction to end
		 * if it is too big or too full.  Update the inode so things
		 * are consistent if we crash before the function returns
		 * release the path so that anybody waiting on the path before
		 * ending their transaction will be able to continue.
		 */
		if (journal_transaction_should_end(th, th->t_blocks_allocated)) {
			retval = restart_transaction(th, inode, &path);
			if (retval)
				goto failure;
		}
		/*
		 * inserting indirect pointers for a hole can take a
		 * long time.  reschedule if needed and also release the write
		 * lock for others.
		 */
		reiserfs_cond_resched(inode->i_sb);

		retval = search_for_position_by_key(inode->i_sb, &key, &path);
		if (retval == IO_ERROR) {
			retval = -EIO;
			goto failure;
		}
		if (retval == POSITION_FOUND) {
			reiserfs_warning(inode->i_sb, "vs-825",
					 "%K should not be found", &key);
			retval = -EEXIST;
			if (allocated_block_nr)
				reiserfs_free_block(th, inode,
						    allocated_block_nr, 1);
			pathrelse(&path);
			goto failure;
		}
		bh = get_last_bh(&path);
		ih = tp_item_head(&path);
		item = tp_item_body(&path);
		pos_in_item = path.pos_in_item;
	} while (1);

	retval = 0;

failure:
	if (th && (!dangle || (retval && !th->t_trans_id))) {
		int err;
		if (th->t_trans_id)
			reiserfs_update_sd(th, inode);
		err = reiserfs_end_persistent_transaction(th);
		if (err)
			retval = err;
	}

	reiserfs_write_unlock(inode->i_sb);
	reiserfs_check_path(&path);
	return retval;
}

static int
reiserfs_readpages(struct file *file, struct address_space *mapping,
		   struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, reiserfs_get_block);
}

/*
 * Compute real number of used bytes by file
 * Following three functions can go away when we'll have enough space in
 * stat item
 */
static int real_space_diff(struct inode *inode, int sd_size)
{
	int bytes;
	loff_t blocksize = inode->i_sb->s_blocksize;

	if (S_ISLNK(inode->i_mode) || S_ISDIR(inode->i_mode))
		return sd_size;

	/*
	 * End of file is also in full block with indirect reference, so round
	 * up to the next block.
	 *
	 * there is just no way to know if the tail is actually packed
	 * on the file, so we have to assume it isn't.  When we pack the
	 * tail, we add 4 bytes to pretend there really is an unformatted
	 * node pointer
	 */
	bytes =
	    ((inode->i_size +
	      (blocksize - 1)) >> inode->i_sb->s_blocksize_bits) * UNFM_P_SIZE +
	    sd_size;
	return bytes;
}

static inline loff_t to_real_used_space(struct inode *inode, ulong blocks,
					int sd_size)
{
	if (S_ISLNK(inode->i_mode) || S_ISDIR(inode->i_mode)) {
		return inode->i_size +
		    (loff_t) (real_space_diff(inode, sd_size));
	}
	return ((loff_t) real_space_diff(inode, sd_size)) +
	    (((loff_t) blocks) << 9);
}

/* Compute number of blocks used by file in ReiserFS counting */
static inline ulong to_fake_used_blocks(struct inode *inode, int sd_size)
{
	loff_t bytes = inode_get_bytes(inode);
	loff_t real_space = real_space_diff(inode, sd_size);

	/* keeps fsck and non-quota versions of reiserfs happy */
	if (S_ISLNK(inode->i_mode) || S_ISDIR(inode->i_mode)) {
		bytes += (loff_t) 511;
	}

	/*
	 * files from before the quota patch might i_blocks such that
	 * bytes < real_space.  Deal with that here to prevent it from
	 * going negative.
	 */
	if (bytes < real_space)
		return 0;
	return (bytes - real_space) >> 9;
}

/*
 * BAD: new directories have stat data of new type and all other items
 * of old type. Version stored in the inode says about body items, so
 * in update_stat_data we can not rely on inode, but have to check
 * item version directly
 */

/* called by read_locked_inode */
static void init_inode(struct inode *inode, struct treepath *path)
{
	struct buffer_head *bh;
	struct item_head *ih;
	__u32 rdev;

	bh = PATH_PLAST_BUFFER(path);
	ih = tp_item_head(path);

	copy_key(INODE_PKEY(inode), &ih->ih_key);

	INIT_LIST_HEAD(&REISERFS_I(inode)->i_prealloc_list);
	REISERFS_I(inode)->i_flags = 0;
	REISERFS_I(inode)->i_prealloc_block = 0;
	REISERFS_I(inode)->i_prealloc_count = 0;
	REISERFS_I(inode)->i_trans_id = 0;
	REISERFS_I(inode)->i_jl = NULL;
	reiserfs_init_xattr_rwsem(inode);

	if (stat_data_v1(ih)) {
		struct stat_data_v1 *sd =
		    (struct stat_data_v1 *)ih_item_body(bh, ih);
		unsigned long blocks;

		set_inode_item_key_version(inode, KEY_FORMAT_3_5);
		set_inode_sd_version(inode, STAT_DATA_V1);
		inode->i_mode = sd_v1_mode(sd);
		set_nlink(inode, sd_v1_nlink(sd));
		i_uid_write(inode, sd_v1_uid(sd));
		i_gid_write(inode, sd_v1_gid(sd));
		inode->i_size = sd_v1_size(sd);
		inode->i_atime.tv_sec = sd_v1_atime(sd);
		inode->i_mtime.tv_sec = sd_v1_mtime(sd);
		inode->i_ctime.tv_sec = sd_v1_ctime(sd);
		inode->i_atime.tv_nsec = 0;
		inode->i_ctime.tv_nsec = 0;
		inode->i_mtime.tv_nsec = 0;

		inode->i_blocks = sd_v1_blocks(sd);
		inode->i_generation = le32_to_cpu(INODE_PKEY(inode)->k_dir_id);
		blocks = (inode->i_size + 511) >> 9;
		blocks = _ROUND_UP(blocks, inode->i_sb->s_blocksize >> 9);

		/*
		 * there was a bug in <=3.5.23 when i_blocks could take
		 * negative values. Starting from 3.5.17 this value could
		 * even be stored in stat data. For such files we set
		 * i_blocks based on file size. Just 2 notes: this can be
		 * wrong for sparse files. On-disk value will be only
		 * updated if file's inode will ever change
		 */
		if (inode->i_blocks > blocks) {
			inode->i_blocks = blocks;
		}

		rdev = sd_v1_rdev(sd);
		REISERFS_I(inode)->i_first_direct_byte =
		    sd_v1_first_direct_byte(sd);

		/*
		 * an early bug in the quota code can give us an odd
		 * number for the block count.  This is incorrect, fix it here.
		 */
		if (inode->i_blocks & 1) {
			inode->i_blocks++;
		}
		inode_set_bytes(inode,
				to_real_used_space(inode, inode->i_blocks,
						   SD_V1_SIZE));
		/*
		 * nopack is initially zero for v1 objects. For v2 objects,
		 * nopack is initialised from sd_attrs
		 */
		REISERFS_I(inode)->i_flags &= ~i_nopack_mask;
	} else {
		/*
		 * new stat data found, but object may have old items
		 * (directories and symlinks)
		 */
		struct stat_data *sd = (struct stat_data *)ih_item_body(bh, ih);

		inode->i_mode = sd_v2_mode(sd);
		set_nlink(inode, sd_v2_nlink(sd));
		i_uid_write(inode, sd_v2_uid(sd));
		inode->i_size = sd_v2_size(sd);
		i_gid_write(inode, sd_v2_gid(sd));
		inode->i_mtime.tv_sec = sd_v2_mtime(sd);
		inode->i_atime.tv_sec = sd_v2_atime(sd);
		inode->i_ctime.tv_sec = sd_v2_ctime(sd);
		inode->i_ctime.tv_nsec = 0;
		inode->i_mtime.tv_nsec = 0;
		inode->i_atime.tv_nsec = 0;
		inode->i_blocks = sd_v2_blocks(sd);
		rdev = sd_v2_rdev(sd);
		if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
			inode->i_generation =
			    le32_to_cpu(INODE_PKEY(inode)->k_dir_id);
		else
			inode->i_generation = sd_v2_generation(sd);

		if (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode))
			set_inode_item_key_version(inode, KEY_FORMAT_3_5);
		else
			set_inode_item_key_version(inode, KEY_FORMAT_3_6);
		REISERFS_I(inode)->i_first_direct_byte = 0;
		set_inode_sd_version(inode, STAT_DATA_V2);
		inode_set_bytes(inode,
				to_real_used_space(inode, inode->i_blocks,
						   SD_V2_SIZE));
		/*
		 * read persistent inode attributes from sd and initialise
		 * generic inode flags from them
		 */
		REISERFS_I(inode)->i_attrs = sd_v2_attrs(sd);
		sd_attrs_to_i_attrs(sd_v2_attrs(sd), inode);
	}

	pathrelse(path);
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &reiserfs_file_inode_operations;
		inode->i_fop = &reiserfs_file_operations;
		inode->i_mapping->a_ops = &reiserfs_address_space_operations;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &reiserfs_dir_inode_operations;
		inode->i_fop = &reiserfs_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &reiserfs_symlink_inode_operations;
		inode->i_mapping->a_ops = &reiserfs_address_space_operations;
	} else {
		inode->i_blocks = 0;
		inode->i_op = &reiserfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, new_decode_dev(rdev));
	}
}

/* update new stat data with inode fields */
static void inode2sd(void *sd, struct inode *inode, loff_t size)
{
	struct stat_data *sd_v2 = (struct stat_data *)sd;
	__u16 flags;

	set_sd_v2_mode(sd_v2, inode->i_mode);
	set_sd_v2_nlink(sd_v2, inode->i_nlink);
	set_sd_v2_uid(sd_v2, i_uid_read(inode));
	set_sd_v2_size(sd_v2, size);
	set_sd_v2_gid(sd_v2, i_gid_read(inode));
	set_sd_v2_mtime(sd_v2, inode->i_mtime.tv_sec);
	set_sd_v2_atime(sd_v2, inode->i_atime.tv_sec);
	set_sd_v2_ctime(sd_v2, inode->i_ctime.tv_sec);
	set_sd_v2_blocks(sd_v2, to_fake_used_blocks(inode, SD_V2_SIZE));
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		set_sd_v2_rdev(sd_v2, new_encode_dev(inode->i_rdev));
	else
		set_sd_v2_generation(sd_v2, inode->i_generation);
	flags = REISERFS_I(inode)->i_attrs;
	i_attrs_to_sd_attrs(inode, &flags);
	set_sd_v2_attrs(sd_v2, flags);
}

/* used to copy inode's fields to old stat data */
static void inode2sd_v1(void *sd, struct inode *inode, loff_t size)
{
	struct stat_data_v1 *sd_v1 = (struct stat_data_v1 *)sd;

	set_sd_v1_mode(sd_v1, inode->i_mode);
	set_sd_v1_uid(sd_v1, i_uid_read(inode));
	set_sd_v1_gid(sd_v1, i_gid_read(inode));
	set_sd_v1_nlink(sd_v1, inode->i_nlink);
	set_sd_v1_size(sd_v1, size);
	set_sd_v1_atime(sd_v1, inode->i_atime.tv_sec);
	set_sd_v1_ctime(sd_v1, inode->i_ctime.tv_sec);
	set_sd_v1_mtime(sd_v1, inode->i_mtime.tv_sec);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		set_sd_v1_rdev(sd_v1, new_encode_dev(inode->i_rdev));
	else
		set_sd_v1_blocks(sd_v1, to_fake_used_blocks(inode, SD_V1_SIZE));

	/* Sigh. i_first_direct_byte is back */
	set_sd_v1_first_direct_byte(sd_v1,
				    REISERFS_I(inode)->i_first_direct_byte);
}

/*
 * NOTE, you must prepare the buffer head before sending it here,
 * and then log it after the call
 */
static void update_stat_data(struct treepath *path, struct inode *inode,
			     loff_t size)
{
	struct buffer_head *bh;
	struct item_head *ih;

	bh = PATH_PLAST_BUFFER(path);
	ih = tp_item_head(path);

	if (!is_statdata_le_ih(ih))
		reiserfs_panic(inode->i_sb, "vs-13065", "key %k, found item %h",
			       INODE_PKEY(inode), ih);

	/* path points to old stat data */
	if (stat_data_v1(ih)) {
		inode2sd_v1(ih_item_body(bh, ih), inode, size);
	} else {
		inode2sd(ih_item_body(bh, ih), inode, size);
	}

	return;
}

void reiserfs_update_sd_size(struct reiserfs_transaction_handle *th,
			     struct inode *inode, loff_t size)
{
	struct cpu_key key;
	INITIALIZE_PATH(path);
	struct buffer_head *bh;
	int fs_gen;
	struct item_head *ih, tmp_ih;
	int retval;

	BUG_ON(!th->t_trans_id);

	/* key type is unimportant */
	make_cpu_key(&key, inode, SD_OFFSET, TYPE_STAT_DATA, 3);

	for (;;) {
		int pos;
		/* look for the object's stat data */
		retval = search_item(inode->i_sb, &key, &path);
		if (retval == IO_ERROR) {
			reiserfs_error(inode->i_sb, "vs-13050",
				       "i/o failure occurred trying to "
				       "update %K stat data", &key);
			return;
		}
		if (retval == ITEM_NOT_FOUND) {
			pos = PATH_LAST_POSITION(&path);
			pathrelse(&path);
			if (inode->i_nlink == 0) {
				/*reiserfs_warning (inode->i_sb, "vs-13050: reiserfs_update_sd: i_nlink == 0, stat data not found"); */
				return;
			}
			reiserfs_warning(inode->i_sb, "vs-13060",
					 "stat data of object %k (nlink == %d) "
					 "not found (pos %d)",
					 INODE_PKEY(inode), inode->i_nlink,
					 pos);
			reiserfs_check_path(&path);
			return;
		}

		/*
		 * sigh, prepare_for_journal might schedule.  When it
		 * schedules the FS might change.  We have to detect that,
		 * and loop back to the search if the stat data item has moved
		 */
		bh = get_last_bh(&path);
		ih = tp_item_head(&path);
		copy_item_head(&tmp_ih, ih);
		fs_gen = get_generation(inode->i_sb);
		reiserfs_prepare_for_journal(inode->i_sb, bh, 1);

		/* Stat_data item has been moved after scheduling. */
		if (fs_changed(fs_gen, inode->i_sb)
		    && item_moved(&tmp_ih, &path)) {
			reiserfs_restore_prepared_buffer(inode->i_sb, bh);
			continue;
		}
		break;
	}
	update_stat_data(&path, inode, size);
	journal_mark_dirty(th, bh);
	pathrelse(&path);
	return;
}

/*
 * reiserfs_read_locked_inode is called to read the inode off disk, and it
 * does a make_bad_inode when things go wrong.  But, we need to make sure
 * and clear the key in the private portion of the inode, otherwise a
 * corresponding iput might try to delete whatever object the inode last
 * represented.
 */
static void reiserfs_make_bad_inode(struct inode *inode)
{
	memset(INODE_PKEY(inode), 0, KEY_SIZE);
	make_bad_inode(inode);
}

/*
 * initially this function was derived from minix or ext2's analog and
 * evolved as the prototype did
 */
int reiserfs_init_locked_inode(struct inode *inode, void *p)
{
	struct reiserfs_iget_args *args = (struct reiserfs_iget_args *)p;
	inode->i_ino = args->objectid;
	INODE_PKEY(inode)->k_dir_id = cpu_to_le32(args->dirid);
	return 0;
}

/*
 * looks for stat data in the tree, and fills up the fields of in-core
 * inode stat data fields
 */
void reiserfs_read_locked_inode(struct inode *inode,
				struct reiserfs_iget_args *args)
{
	INITIALIZE_PATH(path_to_sd);
	struct cpu_key key;
	unsigned long dirino;
	int retval;

	dirino = args->dirid;

	/*
	 * set version 1, version 2 could be used too, because stat data
	 * key is the same in both versions
	 */
	key.version = KEY_FORMAT_3_5;
	key.on_disk_key.k_dir_id = dirino;
	key.on_disk_key.k_objectid = inode->i_ino;
	key.on_disk_key.k_offset = 0;
	key.on_disk_key.k_type = 0;

	/* look for the object's stat data */
	retval = search_item(inode->i_sb, &key, &path_to_sd);
	if (retval == IO_ERROR) {
		reiserfs_error(inode->i_sb, "vs-13070",
			       "i/o failure occurred trying to find "
			       "stat data of %K", &key);
		reiserfs_make_bad_inode(inode);
		return;
	}

	/* a stale NFS handle can trigger this without it being an error */
	if (retval != ITEM_FOUND) {
		pathrelse(&path_to_sd);
		reiserfs_make_bad_inode(inode);
		clear_nlink(inode);
		return;
	}

	init_inode(inode, &path_to_sd);

	/*
	 * It is possible that knfsd is trying to access inode of a file
	 * that is being removed from the disk by some other thread. As we
	 * update sd on unlink all that is required is to check for nlink
	 * here. This bug was first found by Sizif when debugging
	 * SquidNG/Butterfly, forgotten, and found again after Philippe
	 * Gramoulle <philippe.gramoulle@mmania.com> reproduced it.

	 * More logical fix would require changes in fs/inode.c:iput() to
	 * remove inode from hash-table _after_ fs cleaned disk stuff up and
	 * in iget() to return NULL if I_FREEING inode is found in
	 * hash-table.
	 */

	/*
	 * Currently there is one place where it's ok to meet inode with
	 * nlink==0: processing of open-unlinked and half-truncated files
	 * during mount (fs/reiserfs/super.c:finish_unfinished()).
	 */
	if ((inode->i_nlink == 0) &&
	    !REISERFS_SB(inode->i_sb)->s_is_unlinked_ok) {
		reiserfs_warning(inode->i_sb, "vs-13075",
				 "dead inode read from disk %K. "
				 "This is likely to be race with knfsd. Ignore",
				 &key);
		reiserfs_make_bad_inode(inode);
	}

	/* init inode should be relsing */
	reiserfs_check_path(&path_to_sd);

	/*
	 * Stat data v1 doesn't support ACLs.
	 */
	if (get_inode_sd_version(inode) == STAT_DATA_V1)
		cache_no_acl(inode);
}

/*
 * reiserfs_find_actor() - "find actor" reiserfs supplies to iget5_locked().
 *
 * @inode:    inode from hash table to check
 * @opaque:   "cookie" passed to iget5_locked(). This is &reiserfs_iget_args.
 *
 * This function is called by iget5_locked() to distinguish reiserfs inodes
 * having the same inode numbers. Such inodes can only exist due to some
 * error condition. One of them should be bad. Inodes with identical
 * inode numbers (objectids) are distinguished by parent directory ids.
 *
 */
int reiserfs_find_actor(struct inode *inode, void *opaque)
{
	struct reiserfs_iget_args *args;

	args = opaque;
	/* args is already in CPU order */
	return (inode->i_ino == args->objectid) &&
	    (le32_to_cpu(INODE_PKEY(inode)->k_dir_id) == args->dirid);
}

struct inode *reiserfs_iget(struct super_block *s, const struct cpu_key *key)
{
	struct inode *inode;
	struct reiserfs_iget_args args;
	int depth;

	args.objectid = key->on_disk_key.k_objectid;
	args.dirid = key->on_disk_key.k_dir_id;
	depth = reiserfs_write_unlock_nested(s);
	inode = iget5_locked(s, key->on_disk_key.k_objectid,
			     reiserfs_find_actor, reiserfs_init_locked_inode,
			     (void *)(&args));
	reiserfs_write_lock_nested(s, depth);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		reiserfs_read_locked_inode(inode, &args);
		unlock_new_inode(inode);
	}

	if (comp_short_keys(INODE_PKEY(inode), key) || is_bad_inode(inode)) {
		/* either due to i/o error or a stale NFS handle */
		iput(inode);
		inode = NULL;
	}
	return inode;
}

static struct dentry *reiserfs_get_dentry(struct super_block *sb,
	u32 objectid, u32 dir_id, u32 generation)

{
	struct cpu_key key;
	struct inode *inode;

	key.on_disk_key.k_objectid = objectid;
	key.on_disk_key.k_dir_id = dir_id;
	reiserfs_write_lock(sb);
	inode = reiserfs_iget(sb, &key);
	if (inode && !IS_ERR(inode) && generation != 0 &&
	    generation != inode->i_generation) {
		iput(inode);
		inode = NULL;
	}
	reiserfs_write_unlock(sb);

	return d_obtain_alias(inode);
}

struct dentry *reiserfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	/*
	 * fhtype happens to reflect the number of u32s encoded.
	 * due to a bug in earlier code, fhtype might indicate there
	 * are more u32s then actually fitted.
	 * so if fhtype seems to be more than len, reduce fhtype.
	 * Valid types are:
	 *   2 - objectid + dir_id - legacy support
	 *   3 - objectid + dir_id + generation
	 *   4 - objectid + dir_id + objectid and dirid of parent - legacy
	 *   5 - objectid + dir_id + generation + objectid and dirid of parent
	 *   6 - as above plus generation of directory
	 * 6 does not fit in NFSv2 handles
	 */
	if (fh_type > fh_len) {
		if (fh_type != 6 || fh_len != 5)
			reiserfs_warning(sb, "reiserfs-13077",
				"nfsd/reiserfs, fhtype=%d, len=%d - odd",
				fh_type, fh_len);
		fh_type = fh_len;
	}
	if (fh_len < 2)
		return NULL;

	return reiserfs_get_dentry(sb, fid->raw[0], fid->raw[1],
		(fh_type == 3 || fh_type >= 5) ? fid->raw[2] : 0);
}

struct dentry *reiserfs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	if (fh_type > fh_len)
		fh_type = fh_len;
	if (fh_type < 4)
		return NULL;

	return reiserfs_get_dentry(sb,
		(fh_type >= 5) ? fid->raw[3] : fid->raw[2],
		(fh_type >= 5) ? fid->raw[4] : fid->raw[3],
		(fh_type == 6) ? fid->raw[5] : 0);
}

int reiserfs_encode_fh(struct inode *inode, __u32 * data, int *lenp,
		       struct inode *parent)
{
	int maxlen = *lenp;

	if (parent && (maxlen < 5)) {
		*lenp = 5;
		return FILEID_INVALID;
	} else if (maxlen < 3) {
		*lenp = 3;
		return FILEID_INVALID;
	}

	data[0] = inode->i_ino;
	data[1] = le32_to_cpu(INODE_PKEY(inode)->k_dir_id);
	data[2] = inode->i_generation;
	*lenp = 3;
	if (parent) {
		data[3] = parent->i_ino;
		data[4] = le32_to_cpu(INODE_PKEY(parent)->k_dir_id);
		*lenp = 5;
		if (maxlen >= 6) {
			data[5] = parent->i_generation;
			*lenp = 6;
		}
	}
	return *lenp;
}

/*
 * looks for stat data, then copies fields to it, marks the buffer
 * containing stat data as dirty
 */
/*
 * reiserfs inodes are never really dirty, since the dirty inode call
 * always logs them.  This call allows the VFS inode marking routines
 * to properly mark inodes for datasync and such, but only actually
 * does something when called for a synchronous update.
 */
int reiserfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct reiserfs_transaction_handle th;
	int jbegin_count = 1;

	if (inode->i_sb->s_flags & MS_RDONLY)
		return -EROFS;
	/*
	 * memory pressure can sometimes initiate write_inode calls with
	 * sync == 1,
	 * these cases are just when the system needs ram, not when the
	 * inode needs to reach disk for safety, and they can safely be
	 * ignored because the altered inode has already been logged.
	 */
	if (wbc->sync_mode == WB_SYNC_ALL && !(current->flags & PF_MEMALLOC)) {
		reiserfs_write_lock(inode->i_sb);
		if (!journal_begin(&th, inode->i_sb, jbegin_count)) {
			reiserfs_update_sd(&th, inode);
			journal_end_sync(&th);
		}
		reiserfs_write_unlock(inode->i_sb);
	}
	return 0;
}

/*
 * stat data of new object is inserted already, this inserts the item
 * containing "." and ".." entries
 */
static int reiserfs_new_directory(struct reiserfs_transaction_handle *th,
				  struct inode *inode,
				  struct item_head *ih, struct treepath *path,
				  struct inode *dir)
{
	struct super_block *sb = th->t_super;
	char empty_dir[EMPTY_DIR_SIZE];
	char *body = empty_dir;
	struct cpu_key key;
	int retval;

	BUG_ON(!th->t_trans_id);

	_make_cpu_key(&key, KEY_FORMAT_3_5, le32_to_cpu(ih->ih_key.k_dir_id),
		      le32_to_cpu(ih->ih_key.k_objectid), DOT_OFFSET,
		      TYPE_DIRENTRY, 3 /*key length */ );

	/*
	 * compose item head for new item. Directories consist of items of
	 * old type (ITEM_VERSION_1). Do not set key (second arg is 0), it
	 * is done by reiserfs_new_inode
	 */
	if (old_format_only(sb)) {
		make_le_item_head(ih, NULL, KEY_FORMAT_3_5, DOT_OFFSET,
				  TYPE_DIRENTRY, EMPTY_DIR_SIZE_V1, 2);

		make_empty_dir_item_v1(body, ih->ih_key.k_dir_id,
				       ih->ih_key.k_objectid,
				       INODE_PKEY(dir)->k_dir_id,
				       INODE_PKEY(dir)->k_objectid);
	} else {
		make_le_item_head(ih, NULL, KEY_FORMAT_3_5, DOT_OFFSET,
				  TYPE_DIRENTRY, EMPTY_DIR_SIZE, 2);

		make_empty_dir_item(body, ih->ih_key.k_dir_id,
				    ih->ih_key.k_objectid,
				    INODE_PKEY(dir)->k_dir_id,
				    INODE_PKEY(dir)->k_objectid);
	}

	/* look for place in the tree for new item */
	retval = search_item(sb, &key, path);
	if (retval == IO_ERROR) {
		reiserfs_error(sb, "vs-13080",
			       "i/o failure occurred creating new directory");
		return -EIO;
	}
	if (retval == ITEM_FOUND) {
		pathrelse(path);
		reiserfs_warning(sb, "vs-13070",
				 "object with this key exists (%k)",
				 &(ih->ih_key));
		return -EEXIST;
	}

	/* insert item, that is empty directory item */
	return reiserfs_insert_item(th, path, &key, ih, inode, body);
}

/*
 * stat data of object has been inserted, this inserts the item
 * containing the body of symlink
 */
static int reiserfs_new_symlink(struct reiserfs_transaction_handle *th,
				struct inode *inode,
				struct item_head *ih,
				struct treepath *path, const char *symname,
				int item_len)
{
	struct super_block *sb = th->t_super;
	struct cpu_key key;
	int retval;

	BUG_ON(!th->t_trans_id);

	_make_cpu_key(&key, KEY_FORMAT_3_5,
		      le32_to_cpu(ih->ih_key.k_dir_id),
		      le32_to_cpu(ih->ih_key.k_objectid),
		      1, TYPE_DIRECT, 3 /*key length */ );

	make_le_item_head(ih, NULL, KEY_FORMAT_3_5, 1, TYPE_DIRECT, item_len,
			  0 /*free_space */ );

	/* look for place in the tree for new item */
	retval = search_item(sb, &key, path);
	if (retval == IO_ERROR) {
		reiserfs_error(sb, "vs-13080",
			       "i/o failure occurred creating new symlink");
		return -EIO;
	}
	if (retval == ITEM_FOUND) {
		pathrelse(path);
		reiserfs_warning(sb, "vs-13080",
				 "object with this key exists (%k)",
				 &(ih->ih_key));
		return -EEXIST;
	}

	/* insert item, that is body of symlink */
	return reiserfs_insert_item(th, path, &key, ih, inode, symname);
}

/*
 * inserts the stat data into the tree, and then calls
 * reiserfs_new_directory (to insert ".", ".." item if new object is
 * directory) or reiserfs_new_symlink (to insert symlink body if new
 * object is symlink) or nothing (if new object is regular file)

 * NOTE! uid and gid must already be set in the inode.  If we return
 * non-zero due to an error, we have to drop the quota previously allocated
 * for the fresh inode.  This can only be done outside a transaction, so
 * if we return non-zero, we also end the transaction.
 *
 * @th: active transaction handle
 * @dir: parent directory for new inode
 * @mode: mode of new inode
 * @symname: symlink contents if inode is symlink
 * @isize: 0 for regular file, EMPTY_DIR_SIZE for dirs, strlen(symname) for
 *         symlinks
 * @inode: inode to be filled
 * @security: optional security context to associate with this inode
 */
int reiserfs_new_inode(struct reiserfs_transaction_handle *th,
		       struct inode *dir, umode_t mode, const char *symname,
		       /* 0 for regular, EMTRY_DIR_SIZE for dirs,
		          strlen (symname) for symlinks) */
		       loff_t i_size, struct dentry *dentry,
		       struct inode *inode,
		       struct reiserfs_security_handle *security)
{
	struct super_block *sb = dir->i_sb;
	struct reiserfs_iget_args args;
	INITIALIZE_PATH(path_to_key);
	struct cpu_key key;
	struct item_head ih;
	struct stat_data sd;
	int retval;
	int err;
	int depth;

	BUG_ON(!th->t_trans_id);

	depth = reiserfs_write_unlock_nested(sb);
	err = dquot_alloc_inode(inode);
	reiserfs_write_lock_nested(sb, depth);
	if (err)
		goto out_end_trans;
	if (!dir->i_nlink) {
		err = -EPERM;
		goto out_bad_inode;
	}

	/* item head of new item */
	ih.ih_key.k_dir_id = reiserfs_choose_packing(dir);
	ih.ih_key.k_objectid = cpu_to_le32(reiserfs_get_unused_objectid(th));
	if (!ih.ih_key.k_objectid) {
		err = -ENOMEM;
		goto out_bad_inode;
	}
	args.objectid = inode->i_ino = le32_to_cpu(ih.ih_key.k_objectid);
	if (old_format_only(sb))
		make_le_item_head(&ih, NULL, KEY_FORMAT_3_5, SD_OFFSET,
				  TYPE_STAT_DATA, SD_V1_SIZE, MAX_US_INT);
	else
		make_le_item_head(&ih, NULL, KEY_FORMAT_3_6, SD_OFFSET,
				  TYPE_STAT_DATA, SD_SIZE, MAX_US_INT);
	memcpy(INODE_PKEY(inode), &ih.ih_key, KEY_SIZE);
	args.dirid = le32_to_cpu(ih.ih_key.k_dir_id);

	depth = reiserfs_write_unlock_nested(inode->i_sb);
	err = insert_inode_locked4(inode, args.objectid,
			     reiserfs_find_actor, &args);
	reiserfs_write_lock_nested(inode->i_sb, depth);
	if (err) {
		err = -EINVAL;
		goto out_bad_inode;
	}

	if (old_format_only(sb))
		/*
		 * not a perfect generation count, as object ids can be reused,
		 * but this is as good as reiserfs can do right now.
		 * note that the private part of inode isn't filled in yet,
		 * we have to use the directory.
		 */
		inode->i_generation = le32_to_cpu(INODE_PKEY(dir)->k_objectid);
	else
#if defined( USE_INODE_GENERATION_COUNTER )
		inode->i_generation =
		    le32_to_cpu(REISERFS_SB(sb)->s_rs->s_inode_generation);
#else
		inode->i_generation = ++event;
#endif

	/* fill stat data */
	set_nlink(inode, (S_ISDIR(mode) ? 2 : 1));

	/* uid and gid must already be set by the caller for quota init */

	/* symlink cannot be immutable or append only, right? */
	if (S_ISLNK(inode->i_mode))
		inode->i_flags &= ~(S_IMMUTABLE | S_APPEND);

	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_size = i_size;
	inode->i_blocks = 0;
	inode->i_bytes = 0;
	REISERFS_I(inode)->i_first_direct_byte = S_ISLNK(mode) ? 1 :
	    U32_MAX /*NO_BYTES_IN_DIRECT_ITEM */ ;

	INIT_LIST_HEAD(&REISERFS_I(inode)->i_prealloc_list);
	REISERFS_I(inode)->i_flags = 0;
	REISERFS_I(inode)->i_prealloc_block = 0;
	REISERFS_I(inode)->i_prealloc_count = 0;
	REISERFS_I(inode)->i_trans_id = 0;
	REISERFS_I(inode)->i_jl = NULL;
	REISERFS_I(inode)->i_attrs =
	    REISERFS_I(dir)->i_attrs & REISERFS_INHERIT_MASK;
	sd_attrs_to_i_attrs(REISERFS_I(inode)->i_attrs, inode);
	reiserfs_init_xattr_rwsem(inode);

	/* key to search for correct place for new stat data */
	_make_cpu_key(&key, KEY_FORMAT_3_6, le32_to_cpu(ih.ih_key.k_dir_id),
		      le32_to_cpu(ih.ih_key.k_objectid), SD_OFFSET,
		      TYPE_STAT_DATA, 3 /*key length */ );

	/* find proper place for inserting of stat data */
	retval = search_item(sb, &key, &path_to_key);
	if (retval == IO_ERROR) {
		err = -EIO;
		goto out_bad_inode;
	}
	if (retval == ITEM_FOUND) {
		pathrelse(&path_to_key);
		err = -EEXIST;
		goto out_bad_inode;
	}
	if (old_format_only(sb)) {
		/* i_uid or i_gid is too big to be stored in stat data v3.5 */
		if (i_uid_read(inode) & ~0xffff || i_gid_read(inode) & ~0xffff) {
			pathrelse(&path_to_key);
			err = -EINVAL;
			goto out_bad_inode;
		}
		inode2sd_v1(&sd, inode, inode->i_size);
	} else {
		inode2sd(&sd, inode, inode->i_size);
	}
	/*
	 * store in in-core inode the key of stat data and version all
	 * object items will have (directory items will have old offset
	 * format, other new objects will consist of new items)
	 */
	if (old_format_only(sb) || S_ISDIR(mode) || S_ISLNK(mode))
		set_inode_item_key_version(inode, KEY_FORMAT_3_5);
	else
		set_inode_item_key_version(inode, KEY_FORMAT_3_6);
	if (old_format_only(sb))
		set_inode_sd_version(inode, STAT_DATA_V1);
	else
		set_inode_sd_version(inode, STAT_DATA_V2);

	/* insert the stat data into the tree */
#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	if (REISERFS_I(dir)->new_packing_locality)
		th->displace_new_blocks = 1;
#endif
	retval =
	    reiserfs_insert_item(th, &path_to_key, &key, &ih, inode,
				 (char *)(&sd));
	if (retval) {
		err = retval;
		reiserfs_check_path(&path_to_key);
		goto out_bad_inode;
	}
#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	if (!th->displace_new_blocks)
		REISERFS_I(dir)->new_packing_locality = 0;
#endif
	if (S_ISDIR(mode)) {
		/* insert item with "." and ".." */
		retval =
		    reiserfs_new_directory(th, inode, &ih, &path_to_key, dir);
	}

	if (S_ISLNK(mode)) {
		/* insert body of symlink */
		if (!old_format_only(sb))
			i_size = ROUND_UP(i_size);
		retval =
		    reiserfs_new_symlink(th, inode, &ih, &path_to_key, symname,
					 i_size);
	}
	if (retval) {
		err = retval;
		reiserfs_check_path(&path_to_key);
		journal_end(th);
		goto out_inserted_sd;
	}

	if (reiserfs_posixacl(inode->i_sb)) {
		reiserfs_write_unlock(inode->i_sb);
		retval = reiserfs_inherit_default_acl(th, dir, dentry, inode);
		reiserfs_write_lock(inode->i_sb);
		if (retval) {
			err = retval;
			reiserfs_check_path(&path_to_key);
			journal_end(th);
			goto out_inserted_sd;
		}
	} else if (inode->i_sb->s_flags & MS_POSIXACL) {
		reiserfs_warning(inode->i_sb, "jdm-13090",
				 "ACLs aren't enabled in the fs, "
				 "but vfs thinks they are!");
	} else if (IS_PRIVATE(dir))
		inode->i_flags |= S_PRIVATE;

	if (security->name) {
		reiserfs_write_unlock(inode->i_sb);
		retval = reiserfs_security_write(th, inode, security);
		reiserfs_write_lock(inode->i_sb);
		if (retval) {
			err = retval;
			reiserfs_check_path(&path_to_key);
			retval = journal_end(th);
			if (retval)
				err = retval;
			goto out_inserted_sd;
		}
	}

	reiserfs_update_sd(th, inode);
	reiserfs_check_path(&path_to_key);

	return 0;

out_bad_inode:
	/* Invalidate the object, nothing was inserted yet */
	INODE_PKEY(inode)->k_objectid = 0;

	/* Quota change must be inside a transaction for journaling */
	depth = reiserfs_write_unlock_nested(inode->i_sb);
	dquot_free_inode(inode);
	reiserfs_write_lock_nested(inode->i_sb, depth);

out_end_trans:
	journal_end(th);
	/*
	 * Drop can be outside and it needs more credits so it's better
	 * to have it outside
	 */
	depth = reiserfs_write_unlock_nested(inode->i_sb);
	dquot_drop(inode);
	reiserfs_write_lock_nested(inode->i_sb, depth);
	inode->i_flags |= S_NOQUOTA;
	make_bad_inode(inode);

out_inserted_sd:
	clear_nlink(inode);
	th->t_trans_id = 0;	/* so the caller can't use this handle later */
	unlock_new_inode(inode); /* OK to do even if we hadn't locked it */
	iput(inode);
	return err;
}

/*
 * finds the tail page in the page cache,
 * reads the last block in.
 *
 * On success, page_result is set to a locked, pinned page, and bh_result
 * is set to an up to date buffer for the last block in the file.  returns 0.
 *
 * tail conversion is not done, so bh_result might not be valid for writing
 * check buffer_mapped(bh_result) and bh_result->b_blocknr != 0 before
 * trying to write the block.
 *
 * on failure, nonzero is returned, page_result and bh_result are untouched.
 */
static int grab_tail_page(struct inode *inode,
			  struct page **page_result,
			  struct buffer_head **bh_result)
{

	/*
	 * we want the page with the last byte in the file,
	 * not the page that will hold the next byte for appending
	 */
	unsigned long index = (inode->i_size - 1) >> PAGE_CACHE_SHIFT;
	unsigned long pos = 0;
	unsigned long start = 0;
	unsigned long blocksize = inode->i_sb->s_blocksize;
	unsigned long offset = (inode->i_size) & (PAGE_CACHE_SIZE - 1);
	struct buffer_head *bh;
	struct buffer_head *head;
	struct page *page;
	int error;

	/*
	 * we know that we are only called with inode->i_size > 0.
	 * we also know that a file tail can never be as big as a block
	 * If i_size % blocksize == 0, our file is currently block aligned
	 * and it won't need converting or zeroing after a truncate.
	 */
	if ((offset & (blocksize - 1)) == 0) {
		return -ENOENT;
	}
	page = grab_cache_page(inode->i_mapping, index);
	error = -ENOMEM;
	if (!page) {
		goto out;
	}
	/* start within the page of the last block in the file */
	start = (offset / blocksize) * blocksize;

	error = __block_write_begin(page, start, offset - start,
				    reiserfs_get_block_create_0);
	if (error)
		goto unlock;

	head = page_buffers(page);
	bh = head;
	do {
		if (pos >= start) {
			break;
		}
		bh = bh->b_this_page;
		pos += blocksize;
	} while (bh != head);

	if (!buffer_uptodate(bh)) {
		/*
		 * note, this should never happen, prepare_write should be
		 * taking care of this for us.  If the buffer isn't up to
		 * date, I've screwed up the code to find the buffer, or the
		 * code to call prepare_write
		 */
		reiserfs_error(inode->i_sb, "clm-6000",
			       "error reading block %lu", bh->b_blocknr);
		error = -EIO;
		goto unlock;
	}
	*bh_result = bh;
	*page_result = page;

out:
	return error;

unlock:
	unlock_page(page);
	page_cache_release(page);
	return error;
}

/*
 * vfs version of truncate file.  Must NOT be called with
 * a transaction already started.
 *
 * some code taken from block_truncate_page
 */
int reiserfs_truncate_file(struct inode *inode, int update_timestamps)
{
	struct reiserfs_transaction_handle th;
	/* we want the offset for the first byte after the end of the file */
	unsigned long offset = inode->i_size & (PAGE_CACHE_SIZE - 1);
	unsigned blocksize = inode->i_sb->s_blocksize;
	unsigned length;
	struct page *page = NULL;
	int error;
	struct buffer_head *bh = NULL;
	int err2;

	reiserfs_write_lock(inode->i_sb);

	if (inode->i_size > 0) {
		error = grab_tail_page(inode, &page, &bh);
		if (error) {
			/*
			 * -ENOENT means we truncated past the end of the
			 * file, and get_block_create_0 could not find a
			 * block to read in, which is ok.
			 */
			if (error != -ENOENT)
				reiserfs_error(inode->i_sb, "clm-6001",
					       "grab_tail_page failed %d",
					       error);
			page = NULL;
			bh = NULL;
		}
	}

	/*
	 * so, if page != NULL, we have a buffer head for the offset at
	 * the end of the file. if the bh is mapped, and bh->b_blocknr != 0,
	 * then we have an unformatted node.  Otherwise, we have a direct item,
	 * and no zeroing is required on disk.  We zero after the truncate,
	 * because the truncate might pack the item anyway
	 * (it will unmap bh if it packs).
	 *
	 * it is enough to reserve space in transaction for 2 balancings:
	 * one for "save" link adding and another for the first
	 * cut_from_item. 1 is for update_sd
	 */
	error = journal_begin(&th, inode->i_sb,
			      JOURNAL_PER_BALANCE_CNT * 2 + 1);
	if (error)
		goto out;
	reiserfs_update_inode_transaction(inode);
	if (update_timestamps)
		/*
		 * we are doing real truncate: if the system crashes
		 * before the last transaction of truncating gets committed
		 * - on reboot the file either appears truncated properly
		 * or not truncated at all
		 */
		add_save_link(&th, inode, 1);
	err2 = reiserfs_do_truncate(&th, inode, page, update_timestamps);
	error = journal_end(&th);
	if (error)
		goto out;

	/* check reiserfs_do_truncate after ending the transaction */
	if (err2) {
		error = err2;
  		goto out;
	}
	
	if (update_timestamps) {
		error = remove_save_link(inode, 1 /* truncate */);
		if (error)
			goto out;
	}

	if (page) {
		length = offset & (blocksize - 1);
		/* if we are not on a block boundary */
		if (length) {
			length = blocksize - length;
			zero_user(page, offset, length);
			if (buffer_mapped(bh) && bh->b_blocknr != 0) {
				mark_buffer_dirty(bh);
			}
		}
		unlock_page(page);
		page_cache_release(page);
	}

	reiserfs_write_unlock(inode->i_sb);

	return 0;
out:
	if (page) {
		unlock_page(page);
		page_cache_release(page);
	}

	reiserfs_write_unlock(inode->i_sb);

	return error;
}

static int map_block_for_writepage(struct inode *inode,
				   struct buffer_head *bh_result,
				   unsigned long block)
{
	struct reiserfs_transaction_handle th;
	int fs_gen;
	struct item_head tmp_ih;
	struct item_head *ih;
	struct buffer_head *bh;
	__le32 *item;
	struct cpu_key key;
	INITIALIZE_PATH(path);
	int pos_in_item;
	int jbegin_count = JOURNAL_PER_BALANCE_CNT;
	loff_t byte_offset = ((loff_t)block << inode->i_sb->s_blocksize_bits)+1;
	int retval;
	int use_get_block = 0;
	int bytes_copied = 0;
	int copy_size;
	int trans_running = 0;

	/*
	 * catch places below that try to log something without
	 * starting a trans
	 */
	th.t_trans_id = 0;

	if (!buffer_uptodate(bh_result)) {
		return -EIO;
	}

	kmap(bh_result->b_page);
start_over:
	reiserfs_write_lock(inode->i_sb);
	make_cpu_key(&key, inode, byte_offset, TYPE_ANY, 3);

research:
	retval = search_for_position_by_key(inode->i_sb, &key, &path);
	if (retval != POSITION_FOUND) {
		use_get_block = 1;
		goto out;
	}

	bh = get_last_bh(&path);
	ih = tp_item_head(&path);
	item = tp_item_body(&path);
	pos_in_item = path.pos_in_item;

	/* we've found an unformatted node */
	if (indirect_item_found(retval, ih)) {
		if (bytes_copied > 0) {
			reiserfs_warning(inode->i_sb, "clm-6002",
					 "bytes_copied %d", bytes_copied);
		}
		if (!get_block_num(item, pos_in_item)) {
			/* crap, we are writing to a hole */
			use_get_block = 1;
			goto out;
		}
		set_block_dev_mapped(bh_result,
				     get_block_num(item, pos_in_item), inode);
	} else if (is_direct_le_ih(ih)) {
		char *p;
		p = page_address(bh_result->b_page);
		p += (byte_offset - 1) & (PAGE_CACHE_SIZE - 1);
		copy_size = ih_item_len(ih) - pos_in_item;

		fs_gen = get_generation(inode->i_sb);
		copy_item_head(&tmp_ih, ih);

		if (!trans_running) {
			/* vs-3050 is gone, no need to drop the path */
			retval = journal_begin(&th, inode->i_sb, jbegin_count);
			if (retval)
				goto out;
			reiserfs_update_inode_transaction(inode);
			trans_running = 1;
			if (fs_changed(fs_gen, inode->i_sb)
			    && item_moved(&tmp_ih, &path)) {
				reiserfs_restore_prepared_buffer(inode->i_sb,
								 bh);
				goto research;
			}
		}

		reiserfs_prepare_for_journal(inode->i_sb, bh, 1);

		if (fs_changed(fs_gen, inode->i_sb)
		    && item_moved(&tmp_ih, &path)) {
			reiserfs_restore_prepared_buffer(inode->i_sb, bh);
			goto research;
		}

		memcpy(ih_item_body(bh, ih) + pos_in_item, p + bytes_copied,
		       copy_size);

		journal_mark_dirty(&th, bh);
		bytes_copied += copy_size;
		set_block_dev_mapped(bh_result, 0, inode);

		/* are there still bytes left? */
		if (bytes_copied < bh_result->b_size &&
		    (byte_offset + bytes_copied) < inode->i_size) {
			set_cpu_key_k_offset(&key,
					     cpu_key_k_offset(&key) +
					     copy_size);
			goto research;
		}
	} else {
		reiserfs_warning(inode->i_sb, "clm-6003",
				 "bad item inode %lu", inode->i_ino);
		retval = -EIO;
		goto out;
	}
	retval = 0;

out:
	pathrelse(&path);
	if (trans_running) {
		int err = journal_end(&th);
		if (err)
			retval = err;
		trans_running = 0;
	}
	reiserfs_write_unlock(inode->i_sb);

	/* this is where we fill in holes in the file. */
	if (use_get_block) {
		retval = reiserfs_get_block(inode, block, bh_result,
					    GET_BLOCK_CREATE | GET_BLOCK_NO_IMUX
					    | GET_BLOCK_NO_DANGLE);
		if (!retval) {
			if (!buffer_mapped(bh_result)
			    || bh_result->b_blocknr == 0) {
				/* get_block failed to find a mapped unformatted node. */
				use_get_block = 0;
				goto start_over;
			}
		}
	}
	kunmap(bh_result->b_page);

	if (!retval && buffer_mapped(bh_result) && bh_result->b_blocknr == 0) {
		/*
		 * we've copied data from the page into the direct item, so the
		 * buffer in the page is now clean, mark it to reflect that.
		 */
		lock_buffer(bh_result);
		clear_buffer_dirty(bh_result);
		unlock_buffer(bh_result);
	}
	return retval;
}

/*
 * mason@suse.com: updated in 2.5.54 to follow the same general io
 * start/recovery path as __block_write_full_page, along with special
 * code to handle reiserfs tails.
 */
static int reiserfs_write_full_page(struct page *page,
				    struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	unsigned long end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	int error = 0;
	unsigned long block;
	sector_t last_block;
	struct buffer_head *head, *bh;
	int partial = 0;
	int nr = 0;
	int checked = PageChecked(page);
	struct reiserfs_transaction_handle th;
	struct super_block *s = inode->i_sb;
	int bh_per_page = PAGE_CACHE_SIZE / s->s_blocksize;
	th.t_trans_id = 0;

	/* no logging allowed when nonblocking or from PF_MEMALLOC */
	if (checked && (current->flags & PF_MEMALLOC)) {
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}

	/*
	 * The page dirty bit is cleared before writepage is called, which
	 * means we have to tell create_empty_buffers to make dirty buffers
	 * The page really should be up to date at this point, so tossing
	 * in the BH_Uptodate is just a sanity check.
	 */
	if (!page_has_buffers(page)) {
		create_empty_buffers(page, s->s_blocksize,
				     (1 << BH_Dirty) | (1 << BH_Uptodate));
	}
	head = page_buffers(page);

	/*
	 * last page in the file, zero out any contents past the
	 * last byte in the file
	 */
	if (page->index >= end_index) {
		unsigned last_offset;

		last_offset = inode->i_size & (PAGE_CACHE_SIZE - 1);
		/* no file contents in this page */
		if (page->index >= end_index + 1 || !last_offset) {
			unlock_page(page);
			return 0;
		}
		zero_user_segment(page, last_offset, PAGE_CACHE_SIZE);
	}
	bh = head;
	block = page->index << (PAGE_CACHE_SHIFT - s->s_blocksize_bits);
	last_block = (i_size_read(inode) - 1) >> inode->i_blkbits;
	/* first map all the buffers, logging any direct items we find */
	do {
		if (block > last_block) {
			/*
			 * This can happen when the block size is less than
			 * the page size.  The corresponding bytes in the page
			 * were zero filled above
			 */
			clear_buffer_dirty(bh);
			set_buffer_uptodate(bh);
		} else if ((checked || buffer_dirty(bh)) &&
		           (!buffer_mapped(bh) || (buffer_mapped(bh)
						       && bh->b_blocknr ==
						       0))) {
			/*
			 * not mapped yet, or it points to a direct item, search
			 * the btree for the mapping info, and log any direct
			 * items found
			 */
			if ((error = map_block_for_writepage(inode, bh, block))) {
				goto fail;
			}
		}
		bh = bh->b_this_page;
		block++;
	} while (bh != head);

	/*
	 * we start the transaction after map_block_for_writepage,
	 * because it can create holes in the file (an unbounded operation).
	 * starting it here, we can make a reliable estimate for how many
	 * blocks we're going to log
	 */
	if (checked) {
		ClearPageChecked(page);
		reiserfs_write_lock(s);
		error = journal_begin(&th, s, bh_per_page + 1);
		if (error) {
			reiserfs_write_unlock(s);
			goto fail;
		}
		reiserfs_update_inode_transaction(inode);
	}
	/* now go through and lock any dirty buffers on the page */
	do {
		get_bh(bh);
		if (!buffer_mapped(bh))
			continue;
		if (buffer_mapped(bh) && bh->b_blocknr == 0)
			continue;

		if (checked) {
			reiserfs_prepare_for_journal(s, bh, 1);
			journal_mark_dirty(&th, bh);
			continue;
		}
		/*
		 * from this point on, we know the buffer is mapped to a
		 * real block and not a direct item
		 */
		if (wbc->sync_mode != WB_SYNC_NONE) {
			lock_buffer(bh);
		} else {
			if (!trylock_buffer(bh)) {
				redirty_page_for_writepage(wbc, page);
				continue;
			}
		}
		if (test_clear_buffer_dirty(bh)) {
			mark_buffer_async_write(bh);
		} else {
			unlock_buffer(bh);
		}
	} while ((bh = bh->b_this_page) != head);

	if (checked) {
		error = journal_end(&th);
		reiserfs_write_unlock(s);
		if (error)
			goto fail;
	}
	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	unlock_page(page);

	/*
	 * since any buffer might be the only dirty buffer on the page,
	 * the first submit_bh can bring the page out of writeback.
	 * be careful with the buffers.
	 */
	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			submit_bh(WRITE, bh);
			nr++;
		}
		put_bh(bh);
		bh = next;
	} while (bh != head);

	error = 0;
done:
	if (nr == 0) {
		/*
		 * if this page only had a direct item, it is very possible for
		 * no io to be required without there being an error.  Or,
		 * someone else could have locked them and sent them down the
		 * pipe without locking the page
		 */
		bh = head;
		do {
			if (!buffer_uptodate(bh)) {
				partial = 1;
				break;
			}
			bh = bh->b_this_page;
		} while (bh != head);
		if (!partial)
			SetPageUptodate(page);
		end_page_writeback(page);
	}
	return error;

fail:
	/*
	 * catches various errors, we need to make sure any valid dirty blocks
	 * get to the media.  The page is currently locked and not marked for
	 * writeback
	 */
	ClearPageUptodate(page);
	bh = head;
	do {
		get_bh(bh);
		if (buffer_mapped(bh) && buffer_dirty(bh) && bh->b_blocknr) {
			lock_buffer(bh);
			mark_buffer_async_write(bh);
		} else {
			/*
			 * clear any dirty bits that might have come from
			 * getting attached to a dirty page
			 */
			clear_buffer_dirty(bh);
		}
		bh = bh->b_this_page;
	} while (bh != head);
	SetPageError(page);
	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	unlock_page(page);
	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			clear_buffer_dirty(bh);
			submit_bh(WRITE, bh);
			nr++;
		}
		put_bh(bh);
		bh = next;
	} while (bh != head);
	goto done;
}

static int reiserfs_readpage(struct file *f, struct page *page)
{
	return block_read_full_page(page, reiserfs_get_block);
}

static int reiserfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	reiserfs_wait_on_write_block(inode->i_sb);
	return reiserfs_write_full_page(page, wbc);
}

static void reiserfs_truncate_failed_write(struct inode *inode)
{
	truncate_inode_pages(inode->i_mapping, inode->i_size);
	reiserfs_truncate_file(inode, 0);
}

static int reiserfs_write_begin(struct file *file,
				struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata)
{
	struct inode *inode;
	struct page *page;
	pgoff_t index;
	int ret;
	int old_ref = 0;

 	inode = mapping->host;
	*fsdata = 0;
 	if (flags & AOP_FLAG_CONT_EXPAND &&
 	    (pos & (inode->i_sb->s_blocksize - 1)) == 0) {
 		pos ++;
		*fsdata = (void *)(unsigned long)flags;
	}

	index = pos >> PAGE_CACHE_SHIFT;
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	*pagep = page;

	reiserfs_wait_on_write_block(inode->i_sb);
	fix_tail_page_for_writing(page);
	if (reiserfs_transaction_running(inode->i_sb)) {
		struct reiserfs_transaction_handle *th;
		th = (struct reiserfs_transaction_handle *)current->
		    journal_info;
		BUG_ON(!th->t_refcount);
		BUG_ON(!th->t_trans_id);
		old_ref = th->t_refcount;
		th->t_refcount++;
	}
	ret = __block_write_begin(page, pos, len, reiserfs_get_block);
	if (ret && reiserfs_transaction_running(inode->i_sb)) {
		struct reiserfs_transaction_handle *th = current->journal_info;
		/*
		 * this gets a little ugly.  If reiserfs_get_block returned an
		 * error and left a transacstion running, we've got to close
		 * it, and we've got to free handle if it was a persistent
		 * transaction.
		 *
		 * But, if we had nested into an existing transaction, we need
		 * to just drop the ref count on the handle.
		 *
		 * If old_ref == 0, the transaction is from reiserfs_get_block,
		 * and it was a persistent trans.  Otherwise, it was nested
		 * above.
		 */
		if (th->t_refcount > old_ref) {
			if (old_ref)
				th->t_refcount--;
			else {
				int err;
				reiserfs_write_lock(inode->i_sb);
				err = reiserfs_end_persistent_transaction(th);
				reiserfs_write_unlock(inode->i_sb);
				if (err)
					ret = err;
			}
		}
	}
	if (ret) {
		unlock_page(page);
		page_cache_release(page);
		/* Truncate allocated blocks */
		reiserfs_truncate_failed_write(inode);
	}
	return ret;
}

int __reiserfs_write_begin(struct page *page, unsigned from, unsigned len)
{
	struct inode *inode = page->mapping->host;
	int ret;
	int old_ref = 0;
	int depth;

	depth = reiserfs_write_unlock_nested(inode->i_sb);
	reiserfs_wait_on_write_block(inode->i_sb);
	reiserfs_write_lock_nested(inode->i_sb, depth);

	fix_tail_page_for_writing(page);
	if (reiserfs_transaction_running(inode->i_sb)) {
		struct reiserfs_transaction_handle *th;
		th = (struct reiserfs_transaction_handle *)current->
		    journal_info;
		BUG_ON(!th->t_refcount);
		BUG_ON(!th->t_trans_id);
		old_ref = th->t_refcount;
		th->t_refcount++;
	}

	ret = __block_write_begin(page, from, len, reiserfs_get_block);
	if (ret && reiserfs_transaction_running(inode->i_sb)) {
		struct reiserfs_transaction_handle *th = current->journal_info;
		/*
		 * this gets a little ugly.  If reiserfs_get_block returned an
		 * error and left a transacstion running, we've got to close
		 * it, and we've got to free handle if it was a persistent
		 * transaction.
		 *
		 * But, if we had nested into an existing transaction, we need
		 * to just drop the ref count on the handle.
		 *
		 * If old_ref == 0, the transaction is from reiserfs_get_block,
		 * and it was a persistent trans.  Otherwise, it was nested
		 * above.
		 */
		if (th->t_refcount > old_ref) {
			if (old_ref)
				th->t_refcount--;
			else {
				int err;
				reiserfs_write_lock(inode->i_sb);
				err = reiserfs_end_persistent_transaction(th);
				reiserfs_write_unlock(inode->i_sb);
				if (err)
					ret = err;
			}
		}
	}
	return ret;

}

static sector_t reiserfs_aop_bmap(struct address_space *as, sector_t block)
{
	return generic_block_bmap(as, block, reiserfs_bmap);
}

static int reiserfs_write_end(struct file *file, struct address_space *mapping,
			      loff_t pos, unsigned len, unsigned copied,
			      struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;
	int ret = 0;
	int update_sd = 0;
	struct reiserfs_transaction_handle *th;
	unsigned start;
	bool locked = false;

	if ((unsigned long)fsdata & AOP_FLAG_CONT_EXPAND)
		pos ++;

	reiserfs_wait_on_write_block(inode->i_sb);
	if (reiserfs_transaction_running(inode->i_sb))
		th = current->journal_info;
	else
		th = NULL;

	start = pos & (PAGE_CACHE_SIZE - 1);
	if (unlikely(copied < len)) {
		if (!PageUptodate(page))
			copied = 0;

		page_zero_new_buffers(page, start + copied, start + len);
	}
	flush_dcache_page(page);

	reiserfs_commit_page(inode, page, start, start + copied);

	/*
	 * generic_commit_write does this for us, but does not update the
	 * transaction tracking stuff when the size changes.  So, we have
	 * to do the i_size updates here.
	 */
	if (pos + copied > inode->i_size) {
		struct reiserfs_transaction_handle myth;
		reiserfs_write_lock(inode->i_sb);
		locked = true;
		/*
		 * If the file have grown beyond the border where it
		 * can have a tail, unmark it as needing a tail
		 * packing
		 */
		if ((have_large_tails(inode->i_sb)
		     && inode->i_size > i_block_size(inode) * 4)
		    || (have_small_tails(inode->i_sb)
			&& inode->i_size > i_block_size(inode)))
			REISERFS_I(inode)->i_flags &= ~i_pack_on_close_mask;

		ret = journal_begin(&myth, inode->i_sb, 1);
		if (ret)
			goto journal_error;

		reiserfs_update_inode_transaction(inode);
		inode->i_size = pos + copied;
		/*
		 * this will just nest into our transaction.  It's important
		 * to use mark_inode_dirty so the inode gets pushed around on
		 * the dirty lists, and so that O_SYNC works as expected
		 */
		mark_inode_dirty(inode);
		reiserfs_update_sd(&myth, inode);
		update_sd = 1;
		ret = journal_end(&myth);
		if (ret)
			goto journal_error;
	}
	if (th) {
		if (!locked) {
			reiserfs_write_lock(inode->i_sb);
			locked = true;
		}
		if (!update_sd)
			mark_inode_dirty(inode);
		ret = reiserfs_end_persistent_transaction(th);
		if (ret)
			goto out;
	}

out:
	if (locked)
		reiserfs_write_unlock(inode->i_sb);
	unlock_page(page);
	page_cache_release(page);

	if (pos + len > inode->i_size)
		reiserfs_truncate_failed_write(inode);

	return ret == 0 ? copied : ret;

journal_error:
	reiserfs_write_unlock(inode->i_sb);
	locked = false;
	if (th) {
		if (!update_sd)
			reiserfs_update_sd(th, inode);
		ret = reiserfs_end_persistent_transaction(th);
	}
	goto out;
}

int reiserfs_commit_write(struct file *f, struct page *page,
			  unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	loff_t pos = ((loff_t) page->index << PAGE_CACHE_SHIFT) + to;
	int ret = 0;
	int update_sd = 0;
	struct reiserfs_transaction_handle *th = NULL;
	int depth;

	depth = reiserfs_write_unlock_nested(inode->i_sb);
	reiserfs_wait_on_write_block(inode->i_sb);
	reiserfs_write_lock_nested(inode->i_sb, depth);

	if (reiserfs_transaction_running(inode->i_sb)) {
		th = current->journal_info;
	}
	reiserfs_commit_page(inode, page, from, to);

	/*
	 * generic_commit_write does this for us, but does not update the
	 * transaction tracking stuff when the size changes.  So, we have
	 * to do the i_size updates here.
	 */
	if (pos > inode->i_size) {
		struct reiserfs_transaction_handle myth;
		/*
		 * If the file have grown beyond the border where it
		 * can have a tail, unmark it as needing a tail
		 * packing
		 */
		if ((have_large_tails(inode->i_sb)
		     && inode->i_size > i_block_size(inode) * 4)
		    || (have_small_tails(inode->i_sb)
			&& inode->i_size > i_block_size(inode)))
			REISERFS_I(inode)->i_flags &= ~i_pack_on_close_mask;

		ret = journal_begin(&myth, inode->i_sb, 1);
		if (ret)
			goto journal_error;

		reiserfs_update_inode_transaction(inode);
		inode->i_size = pos;
		/*
		 * this will just nest into our transaction.  It's important
		 * to use mark_inode_dirty so the inode gets pushed around
		 * on the dirty lists, and so that O_SYNC works as expected
		 */
		mark_inode_dirty(inode);
		reiserfs_update_sd(&myth, inode);
		update_sd = 1;
		ret = journal_end(&myth);
		if (ret)
			goto journal_error;
	}
	if (th) {
		if (!update_sd)
			mark_inode_dirty(inode);
		ret = reiserfs_end_persistent_transaction(th);
		if (ret)
			goto out;
	}

out:
	return ret;

journal_error:
	if (th) {
		if (!update_sd)
			reiserfs_update_sd(th, inode);
		ret = reiserfs_end_persistent_transaction(th);
	}

	return ret;
}

void sd_attrs_to_i_attrs(__u16 sd_attrs, struct inode *inode)
{
	if (reiserfs_attrs(inode->i_sb)) {
		if (sd_attrs & REISERFS_SYNC_FL)
			inode->i_flags |= S_SYNC;
		else
			inode->i_flags &= ~S_SYNC;
		if (sd_attrs & REISERFS_IMMUTABLE_FL)
			inode->i_flags |= S_IMMUTABLE;
		else
			inode->i_flags &= ~S_IMMUTABLE;
		if (sd_attrs & REISERFS_APPEND_FL)
			inode->i_flags |= S_APPEND;
		else
			inode->i_flags &= ~S_APPEND;
		if (sd_attrs & REISERFS_NOATIME_FL)
			inode->i_flags |= S_NOATIME;
		else
			inode->i_flags &= ~S_NOATIME;
		if (sd_attrs & REISERFS_NOTAIL_FL)
			REISERFS_I(inode)->i_flags |= i_nopack_mask;
		else
			REISERFS_I(inode)->i_flags &= ~i_nopack_mask;
	}
}

void i_attrs_to_sd_attrs(struct inode *inode, __u16 * sd_attrs)
{
	if (reiserfs_attrs(inode->i_sb)) {
		if (inode->i_flags & S_IMMUTABLE)
			*sd_attrs |= REISERFS_IMMUTABLE_FL;
		else
			*sd_attrs &= ~REISERFS_IMMUTABLE_FL;
		if (inode->i_flags & S_SYNC)
			*sd_attrs |= REISERFS_SYNC_FL;
		else
			*sd_attrs &= ~REISERFS_SYNC_FL;
		if (inode->i_flags & S_NOATIME)
			*sd_attrs |= REISERFS_NOATIME_FL;
		else
			*sd_attrs &= ~REISERFS_NOATIME_FL;
		if (REISERFS_I(inode)->i_flags & i_nopack_mask)
			*sd_attrs |= REISERFS_NOTAIL_FL;
		else
			*sd_attrs &= ~REISERFS_NOTAIL_FL;
	}
}

/*
 * decide if this buffer needs to stay around for data logging or ordered
 * write purposes
 */
static int invalidatepage_can_drop(struct inode *inode, struct buffer_head *bh)
{
	int ret = 1;
	struct reiserfs_journal *j = SB_JOURNAL(inode->i_sb);

	lock_buffer(bh);
	spin_lock(&j->j_dirty_buffers_lock);
	if (!buffer_mapped(bh)) {
		goto free_jh;
	}
	/*
	 * the page is locked, and the only places that log a data buffer
	 * also lock the page.
	 */
	if (reiserfs_file_data_log(inode)) {
		/*
		 * very conservative, leave the buffer pinned if
		 * anyone might need it.
		 */
		if (buffer_journaled(bh) || buffer_journal_dirty(bh)) {
			ret = 0;
		}
	} else  if (buffer_dirty(bh)) {
		struct reiserfs_journal_list *jl;
		struct reiserfs_jh *jh = bh->b_private;

		/*
		 * why is this safe?
		 * reiserfs_setattr updates i_size in the on disk
		 * stat data before allowing vmtruncate to be called.
		 *
		 * If buffer was put onto the ordered list for this
		 * transaction, we know for sure either this transaction
		 * or an older one already has updated i_size on disk,
		 * and this ordered data won't be referenced in the file
		 * if we crash.
		 *
		 * if the buffer was put onto the ordered list for an older
		 * transaction, we need to leave it around
		 */
		if (jh && (jl = jh->jl)
		    && jl != SB_JOURNAL(inode->i_sb)->j_current_jl)
			ret = 0;
	}
free_jh:
	if (ret && bh->b_private) {
		reiserfs_free_jh(bh);
	}
	spin_unlock(&j->j_dirty_buffers_lock);
	unlock_buffer(bh);
	return ret;
}

/* clm -- taken from fs/buffer.c:block_invalidate_page */
static void reiserfs_invalidatepage(struct page *page, unsigned int offset,
				    unsigned int length)
{
	struct buffer_head *head, *bh, *next;
	struct inode *inode = page->mapping->host;
	unsigned int curr_off = 0;
	unsigned int stop = offset + length;
	int partial_page = (offset || length < PAGE_CACHE_SIZE);
	int ret = 1;

	BUG_ON(!PageLocked(page));

	if (!partial_page)
		ClearPageChecked(page);

	if (!page_has_buffers(page))
		goto out;

	head = page_buffers(page);
	bh = head;
	do {
		unsigned int next_off = curr_off + bh->b_size;
		next = bh->b_this_page;

		if (next_off > stop)
			goto out;

		/*
		 * is this block fully invalidated?
		 */
		if (offset <= curr_off) {
			if (invalidatepage_can_drop(inode, bh))
				reiserfs_unmap_buffer(bh);
			else
				ret = 0;
		}
		curr_off = next_off;
		bh = next;
	} while (bh != head);

	/*
	 * We release buffers only if the entire page is being invalidated.
	 * The get_block cached value has been unconditionally invalidated,
	 * so real IO is not possible anymore.
	 */
	if (!partial_page && ret) {
		ret = try_to_release_page(page, 0);
		/* maybe should BUG_ON(!ret); - neilb */
	}
out:
	return;
}

static int reiserfs_set_page_dirty(struct page *page)
{
	struct inode *inode = page->mapping->host;
	if (reiserfs_file_data_log(inode)) {
		SetPageChecked(page);
		return __set_page_dirty_nobuffers(page);
	}
	return __set_page_dirty_buffers(page);
}

/*
 * Returns 1 if the page's buffers were dropped.  The page is locked.
 *
 * Takes j_dirty_buffers_lock to protect the b_assoc_buffers list_heads
 * in the buffers at page_buffers(page).
 *
 * even in -o notail mode, we can't be sure an old mount without -o notail
 * didn't create files with tails.
 */
static int reiserfs_releasepage(struct page *page, gfp_t unused_gfp_flags)
{
	struct inode *inode = page->mapping->host;
	struct reiserfs_journal *j = SB_JOURNAL(inode->i_sb);
	struct buffer_head *head;
	struct buffer_head *bh;
	int ret = 1;

	WARN_ON(PageChecked(page));
	spin_lock(&j->j_dirty_buffers_lock);
	head = page_buffers(page);
	bh = head;
	do {
		if (bh->b_private) {
			if (!buffer_dirty(bh) && !buffer_locked(bh)) {
				reiserfs_free_jh(bh);
			} else {
				ret = 0;
				break;
			}
		}
		bh = bh->b_this_page;
	} while (bh != head);
	if (ret)
		ret = try_to_free_buffers(page);
	spin_unlock(&j->j_dirty_buffers_lock);
	return ret;
}

/*
 * We thank Mingming Cao for helping us understand in great detail what
 * to do in this section of the code.
 */
static ssize_t reiserfs_direct_IO(int rw, struct kiocb *iocb,
				  struct iov_iter *iter, loff_t offset)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	ret = blockdev_direct_IO(rw, iocb, inode, iter, offset,
				 reiserfs_get_blocks_direct_io);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely((rw & WRITE) && ret < 0)) {
		loff_t isize = i_size_read(inode);
		loff_t end = offset + count;

		if ((end > isize) && inode_newsize_ok(inode, isize) == 0) {
			truncate_setsize(inode, isize);
			reiserfs_vfs_truncate_file(inode);
		}
	}

	return ret;
}

int reiserfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	unsigned int ia_valid;
	int error;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	/* must be turned off for recursive notify_change calls */
	ia_valid = attr->ia_valid &= ~(ATTR_KILL_SUID|ATTR_KILL_SGID);

	if (is_quota_modification(inode, attr))
		dquot_initialize(inode);
	reiserfs_write_lock(inode->i_sb);
	if (attr->ia_valid & ATTR_SIZE) {
		/*
		 * version 2 items will be caught by the s_maxbytes check
		 * done for us in vmtruncate
		 */
		if (get_inode_item_key_version(inode) == KEY_FORMAT_3_5 &&
		    attr->ia_size > MAX_NON_LFS) {
			reiserfs_write_unlock(inode->i_sb);
			error = -EFBIG;
			goto out;
		}

		inode_dio_wait(inode);

		/* fill in hole pointers in the expanding truncate case. */
		if (attr->ia_size > inode->i_size) {
			error = generic_cont_expand_simple(inode, attr->ia_size);
			if (REISERFS_I(inode)->i_prealloc_count > 0) {
				int err;
				struct reiserfs_transaction_handle th;
				/* we're changing at most 2 bitmaps, inode + super */
				err = journal_begin(&th, inode->i_sb, 4);
				if (!err) {
					reiserfs_discard_prealloc(&th, inode);
					err = journal_end(&th);
				}
				if (err)
					error = err;
			}
			if (error) {
				reiserfs_write_unlock(inode->i_sb);
				goto out;
			}
			/*
			 * file size is changed, ctime and mtime are
			 * to be updated
			 */
			attr->ia_valid |= (ATTR_MTIME | ATTR_CTIME);
		}
	}
	reiserfs_write_unlock(inode->i_sb);

	if ((((attr->ia_valid & ATTR_UID) && (from_kuid(&init_user_ns, attr->ia_uid) & ~0xffff)) ||
	     ((attr->ia_valid & ATTR_GID) && (from_kgid(&init_user_ns, attr->ia_gid) & ~0xffff))) &&
	    (get_inode_sd_version(inode) == STAT_DATA_V1)) {
		/* stat data of format v3.5 has 16 bit uid and gid */
		error = -EINVAL;
		goto out;
	}

	if ((ia_valid & ATTR_UID && !uid_eq(attr->ia_uid, inode->i_uid)) ||
	    (ia_valid & ATTR_GID && !gid_eq(attr->ia_gid, inode->i_gid))) {
		struct reiserfs_transaction_handle th;
		int jbegin_count =
		    2 *
		    (REISERFS_QUOTA_INIT_BLOCKS(inode->i_sb) +
		     REISERFS_QUOTA_DEL_BLOCKS(inode->i_sb)) +
		    2;

		error = reiserfs_chown_xattrs(inode, attr);

		if (error)
			return error;

		/*
		 * (user+group)*(old+new) structure - we count quota
		 * info and , inode write (sb, inode)
		 */
		reiserfs_write_lock(inode->i_sb);
		error = journal_begin(&th, inode->i_sb, jbegin_count);
		reiserfs_write_unlock(inode->i_sb);
		if (error)
			goto out;
		error = dquot_transfer(inode, attr);
		reiserfs_write_lock(inode->i_sb);
		if (error) {
			journal_end(&th);
			reiserfs_write_unlock(inode->i_sb);
			goto out;
		}

		/*
		 * Update corresponding info in inode so that everything
		 * is in one transaction
		 */
		if (attr->ia_valid & ATTR_UID)
			inode->i_uid = attr->ia_uid;
		if (attr->ia_valid & ATTR_GID)
			inode->i_gid = attr->ia_gid;
		mark_inode_dirty(inode);
		error = journal_end(&th);
		reiserfs_write_unlock(inode->i_sb);
		if (error)
			goto out;
	}

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = inode_newsize_ok(inode, attr->ia_size);
		if (!error) {
			/*
			 * Could race against reiserfs_file_release
			 * if called from NFS, so take tailpack mutex.
			 */
			mutex_lock(&REISERFS_I(inode)->tailpack);
			truncate_setsize(inode, attr->ia_size);
			reiserfs_truncate_file(inode, 1);
			mutex_unlock(&REISERFS_I(inode)->tailpack);
		}
	}

	if (!error) {
		setattr_copy(inode, attr);
		mark_inode_dirty(inode);
	}

	if (!error && reiserfs_posixacl(inode->i_sb)) {
		if (attr->ia_valid & ATTR_MODE)
			error = reiserfs_acl_chmod(inode);
	}

out:
	return error;
}

const struct address_space_operations reiserfs_address_space_operations = {
	.writepage = reiserfs_writepage,
	.readpage = reiserfs_readpage,
	.readpages = reiserfs_readpages,
	.releasepage = reiserfs_releasepage,
	.invalidatepage = reiserfs_invalidatepage,
	.write_begin = reiserfs_write_begin,
	.write_end = reiserfs_write_end,
	.bmap = reiserfs_aop_bmap,
	.direct_IO = reiserfs_direct_IO,
	.set_page_dirty = reiserfs_set_page_dirty,
};
