/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * inode.c
 *
 * vfs' aops, fops, dops and iops
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

#include <asm/byteorder.h>

#define MLOG_MASK_PREFIX ML_INODE
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "namei.h"
#include "suballoc.h"
#include "super.h"
#include "symlink.h"
#include "sysfile.h"
#include "uptodate.h"
#include "vote.h"

#include "buffer_head_io.h"

struct ocfs2_find_inode_args
{
	u64		fi_blkno;
	unsigned long	fi_ino;
	unsigned int	fi_flags;
};

static int ocfs2_read_locked_inode(struct inode *inode,
				   struct ocfs2_find_inode_args *args);
static int ocfs2_init_locked_inode(struct inode *inode, void *opaque);
static int ocfs2_find_actor(struct inode *inode, void *opaque);
static int ocfs2_truncate_for_delete(struct ocfs2_super *osb,
				    struct inode *inode,
				    struct buffer_head *fe_bh);

void ocfs2_set_inode_flags(struct inode *inode)
{
	unsigned int flags = OCFS2_I(inode)->ip_attr;

	inode->i_flags &= ~(S_IMMUTABLE |
		S_SYNC | S_APPEND | S_NOATIME | S_DIRSYNC);

	if (flags & OCFS2_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;

	if (flags & OCFS2_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & OCFS2_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & OCFS2_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & OCFS2_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
}

struct inode *ocfs2_ilookup_for_vote(struct ocfs2_super *osb,
				     u64 blkno,
				     int delete_vote)
{
	struct ocfs2_find_inode_args args;

	/* ocfs2_ilookup_for_vote should *only* be called from the
	 * vote thread */
	BUG_ON(current != osb->vote_task);

	args.fi_blkno = blkno;
	args.fi_flags = OCFS2_FI_FLAG_NOWAIT;
	if (delete_vote)
		args.fi_flags |= OCFS2_FI_FLAG_DELETE;
	args.fi_ino = ino_from_blkno(osb->sb, blkno);
	return ilookup5(osb->sb, args.fi_ino, ocfs2_find_actor, &args);
}

struct inode *ocfs2_iget(struct ocfs2_super *osb, u64 blkno, int flags)
{
	struct inode *inode = NULL;
	struct super_block *sb = osb->sb;
	struct ocfs2_find_inode_args args;

	mlog_entry("(blkno = %llu)\n", (unsigned long long)blkno);

	/* Ok. By now we've either got the offsets passed to us by the
	 * caller, or we just pulled them off the bh. Lets do some
	 * sanity checks to make sure they're OK. */
	if (blkno == 0) {
		inode = ERR_PTR(-EINVAL);
		mlog_errno(PTR_ERR(inode));
		goto bail;
	}

	args.fi_blkno = blkno;
	args.fi_flags = flags;
	args.fi_ino = ino_from_blkno(sb, blkno);

	inode = iget5_locked(sb, args.fi_ino, ocfs2_find_actor,
			     ocfs2_init_locked_inode, &args);
	/* inode was *not* in the inode cache. 2.6.x requires
	 * us to do our own read_inode call and unlock it
	 * afterwards. */
	if (inode && inode->i_state & I_NEW) {
		mlog(0, "Inode was not in inode cache, reading it.\n");
		ocfs2_read_locked_inode(inode, &args);
		unlock_new_inode(inode);
	}
	if (inode == NULL) {
		inode = ERR_PTR(-ENOMEM);
		mlog_errno(PTR_ERR(inode));
		goto bail;
	}
	if (is_bad_inode(inode)) {
		iput(inode);
		inode = ERR_PTR(-ESTALE);
		mlog_errno(PTR_ERR(inode));
		goto bail;
	}

bail:
	if (!IS_ERR(inode)) {
		mlog(0, "returning inode with number %llu\n",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
		mlog_exit_ptr(inode);
	} else
		mlog_errno(PTR_ERR(inode));

	return inode;
}


/*
 * here's how inodes get read from disk:
 * iget5_locked -> find_actor -> OCFS2_FIND_ACTOR
 * found? : return the in-memory inode
 * not found? : get_new_inode -> OCFS2_INIT_LOCKED_INODE
 */

static int ocfs2_find_actor(struct inode *inode, void *opaque)
{
	struct ocfs2_find_inode_args *args = NULL;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	int ret = 0;

	mlog_entry("(0x%p, %lu, 0x%p)\n", inode, inode->i_ino, opaque);

	args = opaque;

	mlog_bug_on_msg(!inode, "No inode in find actor!\n");

	if (oi->ip_blkno != args->fi_blkno)
		goto bail;

	/* OCFS2_FI_FLAG_NOWAIT is *only* set from
	 * ocfs2_ilookup_for_vote which won't create an inode for one
	 * that isn't found. The vote thread which doesn't want to get
	 * an inode which is in the process of going away - otherwise
	 * the call to __wait_on_freeing_inode in find_inode_fast will
	 * cause it to deadlock on an inode which may be waiting on a
	 * vote (or lock release) in delete_inode */
	if ((args->fi_flags & OCFS2_FI_FLAG_NOWAIT) &&
	    (inode->i_state & (I_FREEING|I_CLEAR))) {
		/* As stated above, we're not going to return an
		 * inode.  In the case of a delete vote, the voting
		 * code is going to signal the other node to go
		 * ahead. Mark that state here, so this freeing inode
		 * has the state when it gets to delete_inode. */
		if (args->fi_flags & OCFS2_FI_FLAG_DELETE) {
			spin_lock(&oi->ip_lock);
			ocfs2_mark_inode_remotely_deleted(inode);
			spin_unlock(&oi->ip_lock);
		}
		goto bail;
	}

	ret = 1;
bail:
	mlog_exit(ret);
	return ret;
}

/*
 * initialize the new inode, but don't do anything that would cause
 * us to sleep.
 * return 0 on success, 1 on failure
 */
static int ocfs2_init_locked_inode(struct inode *inode, void *opaque)
{
	struct ocfs2_find_inode_args *args = opaque;

	mlog_entry("inode = %p, opaque = %p\n", inode, opaque);

	inode->i_ino = args->fi_ino;
	OCFS2_I(inode)->ip_blkno = args->fi_blkno;

	mlog_exit(0);
	return 0;
}

int ocfs2_populate_inode(struct inode *inode, struct ocfs2_dinode *fe,
		     	 int create_ino)
{
	struct super_block *sb;
	struct ocfs2_super *osb;
	int status = -EINVAL;

	mlog_entry("(0x%p, size:%llu)\n", inode,
		   (unsigned long long)fe->i_size);

	sb = inode->i_sb;
	osb = OCFS2_SB(sb);

	/* this means that read_inode cannot create a superblock inode
	 * today.  change if needed. */
	if (!OCFS2_IS_VALID_DINODE(fe) ||
	    !(fe->i_flags & cpu_to_le32(OCFS2_VALID_FL))) {
		mlog(ML_ERROR, "Invalid dinode: i_ino=%lu, i_blkno=%llu, "
		     "signature = %.*s, flags = 0x%x\n",
		     inode->i_ino,
		     (unsigned long long)le64_to_cpu(fe->i_blkno), 7,
		     fe->i_signature, le32_to_cpu(fe->i_flags));
		goto bail;
	}

	if (le32_to_cpu(fe->i_fs_generation) != osb->fs_generation) {
		mlog(ML_ERROR, "file entry generation does not match "
		     "superblock! osb->fs_generation=%x, "
		     "fe->i_fs_generation=%x\n",
		     osb->fs_generation, le32_to_cpu(fe->i_fs_generation));
		goto bail;
	}

	inode->i_version = 1;
	inode->i_generation = le32_to_cpu(fe->i_generation);
	inode->i_rdev = huge_decode_dev(le64_to_cpu(fe->id1.dev1.i_rdev));
	inode->i_mode = le16_to_cpu(fe->i_mode);
	inode->i_uid = le32_to_cpu(fe->i_uid);
	inode->i_gid = le32_to_cpu(fe->i_gid);

	/* Fast symlinks will have i_size but no allocated clusters. */
	if (S_ISLNK(inode->i_mode) && !fe->i_clusters)
		inode->i_blocks = 0;
	else
		inode->i_blocks =
			ocfs2_align_bytes_to_sectors(le64_to_cpu(fe->i_size));
	inode->i_mapping->a_ops = &ocfs2_aops;
	inode->i_atime.tv_sec = le64_to_cpu(fe->i_atime);
	inode->i_atime.tv_nsec = le32_to_cpu(fe->i_atime_nsec);
	inode->i_mtime.tv_sec = le64_to_cpu(fe->i_mtime);
	inode->i_mtime.tv_nsec = le32_to_cpu(fe->i_mtime_nsec);
	inode->i_ctime.tv_sec = le64_to_cpu(fe->i_ctime);
	inode->i_ctime.tv_nsec = le32_to_cpu(fe->i_ctime_nsec);

	if (OCFS2_I(inode)->ip_blkno != le64_to_cpu(fe->i_blkno))
		mlog(ML_ERROR,
		     "ip_blkno %llu != i_blkno %llu!\n",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno,
		     (unsigned long long)fe->i_blkno);

	OCFS2_I(inode)->ip_clusters = le32_to_cpu(fe->i_clusters);
	OCFS2_I(inode)->ip_orphaned_slot = OCFS2_INVALID_SLOT;
	OCFS2_I(inode)->ip_attr = le32_to_cpu(fe->i_attr);

	inode->i_nlink = le16_to_cpu(fe->i_links_count);

	if (fe->i_flags & cpu_to_le32(OCFS2_SYSTEM_FL))
		OCFS2_I(inode)->ip_flags |= OCFS2_INODE_SYSTEM_FILE;

	if (fe->i_flags & cpu_to_le32(OCFS2_LOCAL_ALLOC_FL)) {
		OCFS2_I(inode)->ip_flags |= OCFS2_INODE_BITMAP;
		mlog(0, "local alloc inode: i_ino=%lu\n", inode->i_ino);
	} else if (fe->i_flags & cpu_to_le32(OCFS2_BITMAP_FL)) {
		OCFS2_I(inode)->ip_flags |= OCFS2_INODE_BITMAP;
	} else if (fe->i_flags & cpu_to_le32(OCFS2_SUPER_BLOCK_FL)) {
		mlog(0, "superblock inode: i_ino=%lu\n", inode->i_ino);
		/* we can't actually hit this as read_inode can't
		 * handle superblocks today ;-) */
		BUG();
	}

	switch (inode->i_mode & S_IFMT) {
	    case S_IFREG:
		    inode->i_fop = &ocfs2_fops;
		    inode->i_op = &ocfs2_file_iops;
		    i_size_write(inode, le64_to_cpu(fe->i_size));
		    break;
	    case S_IFDIR:
		    inode->i_op = &ocfs2_dir_iops;
		    inode->i_fop = &ocfs2_dops;
		    i_size_write(inode, le64_to_cpu(fe->i_size));
		    break;
	    case S_IFLNK:
		    if (ocfs2_inode_is_fast_symlink(inode))
			inode->i_op = &ocfs2_fast_symlink_inode_operations;
		    else
			inode->i_op = &ocfs2_symlink_inode_operations;
		    i_size_write(inode, le64_to_cpu(fe->i_size));
		    break;
	    default:
		    inode->i_op = &ocfs2_special_file_iops;
		    init_special_inode(inode, inode->i_mode,
				       inode->i_rdev);
		    break;
	}

	if (create_ino) {
		inode->i_ino = ino_from_blkno(inode->i_sb,
			       le64_to_cpu(fe->i_blkno));

		/*
		 * If we ever want to create system files from kernel,
		 * the generation argument to
		 * ocfs2_inode_lock_res_init() will have to change.
		 */
		BUG_ON(fe->i_flags & cpu_to_le32(OCFS2_SYSTEM_FL));

		ocfs2_inode_lock_res_init(&OCFS2_I(inode)->ip_meta_lockres,
					  OCFS2_LOCK_TYPE_META, 0, inode);
	}

	ocfs2_inode_lock_res_init(&OCFS2_I(inode)->ip_rw_lockres,
				  OCFS2_LOCK_TYPE_RW, inode->i_generation,
				  inode);

	ocfs2_inode_lock_res_init(&OCFS2_I(inode)->ip_data_lockres,
				  OCFS2_LOCK_TYPE_DATA, inode->i_generation,
				  inode);

	ocfs2_set_inode_flags(inode);

	status = 0;
bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_read_locked_inode(struct inode *inode,
				   struct ocfs2_find_inode_args *args)
{
	struct super_block *sb;
	struct ocfs2_super *osb;
	struct ocfs2_dinode *fe;
	struct buffer_head *bh = NULL;
	int status, can_lock;
	u32 generation = 0;

	mlog_entry("(0x%p, 0x%p)\n", inode, args);

	status = -EINVAL;
	if (inode == NULL || inode->i_sb == NULL) {
		mlog(ML_ERROR, "bad inode\n");
		return status;
	}
	sb = inode->i_sb;
	osb = OCFS2_SB(sb);

	if (!args) {
		mlog(ML_ERROR, "bad inode args\n");
		make_bad_inode(inode);
		return status;
	}

	/*
	 * To improve performance of cold-cache inode stats, we take
	 * the cluster lock here if possible.
	 *
	 * Generally, OCFS2 never trusts the contents of an inode
	 * unless it's holding a cluster lock, so taking it here isn't
	 * a correctness issue as much as it is a performance
	 * improvement.
	 *
	 * There are three times when taking the lock is not a good idea:
	 *
	 * 1) During startup, before we have initialized the DLM.
	 *
	 * 2) If we are reading certain system files which never get
	 *    cluster locks (local alloc, truncate log).
	 *
	 * 3) If the process doing the iget() is responsible for
	 *    orphan dir recovery. We're holding the orphan dir lock and
	 *    can get into a deadlock with another process on another
	 *    node in ->delete_inode().
	 *
	 * #1 and #2 can be simply solved by never taking the lock
	 * here for system files (which are the only type we read
	 * during mount). It's a heavier approach, but our main
	 * concern is user-accesible files anyway.
	 *
	 * #3 works itself out because we'll eventually take the
	 * cluster lock before trusting anything anyway.
	 */
	can_lock = !(args->fi_flags & OCFS2_FI_FLAG_SYSFILE)
		&& !(args->fi_flags & OCFS2_FI_FLAG_NOLOCK);

	/*
	 * To maintain backwards compatibility with older versions of
	 * ocfs2-tools, we still store the generation value for system
	 * files. The only ones that actually matter to userspace are
	 * the journals, but it's easier and inexpensive to just flag
	 * all system files similarly.
	 */
	if (args->fi_flags & OCFS2_FI_FLAG_SYSFILE)
		generation = osb->fs_generation;

	ocfs2_inode_lock_res_init(&OCFS2_I(inode)->ip_meta_lockres,
				  OCFS2_LOCK_TYPE_META,
				  generation, inode);

	if (can_lock) {
		status = ocfs2_meta_lock(inode, NULL, 0);
		if (status) {
			make_bad_inode(inode);
			mlog_errno(status);
			return status;
		}
	}

	status = ocfs2_read_block(osb, args->fi_blkno, &bh, 0,
				  can_lock ? inode : NULL);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = -EINVAL;
	fe = (struct ocfs2_dinode *) bh->b_data;
	if (!OCFS2_IS_VALID_DINODE(fe)) {
		mlog(ML_ERROR, "Invalid dinode #%llu: signature = %.*s\n",
		     (unsigned long long)fe->i_blkno, 7, fe->i_signature);
		goto bail;
	}

	/*
	 * This is a code bug. Right now the caller needs to
	 * understand whether it is asking for a system file inode or
	 * not so the proper lock names can be built.
	 */
	mlog_bug_on_msg(!!(fe->i_flags & cpu_to_le32(OCFS2_SYSTEM_FL)) !=
			!!(args->fi_flags & OCFS2_FI_FLAG_SYSFILE),
			"Inode %llu: system file state is ambigous\n",
			(unsigned long long)args->fi_blkno);

	if (S_ISCHR(le16_to_cpu(fe->i_mode)) ||
	    S_ISBLK(le16_to_cpu(fe->i_mode)))
    		inode->i_rdev = huge_decode_dev(le64_to_cpu(fe->id1.dev1.i_rdev));

	if (ocfs2_populate_inode(inode, fe, 0) < 0) {
		mlog(ML_ERROR, "populate failed! i_blkno=%llu, i_ino=%lu\n",
		     (unsigned long long)fe->i_blkno, inode->i_ino);
		goto bail;
	}

	BUG_ON(args->fi_blkno != le64_to_cpu(fe->i_blkno));

	status = 0;

bail:
	if (can_lock)
		ocfs2_meta_unlock(inode, 0);

	if (status < 0)
		make_bad_inode(inode);

	if (args && bh)
		brelse(bh);

	mlog_exit(status);
	return status;
}

void ocfs2_sync_blockdev(struct super_block *sb)
{
	sync_blockdev(sb->s_bdev);
}

static int ocfs2_truncate_for_delete(struct ocfs2_super *osb,
				     struct inode *inode,
				     struct buffer_head *fe_bh)
{
	int status = 0;
	handle_t *handle = NULL;
	struct ocfs2_truncate_context *tc = NULL;
	struct ocfs2_dinode *fe;

	mlog_entry_void();

	fe = (struct ocfs2_dinode *) fe_bh->b_data;

	/* zero allocation, zero truncate :) */
	if (!fe->i_clusters)
		goto bail;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_set_inode_size(handle, inode, fe_bh, 0ULL);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_commit_trans(osb, handle);
	handle = NULL;

	status = ocfs2_prepare_truncate(osb, inode, fe_bh, &tc);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_commit_truncate(osb, inode, fe_bh, tc);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
bail:
	if (handle)
		ocfs2_commit_trans(osb, handle);

	mlog_exit(status);
	return status;
}

static int ocfs2_remove_inode(struct inode *inode,
			      struct buffer_head *di_bh,
			      struct inode *orphan_dir_inode,
			      struct buffer_head *orphan_dir_bh)
{
	int status;
	struct inode *inode_alloc_inode = NULL;
	struct buffer_head *inode_alloc_bh = NULL;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *) di_bh->b_data;

	inode_alloc_inode =
		ocfs2_get_system_file_inode(osb, INODE_ALLOC_SYSTEM_INODE,
					    le16_to_cpu(di->i_suballoc_slot));
	if (!inode_alloc_inode) {
		status = -EEXIST;
		mlog_errno(status);
		goto bail;
	}

	mutex_lock(&inode_alloc_inode->i_mutex);
	status = ocfs2_meta_lock(inode_alloc_inode, &inode_alloc_bh, 1);
	if (status < 0) {
		mutex_unlock(&inode_alloc_inode->i_mutex);

		mlog_errno(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb, OCFS2_DELETE_INODE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto bail_unlock;
	}

	status = ocfs2_orphan_del(osb, handle, orphan_dir_inode, inode,
				  orphan_dir_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail_commit;
	}

	/* set the inodes dtime */
	status = ocfs2_journal_access(handle, inode, di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail_commit;
	}

	di->i_dtime = cpu_to_le64(CURRENT_TIME.tv_sec);
	le32_and_cpu(&di->i_flags, ~(OCFS2_VALID_FL | OCFS2_ORPHANED_FL));

	status = ocfs2_journal_dirty(handle, di_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail_commit;
	}

	ocfs2_remove_from_cache(inode, di_bh);

	status = ocfs2_free_dinode(handle, inode_alloc_inode,
				   inode_alloc_bh, di);
	if (status < 0)
		mlog_errno(status);

bail_commit:
	ocfs2_commit_trans(osb, handle);
bail_unlock:
	ocfs2_meta_unlock(inode_alloc_inode, 1);
	mutex_unlock(&inode_alloc_inode->i_mutex);
	brelse(inode_alloc_bh);
bail:
	iput(inode_alloc_inode);

	return status;
}

/* 
 * Serialize with orphan dir recovery. If the process doing
 * recovery on this orphan dir does an iget() with the dir
 * i_mutex held, we'll deadlock here. Instead we detect this
 * and exit early - recovery will wipe this inode for us.
 */
static int ocfs2_check_orphan_recovery_state(struct ocfs2_super *osb,
					     int slot)
{
	int ret = 0;

	spin_lock(&osb->osb_lock);
	if (ocfs2_node_map_test_bit(osb, &osb->osb_recovering_orphan_dirs, slot)) {
		mlog(0, "Recovery is happening on orphan dir %d, will skip "
		     "this inode\n", slot);
		ret = -EDEADLK;
		goto out;
	}
	/* This signals to the orphan recovery process that it should
	 * wait for us to handle the wipe. */
	osb->osb_orphan_wipes[slot]++;
out:
	spin_unlock(&osb->osb_lock);
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

static int ocfs2_wipe_inode(struct inode *inode,
			    struct buffer_head *di_bh)
{
	int status, orphaned_slot;
	struct inode *orphan_dir_inode = NULL;
	struct buffer_head *orphan_dir_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	/* We've already voted on this so it should be readonly - no
	 * spinlock needed. */
	orphaned_slot = OCFS2_I(inode)->ip_orphaned_slot;

	status = ocfs2_check_orphan_recovery_state(osb, orphaned_slot);
	if (status)
		return status;

	orphan_dir_inode = ocfs2_get_system_file_inode(osb,
						       ORPHAN_DIR_SYSTEM_INODE,
						       orphaned_slot);
	if (!orphan_dir_inode) {
		status = -EEXIST;
		mlog_errno(status);
		goto bail;
	}

	/* Lock the orphan dir. The lock will be held for the entire
	 * delete_inode operation. We do this now to avoid races with
	 * recovery completion on other nodes. */
	mutex_lock(&orphan_dir_inode->i_mutex);
	status = ocfs2_meta_lock(orphan_dir_inode, &orphan_dir_bh, 1);
	if (status < 0) {
		mutex_unlock(&orphan_dir_inode->i_mutex);

		mlog_errno(status);
		goto bail;
	}

	/* we do this while holding the orphan dir lock because we
	 * don't want recovery being run from another node to vote for
	 * an inode delete on us -- this will result in two nodes
	 * truncating the same file! */
	status = ocfs2_truncate_for_delete(osb, inode, di_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail_unlock_dir;
	}

	status = ocfs2_remove_inode(inode, di_bh, orphan_dir_inode,
				    orphan_dir_bh);
	if (status < 0)
		mlog_errno(status);

bail_unlock_dir:
	ocfs2_meta_unlock(orphan_dir_inode, 1);
	mutex_unlock(&orphan_dir_inode->i_mutex);
	brelse(orphan_dir_bh);
bail:
	iput(orphan_dir_inode);
	ocfs2_signal_wipe_completion(osb, orphaned_slot);

	return status;
}

/* There is a series of simple checks that should be done before a
 * vote is even considered. Encapsulate those in this function. */
static int ocfs2_inode_is_valid_to_delete(struct inode *inode)
{
	int ret = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	/* We shouldn't be getting here for the root directory
	 * inode.. */
	if (inode == osb->root_inode) {
		mlog(ML_ERROR, "Skipping delete of root inode.\n");
		goto bail;
	}

	/* If we're coming from process_vote we can't go into our own
	 * voting [hello, deadlock city!], so unforuntately we just
	 * have to skip deleting this guy. That's OK though because
	 * the node who's doing the actual deleting should handle it
	 * anyway. */
	if (current == osb->vote_task) {
		mlog(0, "Skipping delete of %lu because we're currently "
		     "in process_vote\n", inode->i_ino);
		goto bail;
	}

	spin_lock(&oi->ip_lock);
	/* OCFS2 *never* deletes system files. This should technically
	 * never get here as system file inodes should always have a
	 * positive link count. */
	if (oi->ip_flags & OCFS2_INODE_SYSTEM_FILE) {
		mlog(ML_ERROR, "Skipping delete of system file %llu\n",
		     (unsigned long long)oi->ip_blkno);
		goto bail_unlock;
	}

	/* If we have voted "yes" on the wipe of this inode for
	 * another node, it will be marked here so we can safely skip
	 * it. Recovery will cleanup any inodes we might inadvertantly
	 * skip here. */
	if (oi->ip_flags & OCFS2_INODE_SKIP_DELETE) {
		mlog(0, "Skipping delete of %lu because another node "
		     "has done this for us.\n", inode->i_ino);
		goto bail_unlock;
	}

	ret = 1;
bail_unlock:
	spin_unlock(&oi->ip_lock);
bail:
	return ret;
}

/* Query the cluster to determine whether we should wipe an inode from
 * disk or not.
 *
 * Requires the inode to have the cluster lock. */
static int ocfs2_query_inode_wipe(struct inode *inode,
				  struct buffer_head *di_bh,
				  int *wipe)
{
	int status = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di;

	*wipe = 0;

	/* While we were waiting for the cluster lock in
	 * ocfs2_delete_inode, another node might have asked to delete
	 * the inode. Recheck our flags to catch this. */
	if (!ocfs2_inode_is_valid_to_delete(inode)) {
		mlog(0, "Skipping delete of %llu because flags changed\n",
		     (unsigned long long)oi->ip_blkno);
		goto bail;
	}

	/* Now that we have an up to date inode, we can double check
	 * the link count. */
	if (inode->i_nlink) {
		mlog(0, "Skipping delete of %llu because nlink = %u\n",
		     (unsigned long long)oi->ip_blkno, inode->i_nlink);
		goto bail;
	}

	/* Do some basic inode verification... */
	di = (struct ocfs2_dinode *) di_bh->b_data;
	if (!(di->i_flags & cpu_to_le32(OCFS2_ORPHANED_FL))) {
		/* for lack of a better error? */
		status = -EEXIST;
		mlog(ML_ERROR,
		     "Inode %llu (on-disk %llu) not orphaned! "
		     "Disk flags  0x%x, inode flags 0x%x\n",
		     (unsigned long long)oi->ip_blkno,
		     (unsigned long long)di->i_blkno, di->i_flags,
		     oi->ip_flags);
		goto bail;
	}

	/* has someone already deleted us?! baaad... */
	if (di->i_dtime) {
		status = -EEXIST;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_request_delete_vote(inode);
	/* -EBUSY means that other nodes are still using the
	 * inode. We're done here though, so avoid doing anything on
	 * disk and let them worry about deleting it. */
	if (status == -EBUSY) {
		status = 0;
		mlog(0, "Skipping delete of %llu because it is in use on"
		     "other nodes\n", (unsigned long long)oi->ip_blkno);
		goto bail;
	}
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	spin_lock(&oi->ip_lock);
	if (oi->ip_orphaned_slot == OCFS2_INVALID_SLOT) {
		/* Nobody knew which slot this inode was orphaned
		 * into. This may happen during node death and
		 * recovery knows how to clean it up so we can safely
		 * ignore this inode for now on. */
		mlog(0, "Nobody knew where inode %llu was orphaned!\n",
		     (unsigned long long)oi->ip_blkno);
	} else {
		*wipe = 1;

		mlog(0, "Inode %llu is ok to wipe from orphan dir %d\n",
		     (unsigned long long)oi->ip_blkno, oi->ip_orphaned_slot);
	}
	spin_unlock(&oi->ip_lock);

bail:
	return status;
}

/* Support function for ocfs2_delete_inode. Will help us keep the
 * inode data in a consistent state for clear_inode. Always truncates
 * pages, optionally sync's them first. */
static void ocfs2_cleanup_delete_inode(struct inode *inode,
				       int sync_data)
{
	mlog(0, "Cleanup inode %llu, sync = %d\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno, sync_data);
	if (sync_data)
		write_inode_now(inode, 1);
	truncate_inode_pages(&inode->i_data, 0);
}

void ocfs2_delete_inode(struct inode *inode)
{
	int wipe, status;
	sigset_t blocked, oldset;
	struct buffer_head *di_bh = NULL;

	mlog_entry("(inode->i_ino = %lu)\n", inode->i_ino);

	if (is_bad_inode(inode)) {
		mlog(0, "Skipping delete of bad inode\n");
		goto bail;
	}

	if (!ocfs2_inode_is_valid_to_delete(inode)) {
		/* It's probably not necessary to truncate_inode_pages
		 * here but we do it for safety anyway (it will most
		 * likely be a no-op anyway) */
		ocfs2_cleanup_delete_inode(inode, 0);
		goto bail;
	}

	/* We want to block signals in delete_inode as the lock and
	 * messaging paths may return us -ERESTARTSYS. Which would
	 * cause us to exit early, resulting in inodes being orphaned
	 * forever. */
	sigfillset(&blocked);
	status = sigprocmask(SIG_BLOCK, &blocked, &oldset);
	if (status < 0) {
		mlog_errno(status);
		ocfs2_cleanup_delete_inode(inode, 1);
		goto bail;
	}

	/* Lock down the inode. This gives us an up to date view of
	 * it's metadata (for verification), and allows us to
	 * serialize delete_inode votes. 
	 *
	 * Even though we might be doing a truncate, we don't take the
	 * allocation lock here as it won't be needed - nobody will
	 * have the file open.
	 */
	status = ocfs2_meta_lock(inode, &di_bh, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		ocfs2_cleanup_delete_inode(inode, 0);
		goto bail_unblock;
	}

	/* Query the cluster. This will be the final decision made
	 * before we go ahead and wipe the inode. */
	status = ocfs2_query_inode_wipe(inode, di_bh, &wipe);
	if (!wipe || status < 0) {
		/* Error and inode busy vote both mean we won't be
		 * removing the inode, so they take almost the same
		 * path. */
		if (status < 0)
			mlog_errno(status);

		/* Someone in the cluster has voted to not wipe this
		 * inode, or it was never completely orphaned. Write
		 * out the pages and exit now. */
		ocfs2_cleanup_delete_inode(inode, 1);
		goto bail_unlock_inode;
	}

	ocfs2_cleanup_delete_inode(inode, 0);

	status = ocfs2_wipe_inode(inode, di_bh);
	if (status < 0) {
		if (status != -EDEADLK)
			mlog_errno(status);
		goto bail_unlock_inode;
	}

	/*
	 * Mark the inode as successfully deleted.
	 *
	 * This is important for ocfs2_clear_inode() as it will check
	 * this flag and skip any checkpointing work
	 *
	 * ocfs2_stuff_meta_lvb() also uses this flag to invalidate
	 * the LVB for other nodes.
	 */
	OCFS2_I(inode)->ip_flags |= OCFS2_INODE_DELETED;

bail_unlock_inode:
	ocfs2_meta_unlock(inode, 1);
	brelse(di_bh);
bail_unblock:
	status = sigprocmask(SIG_SETMASK, &oldset, NULL);
	if (status < 0)
		mlog_errno(status);
bail:
	clear_inode(inode);
	mlog_exit_void();
}

void ocfs2_clear_inode(struct inode *inode)
{
	int status;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	mlog_entry_void();

	if (!inode)
		goto bail;

	mlog(0, "Clearing inode: %llu, nlink = %u\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno, inode->i_nlink);

	mlog_bug_on_msg(OCFS2_SB(inode->i_sb) == NULL,
			"Inode=%lu\n", inode->i_ino);

	/* Do these before all the other work so that we don't bounce
	 * the vote thread while waiting to destroy the locks. */
	ocfs2_mark_lockres_freeing(&oi->ip_rw_lockres);
	ocfs2_mark_lockres_freeing(&oi->ip_meta_lockres);
	ocfs2_mark_lockres_freeing(&oi->ip_data_lockres);

	/* We very well may get a clear_inode before all an inodes
	 * metadata has hit disk. Of course, we can't drop any cluster
	 * locks until the journal has finished with it. The only
	 * exception here are successfully wiped inodes - their
	 * metadata can now be considered to be part of the system
	 * inodes from which it came. */
	if (!(OCFS2_I(inode)->ip_flags & OCFS2_INODE_DELETED))
		ocfs2_checkpoint_inode(inode);

	mlog_bug_on_msg(!list_empty(&oi->ip_io_markers),
			"Clear inode of %llu, inode has io markers\n",
			(unsigned long long)oi->ip_blkno);

	ocfs2_extent_map_drop(inode, 0);
	ocfs2_extent_map_init(inode);

	status = ocfs2_drop_inode_locks(inode);
	if (status < 0)
		mlog_errno(status);

	ocfs2_lock_res_free(&oi->ip_rw_lockres);
	ocfs2_lock_res_free(&oi->ip_meta_lockres);
	ocfs2_lock_res_free(&oi->ip_data_lockres);

	ocfs2_metadata_cache_purge(inode);

	mlog_bug_on_msg(oi->ip_metadata_cache.ci_num_cached,
			"Clear inode of %llu, inode has %u cache items\n",
			(unsigned long long)oi->ip_blkno, oi->ip_metadata_cache.ci_num_cached);

	mlog_bug_on_msg(!(oi->ip_flags & OCFS2_INODE_CACHE_INLINE),
			"Clear inode of %llu, inode has a bad flag\n",
			(unsigned long long)oi->ip_blkno);

	mlog_bug_on_msg(spin_is_locked(&oi->ip_lock),
			"Clear inode of %llu, inode is locked\n",
			(unsigned long long)oi->ip_blkno);

	mlog_bug_on_msg(!mutex_trylock(&oi->ip_io_mutex),
			"Clear inode of %llu, io_mutex is locked\n",
			(unsigned long long)oi->ip_blkno);
	mutex_unlock(&oi->ip_io_mutex);

	/*
	 * down_trylock() returns 0, down_write_trylock() returns 1
	 * kernel 1, world 0
	 */
	mlog_bug_on_msg(!down_write_trylock(&oi->ip_alloc_sem),
			"Clear inode of %llu, alloc_sem is locked\n",
			(unsigned long long)oi->ip_blkno);
	up_write(&oi->ip_alloc_sem);

	mlog_bug_on_msg(oi->ip_open_count,
			"Clear inode of %llu has open count %d\n",
			(unsigned long long)oi->ip_blkno, oi->ip_open_count);

	/* Clear all other flags. */
	oi->ip_flags = OCFS2_INODE_CACHE_INLINE;
	oi->ip_created_trans = 0;
	oi->ip_last_trans = 0;
	oi->ip_dir_start_lookup = 0;
	oi->ip_blkno = 0ULL;

bail:
	mlog_exit_void();
}

/* Called under inode_lock, with no more references on the
 * struct inode, so it's safe here to check the flags field
 * and to manipulate i_nlink without any other locks. */
void ocfs2_drop_inode(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	mlog_entry_void();

	mlog(0, "Drop inode %llu, nlink = %u, ip_flags = 0x%x\n",
	     (unsigned long long)oi->ip_blkno, inode->i_nlink, oi->ip_flags);

	/* Testing ip_orphaned_slot here wouldn't work because we may
	 * not have gotten a delete_inode vote from any other nodes
	 * yet. */
	if (oi->ip_flags & OCFS2_INODE_MAYBE_ORPHANED)
		generic_delete_inode(inode);
	else
		generic_drop_inode(inode);

	mlog_exit_void();
}

/*
 * TODO: this should probably be merged into ocfs2_get_block
 *
 * However, you now need to pay attention to the cont_prepare_write()
 * stuff in ocfs2_get_block (that is, ocfs2_get_block pretty much
 * expects never to extend).
 */
struct buffer_head *ocfs2_bread(struct inode *inode,
				int block, int *err, int reada)
{
	struct buffer_head *bh = NULL;
	int tmperr;
	u64 p_blkno;
	int readflags = OCFS2_BH_CACHED;

	if (reada)
		readflags |= OCFS2_BH_READAHEAD;

	if (((u64)block << inode->i_sb->s_blocksize_bits) >=
	    i_size_read(inode)) {
		BUG_ON(!reada);
		return NULL;
	}

	tmperr = ocfs2_extent_map_get_blocks(inode, block, 1,
					     &p_blkno, NULL);
	if (tmperr < 0) {
		mlog_errno(tmperr);
		goto fail;
	}

	tmperr = ocfs2_read_block(OCFS2_SB(inode->i_sb), p_blkno, &bh,
				  readflags, inode);
	if (tmperr < 0)
		goto fail;

	tmperr = 0;

	*err = 0;
	return bh;

fail:
	if (bh) {
		brelse(bh);
		bh = NULL;
	}
	*err = -EIO;
	return NULL;
}

/*
 * This is called from our getattr.
 */
int ocfs2_inode_revalidate(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int status = 0;

	mlog_entry("(inode = 0x%p, ino = %llu)\n", inode,
		   inode ? (unsigned long long)OCFS2_I(inode)->ip_blkno : 0ULL);

	if (!inode) {
		mlog(0, "eep, no inode!\n");
		status = -ENOENT;
		goto bail;
	}

	spin_lock(&OCFS2_I(inode)->ip_lock);
	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_DELETED) {
		spin_unlock(&OCFS2_I(inode)->ip_lock);
		mlog(0, "inode deleted!\n");
		status = -ENOENT;
		goto bail;
	}
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	/* Let ocfs2_meta_lock do the work of updating our struct
	 * inode for us. */
	status = ocfs2_meta_lock(inode, NULL, 0);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto bail;
	}
	ocfs2_meta_unlock(inode, 0);
bail:
	mlog_exit(status);

	return status;
}

/*
 * Updates a disk inode from a
 * struct inode.
 * Only takes ip_lock.
 */
int ocfs2_mark_inode_dirty(handle_t *handle,
			   struct inode *inode,
			   struct buffer_head *bh)
{
	int status;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) bh->b_data;

	mlog_entry("(inode %llu)\n",
		   (unsigned long long)OCFS2_I(inode)->ip_blkno);

	status = ocfs2_journal_access(handle, inode, bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	spin_lock(&OCFS2_I(inode)->ip_lock);
	fe->i_clusters = cpu_to_le32(OCFS2_I(inode)->ip_clusters);
	fe->i_attr = cpu_to_le32(OCFS2_I(inode)->ip_attr);
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	fe->i_size = cpu_to_le64(i_size_read(inode));
	fe->i_links_count = cpu_to_le16(inode->i_nlink);
	fe->i_uid = cpu_to_le32(inode->i_uid);
	fe->i_gid = cpu_to_le32(inode->i_gid);
	fe->i_mode = cpu_to_le16(inode->i_mode);
	fe->i_atime = cpu_to_le64(inode->i_atime.tv_sec);
	fe->i_atime_nsec = cpu_to_le32(inode->i_atime.tv_nsec);
	fe->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
	fe->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);
	fe->i_mtime = cpu_to_le64(inode->i_mtime.tv_sec);
	fe->i_mtime_nsec = cpu_to_le32(inode->i_mtime.tv_nsec);

	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0)
		mlog_errno(status);

	status = 0;
leave:

	mlog_exit(status);
	return status;
}

/*
 *
 * Updates a struct inode from a disk inode.
 * does no i/o, only takes ip_lock.
 */
void ocfs2_refresh_inode(struct inode *inode,
			 struct ocfs2_dinode *fe)
{
	spin_lock(&OCFS2_I(inode)->ip_lock);

	OCFS2_I(inode)->ip_clusters = le32_to_cpu(fe->i_clusters);
	OCFS2_I(inode)->ip_attr = le32_to_cpu(fe->i_attr);
	ocfs2_set_inode_flags(inode);
	i_size_write(inode, le64_to_cpu(fe->i_size));
	inode->i_nlink = le16_to_cpu(fe->i_links_count);
	inode->i_uid = le32_to_cpu(fe->i_uid);
	inode->i_gid = le32_to_cpu(fe->i_gid);
	inode->i_mode = le16_to_cpu(fe->i_mode);
	if (S_ISLNK(inode->i_mode) && le32_to_cpu(fe->i_clusters) == 0)
		inode->i_blocks = 0;
	else
		inode->i_blocks = ocfs2_align_bytes_to_sectors(i_size_read(inode));
	inode->i_atime.tv_sec = le64_to_cpu(fe->i_atime);
	inode->i_atime.tv_nsec = le32_to_cpu(fe->i_atime_nsec);
	inode->i_mtime.tv_sec = le64_to_cpu(fe->i_mtime);
	inode->i_mtime.tv_nsec = le32_to_cpu(fe->i_mtime_nsec);
	inode->i_ctime.tv_sec = le64_to_cpu(fe->i_ctime);
	inode->i_ctime.tv_nsec = le32_to_cpu(fe->i_ctime_nsec);

	spin_unlock(&OCFS2_I(inode)->ip_lock);
}
