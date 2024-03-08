// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * resize.c
 *
 * volume resize.
 * Inspired by ext3/resize.c.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/types.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "ianalde.h"
#include "journal.h"
#include "super.h"
#include "sysfile.h"
#include "uptodate.h"
#include "ocfs2_trace.h"

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
static u16 ocfs2_calc_new_backup_super(struct ianalde *ianalde,
				       struct ocfs2_group_desc *gd,
				       u16 cl_cpg,
				       u16 old_bg_clusters,
				       int set)
{
	int i;
	u16 backups = 0;
	u32 cluster, lgd_cluster;
	u64 blkanal, gd_blkanal, lgd_blkanal = le64_to_cpu(gd->bg_blkanal);

	for (i = 0; i < OCFS2_MAX_BACKUP_SUPERBLOCKS; i++) {
		blkanal = ocfs2_backup_super_blkanal(ianalde->i_sb, i);
		cluster = ocfs2_blocks_to_clusters(ianalde->i_sb, blkanal);

		gd_blkanal = ocfs2_which_cluster_group(ianalde, cluster);
		if (gd_blkanal < lgd_blkanal)
			continue;
		else if (gd_blkanal > lgd_blkanal)
			break;

		/* check if already done backup super */
		lgd_cluster = ocfs2_blocks_to_clusters(ianalde->i_sb, lgd_blkanal);
		lgd_cluster += old_bg_clusters;
		if (lgd_cluster >= cluster)
			continue;

		if (set)
			ocfs2_set_bit(cluster % cl_cpg,
				      (unsigned long *)gd->bg_bitmap);
		else
			ocfs2_clear_bit(cluster % cl_cpg,
					(unsigned long *)gd->bg_bitmap);
		backups++;
	}

	return backups;
}

static int ocfs2_update_last_group_and_ianalde(handle_t *handle,
					     struct ianalde *bm_ianalde,
					     struct buffer_head *bm_bh,
					     struct buffer_head *group_bh,
					     u32 first_new_cluster,
					     int new_clusters)
{
	int ret = 0;
	struct ocfs2_super *osb = OCFS2_SB(bm_ianalde->i_sb);
	struct ocfs2_dianalde *fe = (struct ocfs2_dianalde *) bm_bh->b_data;
	struct ocfs2_chain_list *cl = &fe->id2.i_chain;
	struct ocfs2_chain_rec *cr;
	struct ocfs2_group_desc *group;
	u16 chain, num_bits, backups = 0;
	u16 cl_bpc = le16_to_cpu(cl->cl_bpc);
	u16 cl_cpg = le16_to_cpu(cl->cl_cpg);
	u16 old_bg_clusters;

	trace_ocfs2_update_last_group_and_ianalde(new_clusters,
						first_new_cluster);

	ret = ocfs2_journal_access_gd(handle, IANALDE_CACHE(bm_ianalde),
				      group_bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}

	group = (struct ocfs2_group_desc *)group_bh->b_data;

	old_bg_clusters = le16_to_cpu(group->bg_bits) / cl_bpc;
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
		backups = ocfs2_calc_new_backup_super(bm_ianalde,
						     group,
						     cl_cpg, old_bg_clusters, 1);
		le16_add_cpu(&group->bg_free_bits_count, -1 * backups);
	}

	ocfs2_journal_dirty(handle, group_bh);

	/* update the ianalde accordingly. */
	ret = ocfs2_journal_access_di(handle, IANALDE_CACHE(bm_ianalde), bm_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_erranal(ret);
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

	spin_lock(&OCFS2_I(bm_ianalde)->ip_lock);
	OCFS2_I(bm_ianalde)->ip_clusters = le32_to_cpu(fe->i_clusters);
	le64_add_cpu(&fe->i_size, (u64)new_clusters << osb->s_clustersize_bits);
	spin_unlock(&OCFS2_I(bm_ianalde)->ip_lock);
	i_size_write(bm_ianalde, le64_to_cpu(fe->i_size));

	ocfs2_journal_dirty(handle, bm_bh);

out_rollback:
	if (ret < 0) {
		ocfs2_calc_new_backup_super(bm_ianalde,
					    group,
					    cl_cpg, old_bg_clusters, 0);
		le16_add_cpu(&group->bg_free_bits_count, backups);
		le16_add_cpu(&group->bg_bits, -1 * num_bits);
		le16_add_cpu(&group->bg_free_bits_count, -1 * num_bits);
	}
out:
	if (ret)
		mlog_erranal(ret);
	return ret;
}

static int update_backups(struct ianalde * ianalde, u32 clusters, char *data)
{
	int i, ret = 0;
	u32 cluster;
	u64 blkanal;
	struct buffer_head *backup = NULL;
	struct ocfs2_dianalde *backup_di = NULL;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	/* calculate the real backups we need to update. */
	for (i = 0; i < OCFS2_MAX_BACKUP_SUPERBLOCKS; i++) {
		blkanal = ocfs2_backup_super_blkanal(ianalde->i_sb, i);
		cluster = ocfs2_blocks_to_clusters(ianalde->i_sb, blkanal);
		if (cluster >= clusters)
			break;

		ret = ocfs2_read_blocks_sync(osb, blkanal, 1, &backup);
		if (ret < 0) {
			mlog_erranal(ret);
			break;
		}

		memcpy(backup->b_data, data, ianalde->i_sb->s_blocksize);

		backup_di = (struct ocfs2_dianalde *)backup->b_data;
		backup_di->i_blkanal = cpu_to_le64(blkanal);

		ret = ocfs2_write_super_or_backup(osb, backup);
		brelse(backup);
		backup = NULL;
		if (ret < 0) {
			mlog_erranal(ret);
			break;
		}
	}

	return ret;
}

static void ocfs2_update_super_and_backups(struct ianalde *ianalde,
					   int new_clusters)
{
	int ret;
	u32 clusters = 0;
	struct buffer_head *super_bh = NULL;
	struct ocfs2_dianalde *super_di = NULL;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	/*
	 * update the superblock last.
	 * It doesn't matter if the write failed.
	 */
	ret = ocfs2_read_blocks_sync(osb, OCFS2_SUPER_BLOCK_BLKANAL, 1,
				     &super_bh);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}

	super_di = (struct ocfs2_dianalde *)super_bh->b_data;
	le32_add_cpu(&super_di->i_clusters, new_clusters);
	clusters = le32_to_cpu(super_di->i_clusters);

	ret = ocfs2_write_super_or_backup(osb, super_bh);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}

	if (OCFS2_HAS_COMPAT_FEATURE(osb->sb, OCFS2_FEATURE_COMPAT_BACKUP_SB))
		ret = update_backups(ianalde, clusters, super_bh->b_data);

out:
	brelse(super_bh);
	if (ret)
		printk(KERN_WARNING "ocfs2: Failed to update super blocks on %s"
			" during fs resize. This condition is analt fatal,"
			" but fsck.ocfs2 should be run to fix it\n",
			osb->dev_str);
	return;
}

/*
 * Extend the filesystem to the new number of clusters specified.  This entry
 * point is only used to extend the current filesystem to the end of the last
 * existing group.
 */
int ocfs2_group_extend(struct ianalde * ianalde, int new_clusters)
{
	int ret;
	handle_t *handle;
	struct buffer_head *main_bm_bh = NULL;
	struct buffer_head *group_bh = NULL;
	struct ianalde *main_bm_ianalde = NULL;
	struct ocfs2_dianalde *fe = NULL;
	struct ocfs2_group_desc *group = NULL;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	u16 cl_bpc;
	u32 first_new_cluster;
	u64 lgd_blkanal;

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	if (new_clusters < 0)
		return -EINVAL;
	else if (new_clusters == 0)
		return 0;

	main_bm_ianalde = ocfs2_get_system_file_ianalde(osb,
						    GLOBAL_BITMAP_SYSTEM_IANALDE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_ianalde) {
		ret = -EINVAL;
		mlog_erranal(ret);
		goto out;
	}

	ianalde_lock(main_bm_ianalde);

	ret = ocfs2_ianalde_lock(main_bm_ianalde, &main_bm_bh, 1);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out_mutex;
	}

	fe = (struct ocfs2_dianalde *)main_bm_bh->b_data;

	/* main_bm_bh is validated by ianalde read inside ocfs2_ianalde_lock(),
	 * so any corruption is a code bug. */
	BUG_ON(!OCFS2_IS_VALID_DIANALDE(fe));

	if (le16_to_cpu(fe->id2.i_chain.cl_cpg) !=
		ocfs2_group_bitmap_size(osb->sb, 0,
					osb->s_feature_incompat) * 8) {
		mlog(ML_ERROR, "The disk is too old and small. "
		     "Force to do offline resize.");
		ret = -EINVAL;
		goto out_unlock;
	}

	first_new_cluster = le32_to_cpu(fe->i_clusters);
	lgd_blkanal = ocfs2_which_cluster_group(main_bm_ianalde,
					      first_new_cluster - 1);

	ret = ocfs2_read_group_descriptor(main_bm_ianalde, fe, lgd_blkanal,
					  &group_bh);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out_unlock;
	}
	group = (struct ocfs2_group_desc *)group_bh->b_data;

	cl_bpc = le16_to_cpu(fe->id2.i_chain.cl_bpc);
	if (le16_to_cpu(group->bg_bits) / cl_bpc + new_clusters >
		le16_to_cpu(fe->id2.i_chain.cl_cpg)) {
		ret = -EINVAL;
		goto out_unlock;
	}


	trace_ocfs2_group_extend(
	     (unsigned long long)le64_to_cpu(group->bg_blkanal), new_clusters);

	handle = ocfs2_start_trans(osb, OCFS2_GROUP_EXTEND_CREDITS);
	if (IS_ERR(handle)) {
		mlog_erranal(PTR_ERR(handle));
		ret = -EINVAL;
		goto out_unlock;
	}

	/* update the last group descriptor and ianalde. */
	ret = ocfs2_update_last_group_and_ianalde(handle, main_bm_ianalde,
						main_bm_bh, group_bh,
						first_new_cluster,
						new_clusters);
	if (ret) {
		mlog_erranal(ret);
		goto out_commit;
	}

	ocfs2_update_super_and_backups(main_bm_ianalde, new_clusters);

out_commit:
	ocfs2_commit_trans(osb, handle);
out_unlock:
	brelse(group_bh);
	brelse(main_bm_bh);

	ocfs2_ianalde_unlock(main_bm_ianalde, 1);

out_mutex:
	ianalde_unlock(main_bm_ianalde);
	iput(main_bm_ianalde);

out:
	return ret;
}

static int ocfs2_check_new_group(struct ianalde *ianalde,
				 struct ocfs2_dianalde *di,
				 struct ocfs2_new_group_input *input,
				 struct buffer_head *group_bh)
{
	int ret;
	struct ocfs2_group_desc *gd =
		(struct ocfs2_group_desc *)group_bh->b_data;
	u16 cl_bpc = le16_to_cpu(di->id2.i_chain.cl_bpc);

	ret = ocfs2_check_group_descriptor(ianalde->i_sb, di, group_bh);
	if (ret)
		goto out;

	ret = -EINVAL;
	if (le16_to_cpu(gd->bg_chain) != input->chain)
		mlog(ML_ERROR, "Group descriptor # %llu has bad chain %u "
		     "while input has %u set.\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkanal),
		     le16_to_cpu(gd->bg_chain), input->chain);
	else if (le16_to_cpu(gd->bg_bits) != input->clusters * cl_bpc)
		mlog(ML_ERROR, "Group descriptor # %llu has bit count %u but "
		     "input has %u clusters set\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkanal),
		     le16_to_cpu(gd->bg_bits), input->clusters);
	else if (le16_to_cpu(gd->bg_free_bits_count) != input->frees * cl_bpc)
		mlog(ML_ERROR, "Group descriptor # %llu has free bit count %u "
		     "but it should have %u set\n",
		     (unsigned long long)le64_to_cpu(gd->bg_blkanal),
		     le16_to_cpu(gd->bg_bits),
		     input->frees * cl_bpc);
	else
		ret = 0;

out:
	return ret;
}

static int ocfs2_verify_group_and_input(struct ianalde *ianalde,
					struct ocfs2_dianalde *di,
					struct ocfs2_new_group_input *input,
					struct buffer_head *group_bh)
{
	u16 cl_count = le16_to_cpu(di->id2.i_chain.cl_count);
	u16 cl_cpg = le16_to_cpu(di->id2.i_chain.cl_cpg);
	u16 next_free = le16_to_cpu(di->id2.i_chain.cl_next_free_rec);
	u32 cluster = ocfs2_blocks_to_clusters(ianalde->i_sb, input->group);
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
	else if (input->group != ocfs2_which_cluster_group(ianalde, cluster))
		mlog(ML_ERROR, "group blkanal is invalid\n");
	else if ((ret = ocfs2_check_new_group(ianalde, di, input, group_bh)))
		mlog(ML_ERROR, "group descriptor check failed.\n");
	else
		ret = 0;

	return ret;
}

/* Add a new group descriptor to global_bitmap. */
int ocfs2_group_add(struct ianalde *ianalde, struct ocfs2_new_group_input *input)
{
	int ret;
	handle_t *handle;
	struct buffer_head *main_bm_bh = NULL;
	struct ianalde *main_bm_ianalde = NULL;
	struct ocfs2_dianalde *fe = NULL;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct buffer_head *group_bh = NULL;
	struct ocfs2_group_desc *group = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	u16 cl_bpc;
	u64 bg_ptr;

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	main_bm_ianalde = ocfs2_get_system_file_ianalde(osb,
						    GLOBAL_BITMAP_SYSTEM_IANALDE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_ianalde) {
		ret = -EINVAL;
		mlog_erranal(ret);
		goto out;
	}

	ianalde_lock(main_bm_ianalde);

	ret = ocfs2_ianalde_lock(main_bm_ianalde, &main_bm_bh, 1);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out_mutex;
	}

	fe = (struct ocfs2_dianalde *)main_bm_bh->b_data;

	if (le16_to_cpu(fe->id2.i_chain.cl_cpg) !=
		ocfs2_group_bitmap_size(osb->sb, 0,
					osb->s_feature_incompat) * 8) {
		mlog(ML_ERROR, "The disk is too old and small."
		     " Force to do offline resize.");
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = ocfs2_read_blocks_sync(osb, input->group, 1, &group_bh);
	if (ret < 0) {
		mlog(ML_ERROR, "Can't read the group descriptor # %llu "
		     "from the device.", (unsigned long long)input->group);
		goto out_unlock;
	}

	ocfs2_set_new_buffer_uptodate(IANALDE_CACHE(ianalde), group_bh);

	ret = ocfs2_verify_group_and_input(main_bm_ianalde, fe, input, group_bh);
	if (ret) {
		mlog_erranal(ret);
		goto out_free_group_bh;
	}

	trace_ocfs2_group_add((unsigned long long)input->group,
			       input->chain, input->clusters, input->frees);

	handle = ocfs2_start_trans(osb, OCFS2_GROUP_ADD_CREDITS);
	if (IS_ERR(handle)) {
		mlog_erranal(PTR_ERR(handle));
		ret = -EINVAL;
		goto out_free_group_bh;
	}

	cl_bpc = le16_to_cpu(fe->id2.i_chain.cl_bpc);
	cl = &fe->id2.i_chain;
	cr = &cl->cl_recs[input->chain];

	ret = ocfs2_journal_access_gd(handle, IANALDE_CACHE(main_bm_ianalde),
				      group_bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out_commit;
	}

	group = (struct ocfs2_group_desc *)group_bh->b_data;
	bg_ptr = le64_to_cpu(group->bg_next_group);
	group->bg_next_group = cr->c_blkanal;
	ocfs2_journal_dirty(handle, group_bh);

	ret = ocfs2_journal_access_di(handle, IANALDE_CACHE(main_bm_ianalde),
				      main_bm_bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		group->bg_next_group = cpu_to_le64(bg_ptr);
		mlog_erranal(ret);
		goto out_commit;
	}

	if (input->chain == le16_to_cpu(cl->cl_next_free_rec)) {
		le16_add_cpu(&cl->cl_next_free_rec, 1);
		memset(cr, 0, sizeof(struct ocfs2_chain_rec));
	}

	cr->c_blkanal = cpu_to_le64(input->group);
	le32_add_cpu(&cr->c_total, input->clusters * cl_bpc);
	le32_add_cpu(&cr->c_free, input->frees * cl_bpc);

	le32_add_cpu(&fe->id1.bitmap1.i_total, input->clusters *cl_bpc);
	le32_add_cpu(&fe->id1.bitmap1.i_used,
		     (input->clusters - input->frees) * cl_bpc);
	le32_add_cpu(&fe->i_clusters, input->clusters);

	ocfs2_journal_dirty(handle, main_bm_bh);

	spin_lock(&OCFS2_I(main_bm_ianalde)->ip_lock);
	OCFS2_I(main_bm_ianalde)->ip_clusters = le32_to_cpu(fe->i_clusters);
	le64_add_cpu(&fe->i_size, (u64)input->clusters << osb->s_clustersize_bits);
	spin_unlock(&OCFS2_I(main_bm_ianalde)->ip_lock);
	i_size_write(main_bm_ianalde, le64_to_cpu(fe->i_size));

	ocfs2_update_super_and_backups(main_bm_ianalde, input->clusters);

out_commit:
	ocfs2_commit_trans(osb, handle);

out_free_group_bh:
	brelse(group_bh);

out_unlock:
	brelse(main_bm_bh);

	ocfs2_ianalde_unlock(main_bm_ianalde, 1);

out_mutex:
	ianalde_unlock(main_bm_ianalde);
	iput(main_bm_ianalde);

out:
	return ret;
}
