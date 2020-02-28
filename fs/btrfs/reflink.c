// SPDX-License-Identifier: GPL-2.0

#include <linux/iversion.h>
#include "ctree.h"
#include "reflink.h"
#include "transaction.h"

#define BTRFS_MAX_DEDUPE_LEN	SZ_16M

static int clone_finish_inode_update(struct btrfs_trans_handle *trans,
				     struct inode *inode,
				     u64 endoff,
				     const u64 destoff,
				     const u64 olen,
				     int no_time_update)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	inode_inc_iversion(inode);
	if (!no_time_update)
		inode->i_mtime = inode->i_ctime = current_time(inode);
	/*
	 * We round up to the block size at eof when determining which
	 * extents to clone above, but shouldn't round up the file size.
	 */
	if (endoff > destoff + olen)
		endoff = destoff + olen;
	if (endoff > inode->i_size) {
		i_size_write(inode, endoff);
		btrfs_inode_safe_disk_i_size_write(inode, 0);
	}

	ret = btrfs_update_inode(trans, root, inode);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		goto out;
	}
	ret = btrfs_end_transaction(trans);
out:
	return ret;
}

/*
 * Make sure we do not end up inserting an inline extent into a file that has
 * already other (non-inline) extents. If a file has an inline extent it can
 * not have any other extents and the (single) inline extent must start at the
 * file offset 0. Failing to respect these rules will lead to file corruption,
 * resulting in EIO errors on read/write operations, hitting BUG_ON's in mm, etc
 *
 * We can have extents that have been already written to disk or we can have
 * dirty ranges still in delalloc, in which case the extent maps and items are
 * created only when we run delalloc, and the delalloc ranges might fall outside
 * the range we are currently locking in the inode's io tree. So we check the
 * inode's i_size because of that (i_size updates are done while holding the
 * i_mutex, which we are holding here).
 * We also check to see if the inode has a size not greater than "datal" but has
 * extents beyond it, due to an fallocate with FALLOC_FL_KEEP_SIZE (and we are
 * protected against such concurrent fallocate calls by the i_mutex).
 *
 * If the file has no extents but a size greater than datal, do not allow the
 * copy because we would need turn the inline extent into a non-inline one (even
 * with NO_HOLES enabled). If we find our destination inode only has one inline
 * extent, just overwrite it with the source inline extent if its size is less
 * than the source extent's size, or we could copy the source inline extent's
 * data into the destination inode's inline extent if the later is greater then
 * the former.
 */
static int clone_copy_inline_extent(struct inode *dst,
				    struct btrfs_trans_handle *trans,
				    struct btrfs_path *path,
				    struct btrfs_key *new_key,
				    const u64 drop_start,
				    const u64 datal,
				    const u64 size,
				    const char *inline_data)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dst->i_sb);
	struct btrfs_root *root = BTRFS_I(dst)->root;
	const u64 aligned_end = ALIGN(new_key->offset + datal,
				      fs_info->sectorsize);
	int ret;
	struct btrfs_key key;

	if (new_key->offset > 0)
		return -EOPNOTSUPP;

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
			ASSERT(key.offset > 0);
			return -EOPNOTSUPP;
		}
	} else if (i_size_read(dst) <= datal) {
		struct btrfs_file_extent_item *ei;
		u64 ext_len;

		/*
		 * If the file size is <= datal, make sure there are no other
		 * extents following (can happen do to an fallocate call with
		 * the flag FALLOC_FL_KEEP_SIZE).
		 */
		ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_file_extent_item);
		/*
		 * If it's an inline extent, it can not have other extents
		 * following it.
		 */
		if (btrfs_file_extent_type(path->nodes[0], ei) ==
		    BTRFS_FILE_EXTENT_INLINE)
			goto copy_inline_extent;

		ext_len = btrfs_file_extent_num_bytes(path->nodes[0], ei);
		if (ext_len > aligned_end)
			return -EOPNOTSUPP;

		ret = btrfs_next_item(root, path);
		if (ret < 0) {
			return ret;
		} else if (ret == 0) {
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == btrfs_ino(BTRFS_I(dst)) &&
			    key.type == BTRFS_EXTENT_DATA_KEY)
				return -EOPNOTSUPP;
		}
	}

copy_inline_extent:
	/*
	 * We have no extent items, or we have an extent at offset 0 which may
	 * or may not be inlined. All these cases are dealt the same way.
	 */
	if (i_size_read(dst) > datal) {
		/*
		 * If the destination inode has an inline extent.
		 * This would require copying the data from the source inline
		 * extent into the beginning of the destination's inline extent.
		 * But this is really complex, both extents can be compressed
		 * or just one of them, which would require decompressing and
		 * re-compressing data (which could increase the new compressed
		 * size, not allowing the compressed data to fit anymore in an
		 * inline extent).
		 * So just don't support this case for now (it should be rare,
		 * we are not really saving space when cloning inline extents).
		 */
		return -EOPNOTSUPP;
	}

	btrfs_release_path(path);
	ret = btrfs_drop_extents(trans, root, dst, drop_start, aligned_end, 1);
	if (ret)
		return ret;
	ret = btrfs_insert_empty_item(trans, root, path, new_key, size);
	if (ret)
		return ret;

	write_extent_buffer(path->nodes[0], inline_data,
			    btrfs_item_ptr_offset(path->nodes[0],
						  path->slots[0]),
			    size);
	inode_add_bytes(dst, datal);
	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(dst)->runtime_flags);

	return 0;
}

/**
 * btrfs_clone() - clone a range from inode file to another
 *
 * @src: Inode to clone from
 * @inode: Inode to clone to
 * @off: Offset within source to start clone from
 * @olen: Original length, passed by user, of range to clone
 * @olen_aligned: Block-aligned value of olen
 * @destoff: Offset within @inode to start clone
 * @no_time_update: Whether to update mtime/ctime on the target inode
 */
static int btrfs_clone(struct inode *src, struct inode *inode,
		       const u64 off, const u64 olen, const u64 olen_aligned,
		       const u64 destoff, int no_time_update)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
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
		u64 next_key_min_offset = key.offset + 1;
		struct btrfs_file_extent_item *extent;
		int type;
		u32 size;
		struct btrfs_key new_key;
		u64 disko = 0, diskl = 0;
		u64 datao = 0, datal = 0;
		u64 drop_start;

		/* Note the key will change type as we walk through the tree */
		path->leave_spinning = 1;
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
		 */
		if (key.offset + datal <= off) {
			path->slots[0]++;
			goto process_slot;
		} else if (key.offset >= off + len) {
			break;
		}
		next_key_min_offset = key.offset + datal;
		size = btrfs_item_size_nr(leaf, slot);
		read_extent_buffer(leaf, buf, btrfs_item_ptr_offset(leaf, slot),
				   size);

		btrfs_release_path(path);
		path->leave_spinning = 0;

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
			struct btrfs_clone_extent_info clone_info;

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
			clone_info.item_size = size;
			ret = btrfs_punch_hole_range(inode, path, drop_start,
					new_key.offset + datal - 1, &clone_info,
					&trans);
			if (ret)
				goto out;
		} else if (type == BTRFS_FILE_EXTENT_INLINE) {
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
			if (key.offset != 0 || datal > fs_info->sectorsize)
				return -EUCLEAN;

			/*
			 * If our extent is inline, we know we will drop or
			 * adjust at most 1 extent item in the destination root.
			 *
			 * 1 - adjusting old extent (we may have to split it)
			 * 1 - add new extent
			 * 1 - inode update
			 */
			trans = btrfs_start_transaction(root, 3);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				goto out;
			}

			ret = clone_copy_inline_extent(inode, trans, path,
						       &new_key, drop_start,
						       datal, size, buf);
			if (ret) {
				if (ret != -EOPNOTSUPP)
					btrfs_abort_transaction(trans, ret);
				btrfs_end_transaction(trans);
				goto out;
			}
		}

		btrfs_release_path(path);

		last_dest_end = ALIGN(new_key.offset + datal,
				      fs_info->sectorsize);
		ret = clone_finish_inode_update(trans, inode, last_dest_end,
						destoff, olen, no_time_update);
		if (ret)
			goto out;
		if (new_key.offset + datal >= destoff + len)
			break;

		btrfs_release_path(path);
		key.offset = next_key_min_offset;

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
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
		path->leave_spinning = 0;

		ret = btrfs_punch_hole_range(inode, path, last_dest_end,
				destoff + len - 1, NULL, &trans);
		if (ret)
			goto out;

		ret = clone_finish_inode_update(trans, inode, destoff + len,
						destoff, olen, no_time_update);
	}

out:
	btrfs_free_path(path);
	kvfree(buf);
	return ret;
}

static void btrfs_double_extent_unlock(struct inode *inode1, u64 loff1,
				       struct inode *inode2, u64 loff2, u64 len)
{
	unlock_extent(&BTRFS_I(inode1)->io_tree, loff1, loff1 + len - 1);
	unlock_extent(&BTRFS_I(inode2)->io_tree, loff2, loff2 + len - 1);
}

static void btrfs_double_extent_lock(struct inode *inode1, u64 loff1,
				     struct inode *inode2, u64 loff2, u64 len)
{
	if (inode1 < inode2) {
		swap(inode1, inode2);
		swap(loff1, loff2);
	} else if (inode1 == inode2 && loff2 < loff1) {
		swap(loff1, loff2);
	}
	lock_extent(&BTRFS_I(inode1)->io_tree, loff1, loff1 + len - 1);
	lock_extent(&BTRFS_I(inode2)->io_tree, loff2, loff2 + len - 1);
}

static int btrfs_extent_same_range(struct inode *src, u64 loff, u64 len,
				   struct inode *dst, u64 dst_loff)
{
	const u64 bs = BTRFS_I(src)->root->fs_info->sb->s_blocksize;
	int ret;

	/*
	 * Lock destination range to serialize with concurrent readpages() and
	 * source range to serialize with relocation.
	 */
	btrfs_double_extent_lock(src, loff, dst, dst_loff, len);
	ret = btrfs_clone(src, dst, loff, len, ALIGN(len, bs), dst_loff, 1);
	btrfs_double_extent_unlock(src, loff, dst, dst_loff, len);

	return ret;
}

static int btrfs_extent_same(struct inode *src, u64 loff, u64 olen,
			     struct inode *dst, u64 dst_loff)
{
	int ret;
	u64 i, tail_len, chunk_count;
	struct btrfs_root *root_dst = BTRFS_I(dst)->root;

	spin_lock(&root_dst->root_item_lock);
	if (root_dst->send_in_progress) {
		btrfs_warn_rl(root_dst->fs_info,
"cannot deduplicate to root %llu while send operations are using it (%d in progress)",
			      root_dst->root_key.objectid,
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
	struct inode *inode = file_inode(file);
	struct inode *src = file_inode(file_src);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret;
	u64 len = olen;
	u64 bs = fs_info->sb->s_blocksize;

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

		ret = btrfs_cont_expand(inode, inode->i_size, destoff);
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
		ret = btrfs_wait_ordered_range(inode, wb_start,
					       destoff - wb_start);
		if (ret)
			return ret;
	}

	/*
	 * Lock destination range to serialize with concurrent readpages() and
	 * source range to serialize with relocation.
	 */
	btrfs_double_extent_lock(src, off, inode, destoff, len);
	ret = btrfs_clone(src, inode, off, olen, len, destoff, 0);
	btrfs_double_extent_unlock(src, off, inode, destoff, len);
	/*
	 * Truncate page cache pages so that future reads will see the cloned
	 * data immediately and not the previous data.
	 */
	truncate_inode_pages_range(&inode->i_data,
				round_down(destoff, PAGE_SIZE),
				round_up(destoff + len, PAGE_SIZE) - 1);

	return ret;
}

static int btrfs_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				       struct file *file_out, loff_t pos_out,
				       loff_t *len, unsigned int remap_flags)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);
	u64 bs = BTRFS_I(inode_out)->root->fs_info->sb->s_blocksize;
	bool same_inode = inode_out == inode_in;
	u64 wb_len;
	int ret;

	if (!(remap_flags & REMAP_FILE_DEDUP)) {
		struct btrfs_root *root_out = BTRFS_I(inode_out)->root;

		if (btrfs_root_readonly(root_out))
			return -EROFS;

		if (file_in->f_path.mnt != file_out->f_path.mnt ||
		    inode_in->i_sb != inode_out->i_sb)
			return -EXDEV;
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
	 * Since we don't lock ranges, wait for ongoing lockless dio writes (as
	 * any in progress could create its ordered extents after we wait for
	 * existing ordered extents below).
	 */
	inode_dio_wait(inode_in);
	if (!same_inode)
		inode_dio_wait(inode_out);

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

	ret = btrfs_wait_ordered_range(inode_in, ALIGN_DOWN(pos_in, bs),
				       wb_len);
	if (ret < 0)
		return ret;
	ret = btrfs_wait_ordered_range(inode_out, ALIGN_DOWN(pos_out, bs),
				       wb_len);
	if (ret < 0)
		return ret;

	return generic_remap_file_range_prep(file_in, pos_in, file_out, pos_out,
					    len, remap_flags);
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

	if (same_inode)
		inode_lock(src_inode);
	else
		lock_two_nondirectories(src_inode, dst_inode);

	ret = btrfs_remap_file_range_prep(src_file, off, dst_file, destoff,
					  &len, remap_flags);
	if (ret < 0 || len == 0)
		goto out_unlock;

	if (remap_flags & REMAP_FILE_DEDUP)
		ret = btrfs_extent_same(src_inode, off, len, dst_inode, destoff);
	else
		ret = btrfs_clone_files(dst_file, src_file, off, len, destoff);

out_unlock:
	if (same_inode)
		inode_unlock(src_inode);
	else
		unlock_two_nondirectories(src_inode, dst_inode);

	return ret < 0 ? ret : len;
}
