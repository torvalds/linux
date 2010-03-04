/*
 * Copyright (c) 2008,2009 NEC Software Tohoku, Ltd.
 * Written by Takashi Sato <t-sato@yk.jp.nec.com>
 *            Akira Fujita <a-fujita@rs.jp.nec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include "ext4_jbd2.h"
#include "ext4_extents.h"
#include "ext4.h"

/**
 * get_ext_path - Find an extent path for designated logical block number.
 *
 * @inode:	an inode which is searched
 * @lblock:	logical block number to find an extent path
 * @path:	pointer to an extent path pointer (for output)
 *
 * ext4_ext_find_extent wrapper. Return 0 on success, or a negative error value
 * on failure.
 */
static inline int
get_ext_path(struct inode *inode, ext4_lblk_t lblock,
		struct ext4_ext_path **path)
{
	int ret = 0;

	*path = ext4_ext_find_extent(inode, lblock, *path);
	if (IS_ERR(*path)) {
		ret = PTR_ERR(*path);
		*path = NULL;
	} else if ((*path)[ext_depth(inode)].p_ext == NULL)
		ret = -ENODATA;

	return ret;
}

/**
 * copy_extent_status - Copy the extent's initialization status
 *
 * @src:	an extent for getting initialize status
 * @dest:	an extent to be set the status
 */
static void
copy_extent_status(struct ext4_extent *src, struct ext4_extent *dest)
{
	if (ext4_ext_is_uninitialized(src))
		ext4_ext_mark_uninitialized(dest);
	else
		dest->ee_len = cpu_to_le16(ext4_ext_get_actual_len(dest));
}

/**
 * mext_next_extent - Search for the next extent and set it to "extent"
 *
 * @inode:	inode which is searched
 * @path:	this will obtain data for the next extent
 * @extent:	pointer to the next extent we have just gotten
 *
 * Search the next extent in the array of ext4_ext_path structure (@path)
 * and set it to ext4_extent structure (@extent). In addition, the member of
 * @path (->p_ext) also points the next extent. Return 0 on success, 1 if
 * ext4_ext_path structure refers to the last extent, or a negative error
 * value on failure.
 */
static int
mext_next_extent(struct inode *inode, struct ext4_ext_path *path,
		      struct ext4_extent **extent)
{
	struct ext4_extent_header *eh;
	int ppos, leaf_ppos = path->p_depth;

	ppos = leaf_ppos;
	if (EXT_LAST_EXTENT(path[ppos].p_hdr) > path[ppos].p_ext) {
		/* leaf block */
		*extent = ++path[ppos].p_ext;
		path[ppos].p_block = ext_pblock(path[ppos].p_ext);
		return 0;
	}

	while (--ppos >= 0) {
		if (EXT_LAST_INDEX(path[ppos].p_hdr) >
		    path[ppos].p_idx) {
			int cur_ppos = ppos;

			/* index block */
			path[ppos].p_idx++;
			path[ppos].p_block = idx_pblock(path[ppos].p_idx);
			if (path[ppos+1].p_bh)
				brelse(path[ppos+1].p_bh);
			path[ppos+1].p_bh =
				sb_bread(inode->i_sb, path[ppos].p_block);
			if (!path[ppos+1].p_bh)
				return -EIO;
			path[ppos+1].p_hdr =
				ext_block_hdr(path[ppos+1].p_bh);

			/* Halfway index block */
			while (++cur_ppos < leaf_ppos) {
				path[cur_ppos].p_idx =
					EXT_FIRST_INDEX(path[cur_ppos].p_hdr);
				path[cur_ppos].p_block =
					idx_pblock(path[cur_ppos].p_idx);
				if (path[cur_ppos+1].p_bh)
					brelse(path[cur_ppos+1].p_bh);
				path[cur_ppos+1].p_bh = sb_bread(inode->i_sb,
					path[cur_ppos].p_block);
				if (!path[cur_ppos+1].p_bh)
					return -EIO;
				path[cur_ppos+1].p_hdr =
					ext_block_hdr(path[cur_ppos+1].p_bh);
			}

			path[leaf_ppos].p_ext = *extent = NULL;

			eh = path[leaf_ppos].p_hdr;
			if (le16_to_cpu(eh->eh_entries) == 0)
				/* empty leaf is found */
				return -ENODATA;

			/* leaf block */
			path[leaf_ppos].p_ext = *extent =
				EXT_FIRST_EXTENT(path[leaf_ppos].p_hdr);
			path[leaf_ppos].p_block =
					ext_pblock(path[leaf_ppos].p_ext);
			return 0;
		}
	}
	/* We found the last extent */
	return 1;
}

/**
 * mext_check_null_inode - NULL check for two inodes
 *
 * If inode1 or inode2 is NULL, return -EIO. Otherwise, return 0.
 */
static int
mext_check_null_inode(struct inode *inode1, struct inode *inode2,
		const char *function)
{
	int ret = 0;

	if (inode1 == NULL) {
		__ext4_error(inode2->i_sb, function,
			"Both inodes should not be NULL: "
			"inode1 NULL inode2 %lu", inode2->i_ino);
		ret = -EIO;
	} else if (inode2 == NULL) {
		__ext4_error(inode1->i_sb, function,
			"Both inodes should not be NULL: "
			"inode1 %lu inode2 NULL", inode1->i_ino);
		ret = -EIO;
	}
	return ret;
}

/**
 * double_down_write_data_sem - Acquire two inodes' write lock of i_data_sem
 *
 * @orig_inode:		original inode structure
 * @donor_inode:	donor inode structure
 * Acquire write lock of i_data_sem of the two inodes (orig and donor) by
 * i_ino order.
 */
static void
double_down_write_data_sem(struct inode *orig_inode, struct inode *donor_inode)
{
	struct inode *first = orig_inode, *second = donor_inode;

	/*
	 * Use the inode number to provide the stable locking order instead
	 * of its address, because the C language doesn't guarantee you can
	 * compare pointers that don't come from the same array.
	 */
	if (donor_inode->i_ino < orig_inode->i_ino) {
		first = donor_inode;
		second = orig_inode;
	}

	down_write(&EXT4_I(first)->i_data_sem);
	down_write_nested(&EXT4_I(second)->i_data_sem, SINGLE_DEPTH_NESTING);
}

/**
 * double_up_write_data_sem - Release two inodes' write lock of i_data_sem
 *
 * @orig_inode:		original inode structure to be released its lock first
 * @donor_inode:	donor inode structure to be released its lock second
 * Release write lock of i_data_sem of two inodes (orig and donor).
 */
static void
double_up_write_data_sem(struct inode *orig_inode, struct inode *donor_inode)
{
	up_write(&EXT4_I(orig_inode)->i_data_sem);
	up_write(&EXT4_I(donor_inode)->i_data_sem);
}

/**
 * mext_insert_across_blocks - Insert extents across leaf block
 *
 * @handle:		journal handle
 * @orig_inode:		original inode
 * @o_start:		first original extent to be changed
 * @o_end:		last original extent to be changed
 * @start_ext:		first new extent to be inserted
 * @new_ext:		middle of new extent to be inserted
 * @end_ext:		last new extent to be inserted
 *
 * Allocate a new leaf block and insert extents into it. Return 0 on success,
 * or a negative error value on failure.
 */
static int
mext_insert_across_blocks(handle_t *handle, struct inode *orig_inode,
		struct ext4_extent *o_start, struct ext4_extent *o_end,
		struct ext4_extent *start_ext, struct ext4_extent *new_ext,
		struct ext4_extent *end_ext)
{
	struct ext4_ext_path *orig_path = NULL;
	ext4_lblk_t eblock = 0;
	int new_flag = 0;
	int end_flag = 0;
	int err = 0;

	if (start_ext->ee_len && new_ext->ee_len && end_ext->ee_len) {
		if (o_start == o_end) {

			/*       start_ext   new_ext    end_ext
			 * donor |---------|-----------|--------|
			 * orig  |------------------------------|
			 */
			end_flag = 1;
		} else {

			/*       start_ext   new_ext   end_ext
			 * donor |---------|----------|---------|
			 * orig  |---------------|--------------|
			 */
			o_end->ee_block = end_ext->ee_block;
			o_end->ee_len = end_ext->ee_len;
			ext4_ext_store_pblock(o_end, ext_pblock(end_ext));
		}

		o_start->ee_len = start_ext->ee_len;
		eblock = le32_to_cpu(start_ext->ee_block);
		new_flag = 1;

	} else if (start_ext->ee_len && new_ext->ee_len &&
		   !end_ext->ee_len && o_start == o_end) {

		/*	 start_ext	new_ext
		 * donor |--------------|---------------|
		 * orig  |------------------------------|
		 */
		o_start->ee_len = start_ext->ee_len;
		eblock = le32_to_cpu(start_ext->ee_block);
		new_flag = 1;

	} else if (!start_ext->ee_len && new_ext->ee_len &&
		   end_ext->ee_len && o_start == o_end) {

		/*	  new_ext	end_ext
		 * donor |--------------|---------------|
		 * orig  |------------------------------|
		 */
		o_end->ee_block = end_ext->ee_block;
		o_end->ee_len = end_ext->ee_len;
		ext4_ext_store_pblock(o_end, ext_pblock(end_ext));

		/*
		 * Set 0 to the extent block if new_ext was
		 * the first block.
		 */
		if (new_ext->ee_block)
			eblock = le32_to_cpu(new_ext->ee_block);

		new_flag = 1;
	} else {
		ext4_debug("ext4 move extent: Unexpected insert case\n");
		return -EIO;
	}

	if (new_flag) {
		err = get_ext_path(orig_inode, eblock, &orig_path);
		if (err)
			goto out;

		if (ext4_ext_insert_extent(handle, orig_inode,
					orig_path, new_ext, 0))
			goto out;
	}

	if (end_flag) {
		err = get_ext_path(orig_inode,
				le32_to_cpu(end_ext->ee_block) - 1, &orig_path);
		if (err)
			goto out;

		if (ext4_ext_insert_extent(handle, orig_inode,
					   orig_path, end_ext, 0))
			goto out;
	}
out:
	if (orig_path) {
		ext4_ext_drop_refs(orig_path);
		kfree(orig_path);
	}

	return err;

}

/**
 * mext_insert_inside_block - Insert new extent to the extent block
 *
 * @o_start:		first original extent to be moved
 * @o_end:		last original extent to be moved
 * @start_ext:		first new extent to be inserted
 * @new_ext:		middle of new extent to be inserted
 * @end_ext:		last new extent to be inserted
 * @eh:			extent header of target leaf block
 * @range_to_move:	used to decide how to insert extent
 *
 * Insert extents into the leaf block. The extent (@o_start) is overwritten
 * by inserted extents.
 */
static void
mext_insert_inside_block(struct ext4_extent *o_start,
			      struct ext4_extent *o_end,
			      struct ext4_extent *start_ext,
			      struct ext4_extent *new_ext,
			      struct ext4_extent *end_ext,
			      struct ext4_extent_header *eh,
			      int range_to_move)
{
	int i = 0;
	unsigned long len;

	/* Move the existing extents */
	if (range_to_move && o_end < EXT_LAST_EXTENT(eh)) {
		len = (unsigned long)(EXT_LAST_EXTENT(eh) + 1) -
			(unsigned long)(o_end + 1);
		memmove(o_end + 1 + range_to_move, o_end + 1, len);
	}

	/* Insert start entry */
	if (start_ext->ee_len)
		o_start[i++].ee_len = start_ext->ee_len;

	/* Insert new entry */
	if (new_ext->ee_len) {
		o_start[i] = *new_ext;
		ext4_ext_store_pblock(&o_start[i++], ext_pblock(new_ext));
	}

	/* Insert end entry */
	if (end_ext->ee_len)
		o_start[i] = *end_ext;

	/* Increment the total entries counter on the extent block */
	le16_add_cpu(&eh->eh_entries, range_to_move);
}

/**
 * mext_insert_extents - Insert new extent
 *
 * @handle:	journal handle
 * @orig_inode:	original inode
 * @orig_path:	path indicates first extent to be changed
 * @o_start:	first original extent to be changed
 * @o_end:	last original extent to be changed
 * @start_ext:	first new extent to be inserted
 * @new_ext:	middle of new extent to be inserted
 * @end_ext:	last new extent to be inserted
 *
 * Call the function to insert extents. If we cannot add more extents into
 * the leaf block, we call mext_insert_across_blocks() to create a
 * new leaf block. Otherwise call mext_insert_inside_block(). Return 0
 * on success, or a negative error value on failure.
 */
static int
mext_insert_extents(handle_t *handle, struct inode *orig_inode,
			 struct ext4_ext_path *orig_path,
			 struct ext4_extent *o_start,
			 struct ext4_extent *o_end,
			 struct ext4_extent *start_ext,
			 struct ext4_extent *new_ext,
			 struct ext4_extent *end_ext)
{
	struct  ext4_extent_header *eh;
	unsigned long need_slots, slots_range;
	int	range_to_move, depth, ret;

	/*
	 * The extents need to be inserted
	 * start_extent + new_extent + end_extent.
	 */
	need_slots = (start_ext->ee_len ? 1 : 0) + (end_ext->ee_len ? 1 : 0) +
		(new_ext->ee_len ? 1 : 0);

	/* The number of slots between start and end */
	slots_range = ((unsigned long)(o_end + 1) - (unsigned long)o_start + 1)
		/ sizeof(struct ext4_extent);

	/* Range to move the end of extent */
	range_to_move = need_slots - slots_range;
	depth = orig_path->p_depth;
	orig_path += depth;
	eh = orig_path->p_hdr;

	if (depth) {
		/* Register to journal */
		ret = ext4_journal_get_write_access(handle, orig_path->p_bh);
		if (ret)
			return ret;
	}

	/* Expansion */
	if (range_to_move > 0 &&
		(range_to_move > le16_to_cpu(eh->eh_max)
			- le16_to_cpu(eh->eh_entries))) {

		ret = mext_insert_across_blocks(handle, orig_inode, o_start,
					o_end, start_ext, new_ext, end_ext);
		if (ret < 0)
			return ret;
	} else
		mext_insert_inside_block(o_start, o_end, start_ext, new_ext,
						end_ext, eh, range_to_move);

	if (depth) {
		ret = ext4_handle_dirty_metadata(handle, orig_inode,
						 orig_path->p_bh);
		if (ret)
			return ret;
	} else {
		ret = ext4_mark_inode_dirty(handle, orig_inode);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * mext_leaf_block - Move one leaf extent block into the inode.
 *
 * @handle:		journal handle
 * @orig_inode:		original inode
 * @orig_path:		path indicates first extent to be changed
 * @dext:		donor extent
 * @from:		start offset on the target file
 *
 * In order to insert extents into the leaf block, we must divide the extent
 * in the leaf block into three extents. The one is located to be inserted
 * extents, and the others are located around it.
 *
 * Therefore, this function creates structures to save extents of the leaf
 * block, and inserts extents by calling mext_insert_extents() with
 * created extents. Return 0 on success, or a negative error value on failure.
 */
static int
mext_leaf_block(handle_t *handle, struct inode *orig_inode,
		     struct ext4_ext_path *orig_path, struct ext4_extent *dext,
		     ext4_lblk_t *from)
{
	struct ext4_extent *oext, *o_start, *o_end, *prev_ext;
	struct ext4_extent new_ext, start_ext, end_ext;
	ext4_lblk_t new_ext_end;
	ext4_fsblk_t new_phys_end;
	int oext_alen, new_ext_alen, end_ext_alen;
	int depth = ext_depth(orig_inode);
	int ret;

	o_start = o_end = oext = orig_path[depth].p_ext;
	oext_alen = ext4_ext_get_actual_len(oext);
	start_ext.ee_len = end_ext.ee_len = 0;

	new_ext.ee_block = cpu_to_le32(*from);
	ext4_ext_store_pblock(&new_ext, ext_pblock(dext));
	new_ext.ee_len = dext->ee_len;
	new_ext_alen = ext4_ext_get_actual_len(&new_ext);
	new_ext_end = le32_to_cpu(new_ext.ee_block) + new_ext_alen - 1;
	new_phys_end = ext_pblock(&new_ext) + new_ext_alen - 1;

	/*
	 * Case: original extent is first
	 * oext      |--------|
	 * new_ext      |--|
	 * start_ext |--|
	 */
	if (le32_to_cpu(oext->ee_block) < le32_to_cpu(new_ext.ee_block) &&
		le32_to_cpu(new_ext.ee_block) <
		le32_to_cpu(oext->ee_block) + oext_alen) {
		start_ext.ee_len = cpu_to_le16(le32_to_cpu(new_ext.ee_block) -
					       le32_to_cpu(oext->ee_block));
		start_ext.ee_block = oext->ee_block;
		copy_extent_status(oext, &start_ext);
	} else if (oext > EXT_FIRST_EXTENT(orig_path[depth].p_hdr)) {
		prev_ext = oext - 1;
		/*
		 * We can merge new_ext into previous extent,
		 * if these are contiguous and same extent type.
		 */
		if (ext4_can_extents_be_merged(orig_inode, prev_ext,
					       &new_ext)) {
			o_start = prev_ext;
			start_ext.ee_len = cpu_to_le16(
				ext4_ext_get_actual_len(prev_ext) +
				new_ext_alen);
			start_ext.ee_block = oext->ee_block;
			copy_extent_status(prev_ext, &start_ext);
			new_ext.ee_len = 0;
		}
	}

	/*
	 * Case: new_ext_end must be less than oext
	 * oext      |-----------|
	 * new_ext       |-------|
	 */
	if (le32_to_cpu(oext->ee_block) + oext_alen - 1 < new_ext_end) {
		ext4_error(orig_inode->i_sb,
			"new_ext_end(%u) should be less than or equal to "
			"oext->ee_block(%u) + oext_alen(%d) - 1",
			new_ext_end, le32_to_cpu(oext->ee_block),
			oext_alen);
		ret = -EIO;
		goto out;
	}

	/*
	 * Case: new_ext is smaller than original extent
	 * oext    |---------------|
	 * new_ext |-----------|
	 * end_ext             |---|
	 */
	if (le32_to_cpu(oext->ee_block) <= new_ext_end &&
		new_ext_end < le32_to_cpu(oext->ee_block) + oext_alen - 1) {
		end_ext.ee_len =
			cpu_to_le16(le32_to_cpu(oext->ee_block) +
			oext_alen - 1 - new_ext_end);
		copy_extent_status(oext, &end_ext);
		end_ext_alen = ext4_ext_get_actual_len(&end_ext);
		ext4_ext_store_pblock(&end_ext,
			(ext_pblock(o_end) + oext_alen - end_ext_alen));
		end_ext.ee_block =
			cpu_to_le32(le32_to_cpu(o_end->ee_block) +
			oext_alen - end_ext_alen);
	}

	ret = mext_insert_extents(handle, orig_inode, orig_path, o_start,
				o_end, &start_ext, &new_ext, &end_ext);
out:
	return ret;
}

/**
 * mext_calc_swap_extents - Calculate extents for extent swapping.
 *
 * @tmp_dext:		the extent that will belong to the original inode
 * @tmp_oext:		the extent that will belong to the donor inode
 * @orig_off:		block offset of original inode
 * @donor_off:		block offset of donor inode
 * @max_count:		the maximum length of extents
 *
 * Return 0 on success, or a negative error value on failure.
 */
static int
mext_calc_swap_extents(struct ext4_extent *tmp_dext,
			      struct ext4_extent *tmp_oext,
			      ext4_lblk_t orig_off, ext4_lblk_t donor_off,
			      ext4_lblk_t max_count)
{
	ext4_lblk_t diff, orig_diff;
	struct ext4_extent dext_old, oext_old;

	BUG_ON(orig_off != donor_off);

	/* original and donor extents have to cover the same block offset */
	if (orig_off < le32_to_cpu(tmp_oext->ee_block) ||
	    le32_to_cpu(tmp_oext->ee_block) +
			ext4_ext_get_actual_len(tmp_oext) - 1 < orig_off)
		return -ENODATA;

	if (orig_off < le32_to_cpu(tmp_dext->ee_block) ||
	    le32_to_cpu(tmp_dext->ee_block) +
			ext4_ext_get_actual_len(tmp_dext) - 1 < orig_off)
		return -ENODATA;

	dext_old = *tmp_dext;
	oext_old = *tmp_oext;

	/* When tmp_dext is too large, pick up the target range. */
	diff = donor_off - le32_to_cpu(tmp_dext->ee_block);

	ext4_ext_store_pblock(tmp_dext, ext_pblock(tmp_dext) + diff);
	tmp_dext->ee_block =
			cpu_to_le32(le32_to_cpu(tmp_dext->ee_block) + diff);
	tmp_dext->ee_len = cpu_to_le16(le16_to_cpu(tmp_dext->ee_len) - diff);

	if (max_count < ext4_ext_get_actual_len(tmp_dext))
		tmp_dext->ee_len = cpu_to_le16(max_count);

	orig_diff = orig_off - le32_to_cpu(tmp_oext->ee_block);
	ext4_ext_store_pblock(tmp_oext, ext_pblock(tmp_oext) + orig_diff);

	/* Adjust extent length if donor extent is larger than orig */
	if (ext4_ext_get_actual_len(tmp_dext) >
	    ext4_ext_get_actual_len(tmp_oext) - orig_diff)
		tmp_dext->ee_len = cpu_to_le16(le16_to_cpu(tmp_oext->ee_len) -
						orig_diff);

	tmp_oext->ee_len = cpu_to_le16(ext4_ext_get_actual_len(tmp_dext));

	copy_extent_status(&oext_old, tmp_dext);
	copy_extent_status(&dext_old, tmp_oext);

	return 0;
}

/**
 * mext_replace_branches - Replace original extents with new extents
 *
 * @handle:		journal handle
 * @orig_inode:		original inode
 * @donor_inode:	donor inode
 * @from:		block offset of orig_inode
 * @count:		block count to be replaced
 * @err:		pointer to save return value
 *
 * Replace original inode extents and donor inode extents page by page.
 * We implement this replacement in the following three steps:
 * 1. Save the block information of original and donor inodes into
 *    dummy extents.
 * 2. Change the block information of original inode to point at the
 *    donor inode blocks.
 * 3. Change the block information of donor inode to point at the saved
 *    original inode blocks in the dummy extents.
 *
 * Return replaced block count.
 */
static int
mext_replace_branches(handle_t *handle, struct inode *orig_inode,
			   struct inode *donor_inode, ext4_lblk_t from,
			   ext4_lblk_t count, int *err)
{
	struct ext4_ext_path *orig_path = NULL;
	struct ext4_ext_path *donor_path = NULL;
	struct ext4_extent *oext, *dext;
	struct ext4_extent tmp_dext, tmp_oext;
	ext4_lblk_t orig_off = from, donor_off = from;
	int depth;
	int replaced_count = 0;
	int dext_alen;

	/* Protect extent trees against block allocations via delalloc */
	double_down_write_data_sem(orig_inode, donor_inode);

	/* Get the original extent for the block "orig_off" */
	*err = get_ext_path(orig_inode, orig_off, &orig_path);
	if (*err)
		goto out;

	/* Get the donor extent for the head */
	*err = get_ext_path(donor_inode, donor_off, &donor_path);
	if (*err)
		goto out;
	depth = ext_depth(orig_inode);
	oext = orig_path[depth].p_ext;
	tmp_oext = *oext;

	depth = ext_depth(donor_inode);
	dext = donor_path[depth].p_ext;
	tmp_dext = *dext;

	*err = mext_calc_swap_extents(&tmp_dext, &tmp_oext, orig_off,
				      donor_off, count);
	if (*err)
		goto out;

	/* Loop for the donor extents */
	while (1) {
		/* The extent for donor must be found. */
		if (!dext) {
			ext4_error(donor_inode->i_sb,
				   "The extent for donor must be found");
			*err = -EIO;
			goto out;
		} else if (donor_off != le32_to_cpu(tmp_dext.ee_block)) {
			ext4_error(donor_inode->i_sb,
				"Donor offset(%u) and the first block of donor "
				"extent(%u) should be equal",
				donor_off,
				le32_to_cpu(tmp_dext.ee_block));
			*err = -EIO;
			goto out;
		}

		/* Set donor extent to orig extent */
		*err = mext_leaf_block(handle, orig_inode,
					   orig_path, &tmp_dext, &orig_off);
		if (*err)
			goto out;

		/* Set orig extent to donor extent */
		*err = mext_leaf_block(handle, donor_inode,
					   donor_path, &tmp_oext, &donor_off);
		if (*err)
			goto out;

		dext_alen = ext4_ext_get_actual_len(&tmp_dext);
		replaced_count += dext_alen;
		donor_off += dext_alen;
		orig_off += dext_alen;

		/* Already moved the expected blocks */
		if (replaced_count >= count)
			break;

		if (orig_path)
			ext4_ext_drop_refs(orig_path);
		*err = get_ext_path(orig_inode, orig_off, &orig_path);
		if (*err)
			goto out;
		depth = ext_depth(orig_inode);
		oext = orig_path[depth].p_ext;
		tmp_oext = *oext;

		if (donor_path)
			ext4_ext_drop_refs(donor_path);
		*err = get_ext_path(donor_inode, donor_off, &donor_path);
		if (*err)
			goto out;
		depth = ext_depth(donor_inode);
		dext = donor_path[depth].p_ext;
		tmp_dext = *dext;

		*err = mext_calc_swap_extents(&tmp_dext, &tmp_oext, orig_off,
					   donor_off, count - replaced_count);
		if (*err)
			goto out;
	}

out:
	if (orig_path) {
		ext4_ext_drop_refs(orig_path);
		kfree(orig_path);
	}
	if (donor_path) {
		ext4_ext_drop_refs(donor_path);
		kfree(donor_path);
	}

	ext4_ext_invalidate_cache(orig_inode);
	ext4_ext_invalidate_cache(donor_inode);

	double_up_write_data_sem(orig_inode, donor_inode);

	return replaced_count;
}

/**
 * move_extent_per_page - Move extent data per page
 *
 * @o_filp:			file structure of original file
 * @donor_inode:		donor inode
 * @orig_page_offset:		page index on original file
 * @data_offset_in_page:	block index where data swapping starts
 * @block_len_in_page:		the number of blocks to be swapped
 * @uninit:			orig extent is uninitialized or not
 * @err:			pointer to save return value
 *
 * Save the data in original inode blocks and replace original inode extents
 * with donor inode extents by calling mext_replace_branches().
 * Finally, write out the saved data in new original inode blocks. Return
 * replaced block count.
 */
static int
move_extent_per_page(struct file *o_filp, struct inode *donor_inode,
		  pgoff_t orig_page_offset, int data_offset_in_page,
		  int block_len_in_page, int uninit, int *err)
{
	struct inode *orig_inode = o_filp->f_dentry->d_inode;
	struct address_space *mapping = orig_inode->i_mapping;
	struct buffer_head *bh;
	struct page *page = NULL;
	const struct address_space_operations *a_ops = mapping->a_ops;
	handle_t *handle;
	ext4_lblk_t orig_blk_offset;
	long long offs = orig_page_offset << PAGE_CACHE_SHIFT;
	unsigned long blocksize = orig_inode->i_sb->s_blocksize;
	unsigned int w_flags = 0;
	unsigned int tmp_data_size, data_size, replaced_size;
	void *fsdata;
	int i, jblocks;
	int err2 = 0;
	int replaced_count = 0;
	int blocks_per_page = PAGE_CACHE_SIZE >> orig_inode->i_blkbits;

	/*
	 * It needs twice the amount of ordinary journal buffers because
	 * inode and donor_inode may change each different metadata blocks.
	 */
	jblocks = ext4_writepage_trans_blocks(orig_inode) * 2;
	handle = ext4_journal_start(orig_inode, jblocks);
	if (IS_ERR(handle)) {
		*err = PTR_ERR(handle);
		return 0;
	}

	if (segment_eq(get_fs(), KERNEL_DS))
		w_flags |= AOP_FLAG_UNINTERRUPTIBLE;

	orig_blk_offset = orig_page_offset * blocks_per_page +
		data_offset_in_page;

	/*
	 * If orig extent is uninitialized one,
	 * it's not necessary force the page into memory
	 * and then force it to be written out again.
	 * Just swap data blocks between orig and donor.
	 */
	if (uninit) {
		replaced_count = mext_replace_branches(handle, orig_inode,
						donor_inode, orig_blk_offset,
						block_len_in_page, err);
		goto out2;
	}

	offs = (long long)orig_blk_offset << orig_inode->i_blkbits;

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

	*err = a_ops->write_begin(o_filp, mapping, offs, data_size, w_flags,
				 &page, &fsdata);
	if (unlikely(*err < 0))
		goto out;

	if (!PageUptodate(page)) {
		mapping->a_ops->readpage(o_filp, page);
		lock_page(page);
	}

	/*
	 * try_to_release_page() doesn't call releasepage in writeback mode.
	 * We should care about the order of writing to the same file
	 * by multiple move extent processes.
	 * It needs to call wait_on_page_writeback() to wait for the
	 * writeback of the page.
	 */
	if (PageWriteback(page))
		wait_on_page_writeback(page);

	/* Release old bh and drop refs */
	try_to_release_page(page, 0);

	replaced_count = mext_replace_branches(handle, orig_inode, donor_inode,
					orig_blk_offset, block_len_in_page,
					&err2);
	if (err2) {
		if (replaced_count) {
			block_len_in_page = replaced_count;
			replaced_size =
				block_len_in_page << orig_inode->i_blkbits;
		} else
			goto out;
	}

	if (!page_has_buffers(page))
		create_empty_buffers(page, 1 << orig_inode->i_blkbits, 0);

	bh = page_buffers(page);
	for (i = 0; i < data_offset_in_page; i++)
		bh = bh->b_this_page;

	for (i = 0; i < block_len_in_page; i++) {
		*err = ext4_get_block(orig_inode,
				(sector_t)(orig_blk_offset + i), bh, 0);
		if (*err < 0)
			goto out;

		if (bh->b_this_page != NULL)
			bh = bh->b_this_page;
	}

	*err = a_ops->write_end(o_filp, mapping, offs, data_size, replaced_size,
			       page, fsdata);
	page = NULL;

out:
	if (unlikely(page)) {
		if (PageLocked(page))
			unlock_page(page);
		page_cache_release(page);
		ext4_journal_stop(handle);
	}
out2:
	ext4_journal_stop(handle);

	if (err2)
		*err = err2;

	return replaced_count;
}

/**
 * mext_check_argumants - Check whether move extent can be done
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
	ext4_lblk_t orig_blocks, donor_blocks;
	unsigned int blkbits = orig_inode->i_blkbits;
	unsigned int blocksize = 1 << blkbits;

	if (donor_inode->i_mode & (S_ISUID|S_ISGID)) {
		ext4_debug("ext4 move extent: suid or sgid is set"
			   " to donor file [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* Ext4 move extent does not support swapfile */
	if (IS_SWAPFILE(orig_inode) || IS_SWAPFILE(donor_inode)) {
		ext4_debug("ext4 move extent: The argument files should "
			"not be swapfile [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* Files should be in the same ext4 FS */
	if (orig_inode->i_sb != donor_inode->i_sb) {
		ext4_debug("ext4 move extent: The argument files "
			"should be in same FS [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* Ext4 move extent supports only extent based file */
	if (!(EXT4_I(orig_inode)->i_flags & EXT4_EXTENTS_FL)) {
		ext4_debug("ext4 move extent: orig file is not extents "
			"based file [ino:orig %lu]\n", orig_inode->i_ino);
		return -EOPNOTSUPP;
	} else if (!(EXT4_I(donor_inode)->i_flags & EXT4_EXTENTS_FL)) {
		ext4_debug("ext4 move extent: donor file is not extents "
			"based file [ino:donor %lu]\n", donor_inode->i_ino);
		return -EOPNOTSUPP;
	}

	if ((!orig_inode->i_size) || (!donor_inode->i_size)) {
		ext4_debug("ext4 move extent: File size is 0 byte\n");
		return -EINVAL;
	}

	/* Start offset should be same */
	if (orig_start != donor_start) {
		ext4_debug("ext4 move extent: orig and donor's start "
			"offset are not same [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if ((orig_start > EXT_MAX_BLOCK) ||
	    (donor_start > EXT_MAX_BLOCK) ||
	    (*len > EXT_MAX_BLOCK) ||
	    (orig_start + *len > EXT_MAX_BLOCK))  {
		ext4_debug("ext4 move extent: Can't handle over [%u] blocks "
			"[ino:orig %lu, donor %lu]\n", EXT_MAX_BLOCK,
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if (orig_inode->i_size > donor_inode->i_size) {
		donor_blocks = (donor_inode->i_size + blocksize - 1) >> blkbits;
		/* TODO: eliminate this artificial restriction */
		if (orig_start >= donor_blocks) {
			ext4_debug("ext4 move extent: orig start offset "
			"[%llu] should be less than donor file blocks "
			"[%u] [ino:orig %lu, donor %lu]\n",
			orig_start, donor_blocks,
			orig_inode->i_ino, donor_inode->i_ino);
			return -EINVAL;
		}

		/* TODO: eliminate this artificial restriction */
		if (orig_start + *len > donor_blocks) {
			ext4_debug("ext4 move extent: End offset [%llu] should "
				"be less than donor file blocks [%u]."
				"So adjust length from %llu to %llu "
				"[ino:orig %lu, donor %lu]\n",
				orig_start + *len, donor_blocks,
				*len, donor_blocks - orig_start,
				orig_inode->i_ino, donor_inode->i_ino);
			*len = donor_blocks - orig_start;
		}
	} else {
		orig_blocks = (orig_inode->i_size + blocksize - 1) >> blkbits;
		if (orig_start >= orig_blocks) {
			ext4_debug("ext4 move extent: start offset [%llu] "
				"should be less than original file blocks "
				"[%u] [ino:orig %lu, donor %lu]\n",
				 orig_start, orig_blocks,
				orig_inode->i_ino, donor_inode->i_ino);
			return -EINVAL;
		}

		if (orig_start + *len > orig_blocks) {
			ext4_debug("ext4 move extent: Adjust length "
				"from %llu to %llu. Because it should be "
				"less than original file blocks "
				"[ino:orig %lu, donor %lu]\n",
				*len, orig_blocks - orig_start,
				orig_inode->i_ino, donor_inode->i_ino);
			*len = orig_blocks - orig_start;
		}
	}

	if (!*len) {
		ext4_debug("ext4 move extent: len should not be 0 "
			"[ino:orig %lu, donor %lu]\n", orig_inode->i_ino,
			donor_inode->i_ino);
		return -EINVAL;
	}

	return 0;
}

/**
 * mext_inode_double_lock - Lock i_mutex on both @inode1 and @inode2
 *
 * @inode1:	the inode structure
 * @inode2:	the inode structure
 *
 * Lock two inodes' i_mutex by i_ino order.
 * If inode1 or inode2 is NULL, return -EIO. Otherwise, return 0.
 */
static int
mext_inode_double_lock(struct inode *inode1, struct inode *inode2)
{
	int ret = 0;

	BUG_ON(inode1 == NULL && inode2 == NULL);

	ret = mext_check_null_inode(inode1, inode2, __func__);
	if (ret < 0)
		goto out;

	if (inode1 == inode2) {
		mutex_lock(&inode1->i_mutex);
		goto out;
	}

	if (inode1->i_ino < inode2->i_ino) {
		mutex_lock_nested(&inode1->i_mutex, I_MUTEX_PARENT);
		mutex_lock_nested(&inode2->i_mutex, I_MUTEX_CHILD);
	} else {
		mutex_lock_nested(&inode2->i_mutex, I_MUTEX_PARENT);
		mutex_lock_nested(&inode1->i_mutex, I_MUTEX_CHILD);
	}

out:
	return ret;
}

/**
 * mext_inode_double_unlock - Release i_mutex on both @inode1 and @inode2
 *
 * @inode1:     the inode that is released first
 * @inode2:     the inode that is released second
 *
 * If inode1 or inode2 is NULL, return -EIO. Otherwise, return 0.
 */

static int
mext_inode_double_unlock(struct inode *inode1, struct inode *inode2)
{
	int ret = 0;

	BUG_ON(inode1 == NULL && inode2 == NULL);

	ret = mext_check_null_inode(inode1, inode2, __func__);
	if (ret < 0)
		goto out;

	if (inode1)
		mutex_unlock(&inode1->i_mutex);

	if (inode2 && inode2 != inode1)
		mutex_unlock(&inode2->i_mutex);

out:
	return ret;
}

/**
 * ext4_move_extents - Exchange the specified range of a file
 *
 * @o_filp:		file structure of the original file
 * @d_filp:		file structure of the donor file
 * @orig_start:		start offset in block for orig
 * @donor_start:	start offset in block for donor
 * @len:		the number of blocks to be moved
 * @moved_len:		moved block length
 *
 * This function returns 0 and moved block length is set in moved_len
 * if succeed, otherwise returns error value.
 *
 * Note: ext4_move_extents() proceeds the following order.
 * 1:ext4_move_extents() calculates the last block number of moving extent
 *   function by the start block number (orig_start) and the number of blocks
 *   to be moved (len) specified as arguments.
 *   If the {orig, donor}_start points a hole, the extent's start offset
 *   pointed by ext_cur (current extent), holecheck_path, orig_path are set
 *   after hole behind.
 * 2:Continue step 3 to step 5, until the holecheck_path points to last_extent
 *   or the ext_cur exceeds the block_end which is last logical block number.
 * 3:To get the length of continues area, call mext_next_extent()
 *   specified with the ext_cur (initial value is holecheck_path) re-cursive,
 *   until find un-continuous extent, the start logical block number exceeds
 *   the block_end or the extent points to the last extent.
 * 4:Exchange the original inode data with donor inode data
 *   from orig_page_offset to seq_end_page.
 *   The start indexes of data are specified as arguments.
 *   That of the original inode is orig_page_offset,
 *   and the donor inode is also orig_page_offset
 *   (To easily handle blocksize != pagesize case, the offset for the
 *   donor inode is block unit).
 * 5:Update holecheck_path and orig_path to points a next proceeding extent,
 *   then returns to step 2.
 * 6:Release holecheck_path, orig_path and set the len to moved_len
 *   which shows the number of moved blocks.
 *   The moved_len is useful for the command to calculate the file offset
 *   for starting next move extent ioctl.
 * 7:Return 0 on success, or a negative error value on failure.
 */
int
ext4_move_extents(struct file *o_filp, struct file *d_filp,
		 __u64 orig_start, __u64 donor_start, __u64 len,
		 __u64 *moved_len)
{
	struct inode *orig_inode = o_filp->f_dentry->d_inode;
	struct inode *donor_inode = d_filp->f_dentry->d_inode;
	struct ext4_ext_path *orig_path = NULL, *holecheck_path = NULL;
	struct ext4_extent *ext_prev, *ext_cur, *ext_dummy;
	ext4_lblk_t block_start = orig_start;
	ext4_lblk_t block_end, seq_start, add_blocks, file_end, seq_blocks = 0;
	ext4_lblk_t rest_blocks;
	pgoff_t orig_page_offset = 0, seq_end_page;
	int ret1, ret2, depth, last_extent = 0;
	int blocks_per_page = PAGE_CACHE_SIZE >> orig_inode->i_blkbits;
	int data_offset_in_page;
	int block_len_in_page;
	int uninit;

	/* orig and donor should be different file */
	if (orig_inode->i_ino == donor_inode->i_ino) {
		ext4_debug("ext4 move extent: The argument files should not "
			"be same file [ino:orig %lu, donor %lu]\n",
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

	/* Protect orig and donor inodes against a truncate */
	ret1 = mext_inode_double_lock(orig_inode, donor_inode);
	if (ret1 < 0)
		return ret1;

	/* Protect extent tree against block allocations via delalloc */
	double_down_write_data_sem(orig_inode, donor_inode);
	/* Check the filesystem environment whether move_extent can be done */
	ret1 = mext_check_arguments(orig_inode, donor_inode, orig_start,
				    donor_start, &len);
	if (ret1)
		goto out;

	file_end = (i_size_read(orig_inode) - 1) >> orig_inode->i_blkbits;
	block_end = block_start + len - 1;
	if (file_end < block_end)
		len -= block_end - file_end;

	ret1 = get_ext_path(orig_inode, block_start, &orig_path);
	if (ret1)
		goto out;

	/* Get path structure to check the hole */
	ret1 = get_ext_path(orig_inode, block_start, &holecheck_path);
	if (ret1)
		goto out;

	depth = ext_depth(orig_inode);
	ext_cur = holecheck_path[depth].p_ext;

	/*
	 * Get proper starting location of block replacement if block_start was
	 * within the hole.
	 */
	if (le32_to_cpu(ext_cur->ee_block) +
		ext4_ext_get_actual_len(ext_cur) - 1 < block_start) {
		/*
		 * The hole exists between extents or the tail of
		 * original file.
		 */
		last_extent = mext_next_extent(orig_inode,
					holecheck_path, &ext_cur);
		if (last_extent < 0) {
			ret1 = last_extent;
			goto out;
		}
		last_extent = mext_next_extent(orig_inode, orig_path,
							&ext_dummy);
		if (last_extent < 0) {
			ret1 = last_extent;
			goto out;
		}
		seq_start = le32_to_cpu(ext_cur->ee_block);
	} else if (le32_to_cpu(ext_cur->ee_block) > block_start)
		/* The hole exists at the beginning of original file. */
		seq_start = le32_to_cpu(ext_cur->ee_block);
	else
		seq_start = block_start;

	/* No blocks within the specified range. */
	if (le32_to_cpu(ext_cur->ee_block) > block_end) {
		ext4_debug("ext4 move extent: The specified range of file "
							"may be the hole\n");
		ret1 = -EINVAL;
		goto out;
	}

	/* Adjust start blocks */
	add_blocks = min(le32_to_cpu(ext_cur->ee_block) +
			 ext4_ext_get_actual_len(ext_cur), block_end + 1) -
		     max(le32_to_cpu(ext_cur->ee_block), block_start);

	while (!last_extent && le32_to_cpu(ext_cur->ee_block) <= block_end) {
		seq_blocks += add_blocks;

		/* Adjust tail blocks */
		if (seq_start + seq_blocks - 1 > block_end)
			seq_blocks = block_end - seq_start + 1;

		ext_prev = ext_cur;
		last_extent = mext_next_extent(orig_inode, holecheck_path,
						&ext_cur);
		if (last_extent < 0) {
			ret1 = last_extent;
			break;
		}
		add_blocks = ext4_ext_get_actual_len(ext_cur);

		/*
		 * Extend the length of contiguous block (seq_blocks)
		 * if extents are contiguous.
		 */
		if (ext4_can_extents_be_merged(orig_inode,
					       ext_prev, ext_cur) &&
		    block_end >= le32_to_cpu(ext_cur->ee_block) &&
		    !last_extent)
			continue;

		/* Is original extent is uninitialized */
		uninit = ext4_ext_is_uninitialized(ext_prev);

		data_offset_in_page = seq_start % blocks_per_page;

		/*
		 * Calculate data blocks count that should be swapped
		 * at the first page.
		 */
		if (data_offset_in_page + seq_blocks > blocks_per_page) {
			/* Swapped blocks are across pages */
			block_len_in_page =
					blocks_per_page - data_offset_in_page;
		} else {
			/* Swapped blocks are in a page */
			block_len_in_page = seq_blocks;
		}

		orig_page_offset = seq_start >>
				(PAGE_CACHE_SHIFT - orig_inode->i_blkbits);
		seq_end_page = (seq_start + seq_blocks - 1) >>
				(PAGE_CACHE_SHIFT - orig_inode->i_blkbits);
		seq_start = le32_to_cpu(ext_cur->ee_block);
		rest_blocks = seq_blocks;

		/*
		 * Up semaphore to avoid following problems:
		 * a. transaction deadlock among ext4_journal_start,
		 *    ->write_begin via pagefault, and jbd2_journal_commit
		 * b. racing with ->readpage, ->write_begin, and ext4_get_block
		 *    in move_extent_per_page
		 */
		double_up_write_data_sem(orig_inode, donor_inode);

		while (orig_page_offset <= seq_end_page) {

			/* Swap original branches with new branches */
			block_len_in_page = move_extent_per_page(
						o_filp, donor_inode,
						orig_page_offset,
						data_offset_in_page,
						block_len_in_page, uninit,
						&ret1);

			/* Count how many blocks we have exchanged */
			*moved_len += block_len_in_page;
			if (ret1 < 0)
				break;
			if (*moved_len > len) {
				ext4_error(orig_inode->i_sb,
					"We replaced blocks too much! "
					"sum of replaced: %llu requested: %llu",
					*moved_len, len);
				ret1 = -EIO;
				break;
			}

			orig_page_offset++;
			data_offset_in_page = 0;
			rest_blocks -= block_len_in_page;
			if (rest_blocks > blocks_per_page)
				block_len_in_page = blocks_per_page;
			else
				block_len_in_page = rest_blocks;
		}

		double_down_write_data_sem(orig_inode, donor_inode);
		if (ret1 < 0)
			break;

		/* Decrease buffer counter */
		if (holecheck_path)
			ext4_ext_drop_refs(holecheck_path);
		ret1 = get_ext_path(orig_inode, seq_start, &holecheck_path);
		if (ret1)
			break;
		depth = holecheck_path->p_depth;

		/* Decrease buffer counter */
		if (orig_path)
			ext4_ext_drop_refs(orig_path);
		ret1 = get_ext_path(orig_inode, seq_start, &orig_path);
		if (ret1)
			break;

		ext_cur = holecheck_path[depth].p_ext;
		add_blocks = ext4_ext_get_actual_len(ext_cur);
		seq_blocks = 0;

	}
out:
	if (*moved_len) {
		ext4_discard_preallocations(orig_inode);
		ext4_discard_preallocations(donor_inode);
	}

	if (orig_path) {
		ext4_ext_drop_refs(orig_path);
		kfree(orig_path);
	}
	if (holecheck_path) {
		ext4_ext_drop_refs(holecheck_path);
		kfree(holecheck_path);
	}
	double_up_write_data_sem(orig_inode, donor_inode);
	ret2 = mext_inode_double_unlock(orig_inode, donor_inode);

	if (ret1)
		return ret1;
	else if (ret2)
		return ret2;

	return 0;
}
