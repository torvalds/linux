// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bio.h>
#include <linux/sched/signal.h>
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
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/kernel.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "dir.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"
#include "util.h"
#include "sys.h"
#include "xattr.h"
#include "lops.h"

enum dinode_demise {
	SHOULD_DELETE_DINODE,
	SHOULD_NOT_DELETE_DINODE,
	SHOULD_DEFER_EVICTION,
};

/**
 * gfs2_jindex_free - Clear all the journal index information
 * @sdp: The GFS2 superblock
 *
 */

void gfs2_jindex_free(struct gfs2_sbd *sdp)
{
	struct list_head list;
	struct gfs2_jdesc *jd;

	spin_lock(&sdp->sd_jindex_spin);
	list_add(&list, &sdp->sd_jindex_list);
	list_del_init(&sdp->sd_jindex_list);
	sdp->sd_journals = 0;
	spin_unlock(&sdp->sd_jindex_spin);

	sdp->sd_jdesc = NULL;
	while (!list_empty(&list)) {
		jd = list_first_entry(&list, struct gfs2_jdesc, jd_list);
		gfs2_free_journal_extents(jd);
		list_del(&jd->jd_list);
		iput(jd->jd_inode);
		jd->jd_inode = NULL;
		kfree(jd);
	}
}

static struct gfs2_jdesc *jdesc_find_i(struct list_head *head, unsigned int jid)
{
	struct gfs2_jdesc *jd;

	list_for_each_entry(jd, head, jd_list) {
		if (jd->jd_jid == jid)
			return jd;
	}
	return NULL;
}

struct gfs2_jdesc *gfs2_jdesc_find(struct gfs2_sbd *sdp, unsigned int jid)
{
	struct gfs2_jdesc *jd;

	spin_lock(&sdp->sd_jindex_spin);
	jd = jdesc_find_i(&sdp->sd_jindex_list, jid);
	spin_unlock(&sdp->sd_jindex_spin);

	return jd;
}

int gfs2_jdesc_check(struct gfs2_jdesc *jd)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	u64 size = i_size_read(jd->jd_inode);

	if (gfs2_check_internal_file_size(jd->jd_inode, 8 << 20, BIT(30)))
		return -EIO;

	jd->jd_blocks = size >> sdp->sd_sb.sb_bsize_shift;

	if (gfs2_write_alloc_required(ip, 0, size)) {
		gfs2_consist_inode(ip);
		return -EIO;
	}

	return 0;
}

/**
 * gfs2_make_fs_rw - Turn a Read-Only FS into a Read-Write one
 * @sdp: the filesystem
 *
 * Returns: errno
 */

int gfs2_make_fs_rw(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_jdesc->jd_inode);
	struct gfs2_glock *j_gl = ip->i_gl;
	struct gfs2_log_header_host head;
	int error;

	j_gl->gl_ops->go_inval(j_gl, DIO_METADATA);
	if (gfs2_withdrawn(sdp))
		return -EIO;

	error = gfs2_find_jhead(sdp->sd_jdesc, &head, false);
	if (error || gfs2_withdrawn(sdp))
		return error;

	if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
		gfs2_consist(sdp);
		return -EIO;
	}

	/*  Initialize some head of the log stuff  */
	sdp->sd_log_sequence = head.lh_sequence + 1;
	gfs2_log_pointers_init(sdp, head.lh_blkno);

	error = gfs2_quota_init(sdp);
	if (!error && !gfs2_withdrawn(sdp))
		set_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);
	return error;
}

void gfs2_statfs_change_in(struct gfs2_statfs_change_host *sc, const void *buf)
{
	const struct gfs2_statfs_change *str = buf;

	sc->sc_total = be64_to_cpu(str->sc_total);
	sc->sc_free = be64_to_cpu(str->sc_free);
	sc->sc_dinodes = be64_to_cpu(str->sc_dinodes);
}

void gfs2_statfs_change_out(const struct gfs2_statfs_change_host *sc, void *buf)
{
	struct gfs2_statfs_change *str = buf;

	str->sc_total = cpu_to_be64(sc->sc_total);
	str->sc_free = cpu_to_be64(sc->sc_free);
	str->sc_dinodes = cpu_to_be64(sc->sc_dinodes);
}

int gfs2_statfs_init(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct buffer_head *m_bh;
	struct gfs2_holder gh;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, GL_NOCACHE,
				   &gh);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(m_ip, &m_bh);
	if (error)
		goto out;

	if (sdp->sd_args.ar_spectator) {
		spin_lock(&sdp->sd_statfs_spin);
		gfs2_statfs_change_in(m_sc, m_bh->b_data +
				      sizeof(struct gfs2_dinode));
		spin_unlock(&sdp->sd_statfs_spin);
	} else {
		spin_lock(&sdp->sd_statfs_spin);
		gfs2_statfs_change_in(m_sc, m_bh->b_data +
				      sizeof(struct gfs2_dinode));
		gfs2_statfs_change_in(l_sc, sdp->sd_sc_bh->b_data +
				      sizeof(struct gfs2_dinode));
		spin_unlock(&sdp->sd_statfs_spin);

	}

	brelse(m_bh);
out:
	gfs2_glock_dq_uninit(&gh);
	return 0;
}

void gfs2_statfs_change(struct gfs2_sbd *sdp, s64 total, s64 free,
			s64 dinodes)
{
	struct gfs2_inode *l_ip = GFS2_I(sdp->sd_sc_inode);
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	s64 x, y;
	int need_sync = 0;

	gfs2_trans_add_meta(l_ip->i_gl, sdp->sd_sc_bh);

	spin_lock(&sdp->sd_statfs_spin);
	l_sc->sc_total += total;
	l_sc->sc_free += free;
	l_sc->sc_dinodes += dinodes;
	gfs2_statfs_change_out(l_sc, sdp->sd_sc_bh->b_data +
			       sizeof(struct gfs2_dinode));
	if (sdp->sd_args.ar_statfs_percent) {
		x = 100 * l_sc->sc_free;
		y = m_sc->sc_free * sdp->sd_args.ar_statfs_percent;
		if (x >= y || x <= -y)
			need_sync = 1;
	}
	spin_unlock(&sdp->sd_statfs_spin);

	if (need_sync)
		gfs2_wake_up_statfs(sdp);
}

void update_statfs(struct gfs2_sbd *sdp, struct buffer_head *m_bh)
{
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);
	struct gfs2_inode *l_ip = GFS2_I(sdp->sd_sc_inode);
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;

	gfs2_trans_add_meta(l_ip->i_gl, sdp->sd_sc_bh);
	gfs2_trans_add_meta(m_ip->i_gl, m_bh);

	spin_lock(&sdp->sd_statfs_spin);
	m_sc->sc_total += l_sc->sc_total;
	m_sc->sc_free += l_sc->sc_free;
	m_sc->sc_dinodes += l_sc->sc_dinodes;
	memset(l_sc, 0, sizeof(struct gfs2_statfs_change));
	memset(sdp->sd_sc_bh->b_data + sizeof(struct gfs2_dinode),
	       0, sizeof(struct gfs2_statfs_change));
	gfs2_statfs_change_out(m_sc, m_bh->b_data + sizeof(struct gfs2_dinode));
	spin_unlock(&sdp->sd_statfs_spin);
}

int gfs2_statfs_sync(struct super_block *sb, int type)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct gfs2_holder gh;
	struct buffer_head *m_bh;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, GL_NOCACHE,
				   &gh);
	if (error)
		goto out;

	error = gfs2_meta_inode_buffer(m_ip, &m_bh);
	if (error)
		goto out_unlock;

	spin_lock(&sdp->sd_statfs_spin);
	gfs2_statfs_change_in(m_sc, m_bh->b_data +
			      sizeof(struct gfs2_dinode));
	if (!l_sc->sc_total && !l_sc->sc_free && !l_sc->sc_dinodes) {
		spin_unlock(&sdp->sd_statfs_spin);
		goto out_bh;
	}
	spin_unlock(&sdp->sd_statfs_spin);

	error = gfs2_trans_begin(sdp, 2 * RES_DINODE, 0);
	if (error)
		goto out_bh;

	update_statfs(sdp, m_bh);
	sdp->sd_statfs_force_sync = 0;

	gfs2_trans_end(sdp);

out_bh:
	brelse(m_bh);
out_unlock:
	gfs2_glock_dq_uninit(&gh);
out:
	return error;
}

struct lfcc {
	struct list_head list;
	struct gfs2_holder gh;
};

/**
 * gfs2_lock_fs_check_clean - Stop all writes to the FS and check that all
 *                            journals are clean
 * @sdp: the file system
 *
 * Returns: errno
 */

static int gfs2_lock_fs_check_clean(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip;
	struct gfs2_jdesc *jd;
	struct lfcc *lfcc;
	LIST_HEAD(list);
	struct gfs2_log_header_host lh;
	int error;

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		lfcc = kmalloc(sizeof(struct lfcc), GFP_KERNEL);
		if (!lfcc) {
			error = -ENOMEM;
			goto out;
		}
		ip = GFS2_I(jd->jd_inode);
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &lfcc->gh);
		if (error) {
			kfree(lfcc);
			goto out;
		}
		list_add(&lfcc->list, &list);
	}

	error = gfs2_glock_nq_init(sdp->sd_freeze_gl, LM_ST_EXCLUSIVE,
				   LM_FLAG_NOEXP | GL_NOPID,
				   &sdp->sd_freeze_gh);
	if (error)
		goto out;

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		error = gfs2_jdesc_check(jd);
		if (error)
			break;
		error = gfs2_find_jhead(jd, &lh, false);
		if (error)
			break;
		if (!(lh.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
			error = -EBUSY;
			break;
		}
	}

	if (error)
		gfs2_freeze_unlock(&sdp->sd_freeze_gh);

out:
	while (!list_empty(&list)) {
		lfcc = list_first_entry(&list, struct lfcc, list);
		list_del(&lfcc->list);
		gfs2_glock_dq_uninit(&lfcc->gh);
		kfree(lfcc);
	}
	return error;
}

void gfs2_dinode_out(const struct gfs2_inode *ip, void *buf)
{
	struct gfs2_dinode *str = buf;

	str->di_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	str->di_header.mh_type = cpu_to_be32(GFS2_METATYPE_DI);
	str->di_header.mh_format = cpu_to_be32(GFS2_FORMAT_DI);
	str->di_num.no_addr = cpu_to_be64(ip->i_no_addr);
	str->di_num.no_formal_ino = cpu_to_be64(ip->i_no_formal_ino);
	str->di_mode = cpu_to_be32(ip->i_inode.i_mode);
	str->di_uid = cpu_to_be32(i_uid_read(&ip->i_inode));
	str->di_gid = cpu_to_be32(i_gid_read(&ip->i_inode));
	str->di_nlink = cpu_to_be32(ip->i_inode.i_nlink);
	str->di_size = cpu_to_be64(i_size_read(&ip->i_inode));
	str->di_blocks = cpu_to_be64(gfs2_get_inode_blocks(&ip->i_inode));
	str->di_atime = cpu_to_be64(ip->i_inode.i_atime.tv_sec);
	str->di_mtime = cpu_to_be64(ip->i_inode.i_mtime.tv_sec);
	str->di_ctime = cpu_to_be64(ip->i_inode.i_ctime.tv_sec);

	str->di_goal_meta = cpu_to_be64(ip->i_goal);
	str->di_goal_data = cpu_to_be64(ip->i_goal);
	str->di_generation = cpu_to_be64(ip->i_generation);

	str->di_flags = cpu_to_be32(ip->i_diskflags);
	str->di_height = cpu_to_be16(ip->i_height);
	str->di_payload_format = cpu_to_be32(S_ISDIR(ip->i_inode.i_mode) &&
					     !(ip->i_diskflags & GFS2_DIF_EXHASH) ?
					     GFS2_FORMAT_DE : 0);
	str->di_depth = cpu_to_be16(ip->i_depth);
	str->di_entries = cpu_to_be32(ip->i_entries);

	str->di_eattr = cpu_to_be64(ip->i_eattr);
	str->di_atime_nsec = cpu_to_be32(ip->i_inode.i_atime.tv_nsec);
	str->di_mtime_nsec = cpu_to_be32(ip->i_inode.i_mtime.tv_nsec);
	str->di_ctime_nsec = cpu_to_be32(ip->i_inode.i_ctime.tv_nsec);
}

/**
 * gfs2_write_inode - Make sure the inode is stable on the disk
 * @inode: The inode
 * @wbc: The writeback control structure
 *
 * Returns: errno
 */

static int gfs2_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct address_space *metamapping = gfs2_glock2aspace(ip->i_gl);
	struct backing_dev_info *bdi = inode_to_bdi(metamapping->host);
	int ret = 0;
	bool flush_all = (wbc->sync_mode == WB_SYNC_ALL || gfs2_is_jdata(ip));

	if (flush_all)
		gfs2_log_flush(GFS2_SB(inode), ip->i_gl,
			       GFS2_LOG_HEAD_FLUSH_NORMAL |
			       GFS2_LFC_WRITE_INODE);
	if (bdi->wb.dirty_exceeded)
		gfs2_ail1_flush(sdp, wbc);
	else
		filemap_fdatawrite(metamapping);
	if (flush_all)
		ret = filemap_fdatawait(metamapping);
	if (ret)
		mark_inode_dirty_sync(inode);
	else {
		spin_lock(&inode->i_lock);
		if (!(inode->i_flags & I_DIRTY))
			gfs2_ordered_del_inode(ip);
		spin_unlock(&inode->i_lock);
	}
	return ret;
}

/**
 * gfs2_dirty_inode - check for atime updates
 * @inode: The inode in question
 * @flags: The type of dirty
 *
 * Unfortunately it can be called under any combination of inode
 * glock and transaction lock, so we have to check carefully.
 *
 * At the moment this deals only with atime - it should be possible
 * to expand that role in future, once a review of the locking has
 * been carried out.
 */

static void gfs2_dirty_inode(struct inode *inode, int flags)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *bh;
	struct gfs2_holder gh;
	int need_unlock = 0;
	int need_endtrans = 0;
	int ret;

	if (unlikely(gfs2_withdrawn(sdp)))
		return;
	if (!gfs2_glock_is_locked_by_me(ip->i_gl)) {
		ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
		if (ret) {
			fs_err(sdp, "dirty_inode: glock %d\n", ret);
			gfs2_dump_glock(NULL, ip->i_gl, true);
			return;
		}
		need_unlock = 1;
	} else if (WARN_ON_ONCE(ip->i_gl->gl_state != LM_ST_EXCLUSIVE))
		return;

	if (current->journal_info == NULL) {
		ret = gfs2_trans_begin(sdp, RES_DINODE, 0);
		if (ret) {
			fs_err(sdp, "dirty_inode: gfs2_trans_begin %d\n", ret);
			goto out;
		}
		need_endtrans = 1;
	}

	ret = gfs2_meta_inode_buffer(ip, &bh);
	if (ret == 0) {
		gfs2_trans_add_meta(ip->i_gl, bh);
		gfs2_dinode_out(ip, bh->b_data);
		brelse(bh);
	}

	if (need_endtrans)
		gfs2_trans_end(sdp);
out:
	if (need_unlock)
		gfs2_glock_dq_uninit(&gh);
}

/**
 * gfs2_make_fs_ro - Turn a Read-Write FS into a Read-Only one
 * @sdp: the filesystem
 *
 * Returns: errno
 */

void gfs2_make_fs_ro(struct gfs2_sbd *sdp)
{
	int log_write_allowed = test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);

	gfs2_flush_delete_work(sdp);
	if (!log_write_allowed && current == sdp->sd_quotad_process)
		fs_warn(sdp, "The quotad daemon is withdrawing.\n");
	else if (sdp->sd_quotad_process)
		kthread_stop(sdp->sd_quotad_process);
	sdp->sd_quotad_process = NULL;

	if (!log_write_allowed && current == sdp->sd_logd_process)
		fs_warn(sdp, "The logd daemon is withdrawing.\n");
	else if (sdp->sd_logd_process)
		kthread_stop(sdp->sd_logd_process);
	sdp->sd_logd_process = NULL;

	if (log_write_allowed) {
		gfs2_quota_sync(sdp->sd_vfs, 0);
		gfs2_statfs_sync(sdp->sd_vfs, 0);

		gfs2_log_flush(sdp, NULL, GFS2_LOG_HEAD_FLUSH_SHUTDOWN |
			       GFS2_LFC_MAKE_FS_RO);
		wait_event_timeout(sdp->sd_log_waitq,
				   gfs2_log_is_empty(sdp),
				   HZ * 5);
		gfs2_assert_warn(sdp, gfs2_log_is_empty(sdp));
	} else {
		wait_event_timeout(sdp->sd_log_waitq,
				   gfs2_log_is_empty(sdp),
				   HZ * 5);
	}
	gfs2_quota_cleanup(sdp);

	if (!log_write_allowed)
		sdp->sd_vfs->s_flags |= SB_RDONLY;
}

/**
 * gfs2_put_super - Unmount the filesystem
 * @sb: The VFS superblock
 *
 */

static void gfs2_put_super(struct super_block *sb)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_jdesc *jd;

	/* No more recovery requests */
	set_bit(SDF_NORECOVERY, &sdp->sd_flags);
	smp_mb();

	/* Wait on outstanding recovery */
restart:
	spin_lock(&sdp->sd_jindex_spin);
	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		if (!test_bit(JDF_RECOVERY, &jd->jd_flags))
			continue;
		spin_unlock(&sdp->sd_jindex_spin);
		wait_on_bit(&jd->jd_flags, JDF_RECOVERY,
			    TASK_UNINTERRUPTIBLE);
		goto restart;
	}
	spin_unlock(&sdp->sd_jindex_spin);

	if (!sb_rdonly(sb)) {
		gfs2_make_fs_ro(sdp);
	}
	WARN_ON(gfs2_withdrawing(sdp));

	/*  At this point, we're through modifying the disk  */

	/*  Release stuff  */

	iput(sdp->sd_jindex);
	iput(sdp->sd_statfs_inode);
	iput(sdp->sd_rindex);
	iput(sdp->sd_quota_inode);

	gfs2_glock_put(sdp->sd_rename_gl);
	gfs2_glock_put(sdp->sd_freeze_gl);

	if (!sdp->sd_args.ar_spectator) {
		if (gfs2_holder_initialized(&sdp->sd_journal_gh))
			gfs2_glock_dq_uninit(&sdp->sd_journal_gh);
		if (gfs2_holder_initialized(&sdp->sd_jinode_gh))
			gfs2_glock_dq_uninit(&sdp->sd_jinode_gh);
		brelse(sdp->sd_sc_bh);
		gfs2_glock_dq_uninit(&sdp->sd_sc_gh);
		gfs2_glock_dq_uninit(&sdp->sd_qc_gh);
		free_local_statfs_inodes(sdp);
		iput(sdp->sd_qc_inode);
	}

	gfs2_glock_dq_uninit(&sdp->sd_live_gh);
	gfs2_clear_rgrpd(sdp);
	gfs2_jindex_free(sdp);
	/*  Take apart glock structures and buffer lists  */
	gfs2_gl_hash_clear(sdp);
	truncate_inode_pages_final(&sdp->sd_aspace);
	gfs2_delete_debugfs_file(sdp);
	/*  Unmount the locking protocol  */
	gfs2_lm_unmount(sdp);

	/*  At this point, we're through participating in the lockspace  */
	gfs2_sys_fs_del(sdp);
	free_sbd(sdp);
}

/**
 * gfs2_sync_fs - sync the filesystem
 * @sb: the superblock
 * @wait: true to wait for completion
 *
 * Flushes the log to disk.
 */

static int gfs2_sync_fs(struct super_block *sb, int wait)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;

	gfs2_quota_sync(sb, -1);
	if (wait)
		gfs2_log_flush(sdp, NULL, GFS2_LOG_HEAD_FLUSH_NORMAL |
			       GFS2_LFC_SYNC_FS);
	return sdp->sd_log_error;
}

void gfs2_freeze_func(struct work_struct *work)
{
	int error;
	struct gfs2_holder freeze_gh;
	struct gfs2_sbd *sdp = container_of(work, struct gfs2_sbd, sd_freeze_work);
	struct super_block *sb = sdp->sd_vfs;

	atomic_inc(&sb->s_active);
	error = gfs2_freeze_lock(sdp, &freeze_gh, 0);
	if (error) {
		gfs2_assert_withdraw(sdp, 0);
	} else {
		atomic_set(&sdp->sd_freeze_state, SFS_UNFROZEN);
		error = thaw_super(sb);
		if (error) {
			fs_info(sdp, "GFS2: couldn't thaw filesystem: %d\n",
				error);
			gfs2_assert_withdraw(sdp, 0);
		}
		gfs2_freeze_unlock(&freeze_gh);
	}
	deactivate_super(sb);
	clear_bit_unlock(SDF_FS_FROZEN, &sdp->sd_flags);
	wake_up_bit(&sdp->sd_flags, SDF_FS_FROZEN);
	return;
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

	mutex_lock(&sdp->sd_freeze_mutex);
	if (atomic_read(&sdp->sd_freeze_state) != SFS_UNFROZEN) {
		error = -EBUSY;
		goto out;
	}

	for (;;) {
		if (gfs2_withdrawn(sdp)) {
			error = -EINVAL;
			goto out;
		}

		error = gfs2_lock_fs_check_clean(sdp);
		if (!error)
			break;

		if (error == -EBUSY)
			fs_err(sdp, "waiting for recovery before freeze\n");
		else if (error == -EIO) {
			fs_err(sdp, "Fatal IO error: cannot freeze gfs2 due "
			       "to recovery error.\n");
			goto out;
		} else {
			fs_err(sdp, "error freezing FS: %d\n", error);
		}
		fs_err(sdp, "retrying...\n");
		msleep(1000);
	}
	set_bit(SDF_FS_FROZEN, &sdp->sd_flags);
out:
	mutex_unlock(&sdp->sd_freeze_mutex);
	return error;
}

/**
 * gfs2_unfreeze - reallow writes to the filesystem
 * @sb: the VFS structure for the filesystem
 *
 */

static int gfs2_unfreeze(struct super_block *sb)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;

	mutex_lock(&sdp->sd_freeze_mutex);
	if (atomic_read(&sdp->sd_freeze_state) != SFS_FROZEN ||
	    !gfs2_holder_initialized(&sdp->sd_freeze_gh)) {
		mutex_unlock(&sdp->sd_freeze_mutex);
		return -EINVAL;
	}

	gfs2_freeze_unlock(&sdp->sd_freeze_gh);
	mutex_unlock(&sdp->sd_freeze_mutex);
	return wait_on_bit(&sdp->sd_flags, SDF_FS_FROZEN, TASK_INTERRUPTIBLE);
}

/**
 * statfs_slow_fill - fill in the sg for a given RG
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
	struct gfs2_rgrpd *rgd_next;
	struct gfs2_holder *gha, *gh;
	unsigned int slots = 64;
	unsigned int x;
	int done;
	int error = 0, err;

	memset(sc, 0, sizeof(struct gfs2_statfs_change_host));
	gha = kmalloc_array(slots, sizeof(struct gfs2_holder), GFP_KERNEL);
	if (!gha)
		return -ENOMEM;
	for (x = 0; x < slots; x++)
		gfs2_holder_mark_uninitialized(gha + x);

	rgd_next = gfs2_rgrpd_get_first(sdp);

	for (;;) {
		done = 1;

		for (x = 0; x < slots; x++) {
			gh = gha + x;

			if (gfs2_holder_initialized(gh) && gfs2_glock_poll(gh)) {
				err = gfs2_glock_wait(gh);
				if (err) {
					gfs2_holder_uninit(gh);
					error = err;
				} else {
					if (!error) {
						struct gfs2_rgrpd *rgd =
							gfs2_glock2rgrp(gh->gh_gl);

						error = statfs_slow_fill(rgd, sc);
					}
					gfs2_glock_dq_uninit(gh);
				}
			}

			if (gfs2_holder_initialized(gh))
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

	kfree(gha);
	return error;
}

/**
 * gfs2_statfs_i - Do a statfs
 * @sdp: the filesystem
 * @sc: the sc structure
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
 * @dentry: The name of the link
 * @buf: The buffer
 *
 * Returns: 0 on success or error code
 */

static int gfs2_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_statfs_change_host sc;
	int error;

	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

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
 * gfs2_drop_inode - Drop an inode (test for remote unlink)
 * @inode: The inode to drop
 *
 * If we've received a callback on an iopen lock then it's because a
 * remote node tried to deallocate the inode but failed due to this node
 * still having the inode open. Here we mark the link count zero
 * since we know that it must have reached zero if the GLF_DEMOTE flag
 * is set on the iopen glock. If we didn't do a disk read since the
 * remote node removed the final link then we might otherwise miss
 * this event. This check ensures that this node will deallocate the
 * inode's blocks, or alternatively pass the baton on to another
 * node for later deallocation.
 */

static int gfs2_drop_inode(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);

	if (!test_bit(GIF_FREE_VFS_INODE, &ip->i_flags) &&
	    inode->i_nlink &&
	    gfs2_holder_initialized(&ip->i_iopen_gh)) {
		struct gfs2_glock *gl = ip->i_iopen_gh.gh_gl;
		if (test_bit(GLF_DEMOTE, &gl->gl_flags))
			clear_nlink(inode);
	}

	/*
	 * When under memory pressure when an inode's link count has dropped to
	 * zero, defer deleting the inode to the delete workqueue.  This avoids
	 * calling into DLM under memory pressure, which can deadlock.
	 */
	if (!inode->i_nlink &&
	    unlikely(current->flags & PF_MEMALLOC) &&
	    gfs2_holder_initialized(&ip->i_iopen_gh)) {
		struct gfs2_glock *gl = ip->i_iopen_gh.gh_gl;

		gfs2_glock_hold(gl);
		if (!gfs2_queue_delete_work(gl, 0))
			gfs2_glock_queue_put(gl);
		return 0;
	}

	return generic_drop_inode(inode);
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
 * @root: root of this (sub)tree
 *
 * Returns: 0 on success or error code
 */

static int gfs2_show_options(struct seq_file *s, struct dentry *root)
{
	struct gfs2_sbd *sdp = root->d_sb->s_fs_info;
	struct gfs2_args *args = &sdp->sd_args;
	int val;

	if (is_ancestor(root, sdp->sd_master_dir))
		seq_puts(s, ",meta");
	if (args->ar_lockproto[0])
		seq_show_option(s, "lockproto", args->ar_lockproto);
	if (args->ar_locktable[0])
		seq_show_option(s, "locktable", args->ar_locktable);
	if (args->ar_hostdata[0])
		seq_show_option(s, "hostdata", args->ar_hostdata);
	if (args->ar_spectator)
		seq_puts(s, ",spectator");
	if (args->ar_localflocks)
		seq_puts(s, ",localflocks");
	if (args->ar_debug)
		seq_puts(s, ",debug");
	if (args->ar_posix_acl)
		seq_puts(s, ",acl");
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
		seq_puts(s, ",suiddir");
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
	if (args->ar_discard)
		seq_puts(s, ",discard");
	val = sdp->sd_tune.gt_logd_secs;
	if (val != 30)
		seq_printf(s, ",commit=%d", val);
	val = sdp->sd_tune.gt_statfs_quantum;
	if (val != 30)
		seq_printf(s, ",statfs_quantum=%d", val);
	else if (sdp->sd_tune.gt_statfs_slow)
		seq_puts(s, ",statfs_quantum=0");
	val = sdp->sd_tune.gt_quota_quantum;
	if (val != 60)
		seq_printf(s, ",quota_quantum=%d", val);
	if (args->ar_statfs_percent)
		seq_printf(s, ",statfs_percent=%d", args->ar_statfs_percent);
	if (args->ar_errors != GFS2_ERRORS_DEFAULT) {
		const char *state;

		switch (args->ar_errors) {
		case GFS2_ERRORS_WITHDRAW:
			state = "withdraw";
			break;
		case GFS2_ERRORS_PANIC:
			state = "panic";
			break;
		default:
			state = "unknown";
			break;
		}
		seq_printf(s, ",errors=%s", state);
	}
	if (test_bit(SDF_NOBARRIERS, &sdp->sd_flags))
		seq_puts(s, ",nobarrier");
	if (test_bit(SDF_DEMOTE, &sdp->sd_flags))
		seq_puts(s, ",demote_interface_used");
	if (args->ar_rgrplvb)
		seq_puts(s, ",rgrplvb");
	if (args->ar_loccookie)
		seq_puts(s, ",loccookie");
	return 0;
}

static void gfs2_final_release_pages(struct gfs2_inode *ip)
{
	struct inode *inode = &ip->i_inode;
	struct gfs2_glock *gl = ip->i_gl;

	truncate_inode_pages(gfs2_glock2aspace(ip->i_gl), 0);
	truncate_inode_pages(&inode->i_data, 0);

	if (atomic_read(&gl->gl_revokes) == 0) {
		clear_bit(GLF_LFLUSH, &gl->gl_flags);
		clear_bit(GLF_DIRTY, &gl->gl_flags);
	}
}

static int gfs2_dinode_dealloc(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *rgd;
	struct gfs2_holder gh;
	int error;

	if (gfs2_get_inode_blocks(&ip->i_inode) != 1) {
		gfs2_consist_inode(ip);
		return -EIO;
	}

	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

	error = gfs2_quota_hold(ip, NO_UID_QUOTA_CHANGE, NO_GID_QUOTA_CHANGE);
	if (error)
		return error;

	rgd = gfs2_blk2rgrpd(sdp, ip->i_no_addr, 1);
	if (!rgd) {
		gfs2_consist_inode(ip);
		error = -EIO;
		goto out_qs;
	}

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE,
				   LM_FLAG_NODE_SCOPE, &gh);
	if (error)
		goto out_qs;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_STATFS + RES_QUOTA,
				 sdp->sd_jdesc->jd_blocks);
	if (error)
		goto out_rg_gunlock;

	gfs2_free_di(rgd, ip);

	gfs2_final_release_pages(ip);

	gfs2_trans_end(sdp);

out_rg_gunlock:
	gfs2_glock_dq_uninit(&gh);
out_qs:
	gfs2_quota_unhold(ip);
	return error;
}

/**
 * gfs2_glock_put_eventually
 * @gl:	The glock to put
 *
 * When under memory pressure, trigger a deferred glock put to make sure we
 * won't call into DLM and deadlock.  Otherwise, put the glock directly.
 */

static void gfs2_glock_put_eventually(struct gfs2_glock *gl)
{
	if (current->flags & PF_MEMALLOC)
		gfs2_glock_queue_put(gl);
	else
		gfs2_glock_put(gl);
}

static bool gfs2_upgrade_iopen_glock(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_holder *gh = &ip->i_iopen_gh;
	long timeout = 5 * HZ;
	int error;

	gh->gh_flags |= GL_NOCACHE;
	gfs2_glock_dq_wait(gh);

	/*
	 * If there are no other lock holders, we'll get the lock immediately.
	 * Otherwise, the other nodes holding the lock will be notified about
	 * our locking request.  If they don't have the inode open, they'll
	 * evict the cached inode and release the lock.  Otherwise, if they
	 * poke the inode glock, we'll take this as an indication that they
	 * still need the iopen glock and that they'll take care of deleting
	 * the inode when they're done.  As a last resort, if another node
	 * keeps holding the iopen glock without showing any activity on the
	 * inode glock, we'll eventually time out.
	 *
	 * Note that we're passing the LM_FLAG_TRY_1CB flag to the first
	 * locking request as an optimization to notify lock holders as soon as
	 * possible.  Without that flag, they'd be notified implicitly by the
	 * second locking request.
	 */

	gfs2_holder_reinit(LM_ST_EXCLUSIVE, LM_FLAG_TRY_1CB | GL_NOCACHE, gh);
	error = gfs2_glock_nq(gh);
	if (error != GLR_TRYFAILED)
		return !error;

	gfs2_holder_reinit(LM_ST_EXCLUSIVE, GL_ASYNC | GL_NOCACHE, gh);
	error = gfs2_glock_nq(gh);
	if (error)
		return false;

	timeout = wait_event_interruptible_timeout(sdp->sd_async_glock_wait,
		!test_bit(HIF_WAIT, &gh->gh_iflags) ||
		test_bit(GLF_DEMOTE, &ip->i_gl->gl_flags),
		timeout);
	if (!test_bit(HIF_HOLDER, &gh->gh_iflags)) {
		gfs2_glock_dq(gh);
		return false;
	}
	return gfs2_glock_holder_ready(gh) == 0;
}

/**
 * evict_should_delete - determine whether the inode is eligible for deletion
 * @inode: The inode to evict
 * @gh: The glock holder structure
 *
 * This function determines whether the evicted inode is eligible to be deleted
 * and locks the inode glock.
 *
 * Returns: the fate of the dinode
 */
static enum dinode_demise evict_should_delete(struct inode *inode,
					      struct gfs2_holder *gh)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct super_block *sb = inode->i_sb;
	struct gfs2_sbd *sdp = sb->s_fs_info;
	int ret;

	if (test_bit(GIF_ALLOC_FAILED, &ip->i_flags)) {
		BUG_ON(!gfs2_glock_is_locked_by_me(ip->i_gl));
		goto should_delete;
	}

	if (test_bit(GIF_DEFERRED_DELETE, &ip->i_flags))
		return SHOULD_DEFER_EVICTION;

	/* Deletes should never happen under memory pressure anymore.  */
	if (WARN_ON_ONCE(current->flags & PF_MEMALLOC))
		return SHOULD_DEFER_EVICTION;

	/* Must not read inode block until block type has been verified */
	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_SKIP, gh);
	if (unlikely(ret)) {
		glock_clear_object(ip->i_iopen_gh.gh_gl, ip);
		ip->i_iopen_gh.gh_flags |= GL_NOCACHE;
		gfs2_glock_dq_uninit(&ip->i_iopen_gh);
		return SHOULD_DEFER_EVICTION;
	}

	if (gfs2_inode_already_deleted(ip->i_gl, ip->i_no_formal_ino))
		return SHOULD_NOT_DELETE_DINODE;
	ret = gfs2_check_blk_type(sdp, ip->i_no_addr, GFS2_BLKST_UNLINKED);
	if (ret)
		return SHOULD_NOT_DELETE_DINODE;

	ret = gfs2_instantiate(gh);
	if (ret)
		return SHOULD_NOT_DELETE_DINODE;

	/*
	 * The inode may have been recreated in the meantime.
	 */
	if (inode->i_nlink)
		return SHOULD_NOT_DELETE_DINODE;

should_delete:
	if (gfs2_holder_initialized(&ip->i_iopen_gh) &&
	    test_bit(HIF_HOLDER, &ip->i_iopen_gh.gh_iflags)) {
		if (!gfs2_upgrade_iopen_glock(inode)) {
			gfs2_holder_uninit(&ip->i_iopen_gh);
			return SHOULD_NOT_DELETE_DINODE;
		}
	}
	return SHOULD_DELETE_DINODE;
}

/**
 * evict_unlinked_inode - delete the pieces of an unlinked evicted inode
 * @inode: The inode to evict
 */
static int evict_unlinked_inode(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	int ret;

	if (S_ISDIR(inode->i_mode) &&
	    (ip->i_diskflags & GFS2_DIF_EXHASH)) {
		ret = gfs2_dir_exhash_dealloc(ip);
		if (ret)
			goto out;
	}

	if (ip->i_eattr) {
		ret = gfs2_ea_dealloc(ip);
		if (ret)
			goto out;
	}

	if (!gfs2_is_stuffed(ip)) {
		ret = gfs2_file_dealloc(ip);
		if (ret)
			goto out;
	}

	/* We're about to clear the bitmap for the dinode, but as soon as we
	   do, gfs2_create_inode can create another inode at the same block
	   location and try to set gl_object again. We clear gl_object here so
	   that subsequent inode creates don't see an old gl_object. */
	glock_clear_object(ip->i_gl, ip);
	ret = gfs2_dinode_dealloc(ip);
	gfs2_inode_remember_delete(ip->i_gl, ip->i_no_formal_ino);
out:
	return ret;
}

/*
 * evict_linked_inode - evict an inode whose dinode has not been unlinked
 * @inode: The inode to evict
 */
static int evict_linked_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct address_space *metamapping;
	int ret;

	gfs2_log_flush(sdp, ip->i_gl, GFS2_LOG_HEAD_FLUSH_NORMAL |
		       GFS2_LFC_EVICT_INODE);
	metamapping = gfs2_glock2aspace(ip->i_gl);
	if (test_bit(GLF_DIRTY, &ip->i_gl->gl_flags)) {
		filemap_fdatawrite(metamapping);
		filemap_fdatawait(metamapping);
	}
	write_inode_now(inode, 1);
	gfs2_ail_flush(ip->i_gl, 0);

	ret = gfs2_trans_begin(sdp, 0, sdp->sd_jdesc->jd_blocks);
	if (ret)
		return ret;

	/* Needs to be done before glock release & also in a transaction */
	truncate_inode_pages(&inode->i_data, 0);
	truncate_inode_pages(metamapping, 0);
	gfs2_trans_end(sdp);
	return 0;
}

/**
 * gfs2_evict_inode - Remove an inode from cache
 * @inode: The inode to evict
 *
 * There are three cases to consider:
 * 1. i_nlink == 0, we are final opener (and must deallocate)
 * 2. i_nlink == 0, we are not the final opener (and cannot deallocate)
 * 3. i_nlink > 0
 *
 * If the fs is read only, then we have to treat all cases as per #3
 * since we are unable to do any deallocation. The inode will be
 * deallocated by the next read/write node to attempt an allocation
 * in the same resource group
 *
 * We have to (at the moment) hold the inodes main lock to cover
 * the gap between unlocking the shared lock on the iopen lock and
 * taking the exclusive lock. I'd rather do a shared -> exclusive
 * conversion on the iopen lock, but we can change that later. This
 * is safe, just less efficient.
 */

static void gfs2_evict_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	int ret;

	if (test_bit(GIF_FREE_VFS_INODE, &ip->i_flags)) {
		clear_inode(inode);
		return;
	}

	if (inode->i_nlink || sb_rdonly(sb))
		goto out;

	gfs2_holder_mark_uninitialized(&gh);
	ret = evict_should_delete(inode, &gh);
	if (ret == SHOULD_DEFER_EVICTION)
		goto out;
	if (ret == SHOULD_DELETE_DINODE)
		ret = evict_unlinked_inode(inode);
	else
		ret = evict_linked_inode(inode);

	if (gfs2_rs_active(&ip->i_res))
		gfs2_rs_deltree(&ip->i_res);

	if (gfs2_holder_initialized(&gh)) {
		glock_clear_object(ip->i_gl, ip);
		gfs2_glock_dq_uninit(&gh);
	}
	if (ret && ret != GLR_TRYFAILED && ret != -EROFS)
		fs_warn(sdp, "gfs2_evict_inode: %d\n", ret);
out:
	truncate_inode_pages_final(&inode->i_data);
	if (ip->i_qadata)
		gfs2_assert_warn(sdp, ip->i_qadata->qa_ref == 0);
	gfs2_rs_deltree(&ip->i_res);
	gfs2_ordered_del_inode(ip);
	clear_inode(inode);
	gfs2_dir_hash_inval(ip);
	if (gfs2_holder_initialized(&ip->i_iopen_gh)) {
		struct gfs2_glock *gl = ip->i_iopen_gh.gh_gl;

		glock_clear_object(gl, ip);
		if (test_bit(HIF_HOLDER, &ip->i_iopen_gh.gh_iflags)) {
			ip->i_iopen_gh.gh_flags |= GL_NOCACHE;
			gfs2_glock_dq(&ip->i_iopen_gh);
		}
		gfs2_glock_hold(gl);
		gfs2_holder_uninit(&ip->i_iopen_gh);
		gfs2_glock_put_eventually(gl);
	}
	if (ip->i_gl) {
		glock_clear_object(ip->i_gl, ip);
		wait_on_bit_io(&ip->i_flags, GIF_GLOP_PENDING, TASK_UNINTERRUPTIBLE);
		gfs2_glock_add_to_lru(ip->i_gl);
		gfs2_glock_put_eventually(ip->i_gl);
		ip->i_gl = NULL;
	}
}

static struct inode *gfs2_alloc_inode(struct super_block *sb)
{
	struct gfs2_inode *ip;

	ip = alloc_inode_sb(sb, gfs2_inode_cachep, GFP_KERNEL);
	if (!ip)
		return NULL;
	ip->i_flags = 0;
	ip->i_gl = NULL;
	gfs2_holder_mark_uninitialized(&ip->i_iopen_gh);
	memset(&ip->i_res, 0, sizeof(ip->i_res));
	RB_CLEAR_NODE(&ip->i_res.rs_node);
	ip->i_rahead = 0;
	return &ip->i_inode;
}

static void gfs2_free_inode(struct inode *inode)
{
	kmem_cache_free(gfs2_inode_cachep, GFS2_I(inode));
}

extern void free_local_statfs_inodes(struct gfs2_sbd *sdp)
{
	struct local_statfs_inode *lsi, *safe;

	/* Run through the statfs inodes list to iput and free memory */
	list_for_each_entry_safe(lsi, safe, &sdp->sd_sc_inodes_list, si_list) {
		if (lsi->si_jid == sdp->sd_jdesc->jd_jid)
			sdp->sd_sc_inode = NULL; /* belongs to this node */
		if (lsi->si_sc_inode)
			iput(lsi->si_sc_inode);
		list_del(&lsi->si_list);
		kfree(lsi);
	}
}

extern struct inode *find_local_statfs_inode(struct gfs2_sbd *sdp,
					     unsigned int index)
{
	struct local_statfs_inode *lsi;

	/* Return the local (per node) statfs inode in the
	 * sdp->sd_sc_inodes_list corresponding to the 'index'. */
	list_for_each_entry(lsi, &sdp->sd_sc_inodes_list, si_list) {
		if (lsi->si_jid == index)
			return lsi->si_sc_inode;
	}
	return NULL;
}

const struct super_operations gfs2_super_ops = {
	.alloc_inode		= gfs2_alloc_inode,
	.free_inode		= gfs2_free_inode,
	.write_inode		= gfs2_write_inode,
	.dirty_inode		= gfs2_dirty_inode,
	.evict_inode		= gfs2_evict_inode,
	.put_super		= gfs2_put_super,
	.sync_fs		= gfs2_sync_fs,
	.freeze_super		= gfs2_freeze,
	.thaw_super		= gfs2_unfreeze,
	.statfs			= gfs2_statfs,
	.drop_inode		= gfs2_drop_inode,
	.show_options		= gfs2_show_options,
};

