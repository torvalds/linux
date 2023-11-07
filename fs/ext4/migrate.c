// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright IBM Corporation, 2007
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 */

#include <linux/slab.h>
#include "ext4_jbd2.h"
#include "ext4_extents.h"

/*
 * The contiguous blocks details which can be
 * represented by a single extent
 */
struct migrate_struct {
	ext4_lblk_t first_block, last_block, curr_block;
	ext4_fsblk_t first_pblock, last_pblock;
};

static int finish_range(handle_t *handle, struct inode *inode,
				struct migrate_struct *lb)

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
	/* Locking only for convenience since we are operating on temp inode */
	down_write(&EXT4_I(inode)->i_data_sem);
	path = ext4_find_extent(inode, lb->first_block, NULL, 0);
	if (IS_ERR(path)) {
		retval = PTR_ERR(path);
		path = NULL;
		goto err_out;
	}

	/*
	 * Calculate the credit needed to inserting this extent
	 * Since we are doing this in loop we may accumulate extra
	 * credit. But below we try to not accumulate too much
	 * of them by restarting the journal.
	 */
	needed = ext4_ext_calc_credits_for_single_extent(inode,
		    lb->last_block - lb->first_block + 1, path);

	retval = ext4_datasem_ensure_credits(handle, inode, needed, needed, 0);
	if (retval < 0)
		goto err_out;
	retval = ext4_ext_insert_extent(handle, inode, &path, &newext, 0);
err_out:
	up_write((&EXT4_I(inode)->i_data_sem));
	ext4_ext_drop_refs(path);
	kfree(path);
	lb->first_pblock = 0;
	return retval;
}

static int update_extent_range(handle_t *handle, struct inode *inode,
			       ext4_fsblk_t pblock, struct migrate_struct *lb)
{
	int retval;
	/*
	 * See if we can add on to the existing range (if it exists)
	 */
	if (lb->first_pblock &&
		(lb->last_pblock+1 == pblock) &&
		(lb->last_block+1 == lb->curr_block)) {
		lb->last_pblock = pblock;
		lb->last_block = lb->curr_block;
		lb->curr_block++;
		return 0;
	}
	/*
	 * Start a new range.
	 */
	retval = finish_range(handle, inode, lb);
	lb->first_pblock = lb->last_pblock = pblock;
	lb->first_block = lb->last_block = lb->curr_block;
	lb->curr_block++;
	return retval;
}

static int update_ind_extent_range(handle_t *handle, struct inode *inode,
				   ext4_fsblk_t pblock,
				   struct migrate_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	bh = ext4_sb_bread(inode->i_sb, pblock, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (i_data[i]) {
			retval = update_extent_range(handle, inode,
						le32_to_cpu(i_data[i]), lb);
			if (retval)
				break;
		} else {
			lb->curr_block++;
		}
	}
	put_bh(bh);
	return retval;

}

static int update_dind_extent_range(handle_t *handle, struct inode *inode,
				    ext4_fsblk_t pblock,
				    struct migrate_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	bh = ext4_sb_bread(inode->i_sb, pblock, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (i_data[i]) {
			retval = update_ind_extent_range(handle, inode,
						le32_to_cpu(i_data[i]), lb);
			if (retval)
				break;
		} else {
			/* Only update the file block number */
			lb->curr_block += max_entries;
		}
	}
	put_bh(bh);
	return retval;

}

static int update_tind_extent_range(handle_t *handle, struct inode *inode,
				    ext4_fsblk_t pblock,
				    struct migrate_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	bh = ext4_sb_bread(inode->i_sb, pblock, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (i_data[i]) {
			retval = update_dind_extent_range(handle, inode,
						le32_to_cpu(i_data[i]), lb);
			if (retval)
				break;
		} else {
			/* Only update the file block number */
			lb->curr_block += max_entries * max_entries;
		}
	}
	put_bh(bh);
	return retval;

}

static int free_dind_blocks(handle_t *handle,
				struct inode *inode, __le32 i_data)
{
	int i;
	__le32 *tmp_idata;
	struct buffer_head *bh;
	struct super_block *sb = inode->i_sb;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;
	int err;

	bh = ext4_sb_bread(sb, le32_to_cpu(i_data), 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	tmp_idata = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (tmp_idata[i]) {
			err = ext4_journal_ensure_credits(handle,
				EXT4_RESERVE_TRANS_BLOCKS,
				ext4_free_metadata_revoke_credits(sb, 1));
			if (err < 0) {
				put_bh(bh);
				return err;
			}
			ext4_free_blocks(handle, inode, NULL,
					 le32_to_cpu(tmp_idata[i]), 1,
					 EXT4_FREE_BLOCKS_METADATA |
					 EXT4_FREE_BLOCKS_FORGET);
		}
	}
	put_bh(bh);
	err = ext4_journal_ensure_credits(handle, EXT4_RESERVE_TRANS_BLOCKS,
				ext4_free_metadata_revoke_credits(sb, 1));
	if (err < 0)
		return err;
	ext4_free_blocks(handle, inode, NULL, le32_to_cpu(i_data), 1,
			 EXT4_FREE_BLOCKS_METADATA |
			 EXT4_FREE_BLOCKS_FORGET);
	return 0;
}

static int free_tind_blocks(handle_t *handle,
				struct inode *inode, __le32 i_data)
{
	int i, retval = 0;
	__le32 *tmp_idata;
	struct buffer_head *bh;
	unsigned long max_entries = inode->i_sb->s_blocksize >> 2;

	bh = ext4_sb_bread(inode->i_sb, le32_to_cpu(i_data), 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

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
	retval = ext4_journal_ensure_credits(handle, EXT4_RESERVE_TRANS_BLOCKS,
			ext4_free_metadata_revoke_credits(inode->i_sb, 1));
	if (retval < 0)
		return retval;
	ext4_free_blocks(handle, inode, NULL, le32_to_cpu(i_data), 1,
			 EXT4_FREE_BLOCKS_METADATA |
			 EXT4_FREE_BLOCKS_FORGET);
	return 0;
}

static int free_ind_block(handle_t *handle, struct inode *inode, __le32 *i_data)
{
	int retval;

	/* ei->i_data[EXT4_IND_BLOCK] */
	if (i_data[0]) {
		retval = ext4_journal_ensure_credits(handle,
			EXT4_RESERVE_TRANS_BLOCKS,
			ext4_free_metadata_revoke_credits(inode->i_sb, 1));
		if (retval < 0)
			return retval;
		ext4_free_blocks(handle, inode, NULL,
				le32_to_cpu(i_data[0]), 1,
				 EXT4_FREE_BLOCKS_METADATA |
				 EXT4_FREE_BLOCKS_FORGET);
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
	int retval, retval2 = 0;
	__le32	i_data[3];
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_inode_info *tmp_ei = EXT4_I(tmp_inode);

	/*
	 * One credit accounted for writing the
	 * i_data field of the original inode
	 */
	retval = ext4_journal_ensure_credits(handle, 1, 0);
	if (retval < 0)
		goto err_out;

	i_data[0] = ei->i_data[EXT4_IND_BLOCK];
	i_data[1] = ei->i_data[EXT4_DIND_BLOCK];
	i_data[2] = ei->i_data[EXT4_TIND_BLOCK];

	down_write(&EXT4_I(inode)->i_data_sem);
	/*
	 * if EXT4_STATE_EXT_MIGRATE is cleared a block allocation
	 * happened after we started the migrate. We need to
	 * fail the migrate
	 */
	if (!ext4_test_inode_state(inode, EXT4_STATE_EXT_MIGRATE)) {
		retval = -EAGAIN;
		up_write(&EXT4_I(inode)->i_data_sem);
		goto err_out;
	} else
		ext4_clear_inode_state(inode, EXT4_STATE_EXT_MIGRATE);
	/*
	 * We have the extent map build with the tmp inode.
	 * Now copy the i_data across
	 */
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	memcpy(ei->i_data, tmp_ei->i_data, sizeof(ei->i_data));

	/*
	 * Update i_blocks with the new blocks that got
	 * allocated while adding extents for extent index
	 * blocks.
	 *
	 * While converting to extents we need not
	 * update the original inode i_blocks for extent blocks
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
	retval2 = ext4_mark_inode_dirty(handle, inode);
	if (unlikely(retval2 && !retval))
		retval = retval2;

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

	block = ext4_idx_pblock(ix);
	bh = ext4_sb_bread(inode->i_sb, block, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	eh = (struct ext4_extent_header *)bh->b_data;
	if (eh->eh_depth != 0) {
		ix = EXT_FIRST_INDEX(eh);
		for (i = 0; i < le16_to_cpu(eh->eh_entries); i++, ix++) {
			retval = free_ext_idx(handle, inode, ix);
			if (retval) {
				put_bh(bh);
				return retval;
			}
		}
	}
	put_bh(bh);
	retval = ext4_journal_ensure_credits(handle, EXT4_RESERVE_TRANS_BLOCKS,
			ext4_free_metadata_revoke_credits(inode->i_sb, 1));
	if (retval < 0)
		return retval;
	ext4_free_blocks(handle, inode, NULL, block, 1,
			 EXT4_FREE_BLOCKS_METADATA | EXT4_FREE_BLOCKS_FORGET);
	return 0;
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

int ext4_ext_migrate(struct inode *inode)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	handle_t *handle;
	int retval = 0, i;
	__le32 *i_data;
	struct ext4_inode_info *ei;
	struct inode *tmp_inode = NULL;
	struct migrate_struct lb;
	unsigned long max_entries;
	__u32 goal, tmp_csum_seed;
	uid_t owner[2];

	/*
	 * If the filesystem does not support extents, or the inode
	 * already is extent-based, error out.
	 */
	if (!ext4_has_feature_extents(inode->i_sb) ||
	    ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS) ||
	    ext4_has_inline_data(inode))
		return -EINVAL;

	if (S_ISLNK(inode->i_mode) && inode->i_blocks == 0)
		/*
		 * don't migrate fast symlink
		 */
		return retval;

	percpu_down_write(&sbi->s_writepages_rwsem);

	/*
	 * Worst case we can touch the allocation bitmaps and a block
	 * group descriptor block.  We do need need to worry about
	 * credits for modifying the quota inode.
	 */
	handle = ext4_journal_start(inode, EXT4_HT_MIGRATE,
		3 + EXT4_MAXQUOTAS_TRANS_BLOCKS(inode->i_sb));

	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		goto out_unlock;
	}
	goal = (((inode->i_ino - 1) / EXT4_INODES_PER_GROUP(inode->i_sb)) *
		EXT4_INODES_PER_GROUP(inode->i_sb)) + 1;
	owner[0] = i_uid_read(inode);
	owner[1] = i_gid_read(inode);
	tmp_inode = ext4_new_inode(handle, d_inode(inode->i_sb->s_root),
				   S_IFREG, NULL, goal, owner, 0);
	if (IS_ERR(tmp_inode)) {
		retval = PTR_ERR(tmp_inode);
		ext4_journal_stop(handle);
		goto out_unlock;
	}
	/*
	 * Use the correct seed for checksum (i.e. the seed from 'inode').  This
	 * is so that the metadata blocks will have the correct checksum after
	 * the migration.
	 */
	ei = EXT4_I(inode);
	tmp_csum_seed = EXT4_I(tmp_inode)->i_csum_seed;
	EXT4_I(tmp_inode)->i_csum_seed = ei->i_csum_seed;
	i_size_write(tmp_inode, i_size_read(inode));
	/*
	 * Set the i_nlink to zero so it will be deleted later
	 * when we drop inode reference.
	 */
	clear_nlink(tmp_inode);

	ext4_ext_tree_init(handle, tmp_inode);
	ext4_journal_stop(handle);

	/*
	 * start with one credit accounted for
	 * superblock modification.
	 *
	 * For the tmp_inode we already have committed the
	 * transaction that created the inode. Later as and
	 * when we add extents we extent the journal
	 */
	/*
	 * Even though we take i_mutex we can still cause block
	 * allocation via mmap write to holes. If we have allocated
	 * new blocks we fail migrate.  New block allocation will
	 * clear EXT4_STATE_EXT_MIGRATE flag.  The flag is updated
	 * with i_data_sem held to prevent racing with block
	 * allocation.
	 */
	down_read(&EXT4_I(inode)->i_data_sem);
	ext4_set_inode_state(inode, EXT4_STATE_EXT_MIGRATE);
	up_read((&EXT4_I(inode)->i_data_sem));

	handle = ext4_journal_start(inode, EXT4_HT_MIGRATE, 1);
	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		goto out_tmp_inode;
	}

	i_data = ei->i_data;
	memset(&lb, 0, sizeof(lb));

	/* 32 bit block address 4 bytes */
	max_entries = inode->i_sb->s_blocksize >> 2;
	for (i = 0; i < EXT4_NDIR_BLOCKS; i++) {
		if (i_data[i]) {
			retval = update_extent_range(handle, tmp_inode,
						le32_to_cpu(i_data[i]), &lb);
			if (retval)
				goto err_out;
		} else
			lb.curr_block++;
	}
	if (i_data[EXT4_IND_BLOCK]) {
		retval = update_ind_extent_range(handle, tmp_inode,
				le32_to_cpu(i_data[EXT4_IND_BLOCK]), &lb);
		if (retval)
			goto err_out;
	} else
		lb.curr_block += max_entries;
	if (i_data[EXT4_DIND_BLOCK]) {
		retval = update_dind_extent_range(handle, tmp_inode,
				le32_to_cpu(i_data[EXT4_DIND_BLOCK]), &lb);
		if (retval)
			goto err_out;
	} else
		lb.curr_block += max_entries * max_entries;
	if (i_data[EXT4_TIND_BLOCK]) {
		retval = update_tind_extent_range(handle, tmp_inode,
				le32_to_cpu(i_data[EXT4_TIND_BLOCK]), &lb);
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
	retval = ext4_journal_ensure_credits(handle, 1, 0);
	if (retval < 0)
		goto out_stop;
	/*
	 * Mark the tmp_inode as of size zero
	 */
	i_size_write(tmp_inode, 0);

	/*
	 * set the  i_blocks count to zero
	 * so that the ext4_evict_inode() does the
	 * right job
	 *
	 * We don't need to take the i_lock because
	 * the inode is not visible to user space.
	 */
	tmp_inode->i_blocks = 0;
	EXT4_I(tmp_inode)->i_csum_seed = tmp_csum_seed;

	/* Reset the extent details */
	ext4_ext_tree_init(handle, tmp_inode);
out_stop:
	ext4_journal_stop(handle);
out_tmp_inode:
	unlock_new_inode(tmp_inode);
	iput(tmp_inode);
out_unlock:
	percpu_up_write(&sbi->s_writepages_rwsem);
	return retval;
}

/*
 * Migrate a simple extent-based inode to use the i_blocks[] array
 */
int ext4_ind_migrate(struct inode *inode)
{
	struct ext4_extent_header	*eh;
	struct ext4_sb_info		*sbi = EXT4_SB(inode->i_sb);
	struct ext4_super_block		*es = sbi->s_es;
	struct ext4_inode_info		*ei = EXT4_I(inode);
	struct ext4_extent		*ex;
	unsigned int			i, len;
	ext4_lblk_t			start, end;
	ext4_fsblk_t			blk;
	handle_t			*handle;
	int				ret, ret2 = 0;

	if (!ext4_has_feature_extents(inode->i_sb) ||
	    (!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)))
		return -EINVAL;

	if (ext4_has_feature_bigalloc(inode->i_sb))
		return -EOPNOTSUPP;

	/*
	 * In order to get correct extent info, force all delayed allocation
	 * blocks to be allocated, otherwise delayed allocation blocks may not
	 * be reflected and bypass the checks on extent header.
	 */
	if (test_opt(inode->i_sb, DELALLOC))
		ext4_alloc_da_blocks(inode);

	percpu_down_write(&sbi->s_writepages_rwsem);

	handle = ext4_journal_start(inode, EXT4_HT_MIGRATE, 1);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out_unlock;
	}

	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_check_inode(inode);
	if (ret)
		goto errout;

	eh = ext_inode_hdr(inode);
	ex  = EXT_FIRST_EXTENT(eh);
	if (ext4_blocks_count(es) > EXT4_MAX_BLOCK_FILE_PHYS ||
	    eh->eh_depth != 0 || le16_to_cpu(eh->eh_entries) > 1) {
		ret = -EOPNOTSUPP;
		goto errout;
	}
	if (eh->eh_entries == 0)
		blk = len = start = end = 0;
	else {
		len = le16_to_cpu(ex->ee_len);
		blk = ext4_ext_pblock(ex);
		start = le32_to_cpu(ex->ee_block);
		end = start + len - 1;
		if (end >= EXT4_NDIR_BLOCKS) {
			ret = -EOPNOTSUPP;
			goto errout;
		}
	}

	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	memset(ei->i_data, 0, sizeof(ei->i_data));
	for (i = start; i <= end; i++)
		ei->i_data[i] = cpu_to_le32(blk++);
	ret2 = ext4_mark_inode_dirty(handle, inode);
	if (unlikely(ret2 && !ret))
		ret = ret2;
errout:
	ext4_journal_stop(handle);
	up_write(&EXT4_I(inode)->i_data_sem);
out_unlock:
	percpu_up_write(&sbi->s_writepages_rwsem);
	return ret;
}
