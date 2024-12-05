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
	struct file *lower_file;
	ssize_t rc;

	lower_file = ecryptfs_inode_to_private(ecryptfs_inode)->lower_file;
	if (!lower_file)
		return -EIO;
	rc = kernel_write(lower_file, data, size, &offset);
	mark_inode_dirty_sync(ecryptfs_inode);
	return rc;
}

/**
 * ecryptfs_write_lower_page_segment
 * @ecryptfs_inode: The eCryptfs inode
 * @folio_for_lower: The folio containing the data to be written to the
 *                  lower file
 * @offset_in_page: The offset in the @folio_for_lower from which to
 *                  start writing the data
 * @size: The amount of data from @folio_for_lower to write to the
 *        lower file
 *
 * Determines the byte offset in the file for the given page and
 * offset within the page, maps the page, and makes the call to write
 * the contents of @folio_for_lower to the lower inode.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_write_lower_page_segment(struct inode *ecryptfs_inode,
				      struct folio *folio_for_lower,
				      size_t offset_in_page, size_t size)
{
	char *virt;
	loff_t offset;
	int rc;

	offset = (loff_t)folio_for_lower->index * PAGE_SIZE + offset_in_page;
	virt = kmap_local_folio(folio_for_lower, 0);
	rc = ecryptfs_write_lower(ecryptfs_inode, virt, offset, size);
	if (rc > 0)
		rc = 0;
	kunmap_local(virt);
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
		struct folio *ecryptfs_folio;
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
		ecryptfs_folio = read_mapping_folio(ecryptfs_inode->i_mapping,
				ecryptfs_page_idx, NULL);
		if (IS_ERR(ecryptfs_folio)) {
			rc = PTR_ERR(ecryptfs_folio);
			printk(KERN_ERR "%s: Error getting page at "
			       "index [%ld] from eCryptfs inode "
			       "mapping; rc = [%d]\n", __func__,
			       ecryptfs_page_idx, rc);
			goto out;
		}
		folio_lock(ecryptfs_folio);
		ecryptfs_page_virt = kmap_local_folio(ecryptfs_folio, 0);

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
				PAGE_SIZE - start_offset_in_page);
		}

		/* pos >= offset, we are now writing the data request */
		if (pos >= offset) {
			memcpy(((char *)ecryptfs_page_virt
				+ start_offset_in_page),
			       (data + data_offset), num_bytes);
			data_offset += num_bytes;
		}
		kunmap_local(ecryptfs_page_virt);
		flush_dcache_folio(ecryptfs_folio);
		folio_mark_uptodate(ecryptfs_folio);
		folio_unlock(ecryptfs_folio);
		if (crypt_stat->flags & ECRYPTFS_ENCRYPTED)
			rc = ecryptfs_encrypt_page(ecryptfs_folio);
		else
			rc = ecryptfs_write_lower_page_segment(ecryptfs_inode,
						ecryptfs_folio,
						start_offset_in_page,
						data_offset);
		folio_put(ecryptfs_folio);
		if (rc) {
			printk(KERN_ERR "%s: Error encrypting "
			       "page; rc = [%d]\n", __func__, rc);
			goto out;
		}
		pos += num_bytes;
	}
	if (pos > ecryptfs_file_size) {
		i_size_write(ecryptfs_inode, pos);
		if (crypt_stat->flags & ECRYPTFS_ENCRYPTED) {
			int rc2;

			rc2 = ecryptfs_write_inode_size_to_metadata(
								ecryptfs_inode);
			if (rc2) {
				printk(KERN_ERR	"Problem with "
				       "ecryptfs_write_inode_size_to_metadata; "
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
	struct file *lower_file;
	lower_file = ecryptfs_inode_to_private(ecryptfs_inode)->lower_file;
	if (!lower_file)
		return -EIO;
	return kernel_read(lower_file, data, size, &offset);
}

/**
 * ecryptfs_read_lower_page_segment
 * @folio_for_ecryptfs: The folio into which data for eCryptfs will be
 *                     written
 * @page_index: Page index in @page_for_ecryptfs from which to start
 *		writing
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
int ecryptfs_read_lower_page_segment(struct folio *folio_for_ecryptfs,
				     pgoff_t page_index,
				     size_t offset_in_page, size_t size,
				     struct inode *ecryptfs_inode)
{
	char *virt;
	loff_t offset;
	int rc;

	offset = (loff_t)page_index * PAGE_SIZE + offset_in_page;
	virt = kmap_local_folio(folio_for_ecryptfs, 0);
	rc = ecryptfs_read_lower(virt, offset, size, ecryptfs_inode);
	if (rc > 0)
		rc = 0;
	kunmap_local(virt);
	flush_dcache_folio(folio_for_ecryptfs);
	return rc;
}
