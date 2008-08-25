/*
 *  Implementation of operations over local quota file
 */

#include <linux/fs.h>
#include <linux/quota.h>
#include <linux/quotaops.h>
#include <linux/module.h>

#define MLOG_MASK_PREFIX ML_QUOTA
#include <cluster/masklog.h>

#include "ocfs2_fs.h"
#include "ocfs2.h"
#include "inode.h"
#include "alloc.h"
#include "file.h"
#include "buffer_head_io.h"
#include "journal.h"
#include "sysfile.h"
#include "dlmglue.h"
#include "quota.h"

/* Number of local quota structures per block */
static inline unsigned int ol_quota_entries_per_block(struct super_block *sb)
{
	return ((sb->s_blocksize - OCFS2_QBLK_RESERVED_SPACE) /
		sizeof(struct ocfs2_local_disk_dqblk));
}

/* Number of blocks with entries in one chunk */
static inline unsigned int ol_chunk_blocks(struct super_block *sb)
{
	return ((sb->s_blocksize - sizeof(struct ocfs2_local_disk_chunk) -
		 OCFS2_QBLK_RESERVED_SPACE) << 3) /
	       ol_quota_entries_per_block(sb);
}

/* Number of entries in a chunk bitmap */
static unsigned int ol_chunk_entries(struct super_block *sb)
{
	return ol_chunk_blocks(sb) * ol_quota_entries_per_block(sb);
}

/* Offset of the chunk in quota file */
static unsigned int ol_quota_chunk_block(struct super_block *sb, int c)
{
	/* 1 block for local quota file info, 1 block per chunk for chunk info */
	return 1 + (ol_chunk_blocks(sb) + 1) * c;
}

/* Offset of the dquot structure in the quota file */
static loff_t ol_dqblk_off(struct super_block *sb, int c, int off)
{
	int epb = ol_quota_entries_per_block(sb);

	return ((ol_quota_chunk_block(sb, c) + 1 + off / epb)
		<< sb->s_blocksize_bits) +
		(off % epb) * sizeof(struct ocfs2_local_disk_dqblk);
}

/* Compute block number from given offset */
static inline unsigned int ol_dqblk_file_block(struct super_block *sb, loff_t off)
{
	return off >> sb->s_blocksize_bits;
}

static inline unsigned int ol_dqblk_block_offset(struct super_block *sb, loff_t off)
{
	return off & ((1 << sb->s_blocksize_bits) - 1);
}

/* Compute offset in the chunk of a structure with the given offset */
static int ol_dqblk_chunk_off(struct super_block *sb, int c, loff_t off)
{
	int epb = ol_quota_entries_per_block(sb);

	return ((off >> sb->s_blocksize_bits) -
			ol_quota_chunk_block(sb, c) - 1) * epb
	       + ((unsigned int)(off & ((1 << sb->s_blocksize_bits) - 1))) /
		 sizeof(struct ocfs2_local_disk_dqblk);
}

/* Write bufferhead into the fs */
static int ocfs2_modify_bh(struct inode *inode, struct buffer_head *bh,
		void (*modify)(struct buffer_head *, void *), void *private)
{
	struct super_block *sb = inode->i_sb;
	handle_t *handle;
	int status;

	handle = ocfs2_start_trans(OCFS2_SB(sb), 1);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		return status;
	}
	status = ocfs2_journal_access(handle, inode, bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		ocfs2_commit_trans(OCFS2_SB(sb), handle);
		return status;
	}
	lock_buffer(bh);
	modify(bh, private);
	unlock_buffer(bh);
	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0) {
		mlog_errno(status);
		ocfs2_commit_trans(OCFS2_SB(sb), handle);
		return status;
	}
	status = ocfs2_commit_trans(OCFS2_SB(sb), handle);
	if (status < 0) {
		mlog_errno(status);
		return status;
	}
	return 0;
}

/* Check whether we understand format of quota files */
static int ocfs2_local_check_quota_file(struct super_block *sb, int type)
{
	unsigned int lmagics[MAXQUOTAS] = OCFS2_LOCAL_QMAGICS;
	unsigned int lversions[MAXQUOTAS] = OCFS2_LOCAL_QVERSIONS;
	unsigned int gmagics[MAXQUOTAS] = OCFS2_GLOBAL_QMAGICS;
	unsigned int gversions[MAXQUOTAS] = OCFS2_GLOBAL_QVERSIONS;
	unsigned int ino[MAXQUOTAS] = { USER_QUOTA_SYSTEM_INODE,
					GROUP_QUOTA_SYSTEM_INODE };
	struct buffer_head *bh;
	struct inode *linode = sb_dqopt(sb)->files[type];
	struct inode *ginode = NULL;
	struct ocfs2_disk_dqheader *dqhead;
	int status, ret = 0;

	/* First check whether we understand local quota file */
	bh = ocfs2_read_quota_block(linode, 0, &status);
	if (!bh) {
		mlog_errno(status);
		mlog(ML_ERROR, "failed to read quota file header (type=%d)\n",
			type);
		goto out_err;
	}
	dqhead = (struct ocfs2_disk_dqheader *)(bh->b_data);
	if (le32_to_cpu(dqhead->dqh_magic) != lmagics[type]) {
		mlog(ML_ERROR, "quota file magic does not match (%u != %u),"
			" type=%d\n", le32_to_cpu(dqhead->dqh_magic),
			lmagics[type], type);
		goto out_err;
	}
	if (le32_to_cpu(dqhead->dqh_version) != lversions[type]) {
		mlog(ML_ERROR, "quota file version does not match (%u != %u),"
			" type=%d\n", le32_to_cpu(dqhead->dqh_version),
			lversions[type], type);
		goto out_err;
	}
	brelse(bh);
	bh = NULL;

	/* Next check whether we understand global quota file */
	ginode = ocfs2_get_system_file_inode(OCFS2_SB(sb), ino[type],
						OCFS2_INVALID_SLOT);
	if (!ginode) {
		mlog(ML_ERROR, "cannot get global quota file inode "
				"(type=%d)\n", type);
		goto out_err;
	}
	/* Since the header is read only, we don't care about locking */
	bh = ocfs2_read_quota_block(ginode, 0, &status);
	if (!bh) {
		mlog_errno(status);
		mlog(ML_ERROR, "failed to read global quota file header "
				"(type=%d)\n", type);
		goto out_err;
	}
	dqhead = (struct ocfs2_disk_dqheader *)(bh->b_data);
	if (le32_to_cpu(dqhead->dqh_magic) != gmagics[type]) {
		mlog(ML_ERROR, "global quota file magic does not match "
			"(%u != %u), type=%d\n",
			le32_to_cpu(dqhead->dqh_magic), gmagics[type], type);
		goto out_err;
	}
	if (le32_to_cpu(dqhead->dqh_version) != gversions[type]) {
		mlog(ML_ERROR, "global quota file version does not match "
			"(%u != %u), type=%d\n",
			le32_to_cpu(dqhead->dqh_version), gversions[type],
			type);
		goto out_err;
	}

	ret = 1;
out_err:
	brelse(bh);
	iput(ginode);
	return ret;
}

/* Release given list of quota file chunks */
static void ocfs2_release_local_quota_bitmaps(struct list_head *head)
{
	struct ocfs2_quota_chunk *pos, *next;

	list_for_each_entry_safe(pos, next, head, qc_chunk) {
		list_del(&pos->qc_chunk);
		brelse(pos->qc_headerbh);
		kmem_cache_free(ocfs2_qf_chunk_cachep, pos);
	}
}

/* Load quota bitmaps into memory */
static int ocfs2_load_local_quota_bitmaps(struct inode *inode,
			struct ocfs2_local_disk_dqinfo *ldinfo,
			struct list_head *head)
{
	struct ocfs2_quota_chunk *newchunk;
	int i, status;

	INIT_LIST_HEAD(head);
	for (i = 0; i < le32_to_cpu(ldinfo->dqi_chunks); i++) {
		newchunk = kmem_cache_alloc(ocfs2_qf_chunk_cachep, GFP_NOFS);
		if (!newchunk) {
			ocfs2_release_local_quota_bitmaps(head);
			return -ENOMEM;
		}
		newchunk->qc_num = i;
		newchunk->qc_headerbh = ocfs2_read_quota_block(inode,
				ol_quota_chunk_block(inode->i_sb, i),
				&status);
		if (!newchunk->qc_headerbh) {
			mlog_errno(status);
			kmem_cache_free(ocfs2_qf_chunk_cachep, newchunk);
			ocfs2_release_local_quota_bitmaps(head);
			return status;
		}
		list_add_tail(&newchunk->qc_chunk, head);
	}
	return 0;
}

static void olq_update_info(struct buffer_head *bh, void *private)
{
	struct mem_dqinfo *info = private;
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	struct ocfs2_local_disk_dqinfo *ldinfo;

	ldinfo = (struct ocfs2_local_disk_dqinfo *)(bh->b_data +
						OCFS2_LOCAL_INFO_OFF);
	spin_lock(&dq_data_lock);
	ldinfo->dqi_flags = cpu_to_le32(info->dqi_flags & DQF_MASK);
	ldinfo->dqi_chunks = cpu_to_le32(oinfo->dqi_chunks);
	ldinfo->dqi_blocks = cpu_to_le32(oinfo->dqi_blocks);
	spin_unlock(&dq_data_lock);
}

/* Read information header from quota file */
static int ocfs2_local_read_info(struct super_block *sb, int type)
{
	struct ocfs2_local_disk_dqinfo *ldinfo;
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo;
	struct inode *lqinode = sb_dqopt(sb)->files[type];
	int status;
	struct buffer_head *bh = NULL;
	int locked = 0;

	info->dqi_maxblimit = 0x7fffffffffffffffLL;
	info->dqi_maxilimit = 0x7fffffffffffffffLL;
	oinfo = kmalloc(sizeof(struct ocfs2_mem_dqinfo), GFP_NOFS);
	if (!oinfo) {
		mlog(ML_ERROR, "failed to allocate memory for ocfs2 quota"
			       " info.");
		goto out_err;
	}
	info->dqi_priv = oinfo;
	oinfo->dqi_type = type;
	INIT_LIST_HEAD(&oinfo->dqi_chunk);
	oinfo->dqi_lqi_bh = NULL;
	oinfo->dqi_ibh = NULL;

	status = ocfs2_global_read_info(sb, type);
	if (status < 0)
		goto out_err;

	status = ocfs2_inode_lock(lqinode, &oinfo->dqi_lqi_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto out_err;
	}
	locked = 1;

	/* Now read local header */
	bh = ocfs2_read_quota_block(lqinode, 0, &status);
	if (!bh) {
		mlog_errno(status);
		mlog(ML_ERROR, "failed to read quota file info header "
			"(type=%d)\n", type);
		goto out_err;
	}
	ldinfo = (struct ocfs2_local_disk_dqinfo *)(bh->b_data +
						OCFS2_LOCAL_INFO_OFF);
	info->dqi_flags = le32_to_cpu(ldinfo->dqi_flags);
	oinfo->dqi_chunks = le32_to_cpu(ldinfo->dqi_chunks);
	oinfo->dqi_blocks = le32_to_cpu(ldinfo->dqi_blocks);
	oinfo->dqi_ibh = bh;

	/* We crashed when using local quota file? */
	if (!(info->dqi_flags & OLQF_CLEAN))
		goto out_err;	/* So far we just bail out. Later we should resync here */

	status = ocfs2_load_local_quota_bitmaps(sb_dqopt(sb)->files[type],
						ldinfo,
						&oinfo->dqi_chunk);
	if (status < 0) {
		mlog_errno(status);
		goto out_err;
	}

	/* Now mark quota file as used */
	info->dqi_flags &= ~OLQF_CLEAN;
	status = ocfs2_modify_bh(lqinode, bh, olq_update_info, info);
	if (status < 0) {
		mlog_errno(status);
		goto out_err;
	}

	return 0;
out_err:
	if (oinfo) {
		iput(oinfo->dqi_gqinode);
		ocfs2_simple_drop_lockres(OCFS2_SB(sb), &oinfo->dqi_gqlock);
		ocfs2_lock_res_free(&oinfo->dqi_gqlock);
		brelse(oinfo->dqi_lqi_bh);
		if (locked)
			ocfs2_inode_unlock(lqinode, 1);
		ocfs2_release_local_quota_bitmaps(&oinfo->dqi_chunk);
		kfree(oinfo);
	}
	brelse(bh);
	return -1;
}

/* Write local info to quota file */
static int ocfs2_local_write_info(struct super_block *sb, int type)
{
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct buffer_head *bh = ((struct ocfs2_mem_dqinfo *)info->dqi_priv)
						->dqi_ibh;
	int status;

	status = ocfs2_modify_bh(sb_dqopt(sb)->files[type], bh, olq_update_info,
				 info);
	if (status < 0) {
		mlog_errno(status);
		return -1;
	}

	return 0;
}

/* Release info from memory */
static int ocfs2_local_free_info(struct super_block *sb, int type)
{
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	struct ocfs2_quota_chunk *chunk;
	struct ocfs2_local_disk_chunk *dchunk;
	int mark_clean = 1, len;
	int status;

	iput(oinfo->dqi_gqinode);
	ocfs2_simple_drop_lockres(OCFS2_SB(sb), &oinfo->dqi_gqlock);
	ocfs2_lock_res_free(&oinfo->dqi_gqlock);
	list_for_each_entry(chunk, &oinfo->dqi_chunk, qc_chunk) {
		dchunk = (struct ocfs2_local_disk_chunk *)
					(chunk->qc_headerbh->b_data);
		if (chunk->qc_num < oinfo->dqi_chunks - 1) {
			len = ol_chunk_entries(sb);
		} else {
			len = (oinfo->dqi_blocks -
			       ol_quota_chunk_block(sb, chunk->qc_num) - 1)
			      * ol_quota_entries_per_block(sb);
		}
		/* Not all entries free? Bug! */
		if (le32_to_cpu(dchunk->dqc_free) != len) {
			mlog(ML_ERROR, "releasing quota file with used "
					"entries (type=%d)\n", type);
			mark_clean = 0;
		}
	}
	ocfs2_release_local_quota_bitmaps(&oinfo->dqi_chunk);

	if (!mark_clean)
		goto out;

	/* Mark local file as clean */
	info->dqi_flags |= OLQF_CLEAN;
	status = ocfs2_modify_bh(sb_dqopt(sb)->files[type],
				 oinfo->dqi_ibh,
				 olq_update_info,
				 info);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

out:
	ocfs2_inode_unlock(sb_dqopt(sb)->files[type], 1);
	brelse(oinfo->dqi_ibh);
	brelse(oinfo->dqi_lqi_bh);
	kfree(oinfo);
	return 0;
}

static void olq_set_dquot(struct buffer_head *bh, void *private)
{
	struct ocfs2_dquot *od = private;
	struct ocfs2_local_disk_dqblk *dqblk;
	struct super_block *sb = od->dq_dquot.dq_sb;

	dqblk = (struct ocfs2_local_disk_dqblk *)(bh->b_data
		+ ol_dqblk_block_offset(sb, od->dq_local_off));

	dqblk->dqb_id = cpu_to_le64(od->dq_dquot.dq_id);
	spin_lock(&dq_data_lock);
	dqblk->dqb_spacemod = cpu_to_le64(od->dq_dquot.dq_dqb.dqb_curspace -
					  od->dq_origspace);
	dqblk->dqb_inodemod = cpu_to_le64(od->dq_dquot.dq_dqb.dqb_curinodes -
					  od->dq_originodes);
	spin_unlock(&dq_data_lock);
	mlog(0, "Writing local dquot %u space %lld inodes %lld\n",
	     od->dq_dquot.dq_id, dqblk->dqb_spacemod, dqblk->dqb_inodemod);
}

/* Write dquot to local quota file */
static int ocfs2_local_write_dquot(struct dquot *dquot)
{
	struct super_block *sb = dquot->dq_sb;
	struct ocfs2_dquot *od = OCFS2_DQUOT(dquot);
	struct buffer_head *bh;
	int status;

	bh = ocfs2_read_quota_block(sb_dqopt(sb)->files[dquot->dq_type],
				    ol_dqblk_file_block(sb, od->dq_local_off),
				    &status);
	if (!bh) {
		mlog_errno(status);
		goto out;
	}
	status = ocfs2_modify_bh(sb_dqopt(sb)->files[dquot->dq_type], bh,
				 olq_set_dquot, od);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
out:
	brelse(bh);
	return status;
}

/* Find free entry in local quota file */
static struct ocfs2_quota_chunk *ocfs2_find_free_entry(struct super_block *sb,
						       int type,
						       int *offset)
{
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	struct ocfs2_quota_chunk *chunk;
	struct ocfs2_local_disk_chunk *dchunk;
	int found = 0, len;

	list_for_each_entry(chunk, &oinfo->dqi_chunk, qc_chunk) {
		dchunk = (struct ocfs2_local_disk_chunk *)
						chunk->qc_headerbh->b_data;
		if (le32_to_cpu(dchunk->dqc_free) > 0) {
			found = 1;
			break;
		}
	}
	if (!found)
		return NULL;

	if (chunk->qc_num < oinfo->dqi_chunks - 1) {
		len = ol_chunk_entries(sb);
	} else {
		len = (oinfo->dqi_blocks -
		       ol_quota_chunk_block(sb, chunk->qc_num) - 1)
		      * ol_quota_entries_per_block(sb);
	}

	found = ocfs2_find_next_zero_bit(dchunk->dqc_bitmap, len, 0);
	/* We failed? */
	if (found == len) {
		mlog(ML_ERROR, "Did not find empty entry in chunk %d with %u"
		     " entries free (type=%d)\n", chunk->qc_num,
		     le32_to_cpu(dchunk->dqc_free), type);
		return ERR_PTR(-EIO);
	}
	*offset = found;
	return chunk;
}

/* Add new chunk to the local quota file */
static struct ocfs2_quota_chunk *ocfs2_local_quota_add_chunk(
							struct super_block *sb,
							int type,
							int *offset)
{
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	struct inode *lqinode = sb_dqopt(sb)->files[type];
	struct ocfs2_quota_chunk *chunk = NULL;
	struct ocfs2_local_disk_chunk *dchunk;
	int status;
	handle_t *handle;
	struct buffer_head *bh = NULL;
	u64 p_blkno;

	/* We are protected by dqio_sem so no locking needed */
	status = ocfs2_extend_no_holes(lqinode,
				       lqinode->i_size + 2 * sb->s_blocksize,
				       lqinode->i_size);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
	status = ocfs2_simple_size_update(lqinode, oinfo->dqi_lqi_bh,
					  lqinode->i_size + 2 * sb->s_blocksize);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	chunk = kmem_cache_alloc(ocfs2_qf_chunk_cachep, GFP_NOFS);
	if (!chunk) {
		status = -ENOMEM;
		mlog_errno(status);
		goto out;
	}

	down_read(&OCFS2_I(lqinode)->ip_alloc_sem);
	status = ocfs2_extent_map_get_blocks(lqinode, oinfo->dqi_blocks,
					     &p_blkno, NULL, NULL);
	up_read(&OCFS2_I(lqinode)->ip_alloc_sem);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
	bh = sb_getblk(sb, p_blkno);
	if (!bh) {
		status = -ENOMEM;
		mlog_errno(status);
		goto out;
	}
	dchunk = (struct ocfs2_local_disk_chunk *)bh->b_data;

	handle = ocfs2_start_trans(OCFS2_SB(sb), 2);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_journal_access(handle, lqinode, bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	lock_buffer(bh);
	dchunk->dqc_free = ol_quota_entries_per_block(sb);
	memset(dchunk->dqc_bitmap, 0,
	       sb->s_blocksize - sizeof(struct ocfs2_local_disk_chunk) -
	       OCFS2_QBLK_RESERVED_SPACE);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}

	oinfo->dqi_blocks += 2;
	oinfo->dqi_chunks++;
	status = ocfs2_local_write_info(sb, type);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	status = ocfs2_commit_trans(OCFS2_SB(sb), handle);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	list_add_tail(&chunk->qc_chunk, &oinfo->dqi_chunk);
	chunk->qc_num = list_entry(chunk->qc_chunk.prev,
				   struct ocfs2_quota_chunk,
				   qc_chunk)->qc_num + 1;
	chunk->qc_headerbh = bh;
	*offset = 0;
	return chunk;
out_trans:
	ocfs2_commit_trans(OCFS2_SB(sb), handle);
out:
	brelse(bh);
	kmem_cache_free(ocfs2_qf_chunk_cachep, chunk);
	return ERR_PTR(status);
}

/* Find free entry in local quota file */
static struct ocfs2_quota_chunk *ocfs2_extend_local_quota_file(
						       struct super_block *sb,
						       int type,
						       int *offset)
{
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	struct ocfs2_quota_chunk *chunk;
	struct inode *lqinode = sb_dqopt(sb)->files[type];
	struct ocfs2_local_disk_chunk *dchunk;
	int epb = ol_quota_entries_per_block(sb);
	unsigned int chunk_blocks;
	int status;
	handle_t *handle;

	if (list_empty(&oinfo->dqi_chunk))
		return ocfs2_local_quota_add_chunk(sb, type, offset);
	/* Is the last chunk full? */
	chunk = list_entry(oinfo->dqi_chunk.prev,
			struct ocfs2_quota_chunk, qc_chunk);
	chunk_blocks = oinfo->dqi_blocks -
			ol_quota_chunk_block(sb, chunk->qc_num) - 1;
	if (ol_chunk_blocks(sb) == chunk_blocks)
		return ocfs2_local_quota_add_chunk(sb, type, offset);

	/* We are protected by dqio_sem so no locking needed */
	status = ocfs2_extend_no_holes(lqinode,
				       lqinode->i_size + sb->s_blocksize,
				       lqinode->i_size);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
	status = ocfs2_simple_size_update(lqinode, oinfo->dqi_lqi_bh,
					  lqinode->i_size + sb->s_blocksize);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
	handle = ocfs2_start_trans(OCFS2_SB(sb), 2);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}
	status = ocfs2_journal_access(handle, lqinode, chunk->qc_headerbh,
				 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}

	dchunk = (struct ocfs2_local_disk_chunk *)chunk->qc_headerbh->b_data;
	lock_buffer(chunk->qc_headerbh);
	le32_add_cpu(&dchunk->dqc_free, ol_quota_entries_per_block(sb));
	unlock_buffer(chunk->qc_headerbh);
	status = ocfs2_journal_dirty(handle, chunk->qc_headerbh);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	oinfo->dqi_blocks++;
	status = ocfs2_local_write_info(sb, type);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}

	status = ocfs2_commit_trans(OCFS2_SB(sb), handle);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
	*offset = chunk_blocks * epb;
	return chunk;
out_trans:
	ocfs2_commit_trans(OCFS2_SB(sb), handle);
out:
	return ERR_PTR(status);
}

void olq_alloc_dquot(struct buffer_head *bh, void *private)
{
	int *offset = private;
	struct ocfs2_local_disk_chunk *dchunk;

	dchunk = (struct ocfs2_local_disk_chunk *)bh->b_data;
	ocfs2_set_bit(*offset, dchunk->dqc_bitmap);
	le32_add_cpu(&dchunk->dqc_free, -1);
}

/* Create dquot in the local file for given id */
static int ocfs2_create_local_dquot(struct dquot *dquot)
{
	struct super_block *sb = dquot->dq_sb;
	int type = dquot->dq_type;
	struct inode *lqinode = sb_dqopt(sb)->files[type];
	struct ocfs2_quota_chunk *chunk;
	struct ocfs2_dquot *od = OCFS2_DQUOT(dquot);
	int offset;
	int status;

	chunk = ocfs2_find_free_entry(sb, type, &offset);
	if (!chunk) {
		chunk = ocfs2_extend_local_quota_file(sb, type, &offset);
		if (IS_ERR(chunk))
			return PTR_ERR(chunk);
	} else if (IS_ERR(chunk)) {
		return PTR_ERR(chunk);
	}
	od->dq_local_off = ol_dqblk_off(sb, chunk->qc_num, offset);
	od->dq_chunk = chunk;

	/* Initialize dquot structure on disk */
	status = ocfs2_local_write_dquot(dquot);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	/* Mark structure as allocated */
	status = ocfs2_modify_bh(lqinode, chunk->qc_headerbh, olq_alloc_dquot,
				 &offset);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
out:
	return status;
}

/* Create entry in local file for dquot, load data from the global file */
static int ocfs2_local_read_dquot(struct dquot *dquot)
{
	int status;

	mlog_entry("id=%u, type=%d\n", dquot->dq_id, dquot->dq_type);

	status = ocfs2_global_read_dquot(dquot);
	if (status < 0) {
		mlog_errno(status);
		goto out_err;
	}

	/* Now create entry in the local quota file */
	status = ocfs2_create_local_dquot(dquot);
	if (status < 0) {
		mlog_errno(status);
		goto out_err;
	}
	mlog_exit(0);
	return 0;
out_err:
	mlog_exit(status);
	return status;
}

/* Release dquot structure from local quota file. ocfs2_release_dquot() has
 * already started a transaction and obtained exclusive lock for global
 * quota file. */
static int ocfs2_local_release_dquot(struct dquot *dquot)
{
	int status;
	int type = dquot->dq_type;
	struct ocfs2_dquot *od = OCFS2_DQUOT(dquot);
	struct super_block *sb = dquot->dq_sb;
	struct ocfs2_local_disk_chunk *dchunk;
	int offset;
	handle_t *handle = journal_current_handle();

	BUG_ON(!handle);
	/* First write all local changes to global file */
	status = ocfs2_global_release_dquot(dquot);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_journal_access(handle, sb_dqopt(sb)->files[type],
			od->dq_chunk->qc_headerbh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
	offset = ol_dqblk_chunk_off(sb, od->dq_chunk->qc_num,
					     od->dq_local_off);
	dchunk = (struct ocfs2_local_disk_chunk *)
			(od->dq_chunk->qc_headerbh->b_data);
	/* Mark structure as freed */
	lock_buffer(od->dq_chunk->qc_headerbh);
	ocfs2_clear_bit(offset, dchunk->dqc_bitmap);
	le32_add_cpu(&dchunk->dqc_free, 1);
	unlock_buffer(od->dq_chunk->qc_headerbh);
	status = ocfs2_journal_dirty(handle, od->dq_chunk->qc_headerbh);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}
	status = 0;
out:
	/* Clear the read bit so that next time someone uses this
	 * dquot he reads fresh info from disk and allocates local
	 * dquot structure */
	clear_bit(DQ_READ_B, &dquot->dq_flags);
	return status;
}

static struct quota_format_ops ocfs2_format_ops = {
	.check_quota_file	= ocfs2_local_check_quota_file,
	.read_file_info		= ocfs2_local_read_info,
	.write_file_info	= ocfs2_global_write_info,
	.free_file_info		= ocfs2_local_free_info,
	.read_dqblk		= ocfs2_local_read_dquot,
	.commit_dqblk		= ocfs2_local_write_dquot,
	.release_dqblk		= ocfs2_local_release_dquot,
};

struct quota_format_type ocfs2_quota_format = {
	.qf_fmt_id = QFMT_OCFS2,
	.qf_ops = &ocfs2_format_ops,
	.qf_owner = THIS_MODULE
};
