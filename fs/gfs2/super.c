/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/bio.h>
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
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/writeback.h>

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

#define args_neq(a1, a2, x) ((a1)->ar_##x != (a2)->ar_##x)

enum {
	Opt_lockproto,
	Opt_locktable,
	Opt_hostdata,
	Opt_spectator,
	Opt_ignore_local_fs,
	Opt_localflocks,
	Opt_localcaching,
	Opt_debug,
	Opt_nodebug,
	Opt_upgrade,
	Opt_acl,
	Opt_noacl,
	Opt_quota_off,
	Opt_quota_account,
	Opt_quota_on,
	Opt_quota,
	Opt_noquota,
	Opt_suiddir,
	Opt_nosuiddir,
	Opt_data_writeback,
	Opt_data_ordered,
	Opt_meta,
	Opt_discard,
	Opt_nodiscard,
	Opt_commit,
	Opt_err_withdraw,
	Opt_err_panic,
	Opt_statfs_quantum,
	Opt_statfs_percent,
	Opt_quota_quantum,
	Opt_barrier,
	Opt_nobarrier,
	Opt_error,
};

static const match_table_t tokens = {
	{Opt_lockproto, "lockproto=%s"},
	{Opt_locktable, "locktable=%s"},
	{Opt_hostdata, "hostdata=%s"},
	{Opt_spectator, "spectator"},
	{Opt_spectator, "norecovery"},
	{Opt_ignore_local_fs, "ignore_local_fs"},
	{Opt_localflocks, "localflocks"},
	{Opt_localcaching, "localcaching"},
	{Opt_debug, "debug"},
	{Opt_nodebug, "nodebug"},
	{Opt_upgrade, "upgrade"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_quota_off, "quota=off"},
	{Opt_quota_account, "quota=account"},
	{Opt_quota_on, "quota=on"},
	{Opt_quota, "quota"},
	{Opt_noquota, "noquota"},
	{Opt_suiddir, "suiddir"},
	{Opt_nosuiddir, "nosuiddir"},
	{Opt_data_writeback, "data=writeback"},
	{Opt_data_ordered, "data=ordered"},
	{Opt_meta, "meta"},
	{Opt_discard, "discard"},
	{Opt_nodiscard, "nodiscard"},
	{Opt_commit, "commit=%d"},
	{Opt_err_withdraw, "errors=withdraw"},
	{Opt_err_panic, "errors=panic"},
	{Opt_statfs_quantum, "statfs_quantum=%d"},
	{Opt_statfs_percent, "statfs_percent=%d"},
	{Opt_quota_quantum, "quota_quantum=%d"},
	{Opt_barrier, "barrier"},
	{Opt_nobarrier, "nobarrier"},
	{Opt_error, NULL}
};

/**
 * gfs2_mount_args - Parse mount options
 * @args: The structure into which the parsed options will be written
 * @options: The options to parse
 *
 * Return: errno
 */

int gfs2_mount_args(struct gfs2_args *args, char *options)
{
	char *o;
	int token;
	substring_t tmp[MAX_OPT_ARGS];
	int rv;

	/* Split the options into tokens with the "," character and
	   process them */

	while (1) {
		o = strsep(&options, ",");
		if (o == NULL)
			break;
		if (*o == '\0')
			continue;

		token = match_token(o, tokens, tmp);
		switch (token) {
		case Opt_lockproto:
			match_strlcpy(args->ar_lockproto, &tmp[0],
				      GFS2_LOCKNAME_LEN);
			break;
		case Opt_locktable:
			match_strlcpy(args->ar_locktable, &tmp[0],
				      GFS2_LOCKNAME_LEN);
			break;
		case Opt_hostdata:
			match_strlcpy(args->ar_hostdata, &tmp[0],
				      GFS2_LOCKNAME_LEN);
			break;
		case Opt_spectator:
			args->ar_spectator = 1;
			break;
		case Opt_ignore_local_fs:
			/* Retained for backwards compat only */
			break;
		case Opt_localflocks:
			args->ar_localflocks = 1;
			break;
		case Opt_localcaching:
			/* Retained for backwards compat only */
			break;
		case Opt_debug:
			if (args->ar_errors == GFS2_ERRORS_PANIC) {
				printk(KERN_WARNING "GFS2: -o debug and -o errors=panic "
				       "are mutually exclusive.\n");
				return -EINVAL;
			}
			args->ar_debug = 1;
			break;
		case Opt_nodebug:
			args->ar_debug = 0;
			break;
		case Opt_upgrade:
			/* Retained for backwards compat only */
			break;
		case Opt_acl:
			args->ar_posix_acl = 1;
			break;
		case Opt_noacl:
			args->ar_posix_acl = 0;
			break;
		case Opt_quota_off:
		case Opt_noquota:
			args->ar_quota = GFS2_QUOTA_OFF;
			break;
		case Opt_quota_account:
			args->ar_quota = GFS2_QUOTA_ACCOUNT;
			break;
		case Opt_quota_on:
		case Opt_quota:
			args->ar_quota = GFS2_QUOTA_ON;
			break;
		case Opt_suiddir:
			args->ar_suiddir = 1;
			break;
		case Opt_nosuiddir:
			args->ar_suiddir = 0;
			break;
		case Opt_data_writeback:
			args->ar_data = GFS2_DATA_WRITEBACK;
			break;
		case Opt_data_ordered:
			args->ar_data = GFS2_DATA_ORDERED;
			break;
		case Opt_meta:
			args->ar_meta = 1;
			break;
		case Opt_discard:
			args->ar_discard = 1;
			break;
		case Opt_nodiscard:
			args->ar_discard = 0;
			break;
		case Opt_commit:
			rv = match_int(&tmp[0], &args->ar_commit);
			if (rv || args->ar_commit <= 0) {
				printk(KERN_WARNING "GFS2: commit mount option requires a positive numeric argument\n");
				return rv ? rv : -EINVAL;
			}
			break;
		case Opt_statfs_quantum:
			rv = match_int(&tmp[0], &args->ar_statfs_quantum);
			if (rv || args->ar_statfs_quantum < 0) {
				printk(KERN_WARNING "GFS2: statfs_quantum mount option requires a non-negative numeric argument\n");
				return rv ? rv : -EINVAL;
			}
			break;
		case Opt_quota_quantum:
			rv = match_int(&tmp[0], &args->ar_quota_quantum);
			if (rv || args->ar_quota_quantum <= 0) {
				printk(KERN_WARNING "GFS2: quota_quantum mount option requires a positive numeric argument\n");
				return rv ? rv : -EINVAL;
			}
			break;
		case Opt_statfs_percent:
			rv = match_int(&tmp[0], &args->ar_statfs_percent);
			if (rv || args->ar_statfs_percent < 0 ||
			    args->ar_statfs_percent > 100) {
				printk(KERN_WARNING "statfs_percent mount option requires a numeric argument between 0 and 100\n");
				return rv ? rv : -EINVAL;
			}
			break;
		case Opt_err_withdraw:
			args->ar_errors = GFS2_ERRORS_WITHDRAW;
			break;
		case Opt_err_panic:
			if (args->ar_debug) {
				printk(KERN_WARNING "GFS2: -o debug and -o errors=panic "
					"are mutually exclusive.\n");
				return -EINVAL;
			}
			args->ar_errors = GFS2_ERRORS_PANIC;
			break;
		case Opt_barrier:
			args->ar_nobarrier = 0;
			break;
		case Opt_nobarrier:
			args->ar_nobarrier = 1;
			break;
		case Opt_error:
		default:
			printk(KERN_WARNING "GFS2: invalid mount option: %s\n", o);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * gfs2_jindex_free - Clear all the journal index information
 * @sdp: The GFS2 superblock
 *
 */

void gfs2_jindex_free(struct gfs2_sbd *sdp)
{
	struct list_head list, *head;
	struct gfs2_jdesc *jd;
	struct gfs2_journal_extent *jext;

	spin_lock(&sdp->sd_jindex_spin);
	list_add(&list, &sdp->sd_jindex_list);
	list_del_init(&sdp->sd_jindex_list);
	sdp->sd_journals = 0;
	spin_unlock(&sdp->sd_jindex_spin);

	while (!list_empty(&list)) {
		jd = list_entry(list.next, struct gfs2_jdesc, jd_list);
		head = &jd->extent_list;
		while (!list_empty(head)) {
			jext = list_entry(head->next,
					  struct gfs2_journal_extent,
					  extent_list);
			list_del(&jext->extent_list);
			kfree(jext);
		}
		list_del(&jd->jd_list);
		iput(jd->jd_inode);
		kfree(jd);
	}
}

static struct gfs2_jdesc *jdesc_find_i(struct list_head *head, unsigned int jid)
{
	struct gfs2_jdesc *jd;
	int found = 0;

	list_for_each_entry(jd, head, jd_list) {
		if (jd->jd_jid == jid) {
			found = 1;
			break;
		}
	}

	if (!found)
		jd = NULL;

	return jd;
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

	if (gfs2_check_internal_file_size(jd->jd_inode, 8 << 20, 1 << 30))
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
	struct gfs2_holder t_gh;
	struct gfs2_log_header_host head;
	int error;

	error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_SHARED, 0, &t_gh);
	if (error)
		return error;

	j_gl->gl_ops->go_inval(j_gl, DIO_METADATA);

	error = gfs2_find_jhead(sdp->sd_jdesc, &head);
	if (error)
		goto fail;

	if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
		gfs2_consist(sdp);
		error = -EIO;
		goto fail;
	}

	/*  Initialize some head of the log stuff  */
	sdp->sd_log_sequence = head.lh_sequence + 1;
	gfs2_log_pointers_init(sdp, head.lh_blkno);

	error = gfs2_quota_init(sdp);
	if (error)
		goto fail;

	set_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);

	gfs2_glock_dq_uninit(&t_gh);

	return 0;

fail:
	t_gh.gh_flags |= GL_NOCACHE;
	gfs2_glock_dq_uninit(&t_gh);

	return error;
}

void gfs2_statfs_change_in(struct gfs2_statfs_change_host *sc, const void *buf)
{
	const struct gfs2_statfs_change *str = buf;

	sc->sc_total = be64_to_cpu(str->sc_total);
	sc->sc_free = be64_to_cpu(str->sc_free);
	sc->sc_dinodes = be64_to_cpu(str->sc_dinodes);
}

static void gfs2_statfs_change_out(const struct gfs2_statfs_change_host *sc, void *buf)
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
	struct gfs2_inode *l_ip = GFS2_I(sdp->sd_sc_inode);
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct buffer_head *m_bh, *l_bh;
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
		error = gfs2_meta_inode_buffer(l_ip, &l_bh);
		if (error)
			goto out_m_bh;

		spin_lock(&sdp->sd_statfs_spin);
		gfs2_statfs_change_in(m_sc, m_bh->b_data +
				      sizeof(struct gfs2_dinode));
		gfs2_statfs_change_in(l_sc, l_bh->b_data +
				      sizeof(struct gfs2_dinode));
		spin_unlock(&sdp->sd_statfs_spin);

		brelse(l_bh);
	}

out_m_bh:
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
	struct buffer_head *l_bh;
	s64 x, y;
	int need_sync = 0;
	int error;

	error = gfs2_meta_inode_buffer(l_ip, &l_bh);
	if (error)
		return;

	gfs2_trans_add_bh(l_ip->i_gl, l_bh, 1);

	spin_lock(&sdp->sd_statfs_spin);
	l_sc->sc_total += total;
	l_sc->sc_free += free;
	l_sc->sc_dinodes += dinodes;
	gfs2_statfs_change_out(l_sc, l_bh->b_data + sizeof(struct gfs2_dinode));
	if (sdp->sd_args.ar_statfs_percent) {
		x = 100 * l_sc->sc_free;
		y = m_sc->sc_free * sdp->sd_args.ar_statfs_percent;
		if (x >= y || x <= -y)
			need_sync = 1;
	}
	spin_unlock(&sdp->sd_statfs_spin);

	brelse(l_bh);
	if (need_sync)
		gfs2_wake_up_statfs(sdp);
}

void update_statfs(struct gfs2_sbd *sdp, struct buffer_head *m_bh,
		   struct buffer_head *l_bh)
{
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);
	struct gfs2_inode *l_ip = GFS2_I(sdp->sd_sc_inode);
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;

	gfs2_trans_add_bh(l_ip->i_gl, l_bh, 1);

	spin_lock(&sdp->sd_statfs_spin);
	m_sc->sc_total += l_sc->sc_total;
	m_sc->sc_free += l_sc->sc_free;
	m_sc->sc_dinodes += l_sc->sc_dinodes;
	memset(l_sc, 0, sizeof(struct gfs2_statfs_change));
	memset(l_bh->b_data + sizeof(struct gfs2_dinode),
	       0, sizeof(struct gfs2_statfs_change));
	spin_unlock(&sdp->sd_statfs_spin);

	gfs2_trans_add_bh(m_ip->i_gl, m_bh, 1);
	gfs2_statfs_change_out(m_sc, m_bh->b_data + sizeof(struct gfs2_dinode));
}

int gfs2_statfs_sync(struct super_block *sb, int type)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);
	struct gfs2_inode *l_ip = GFS2_I(sdp->sd_sc_inode);
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct gfs2_holder gh;
	struct buffer_head *m_bh, *l_bh;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, GL_NOCACHE,
				   &gh);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(m_ip, &m_bh);
	if (error)
		goto out;

	spin_lock(&sdp->sd_statfs_spin);
	gfs2_statfs_change_in(m_sc, m_bh->b_data +
			      sizeof(struct gfs2_dinode));
	if (!l_sc->sc_total && !l_sc->sc_free && !l_sc->sc_dinodes) {
		spin_unlock(&sdp->sd_statfs_spin);
		goto out_bh;
	}
	spin_unlock(&sdp->sd_statfs_spin);

	error = gfs2_meta_inode_buffer(l_ip, &l_bh);
	if (error)
		goto out_bh;

	error = gfs2_trans_begin(sdp, 2 * RES_DINODE, 0);
	if (error)
		goto out_bh2;

	update_statfs(sdp, m_bh, l_bh);
	sdp->sd_statfs_force_sync = 0;

	gfs2_trans_end(sdp);

out_bh2:
	brelse(l_bh);
out_bh:
	brelse(m_bh);
out:
	gfs2_glock_dq_uninit(&gh);
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
 * @state: the state to put the transaction lock into
 * @t_gh: the hold on the transaction lock
 *
 * Returns: errno
 */

static int gfs2_lock_fs_check_clean(struct gfs2_sbd *sdp,
				    struct gfs2_holder *t_gh)
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

	error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_DEFERRED,
				   GL_NOCACHE, t_gh);

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		error = gfs2_jdesc_check(jd);
		if (error)
			break;
		error = gfs2_find_jhead(jd, &lh);
		if (error)
			break;
		if (!(lh.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
			error = -EBUSY;
			break;
		}
	}

	if (error)
		gfs2_glock_dq_uninit(t_gh);

out:
	while (!list_empty(&list)) {
		lfcc = list_entry(list.next, struct lfcc, list);
		list_del(&lfcc->list);
		gfs2_glock_dq_uninit(&lfcc->gh);
		kfree(lfcc);
	}
	return error;
}

/**
 * gfs2_freeze_fs - freezes the file system
 * @sdp: the file system
 *
 * This function flushes data and meta data for all machines by
 * aquiring the transaction log exclusively.  All journals are
 * ensured to be in a clean state as well.
 *
 * Returns: errno
 */

int gfs2_freeze_fs(struct gfs2_sbd *sdp)
{
	int error = 0;

	mutex_lock(&sdp->sd_freeze_lock);

	if (!sdp->sd_freeze_count++) {
		error = gfs2_lock_fs_check_clean(sdp, &sdp->sd_freeze_gh);
		if (error)
			sdp->sd_freeze_count--;
	}

	mutex_unlock(&sdp->sd_freeze_lock);

	return error;
}

/**
 * gfs2_unfreeze_fs - unfreezes the file system
 * @sdp: the file system
 *
 * This function allows the file system to proceed by unlocking
 * the exclusively held transaction lock.  Other GFS2 nodes are
 * now free to acquire the lock shared and go on with their lives.
 *
 */

void gfs2_unfreeze_fs(struct gfs2_sbd *sdp)
{
	mutex_lock(&sdp->sd_freeze_lock);

	if (sdp->sd_freeze_count && !--sdp->sd_freeze_count)
		gfs2_glock_dq_uninit(&sdp->sd_freeze_gh);

	mutex_unlock(&sdp->sd_freeze_lock);
}


/**
 * gfs2_write_inode - Make sure the inode is stable on the disk
 * @inode: The inode
 * @sync: synchronous write flag
 *
 * Returns: errno
 */

static int gfs2_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_holder gh;
	struct buffer_head *bh;
	struct timespec atime;
	struct gfs2_dinode *di;
	int ret = 0;

	/* Check this is a "normal" inode, etc */
	if (current->flags & PF_MEMALLOC)
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
	if (wbc->sync_mode == WB_SYNC_ALL)
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

	flush_workqueue(gfs2_delete_workqueue);
	gfs2_quota_sync(sdp->sd_vfs, 0, 1);
	gfs2_statfs_sync(sdp->sd_vfs, 0);

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

static int gfs2_umount_recovery_wait(void *word)
{
	schedule();
	return 0;
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
	struct gfs2_jdesc *jd;

	/*  Unfreeze the filesystem, if we need to  */

	mutex_lock(&sdp->sd_freeze_lock);
	if (sdp->sd_freeze_count)
		gfs2_glock_dq_uninit(&sdp->sd_freeze_gh);
	mutex_unlock(&sdp->sd_freeze_lock);

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
			    gfs2_umount_recovery_wait, TASK_UNINTERRUPTIBLE);
		goto restart;
	}
	spin_unlock(&sdp->sd_jindex_spin);

	kthread_stop(sdp->sd_quotad_process);
	kthread_stop(sdp->sd_logd_process);

	if (!(sb->s_flags & MS_RDONLY)) {
		error = gfs2_make_fs_ro(sdp);
		if (error)
			gfs2_io_error(sdp);
	}
	/*  At this point, we're through modifying the disk  */

	/*  Release stuff  */

	iput(sdp->sd_jindex);
	iput(sdp->sd_statfs_inode);
	iput(sdp->sd_rindex);
	iput(sdp->sd_quota_inode);

	gfs2_glock_put(sdp->sd_rename_gl);
	gfs2_glock_put(sdp->sd_trans_gl);

	if (!sdp->sd_args.ar_spectator) {
		gfs2_glock_dq_uninit(&sdp->sd_journal_gh);
		gfs2_glock_dq_uninit(&sdp->sd_jinode_gh);
		gfs2_glock_dq_uninit(&sdp->sd_sc_gh);
		gfs2_glock_dq_uninit(&sdp->sd_qc_gh);
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
 * gfs2_sync_fs - sync the filesystem
 * @sb: the superblock
 *
 * Flushes the log to disk.
 */

static int gfs2_sync_fs(struct super_block *sb, int wait)
{
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
	struct gfs2_args args = sdp->sd_args; /* Default to current settings */
	struct gfs2_tune *gt = &sdp->sd_tune;
	int error;

	spin_lock(&gt->gt_spin);
	args.ar_commit = gt->gt_logd_secs;
	args.ar_quota_quantum = gt->gt_quota_quantum;
	if (gt->gt_statfs_slow)
		args.ar_statfs_quantum = 0;
	else
		args.ar_statfs_quantum = gt->gt_statfs_quantum;
	spin_unlock(&gt->gt_spin);
	error = gfs2_mount_args(&args, data);
	if (error)
		return error;

	/* Not allowed to change locking details */
	if (strcmp(args.ar_lockproto, sdp->sd_args.ar_lockproto) ||
	    strcmp(args.ar_locktable, sdp->sd_args.ar_locktable) ||
	    strcmp(args.ar_hostdata, sdp->sd_args.ar_hostdata))
		return -EINVAL;

	/* Some flags must not be changed */
	if (args_neq(&args, &sdp->sd_args, spectator) ||
	    args_neq(&args, &sdp->sd_args, localflocks) ||
	    args_neq(&args, &sdp->sd_args, meta))
		return -EINVAL;

	if (sdp->sd_args.ar_spectator)
		*flags |= MS_RDONLY;

	if ((sb->s_flags ^ *flags) & MS_RDONLY) {
		if (*flags & MS_RDONLY)
			error = gfs2_make_fs_ro(sdp);
		else
			error = gfs2_make_fs_rw(sdp);
		if (error)
			return error;
	}

	sdp->sd_args = args;
	if (sdp->sd_args.ar_posix_acl)
		sb->s_flags |= MS_POSIXACL;
	else
		sb->s_flags &= ~MS_POSIXACL;
	if (sdp->sd_args.ar_nobarrier)
		set_bit(SDF_NOBARRIERS, &sdp->sd_flags);
	else
		clear_bit(SDF_NOBARRIERS, &sdp->sd_flags);
	spin_lock(&gt->gt_spin);
	gt->gt_logd_secs = args.ar_commit;
	gt->gt_quota_quantum = args.ar_quota_quantum;
	if (args.ar_statfs_quantum) {
		gt->gt_statfs_slow = 0;
		gt->gt_statfs_quantum = args.ar_statfs_quantum;
	}
	else {
		gt->gt_statfs_slow = 1;
		gt->gt_statfs_quantum = 30;
	}
	spin_unlock(&gt->gt_spin);

	gfs2_online_uevent(sdp);
	return 0;
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

static int gfs2_drop_inode(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);

	if (inode->i_nlink) {
		struct gfs2_glock *gl = ip->i_iopen_gh.gh_gl;
		if (gl && test_bit(GLF_DEMOTE, &gl->gl_flags))
			clear_nlink(inode);
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
 * @mnt: vfsmount
 *
 * Returns: 0 on success or error code
 */

static int gfs2_show_options(struct seq_file *s, struct vfsmount *mnt)
{
	struct gfs2_sbd *sdp = mnt->mnt_sb->s_fs_info;
	struct gfs2_args *args = &sdp->sd_args;
	int val;

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
	if (args->ar_localflocks)
		seq_printf(s, ",localflocks");
	if (args->ar_debug)
		seq_printf(s, ",debug");
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
	if (args->ar_discard)
		seq_printf(s, ",discard");
	val = sdp->sd_tune.gt_logd_secs;
	if (val != 30)
		seq_printf(s, ",commit=%d", val);
	val = sdp->sd_tune.gt_statfs_quantum;
	if (val != 30)
		seq_printf(s, ",statfs_quantum=%d", val);
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
		seq_printf(s, ",nobarrier");
	if (test_bit(SDF_DEMOTE, &sdp->sd_flags))
		seq_printf(s, ",demote_interface_used");
	return 0;
}

/*
 * We have to (at the moment) hold the inodes main lock to cover
 * the gap between unlocking the shared lock on the iopen lock and
 * taking the exclusive lock. I'd rather do a shared -> exclusive
 * conversion on the iopen lock, but we can change that later. This
 * is safe, just less efficient.
 */

static void gfs2_evict_inode(struct inode *inode)
{
	struct gfs2_sbd *sdp = inode->i_sb->s_fs_info;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	int error;

	if (inode->i_nlink)
		goto out;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (unlikely(error)) {
		gfs2_glock_dq_uninit(&ip->i_iopen_gh);
		goto out;
	}

	error = gfs2_check_blk_type(sdp, ip->i_no_addr, GFS2_BLKST_UNLINKED);
	if (error)
		goto out_truncate;

	ip->i_iopen_gh.gh_flags |= GL_NOCACHE;
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
	if (error && error != GLR_TRYFAILED && error != -EROFS)
		fs_warn(sdp, "gfs2_evict_inode: %d\n", error);
out:
	truncate_inode_pages(&inode->i_data, 0);
	end_writeback(inode);

	ip->i_gl->gl_object = NULL;
	gfs2_glock_put(ip->i_gl);
	ip->i_gl = NULL;
	if (ip->i_iopen_gh.gh_gl) {
		ip->i_iopen_gh.gh_gl->gl_object = NULL;
		gfs2_glock_dq_uninit(&ip->i_iopen_gh);
	}
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

static void gfs2_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	INIT_LIST_HEAD(&inode->i_dentry);
	kmem_cache_free(gfs2_inode_cachep, inode);
}

static void gfs2_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, gfs2_i_callback);
}

const struct super_operations gfs2_super_ops = {
	.alloc_inode		= gfs2_alloc_inode,
	.destroy_inode		= gfs2_destroy_inode,
	.write_inode		= gfs2_write_inode,
	.evict_inode		= gfs2_evict_inode,
	.put_super		= gfs2_put_super,
	.sync_fs		= gfs2_sync_fs,
	.freeze_fs 		= gfs2_freeze,
	.unfreeze_fs		= gfs2_unfreeze,
	.statfs			= gfs2_statfs,
	.remount_fs		= gfs2_remount_fs,
	.drop_inode		= gfs2_drop_inode,
	.show_options		= gfs2_show_options,
};

