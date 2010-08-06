/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 2007 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include "ecryptfs_kernel.h"

/**
 * ecryptfs_write_lower
 * @ecryptfs_inode: The eCryptfs inode
 * @data: Data to write
 * @offset: Byte offset in the lower file to which to write the data
 * @size: Number of bytes from @data to write at @offset in the lower
 *        file
 *
 * Write data to the lower file.
 *
 * Returns bytes written on success; less than zero on error
 */
int ecryptfs_write_lower(struct inode *ecryptfs_inode, char *data,
			 loff_t offset, size_t size)
{
	struct ecryptfs_inode_info *inode_info;
	mm_segment_t fs_save;
	ssize_t rc;

	inode_info = ecryptfs_inode_to_private(ecryptfs_inode);
	mutex_lock(&inode_info->lower_file_mutex);
	BUG_ON(!inode_info->lower_file);
	inode_info->lower_file->f_pos = offset;
	fs_save = get_fs();
	set_fs(get_ds());
	rc = vfs_write(inode_info->lower_file, data, size,
		       &inode_info->lower_file->f_pos);
	set_fs(fs_save);
	mutex_unlock(&inode_info->lower_file_mutex);
	mark_inode_dirty_sync(ecryptfs_inode);
	return rc;
}

/**
 * ecryptfs_write_lower_page_segment
 * @ecryptfs_inode: The eCryptfs inode
 * @page_for_lower: The page containing the data to be written to the
 *                  lower file
 * @offset_in_page: The offset in the @page_for_lower from which to
 *                  start writing the data
 * @size: The amount of data from @page_for_lower to write to the
 *        lower file
 *
 * Determines the byte offset in the file for the given page and
 * offset within the page, maps the page, and makes the call to write
 * the contents of @page_for_lower to the lower inode.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_write_lower_page_segment(struct inode *ecryptfs_inode,
				      struct page *page_for_lower,
				      size_t offset_in_page, size_t size)
{
	char *virt;
	loff_t offset;
	int rc;

	offset = ((((loff_t)page_for_lower->index) << PAGE_CACHE_SHIFT)
		  + offset_in_page);
	virt = kmap(page_for_lower);
	rc = ecryptfs_write_lower(ecryptfs_inode, virt, offset, size);
	if (rc > 0)
		rc = 0;
	kunmap(page_for_lower);
	return rc;
}

/**
 * ecryptfs_write
 * @ecryptfs_inode: The eCryptfs file into which to write
 * @data: Virtual address where data to write is located
 * @offset: Offset in the eCryptfs file at which to begin writing the
 *          data from @data
 * @size: The number of bytes to write from @data
 *
 * Write an arbitrary amount of data to an arbitrary location in the
 * eCryptfs inode page cache. This is done on a page-by-page, and then
 * by an extent-by-extent, basis; individual extents are encrypted and
 * written to the lower page cache (via VFS writes). This function
 * takes care of all the address translation to locations in the lower
 * filesystem; it also handles truncate events, writing out zeros
 * where necessary.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_write(struct inode *ecryptfs_inode, char *data, loff_t offset,
		   size_t size)
{
	struct page *ecryptfs_page;
	struct ecryptfs_crypt_stat *crypt_stat;
	char *ecryptfs_page_virt;
	loff_t ecryptfs_file_size = i_size_read(ecryptfs_inode);
	loff_t data_offset = 0;
	loff_t pos;
	int rc = 0;

	crypt_stat = &ecryptfs_inode_to_private(ecryptfs_inode)->crypt_stat;
	/*
	 * if we are writing beyond current size, then start pos
	 * at the current size - we'll fill in zeros from there.
	 */
	if (offset > ecryptfs_file_size)
		pos = ecryptfs_file_size;
	else
		pos = offset;
	while (pos < (offset + size)) {
		pgoff_t ecryptfs_page_idx = (pos >> PAGE_CACHE_SHIFT);
		size_t start_offset_in_page = (pos & ~PAGE_CACHE_MASK);
		size_t num_bytes = (PAGE_CACHE_SIZE - start_offset_in_page);
		size_t total_remaining_bytes = ((offset + size) - pos);

		if (num_bytes > total_remaining_bytes)
			num_bytes = total_remaining_bytes;
		if (pos < offset) {
			/* remaining zeros to write, up to destination offset */
			size_t total_remaining_zeros = (offset - pos);

			if (num_bytes > total_remaining_zeros)
				num_bytes = total_remaining_zeros;
		}
		ecryptfs_page = ecryptfs_get_locked_page(ecryptfs_inode,
							 ecryptfs_page_idx);
		if (IS_ERR(ecryptfs_page)) {
			rc = PTR_ERR(ecryptfs_page);
			printk(KERN_ERR "%s: Error getting page at "
			       "index [%ld] from eCryptfs inode "
			       "mapping; rc = [%d]\n", __func__,
			       ecryptfs_page_idx, rc);
			goto out;
		}
		ecryptfs_page_virt = kmap_atomic(ecryptfs_page, KM_USER0);

		/*
		 * pos: where we're now writing, offset: where the request was
		 * If current pos is before request, we are filling zeros
		 * If we are at or beyond request, we are writing the *data*
		 * If we're in a fresh page beyond eof, zero it in either case
		 */
		if (pos < offset || !start_offset_in_page) {
			/* We are extending past the previous end of the file.
			 * Fill in zero values to the end of the page */
			memset(((char *)ecryptfs_page_virt
				+ start_offset_in_page), 0,
				PAGE_CACHE_SIZE - start_offset_in_page);
		}

		/* pos >= offset, we are now writing the data request */
		if (pos >= offset) {
			memcpy(((char *)ecryptfs_page_virt
				+ start_offset_in_page),
			       (data + data_offset), num_bytes);
			data_offset += num_bytes;
		}
		kunmap_atomic(ecryptfs_page_virt, KM_USER0);
		flush_dcache_page(ecryptfs_page);
		SetPageUptodate(ecryptfs_page);
		unlock_page(ecryptfs_page);
		if (crypt_stat->flags & ECRYPTFS_ENCRYPTED)
			rc = ecryptfs_encrypt_page(ecryptfs_page);
		else
			rc = ecryptfs_write_lower_page_segment(ecryptfs_inode,
						ecryptfs_page,
						start_offset_in_page,
						data_offset);
		page_cache_release(ecryptfs_page);
		if (rc) {
			printk(KERN_ERR "%s: Error encrypting "
			       "page; rc = [%d]\n", __func__, rc);
			goto out;
		}
		pos += num_bytes;
	}
	if ((offset + size) > ecryptfs_file_size) {
		i_size_write(ecryptfs_inode, (offset + size));
		if (crypt_stat->flags & ECRYPTFS_ENCRYPTED) {
			rc = ecryptfs_write_inode_size_to_metadata(
								ecryptfs_inode);
			if (rc) {
				printk(KERN_ERR	"Problem with "
				       "ecryptfs_write_inode_size_to_metadata; "
				       "rc = [%d]\n", rc);
				goto out;
			}
		}
	}
out:
	return rc;
}

/**
 * ecryptfs_read_lower
 * @data: The read data is stored here by this function
 * @offset: Byte offset in the lower file from which to read the data
 * @size: Number of bytes to read from @offset of the lower file and
 *        store into @data
 * @ecryptfs_inode: The eCryptfs inode
 *
 * Read @size bytes of data at byte offset @offset from the lower
 * inode into memory location @data.
 *
 * Returns bytes read on success; 0 on EOF; less than zero on error
 */
int ecryptfs_read_lower(char *data, loff_t offset, size_t size,
			struct inode *ecryptfs_inode)
{
	struct ecryptfs_inode_info *inode_info =
		ecryptfs_inode_to_private(ecryptfs_inode);
	mm_segment_t fs_save;
	ssize_t rc;

	mutex_lock(&inode_info->lower_file_mutex);
	BUG_ON(!inode_info->lower_file);
	inode_info->lower_file->f_pos = offset;
	fs_save = get_fs();
	set_fs(get_ds());
	rc = vfs_read(inode_info->lower_file, data, size,
		      &inode_info->lower_file->f_pos);
	set_fs(fs_save);
	mutex_unlock(&inode_info->lower_file_mutex);
	return rc;
}

/**
 * ecryptfs_read_lower_page_segment
 * @page_for_ecryptfs: The page into which data for eCryptfs will be
 *                     written
 * @offset_in_page: Offset in @page_for_ecryptfs from which to start
 *                  writing
 * @size: The number of bytes to write into @page_for_ecryptfs
 * @ecryptfs_inode: The eCryptfs inode
 *
 * Determines the byte offset in the file for the given page and
 * offset within the page, maps the page, and makes the call to read
 * the contents of @page_for_ecryptfs from the lower inode.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_read_lower_page_segment(struct page *page_for_ecryptfs,
				     pgoff_t page_index,
				     size_t offset_in_page, size_t size,
				     struct inode *ecryptfs_inode)
{
	char *virt;
	loff_t offset;
	int rc;

	offset = ((((loff_t)page_index) << PAGE_CACHE_SHIFT) + offset_in_page);
	virt = kmap(page_for_ecryptfs);
	rc = ecryptfs_read_lower(virt, offset, size, ecryptfs_inode);
	if (rc > 0)
		rc = 0;
	kunmap(page_for_ecryptfs);
	flush_dcache_page(page_for_ecryptfs);
	return rc;
}

#if 0
/**
 * ecryptfs_read
 * @data: The virtual address into which to write the data read (and
 *        possibly decrypted) from the lower file
 * @offset: The offset in the decrypted view of the file from which to
 *          read into @data
 * @size: The number of bytes to read into @data
 * @ecryptfs_file: The eCryptfs file from which to read
 *
 * Read an arbitrary amount of data from an arbitrary location in the
 * eCryptfs page cache. This is done on an extent-by-extent basis;
 * individual extents are decrypted and read from the lower page
 * cache (via VFS reads). This function takes care of all the
 * address translation to locations in the lower filesystem.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_read(char *data, loff_t offset, size_t size,
		  struct file *ecryptfs_file)
{
	struct inode *ecryptfs_inode = ecryptfs_file->f_dentry->d_inode;
	struct page *ecryptfs_page;
	char *ecryptfs_page_virt;
	loff_t ecryptfs_file_size = i_size_read(ecryptfs_inode);
	loff_t data_offset = 0;
	loff_t pos;
	int rc = 0;

	if ((offset + size) > ecryptfs_file_size) {
		rc = -EINVAL;
		printk(KERN_ERR "%s: Attempt to read data past the end of the "
			"file; offset = [%lld]; size = [%td]; "
		       "ecryptfs_file_size = [%lld]\n",
		       __func__, offset, size, ecryptfs_file_size);
		goto out;
	}
	pos = offset;
	while (pos < (offset + size)) {
		pgoff_t ecryptfs_page_idx = (pos >> PAGE_CACHE_SHIFT);
		size_t start_offset_in_page = (pos & ~PAGE_CACHE_MASK);
		size_t num_bytes = (PAGE_CACHE_SIZE - start_offset_in_page);
		size_t total_remaining_bytes = ((offset + size) - pos);

		if (num_bytes > total_remaining_bytes)
			num_bytes = total_remaining_bytes;
		ecryptfs_page = ecryptfs_get_locked_page(ecryptfs_inode,
							 ecryptfs_page_idx);
		if (IS_ERR(ecryptfs_page)) {
			rc = PTR_ERR(ecryptfs_page);
			printk(KERN_ERR "%s: Error getting page at "
			       "index [%ld] from eCryptfs inode "
			       "mapping; rc = [%d]\n", __func__,
			       ecryptfs_page_idx, rc);
			goto out;
		}
		ecryptfs_page_virt = kmap_atomic(ecryptfs_page, KM_USER0);
		memcpy((data + data_offset),
		       ((char *)ecryptfs_page_virt + start_offset_in_page),
		       num_bytes);
		kunmap_atomic(ecryptfs_page_virt, KM_USER0);
		flush_dcache_page(ecryptfs_page);
		SetPageUptodate(ecryptfs_page);
		unlock_page(ecryptfs_page);
		page_cache_release(ecryptfs_page);
		pos += num_bytes;
		data_offset += num_bytes;
	}
out:
	return rc;
}
#endif  /*  0  */
