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
#include "uptodate.h"

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

static unsigned int ol_dqblk_block(struct super_block *sb, int c, int off)
{
	int epb = ol_quota_entries_per_block(sb);

	return ol_quota_chunk_block(sb, c) + 1 + off / epb;
}

static unsigned int ol_dqblk_block_off(struct super_block *sb, int c, int off)
{
	int epb = ol_quota_entries_per_block(sb);

	return (off % epb) * sizeof(struct ocfs2_local_disk_dqblk);
}

/* Offset of the dquot structure in the quota file */
static loff_t ol_dqblk_off(struct super_block *sb, int c, int off)
{
	return (ol_dqblk_block(sb, c, off) << sb->s_blocksize_bits) +
	       ol_dqblk_block_off(sb, c, off);
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

	handle = ocfs2_start_trans(OCFS2_SB(sb),
				   OCFS2_QUOTA_BLOCK_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		return status;
	}
	status = ocfs2_journal_access_dq(handle, inode, bh,
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
	struct buffer_head *bh = NULL;
	struct inode *linode = sb_dqopt(sb)->files[type];
	struct inode *ginode = NULL;
	struct ocfs2_disk_dqheader *dqhead;
	int status, ret = 0;

	/* First check whether we understand local quota file */
	status = ocfs2_read_quota_block(linode, 0, &bh);
	if (status) {
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
	status = ocfs2_read_quota_block(ginode, 0, &bh);
	if (status) {
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
		newchunk->qc_headerbh = NULL;
		status = ocfs2_read_quota_block(inode,
				ol_quota_chunk_block(inode->i_sb, i),
				&newchunk->qc_headerbh);
		if (status) {
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

static int ocfs2_add_recovery_chunk(struct super_block *sb,
				    struct ocfs2_local_disk_chunk *dchunk,
				    int chunk,
				    struct list_head *head)
{
	struct ocfs2_recovery_chunk *rc;

	rc = kmalloc(sizeof(struct ocfs2_recovery_chunk), GFP_NOFS);
	if (!rc)
		return -ENOMEM;
	rc->rc_chunk = chunk;
	rc->rc_bitmap = kmalloc(sb->s_blocksize, GFP_NOFS);
	if (!rc->rc_bitmap) {
		kfree(rc);
		return -ENOMEM;
	}
	memcpy(rc->rc_bitmap, dchunk->dqc_bitmap,
	       (ol_chunk_entries(sb) + 7) >> 3);
	list_add_tail(&rc->rc_list, head);
	return 0;
}

static void free_recovery_list(struct list_head *head)
{
	struct ocfs2_recovery_chunk *next;
	struct ocfs2_recovery_chunk *rchunk;

	list_for_each_entry_safe(rchunk, next, head, rc_list) {
		list_del(&rchunk->rc_list);
		kfree(rchunk->rc_bitmap);
		kfree(rchunk);
	}
}

void ocfs2_free_quota_recovery(struct ocfs2_quota_recovery *rec)
{
	int type;

	for (type = 0; type < MAXQUOTAS; type++)
		free_recovery_list(&(rec->r_list[type]));
	kfree(rec);
}

/* Load entries in our quota file we have to recover*/
static int ocfs2_recovery_load_quota(struct inode *lqinode,
				     struct ocfs2_local_disk_dqinfo *ldinfo,
				     int type,
				     struct list_head *head)
{
	struct super_block *sb = lqinode->i_sb;
	struct buffer_head *hbh;
	struct ocfs2_local_disk_chunk *dchunk;
	int i, chunks = le32_to_cpu(ldinfo->dqi_chunks);
	int status = 0;

	for (i = 0; i < chunks; i++) {
		hbh = NULL;
		status = ocfs2_read_quota_block(lqinode,
						ol_quota_chunk_block(sb, i),
						&hbh);
		if (status) {
			mlog_errno(status);
			break;
		}
		dchunk = (struct ocfs2_local_disk_chunk *)hbh->b_data;
		if (le32_to_cpu(dchunk->dqc_free) < ol_chunk_entries(sb))
			status = ocfs2_add_recovery_chunk(sb, dchunk, i, head);
		brelse(hbh);
		if (status < 0)
			break;
	}
	if (status < 0)
		free_recovery_list(head);
	return status;
}

static struct ocfs2_quota_recovery *ocfs2_alloc_quota_recovery(void)
{
	int type;
	struct ocfs2_quota_recovery *rec;

	rec = kmalloc(sizeof(struct ocfs2_quota_recovery), GFP_NOFS);
	if (!rec)
		return NULL;
	for (type = 0; type < MAXQUOTAS; type++)
		INIT_LIST_HEAD(&(rec->r_list[type]));
	return rec;
}

/* Load information we need for quota recovery into memory */
struct ocfs2_quota_recovery *ocfs2_begin_quota_recovery(
						struct ocfs2_super *osb,
						int slot_num)
{
	unsigned int feature[MAXQUOTAS] = { OCFS2_FEATURE_RO_COMPAT_USRQUOTA,
					    OCFS2_FEATURE_RO_COMPAT_GRPQUOTA};
	unsigned int ino[MAXQUOTAS] = { LOCAL_USER_QUOTA_SYSTEM_INODE,
					LOCAL_GROUP_QUOTA_SYSTEM_INODE };
	struct super_block *sb = osb->sb;
	struct ocfs2_local_disk_dqinfo *ldinfo;
	struct inode *lqinode;
	struct buffer_head *bh;
	int type;
	int status = 0;
	struct ocfs2_quota_recovery *rec;

	mlog(ML_NOTICE, "Beginning quota recovery in slot %u\n", slot_num);
	rec = ocfs2_alloc_quota_recovery();
	if (!rec)
		return ERR_PTR(-ENOMEM);
	/* First init... */

	for (type = 0; type < MAXQUOTAS; type++) {
		if (!OCFS2_HAS_RO_COMPAT_FEATURE(sb, feature[type]))
			continue;
		/* At this point, journal of the slot is already replayed so
		 * we can trust metadata and data of the quota file */
		lqinode = ocfs2_get_system_file_inode(osb, ino[type], slot_num);
		if (!lqinode) {
			status = -ENOENT;
			goto out;
		}
		status = ocfs2_inode_lock_full(lqinode, NULL, 1,
					       OCFS2_META_LOCK_RECOVERY);
		if (status < 0) {
			mlog_errno(status);
			goto out_put;
		}
		/* Now read local header */
		bh = NULL;
		status = ocfs2_read_quota_block(lqinode, 0, &bh);
		if (status) {
			mlog_errno(status);
			mlog(ML_ERROR, "failed to read quota file info header "
				"(slot=%d type=%d)\n", slot_num, type);
			goto out_lock;
		}
		ldinfo = (struct ocfs2_local_disk_dqinfo *)(bh->b_data +
							OCFS2_LOCAL_INFO_OFF);
		status = ocfs2_recovery_load_quota(lqinode, ldinfo, type,
						   &rec->r_list[type]);
		brelse(bh);
out_lock:
		ocfs2_inode_unlock(lqinode, 1);
out_put:
		iput(lqinode);
		if (status < 0)
			break;
	}
out:
	if (status < 0) {
		ocfs2_free_quota_recovery(rec);
		rec = ERR_PTR(status);
	}
	return rec;
}

/* Sync changes in local quota file into global quota file and
 * reinitialize local quota file.
 * The function expects local quota file to be already locked and
 * dqonoff_mutex locked. */
static int ocfs2_recover_local_quota_file(struct inode *lqinode,
					  int type,
					  struct ocfs2_quota_recovery *rec)
{
	struct super_block *sb = lqinode->i_sb;
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;
	struct ocfs2_local_disk_chunk *dchunk;
	struct ocfs2_local_disk_dqblk *dqblk;
	struct dquot *dquot;
	handle_t *handle;
	struct buffer_head *hbh = NULL, *qbh = NULL;
	int status = 0;
	int bit, chunk;
	struct ocfs2_recovery_chunk *rchunk, *next;
	qsize_t spacechange, inodechange;

	mlog_entry("ino=%lu type=%u", (unsigned long)lqinode->i_ino, type);

	list_for_each_entry_safe(rchunk, next, &(rec->r_list[type]), rc_list) {
		chunk = rchunk->rc_chunk;
		hbh = NULL;
		status = ocfs2_read_quota_block(lqinode,
						ol_quota_chunk_block(sb, chunk),
						&hbh);
		if (status) {
			mlog_errno(status);
			break;
		}
		dchunk = (struct ocfs2_local_disk_chunk *)hbh->b_data;
		for_each_bit(bit, rchunk->rc_bitmap, ol_chunk_entries(sb)) {
			qbh = NULL;
			status = ocfs2_read_quota_block(lqinode,
						ol_dqblk_block(sb, chunk, bit),
						&qbh);
			if (status) {
				mlog_errno(status);
				break;
			}
			dqblk = (struct ocfs2_local_disk_dqblk *)(qbh->b_data +
				ol_dqblk_block_off(sb, chunk, bit));
			dquot = dqget(sb, le64_to_cpu(dqblk->dqb_id), type);
			if (!dquot) {
				status = -EIO;
				mlog(ML_ERROR, "Failed to get quota structure "
				     "for id %u, type %d. Cannot finish quota "
				     "file recovery.\n",
				     (unsigned)le64_to_cpu(dqblk->dqb_id),
				     type);
				goto out_put_bh;
			}
			status = ocfs2_lock_global_qf(oinfo, 1);
			if (status < 0) {
				mlog_errno(status);
				goto out_put_dquot;
			}

			handle = ocfs2_start_trans(OCFS2_SB(sb),
						   OCFS2_QSYNC_CREDITS);
			if (IS_ERR(handle)) {
				status = PTR_ERR(handle);
				mlog_errno(status);
				goto out_drop_lock;
			}
			mutex_lock(&sb_dqopt(sb)->dqio_mutex);
			spin_lock(&dq_data_lock);
			/* Add usage from quota entry into quota changes
			 * of our node. Auxiliary variables are important
			 * due to signedness */
			spacechange = le64_to_cpu(dqblk->dqb_spacemod);
			inodechange = le64_to_cpu(dqblk->dqb_inodemod);
			dquot->dq_dqb.dqb_curspace += spacechange;
			dquot->dq_dqb.dqb_curinodes += inodechange;
			spin_unlock(&dq_data_lock);
			/* We want to drop reference held by the crashed
			 * node. Since we have our own reference we know
			 * global structure actually won't be freed. */
			status = ocfs2_global_release_dquot(dquot);
			if (status < 0) {
				mlog_errno(status);
				goto out_commit;
			}
			/* Release local quota file entry */
			status = ocfs2_journal_access_dq(handle, lqinode,
					qbh, OCFS2_JOURNAL_ACCESS_WRITE);
			if (status < 0) {
				mlog_errno(status);
				goto out_commit;
			}
			lock_buffer(qbh);
			WARN_ON(!ocfs2_test_bit(bit, dchunk->dqc_bitmap));
			ocfs2_clear_bit(bit, dchunk->dqc_bitmap);
			le32_add_cpu(&dchunk->dqc_free, 1);
			unlock_buffer(qbh);
			status = ocfs2_journal_dirty(handle, qbh);
			if (status < 0)
				mlog_errno(status);
out_commit:
			mutex_unlock(&sb_dqopt(sb)->dqio_mutex);
			ocfs2_commit_trans(OCFS2_SB(sb), handle);
out_drop_lock:
			ocfs2_unlock_global_qf(oinfo, 1);
out_put_dquot:
			dqput(dquot);
out_put_bh:
			brelse(qbh);
			if (status < 0)
				break;
		}
		brelse(hbh);
		list_del(&rchunk->rc_list);
		kfree(rchunk->rc_bitmap);
		kfree(rchunk);
		if (status < 0)
			break;
	}
	if (status < 0)
		free_recovery_list(&(rec->r_list[type]));
	mlog_exit(status);
	return status;
}

/* Recover local quota files for given node different from us */
int ocfs2_finish_quota_recovery(struct ocfs2_super *osb,
				struct ocfs2_quota_recovery *rec,
				int slot_num)
{
	unsigned int ino[MAXQUOTAS] = { LOCAL_USER_QUOTA_SYSTEM_INODE,
					LOCAL_GROUP_QUOTA_SYSTEM_INODE };
	struct super_block *sb = osb->sb;
	struct ocfs2_local_disk_dqinfo *ldinfo;
	struct buffer_head *bh;
	handle_t *handle;
	int type;
	int status = 0;
	struct inode *lqinode;
	unsigned int flags;

	mlog(ML_NOTICE, "Finishing quota recovery in slot %u\n", slot_num);
	mutex_lock(&sb_dqopt(sb)->dqonoff_mutex);
	for (type = 0; type < MAXQUOTAS; type++) {
		if (list_empty(&(rec->r_list[type])))
			continue;
		mlog(0, "Recovering quota in slot %d\n", slot_num);
		lqinode = ocfs2_get_system_file_inode(osb, ino[type], slot_num);
		if (!lqinode) {
			status = -ENOENT;
			goto out;
		}
		status = ocfs2_inode_lock_full(lqinode, NULL, 1,
						       OCFS2_META_LOCK_NOQUEUE);
		/* Someone else is holding the lock? Then he must be
		 * doing the recovery. Just skip the file... */
		if (status == -EAGAIN) {
			mlog(ML_NOTICE, "skipping quota recovery for slot %d "
			     "because quota file is locked.\n", slot_num);
			status = 0;
			goto out_put;
		} else if (status < 0) {
			mlog_errno(status);
			goto out_put;
		}
		/* Now read local header */
		bh = NULL;
		status = ocfs2_read_quota_block(lqinode, 0, &bh);
		if (status) {
			mlog_errno(status);
			mlog(ML_ERROR, "failed to read quota file info header "
				"(slot=%d type=%d)\n", slot_num, type);
			goto out_lock;
		}
		ldinfo = (struct ocfs2_local_disk_dqinfo *)(bh->b_data +
							OCFS2_LOCAL_INFO_OFF);
		/* Is recovery still needed? */
		flags = le32_to_cpu(ldinfo->dqi_flags);
		if (!(flags & OLQF_CLEAN))
			status = ocfs2_recover_local_quota_file(lqinode,
								type,
								rec);
		/* We don't want to mark file as clean when it is actually
		 * active */
		if (slot_num == osb->slot_num)
			goto out_bh;
		/* Mark quota file as clean if we are recovering quota file of
		 * some other node. */
		handle = ocfs2_start_trans(osb,
					   OCFS2_LOCAL_QINFO_WRITE_CREDITS);
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			mlog_errno(status);
			goto out_bh;
		}
		status = ocfs2_journal_access_dq(handle, lqinode, bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto out_trans;
		}
		lock_buffer(bh);
		ldinfo->dqi_flags = cpu_to_le32(flags | OLQF_CLEAN);
		unlock_buffer(bh);
		status = ocfs2_journal_dirty(handle, bh);
		if (status < 0)
			mlog_errno(status);
out_trans:
		ocfs2_commit_trans(osb, handle);
out_bh:
		brelse(bh);
out_lock:
		ocfs2_inode_unlock(lqinode, 1);
out_put:
		iput(lqinode);
		if (status < 0)
			break;
	}
out:
	mutex_unlock(&sb_dqopt(sb)->dqonoff_mutex);
	kfree(rec);
	return status;
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
	struct ocfs2_quota_recovery *rec;
	int locked = 0;

	/* We don't need the lock and we have to acquire quota file locks
	 * which will later depend on this lock */
	mutex_unlock(&sb_dqopt(sb)->dqio_mutex);
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
	oinfo->dqi_rec = NULL;
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
	status = ocfs2_read_quota_block(lqinode, 0, &bh);
	if (status) {
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
	if (!(info->dqi_flags & OLQF_CLEAN)) {
		rec = OCFS2_SB(sb)->quota_rec;
		if (!rec) {
			rec = ocfs2_alloc_quota_recovery();
			if (!rec) {
				status = -ENOMEM;
				mlog_errno(status);
				goto out_err;
			}
			OCFS2_SB(sb)->quota_rec = rec;
		}

		status = ocfs2_recovery_load_quota(lqinode, ldinfo, type,
                                                   &rec->r_list[type]);
		if (status < 0) {
			mlog_errno(status);
			goto out_err;
		}
	}

	status = ocfs2_load_local_quota_bitmaps(lqinode,
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

	mutex_lock(&sb_dqopt(sb)->dqio_mutex);
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
	mutex_lock(&sb_dqopt(sb)->dqio_mutex);
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

	/* At this point we know there are no more dquots and thus
	 * even if there's some sync in the pdflush queue, it won't
	 * find any dquots and return without doing anything */
	cancel_delayed_work_sync(&oinfo->dqi_sync_work);
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

	/* dqonoff_mutex protects us against racing with recovery thread... */
	if (oinfo->dqi_rec) {
		ocfs2_free_quota_recovery(oinfo->dqi_rec);
		mark_clean = 0;
	}

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
	     od->dq_dquot.dq_id, (long long)le64_to_cpu(dqblk->dqb_spacemod),
	     (long long)le64_to_cpu(dqblk->dqb_inodemod));
}

/* Write dquot to local quota file */
static int ocfs2_local_write_dquot(struct dquot *dquot)
{
	struct super_block *sb = dquot->dq_sb;
	struct ocfs2_dquot *od = OCFS2_DQUOT(dquot);
	struct buffer_head *bh = NULL;
	int status;

	status = ocfs2_read_quota_block(sb_dqopt(sb)->files[dquot->dq_type],
				    ol_dqblk_file_block(sb, od->dq_local_off),
				    &bh);
	if (status) {
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
	struct buffer_head *bh = NULL, *dbh = NULL;
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
	/* Local quota info and two new blocks we initialize */
	handle = ocfs2_start_trans(OCFS2_SB(sb),
			OCFS2_LOCAL_QINFO_WRITE_CREDITS +
			2 * OCFS2_QUOTA_BLOCK_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}

	/* Initialize chunk header */
	down_read(&OCFS2_I(lqinode)->ip_alloc_sem);
	status = ocfs2_extent_map_get_blocks(lqinode, oinfo->dqi_blocks,
					     &p_blkno, NULL, NULL);
	up_read(&OCFS2_I(lqinode)->ip_alloc_sem);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	bh = sb_getblk(sb, p_blkno);
	if (!bh) {
		status = -ENOMEM;
		mlog_errno(status);
		goto out_trans;
	}
	dchunk = (struct ocfs2_local_disk_chunk *)bh->b_data;
	ocfs2_set_new_buffer_uptodate(lqinode, bh);
	status = ocfs2_journal_access_dq(handle, lqinode, bh,
					 OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	lock_buffer(bh);
	dchunk->dqc_free = cpu_to_le32(ol_quota_entries_per_block(sb));
	memset(dchunk->dqc_bitmap, 0,
	       sb->s_blocksize - sizeof(struct ocfs2_local_disk_chunk) -
	       OCFS2_QBLK_RESERVED_SPACE);
	unlock_buffer(bh);
	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}

	/* Initialize new block with structures */
	down_read(&OCFS2_I(lqinode)->ip_alloc_sem);
	status = ocfs2_extent_map_get_blocks(lqinode, oinfo->dqi_blocks + 1,
					     &p_blkno, NULL, NULL);
	up_read(&OCFS2_I(lqinode)->ip_alloc_sem);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	dbh = sb_getblk(sb, p_blkno);
	if (!dbh) {
		status = -ENOMEM;
		mlog_errno(status);
		goto out_trans;
	}
	ocfs2_set_new_buffer_uptodate(lqinode, dbh);
	status = ocfs2_journal_access_dq(handle, lqinode, dbh,
					 OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	lock_buffer(dbh);
	memset(dbh->b_data, 0, sb->s_blocksize - OCFS2_QBLK_RESERVED_SPACE);
	unlock_buffer(dbh);
	status = ocfs2_journal_dirty(handle, dbh);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}

	/* Update local quotafile info */
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
	brelse(dbh);
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
	struct buffer_head *bh;
	u64 p_blkno;
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

	/* Get buffer from the just added block */
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
	ocfs2_set_new_buffer_uptodate(lqinode, bh);

	/* Local quota info, chunk header and the new block we initialize */
	handle = ocfs2_start_trans(OCFS2_SB(sb),
			OCFS2_LOCAL_QINFO_WRITE_CREDITS +
			2 * OCFS2_QUOTA_BLOCK_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}
	/* Zero created block */
	status = ocfs2_journal_access_dq(handle, lqinode, bh,
				 OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	lock_buffer(bh);
	memset(bh->b_data, 0, sb->s_blocksize);
	unlock_buffer(bh);
	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	/* Update chunk header */
	status = ocfs2_journal_access_dq(handle, lqinode, chunk->qc_headerbh,
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
	/* Update file header */
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

static void olq_alloc_dquot(struct buffer_head *bh, void *private)
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

	status = ocfs2_journal_access_dq(handle, sb_dqopt(sb)->files[type],
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
