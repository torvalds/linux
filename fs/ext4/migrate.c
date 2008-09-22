/*
 * Copyright IBM Corporation, 2007
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <linux/module.h>
#include "ext4_jbd2.h"
#include "ext4_extents.h"

/*
 * The contiguous blocks details which can be
 * represented by a single extent
 */
struct list_blocks_struct {
	ext4_lblk_t first_block, last_block;
	ext4_fsblk_t first_pblock, last_pblock;
};

static int finish_range(handle_t *handle, struct inode *inode,
				struct list_blocks_struct *lb)

{
	int retval = 0, needed;
	struct ext4_extent newext;
	struct ext4_ext_path *path;
	if (lb->first_pblock == 0)
		return 0;

	/* Add the extent to temp inode*/
	newext.ee_block = cpu_to_le32(lb->first_block);
	newext.ee_len   = cpu_to_le16(lb->last_block - lb->first_block + 1);
	ext4_ext_store_pblock(&newext, lb->first_pblock);
	path = ext4_ext_find_extent(inode, lb->first_block, NULL);

	if (IS_ERR(path)) {
		retval = PTR_ERR(path);
		path = NULL;
		goto err_out;
	}

	/*
	 * Calculate the credit needed to inserting this extent
	 * Since we are doing this in loop we may accumalate extra
	 * credit. But below we try to not accumalate too much
	 * of them by restarting the journal.
	 */
	needed = ext4_ext_calc_credits_for_single_extent(inode,
		    lb->last_block - lb->first_block + 1, path);

	/*
	 * Make sure the credit we accumalated is not really high
	 */
	if (needed && handle->h_buffer_credits >= EXT4_RESERVE_TRANS_BLOCKS) {
		retval = ext4_journal_restart(handle, needed);
		if (retval)
			goto err_out;
	} else if (needed) {
		retval = ext4_journal_extend(handle, needed);
		if (retval) {
			/*
			 * IF not able to extend the journal restart the journal
			 */
			retval = ext4_journal_restart(handle, needed);
			if (retval)
				goto err_out;
		}
	}
	retval = ext4_ext_insert_extent(handle, inode, path, &newext);
err_out:
	if (path) {
		ext4_ext_drop_refs(path);
		kfree(path);
	}
	lb->first_pblock = 0;
	return retval;
}

static int update_extent_range(handle_t *handle, struct inode *inode,
				ext4_fsblk_t pblock, ext4_lblk_t blk_num,
				struct list_blocks_struct *lb)
{
	int retval;
	/*
	 * See if we can add on to the existing range (if it exists)
	 */
	if (lb->first_pblock &&
		(lb->last_pblock+1 == pblock) &&
		(lb->last_block+1 == blk_num)) {
		lb->last_pblock = pblock;
		lb->last_block = blk_num;
		return 0;
	}
	/*
	 * Start a new range.
	 */
	retval = finish_range(handle, inode, lb);
	lb->first_pblock = lb->last_pblock = pblock;
	lb->first_block = lb->last_block = blk_num;

	return retval;
}

static int update_ind_extent_range(handle_t *handle, struct inode *inode,
				   ext4_fsblk_t pblock, ext4_lblk_t *blk_nump,
				   struct list_blocks_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	ext4_lblk_t blk_count = *blk_nump;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	if (!pblock) {
		/* Only update the file block number */
		*blk_nump += max_entries;
		return 0;
	}

	bh = sb_bread(inode->i_sb, pblock);
	if (!bh)
		return -EIO;

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++, blk_count++) {
		if (i_data[i]) {
			retval = update_extent_range(handle, inode,
						le32_to_cpu(i_data[i]),
						blk_count, lb);
			if (retval)
				break;
		}
	}

	/* Update the file block number */
	*blk_nump = blk_count;
	put_bh(bh);
	return retval;

}

static int update_dind_extent_range(handle_t *handle, struct inode *inode,
				    ext4_fsblk_t pblock, ext4_lblk_t *blk_nump,
				    struct list_blocks_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	ext4_lblk_t blk_count = *blk_nump;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	if (!pblock) {
		/* Only update the file block number */
		*blk_nump += max_entries * max_entries;
		return 0;
	}
	bh = sb_bread(inode->i_sb, pblock);
	if (!bh)
		return -EIO;

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (i_data[i]) {
			retval = update_ind_extent_range(handle, inode,
						le32_to_cpu(i_data[i]),
						&blk_count, lb);
			if (retval)
				break;
		} else {
			/* Only update the file block number */
			blk_count += max_entries;
		}
	}

	/* Update the file block number */
	*blk_nump = blk_count;
	put_bh(bh);
	return retval;

}

static int update_tind_extent_range(handle_t *handle, struct inode *inode,
				     ext4_fsblk_t pblock, ext4_lblk_t *blk_nump,
				     struct list_blocks_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	ext4_lblk_t blk_count = *blk_nump;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	if (!pblock) {
		/* Only update the file block number */
		*blk_nump += max_entries * max_entries * max_entries;
		return 0;
	}
	bh = sb_bread(inode->i_sb, pblock);
	if (!bh)
		return -EIO;

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (i_data[i]) {
			retval = update_dind_extent_range(handle, inode,
						le32_to_cpu(i_data[i]),
						&blk_count, lb);
			if (retval)
				break;
		} else
			/* Only update the file block number */
			blk_count += max_entries * max_entries;
	}
	/* Update the file block number */
	*blk_nump = blk_count;
	put_bh(bh);
	return retval;

}

static int extend_credit_for_blkdel(handle_t *handle, struct inode *inode)
{
	int retval = 0, needed;

	if (handle->h_buffer_credits > EXT4_RESERVE_TRANS_BLOCKS)
		return 0;
	/*
	 * We are freeing a blocks. During this we touch
	 * superblock, group descriptor and block bitmap.
	 * So allocate a credit of 3. We may update
	 * quota (user and group).
	 */
	needed = 3 + 2*EXT4_QUOTA_TRANS_BLOCKS(inode->i_sb);

	if (ext4_journal_extend(handle, needed) != 0)
		retval = ext4_journal_restart(handle, needed);

	return retval;
}

static int free_dind_blocks(handle_t *handle,
				struct inode *inode, __le32 i_data)
{
	int i;
	__le32 *tmp_idata;
	struct buffer_head *bh;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	bh = sb_bread(inode->i_sb, le32_to_cpu(i_data));
	if (!bh)
		return -EIO;

	tmp_idata = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (tmp_idata[i]) {
			extend_credit_for_blkdel(handle, inode);
			ext4_free_blocks(handle, inode,
					le32_to_cpu(tmp_idata[i]), 1, 1);
		}
	}
	put_bh(bh);
	extend_credit_for_blkdel(handle, inode);
	ext4_free_blocks(handle, inode, le32_to_cpu(i_data), 1, 1);
	return 0;
}

static int free_tind_blocks(handle_t *handle,
				struct inode *inode, __le32 i_data)
{
	int i, retval = 0;
	__le32 *tmp_idata;
	struct buffer_head *bh;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	bh = sb_bread(inode->i_sb, le32_to_cpu(i_data));
	if (!bh)
		return -EIO;

	tmp_idata = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (tmp_idata[i]) {
			retval = free_dind_blocks(handle,
					inode, tmp_idata[i]);
			if (retval) {
				put_bh(bh);
				return retval;
			}
		}
	}
	put_bh(bh);
	extend_credit_for_blkdel(handle, inode);
	ext4_free_blocks(handle, inode, le32_to_cpu(i_data), 1, 1);
	return 0;
}

static int free_ind_block(handle_t *handle, struct inode *inode, __le32 *i_data)
{
	int retval;

	/* ei->i_data[EXT4_IND_BLOCK] */
	if (i_data[0]) {
		extend_credit_for_blkdel(handle, inode);
		ext4_free_blocks(handle, inode,
				le32_to_cpu(i_data[0]), 1, 1);
	}

	/* ei->i_data[EXT4_DIND_BLOCK] */
	if (i_data[1]) {
		retval = free_dind_blocks(handle, inode, i_data[1]);
		if (retval)
			return retval;
	}

	/* ei->i_data[EXT4_TIND_BLOCK] */
	if (i_data[2]) {
		retval = free_tind_blocks(handle, inode, i_data[2]);
		if (retval)
			return retval;
	}
	return 0;
}

static int ext4_ext_swap_inode_data(handle_t *handle, struct inode *inode,
						struct inode *tmp_inode)
{
	int retval;
	__le32	i_data[3];
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_inode_info *tmp_ei = EXT4_I(tmp_inode);

	/*
	 * One credit accounted for writing the
	 * i_data field of the original inode
	 */
	retval = ext4_journal_extend(handle, 1);
	if (retval) {
		retval = ext4_journal_restart(handle, 1);
		if (retval)
			goto err_out;
	}

	i_data[0] = ei->i_data[EXT4_IND_BLOCK];
	i_data[1] = ei->i_data[EXT4_DIND_BLOCK];
	i_data[2] = ei->i_data[EXT4_TIND_BLOCK];

	down_write(&EXT4_I(inode)->i_data_sem);
	/*
	 * if EXT4_EXT_MIGRATE is cleared a block allocation
	 * happened after we started the migrate. We need to
	 * fail the migrate
	 */
	if (!(EXT4_I(inode)->i_flags & EXT4_EXT_MIGRATE)) {
		retval = -EAGAIN;
		up_write(&EXT4_I(inode)->i_data_sem);
		goto err_out;
	} else
		EXT4_I(inode)->i_flags = EXT4_I(inode)->i_flags &
							~EXT4_EXT_MIGRATE;
	/*
	 * We have the extent map build with the tmp inode.
	 * Now copy the i_data across
	 */
	ei->i_flags |= EXT4_EXTENTS_FL;
	memcpy(ei->i_data, tmp_ei->i_data, sizeof(ei->i_data));

	/*
	 * Update i_blocks with the new blocks that got
	 * allocated while adding extents for extent index
	 * blocks.
	 *
	 * While converting to extents we need not
	 * update the orignal inode i_blocks for extent blocks
	 * via quota APIs. The quota update happened via tmp_inode already.
	 */
	spin_lock(&inode->i_lock);
	inode->i_blocks += tmp_inode->i_blocks;
	spin_unlock(&inode->i_lock);
	up_write(&EXT4_I(inode)->i_data_sem);

	/*
	 * We mark the inode dirty after, because we decrement the
	 * i_blocks when freeing the indirect meta-data blocks
	 */
	retval = free_ind_block(handle, inode, i_data);
	ext4_mark_inode_dirty(handle, inode);

err_out:
	return retval;
}

static int free_ext_idx(handle_t *handle, struct inode *inode,
					struct ext4_extent_idx *ix)
{
	int i, retval = 0;
	ext4_fsblk_t block;
	struct buffer_head *bh;
	struct ext4_extent_header *eh;

	block = idx_pblock(ix);
	bh = sb_bread(inode->i_sb, block);
	if (!bh)
		return -EIO;

	eh = (struct ext4_extent_header *)bh->b_data;
	if (eh->eh_depth != 0) {
		ix = EXT_FIRST_INDEX(eh);
		for (i = 0; i < le16_to_cpu(eh->eh_entries); i++, ix++) {
			retval = free_ext_idx(handle, inode, ix);
			if (retval)
				break;
		}
	}
	put_bh(bh);
	extend_credit_for_blkdel(handle, inode);
	ext4_free_blocks(handle, inode, block, 1, 1);
	return retval;
}

/*
 * Free the extent meta data blocks only
 */
static int free_ext_block(handle_t *handle, struct inode *inode)
{
	int i, retval = 0;
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_extent_header *eh = (struct ext4_extent_header *)ei->i_data;
	struct ext4_extent_idx *ix;
	if (eh->eh_depth == 0)
		/*
		 * No extra blocks allocated for extent meta data
		 */
		return 0;
	ix = EXT_FIRST_INDEX(eh);
	for (i = 0; i < le16_to_cpu(eh->eh_entries); i++, ix++) {
		retval = free_ext_idx(handle, inode, ix);
		if (retval)
			return retval;
	}
	return retval;

}

int ext4_ext_migrate(struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg)
{
	handle_t *handle;
	int retval = 0, i;
	__le32 *i_data;
	ext4_lblk_t blk_count = 0;
	struct ext4_inode_info *ei;
	struct inode *tmp_inode = NULL;
	struct list_blocks_struct lb;
	unsigned long max_entries;

	if (!test_opt(inode->i_sb, EXTENTS))
		/*
		 * if mounted with noextents we don't allow the migrate
		 */
		return -EINVAL;

	if ((EXT4_I(inode)->i_flags & EXT4_EXTENTS_FL))
		return -EINVAL;

	if (S_ISLNK(inode->i_mode) && inode->i_blocks == 0)
		/*
		 * don't migrate fast symlink
		 */
		return retval;

	handle = ext4_journal_start(inode,
					EXT4_DATA_TRANS_BLOCKS(inode->i_sb) +
					EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3 +
					2 * EXT4_QUOTA_INIT_BLOCKS(inode->i_sb)
					+ 1);
	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		goto err_out;
	}
	tmp_inode = ext4_new_inode(handle,
				inode->i_sb->s_root->d_inode,
				S_IFREG);
	if (IS_ERR(tmp_inode)) {
		retval = -ENOMEM;
		ext4_journal_stop(handle);
		tmp_inode = NULL;
		goto err_out;
	}
	i_size_write(tmp_inode, i_size_read(inode));
	/*
	 * We don't want the inode to be reclaimed
	 * if we got interrupted in between. We have
	 * this tmp inode carrying reference to the
	 * data blocks of the original file. We set
	 * the i_nlink to zero at the last stage after
	 * switching the original file to extent format
	 */
	tmp_inode->i_nlink = 1;

	ext4_ext_tree_init(handle, tmp_inode);
	ext4_orphan_add(handle, tmp_inode);
	ext4_journal_stop(handle);

	/*
	 * start with one credit accounted for
	 * superblock modification.
	 *
	 * For the tmp_inode we already have commited the
	 * trascation that created the inode. Later as and
	 * when we add extents we extent the journal
	 */
	/*
	 * inode_mutex prevent write and truncate on the file. Read still goes
	 * through. We take i_data_sem in ext4_ext_swap_inode_data before we
	 * switch the inode format to prevent read.
	 */
	mutex_lock(&(inode->i_mutex));
	/*
	 * Even though we take i_mutex we can still cause block allocation
	 * via mmap write to holes. If we have allocated new blocks we fail
	 * migrate.  New block allocation will clear EXT4_EXT_MIGRATE flag.
	 * The flag is updated with i_data_sem held to prevent racing with
	 * block allocation.
	 */
	down_read((&EXT4_I(inode)->i_data_sem));
	EXT4_I(inode)->i_flags = EXT4_I(inode)->i_flags | EXT4_EXT_MIGRATE;
	up_read((&EXT4_I(inode)->i_data_sem));

	handle = ext4_journal_start(inode, 1);

	ei = EXT4_I(inode);
	i_data = ei->i_data;
	memset(&lb, 0, sizeof(lb));

	/* 32 bit block address 4 bytes */
	max_entries = inode->i_sb->s_blocksize >> 2;
	for (i = 0; i < EXT4_NDIR_BLOCKS; i++, blk_count++) {
		if (i_data[i]) {
			retval = update_extent_range(handle, tmp_inode,
						le32_to_cpu(i_data[i]),
						blk_count, &lb);
			if (retval)
				goto err_out;
		}
	}
	if (i_data[EXT4_IND_BLOCK]) {
		retval = update_ind_extent_range(handle, tmp_inode,
					le32_to_cpu(i_data[EXT4_IND_BLOCK]),
					&blk_count, &lb);
			if (retval)
				goto err_out;
	} else
		blk_count +=  max_entries;
	if (i_data[EXT4_DIND_BLOCK]) {
		retval = update_dind_extent_range(handle, tmp_inode,
					le32_to_cpu(i_data[EXT4_DIND_BLOCK]),
					&blk_count, &lb);
			if (retval)
				goto err_out;
	} else
		blk_count += max_entries * max_entries;
	if (i_data[EXT4_TIND_BLOCK]) {
		retval = update_tind_extent_range(handle, tmp_inode,
					le32_to_cpu(i_data[EXT4_TIND_BLOCK]),
					&blk_count, &lb);
			if (retval)
				goto err_out;
	}
	/*
	 * Build the last extent
	 */
	retval = finish_range(handle, tmp_inode, &lb);
err_out:
	if (retval)
		/*
		 * Failure case delete the extent information with the
		 * tmp_inode
		 */
		free_ext_block(handle, tmp_inode);
	else {
		retval = ext4_ext_swap_inode_data(handle, inode, tmp_inode);
		if (retval)
			/*
			 * if we fail to swap inode data free the extent
			 * details of the tmp inode
			 */
			free_ext_block(handle, tmp_inode);
	}

	/* We mark the tmp_inode dirty via ext4_ext_tree_init. */
	if (ext4_journal_extend(handle, 1) != 0)
		ext4_journal_restart(handle, 1);

	/*
	 * Mark the tmp_inode as of size zero
	 */
	i_size_write(tmp_inode, 0);

	/*
	 * set the  i_blocks count to zero
	 * so that the ext4_delete_inode does the
	 * right job
	 *
	 * We don't need to take the i_lock because
	 * the inode is not visible to user space.
	 */
	tmp_inode->i_blocks = 0;

	/* Reset the extent details */
	ext4_ext_tree_init(handle, tmp_inode);

	/*
	 * Set the i_nlink to zero so that
	 * generic_drop_inode really deletes the
	 * inode
	 */
	tmp_inode->i_nlink = 0;

	ext4_journal_stop(handle);
	mutex_unlock(&(inode->i_mutex));

	if (tmp_inode)
		iput(tmp_inode);

	return retval;
}
