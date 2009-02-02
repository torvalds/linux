/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/lm_interface.h>
#include <linux/time.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "inode.h"
#include "log.h"
#include "mount.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "sys.h"
#include "util.h"
#include "trans.h"
#include "dir.h"
#include "eattr.h"
#include "bmap.h"
#include "meta_io.h"

/**
 * gfs2_write_inode - Make sure the inode is stable on the disk
 * @inode: The inode
 * @sync: synchronous write flag
 *
 * Returns: errno
 */

static int gfs2_write_inode(struct inode *inode, int sync)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_holder gh;
	struct buffer_head *bh;
	struct timespec atime;
	struct gfs2_dinode *di;
	int ret = 0;

	/* Check this is a "normal" inode, etc */
	if (!test_bit(GIF_USER, &ip->i_flags) ||
	    (current->flags & PF_MEMALLOC))
		return 0;
	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (ret)
		goto do_flush;
	ret = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (ret)
		goto do_unlock;
	ret = gfs2_meta_inode_buffer(ip, &bh);
	if (ret == 0) {
		di = (struct gfs2_dinode *)bh->b_data;
		atime.tv_sec = be64_to_cpu(di->di_atime);
		atime.tv_nsec = be32_to_cpu(di->di_atime_nsec);
		if (timespec_compare(&inode->i_atime, &atime) > 0) {
			gfs2_trans_add_bh(ip->i_gl, bh, 1);
			gfs2_dinode_out(ip, bh->b_data);
		}
		brelse(bh);
	}
	gfs2_trans_end(sdp);
do_unlock:
	gfs2_glock_dq_uninit(&gh);
do_flush:
	if (sync != 0)
		gfs2_log_flush(GFS2_SB(inode), ip->i_gl);
	return ret;
}

/**
 * gfs2_make_fs_ro - Turn a Read-Write FS into a Read-Only one
 * @sdp: the filesystem
 *
 * Returns: errno
 */

static int gfs2_make_fs_ro(struct gfs2_sbd *sdp)
{
	struct gfs2_holder t_gh;
	int error;

	gfs2_quota_sync(sdp);
	gfs2_statfs_sync(sdp);

	error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_SHARED, GL_NOCACHE,
				   &t_gh);
	if (error && !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return error;

	gfs2_meta_syncfs(sdp);
	gfs2_log_shutdown(sdp);

	clear_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);

	if (t_gh.gh_gl)
		gfs2_glock_dq_uninit(&t_gh);

	gfs2_quota_cleanup(sdp);

	return error;
}

/**
 * gfs2_put_super - Unmount the filesystem
 * @sb: The VFS superblock
 *
 */

static void gfs2_put_super(struct super_block *sb)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	int error;

	/*  Unfreeze the filesystem, if we need to  */

	mutex_lock(&sdp->sd_freeze_lock);
	if (sdp->sd_freeze_count)
		gfs2_glock_dq_uninit(&sdp->sd_freeze_gh);
	mutex_unlock(&sdp->sd_freeze_lock);

	kthread_stop(sdp->sd_quotad_process);
	kthread_stop(sdp->sd_logd_process);
	kthread_stop(sdp->sd_recoverd_process);

	if (!(sb->s_flags & MS_RDONLY)) {
		error = gfs2_make_fs_ro(sdp);
		if (error)
			gfs2_io_error(sdp);
	}
	/*  At this point, we're through modifying the disk  */

	/*  Release stuff  */

	iput(sdp->sd_jindex);
	iput(sdp->sd_inum_inode);
	iput(sdp->sd_statfs_inode);
	iput(sdp->sd_rindex);
	iput(sdp->sd_quota_inode);

	gfs2_glock_put(sdp->sd_rename_gl);
	gfs2_glock_put(sdp->sd_trans_gl);

	if (!sdp->sd_args.ar_spectator) {
		gfs2_glock_dq_uninit(&sdp->sd_journal_gh);
		gfs2_glock_dq_uninit(&sdp->sd_jinode_gh);
		gfs2_glock_dq_uninit(&sdp->sd_ir_gh);
		gfs2_glock_dq_uninit(&sdp->sd_sc_gh);
		gfs2_glock_dq_uninit(&sdp->sd_qc_gh);
		iput(sdp->sd_ir_inode);
		iput(sdp->sd_sc_inode);
		iput(sdp->sd_qc_inode);
	}

	gfs2_glock_dq_uninit(&sdp->sd_live_gh);
	gfs2_clear_rgrpd(sdp);
	gfs2_jindex_free(sdp);
	/*  Take apart glock structures and buffer lists  */
	gfs2_gl_hash_clear(sdp);
	/*  Unmount the locking protocol  */
	gfs2_lm_unmount(sdp);

	/*  At this point, we're through participating in the lockspace  */
	gfs2_sys_fs_del(sdp);
}

/**
 * gfs2_write_super
 * @sb: the superblock
 *
 */

static void gfs2_write_super(struct super_block *sb)
{
	sb->s_dirt = 0;
}

/**
 * gfs2_sync_fs - sync the filesystem
 * @sb: the superblock
 *
 * Flushes the log to disk.
 */

static int gfs2_sync_fs(struct super_block *sb, int wait)
{
	sb->s_dirt = 0;
	if (wait && sb->s_fs_info)
		gfs2_log_flush(sb->s_fs_info, NULL);
	return 0;
}

/**
 * gfs2_freeze - prevent further writes to the filesystem
 * @sb: the VFS structure for the filesystem
 *
 */

static int gfs2_freeze(struct super_block *sb)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	int error;

	if (test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return -EINVAL;

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
	return 0;
}

/**
 * gfs2_unfreeze - reallow writes to the filesystem
 * @sb: the VFS structure for the filesystem
 *
 */

static int gfs2_unfreeze(struct super_block *sb)
{
	gfs2_unfreeze_fs(sb->s_fs_info);
	return 0;
}

/**
 * statfs_fill - fill in the sg for a given RG
 * @rgd: the RG
 * @sc: the sc structure
 *
 * Returns: 0 on success, -ESTALE if the LVB is invalid
 */

static int statfs_slow_fill(struct gfs2_rgrpd *rgd,
			    struct gfs2_statfs_change_host *sc)
{
	gfs2_rgrp_verify(rgd);
	sc->sc_total += rgd->rd_data;
	sc->sc_free += rgd->rd_free;
	sc->sc_dinodes += rgd->rd_dinodes;
	return 0;
}

/**
 * gfs2_statfs_slow - Stat a filesystem using asynchronous locking
 * @sdp: the filesystem
 * @sc: the sc info that will be returned
 *
 * Any error (other than a signal) will cause this routine to fall back
 * to the synchronous version.
 *
 * FIXME: This really shouldn't busy wait like this.
 *
 * Returns: errno
 */

static int gfs2_statfs_slow(struct gfs2_sbd *sdp, struct gfs2_statfs_change_host *sc)
{
	struct gfs2_holder ri_gh;
	struct gfs2_rgrpd *rgd_next;
	struct gfs2_holder *gha, *gh;
	unsigned int slots = 64;
	unsigned int x;
	int done;
	int error = 0, err;

	memset(sc, 0, sizeof(struct gfs2_statfs_change_host));
	gha = kcalloc(slots, sizeof(struct gfs2_holder), GFP_KERNEL);
	if (!gha)
		return -ENOMEM;

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		goto out;

	rgd_next = gfs2_rgrpd_get_first(sdp);

	for (;;) {
		done = 1;

		for (x = 0; x < slots; x++) {
			gh = gha + x;

			if (gh->gh_gl && gfs2_glock_poll(gh)) {
				err = gfs2_glock_wait(gh);
				if (err) {
					gfs2_holder_uninit(gh);
					error = err;
				} else {
					if (!error)
						error = statfs_slow_fill(
							gh->gh_gl->gl_object, sc);
					gfs2_glock_dq_uninit(gh);
				}
			}

			if (gh->gh_gl)
				done = 0;
			else if (rgd_next && !error) {
				error = gfs2_glock_nq_init(rgd_next->rd_gl,
							   LM_ST_SHARED,
							   GL_ASYNC,
							   gh);
				rgd_next = gfs2_rgrpd_get_next(rgd_next);
				done = 0;
			}

			if (signal_pending(current))
				error = -ERESTARTSYS;
		}

		if (done)
			break;

		yield();
	}

	gfs2_glock_dq_uninit(&ri_gh);

out:
	kfree(gha);
	return error;
}

/**
 * gfs2_statfs_i - Do a statfs
 * @sdp: the filesystem
 * @sg: the sg structure
 *
 * Returns: errno
 */

static int gfs2_statfs_i(struct gfs2_sbd *sdp, struct gfs2_statfs_change_host *sc)
{
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;

	spin_lock(&sdp->sd_statfs_spin);

	*sc = *m_sc;
	sc->sc_total += l_sc->sc_total;
	sc->sc_free += l_sc->sc_free;
	sc->sc_dinodes += l_sc->sc_dinodes;

	spin_unlock(&sdp->sd_statfs_spin);

	if (sc->sc_free < 0)
		sc->sc_free = 0;
	if (sc->sc_free > sc->sc_total)
		sc->sc_free = sc->sc_total;
	if (sc->sc_dinodes < 0)
		sc->sc_dinodes = 0;

	return 0;
}

/**
 * gfs2_statfs - Gather and return stats about the filesystem
 * @sb: The superblock
 * @statfsbuf: The buffer
 *
 * Returns: 0 on success or error code
 */

static int gfs2_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_inode->i_sb;
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_statfs_change_host sc;
	int error;

	if (gfs2_tune_get(sdp, gt_statfs_slow))
		error = gfs2_statfs_slow(sdp, &sc);
	else
		error = gfs2_statfs_i(sdp, &sc);

	if (error)
		return error;

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
	struct gfs2_sbd *sdp = sb->s_fs_info;
	int error;

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

	return error;
}

/**
 * gfs2_drop_inode - Drop an inode (test for remote unlink)
 * @inode: The inode to drop
 *
 * If we've received a callback on an iopen lock then its because a
 * remote node tried to deallocate the inode but failed due to this node
 * still having the inode open. Here we mark the link count zero
 * since we know that it must have reached zero if the GLF_DEMOTE flag
 * is set on the iopen glock. If we didn't do a disk read since the
 * remote node removed the final link then we might otherwise miss
 * this event. This check ensures that this node will deallocate the
 * inode's blocks, or alternatively pass the baton on to another
 * node for later deallocation.
 */

static void gfs2_drop_inode(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);

	if (test_bit(GIF_USER, &ip->i_flags) && inode->i_nlink) {
		struct gfs2_glock *gl = ip->i_iopen_gh.gh_gl;
		if (gl && test_bit(GLF_DEMOTE, &gl->gl_flags))
			clear_nlink(inode);
	}
	generic_drop_inode(inode);
}

/**
 * gfs2_clear_inode - Deallocate an inode when VFS is done with it
 * @inode: The VFS inode
 *
 */

static void gfs2_clear_inode(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);

	/* This tells us its a "real" inode and not one which only
	 * serves to contain an address space (see rgrp.c, meta_io.c)
	 * which therefore doesn't have its own glocks.
	 */
	if (test_bit(GIF_USER, &ip->i_flags)) {
		ip->i_gl->gl_object = NULL;
		gfs2_glock_put(ip->i_gl);
		ip->i_gl = NULL;
		if (ip->i_iopen_gh.gh_gl) {
			ip->i_iopen_gh.gh_gl->gl_object = NULL;
			gfs2_glock_dq_uninit(&ip->i_iopen_gh);
		}
	}
}

static int is_ancestor(const struct dentry *d1, const struct dentry *d2)
{
	do {
		if (d1 == d2)
			return 1;
		d1 = d1->d_parent;
	} while (!IS_ROOT(d1));
	return 0;
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
	struct gfs2_sbd *sdp = mnt->mnt_sb->s_fs_info;
	struct gfs2_args *args = &sdp->sd_args;

	if (is_ancestor(mnt->mnt_root, sdp->sd_master_dir))
		seq_printf(s, ",meta");
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

/*
 * We have to (at the moment) hold the inodes main lock to cover
 * the gap between unlocking the shared lock on the iopen lock and
 * taking the exclusive lock. I'd rather do a shared -> exclusive
 * conversion on the iopen lock, but we can change that later. This
 * is safe, just less efficient.
 */

static void gfs2_delete_inode(struct inode *inode)
{
	struct gfs2_sbd *sdp = inode->i_sb->s_fs_info;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	int error;

	if (!test_bit(GIF_USER, &ip->i_flags))
		goto out;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (unlikely(error)) {
		gfs2_glock_dq_uninit(&ip->i_iopen_gh);
		goto out;
	}

	gfs2_glock_dq_wait(&ip->i_iopen_gh);
	gfs2_holder_reinit(LM_ST_EXCLUSIVE, LM_FLAG_TRY_1CB | GL_NOCACHE, &ip->i_iopen_gh);
	error = gfs2_glock_nq(&ip->i_iopen_gh);
	if (error)
		goto out_truncate;

	if (S_ISDIR(inode->i_mode) &&
	    (ip->i_diskflags & GFS2_DIF_EXHASH)) {
		error = gfs2_dir_exhash_dealloc(ip);
		if (error)
			goto out_unlock;
	}

	if (ip->i_eattr) {
		error = gfs2_ea_dealloc(ip);
		if (error)
			goto out_unlock;
	}

	if (!gfs2_is_stuffed(ip)) {
		error = gfs2_file_dealloc(ip);
		if (error)
			goto out_unlock;
	}

	error = gfs2_dinode_dealloc(ip);
	if (error)
		goto out_unlock;

out_truncate:
	error = gfs2_trans_begin(sdp, 0, sdp->sd_jdesc->jd_blocks);
	if (error)
		goto out_unlock;
	/* Needs to be done before glock release & also in a transaction */
	truncate_inode_pages(&inode->i_data, 0);
	gfs2_trans_end(sdp);

out_unlock:
	if (test_bit(HIF_HOLDER, &ip->i_iopen_gh.gh_iflags))
		gfs2_glock_dq(&ip->i_iopen_gh);
	gfs2_holder_uninit(&ip->i_iopen_gh);
	gfs2_glock_dq_uninit(&gh);
	if (error && error != GLR_TRYFAILED)
		fs_warn(sdp, "gfs2_delete_inode: %d\n", error);
out:
	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
}

static struct inode *gfs2_alloc_inode(struct super_block *sb)
{
	struct gfs2_inode *ip;

	ip = kmem_cache_alloc(gfs2_inode_cachep, GFP_KERNEL);
	if (ip) {
		ip->i_flags = 0;
		ip->i_gl = NULL;
	}
	return &ip->i_inode;
}

static void gfs2_destroy_inode(struct inode *inode)
{
	kmem_cache_free(gfs2_inode_cachep, inode);
}

const struct super_operations gfs2_super_ops = {
	.alloc_inode		= gfs2_alloc_inode,
	.destroy_inode		= gfs2_destroy_inode,
	.write_inode		= gfs2_write_inode,
	.delete_inode		= gfs2_delete_inode,
	.put_super		= gfs2_put_super,
	.write_super		= gfs2_write_super,
	.sync_fs		= gfs2_sync_fs,
	.freeze_fs 		= gfs2_freeze,
	.unfreeze_fs		= gfs2_unfreeze,
	.statfs			= gfs2_statfs,
	.remount_fs		= gfs2_remount_fs,
	.clear_inode		= gfs2_clear_inode,
	.drop_inode		= gfs2_drop_inode,
	.show_options		= gfs2_show_options,
};

