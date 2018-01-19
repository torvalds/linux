// SPDX-License-Identifier: GPL-2.0
/*
 *  Implementation of operations over global quota file
 */
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/quota.h>
#include <linux/quotaops.h>
#include <linux/dqblk_qtree.h>
#include <linux/jiffies.h>
#include <linux/writeback.h>
#include <linux/workqueue.h>
#include <linux/llist.h>

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
#include "super.h"
#include "buffer_head_io.h"
#include "quota.h"
#include "ocfs2_trace.h"

/*
 * Locking of quotas with OCFS2 is rather complex. Here are rules that
 * should be obeyed by all the functions:
 * - any write of quota structure (either to local or global file) is protected
 *   by dqio_sem or dquot->dq_lock.
 * - any modification of global quota file holds inode cluster lock, i_mutex,
 *   and ip_alloc_sem of the global quota file (achieved by
 *   ocfs2_lock_global_qf). It also has to hold qinfo_lock.
 * - an allocation of new blocks for local quota file is protected by
 *   its ip_alloc_sem
 *
 * A rough sketch of locking dependencies (lf = local file, gf = global file):
 * Normal filesystem operation:
 *   start_trans -> dqio_sem -> write to lf
 * Syncing of local and global file:
 *   ocfs2_lock_global_qf -> start_trans -> dqio_sem -> qinfo_lock ->
 *     write to gf
 *						       -> write to lf
 * Acquire dquot for the first time:
 *   dq_lock -> ocfs2_lock_global_qf -> qinfo_lock -> read from gf
 *				     -> alloc space for gf
 *				     -> start_trans -> qinfo_lock -> write to gf
 *	     -> ip_alloc_sem of lf -> alloc space for lf
 *	     -> write to lf
 * Release last reference to dquot:
 *   dq_lock -> ocfs2_lock_global_qf -> start_trans -> qinfo_lock -> write to gf
 *	     -> write to lf
 * Note that all the above operations also hold the inode cluster lock of lf.
 * Recovery:
 *   inode cluster lock of recovered lf
 *     -> read bitmaps -> ip_alloc_sem of lf
 *     -> ocfs2_lock_global_qf -> start_trans -> dqio_sem -> qinfo_lock ->
 *        write to gf
 */

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

	d->dqb_id = cpu_to_le32(from_kqid(&init_user_ns, dquot->dq_id));
	d->dqb_use_count = cpu_to_le32(OCFS2_DQUOT(dquot)->dq_use_count);
	d->dqb_ihardlimit = cpu_to_le64(m->dqb_ihardlimit);
	d->dqb_isoftlimit = cpu_to_le64(m->dqb_isoftlimit);
	d->dqb_curinodes = cpu_to_le64(m->dqb_curinodes);
	d->dqb_bhardlimit = cpu_to_le64(m->dqb_bhardlimit);
	d->dqb_bsoftlimit = cpu_to_le64(m->dqb_bsoftlimit);
	d->dqb_curspace = cpu_to_le64(m->dqb_curspace);
	d->dqb_btime = cpu_to_le64(m->dqb_btime);
	d->dqb_itime = cpu_to_le64(m->dqb_itime);
	d->dqb_pad1 = d->dqb_pad2 = 0;
}

static int ocfs2_global_is_id(void *dp, struct dquot *dquot)
{
	struct ocfs2_global_disk_dqblk *d = dp;
	struct ocfs2_mem_dqinfo *oinfo =
			sb_dqinfo(dquot->dq_sb, dquot->dq_id.type)->dqi_priv;

	if (qtree_entry_unused(&oinfo->dqi_gi, dp))
		return 0;

	return qid_eq(make_kqid(&init_user_ns, dquot->dq_id.type,
				le32_to_cpu(d->dqb_id)),
		      dquot->dq_id);
}

const struct qtree_fmt_operations ocfs2_global_ops = {
	.mem2disk_dqblk = ocfs2_global_mem2diskdqb,
	.disk2mem_dqblk = ocfs2_global_disk2memdqb,
	.is_id = ocfs2_global_is_id,
};

int ocfs2_validate_quota_block(struct super_block *sb, struct buffer_head *bh)
{
	struct ocfs2_disk_dqtrailer *dqt =
		ocfs2_block_dqtrailer(sb->s_blocksize, bh->b_data);

	trace_ocfs2_validate_quota_block((unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * If the ecc fails, we return the error but otherwise
	 * leave the filesystem running.  We know any error is
	 * local to this block.
	 */
	return ocfs2_validate_meta_ecc(sb, bh->b_data, &dqt->dq_check);
}

int ocfs2_read_quota_phys_block(struct inode *inode, u64 p_block,
				struct buffer_head **bhp)
{
	int rc;

	*bhp = NULL;
	rc = ocfs2_read_blocks(INODE_CACHE(inode), p_block, 1, bhp, 0,
			       ocfs2_validate_quota_block);
	if (rc)
		mlog_errno(rc);
	return rc;
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
	u64 pblock = 0, pcount = 0;

	if (off > i_size)
		return 0;
	if (off + len > i_size)
		len = i_size - off;
	toread = len;
	while (toread > 0) {
		tocopy = min_t(size_t, (sb->s_blocksize - offset), toread);
		if (!pcount) {
			err = ocfs2_extent_map_get_blocks(gqinode, blk, &pblock,
							  &pcount, NULL);
			if (err) {
				mlog_errno(err);
				return err;
			}
		} else {
			pcount--;
			pblock++;
		}
		bh = NULL;
		err = ocfs2_read_quota_phys_block(gqinode, pblock, &bh);
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
	u64 pblock, pcount;

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

	if (i_size_read(gqinode) < off + len) {
		loff_t rounded_end =
				ocfs2_align_bytes_to_blocks(sb, off + len);

		/* Space is already allocated in ocfs2_acquire_dquot() */
		err = ocfs2_simple_size_update(gqinode,
					       oinfo->dqi_gqi_bh,
					       rounded_end);
		if (err < 0)
			goto out;
		new = 1;
	}
	err = ocfs2_extent_map_get_blocks(gqinode, blk, &pblock, &pcount, NULL);
	if (err) {
		mlog_errno(err);
		goto out;
	}
	/* Not rewriting whole block? */
	if ((offset || len < sb->s_blocksize - OCFS2_QBLK_RESERVED_SPACE) &&
	    !new) {
		err = ocfs2_read_quota_phys_block(gqinode, pblock, &bh);
		ja_type = OCFS2_JOURNAL_ACCESS_WRITE;
	} else {
		bh = sb_getblk(sb, pblock);
		if (!bh)
			err = -ENOMEM;
		ja_type = OCFS2_JOURNAL_ACCESS_CREATE;
	}
	if (err) {
		mlog_errno(err);
		goto out;
	}
	lock_buffer(bh);
	if (new)
		memset(bh->b_data, 0, sb->s_blocksize);
	memcpy(bh->b_data + offset, data, len);
	flush_dcache_page(bh->b_page);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	ocfs2_set_buffer_uptodate(INODE_CACHE(gqinode), bh);
	err = ocfs2_journal_access_dq(handle, INODE_CACHE(gqinode), bh,
				      ja_type);
	if (err < 0) {
		brelse(bh);
		goto out;
	}
	ocfs2_journal_dirty(handle, bh);
	brelse(bh);
out:
	if (err) {
		mlog_errno(err);
		return err;
	}
	gqinode->i_version++;
	ocfs2_mark_inode_dirty(handle, gqinode, oinfo->dqi_gqi_bh);
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
	if (ex) {
		inode_lock(oinfo->dqi_gqinode);
		down_write(&OCFS2_I(oinfo->dqi_gqinode)->ip_alloc_sem);
	} else {
		down_read(&OCFS2_I(oinfo->dqi_gqinode)->ip_alloc_sem);
	}
	return 0;
}

void ocfs2_unlock_global_qf(struct ocfs2_mem_dqinfo *oinfo, int ex)
{
	if (ex) {
		up_write(&OCFS2_I(oinfo->dqi_gqinode)->ip_alloc_sem);
		inode_unlock(oinfo->dqi_gqinode);
	} else {
		up_read(&OCFS2_I(oinfo->dqi_gqinode)->ip_alloc_sem);
	}
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
	unsigned int ino[OCFS2_MAXQUOTAS] = { USER_QUOTA_SYSTEM_INODE,
					      GROUP_QUOTA_SYSTEM_INODE };
	struct ocfs2_global_disk_dqinfo dinfo;
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct ocfs2_mem_dqinfo *oinfo = info->dqi_priv;
	u64 pcount;
	int status;

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

	status = ocfs2_extent_map_get_blocks(gqinode, 0, &oinfo->dqi_giblk,
					     &pcount, NULL);
	if (status < 0)
		goto out_unlock;

	status = ocfs2_qinfo_lock(oinfo, 0);
	if (status < 0)
		goto out_unlock;
	status = sb->s_op->quota_read(sb, type, (char *)&dinfo,
				      sizeof(struct ocfs2_global_disk_dqinfo),
				      OCFS2_GLOBAL_INFO_OFF);
	ocfs2_qinfo_unlock(oinfo, 0);
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
	oinfo->dqi_gi.dqi_blocks = le32_to_cpu(dinfo.dqi_blocks);
	oinfo->dqi_gi.dqi_free_blk = le32_to_cpu(dinfo.dqi_free_blk);
	oinfo->dqi_gi.dqi_free_entry = le32_to_cpu(dinfo.dqi_free_entry);
	oinfo->dqi_gi.dqi_blocksize_bits = sb->s_blocksize_bits;
	oinfo->dqi_gi.dqi_usable_bs = sb->s_blocksize -
						OCFS2_QBLK_RESERVED_SPACE;
	oinfo->dqi_gi.dqi_qtree_depth = qtree_depth(&oinfo->dqi_gi);
	INIT_DELAYED_WORK(&oinfo->dqi_sync_work, qsync_work_fn);
	schedule_delayed_work(&oinfo->dqi_sync_work,
			      msecs_to_jiffies(oinfo->dqi_syncms));

out_err:
	return status;
out_unlock:
	ocfs2_unlock_global_qf(oinfo, 0);
	mlog_errno(status);
	goto out_err;
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
	struct quota_info *dqopt = sb_dqopt(sb);
	struct ocfs2_mem_dqinfo *info = dqopt->info[type].dqi_priv;

	down_write(&dqopt->dqio_sem);
	err = ocfs2_qinfo_lock(info, 1);
	if (err < 0)
		goto out_sem;
	err = __ocfs2_global_write_info(sb, type);
	ocfs2_qinfo_unlock(info, 1);
out_sem:
	up_write(&dqopt->dqio_sem);
	return err;
}

static int ocfs2_global_qinit_alloc(struct super_block *sb, int type)
{
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;

	/*
	 * We may need to allocate tree blocks and a leaf block but not the
	 * root block
	 */
	return oinfo->dqi_gi.dqi_qtree_depth;
}

static int ocfs2_calc_global_qinit_credits(struct super_block *sb, int type)
{
	/* We modify all the allocated blocks, tree root, info block and
	 * the inode */
	return (ocfs2_global_qinit_alloc(sb, type) + 2) *
			OCFS2_QUOTA_BLOCK_UPDATE_CREDITS + 1;
}

/* Sync local information about quota modifications with global quota file.
 * Caller must have started the transaction and obtained exclusive lock for
 * global quota file inode */
int __ocfs2_sync_dquot(struct dquot *dquot, int freeing)
{
	int err, err2;
	struct super_block *sb = dquot->dq_sb;
	int type = dquot->dq_id.type;
	struct ocfs2_mem_dqinfo *info = sb_dqinfo(sb, type)->dqi_priv;
	struct ocfs2_global_disk_dqblk dqblk;
	s64 spacechange, inodechange;
	time64_t olditime, oldbtime;

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
	spin_lock(&dquot->dq_dqb_lock);
	spacechange = dquot->dq_dqb.dqb_curspace -
					OCFS2_DQUOT(dquot)->dq_origspace;
	inodechange = dquot->dq_dqb.dqb_curinodes -
					OCFS2_DQUOT(dquot)->dq_originodes;
	olditime = dquot->dq_dqb.dqb_itime;
	oldbtime = dquot->dq_dqb.dqb_btime;
	ocfs2_global_disk2memdqb(dquot, &dqblk);
	trace_ocfs2_sync_dquot(from_kqid(&init_user_ns, dquot->dq_id),
			       dquot->dq_dqb.dqb_curspace,
			       (long long)spacechange,
			       dquot->dq_dqb.dqb_curinodes,
			       (long long)inodechange);
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
	spin_unlock(&dquot->dq_dqb_lock);
	err = ocfs2_qinfo_lock(info, freeing);
	if (err < 0) {
		mlog(ML_ERROR, "Failed to lock quota info, losing quota write"
			       " (type=%d, id=%u)\n", dquot->dq_id.type,
			       (unsigned)from_kqid(&init_user_ns, dquot->dq_id));
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

	trace_ocfs2_sync_dquot_helper(from_kqid(&init_user_ns, dquot->dq_id),
				      dquot->dq_id.type,
				      type, sb->s_id);
	if (type != dquot->dq_id.type)
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
	down_write(&sb_dqopt(sb)->dqio_sem);
	status = ocfs2_sync_dquot(dquot);
	if (status < 0)
		mlog_errno(status);
	/* We have to write local structure as well... */
	status = ocfs2_local_write_dquot(dquot);
	if (status < 0)
		mlog_errno(status);
	up_write(&sb_dqopt(sb)->dqio_sem);
	ocfs2_commit_trans(osb, handle);
out_ilock:
	ocfs2_unlock_global_qf(oinfo, 1);
out:
	return status;
}

static void qsync_work_fn(struct work_struct *work)
{
	struct ocfs2_mem_dqinfo *oinfo = container_of(work,
						      struct ocfs2_mem_dqinfo,
						      dqi_sync_work.work);
	struct super_block *sb = oinfo->dqi_gqinode->i_sb;

	/*
	 * We have to be careful here not to deadlock on s_umount as umount
	 * disabling quotas may be in progress and it waits for this work to
	 * complete. If trylock fails, we'll do the sync next time...
	 */
	if (down_read_trylock(&sb->s_umount)) {
		dquot_scan_active(sb, ocfs2_sync_dquot_helper, oinfo->dqi_type);
		up_read(&sb->s_umount);
	}
	schedule_delayed_work(&oinfo->dqi_sync_work,
			      msecs_to_jiffies(oinfo->dqi_syncms));
}

/*
 *  Wrappers for generic quota functions
 */

static int ocfs2_write_dquot(struct dquot *dquot)
{
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(dquot->dq_sb);
	int status = 0;

	trace_ocfs2_write_dquot(from_kqid(&init_user_ns, dquot->dq_id),
				dquot->dq_id.type);

	handle = ocfs2_start_trans(osb, OCFS2_QWRITE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}
	down_write(&sb_dqopt(dquot->dq_sb)->dqio_sem);
	status = ocfs2_local_write_dquot(dquot);
	up_write(&sb_dqopt(dquot->dq_sb)->dqio_sem);
	ocfs2_commit_trans(osb, handle);
out:
	return status;
}

static int ocfs2_calc_qdel_credits(struct super_block *sb, int type)
{
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;
	/*
	 * We modify tree, leaf block, global info, local chunk header,
	 * global and local inode; OCFS2_QINFO_WRITE_CREDITS already
	 * accounts for inode update
	 */
	return (oinfo->dqi_gi.dqi_qtree_depth + 2) *
	       OCFS2_QUOTA_BLOCK_UPDATE_CREDITS +
	       OCFS2_QINFO_WRITE_CREDITS +
	       OCFS2_INODE_UPDATE_CREDITS;
}

void ocfs2_drop_dquot_refs(struct work_struct *work)
{
	struct ocfs2_super *osb = container_of(work, struct ocfs2_super,
					       dquot_drop_work);
	struct llist_node *list;
	struct ocfs2_dquot *odquot, *next_odquot;

	list = llist_del_all(&osb->dquot_drop_list);
	llist_for_each_entry_safe(odquot, next_odquot, list, list) {
		/* Drop the reference we acquired in ocfs2_dquot_release() */
		dqput(&odquot->dq_dquot);
	}
}

/*
 * Called when the last reference to dquot is dropped. If we are called from
 * downconvert thread, we cannot do all the handling here because grabbing
 * quota lock could deadlock (the node holding the quota lock could need some
 * other cluster lock to proceed but with blocked downconvert thread we cannot
 * release any lock).
 */
static int ocfs2_release_dquot(struct dquot *dquot)
{
	handle_t *handle;
	struct ocfs2_mem_dqinfo *oinfo =
			sb_dqinfo(dquot->dq_sb, dquot->dq_id.type)->dqi_priv;
	struct ocfs2_super *osb = OCFS2_SB(dquot->dq_sb);
	int status = 0;

	trace_ocfs2_release_dquot(from_kqid(&init_user_ns, dquot->dq_id),
				  dquot->dq_id.type);

	mutex_lock(&dquot->dq_lock);
	/* Check whether we are not racing with some other dqget() */
	if (atomic_read(&dquot->dq_count) > 1)
		goto out;
	/* Running from downconvert thread? Postpone quota processing to wq */
	if (current == osb->dc_task) {
		/*
		 * Grab our own reference to dquot and queue it for delayed
		 * dropping.  Quota code rechecks after calling
		 * ->release_dquot() and won't free dquot structure.
		 */
		dqgrab(dquot);
		/* First entry on list -> queue work */
		if (llist_add(&OCFS2_DQUOT(dquot)->list, &osb->dquot_drop_list))
			queue_work(osb->ocfs2_wq, &osb->dquot_drop_work);
		goto out;
	}
	status = ocfs2_lock_global_qf(oinfo, 1);
	if (status < 0)
		goto out;
	handle = ocfs2_start_trans(osb,
		ocfs2_calc_qdel_credits(dquot->dq_sb, dquot->dq_id.type));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out_ilock;
	}

	status = ocfs2_global_release_dquot(dquot);
	if (status < 0) {
		mlog_errno(status);
		goto out_trans;
	}
	status = ocfs2_local_release_dquot(handle, dquot);
	/*
	 * If we fail here, we cannot do much as global structure is
	 * already released. So just complain...
	 */
	if (status < 0)
		mlog_errno(status);
	/*
	 * Clear dq_off so that we search for the structure in quota file next
	 * time we acquire it. The structure might be deleted and reallocated
	 * elsewhere by another node while our dquot structure is on freelist.
	 */
	dquot->dq_off = 0;
	clear_bit(DQ_ACTIVE_B, &dquot->dq_flags);
out_trans:
	ocfs2_commit_trans(osb, handle);
out_ilock:
	ocfs2_unlock_global_qf(oinfo, 1);
out:
	mutex_unlock(&dquot->dq_lock);
	if (status)
		mlog_errno(status);
	return status;
}

/*
 * Read global dquot structure from disk or create it if it does
 * not exist. Also update use count of the global structure and
 * create structure in node-local quota file.
 */
static int ocfs2_acquire_dquot(struct dquot *dquot)
{
	int status = 0, err;
	int ex = 0;
	struct super_block *sb = dquot->dq_sb;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	int type = dquot->dq_id.type;
	struct ocfs2_mem_dqinfo *info = sb_dqinfo(sb, type)->dqi_priv;
	struct inode *gqinode = info->dqi_gqinode;
	int need_alloc = ocfs2_global_qinit_alloc(sb, type);
	handle_t *handle;

	trace_ocfs2_acquire_dquot(from_kqid(&init_user_ns, dquot->dq_id),
				  type);
	mutex_lock(&dquot->dq_lock);
	/*
	 * We need an exclusive lock, because we're going to update use count
	 * and instantiate possibly new dquot structure
	 */
	status = ocfs2_lock_global_qf(info, 1);
	if (status < 0)
		goto out;
	status = ocfs2_qinfo_lock(info, 0);
	if (status < 0)
		goto out_dq;
	/*
	 * We always want to read dquot structure from disk because we don't
	 * know what happened with it while it was on freelist.
	 */
	status = qtree_read_dquot(&info->dqi_gi, dquot);
	ocfs2_qinfo_unlock(info, 0);
	if (status < 0)
		goto out_dq;

	OCFS2_DQUOT(dquot)->dq_use_count++;
	OCFS2_DQUOT(dquot)->dq_origspace = dquot->dq_dqb.dqb_curspace;
	OCFS2_DQUOT(dquot)->dq_originodes = dquot->dq_dqb.dqb_curinodes;
	if (!dquot->dq_off) {	/* No real quota entry? */
		ex = 1;
		/*
		 * Add blocks to quota file before we start a transaction since
		 * locking allocators ranks above a transaction start
		 */
		WARN_ON(journal_current_handle());
		status = ocfs2_extend_no_holes(gqinode, NULL,
			i_size_read(gqinode) + (need_alloc << sb->s_blocksize_bits),
			i_size_read(gqinode));
		if (status < 0)
			goto out_dq;
	}

	handle = ocfs2_start_trans(osb,
				   ocfs2_calc_global_qinit_credits(sb, type));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		goto out_dq;
	}
	status = ocfs2_qinfo_lock(info, ex);
	if (status < 0)
		goto out_trans;
	status = qtree_write_dquot(&info->dqi_gi, dquot);
	if (ex && info_dirty(sb_dqinfo(sb, type))) {
		err = __ocfs2_global_write_info(sb, type);
		if (!status)
			status = err;
	}
	ocfs2_qinfo_unlock(info, ex);
out_trans:
	ocfs2_commit_trans(osb, handle);
out_dq:
	ocfs2_unlock_global_qf(info, 1);
	if (status < 0)
		goto out;

	status = ocfs2_create_local_dquot(dquot);
	if (status < 0)
		goto out;
	set_bit(DQ_ACTIVE_B, &dquot->dq_flags);
out:
	mutex_unlock(&dquot->dq_lock);
	if (status)
		mlog_errno(status);
	return status;
}

static int ocfs2_get_next_id(struct super_block *sb, struct kqid *qid)
{
	int type = qid->type;
	struct ocfs2_mem_dqinfo *info = sb_dqinfo(sb, type)->dqi_priv;
	int status = 0;

	trace_ocfs2_get_next_id(from_kqid(&init_user_ns, *qid), type);
	if (!sb_has_quota_loaded(sb, type)) {
		status = -ESRCH;
		goto out;
	}
	status = ocfs2_lock_global_qf(info, 0);
	if (status < 0)
		goto out;
	status = ocfs2_qinfo_lock(info, 0);
	if (status < 0)
		goto out_global;
	status = qtree_get_next_id(&info->dqi_gi, qid);
	ocfs2_qinfo_unlock(info, 0);
out_global:
	ocfs2_unlock_global_qf(info, 0);
out:
	/*
	 * Avoid logging ENOENT since it just means there isn't next ID and
	 * ESRCH which means quota isn't enabled for the filesystem.
	 */
	if (status && status != -ENOENT && status != -ESRCH)
		mlog_errno(status);
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
	int type = dquot->dq_id.type;
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(sb);

	trace_ocfs2_mark_dquot_dirty(from_kqid(&init_user_ns, dquot->dq_id),
				     type);

	/* In case user set some limits, sync dquot immediately to global
	 * quota file so that information propagates quicker */
	spin_lock(&dquot->dq_dqb_lock);
	if (dquot->dq_flags & mask)
		sync = 1;
	spin_unlock(&dquot->dq_dqb_lock);
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
	down_write(&sb_dqopt(sb)->dqio_sem);
	status = ocfs2_sync_dquot(dquot);
	if (status < 0) {
		mlog_errno(status);
		goto out_dlock;
	}
	/* Now write updated local dquot structure */
	status = ocfs2_local_write_dquot(dquot);
out_dlock:
	up_write(&sb_dqopt(sb)->dqio_sem);
	ocfs2_commit_trans(osb, handle);
out_ilock:
	ocfs2_unlock_global_qf(oinfo, 1);
out:
	if (status)
		mlog_errno(status);
	return status;
}

/* This should happen only after set_dqinfo(). */
static int ocfs2_write_info(struct super_block *sb, int type)
{
	handle_t *handle;
	int status = 0;
	struct ocfs2_mem_dqinfo *oinfo = sb_dqinfo(sb, type)->dqi_priv;

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
	if (status)
		mlog_errno(status);
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

const struct dquot_operations ocfs2_quota_operations = {
	/* We never make dquot dirty so .write_dquot is never called */
	.acquire_dquot	= ocfs2_acquire_dquot,
	.release_dquot	= ocfs2_release_dquot,
	.mark_dirty	= ocfs2_mark_dquot_dirty,
	.write_info	= ocfs2_write_info,
	.alloc_dquot	= ocfs2_alloc_dquot,
	.destroy_dquot	= ocfs2_destroy_dquot,
	.get_next_id	= ocfs2_get_next_id,
};
