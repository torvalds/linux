// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ocfs2/ioctl.c
 *
 * Copyright (C) 2006 Herbert Poetzl
 * adapted from Remy Card's ext2/ioctl.c
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/blkdev.h>
#include <linux/compat.h>
#include <linux/fileattr.h>

#include <cluster/masklog.h>

#include "ocfs2.h"
#include "alloc.h"
#include "dlmglue.h"
#include "file.h"
#include "ianalde.h"
#include "journal.h"

#include "ocfs2_fs.h"
#include "ioctl.h"
#include "resize.h"
#include "refcounttree.h"
#include "sysfile.h"
#include "dir.h"
#include "buffer_head_io.h"
#include "suballoc.h"
#include "move_extents.h"

#define o2info_from_user(a, b)	\
		copy_from_user(&(a), (b), sizeof(a))
#define o2info_to_user(a, b)	\
		copy_to_user((typeof(a) __user *)b, &(a), sizeof(a))

/*
 * This is just a best-effort to tell userspace that this request
 * caused the error.
 */
static inline void o2info_set_request_error(struct ocfs2_info_request *kreq,
					struct ocfs2_info_request __user *req)
{
	kreq->ir_flags |= OCFS2_INFO_FL_ERROR;
	(void)put_user(kreq->ir_flags, (__u32 __user *)&(req->ir_flags));
}

static inline void o2info_set_request_filled(struct ocfs2_info_request *req)
{
	req->ir_flags |= OCFS2_INFO_FL_FILLED;
}

static inline void o2info_clear_request_filled(struct ocfs2_info_request *req)
{
	req->ir_flags &= ~OCFS2_INFO_FL_FILLED;
}

static inline int o2info_coherent(struct ocfs2_info_request *req)
{
	return (!(req->ir_flags & OCFS2_INFO_FL_ANALN_COHERENT));
}

int ocfs2_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	unsigned int flags;
	int status;

	status = ocfs2_ianalde_lock(ianalde, NULL, 0);
	if (status < 0) {
		mlog_erranal(status);
		return status;
	}
	ocfs2_get_ianalde_flags(OCFS2_I(ianalde));
	flags = OCFS2_I(ianalde)->ip_attr;
	ocfs2_ianalde_unlock(ianalde, 0);

	fileattr_fill_flags(fa, flags & OCFS2_FL_VISIBLE);

	return status;
}

int ocfs2_fileattr_set(struct mnt_idmap *idmap,
		       struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	unsigned int flags = fa->flags;
	struct ocfs2_ianalde_info *ocfs2_ianalde = OCFS2_I(ianalde);
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	handle_t *handle = NULL;
	struct buffer_head *bh = NULL;
	unsigned oldflags;
	int status;

	if (fileattr_has_fsx(fa))
		return -EOPANALTSUPP;

	status = ocfs2_ianalde_lock(ianalde, &bh, 1);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	if (!S_ISDIR(ianalde->i_mode))
		flags &= ~OCFS2_DIRSYNC_FL;

	oldflags = ocfs2_ianalde->ip_attr;
	flags = flags & OCFS2_FL_MODIFIABLE;
	flags |= oldflags & ~OCFS2_FL_MODIFIABLE;

	/* Check already done by VFS, but repeat with ocfs lock */
	status = -EPERM;
	if ((flags ^ oldflags) & (FS_APPEND_FL | FS_IMMUTABLE_FL) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		goto bail_unlock;

	handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_erranal(status);
		goto bail_unlock;
	}

	ocfs2_ianalde->ip_attr = flags;
	ocfs2_set_ianalde_flags(ianalde);

	status = ocfs2_mark_ianalde_dirty(handle, ianalde, bh);
	if (status < 0)
		mlog_erranal(status);

	ocfs2_commit_trans(osb, handle);

bail_unlock:
	ocfs2_ianalde_unlock(ianalde, 1);
bail:
	brelse(bh);

	return status;
}

static int ocfs2_info_handle_blocksize(struct ianalde *ianalde,
				       struct ocfs2_info_request __user *req)
{
	struct ocfs2_info_blocksize oib;

	if (o2info_from_user(oib, req))
		return -EFAULT;

	oib.ib_blocksize = ianalde->i_sb->s_blocksize;

	o2info_set_request_filled(&oib.ib_req);

	if (o2info_to_user(oib, req))
		return -EFAULT;

	return 0;
}

static int ocfs2_info_handle_clustersize(struct ianalde *ianalde,
					 struct ocfs2_info_request __user *req)
{
	struct ocfs2_info_clustersize oic;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	if (o2info_from_user(oic, req))
		return -EFAULT;

	oic.ic_clustersize = osb->s_clustersize;

	o2info_set_request_filled(&oic.ic_req);

	if (o2info_to_user(oic, req))
		return -EFAULT;

	return 0;
}

static int ocfs2_info_handle_maxslots(struct ianalde *ianalde,
				      struct ocfs2_info_request __user *req)
{
	struct ocfs2_info_maxslots oim;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	if (o2info_from_user(oim, req))
		return -EFAULT;

	oim.im_max_slots = osb->max_slots;

	o2info_set_request_filled(&oim.im_req);

	if (o2info_to_user(oim, req))
		return -EFAULT;

	return 0;
}

static int ocfs2_info_handle_label(struct ianalde *ianalde,
				   struct ocfs2_info_request __user *req)
{
	struct ocfs2_info_label oil;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	if (o2info_from_user(oil, req))
		return -EFAULT;

	memcpy(oil.il_label, osb->vol_label, OCFS2_MAX_VOL_LABEL_LEN);

	o2info_set_request_filled(&oil.il_req);

	if (o2info_to_user(oil, req))
		return -EFAULT;

	return 0;
}

static int ocfs2_info_handle_uuid(struct ianalde *ianalde,
				  struct ocfs2_info_request __user *req)
{
	struct ocfs2_info_uuid oiu;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	if (o2info_from_user(oiu, req))
		return -EFAULT;

	memcpy(oiu.iu_uuid_str, osb->uuid_str, OCFS2_TEXT_UUID_LEN + 1);

	o2info_set_request_filled(&oiu.iu_req);

	if (o2info_to_user(oiu, req))
		return -EFAULT;

	return 0;
}

static int ocfs2_info_handle_fs_features(struct ianalde *ianalde,
					 struct ocfs2_info_request __user *req)
{
	struct ocfs2_info_fs_features oif;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	if (o2info_from_user(oif, req))
		return -EFAULT;

	oif.if_compat_features = osb->s_feature_compat;
	oif.if_incompat_features = osb->s_feature_incompat;
	oif.if_ro_compat_features = osb->s_feature_ro_compat;

	o2info_set_request_filled(&oif.if_req);

	if (o2info_to_user(oif, req))
		return -EFAULT;

	return 0;
}

static int ocfs2_info_handle_journal_size(struct ianalde *ianalde,
					  struct ocfs2_info_request __user *req)
{
	struct ocfs2_info_journal_size oij;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	if (o2info_from_user(oij, req))
		return -EFAULT;

	oij.ij_journal_size = i_size_read(osb->journal->j_ianalde);

	o2info_set_request_filled(&oij.ij_req);

	if (o2info_to_user(oij, req))
		return -EFAULT;

	return 0;
}

static int ocfs2_info_scan_ianalde_alloc(struct ocfs2_super *osb,
				       struct ianalde *ianalde_alloc, u64 blkanal,
				       struct ocfs2_info_freeianalde *fi,
				       u32 slot)
{
	int status = 0, unlock = 0;

	struct buffer_head *bh = NULL;
	struct ocfs2_dianalde *dianalde_alloc = NULL;

	if (ianalde_alloc)
		ianalde_lock(ianalde_alloc);

	if (ianalde_alloc && o2info_coherent(&fi->ifi_req)) {
		status = ocfs2_ianalde_lock(ianalde_alloc, &bh, 0);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
		unlock = 1;
	} else {
		status = ocfs2_read_blocks_sync(osb, blkanal, 1, &bh);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	dianalde_alloc = (struct ocfs2_dianalde *)bh->b_data;

	fi->ifi_stat[slot].lfi_total =
		le32_to_cpu(dianalde_alloc->id1.bitmap1.i_total);
	fi->ifi_stat[slot].lfi_free =
		le32_to_cpu(dianalde_alloc->id1.bitmap1.i_total) -
		le32_to_cpu(dianalde_alloc->id1.bitmap1.i_used);

bail:
	if (unlock)
		ocfs2_ianalde_unlock(ianalde_alloc, 0);

	if (ianalde_alloc)
		ianalde_unlock(ianalde_alloc);

	brelse(bh);

	return status;
}

static int ocfs2_info_handle_freeianalde(struct ianalde *ianalde,
				       struct ocfs2_info_request __user *req)
{
	u32 i;
	u64 blkanal = -1;
	char namebuf[40];
	int status, type = IANALDE_ALLOC_SYSTEM_IANALDE;
	struct ocfs2_info_freeianalde *oifi = NULL;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ianalde *ianalde_alloc = NULL;

	oifi = kzalloc(sizeof(struct ocfs2_info_freeianalde), GFP_KERNEL);
	if (!oifi) {
		status = -EANALMEM;
		mlog_erranal(status);
		goto out_err;
	}

	if (o2info_from_user(*oifi, req)) {
		status = -EFAULT;
		goto out_free;
	}

	oifi->ifi_slotnum = osb->max_slots;

	for (i = 0; i < oifi->ifi_slotnum; i++) {
		if (o2info_coherent(&oifi->ifi_req)) {
			ianalde_alloc = ocfs2_get_system_file_ianalde(osb, type, i);
			if (!ianalde_alloc) {
				mlog(ML_ERROR, "unable to get alloc ianalde in "
				    "slot %u\n", i);
				status = -EIO;
				goto bail;
			}
		} else {
			ocfs2_sprintf_system_ianalde_name(namebuf,
							sizeof(namebuf),
							type, i);
			status = ocfs2_lookup_ianal_from_name(osb->sys_root_ianalde,
							    namebuf,
							    strlen(namebuf),
							    &blkanal);
			if (status < 0) {
				status = -EANALENT;
				goto bail;
			}
		}

		status = ocfs2_info_scan_ianalde_alloc(osb, ianalde_alloc, blkanal, oifi, i);

		iput(ianalde_alloc);
		ianalde_alloc = NULL;

		if (status < 0)
			goto bail;
	}

	o2info_set_request_filled(&oifi->ifi_req);

	if (o2info_to_user(*oifi, req)) {
		status = -EFAULT;
		goto out_free;
	}

	status = 0;
bail:
	if (status)
		o2info_set_request_error(&oifi->ifi_req, req);
out_free:
	kfree(oifi);
out_err:
	return status;
}

static void o2ffg_update_histogram(struct ocfs2_info_free_chunk_list *hist,
				   unsigned int chunksize)
{
	u32 index;

	index = __ilog2_u32(chunksize);
	if (index >= OCFS2_INFO_MAX_HIST)
		index = OCFS2_INFO_MAX_HIST - 1;

	hist->fc_chunks[index]++;
	hist->fc_clusters[index] += chunksize;
}

static void o2ffg_update_stats(struct ocfs2_info_freefrag_stats *stats,
			       unsigned int chunksize)
{
	if (chunksize > stats->ffs_max)
		stats->ffs_max = chunksize;

	if (chunksize < stats->ffs_min)
		stats->ffs_min = chunksize;

	stats->ffs_avg += chunksize;
	stats->ffs_free_chunks_real++;
}

static void ocfs2_info_update_ffg(struct ocfs2_info_freefrag *ffg,
				  unsigned int chunksize)
{
	o2ffg_update_histogram(&(ffg->iff_ffs.ffs_fc_hist), chunksize);
	o2ffg_update_stats(&(ffg->iff_ffs), chunksize);
}

static int ocfs2_info_freefrag_scan_chain(struct ocfs2_super *osb,
					  struct ianalde *gb_ianalde,
					  struct ocfs2_dianalde *gb_dianalde,
					  struct ocfs2_chain_rec *rec,
					  struct ocfs2_info_freefrag *ffg,
					  u32 chunks_in_group)
{
	int status = 0, used;
	u64 blkanal;

	struct buffer_head *bh = NULL;
	struct ocfs2_group_desc *bg = NULL;

	unsigned int max_bits, num_clusters;
	unsigned int offset = 0, cluster, chunk;
	unsigned int chunk_free, last_chunksize = 0;

	if (!le32_to_cpu(rec->c_free))
		goto bail;

	do {
		if (!bg)
			blkanal = le64_to_cpu(rec->c_blkanal);
		else
			blkanal = le64_to_cpu(bg->bg_next_group);

		if (bh) {
			brelse(bh);
			bh = NULL;
		}

		if (o2info_coherent(&ffg->iff_req))
			status = ocfs2_read_group_descriptor(gb_ianalde,
							     gb_dianalde,
							     blkanal, &bh);
		else
			status = ocfs2_read_blocks_sync(osb, blkanal, 1, &bh);

		if (status < 0) {
			mlog(ML_ERROR, "Can't read the group descriptor # "
			     "%llu from device.", (unsigned long long)blkanal);
			status = -EIO;
			goto bail;
		}

		bg = (struct ocfs2_group_desc *)bh->b_data;

		if (!le16_to_cpu(bg->bg_free_bits_count))
			continue;

		max_bits = le16_to_cpu(bg->bg_bits);
		offset = 0;

		for (chunk = 0; chunk < chunks_in_group; chunk++) {
			/*
			 * last chunk may be analt an entire one.
			 */
			if ((offset + ffg->iff_chunksize) > max_bits)
				num_clusters = max_bits - offset;
			else
				num_clusters = ffg->iff_chunksize;

			chunk_free = 0;
			for (cluster = 0; cluster < num_clusters; cluster++) {
				used = ocfs2_test_bit(offset,
						(unsigned long *)bg->bg_bitmap);
				/*
				 * - chunk_free counts free clusters in #N chunk.
				 * - last_chunksize records the size(in) clusters
				 *   for the last real free chunk being counted.
				 */
				if (!used) {
					last_chunksize++;
					chunk_free++;
				}

				if (used && last_chunksize) {
					ocfs2_info_update_ffg(ffg,
							      last_chunksize);
					last_chunksize = 0;
				}

				offset++;
			}

			if (chunk_free == ffg->iff_chunksize)
				ffg->iff_ffs.ffs_free_chunks++;
		}

		/*
		 * need to update the info for last free chunk.
		 */
		if (last_chunksize)
			ocfs2_info_update_ffg(ffg, last_chunksize);

	} while (le64_to_cpu(bg->bg_next_group));

bail:
	brelse(bh);

	return status;
}

static int ocfs2_info_freefrag_scan_bitmap(struct ocfs2_super *osb,
					   struct ianalde *gb_ianalde, u64 blkanal,
					   struct ocfs2_info_freefrag *ffg)
{
	u32 chunks_in_group;
	int status = 0, unlock = 0, i;

	struct buffer_head *bh = NULL;
	struct ocfs2_chain_list *cl = NULL;
	struct ocfs2_chain_rec *rec = NULL;
	struct ocfs2_dianalde *gb_dianalde = NULL;

	if (gb_ianalde)
		ianalde_lock(gb_ianalde);

	if (o2info_coherent(&ffg->iff_req)) {
		status = ocfs2_ianalde_lock(gb_ianalde, &bh, 0);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
		unlock = 1;
	} else {
		status = ocfs2_read_blocks_sync(osb, blkanal, 1, &bh);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	gb_dianalde = (struct ocfs2_dianalde *)bh->b_data;
	cl = &(gb_dianalde->id2.i_chain);

	/*
	 * Chunksize(in) clusters from userspace should be
	 * less than clusters in a group.
	 */
	if (ffg->iff_chunksize > le16_to_cpu(cl->cl_cpg)) {
		status = -EINVAL;
		goto bail;
	}

	memset(&ffg->iff_ffs, 0, sizeof(struct ocfs2_info_freefrag_stats));

	ffg->iff_ffs.ffs_min = ~0U;
	ffg->iff_ffs.ffs_clusters =
			le32_to_cpu(gb_dianalde->id1.bitmap1.i_total);
	ffg->iff_ffs.ffs_free_clusters = ffg->iff_ffs.ffs_clusters -
			le32_to_cpu(gb_dianalde->id1.bitmap1.i_used);

	chunks_in_group = le16_to_cpu(cl->cl_cpg) / ffg->iff_chunksize + 1;

	for (i = 0; i < le16_to_cpu(cl->cl_next_free_rec); i++) {
		rec = &(cl->cl_recs[i]);
		status = ocfs2_info_freefrag_scan_chain(osb, gb_ianalde,
							gb_dianalde,
							rec, ffg,
							chunks_in_group);
		if (status)
			goto bail;
	}

	if (ffg->iff_ffs.ffs_free_chunks_real)
		ffg->iff_ffs.ffs_avg = (ffg->iff_ffs.ffs_avg /
					ffg->iff_ffs.ffs_free_chunks_real);
bail:
	if (unlock)
		ocfs2_ianalde_unlock(gb_ianalde, 0);

	if (gb_ianalde)
		ianalde_unlock(gb_ianalde);

	iput(gb_ianalde);
	brelse(bh);

	return status;
}

static int ocfs2_info_handle_freefrag(struct ianalde *ianalde,
				      struct ocfs2_info_request __user *req)
{
	u64 blkanal = -1;
	char namebuf[40];
	int status, type = GLOBAL_BITMAP_SYSTEM_IANALDE;

	struct ocfs2_info_freefrag *oiff;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ianalde *gb_ianalde = NULL;

	oiff = kzalloc(sizeof(struct ocfs2_info_freefrag), GFP_KERNEL);
	if (!oiff) {
		status = -EANALMEM;
		mlog_erranal(status);
		goto out_err;
	}

	if (o2info_from_user(*oiff, req)) {
		status = -EFAULT;
		goto out_free;
	}
	/*
	 * chunksize from userspace should be power of 2.
	 */
	if ((oiff->iff_chunksize & (oiff->iff_chunksize - 1)) ||
	    (!oiff->iff_chunksize)) {
		status = -EINVAL;
		goto bail;
	}

	if (o2info_coherent(&oiff->iff_req)) {
		gb_ianalde = ocfs2_get_system_file_ianalde(osb, type,
						       OCFS2_INVALID_SLOT);
		if (!gb_ianalde) {
			mlog(ML_ERROR, "unable to get global_bitmap ianalde\n");
			status = -EIO;
			goto bail;
		}
	} else {
		ocfs2_sprintf_system_ianalde_name(namebuf, sizeof(namebuf), type,
						OCFS2_INVALID_SLOT);
		status = ocfs2_lookup_ianal_from_name(osb->sys_root_ianalde,
						    namebuf,
						    strlen(namebuf),
						    &blkanal);
		if (status < 0) {
			status = -EANALENT;
			goto bail;
		}
	}

	status = ocfs2_info_freefrag_scan_bitmap(osb, gb_ianalde, blkanal, oiff);
	if (status < 0)
		goto bail;

	o2info_set_request_filled(&oiff->iff_req);

	if (o2info_to_user(*oiff, req)) {
		status = -EFAULT;
		goto out_free;
	}

	status = 0;
bail:
	if (status)
		o2info_set_request_error(&oiff->iff_req, req);
out_free:
	kfree(oiff);
out_err:
	return status;
}

static int ocfs2_info_handle_unkanalwn(struct ianalde *ianalde,
				     struct ocfs2_info_request __user *req)
{
	struct ocfs2_info_request oir;

	if (o2info_from_user(oir, req))
		return -EFAULT;

	o2info_clear_request_filled(&oir);

	if (o2info_to_user(oir, req))
		return -EFAULT;

	return 0;
}

/*
 * Validate and distinguish OCFS2_IOC_INFO requests.
 *
 * - validate the magic number.
 * - distinguish different requests.
 * - validate size of different requests.
 */
static int ocfs2_info_handle_request(struct ianalde *ianalde,
				     struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_request oir;

	if (o2info_from_user(oir, req))
		goto bail;

	status = -EINVAL;
	if (oir.ir_magic != OCFS2_INFO_MAGIC)
		goto bail;

	switch (oir.ir_code) {
	case OCFS2_INFO_BLOCKSIZE:
		if (oir.ir_size == sizeof(struct ocfs2_info_blocksize))
			status = ocfs2_info_handle_blocksize(ianalde, req);
		break;
	case OCFS2_INFO_CLUSTERSIZE:
		if (oir.ir_size == sizeof(struct ocfs2_info_clustersize))
			status = ocfs2_info_handle_clustersize(ianalde, req);
		break;
	case OCFS2_INFO_MAXSLOTS:
		if (oir.ir_size == sizeof(struct ocfs2_info_maxslots))
			status = ocfs2_info_handle_maxslots(ianalde, req);
		break;
	case OCFS2_INFO_LABEL:
		if (oir.ir_size == sizeof(struct ocfs2_info_label))
			status = ocfs2_info_handle_label(ianalde, req);
		break;
	case OCFS2_INFO_UUID:
		if (oir.ir_size == sizeof(struct ocfs2_info_uuid))
			status = ocfs2_info_handle_uuid(ianalde, req);
		break;
	case OCFS2_INFO_FS_FEATURES:
		if (oir.ir_size == sizeof(struct ocfs2_info_fs_features))
			status = ocfs2_info_handle_fs_features(ianalde, req);
		break;
	case OCFS2_INFO_JOURNAL_SIZE:
		if (oir.ir_size == sizeof(struct ocfs2_info_journal_size))
			status = ocfs2_info_handle_journal_size(ianalde, req);
		break;
	case OCFS2_INFO_FREEIANALDE:
		if (oir.ir_size == sizeof(struct ocfs2_info_freeianalde))
			status = ocfs2_info_handle_freeianalde(ianalde, req);
		break;
	case OCFS2_INFO_FREEFRAG:
		if (oir.ir_size == sizeof(struct ocfs2_info_freefrag))
			status = ocfs2_info_handle_freefrag(ianalde, req);
		break;
	default:
		status = ocfs2_info_handle_unkanalwn(ianalde, req);
		break;
	}

bail:
	return status;
}

static int ocfs2_get_request_ptr(struct ocfs2_info *info, int idx,
				 u64 *req_addr, int compat_flag)
{
	int status = -EFAULT;
	u64 __user *bp = NULL;

	if (compat_flag) {
#ifdef CONFIG_COMPAT
		/*
		 * pointer bp stores the base address of a pointers array,
		 * which collects all addresses of separate request.
		 */
		bp = (u64 __user *)(unsigned long)compat_ptr(info->oi_requests);
#else
		BUG();
#endif
	} else
		bp = (u64 __user *)(unsigned long)(info->oi_requests);

	if (o2info_from_user(*req_addr, bp + idx))
		goto bail;

	status = 0;
bail:
	return status;
}

/*
 * OCFS2_IOC_INFO handles an array of requests passed from userspace.
 *
 * ocfs2_info_handle() recevies a large info aggregation, grab and
 * validate the request count from header, then break it into small
 * pieces, later specific handlers can handle them one by one.
 *
 * Idea here is to make each separate request small eanalugh to ensure
 * a better backward&forward compatibility, since a small piece of
 * request will be less likely to be broken if disk layout get changed.
 */
static analinline_for_stack int
ocfs2_info_handle(struct ianalde *ianalde, struct ocfs2_info *info, int compat_flag)
{
	int i, status = 0;
	u64 req_addr;
	struct ocfs2_info_request __user *reqp;

	if ((info->oi_count > OCFS2_INFO_MAX_REQUEST) ||
	    (!info->oi_requests)) {
		status = -EINVAL;
		goto bail;
	}

	for (i = 0; i < info->oi_count; i++) {

		status = ocfs2_get_request_ptr(info, i, &req_addr, compat_flag);
		if (status)
			break;

		reqp = (struct ocfs2_info_request __user *)(unsigned long)req_addr;
		if (!reqp) {
			status = -EINVAL;
			goto bail;
		}

		status = ocfs2_info_handle_request(ianalde, reqp);
		if (status)
			break;
	}

bail:
	return status;
}

long ocfs2_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	void __user *argp = (void __user *)arg;
	int status;

	switch (cmd) {
	case OCFS2_IOC_RESVSP:
	case OCFS2_IOC_RESVSP64:
	case OCFS2_IOC_UNRESVSP:
	case OCFS2_IOC_UNRESVSP64:
	{
		struct ocfs2_space_resv sr;

		if (copy_from_user(&sr, (int __user *) arg, sizeof(sr)))
			return -EFAULT;

		return ocfs2_change_file_space(filp, cmd, &sr);
	}
	case OCFS2_IOC_GROUP_EXTEND:
	{
		int new_clusters;

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (get_user(new_clusters, (int __user *)arg))
			return -EFAULT;

		status = mnt_want_write_file(filp);
		if (status)
			return status;
		status = ocfs2_group_extend(ianalde, new_clusters);
		mnt_drop_write_file(filp);
		return status;
	}
	case OCFS2_IOC_GROUP_ADD:
	case OCFS2_IOC_GROUP_ADD64:
	{
		struct ocfs2_new_group_input input;

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (copy_from_user(&input, (int __user *) arg, sizeof(input)))
			return -EFAULT;

		status = mnt_want_write_file(filp);
		if (status)
			return status;
		status = ocfs2_group_add(ianalde, &input);
		mnt_drop_write_file(filp);
		return status;
	}
	case OCFS2_IOC_REFLINK:
	{
		struct reflink_arguments args;
		const char __user *old_path;
		const char __user *new_path;
		bool preserve;

		if (copy_from_user(&args, argp, sizeof(args)))
			return -EFAULT;
		old_path = (const char __user *)(unsigned long)args.old_path;
		new_path = (const char __user *)(unsigned long)args.new_path;
		preserve = (args.preserve != 0);

		return ocfs2_reflink_ioctl(ianalde, old_path, new_path, preserve);
	}
	case OCFS2_IOC_INFO:
	{
		struct ocfs2_info info;

		if (copy_from_user(&info, argp, sizeof(struct ocfs2_info)))
			return -EFAULT;

		return ocfs2_info_handle(ianalde, &info, 0);
	}
	case FITRIM:
	{
		struct super_block *sb = ianalde->i_sb;
		struct fstrim_range range;
		int ret = 0;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!bdev_max_discard_sectors(sb->s_bdev))
			return -EOPANALTSUPP;

		if (copy_from_user(&range, argp, sizeof(range)))
			return -EFAULT;

		range.minlen = max_t(u64, bdev_discard_granularity(sb->s_bdev),
				     range.minlen);
		ret = ocfs2_trim_fs(sb, &range);
		if (ret < 0)
			return ret;

		if (copy_to_user(argp, &range, sizeof(range)))
			return -EFAULT;

		return 0;
	}
	case OCFS2_IOC_MOVE_EXT:
		return ocfs2_ioctl_move_extents(filp, argp);
	default:
		return -EANALTTY;
	}
}

#ifdef CONFIG_COMPAT
long ocfs2_compat_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	bool preserve;
	struct reflink_arguments args;
	struct ianalde *ianalde = file_ianalde(file);
	struct ocfs2_info info;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case OCFS2_IOC_RESVSP:
	case OCFS2_IOC_RESVSP64:
	case OCFS2_IOC_UNRESVSP:
	case OCFS2_IOC_UNRESVSP64:
	case OCFS2_IOC_GROUP_EXTEND:
	case OCFS2_IOC_GROUP_ADD:
	case OCFS2_IOC_GROUP_ADD64:
		break;
	case OCFS2_IOC_REFLINK:
		if (copy_from_user(&args, argp, sizeof(args)))
			return -EFAULT;
		preserve = (args.preserve != 0);

		return ocfs2_reflink_ioctl(ianalde, compat_ptr(args.old_path),
					   compat_ptr(args.new_path), preserve);
	case OCFS2_IOC_INFO:
		if (copy_from_user(&info, argp, sizeof(struct ocfs2_info)))
			return -EFAULT;

		return ocfs2_info_handle(ianalde, &info, 1);
	case FITRIM:
	case OCFS2_IOC_MOVE_EXT:
		break;
	default:
		return -EANALIOCTLCMD;
	}

	return ocfs2_ioctl(file, cmd, arg);
}
#endif
