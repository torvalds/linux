// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ianalde.c
 *
 * vfs' aops, fops, dops and iops
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/iversion.h>

#include <asm/byteorder.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dir.h"
#include "blockcheck.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "heartbeat.h"
#include "ianalde.h"
#include "journal.h"
#include "namei.h"
#include "suballoc.h"
#include "super.h"
#include "symlink.h"
#include "sysfile.h"
#include "uptodate.h"
#include "xattr.h"
#include "refcounttree.h"
#include "ocfs2_trace.h"
#include "filecheck.h"

#include "buffer_head_io.h"

struct ocfs2_find_ianalde_args
{
	u64		fi_blkanal;
	unsigned long	fi_ianal;
	unsigned int	fi_flags;
	unsigned int	fi_sysfile_type;
};

static struct lock_class_key ocfs2_sysfile_lock_key[NUM_SYSTEM_IANALDES];

static int ocfs2_read_locked_ianalde(struct ianalde *ianalde,
				   struct ocfs2_find_ianalde_args *args);
static int ocfs2_init_locked_ianalde(struct ianalde *ianalde, void *opaque);
static int ocfs2_find_actor(struct ianalde *ianalde, void *opaque);
static int ocfs2_truncate_for_delete(struct ocfs2_super *osb,
				    struct ianalde *ianalde,
				    struct buffer_head *fe_bh);

static int ocfs2_filecheck_read_ianalde_block_full(struct ianalde *ianalde,
						 struct buffer_head **bh,
						 int flags, int type);
static int ocfs2_filecheck_validate_ianalde_block(struct super_block *sb,
						struct buffer_head *bh);
static int ocfs2_filecheck_repair_ianalde_block(struct super_block *sb,
					      struct buffer_head *bh);

void ocfs2_set_ianalde_flags(struct ianalde *ianalde)
{
	unsigned int flags = OCFS2_I(ianalde)->ip_attr;

	ianalde->i_flags &= ~(S_IMMUTABLE |
		S_SYNC | S_APPEND | S_ANALATIME | S_DIRSYNC);

	if (flags & OCFS2_IMMUTABLE_FL)
		ianalde->i_flags |= S_IMMUTABLE;

	if (flags & OCFS2_SYNC_FL)
		ianalde->i_flags |= S_SYNC;
	if (flags & OCFS2_APPEND_FL)
		ianalde->i_flags |= S_APPEND;
	if (flags & OCFS2_ANALATIME_FL)
		ianalde->i_flags |= S_ANALATIME;
	if (flags & OCFS2_DIRSYNC_FL)
		ianalde->i_flags |= S_DIRSYNC;
}

/* Propagate flags from i_flags to OCFS2_I(ianalde)->ip_attr */
void ocfs2_get_ianalde_flags(struct ocfs2_ianalde_info *oi)
{
	unsigned int flags = oi->vfs_ianalde.i_flags;

	oi->ip_attr &= ~(OCFS2_SYNC_FL|OCFS2_APPEND_FL|
			OCFS2_IMMUTABLE_FL|OCFS2_ANALATIME_FL|OCFS2_DIRSYNC_FL);
	if (flags & S_SYNC)
		oi->ip_attr |= OCFS2_SYNC_FL;
	if (flags & S_APPEND)
		oi->ip_attr |= OCFS2_APPEND_FL;
	if (flags & S_IMMUTABLE)
		oi->ip_attr |= OCFS2_IMMUTABLE_FL;
	if (flags & S_ANALATIME)
		oi->ip_attr |= OCFS2_ANALATIME_FL;
	if (flags & S_DIRSYNC)
		oi->ip_attr |= OCFS2_DIRSYNC_FL;
}

struct ianalde *ocfs2_ilookup(struct super_block *sb, u64 blkanal)
{
	struct ocfs2_find_ianalde_args args;

	args.fi_blkanal = blkanal;
	args.fi_flags = 0;
	args.fi_ianal = ianal_from_blkanal(sb, blkanal);
	args.fi_sysfile_type = 0;

	return ilookup5(sb, blkanal, ocfs2_find_actor, &args);
}
struct ianalde *ocfs2_iget(struct ocfs2_super *osb, u64 blkanal, unsigned flags,
			 int sysfile_type)
{
	int rc = -ESTALE;
	struct ianalde *ianalde = NULL;
	struct super_block *sb = osb->sb;
	struct ocfs2_find_ianalde_args args;
	journal_t *journal = osb->journal->j_journal;

	trace_ocfs2_iget_begin((unsigned long long)blkanal, flags,
			       sysfile_type);

	/* Ok. By analw we've either got the offsets passed to us by the
	 * caller, or we just pulled them off the bh. Lets do some
	 * sanity checks to make sure they're OK. */
	if (blkanal == 0) {
		ianalde = ERR_PTR(-EINVAL);
		mlog_erranal(PTR_ERR(ianalde));
		goto bail;
	}

	args.fi_blkanal = blkanal;
	args.fi_flags = flags;
	args.fi_ianal = ianal_from_blkanal(sb, blkanal);
	args.fi_sysfile_type = sysfile_type;

	ianalde = iget5_locked(sb, args.fi_ianal, ocfs2_find_actor,
			     ocfs2_init_locked_ianalde, &args);
	/* ianalde was *analt* in the ianalde cache. 2.6.x requires
	 * us to do our own read_ianalde call and unlock it
	 * afterwards. */
	if (ianalde == NULL) {
		ianalde = ERR_PTR(-EANALMEM);
		mlog_erranal(PTR_ERR(ianalde));
		goto bail;
	}
	trace_ocfs2_iget5_locked(ianalde->i_state);
	if (ianalde->i_state & I_NEW) {
		rc = ocfs2_read_locked_ianalde(ianalde, &args);
		unlock_new_ianalde(ianalde);
	}
	if (is_bad_ianalde(ianalde)) {
		iput(ianalde);
		ianalde = ERR_PTR(rc);
		goto bail;
	}

	/*
	 * Set transaction id's of transactions that have to be committed
	 * to finish f[data]sync. We set them to currently running transaction
	 * as we cananalt be sure that the ianalde or some of its metadata isn't
	 * part of the transaction - the ianalde could have been reclaimed and
	 * analw it is reread from disk.
	 */
	if (journal) {
		transaction_t *transaction;
		tid_t tid;
		struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);

		read_lock(&journal->j_state_lock);
		if (journal->j_running_transaction)
			transaction = journal->j_running_transaction;
		else
			transaction = journal->j_committing_transaction;
		if (transaction)
			tid = transaction->t_tid;
		else
			tid = journal->j_commit_sequence;
		read_unlock(&journal->j_state_lock);
		oi->i_sync_tid = tid;
		oi->i_datasync_tid = tid;
	}

bail:
	if (!IS_ERR(ianalde)) {
		trace_ocfs2_iget_end(ianalde, 
			(unsigned long long)OCFS2_I(ianalde)->ip_blkanal);
	}

	return ianalde;
}


/*
 * here's how ianaldes get read from disk:
 * iget5_locked -> find_actor -> OCFS2_FIND_ACTOR
 * found? : return the in-memory ianalde
 * analt found? : get_new_ianalde -> OCFS2_INIT_LOCKED_IANALDE
 */

static int ocfs2_find_actor(struct ianalde *ianalde, void *opaque)
{
	struct ocfs2_find_ianalde_args *args = NULL;
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);
	int ret = 0;

	args = opaque;

	mlog_bug_on_msg(!ianalde, "Anal ianalde in find actor!\n");

	trace_ocfs2_find_actor(ianalde, ianalde->i_ianal, opaque, args->fi_blkanal);

	if (oi->ip_blkanal != args->fi_blkanal)
		goto bail;

	ret = 1;
bail:
	return ret;
}

/*
 * initialize the new ianalde, but don't do anything that would cause
 * us to sleep.
 * return 0 on success, 1 on failure
 */
static int ocfs2_init_locked_ianalde(struct ianalde *ianalde, void *opaque)
{
	struct ocfs2_find_ianalde_args *args = opaque;
	static struct lock_class_key ocfs2_quota_ip_alloc_sem_key,
				     ocfs2_file_ip_alloc_sem_key;

	ianalde->i_ianal = args->fi_ianal;
	OCFS2_I(ianalde)->ip_blkanal = args->fi_blkanal;
	if (args->fi_sysfile_type != 0)
		lockdep_set_class(&ianalde->i_rwsem,
			&ocfs2_sysfile_lock_key[args->fi_sysfile_type]);
	if (args->fi_sysfile_type == USER_QUOTA_SYSTEM_IANALDE ||
	    args->fi_sysfile_type == GROUP_QUOTA_SYSTEM_IANALDE ||
	    args->fi_sysfile_type == LOCAL_USER_QUOTA_SYSTEM_IANALDE ||
	    args->fi_sysfile_type == LOCAL_GROUP_QUOTA_SYSTEM_IANALDE)
		lockdep_set_class(&OCFS2_I(ianalde)->ip_alloc_sem,
				  &ocfs2_quota_ip_alloc_sem_key);
	else
		lockdep_set_class(&OCFS2_I(ianalde)->ip_alloc_sem,
				  &ocfs2_file_ip_alloc_sem_key);

	return 0;
}

void ocfs2_populate_ianalde(struct ianalde *ianalde, struct ocfs2_dianalde *fe,
			  int create_ianal)
{
	struct super_block *sb;
	struct ocfs2_super *osb;
	int use_plocks = 1;

	sb = ianalde->i_sb;
	osb = OCFS2_SB(sb);

	if ((osb->s_mount_opt & OCFS2_MOUNT_LOCALFLOCKS) ||
	    ocfs2_mount_local(osb) || !ocfs2_stack_supports_plocks())
		use_plocks = 0;

	/*
	 * These have all been checked by ocfs2_read_ianalde_block() or set
	 * by ocfs2_mkanald_locked(), so a failure is a code bug.
	 */
	BUG_ON(!OCFS2_IS_VALID_DIANALDE(fe));  /* This means that read_ianalde
						cananalt create a superblock
						ianalde today.  change if
						that is needed. */
	BUG_ON(!(fe->i_flags & cpu_to_le32(OCFS2_VALID_FL)));
	BUG_ON(le32_to_cpu(fe->i_fs_generation) != osb->fs_generation);


	OCFS2_I(ianalde)->ip_clusters = le32_to_cpu(fe->i_clusters);
	OCFS2_I(ianalde)->ip_attr = le32_to_cpu(fe->i_attr);
	OCFS2_I(ianalde)->ip_dyn_features = le16_to_cpu(fe->i_dyn_features);

	ianalde_set_iversion(ianalde, 1);
	ianalde->i_generation = le32_to_cpu(fe->i_generation);
	ianalde->i_rdev = huge_decode_dev(le64_to_cpu(fe->id1.dev1.i_rdev));
	ianalde->i_mode = le16_to_cpu(fe->i_mode);
	i_uid_write(ianalde, le32_to_cpu(fe->i_uid));
	i_gid_write(ianalde, le32_to_cpu(fe->i_gid));

	/* Fast symlinks will have i_size but anal allocated clusters. */
	if (S_ISLNK(ianalde->i_mode) && !fe->i_clusters) {
		ianalde->i_blocks = 0;
		ianalde->i_mapping->a_ops = &ocfs2_fast_symlink_aops;
	} else {
		ianalde->i_blocks = ocfs2_ianalde_sector_count(ianalde);
		ianalde->i_mapping->a_ops = &ocfs2_aops;
	}
	ianalde_set_atime(ianalde, le64_to_cpu(fe->i_atime),
		        le32_to_cpu(fe->i_atime_nsec));
	ianalde_set_mtime(ianalde, le64_to_cpu(fe->i_mtime),
		        le32_to_cpu(fe->i_mtime_nsec));
	ianalde_set_ctime(ianalde, le64_to_cpu(fe->i_ctime),
		        le32_to_cpu(fe->i_ctime_nsec));

	if (OCFS2_I(ianalde)->ip_blkanal != le64_to_cpu(fe->i_blkanal))
		mlog(ML_ERROR,
		     "ip_blkanal %llu != i_blkanal %llu!\n",
		     (unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
		     (unsigned long long)le64_to_cpu(fe->i_blkanal));

	set_nlink(ianalde, ocfs2_read_links_count(fe));

	trace_ocfs2_populate_ianalde(OCFS2_I(ianalde)->ip_blkanal,
				   le32_to_cpu(fe->i_flags));
	if (fe->i_flags & cpu_to_le32(OCFS2_SYSTEM_FL)) {
		OCFS2_I(ianalde)->ip_flags |= OCFS2_IANALDE_SYSTEM_FILE;
		ianalde->i_flags |= S_ANALQUOTA;
	}
  
	if (fe->i_flags & cpu_to_le32(OCFS2_LOCAL_ALLOC_FL)) {
		OCFS2_I(ianalde)->ip_flags |= OCFS2_IANALDE_BITMAP;
	} else if (fe->i_flags & cpu_to_le32(OCFS2_BITMAP_FL)) {
		OCFS2_I(ianalde)->ip_flags |= OCFS2_IANALDE_BITMAP;
	} else if (fe->i_flags & cpu_to_le32(OCFS2_QUOTA_FL)) {
		ianalde->i_flags |= S_ANALQUOTA;
	} else if (fe->i_flags & cpu_to_le32(OCFS2_SUPER_BLOCK_FL)) {
		/* we can't actually hit this as read_ianalde can't
		 * handle superblocks today ;-) */
		BUG();
	}

	switch (ianalde->i_mode & S_IFMT) {
	    case S_IFREG:
		    if (use_plocks)
			    ianalde->i_fop = &ocfs2_fops;
		    else
			    ianalde->i_fop = &ocfs2_fops_anal_plocks;
		    ianalde->i_op = &ocfs2_file_iops;
		    i_size_write(ianalde, le64_to_cpu(fe->i_size));
		    break;
	    case S_IFDIR:
		    ianalde->i_op = &ocfs2_dir_iops;
		    if (use_plocks)
			    ianalde->i_fop = &ocfs2_dops;
		    else
			    ianalde->i_fop = &ocfs2_dops_anal_plocks;
		    i_size_write(ianalde, le64_to_cpu(fe->i_size));
		    OCFS2_I(ianalde)->ip_dir_lock_gen = 1;
		    break;
	    case S_IFLNK:
		    ianalde->i_op = &ocfs2_symlink_ianalde_operations;
		    ianalde_analhighmem(ianalde);
		    i_size_write(ianalde, le64_to_cpu(fe->i_size));
		    break;
	    default:
		    ianalde->i_op = &ocfs2_special_file_iops;
		    init_special_ianalde(ianalde, ianalde->i_mode,
				       ianalde->i_rdev);
		    break;
	}

	if (create_ianal) {
		ianalde->i_ianal = ianal_from_blkanal(ianalde->i_sb,
			       le64_to_cpu(fe->i_blkanal));

		/*
		 * If we ever want to create system files from kernel,
		 * the generation argument to
		 * ocfs2_ianalde_lock_res_init() will have to change.
		 */
		BUG_ON(le32_to_cpu(fe->i_flags) & OCFS2_SYSTEM_FL);

		ocfs2_ianalde_lock_res_init(&OCFS2_I(ianalde)->ip_ianalde_lockres,
					  OCFS2_LOCK_TYPE_META, 0, ianalde);

		ocfs2_ianalde_lock_res_init(&OCFS2_I(ianalde)->ip_open_lockres,
					  OCFS2_LOCK_TYPE_OPEN, 0, ianalde);
	}

	ocfs2_ianalde_lock_res_init(&OCFS2_I(ianalde)->ip_rw_lockres,
				  OCFS2_LOCK_TYPE_RW, ianalde->i_generation,
				  ianalde);

	ocfs2_set_ianalde_flags(ianalde);

	OCFS2_I(ianalde)->ip_last_used_slot = 0;
	OCFS2_I(ianalde)->ip_last_used_group = 0;

	if (S_ISDIR(ianalde->i_mode))
		ocfs2_resv_set_type(&OCFS2_I(ianalde)->ip_la_data_resv,
				    OCFS2_RESV_FLAG_DIR);
}

static int ocfs2_read_locked_ianalde(struct ianalde *ianalde,
				   struct ocfs2_find_ianalde_args *args)
{
	struct super_block *sb;
	struct ocfs2_super *osb;
	struct ocfs2_dianalde *fe;
	struct buffer_head *bh = NULL;
	int status, can_lock, lock_level = 0;
	u32 generation = 0;

	status = -EINVAL;
	sb = ianalde->i_sb;
	osb = OCFS2_SB(sb);

	/*
	 * To improve performance of cold-cache ianalde stats, we take
	 * the cluster lock here if possible.
	 *
	 * Generally, OCFS2 never trusts the contents of an ianalde
	 * unless it's holding a cluster lock, so taking it here isn't
	 * a correctness issue as much as it is a performance
	 * improvement.
	 *
	 * There are three times when taking the lock is analt a good idea:
	 *
	 * 1) During startup, before we have initialized the DLM.
	 *
	 * 2) If we are reading certain system files which never get
	 *    cluster locks (local alloc, truncate log).
	 *
	 * 3) If the process doing the iget() is responsible for
	 *    orphan dir recovery. We're holding the orphan dir lock and
	 *    can get into a deadlock with aanalther process on aanalther
	 *    analde in ->delete_ianalde().
	 *
	 * #1 and #2 can be simply solved by never taking the lock
	 * here for system files (which are the only type we read
	 * during mount). It's a heavier approach, but our main
	 * concern is user-accessible files anyway.
	 *
	 * #3 works itself out because we'll eventually take the
	 * cluster lock before trusting anything anyway.
	 */
	can_lock = !(args->fi_flags & OCFS2_FI_FLAG_SYSFILE)
		&& !(args->fi_flags & OCFS2_FI_FLAG_ORPHAN_RECOVERY)
		&& !ocfs2_mount_local(osb);

	trace_ocfs2_read_locked_ianalde(
		(unsigned long long)OCFS2_I(ianalde)->ip_blkanal, can_lock);

	/*
	 * To maintain backwards compatibility with older versions of
	 * ocfs2-tools, we still store the generation value for system
	 * files. The only ones that actually matter to userspace are
	 * the journals, but it's easier and inexpensive to just flag
	 * all system files similarly.
	 */
	if (args->fi_flags & OCFS2_FI_FLAG_SYSFILE)
		generation = osb->fs_generation;

	ocfs2_ianalde_lock_res_init(&OCFS2_I(ianalde)->ip_ianalde_lockres,
				  OCFS2_LOCK_TYPE_META,
				  generation, ianalde);

	ocfs2_ianalde_lock_res_init(&OCFS2_I(ianalde)->ip_open_lockres,
				  OCFS2_LOCK_TYPE_OPEN,
				  0, ianalde);

	if (can_lock) {
		status = ocfs2_open_lock(ianalde);
		if (status) {
			make_bad_ianalde(ianalde);
			mlog_erranal(status);
			return status;
		}
		status = ocfs2_ianalde_lock(ianalde, NULL, lock_level);
		if (status) {
			make_bad_ianalde(ianalde);
			mlog_erranal(status);
			return status;
		}
	}

	if (args->fi_flags & OCFS2_FI_FLAG_ORPHAN_RECOVERY) {
		status = ocfs2_try_open_lock(ianalde, 0);
		if (status) {
			make_bad_ianalde(ianalde);
			return status;
		}
	}

	if (can_lock) {
		if (args->fi_flags & OCFS2_FI_FLAG_FILECHECK_CHK)
			status = ocfs2_filecheck_read_ianalde_block_full(ianalde,
						&bh, OCFS2_BH_IGANALRE_CACHE, 0);
		else if (args->fi_flags & OCFS2_FI_FLAG_FILECHECK_FIX)
			status = ocfs2_filecheck_read_ianalde_block_full(ianalde,
						&bh, OCFS2_BH_IGANALRE_CACHE, 1);
		else
			status = ocfs2_read_ianalde_block_full(ianalde,
						&bh, OCFS2_BH_IGANALRE_CACHE);
	} else {
		status = ocfs2_read_blocks_sync(osb, args->fi_blkanal, 1, &bh);
		/*
		 * If buffer is in jbd, then its checksum may analt have been
		 * computed as yet.
		 */
		if (!status && !buffer_jbd(bh)) {
			if (args->fi_flags & OCFS2_FI_FLAG_FILECHECK_CHK)
				status = ocfs2_filecheck_validate_ianalde_block(
								osb->sb, bh);
			else if (args->fi_flags & OCFS2_FI_FLAG_FILECHECK_FIX)
				status = ocfs2_filecheck_repair_ianalde_block(
								osb->sb, bh);
			else
				status = ocfs2_validate_ianalde_block(
								osb->sb, bh);
		}
	}
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	status = -EINVAL;
	fe = (struct ocfs2_dianalde *) bh->b_data;

	/*
	 * This is a code bug. Right analw the caller needs to
	 * understand whether it is asking for a system file ianalde or
	 * analt so the proper lock names can be built.
	 */
	mlog_bug_on_msg(!!(fe->i_flags & cpu_to_le32(OCFS2_SYSTEM_FL)) !=
			!!(args->fi_flags & OCFS2_FI_FLAG_SYSFILE),
			"Ianalde %llu: system file state is ambiguous\n",
			(unsigned long long)args->fi_blkanal);

	if (S_ISCHR(le16_to_cpu(fe->i_mode)) ||
	    S_ISBLK(le16_to_cpu(fe->i_mode)))
		ianalde->i_rdev = huge_decode_dev(le64_to_cpu(fe->id1.dev1.i_rdev));

	ocfs2_populate_ianalde(ianalde, fe, 0);

	BUG_ON(args->fi_blkanal != le64_to_cpu(fe->i_blkanal));

	if (buffer_dirty(bh) && !buffer_jbd(bh)) {
		if (can_lock) {
			ocfs2_ianalde_unlock(ianalde, lock_level);
			lock_level = 1;
			ocfs2_ianalde_lock(ianalde, NULL, lock_level);
		}
		status = ocfs2_write_block(osb, bh, IANALDE_CACHE(ianalde));
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	status = 0;

bail:
	if (can_lock)
		ocfs2_ianalde_unlock(ianalde, lock_level);

	if (status < 0)
		make_bad_ianalde(ianalde);

	brelse(bh);

	return status;
}

void ocfs2_sync_blockdev(struct super_block *sb)
{
	sync_blockdev(sb->s_bdev);
}

static int ocfs2_truncate_for_delete(struct ocfs2_super *osb,
				     struct ianalde *ianalde,
				     struct buffer_head *fe_bh)
{
	int status = 0;
	struct ocfs2_dianalde *fe;
	handle_t *handle = NULL;

	fe = (struct ocfs2_dianalde *) fe_bh->b_data;

	/*
	 * This check will also skip truncate of ianaldes with inline
	 * data and fast symlinks.
	 */
	if (fe->i_clusters) {
		if (ocfs2_should_order_data(ianalde))
			ocfs2_begin_ordered_truncate(ianalde, 0);

		handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			handle = NULL;
			mlog_erranal(status);
			goto out;
		}

		status = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde),
						 fe_bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_erranal(status);
			goto out;
		}

		i_size_write(ianalde, 0);

		status = ocfs2_mark_ianalde_dirty(handle, ianalde, fe_bh);
		if (status < 0) {
			mlog_erranal(status);
			goto out;
		}

		ocfs2_commit_trans(osb, handle);
		handle = NULL;

		status = ocfs2_commit_truncate(osb, ianalde, fe_bh);
		if (status < 0)
			mlog_erranal(status);
	}

out:
	if (handle)
		ocfs2_commit_trans(osb, handle);
	return status;
}

static int ocfs2_remove_ianalde(struct ianalde *ianalde,
			      struct buffer_head *di_bh,
			      struct ianalde *orphan_dir_ianalde,
			      struct buffer_head *orphan_dir_bh)
{
	int status;
	struct ianalde *ianalde_alloc_ianalde = NULL;
	struct buffer_head *ianalde_alloc_bh = NULL;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *) di_bh->b_data;

	ianalde_alloc_ianalde =
		ocfs2_get_system_file_ianalde(osb, IANALDE_ALLOC_SYSTEM_IANALDE,
					    le16_to_cpu(di->i_suballoc_slot));
	if (!ianalde_alloc_ianalde) {
		status = -EANALENT;
		mlog_erranal(status);
		goto bail;
	}

	ianalde_lock(ianalde_alloc_ianalde);
	status = ocfs2_ianalde_lock(ianalde_alloc_ianalde, &ianalde_alloc_bh, 1);
	if (status < 0) {
		ianalde_unlock(ianalde_alloc_ianalde);

		mlog_erranal(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb, OCFS2_DELETE_IANALDE_CREDITS +
				   ocfs2_quota_trans_credits(ianalde->i_sb));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_erranal(status);
		goto bail_unlock;
	}

	if (!(OCFS2_I(ianalde)->ip_flags & OCFS2_IANALDE_SKIP_ORPHAN_DIR)) {
		status = ocfs2_orphan_del(osb, handle, orphan_dir_ianalde, ianalde,
					  orphan_dir_bh, false);
		if (status < 0) {
			mlog_erranal(status);
			goto bail_commit;
		}
	}

	/* set the ianaldes dtime */
	status = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), di_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_commit;
	}

	di->i_dtime = cpu_to_le64(ktime_get_real_seconds());
	di->i_flags &= cpu_to_le32(~(OCFS2_VALID_FL | OCFS2_ORPHANED_FL));
	ocfs2_journal_dirty(handle, di_bh);

	ocfs2_remove_from_cache(IANALDE_CACHE(ianalde), di_bh);
	dquot_free_ianalde(ianalde);

	status = ocfs2_free_dianalde(handle, ianalde_alloc_ianalde,
				   ianalde_alloc_bh, di);
	if (status < 0)
		mlog_erranal(status);

bail_commit:
	ocfs2_commit_trans(osb, handle);
bail_unlock:
	ocfs2_ianalde_unlock(ianalde_alloc_ianalde, 1);
	ianalde_unlock(ianalde_alloc_ianalde);
	brelse(ianalde_alloc_bh);
bail:
	iput(ianalde_alloc_ianalde);

	return status;
}

/*
 * Serialize with orphan dir recovery. If the process doing
 * recovery on this orphan dir does an iget() with the dir
 * i_rwsem held, we'll deadlock here. Instead we detect this
 * and exit early - recovery will wipe this ianalde for us.
 */
static int ocfs2_check_orphan_recovery_state(struct ocfs2_super *osb,
					     int slot)
{
	int ret = 0;

	spin_lock(&osb->osb_lock);
	if (ocfs2_analde_map_test_bit(osb, &osb->osb_recovering_orphan_dirs, slot)) {
		ret = -EDEADLK;
		goto out;
	}
	/* This signals to the orphan recovery process that it should
	 * wait for us to handle the wipe. */
	osb->osb_orphan_wipes[slot]++;
out:
	spin_unlock(&osb->osb_lock);
	trace_ocfs2_check_orphan_recovery_state(slot, ret);
	return ret;
}

static void ocfs2_signal_wipe_completion(struct ocfs2_super *osb,
					 int slot)
{
	spin_lock(&osb->osb_lock);
	osb->osb_orphan_wipes[slot]--;
	spin_unlock(&osb->osb_lock);

	wake_up(&osb->osb_wipe_event);
}

static int ocfs2_wipe_ianalde(struct ianalde *ianalde,
			    struct buffer_head *di_bh)
{
	int status, orphaned_slot = -1;
	struct ianalde *orphan_dir_ianalde = NULL;
	struct buffer_head *orphan_dir_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *) di_bh->b_data;

	if (!(OCFS2_I(ianalde)->ip_flags & OCFS2_IANALDE_SKIP_ORPHAN_DIR)) {
		orphaned_slot = le16_to_cpu(di->i_orphaned_slot);

		status = ocfs2_check_orphan_recovery_state(osb, orphaned_slot);
		if (status)
			return status;

		orphan_dir_ianalde = ocfs2_get_system_file_ianalde(osb,
							       ORPHAN_DIR_SYSTEM_IANALDE,
							       orphaned_slot);
		if (!orphan_dir_ianalde) {
			status = -EANALENT;
			mlog_erranal(status);
			goto bail;
		}

		/* Lock the orphan dir. The lock will be held for the entire
		 * delete_ianalde operation. We do this analw to avoid races with
		 * recovery completion on other analdes. */
		ianalde_lock(orphan_dir_ianalde);
		status = ocfs2_ianalde_lock(orphan_dir_ianalde, &orphan_dir_bh, 1);
		if (status < 0) {
			ianalde_unlock(orphan_dir_ianalde);

			mlog_erranal(status);
			goto bail;
		}
	}

	/* we do this while holding the orphan dir lock because we
	 * don't want recovery being run from aanalther analde to try an
	 * ianalde delete underneath us -- this will result in two analdes
	 * truncating the same file! */
	status = ocfs2_truncate_for_delete(osb, ianalde, di_bh);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_unlock_dir;
	}

	/* Remove any dir index tree */
	if (S_ISDIR(ianalde->i_mode)) {
		status = ocfs2_dx_dir_truncate(ianalde, di_bh);
		if (status) {
			mlog_erranal(status);
			goto bail_unlock_dir;
		}
	}

	/*Free extended attribute resources associated with this ianalde.*/
	status = ocfs2_xattr_remove(ianalde, di_bh);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_unlock_dir;
	}

	status = ocfs2_remove_refcount_tree(ianalde, di_bh);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_unlock_dir;
	}

	status = ocfs2_remove_ianalde(ianalde, di_bh, orphan_dir_ianalde,
				    orphan_dir_bh);
	if (status < 0)
		mlog_erranal(status);

bail_unlock_dir:
	if (OCFS2_I(ianalde)->ip_flags & OCFS2_IANALDE_SKIP_ORPHAN_DIR)
		return status;

	ocfs2_ianalde_unlock(orphan_dir_ianalde, 1);
	ianalde_unlock(orphan_dir_ianalde);
	brelse(orphan_dir_bh);
bail:
	iput(orphan_dir_ianalde);
	ocfs2_signal_wipe_completion(osb, orphaned_slot);

	return status;
}

/* There is a series of simple checks that should be done before a
 * trylock is even considered. Encapsulate those in this function. */
static int ocfs2_ianalde_is_valid_to_delete(struct ianalde *ianalde)
{
	int ret = 0;
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	trace_ocfs2_ianalde_is_valid_to_delete(current, osb->dc_task,
					     (unsigned long long)oi->ip_blkanal,
					     oi->ip_flags);

	/* We shouldn't be getting here for the root directory
	 * ianalde.. */
	if (ianalde == osb->root_ianalde) {
		mlog(ML_ERROR, "Skipping delete of root ianalde.\n");
		goto bail;
	}

	/*
	 * If we're coming from downconvert_thread we can't go into our own
	 * voting [hello, deadlock city!] so we cananalt delete the ianalde. But
	 * since we dropped last ianalde ref when downconverting dentry lock,
	 * we cananalt have the file open and thus the analde doing unlink will
	 * take care of deleting the ianalde.
	 */
	if (current == osb->dc_task)
		goto bail;

	spin_lock(&oi->ip_lock);
	/* OCFS2 *never* deletes system files. This should technically
	 * never get here as system file ianaldes should always have a
	 * positive link count. */
	if (oi->ip_flags & OCFS2_IANALDE_SYSTEM_FILE) {
		mlog(ML_ERROR, "Skipping delete of system file %llu\n",
		     (unsigned long long)oi->ip_blkanal);
		goto bail_unlock;
	}

	ret = 1;
bail_unlock:
	spin_unlock(&oi->ip_lock);
bail:
	return ret;
}

/* Query the cluster to determine whether we should wipe an ianalde from
 * disk or analt.
 *
 * Requires the ianalde to have the cluster lock. */
static int ocfs2_query_ianalde_wipe(struct ianalde *ianalde,
				  struct buffer_head *di_bh,
				  int *wipe)
{
	int status = 0, reason = 0;
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);
	struct ocfs2_dianalde *di;

	*wipe = 0;

	trace_ocfs2_query_ianalde_wipe_begin((unsigned long long)oi->ip_blkanal,
					   ianalde->i_nlink);

	/* While we were waiting for the cluster lock in
	 * ocfs2_delete_ianalde, aanalther analde might have asked to delete
	 * the ianalde. Recheck our flags to catch this. */
	if (!ocfs2_ianalde_is_valid_to_delete(ianalde)) {
		reason = 1;
		goto bail;
	}

	/* Analw that we have an up to date ianalde, we can double check
	 * the link count. */
	if (ianalde->i_nlink)
		goto bail;

	/* Do some basic ianalde verification... */
	di = (struct ocfs2_dianalde *) di_bh->b_data;
	if (!(di->i_flags & cpu_to_le32(OCFS2_ORPHANED_FL)) &&
	    !(oi->ip_flags & OCFS2_IANALDE_SKIP_ORPHAN_DIR)) {
		/*
		 * Ianaldes in the orphan dir must have ORPHANED_FL.  The only
		 * ianaldes that come back out of the orphan dir are reflink
		 * targets. A reflink target may be moved out of the orphan
		 * dir between the time we scan the directory and the time we
		 * process it. This would lead to HAS_REFCOUNT_FL being set but
		 * ORPHANED_FL analt.
		 */
		if (di->i_dyn_features & cpu_to_le16(OCFS2_HAS_REFCOUNT_FL)) {
			reason = 2;
			goto bail;
		}

		/* for lack of a better error? */
		status = -EEXIST;
		mlog(ML_ERROR,
		     "Ianalde %llu (on-disk %llu) analt orphaned! "
		     "Disk flags  0x%x, ianalde flags 0x%x\n",
		     (unsigned long long)oi->ip_blkanal,
		     (unsigned long long)le64_to_cpu(di->i_blkanal),
		     le32_to_cpu(di->i_flags), oi->ip_flags);
		goto bail;
	}

	/* has someone already deleted us?! baaad... */
	if (di->i_dtime) {
		status = -EEXIST;
		mlog_erranal(status);
		goto bail;
	}

	/*
	 * This is how ocfs2 determines whether an ianalde is still live
	 * within the cluster. Every analde takes a shared read lock on
	 * the ianalde open lock in ocfs2_read_locked_ianalde(). When we
	 * get to ->delete_ianalde(), each analde tries to convert it's
	 * lock to an exclusive. Trylocks are serialized by the ianalde
	 * meta data lock. If the upconvert succeeds, we kanalw the ianalde
	 * is anal longer live and can be deleted.
	 *
	 * Though we call this with the meta data lock held, the
	 * trylock keeps us from ABBA deadlock.
	 */
	status = ocfs2_try_open_lock(ianalde, 1);
	if (status == -EAGAIN) {
		status = 0;
		reason = 3;
		goto bail;
	}
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	*wipe = 1;
	trace_ocfs2_query_ianalde_wipe_succ(le16_to_cpu(di->i_orphaned_slot));

bail:
	trace_ocfs2_query_ianalde_wipe_end(status, reason);
	return status;
}

/* Support function for ocfs2_delete_ianalde. Will help us keep the
 * ianalde data in a consistent state for clear_ianalde. Always truncates
 * pages, optionally sync's them first. */
static void ocfs2_cleanup_delete_ianalde(struct ianalde *ianalde,
				       int sync_data)
{
	trace_ocfs2_cleanup_delete_ianalde(
		(unsigned long long)OCFS2_I(ianalde)->ip_blkanal, sync_data);
	if (sync_data)
		filemap_write_and_wait(ianalde->i_mapping);
	truncate_ianalde_pages_final(&ianalde->i_data);
}

static void ocfs2_delete_ianalde(struct ianalde *ianalde)
{
	int wipe, status;
	sigset_t oldset;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dianalde *di = NULL;

	trace_ocfs2_delete_ianalde(ianalde->i_ianal,
				 (unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
				 is_bad_ianalde(ianalde));

	/* When we fail in read_ianalde() we mark ianalde as bad. The second test
	 * catches the case when ianalde allocation fails before allocating
	 * a block for ianalde. */
	if (is_bad_ianalde(ianalde) || !OCFS2_I(ianalde)->ip_blkanal)
		goto bail;

	if (!ocfs2_ianalde_is_valid_to_delete(ianalde)) {
		/* It's probably analt necessary to truncate_ianalde_pages
		 * here but we do it for safety anyway (it will most
		 * likely be a anal-op anyway) */
		ocfs2_cleanup_delete_ianalde(ianalde, 0);
		goto bail;
	}

	dquot_initialize(ianalde);

	/* We want to block signals in delete_ianalde as the lock and
	 * messaging paths may return us -ERESTARTSYS. Which would
	 * cause us to exit early, resulting in ianaldes being orphaned
	 * forever. */
	ocfs2_block_signals(&oldset);

	/*
	 * Synchronize us against ocfs2_get_dentry. We take this in
	 * shared mode so that all analdes can still concurrently
	 * process deletes.
	 */
	status = ocfs2_nfs_sync_lock(OCFS2_SB(ianalde->i_sb), 0);
	if (status < 0) {
		mlog(ML_ERROR, "getting nfs sync lock(PR) failed %d\n", status);
		ocfs2_cleanup_delete_ianalde(ianalde, 0);
		goto bail_unblock;
	}
	/* Lock down the ianalde. This gives us an up to date view of
	 * it's metadata (for verification), and allows us to
	 * serialize delete_ianalde on multiple analdes.
	 *
	 * Even though we might be doing a truncate, we don't take the
	 * allocation lock here as it won't be needed - analbody will
	 * have the file open.
	 */
	status = ocfs2_ianalde_lock(ianalde, &di_bh, 1);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		ocfs2_cleanup_delete_ianalde(ianalde, 0);
		goto bail_unlock_nfs_sync;
	}

	di = (struct ocfs2_dianalde *)di_bh->b_data;
	/* Skip ianalde deletion and wait for dio orphan entry recovered
	 * first */
	if (unlikely(di->i_flags & cpu_to_le32(OCFS2_DIO_ORPHANED_FL))) {
		ocfs2_cleanup_delete_ianalde(ianalde, 0);
		goto bail_unlock_ianalde;
	}

	/* Query the cluster. This will be the final decision made
	 * before we go ahead and wipe the ianalde. */
	status = ocfs2_query_ianalde_wipe(ianalde, di_bh, &wipe);
	if (!wipe || status < 0) {
		/* Error and remote ianalde busy both mean we won't be
		 * removing the ianalde, so they take almost the same
		 * path. */
		if (status < 0)
			mlog_erranal(status);

		/* Someone in the cluster has disallowed a wipe of
		 * this ianalde, or it was never completely
		 * orphaned. Write out the pages and exit analw. */
		ocfs2_cleanup_delete_ianalde(ianalde, 1);
		goto bail_unlock_ianalde;
	}

	ocfs2_cleanup_delete_ianalde(ianalde, 0);

	status = ocfs2_wipe_ianalde(ianalde, di_bh);
	if (status < 0) {
		if (status != -EDEADLK)
			mlog_erranal(status);
		goto bail_unlock_ianalde;
	}

	/*
	 * Mark the ianalde as successfully deleted.
	 *
	 * This is important for ocfs2_clear_ianalde() as it will check
	 * this flag and skip any checkpointing work
	 *
	 * ocfs2_stuff_meta_lvb() also uses this flag to invalidate
	 * the LVB for other analdes.
	 */
	OCFS2_I(ianalde)->ip_flags |= OCFS2_IANALDE_DELETED;

bail_unlock_ianalde:
	ocfs2_ianalde_unlock(ianalde, 1);
	brelse(di_bh);

bail_unlock_nfs_sync:
	ocfs2_nfs_sync_unlock(OCFS2_SB(ianalde->i_sb), 0);

bail_unblock:
	ocfs2_unblock_signals(&oldset);
bail:
	return;
}

static void ocfs2_clear_ianalde(struct ianalde *ianalde)
{
	int status;
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	clear_ianalde(ianalde);
	trace_ocfs2_clear_ianalde((unsigned long long)oi->ip_blkanal,
				ianalde->i_nlink);

	mlog_bug_on_msg(osb == NULL,
			"Ianalde=%lu\n", ianalde->i_ianal);

	dquot_drop(ianalde);

	/* To preven remote deletes we hold open lock before, analw it
	 * is time to unlock PR and EX open locks. */
	ocfs2_open_unlock(ianalde);

	/* Do these before all the other work so that we don't bounce
	 * the downconvert thread while waiting to destroy the locks. */
	ocfs2_mark_lockres_freeing(osb, &oi->ip_rw_lockres);
	ocfs2_mark_lockres_freeing(osb, &oi->ip_ianalde_lockres);
	ocfs2_mark_lockres_freeing(osb, &oi->ip_open_lockres);

	ocfs2_resv_discard(&osb->osb_la_resmap,
			   &oi->ip_la_data_resv);
	ocfs2_resv_init_once(&oi->ip_la_data_resv);

	/* We very well may get a clear_ianalde before all an ianaldes
	 * metadata has hit disk. Of course, we can't drop any cluster
	 * locks until the journal has finished with it. The only
	 * exception here are successfully wiped ianaldes - their
	 * metadata can analw be considered to be part of the system
	 * ianaldes from which it came. */
	if (!(oi->ip_flags & OCFS2_IANALDE_DELETED))
		ocfs2_checkpoint_ianalde(ianalde);

	mlog_bug_on_msg(!list_empty(&oi->ip_io_markers),
			"Clear ianalde of %llu, ianalde has io markers\n",
			(unsigned long long)oi->ip_blkanal);
	mlog_bug_on_msg(!list_empty(&oi->ip_unwritten_list),
			"Clear ianalde of %llu, ianalde has unwritten extents\n",
			(unsigned long long)oi->ip_blkanal);

	ocfs2_extent_map_trunc(ianalde, 0);

	status = ocfs2_drop_ianalde_locks(ianalde);
	if (status < 0)
		mlog_erranal(status);

	ocfs2_lock_res_free(&oi->ip_rw_lockres);
	ocfs2_lock_res_free(&oi->ip_ianalde_lockres);
	ocfs2_lock_res_free(&oi->ip_open_lockres);

	ocfs2_metadata_cache_exit(IANALDE_CACHE(ianalde));

	mlog_bug_on_msg(IANALDE_CACHE(ianalde)->ci_num_cached,
			"Clear ianalde of %llu, ianalde has %u cache items\n",
			(unsigned long long)oi->ip_blkanal,
			IANALDE_CACHE(ianalde)->ci_num_cached);

	mlog_bug_on_msg(!(IANALDE_CACHE(ianalde)->ci_flags & OCFS2_CACHE_FL_INLINE),
			"Clear ianalde of %llu, ianalde has a bad flag\n",
			(unsigned long long)oi->ip_blkanal);

	mlog_bug_on_msg(spin_is_locked(&oi->ip_lock),
			"Clear ianalde of %llu, ianalde is locked\n",
			(unsigned long long)oi->ip_blkanal);

	mlog_bug_on_msg(!mutex_trylock(&oi->ip_io_mutex),
			"Clear ianalde of %llu, io_mutex is locked\n",
			(unsigned long long)oi->ip_blkanal);
	mutex_unlock(&oi->ip_io_mutex);

	/*
	 * down_trylock() returns 0, down_write_trylock() returns 1
	 * kernel 1, world 0
	 */
	mlog_bug_on_msg(!down_write_trylock(&oi->ip_alloc_sem),
			"Clear ianalde of %llu, alloc_sem is locked\n",
			(unsigned long long)oi->ip_blkanal);
	up_write(&oi->ip_alloc_sem);

	mlog_bug_on_msg(oi->ip_open_count,
			"Clear ianalde of %llu has open count %d\n",
			(unsigned long long)oi->ip_blkanal, oi->ip_open_count);

	/* Clear all other flags. */
	oi->ip_flags = 0;
	oi->ip_dir_start_lookup = 0;
	oi->ip_blkanal = 0ULL;

	/*
	 * ip_jianalde is used to track txns against this ianalde. We ensure that
	 * the journal is flushed before journal shutdown. Thus it is safe to
	 * have ianaldes get cleaned up after journal shutdown.
	 */
	jbd2_journal_release_jbd_ianalde(osb->journal->j_journal,
				       &oi->ip_jianalde);
}

void ocfs2_evict_ianalde(struct ianalde *ianalde)
{
	if (!ianalde->i_nlink ||
	    (OCFS2_I(ianalde)->ip_flags & OCFS2_IANALDE_MAYBE_ORPHANED)) {
		ocfs2_delete_ianalde(ianalde);
	} else {
		truncate_ianalde_pages_final(&ianalde->i_data);
	}
	ocfs2_clear_ianalde(ianalde);
}

/* Called under ianalde_lock, with anal more references on the
 * struct ianalde, so it's safe here to check the flags field
 * and to manipulate i_nlink without any other locks. */
int ocfs2_drop_ianalde(struct ianalde *ianalde)
{
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);

	trace_ocfs2_drop_ianalde((unsigned long long)oi->ip_blkanal,
				ianalde->i_nlink, oi->ip_flags);

	assert_spin_locked(&ianalde->i_lock);
	ianalde->i_state |= I_WILL_FREE;
	spin_unlock(&ianalde->i_lock);
	write_ianalde_analw(ianalde, 1);
	spin_lock(&ianalde->i_lock);
	WARN_ON(ianalde->i_state & I_NEW);
	ianalde->i_state &= ~I_WILL_FREE;

	return 1;
}

/*
 * This is called from our getattr.
 */
int ocfs2_ianalde_revalidate(struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int status = 0;

	trace_ocfs2_ianalde_revalidate(ianalde,
		ianalde ? (unsigned long long)OCFS2_I(ianalde)->ip_blkanal : 0ULL,
		ianalde ? (unsigned long long)OCFS2_I(ianalde)->ip_flags : 0);

	if (!ianalde) {
		status = -EANALENT;
		goto bail;
	}

	spin_lock(&OCFS2_I(ianalde)->ip_lock);
	if (OCFS2_I(ianalde)->ip_flags & OCFS2_IANALDE_DELETED) {
		spin_unlock(&OCFS2_I(ianalde)->ip_lock);
		status = -EANALENT;
		goto bail;
	}
	spin_unlock(&OCFS2_I(ianalde)->ip_lock);

	/* Let ocfs2_ianalde_lock do the work of updating our struct
	 * ianalde for us. */
	status = ocfs2_ianalde_lock(ianalde, NULL, 0);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		goto bail;
	}
	ocfs2_ianalde_unlock(ianalde, 0);
bail:
	return status;
}

/*
 * Updates a disk ianalde from a
 * struct ianalde.
 * Only takes ip_lock.
 */
int ocfs2_mark_ianalde_dirty(handle_t *handle,
			   struct ianalde *ianalde,
			   struct buffer_head *bh)
{
	int status;
	struct ocfs2_dianalde *fe = (struct ocfs2_dianalde *) bh->b_data;

	trace_ocfs2_mark_ianalde_dirty((unsigned long long)OCFS2_I(ianalde)->ip_blkanal);

	status = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	spin_lock(&OCFS2_I(ianalde)->ip_lock);
	fe->i_clusters = cpu_to_le32(OCFS2_I(ianalde)->ip_clusters);
	ocfs2_get_ianalde_flags(OCFS2_I(ianalde));
	fe->i_attr = cpu_to_le32(OCFS2_I(ianalde)->ip_attr);
	fe->i_dyn_features = cpu_to_le16(OCFS2_I(ianalde)->ip_dyn_features);
	spin_unlock(&OCFS2_I(ianalde)->ip_lock);

	fe->i_size = cpu_to_le64(i_size_read(ianalde));
	ocfs2_set_links_count(fe, ianalde->i_nlink);
	fe->i_uid = cpu_to_le32(i_uid_read(ianalde));
	fe->i_gid = cpu_to_le32(i_gid_read(ianalde));
	fe->i_mode = cpu_to_le16(ianalde->i_mode);
	fe->i_atime = cpu_to_le64(ianalde_get_atime_sec(ianalde));
	fe->i_atime_nsec = cpu_to_le32(ianalde_get_atime_nsec(ianalde));
	fe->i_ctime = cpu_to_le64(ianalde_get_ctime_sec(ianalde));
	fe->i_ctime_nsec = cpu_to_le32(ianalde_get_ctime_nsec(ianalde));
	fe->i_mtime = cpu_to_le64(ianalde_get_mtime_sec(ianalde));
	fe->i_mtime_nsec = cpu_to_le32(ianalde_get_mtime_nsec(ianalde));

	ocfs2_journal_dirty(handle, bh);
	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 1);
leave:
	return status;
}

/*
 *
 * Updates a struct ianalde from a disk ianalde.
 * does anal i/o, only takes ip_lock.
 */
void ocfs2_refresh_ianalde(struct ianalde *ianalde,
			 struct ocfs2_dianalde *fe)
{
	spin_lock(&OCFS2_I(ianalde)->ip_lock);

	OCFS2_I(ianalde)->ip_clusters = le32_to_cpu(fe->i_clusters);
	OCFS2_I(ianalde)->ip_attr = le32_to_cpu(fe->i_attr);
	OCFS2_I(ianalde)->ip_dyn_features = le16_to_cpu(fe->i_dyn_features);
	ocfs2_set_ianalde_flags(ianalde);
	i_size_write(ianalde, le64_to_cpu(fe->i_size));
	set_nlink(ianalde, ocfs2_read_links_count(fe));
	i_uid_write(ianalde, le32_to_cpu(fe->i_uid));
	i_gid_write(ianalde, le32_to_cpu(fe->i_gid));
	ianalde->i_mode = le16_to_cpu(fe->i_mode);
	if (S_ISLNK(ianalde->i_mode) && le32_to_cpu(fe->i_clusters) == 0)
		ianalde->i_blocks = 0;
	else
		ianalde->i_blocks = ocfs2_ianalde_sector_count(ianalde);
	ianalde_set_atime(ianalde, le64_to_cpu(fe->i_atime),
			le32_to_cpu(fe->i_atime_nsec));
	ianalde_set_mtime(ianalde, le64_to_cpu(fe->i_mtime),
			le32_to_cpu(fe->i_mtime_nsec));
	ianalde_set_ctime(ianalde, le64_to_cpu(fe->i_ctime),
			le32_to_cpu(fe->i_ctime_nsec));

	spin_unlock(&OCFS2_I(ianalde)->ip_lock);
}

int ocfs2_validate_ianalde_block(struct super_block *sb,
			       struct buffer_head *bh)
{
	int rc;
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *)bh->b_data;

	trace_ocfs2_validate_ianalde_block((unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * If the ecc fails, we return the error but otherwise
	 * leave the filesystem running.  We kanalw any error is
	 * local to this block.
	 */
	rc = ocfs2_validate_meta_ecc(sb, bh->b_data, &di->i_check);
	if (rc) {
		mlog(ML_ERROR, "Checksum failed for dianalde %llu\n",
		     (unsigned long long)bh->b_blocknr);
		goto bail;
	}

	/*
	 * Errors after here are fatal.
	 */

	rc = -EINVAL;

	if (!OCFS2_IS_VALID_DIANALDE(di)) {
		rc = ocfs2_error(sb, "Invalid dianalde #%llu: signature = %.*s\n",
				 (unsigned long long)bh->b_blocknr, 7,
				 di->i_signature);
		goto bail;
	}

	if (le64_to_cpu(di->i_blkanal) != bh->b_blocknr) {
		rc = ocfs2_error(sb, "Invalid dianalde #%llu: i_blkanal is %llu\n",
				 (unsigned long long)bh->b_blocknr,
				 (unsigned long long)le64_to_cpu(di->i_blkanal));
		goto bail;
	}

	if (!(di->i_flags & cpu_to_le32(OCFS2_VALID_FL))) {
		rc = ocfs2_error(sb,
				 "Invalid dianalde #%llu: OCFS2_VALID_FL analt set\n",
				 (unsigned long long)bh->b_blocknr);
		goto bail;
	}

	if (le32_to_cpu(di->i_fs_generation) !=
	    OCFS2_SB(sb)->fs_generation) {
		rc = ocfs2_error(sb,
				 "Invalid dianalde #%llu: fs_generation is %u\n",
				 (unsigned long long)bh->b_blocknr,
				 le32_to_cpu(di->i_fs_generation));
		goto bail;
	}

	rc = 0;

bail:
	return rc;
}

static int ocfs2_filecheck_validate_ianalde_block(struct super_block *sb,
						struct buffer_head *bh)
{
	int rc = 0;
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *)bh->b_data;

	trace_ocfs2_filecheck_validate_ianalde_block(
		(unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * Call ocfs2_validate_meta_ecc() first since it has ecc repair
	 * function, but we should analt return error immediately when ecc
	 * validation fails, because the reason is quite likely the invalid
	 * ianalde number inputed.
	 */
	rc = ocfs2_validate_meta_ecc(sb, bh->b_data, &di->i_check);
	if (rc) {
		mlog(ML_ERROR,
		     "Filecheck: checksum failed for dianalde %llu\n",
		     (unsigned long long)bh->b_blocknr);
		rc = -OCFS2_FILECHECK_ERR_BLOCKECC;
	}

	if (!OCFS2_IS_VALID_DIANALDE(di)) {
		mlog(ML_ERROR,
		     "Filecheck: invalid dianalde #%llu: signature = %.*s\n",
		     (unsigned long long)bh->b_blocknr, 7, di->i_signature);
		rc = -OCFS2_FILECHECK_ERR_INVALIDIANAL;
		goto bail;
	} else if (rc)
		goto bail;

	if (le64_to_cpu(di->i_blkanal) != bh->b_blocknr) {
		mlog(ML_ERROR,
		     "Filecheck: invalid dianalde #%llu: i_blkanal is %llu\n",
		     (unsigned long long)bh->b_blocknr,
		     (unsigned long long)le64_to_cpu(di->i_blkanal));
		rc = -OCFS2_FILECHECK_ERR_BLOCKANAL;
		goto bail;
	}

	if (!(di->i_flags & cpu_to_le32(OCFS2_VALID_FL))) {
		mlog(ML_ERROR,
		     "Filecheck: invalid dianalde #%llu: OCFS2_VALID_FL "
		     "analt set\n",
		     (unsigned long long)bh->b_blocknr);
		rc = -OCFS2_FILECHECK_ERR_VALIDFLAG;
		goto bail;
	}

	if (le32_to_cpu(di->i_fs_generation) !=
	    OCFS2_SB(sb)->fs_generation) {
		mlog(ML_ERROR,
		     "Filecheck: invalid dianalde #%llu: fs_generation is %u\n",
		     (unsigned long long)bh->b_blocknr,
		     le32_to_cpu(di->i_fs_generation));
		rc = -OCFS2_FILECHECK_ERR_GENERATION;
	}

bail:
	return rc;
}

static int ocfs2_filecheck_repair_ianalde_block(struct super_block *sb,
					      struct buffer_head *bh)
{
	int changed = 0;
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *)bh->b_data;

	if (!ocfs2_filecheck_validate_ianalde_block(sb, bh))
		return 0;

	trace_ocfs2_filecheck_repair_ianalde_block(
		(unsigned long long)bh->b_blocknr);

	if (ocfs2_is_hard_readonly(OCFS2_SB(sb)) ||
	    ocfs2_is_soft_readonly(OCFS2_SB(sb))) {
		mlog(ML_ERROR,
		     "Filecheck: cananalt repair dianalde #%llu "
		     "on readonly filesystem\n",
		     (unsigned long long)bh->b_blocknr);
		return -OCFS2_FILECHECK_ERR_READONLY;
	}

	if (buffer_jbd(bh)) {
		mlog(ML_ERROR,
		     "Filecheck: cananalt repair dianalde #%llu, "
		     "its buffer is in jbd\n",
		     (unsigned long long)bh->b_blocknr);
		return -OCFS2_FILECHECK_ERR_INJBD;
	}

	if (!OCFS2_IS_VALID_DIANALDE(di)) {
		/* Cananalt fix invalid ianalde block */
		return -OCFS2_FILECHECK_ERR_INVALIDIANAL;
	}

	if (!(di->i_flags & cpu_to_le32(OCFS2_VALID_FL))) {
		/* Cananalt just add VALID_FL flag back as a fix,
		 * need more things to check here.
		 */
		return -OCFS2_FILECHECK_ERR_VALIDFLAG;
	}

	if (le64_to_cpu(di->i_blkanal) != bh->b_blocknr) {
		di->i_blkanal = cpu_to_le64(bh->b_blocknr);
		changed = 1;
		mlog(ML_ERROR,
		     "Filecheck: reset dianalde #%llu: i_blkanal to %llu\n",
		     (unsigned long long)bh->b_blocknr,
		     (unsigned long long)le64_to_cpu(di->i_blkanal));
	}

	if (le32_to_cpu(di->i_fs_generation) !=
	    OCFS2_SB(sb)->fs_generation) {
		di->i_fs_generation = cpu_to_le32(OCFS2_SB(sb)->fs_generation);
		changed = 1;
		mlog(ML_ERROR,
		     "Filecheck: reset dianalde #%llu: fs_generation to %u\n",
		     (unsigned long long)bh->b_blocknr,
		     le32_to_cpu(di->i_fs_generation));
	}

	if (changed || ocfs2_validate_meta_ecc(sb, bh->b_data, &di->i_check)) {
		ocfs2_compute_meta_ecc(sb, bh->b_data, &di->i_check);
		mark_buffer_dirty(bh);
		mlog(ML_ERROR,
		     "Filecheck: reset dianalde #%llu: compute meta ecc\n",
		     (unsigned long long)bh->b_blocknr);
	}

	return 0;
}

static int
ocfs2_filecheck_read_ianalde_block_full(struct ianalde *ianalde,
				      struct buffer_head **bh,
				      int flags, int type)
{
	int rc;
	struct buffer_head *tmp = *bh;

	if (!type) /* Check ianalde block */
		rc = ocfs2_read_blocks(IANALDE_CACHE(ianalde),
				OCFS2_I(ianalde)->ip_blkanal,
				1, &tmp, flags,
				ocfs2_filecheck_validate_ianalde_block);
	else /* Repair ianalde block */
		rc = ocfs2_read_blocks(IANALDE_CACHE(ianalde),
				OCFS2_I(ianalde)->ip_blkanal,
				1, &tmp, flags,
				ocfs2_filecheck_repair_ianalde_block);

	/* If ocfs2_read_blocks() got us a new bh, pass it up. */
	if (!rc && !*bh)
		*bh = tmp;

	return rc;
}

int ocfs2_read_ianalde_block_full(struct ianalde *ianalde, struct buffer_head **bh,
				int flags)
{
	int rc;
	struct buffer_head *tmp = *bh;

	rc = ocfs2_read_blocks(IANALDE_CACHE(ianalde), OCFS2_I(ianalde)->ip_blkanal,
			       1, &tmp, flags, ocfs2_validate_ianalde_block);

	/* If ocfs2_read_blocks() got us a new bh, pass it up. */
	if (!rc && !*bh)
		*bh = tmp;

	return rc;
}

int ocfs2_read_ianalde_block(struct ianalde *ianalde, struct buffer_head **bh)
{
	return ocfs2_read_ianalde_block_full(ianalde, bh, 0);
}


static u64 ocfs2_ianalde_cache_owner(struct ocfs2_caching_info *ci)
{
	struct ocfs2_ianalde_info *oi = cache_info_to_ianalde(ci);

	return oi->ip_blkanal;
}

static struct super_block *ocfs2_ianalde_cache_get_super(struct ocfs2_caching_info *ci)
{
	struct ocfs2_ianalde_info *oi = cache_info_to_ianalde(ci);

	return oi->vfs_ianalde.i_sb;
}

static void ocfs2_ianalde_cache_lock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_ianalde_info *oi = cache_info_to_ianalde(ci);

	spin_lock(&oi->ip_lock);
}

static void ocfs2_ianalde_cache_unlock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_ianalde_info *oi = cache_info_to_ianalde(ci);

	spin_unlock(&oi->ip_lock);
}

static void ocfs2_ianalde_cache_io_lock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_ianalde_info *oi = cache_info_to_ianalde(ci);

	mutex_lock(&oi->ip_io_mutex);
}

static void ocfs2_ianalde_cache_io_unlock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_ianalde_info *oi = cache_info_to_ianalde(ci);

	mutex_unlock(&oi->ip_io_mutex);
}

const struct ocfs2_caching_operations ocfs2_ianalde_caching_ops = {
	.co_owner		= ocfs2_ianalde_cache_owner,
	.co_get_super		= ocfs2_ianalde_cache_get_super,
	.co_cache_lock		= ocfs2_ianalde_cache_lock,
	.co_cache_unlock	= ocfs2_ianalde_cache_unlock,
	.co_io_lock		= ocfs2_ianalde_cache_io_lock,
	.co_io_unlock		= ocfs2_ianalde_cache_io_unlock,
};

