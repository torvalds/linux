/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * resize.c
 *
 * volume resize.
 * Inspired by ext3/resize.c.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/types.h>

#define MLOG_MASK_PREFIX ML_DISK_ALLOC
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "inode.h"
#include "journal.h"
#include "super.h"
#include "sysfile.h"
#include "uptodate.h"

#include "buffer_head_io.h"
#include "suballoc.h"
#include "resize.h"

/*
 * Check whether there are new backup superblocks exist
 * in the last group. If there are some, mark them or clear
 * them in the bitmap.
 *
 * Return how many backups we find in the last group.
 */
static u16 ocfs2_calc_new_backup_super(struct inode *inode,
				       struct ocfs2_group_desc *gd,
				       int new_clusters,
				       u32 first_new_cluster,
				       u16 cl_cpg,
				       int set)
{
	int i;
	u16 backups = 0;
	u32 cluster;
	u64 blkno, gd_blkno, lgd_blkno = le64_to_cpu(gd->bg_blkno);

	for (i = 0; i < OCFS2_MAX_BACKUP_SUPERBLOCKS; i++) {
		blkno = ocfs2_backup_super_blkno(inode->i_sb, i);
		cluster = ocfs2_blocks_to_clusters(inode->i_sb, blkno);

		gd_blkno = ocfs2_which_cluster_group(inode, cluster);
		if (gd_blkno < lgd_blkno)
			continue;
		else if (gd_blkno > lgd_blkno)
			break;

		if (set)
			ocfs2_set_bit(cluster % cl_cpg,
				      (unsigned long *)gd->bg_bitmap);
		else
			ocfs2_clear_bit(cluster % cl_cpg,
					(unsigned long *)gd->bg_bitmap);
		backups++;
	}

	mlog_exit_void();
	return backups;
}

static int ocfs2_update_last_group_and_inode(handle_t *handle,
					     struct inode *bm_inode,
					     struct buffer_head *bm_bh,
					     struct buffer_head *group_bh,
					     u32 first_new_cluster,
					     int new_clusters)
{
	int ret = 0;
	struct ocfs2_super *osb = OCFS2_SB(bm_inode->i_sb);
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) bm_bh->b_data;
	struct ocfs2_chain_list *cl = &fe->id2.i_chain;
	struct ocfs2_chain_rec *cr;
	struct ocfs2_group_desc *group;
	u16 chain, num_bits, backups = 0;
	u16 cl_bpc = le16_to_cpu(cl->cl_bpc);
	u16 cl_cpg = le16_to_cpu(cl->cl_cpg);

	mlog_entry("(new_clusters=%d, first_new_cluster = %u)\n",
		   new_clusters, first_new_cluster);

	ret = ocfs2_journal_access(handle, bm_inode, group_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	group = (struct ocfs2_group_desc *)group_bh->b_data;

	/* update the group first. */
	num_bits = new_clusters * cl_bpc;
	le16_add_cpu(&group->bg_bits, num_bits);
	le16_add_cpu(&group->bg_free_bits_count, num_bits);

	/*
	 * check whether there are some new backup superblocks exist in
	 * this group and update the group bitmap accordingly.
	 */
	if (OCFS2_HAS_COMPAT_FEATURE(osb->sb,
				     OCFS2_FEATURE_COMPAT_BACKUP_SB)) {
		backups = ocfs2_calc_new_backup_super(bm_inode,
						     group,
						     new_clusters,
						     first_new_cluster,
						     cl_cpg, 1);
		le16_add_cpu(&group->bg_free_bits_count, -1 * backups);
	}

	ret = ocfs2_journal_dirty(handle, group_bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_rollback;
	}

	/* update the inode accordingly. */
	ret = ocfs2_journal_access(handle, bm_inode, bm_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_rollback;
	}

	chain = le16_to_cpu(group->bg_chain);
	cr = (&cl->cl_recs[chain]);
	le32_add_cpu(&cr->c_total, num_bits);
	le32_add_cpu(&cr->c_free, num_bits);
	le32_add_cpu(&fe->id1.bitmap1.i_total, num_bits);
	le32_add_cpu(&fe->i_clusters, new_clusters);

	if (backups) {
		le32_add_cpu(&cr->c_free, -1 * backups);
		le32_add_cpu(&fe->id1.bitmap1.i_used, backups);
	}

	spin_lock(&OCFS2_I(bm_inode)->ip_lock);
	OCFS2_I(bm_inode)->ip_clusters = le32_to_cpu(fe->i_clusters);
	le64_add_cpu(&fe->i_size, new_clusters << osb->s_clustersize_bits);
	spin_unlock(&OCFS2_I(bm_inode)->ip_lock);
	i_size_write(bm_inode, le64_to_cpu(fe->i_size));

	ocfs2_journal_dirty(handle, bm_bh);

out_rollback:
	if (ret < 0) {
		ocfs2_calc_new_backup_super(bm_inode,
					    group,
					    new_clusters,
					    first_new_cluster,
					    cl_cpg, 0);
		le16_add_cpu(&group->bg_free_bits_count, backups);
		le16_add_cpu(&group->bg_bits, -1 * num_bits);
		le16_add_cpu(&group->bg_free_bits_count, -1 * num_bits);
	}
out:
	mlog_exit(ret);
	return ret;
}

static int update_backups(struct inode * inode, u32 clusters, char *data)
{
	int i, ret = 0;
	u32 cluster;
	u64 blkno;
	struct buffer_head *backup = NULL;
	struct ocfs2_dinode *backup_di = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	/* calculate the real backups we need to update. */
	for (i = 0; i < OCFS2_MAX_BACKUP_SUPERBLOCKS; i++) {
		blkno = ocfs2_backup_super_blkno(inode->i_sb, i);
		cluster = ocfs2_blocks_to_clusters(inode->i_sb, blkno);
		if (cluster > clusters)
			break;

		ret = ocfs2_read_block(osb, blkno, &backup, 0, NULL);
		if (ret < 0) {
			mlog_errno(ret);
			break;
		}

		memcpy(backup->b_data, data, inode->i_sb->s_blocksize);

		backup_di = (struct ocfs2_dinode *)backup->b_data;
		backup_di->i_blkno = cpu_to_le64(blkno);

		ret = ocfs2_write_super_or_backup(osb, backup);
		brelse(backup);
		backup = NULL;
		if (ret < 0) {
			mlog_errno(ret);
			break;
		}
	}

	return ret;
}

static void ocfs2_update_super_and_backups(struct inode *inode,
					   int new_clusters)
{
	int ret;
	u32 clusters = 0;
	struct buffer_head *super_bh = NULL;
	struct ocfs2_dinode *super_di = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	/*
	 * update the superblock last.
	 * It doesn't matter if the write failed.
	 */
	ret = ocfs2_read_block(osb, OCFS2_SUPER_BLOCK_BLKNO,
			       &super_bh, 0, NULL);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	super_di = (struct ocfs2_dinode *)super_bh->b_data;
	le32_add_cpu(&super_di->i_clusters, new_clusters);
	clusters = le32_to_cpu(super_di->i_clusters);

	ret = ocfs2_write_super_or_backup(osb, super_bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	if (OCFS2_HAS_COMPAT_FEATURE(osb->sb, OCFS2_FEATURE_COMPAT_BACKUP_SB))
		ret = update_backups(inode, clusters, super_bh->b_data);

out:
	brelse(super_bh);
	if (ret)
		printk(KERN_WARNING "ocfs2: Failed to update super blocks on %s"
			" during fs resize. This condition is not fatal,"
			" but fsck.ocfs2 should be run to fix it\n",
			osb->dev_str);
	return;
}

/*
 * Extend the filesystem to the new number of clusters specified.  This entry
 * point is only used to extend the current filesystem to the end of the last
 * existing group.
 */
int ocfs2_group_extend(struct inode * inode, int new_clusters)
{
	int ret;
	handle_t *handle;
	struct buffer_head *main_bm_bh = NULL;
	struct buffer_head *group_bh = NULL;
	struct inode *main_bm_inode = NULL;
	struct ocfs2_dinode *fe = NULL;
	struct ocfs2_group_desc *group = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	u16 cl_bpc;
	u32 first_new_cluster;
	u64 lgd_blkno;

	mlog_entry_void();

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	if (new_clusters < 0)
		return -EINVAL;
	else if (new_clusters == 0)
		return 0;

	main_bm_inode = ocfs2_get_system_file_inode(osb,
						    GLOBAL_BITMAP_SYSTEM_INODE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_inode) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto out;
	}

	mutex_lock(&main_bm_inode->i_mutex);

	ret = ocfs2_inode_lock(main_bm_inode, &main_bm_bh, 1);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_mutex;
	}

	fe = (struct ocfs2_dinode *)main_bm_bh->b_data;

	if (le16_to_cpu(fe->id2.i_chain.cl_cpg) !=
				 ocfs2_group_bitmap_size(osb->sb) * 8) {
		mlog(ML_ERROR, "The disk is too old and small. "
		     "Force to do offline resize.");
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(main_bm_inode->i_sb, fe);
		ret = -EIO;
		goto out_unlock;
	}

	first_new_cluster = le32_to_cpu(fe->i_clusters);
	lgd_blkno = ocfs2_which_cluster_group(main_bm_inode,
					      first_new_cluster - 1);

	ret = ocfs2_read_block(osb, lgd_blkno, &group_bh, OCFS2_BH_CACHED,
			       main_bm_inode);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_unlock;
	}

	group = (struct ocfs2_group_desc *)group_bh->b_data;

	ret = ocfs2_check_group_descriptor(inode->i_sb, fe, group);
	if (ret) {
		mlog_errno(ret);
		goto out_unlock;
	}

	cl_bpc = le16_to_cpu(fe->id2.i_chain.cl_bpc);
	if (le16_to_cpu(group->bg_bits) / cl_bpc + new_clusters >
		le16_to_cpu(fe->id2.i_chain.cl_cpg)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	mlog(0, "extend the last group at %llu, new clusters = %d\n",
	     (unsigned long long)le64_to_cpu(group->bg_blkno), new_clusters);

	handle = ocfs2_start_trans(osb, OCFS2_GROUP_EXTEND_CREDITS);
	if (IS_ERR(handle)) {
		mlog_errno(PTR_ERR(handle));
		ret = -EINVAL;
		goto out_unlock;
	}

	/* update the last group descriptor and inode. */
	ret = ocfs2_update_last_group_and_inode(handle, main_bm_inode,
						main_bm_bh, group_bh,
						first_new_cluster,
						new_clusters);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ocfs2_update_super_and_backups(main_bm_inode, new_clusters);

out_commit:
	ocfs2_commit_trans(osb, handle);
out_unlock:
	brelse(group_bh);
	brelse(main_bm_bh);

	ocfs2_inode_unlock(main_bm_inode, 1);

out_mutex:
	mutex_unlock(&main_bm_inode->i_mutex);
	iput(main_bm_inode);

out:
	mlog_exit_void();
	return ret;
}

static int ocfs2_check_new_group(struct inode *inode,
				 struct ocfs2_dinode *di,
				 struct ocfs2_new_group_input *input,
				 struct buffer_head *group_bh)
{
	int ret;
	struct ocfs2_group_desc *gd;
	u16 cl_bpc = le16_to_cpu(di->id2.i_chain.cl_bpc);
	unsigned int max_bits = le16_to_cpu(di->id2.i_chain.cl_cpg) *
				le16_to_cpu(di->id2.i_chain.cl_bpc);


	gd = (struct ocfs2_group_desc *)group_bh->b_data;

	ret = -EIO;
	if (!OCFS2_IS_VALID_GROUP_DESC(gd))
		mlog(ML_ERROR, "Group descriptor # %llu isn't valid.\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkno));
	else if (di->i_blkno != gd->bg_parent_dinode)
		mlog(ML_ERROR, "Group descriptor # %llu has bad parent "
		     "pointer (%llu, expected %llu)\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkno),
		     (unsigned long long)le64_to_cpu(gd->bg_parent_dinode),
		     (unsigned long long)le64_to_cpu(di->i_blkno));
	else if (le16_to_cpu(gd->bg_bits) > max_bits)
		mlog(ML_ERROR, "Group descriptor # %llu has bit count of %u\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkno),
		     le16_to_cpu(gd->bg_bits));
	else if (le16_to_cpu(gd->bg_free_bits_count) > le16_to_cpu(gd->bg_bits))
		mlog(ML_ERROR, "Group descriptor # %llu has bit count %u but "
		     "claims that %u are free\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkno),
		     le16_to_cpu(gd->bg_bits),
		     le16_to_cpu(gd->bg_free_bits_count));
	else if (le16_to_cpu(gd->bg_bits) > (8 * le16_to_cpu(gd->bg_size)))
		mlog(ML_ERROR, "Group descriptor # %llu has bit count %u but "
		     "max bitmap bits of %u\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkno),
		     le16_to_cpu(gd->bg_bits),
		     8 * le16_to_cpu(gd->bg_size));
	else if (le16_to_cpu(gd->bg_chain) != input->chain)
		mlog(ML_ERROR, "Group descriptor # %llu has bad chain %u "
		     "while input has %u set.\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkno),
		     le16_to_cpu(gd->bg_chain), input->chain);
	else if (le16_to_cpu(gd->bg_bits) != input->clusters * cl_bpc)
		mlog(ML_ERROR, "Group descriptor # %llu has bit count %u but "
		     "input has %u clusters set\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkno),
		     le16_to_cpu(gd->bg_bits), input->clusters);
	else if (le16_to_cpu(gd->bg_free_bits_count) != input->frees * cl_bpc)
		mlog(ML_ERROR, "Group descriptor # %llu has free bit count %u "
		     "but it should have %u set\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkno),
		     le16_to_cpu(gd->bg_bits),
		     input->frees * cl_bpc);
	else
		ret = 0;

	return ret;
}

static int ocfs2_verify_group_and_input(struct inode *inode,
					struct ocfs2_dinode *di,
					struct ocfs2_new_group_input *input,
					struct buffer_head *group_bh)
{
	u16 cl_count = le16_to_cpu(di->id2.i_chain.cl_count);
	u16 cl_cpg = le16_to_cpu(di->id2.i_chain.cl_cpg);
	u16 next_free = le16_to_cpu(di->id2.i_chain.cl_next_free_rec);
	u32 cluster = ocfs2_blocks_to_clusters(inode->i_sb, input->group);
	u32 total_clusters = le32_to_cpu(di->i_clusters);
	int ret = -EINVAL;

	if (cluster < total_clusters)
		mlog(ML_ERROR, "add a group which is in the current volume.\n");
	else if (input->chain >= cl_count)
		mlog(ML_ERROR, "input chain exceeds the limit.\n");
	else if (next_free != cl_count && next_free != input->chain)
		mlog(ML_ERROR,
		     "the add group should be in chain %u\n", next_free);
	else if (total_clusters + input->clusters < total_clusters)
		mlog(ML_ERROR, "add group's clusters overflow.\n");
	else if (input->clusters > cl_cpg)
		mlog(ML_ERROR, "the cluster exceeds the maximum of a group\n");
	else if (input->frees > input->clusters)
		mlog(ML_ERROR, "the free cluster exceeds the total clusters\n");
	else if (total_clusters % cl_cpg != 0)
		mlog(ML_ERROR,
		     "the last group isn't full. Use group extend first.\n");
	else if (input->group != ocfs2_which_cluster_group(inode, cluster))
		mlog(ML_ERROR, "group blkno is invalid\n");
	else if ((ret = ocfs2_check_new_group(inode, di, input, group_bh)))
		mlog(ML_ERROR, "group descriptor check failed.\n");
	else
		ret = 0;

	return ret;
}

/* Add a new group descriptor to global_bitmap. */
int ocfs2_group_add(struct inode *inode, struct ocfs2_new_group_input *input)
{
	int ret;
	handle_t *handle;
	struct buffer_head *main_bm_bh = NULL;
	struct inode *main_bm_inode = NULL;
	struct ocfs2_dinode *fe = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *group_bh = NULL;
	struct ocfs2_group_desc *group = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	u16 cl_bpc;

	mlog_entry_void();

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	main_bm_inode = ocfs2_get_system_file_inode(osb,
						    GLOBAL_BITMAP_SYSTEM_INODE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_inode) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto out;
	}

	mutex_lock(&main_bm_inode->i_mutex);

	ret = ocfs2_inode_lock(main_bm_inode, &main_bm_bh, 1);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_mutex;
	}

	fe = (struct ocfs2_dinode *)main_bm_bh->b_data;

	if (le16_to_cpu(fe->id2.i_chain.cl_cpg) !=
				 ocfs2_group_bitmap_size(osb->sb) * 8) {
		mlog(ML_ERROR, "The disk is too old and small."
		     " Force to do offline resize.");
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = ocfs2_read_block(osb, input->group, &group_bh, 0, NULL);
	if (ret < 0) {
		mlog(ML_ERROR, "Can't read the group descriptor # %llu "
		     "from the device.", (unsigned long long)input->group);
		goto out_unlock;
	}

	ocfs2_set_new_buffer_uptodate(inode, group_bh);

	ret = ocfs2_verify_group_and_input(main_bm_inode, fe, input, group_bh);
	if (ret) {
		mlog_errno(ret);
		goto out_unlock;
	}

	mlog(0, "Add a new group  %llu in chain = %u, length = %u\n",
	     (unsigned long long)input->group, input->chain, input->clusters);

	handle = ocfs2_start_trans(osb, OCFS2_GROUP_ADD_CREDITS);
	if (IS_ERR(handle)) {
		mlog_errno(PTR_ERR(handle));
		ret = -EINVAL;
		goto out_unlock;
	}

	cl_bpc = le16_to_cpu(fe->id2.i_chain.cl_bpc);
	cl = &fe->id2.i_chain;
	cr = &cl->cl_recs[input->chain];

	ret = ocfs2_journal_access(handle, main_bm_inode, group_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_commit;
	}

	group = (struct ocfs2_group_desc *)group_bh->b_data;
	group->bg_next_group = cr->c_blkno;

	ret = ocfs2_journal_dirty(handle, group_bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_journal_access(handle, main_bm_inode, main_bm_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_commit;
	}

	if (input->chain == le16_to_cpu(cl->cl_next_free_rec)) {
		le16_add_cpu(&cl->cl_next_free_rec, 1);
		memset(cr, 0, sizeof(struct ocfs2_chain_rec));
	}

	cr->c_blkno = le64_to_cpu(input->group);
	le32_add_cpu(&cr->c_total, input->clusters * cl_bpc);
	le32_add_cpu(&cr->c_free, input->frees * cl_bpc);

	le32_add_cpu(&fe->id1.bitmap1.i_total, input->clusters *cl_bpc);
	le32_add_cpu(&fe->id1.bitmap1.i_used,
		     (input->clusters - input->frees) * cl_bpc);
	le32_add_cpu(&fe->i_clusters, input->clusters);

	ocfs2_journal_dirty(handle, main_bm_bh);

	spin_lock(&OCFS2_I(main_bm_inode)->ip_lock);
	OCFS2_I(main_bm_inode)->ip_clusters = le32_to_cpu(fe->i_clusters);
	le64_add_cpu(&fe->i_size, input->clusters << osb->s_clustersize_bits);
	spin_unlock(&OCFS2_I(main_bm_inode)->ip_lock);
	i_size_write(main_bm_inode, le64_to_cpu(fe->i_size));

	ocfs2_update_super_and_backups(main_bm_inode, input->clusters);

out_commit:
	ocfs2_commit_trans(osb, handle);
out_unlock:
	brelse(group_bh);
	brelse(main_bm_bh);

	ocfs2_inode_unlock(main_bm_inode, 1);

out_mutex:
	mutex_unlock(&main_bm_inode->i_mutex);
	iput(main_bm_inode);

out:
	mlog_exit_void();
	return ret;
}
