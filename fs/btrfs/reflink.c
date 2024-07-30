// SPDX-License-Identifier: GPL-2.0

#include <linux/blkdev.h>
#include <linux/iversion.h>
#include "ctree.h"
#include "fs.h"
#include "messages.h"
#include "compression.h"
#include "delalloc-space.h"
#include "disk-io.h"
#include "reflink.h"
#include "transaction.h"
#include "subpage.h"
#include "accessors.h"
#include "file-item.h"
#include "file.h"
#include "super.h"

#define BTRFS_MAX_DEDUPE_LEN	SZ_16M

static int clone_finish_inode_update(struct btrfs_trans_handle *trans,
				     struct inode *inode,
				     u64 endoff,
				     const u64 destoff,
				     const u64 olen,
				     int no_time_update)
{
	int ret;

	inode_inc_iversion(inode);
	if (!no_time_update) {
		inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	}
	/*
	 * We round up to the block size at eof when determining which
	 * extents to clone above, but shouldn't round up the file size.
	 */
	if (endoff > destoff + olen)
		endoff = destoff + olen;
	if (endoff > inode->i_size) {
		i_size_write(inode, endoff);
		btrfs_inode_safe_disk_i_size_write(BTRFS_I(inode), 0);
	}

	ret = btrfs_update_inode(trans, BTRFS_I(inode));
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		goto out;
	}
	ret = btrfs_end_transaction(trans);
out:
	return ret;
}

static int copy_inline_to_page(struct btrfs_inode *inode,
			       const u64 file_offset,
			       char *inline_data,
			       const u64 size,
			       const u64 datal,
			       const u8 comp_type)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	const u32 block_size = fs_info->sectorsize;
	const u64 range_end = file_offset + block_size - 1;
	const size_t inline_size = size - btrfs_file_extent_calc_inline_size(0);
	char *data_start = inline_data + btrfs_file_extent_calc_inline_size(0);
	struct extent_changeset *data_reserved = NULL;
	struct page *page = NULL;
	struct address_space *mapping = inode->vfs_inode.i_mapping;
	int ret;

	ASSERT(IS_ALIGNED(file_offset, block_size));

	/*
	 * We have flushed and locked the ranges of the source and destination
	 * inodes, we also have locked the inodes, so we are safe to do a
	 * reservation here. Also we must not do the reservation while holding
	 * a transaction open, otherwise we would deadlock.
	 */
	ret = btrfs_delalloc_reserve_space(inode, &data_reserved, file_offset,
					   block_size);
	if (ret)
		goto out;

	page = find_or_create_page(mapping, file_offset >> PAGE_SHIFT,
				   btrfs_alloc_write_mask(mapping));
	if (!page) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	ret = set_page_extent_mapped(page);
	if (ret < 0)
		goto out_unlock;

	clear_extent_bit(&inode->io_tree, file_offset, range_end,
			 EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG,
			 NULL);
	ret = btrfs_set_extent_delalloc(inode, file_offset, range_end, 0, NULL);
	if (ret)
		goto out_unlock;

	/*
	 * After dirtying the page our caller will need to start a transaction,
	 * and if we are low on metadata free space, that can cause flushing of
	 * delalloc for all inodes in order to get metadata space released.
	 * However we are holding the range locked for the whole duration of
	 * the clone/dedupe operation, so we may deadlock if that happens and no
	 * other task releases enough space. So mark this inode as not being
	 * possible to flush to avoid such deadlock. We will clear that flag
	 * when we finish cloning all extents, since a transaction is started
	 * after finding each extent to clone.
	 */
	set_bit(BTRFS_INODE_NO_DELALLOC_FLUSH, &inode->runtime_flags);

	if (comp_type == BTRFS_COMPRESS_NONE) {
		memcpy_to_page(page, offset_in_page(file_offset), data_start,
			       datal);
	} else {
		ret = btrfs_decompress(comp_type, data_start, page,
				       offset_in_page(file_offset),
				       inline_size, datal);
		if (ret)
			goto out_unlock;
		flush_dcache_page(page);
	}

	/*
	 * If our inline data is smaller then the block/page size, then the
	 * remaining of the block/page is equivalent to zeroes. We had something
	 * like the following done:
	 *
	 * $ xfs_io -f -c "pwrite -S 0xab 0 500" file
	 * $ sync  # (or fsync)
	 * $ xfs_io -c "falloc 0 4K" file
	 * $ xfs_io -c "pwrite -S 0xcd 4K 4K"
	 *
	 * So what's in the range [500, 4095] corresponds to zeroes.
	 */
	if (datal < block_size)
		memzero_page(page, datal, block_size - datal);

	btrfs_folio_set_uptodate(fs_info, page_folio(page), file_offset, block_size);
	btrfs_folio_clear_checked(fs_info, page_folio(page), file_offset, block_size);
	btrfs_folio_set_dirty(fs_info, page_folio(page), file_offset, block_size);
out_unlock:
	if (page) {
		unlock_page(page);
		put_page(page);
	}
	if (ret)
		btrfs_delalloc_release_space(inode, data_reserved, file_offset,
					     block_size, true);
	btrfs_delalloc_release_extents(inode, block_size);
out:
	extent_changeset_free(data_reserved);

	return ret;
}

/*
 * Deal with cloning of inline extents. We try to copy the inline extent from
 * the source inode to destination inode when possible. When not possible we
 * copy the inline extent's data into the respective page of the inode.
 */
static int clone_copy_inline_extent(struct inode *dst,
				    struct btrfs_path *path,
				    struct btrfs_key *new_key,
				    const u64 drop_start,
				    const u64 datal,
				    const u64 size,
				    const u8 comp_type,
				    char *inline_data,
				    struct btrfs_trans_handle **trans_out)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(dst);
	struct btrfs_root *root = BTRFS_I(dst)->root;
	const u64 aligned_end = ALIGN(new_key->offset + datal,
				      fs_info->sectorsize);
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_drop_extents_args drop_args = { 0 };
	int ret;
	struct btrfs_key key;

	if (new_key->offset > 0) {
		ret = copy_inline_to_page(BTRFS_I(dst), new_key->offset,
					  inline_data, size, datal, comp_type);
		goto out;
	}

	key.objectid = btrfs_ino(BTRFS_I(dst));
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		return ret;
	} else if (ret > 0) {
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				return ret;
			else if (ret > 0)
				goto copy_inline_extent;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid == btrfs_ino(BTRFS_I(dst)) &&
		    key.type == BTRFS_EXTENT_DATA_KEY) {
			/*
			 * There's an implicit hole at file offset 0, copy the
			 * inline extent's data to the page.
			 */
			ASSERT(key.offset > 0);
			goto copy_to_page;
		}
	} else if (i_size_read(dst) <= datal) {
		struct btrfs_file_extent_item *ei;

		ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_file_extent_item);
		/*
		 * If it's an inline extent replace it with the source inline
		 * extent, otherwise copy the source inline extent data into
		 * the respective page at the destination inode.
		 */
		if (btrfs_file_extent_type(path->nodes[0], ei) ==
		    BTRFS_FILE_EXTENT_INLINE)
			goto copy_inline_extent;

		goto copy_to_page;
	}

copy_inline_extent:
	/*
	 * We have no extent items, or we have an extent at offset 0 which may
	 * or may not be inlined. All these cases are dealt the same way.
	 */
	if (i_size_read(dst) > datal) {
		/*
		 * At the destination offset 0 we have either a hole, a regular
		 * extent or an inline extent larger then the one we want to
		 * clone. Deal with all these cases by copying the inline extent
		 * data into the respective page at the destination inode.
		 */
		goto copy_to_page;
	}

	/*
	 * Release path before starting a new transaction so we don't hold locks
	 * that would confuse lockdep.
	 */
	btrfs_release_path(path);
	/*
	 * If we end up here it means were copy the inline extent into a leaf
	 * of the destination inode. We know we will drop or adjust at most one
	 * extent item in the destination root.
	 *
	 * 1 unit - adjusting old extent (we may have to split it)
	 * 1 unit - add new extent
	 * 1 unit - inode update
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}
	drop_args.path = path;
	drop_args.start = drop_start;
	drop_args.end = aligned_end;
	drop_args.drop_cache = true;
	ret = btrfs_drop_extents(trans, root, BTRFS_I(dst), &drop_args);
	if (ret)
		goto out;
	ret = btrfs_insert_empty_item(trans, root, path, new_key, size);
	if (ret)
		goto out;

	write_extent_buffer(path->nodes[0], inline_data,
			    btrfs_item_ptr_offset(path->nodes[0],
						  path->slots[0]),
			    size);
	btrfs_update_inode_bytes(BTRFS_I(dst), datal, drop_args.bytes_found);
	btrfs_set_inode_full_sync(BTRFS_I(dst));
	ret = btrfs_inode_set_file_extent_range(BTRFS_I(dst), 0, aligned_end);
out:
	if (!ret && !trans) {
		/*
		 * No transaction here means we copied the inline extent into a
		 * page of the destination inode.
		 *
		 * 1 unit to update inode item
		 */
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
		}
	}
	if (ret && trans) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
	}
	if (!ret)
		*trans_out = trans;

	return ret;

copy_to_page:
	/*
	 * Release our path because we don't need it anymore and also because
	 * copy_inline_to_page() needs to reserve data and metadata, which may
	 * need to flush delalloc when we are low on available space and
	 * therefore cause a deadlock if writeback of an inline extent needs to
	 * write to the same leaf or an ordered extent completion needs to write
	 * to the same leaf.
	 */
	btrfs_release_path(path);

	ret = copy_inline_to_page(BTRFS_I(dst), new_key->offset,
				  inline_data, size, datal, comp_type);
	goto out;
}

/*
 * Clone a range from inode file to another.
 *
 * @src:             Inode to clone from
 * @inode:           Inode to clone to
 * @off:             Offset within source to start clone from
 * @olen:            Original length, passed by user, of range to clone
 * @olen_aligned:    Block-aligned value of olen
 * @destoff:         Offset within @inode to start clone
 * @no_time_update:  Whether to update mtime/ctime on the target inode
 */
static int btrfs_clone(struct inode *src, struct inode *inode,
		       const u64 off, const u64 olen, const u64 olen_aligned,
		       const u64 destoff, int no_time_update)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf;
	struct btrfs_trans_handle *trans;
	char *buf = NULL;
	struct btrfs_key key;
	u32 nritems;
	int slot;
	int ret;
	const u64 len = olen_aligned;
	u64 last_dest_end = destoff;
	u64 prev_extent_end = off;

	ret = -ENOMEM;
	buf = kvmalloc(fs_info->nodesize, GFP_KERNEL);
	if (!buf)
		return ret;

	path = btrfs_alloc_path();
	if (!path) {
		kvfree(buf);
		return ret;
	}

	path->reada = READA_FORWARD;
	/* Clone data */
	key.objectid = btrfs_ino(BTRFS_I(src));
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = off;

	while (1) {
		struct btrfs_file_extent_item *extent;
		u64 extent_gen;
		int type;
		u32 size;
		struct btrfs_key new_key;
		u64 disko = 0, diskl = 0;
		u64 datao = 0, datal = 0;
		u8 comp;
		u64 drop_start;

		/* Note the key will change type as we walk through the tree */
		ret = btrfs_search_slot(NULL, BTRFS_I(src)->root, &key, path,
				0, 0);
		if (ret < 0)
			goto out;
		/*
		 * First search, if no extent item that starts at offset off was
		 * found but the previous item is an extent item, it's possible
		 * it might overlap our target range, therefore process it.
		 */
		if (key.offset == off && ret > 0 && path->slots[0] > 0) {
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0] - 1);
			if (key.type == BTRFS_EXTENT_DATA_KEY)
				path->slots[0]--;
		}

		nritems = btrfs_header_nritems(path->nodes[0]);
process_slot:
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(BTRFS_I(src)->root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			nritems = btrfs_header_nritems(path->nodes[0]);
		}
		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.type > BTRFS_EXTENT_DATA_KEY ||
		    key.objectid != btrfs_ino(BTRFS_I(src)))
			break;

		ASSERT(key.type == BTRFS_EXTENT_DATA_KEY);

		extent = btrfs_item_ptr(leaf, slot,
					struct btrfs_file_extent_item);
		extent_gen = btrfs_file_extent_generation(leaf, extent);
		comp = btrfs_file_extent_compression(leaf, extent);
		type = btrfs_file_extent_type(leaf, extent);
		if (type == BTRFS_FILE_EXTENT_REG ||
		    type == BTRFS_FILE_EXTENT_PREALLOC) {
			disko = btrfs_file_extent_disk_bytenr(leaf, extent);
			diskl = btrfs_file_extent_disk_num_bytes(leaf, extent);
			datao = btrfs_file_extent_offset(leaf, extent);
			datal = btrfs_file_extent_num_bytes(leaf, extent);
		} else if (type == BTRFS_FILE_EXTENT_INLINE) {
			/* Take upper bound, may be compressed */
			datal = btrfs_file_extent_ram_bytes(leaf, extent);
		}

		/*
		 * The first search might have left us at an extent item that
		 * ends before our target range's start, can happen if we have
		 * holes and NO_HOLES feature enabled.
		 *
		 * Subsequent searches may leave us on a file range we have
		 * processed before - this happens due to a race with ordered
		 * extent completion for a file range that is outside our source
		 * range, but that range was part of a file extent item that
		 * also covered a leading part of our source range.
		 */
		if (key.offset + datal <= prev_extent_end) {
			path->slots[0]++;
			goto process_slot;
		} else if (key.offset >= off + len) {
			break;
		}

		prev_extent_end = key.offset + datal;
		size = btrfs_item_size(leaf, slot);
		read_extent_buffer(leaf, buf, btrfs_item_ptr_offset(leaf, slot),
				   size);

		btrfs_release_path(path);

		memcpy(&new_key, &key, sizeof(new_key));
		new_key.objectid = btrfs_ino(BTRFS_I(inode));
		if (off <= key.offset)
			new_key.offset = key.offset + destoff - off;
		else
			new_key.offset = destoff;

		/*
		 * Deal with a hole that doesn't have an extent item that
		 * represents it (NO_HOLES feature enabled).
		 * This hole is either in the middle of the cloning range or at
		 * the beginning (fully overlaps it or partially overlaps it).
		 */
		if (new_key.offset != last_dest_end)
			drop_start = last_dest_end;
		else
			drop_start = new_key.offset;

		if (type == BTRFS_FILE_EXTENT_REG ||
		    type == BTRFS_FILE_EXTENT_PREALLOC) {
			struct btrfs_replace_extent_info clone_info;

			/*
			 *    a  | --- range to clone ---|  b
			 * | ------------- extent ------------- |
			 */

			/* Subtract range b */
			if (key.offset + datal > off + len)
				datal = off + len - key.offset;

			/* Subtract range a */
			if (off > key.offset) {
				datao += off - key.offset;
				datal -= off - key.offset;
			}

			clone_info.disk_offset = disko;
			clone_info.disk_len = diskl;
			clone_info.data_offset = datao;
			clone_info.data_len = datal;
			clone_info.file_offset = new_key.offset;
			clone_info.extent_buf = buf;
			clone_info.is_new_extent = false;
			clone_info.update_times = !no_time_update;
			ret = btrfs_replace_file_extents(BTRFS_I(inode), path,
					drop_start, new_key.offset + datal - 1,
					&clone_info, &trans);
			if (ret)
				goto out;
		} else {
			ASSERT(type == BTRFS_FILE_EXTENT_INLINE);
			/*
			 * Inline extents always have to start at file offset 0
			 * and can never be bigger then the sector size. We can
			 * never clone only parts of an inline extent, since all
			 * reflink operations must start at a sector size aligned
			 * offset, and the length must be aligned too or end at
			 * the i_size (which implies the whole inlined data).
			 */
			ASSERT(key.offset == 0);
			ASSERT(datal <= fs_info->sectorsize);
			if (WARN_ON(type != BTRFS_FILE_EXTENT_INLINE) ||
			    WARN_ON(key.offset != 0) ||
			    WARN_ON(datal > fs_info->sectorsize)) {
				ret = -EUCLEAN;
				goto out;
			}

			ret = clone_copy_inline_extent(inode, path, &new_key,
						       drop_start, datal, size,
						       comp, buf, &trans);
			if (ret)
				goto out;
		}

		btrfs_release_path(path);

		/*
		 * Whenever we share an extent we update the last_reflink_trans
		 * of each inode to the current transaction. This is needed to
		 * make sure fsync does not log multiple checksum items with
		 * overlapping ranges (because some extent items might refer
		 * only to sections of the original extent). For the destination
		 * inode we do this regardless of the generation of the extents
		 * or even if they are inline extents or explicit holes, to make
		 * sure a full fsync does not skip them. For the source inode,
		 * we only need to update last_reflink_trans in case it's a new
		 * extent that is not a hole or an inline extent, to deal with
		 * the checksums problem on fsync.
		 */
		if (extent_gen == trans->transid && disko > 0)
			BTRFS_I(src)->last_reflink_trans = trans->transid;

		BTRFS_I(inode)->last_reflink_trans = trans->transid;

		last_dest_end = ALIGN(new_key.offset + datal,
				      fs_info->sectorsize);
		ret = clone_finish_inode_update(trans, inode, last_dest_end,
						destoff, olen, no_time_update);
		if (ret)
			goto out;
		if (new_key.offset + datal >= destoff + len)
			break;

		btrfs_release_path(path);
		key.offset = prev_extent_end;

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}

		cond_resched();
	}
	ret = 0;

	if (last_dest_end < destoff + len) {
		/*
		 * We have an implicit hole that fully or partially overlaps our
		 * cloning range at its end. This means that we either have the
		 * NO_HOLES feature enabled or the implicit hole happened due to
		 * mixing buffered and direct IO writes against this file.
		 */
		btrfs_release_path(path);

		/*
		 * When using NO_HOLES and we are cloning a range that covers
		 * only a hole (no extents) into a range beyond the current
		 * i_size, punching a hole in the target range will not create
		 * an extent map defining a hole, because the range starts at or
		 * beyond current i_size. If the file previously had an i_size
		 * greater than the new i_size set by this clone operation, we
		 * need to make sure the next fsync is a full fsync, so that it
		 * detects and logs a hole covering a range from the current
		 * i_size to the new i_size. If the clone range covers extents,
		 * besides a hole, then we know the full sync flag was already
		 * set by previous calls to btrfs_replace_file_extents() that
		 * replaced file extent items.
		 */
		if (last_dest_end >= i_size_read(inode))
			btrfs_set_inode_full_sync(BTRFS_I(inode));

		ret = btrfs_replace_file_extents(BTRFS_I(inode), path,
				last_dest_end, destoff + len - 1, NULL, &trans);
		if (ret)
			goto out;

		ret = clone_finish_inode_update(trans, inode, destoff + len,
						destoff, olen, no_time_update);
	}

out:
	btrfs_free_path(path);
	kvfree(buf);
	clear_bit(BTRFS_INODE_NO_DELALLOC_FLUSH, &BTRFS_I(inode)->runtime_flags);

	return ret;
}

static void btrfs_double_mmap_lock(struct inode *inode1, struct inode *inode2)
{
	if (inode1 < inode2)
		swap(inode1, inode2);
	down_write(&BTRFS_I(inode1)->i_mmap_lock);
	down_write_nested(&BTRFS_I(inode2)->i_mmap_lock, SINGLE_DEPTH_NESTING);
}

static void btrfs_double_mmap_unlock(struct inode *inode1, struct inode *inode2)
{
	up_write(&BTRFS_I(inode1)->i_mmap_lock);
	up_write(&BTRFS_I(inode2)->i_mmap_lock);
}

static int btrfs_extent_same_range(struct inode *src, u64 loff, u64 len,
				   struct inode *dst, u64 dst_loff)
{
	const u64 end = dst_loff + len - 1;
	struct extent_state *cached_state = NULL;
	struct btrfs_fs_info *fs_info = BTRFS_I(src)->root->fs_info;
	const u64 bs = fs_info->sectorsize;
	int ret;

	/*
	 * Lock destination range to serialize with concurrent readahead(), and
	 * we are safe from concurrency with relocation of source extents
	 * because we have already locked the inode's i_mmap_lock in exclusive
	 * mode.
	 */
	lock_extent(&BTRFS_I(dst)->io_tree, dst_loff, end, &cached_state);
	ret = btrfs_clone(src, dst, loff, len, ALIGN(len, bs), dst_loff, 1);
	unlock_extent(&BTRFS_I(dst)->io_tree, dst_loff, end, &cached_state);

	btrfs_btree_balance_dirty(fs_info);

	return ret;
}

static int btrfs_extent_same(struct inode *src, u64 loff, u64 olen,
			     struct inode *dst, u64 dst_loff)
{
	int ret = 0;
	u64 i, tail_len, chunk_count;
	struct btrfs_root *root_dst = BTRFS_I(dst)->root;

	spin_lock(&root_dst->root_item_lock);
	if (root_dst->send_in_progress) {
		btrfs_warn_rl(root_dst->fs_info,
"cannot deduplicate to root %llu while send operations are using it (%d in progress)",
			      btrfs_root_id(root_dst),
			      root_dst->send_in_progress);
		spin_unlock(&root_dst->root_item_lock);
		return -EAGAIN;
	}
	root_dst->dedupe_in_progress++;
	spin_unlock(&root_dst->root_item_lock);

	tail_len = olen % BTRFS_MAX_DEDUPE_LEN;
	chunk_count = div_u64(olen, BTRFS_MAX_DEDUPE_LEN);

	for (i = 0; i < chunk_count; i++) {
		ret = btrfs_extent_same_range(src, loff, BTRFS_MAX_DEDUPE_LEN,
					      dst, dst_loff);
		if (ret)
			goto out;

		loff += BTRFS_MAX_DEDUPE_LEN;
		dst_loff += BTRFS_MAX_DEDUPE_LEN;
	}

	if (tail_len > 0)
		ret = btrfs_extent_same_range(src, loff, tail_len, dst, dst_loff);
out:
	spin_lock(&root_dst->root_item_lock);
	root_dst->dedupe_in_progress--;
	spin_unlock(&root_dst->root_item_lock);

	return ret;
}

static noinline int btrfs_clone_files(struct file *file, struct file *file_src,
					u64 off, u64 olen, u64 destoff)
{
	struct extent_state *cached_state = NULL;
	struct inode *inode = file_inode(file);
	struct inode *src = file_inode(file_src);
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
	int ret;
	int wb_ret;
	u64 len = olen;
	u64 bs = fs_info->sectorsize;
	u64 end;

	/*
	 * VFS's generic_remap_file_range_prep() protects us from cloning the
	 * eof block into the middle of a file, which would result in corruption
	 * if the file size is not blocksize aligned. So we don't need to check
	 * for that case here.
	 */
	if (off + len == src->i_size)
		len = ALIGN(src->i_size, bs) - off;

	if (destoff > inode->i_size) {
		const u64 wb_start = ALIGN_DOWN(inode->i_size, bs);

		ret = btrfs_cont_expand(BTRFS_I(inode), inode->i_size, destoff);
		if (ret)
			return ret;
		/*
		 * We may have truncated the last block if the inode's size is
		 * not sector size aligned, so we need to wait for writeback to
		 * complete before proceeding further, otherwise we can race
		 * with cloning and attempt to increment a reference to an
		 * extent that no longer exists (writeback completed right after
		 * we found the previous extent covering eof and before we
		 * attempted to increment its reference count).
		 */
		ret = btrfs_wait_ordered_range(BTRFS_I(inode), wb_start,
					       destoff - wb_start);
		if (ret)
			return ret;
	}

	/*
	 * Lock destination range to serialize with concurrent readahead(), and
	 * we are safe from concurrency with relocation of source extents
	 * because we have already locked the inode's i_mmap_lock in exclusive
	 * mode.
	 */
	end = destoff + len - 1;
	lock_extent(&BTRFS_I(inode)->io_tree, destoff, end, &cached_state);
	ret = btrfs_clone(src, inode, off, olen, len, destoff, 0);
	unlock_extent(&BTRFS_I(inode)->io_tree, destoff, end, &cached_state);

	/*
	 * We may have copied an inline extent into a page of the destination
	 * range, so wait for writeback to complete before truncating pages
	 * from the page cache. This is a rare case.
	 */
	wb_ret = btrfs_wait_ordered_range(BTRFS_I(inode), destoff, len);
	ret = ret ? ret : wb_ret;
	/*
	 * Truncate page cache pages so that future reads will see the cloned
	 * data immediately and not the previous data.
	 */
	truncate_inode_pages_range(&inode->i_data,
				round_down(destoff, PAGE_SIZE),
				round_up(destoff + len, PAGE_SIZE) - 1);

	btrfs_btree_balance_dirty(fs_info);

	return ret;
}

static int btrfs_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				       struct file *file_out, loff_t pos_out,
				       loff_t *len, unsigned int remap_flags)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);
	u64 bs = BTRFS_I(inode_out)->root->fs_info->sectorsize;
	u64 wb_len;
	int ret;

	if (!(remap_flags & REMAP_FILE_DEDUP)) {
		struct btrfs_root *root_out = BTRFS_I(inode_out)->root;

		if (btrfs_root_readonly(root_out))
			return -EROFS;

		ASSERT(inode_in->i_sb == inode_out->i_sb);
	}

	/* Don't make the dst file partly checksummed */
	if ((BTRFS_I(inode_in)->flags & BTRFS_INODE_NODATASUM) !=
	    (BTRFS_I(inode_out)->flags & BTRFS_INODE_NODATASUM)) {
		return -EINVAL;
	}

	/*
	 * Now that the inodes are locked, we need to start writeback ourselves
	 * and can not rely on the writeback from the VFS's generic helper
	 * generic_remap_file_range_prep() because:
	 *
	 * 1) For compression we must call filemap_fdatawrite_range() range
	 *    twice (btrfs_fdatawrite_range() does it for us), and the generic
	 *    helper only calls it once;
	 *
	 * 2) filemap_fdatawrite_range(), called by the generic helper only
	 *    waits for the writeback to complete, i.e. for IO to be done, and
	 *    not for the ordered extents to complete. We need to wait for them
	 *    to complete so that new file extent items are in the fs tree.
	 */
	if (*len == 0 && !(remap_flags & REMAP_FILE_DEDUP))
		wb_len = ALIGN(inode_in->i_size, bs) - ALIGN_DOWN(pos_in, bs);
	else
		wb_len = ALIGN(*len, bs);

	/*
	 * Workaround to make sure NOCOW buffered write reach disk as NOCOW.
	 *
	 * Btrfs' back references do not have a block level granularity, they
	 * work at the whole extent level.
	 * NOCOW buffered write without data space reserved may not be able
	 * to fall back to CoW due to lack of data space, thus could cause
	 * data loss.
	 *
	 * Here we take a shortcut by flushing the whole inode, so that all
	 * nocow write should reach disk as nocow before we increase the
	 * reference of the extent. We could do better by only flushing NOCOW
	 * data, but that needs extra accounting.
	 *
	 * Also we don't need to check ASYNC_EXTENT, as async extent will be
	 * CoWed anyway, not affecting nocow part.
	 */
	ret = filemap_flush(inode_in->i_mapping);
	if (ret < 0)
		return ret;

	ret = btrfs_wait_ordered_range(BTRFS_I(inode_in), ALIGN_DOWN(pos_in, bs),
				       wb_len);
	if (ret < 0)
		return ret;
	ret = btrfs_wait_ordered_range(BTRFS_I(inode_out), ALIGN_DOWN(pos_out, bs),
				       wb_len);
	if (ret < 0)
		return ret;

	return generic_remap_file_range_prep(file_in, pos_in, file_out, pos_out,
					    len, remap_flags);
}

static bool file_sync_write(const struct file *file)
{
	if (file->f_flags & (__O_SYNC | O_DSYNC))
		return true;
	if (IS_SYNC(file_inode(file)))
		return true;

	return false;
}

loff_t btrfs_remap_file_range(struct file *src_file, loff_t off,
		struct file *dst_file, loff_t destoff, loff_t len,
		unsigned int remap_flags)
{
	struct inode *src_inode = file_inode(src_file);
	struct inode *dst_inode = file_inode(dst_file);
	bool same_inode = dst_inode == src_inode;
	int ret;

	if (remap_flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_ADVISORY))
		return -EINVAL;

	if (same_inode) {
		btrfs_inode_lock(BTRFS_I(src_inode), BTRFS_ILOCK_MMAP);
	} else {
		lock_two_nondirectories(src_inode, dst_inode);
		btrfs_double_mmap_lock(src_inode, dst_inode);
	}

	ret = btrfs_remap_file_range_prep(src_file, off, dst_file, destoff,
					  &len, remap_flags);
	if (ret < 0 || len == 0)
		goto out_unlock;

	if (remap_flags & REMAP_FILE_DEDUP)
		ret = btrfs_extent_same(src_inode, off, len, dst_inode, destoff);
	else
		ret = btrfs_clone_files(dst_file, src_file, off, len, destoff);

out_unlock:
	if (same_inode) {
		btrfs_inode_unlock(BTRFS_I(src_inode), BTRFS_ILOCK_MMAP);
	} else {
		btrfs_double_mmap_unlock(src_inode, dst_inode);
		unlock_two_nondirectories(src_inode, dst_inode);
	}

	/*
	 * If either the source or the destination file was opened with O_SYNC,
	 * O_DSYNC or has the S_SYNC attribute, fsync both the destination and
	 * source files/ranges, so that after a successful return (0) followed
	 * by a power failure results in the reflinked data to be readable from
	 * both files/ranges.
	 */
	if (ret == 0 && len > 0 &&
	    (file_sync_write(src_file) || file_sync_write(dst_file))) {
		ret = btrfs_sync_file(src_file, off, off + len - 1, 0);
		if (ret == 0)
			ret = btrfs_sync_file(dst_file, destoff,
					      destoff + len - 1, 0);
	}

	return ret < 0 ? ret : len;
}
