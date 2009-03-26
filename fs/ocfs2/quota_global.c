/*
 *  Implementation of operations over global quota file
 */
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/quota.h>
#include <linux/quotaops.h>
#include <linux/dqblk_qtree.h>
#include <linux/jiffies.h>
#include <linux/writeback.h>
#include <linux/workqueue.h>

#define MLOG_MASK_PREFIX ML_QUOTA
#include <cluster/masklog.h>

#include "ocfs2_fs.h"
#include "ocfs2.h"
#include "alloc.h"
#include "blockcheck.h"
#include "inode.h"
#include "journal.h"
#include "file.h"
#include "sysfile.h"
#include "dlmglue.h"
#include "uptodate.h"
#include "quota.h"

static struct workqueue_struct *ocfs2_quota_wq = NULL;

static void qsync_work_fn(struct work_struct *work);

static void ocfs2_global_disk2memdqb(struct dquot *dquot, void *dp)
{
	struct ocfs2_global_disk_dqblk *d = dp;
	struct mem_dqblk *m = &dquot->dq_dqb;

	/* Update from disk only entries not set by the admin */
	if (!test_bit(DQ_LASTSET_B + QIF_ILIMITS_B, &dquot->dq_flags)) {
		m->dqb_ihardlimit = le64_to_cpu(d->dqb_ihardlimit);
		m->dqb_isoftlimit = le64_to_cpu(d->dqb_isoftlimit);
	}
	if (!test_bit(DQ_LASTSET_B + QIF_INODES_B, &dquot->dq_flags))
		m->dqb_curinodes = le64_to_cpu(d->dqb_curinodes);
	if (!test_bit(DQ_LASTSET_B + QIF_BLIMITS_B, &dquot->dq_flags)) {
		m->dqb_bhardlimit = le64_to_cpu(d->dqb_bhardlimit);
		m->dqb_bsoftlimit = le64_to_cpu(d->dqb_bsoftlimit);
	}
	if (!test_bit(DQ_LASTSET_B + QIF_SPACE_B, &dquot->dq_flags))
		m->dqb_curspace = le64_to_cpu(d->dqb_curspace);
	if (!test_bit(DQ_LASTSET_B + QIF_BTIME_B, &dquot->dq_flags))
		m->dqb_btime = le64_to_cpu(d->dqb_btime);
	if (!test_bit(DQ_LASTSET_B + QIF_ITIME_B, &dquot->dq_flags))
		m->dqb_itime = le64_to_cpu(d->dqb_itime);
	OCFS2_DQUOT(dquot)->dq_use_count = le32_to_cpu(d->dqb_use_count);
}

static void ocfs2_global_mem2diskdqb(void *dp, struct dquot *dquot)
{
	struct ocfs2_global_disk_dqblk *d = dp;
	struct mem_dqblk *m = &dquot->dq_dqb;

	d->dqb_id = cpu_to_le32(dquot->dq_id);
	d->dqb_use_count = cpu_to_le32(OCFS2_DQUOT(dquot)->dq_use_count);
	d->dqb_ihardlimit = cpu_to_le64(m->dqb_ihardlimit);
	d->dqb_isoftlimit = cpu_to_le64(m->dqb_isoftlimit);
	d->dqb_curinodes = cpu_to_le64(m->dqb_curinodes);
	d->dqb_bhardlimit = cpu_to_le64(m->dqb_bhardlimit);
	d->dqb_bsoftlimit = cpu_to_le64(m->dqb_bsoftlimit);
	d->dqb_curspace = cpu_to_le64(m->dqb_curspace);
	d->dqb_btime = cpu_to_le64(m->dqb_btime);
	d->dqb_itime = cpu_to_le64(m->dqb_itime);
}

static int ocfs2_global_is_id(void *dp, struct dquot *dquot)
{
	struct ocfs2_global_disk_dqblk *d = dp;
	struct ocfs2_mem_dqinfo *oinfo =
			sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv;

	if (qtree_entry_unused(&oinfo->dqi_gi, dp))
		return 0;
	return le32_to_cpu(d->dqb_id) == dquot->dq_id;
}

struct qtree_fmt_operations ocfs2_global_ops = {
	.mem2disk_dqblk = ocfs2_global_mem2diskdqb,
	.disk2mem_dqblk = ocfs2_global_disk2memdqb,
	.is_id = ocfs2_global_is_id,
};

static int ocfs2_validate_quota_block(struct super_block *sb,
				      struct buffer_head *bh)
{
	struct ocfs2_disk_dqtrailer *dqt =
		ocfs2_block_dqtrailer(sb->s_blocksize, bh->b_data);

	mlog(0, "Validating quota block %llu\n",
	     (unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * If the ecc fails, we return the error but otherwise
	 * leave the filesystem running.  We know any error is
	 * local to this block.
	 */
	return ocfs2_validate_meta_ecc(sb, bh->b_data, &dqt->dq_check);
}

int ocfs2_read_quota_block(struct inode *inode, u64 v_block,
			   struct buffer_head **bh)
{
	int rc = 0;
	struct buffer_head *tmp = *bh;

	rc = ocfs2_read_virt_blocks(inode, v_block, 1, &tmp, 0,
				    ocfs2_validate_quota_block);
	if (rc)
		mlog_errno(rc);

	/* If ocfs2_read_virt_blocks() got us a new bh, pass it up. */
	if (!rc && !*bh)
		*bh = tmp;

	return rc;
}

static int ocfs2_get_quota_block(struct inode *inode, int block,
				 struct buffer_head **bh)
{
	u64 pblock, pcount;
	int err;

	down_read(&OCFS2_I(inode)->ip_alloc_sem);
	err = ocfs2_extent_map_get_blocks(inode, block, &pblock, &pcount, NULL);
	up_read(&OCFS2_I(inode)->ip_alloc_sem);
	if (err) {
		mlog_errno(err);
		return err;
	}
	*bh = sb_getblk(inode->i_sb, pblock);
	if (!*bh) {
		err = -EIO;
		mlog_errno(err);
	}
	return err;;
}

/* Read data from global quotafile - avoid pagecache and such because we cannot
 * afford acquiring the locks... We use quota cluster lock to serialize
 * operations. Caller is responsible for acquiring it. */
ssize_t ocfs2_quota_read(struct super_block *sb, int type, char *data,
			 size_t len, loff_t off)
{
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;
	struct inode *gqinode = oinfo->dqi_gqinode;
	loff_t i_size = i_size_read(gqinode);
	int offset = off & (sb->s_blocksize - 1);
	sector_t blk = off >> sb->s_blocksize_bits;
	int err = 0;
	struct buffer_head *bh;
	size_t toread, tocopy;

	if (off > i_size)
		return 0;
	if (off + len > i_size)
		len = i_size - off;
	toread = len;
	while (toread > 0) {
		tocopy = min_t(size_t, (sb->s_blocksize - offset), toread);
		bh = NULL;
		err = ocfs2_read_quota_block(gqinode, blk, &bh);
		if (err) {
			mlog_errno(err);
			return err;
		}
		memcpy(data, bh->b_data + offset, tocopy);
		brelse(bh);
		offset = 0;
		toread -= tocopy;
		data += tocopy;
		blk++;
	}
	return len;
}

/* Write to quotafile (we know the transaction is already started and has
 * enough credits) */
ssize_t ocfs2_quota_write(struct super_block *sb, int type,
			  const char *data, size_t len, loff_t off)
{
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	struct inode *gqinode = oinfo->dqi_gqinode;
	int offset = off & (sb->s_blocksize - 1);
	sector_t blk = off >> sb->s_blocksize_bits;
	int err = 0, new = 0, ja_type;
	struct buffer_head *bh = NULL;
	handle_t *handle = journal_current_handle();

	if (!handle) {
		mlog(ML_ERROR, "Quota write (off=%llu, len=%llu) cancelled "
		     "because transaction was not started.\n",
		     (unsigned long long)off, (unsigned long long)len);
		return -EIO;
	}
	if (len > sb->s_blocksize - OCFS2_QBLK_RESERVED_SPACE - offset) {
		WARN_ON(1);
		len = sb->s_blocksize - OCFS2_QBLK_RESERVED_SPACE - offset;
	}

	mutex_lock_nested(&gqinode->i_mutex, I_MUTEX_QUOTA);
	if (gqinode->i_size < off + len) {
		down_write(&OCFS2_I(gqinode)->ip_alloc_sem);
		err = ocfs2_extend_no_holes(gqinode, off + len, off);
		up_write(&OCFS2_I(gqinode)->ip_alloc_sem);
		if (err < 0)
			goto out;
		err = ocfs2_simple_size_update(gqinode,
					       oinfo->dqi_gqi_bh,
					       off + len);
		if (err < 0)
			goto out;
		new = 1;
	}
	/* Not rewriting whole block? */
	if ((offset || len < sb->s_blocksize - OCFS2_QBLK_RESERVED_SPACE) &&
	    !new) {
		err = ocfs2_read_quota_block(gqinode, blk, &bh);
		ja_type = OCFS2_JOURNAL_ACCESS_WRITE;
	} else {
		err = ocfs2_get_quota_block(gqinode, blk, &bh);
		ja_type = OCFS2_JOURNAL_ACCESS_CREATE;
	}
	if (err) {
		mlog_errno(err);
		return err;
	}
	lock_buffer(bh);
	if (new)
		memset(bh->b_data, 0, sb->s_blocksize);
	memcpy(bh->b_data + offset, data, len);
	flush_dcache_page(bh->b_page);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	ocfs2_set_buffer_uptodate(gqinode, bh);
	err = ocfs2_journal_access_dq(handle, gqinode, bh, ja_type);
	if (err < 0) {
		brelse(bh);
		goto out;
	}
	err = ocfs2_journal_dirty(handle, bh);
	brelse(bh);
	if (err < 0)
		goto out;
out:
	if (err) {
		mutex_unlock(&gqinode->i_mutex);
		mlog_errno(err);
		return err;
	}
	gqinode->i_version++;
	ocfs2_mark_inode_dirty(handle, gqinode, oinfo->dqi_gqi_bh);
	mutex_unlock(&gqinode->i_mutex);
	return len;
}

int ocfs2_lock_global_qf(struct ocfs2_mem_dqinfo *oinfo, int ex)
{
	int status;
	struct buffer_head *bh = NULL;

	status = ocfs2_inode_lock(oinfo->dqi_gqinode, &bh, ex);
	if (status < 0)
		return status;
	spin_lock(&dq_data_lock);
	if (!oinfo->dqi_gqi_count++)
		oinfo->dqi_gqi_bh = bh;
	else
		WARN_ON(bh != oinfo->dqi_gqi_bh);
	spin_unlock(&dq_data_lock);
	return 0;
}

void ocfs2_unlock_global_qf(struct ocfs2_mem_dqinfo *oinfo, int ex)
{
	ocfs2_inode_unlock(oinfo->dqi_gqinode, ex);
	brelse(oinfo->dqi_gqi_bh);
	spin_lock(&dq_data_lock);
	if (!--oinfo->dqi_gqi_count)
		oinfo->dqi_gqi_bh = NULL;
	spin_unlock(&dq_data_lock);
}

/* Read information header from global quota file */
int ocfs2_global_read_info(struct super_block *sb, int type)
{
	struct inode *gqinode = NULL;
	unsigned int ino[MAXQUOTAS] = { USER_QUOTA_SYSTEM_INODE,
					GROUP_QUOTA_SYSTEM_INODE };
	struct ocfs2_global_disk_dqinfo dinfo;
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	int status;

	mlog_entry_void();

	/* Read global header */
	gqinode = ocfs2_get_system_file_inode(OCFS2_SB(sb), ino[type],
			OCFS2_INVALID_SLOT);
	if (!gqinode) {
		mlog(ML_ERROR, "failed to get global quota inode (type=%d)\n",
			type);
		status = -EINVAL;
		goto out_err;
	}
	oinfo->dqi_gi.dqi_sb = sb;
	oinfo->dqi_gi.dqi_type = type;
	ocfs2_qinfo_lock_res_init(&oinfo->dqi_gqlock, oinfo);
	oinfo->dqi_gi.dqi_entry_size = sizeof(struct ocfs2_global_disk_dqblk);
	oinfo->dqi_gi.dqi_ops = &ocfs2_global_ops;
	oinfo->dqi_gqi_bh = NULL;
	oinfo->dqi_gqi_count = 0;
	oinfo->dqi_gqinode = gqinode;
	status = ocfs2_lock_global_qf(oinfo, 0);
	if (status < 0) {
		mlog_errno(status);
		goto out_err;
	}
	status = sb->s_op->quota_read(sb, type, (char *)&dinfo,
				      sizeof(struct ocfs2_global_disk_dqinfo),
				      OCFS2_GLOBAL_INFO_OFF);
	ocfs2_unlock_global_qf(oinfo, 0);
	if (status != sizeof(struct ocfs2_global_disk_dqinfo)) {
		mlog(ML_ERROR, "Cannot read global quota info (%d).\n",
		     status);
		if (status >= 0)
			status = -EIO;
		mlog_errno(status);
		goto out_err;
	}
	info->dqi_bgrace = le32_to_cpu(dinfo.dqi_bgrace);
	info->dqi_igrace = le32_to_cpu(dinfo.dqi_igrace);
	oinfo->dqi_syncms = le32_to_cpu(dinfo.dqi_syncms);
	oinfo->dqi_syncjiff = msecs_to_jiffies(oinfo->dqi_syncms);
	oinfo->dqi_gi.dqi_blocks = le32_to_cpu(dinfo.dqi_blocks);
	oinfo->dqi_gi.dqi_free_blk = le32_to_cpu(dinfo.dqi_free_blk);
	oinfo->dqi_gi.dqi_free_entry = le32_to_cpu(dinfo.dqi_free_entry);
	oinfo->dqi_gi.dqi_blocksize_bits = sb->s_blocksize_bits;
	oinfo->dqi_gi.dqi_usable_bs = sb->s_blocksize -
						OCFS2_QBLK_RESERVED_SPACE;
	oinfo->dqi_gi.dqi_qtree_depth = qtree_depth(&oinfo->dqi_gi);
	INIT_DELAYED_WORK(&oinfo->dqi_sync_work, qsync_work_fn);
	queue_delayed_work(ocfs2_quota_wq, &oinfo->dqi_sync_work,
			   oinfo->dqi_syncjiff);

out_err:
	mlog_exit(status);
	return status;
}

/* Write information to global quota file. Expects exlusive lock on quota
 * file inode and quota info */
static int __ocfs2_global_write_info(struct super_block *sb, int type)
{
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	struct ocfs2_global_disk_dqinfo dinfo;
	ssize_t size;

	spin_lock(&dq_data_lock);
	info->dqi_flags &= ~DQF_INFO_DIRTY;
	dinfo.dqi_bgrace = cpu_to_le32(info->dqi_bgrace);
	dinfo.dqi_igrace = cpu_to_le32(info->dqi_igrace);
	spin_unlock(&dq_data_lock);
	dinfo.dqi_syncms = cpu_to_le32(oinfo->dqi_syncms);
	dinfo.dqi_blocks = cpu_to_le32(oinfo->dqi_gi.dqi_blocks);
	dinfo.dqi_free_blk = cpu_to_le32(oinfo->dqi_gi.dqi_free_blk);
	dinfo.dqi_free_entry = cpu_to_le32(oinfo->dqi_gi.dqi_free_entry);
	size = sb->s_op->quota_write(sb, type, (char *)&dinfo,
				     sizeof(struct ocfs2_global_disk_dqinfo),
				     OCFS2_GLOBAL_INFO_OFF);
	if (size != sizeof(struct ocfs2_global_disk_dqinfo)) {
		mlog(ML_ERROR, "Cannot write global quota info structure\n");
		if (size >= 0)
			size = -EIO;
		return size;
	}
	return 0;
}

int ocfs2_global_write_info(struct super_block *sb, int type)
{
	int err;
	struct ocfs2_mem_dqinfo *info = sb_dqinfo(sb, type)->dqi_priv;

	err = ocfs2_qinfo_lock(info, 1);
	if (err < 0)
		return err;
	err = __ocfs2_global_write_info(sb, type);
	ocfs2_qinfo_unlock(info, 1);
	return err;
}

/* Read in information from global quota file and acquire a reference to it.
 * dquot_acquire() has already started the transaction and locked quota file */
int ocfs2_global_read_dquot(struct dquot *dquot)
{
	int err, err2, ex = 0;
	struct ocfs2_mem_dqinfo *info =
			sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv;

	err = ocfs2_qinfo_lock(info, 0);
	if (err < 0)
		goto out;
	err = qtree_read_dquot(&info->dqi_gi, dquot);
	if (err < 0)
		goto out_qlock;
	OCFS2_DQUOT(dquot)->dq_use_count++;
	OCFS2_DQUOT(dquot)->dq_origspace = dquot->dq_dqb.dqb_curspace;
	OCFS2_DQUOT(dquot)->dq_originodes = dquot->dq_dqb.dqb_curinodes;
	if (!dquot->dq_off) {	/* No real quota entry? */
		/* Upgrade to exclusive lock for allocation */
		err = ocfs2_qinfo_lock(info, 1);
		if (err < 0)
			goto out_qlock;
		ex = 1;
	}
	err = qtree_write_dquot(&info->dqi_gi, dquot);
	if (ex && info_dirty(sb_dqinfo(dquot->dq_sb, dquot->dq_type))) {
		err2 = __ocfs2_global_write_info(dquot->dq_sb, dquot->dq_type);
		if (!err)
			err = err2;
	}
out_qlock:
	if (ex)
		ocfs2_qinfo_unlock(info, 1);
	ocfs2_qinfo_unlock(info, 0);
out:
	if (err < 0)
		mlog_errno(err);
	return err;
}

/* Sync local information about quota modifications with global quota file.
 * Caller must have started the transaction and obtained exclusive lock for
 * global quota file inode */
int __ocfs2_sync_dquot(struct dquot *dquot, int freeing)
{
	int err, err2;
	struct super_block *sb = dquot->dq_sb;
	int type = dquot->dq_type;
	struct ocfs2_mem_dqinfo *info = sb_dqinfo(sb, type)->dqi_priv;
	struct ocfs2_global_disk_dqblk dqblk;
	s64 spacechange, inodechange;
	time_t olditime, oldbtime;

	err = sb->s_op->quota_read(sb, type, (char *)&dqblk,
				   sizeof(struct ocfs2_global_disk_dqblk),
				   dquot->dq_off);
	if (err != sizeof(struct ocfs2_global_disk_dqblk)) {
		if (err >= 0) {
			mlog(ML_ERROR, "Short read from global quota file "
				       "(%u read)\n", err);
			err = -EIO;
		}
		goto out;
	}

	/* Update space and inode usage. Get also other information from
	 * global quota file so that we don't overwrite any changes there.
	 * We are */
	spin_lock(&dq_data_lock);
	spacechange = dquot->dq_dqb.dqb_curspace -
					OCFS2_DQUOT(dquot)->dq_origspace;
	inodechange = dquot->dq_dqb.dqb_curinodes -
					OCFS2_DQUOT(dquot)->dq_originodes;
	olditime = dquot->dq_dqb.dqb_itime;
	oldbtime = dquot->dq_dqb.dqb_btime;
	ocfs2_global_disk2memdqb(dquot, &dqblk);
	mlog(0, "Syncing global dquot %u space %lld+%lld, inodes %lld+%lld\n",
	     dquot->dq_id, dquot->dq_dqb.dqb_curspace, (long long)spacechange,
	     dquot->dq_dqb.dqb_curinodes, (long long)inodechange);
	if (!test_bit(DQ_LASTSET_B + QIF_SPACE_B, &dquot->dq_flags))
		dquot->dq_dqb.dqb_curspace += spacechange;
	if (!test_bit(DQ_LASTSET_B + QIF_INODES_B, &dquot->dq_flags))
		dquot->dq_dqb.dqb_curinodes += inodechange;
	/* Set properly space grace time... */
	if (dquot->dq_dqb.dqb_bsoftlimit &&
	    dquot->dq_dqb.dqb_curspace > dquot->dq_dqb.dqb_bsoftlimit) {
		if (!test_bit(DQ_LASTSET_B + QIF_BTIME_B, &dquot->dq_flags) &&
		    oldbtime > 0) {
			if (dquot->dq_dqb.dqb_btime > 0)
				dquot->dq_dqb.dqb_btime =
					min(dquot->dq_dqb.dqb_btime, oldbtime);
			else
				dquot->dq_dqb.dqb_btime = oldbtime;
		}
	} else {
		dquot->dq_dqb.dqb_btime = 0;
		clear_bit(DQ_BLKS_B, &dquot->dq_flags);
	}
	/* Set properly inode grace time... */
	if (dquot->dq_dqb.dqb_isoftlimit &&
	    dquot->dq_dqb.dqb_curinodes > dquot->dq_dqb.dqb_isoftlimit) {
		if (!test_bit(DQ_LASTSET_B + QIF_ITIME_B, &dquot->dq_flags) &&
		    olditime > 0) {
			if (dquot->dq_dqb.dqb_itime > 0)
				dquot->dq_dqb.dqb_itime =
					min(dquot->dq_dqb.dqb_itime, olditime);
			else
				dquot->dq_dqb.dqb_itime = olditime;
		}
	} else {
		dquot->dq_dqb.dqb_itime = 0;
		clear_bit(DQ_INODES_B, &dquot->dq_flags);
	}
	/* All information is properly updated, clear the flags */
	__clear_bit(DQ_LASTSET_B + QIF_SPACE_B, &dquot->dq_flags);
	__clear_bit(DQ_LASTSET_B + QIF_INODES_B, &dquot->dq_flags);
	__clear_bit(DQ_LASTSET_B + QIF_BLIMITS_B, &dquot->dq_flags);
	__clear_bit(DQ_LASTSET_B + QIF_ILIMITS_B, &dquot->dq_flags);
	__clear_bit(DQ_LASTSET_B + QIF_BTIME_B, &dquot->dq_flags);
	__clear_bit(DQ_LASTSET_B + QIF_ITIME_B, &dquot->dq_flags);
	OCFS2_DQUOT(dquot)->dq_origspace = dquot->dq_dqb.dqb_curspace;
	OCFS2_DQUOT(dquot)->dq_originodes = dquot->dq_dqb.dqb_curinodes;
	spin_unlock(&dq_data_lock);
	err = ocfs2_qinfo_lock(info, freeing);
	if (err < 0) {
		mlog(ML_ERROR, "Failed to lock quota info, loosing quota write"
			       " (type=%d, id=%u)\n", dquot->dq_type,
			       (unsigned)dquot->dq_id);
		goto out;
	}
	if (freeing)
		OCFS2_DQUOT(dquot)->dq_use_count--;
	err = qtree_write_dquot(&info->dqi_gi, dquot);
	if (err < 0)
		goto out_qlock;
	if (freeing && !OCFS2_DQUOT(dquot)->dq_use_count) {
		err = qtree_release_dquot(&info->dqi_gi, dquot);
		if (info_dirty(sb_dqinfo(sb, type))) {
			err2 = __ocfs2_global_write_info(sb, type);
			if (!err)
				err = err2;
		}
	}
out_qlock:
	ocfs2_qinfo_unlock(info, freeing);
out:
	if (err < 0)
		mlog_errno(err);
	return err;
}

/*
 *  Functions for periodic syncing of dquots with global file
 */
static int ocfs2_sync_dquot_helper(struct dquot *dquot, unsigned long type)
{
	handle_t *handle;
	struct super_block *sb = dquot->dq_sb;
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	int status = 0;

	mlog_entry("id=%u qtype=%u type=%lu device=%s\n", dquot->dq_id,
		   dquot->dq_type, type, sb->s_id);
	if (type != dquot->dq_type)
		goto out;
	status = ocfs2_lock_global_qf(oinfo, 1);
	if (status < 0)
		goto out;

	handle = ocfs2_start_trans(osb, OCFS2_QSYNC_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out_ilock;
	}
	mutex_lock(&sb_dqopt(sb)->dqio_mutex);
	status = ocfs2_sync_dquot(dquot);
	mutex_unlock(&sb_dqopt(sb)->dqio_mutex);
	if (status < 0)
		mlog_errno(status);
	/* We have to write local structure as well... */
	dquot_mark_dquot_dirty(dquot);
	status = dquot_commit(dquot);
	if (status < 0)
		mlog_errno(status);
	ocfs2_commit_trans(osb, handle);
out_ilock:
	ocfs2_unlock_global_qf(oinfo, 1);
out:
	mlog_exit(status);
	return status;
}

static void qsync_work_fn(struct work_struct *work)
{
	struct ocfs2_mem_dqinfo *oinfo = container_of(work,
						      struct ocfs2_mem_dqinfo,
						      dqi_sync_work.work);
	struct super_block *sb = oinfo->dqi_gqinode->i_sb;

	dquot_scan_active(sb, ocfs2_sync_dquot_helper, oinfo->dqi_type);
	queue_delayed_work(ocfs2_quota_wq, &oinfo->dqi_sync_work,
			   oinfo->dqi_syncjiff);
}

/*
 *  Wrappers for generic quota functions
 */

static int ocfs2_write_dquot(struct dquot *dquot)
{
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(dquot->dq_sb);
	int status = 0;

	mlog_entry("id=%u, type=%d", dquot->dq_id, dquot->dq_type);

	handle = ocfs2_start_trans(osb, OCFS2_QWRITE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}
	status = dquot_commit(dquot);
	ocfs2_commit_trans(osb, handle);
out:
	mlog_exit(status);
	return status;
}

int ocfs2_calc_qdel_credits(struct super_block *sb, int type)
{
	struct ocfs2_mem_dqinfo *oinfo;
	int features[MAXQUOTAS] = { OCFS2_FEATURE_RO_COMPAT_USRQUOTA,
				    OCFS2_FEATURE_RO_COMPAT_GRPQUOTA };

	if (!OCFS2_HAS_RO_COMPAT_FEATURE(sb, features[type]))
		return 0;

	oinfo = sb_dqinfo(sb, type)->dqi_priv;
	/* We modify tree, leaf block, global info, local chunk header,
	 * global and local inode */
	return oinfo->dqi_gi.dqi_qtree_depth + 2 + 1 +
	       2 * OCFS2_INODE_UPDATE_CREDITS;
}

static int ocfs2_release_dquot(struct dquot *dquot)
{
	handle_t *handle;
	struct ocfs2_mem_dqinfo *oinfo =
			sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv;
	struct ocfs2_super *osb = OCFS2_SB(dquot->dq_sb);
	int status = 0;

	mlog_entry("id=%u, type=%d", dquot->dq_id, dquot->dq_type);

	status = ocfs2_lock_global_qf(oinfo, 1);
	if (status < 0)
		goto out;
	handle = ocfs2_start_trans(osb,
		ocfs2_calc_qdel_credits(dquot->dq_sb, dquot->dq_type));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out_ilock;
	}
	status = dquot_release(dquot);
	ocfs2_commit_trans(osb, handle);
out_ilock:
	ocfs2_unlock_global_qf(oinfo, 1);
out:
	mlog_exit(status);
	return status;
}

int ocfs2_calc_qinit_credits(struct super_block *sb, int type)
{
	struct ocfs2_mem_dqinfo *oinfo;
	int features[MAXQUOTAS] = { OCFS2_FEATURE_RO_COMPAT_USRQUOTA,
				    OCFS2_FEATURE_RO_COMPAT_GRPQUOTA };
	struct ocfs2_dinode *lfe, *gfe;

	if (!OCFS2_HAS_RO_COMPAT_FEATURE(sb, features[type]))
		return 0;

	oinfo = sb_dqinfo(sb, type)->dqi_priv;
	gfe = (struct ocfs2_dinode *)oinfo->dqi_gqi_bh->b_data;
	lfe = (struct ocfs2_dinode *)oinfo->dqi_lqi_bh->b_data;
	/* We can extend local file + global file. In local file we
	 * can modify info, chunk header block and dquot block. In
	 * global file we can modify info, tree and leaf block */
	return ocfs2_calc_extend_credits(sb, &lfe->id2.i_list, 0) +
	       ocfs2_calc_extend_credits(sb, &gfe->id2.i_list, 0) +
	       3 + oinfo->dqi_gi.dqi_qtree_depth + 2;
}

static int ocfs2_acquire_dquot(struct dquot *dquot)
{
	handle_t *handle;
	struct ocfs2_mem_dqinfo *oinfo =
			sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv;
	struct ocfs2_super *osb = OCFS2_SB(dquot->dq_sb);
	int status = 0;

	mlog_entry("id=%u, type=%d", dquot->dq_id, dquot->dq_type);
	/* We need an exclusive lock, because we're going to update use count
	 * and instantiate possibly new dquot structure */
	status = ocfs2_lock_global_qf(oinfo, 1);
	if (status < 0)
		goto out;
	handle = ocfs2_start_trans(osb,
		ocfs2_calc_qinit_credits(dquot->dq_sb, dquot->dq_type));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out_ilock;
	}
	status = dquot_acquire(dquot);
	ocfs2_commit_trans(osb, handle);
out_ilock:
	ocfs2_unlock_global_qf(oinfo, 1);
out:
	mlog_exit(status);
	return status;
}

static int ocfs2_mark_dquot_dirty(struct dquot *dquot)
{
	unsigned long mask = (1 << (DQ_LASTSET_B + QIF_ILIMITS_B)) |
			     (1 << (DQ_LASTSET_B + QIF_BLIMITS_B)) |
			     (1 << (DQ_LASTSET_B + QIF_INODES_B)) |
			     (1 << (DQ_LASTSET_B + QIF_SPACE_B)) |
			     (1 << (DQ_LASTSET_B + QIF_BTIME_B)) |
			     (1 << (DQ_LASTSET_B + QIF_ITIME_B));
	int sync = 0;
	int status;
	struct super_block *sb = dquot->dq_sb;
	int type = dquot->dq_type;
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(sb);

	mlog_entry("id=%u, type=%d", dquot->dq_id, type);
	dquot_mark_dquot_dirty(dquot);

	/* In case user set some limits, sync dquot immediately to global
	 * quota file so that information propagates quicker */
	spin_lock(&dq_data_lock);
	if (dquot->dq_flags & mask)
		sync = 1;
	spin_unlock(&dq_data_lock);
	/* This is a slight hack but we can't afford getting global quota
	 * lock if we already have a transaction started. */
	if (!sync || journal_current_handle()) {
		status = ocfs2_write_dquot(dquot);
		goto out;
	}
	status = ocfs2_lock_global_qf(oinfo, 1);
	if (status < 0)
		goto out;
	handle = ocfs2_start_trans(osb, OCFS2_QSYNC_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out_ilock;
	}
	status = ocfs2_sync_dquot(dquot);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	/* Now write updated local dquot structure */
	status = dquot_commit(dquot);
out_trans:
	ocfs2_commit_trans(osb, handle);
out_ilock:
	ocfs2_unlock_global_qf(oinfo, 1);
out:
	mlog_exit(status);
	return status;
}

/* This should happen only after set_dqinfo(). */
static int ocfs2_write_info(struct super_block *sb, int type)
{
	handle_t *handle;
	int status = 0;
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;

	mlog_entry_void();

	status = ocfs2_lock_global_qf(oinfo, 1);
	if (status < 0)
		goto out;
	handle = ocfs2_start_trans(OCFS2_SB(sb), OCFS2_QINFO_WRITE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out_ilock;
	}
	status = dquot_commit_info(sb, type);
	ocfs2_commit_trans(OCFS2_SB(sb), handle);
out_ilock:
	ocfs2_unlock_global_qf(oinfo, 1);
out:
	mlog_exit(status);
	return status;
}

static struct dquot *ocfs2_alloc_dquot(struct super_block *sb, int type)
{
	struct ocfs2_dquot *dquot =
				kmem_cache_zalloc(ocfs2_dquot_cachep, GFP_NOFS);

	if (!dquot)
		return NULL;
	return &dquot->dq_dquot;
}

static void ocfs2_destroy_dquot(struct dquot *dquot)
{
	kmem_cache_free(ocfs2_dquot_cachep, dquot);
}

struct dquot_operations ocfs2_quota_operations = {
	.initialize	= dquot_initialize,
	.drop		= dquot_drop,
	.alloc_space	= dquot_alloc_space,
	.alloc_inode	= dquot_alloc_inode,
	.free_space	= dquot_free_space,
	.free_inode	= dquot_free_inode,
	.transfer	= dquot_transfer,
	.write_dquot	= ocfs2_write_dquot,
	.acquire_dquot	= ocfs2_acquire_dquot,
	.release_dquot	= ocfs2_release_dquot,
	.mark_dirty	= ocfs2_mark_dquot_dirty,
	.write_info	= ocfs2_write_info,
	.alloc_dquot	= ocfs2_alloc_dquot,
	.destroy_dquot	= ocfs2_destroy_dquot,
};

int ocfs2_quota_setup(void)
{
	ocfs2_quota_wq = create_workqueue("o2quot");
	if (!ocfs2_quota_wq)
		return -ENOMEM;
	return 0;
}

void ocfs2_quota_shutdown(void)
{
	if (ocfs2_quota_wq) {
		flush_workqueue(ocfs2_quota_wq);
		destroy_workqueue(ocfs2_quota_wq);
		ocfs2_quota_wq = NULL;
	}
}
