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
 * changes to f2fs.  Extended attributes cannot be used because (a) f2fs limits
 * the total size of an inode's xattr entries to 4096 bytes, which wouldn't be
 * enough for even a single Merkle tree block, and (b) f2fs encryption doesn't
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

static inline loff_t f2fs_verity_metadata_pos(const struct inode *inode)
{
	return round_up(inode->i_size, 65536);
}

/*
 * Read some verity metadata from the inode.  __vfs_read() can't be used because
 * we need to read beyond i_size.
 */
static int pagecache_read(struct inode *inode, void *buf, size_t count,
			  loff_t pos)
{
	while (count) {
		size_t n = min_t(size_t, count,
				 PAGE_SIZE - offset_in_page(pos));
		struct page *page;
		void *addr;

		page = read_mapping_page(inode->i_mapping, pos >> PAGE_SHIFT,
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
 * Write some verity metadata to the inode for FS_IOC_ENABLE_VERITY.
 * kernel_write() can't be used because the file descriptor is readonly.
 */
static int pagecache_write(struct inode *inode, const void *buf, size_t count,
			   loff_t pos)
{
	if (pos + count > inode->i_sb->s_maxbytes)
		return -EFBIG;

	while (count) {
		size_t n = min_t(size_t, count,
				 PAGE_SIZE - offset_in_page(pos));
		struct page *page;
		void *fsdata;
		void *addr;
		int res;

		res = pagecache_write_begin(NULL, inode->i_mapping, pos, n, 0,
					    &page, &fsdata);
		if (res)
			return res;

		addr = kmap_atomic(page);
		memcpy(addr + offset_in_page(pos), buf, n);
		kunmap_atomic(addr);

		res = pagecache_write_end(NULL, inode->i_mapping, pos, n, n,
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

/*
 * Format of f2fs verity xattr.  This points to the location of the verity
 * descriptor within the file data rather than containing it directly because
 * the verity descriptor *must* be encrypted when f2fs encryption is used.  But,
 * f2fs encryption does not encrypt xattrs.
 */
struct fsverity_descriptor_location {
	__le32 version;
	__le32 size;
	__le64 pos;
};

static int f2fs_begin_enable_verity(struct file *filp)
{
	struct inode *inode = file_inode(filp);
	int err;

	if (f2fs_verity_in_progress(inode))
		return -EBUSY;

	if (f2fs_is_atomic_file(inode) || f2fs_is_volatile_file(inode))
		return -EOPNOTSUPP;

	/*
	 * Since the file was opened readonly, we have to initialize the quotas
	 * here and not rely on ->open() doing it.  This must be done before
	 * evicting the inline data.
	 */
	err = dquot_initialize(inode);
	if (err)
		return err;

	err = f2fs_convert_inline_inode(inode);
	if (err)
		return err;

	set_inode_flag(inode, FI_VERITY_IN_PROGRESS);
	return 0;
}

static int f2fs_end_enable_verity(struct file *filp, const void *desc,
				  size_t desc_size, u64 merkle_tree_size)
{
	struct inode *inode = file_inode(filp);
	u64 desc_pos = f2fs_verity_metadata_pos(inode) + merkle_tree_size;
	struct fsverity_descriptor_location dloc = {
		.version = cpu_to_le32(1),
		.size = cpu_to_le32(desc_size),
		.pos = cpu_to_le64(desc_pos),
	};
	int err = 0;

	if (desc != NULL) {
		/* Succeeded; write the verity descriptor. */
		err = pagecache_write(inode, desc, desc_size, desc_pos);

		/* Write all pages before clearing FI_VERITY_IN_PROGRESS. */
		if (!err)
			err = filemap_write_and_wait(inode->i_mapping);
	}

	/* If we failed, truncate anything we wrote past i_size. */
	if (desc == NULL || err)
		f2fs_truncate(inode);

	clear_inode_flag(inode, FI_VERITY_IN_PROGRESS);

	if (desc != NULL && !err) {
		err = f2fs_setxattr(inode, F2FS_XATTR_INDEX_VERITY,
				    F2FS_XATTR_NAME_VERITY, &dloc, sizeof(dloc),
				    NULL, XATTR_CREATE);
		if (!err) {
			file_set_verity(inode);
			f2fs_set_inode_flags(inode);
			f2fs_mark_inode_dirty_sync(inode, true);
		}
	}
	return err;
}

static int f2fs_get_verity_descriptor(struct inode *inode, void *buf,
				      size_t buf_size)
{
	struct fsverity_descriptor_location dloc;
	int res;
	u32 size;
	u64 pos;

	/* Get the descriptor location */
	res = f2fs_getxattr(inode, F2FS_XATTR_INDEX_VERITY,
			    F2FS_XATTR_NAME_VERITY, &dloc, sizeof(dloc), NULL);
	if (res < 0 && res != -ERANGE)
		return res;
	if (res != sizeof(dloc) || dloc.version != cpu_to_le32(1)) {
		f2fs_warn(F2FS_I_SB(inode), "unknown verity xattr format");
		return -EINVAL;
	}
	size = le32_to_cpu(dloc.size);
	pos = le64_to_cpu(dloc.pos);

	/* Get the descriptor */
	if (pos + size < pos || pos + size > inode->i_sb->s_maxbytes ||
	    pos < f2fs_verity_metadata_pos(inode) || size > INT_MAX) {
		f2fs_warn(F2FS_I_SB(inode), "invalid verity xattr");
		return -EFSCORRUPTED;
	}
	if (buf_size) {
		if (size > buf_size)
			return -ERANGE;
		res = pagecache_read(inode, buf, size, pos);
		if (res)
			return res;
	}
	return size;
}

/*
 * Prefetch some pages from the file's Merkle tree.
 *
 * This is basically a stripped-down version of __do_page_cache_readahead()
 * which works on pages past i_size.
 */
static void f2fs_merkle_tree_readahead(struct address_space *mapping,
				       pgoff_t start_index, unsigned long count)
{
	LIST_HEAD(pages);
	unsigned int nr_pages = 0;
	struct page *page;
	pgoff_t index;
	struct blk_plug plug;

	for (index = start_index; index < start_index + count; index++) {
		rcu_read_lock();
		page = radix_tree_lookup(&mapping->i_pages, index);
		rcu_read_unlock();
		if (!page || radix_tree_exceptional_entry(page)) {
			page = __page_cache_alloc(readahead_gfp_mask(mapping));
			if (!page)
				break;
			page->index = index;
			list_add(&page->lru, &pages);
			nr_pages++;
		}
	}
	blk_start_plug(&plug);
	f2fs_mpage_readpages(mapping, &pages, NULL, nr_pages, true);
	blk_finish_plug(&plug);
}

static struct page *f2fs_read_merkle_tree_page(struct inode *inode,
					       pgoff_t index,
					       unsigned long num_ra_pages)
{
	struct page *page;

	index += f2fs_verity_metadata_pos(inode) >> PAGE_SHIFT;

	page = find_get_page_flags(inode->i_mapping, index, FGP_ACCESSED);
	if (!page || !PageUptodate(page)) {
		if (page)
			put_page(page);
		else if (num_ra_pages > 1)
			f2fs_merkle_tree_readahead(inode->i_mapping, index,
						   num_ra_pages);
		page = read_mapping_page(inode->i_mapping, index, NULL);
	}
	return page;
}

static int f2fs_write_merkle_tree_block(struct inode *inode, const void *buf,
					u64 index, int log_blocksize)
{
	loff_t pos = f2fs_verity_metadata_pos(inode) + (index << log_blocksize);

	return pagecache_write(inode, buf, 1 << log_blocksize, pos);
}

const struct fsverity_operations f2fs_verityops = {
	.begin_enable_verity	= f2fs_begin_enable_verity,
	.end_enable_verity	= f2fs_end_enable_verity,
	.get_verity_descriptor	= f2fs_get_verity_descriptor,
	.read_merkle_tree_page	= f2fs_read_merkle_tree_page,
	.write_merkle_tree_block = f2fs_write_merkle_tree_block,
};
