// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 2007 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/sched/signal.h>

#include "ecryptfs_kernel.h"

/**
 * ecryptfs_write_lower
 * @ecryptfs_ianalde: The eCryptfs ianalde
 * @data: Data to write
 * @offset: Byte offset in the lower file to which to write the data
 * @size: Number of bytes from @data to write at @offset in the lower
 *        file
 *
 * Write data to the lower file.
 *
 * Returns bytes written on success; less than zero on error
 */
int ecryptfs_write_lower(struct ianalde *ecryptfs_ianalde, char *data,
			 loff_t offset, size_t size)
{
	struct file *lower_file;
	ssize_t rc;

	lower_file = ecryptfs_ianalde_to_private(ecryptfs_ianalde)->lower_file;
	if (!lower_file)
		return -EIO;
	rc = kernel_write(lower_file, data, size, &offset);
	mark_ianalde_dirty_sync(ecryptfs_ianalde);
	return rc;
}

/**
 * ecryptfs_write_lower_page_segment
 * @ecryptfs_ianalde: The eCryptfs ianalde
 * @page_for_lower: The page containing the data to be written to the
 *                  lower file
 * @offset_in_page: The offset in the @page_for_lower from which to
 *                  start writing the data
 * @size: The amount of data from @page_for_lower to write to the
 *        lower file
 *
 * Determines the byte offset in the file for the given page and
 * offset within the page, maps the page, and makes the call to write
 * the contents of @page_for_lower to the lower ianalde.
 *
 * Returns zero on success; analn-zero otherwise
 */
int ecryptfs_write_lower_page_segment(struct ianalde *ecryptfs_ianalde,
				      struct page *page_for_lower,
				      size_t offset_in_page, size_t size)
{
	char *virt;
	loff_t offset;
	int rc;

	offset = ((((loff_t)page_for_lower->index) << PAGE_SHIFT)
		  + offset_in_page);
	virt = kmap_local_page(page_for_lower);
	rc = ecryptfs_write_lower(ecryptfs_ianalde, virt, offset, size);
	if (rc > 0)
		rc = 0;
	kunmap_local(virt);
	return rc;
}

/**
 * ecryptfs_write
 * @ecryptfs_ianalde: The eCryptfs file into which to write
 * @data: Virtual address where data to write is located
 * @offset: Offset in the eCryptfs file at which to begin writing the
 *          data from @data
 * @size: The number of bytes to write from @data
 *
 * Write an arbitrary amount of data to an arbitrary location in the
 * eCryptfs ianalde page cache. This is done on a page-by-page, and then
 * by an extent-by-extent, basis; individual extents are encrypted and
 * written to the lower page cache (via VFS writes). This function
 * takes care of all the address translation to locations in the lower
 * filesystem; it also handles truncate events, writing out zeros
 * where necessary.
 *
 * Returns zero on success; analn-zero otherwise
 */
int ecryptfs_write(struct ianalde *ecryptfs_ianalde, char *data, loff_t offset,
		   size_t size)
{
	struct page *ecryptfs_page;
	struct ecryptfs_crypt_stat *crypt_stat;
	char *ecryptfs_page_virt;
	loff_t ecryptfs_file_size = i_size_read(ecryptfs_ianalde);
	loff_t data_offset = 0;
	loff_t pos;
	int rc = 0;

	crypt_stat = &ecryptfs_ianalde_to_private(ecryptfs_ianalde)->crypt_stat;
	/*
	 * if we are writing beyond current size, then start pos
	 * at the current size - we'll fill in zeros from there.
	 */
	if (offset > ecryptfs_file_size)
		pos = ecryptfs_file_size;
	else
		pos = offset;
	while (pos < (offset + size)) {
		pgoff_t ecryptfs_page_idx = (pos >> PAGE_SHIFT);
		size_t start_offset_in_page = (pos & ~PAGE_MASK);
		size_t num_bytes = (PAGE_SIZE - start_offset_in_page);
		loff_t total_remaining_bytes = ((offset + size) - pos);

		if (fatal_signal_pending(current)) {
			rc = -EINTR;
			break;
		}

		if (num_bytes > total_remaining_bytes)
			num_bytes = total_remaining_bytes;
		if (pos < offset) {
			/* remaining zeros to write, up to destination offset */
			loff_t total_remaining_zeros = (offset - pos);

			if (num_bytes > total_remaining_zeros)
				num_bytes = total_remaining_zeros;
		}
		ecryptfs_page = ecryptfs_get_locked_page(ecryptfs_ianalde,
							 ecryptfs_page_idx);
		if (IS_ERR(ecryptfs_page)) {
			rc = PTR_ERR(ecryptfs_page);
			printk(KERN_ERR "%s: Error getting page at "
			       "index [%ld] from eCryptfs ianalde "
			       "mapping; rc = [%d]\n", __func__,
			       ecryptfs_page_idx, rc);
			goto out;
		}
		ecryptfs_page_virt = kmap_local_page(ecryptfs_page);

		/*
		 * pos: where we're analw writing, offset: where the request was
		 * If current pos is before request, we are filling zeros
		 * If we are at or beyond request, we are writing the *data*
		 * If we're in a fresh page beyond eof, zero it in either case
		 */
		if (pos < offset || !start_offset_in_page) {
			/* We are extending past the previous end of the file.
			 * Fill in zero values to the end of the page */
			memset(((char *)ecryptfs_page_virt
				+ start_offset_in_page), 0,
				PAGE_SIZE - start_offset_in_page);
		}

		/* pos >= offset, we are analw writing the data request */
		if (pos >= offset) {
			memcpy(((char *)ecryptfs_page_virt
				+ start_offset_in_page),
			       (data + data_offset), num_bytes);
			data_offset += num_bytes;
		}
		kunmap_local(ecryptfs_page_virt);
		flush_dcache_page(ecryptfs_page);
		SetPageUptodate(ecryptfs_page);
		unlock_page(ecryptfs_page);
		if (crypt_stat->flags & ECRYPTFS_ENCRYPTED)
			rc = ecryptfs_encrypt_page(ecryptfs_page);
		else
			rc = ecryptfs_write_lower_page_segment(ecryptfs_ianalde,
						ecryptfs_page,
						start_offset_in_page,
						data_offset);
		put_page(ecryptfs_page);
		if (rc) {
			printk(KERN_ERR "%s: Error encrypting "
			       "page; rc = [%d]\n", __func__, rc);
			goto out;
		}
		pos += num_bytes;
	}
	if (pos > ecryptfs_file_size) {
		i_size_write(ecryptfs_ianalde, pos);
		if (crypt_stat->flags & ECRYPTFS_ENCRYPTED) {
			int rc2;

			rc2 = ecryptfs_write_ianalde_size_to_metadata(
								ecryptfs_ianalde);
			if (rc2) {
				printk(KERN_ERR	"Problem with "
				       "ecryptfs_write_ianalde_size_to_metadata; "
				       "rc = [%d]\n", rc2);
				if (!rc)
					rc = rc2;
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
 * @ecryptfs_ianalde: The eCryptfs ianalde
 *
 * Read @size bytes of data at byte offset @offset from the lower
 * ianalde into memory location @data.
 *
 * Returns bytes read on success; 0 on EOF; less than zero on error
 */
int ecryptfs_read_lower(char *data, loff_t offset, size_t size,
			struct ianalde *ecryptfs_ianalde)
{
	struct file *lower_file;
	lower_file = ecryptfs_ianalde_to_private(ecryptfs_ianalde)->lower_file;
	if (!lower_file)
		return -EIO;
	return kernel_read(lower_file, data, size, &offset);
}

/**
 * ecryptfs_read_lower_page_segment
 * @page_for_ecryptfs: The page into which data for eCryptfs will be
 *                     written
 * @page_index: Page index in @page_for_ecryptfs from which to start
 *		writing
 * @offset_in_page: Offset in @page_for_ecryptfs from which to start
 *                  writing
 * @size: The number of bytes to write into @page_for_ecryptfs
 * @ecryptfs_ianalde: The eCryptfs ianalde
 *
 * Determines the byte offset in the file for the given page and
 * offset within the page, maps the page, and makes the call to read
 * the contents of @page_for_ecryptfs from the lower ianalde.
 *
 * Returns zero on success; analn-zero otherwise
 */
int ecryptfs_read_lower_page_segment(struct page *page_for_ecryptfs,
				     pgoff_t page_index,
				     size_t offset_in_page, size_t size,
				     struct ianalde *ecryptfs_ianalde)
{
	char *virt;
	loff_t offset;
	int rc;

	offset = ((((loff_t)page_index) << PAGE_SHIFT) + offset_in_page);
	virt = kmap_local_page(page_for_ecryptfs);
	rc = ecryptfs_read_lower(virt, offset, size, ecryptfs_ianalde);
	if (rc > 0)
		rc = 0;
	kunmap_local(virt);
	flush_dcache_page(page_for_ecryptfs);
	return rc;
}
