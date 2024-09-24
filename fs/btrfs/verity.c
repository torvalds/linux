// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include <linux/posix_acl_xattr.h>
#include <linux/iversion.h>
#include <linux/fsverity.h>
#include <linux/sched/mm.h>
#include "messages.h"
#include "ctree.h"
#include "btrfs_inode.h"
#include "transaction.h"
#include "locking.h"
#include "fs.h"
#include "accessors.h"
#include "ioctl.h"
#include "verity.h"
#include "orphan.h"

/*
 * Implementation of the interface defined in struct fsverity_operations.
 *
 * The main question is how and where to store the verity descriptor and the
 * Merkle tree. We store both in dedicated btree items in the filesystem tree,
 * together with the rest of the inode metadata. This means we'll need to do
 * extra work to encrypt them once encryption is supported in btrfs, but btrfs
 * has a lot of careful code around i_size and it seems better to make a new key
 * type than try and adjust all of our expectations for i_size.
 *
 * Note that this differs from the implementation in ext4 and f2fs, where
 * this data is stored as if it were in the file, but past EOF. However, btrfs
 * does not have a widespread mechanism for caching opaque metadata pages, so we
 * do pretend that the Merkle tree pages themselves are past EOF for the
 * purposes of caching them (as opposed to creating a virtual inode).
 *
 * fs verity items are stored under two different key types on disk.
 * The descriptor items:
 * [ inode objectid, BTRFS_VERITY_DESC_ITEM_KEY, offset ]
 *
 * At offset 0, we store a btrfs_verity_descriptor_item which tracks the
 * size of the descriptor item and some extra data for encryption.
 * Starting at offset 1, these hold the generic fs verity descriptor.
 * The latter are opaque to btrfs, we just read and write them as a blob for
 * the higher level verity code.  The most common descriptor size is 256 bytes.
 *
 * The merkle tree items:
 * [ inode objectid, BTRFS_VERITY_MERKLE_ITEM_KEY, offset ]
 *
 * These also start at offset 0, and correspond to the merkle tree bytes.
 * So when fsverity asks for page 0 of the merkle tree, we pull up one page
 * starting at offset 0 for this key type.  These are also opaque to btrfs,
 * we're blindly storing whatever fsverity sends down.
 *
 * Another important consideration is the fact that the Merkle tree data scales
 * linearly with the size of the file (with 4K pages/blocks and SHA-256, it's
 * ~1/127th the size) so for large files, writing the tree can be a lengthy
 * operation. For that reason, we guard the whole enable verity operation
 * (between begin_enable_verity and end_enable_verity) with an orphan item.
 * Again, because the data can be pretty large, it's quite possible that we
 * could run out of space writing it, so we try our best to handle errors by
 * stopping and rolling back rather than aborting the victim transaction.
 */

#define MERKLE_START_ALIGN			65536

/*
 * Compute the logical file offset where we cache the Merkle tree.
 *
 * @inode:  inode of the verity file
 *
 * For the purposes of caching the Merkle tree pages, as required by
 * fs-verity, it is convenient to do size computations in terms of a file
 * offset, rather than in terms of page indices.
 *
 * Use 64K to be sure it's past the last page in the file, even with 64K pages.
 * That rounding operation itself can overflow loff_t, so we do it in u64 and
 * check.
 *
 * Returns the file offset on success, negative error code on failure.
 */
static loff_t merkle_file_pos(const struct inode *inode)
{
	u64 sz = inode->i_size;
	u64 rounded = round_up(sz, MERKLE_START_ALIGN);

	if (rounded > inode->i_sb->s_maxbytes)
		return -EFBIG;

	return rounded;
}

/*
 * Drop all the items for this inode with this key_type.
 *
 * @inode:     inode to drop items for
 * @key_type:  type of items to drop (BTRFS_VERITY_DESC_ITEM or
 *             BTRFS_VERITY_MERKLE_ITEM)
 *
 * Before doing a verity enable we cleanup any existing verity items.
 * This is also used to clean up if a verity enable failed half way through.
 *
 * Returns number of dropped items on success, negative error code on failure.
 */
static int drop_verity_items(struct btrfs_inode *inode, u8 key_type)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = inode->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int count = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		/* 1 for the item being dropped */
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}

		/*
		 * Walk backwards through all the items until we find one that
		 * isn't from our key type or objectid
		 */
		key.objectid = btrfs_ino(inode);
		key.type = key_type;
		key.offset = (u64)-1;

		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0) {
			ret = 0;
			/* No more keys of this type, we're done */
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		} else if (ret < 0) {
			btrfs_end_transaction(trans);
			goto out;
		}

		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		/* No more keys of this type, we're done */
		if (key.objectid != btrfs_ino(inode) || key.type != key_type)
			break;

		/*
		 * This shouldn't be a performance sensitive function because
		 * it's not used as part of truncate.  If it ever becomes
		 * perf sensitive, change this to walk forward and bulk delete
		 * items
		 */
		ret = btrfs_del_items(trans, root, path, path->slots[0], 1);
		if (ret) {
			btrfs_end_transaction(trans);
			goto out;
		}
		count++;
		btrfs_release_path(path);
		btrfs_end_transaction(trans);
	}
	ret = count;
	btrfs_end_transaction(trans);
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Drop all verity items
 *
 * @inode:  inode to drop verity items for
 *
 * In most contexts where we are dropping verity items, we want to do it for all
 * the types of verity items, not a particular one.
 *
 * Returns: 0 on success, negative error code on failure.
 */
int btrfs_drop_verity_items(struct btrfs_inode *inode)
{
	int ret;

	ret = drop_verity_items(inode, BTRFS_VERITY_DESC_ITEM_KEY);
	if (ret < 0)
		return ret;
	ret = drop_verity_items(inode, BTRFS_VERITY_MERKLE_ITEM_KEY);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * Insert and write inode items with a given key type and offset.
 *
 * @inode:     inode to insert for
 * @key_type:  key type to insert
 * @offset:    item offset to insert at
 * @src:       source data to write
 * @len:       length of source data to write
 *
 * Write len bytes from src into items of up to 2K length.
 * The inserted items will have key (ino, key_type, offset + off) where off is
 * consecutively increasing from 0 up to the last item ending at offset + len.
 *
 * Returns 0 on success and a negative error code on failure.
 */
static int write_key_bytes(struct btrfs_inode *inode, u8 key_type, u64 offset,
			   const char *src, u64 len)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_root *root = inode->root;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	unsigned long copy_bytes;
	unsigned long src_offset = 0;
	void *data;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (len > 0) {
		/* 1 for the new item being inserted */
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}

		key.objectid = btrfs_ino(inode);
		key.type = key_type;
		key.offset = offset;

		/*
		 * Insert 2K at a time mostly to be friendly for smaller leaf
		 * size filesystems
		 */
		copy_bytes = min_t(u64, len, 2048);

		ret = btrfs_insert_empty_item(trans, root, path, &key, copy_bytes);
		if (ret) {
			btrfs_end_transaction(trans);
			break;
		}

		leaf = path->nodes[0];

		data = btrfs_item_ptr(leaf, path->slots[0], void);
		write_extent_buffer(leaf, src + src_offset,
				    (unsigned long)data, copy_bytes);
		offset += copy_bytes;
		src_offset += copy_bytes;
		len -= copy_bytes;

		btrfs_release_path(path);
		btrfs_end_transaction(trans);
	}

	btrfs_free_path(path);
	return ret;
}

/*
 * Read inode items of the given key type and offset from the btree.
 *
 * @inode:      inode to read items of
 * @key_type:   key type to read
 * @offset:     item offset to read from
 * @dest:       Buffer to read into. This parameter has slightly tricky
 *              semantics.  If it is NULL, the function will not do any copying
 *              and will just return the size of all the items up to len bytes.
 *              If dest_page is passed, then the function will kmap_local the
 *              page and ignore dest, but it must still be non-NULL to avoid the
 *              counting-only behavior.
 * @len:        length in bytes to read
 * @dest_folio: copy into this folio instead of the dest buffer
 *
 * Helper function to read items from the btree.  This returns the number of
 * bytes read or < 0 for errors.  We can return short reads if the items don't
 * exist on disk or aren't big enough to fill the desired length.  Supports
 * reading into a provided buffer (dest) or into the page cache
 *
 * Returns number of bytes read or a negative error code on failure.
 */
static int read_key_bytes(struct btrfs_inode *inode, u8 key_type, u64 offset,
			  char *dest, u64 len, struct folio *dest_folio)
{
	struct btrfs_path *path;
	struct btrfs_root *root = inode->root;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 item_end;
	u64 copy_end;
	int copied = 0;
	u32 copy_offset;
	unsigned long copy_bytes;
	unsigned long dest_offset = 0;
	void *data;
	char *kaddr = dest;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (dest_folio)
		path->reada = READA_FORWARD;

	key.objectid = btrfs_ino(inode);
	key.type = key_type;
	key.offset = offset;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		ret = 0;
		if (path->slots[0] == 0)
			goto out;
		path->slots[0]--;
	}

	while (len > 0) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

		if (key.objectid != btrfs_ino(inode) || key.type != key_type)
			break;

		item_end = btrfs_item_size(leaf, path->slots[0]) + key.offset;

		if (copied > 0) {
			/*
			 * Once we've copied something, we want all of the items
			 * to be sequential
			 */
			if (key.offset != offset)
				break;
		} else {
			/*
			 * Our initial offset might be in the middle of an
			 * item.  Make sure it all makes sense.
			 */
			if (key.offset > offset)
				break;
			if (item_end <= offset)
				break;
		}

		/* desc = NULL to just sum all the item lengths */
		if (!dest)
			copy_end = item_end;
		else
			copy_end = min(offset + len, item_end);

		/* Number of bytes in this item we want to copy */
		copy_bytes = copy_end - offset;

		/* Offset from the start of item for copying */
		copy_offset = offset - key.offset;

		if (dest) {
			if (dest_folio)
				kaddr = kmap_local_folio(dest_folio, 0);

			data = btrfs_item_ptr(leaf, path->slots[0], void);
			read_extent_buffer(leaf, kaddr + dest_offset,
					   (unsigned long)data + copy_offset,
					   copy_bytes);

			if (dest_folio)
				kunmap_local(kaddr);
		}

		offset += copy_bytes;
		dest_offset += copy_bytes;
		len -= copy_bytes;
		copied += copy_bytes;

		path->slots[0]++;
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			/*
			 * We've reached the last slot in this leaf and we need
			 * to go to the next leaf.
			 */
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				break;
			} else if (ret > 0) {
				ret = 0;
				break;
			}
		}
	}
out:
	btrfs_free_path(path);
	if (!ret)
		ret = copied;
	return ret;
}

/*
 * Delete an fsverity orphan
 *
 * @trans:  transaction to do the delete in
 * @inode:  inode to orphan
 *
 * Capture verity orphan specific logic that is repeated in the couple places
 * we delete verity orphans. Specifically, handling ENOENT and ignoring inodes
 * with 0 links.
 *
 * Returns zero on success or a negative error code on failure.
 */
static int del_orphan(struct btrfs_trans_handle *trans, struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;
	int ret;

	/*
	 * If the inode has no links, it is either already unlinked, or was
	 * created with O_TMPFILE. In either case, it should have an orphan from
	 * that other operation. Rather than reference count the orphans, we
	 * simply ignore them here, because we only invoke the verity path in
	 * the orphan logic when i_nlink is 1.
	 */
	if (!inode->vfs_inode.i_nlink)
		return 0;

	ret = btrfs_del_orphan_item(trans, root, btrfs_ino(inode));
	if (ret == -ENOENT)
		ret = 0;
	return ret;
}

/*
 * Rollback in-progress verity if we encounter an error.
 *
 * @inode:  inode verity had an error for
 *
 * We try to handle recoverable errors while enabling verity by rolling it back
 * and just failing the operation, rather than having an fs level error no
 * matter what. However, any error in rollback is unrecoverable.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int rollback_verity(struct btrfs_inode *inode)
{
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *root = inode->root;
	int ret;

	btrfs_assert_inode_locked(inode);
	truncate_inode_pages(inode->vfs_inode.i_mapping, inode->vfs_inode.i_size);
	clear_bit(BTRFS_INODE_VERITY_IN_PROGRESS, &inode->runtime_flags);
	ret = btrfs_drop_verity_items(inode);
	if (ret) {
		btrfs_handle_fs_error(root->fs_info, ret,
				"failed to drop verity items in rollback %llu",
				(u64)inode->vfs_inode.i_ino);
		goto out;
	}

	/*
	 * 1 for updating the inode flag
	 * 1 for deleting the orphan
	 */
	trans = btrfs_start_transaction(root, 2);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		btrfs_handle_fs_error(root->fs_info, ret,
			"failed to start transaction in verity rollback %llu",
			(u64)inode->vfs_inode.i_ino);
		goto out;
	}
	inode->ro_flags &= ~BTRFS_INODE_RO_VERITY;
	btrfs_sync_inode_flags_to_i_flags(&inode->vfs_inode);
	ret = btrfs_update_inode(trans, inode);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
	ret = del_orphan(trans, inode);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
out:
	if (trans)
		btrfs_end_transaction(trans);
	return ret;
}

/*
 * Finalize making the file a valid verity file
 *
 * @inode:      inode to be marked as verity
 * @desc:       contents of the verity descriptor to write (not NULL)
 * @desc_size:  size of the verity descriptor
 *
 * Do the actual work of finalizing verity after successfully writing the Merkle
 * tree:
 *
 * - write out the descriptor items
 * - mark the inode with the verity flag
 * - delete the orphan item
 * - mark the ro compat bit
 * - clear the in progress bit
 *
 * Returns 0 on success, negative error code on failure.
 */
static int finish_verity(struct btrfs_inode *inode, const void *desc,
			 size_t desc_size)
{
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *root = inode->root;
	struct btrfs_verity_descriptor_item item;
	int ret;

	/* Write out the descriptor item */
	memset(&item, 0, sizeof(item));
	btrfs_set_stack_verity_descriptor_size(&item, desc_size);
	ret = write_key_bytes(inode, BTRFS_VERITY_DESC_ITEM_KEY, 0,
			      (const char *)&item, sizeof(item));
	if (ret)
		goto out;

	/* Write out the descriptor itself */
	ret = write_key_bytes(inode, BTRFS_VERITY_DESC_ITEM_KEY, 1,
			      desc, desc_size);
	if (ret)
		goto out;

	/*
	 * 1 for updating the inode flag
	 * 1 for deleting the orphan
	 */
	trans = btrfs_start_transaction(root, 2);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}
	inode->ro_flags |= BTRFS_INODE_RO_VERITY;
	btrfs_sync_inode_flags_to_i_flags(&inode->vfs_inode);
	ret = btrfs_update_inode(trans, inode);
	if (ret)
		goto end_trans;
	ret = del_orphan(trans, inode);
	if (ret)
		goto end_trans;
	clear_bit(BTRFS_INODE_VERITY_IN_PROGRESS, &inode->runtime_flags);
	btrfs_set_fs_compat_ro(root->fs_info, VERITY);
end_trans:
	btrfs_end_transaction(trans);
out:
	return ret;

}

/*
 * fsverity op that begins enabling verity.
 *
 * @filp:  file to enable verity on
 *
 * Begin enabling fsverity for the file. We drop any existing verity items, add
 * an orphan and set the in progress bit.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int btrfs_begin_enable_verity(struct file *filp)
{
	struct btrfs_inode *inode = BTRFS_I(file_inode(filp));
	struct btrfs_root *root = inode->root;
	struct btrfs_trans_handle *trans;
	int ret;

	btrfs_assert_inode_locked(inode);

	if (test_bit(BTRFS_INODE_VERITY_IN_PROGRESS, &inode->runtime_flags))
		return -EBUSY;

	/*
	 * This should almost never do anything, but theoretically, it's
	 * possible that we failed to enable verity on a file, then were
	 * interrupted or failed while rolling back, failed to cleanup the
	 * orphan, and finally attempt to enable verity again.
	 */
	ret = btrfs_drop_verity_items(inode);
	if (ret)
		return ret;

	/* 1 for the orphan item */
	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_orphan_add(trans, inode);
	if (!ret)
		set_bit(BTRFS_INODE_VERITY_IN_PROGRESS, &inode->runtime_flags);
	btrfs_end_transaction(trans);

	return 0;
}

/*
 * fsverity op that ends enabling verity.
 *
 * @filp:              file we are finishing enabling verity on
 * @desc:              verity descriptor to write out (NULL in error conditions)
 * @desc_size:         size of the verity descriptor (variable with signatures)
 * @merkle_tree_size:  size of the merkle tree in bytes
 *
 * If desc is null, then VFS is signaling an error occurred during verity
 * enable, and we should try to rollback. Otherwise, attempt to finish verity.
 *
 * Returns 0 on success, negative error code on error.
 */
static int btrfs_end_enable_verity(struct file *filp, const void *desc,
				   size_t desc_size, u64 merkle_tree_size)
{
	struct btrfs_inode *inode = BTRFS_I(file_inode(filp));
	int ret = 0;
	int rollback_ret;

	btrfs_assert_inode_locked(inode);

	if (desc == NULL)
		goto rollback;

	ret = finish_verity(inode, desc, desc_size);
	if (ret)
		goto rollback;
	return ret;

rollback:
	rollback_ret = rollback_verity(inode);
	if (rollback_ret)
		btrfs_err(inode->root->fs_info,
			  "failed to rollback verity items: %d", rollback_ret);
	return ret;
}

/*
 * fsverity op that gets the struct fsverity_descriptor.
 *
 * @inode:     inode to get the descriptor of
 * @buf:       output buffer for the descriptor contents
 * @buf_size:  size of the output buffer. 0 to query the size
 *
 * fsverity does a two pass setup for reading the descriptor, in the first pass
 * it calls with buf_size = 0 to query the size of the descriptor, and then in
 * the second pass it actually reads the descriptor off disk.
 *
 * Returns the size on success or a negative error code on failure.
 */
int btrfs_get_verity_descriptor(struct inode *inode, void *buf, size_t buf_size)
{
	u64 true_size;
	int ret = 0;
	struct btrfs_verity_descriptor_item item;

	memset(&item, 0, sizeof(item));
	ret = read_key_bytes(BTRFS_I(inode), BTRFS_VERITY_DESC_ITEM_KEY, 0,
			     (char *)&item, sizeof(item), NULL);
	if (ret < 0)
		return ret;

	if (item.reserved[0] != 0 || item.reserved[1] != 0)
		return -EUCLEAN;

	true_size = btrfs_stack_verity_descriptor_size(&item);
	if (true_size > INT_MAX)
		return -EUCLEAN;

	if (buf_size == 0)
		return true_size;
	if (buf_size < true_size)
		return -ERANGE;

	ret = read_key_bytes(BTRFS_I(inode), BTRFS_VERITY_DESC_ITEM_KEY, 1,
			     buf, buf_size, NULL);
	if (ret < 0)
		return ret;
	if (ret != true_size)
		return -EIO;

	return true_size;
}

/*
 * fsverity op that reads and caches a merkle tree page.
 *
 * @inode:         inode to read a merkle tree page for
 * @index:         page index relative to the start of the merkle tree
 * @num_ra_pages:  number of pages to readahead. Optional, we ignore it
 *
 * The Merkle tree is stored in the filesystem btree, but its pages are cached
 * with a logical position past EOF in the inode's mapping.
 *
 * Returns the page we read, or an ERR_PTR on error.
 */
static struct page *btrfs_read_merkle_tree_page(struct inode *inode,
						pgoff_t index,
						unsigned long num_ra_pages)
{
	struct folio *folio;
	u64 off = (u64)index << PAGE_SHIFT;
	loff_t merkle_pos = merkle_file_pos(inode);
	int ret;

	if (merkle_pos < 0)
		return ERR_PTR(merkle_pos);
	if (merkle_pos > inode->i_sb->s_maxbytes - off - PAGE_SIZE)
		return ERR_PTR(-EFBIG);
	index += merkle_pos >> PAGE_SHIFT;
again:
	folio = __filemap_get_folio(inode->i_mapping, index, FGP_ACCESSED, 0);
	if (!IS_ERR(folio)) {
		if (folio_test_uptodate(folio))
			goto out;

		folio_lock(folio);
		/* If it's not uptodate after we have the lock, we got a read error. */
		if (!folio_test_uptodate(folio)) {
			folio_unlock(folio);
			folio_put(folio);
			return ERR_PTR(-EIO);
		}
		folio_unlock(folio);
		goto out;
	}

	folio = filemap_alloc_folio(mapping_gfp_constraint(inode->i_mapping, ~__GFP_FS),
				    0);
	if (!folio)
		return ERR_PTR(-ENOMEM);

	ret = filemap_add_folio(inode->i_mapping, folio, index, GFP_NOFS);
	if (ret) {
		folio_put(folio);
		/* Did someone else insert a folio here? */
		if (ret == -EEXIST)
			goto again;
		return ERR_PTR(ret);
	}

	/*
	 * Merkle item keys are indexed from byte 0 in the merkle tree.
	 * They have the form:
	 *
	 * [ inode objectid, BTRFS_MERKLE_ITEM_KEY, offset in bytes ]
	 */
	ret = read_key_bytes(BTRFS_I(inode), BTRFS_VERITY_MERKLE_ITEM_KEY, off,
			     folio_address(folio), PAGE_SIZE, folio);
	if (ret < 0) {
		folio_put(folio);
		return ERR_PTR(ret);
	}
	if (ret < PAGE_SIZE)
		folio_zero_segment(folio, ret, PAGE_SIZE);

	folio_mark_uptodate(folio);
	folio_unlock(folio);

out:
	return folio_file_page(folio, index);
}

/*
 * fsverity op that writes a Merkle tree block into the btree.
 *
 * @inode:	inode to write a Merkle tree block for
 * @buf:	Merkle tree block to write
 * @pos:	the position of the block in the Merkle tree (in bytes)
 * @size:	the Merkle tree block size (in bytes)
 *
 * Returns 0 on success or negative error code on failure
 */
static int btrfs_write_merkle_tree_block(struct inode *inode, const void *buf,
					 u64 pos, unsigned int size)
{
	loff_t merkle_pos = merkle_file_pos(inode);

	if (merkle_pos < 0)
		return merkle_pos;
	if (merkle_pos > inode->i_sb->s_maxbytes - pos - size)
		return -EFBIG;

	return write_key_bytes(BTRFS_I(inode), BTRFS_VERITY_MERKLE_ITEM_KEY,
			       pos, buf, size);
}

const struct fsverity_operations btrfs_verityops = {
	.begin_enable_verity     = btrfs_begin_enable_verity,
	.end_enable_verity       = btrfs_end_enable_verity,
	.get_verity_descriptor   = btrfs_get_verity_descriptor,
	.read_merkle_tree_page   = btrfs_read_merkle_tree_page,
	.write_merkle_tree_block = btrfs_write_merkle_tree_block,
};
