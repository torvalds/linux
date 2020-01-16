// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * iyesde.c
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
#include "iyesde.h"
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

struct ocfs2_find_iyesde_args
{
	u64		fi_blkyes;
	unsigned long	fi_iyes;
	unsigned int	fi_flags;
	unsigned int	fi_sysfile_type;
};

static struct lock_class_key ocfs2_sysfile_lock_key[NUM_SYSTEM_INODES];

static int ocfs2_read_locked_iyesde(struct iyesde *iyesde,
				   struct ocfs2_find_iyesde_args *args);
static int ocfs2_init_locked_iyesde(struct iyesde *iyesde, void *opaque);
static int ocfs2_find_actor(struct iyesde *iyesde, void *opaque);
static int ocfs2_truncate_for_delete(struct ocfs2_super *osb,
				    struct iyesde *iyesde,
				    struct buffer_head *fe_bh);

static int ocfs2_filecheck_read_iyesde_block_full(struct iyesde *iyesde,
						 struct buffer_head **bh,
						 int flags, int type);
static int ocfs2_filecheck_validate_iyesde_block(struct super_block *sb,
						struct buffer_head *bh);
static int ocfs2_filecheck_repair_iyesde_block(struct super_block *sb,
					      struct buffer_head *bh);

void ocfs2_set_iyesde_flags(struct iyesde *iyesde)
{
	unsigned int flags = OCFS2_I(iyesde)->ip_attr;

	iyesde->i_flags &= ~(S_IMMUTABLE |
		S_SYNC | S_APPEND | S_NOATIME | S_DIRSYNC);

	if (flags & OCFS2_IMMUTABLE_FL)
		iyesde->i_flags |= S_IMMUTABLE;

	if (flags & OCFS2_SYNC_FL)
		iyesde->i_flags |= S_SYNC;
	if (flags & OCFS2_APPEND_FL)
		iyesde->i_flags |= S_APPEND;
	if (flags & OCFS2_NOATIME_FL)
		iyesde->i_flags |= S_NOATIME;
	if (flags & OCFS2_DIRSYNC_FL)
		iyesde->i_flags |= S_DIRSYNC;
}

/* Propagate flags from i_flags to OCFS2_I(iyesde)->ip_attr */
void ocfs2_get_iyesde_flags(struct ocfs2_iyesde_info *oi)
{
	unsigned int flags = oi->vfs_iyesde.i_flags;

	oi->ip_attr &= ~(OCFS2_SYNC_FL|OCFS2_APPEND_FL|
			OCFS2_IMMUTABLE_FL|OCFS2_NOATIME_FL|OCFS2_DIRSYNC_FL);
	if (flags & S_SYNC)
		oi->ip_attr |= OCFS2_SYNC_FL;
	if (flags & S_APPEND)
		oi->ip_attr |= OCFS2_APPEND_FL;
	if (flags & S_IMMUTABLE)
		oi->ip_attr |= OCFS2_IMMUTABLE_FL;
	if (flags & S_NOATIME)
		oi->ip_attr |= OCFS2_NOATIME_FL;
	if (flags & S_DIRSYNC)
		oi->ip_attr |= OCFS2_DIRSYNC_FL;
}

struct iyesde *ocfs2_ilookup(struct super_block *sb, u64 blkyes)
{
	struct ocfs2_find_iyesde_args args;

	args.fi_blkyes = blkyes;
	args.fi_flags = 0;
	args.fi_iyes = iyes_from_blkyes(sb, blkyes);
	args.fi_sysfile_type = 0;

	return ilookup5(sb, blkyes, ocfs2_find_actor, &args);
}
struct iyesde *ocfs2_iget(struct ocfs2_super *osb, u64 blkyes, unsigned flags,
			 int sysfile_type)
{
	int rc = -ESTALE;
	struct iyesde *iyesde = NULL;
	struct super_block *sb = osb->sb;
	struct ocfs2_find_iyesde_args args;
	journal_t *journal = OCFS2_SB(sb)->journal->j_journal;

	trace_ocfs2_iget_begin((unsigned long long)blkyes, flags,
			       sysfile_type);

	/* Ok. By yesw we've either got the offsets passed to us by the
	 * caller, or we just pulled them off the bh. Lets do some
	 * sanity checks to make sure they're OK. */
	if (blkyes == 0) {
		iyesde = ERR_PTR(-EINVAL);
		mlog_erryes(PTR_ERR(iyesde));
		goto bail;
	}

	args.fi_blkyes = blkyes;
	args.fi_flags = flags;
	args.fi_iyes = iyes_from_blkyes(sb, blkyes);
	args.fi_sysfile_type = sysfile_type;

	iyesde = iget5_locked(sb, args.fi_iyes, ocfs2_find_actor,
			     ocfs2_init_locked_iyesde, &args);
	/* iyesde was *yest* in the iyesde cache. 2.6.x requires
	 * us to do our own read_iyesde call and unlock it
	 * afterwards. */
	if (iyesde == NULL) {
		iyesde = ERR_PTR(-ENOMEM);
		mlog_erryes(PTR_ERR(iyesde));
		goto bail;
	}
	trace_ocfs2_iget5_locked(iyesde->i_state);
	if (iyesde->i_state & I_NEW) {
		rc = ocfs2_read_locked_iyesde(iyesde, &args);
		unlock_new_iyesde(iyesde);
	}
	if (is_bad_iyesde(iyesde)) {
		iput(iyesde);
		iyesde = ERR_PTR(rc);
		goto bail;
	}

	/*
	 * Set transaction id's of transactions that have to be committed
	 * to finish f[data]sync. We set them to currently running transaction
	 * as we canyest be sure that the iyesde or some of its metadata isn't
	 * part of the transaction - the iyesde could have been reclaimed and
	 * yesw it is reread from disk.
	 */
	if (journal) {
		transaction_t *transaction;
		tid_t tid;
		struct ocfs2_iyesde_info *oi = OCFS2_I(iyesde);

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
	if (!IS_ERR(iyesde)) {
		trace_ocfs2_iget_end(iyesde, 
			(unsigned long long)OCFS2_I(iyesde)->ip_blkyes);
	}

	return iyesde;
}


/*
 * here's how iyesdes get read from disk:
 * iget5_locked -> find_actor -> OCFS2_FIND_ACTOR
 * found? : return the in-memory iyesde
 * yest found? : get_new_iyesde -> OCFS2_INIT_LOCKED_INODE
 */

static int ocfs2_find_actor(struct iyesde *iyesde, void *opaque)
{
	struct ocfs2_find_iyesde_args *args = NULL;
	struct ocfs2_iyesde_info *oi = OCFS2_I(iyesde);
	int ret = 0;

	args = opaque;

	mlog_bug_on_msg(!iyesde, "No iyesde in find actor!\n");

	trace_ocfs2_find_actor(iyesde, iyesde->i_iyes, opaque, args->fi_blkyes);

	if (oi->ip_blkyes != args->fi_blkyes)
		goto bail;

	ret = 1;
bail:
	return ret;
}

/*
 * initialize the new iyesde, but don't do anything that would cause
 * us to sleep.
 * return 0 on success, 1 on failure
 */
static int ocfs2_init_locked_iyesde(struct iyesde *iyesde, void *opaque)
{
	struct ocfs2_find_iyesde_args *args = opaque;
	static struct lock_class_key ocfs2_quota_ip_alloc_sem_key,
				     ocfs2_file_ip_alloc_sem_key;

	iyesde->i_iyes = args->fi_iyes;
	OCFS2_I(iyesde)->ip_blkyes = args->fi_blkyes;
	if (args->fi_sysfile_type != 0)
		lockdep_set_class(&iyesde->i_rwsem,
			&ocfs2_sysfile_lock_key[args->fi_sysfile_type]);
	if (args->fi_sysfile_type == USER_QUOTA_SYSTEM_INODE ||
	    args->fi_sysfile_type == GROUP_QUOTA_SYSTEM_INODE ||
	    args->fi_sysfile_type == LOCAL_USER_QUOTA_SYSTEM_INODE ||
	    args->fi_sysfile_type == LOCAL_GROUP_QUOTA_SYSTEM_INODE)
		lockdep_set_class(&OCFS2_I(iyesde)->ip_alloc_sem,
				  &ocfs2_quota_ip_alloc_sem_key);
	else
		lockdep_set_class(&OCFS2_I(iyesde)->ip_alloc_sem,
				  &ocfs2_file_ip_alloc_sem_key);

	return 0;
}

void ocfs2_populate_iyesde(struct iyesde *iyesde, struct ocfs2_diyesde *fe,
			  int create_iyes)
{
	struct super_block *sb;
	struct ocfs2_super *osb;
	int use_plocks = 1;

	sb = iyesde->i_sb;
	osb = OCFS2_SB(sb);

	if ((osb->s_mount_opt & OCFS2_MOUNT_LOCALFLOCKS) ||
	    ocfs2_mount_local(osb) || !ocfs2_stack_supports_plocks())
		use_plocks = 0;

	/*
	 * These have all been checked by ocfs2_read_iyesde_block() or set
	 * by ocfs2_mkyesd_locked(), so a failure is a code bug.
	 */
	BUG_ON(!OCFS2_IS_VALID_DINODE(fe));  /* This means that read_iyesde
						canyest create a superblock
						iyesde today.  change if
						that is needed. */
	BUG_ON(!(fe->i_flags & cpu_to_le32(OCFS2_VALID_FL)));
	BUG_ON(le32_to_cpu(fe->i_fs_generation) != osb->fs_generation);


	OCFS2_I(iyesde)->ip_clusters = le32_to_cpu(fe->i_clusters);
	OCFS2_I(iyesde)->ip_attr = le32_to_cpu(fe->i_attr);
	OCFS2_I(iyesde)->ip_dyn_features = le16_to_cpu(fe->i_dyn_features);

	iyesde_set_iversion(iyesde, 1);
	iyesde->i_generation = le32_to_cpu(fe->i_generation);
	iyesde->i_rdev = huge_decode_dev(le64_to_cpu(fe->id1.dev1.i_rdev));
	iyesde->i_mode = le16_to_cpu(fe->i_mode);
	i_uid_write(iyesde, le32_to_cpu(fe->i_uid));
	i_gid_write(iyesde, le32_to_cpu(fe->i_gid));

	/* Fast symlinks will have i_size but yes allocated clusters. */
	if (S_ISLNK(iyesde->i_mode) && !fe->i_clusters) {
		iyesde->i_blocks = 0;
		iyesde->i_mapping->a_ops = &ocfs2_fast_symlink_aops;
	} else {
		iyesde->i_blocks = ocfs2_iyesde_sector_count(iyesde);
		iyesde->i_mapping->a_ops = &ocfs2_aops;
	}
	iyesde->i_atime.tv_sec = le64_to_cpu(fe->i_atime);
	iyesde->i_atime.tv_nsec = le32_to_cpu(fe->i_atime_nsec);
	iyesde->i_mtime.tv_sec = le64_to_cpu(fe->i_mtime);
	iyesde->i_mtime.tv_nsec = le32_to_cpu(fe->i_mtime_nsec);
	iyesde->i_ctime.tv_sec = le64_to_cpu(fe->i_ctime);
	iyesde->i_ctime.tv_nsec = le32_to_cpu(fe->i_ctime_nsec);

	if (OCFS2_I(iyesde)->ip_blkyes != le64_to_cpu(fe->i_blkyes))
		mlog(ML_ERROR,
		     "ip_blkyes %llu != i_blkyes %llu!\n",
		     (unsigned long long)OCFS2_I(iyesde)->ip_blkyes,
		     (unsigned long long)le64_to_cpu(fe->i_blkyes));

	set_nlink(iyesde, ocfs2_read_links_count(fe));

	trace_ocfs2_populate_iyesde(OCFS2_I(iyesde)->ip_blkyes,
				   le32_to_cpu(fe->i_flags));
	if (fe->i_flags & cpu_to_le32(OCFS2_SYSTEM_FL)) {
		OCFS2_I(iyesde)->ip_flags |= OCFS2_INODE_SYSTEM_FILE;
		iyesde->i_flags |= S_NOQUOTA;
	}
  
	if (fe->i_flags & cpu_to_le32(OCFS2_LOCAL_ALLOC_FL)) {
		OCFS2_I(iyesde)->ip_flags |= OCFS2_INODE_BITMAP;
	} else if (fe->i_flags & cpu_to_le32(OCFS2_BITMAP_FL)) {
		OCFS2_I(iyesde)->ip_flags |= OCFS2_INODE_BITMAP;
	} else if (fe->i_flags & cpu_to_le32(OCFS2_QUOTA_FL)) {
		iyesde->i_flags |= S_NOQUOTA;
	} else if (fe->i_flags & cpu_to_le32(OCFS2_SUPER_BLOCK_FL)) {
		/* we can't actually hit this as read_iyesde can't
		 * handle superblocks today ;-) */
		BUG();
	}

	switch (iyesde->i_mode & S_IFMT) {
	    case S_IFREG:
		    if (use_plocks)
			    iyesde->i_fop = &ocfs2_fops;
		    else
			    iyesde->i_fop = &ocfs2_fops_yes_plocks;
		    iyesde->i_op = &ocfs2_file_iops;
		    i_size_write(iyesde, le64_to_cpu(fe->i_size));
		    break;
	    case S_IFDIR:
		    iyesde->i_op = &ocfs2_dir_iops;
		    if (use_plocks)
			    iyesde->i_fop = &ocfs2_dops;
		    else
			    iyesde->i_fop = &ocfs2_dops_yes_plocks;
		    i_size_write(iyesde, le64_to_cpu(fe->i_size));
		    OCFS2_I(iyesde)->ip_dir_lock_gen = 1;
		    break;
	    case S_IFLNK:
		    iyesde->i_op = &ocfs2_symlink_iyesde_operations;
		    iyesde_yeshighmem(iyesde);
		    i_size_write(iyesde, le64_to_cpu(fe->i_size));
		    break;
	    default:
		    iyesde->i_op = &ocfs2_special_file_iops;
		    init_special_iyesde(iyesde, iyesde->i_mode,
				       iyesde->i_rdev);
		    break;
	}

	if (create_iyes) {
		iyesde->i_iyes = iyes_from_blkyes(iyesde->i_sb,
			       le64_to_cpu(fe->i_blkyes));

		/*
		 * If we ever want to create system files from kernel,
		 * the generation argument to
		 * ocfs2_iyesde_lock_res_init() will have to change.
		 */
		BUG_ON(le32_to_cpu(fe->i_flags) & OCFS2_SYSTEM_FL);

		ocfs2_iyesde_lock_res_init(&OCFS2_I(iyesde)->ip_iyesde_lockres,
					  OCFS2_LOCK_TYPE_META, 0, iyesde);

		ocfs2_iyesde_lock_res_init(&OCFS2_I(iyesde)->ip_open_lockres,
					  OCFS2_LOCK_TYPE_OPEN, 0, iyesde);
	}

	ocfs2_iyesde_lock_res_init(&OCFS2_I(iyesde)->ip_rw_lockres,
				  OCFS2_LOCK_TYPE_RW, iyesde->i_generation,
				  iyesde);

	ocfs2_set_iyesde_flags(iyesde);

	OCFS2_I(iyesde)->ip_last_used_slot = 0;
	OCFS2_I(iyesde)->ip_last_used_group = 0;

	if (S_ISDIR(iyesde->i_mode))
		ocfs2_resv_set_type(&OCFS2_I(iyesde)->ip_la_data_resv,
				    OCFS2_RESV_FLAG_DIR);
}

static int ocfs2_read_locked_iyesde(struct iyesde *iyesde,
				   struct ocfs2_find_iyesde_args *args)
{
	struct super_block *sb;
	struct ocfs2_super *osb;
	struct ocfs2_diyesde *fe;
	struct buffer_head *bh = NULL;
	int status, can_lock, lock_level = 0;
	u32 generation = 0;

	status = -EINVAL;
	sb = iyesde->i_sb;
	osb = OCFS2_SB(sb);

	/*
	 * To improve performance of cold-cache iyesde stats, we take
	 * the cluster lock here if possible.
	 *
	 * Generally, OCFS2 never trusts the contents of an iyesde
	 * unless it's holding a cluster lock, so taking it here isn't
	 * a correctness issue as much as it is a performance
	 * improvement.
	 *
	 * There are three times when taking the lock is yest a good idea:
	 *
	 * 1) During startup, before we have initialized the DLM.
	 *
	 * 2) If we are reading certain system files which never get
	 *    cluster locks (local alloc, truncate log).
	 *
	 * 3) If the process doing the iget() is responsible for
	 *    orphan dir recovery. We're holding the orphan dir lock and
	 *    can get into a deadlock with ayesther process on ayesther
	 *    yesde in ->delete_iyesde().
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

	trace_ocfs2_read_locked_iyesde(
		(unsigned long long)OCFS2_I(iyesde)->ip_blkyes, can_lock);

	/*
	 * To maintain backwards compatibility with older versions of
	 * ocfs2-tools, we still store the generation value for system
	 * files. The only ones that actually matter to userspace are
	 * the journals, but it's easier and inexpensive to just flag
	 * all system files similarly.
	 */
	if (args->fi_flags & OCFS2_FI_FLAG_SYSFILE)
		generation = osb->fs_generation;

	ocfs2_iyesde_lock_res_init(&OCFS2_I(iyesde)->ip_iyesde_lockres,
				  OCFS2_LOCK_TYPE_META,
				  generation, iyesde);

	ocfs2_iyesde_lock_res_init(&OCFS2_I(iyesde)->ip_open_lockres,
				  OCFS2_LOCK_TYPE_OPEN,
				  0, iyesde);

	if (can_lock) {
		status = ocfs2_open_lock(iyesde);
		if (status) {
			make_bad_iyesde(iyesde);
			mlog_erryes(status);
			return status;
		}
		status = ocfs2_iyesde_lock(iyesde, NULL, lock_level);
		if (status) {
			make_bad_iyesde(iyesde);
			mlog_erryes(status);
			return status;
		}
	}

	if (args->fi_flags & OCFS2_FI_FLAG_ORPHAN_RECOVERY) {
		status = ocfs2_try_open_lock(iyesde, 0);
		if (status) {
			make_bad_iyesde(iyesde);
			return status;
		}
	}

	if (can_lock) {
		if (args->fi_flags & OCFS2_FI_FLAG_FILECHECK_CHK)
			status = ocfs2_filecheck_read_iyesde_block_full(iyesde,
						&bh, OCFS2_BH_IGNORE_CACHE, 0);
		else if (args->fi_flags & OCFS2_FI_FLAG_FILECHECK_FIX)
			status = ocfs2_filecheck_read_iyesde_block_full(iyesde,
						&bh, OCFS2_BH_IGNORE_CACHE, 1);
		else
			status = ocfs2_read_iyesde_block_full(iyesde,
						&bh, OCFS2_BH_IGNORE_CACHE);
	} else {
		status = ocfs2_read_blocks_sync(osb, args->fi_blkyes, 1, &bh);
		/*
		 * If buffer is in jbd, then its checksum may yest have been
		 * computed as yet.
		 */
		if (!status && !buffer_jbd(bh)) {
			if (args->fi_flags & OCFS2_FI_FLAG_FILECHECK_CHK)
				status = ocfs2_filecheck_validate_iyesde_block(
								osb->sb, bh);
			else if (args->fi_flags & OCFS2_FI_FLAG_FILECHECK_FIX)
				status = ocfs2_filecheck_repair_iyesde_block(
								osb->sb, bh);
			else
				status = ocfs2_validate_iyesde_block(
								osb->sb, bh);
		}
	}
	if (status < 0) {
		mlog_erryes(status);
		goto bail;
	}

	status = -EINVAL;
	fe = (struct ocfs2_diyesde *) bh->b_data;

	/*
	 * This is a code bug. Right yesw the caller needs to
	 * understand whether it is asking for a system file iyesde or
	 * yest so the proper lock names can be built.
	 */
	mlog_bug_on_msg(!!(fe->i_flags & cpu_to_le32(OCFS2_SYSTEM_FL)) !=
			!!(args->fi_flags & OCFS2_FI_FLAG_SYSFILE),
			"Iyesde %llu: system file state is ambiguous\n",
			(unsigned long long)args->fi_blkyes);

	if (S_ISCHR(le16_to_cpu(fe->i_mode)) ||
	    S_ISBLK(le16_to_cpu(fe->i_mode)))
		iyesde->i_rdev = huge_decode_dev(le64_to_cpu(fe->id1.dev1.i_rdev));

	ocfs2_populate_iyesde(iyesde, fe, 0);

	BUG_ON(args->fi_blkyes != le64_to_cpu(fe->i_blkyes));

	if (buffer_dirty(bh) && !buffer_jbd(bh)) {
		if (can_lock) {
			ocfs2_iyesde_unlock(iyesde, lock_level);
			lock_level = 1;
			ocfs2_iyesde_lock(iyesde, NULL, lock_level);
		}
		status = ocfs2_write_block(osb, bh, INODE_CACHE(iyesde));
		if (status < 0) {
			mlog_erryes(status);
			goto bail;
		}
	}

	status = 0;

bail:
	if (can_lock)
		ocfs2_iyesde_unlock(iyesde, lock_level);

	if (status < 0)
		make_bad_iyesde(iyesde);

	brelse(bh);

	return status;
}

void ocfs2_sync_blockdev(struct super_block *sb)
{
	sync_blockdev(sb->s_bdev);
}

static int ocfs2_truncate_for_delete(struct ocfs2_super *osb,
				     struct iyesde *iyesde,
				     struct buffer_head *fe_bh)
{
	int status = 0;
	struct ocfs2_diyesde *fe;
	handle_t *handle = NULL;

	fe = (struct ocfs2_diyesde *) fe_bh->b_data;

	/*
	 * This check will also skip truncate of iyesdes with inline
	 * data and fast symlinks.
	 */
	if (fe->i_clusters) {
		if (ocfs2_should_order_data(iyesde))
			ocfs2_begin_ordered_truncate(iyesde, 0);

		handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			handle = NULL;
			mlog_erryes(status);
			goto out;
		}

		status = ocfs2_journal_access_di(handle, INODE_CACHE(iyesde),
						 fe_bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_erryes(status);
			goto out;
		}

		i_size_write(iyesde, 0);

		status = ocfs2_mark_iyesde_dirty(handle, iyesde, fe_bh);
		if (status < 0) {
			mlog_erryes(status);
			goto out;
		}

		ocfs2_commit_trans(osb, handle);
		handle = NULL;

		status = ocfs2_commit_truncate(osb, iyesde, fe_bh);
		if (status < 0)
			mlog_erryes(status);
	}

out:
	if (handle)
		ocfs2_commit_trans(osb, handle);
	return status;
}

static int ocfs2_remove_iyesde(struct iyesde *iyesde,
			      struct buffer_head *di_bh,
			      struct iyesde *orphan_dir_iyesde,
			      struct buffer_head *orphan_dir_bh)
{
	int status;
	struct iyesde *iyesde_alloc_iyesde = NULL;
	struct buffer_head *iyesde_alloc_bh = NULL;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(iyesde->i_sb);
	struct ocfs2_diyesde *di = (struct ocfs2_diyesde *) di_bh->b_data;

	iyesde_alloc_iyesde =
		ocfs2_get_system_file_iyesde(osb, INODE_ALLOC_SYSTEM_INODE,
					    le16_to_cpu(di->i_suballoc_slot));
	if (!iyesde_alloc_iyesde) {
		status = -ENOENT;
		mlog_erryes(status);
		goto bail;
	}

	iyesde_lock(iyesde_alloc_iyesde);
	status = ocfs2_iyesde_lock(iyesde_alloc_iyesde, &iyesde_alloc_bh, 1);
	if (status < 0) {
		iyesde_unlock(iyesde_alloc_iyesde);

		mlog_erryes(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb, OCFS2_DELETE_INODE_CREDITS +
				   ocfs2_quota_trans_credits(iyesde->i_sb));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_erryes(status);
		goto bail_unlock;
	}

	if (!(OCFS2_I(iyesde)->ip_flags & OCFS2_INODE_SKIP_ORPHAN_DIR)) {
		status = ocfs2_orphan_del(osb, handle, orphan_dir_iyesde, iyesde,
					  orphan_dir_bh, false);
		if (status < 0) {
			mlog_erryes(status);
			goto bail_commit;
		}
	}

	/* set the iyesdes dtime */
	status = ocfs2_journal_access_di(handle, INODE_CACHE(iyesde), di_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erryes(status);
		goto bail_commit;
	}

	di->i_dtime = cpu_to_le64(ktime_get_real_seconds());
	di->i_flags &= cpu_to_le32(~(OCFS2_VALID_FL | OCFS2_ORPHANED_FL));
	ocfs2_journal_dirty(handle, di_bh);

	ocfs2_remove_from_cache(INODE_CACHE(iyesde), di_bh);
	dquot_free_iyesde(iyesde);

	status = ocfs2_free_diyesde(handle, iyesde_alloc_iyesde,
				   iyesde_alloc_bh, di);
	if (status < 0)
		mlog_erryes(status);

bail_commit:
	ocfs2_commit_trans(osb, handle);
bail_unlock:
	ocfs2_iyesde_unlock(iyesde_alloc_iyesde, 1);
	iyesde_unlock(iyesde_alloc_iyesde);
	brelse(iyesde_alloc_bh);
bail:
	iput(iyesde_alloc_iyesde);

	return status;
}

/*
 * Serialize with orphan dir recovery. If the process doing
 * recovery on this orphan dir does an iget() with the dir
 * i_mutex held, we'll deadlock here. Instead we detect this
 * and exit early - recovery will wipe this iyesde for us.
 */
static int ocfs2_check_orphan_recovery_state(struct ocfs2_super *osb,
					     int slot)
{
	int ret = 0;

	spin_lock(&osb->osb_lock);
	if (ocfs2_yesde_map_test_bit(osb, &osb->osb_recovering_orphan_dirs, slot)) {
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

static int ocfs2_wipe_iyesde(struct iyesde *iyesde,
			    struct buffer_head *di_bh)
{
	int status, orphaned_slot = -1;
	struct iyesde *orphan_dir_iyesde = NULL;
	struct buffer_head *orphan_dir_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(iyesde->i_sb);
	struct ocfs2_diyesde *di = (struct ocfs2_diyesde *) di_bh->b_data;

	if (!(OCFS2_I(iyesde)->ip_flags & OCFS2_INODE_SKIP_ORPHAN_DIR)) {
		orphaned_slot = le16_to_cpu(di->i_orphaned_slot);

		status = ocfs2_check_orphan_recovery_state(osb, orphaned_slot);
		if (status)
			return status;

		orphan_dir_iyesde = ocfs2_get_system_file_iyesde(osb,
							       ORPHAN_DIR_SYSTEM_INODE,
							       orphaned_slot);
		if (!orphan_dir_iyesde) {
			status = -ENOENT;
			mlog_erryes(status);
			goto bail;
		}

		/* Lock the orphan dir. The lock will be held for the entire
		 * delete_iyesde operation. We do this yesw to avoid races with
		 * recovery completion on other yesdes. */
		iyesde_lock(orphan_dir_iyesde);
		status = ocfs2_iyesde_lock(orphan_dir_iyesde, &orphan_dir_bh, 1);
		if (status < 0) {
			iyesde_unlock(orphan_dir_iyesde);

			mlog_erryes(status);
			goto bail;
		}
	}

	/* we do this while holding the orphan dir lock because we
	 * don't want recovery being run from ayesther yesde to try an
	 * iyesde delete underneath us -- this will result in two yesdes
	 * truncating the same file! */
	status = ocfs2_truncate_for_delete(osb, iyesde, di_bh);
	if (status < 0) {
		mlog_erryes(status);
		goto bail_unlock_dir;
	}

	/* Remove any dir index tree */
	if (S_ISDIR(iyesde->i_mode)) {
		status = ocfs2_dx_dir_truncate(iyesde, di_bh);
		if (status) {
			mlog_erryes(status);
			goto bail_unlock_dir;
		}
	}

	/*Free extended attribute resources associated with this iyesde.*/
	status = ocfs2_xattr_remove(iyesde, di_bh);
	if (status < 0) {
		mlog_erryes(status);
		goto bail_unlock_dir;
	}

	status = ocfs2_remove_refcount_tree(iyesde, di_bh);
	if (status < 0) {
		mlog_erryes(status);
		goto bail_unlock_dir;
	}

	status = ocfs2_remove_iyesde(iyesde, di_bh, orphan_dir_iyesde,
				    orphan_dir_bh);
	if (status < 0)
		mlog_erryes(status);

bail_unlock_dir:
	if (OCFS2_I(iyesde)->ip_flags & OCFS2_INODE_SKIP_ORPHAN_DIR)
		return status;

	ocfs2_iyesde_unlock(orphan_dir_iyesde, 1);
	iyesde_unlock(orphan_dir_iyesde);
	brelse(orphan_dir_bh);
bail:
	iput(orphan_dir_iyesde);
	ocfs2_signal_wipe_completion(osb, orphaned_slot);

	return status;
}

/* There is a series of simple checks that should be done before a
 * trylock is even considered. Encapsulate those in this function. */
static int ocfs2_iyesde_is_valid_to_delete(struct iyesde *iyesde)
{
	int ret = 0;
	struct ocfs2_iyesde_info *oi = OCFS2_I(iyesde);
	struct ocfs2_super *osb = OCFS2_SB(iyesde->i_sb);

	trace_ocfs2_iyesde_is_valid_to_delete(current, osb->dc_task,
					     (unsigned long long)oi->ip_blkyes,
					     oi->ip_flags);

	/* We shouldn't be getting here for the root directory
	 * iyesde.. */
	if (iyesde == osb->root_iyesde) {
		mlog(ML_ERROR, "Skipping delete of root iyesde.\n");
		goto bail;
	}

	/*
	 * If we're coming from downconvert_thread we can't go into our own
	 * voting [hello, deadlock city!] so we canyest delete the iyesde. But
	 * since we dropped last iyesde ref when downconverting dentry lock,
	 * we canyest have the file open and thus the yesde doing unlink will
	 * take care of deleting the iyesde.
	 */
	if (current == osb->dc_task)
		goto bail;

	spin_lock(&oi->ip_lock);
	/* OCFS2 *never* deletes system files. This should technically
	 * never get here as system file iyesdes should always have a
	 * positive link count. */
	if (oi->ip_flags & OCFS2_INODE_SYSTEM_FILE) {
		mlog(ML_ERROR, "Skipping delete of system file %llu\n",
		     (unsigned long long)oi->ip_blkyes);
		goto bail_unlock;
	}

	ret = 1;
bail_unlock:
	spin_unlock(&oi->ip_lock);
bail:
	return ret;
}

/* Query the cluster to determine whether we should wipe an iyesde from
 * disk or yest.
 *
 * Requires the iyesde to have the cluster lock. */
static int ocfs2_query_iyesde_wipe(struct iyesde *iyesde,
				  struct buffer_head *di_bh,
				  int *wipe)
{
	int status = 0, reason = 0;
	struct ocfs2_iyesde_info *oi = OCFS2_I(iyesde);
	struct ocfs2_diyesde *di;

	*wipe = 0;

	trace_ocfs2_query_iyesde_wipe_begin((unsigned long long)oi->ip_blkyes,
					   iyesde->i_nlink);

	/* While we were waiting for the cluster lock in
	 * ocfs2_delete_iyesde, ayesther yesde might have asked to delete
	 * the iyesde. Recheck our flags to catch this. */
	if (!ocfs2_iyesde_is_valid_to_delete(iyesde)) {
		reason = 1;
		goto bail;
	}

	/* Now that we have an up to date iyesde, we can double check
	 * the link count. */
	if (iyesde->i_nlink)
		goto bail;

	/* Do some basic iyesde verification... */
	di = (struct ocfs2_diyesde *) di_bh->b_data;
	if (!(di->i_flags & cpu_to_le32(OCFS2_ORPHANED_FL)) &&
	    !(oi->ip_flags & OCFS2_INODE_SKIP_ORPHAN_DIR)) {
		/*
		 * Iyesdes in the orphan dir must have ORPHANED_FL.  The only
		 * iyesdes that come back out of the orphan dir are reflink
		 * targets. A reflink target may be moved out of the orphan
		 * dir between the time we scan the directory and the time we
		 * process it. This would lead to HAS_REFCOUNT_FL being set but
		 * ORPHANED_FL yest.
		 */
		if (di->i_dyn_features & cpu_to_le16(OCFS2_HAS_REFCOUNT_FL)) {
			reason = 2;
			goto bail;
		}

		/* for lack of a better error? */
		status = -EEXIST;
		mlog(ML_ERROR,
		     "Iyesde %llu (on-disk %llu) yest orphaned! "
		     "Disk flags  0x%x, iyesde flags 0x%x\n",
		     (unsigned long long)oi->ip_blkyes,
		     (unsigned long long)le64_to_cpu(di->i_blkyes),
		     le32_to_cpu(di->i_flags), oi->ip_flags);
		goto bail;
	}

	/* has someone already deleted us?! baaad... */
	if (di->i_dtime) {
		status = -EEXIST;
		mlog_erryes(status);
		goto bail;
	}

	/*
	 * This is how ocfs2 determines whether an iyesde is still live
	 * within the cluster. Every yesde takes a shared read lock on
	 * the iyesde open lock in ocfs2_read_locked_iyesde(). When we
	 * get to ->delete_iyesde(), each yesde tries to convert it's
	 * lock to an exclusive. Trylocks are serialized by the iyesde
	 * meta data lock. If the upconvert succeeds, we kyesw the iyesde
	 * is yes longer live and can be deleted.
	 *
	 * Though we call this with the meta data lock held, the
	 * trylock keeps us from ABBA deadlock.
	 */
	status = ocfs2_try_open_lock(iyesde, 1);
	if (status == -EAGAIN) {
		status = 0;
		reason = 3;
		goto bail;
	}
	if (status < 0) {
		mlog_erryes(status);
		goto bail;
	}

	*wipe = 1;
	trace_ocfs2_query_iyesde_wipe_succ(le16_to_cpu(di->i_orphaned_slot));

bail:
	trace_ocfs2_query_iyesde_wipe_end(status, reason);
	return status;
}

/* Support function for ocfs2_delete_iyesde. Will help us keep the
 * iyesde data in a consistent state for clear_iyesde. Always truncates
 * pages, optionally sync's them first. */
static void ocfs2_cleanup_delete_iyesde(struct iyesde *iyesde,
				       int sync_data)
{
	trace_ocfs2_cleanup_delete_iyesde(
		(unsigned long long)OCFS2_I(iyesde)->ip_blkyes, sync_data);
	if (sync_data)
		filemap_write_and_wait(iyesde->i_mapping);
	truncate_iyesde_pages_final(&iyesde->i_data);
}

static void ocfs2_delete_iyesde(struct iyesde *iyesde)
{
	int wipe, status;
	sigset_t oldset;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_diyesde *di = NULL;

	trace_ocfs2_delete_iyesde(iyesde->i_iyes,
				 (unsigned long long)OCFS2_I(iyesde)->ip_blkyes,
				 is_bad_iyesde(iyesde));

	/* When we fail in read_iyesde() we mark iyesde as bad. The second test
	 * catches the case when iyesde allocation fails before allocating
	 * a block for iyesde. */
	if (is_bad_iyesde(iyesde) || !OCFS2_I(iyesde)->ip_blkyes)
		goto bail;

	if (!ocfs2_iyesde_is_valid_to_delete(iyesde)) {
		/* It's probably yest necessary to truncate_iyesde_pages
		 * here but we do it for safety anyway (it will most
		 * likely be a yes-op anyway) */
		ocfs2_cleanup_delete_iyesde(iyesde, 0);
		goto bail;
	}

	dquot_initialize(iyesde);

	/* We want to block signals in delete_iyesde as the lock and
	 * messaging paths may return us -ERESTARTSYS. Which would
	 * cause us to exit early, resulting in iyesdes being orphaned
	 * forever. */
	ocfs2_block_signals(&oldset);

	/*
	 * Synchronize us against ocfs2_get_dentry. We take this in
	 * shared mode so that all yesdes can still concurrently
	 * process deletes.
	 */
	status = ocfs2_nfs_sync_lock(OCFS2_SB(iyesde->i_sb), 0);
	if (status < 0) {
		mlog(ML_ERROR, "getting nfs sync lock(PR) failed %d\n", status);
		ocfs2_cleanup_delete_iyesde(iyesde, 0);
		goto bail_unblock;
	}
	/* Lock down the iyesde. This gives us an up to date view of
	 * it's metadata (for verification), and allows us to
	 * serialize delete_iyesde on multiple yesdes.
	 *
	 * Even though we might be doing a truncate, we don't take the
	 * allocation lock here as it won't be needed - yesbody will
	 * have the file open.
	 */
	status = ocfs2_iyesde_lock(iyesde, &di_bh, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_erryes(status);
		ocfs2_cleanup_delete_iyesde(iyesde, 0);
		goto bail_unlock_nfs_sync;
	}

	di = (struct ocfs2_diyesde *)di_bh->b_data;
	/* Skip iyesde deletion and wait for dio orphan entry recovered
	 * first */
	if (unlikely(di->i_flags & cpu_to_le32(OCFS2_DIO_ORPHANED_FL))) {
		ocfs2_cleanup_delete_iyesde(iyesde, 0);
		goto bail_unlock_iyesde;
	}

	/* Query the cluster. This will be the final decision made
	 * before we go ahead and wipe the iyesde. */
	status = ocfs2_query_iyesde_wipe(iyesde, di_bh, &wipe);
	if (!wipe || status < 0) {
		/* Error and remote iyesde busy both mean we won't be
		 * removing the iyesde, so they take almost the same
		 * path. */
		if (status < 0)
			mlog_erryes(status);

		/* Someone in the cluster has disallowed a wipe of
		 * this iyesde, or it was never completely
		 * orphaned. Write out the pages and exit yesw. */
		ocfs2_cleanup_delete_iyesde(iyesde, 1);
		goto bail_unlock_iyesde;
	}

	ocfs2_cleanup_delete_iyesde(iyesde, 0);

	status = ocfs2_wipe_iyesde(iyesde, di_bh);
	if (status < 0) {
		if (status != -EDEADLK)
			mlog_erryes(status);
		goto bail_unlock_iyesde;
	}

	/*
	 * Mark the iyesde as successfully deleted.
	 *
	 * This is important for ocfs2_clear_iyesde() as it will check
	 * this flag and skip any checkpointing work
	 *
	 * ocfs2_stuff_meta_lvb() also uses this flag to invalidate
	 * the LVB for other yesdes.
	 */
	OCFS2_I(iyesde)->ip_flags |= OCFS2_INODE_DELETED;

bail_unlock_iyesde:
	ocfs2_iyesde_unlock(iyesde, 1);
	brelse(di_bh);

bail_unlock_nfs_sync:
	ocfs2_nfs_sync_unlock(OCFS2_SB(iyesde->i_sb), 0);

bail_unblock:
	ocfs2_unblock_signals(&oldset);
bail:
	return;
}

static void ocfs2_clear_iyesde(struct iyesde *iyesde)
{
	int status;
	struct ocfs2_iyesde_info *oi = OCFS2_I(iyesde);
	struct ocfs2_super *osb = OCFS2_SB(iyesde->i_sb);

	clear_iyesde(iyesde);
	trace_ocfs2_clear_iyesde((unsigned long long)oi->ip_blkyes,
				iyesde->i_nlink);

	mlog_bug_on_msg(osb == NULL,
			"Iyesde=%lu\n", iyesde->i_iyes);

	dquot_drop(iyesde);

	/* To preven remote deletes we hold open lock before, yesw it
	 * is time to unlock PR and EX open locks. */
	ocfs2_open_unlock(iyesde);

	/* Do these before all the other work so that we don't bounce
	 * the downconvert thread while waiting to destroy the locks. */
	ocfs2_mark_lockres_freeing(osb, &oi->ip_rw_lockres);
	ocfs2_mark_lockres_freeing(osb, &oi->ip_iyesde_lockres);
	ocfs2_mark_lockres_freeing(osb, &oi->ip_open_lockres);

	ocfs2_resv_discard(&osb->osb_la_resmap,
			   &oi->ip_la_data_resv);
	ocfs2_resv_init_once(&oi->ip_la_data_resv);

	/* We very well may get a clear_iyesde before all an iyesdes
	 * metadata has hit disk. Of course, we can't drop any cluster
	 * locks until the journal has finished with it. The only
	 * exception here are successfully wiped iyesdes - their
	 * metadata can yesw be considered to be part of the system
	 * iyesdes from which it came. */
	if (!(oi->ip_flags & OCFS2_INODE_DELETED))
		ocfs2_checkpoint_iyesde(iyesde);

	mlog_bug_on_msg(!list_empty(&oi->ip_io_markers),
			"Clear iyesde of %llu, iyesde has io markers\n",
			(unsigned long long)oi->ip_blkyes);
	mlog_bug_on_msg(!list_empty(&oi->ip_unwritten_list),
			"Clear iyesde of %llu, iyesde has unwritten extents\n",
			(unsigned long long)oi->ip_blkyes);

	ocfs2_extent_map_trunc(iyesde, 0);

	status = ocfs2_drop_iyesde_locks(iyesde);
	if (status < 0)
		mlog_erryes(status);

	ocfs2_lock_res_free(&oi->ip_rw_lockres);
	ocfs2_lock_res_free(&oi->ip_iyesde_lockres);
	ocfs2_lock_res_free(&oi->ip_open_lockres);

	ocfs2_metadata_cache_exit(INODE_CACHE(iyesde));

	mlog_bug_on_msg(INODE_CACHE(iyesde)->ci_num_cached,
			"Clear iyesde of %llu, iyesde has %u cache items\n",
			(unsigned long long)oi->ip_blkyes,
			INODE_CACHE(iyesde)->ci_num_cached);

	mlog_bug_on_msg(!(INODE_CACHE(iyesde)->ci_flags & OCFS2_CACHE_FL_INLINE),
			"Clear iyesde of %llu, iyesde has a bad flag\n",
			(unsigned long long)oi->ip_blkyes);

	mlog_bug_on_msg(spin_is_locked(&oi->ip_lock),
			"Clear iyesde of %llu, iyesde is locked\n",
			(unsigned long long)oi->ip_blkyes);

	mlog_bug_on_msg(!mutex_trylock(&oi->ip_io_mutex),
			"Clear iyesde of %llu, io_mutex is locked\n",
			(unsigned long long)oi->ip_blkyes);
	mutex_unlock(&oi->ip_io_mutex);

	/*
	 * down_trylock() returns 0, down_write_trylock() returns 1
	 * kernel 1, world 0
	 */
	mlog_bug_on_msg(!down_write_trylock(&oi->ip_alloc_sem),
			"Clear iyesde of %llu, alloc_sem is locked\n",
			(unsigned long long)oi->ip_blkyes);
	up_write(&oi->ip_alloc_sem);

	mlog_bug_on_msg(oi->ip_open_count,
			"Clear iyesde of %llu has open count %d\n",
			(unsigned long long)oi->ip_blkyes, oi->ip_open_count);

	/* Clear all other flags. */
	oi->ip_flags = 0;
	oi->ip_dir_start_lookup = 0;
	oi->ip_blkyes = 0ULL;

	/*
	 * ip_jiyesde is used to track txns against this iyesde. We ensure that
	 * the journal is flushed before journal shutdown. Thus it is safe to
	 * have iyesdes get cleaned up after journal shutdown.
	 */
	jbd2_journal_release_jbd_iyesde(osb->journal->j_journal,
				       &oi->ip_jiyesde);
}

void ocfs2_evict_iyesde(struct iyesde *iyesde)
{
	if (!iyesde->i_nlink ||
	    (OCFS2_I(iyesde)->ip_flags & OCFS2_INODE_MAYBE_ORPHANED)) {
		ocfs2_delete_iyesde(iyesde);
	} else {
		truncate_iyesde_pages_final(&iyesde->i_data);
	}
	ocfs2_clear_iyesde(iyesde);
}

/* Called under iyesde_lock, with yes more references on the
 * struct iyesde, so it's safe here to check the flags field
 * and to manipulate i_nlink without any other locks. */
int ocfs2_drop_iyesde(struct iyesde *iyesde)
{
	struct ocfs2_iyesde_info *oi = OCFS2_I(iyesde);

	trace_ocfs2_drop_iyesde((unsigned long long)oi->ip_blkyes,
				iyesde->i_nlink, oi->ip_flags);

	assert_spin_locked(&iyesde->i_lock);
	iyesde->i_state |= I_WILL_FREE;
	spin_unlock(&iyesde->i_lock);
	write_iyesde_yesw(iyesde, 1);
	spin_lock(&iyesde->i_lock);
	WARN_ON(iyesde->i_state & I_NEW);
	iyesde->i_state &= ~I_WILL_FREE;

	return 1;
}

/*
 * This is called from our getattr.
 */
int ocfs2_iyesde_revalidate(struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int status = 0;

	trace_ocfs2_iyesde_revalidate(iyesde,
		iyesde ? (unsigned long long)OCFS2_I(iyesde)->ip_blkyes : 0ULL,
		iyesde ? (unsigned long long)OCFS2_I(iyesde)->ip_flags : 0);

	if (!iyesde) {
		status = -ENOENT;
		goto bail;
	}

	spin_lock(&OCFS2_I(iyesde)->ip_lock);
	if (OCFS2_I(iyesde)->ip_flags & OCFS2_INODE_DELETED) {
		spin_unlock(&OCFS2_I(iyesde)->ip_lock);
		status = -ENOENT;
		goto bail;
	}
	spin_unlock(&OCFS2_I(iyesde)->ip_lock);

	/* Let ocfs2_iyesde_lock do the work of updating our struct
	 * iyesde for us. */
	status = ocfs2_iyesde_lock(iyesde, NULL, 0);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_erryes(status);
		goto bail;
	}
	ocfs2_iyesde_unlock(iyesde, 0);
bail:
	return status;
}

/*
 * Updates a disk iyesde from a
 * struct iyesde.
 * Only takes ip_lock.
 */
int ocfs2_mark_iyesde_dirty(handle_t *handle,
			   struct iyesde *iyesde,
			   struct buffer_head *bh)
{
	int status;
	struct ocfs2_diyesde *fe = (struct ocfs2_diyesde *) bh->b_data;

	trace_ocfs2_mark_iyesde_dirty((unsigned long long)OCFS2_I(iyesde)->ip_blkyes);

	status = ocfs2_journal_access_di(handle, INODE_CACHE(iyesde), bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erryes(status);
		goto leave;
	}

	spin_lock(&OCFS2_I(iyesde)->ip_lock);
	fe->i_clusters = cpu_to_le32(OCFS2_I(iyesde)->ip_clusters);
	ocfs2_get_iyesde_flags(OCFS2_I(iyesde));
	fe->i_attr = cpu_to_le32(OCFS2_I(iyesde)->ip_attr);
	fe->i_dyn_features = cpu_to_le16(OCFS2_I(iyesde)->ip_dyn_features);
	spin_unlock(&OCFS2_I(iyesde)->ip_lock);

	fe->i_size = cpu_to_le64(i_size_read(iyesde));
	ocfs2_set_links_count(fe, iyesde->i_nlink);
	fe->i_uid = cpu_to_le32(i_uid_read(iyesde));
	fe->i_gid = cpu_to_le32(i_gid_read(iyesde));
	fe->i_mode = cpu_to_le16(iyesde->i_mode);
	fe->i_atime = cpu_to_le64(iyesde->i_atime.tv_sec);
	fe->i_atime_nsec = cpu_to_le32(iyesde->i_atime.tv_nsec);
	fe->i_ctime = cpu_to_le64(iyesde->i_ctime.tv_sec);
	fe->i_ctime_nsec = cpu_to_le32(iyesde->i_ctime.tv_nsec);
	fe->i_mtime = cpu_to_le64(iyesde->i_mtime.tv_sec);
	fe->i_mtime_nsec = cpu_to_le32(iyesde->i_mtime.tv_nsec);

	ocfs2_journal_dirty(handle, bh);
	ocfs2_update_iyesde_fsync_trans(handle, iyesde, 1);
leave:
	return status;
}

/*
 *
 * Updates a struct iyesde from a disk iyesde.
 * does yes i/o, only takes ip_lock.
 */
void ocfs2_refresh_iyesde(struct iyesde *iyesde,
			 struct ocfs2_diyesde *fe)
{
	spin_lock(&OCFS2_I(iyesde)->ip_lock);

	OCFS2_I(iyesde)->ip_clusters = le32_to_cpu(fe->i_clusters);
	OCFS2_I(iyesde)->ip_attr = le32_to_cpu(fe->i_attr);
	OCFS2_I(iyesde)->ip_dyn_features = le16_to_cpu(fe->i_dyn_features);
	ocfs2_set_iyesde_flags(iyesde);
	i_size_write(iyesde, le64_to_cpu(fe->i_size));
	set_nlink(iyesde, ocfs2_read_links_count(fe));
	i_uid_write(iyesde, le32_to_cpu(fe->i_uid));
	i_gid_write(iyesde, le32_to_cpu(fe->i_gid));
	iyesde->i_mode = le16_to_cpu(fe->i_mode);
	if (S_ISLNK(iyesde->i_mode) && le32_to_cpu(fe->i_clusters) == 0)
		iyesde->i_blocks = 0;
	else
		iyesde->i_blocks = ocfs2_iyesde_sector_count(iyesde);
	iyesde->i_atime.tv_sec = le64_to_cpu(fe->i_atime);
	iyesde->i_atime.tv_nsec = le32_to_cpu(fe->i_atime_nsec);
	iyesde->i_mtime.tv_sec = le64_to_cpu(fe->i_mtime);
	iyesde->i_mtime.tv_nsec = le32_to_cpu(fe->i_mtime_nsec);
	iyesde->i_ctime.tv_sec = le64_to_cpu(fe->i_ctime);
	iyesde->i_ctime.tv_nsec = le32_to_cpu(fe->i_ctime_nsec);

	spin_unlock(&OCFS2_I(iyesde)->ip_lock);
}

int ocfs2_validate_iyesde_block(struct super_block *sb,
			       struct buffer_head *bh)
{
	int rc;
	struct ocfs2_diyesde *di = (struct ocfs2_diyesde *)bh->b_data;

	trace_ocfs2_validate_iyesde_block((unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * If the ecc fails, we return the error but otherwise
	 * leave the filesystem running.  We kyesw any error is
	 * local to this block.
	 */
	rc = ocfs2_validate_meta_ecc(sb, bh->b_data, &di->i_check);
	if (rc) {
		mlog(ML_ERROR, "Checksum failed for diyesde %llu\n",
		     (unsigned long long)bh->b_blocknr);
		goto bail;
	}

	/*
	 * Errors after here are fatal.
	 */

	rc = -EINVAL;

	if (!OCFS2_IS_VALID_DINODE(di)) {
		rc = ocfs2_error(sb, "Invalid diyesde #%llu: signature = %.*s\n",
				 (unsigned long long)bh->b_blocknr, 7,
				 di->i_signature);
		goto bail;
	}

	if (le64_to_cpu(di->i_blkyes) != bh->b_blocknr) {
		rc = ocfs2_error(sb, "Invalid diyesde #%llu: i_blkyes is %llu\n",
				 (unsigned long long)bh->b_blocknr,
				 (unsigned long long)le64_to_cpu(di->i_blkyes));
		goto bail;
	}

	if (!(di->i_flags & cpu_to_le32(OCFS2_VALID_FL))) {
		rc = ocfs2_error(sb,
				 "Invalid diyesde #%llu: OCFS2_VALID_FL yest set\n",
				 (unsigned long long)bh->b_blocknr);
		goto bail;
	}

	if (le32_to_cpu(di->i_fs_generation) !=
	    OCFS2_SB(sb)->fs_generation) {
		rc = ocfs2_error(sb,
				 "Invalid diyesde #%llu: fs_generation is %u\n",
				 (unsigned long long)bh->b_blocknr,
				 le32_to_cpu(di->i_fs_generation));
		goto bail;
	}

	rc = 0;

bail:
	return rc;
}

static int ocfs2_filecheck_validate_iyesde_block(struct super_block *sb,
						struct buffer_head *bh)
{
	int rc = 0;
	struct ocfs2_diyesde *di = (struct ocfs2_diyesde *)bh->b_data;

	trace_ocfs2_filecheck_validate_iyesde_block(
		(unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * Call ocfs2_validate_meta_ecc() first since it has ecc repair
	 * function, but we should yest return error immediately when ecc
	 * validation fails, because the reason is quite likely the invalid
	 * iyesde number inputed.
	 */
	rc = ocfs2_validate_meta_ecc(sb, bh->b_data, &di->i_check);
	if (rc) {
		mlog(ML_ERROR,
		     "Filecheck: checksum failed for diyesde %llu\n",
		     (unsigned long long)bh->b_blocknr);
		rc = -OCFS2_FILECHECK_ERR_BLOCKECC;
	}

	if (!OCFS2_IS_VALID_DINODE(di)) {
		mlog(ML_ERROR,
		     "Filecheck: invalid diyesde #%llu: signature = %.*s\n",
		     (unsigned long long)bh->b_blocknr, 7, di->i_signature);
		rc = -OCFS2_FILECHECK_ERR_INVALIDINO;
		goto bail;
	} else if (rc)
		goto bail;

	if (le64_to_cpu(di->i_blkyes) != bh->b_blocknr) {
		mlog(ML_ERROR,
		     "Filecheck: invalid diyesde #%llu: i_blkyes is %llu\n",
		     (unsigned long long)bh->b_blocknr,
		     (unsigned long long)le64_to_cpu(di->i_blkyes));
		rc = -OCFS2_FILECHECK_ERR_BLOCKNO;
		goto bail;
	}

	if (!(di->i_flags & cpu_to_le32(OCFS2_VALID_FL))) {
		mlog(ML_ERROR,
		     "Filecheck: invalid diyesde #%llu: OCFS2_VALID_FL "
		     "yest set\n",
		     (unsigned long long)bh->b_blocknr);
		rc = -OCFS2_FILECHECK_ERR_VALIDFLAG;
		goto bail;
	}

	if (le32_to_cpu(di->i_fs_generation) !=
	    OCFS2_SB(sb)->fs_generation) {
		mlog(ML_ERROR,
		     "Filecheck: invalid diyesde #%llu: fs_generation is %u\n",
		     (unsigned long long)bh->b_blocknr,
		     le32_to_cpu(di->i_fs_generation));
		rc = -OCFS2_FILECHECK_ERR_GENERATION;
	}

bail:
	return rc;
}

static int ocfs2_filecheck_repair_iyesde_block(struct super_block *sb,
					      struct buffer_head *bh)
{
	int changed = 0;
	struct ocfs2_diyesde *di = (struct ocfs2_diyesde *)bh->b_data;

	if (!ocfs2_filecheck_validate_iyesde_block(sb, bh))
		return 0;

	trace_ocfs2_filecheck_repair_iyesde_block(
		(unsigned long long)bh->b_blocknr);

	if (ocfs2_is_hard_readonly(OCFS2_SB(sb)) ||
	    ocfs2_is_soft_readonly(OCFS2_SB(sb))) {
		mlog(ML_ERROR,
		     "Filecheck: canyest repair diyesde #%llu "
		     "on readonly filesystem\n",
		     (unsigned long long)bh->b_blocknr);
		return -OCFS2_FILECHECK_ERR_READONLY;
	}

	if (buffer_jbd(bh)) {
		mlog(ML_ERROR,
		     "Filecheck: canyest repair diyesde #%llu, "
		     "its buffer is in jbd\n",
		     (unsigned long long)bh->b_blocknr);
		return -OCFS2_FILECHECK_ERR_INJBD;
	}

	if (!OCFS2_IS_VALID_DINODE(di)) {
		/* Canyest fix invalid iyesde block */
		return -OCFS2_FILECHECK_ERR_INVALIDINO;
	}

	if (!(di->i_flags & cpu_to_le32(OCFS2_VALID_FL))) {
		/* Canyest just add VALID_FL flag back as a fix,
		 * need more things to check here.
		 */
		return -OCFS2_FILECHECK_ERR_VALIDFLAG;
	}

	if (le64_to_cpu(di->i_blkyes) != bh->b_blocknr) {
		di->i_blkyes = cpu_to_le64(bh->b_blocknr);
		changed = 1;
		mlog(ML_ERROR,
		     "Filecheck: reset diyesde #%llu: i_blkyes to %llu\n",
		     (unsigned long long)bh->b_blocknr,
		     (unsigned long long)le64_to_cpu(di->i_blkyes));
	}

	if (le32_to_cpu(di->i_fs_generation) !=
	    OCFS2_SB(sb)->fs_generation) {
		di->i_fs_generation = cpu_to_le32(OCFS2_SB(sb)->fs_generation);
		changed = 1;
		mlog(ML_ERROR,
		     "Filecheck: reset diyesde #%llu: fs_generation to %u\n",
		     (unsigned long long)bh->b_blocknr,
		     le32_to_cpu(di->i_fs_generation));
	}

	if (changed || ocfs2_validate_meta_ecc(sb, bh->b_data, &di->i_check)) {
		ocfs2_compute_meta_ecc(sb, bh->b_data, &di->i_check);
		mark_buffer_dirty(bh);
		mlog(ML_ERROR,
		     "Filecheck: reset diyesde #%llu: compute meta ecc\n",
		     (unsigned long long)bh->b_blocknr);
	}

	return 0;
}

static int
ocfs2_filecheck_read_iyesde_block_full(struct iyesde *iyesde,
				      struct buffer_head **bh,
				      int flags, int type)
{
	int rc;
	struct buffer_head *tmp = *bh;

	if (!type) /* Check iyesde block */
		rc = ocfs2_read_blocks(INODE_CACHE(iyesde),
				OCFS2_I(iyesde)->ip_blkyes,
				1, &tmp, flags,
				ocfs2_filecheck_validate_iyesde_block);
	else /* Repair iyesde block */
		rc = ocfs2_read_blocks(INODE_CACHE(iyesde),
				OCFS2_I(iyesde)->ip_blkyes,
				1, &tmp, flags,
				ocfs2_filecheck_repair_iyesde_block);

	/* If ocfs2_read_blocks() got us a new bh, pass it up. */
	if (!rc && !*bh)
		*bh = tmp;

	return rc;
}

int ocfs2_read_iyesde_block_full(struct iyesde *iyesde, struct buffer_head **bh,
				int flags)
{
	int rc;
	struct buffer_head *tmp = *bh;

	rc = ocfs2_read_blocks(INODE_CACHE(iyesde), OCFS2_I(iyesde)->ip_blkyes,
			       1, &tmp, flags, ocfs2_validate_iyesde_block);

	/* If ocfs2_read_blocks() got us a new bh, pass it up. */
	if (!rc && !*bh)
		*bh = tmp;

	return rc;
}

int ocfs2_read_iyesde_block(struct iyesde *iyesde, struct buffer_head **bh)
{
	return ocfs2_read_iyesde_block_full(iyesde, bh, 0);
}


static u64 ocfs2_iyesde_cache_owner(struct ocfs2_caching_info *ci)
{
	struct ocfs2_iyesde_info *oi = cache_info_to_iyesde(ci);

	return oi->ip_blkyes;
}

static struct super_block *ocfs2_iyesde_cache_get_super(struct ocfs2_caching_info *ci)
{
	struct ocfs2_iyesde_info *oi = cache_info_to_iyesde(ci);

	return oi->vfs_iyesde.i_sb;
}

static void ocfs2_iyesde_cache_lock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_iyesde_info *oi = cache_info_to_iyesde(ci);

	spin_lock(&oi->ip_lock);
}

static void ocfs2_iyesde_cache_unlock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_iyesde_info *oi = cache_info_to_iyesde(ci);

	spin_unlock(&oi->ip_lock);
}

static void ocfs2_iyesde_cache_io_lock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_iyesde_info *oi = cache_info_to_iyesde(ci);

	mutex_lock(&oi->ip_io_mutex);
}

static void ocfs2_iyesde_cache_io_unlock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_iyesde_info *oi = cache_info_to_iyesde(ci);

	mutex_unlock(&oi->ip_io_mutex);
}

const struct ocfs2_caching_operations ocfs2_iyesde_caching_ops = {
	.co_owner		= ocfs2_iyesde_cache_owner,
	.co_get_super		= ocfs2_iyesde_cache_get_super,
	.co_cache_lock		= ocfs2_iyesde_cache_lock,
	.co_cache_unlock	= ocfs2_iyesde_cache_unlock,
	.co_io_lock		= ocfs2_iyesde_cache_io_lock,
	.co_io_unlock		= ocfs2_iyesde_cache_io_unlock,
};

