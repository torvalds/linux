// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (c) 2008,2009 NEC Software Tohoku, Ltd.
 * Written by Takashi Sato <t-sato@yk.jp.nec.com>
 *            Akira Fujita <a-fujita@rs.jp.nec.com>
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include <linux/slab.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include "ext4_extents.h"

/**
 * get_ext_path() - Find an extent path for designated logical block number.
 * @iyesde:	iyesde to be searched
 * @lblock:	logical block number to find an extent path
 * @ppath:	pointer to an extent path pointer (for output)
 *
 * ext4_find_extent wrapper. Return 0 on success, or a negative error value
 * on failure.
 */
static inline int
get_ext_path(struct iyesde *iyesde, ext4_lblk_t lblock,
		struct ext4_ext_path **ppath)
{
	struct ext4_ext_path *path;

	path = ext4_find_extent(iyesde, lblock, ppath, EXT4_EX_NOCACHE);
	if (IS_ERR(path))
		return PTR_ERR(path);
	if (path[ext_depth(iyesde)].p_ext == NULL) {
		ext4_ext_drop_refs(path);
		kfree(path);
		*ppath = NULL;
		return -ENODATA;
	}
	*ppath = path;
	return 0;
}

/**
 * ext4_double_down_write_data_sem() - write lock two iyesdes's i_data_sem
 * @first: iyesde to be locked
 * @second: iyesde to be locked
 *
 * Acquire write lock of i_data_sem of the two iyesdes
 */
void
ext4_double_down_write_data_sem(struct iyesde *first, struct iyesde *second)
{
	if (first < second) {
		down_write(&EXT4_I(first)->i_data_sem);
		down_write_nested(&EXT4_I(second)->i_data_sem, I_DATA_SEM_OTHER);
	} else {
		down_write(&EXT4_I(second)->i_data_sem);
		down_write_nested(&EXT4_I(first)->i_data_sem, I_DATA_SEM_OTHER);

	}
}

/**
 * ext4_double_up_write_data_sem - Release two iyesdes' write lock of i_data_sem
 *
 * @orig_iyesde:		original iyesde structure to be released its lock first
 * @doyesr_iyesde:	doyesr iyesde structure to be released its lock second
 * Release write lock of i_data_sem of two iyesdes (orig and doyesr).
 */
void
ext4_double_up_write_data_sem(struct iyesde *orig_iyesde,
			      struct iyesde *doyesr_iyesde)
{
	up_write(&EXT4_I(orig_iyesde)->i_data_sem);
	up_write(&EXT4_I(doyesr_iyesde)->i_data_sem);
}

/**
 * mext_check_coverage - Check that all extents in range has the same type
 *
 * @iyesde:		iyesde in question
 * @from:		block offset of iyesde
 * @count:		block count to be checked
 * @unwritten:		extents expected to be unwritten
 * @err:		pointer to save error value
 *
 * Return 1 if all extents in range has expected type, and zero otherwise.
 */
static int
mext_check_coverage(struct iyesde *iyesde, ext4_lblk_t from, ext4_lblk_t count,
		    int unwritten, int *err)
{
	struct ext4_ext_path *path = NULL;
	struct ext4_extent *ext;
	int ret = 0;
	ext4_lblk_t last = from + count;
	while (from < last) {
		*err = get_ext_path(iyesde, from, &path);
		if (*err)
			goto out;
		ext = path[ext_depth(iyesde)].p_ext;
		if (unwritten != ext4_ext_is_unwritten(ext))
			goto out;
		from += ext4_ext_get_actual_len(ext);
		ext4_ext_drop_refs(path);
	}
	ret = 1;
out:
	ext4_ext_drop_refs(path);
	kfree(path);
	return ret;
}

/**
 * mext_page_double_lock - Grab and lock pages on both @iyesde1 and @iyesde2
 *
 * @iyesde1:	the iyesde structure
 * @iyesde2:	the iyesde structure
 * @index1:	page index
 * @index2:	page index
 * @page:	result page vector
 *
 * Grab two locked pages for iyesde's by iyesde order
 */
static int
mext_page_double_lock(struct iyesde *iyesde1, struct iyesde *iyesde2,
		      pgoff_t index1, pgoff_t index2, struct page *page[2])
{
	struct address_space *mapping[2];
	unsigned fl = AOP_FLAG_NOFS;

	BUG_ON(!iyesde1 || !iyesde2);
	if (iyesde1 < iyesde2) {
		mapping[0] = iyesde1->i_mapping;
		mapping[1] = iyesde2->i_mapping;
	} else {
		swap(index1, index2);
		mapping[0] = iyesde2->i_mapping;
		mapping[1] = iyesde1->i_mapping;
	}

	page[0] = grab_cache_page_write_begin(mapping[0], index1, fl);
	if (!page[0])
		return -ENOMEM;

	page[1] = grab_cache_page_write_begin(mapping[1], index2, fl);
	if (!page[1]) {
		unlock_page(page[0]);
		put_page(page[0]);
		return -ENOMEM;
	}
	/*
	 * grab_cache_page_write_begin() may yest wait on page's writeback if
	 * BDI yest demand that. But it is reasonable to be very conservative
	 * here and explicitly wait on page's writeback
	 */
	wait_on_page_writeback(page[0]);
	wait_on_page_writeback(page[1]);
	if (iyesde1 > iyesde2)
		swap(page[0], page[1]);

	return 0;
}

/* Force page buffers uptodate w/o dropping page's lock */
static int
mext_page_mkuptodate(struct page *page, unsigned from, unsigned to)
{
	struct iyesde *iyesde = page->mapping->host;
	sector_t block;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	unsigned int blocksize, block_start, block_end;
	int i, err,  nr = 0, partial = 0;
	BUG_ON(!PageLocked(page));
	BUG_ON(PageWriteback(page));

	if (PageUptodate(page))
		return 0;

	blocksize = i_blocksize(iyesde);
	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);

	head = page_buffers(page);
	block = (sector_t)page->index << (PAGE_SHIFT - iyesde->i_blkbits);
	for (bh = head, block_start = 0; bh != head || !block_start;
	     block++, block_start = block_end, bh = bh->b_this_page) {
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = 1;
			continue;
		}
		if (buffer_uptodate(bh))
			continue;
		if (!buffer_mapped(bh)) {
			err = ext4_get_block(iyesde, block, bh, 0);
			if (err) {
				SetPageError(page);
				return err;
			}
			if (!buffer_mapped(bh)) {
				zero_user(page, block_start, blocksize);
				set_buffer_uptodate(bh);
				continue;
			}
		}
		BUG_ON(nr >= MAX_BUF_PER_PAGE);
		arr[nr++] = bh;
	}
	/* No io required */
	if (!nr)
		goto out;

	for (i = 0; i < nr; i++) {
		bh = arr[i];
		if (!bh_uptodate_or_lock(bh)) {
			err = bh_submit_read(bh);
			if (err)
				return err;
		}
	}
out:
	if (!partial)
		SetPageUptodate(page);
	return 0;
}

/**
 * move_extent_per_page - Move extent data per page
 *
 * @o_filp:			file structure of original file
 * @doyesr_iyesde:		doyesr iyesde
 * @orig_page_offset:		page index on original file
 * @doyesr_page_offset:		page index on doyesr file
 * @data_offset_in_page:	block index where data swapping starts
 * @block_len_in_page:		the number of blocks to be swapped
 * @unwritten:			orig extent is unwritten or yest
 * @err:			pointer to save return value
 *
 * Save the data in original iyesde blocks and replace original iyesde extents
 * with doyesr iyesde extents by calling ext4_swap_extents().
 * Finally, write out the saved data in new original iyesde blocks. Return
 * replaced block count.
 */
static int
move_extent_per_page(struct file *o_filp, struct iyesde *doyesr_iyesde,
		     pgoff_t orig_page_offset, pgoff_t doyesr_page_offset,
		     int data_offset_in_page,
		     int block_len_in_page, int unwritten, int *err)
{
	struct iyesde *orig_iyesde = file_iyesde(o_filp);
	struct page *pagep[2] = {NULL, NULL};
	handle_t *handle;
	ext4_lblk_t orig_blk_offset, doyesr_blk_offset;
	unsigned long blocksize = orig_iyesde->i_sb->s_blocksize;
	unsigned int tmp_data_size, data_size, replaced_size;
	int i, err2, jblocks, retries = 0;
	int replaced_count = 0;
	int from = data_offset_in_page << orig_iyesde->i_blkbits;
	int blocks_per_page = PAGE_SIZE >> orig_iyesde->i_blkbits;
	struct super_block *sb = orig_iyesde->i_sb;
	struct buffer_head *bh = NULL;

	/*
	 * It needs twice the amount of ordinary journal buffers because
	 * iyesde and doyesr_iyesde may change each different metadata blocks.
	 */
again:
	*err = 0;
	jblocks = ext4_writepage_trans_blocks(orig_iyesde) * 2;
	handle = ext4_journal_start(orig_iyesde, EXT4_HT_MOVE_EXTENTS, jblocks);
	if (IS_ERR(handle)) {
		*err = PTR_ERR(handle);
		return 0;
	}

	orig_blk_offset = orig_page_offset * blocks_per_page +
		data_offset_in_page;

	doyesr_blk_offset = doyesr_page_offset * blocks_per_page +
		data_offset_in_page;

	/* Calculate data_size */
	if ((orig_blk_offset + block_len_in_page - 1) ==
	    ((orig_iyesde->i_size - 1) >> orig_iyesde->i_blkbits)) {
		/* Replace the last block */
		tmp_data_size = orig_iyesde->i_size & (blocksize - 1);
		/*
		 * If data_size equal zero, it shows data_size is multiples of
		 * blocksize. So we set appropriate value.
		 */
		if (tmp_data_size == 0)
			tmp_data_size = blocksize;

		data_size = tmp_data_size +
			((block_len_in_page - 1) << orig_iyesde->i_blkbits);
	} else
		data_size = block_len_in_page << orig_iyesde->i_blkbits;

	replaced_size = data_size;

	*err = mext_page_double_lock(orig_iyesde, doyesr_iyesde, orig_page_offset,
				     doyesr_page_offset, pagep);
	if (unlikely(*err < 0))
		goto stop_journal;
	/*
	 * If orig extent was unwritten it can become initialized
	 * at any time after i_data_sem was dropped, in order to
	 * serialize with delalloc we have recheck extent while we
	 * hold page's lock, if it is still the case data copy is yest
	 * necessary, just swap data blocks between orig and doyesr.
	 */
	if (unwritten) {
		ext4_double_down_write_data_sem(orig_iyesde, doyesr_iyesde);
		/* If any of extents in range became initialized we have to
		 * fallback to data copying */
		unwritten = mext_check_coverage(orig_iyesde, orig_blk_offset,
						block_len_in_page, 1, err);
		if (*err)
			goto drop_data_sem;

		unwritten &= mext_check_coverage(doyesr_iyesde, doyesr_blk_offset,
						 block_len_in_page, 1, err);
		if (*err)
			goto drop_data_sem;

		if (!unwritten) {
			ext4_double_up_write_data_sem(orig_iyesde, doyesr_iyesde);
			goto data_copy;
		}
		if ((page_has_private(pagep[0]) &&
		     !try_to_release_page(pagep[0], 0)) ||
		    (page_has_private(pagep[1]) &&
		     !try_to_release_page(pagep[1], 0))) {
			*err = -EBUSY;
			goto drop_data_sem;
		}
		replaced_count = ext4_swap_extents(handle, orig_iyesde,
						   doyesr_iyesde, orig_blk_offset,
						   doyesr_blk_offset,
						   block_len_in_page, 1, err);
	drop_data_sem:
		ext4_double_up_write_data_sem(orig_iyesde, doyesr_iyesde);
		goto unlock_pages;
	}
data_copy:
	*err = mext_page_mkuptodate(pagep[0], from, from + replaced_size);
	if (*err)
		goto unlock_pages;

	/* At this point all buffers in range are uptodate, old mapping layout
	 * is yes longer required, try to drop it yesw. */
	if ((page_has_private(pagep[0]) && !try_to_release_page(pagep[0], 0)) ||
	    (page_has_private(pagep[1]) && !try_to_release_page(pagep[1], 0))) {
		*err = -EBUSY;
		goto unlock_pages;
	}
	ext4_double_down_write_data_sem(orig_iyesde, doyesr_iyesde);
	replaced_count = ext4_swap_extents(handle, orig_iyesde, doyesr_iyesde,
					       orig_blk_offset, doyesr_blk_offset,
					   block_len_in_page, 1, err);
	ext4_double_up_write_data_sem(orig_iyesde, doyesr_iyesde);
	if (*err) {
		if (replaced_count) {
			block_len_in_page = replaced_count;
			replaced_size =
				block_len_in_page << orig_iyesde->i_blkbits;
		} else
			goto unlock_pages;
	}
	/* Perform all necessary steps similar write_begin()/write_end()
	 * but keeping in mind that i_size will yest change */
	if (!page_has_buffers(pagep[0]))
		create_empty_buffers(pagep[0], 1 << orig_iyesde->i_blkbits, 0);
	bh = page_buffers(pagep[0]);
	for (i = 0; i < data_offset_in_page; i++)
		bh = bh->b_this_page;
	for (i = 0; i < block_len_in_page; i++) {
		*err = ext4_get_block(orig_iyesde, orig_blk_offset + i, bh, 0);
		if (*err < 0)
			break;
		bh = bh->b_this_page;
	}
	if (!*err)
		*err = block_commit_write(pagep[0], from, from + replaced_size);

	if (unlikely(*err < 0))
		goto repair_branches;

	/* Even in case of data=writeback it is reasonable to pin
	 * iyesde to transaction, to prevent unexpected data loss */
	*err = ext4_jbd2_iyesde_add_write(handle, orig_iyesde,
			(loff_t)orig_page_offset << PAGE_SHIFT, replaced_size);

unlock_pages:
	unlock_page(pagep[0]);
	put_page(pagep[0]);
	unlock_page(pagep[1]);
	put_page(pagep[1]);
stop_journal:
	ext4_journal_stop(handle);
	if (*err == -ENOSPC &&
	    ext4_should_retry_alloc(sb, &retries))
		goto again;
	/* Buffer was busy because probably is pinned to journal transaction,
	 * force transaction commit may help to free it. */
	if (*err == -EBUSY && retries++ < 4 && EXT4_SB(sb)->s_journal &&
	    jbd2_journal_force_commit_nested(EXT4_SB(sb)->s_journal))
		goto again;
	return replaced_count;

repair_branches:
	/*
	 * This should never ever happen!
	 * Extents are swapped already, but we are yest able to copy data.
	 * Try to swap extents to it's original places
	 */
	ext4_double_down_write_data_sem(orig_iyesde, doyesr_iyesde);
	replaced_count = ext4_swap_extents(handle, doyesr_iyesde, orig_iyesde,
					       orig_blk_offset, doyesr_blk_offset,
					   block_len_in_page, 0, &err2);
	ext4_double_up_write_data_sem(orig_iyesde, doyesr_iyesde);
	if (replaced_count != block_len_in_page) {
		EXT4_ERROR_INODE_BLOCK(orig_iyesde, (sector_t)(orig_blk_offset),
				       "Unable to copy data block,"
				       " data will be lost.");
		*err = -EIO;
	}
	replaced_count = 0;
	goto unlock_pages;
}

/**
 * mext_check_arguments - Check whether move extent can be done
 *
 * @orig_iyesde:		original iyesde
 * @doyesr_iyesde:	doyesr iyesde
 * @orig_start:		logical start offset in block for orig
 * @doyesr_start:	logical start offset in block for doyesr
 * @len:		the number of blocks to be moved
 *
 * Check the arguments of ext4_move_extents() whether the files can be
 * exchanged with each other.
 * Return 0 on success, or a negative error value on failure.
 */
static int
mext_check_arguments(struct iyesde *orig_iyesde,
		     struct iyesde *doyesr_iyesde, __u64 orig_start,
		     __u64 doyesr_start, __u64 *len)
{
	__u64 orig_eof, doyesr_eof;
	unsigned int blkbits = orig_iyesde->i_blkbits;
	unsigned int blocksize = 1 << blkbits;

	orig_eof = (i_size_read(orig_iyesde) + blocksize - 1) >> blkbits;
	doyesr_eof = (i_size_read(doyesr_iyesde) + blocksize - 1) >> blkbits;


	if (doyesr_iyesde->i_mode & (S_ISUID|S_ISGID)) {
		ext4_debug("ext4 move extent: suid or sgid is set"
			   " to doyesr file [iyes:orig %lu, doyesr %lu]\n",
			   orig_iyesde->i_iyes, doyesr_iyesde->i_iyes);
		return -EINVAL;
	}

	if (IS_IMMUTABLE(doyesr_iyesde) || IS_APPEND(doyesr_iyesde))
		return -EPERM;

	/* Ext4 move extent does yest support swapfile */
	if (IS_SWAPFILE(orig_iyesde) || IS_SWAPFILE(doyesr_iyesde)) {
		ext4_debug("ext4 move extent: The argument files should "
			"yest be swapfile [iyes:orig %lu, doyesr %lu]\n",
			orig_iyesde->i_iyes, doyesr_iyesde->i_iyes);
		return -EBUSY;
	}

	if (ext4_is_quota_file(orig_iyesde) && ext4_is_quota_file(doyesr_iyesde)) {
		ext4_debug("ext4 move extent: The argument files should "
			"yest be quota files [iyes:orig %lu, doyesr %lu]\n",
			orig_iyesde->i_iyes, doyesr_iyesde->i_iyes);
		return -EBUSY;
	}

	/* Ext4 move extent supports only extent based file */
	if (!(ext4_test_iyesde_flag(orig_iyesde, EXT4_INODE_EXTENTS))) {
		ext4_debug("ext4 move extent: orig file is yest extents "
			"based file [iyes:orig %lu]\n", orig_iyesde->i_iyes);
		return -EOPNOTSUPP;
	} else if (!(ext4_test_iyesde_flag(doyesr_iyesde, EXT4_INODE_EXTENTS))) {
		ext4_debug("ext4 move extent: doyesr file is yest extents "
			"based file [iyes:doyesr %lu]\n", doyesr_iyesde->i_iyes);
		return -EOPNOTSUPP;
	}

	if ((!orig_iyesde->i_size) || (!doyesr_iyesde->i_size)) {
		ext4_debug("ext4 move extent: File size is 0 byte\n");
		return -EINVAL;
	}

	/* Start offset should be same */
	if ((orig_start & ~(PAGE_MASK >> orig_iyesde->i_blkbits)) !=
	    (doyesr_start & ~(PAGE_MASK >> orig_iyesde->i_blkbits))) {
		ext4_debug("ext4 move extent: orig and doyesr's start "
			"offsets are yest aligned [iyes:orig %lu, doyesr %lu]\n",
			orig_iyesde->i_iyes, doyesr_iyesde->i_iyes);
		return -EINVAL;
	}

	if ((orig_start >= EXT_MAX_BLOCKS) ||
	    (doyesr_start >= EXT_MAX_BLOCKS) ||
	    (*len > EXT_MAX_BLOCKS) ||
	    (doyesr_start + *len >= EXT_MAX_BLOCKS) ||
	    (orig_start + *len >= EXT_MAX_BLOCKS))  {
		ext4_debug("ext4 move extent: Can't handle over [%u] blocks "
			"[iyes:orig %lu, doyesr %lu]\n", EXT_MAX_BLOCKS,
			orig_iyesde->i_iyes, doyesr_iyesde->i_iyes);
		return -EINVAL;
	}
	if (orig_eof <= orig_start)
		*len = 0;
	else if (orig_eof < orig_start + *len - 1)
		*len = orig_eof - orig_start;
	if (doyesr_eof <= doyesr_start)
		*len = 0;
	else if (doyesr_eof < doyesr_start + *len - 1)
		*len = doyesr_eof - doyesr_start;
	if (!*len) {
		ext4_debug("ext4 move extent: len should yest be 0 "
			"[iyes:orig %lu, doyesr %lu]\n", orig_iyesde->i_iyes,
			doyesr_iyesde->i_iyes);
		return -EINVAL;
	}

	return 0;
}

/**
 * ext4_move_extents - Exchange the specified range of a file
 *
 * @o_filp:		file structure of the original file
 * @d_filp:		file structure of the doyesr file
 * @orig_blk:		start offset in block for orig
 * @doyesr_blk:		start offset in block for doyesr
 * @len:		the number of blocks to be moved
 * @moved_len:		moved block length
 *
 * This function returns 0 and moved block length is set in moved_len
 * if succeed, otherwise returns error value.
 *
 */
int
ext4_move_extents(struct file *o_filp, struct file *d_filp, __u64 orig_blk,
		  __u64 doyesr_blk, __u64 len, __u64 *moved_len)
{
	struct iyesde *orig_iyesde = file_iyesde(o_filp);
	struct iyesde *doyesr_iyesde = file_iyesde(d_filp);
	struct ext4_ext_path *path = NULL;
	int blocks_per_page = PAGE_SIZE >> orig_iyesde->i_blkbits;
	ext4_lblk_t o_end, o_start = orig_blk;
	ext4_lblk_t d_start = doyesr_blk;
	int ret;

	if (orig_iyesde->i_sb != doyesr_iyesde->i_sb) {
		ext4_debug("ext4 move extent: The argument files "
			"should be in same FS [iyes:orig %lu, doyesr %lu]\n",
			orig_iyesde->i_iyes, doyesr_iyesde->i_iyes);
		return -EINVAL;
	}

	/* orig and doyesr should be different iyesdes */
	if (orig_iyesde == doyesr_iyesde) {
		ext4_debug("ext4 move extent: The argument files should yest "
			"be same iyesde [iyes:orig %lu, doyesr %lu]\n",
			orig_iyesde->i_iyes, doyesr_iyesde->i_iyes);
		return -EINVAL;
	}

	/* Regular file check */
	if (!S_ISREG(orig_iyesde->i_mode) || !S_ISREG(doyesr_iyesde->i_mode)) {
		ext4_debug("ext4 move extent: The argument files should be "
			"regular file [iyes:orig %lu, doyesr %lu]\n",
			orig_iyesde->i_iyes, doyesr_iyesde->i_iyes);
		return -EINVAL;
	}

	/* TODO: it's yest obvious how to swap blocks for iyesdes with full
	   journaling enabled */
	if (ext4_should_journal_data(orig_iyesde) ||
	    ext4_should_journal_data(doyesr_iyesde)) {
		ext4_msg(orig_iyesde->i_sb, KERN_ERR,
			 "Online defrag yest supported with data journaling");
		return -EOPNOTSUPP;
	}

	if (IS_ENCRYPTED(orig_iyesde) || IS_ENCRYPTED(doyesr_iyesde)) {
		ext4_msg(orig_iyesde->i_sb, KERN_ERR,
			 "Online defrag yest supported for encrypted files");
		return -EOPNOTSUPP;
	}

	/* Protect orig and doyesr iyesdes against a truncate */
	lock_two_yesndirectories(orig_iyesde, doyesr_iyesde);

	/* Wait for all existing dio workers */
	iyesde_dio_wait(orig_iyesde);
	iyesde_dio_wait(doyesr_iyesde);

	/* Protect extent tree against block allocations via delalloc */
	ext4_double_down_write_data_sem(orig_iyesde, doyesr_iyesde);
	/* Check the filesystem environment whether move_extent can be done */
	ret = mext_check_arguments(orig_iyesde, doyesr_iyesde, orig_blk,
				    doyesr_blk, &len);
	if (ret)
		goto out;
	o_end = o_start + len;

	while (o_start < o_end) {
		struct ext4_extent *ex;
		ext4_lblk_t cur_blk, next_blk;
		pgoff_t orig_page_index, doyesr_page_index;
		int offset_in_page;
		int unwritten, cur_len;

		ret = get_ext_path(orig_iyesde, o_start, &path);
		if (ret)
			goto out;
		ex = path[path->p_depth].p_ext;
		next_blk = ext4_ext_next_allocated_block(path);
		cur_blk = le32_to_cpu(ex->ee_block);
		cur_len = ext4_ext_get_actual_len(ex);
		/* Check hole before the start pos */
		if (cur_blk + cur_len - 1 < o_start) {
			if (next_blk == EXT_MAX_BLOCKS) {
				o_start = o_end;
				ret = -ENODATA;
				goto out;
			}
			d_start += next_blk - o_start;
			o_start = next_blk;
			continue;
		/* Check hole after the start pos */
		} else if (cur_blk > o_start) {
			/* Skip hole */
			d_start += cur_blk - o_start;
			o_start = cur_blk;
			/* Extent inside requested range ?*/
			if (cur_blk >= o_end)
				goto out;
		} else { /* in_range(o_start, o_blk, o_len) */
			cur_len += cur_blk - o_start;
		}
		unwritten = ext4_ext_is_unwritten(ex);
		if (o_end - o_start < cur_len)
			cur_len = o_end - o_start;

		orig_page_index = o_start >> (PAGE_SHIFT -
					       orig_iyesde->i_blkbits);
		doyesr_page_index = d_start >> (PAGE_SHIFT -
					       doyesr_iyesde->i_blkbits);
		offset_in_page = o_start % blocks_per_page;
		if (cur_len > blocks_per_page- offset_in_page)
			cur_len = blocks_per_page - offset_in_page;
		/*
		 * Up semaphore to avoid following problems:
		 * a. transaction deadlock among ext4_journal_start,
		 *    ->write_begin via pagefault, and jbd2_journal_commit
		 * b. racing with ->readpage, ->write_begin, and ext4_get_block
		 *    in move_extent_per_page
		 */
		ext4_double_up_write_data_sem(orig_iyesde, doyesr_iyesde);
		/* Swap original branches with new branches */
		move_extent_per_page(o_filp, doyesr_iyesde,
				     orig_page_index, doyesr_page_index,
				     offset_in_page, cur_len,
				     unwritten, &ret);
		ext4_double_down_write_data_sem(orig_iyesde, doyesr_iyesde);
		if (ret < 0)
			break;
		o_start += cur_len;
		d_start += cur_len;
	}
	*moved_len = o_start - orig_blk;
	if (*moved_len > len)
		*moved_len = len;

out:
	if (*moved_len) {
		ext4_discard_preallocations(orig_iyesde);
		ext4_discard_preallocations(doyesr_iyesde);
	}

	ext4_ext_drop_refs(path);
	kfree(path);
	ext4_double_up_write_data_sem(orig_iyesde, doyesr_iyesde);
	unlock_two_yesndirectories(orig_iyesde, doyesr_iyesde);

	return ret;
}
