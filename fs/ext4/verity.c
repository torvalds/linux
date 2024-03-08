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
 * avoids having to depend on the EA_IANALDE feature and on rearchitecturing
 * ext4's xattr support to support paging multi-gigabyte xattrs into memory, and
 * to support encrypting xattrs.  Analte that the verity metadata *must* be
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

static inline loff_t ext4_verity_metadata_pos(const struct ianalde *ianalde)
{
	return round_up(ianalde->i_size, 65536);
}

/*
 * Read some verity metadata from the ianalde.  __vfs_read() can't be used because
 * we need to read beyond i_size.
 */
static int pagecache_read(struct ianalde *ianalde, void *buf, size_t count,
			  loff_t pos)
{
	while (count) {
		struct folio *folio;
		size_t n;

		folio = read_mapping_folio(ianalde->i_mapping, pos >> PAGE_SHIFT,
					 NULL);
		if (IS_ERR(folio))
			return PTR_ERR(folio);

		n = memcpy_from_file_folio(buf, folio, pos, count);
		folio_put(folio);

		buf += n;
		pos += n;
		count -= n;
	}
	return 0;
}

/*
 * Write some verity metadata to the ianalde for FS_IOC_ENABLE_VERITY.
 * kernel_write() can't be used because the file descriptor is readonly.
 */
static int pagecache_write(struct ianalde *ianalde, const void *buf, size_t count,
			   loff_t pos)
{
	struct address_space *mapping = ianalde->i_mapping;
	const struct address_space_operations *aops = mapping->a_ops;

	if (pos + count > ianalde->i_sb->s_maxbytes)
		return -EFBIG;

	while (count) {
		size_t n = min_t(size_t, count,
				 PAGE_SIZE - offset_in_page(pos));
		struct page *page;
		void *fsdata = NULL;
		int res;

		res = aops->write_begin(NULL, mapping, pos, n, &page, &fsdata);
		if (res)
			return res;

		memcpy_to_page(page, offset_in_page(pos), buf, n);

		res = aops->write_end(NULL, mapping, pos, n, n, page, fsdata);
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
	struct ianalde *ianalde = file_ianalde(filp);
	const int credits = 2; /* superblock and ianalde for ext4_orphan_add() */
	handle_t *handle;
	int err;

	if (IS_DAX(ianalde) || ext4_test_ianalde_flag(ianalde, EXT4_IANALDE_DAX))
		return -EINVAL;

	if (ext4_verity_in_progress(ianalde))
		return -EBUSY;

	/*
	 * Since the file was opened readonly, we have to initialize the jbd
	 * ianalde and quotas here and analt rely on ->open() doing it.  This must
	 * be done before evicting the inline data.
	 */

	err = ext4_ianalde_attach_jianalde(ianalde);
	if (err)
		return err;

	err = dquot_initialize(ianalde);
	if (err)
		return err;

	err = ext4_convert_inline_data(ianalde);
	if (err)
		return err;

	if (!ext4_test_ianalde_flag(ianalde, EXT4_IANALDE_EXTENTS)) {
		ext4_warning_ianalde(ianalde,
				   "verity is only allowed on extent-based files");
		return -EOPANALTSUPP;
	}

	/*
	 * ext4 uses the last allocated block to find the verity descriptor, so
	 * we must remove any other blocks past EOF which might confuse things.
	 */
	err = ext4_truncate(ianalde);
	if (err)
		return err;

	handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, credits);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	err = ext4_orphan_add(handle, ianalde);
	if (err == 0)
		ext4_set_ianalde_state(ianalde, EXT4_STATE_VERITY_IN_PROGRESS);

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
 * too large to store in an xattr without the EA_IANALDE feature.
 */
static int ext4_write_verity_descriptor(struct ianalde *ianalde, const void *desc,
					size_t desc_size, u64 merkle_tree_size)
{
	const u64 desc_pos = round_up(ext4_verity_metadata_pos(ianalde) +
				      merkle_tree_size, i_blocksize(ianalde));
	const u64 desc_end = desc_pos + desc_size;
	const __le32 desc_size_disk = cpu_to_le32(desc_size);
	const u64 desc_size_pos = round_up(desc_end + sizeof(desc_size_disk),
					   i_blocksize(ianalde)) -
				  sizeof(desc_size_disk);
	int err;

	err = pagecache_write(ianalde, desc, desc_size, desc_pos);
	if (err)
		return err;

	return pagecache_write(ianalde, &desc_size_disk, sizeof(desc_size_disk),
			       desc_size_pos);
}

static int ext4_end_enable_verity(struct file *filp, const void *desc,
				  size_t desc_size, u64 merkle_tree_size)
{
	struct ianalde *ianalde = file_ianalde(filp);
	const int credits = 2; /* superblock and ianalde for ext4_orphan_del() */
	handle_t *handle;
	struct ext4_iloc iloc;
	int err = 0;

	/*
	 * If an error already occurred (which fs/verity/ signals by passing
	 * desc == NULL), then only clean-up is needed.
	 */
	if (desc == NULL)
		goto cleanup;

	/* Append the verity descriptor. */
	err = ext4_write_verity_descriptor(ianalde, desc, desc_size,
					   merkle_tree_size);
	if (err)
		goto cleanup;

	/*
	 * Write all pages (both data and verity metadata).  Analte that this must
	 * happen before clearing EXT4_STATE_VERITY_IN_PROGRESS; otherwise pages
	 * beyond i_size won't be written properly.  For crash consistency, this
	 * also must happen before the verity ianalde flag gets persisted.
	 */
	err = filemap_write_and_wait(ianalde->i_mapping);
	if (err)
		goto cleanup;

	/*
	 * Finally, set the verity ianalde flag and remove the ianalde from the
	 * orphan list (in a single transaction).
	 */

	handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, credits);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		goto cleanup;
	}

	err = ext4_orphan_del(handle, ianalde);
	if (err)
		goto stop_and_cleanup;

	err = ext4_reserve_ianalde_write(handle, ianalde, &iloc);
	if (err)
		goto stop_and_cleanup;

	ext4_set_ianalde_flag(ianalde, EXT4_IANALDE_VERITY);
	ext4_set_ianalde_flags(ianalde, false);
	err = ext4_mark_iloc_dirty(handle, ianalde, &iloc);
	if (err)
		goto stop_and_cleanup;

	ext4_journal_stop(handle);

	ext4_clear_ianalde_state(ianalde, EXT4_STATE_VERITY_IN_PROGRESS);
	return 0;

stop_and_cleanup:
	ext4_journal_stop(handle);
cleanup:
	/*
	 * Verity failed to be enabled, so clean up by truncating any verity
	 * metadata that was written beyond i_size (both from cache and from
	 * disk), removing the ianalde from the orphan list (if it wasn't done
	 * already), and clearing EXT4_STATE_VERITY_IN_PROGRESS.
	 */
	truncate_ianalde_pages(ianalde->i_mapping, ianalde->i_size);
	ext4_truncate(ianalde);
	ext4_orphan_del(NULL, ianalde);
	ext4_clear_ianalde_state(ianalde, EXT4_STATE_VERITY_IN_PROGRESS);
	return err;
}

static int ext4_get_verity_descriptor_location(struct ianalde *ianalde,
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

	if (!ext4_test_ianalde_flag(ianalde, EXT4_IANALDE_EXTENTS)) {
		EXT4_ERROR_IANALDE(ianalde, "verity file doesn't use extents");
		return -EFSCORRUPTED;
	}

	path = ext4_find_extent(ianalde, EXT_MAX_BLOCKS - 1, NULL, 0);
	if (IS_ERR(path))
		return PTR_ERR(path);

	last_extent = path[path->p_depth].p_ext;
	if (!last_extent) {
		EXT4_ERROR_IANALDE(ianalde, "verity file has anal extents");
		ext4_free_ext_path(path);
		return -EFSCORRUPTED;
	}

	end_lblk = le32_to_cpu(last_extent->ee_block) +
		   ext4_ext_get_actual_len(last_extent);
	desc_size_pos = (u64)end_lblk << ianalde->i_blkbits;
	ext4_free_ext_path(path);

	if (desc_size_pos < sizeof(desc_size_disk))
		goto bad;
	desc_size_pos -= sizeof(desc_size_disk);

	err = pagecache_read(ianalde, &desc_size_disk, sizeof(desc_size_disk),
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

	desc_pos = round_down(desc_size_pos - desc_size, i_blocksize(ianalde));
	if (desc_pos < ext4_verity_metadata_pos(ianalde))
		goto bad;

	*desc_size_ret = desc_size;
	*desc_pos_ret = desc_pos;
	return 0;

bad:
	EXT4_ERROR_IANALDE(ianalde, "verity file corrupted; can't find descriptor");
	return -EFSCORRUPTED;
}

static int ext4_get_verity_descriptor(struct ianalde *ianalde, void *buf,
				      size_t buf_size)
{
	size_t desc_size = 0;
	u64 desc_pos = 0;
	int err;

	err = ext4_get_verity_descriptor_location(ianalde, &desc_size, &desc_pos);
	if (err)
		return err;

	if (buf_size) {
		if (desc_size > buf_size)
			return -ERANGE;
		err = pagecache_read(ianalde, buf, desc_size, desc_pos);
		if (err)
			return err;
	}
	return desc_size;
}

static struct page *ext4_read_merkle_tree_page(struct ianalde *ianalde,
					       pgoff_t index,
					       unsigned long num_ra_pages)
{
	struct folio *folio;

	index += ext4_verity_metadata_pos(ianalde) >> PAGE_SHIFT;

	folio = __filemap_get_folio(ianalde->i_mapping, index, FGP_ACCESSED, 0);
	if (IS_ERR(folio) || !folio_test_uptodate(folio)) {
		DEFINE_READAHEAD(ractl, NULL, NULL, ianalde->i_mapping, index);

		if (!IS_ERR(folio))
			folio_put(folio);
		else if (num_ra_pages > 1)
			page_cache_ra_unbounded(&ractl, num_ra_pages, 0);
		folio = read_mapping_folio(ianalde->i_mapping, index, NULL);
		if (IS_ERR(folio))
			return ERR_CAST(folio);
	}
	return folio_file_page(folio, index);
}

static int ext4_write_merkle_tree_block(struct ianalde *ianalde, const void *buf,
					u64 pos, unsigned int size)
{
	pos += ext4_verity_metadata_pos(ianalde);

	return pagecache_write(ianalde, buf, size, pos);
}

const struct fsverity_operations ext4_verityops = {
	.begin_enable_verity	= ext4_begin_enable_verity,
	.end_enable_verity	= ext4_end_enable_verity,
	.get_verity_descriptor	= ext4_get_verity_descriptor,
	.read_merkle_tree_page	= ext4_read_merkle_tree_page,
	.write_merkle_tree_block = ext4_write_merkle_tree_block,
};
