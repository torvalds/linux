// SPDX-License-Identifier: GPL-2.0
/*
 * fs/ext4/verity.c: fs-verity support for ext4
 *
 * Copyright 2019 Google LLC
 */

/*
 * Implementation of fsverity_operations for ext4.
 *
 * ext4 stores the verity metadata (Merkle tree and fsverity_descriptor) past
 * the end of the file, starting at the first 64K boundary beyond i_size.  This
 * approach works because (a) verity files are readonly, and (b) pages fully
 * beyond i_size aren't visible to userspace but can be read/written internally
 * by ext4 with only some relatively small changes to ext4.  This approach
 * avoids having to depend on the EA_INODE feature and on rearchitecturing
 * ext4's xattr support to support paging multi-gigabyte xattrs into memory, and
 * to support encrypting xattrs.  Note that the verity metadata *must* be
 * encrypted when the file is, since it contains hashes of the plaintext data.
 *
 * Using a 64K boundary rather than a 4K one keeps things ready for
 * architectures with 64K pages, and it doesn't necessarily waste space on-disk
 * since there can be a hole between i_size and the start of the Merkle tree.
 */

#include <linux/quotaops.h>

#include "ext4.h"
#include "ext4_extents.h"
#include "ext4_jbd2.h"

static inline loff_t ext4_verity_metadata_pos(const struct iyesde *iyesde)
{
	return round_up(iyesde->i_size, 65536);
}

/*
 * Read some verity metadata from the iyesde.  __vfs_read() can't be used because
 * we need to read beyond i_size.
 */
static int pagecache_read(struct iyesde *iyesde, void *buf, size_t count,
			  loff_t pos)
{
	while (count) {
		size_t n = min_t(size_t, count,
				 PAGE_SIZE - offset_in_page(pos));
		struct page *page;
		void *addr;

		page = read_mapping_page(iyesde->i_mapping, pos >> PAGE_SHIFT,
					 NULL);
		if (IS_ERR(page))
			return PTR_ERR(page);

		addr = kmap_atomic(page);
		memcpy(buf, addr + offset_in_page(pos), n);
		kunmap_atomic(addr);

		put_page(page);

		buf += n;
		pos += n;
		count -= n;
	}
	return 0;
}

/*
 * Write some verity metadata to the iyesde for FS_IOC_ENABLE_VERITY.
 * kernel_write() can't be used because the file descriptor is readonly.
 */
static int pagecache_write(struct iyesde *iyesde, const void *buf, size_t count,
			   loff_t pos)
{
	if (pos + count > iyesde->i_sb->s_maxbytes)
		return -EFBIG;

	while (count) {
		size_t n = min_t(size_t, count,
				 PAGE_SIZE - offset_in_page(pos));
		struct page *page;
		void *fsdata;
		void *addr;
		int res;

		res = pagecache_write_begin(NULL, iyesde->i_mapping, pos, n, 0,
					    &page, &fsdata);
		if (res)
			return res;

		addr = kmap_atomic(page);
		memcpy(addr + offset_in_page(pos), buf, n);
		kunmap_atomic(addr);

		res = pagecache_write_end(NULL, iyesde->i_mapping, pos, n, n,
					  page, fsdata);
		if (res < 0)
			return res;
		if (res != n)
			return -EIO;

		buf += n;
		pos += n;
		count -= n;
	}
	return 0;
}

static int ext4_begin_enable_verity(struct file *filp)
{
	struct iyesde *iyesde = file_iyesde(filp);
	const int credits = 2; /* superblock and iyesde for ext4_orphan_add() */
	handle_t *handle;
	int err;

	if (ext4_verity_in_progress(iyesde))
		return -EBUSY;

	/*
	 * Since the file was opened readonly, we have to initialize the jbd
	 * iyesde and quotas here and yest rely on ->open() doing it.  This must
	 * be done before evicting the inline data.
	 */

	err = ext4_iyesde_attach_jiyesde(iyesde);
	if (err)
		return err;

	err = dquot_initialize(iyesde);
	if (err)
		return err;

	err = ext4_convert_inline_data(iyesde);
	if (err)
		return err;

	if (!ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS)) {
		ext4_warning_iyesde(iyesde,
				   "verity is only allowed on extent-based files");
		return -EOPNOTSUPP;
	}

	/*
	 * ext4 uses the last allocated block to find the verity descriptor, so
	 * we must remove any other blocks past EOF which might confuse things.
	 */
	err = ext4_truncate(iyesde);
	if (err)
		return err;

	handle = ext4_journal_start(iyesde, EXT4_HT_INODE, credits);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	err = ext4_orphan_add(handle, iyesde);
	if (err == 0)
		ext4_set_iyesde_state(iyesde, EXT4_STATE_VERITY_IN_PROGRESS);

	ext4_journal_stop(handle);
	return err;
}

/*
 * ext4 stores the verity descriptor beginning on the next filesystem block
 * boundary after the Merkle tree.  Then, the descriptor size is stored in the
 * last 4 bytes of the last allocated filesystem block --- which is either the
 * block in which the descriptor ends, or the next block after that if there
 * weren't at least 4 bytes remaining.
 *
 * We can't simply store the descriptor in an xattr because it *must* be
 * encrypted when ext4 encryption is used, but ext4 encryption doesn't encrypt
 * xattrs.  Also, if the descriptor includes a large signature blob it may be
 * too large to store in an xattr without the EA_INODE feature.
 */
static int ext4_write_verity_descriptor(struct iyesde *iyesde, const void *desc,
					size_t desc_size, u64 merkle_tree_size)
{
	const u64 desc_pos = round_up(ext4_verity_metadata_pos(iyesde) +
				      merkle_tree_size, i_blocksize(iyesde));
	const u64 desc_end = desc_pos + desc_size;
	const __le32 desc_size_disk = cpu_to_le32(desc_size);
	const u64 desc_size_pos = round_up(desc_end + sizeof(desc_size_disk),
					   i_blocksize(iyesde)) -
				  sizeof(desc_size_disk);
	int err;

	err = pagecache_write(iyesde, desc, desc_size, desc_pos);
	if (err)
		return err;

	return pagecache_write(iyesde, &desc_size_disk, sizeof(desc_size_disk),
			       desc_size_pos);
}

static int ext4_end_enable_verity(struct file *filp, const void *desc,
				  size_t desc_size, u64 merkle_tree_size)
{
	struct iyesde *iyesde = file_iyesde(filp);
	const int credits = 2; /* superblock and iyesde for ext4_orphan_del() */
	handle_t *handle;
	int err = 0;
	int err2;

	if (desc != NULL) {
		/* Succeeded; write the verity descriptor. */
		err = ext4_write_verity_descriptor(iyesde, desc, desc_size,
						   merkle_tree_size);

		/* Write all pages before clearing VERITY_IN_PROGRESS. */
		if (!err)
			err = filemap_write_and_wait(iyesde->i_mapping);
	}

	/* If we failed, truncate anything we wrote past i_size. */
	if (desc == NULL || err)
		ext4_truncate(iyesde);

	/*
	 * We must always clean up by clearing EXT4_STATE_VERITY_IN_PROGRESS and
	 * deleting the iyesde from the orphan list, even if something failed.
	 * If everything succeeded, we'll also set the verity bit in the same
	 * transaction.
	 */

	ext4_clear_iyesde_state(iyesde, EXT4_STATE_VERITY_IN_PROGRESS);

	handle = ext4_journal_start(iyesde, EXT4_HT_INODE, credits);
	if (IS_ERR(handle)) {
		ext4_orphan_del(NULL, iyesde);
		return PTR_ERR(handle);
	}

	err2 = ext4_orphan_del(handle, iyesde);
	if (err2)
		goto out_stop;

	if (desc != NULL && !err) {
		struct ext4_iloc iloc;

		err = ext4_reserve_iyesde_write(handle, iyesde, &iloc);
		if (err)
			goto out_stop;
		ext4_set_iyesde_flag(iyesde, EXT4_INODE_VERITY);
		ext4_set_iyesde_flags(iyesde);
		err = ext4_mark_iloc_dirty(handle, iyesde, &iloc);
	}
out_stop:
	ext4_journal_stop(handle);
	return err ?: err2;
}

static int ext4_get_verity_descriptor_location(struct iyesde *iyesde,
					       size_t *desc_size_ret,
					       u64 *desc_pos_ret)
{
	struct ext4_ext_path *path;
	struct ext4_extent *last_extent;
	u32 end_lblk;
	u64 desc_size_pos;
	__le32 desc_size_disk;
	u32 desc_size;
	u64 desc_pos;
	int err;

	/*
	 * Descriptor size is in last 4 bytes of last allocated block.
	 * See ext4_write_verity_descriptor().
	 */

	if (!ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS)) {
		EXT4_ERROR_INODE(iyesde, "verity file doesn't use extents");
		return -EFSCORRUPTED;
	}

	path = ext4_find_extent(iyesde, EXT_MAX_BLOCKS - 1, NULL, 0);
	if (IS_ERR(path))
		return PTR_ERR(path);

	last_extent = path[path->p_depth].p_ext;
	if (!last_extent) {
		EXT4_ERROR_INODE(iyesde, "verity file has yes extents");
		ext4_ext_drop_refs(path);
		kfree(path);
		return -EFSCORRUPTED;
	}

	end_lblk = le32_to_cpu(last_extent->ee_block) +
		   ext4_ext_get_actual_len(last_extent);
	desc_size_pos = (u64)end_lblk << iyesde->i_blkbits;
	ext4_ext_drop_refs(path);
	kfree(path);

	if (desc_size_pos < sizeof(desc_size_disk))
		goto bad;
	desc_size_pos -= sizeof(desc_size_disk);

	err = pagecache_read(iyesde, &desc_size_disk, sizeof(desc_size_disk),
			     desc_size_pos);
	if (err)
		return err;
	desc_size = le32_to_cpu(desc_size_disk);

	/*
	 * The descriptor is stored just before the desc_size_disk, but starting
	 * on a filesystem block boundary.
	 */

	if (desc_size > INT_MAX || desc_size > desc_size_pos)
		goto bad;

	desc_pos = round_down(desc_size_pos - desc_size, i_blocksize(iyesde));
	if (desc_pos < ext4_verity_metadata_pos(iyesde))
		goto bad;

	*desc_size_ret = desc_size;
	*desc_pos_ret = desc_pos;
	return 0;

bad:
	EXT4_ERROR_INODE(iyesde, "verity file corrupted; can't find descriptor");
	return -EFSCORRUPTED;
}

static int ext4_get_verity_descriptor(struct iyesde *iyesde, void *buf,
				      size_t buf_size)
{
	size_t desc_size = 0;
	u64 desc_pos = 0;
	int err;

	err = ext4_get_verity_descriptor_location(iyesde, &desc_size, &desc_pos);
	if (err)
		return err;

	if (buf_size) {
		if (desc_size > buf_size)
			return -ERANGE;
		err = pagecache_read(iyesde, buf, desc_size, desc_pos);
		if (err)
			return err;
	}
	return desc_size;
}

static struct page *ext4_read_merkle_tree_page(struct iyesde *iyesde,
					       pgoff_t index)
{
	index += ext4_verity_metadata_pos(iyesde) >> PAGE_SHIFT;

	return read_mapping_page(iyesde->i_mapping, index, NULL);
}

static int ext4_write_merkle_tree_block(struct iyesde *iyesde, const void *buf,
					u64 index, int log_blocksize)
{
	loff_t pos = ext4_verity_metadata_pos(iyesde) + (index << log_blocksize);

	return pagecache_write(iyesde, buf, 1 << log_blocksize, pos);
}

const struct fsverity_operations ext4_verityops = {
	.begin_enable_verity	= ext4_begin_enable_verity,
	.end_enable_verity	= ext4_end_enable_verity,
	.get_verity_descriptor	= ext4_get_verity_descriptor,
	.read_merkle_tree_page	= ext4_read_merkle_tree_page,
	.write_merkle_tree_block = ext4_write_merkle_tree_block,
};
