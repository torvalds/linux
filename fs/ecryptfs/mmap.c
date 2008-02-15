/**
 * eCryptfs: Linux filesystem encryption layer
 * This is where eCryptfs coordinates the symmetric encryption and
 * decryption of the file data as it passes between the lower
 * encrypted file and the upper decrypted file.
 *
 * Copyright (C) 1997-2003 Erez Zadok
 * Copyright (C) 2001-2003 Stony Brook University
 * Copyright (C) 2004-2007 International Business Machines Corp.
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

#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/page-flags.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include "ecryptfs_kernel.h"

/**
 * ecryptfs_get_locked_page
 *
 * Get one page from cache or lower f/s, return error otherwise.
 *
 * Returns locked and up-to-date page (if ok), with increased
 * refcnt.
 */
struct page *ecryptfs_get_locked_page(struct file *file, loff_t index)
{
	struct dentry *dentry;
	struct inode *inode;
	struct address_space *mapping;
	struct page *page;

	dentry = file->f_path.dentry;
	inode = dentry->d_inode;
	mapping = inode->i_mapping;
	page = read_mapping_page(mapping, index, (void *)file);
	if (!IS_ERR(page))
		lock_page(page);
	return page;
}

/**
 * ecryptfs_writepage
 * @page: Page that is locked before this call is made
 *
 * Returns zero on success; non-zero otherwise
 */
static int ecryptfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int rc;

	rc = ecryptfs_encrypt_page(page);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error encrypting "
				"page (upper index [0x%.16x])\n", page->index);
		ClearPageUptodate(page);
		goto out;
	}
	SetPageUptodate(page);
	unlock_page(page);
out:
	return rc;
}

/**
 *   Header Extent:
 *     Octets 0-7:        Unencrypted file size (big-endian)
 *     Octets 8-15:       eCryptfs special marker
 *     Octets 16-19:      Flags
 *      Octet 16:         File format version number (between 0 and 255)
 *      Octets 17-18:     Reserved
 *      Octet 19:         Bit 1 (lsb): Reserved
 *                        Bit 2: Encrypted?
 *                        Bits 3-8: Reserved
 *     Octets 20-23:      Header extent size (big-endian)
 *     Octets 24-25:      Number of header extents at front of file
 *                        (big-endian)
 *     Octet  26:         Begin RFC 2440 authentication token packet set
 */
static void set_header_info(char *page_virt,
			    struct ecryptfs_crypt_stat *crypt_stat)
{
	size_t written;
	size_t save_num_header_bytes_at_front =
		crypt_stat->num_header_bytes_at_front;

	crypt_stat->num_header_bytes_at_front =
		ECRYPTFS_MINIMUM_HEADER_EXTENT_SIZE;
	ecryptfs_write_header_metadata(page_virt + 20, crypt_stat, &written);
	crypt_stat->num_header_bytes_at_front =
		save_num_header_bytes_at_front;
}

/**
 * ecryptfs_copy_up_encrypted_with_header
 * @page: Sort of a ``virtual'' representation of the encrypted lower
 *        file. The actual lower file does not have the metadata in
 *        the header. This is locked.
 * @crypt_stat: The eCryptfs inode's cryptographic context
 *
 * The ``view'' is the version of the file that userspace winds up
 * seeing, with the header information inserted.
 */
static int
ecryptfs_copy_up_encrypted_with_header(struct page *page,
				       struct ecryptfs_crypt_stat *crypt_stat)
{
	loff_t extent_num_in_page = 0;
	loff_t num_extents_per_page = (PAGE_CACHE_SIZE
				       / crypt_stat->extent_size);
	int rc = 0;

	while (extent_num_in_page < num_extents_per_page) {
		loff_t view_extent_num = ((((loff_t)page->index)
					   * num_extents_per_page)
					  + extent_num_in_page);
		size_t num_header_extents_at_front =
			(crypt_stat->num_header_bytes_at_front
			 / crypt_stat->extent_size);

		if (view_extent_num < num_header_extents_at_front) {
			/* This is a header extent */
			char *page_virt;

			page_virt = kmap_atomic(page, KM_USER0);
			memset(page_virt, 0, PAGE_CACHE_SIZE);
			/* TODO: Support more than one header extent */
			if (view_extent_num == 0) {
				rc = ecryptfs_read_xattr_region(
					page_virt, page->mapping->host);
				set_header_info(page_virt, crypt_stat);
			}
			kunmap_atomic(page_virt, KM_USER0);
			flush_dcache_page(page);
			if (rc) {
				printk(KERN_ERR "%s: Error reading xattr "
				       "region; rc = [%d]\n", __FUNCTION__, rc);
				goto out;
			}
		} else {
			/* This is an encrypted data extent */
			loff_t lower_offset =
				((view_extent_num * crypt_stat->extent_size)
				 - crypt_stat->num_header_bytes_at_front);

			rc = ecryptfs_read_lower_page_segment(
				page, (lower_offset >> PAGE_CACHE_SHIFT),
				(lower_offset & ~PAGE_CACHE_MASK),
				crypt_stat->extent_size, page->mapping->host);
			if (rc) {
				printk(KERN_ERR "%s: Error attempting to read "
				       "extent at offset [%lld] in the lower "
				       "file; rc = [%d]\n", __FUNCTION__,
				       lower_offset, rc);
				goto out;
			}
		}
		extent_num_in_page++;
	}
out:
	return rc;
}

/**
 * ecryptfs_readpage
 * @file: An eCryptfs file
 * @page: Page from eCryptfs inode mapping into which to stick the read data
 *
 * Read in a page, decrypting if necessary.
 *
 * Returns zero on success; non-zero on error.
 */
static int ecryptfs_readpage(struct file *file, struct page *page)
{
	struct ecryptfs_crypt_stat *crypt_stat =
		&ecryptfs_inode_to_private(file->f_path.dentry->d_inode)->crypt_stat;
	int rc = 0;

	if (!crypt_stat
	    || !(crypt_stat->flags & ECRYPTFS_ENCRYPTED)
	    || (crypt_stat->flags & ECRYPTFS_NEW_FILE)) {
		ecryptfs_printk(KERN_DEBUG,
				"Passing through unencrypted page\n");
		rc = ecryptfs_read_lower_page_segment(page, page->index, 0,
						      PAGE_CACHE_SIZE,
						      page->mapping->host);
	} else if (crypt_stat->flags & ECRYPTFS_VIEW_AS_ENCRYPTED) {
		if (crypt_stat->flags & ECRYPTFS_METADATA_IN_XATTR) {
			rc = ecryptfs_copy_up_encrypted_with_header(page,
								    crypt_stat);
			if (rc) {
				printk(KERN_ERR "%s: Error attempting to copy "
				       "the encrypted content from the lower "
				       "file whilst inserting the metadata "
				       "from the xattr into the header; rc = "
				       "[%d]\n", __FUNCTION__, rc);
				goto out;
			}

		} else {
			rc = ecryptfs_read_lower_page_segment(
				page, page->index, 0, PAGE_CACHE_SIZE,
				page->mapping->host);
			if (rc) {
				printk(KERN_ERR "Error reading page; rc = "
				       "[%d]\n", rc);
				goto out;
			}
		}
	} else {
		rc = ecryptfs_decrypt_page(page);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error decrypting page; "
					"rc = [%d]\n", rc);
			goto out;
		}
	}
out:
	if (rc)
		ClearPageUptodate(page);
	else
		SetPageUptodate(page);
	ecryptfs_printk(KERN_DEBUG, "Unlocking page with index = [0x%.16x]\n",
			page->index);
	unlock_page(page);
	return rc;
}

/**
 * Called with lower inode mutex held.
 */
static int fill_zeros_to_end_of_page(struct page *page, unsigned int to)
{
	struct inode *inode = page->mapping->host;
	int end_byte_in_page;

	if ((i_size_read(inode) / PAGE_CACHE_SIZE) != page->index)
		goto out;
	end_byte_in_page = i_size_read(inode) % PAGE_CACHE_SIZE;
	if (to > end_byte_in_page)
		end_byte_in_page = to;
	zero_user_segment(page, end_byte_in_page, PAGE_CACHE_SIZE);
out:
	return 0;
}

/* This function must zero any hole we create */
static int ecryptfs_prepare_write(struct file *file, struct page *page,
				  unsigned from, unsigned to)
{
	int rc = 0;
	loff_t prev_page_end_size;

	if (!PageUptodate(page)) {
		rc = ecryptfs_read_lower_page_segment(page, page->index, 0,
						      PAGE_CACHE_SIZE,
						      page->mapping->host);
		if (rc) {
			printk(KERN_ERR "%s: Error attemping to read lower "
			       "page segment; rc = [%d]\n", __FUNCTION__, rc);
			ClearPageUptodate(page);
			goto out;
		} else
			SetPageUptodate(page);
	}

	prev_page_end_size = ((loff_t)page->index << PAGE_CACHE_SHIFT);

	/*
	 * If creating a page or more of holes, zero them out via truncate.
	 * Note, this will increase i_size.
	 */
	if (page->index != 0) {
		if (prev_page_end_size > i_size_read(page->mapping->host)) {
			rc = ecryptfs_truncate(file->f_path.dentry,
					       prev_page_end_size);
			if (rc) {
				printk(KERN_ERR "Error on attempt to "
				       "truncate to (higher) offset [%lld];"
				       " rc = [%d]\n", prev_page_end_size, rc);
				goto out;
			}
		}
	}
	/*
	 * Writing to a new page, and creating a small hole from start of page?
	 * Zero it out.
	 */
	if ((i_size_read(page->mapping->host) == prev_page_end_size) &&
	    (from != 0)) {
		zero_user(page, 0, PAGE_CACHE_SIZE);
	}
out:
	return rc;
}

/**
 * ecryptfs_write_inode_size_to_header
 *
 * Writes the lower file size to the first 8 bytes of the header.
 *
 * Returns zero on success; non-zero on error.
 */
static int ecryptfs_write_inode_size_to_header(struct inode *ecryptfs_inode)
{
	u64 file_size;
	char *file_size_virt;
	int rc;

	file_size_virt = kmalloc(sizeof(u64), GFP_KERNEL);
	if (!file_size_virt) {
		rc = -ENOMEM;
		goto out;
	}
	file_size = (u64)i_size_read(ecryptfs_inode);
	file_size = cpu_to_be64(file_size);
	memcpy(file_size_virt, &file_size, sizeof(u64));
	rc = ecryptfs_write_lower(ecryptfs_inode, file_size_virt, 0,
				  sizeof(u64));
	kfree(file_size_virt);
	if (rc)
		printk(KERN_ERR "%s: Error writing file size to header; "
		       "rc = [%d]\n", __FUNCTION__, rc);
out:
	return rc;
}

struct kmem_cache *ecryptfs_xattr_cache;

static int ecryptfs_write_inode_size_to_xattr(struct inode *ecryptfs_inode)
{
	ssize_t size;
	void *xattr_virt;
	struct dentry *lower_dentry =
		ecryptfs_inode_to_private(ecryptfs_inode)->lower_file->f_dentry;
	struct inode *lower_inode = lower_dentry->d_inode;
	u64 file_size;
	int rc;

	if (!lower_inode->i_op->getxattr || !lower_inode->i_op->setxattr) {
		printk(KERN_WARNING
		       "No support for setting xattr in lower filesystem\n");
		rc = -ENOSYS;
		goto out;
	}
	xattr_virt = kmem_cache_alloc(ecryptfs_xattr_cache, GFP_KERNEL);
	if (!xattr_virt) {
		printk(KERN_ERR "Out of memory whilst attempting to write "
		       "inode size to xattr\n");
		rc = -ENOMEM;
		goto out;
	}
	mutex_lock(&lower_inode->i_mutex);
	size = lower_inode->i_op->getxattr(lower_dentry, ECRYPTFS_XATTR_NAME,
					   xattr_virt, PAGE_CACHE_SIZE);
	if (size < 0)
		size = 8;
	file_size = (u64)i_size_read(ecryptfs_inode);
	file_size = cpu_to_be64(file_size);
	memcpy(xattr_virt, &file_size, sizeof(u64));
	rc = lower_inode->i_op->setxattr(lower_dentry, ECRYPTFS_XATTR_NAME,
					 xattr_virt, size, 0);
	mutex_unlock(&lower_inode->i_mutex);
	if (rc)
		printk(KERN_ERR "Error whilst attempting to write inode size "
		       "to lower file xattr; rc = [%d]\n", rc);
	kmem_cache_free(ecryptfs_xattr_cache, xattr_virt);
out:
	return rc;
}

int ecryptfs_write_inode_size_to_metadata(struct inode *ecryptfs_inode)
{
	struct ecryptfs_crypt_stat *crypt_stat;

	crypt_stat = &ecryptfs_inode_to_private(ecryptfs_inode)->crypt_stat;
	if (crypt_stat->flags & ECRYPTFS_METADATA_IN_XATTR)
		return ecryptfs_write_inode_size_to_xattr(ecryptfs_inode);
	else
		return ecryptfs_write_inode_size_to_header(ecryptfs_inode);
}

/**
 * ecryptfs_commit_write
 * @file: The eCryptfs file object
 * @page: The eCryptfs page
 * @from: Ignored (we rotate the page IV on each write)
 * @to: Ignored
 *
 * This is where we encrypt the data and pass the encrypted data to
 * the lower filesystem.  In OpenPGP-compatible mode, we operate on
 * entire underlying packets.
 */
static int ecryptfs_commit_write(struct file *file, struct page *page,
				 unsigned from, unsigned to)
{
	loff_t pos;
	struct inode *ecryptfs_inode = page->mapping->host;
	struct ecryptfs_crypt_stat *crypt_stat =
		&ecryptfs_inode_to_private(file->f_path.dentry->d_inode)->crypt_stat;
	int rc;

	if (crypt_stat->flags & ECRYPTFS_NEW_FILE) {
		ecryptfs_printk(KERN_DEBUG, "ECRYPTFS_NEW_FILE flag set in "
			"crypt_stat at memory location [%p]\n", crypt_stat);
		crypt_stat->flags &= ~(ECRYPTFS_NEW_FILE);
	} else
		ecryptfs_printk(KERN_DEBUG, "Not a new file\n");
	ecryptfs_printk(KERN_DEBUG, "Calling fill_zeros_to_end_of_page"
			"(page w/ index = [0x%.16x], to = [%d])\n", page->index,
			to);
	/* Fills in zeros if 'to' goes beyond inode size */
	rc = fill_zeros_to_end_of_page(page, to);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error attempting to fill "
				"zeros in page with index = [0x%.16x]\n",
				page->index);
		goto out;
	}
	rc = ecryptfs_encrypt_page(page);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error encrypting page (upper "
				"index [0x%.16x])\n", page->index);
		goto out;
	}
	pos = (((loff_t)page->index) << PAGE_CACHE_SHIFT) + to;
	if (pos > i_size_read(ecryptfs_inode)) {
		i_size_write(ecryptfs_inode, pos);
		ecryptfs_printk(KERN_DEBUG, "Expanded file size to "
				"[0x%.16x]\n", i_size_read(ecryptfs_inode));
	}
	rc = ecryptfs_write_inode_size_to_metadata(ecryptfs_inode);
	if (rc)
		printk(KERN_ERR "Error writing inode size to metadata; "
		       "rc = [%d]\n", rc);
out:
	return rc;
}

static sector_t ecryptfs_bmap(struct address_space *mapping, sector_t block)
{
	int rc = 0;
	struct inode *inode;
	struct inode *lower_inode;

	inode = (struct inode *)mapping->host;
	lower_inode = ecryptfs_inode_to_lower(inode);
	if (lower_inode->i_mapping->a_ops->bmap)
		rc = lower_inode->i_mapping->a_ops->bmap(lower_inode->i_mapping,
							 block);
	return rc;
}

struct address_space_operations ecryptfs_aops = {
	.writepage = ecryptfs_writepage,
	.readpage = ecryptfs_readpage,
	.prepare_write = ecryptfs_prepare_write,
	.commit_write = ecryptfs_commit_write,
	.bmap = ecryptfs_bmap,
};
