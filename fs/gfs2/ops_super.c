/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/vmalloc.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "glock.h"
#include "inode.h"
#include "lm.h"
#include "log.h"
#include "mount.h"
#include "ops_super.h"
#include "page.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "sys.h"

/**
 * gfs2_write_inode - Make sure the inode is stable on the disk
 * @inode: The inode
 * @sync: synchronous write flag
 *
 * Returns: errno
 */

static int gfs2_write_inode(struct inode *inode, int sync)
{
	struct gfs2_inode *ip = get_v2ip(inode);

	atomic_inc(&ip->i_sbd->sd_ops_super);

	if (current->flags & PF_MEMALLOC)
		return 0;
	if (ip && sync)
		gfs2_log_flush_glock(ip->i_gl);

	return 0;
}

/**
 * gfs2_put_super - Unmount the filesystem
 * @sb: The VFS superblock
 *
 */

static void gfs2_put_super(struct super_block *sb)
{
	struct gfs2_sbd *sdp = get_v2sdp(sb);
	int error;

	if (!sdp)
		return;

	atomic_inc(&sdp->sd_ops_super);

	/*  Unfreeze the filesystem, if we need to  */

	down(&sdp->sd_freeze_lock);
	if (sdp->sd_freeze_count)
		gfs2_glock_dq_uninit(&sdp->sd_freeze_gh);
	up(&sdp->sd_freeze_lock);

	kthread_stop(sdp->sd_inoded_process);
	kthread_stop(sdp->sd_quotad_process);
	kthread_stop(sdp->sd_logd_process);
	kthread_stop(sdp->sd_recoverd_process);
	while (sdp->sd_glockd_num--)
		kthread_stop(sdp->sd_glockd_process[sdp->sd_glockd_num]);
	kthread_stop(sdp->sd_scand_process);

	if (!(sb->s_flags & MS_RDONLY)) {
		error = gfs2_make_fs_ro(sdp);
		if (error)
			gfs2_io_error(sdp);
	}

	/*  At this point, we're through modifying the disk  */

	/*  Release stuff  */

	iput(sdp->sd_master_dir);
	iput(sdp->sd_jindex);
	iput(sdp->sd_inum_inode);
	iput(sdp->sd_statfs_inode);
	iput(sdp->sd_rindex);
	iput(sdp->sd_quota_inode);
	iput(sdp->sd_root_dir);

	gfs2_glock_put(sdp->sd_rename_gl);
	gfs2_glock_put(sdp->sd_trans_gl);

	if (!sdp->sd_args.ar_spectator) {
		gfs2_glock_dq_uninit(&sdp->sd_journal_gh);
		gfs2_glock_dq_uninit(&sdp->sd_jinode_gh);
		gfs2_glock_dq_uninit(&sdp->sd_ir_gh);
		gfs2_glock_dq_uninit(&sdp->sd_sc_gh);
		gfs2_glock_dq_uninit(&sdp->sd_ut_gh);
		gfs2_glock_dq_uninit(&sdp->sd_qc_gh);
		iput(sdp->sd_ir_inode);
		iput(sdp->sd_sc_inode);
		iput(sdp->sd_ut_inode);
		iput(sdp->sd_qc_inode);
	}

	gfs2_glock_dq_uninit(&sdp->sd_live_gh);

	gfs2_clear_rgrpd(sdp);
	gfs2_jindex_free(sdp);

	/*  Take apart glock structures and buffer lists  */
	gfs2_gl_hash_clear(sdp, WAIT);

	/*  Unmount the locking protocol  */
	gfs2_lm_unmount(sdp);

	/*  At this point, we're through participating in the lockspace  */

	gfs2_sys_fs_del(sdp);

	/*  Get rid of any extra inodes  */
	while (invalidate_inodes(sb))
		yield();

	vfree(sdp);

	set_v2sdp(sb, NULL);
}

/**
 * gfs2_write_super - disk commit all incore transactions
 * @sb: the filesystem
 *
 * This function is called every time sync(2) is called.
 * After this exits, all dirty buffers and synced.
 */

static void gfs2_write_super(struct super_block *sb)
{
	struct gfs2_sbd *sdp = get_v2sdp(sb);
	atomic_inc(&sdp->sd_ops_super);
	gfs2_log_flush(sdp);
}

/**
 * gfs2_write_super_lockfs - prevent further writes to the filesystem
 * @sb: the VFS structure for the filesystem
 *
 */

static void gfs2_write_super_lockfs(struct super_block *sb)
{
	struct gfs2_sbd *sdp = get_v2sdp(sb);
	int error;

	atomic_inc(&sdp->sd_ops_super);

	for (;;) {
		error = gfs2_freeze_fs(sdp);
		if (!error)
			break;

		switch (error) {
		case -EBUSY:
			fs_err(sdp, "waiting for recovery before freeze\n");
			break;

		default:
			fs_err(sdp, "error freezing FS: %d\n", error);
			break;
		}

		fs_err(sdp, "retrying...\n");
		msleep(1000);
	}
}

/**
 * gfs2_unlockfs - reallow writes to the filesystem
 * @sb: the VFS structure for the filesystem
 *
 */

static void gfs2_unlockfs(struct super_block *sb)
{
	struct gfs2_sbd *sdp = get_v2sdp(sb);

	atomic_inc(&sdp->sd_ops_super);
	gfs2_unfreeze_fs(sdp);
}

/**
 * gfs2_statfs - Gather and return stats about the filesystem
 * @sb: The superblock
 * @statfsbuf: The buffer
 *
 * Returns: 0 on success or error code
 */

static int gfs2_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct gfs2_sbd *sdp = get_v2sdp(sb);
	struct gfs2_statfs_change sc;
	int error;

	atomic_inc(&sdp->sd_ops_super);

	if (gfs2_tune_get(sdp, gt_statfs_slow))
		error = gfs2_statfs_slow(sdp, &sc);
	else
		error = gfs2_statfs_i(sdp, &sc);

	if (error)
		return error;

	memset(buf, 0, sizeof(struct kstatfs));

	buf->f_type = GFS2_MAGIC;
	buf->f_bsize = sdp->sd_sb.sb_bsize;
	buf->f_blocks = sc.sc_total;
	buf->f_bfree = sc.sc_free;
	buf->f_bavail = sc.sc_free;
	buf->f_files = sc.sc_dinodes + sc.sc_free;
	buf->f_ffree = sc.sc_free;
	buf->f_namelen = GFS2_FNAMESIZE;

	return 0;
}

/**
 * gfs2_remount_fs - called when the FS is remounted
 * @sb:  the filesystem
 * @flags:  the remount flags
 * @data:  extra data passed in (not used right now)
 *
 * Returns: errno
 */

static int gfs2_remount_fs(struct super_block *sb, int *flags, char *data)
{
	struct gfs2_sbd *sdp = get_v2sdp(sb);
	int error;

	atomic_inc(&sdp->sd_ops_super);

	error = gfs2_mount_args(sdp, data, 1);
	if (error)
		return error;

	if (sdp->sd_args.ar_spectator)
		*flags |= MS_RDONLY;
	else {
		if (*flags & MS_RDONLY) {
			if (!(sb->s_flags & MS_RDONLY))
				error = gfs2_make_fs_ro(sdp);
		} else if (!(*flags & MS_RDONLY) &&
			   (sb->s_flags & MS_RDONLY)) {
			error = gfs2_make_fs_rw(sdp);
		}
	}

	if (*flags & (MS_NOATIME | MS_NODIRATIME))
		set_bit(SDF_NOATIME, &sdp->sd_flags);
	else
		clear_bit(SDF_NOATIME, &sdp->sd_flags);

	/* Don't let the VFS update atimes.  GFS2 handles this itself. */
	*flags |= MS_NOATIME | MS_NODIRATIME;

	return error;
}

/**
 * gfs2_clear_inode - Deallocate an inode when VFS is done with it
 * @inode: The VFS inode
 *
 */

static void gfs2_clear_inode(struct inode *inode)
{
	struct gfs2_inode *ip = get_v2ip(inode);

	atomic_inc(&get_v2sdp(inode->i_sb)->sd_ops_super);

	if (ip) {
		spin_lock(&ip->i_spin);
		ip->i_vnode = NULL;
		set_v2ip(inode, NULL);
		spin_unlock(&ip->i_spin);

		gfs2_glock_schedule_for_reclaim(ip->i_gl);
		gfs2_inode_put(ip);
	}
}

/**
 * gfs2_show_options - Show mount options for /proc/mounts
 * @s: seq_file structure
 * @mnt: vfsmount
 *
 * Returns: 0 on success or error code
 */

static int gfs2_show_options(struct seq_file *s, struct vfsmount *mnt)
{
	struct gfs2_sbd *sdp = get_v2sdp(mnt->mnt_sb);
	struct gfs2_args *args = &sdp->sd_args;

	atomic_inc(&sdp->sd_ops_super);

	if (args->ar_lockproto[0])
		seq_printf(s, ",lockproto=%s", args->ar_lockproto);
	if (args->ar_locktable[0])
		seq_printf(s, ",locktable=%s", args->ar_locktable);
	if (args->ar_hostdata[0])
		seq_printf(s, ",hostdata=%s", args->ar_hostdata);
	if (args->ar_spectator)
		seq_printf(s, ",spectator");
	if (args->ar_ignore_local_fs)
		seq_printf(s, ",ignore_local_fs");
	if (args->ar_localflocks)
		seq_printf(s, ",localflocks");
	if (args->ar_localcaching)
		seq_printf(s, ",localcaching");
	if (args->ar_debug)
		seq_printf(s, ",debug");
	if (args->ar_upgrade)
		seq_printf(s, ",upgrade");
	if (args->ar_num_glockd != GFS2_GLOCKD_DEFAULT)
		seq_printf(s, ",num_glockd=%u", args->ar_num_glockd);
	if (args->ar_posix_acl)
		seq_printf(s, ",acl");
	if (args->ar_quota != GFS2_QUOTA_DEFAULT) {
		char *state;
		switch (args->ar_quota) {
		case GFS2_QUOTA_OFF:
			state = "off";
			break;
		case GFS2_QUOTA_ACCOUNT:
			state = "account";
			break;
		case GFS2_QUOTA_ON:
			state = "on";
			break;
		default:
			state = "unknown";
			break;
		}
		seq_printf(s, ",quota=%s", state);
	}
	if (args->ar_suiddir)
		seq_printf(s, ",suiddir");
	if (args->ar_data != GFS2_DATA_DEFAULT) {
		char *state;
		switch (args->ar_data) {
		case GFS2_DATA_WRITEBACK:
			state = "writeback";
			break;
		case GFS2_DATA_ORDERED:
			state = "ordered";
			break;
		default:
			state = "unknown";
			break;
		}
		seq_printf(s, ",data=%s", state);
	}

	return 0;
}

struct super_operations gfs2_super_ops = {
	.write_inode = gfs2_write_inode,
	.put_super = gfs2_put_super,
	.write_super = gfs2_write_super,
	.write_super_lockfs = gfs2_write_super_lockfs,
	.unlockfs = gfs2_unlockfs,
	.statfs = gfs2_statfs,
	.remount_fs = gfs2_remount_fs,
	.clear_inode = gfs2_clear_inode,
	.show_options = gfs2_show_options,
};

