// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (c) 2008,2009 NEC Software Tohoku, Ltd.
 * Written by Takashi Sato <t-sato@yk.jp.nec.com>
 *            Akira Fujita <a-fujita@rs.jp.nec.com>
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include "ext4_extents.h"

/**
 * get_ext_path() - Find an extent path for designated logical block number.
 * @inode:	inode to be searched
 * @lblock:	logical block number to find an extent path
 * @ppath:	pointer to an extent path pointer (for output)
 *
 * ext4_find_extent wrapper. Return 0 on success, or a negative error value
 * on failure.
 */
static inline int
get_ext_path(struct inode *inode, ext4_lblk_t lblock,
		struct ext4_ext_path **ppath)
{
	struct ext4_ext_path *path;

	path = ext4_find_extent(inode, lblock, ppath, EXT4_EX_NOCACHE);
	if (IS_ERR(path))
		return PTR_ERR(path);
	if (path[ext_depth(inode)].p_ext == NULL) {
		ext4_free_ext_path(path);
		*ppath = NULL;
		return -ENODATA;
	}
	*ppath = path;
	return 0;
}

/**
 * ext4_double_down_write_data_sem() - write lock two inodes's i_data_sem
 * @first: inode to be locked
 * @second: inode to be locked
 *
 * Acquire write lock of i_data_sem of the two inodes
 */
void
ext4_double_down_write_data_sem(struct inode *first, struct inode *second)
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
 * ext4_double_up_write_data_sem - Release two inodes' write lock of i_data_sem
 *
 * @orig_inode:		original inode structure to be released its lock first
 * @donor_inode:	donor inode structure to be released its lock second
 * Release write lock of i_data_sem of two inodes (orig and donor).
 */
void
ext4_double_up_write_data_sem(struct inode *orig_inode,
			      struct inode *donor_inode)
{
	up_write(&EXT4_I(orig_inode)->i_data_sem);
	up_write(&EXT4_I(donor_inode)->i_data_sem);
}

/**
 * mext_check_coverage - Check that all extents in range has the same type
 *
 * @inode:		inode in question
 * @from:		block offset of inode
 * @count:		block count to be checked
 * @unwritten:		extents expected to be unwritten
 * @err:		pointer to save error value
 *
 * Return 1 if all extents in range has expected type, and zero otherwise.
 */
static int
mext_check_coverage(struct inode *inode, ext4_lblk_t from, ext4_lblk_t count,
		    int unwritten, int *err)
{
	struct ext4_ext_path *path = NULL;
	struct ext4_extent *ext;
	int ret = 0;
	ext4_lblk_t last = from + count;
	while (from < last) {
		*err = get_ext_path(inode, from, &path);
		if (*err)
			goto out;
		ext = path[ext_depth(inode)].p_ext;
		if (unwritten != ext4_ext_is_unwritten(ext))
			goto out;
		from += ext4_ext_get_actual_len(ext);
	}
	ret = 1;
out:
	ext4_free_ext_path(path);
	return ret;
}

/**
 * mext_folio_double_lock - Grab and lock folio on both @inode1 and @inode2
 *
 * @inode1:	the inode structure
 * @inode2:	the inode structure
 * @index1:	folio index
 * @index2:	folio index
 * @folio:	result folio vector
 *
 * Grab two locked folio for inode's by inode order
 */
static int
mext_folio_double_lock(struct inode *inode1, struct inode *inode2,
		      pgoff_t index1, pgoff_t index2, struct folio *folio[2])
{
	struct address_space *mapping[2];
	unsigned int flags;

	BUG_ON(!inode1 || !inode2);
	if (inode1 < inode2) {
		mapping[0] = inode1->i_mapping;
		mapping[1] = inode2->i_mapping;
	} else {
		swap(index1, index2);
		mapping[0] = inode2->i_mapping;
		mapping[1] = inode1->i_mapping;
	}

	flags = memalloc_nofs_save();
	folio[0] = __filemap_get_folio(mapping[0], index1, FGP_WRITEBEGIN,
			mapping_gfp_mask(mapping[0]));
	if (IS_ERR(folio[0])) {
		memalloc_nofs_restore(flags);
		return PTR_ERR(folio[0]);
	}

	folio[1] = __filemap_get_folio(mapping[1], index2, FGP_WRITEBEGIN,
			mapping_gfp_mask(mapping[1]));
	memalloc_nofs_restore(flags);
	if (IS_ERR(folio[1])) {
		folio_unlock(folio[0]);
		folio_put(folio[0]);
		return PTR_ERR(folio[1]);
	}
	/*
	 * __filemap_get_folio() may not wait on folio's writeback if
	 * BDI not demand that. But it is reasonable to be very conservative
	 * here and explicitly wait on folio's writeback
	 */
	folio_wait_writeback(folio[0]);
	folio_wait_writeback(folio[1]);
	if (inode1 > inode2)
		swap(folio[0], folio[1]);

	return 0;
}

/* Force page buffers uptodate w/o dropping page's lock */
static int
mext_page_mkuptodate(struct folio *folio, unsigned from, unsigned to)
{
	struct inode *inode = folio->mapping->host;
	sector_t block;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	unsigned int blocksize, block_start, block_end;
	int i, err,  nr = 0, partial = 0;
	BUG_ON(!folio_test_locked(folio));
	BUG_ON(folio_test_writeback(folio));

	if (folio_test_uptodate(folio))
		return 0;

	blocksize = i_blocksize(inode);
	head = folio_buffers(folio);
	if (!head) {
		create_empty_buffers(&folio->page, blocksize, 0);
		head = folio_buffers(folio);
	}

	block = (sector_t)folio->index << (PAGE_SHIFT - inode->i_blkbits);
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
			err = ext4_get_block(inode, block, bh, 0);
			if (err) {
				folio_set_error(folio);
				return err;
			}
			if (!buffer_mapped(bh)) {
				folio_zero_range(folio, block_start, blocksize);
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
			err = ext4_read_bh(bh, 0, NULL);
			if (err)
				return err;
		}
	}
out:
	if (!partial)
		folio_mark_uptodate(folio);
	return 0;
}

/**
 * move_extent_per_page - Move extent data per page
 *
 * @o_filp:			file structure of original file
 * @donor_inode:		donor inode
 * @orig_page_offset:		page index on original file
 * @donor_page_offset:		page index on donor file
 * @data_offset_in_page:	block index where data swapping starts
 * @block_len_in_page:		the number of blocks to be swapped
 * @unwritten:			orig extent is unwritten or not
 * @err:			pointer to save return value
 *
 * Save the data in original inode blocks and replace original inode extents
 * with donor inode extents by calling ext4_swap_extents().
 * Finally, write out the saved data in new original inode blocks. Return
 * replaced block count.
 */
static int
move_extent_per_page(struct file *o_filp, struct inode *donor_inode,
		     pgoff_t orig_page_offset, pgoff_t donor_page_offset,
		     int data_offset_in_page,
		     int block_len_in_page, int unwritten, int *err)
{
	struct inode *orig_inode = file_inode(o_filp);
	struct folio *folio[2] = {NULL, NULL};
	handle_t *handle;
	ext4_lblk_t orig_blk_offset, donor_blk_offset;
	unsigned long blocksize = orig_inode->i_sb->s_blocksize;
	unsigned int tmp_data_size, data_size, replaced_size;
	int i, err2, jblocks, retries = 0;
	int replaced_count = 0;
	int from = data_offset_in_page << orig_inode->i_blkbits;
	int blocks_per_page = PAGE_SIZE >> orig_inode->i_blkbits;
	struct super_block *sb = orig_inode->i_sb;
	struct buffer_head *bh = NULL;

	/*
	 * It needs twice the amount of ordinary journal buffers because
	 * inode and donor_inode may change each different metadata blocks.
	 */
again:
	*err = 0;
	jblocks = ext4_writepage_trans_blocks(orig_inode) * 2;
	handle = ext4_journal_start(orig_inode, EXT4_HT_MOVE_EXTENTS, jblocks);
	if (IS_ERR(handle)) {
		*err = PTR_ERR(handle);
		return 0;
	}

	orig_blk_offset = orig_page_offset * blocks_per_page +
		data_offset_in_page;

	donor_blk_offset = donor_page_offset * blocks_per_page +
		data_offset_in_page;

	/* Calculate data_size */
	if ((orig_blk_offset + block_len_in_page - 1) ==
	    ((orig_inode->i_size - 1) >> orig_inode->i_blkbits)) {
		/* Replace the last block */
		tmp_data_size = orig_inode->i_size & (blocksize - 1);
		/*
		 * If data_size equal zero, it shows data_size is multiples of
		 * blocksize. So we set appropriate value.
		 */
		if (tmp_data_size == 0)
			tmp_data_size = blocksize;

		data_size = tmp_data_size +
			((block_len_in_page - 1) << orig_inode->i_blkbits);
	} else
		data_size = block_len_in_page << orig_inode->i_blkbits;

	replaced_size = data_size;

	*err = mext_folio_double_lock(orig_inode, donor_inode, orig_page_offset,
				     donor_page_offset, folio);
	if (unlikely(*err < 0))
		goto stop_journal;
	/*
	 * If orig extent was unwritten it can become initialized
	 * at any time after i_data_sem was dropped, in order to
	 * serialize with delalloc we have recheck extent while we
	 * hold page's lock, if it is still the case data copy is not
	 * necessary, just swap data blocks between orig and donor.
	 */

	VM_BUG_ON_FOLIO(folio_test_large(folio[0]), folio[0]);
	VM_BUG_ON_FOLIO(folio_test_large(folio[1]), folio[1]);
	VM_BUG_ON_FOLIO(folio_nr_pages(folio[0]) != folio_nr_pages(folio[1]), folio[1]);

	if (unwritten) {
		ext4_double_down_write_data_sem(orig_inode, donor_inode);
		/* If any of extents in range became initialized we have to
		 * fallback to data copying */
		unwritten = mext_check_coverage(orig_inode, orig_blk_offset,
						block_len_in_page, 1, err);
		if (*err)
			goto drop_data_sem;

		unwritten &= mext_check_coverage(donor_inode, donor_blk_offset,
						 block_len_in_page, 1, err);
		if (*err)
			goto drop_data_sem;

		if (!unwritten) {
			ext4_double_up_write_data_sem(orig_inode, donor_inode);
			goto data_copy;
		}
		if ((folio_has_private(folio[0]) &&
		     !filemap_release_folio(folio[0], 0)) ||
		    (folio_has_private(folio[1]) &&
		     !filemap_release_folio(folio[1], 0))) {
			*err = -EBUSY;
			goto drop_data_sem;
		}
		replaced_count = ext4_swap_extents(handle, orig_inode,
						   donor_inode, orig_blk_offset,
						   donor_blk_offset,
						   block_len_in_page, 1, err);
	drop_data_sem:
		ext4_double_up_write_data_sem(orig_inode, donor_inode);
		goto unlock_folios;
	}
data_copy:
	*err = mext_page_mkuptodate(folio[0], from, from + replaced_size);
	if (*err)
		goto unlock_folios;

	/* At this point all buffers in range are uptodate, old mapping layout
	 * is no longer required, try to drop it now. */
	if ((folio_has_private(folio[0]) &&
		!filemap_release_folio(folio[0], 0)) ||
	    (folio_has_private(folio[1]) &&
		!filemap_release_folio(folio[1], 0))) {
		*err = -EBUSY;
		goto unlock_folios;
	}
	ext4_double_down_write_data_sem(orig_inode, donor_inode);
	replaced_count = ext4_swap_extents(handle, orig_inode, donor_inode,
					       orig_blk_offset, donor_blk_offset,
					   block_len_in_page, 1, err);
	ext4_double_up_write_data_sem(orig_inode, donor_inode);
	if (*err) {
		if (replaced_count) {
			block_len_in_page = replaced_count;
			replaced_size =
				block_len_in_page << orig_inode->i_blkbits;
		} else
			goto unlock_folios;
	}
	/* Perform all necessary steps similar write_begin()/write_end()
	 * but keeping in mind that i_size will not change */
	if (!folio_buffers(folio[0]))
		create_empty_buffers(&folio[0]->page, 1 << orig_inode->i_blkbits, 0);
	bh = folio_buffers(folio[0]);
	for (i = 0; i < data_offset_in_page; i++)
		bh = bh->b_this_page;
	for (i = 0; i < block_len_in_page; i++) {
		*err = ext4_get_block(orig_inode, orig_blk_offset + i, bh, 0);
		if (*err < 0)
			goto repair_branches;
		bh = bh->b_this_page;
	}

	block_commit_write(&folio[0]->page, from, from + replaced_size);

	/* Even in case of data=writeback it is reasonable to pin
	 * inode to transaction, to prevent unexpected data loss */
	*err = ext4_jbd2_inode_add_write(handle, orig_inode,
			(loff_t)orig_page_offset << PAGE_SHIFT, replaced_size);

unlock_folios:
	folio_unlock(folio[0]);
	folio_put(folio[0]);
	folio_unlock(folio[1]);
	folio_put(folio[1]);
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
	 * Extents are swapped already, but we are not able to copy data.
	 * Try to swap extents to it's original places
	 */
	ext4_double_down_write_data_sem(orig_inode, donor_inode);
	replaced_count = ext4_swap_extents(handle, donor_inode, orig_inode,
					       orig_blk_offset, donor_blk_offset,
					   block_len_in_page, 0, &err2);
	ext4_double_up_write_data_sem(orig_inode, donor_inode);
	if (replaced_count != block_len_in_page) {
		ext4_error_inode_block(orig_inode, (sector_t)(orig_blk_offset),
				       EIO, "Unable to copy data block,"
				       " data will be lost.");
		*err = -EIO;
	}
	replaced_count = 0;
	goto unlock_folios;
}

/**
 * mext_check_arguments - Check whether move extent can be done
 *
 * @orig_inode:		original inode
 * @donor_inode:	donor inode
 * @orig_start:		logical start offset in block for orig
 * @donor_start:	logical start offset in block for donor
 * @len:		the number of blocks to be moved
 *
 * Check the arguments of ext4_move_extents() whether the files can be
 * exchanged with each other.
 * Return 0 on success, or a negative error value on failure.
 */
static int
mext_check_arguments(struct inode *orig_inode,
		     struct inode *donor_inode, __u64 orig_start,
		     __u64 donor_start, __u64 *len)
{
	__u64 orig_eof, donor_eof;
	unsigned int blkbits = orig_inode->i_blkbits;
	unsigned int blocksize = 1 << blkbits;

	orig_eof = (i_size_read(orig_inode) + blocksize - 1) >> blkbits;
	donor_eof = (i_size_read(donor_inode) + blocksize - 1) >> blkbits;


	if (donor_inode->i_mode & (S_ISUID|S_ISGID)) {
		ext4_debug("ext4 move extent: suid or sgid is set"
			   " to donor file [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if (IS_IMMUTABLE(donor_inode) || IS_APPEND(donor_inode))
		return -EPERM;

	/* Ext4 move extent does not support swap files */
	if (IS_SWAPFILE(orig_inode) || IS_SWAPFILE(donor_inode)) {
		ext4_debug("ext4 move extent: The argument files should not be swap files [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -ETXTBSY;
	}

	if (ext4_is_quota_file(orig_inode) && ext4_is_quota_file(donor_inode)) {
		ext4_debug("ext4 move extent: The argument files should not be quota files [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EOPNOTSUPP;
	}

	/* Ext4 move extent supports only extent based file */
	if (!(ext4_test_inode_flag(orig_inode, EXT4_INODE_EXTENTS))) {
		ext4_debug("ext4 move extent: orig file is not extents "
			"based file [ino:orig %lu]\n", orig_inode->i_ino);
		return -EOPNOTSUPP;
	} else if (!(ext4_test_inode_flag(donor_inode, EXT4_INODE_EXTENTS))) {
		ext4_debug("ext4 move extent: donor file is not extents "
			"based file [ino:donor %lu]\n", donor_inode->i_ino);
		return -EOPNOTSUPP;
	}

	if ((!orig_inode->i_size) || (!donor_inode->i_size)) {
		ext4_debug("ext4 move extent: File size is 0 byte\n");
		return -EINVAL;
	}

	/* Start offset should be same */
	if ((orig_start & ~(PAGE_MASK >> orig_inode->i_blkbits)) !=
	    (donor_start & ~(PAGE_MASK >> orig_inode->i_blkbits))) {
		ext4_debug("ext4 move extent: orig and donor's start "
			"offsets are not aligned [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if ((orig_start >= EXT_MAX_BLOCKS) ||
	    (donor_start >= EXT_MAX_BLOCKS) ||
	    (*len > EXT_MAX_BLOCKS) ||
	    (donor_start + *len >= EXT_MAX_BLOCKS) ||
	    (orig_start + *len >= EXT_MAX_BLOCKS))  {
		ext4_debug("ext4 move extent: Can't handle over [%u] blocks "
			"[ino:orig %lu, donor %lu]\n", EXT_MAX_BLOCKS,
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}
	if (orig_eof <= orig_start)
		*len = 0;
	else if (orig_eof < orig_start + *len - 1)
		*len = orig_eof - orig_start;
	if (donor_eof <= donor_start)
		*len = 0;
	else if (donor_eof < donor_start + *len - 1)
		*len = donor_eof - donor_start;
	if (!*len) {
		ext4_debug("ext4 move extent: len should not be 0 "
			"[ino:orig %lu, donor %lu]\n", orig_inode->i_ino,
			donor_inode->i_ino);
		return -EINVAL;
	}

	return 0;
}

/**
 * ext4_move_extents - Exchange the specified range of a file
 *
 * @o_filp:		file structure of the original file
 * @d_filp:		file structure of the donor file
 * @orig_blk:		start offset in block for orig
 * @donor_blk:		start offset in block for donor
 * @len:		the number of blocks to be moved
 * @moved_len:		moved block length
 *
 * This function returns 0 and moved block length is set in moved_len
 * if succeed, otherwise returns error value.
 *
 */
int
ext4_move_extents(struct file *o_filp, struct file *d_filp, __u64 orig_blk,
		  __u64 donor_blk, __u64 len, __u64 *moved_len)
{
	struct inode *orig_inode = file_inode(o_filp);
	struct inode *donor_inode = file_inode(d_filp);
	struct ext4_ext_path *path = NULL;
	int blocks_per_page = PAGE_SIZE >> orig_inode->i_blkbits;
	ext4_lblk_t o_end, o_start = orig_blk;
	ext4_lblk_t d_start = donor_blk;
	int ret;

	if (orig_inode->i_sb != donor_inode->i_sb) {
		ext4_debug("ext4 move extent: The argument files "
			"should be in same FS [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* orig and donor should be different inodes */
	if (orig_inode == donor_inode) {
		ext4_debug("ext4 move extent: The argument files should not "
			"be same inode [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* Regular file check */
	if (!S_ISREG(orig_inode->i_mode) || !S_ISREG(donor_inode->i_mode)) {
		ext4_debug("ext4 move extent: The argument files should be "
			"regular file [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* TODO: it's not obvious how to swap blocks for inodes with full
	   journaling enabled */
	if (ext4_should_journal_data(orig_inode) ||
	    ext4_should_journal_data(donor_inode)) {
		ext4_msg(orig_inode->i_sb, KERN_ERR,
			 "Online defrag not supported with data journaling");
		return -EOPNOTSUPP;
	}

	if (IS_ENCRYPTED(orig_inode) || IS_ENCRYPTED(donor_inode)) {
		ext4_msg(orig_inode->i_sb, KERN_ERR,
			 "Online defrag not supported for encrypted files");
		return -EOPNOTSUPP;
	}

	/* Protect orig and donor inodes against a truncate */
	lock_two_nondirectories(orig_inode, donor_inode);

	/* Wait for all existing dio workers */
	inode_dio_wait(orig_inode);
	inode_dio_wait(donor_inode);

	/* Protect extent tree against block allocations via delalloc */
	ext4_double_down_write_data_sem(orig_inode, donor_inode);
	/* Check the filesystem environment whether move_extent can be done */
	ret = mext_check_arguments(orig_inode, donor_inode, orig_blk,
				    donor_blk, &len);
	if (ret)
		goto out;
	o_end = o_start + len;

	while (o_start < o_end) {
		struct ext4_extent *ex;
		ext4_lblk_t cur_blk, next_blk;
		pgoff_t orig_page_index, donor_page_index;
		int offset_in_page;
		int unwritten, cur_len;

		ret = get_ext_path(orig_inode, o_start, &path);
		if (ret)
			goto out;
		ex = path[path->p_depth].p_ext;
		cur_blk = le32_to_cpu(ex->ee_block);
		cur_len = ext4_ext_get_actual_len(ex);
		/* Check hole before the start pos */
		if (cur_blk + cur_len - 1 < o_start) {
			next_blk = ext4_ext_next_allocated_block(path);
			if (next_blk == EXT_MAX_BLOCKS) {
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
					       orig_inode->i_blkbits);
		donor_page_index = d_start >> (PAGE_SHIFT -
					       donor_inode->i_blkbits);
		offset_in_page = o_start % blocks_per_page;
		if (cur_len > blocks_per_page - offset_in_page)
			cur_len = blocks_per_page - offset_in_page;
		/*
		 * Up semaphore to avoid following problems:
		 * a. transaction deadlock among ext4_journal_start,
		 *    ->write_begin via pagefault, and jbd2_journal_commit
		 * b. racing with ->read_folio, ->write_begin, and
		 *    ext4_get_block in move_extent_per_page
		 */
		ext4_double_up_write_data_sem(orig_inode, donor_inode);
		/* Swap original branches with new branches */
		move_extent_per_page(o_filp, donor_inode,
				     orig_page_index, donor_page_index,
				     offset_in_page, cur_len,
				     unwritten, &ret);
		ext4_double_down_write_data_sem(orig_inode, donor_inode);
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
		ext4_discard_preallocations(orig_inode, 0);
		ext4_discard_preallocations(donor_inode, 0);
	}

	ext4_free_ext_path(path);
	ext4_double_up_write_data_sem(orig_inode, donor_inode);
	unlock_two_nondirectories(orig_inode, donor_inode);

	return ret;
}
