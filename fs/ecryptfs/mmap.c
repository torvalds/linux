/**
 * eCryptfs: Linux filesystem encryption layer
 * This is where eCryptfs coordinates the symmetric encryption and
 * decryption of the file data as it passes between the lower
 * encrypted file and the upper decrypted file.
 *
 * Copyright (C) 1997-2003 Erez Zadok
 * Copyright (C) 2001-2003 Stony Brook University
 * Copyright (C) 2004-2006 International Business Machines Corp.
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

struct kmem_cache *ecryptfs_lower_page_cache;

/**
 * ecryptfs_get1page
 *
 * Get one page from cache or lower f/s, return error otherwise.
 *
 * Returns unlocked and up-to-date page (if ok), with increased
 * refcnt.
 */
static struct page *ecryptfs_get1page(struct file *file, int index)
{
	struct page *page;
	struct dentry *dentry;
	struct inode *inode;
	struct address_space *mapping;

	dentry = file->f_dentry;
	inode = dentry->d_inode;
	mapping = inode->i_mapping;
	page = read_cache_page(mapping, index,
			       (filler_t *)mapping->a_ops->readpage,
			       (void *)file);
	if (IS_ERR(page))
		goto out;
	wait_on_page_locked(page);
out:
	return page;
}

static
int write_zeros(struct file *file, pgoff_t index, int start, int num_zeros);

/**
 * ecryptfs_fill_zeros
 * @file: The ecryptfs file
 * @new_length: The new length of the data in the underlying file;
 *              everything between the prior end of the file and the
 *              new end of the file will be filled with zero's.
 *              new_length must be greater than  current length
 *
 * Function for handling lseek-ing past the end of the file.
 *
 * This function does not support shrinking, only growing a file.
 *
 * Returns zero on success; non-zero otherwise.
 */
int ecryptfs_fill_zeros(struct file *file, loff_t new_length)
{
	int rc = 0;
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	pgoff_t old_end_page_index = 0;
	pgoff_t index = old_end_page_index;
	int old_end_pos_in_page = -1;
	pgoff_t new_end_page_index;
	int new_end_pos_in_page;
	loff_t cur_length = i_size_read(inode);

	if (cur_length != 0) {
		index = old_end_page_index =
		    ((cur_length - 1) >> PAGE_CACHE_SHIFT);
		old_end_pos_in_page = ((cur_length - 1) & ~PAGE_CACHE_MASK);
	}
	new_end_page_index = ((new_length - 1) >> PAGE_CACHE_SHIFT);
	new_end_pos_in_page = ((new_length - 1) & ~PAGE_CACHE_MASK);
	ecryptfs_printk(KERN_DEBUG, "old_end_page_index = [0x%.16x]; "
			"old_end_pos_in_page = [%d]; "
			"new_end_page_index = [0x%.16x]; "
			"new_end_pos_in_page = [%d]\n",
			old_end_page_index, old_end_pos_in_page,
			new_end_page_index, new_end_pos_in_page);
	if (old_end_page_index == new_end_page_index) {
		/* Start and end are in the same page; we just need to
		 * set a portion of the existing page to zero's */
		rc = write_zeros(file, index, (old_end_pos_in_page + 1),
				 (new_end_pos_in_page - old_end_pos_in_page));
		if (rc)
			ecryptfs_printk(KERN_ERR, "write_zeros(file=[%p], "
					"index=[0x%.16x], "
					"old_end_pos_in_page=[d], "
					"(PAGE_CACHE_SIZE - new_end_pos_in_page"
					"=[%d]"
					")=[d]) returned [%d]\n", file, index,
					old_end_pos_in_page,
					new_end_pos_in_page,
					(PAGE_CACHE_SIZE - new_end_pos_in_page),
					rc);
		goto out;
	}
	/* Fill the remainder of the previous last page with zeros */
	rc = write_zeros(file, index, (old_end_pos_in_page + 1),
			 ((PAGE_CACHE_SIZE - 1) - old_end_pos_in_page));
	if (rc) {
		ecryptfs_printk(KERN_ERR, "write_zeros(file=[%p], "
				"index=[0x%.16x], old_end_pos_in_page=[d], "
				"(PAGE_CACHE_SIZE - old_end_pos_in_page)=[d]) "
				"returned [%d]\n", file, index,
				old_end_pos_in_page,
				(PAGE_CACHE_SIZE - old_end_pos_in_page), rc);
		goto out;
	}
	index++;
	while (index < new_end_page_index) {
		/* Fill all intermediate pages with zeros */
		rc = write_zeros(file, index, 0, PAGE_CACHE_SIZE);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "write_zeros(file=[%p], "
					"index=[0x%.16x], "
					"old_end_pos_in_page=[d], "
					"(PAGE_CACHE_SIZE - new_end_pos_in_page"
					"=[%d]"
					")=[d]) returned [%d]\n", file, index,
					old_end_pos_in_page,
					new_end_pos_in_page,
					(PAGE_CACHE_SIZE - new_end_pos_in_page),
					rc);
			goto out;
		}
		index++;
	}
	/* Fill the portion at the beginning of the last new page with
	 * zero's */
	rc = write_zeros(file, index, 0, (new_end_pos_in_page + 1));
	if (rc) {
		ecryptfs_printk(KERN_ERR, "write_zeros(file="
				"[%p], index=[0x%.16x], 0, "
				"new_end_pos_in_page=[%d]"
				"returned [%d]\n", file, index,
				new_end_pos_in_page, rc);
		goto out;
	}
out:
	return rc;
}

/**
 * ecryptfs_writepage
 * @page: Page that is locked before this call is made
 *
 * Returns zero on success; non-zero otherwise
 */
static int ecryptfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct ecryptfs_page_crypt_context ctx;
	int rc;

	ctx.page = page;
	ctx.mode = ECRYPTFS_WRITEPAGE_MODE;
	ctx.param.wbc = wbc;
	rc = ecryptfs_encrypt_page(&ctx);
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
 * Reads the data from the lower file file at index lower_page_index
 * and copies that data into page.
 *
 * @param page	Page to fill
 * @param lower_page_index Index of the page in the lower file to get
 */
int ecryptfs_do_readpage(struct file *file, struct page *page,
			 pgoff_t lower_page_index)
{
	int rc;
	struct dentry *dentry;
	struct file *lower_file;
	struct dentry *lower_dentry;
	struct inode *inode;
	struct inode *lower_inode;
	char *page_data;
	struct page *lower_page = NULL;
	char *lower_page_data;
	const struct address_space_operations *lower_a_ops;

	dentry = file->f_dentry;
	lower_file = ecryptfs_file_to_lower(file);
	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	inode = dentry->d_inode;
	lower_inode = ecryptfs_inode_to_lower(inode);
	lower_a_ops = lower_inode->i_mapping->a_ops;
	lower_page = read_cache_page(lower_inode->i_mapping, lower_page_index,
				     (filler_t *)lower_a_ops->readpage,
				     (void *)lower_file);
	if (IS_ERR(lower_page)) {
		rc = PTR_ERR(lower_page);
		lower_page = NULL;
		ecryptfs_printk(KERN_ERR, "Error reading from page cache\n");
		goto out;
	}
	wait_on_page_locked(lower_page);
	page_data = (char *)kmap(page);
	if (!page_data) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Error mapping page\n");
		goto out;
	}
	lower_page_data = (char *)kmap(lower_page);
	if (!lower_page_data) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Error mapping page\n");
		kunmap(page);
		goto out;
	}
	memcpy(page_data, lower_page_data, PAGE_CACHE_SIZE);
	kunmap(lower_page);
	kunmap(page);
	rc = 0;
out:
	if (likely(lower_page))
		page_cache_release(lower_page);
	if (rc == 0)
		SetPageUptodate(page);
	else
		ClearPageUptodate(page);
	return rc;
}

/**
 * ecryptfs_readpage
 * @file: This is an ecryptfs file
 * @page: ecryptfs associated page to stick the read data into
 *
 * Read in a page, decrypting if necessary.
 *
 * Returns zero on success; non-zero on error.
 */
static int ecryptfs_readpage(struct file *file, struct page *page)
{
	int rc = 0;
	struct ecryptfs_crypt_stat *crypt_stat;

	BUG_ON(!(file && file->f_dentry && file->f_dentry->d_inode));
	crypt_stat =
		&ecryptfs_inode_to_private(file->f_dentry->d_inode)->crypt_stat;
	if (!crypt_stat
	    || !ECRYPTFS_CHECK_FLAG(crypt_stat->flags, ECRYPTFS_ENCRYPTED)
	    || ECRYPTFS_CHECK_FLAG(crypt_stat->flags, ECRYPTFS_NEW_FILE)) {
		ecryptfs_printk(KERN_DEBUG,
				"Passing through unencrypted page\n");
		rc = ecryptfs_do_readpage(file, page, page->index);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error reading page; rc = "
					"[%d]\n", rc);
			goto out;
		}
	} else {
		rc = ecryptfs_decrypt_page(file, page);
		if (rc) {

			ecryptfs_printk(KERN_ERR, "Error decrypting page; "
					"rc = [%d]\n", rc);
			goto out;
		}
	}
	SetPageUptodate(page);
out:
	if (rc)
		ClearPageUptodate(page);
	ecryptfs_printk(KERN_DEBUG, "Unlocking page with index = [0x%.16x]\n",
			page->index);
	unlock_page(page);
	return rc;
}

static int fill_zeros_to_end_of_page(struct page *page, unsigned int to)
{
	struct inode *inode = page->mapping->host;
	int end_byte_in_page;
	int rc = 0;
	char *page_virt;

	if ((i_size_read(inode) / PAGE_CACHE_SIZE) == page->index) {
		end_byte_in_page = i_size_read(inode) % PAGE_CACHE_SIZE;
		if (to > end_byte_in_page)
			end_byte_in_page = to;
		page_virt = kmap(page);
		if (!page_virt) {
			rc = -ENOMEM;
			ecryptfs_printk(KERN_WARNING,
					"Could not map page\n");
			goto out;
		}
		memset((page_virt + end_byte_in_page), 0,
		       (PAGE_CACHE_SIZE - end_byte_in_page));
		kunmap(page);
	}
out:
	return rc;
}

static int ecryptfs_prepare_write(struct file *file, struct page *page,
				  unsigned from, unsigned to)
{
	int rc = 0;

	kmap(page);
	if (from == 0 && to == PAGE_CACHE_SIZE)
		goto out;	/* If we are writing a full page, it will be
				   up to date. */
	if (!PageUptodate(page))
		rc = ecryptfs_do_readpage(file, page, page->index);
out:
	return rc;
}

int ecryptfs_grab_and_map_lower_page(struct page **lower_page,
				     char **lower_virt,
				     struct inode *lower_inode,
				     unsigned long lower_page_index)
{
	int rc = 0;

	(*lower_page) = grab_cache_page(lower_inode->i_mapping,
					lower_page_index);
	if (!(*lower_page)) {
		ecryptfs_printk(KERN_ERR, "grab_cache_page for "
				"lower_page_index = [0x%.16x] failed\n",
				lower_page_index);
		rc = -EINVAL;
		goto out;
	}
	if (lower_virt)
		(*lower_virt) = kmap((*lower_page));
	else
		kmap((*lower_page));
out:
	return rc;
}

int ecryptfs_writepage_and_release_lower_page(struct page *lower_page,
					      struct inode *lower_inode,
					      struct writeback_control *wbc)
{
	int rc = 0;

	rc = lower_inode->i_mapping->a_ops->writepage(lower_page, wbc);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error calling lower writepage(); "
				"rc = [%d]\n", rc);
		goto out;
	}
	lower_inode->i_mtime = lower_inode->i_ctime = CURRENT_TIME;
	page_cache_release(lower_page);
out:
	return rc;
}

static void ecryptfs_unmap_and_release_lower_page(struct page *lower_page)
{
	kunmap(lower_page);
	ecryptfs_printk(KERN_DEBUG, "Unlocking lower page with index = "
			"[0x%.16x]\n", lower_page->index);
	unlock_page(lower_page);
	page_cache_release(lower_page);
}

/**
 * ecryptfs_write_inode_size_to_header
 *
 * Writes the lower file size to the first 8 bytes of the header.
 *
 * Returns zero on success; non-zero on error.
 */
int
ecryptfs_write_inode_size_to_header(struct file *lower_file,
				    struct inode *lower_inode,
				    struct inode *inode)
{
	int rc = 0;
	struct page *header_page;
	char *header_virt;
	const struct address_space_operations *lower_a_ops;
	u64 file_size;

	rc = ecryptfs_grab_and_map_lower_page(&header_page, &header_virt,
					      lower_inode, 0);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "grab_cache_page for header page "
				"failed\n");
		goto out;
	}
	lower_a_ops = lower_inode->i_mapping->a_ops;
	rc = lower_a_ops->prepare_write(lower_file, header_page, 0, 8);
	file_size = (u64)i_size_read(inode);
	ecryptfs_printk(KERN_DEBUG, "Writing size: [0x%.16x]\n", file_size);
	file_size = cpu_to_be64(file_size);
	memcpy(header_virt, &file_size, sizeof(u64));
	rc = lower_a_ops->commit_write(lower_file, header_page, 0, 8);
	if (rc < 0)
		ecryptfs_printk(KERN_ERR, "Error commiting header page "
				"write\n");
	ecryptfs_unmap_and_release_lower_page(header_page);
	lower_inode->i_mtime = lower_inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty_sync(inode);
out:
	return rc;
}

int ecryptfs_get_lower_page(struct page **lower_page, struct inode *lower_inode,
			    struct file *lower_file,
			    unsigned long lower_page_index, int byte_offset,
			    int region_bytes)
{
	int rc = 0;

	rc = ecryptfs_grab_and_map_lower_page(lower_page, NULL, lower_inode,
					      lower_page_index);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error attempting to grab and map "
				"lower page with index [0x%.16x]\n",
				lower_page_index);
		goto out;
	}
	rc = lower_inode->i_mapping->a_ops->prepare_write(lower_file,
							  (*lower_page),
							  byte_offset,
							  region_bytes);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "prepare_write for "
				"lower_page_index = [0x%.16x] failed; rc = "
				"[%d]\n", lower_page_index, rc);
	}
out:
	if (rc && (*lower_page)) {
		ecryptfs_unmap_and_release_lower_page(*lower_page);
		(*lower_page) = NULL;
	}
	return rc;
}

/**
 * ecryptfs_commit_lower_page
 *
 * Returns zero on success; non-zero on error
 */
int
ecryptfs_commit_lower_page(struct page *lower_page, struct inode *lower_inode,
			   struct file *lower_file, int byte_offset,
			   int region_size)
{
	int rc = 0;

	rc = lower_inode->i_mapping->a_ops->commit_write(
		lower_file, lower_page, byte_offset, region_size);
	if (rc < 0) {
		ecryptfs_printk(KERN_ERR,
				"Error committing write; rc = [%d]\n", rc);
	} else
		rc = 0;
	ecryptfs_unmap_and_release_lower_page(lower_page);
	return rc;
}

/**
 * ecryptfs_copy_page_to_lower
 *
 * Used for plaintext pass-through; no page index interpolation
 * required.
 */
int ecryptfs_copy_page_to_lower(struct page *page, struct inode *lower_inode,
				struct file *lower_file)
{
	int rc = 0;
	struct page *lower_page;

	rc = ecryptfs_get_lower_page(&lower_page, lower_inode, lower_file,
				     page->index, 0, PAGE_CACHE_SIZE);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error attempting to get page "
				"at index [0x%.16x]\n", page->index);
		goto out;
	}
	/* TODO: aops */
	memcpy((char *)page_address(lower_page), page_address(page),
	       PAGE_CACHE_SIZE);
	rc = ecryptfs_commit_lower_page(lower_page, lower_inode, lower_file,
					0, PAGE_CACHE_SIZE);
	if (rc)
		ecryptfs_printk(KERN_ERR, "Error attempting to commit page "
				"at index [0x%.16x]\n", page->index);
out:
	return rc;
}

static int
process_new_file(struct ecryptfs_crypt_stat *crypt_stat,
		 struct file *file, struct inode *inode)
{
	struct page *header_page;
	const struct address_space_operations *lower_a_ops;
	struct inode *lower_inode;
	struct file *lower_file;
	char *header_virt;
	int rc = 0;
	int current_header_page = 0;
	int header_pages;
	int more_header_data_to_be_written = 1;

	lower_inode = ecryptfs_inode_to_lower(inode);
	lower_file = ecryptfs_file_to_lower(file);
	lower_a_ops = lower_inode->i_mapping->a_ops;
	header_pages = ((crypt_stat->header_extent_size
			 * crypt_stat->num_header_extents_at_front)
			/ PAGE_CACHE_SIZE);
	BUG_ON(header_pages < 1);
	while (current_header_page < header_pages) {
		rc = ecryptfs_grab_and_map_lower_page(&header_page,
						      &header_virt,
						      lower_inode,
						      current_header_page);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "grab_cache_page for "
					"header page [%d] failed; rc = [%d]\n",
					current_header_page, rc);
			goto out;
		}
		rc = lower_a_ops->prepare_write(lower_file, header_page, 0,
						PAGE_CACHE_SIZE);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error preparing to write "
					"header page out; rc = [%d]\n", rc);
			goto out;
		}
		memset(header_virt, 0, PAGE_CACHE_SIZE);
		if (more_header_data_to_be_written) {
			rc = ecryptfs_write_headers_virt(header_virt,
							 crypt_stat,
							 file->f_dentry);
			if (rc) {
				ecryptfs_printk(KERN_WARNING, "Error "
						"generating header; rc = "
						"[%d]\n", rc);
				rc = -EIO;
				memset(header_virt, 0, PAGE_CACHE_SIZE);
				ecryptfs_unmap_and_release_lower_page(
					header_page);
				goto out;
			}
			if (current_header_page == 0)
				memset(header_virt, 0, 8);
			more_header_data_to_be_written = 0;
		}
		rc = lower_a_ops->commit_write(lower_file, header_page, 0,
					       PAGE_CACHE_SIZE);
		ecryptfs_unmap_and_release_lower_page(header_page);
		if (rc < 0) {
			ecryptfs_printk(KERN_ERR,
					"Error commiting header page write; "
					"rc = [%d]\n", rc);
			break;
		}
		current_header_page++;
	}
	if (rc >= 0) {
		rc = 0;
		ecryptfs_printk(KERN_DEBUG, "lower_inode->i_blocks = "
				"[0x%.16x]\n", lower_inode->i_blocks);
		i_size_write(inode, 0);
		lower_inode->i_mtime = lower_inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty_sync(inode);
	}
	ecryptfs_printk(KERN_DEBUG, "Clearing ECRYPTFS_NEW_FILE flag in "
			"crypt_stat at memory location [%p]\n", crypt_stat);
	ECRYPTFS_CLEAR_FLAG(crypt_stat->flags, ECRYPTFS_NEW_FILE);
out:
	return rc;
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
	struct ecryptfs_page_crypt_context ctx;
	loff_t pos;
	struct inode *inode;
	struct inode *lower_inode;
	struct file *lower_file;
	struct ecryptfs_crypt_stat *crypt_stat;
	int rc;

	inode = page->mapping->host;
	lower_inode = ecryptfs_inode_to_lower(inode);
	lower_file = ecryptfs_file_to_lower(file);
	mutex_lock(&lower_inode->i_mutex);
	crypt_stat =
		&ecryptfs_inode_to_private(file->f_dentry->d_inode)->crypt_stat;
	if (ECRYPTFS_CHECK_FLAG(crypt_stat->flags, ECRYPTFS_NEW_FILE)) {
		ecryptfs_printk(KERN_DEBUG, "ECRYPTFS_NEW_FILE flag set in "
			"crypt_stat at memory location [%p]\n", crypt_stat);
		rc = process_new_file(crypt_stat, file, inode);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error processing new "
					"file; rc = [%d]\n", rc);
			goto out;
		}
	} else
		ecryptfs_printk(KERN_DEBUG, "Not a new file\n");
	ecryptfs_printk(KERN_DEBUG, "Calling fill_zeros_to_end_of_page"
			"(page w/ index = [0x%.16x], to = [%d])\n", page->index,
			to);
	rc = fill_zeros_to_end_of_page(page, to);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error attempting to fill "
				"zeros in page with index = [0x%.16x]\n",
				page->index);
		goto out;
	}
	ctx.page = page;
	ctx.mode = ECRYPTFS_PREPARE_COMMIT_MODE;
	ctx.param.lower_file = lower_file;
	rc = ecryptfs_encrypt_page(&ctx);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error encrypting page (upper "
				"index [0x%.16x])\n", page->index);
		goto out;
	}
	rc = 0;
	inode->i_blocks = lower_inode->i_blocks;
	pos = (page->index << PAGE_CACHE_SHIFT) + to;
	if (pos > i_size_read(inode)) {
		i_size_write(inode, pos);
		ecryptfs_printk(KERN_DEBUG, "Expanded file size to "
				"[0x%.16x]\n", i_size_read(inode));
	}
	ecryptfs_write_inode_size_to_header(lower_file, lower_inode, inode);
	lower_inode->i_mtime = lower_inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty_sync(inode);
out:
	kunmap(page); /* mapped in prior call (prepare_write) */
	if (rc < 0)
		ClearPageUptodate(page);
	else
		SetPageUptodate(page);
	mutex_unlock(&lower_inode->i_mutex);
	return rc;
}

/**
 * write_zeros
 * @file: The ecryptfs file
 * @index: The index in which we are writing
 * @start: The position after the last block of data
 * @num_zeros: The number of zeros to write
 *
 * Write a specified number of zero's to a page.
 *
 * (start + num_zeros) must be less than or equal to PAGE_CACHE_SIZE
 */
static
int write_zeros(struct file *file, pgoff_t index, int start, int num_zeros)
{
	int rc = 0;
	struct page *tmp_page;

	tmp_page = ecryptfs_get1page(file, index);
	if (IS_ERR(tmp_page)) {
		ecryptfs_printk(KERN_ERR, "Error getting page at index "
				"[0x%.16x]\n", index);
		rc = PTR_ERR(tmp_page);
		goto out;
	}
	kmap(tmp_page);
	rc = ecryptfs_prepare_write(file, tmp_page, start, start + num_zeros);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error preparing to write zero's "
				"to remainder of page at index [0x%.16x]\n",
				index);
		kunmap(tmp_page);
		page_cache_release(tmp_page);
		goto out;
	}
	memset(((char *)page_address(tmp_page) + start), 0, num_zeros);
	rc = ecryptfs_commit_write(file, tmp_page, start, start + num_zeros);
	if (rc < 0) {
		ecryptfs_printk(KERN_ERR, "Error attempting to write zero's "
				"to remainder of page at index [0x%.16x]\n",
				index);
		kunmap(tmp_page);
		page_cache_release(tmp_page);
		goto out;
	}
	rc = 0;
	kunmap(tmp_page);
	page_cache_release(tmp_page);
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

static void ecryptfs_sync_page(struct page *page)
{
	struct inode *inode;
	struct inode *lower_inode;
	struct page *lower_page;

	inode = page->mapping->host;
	lower_inode = ecryptfs_inode_to_lower(inode);
	/* NOTE: Recently swapped with grab_cache_page(), since
	 * sync_page() just makes sure that pending I/O gets done. */
	lower_page = find_lock_page(lower_inode->i_mapping, page->index);
	if (!lower_page) {
		ecryptfs_printk(KERN_DEBUG, "find_lock_page failed\n");
		return;
	}
	lower_page->mapping->a_ops->sync_page(lower_page);
	ecryptfs_printk(KERN_DEBUG, "Unlocking page with index = [0x%.16x]\n",
			lower_page->index);
	unlock_page(lower_page);
	page_cache_release(lower_page);
}

struct address_space_operations ecryptfs_aops = {
	.writepage = ecryptfs_writepage,
	.readpage = ecryptfs_readpage,
	.prepare_write = ecryptfs_prepare_write,
	.commit_write = ecryptfs_commit_write,
	.bmap = ecryptfs_bmap,
	.sync_page = ecryptfs_sync_page,
};
