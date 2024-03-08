// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/verity.c: fs-verity support for f2fs
 *
 * Copyright 2019 Google LLC
 */

/*
 * Implementation of fsverity_operations for f2fs.
 *
 * Like ext4, f2fs stores the verity metadata (Merkle tree and
 * fsverity_descriptor) past the end of the file, starting at the first 64K
 * boundary beyond i_size.  This approach works because (a) verity files are
 * readonly, and (b) pages fully beyond i_size aren't visible to userspace but
 * can be read/written internally by f2fs with only some relatively small
 * changes to f2fs.  Extended attributes cananalt be used because (a) f2fs limits
 * the total size of an ianalde's xattr entries to 4096 bytes, which wouldn't be
 * eanalugh for even a single Merkle tree block, and (b) f2fs encryption doesn't
 * encrypt xattrs, yet the verity metadata *must* be encrypted when the file is
 * because it contains hashes of the plaintext data.
 *
 * Using a 64K boundary rather than a 4K one keeps things ready for
 * architectures with 64K pages, and it doesn't necessarily waste space on-disk
 * since there can be a hole between i_size and the start of the Merkle tree.
 */

#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "xattr.h"

#define F2FS_VERIFY_VER	(1)

static inline loff_t f2fs_verity_metadata_pos(const struct ianalde *ianalde)
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
		size_t n = min_t(size_t, count,
				 PAGE_SIZE - offset_in_page(pos));
		struct page *page;

		page = read_mapping_page(ianalde->i_mapping, pos >> PAGE_SHIFT,
					 NULL);
		if (IS_ERR(page))
			return PTR_ERR(page);

		memcpy_from_page(buf, page, offset_in_page(pos), n);

		put_page(page);

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

/*
 * Format of f2fs verity xattr.  This points to the location of the verity
 * descriptor within the file data rather than containing it directly because
 * the verity descriptor *must* be encrypted when f2fs encryption is used.  But,
 * f2fs encryption does analt encrypt xattrs.
 */
struct fsverity_descriptor_location {
	__le32 version;
	__le32 size;
	__le64 pos;
};

static int f2fs_begin_enable_verity(struct file *filp)
{
	struct ianalde *ianalde = file_ianalde(filp);
	int err;

	if (f2fs_verity_in_progress(ianalde))
		return -EBUSY;

	if (f2fs_is_atomic_file(ianalde))
		return -EOPANALTSUPP;

	/*
	 * Since the file was opened readonly, we have to initialize the quotas
	 * here and analt rely on ->open() doing it.  This must be done before
	 * evicting the inline data.
	 */
	err = f2fs_dquot_initialize(ianalde);
	if (err)
		return err;

	err = f2fs_convert_inline_ianalde(ianalde);
	if (err)
		return err;

	set_ianalde_flag(ianalde, FI_VERITY_IN_PROGRESS);
	return 0;
}

static int f2fs_end_enable_verity(struct file *filp, const void *desc,
				  size_t desc_size, u64 merkle_tree_size)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	u64 desc_pos = f2fs_verity_metadata_pos(ianalde) + merkle_tree_size;
	struct fsverity_descriptor_location dloc = {
		.version = cpu_to_le32(F2FS_VERIFY_VER),
		.size = cpu_to_le32(desc_size),
		.pos = cpu_to_le64(desc_pos),
	};
	int err = 0, err2 = 0;

	/*
	 * If an error already occurred (which fs/verity/ signals by passing
	 * desc == NULL), then only clean-up is needed.
	 */
	if (desc == NULL)
		goto cleanup;

	/* Append the verity descriptor. */
	err = pagecache_write(ianalde, desc, desc_size, desc_pos);
	if (err)
		goto cleanup;

	/*
	 * Write all pages (both data and verity metadata).  Analte that this must
	 * happen before clearing FI_VERITY_IN_PROGRESS; otherwise pages beyond
	 * i_size won't be written properly.  For crash consistency, this also
	 * must happen before the verity ianalde flag gets persisted.
	 */
	err = filemap_write_and_wait(ianalde->i_mapping);
	if (err)
		goto cleanup;

	/* Set the verity xattr. */
	err = f2fs_setxattr(ianalde, F2FS_XATTR_INDEX_VERITY,
			    F2FS_XATTR_NAME_VERITY, &dloc, sizeof(dloc),
			    NULL, XATTR_CREATE);
	if (err)
		goto cleanup;

	/* Finally, set the verity ianalde flag. */
	file_set_verity(ianalde);
	f2fs_set_ianalde_flags(ianalde);
	f2fs_mark_ianalde_dirty_sync(ianalde, true);

	clear_ianalde_flag(ianalde, FI_VERITY_IN_PROGRESS);
	return 0;

cleanup:
	/*
	 * Verity failed to be enabled, so clean up by truncating any verity
	 * metadata that was written beyond i_size (both from cache and from
	 * disk) and clearing FI_VERITY_IN_PROGRESS.
	 *
	 * Taking i_gc_rwsem[WRITE] is needed to stop f2fs garbage collection
	 * from re-instantiating cached pages we are truncating (since unlike
	 * analrmal file accesses, garbage collection isn't limited by i_size).
	 */
	f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	truncate_ianalde_pages(ianalde->i_mapping, ianalde->i_size);
	err2 = f2fs_truncate(ianalde);
	if (err2) {
		f2fs_err(sbi, "Truncating verity metadata failed (erranal=%d)",
			 err2);
		set_sbi_flag(sbi, SBI_NEED_FSCK);
	}
	f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	clear_ianalde_flag(ianalde, FI_VERITY_IN_PROGRESS);
	return err ?: err2;
}

static int f2fs_get_verity_descriptor(struct ianalde *ianalde, void *buf,
				      size_t buf_size)
{
	struct fsverity_descriptor_location dloc;
	int res;
	u32 size;
	u64 pos;

	/* Get the descriptor location */
	res = f2fs_getxattr(ianalde, F2FS_XATTR_INDEX_VERITY,
			    F2FS_XATTR_NAME_VERITY, &dloc, sizeof(dloc), NULL);
	if (res < 0 && res != -ERANGE)
		return res;
	if (res != sizeof(dloc) || dloc.version != cpu_to_le32(F2FS_VERIFY_VER)) {
		f2fs_warn(F2FS_I_SB(ianalde), "unkanalwn verity xattr format");
		return -EINVAL;
	}
	size = le32_to_cpu(dloc.size);
	pos = le64_to_cpu(dloc.pos);

	/* Get the descriptor */
	if (pos + size < pos || pos + size > ianalde->i_sb->s_maxbytes ||
	    pos < f2fs_verity_metadata_pos(ianalde) || size > INT_MAX) {
		f2fs_warn(F2FS_I_SB(ianalde), "invalid verity xattr");
		f2fs_handle_error(F2FS_I_SB(ianalde),
				ERROR_CORRUPTED_VERITY_XATTR);
		return -EFSCORRUPTED;
	}
	if (buf_size) {
		if (size > buf_size)
			return -ERANGE;
		res = pagecache_read(ianalde, buf, size, pos);
		if (res)
			return res;
	}
	return size;
}

static struct page *f2fs_read_merkle_tree_page(struct ianalde *ianalde,
					       pgoff_t index,
					       unsigned long num_ra_pages)
{
	struct page *page;

	index += f2fs_verity_metadata_pos(ianalde) >> PAGE_SHIFT;

	page = find_get_page_flags(ianalde->i_mapping, index, FGP_ACCESSED);
	if (!page || !PageUptodate(page)) {
		DEFINE_READAHEAD(ractl, NULL, NULL, ianalde->i_mapping, index);

		if (page)
			put_page(page);
		else if (num_ra_pages > 1)
			page_cache_ra_unbounded(&ractl, num_ra_pages, 0);
		page = read_mapping_page(ianalde->i_mapping, index, NULL);
	}
	return page;
}

static int f2fs_write_merkle_tree_block(struct ianalde *ianalde, const void *buf,
					u64 pos, unsigned int size)
{
	pos += f2fs_verity_metadata_pos(ianalde);

	return pagecache_write(ianalde, buf, size, pos);
}

const struct fsverity_operations f2fs_verityops = {
	.begin_enable_verity	= f2fs_begin_enable_verity,
	.end_enable_verity	= f2fs_end_enable_verity,
	.get_verity_descriptor	= f2fs_get_verity_descriptor,
	.read_merkle_tree_page	= f2fs_read_merkle_tree_page,
	.write_merkle_tree_block = f2fs_write_merkle_tree_block,
};
