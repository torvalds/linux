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

static int finish_range(handle_t *handle, struct iyesde *iyesde,
				struct migrate_struct *lb)

{
	int retval = 0, needed;
	struct ext4_extent newext;
	struct ext4_ext_path *path;
	if (lb->first_pblock == 0)
		return 0;

	/* Add the extent to temp iyesde*/
	newext.ee_block = cpu_to_le32(lb->first_block);
	newext.ee_len   = cpu_to_le16(lb->last_block - lb->first_block + 1);
	ext4_ext_store_pblock(&newext, lb->first_pblock);
	/* Locking only for convinience since we are operating on temp iyesde */
	down_write(&EXT4_I(iyesde)->i_data_sem);
	path = ext4_find_extent(iyesde, lb->first_block, NULL, 0);
	if (IS_ERR(path)) {
		retval = PTR_ERR(path);
		path = NULL;
		goto err_out;
	}

	/*
	 * Calculate the credit needed to inserting this extent
	 * Since we are doing this in loop we may accumalate extra
	 * credit. But below we try to yest accumalate too much
	 * of them by restarting the journal.
	 */
	needed = ext4_ext_calc_credits_for_single_extent(iyesde,
		    lb->last_block - lb->first_block + 1, path);

	retval = ext4_datasem_ensure_credits(handle, iyesde, needed, needed, 0);
	if (retval < 0)
		goto err_out;
	retval = ext4_ext_insert_extent(handle, iyesde, &path, &newext, 0);
err_out:
	up_write((&EXT4_I(iyesde)->i_data_sem));
	ext4_ext_drop_refs(path);
	kfree(path);
	lb->first_pblock = 0;
	return retval;
}

static int update_extent_range(handle_t *handle, struct iyesde *iyesde,
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
	retval = finish_range(handle, iyesde, lb);
	lb->first_pblock = lb->last_pblock = pblock;
	lb->first_block = lb->last_block = lb->curr_block;
	lb->curr_block++;
	return retval;
}

static int update_ind_extent_range(handle_t *handle, struct iyesde *iyesde,
				   ext4_fsblk_t pblock,
				   struct migrate_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	unsigned long max_entries = iyesde->i_sb->s_blocksize >> 2;

	bh = ext4_sb_bread(iyesde->i_sb, pblock, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (i_data[i]) {
			retval = update_extent_range(handle, iyesde,
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

static int update_dind_extent_range(handle_t *handle, struct iyesde *iyesde,
				    ext4_fsblk_t pblock,
				    struct migrate_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	unsigned long max_entries = iyesde->i_sb->s_blocksize >> 2;

	bh = ext4_sb_bread(iyesde->i_sb, pblock, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (i_data[i]) {
			retval = update_ind_extent_range(handle, iyesde,
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

static int update_tind_extent_range(handle_t *handle, struct iyesde *iyesde,
				    ext4_fsblk_t pblock,
				    struct migrate_struct *lb)
{
	struct buffer_head *bh;
	__le32 *i_data;
	int i, retval = 0;
	unsigned long max_entries = iyesde->i_sb->s_blocksize >> 2;

	bh = ext4_sb_bread(iyesde->i_sb, pblock, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	i_data = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (i_data[i]) {
			retval = update_dind_extent_range(handle, iyesde,
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
				struct iyesde *iyesde, __le32 i_data)
{
	int i;
	__le32 *tmp_idata;
	struct buffer_head *bh;
	struct super_block *sb = iyesde->i_sb;
	unsigned long max_entries = iyesde->i_sb->s_blocksize >> 2;
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
			ext4_free_blocks(handle, iyesde, NULL,
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
	ext4_free_blocks(handle, iyesde, NULL, le32_to_cpu(i_data), 1,
			 EXT4_FREE_BLOCKS_METADATA |
			 EXT4_FREE_BLOCKS_FORGET);
	return 0;
}

static int free_tind_blocks(handle_t *handle,
				struct iyesde *iyesde, __le32 i_data)
{
	int i, retval = 0;
	__le32 *tmp_idata;
	struct buffer_head *bh;
	unsigned long max_entries = iyesde->i_sb->s_blocksize >> 2;

	bh = ext4_sb_bread(iyesde->i_sb, le32_to_cpu(i_data), 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	tmp_idata = (__le32 *)bh->b_data;
	for (i = 0; i < max_entries; i++) {
		if (tmp_idata[i]) {
			retval = free_dind_blocks(handle,
					iyesde, tmp_idata[i]);
			if (retval) {
				put_bh(bh);
				return retval;
			}
		}
	}
	put_bh(bh);
	retval = ext4_journal_ensure_credits(handle, EXT4_RESERVE_TRANS_BLOCKS,
			ext4_free_metadata_revoke_credits(iyesde->i_sb, 1));
	if (retval < 0)
		return retval;
	ext4_free_blocks(handle, iyesde, NULL, le32_to_cpu(i_data), 1,
			 EXT4_FREE_BLOCKS_METADATA |
			 EXT4_FREE_BLOCKS_FORGET);
	return 0;
}

static int free_ind_block(handle_t *handle, struct iyesde *iyesde, __le32 *i_data)
{
	int retval;

	/* ei->i_data[EXT4_IND_BLOCK] */
	if (i_data[0]) {
		retval = ext4_journal_ensure_credits(handle,
			EXT4_RESERVE_TRANS_BLOCKS,
			ext4_free_metadata_revoke_credits(iyesde->i_sb, 1));
		if (retval < 0)
			return retval;
		ext4_free_blocks(handle, iyesde, NULL,
				le32_to_cpu(i_data[0]), 1,
				 EXT4_FREE_BLOCKS_METADATA |
				 EXT4_FREE_BLOCKS_FORGET);
	}

	/* ei->i_data[EXT4_DIND_BLOCK] */
	if (i_data[1]) {
		retval = free_dind_blocks(handle, iyesde, i_data[1]);
		if (retval)
			return retval;
	}

	/* ei->i_data[EXT4_TIND_BLOCK] */
	if (i_data[2]) {
		retval = free_tind_blocks(handle, iyesde, i_data[2]);
		if (retval)
			return retval;
	}
	return 0;
}

static int ext4_ext_swap_iyesde_data(handle_t *handle, struct iyesde *iyesde,
						struct iyesde *tmp_iyesde)
{
	int retval;
	__le32	i_data[3];
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);
	struct ext4_iyesde_info *tmp_ei = EXT4_I(tmp_iyesde);

	/*
	 * One credit accounted for writing the
	 * i_data field of the original iyesde
	 */
	retval = ext4_journal_ensure_credits(handle, 1, 0);
	if (retval < 0)
		goto err_out;

	i_data[0] = ei->i_data[EXT4_IND_BLOCK];
	i_data[1] = ei->i_data[EXT4_DIND_BLOCK];
	i_data[2] = ei->i_data[EXT4_TIND_BLOCK];

	down_write(&EXT4_I(iyesde)->i_data_sem);
	/*
	 * if EXT4_STATE_EXT_MIGRATE is cleared a block allocation
	 * happened after we started the migrate. We need to
	 * fail the migrate
	 */
	if (!ext4_test_iyesde_state(iyesde, EXT4_STATE_EXT_MIGRATE)) {
		retval = -EAGAIN;
		up_write(&EXT4_I(iyesde)->i_data_sem);
		goto err_out;
	} else
		ext4_clear_iyesde_state(iyesde, EXT4_STATE_EXT_MIGRATE);
	/*
	 * We have the extent map build with the tmp iyesde.
	 * Now copy the i_data across
	 */
	ext4_set_iyesde_flag(iyesde, EXT4_INODE_EXTENTS);
	memcpy(ei->i_data, tmp_ei->i_data, sizeof(ei->i_data));

	/*
	 * Update i_blocks with the new blocks that got
	 * allocated while adding extents for extent index
	 * blocks.
	 *
	 * While converting to extents we need yest
	 * update the original iyesde i_blocks for extent blocks
	 * via quota APIs. The quota update happened via tmp_iyesde already.
	 */
	spin_lock(&iyesde->i_lock);
	iyesde->i_blocks += tmp_iyesde->i_blocks;
	spin_unlock(&iyesde->i_lock);
	up_write(&EXT4_I(iyesde)->i_data_sem);

	/*
	 * We mark the iyesde dirty after, because we decrement the
	 * i_blocks when freeing the indirect meta-data blocks
	 */
	retval = free_ind_block(handle, iyesde, i_data);
	ext4_mark_iyesde_dirty(handle, iyesde);

err_out:
	return retval;
}

static int free_ext_idx(handle_t *handle, struct iyesde *iyesde,
					struct ext4_extent_idx *ix)
{
	int i, retval = 0;
	ext4_fsblk_t block;
	struct buffer_head *bh;
	struct ext4_extent_header *eh;

	block = ext4_idx_pblock(ix);
	bh = ext4_sb_bread(iyesde->i_sb, block, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	eh = (struct ext4_extent_header *)bh->b_data;
	if (eh->eh_depth != 0) {
		ix = EXT_FIRST_INDEX(eh);
		for (i = 0; i < le16_to_cpu(eh->eh_entries); i++, ix++) {
			retval = free_ext_idx(handle, iyesde, ix);
			if (retval) {
				put_bh(bh);
				return retval;
			}
		}
	}
	put_bh(bh);
	retval = ext4_journal_ensure_credits(handle, EXT4_RESERVE_TRANS_BLOCKS,
			ext4_free_metadata_revoke_credits(iyesde->i_sb, 1));
	if (retval < 0)
		return retval;
	ext4_free_blocks(handle, iyesde, NULL, block, 1,
			 EXT4_FREE_BLOCKS_METADATA | EXT4_FREE_BLOCKS_FORGET);
	return 0;
}

/*
 * Free the extent meta data blocks only
 */
static int free_ext_block(handle_t *handle, struct iyesde *iyesde)
{
	int i, retval = 0;
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);
	struct ext4_extent_header *eh = (struct ext4_extent_header *)ei->i_data;
	struct ext4_extent_idx *ix;
	if (eh->eh_depth == 0)
		/*
		 * No extra blocks allocated for extent meta data
		 */
		return 0;
	ix = EXT_FIRST_INDEX(eh);
	for (i = 0; i < le16_to_cpu(eh->eh_entries); i++, ix++) {
		retval = free_ext_idx(handle, iyesde, ix);
		if (retval)
			return retval;
	}
	return retval;
}

int ext4_ext_migrate(struct iyesde *iyesde)
{
	handle_t *handle;
	int retval = 0, i;
	__le32 *i_data;
	struct ext4_iyesde_info *ei;
	struct iyesde *tmp_iyesde = NULL;
	struct migrate_struct lb;
	unsigned long max_entries;
	__u32 goal;
	uid_t owner[2];

	/*
	 * If the filesystem does yest support extents, or the iyesde
	 * already is extent-based, error out.
	 */
	if (!ext4_has_feature_extents(iyesde->i_sb) ||
	    (ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS)))
		return -EINVAL;

	if (S_ISLNK(iyesde->i_mode) && iyesde->i_blocks == 0)
		/*
		 * don't migrate fast symlink
		 */
		return retval;

	/*
	 * Worst case we can touch the allocation bitmaps, a bgd
	 * block, and a block to link in the orphan list.  We do need
	 * need to worry about credits for modifying the quota iyesde.
	 */
	handle = ext4_journal_start(iyesde, EXT4_HT_MIGRATE,
		4 + EXT4_MAXQUOTAS_TRANS_BLOCKS(iyesde->i_sb));

	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		return retval;
	}
	goal = (((iyesde->i_iyes - 1) / EXT4_INODES_PER_GROUP(iyesde->i_sb)) *
		EXT4_INODES_PER_GROUP(iyesde->i_sb)) + 1;
	owner[0] = i_uid_read(iyesde);
	owner[1] = i_gid_read(iyesde);
	tmp_iyesde = ext4_new_iyesde(handle, d_iyesde(iyesde->i_sb->s_root),
				   S_IFREG, NULL, goal, owner, 0);
	if (IS_ERR(tmp_iyesde)) {
		retval = PTR_ERR(tmp_iyesde);
		ext4_journal_stop(handle);
		return retval;
	}
	i_size_write(tmp_iyesde, i_size_read(iyesde));
	/*
	 * Set the i_nlink to zero so it will be deleted later
	 * when we drop iyesde reference.
	 */
	clear_nlink(tmp_iyesde);

	ext4_ext_tree_init(handle, tmp_iyesde);
	ext4_orphan_add(handle, tmp_iyesde);
	ext4_journal_stop(handle);

	/*
	 * start with one credit accounted for
	 * superblock modification.
	 *
	 * For the tmp_iyesde we already have committed the
	 * transaction that created the iyesde. Later as and
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
	down_read(&EXT4_I(iyesde)->i_data_sem);
	ext4_set_iyesde_state(iyesde, EXT4_STATE_EXT_MIGRATE);
	up_read((&EXT4_I(iyesde)->i_data_sem));

	handle = ext4_journal_start(iyesde, EXT4_HT_MIGRATE, 1);
	if (IS_ERR(handle)) {
		/*
		 * It is impossible to update on-disk structures without
		 * a handle, so just rollback in-core changes and live other
		 * work to orphan_list_cleanup()
		 */
		ext4_orphan_del(NULL, tmp_iyesde);
		retval = PTR_ERR(handle);
		goto out;
	}

	ei = EXT4_I(iyesde);
	i_data = ei->i_data;
	memset(&lb, 0, sizeof(lb));

	/* 32 bit block address 4 bytes */
	max_entries = iyesde->i_sb->s_blocksize >> 2;
	for (i = 0; i < EXT4_NDIR_BLOCKS; i++) {
		if (i_data[i]) {
			retval = update_extent_range(handle, tmp_iyesde,
						le32_to_cpu(i_data[i]), &lb);
			if (retval)
				goto err_out;
		} else
			lb.curr_block++;
	}
	if (i_data[EXT4_IND_BLOCK]) {
		retval = update_ind_extent_range(handle, tmp_iyesde,
				le32_to_cpu(i_data[EXT4_IND_BLOCK]), &lb);
		if (retval)
			goto err_out;
	} else
		lb.curr_block += max_entries;
	if (i_data[EXT4_DIND_BLOCK]) {
		retval = update_dind_extent_range(handle, tmp_iyesde,
				le32_to_cpu(i_data[EXT4_DIND_BLOCK]), &lb);
		if (retval)
			goto err_out;
	} else
		lb.curr_block += max_entries * max_entries;
	if (i_data[EXT4_TIND_BLOCK]) {
		retval = update_tind_extent_range(handle, tmp_iyesde,
				le32_to_cpu(i_data[EXT4_TIND_BLOCK]), &lb);
		if (retval)
			goto err_out;
	}
	/*
	 * Build the last extent
	 */
	retval = finish_range(handle, tmp_iyesde, &lb);
err_out:
	if (retval)
		/*
		 * Failure case delete the extent information with the
		 * tmp_iyesde
		 */
		free_ext_block(handle, tmp_iyesde);
	else {
		retval = ext4_ext_swap_iyesde_data(handle, iyesde, tmp_iyesde);
		if (retval)
			/*
			 * if we fail to swap iyesde data free the extent
			 * details of the tmp iyesde
			 */
			free_ext_block(handle, tmp_iyesde);
	}

	/* We mark the tmp_iyesde dirty via ext4_ext_tree_init. */
	retval = ext4_journal_ensure_credits(handle, 1, 0);
	if (retval < 0)
		goto out_stop;
	/*
	 * Mark the tmp_iyesde as of size zero
	 */
	i_size_write(tmp_iyesde, 0);

	/*
	 * set the  i_blocks count to zero
	 * so that the ext4_evict_iyesde() does the
	 * right job
	 *
	 * We don't need to take the i_lock because
	 * the iyesde is yest visible to user space.
	 */
	tmp_iyesde->i_blocks = 0;

	/* Reset the extent details */
	ext4_ext_tree_init(handle, tmp_iyesde);
out_stop:
	ext4_journal_stop(handle);
out:
	unlock_new_iyesde(tmp_iyesde);
	iput(tmp_iyesde);

	return retval;
}

/*
 * Migrate a simple extent-based iyesde to use the i_blocks[] array
 */
int ext4_ind_migrate(struct iyesde *iyesde)
{
	struct ext4_extent_header	*eh;
	struct ext4_super_block		*es = EXT4_SB(iyesde->i_sb)->s_es;
	struct ext4_iyesde_info		*ei = EXT4_I(iyesde);
	struct ext4_extent		*ex;
	unsigned int			i, len;
	ext4_lblk_t			start, end;
	ext4_fsblk_t			blk;
	handle_t			*handle;
	int				ret;

	if (!ext4_has_feature_extents(iyesde->i_sb) ||
	    (!ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS)))
		return -EINVAL;

	if (ext4_has_feature_bigalloc(iyesde->i_sb))
		return -EOPNOTSUPP;

	/*
	 * In order to get correct extent info, force all delayed allocation
	 * blocks to be allocated, otherwise delayed allocation blocks may yest
	 * be reflected and bypass the checks on extent header.
	 */
	if (test_opt(iyesde->i_sb, DELALLOC))
		ext4_alloc_da_blocks(iyesde);

	handle = ext4_journal_start(iyesde, EXT4_HT_MIGRATE, 1);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	down_write(&EXT4_I(iyesde)->i_data_sem);
	ret = ext4_ext_check_iyesde(iyesde);
	if (ret)
		goto errout;

	eh = ext_iyesde_hdr(iyesde);
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

	ext4_clear_iyesde_flag(iyesde, EXT4_INODE_EXTENTS);
	memset(ei->i_data, 0, sizeof(ei->i_data));
	for (i = start; i <= end; i++)
		ei->i_data[i] = cpu_to_le32(blk++);
	ext4_mark_iyesde_dirty(handle, iyesde);
errout:
	ext4_journal_stop(handle);
	up_write(&EXT4_I(iyesde)->i_data_sem);
	return ret;
}
