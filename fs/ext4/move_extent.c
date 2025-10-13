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

struct mext_data {
	struct inode *orig_inode;	/* Origin file inode */
	struct inode *donor_inode;	/* Donor file inode */
	struct ext4_map_blocks orig_map;/* Origin file's move mapping */
	ext4_lblk_t donor_lblk;		/* Start block of the donor file */
};

/**
 * get_ext_path() - Find an extent path for designated logical block number.
 * @inode:	inode to be searched
 * @lblock:	logical block number to find an extent path
 * @path:	pointer to an extent path
 *
 * ext4_find_extent wrapper. Return an extent path pointer on success,
 * or an error pointer on failure.
 */
static inline struct ext4_ext_path *
get_ext_path(struct inode *inode, ext4_lblk_t lblock,
	     struct ext4_ext_path *path)
{
	path = ext4_find_extent(inode, lblock, path, EXT4_EX_NOCACHE);
	if (IS_ERR(path))
		return path;
	if (path[ext_depth(inode)].p_ext == NULL) {
		ext4_free_ext_path(path);
		return ERR_PTR(-ENODATA);
	}
	return path;
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
		path = get_ext_path(inode, from, path);
		if (IS_ERR(path)) {
			*err = PTR_ERR(path);
			return ret;
		}
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

static void mext_folio_double_unlock(struct folio *folio[2])
{
	folio_unlock(folio[0]);
	folio_put(folio[0]);
	folio_unlock(folio[1]);
	folio_put(folio[1]);
}

/* Force folio buffers uptodate w/o dropping folio's lock */
static int mext_folio_mkuptodate(struct folio *folio, size_t from, size_t to)
{
	struct inode *inode = folio->mapping->host;
	sector_t block;
	struct buffer_head *bh, *head;
	unsigned int blocksize, block_start, block_end;
	int nr = 0;
	bool partial = false;

	BUG_ON(!folio_test_locked(folio));
	BUG_ON(folio_test_writeback(folio));

	if (folio_test_uptodate(folio))
		return 0;

	blocksize = i_blocksize(inode);
	head = folio_buffers(folio);
	if (!head)
		head = create_empty_buffers(folio, blocksize, 0);

	block = folio_pos(folio) >> inode->i_blkbits;
	block_end = 0;
	bh = head;
	do {
		block_start = block_end;
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = true;
			continue;
		}
		if (buffer_uptodate(bh))
			continue;
		if (!buffer_mapped(bh)) {
			int err = ext4_get_block(inode, block, bh, 0);
			if (err)
				return err;
			if (!buffer_mapped(bh)) {
				folio_zero_range(folio, block_start, blocksize);
				set_buffer_uptodate(bh);
				continue;
			}
		}
		lock_buffer(bh);
		if (buffer_uptodate(bh)) {
			unlock_buffer(bh);
			continue;
		}
		ext4_read_bh_nowait(bh, 0, NULL, false);
		nr++;
	} while (block++, (bh = bh->b_this_page) != head);

	/* No io required */
	if (!nr)
		goto out;

	bh = head;
	do {
		if (bh_offset(bh) + blocksize <= from)
			continue;
		if (bh_offset(bh) >= to)
			break;
		wait_on_buffer(bh);
		if (buffer_uptodate(bh))
			continue;
		return -EIO;
	} while ((bh = bh->b_this_page) != head);
out:
	if (!partial)
		folio_mark_uptodate(folio);
	return 0;
}

enum mext_move_type {MEXT_SKIP_EXTENT, MEXT_MOVE_EXTENT, MEXT_COPY_DATA};

/*
 * Start to move extent between the origin inode and the donor inode,
 * hold one folio for each inode and check the candidate moving extent
 * mapping status again.
 */
static int mext_move_begin(struct mext_data *mext, struct folio *folio[2],
			   enum mext_move_type *move_type)
{
	struct inode *orig_inode = mext->orig_inode;
	struct inode *donor_inode = mext->donor_inode;
	unsigned int blkbits = orig_inode->i_blkbits;
	struct ext4_map_blocks donor_map = {0};
	loff_t orig_pos, donor_pos;
	size_t move_len;
	int ret;

	orig_pos = ((loff_t)mext->orig_map.m_lblk) << blkbits;
	donor_pos = ((loff_t)mext->donor_lblk) << blkbits;
	ret = mext_folio_double_lock(orig_inode, donor_inode,
			orig_pos >> PAGE_SHIFT, donor_pos >> PAGE_SHIFT, folio);
	if (ret)
		return ret;

	/*
	 * Check the origin inode's mapping information again under the
	 * folio lock, as we do not hold the i_data_sem at all times, and
	 * it may change during the concurrent write-back operation.
	 */
	if (mext->orig_map.m_seq != READ_ONCE(EXT4_I(orig_inode)->i_es_seq)) {
		ret = -ESTALE;
		goto error;
	}

	/* Adjust the moving length according to the length of shorter folio. */
	move_len = umin(folio_pos(folio[0]) + folio_size(folio[0]) - orig_pos,
			folio_pos(folio[1]) + folio_size(folio[1]) - donor_pos);
	move_len >>= blkbits;
	if (move_len < mext->orig_map.m_len)
		mext->orig_map.m_len = move_len;

	donor_map.m_lblk = mext->donor_lblk;
	donor_map.m_len = mext->orig_map.m_len;
	donor_map.m_flags = 0;
	ret = ext4_map_blocks(NULL, donor_inode, &donor_map, 0);
	if (ret < 0)
		goto error;

	/* Adjust the moving length according to the donor mapping length. */
	mext->orig_map.m_len = donor_map.m_len;

	/* Skip moving if the donor range is a hole or a delalloc extent. */
	if (!(donor_map.m_flags & (EXT4_MAP_MAPPED | EXT4_MAP_UNWRITTEN)))
		*move_type = MEXT_SKIP_EXTENT;
	/* If both mapping ranges are unwritten, no need to copy data. */
	else if ((mext->orig_map.m_flags & EXT4_MAP_UNWRITTEN) &&
		 (donor_map.m_flags & EXT4_MAP_UNWRITTEN))
		*move_type = MEXT_MOVE_EXTENT;
	else
		*move_type = MEXT_COPY_DATA;

	return 0;
error:
	mext_folio_double_unlock(folio);
	return ret;
}

/*
 * Re-create the new moved mapping buffers of the original inode and commit
 * the entire written range.
 */
static int mext_folio_mkwrite(struct inode *inode, struct folio *folio,
			      size_t from, size_t to)
{
	unsigned int blocksize = i_blocksize(inode);
	struct buffer_head *bh, *head;
	size_t block_start, block_end;
	sector_t block;
	int ret;

	head = folio_buffers(folio);
	if (!head)
		head = create_empty_buffers(folio, blocksize, 0);

	block = folio_pos(folio) >> inode->i_blkbits;
	block_end = 0;
	bh = head;
	do {
		block_start = block_end;
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to)
			continue;

		ret = ext4_get_block(inode, block, bh, 0);
		if (ret)
			return ret;
	} while (block++, (bh = bh->b_this_page) != head);

	block_commit_write(folio, from, to);
	return 0;
}

/*
 * Save the data in original inode extent blocks and replace one folio size
 * aligned original inode extent with one or one partial donor inode extent,
 * and then write out the saved data in new original inode blocks. Pass out
 * the replaced block count through m_len. Return 0 on success, and an error
 * code otherwise.
 */
static __used int mext_move_extent(struct mext_data *mext, u64 *m_len)
{
	struct inode *orig_inode = mext->orig_inode;
	struct inode *donor_inode = mext->donor_inode;
	struct ext4_map_blocks *orig_map = &mext->orig_map;
	unsigned int blkbits = orig_inode->i_blkbits;
	struct folio *folio[2] = {NULL, NULL};
	loff_t from, length;
	enum mext_move_type move_type = 0;
	handle_t *handle;
	u64 r_len = 0;
	unsigned int credits;
	int ret, ret2;

	*m_len = 0;
	credits = ext4_chunk_trans_extent(orig_inode, 0) * 2;
	handle = ext4_journal_start(orig_inode, EXT4_HT_MOVE_EXTENTS, credits);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	ret = mext_move_begin(mext, folio, &move_type);
	if (ret)
		goto stop_handle;

	if (move_type == MEXT_SKIP_EXTENT)
		goto unlock;

	/*
	 * Copy the data. First, read the original inode data into the page
	 * cache. Then, release the existing mapping relationships and swap
	 * the extent. Finally, re-establish the new mapping relationships
	 * and dirty the page cache.
	 */
	if (move_type == MEXT_COPY_DATA) {
		from = offset_in_folio(folio[0],
				((loff_t)orig_map->m_lblk) << blkbits);
		length = ((loff_t)orig_map->m_len) << blkbits;

		ret = mext_folio_mkuptodate(folio[0], from, from + length);
		if (ret)
			goto unlock;
	}

	if (!filemap_release_folio(folio[0], 0) ||
	    !filemap_release_folio(folio[1], 0)) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Move extent */
	ext4_double_down_write_data_sem(orig_inode, donor_inode);
	*m_len = ext4_swap_extents(handle, orig_inode, donor_inode,
				   orig_map->m_lblk, mext->donor_lblk,
				   orig_map->m_len, 1, &ret);
	ext4_double_up_write_data_sem(orig_inode, donor_inode);

	/* A short-length swap cannot occur after a successful swap extent. */
	if (WARN_ON_ONCE(!ret && (*m_len != orig_map->m_len)))
		ret = -EIO;

	if (!(*m_len) || (move_type == MEXT_MOVE_EXTENT))
		goto unlock;

	/* Copy data */
	length = (*m_len) << blkbits;
	ret2 = mext_folio_mkwrite(orig_inode, folio[0], from, from + length);
	if (ret2) {
		if (!ret)
			ret = ret2;
		goto repair_branches;
	}
	/*
	 * Even in case of data=writeback it is reasonable to pin
	 * inode to transaction, to prevent unexpected data loss.
	 */
	ret2 = ext4_jbd2_inode_add_write(handle, orig_inode,
			((loff_t)orig_map->m_lblk) << blkbits, length);
	if (!ret)
		ret = ret2;
unlock:
	mext_folio_double_unlock(folio);
stop_handle:
	ext4_journal_stop(handle);
	return ret;

repair_branches:
	ret2 = 0;
	r_len = ext4_swap_extents(handle, donor_inode, orig_inode,
				  mext->donor_lblk, orig_map->m_lblk,
				  *m_len, 0, &ret2);
	if (ret2 || r_len != *m_len) {
		ext4_error_inode_block(orig_inode, (sector_t)(orig_map->m_lblk),
				       EIO, "Unable to copy data block, data will be lost!");
		ret = -EIO;
	}
	*m_len = 0;
	goto unlock;
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
	int from;
	int blocks_per_page = PAGE_SIZE >> orig_inode->i_blkbits;
	struct super_block *sb = orig_inode->i_sb;
	struct buffer_head *bh = NULL;

	/*
	 * It needs twice the amount of ordinary journal buffers because
	 * inode and donor_inode may change each different metadata blocks.
	 */
again:
	*err = 0;
	jblocks = ext4_meta_trans_blocks(orig_inode, block_len_in_page,
					 block_len_in_page) * 2;
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
		if (!filemap_release_folio(folio[0], 0) ||
		    !filemap_release_folio(folio[1], 0)) {
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
	from = offset_in_folio(folio[0],
			       orig_blk_offset << orig_inode->i_blkbits);
	*err = mext_folio_mkuptodate(folio[0], from, from + replaced_size);
	if (*err)
		goto unlock_folios;

	/* At this point all buffers in range are uptodate, old mapping layout
	 * is no longer required, try to drop it now. */
	if (!filemap_release_folio(folio[0], 0) ||
	    !filemap_release_folio(folio[1], 0)) {
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
	bh = folio_buffers(folio[0]);
	if (!bh)
		bh = create_empty_buffers(folio[0],
				1 << orig_inode->i_blkbits, 0);
	for (i = 0; i < from >> orig_inode->i_blkbits; i++)
		bh = bh->b_this_page;
	for (i = 0; i < block_len_in_page; i++) {
		*err = ext4_get_block(orig_inode, orig_blk_offset + i, bh, 0);
		if (*err < 0)
			goto repair_branches;
		bh = bh->b_this_page;
	}

	block_commit_write(folio[0], from, from + replaced_size);

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

/*
 * Check the validity of the basic filesystem environment and the
 * inodes' support status.
 */
static int mext_check_validity(struct inode *orig_inode,
			       struct inode *donor_inode)
{
	struct super_block *sb = orig_inode->i_sb;

	/* origin and donor should be different inodes */
	if (orig_inode == donor_inode) {
		ext4_debug("ext4 move extent: The argument files should not be same inode [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* origin and donor should belone to the same filesystem */
	if (orig_inode->i_sb != donor_inode->i_sb) {
		ext4_debug("ext4 move extent: The argument files should be in same FS [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* Regular file check */
	if (!S_ISREG(orig_inode->i_mode) || !S_ISREG(donor_inode->i_mode)) {
		ext4_debug("ext4 move extent: The argument files should be regular file [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if (ext4_has_feature_bigalloc(sb)) {
		ext4_msg(sb, KERN_ERR,
			 "Online defrag not supported with bigalloc");
		return -EOPNOTSUPP;
	}

	if (IS_DAX(orig_inode)) {
		ext4_msg(sb, KERN_ERR,
			 "Online defrag not supported with DAX");
		return -EOPNOTSUPP;
	}

	/*
	 * TODO: it's not obvious how to swap blocks for inodes with full
	 * journaling enabled.
	 */
	if (ext4_should_journal_data(orig_inode) ||
	    ext4_should_journal_data(donor_inode)) {
		ext4_msg(sb, KERN_ERR,
			 "Online defrag not supported with data journaling");
		return -EOPNOTSUPP;
	}

	if (IS_ENCRYPTED(orig_inode) || IS_ENCRYPTED(donor_inode)) {
		ext4_msg(sb, KERN_ERR,
			 "Online defrag not supported for encrypted files");
		return -EOPNOTSUPP;
	}

	/* Ext4 move extent supports only extent based file */
	if (!(ext4_test_inode_flag(orig_inode, EXT4_INODE_EXTENTS)) ||
	    !(ext4_test_inode_flag(donor_inode, EXT4_INODE_EXTENTS))) {
		ext4_msg(sb, KERN_ERR,
			 "Online defrag not supported for non-extent files");
		return -EOPNOTSUPP;
	}

	if (donor_inode->i_mode & (S_ISUID|S_ISGID)) {
		ext4_debug("ext4 move extent: suid or sgid is set to donor file [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if (IS_IMMUTABLE(donor_inode) || IS_APPEND(donor_inode)) {
		ext4_debug("ext4 move extent: donor should not be immutable or append file [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EPERM;
	}

	/* Ext4 move extent does not support swap files */
	if (IS_SWAPFILE(orig_inode) || IS_SWAPFILE(donor_inode)) {
		ext4_debug("ext4 move extent: The argument files should not be swap files [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -ETXTBSY;
	}

	if (ext4_is_quota_file(orig_inode) || ext4_is_quota_file(donor_inode)) {
		ext4_debug("ext4 move extent: The argument files should not be quota files [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EOPNOTSUPP;
	}

	if ((!orig_inode->i_size) || (!donor_inode->i_size)) {
		ext4_debug("ext4 move extent: File size is 0 byte\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * Check the moving range of ext4_move_extents() whether the files can be
 * exchanged with each other, and adjust the length to fit within the file
 * size. Return 0 on success, or a negative error value on failure.
 */
static int mext_check_adjust_range(struct inode *orig_inode,
				   struct inode *donor_inode, __u64 orig_start,
				   __u64 donor_start, __u64 *len)
{
	__u64 orig_eof, donor_eof;

	/* Start offset should be same */
	if ((orig_start & ~(PAGE_MASK >> orig_inode->i_blkbits)) !=
	    (donor_start & ~(PAGE_MASK >> orig_inode->i_blkbits))) {
		ext4_debug("ext4 move extent: orig and donor's start offsets are not aligned [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if ((orig_start >= EXT_MAX_BLOCKS) ||
	    (donor_start >= EXT_MAX_BLOCKS) ||
	    (*len > EXT_MAX_BLOCKS) ||
	    (donor_start + *len >= EXT_MAX_BLOCKS) ||
	    (orig_start + *len >= EXT_MAX_BLOCKS))  {
		ext4_debug("ext4 move extent: Can't handle over [%u] blocks [ino:orig %lu, donor %lu]\n",
			   EXT_MAX_BLOCKS,
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	orig_eof = EXT4_B_TO_LBLK(orig_inode, i_size_read(orig_inode));
	donor_eof = EXT4_B_TO_LBLK(donor_inode, i_size_read(donor_inode));
	if (orig_eof <= orig_start)
		*len = 0;
	else if (orig_eof < orig_start + *len - 1)
		*len = orig_eof - orig_start;
	if (donor_eof <= donor_start)
		*len = 0;
	else if (donor_eof < donor_start + *len - 1)
		*len = donor_eof - donor_start;
	if (!*len) {
		ext4_debug("ext4 move extent: len should not be 0 [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
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

	/* Protect orig and donor inodes against a truncate */
	lock_two_nondirectories(orig_inode, donor_inode);

	ret = mext_check_validity(orig_inode, donor_inode);
	if (ret)
		goto unlock;

	/* Wait for all existing dio workers */
	inode_dio_wait(orig_inode);
	inode_dio_wait(donor_inode);

	/* Protect extent tree against block allocations via delalloc */
	ext4_double_down_write_data_sem(orig_inode, donor_inode);
	/* Check and adjust the specified move_extent range. */
	ret = mext_check_adjust_range(orig_inode, donor_inode, orig_blk,
				      donor_blk, &len);
	if (ret)
		goto out;
	o_end = o_start + len;

	*moved_len = 0;
	while (o_start < o_end) {
		struct ext4_extent *ex;
		ext4_lblk_t cur_blk, next_blk;
		pgoff_t orig_page_index, donor_page_index;
		int offset_in_page;
		int unwritten, cur_len;

		path = get_ext_path(orig_inode, o_start, path);
		if (IS_ERR(path)) {
			ret = PTR_ERR(path);
			goto out;
		}
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
		*moved_len += move_extent_per_page(o_filp, donor_inode,
				     orig_page_index, donor_page_index,
				     offset_in_page, cur_len,
				     unwritten, &ret);
		ext4_double_down_write_data_sem(orig_inode, donor_inode);
		if (ret < 0)
			break;
		o_start += cur_len;
		d_start += cur_len;
	}

out:
	if (*moved_len) {
		ext4_discard_preallocations(orig_inode);
		ext4_discard_preallocations(donor_inode);
	}

	ext4_free_ext_path(path);
	ext4_double_up_write_data_sem(orig_inode, donor_inode);
unlock:
	unlock_two_nondirectories(orig_inode, donor_inode);

	return ret;
}
