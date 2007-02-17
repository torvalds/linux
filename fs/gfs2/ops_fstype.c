/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
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
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "daemon.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "lm.h"
#include "mount.h"
#include "ops_export.h"
#include "ops_fstype.h"
#include "ops_super.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "sys.h"
#include "util.h"

#define DO 0
#define UNDO 1

extern struct dentry_operations gfs2_dops;

static struct gfs2_sbd *init_sbd(struct super_block *sb)
{
	struct gfs2_sbd *sdp;

	sdp = kzalloc(sizeof(struct gfs2_sbd), GFP_KERNEL);
	if (!sdp)
		return NULL;

	sb->s_fs_info = sdp;
	sdp->sd_vfs = sb;

	gfs2_tune_init(&sdp->sd_tune);

	INIT_LIST_HEAD(&sdp->sd_reclaim_list);
	spin_lock_init(&sdp->sd_reclaim_lock);
	init_waitqueue_head(&sdp->sd_reclaim_wq);

	mutex_init(&sdp->sd_inum_mutex);
	spin_lock_init(&sdp->sd_statfs_spin);
	mutex_init(&sdp->sd_statfs_mutex);

	spin_lock_init(&sdp->sd_rindex_spin);
	mutex_init(&sdp->sd_rindex_mutex);
	INIT_LIST_HEAD(&sdp->sd_rindex_list);
	INIT_LIST_HEAD(&sdp->sd_rindex_mru_list);
	INIT_LIST_HEAD(&sdp->sd_rindex_recent_list);

	INIT_LIST_HEAD(&sdp->sd_jindex_list);
	spin_lock_init(&sdp->sd_jindex_spin);
	mutex_init(&sdp->sd_jindex_mutex);

	INIT_LIST_HEAD(&sdp->sd_quota_list);
	spin_lock_init(&sdp->sd_quota_spin);
	mutex_init(&sdp->sd_quota_mutex);

	spin_lock_init(&sdp->sd_log_lock);

	INIT_LIST_HEAD(&sdp->sd_log_le_gl);
	INIT_LIST_HEAD(&sdp->sd_log_le_buf);
	INIT_LIST_HEAD(&sdp->sd_log_le_revoke);
	INIT_LIST_HEAD(&sdp->sd_log_le_rg);
	INIT_LIST_HEAD(&sdp->sd_log_le_databuf);

	mutex_init(&sdp->sd_log_reserve_mutex);
	INIT_LIST_HEAD(&sdp->sd_ail1_list);
	INIT_LIST_HEAD(&sdp->sd_ail2_list);

	init_rwsem(&sdp->sd_log_flush_lock);
	INIT_LIST_HEAD(&sdp->sd_log_flush_list);

	INIT_LIST_HEAD(&sdp->sd_revoke_list);

	mutex_init(&sdp->sd_freeze_lock);

	return sdp;
}

static void init_vfs(struct super_block *sb, unsigned noatime)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;

	sb->s_magic = GFS2_MAGIC;
	sb->s_op = &gfs2_super_ops;
	sb->s_export_op = &gfs2_export_ops;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	if (sb->s_flags & (MS_NOATIME | MS_NODIRATIME))
		set_bit(noatime, &sdp->sd_flags);

	/* Don't let the VFS update atimes.  GFS2 handles this itself. */
	sb->s_flags |= MS_NOATIME | MS_NODIRATIME;
}

static int init_names(struct gfs2_sbd *sdp, int silent)
{
	struct page *page;
	char *proto, *table;
	int error = 0;

	proto = sdp->sd_args.ar_lockproto;
	table = sdp->sd_args.ar_locktable;

	/*  Try to autodetect  */

	if (!proto[0] || !table[0]) {
		struct gfs2_sb *sb;
		page = gfs2_read_super(sdp->sd_vfs, GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift);
		if (!page)
			return -ENOBUFS;
		sb = kmap(page);
		gfs2_sb_in(&sdp->sd_sb, sb);
		kunmap(page);
		__free_page(page);

		error = gfs2_check_sb(sdp, &sdp->sd_sb, silent);
		if (error)
			goto out;

		if (!proto[0])
			proto = sdp->sd_sb.sb_lockproto;
		if (!table[0])
			table = sdp->sd_sb.sb_locktable;
	}

	if (!table[0])
		table = sdp->sd_vfs->s_id;

	snprintf(sdp->sd_proto_name, GFS2_FSNAME_LEN, "%s", proto);
	snprintf(sdp->sd_table_name, GFS2_FSNAME_LEN, "%s", table);

out:
	return error;
}

static int init_locking(struct gfs2_sbd *sdp, struct gfs2_holder *mount_gh,
			int undo)
{
	struct task_struct *p;
	int error = 0;

	if (undo)
		goto fail_trans;

	p = kthread_run(gfs2_scand, sdp, "gfs2_scand");
	error = IS_ERR(p);
	if (error) {
		fs_err(sdp, "can't start scand thread: %d\n", error);
		return error;
	}
	sdp->sd_scand_process = p;

	for (sdp->sd_glockd_num = 0;
	     sdp->sd_glockd_num < sdp->sd_args.ar_num_glockd;
	     sdp->sd_glockd_num++) {
		p = kthread_run(gfs2_glockd, sdp, "gfs2_glockd");
		error = IS_ERR(p);
		if (error) {
			fs_err(sdp, "can't start glockd thread: %d\n", error);
			goto fail;
		}
		sdp->sd_glockd_process[sdp->sd_glockd_num] = p;
	}

	error = gfs2_glock_nq_num(sdp,
				  GFS2_MOUNT_LOCK, &gfs2_nondisk_glops,
				  LM_ST_EXCLUSIVE, LM_FLAG_NOEXP | GL_NOCACHE,
				  mount_gh);
	if (error) {
		fs_err(sdp, "can't acquire mount glock: %d\n", error);
		goto fail;
	}

	error = gfs2_glock_nq_num(sdp,
				  GFS2_LIVE_LOCK, &gfs2_nondisk_glops,
				  LM_ST_SHARED,
				  LM_FLAG_NOEXP | GL_EXACT,
				  &sdp->sd_live_gh);
	if (error) {
		fs_err(sdp, "can't acquire live glock: %d\n", error);
		goto fail_mount;
	}

	error = gfs2_glock_get(sdp, GFS2_RENAME_LOCK, &gfs2_nondisk_glops,
			       CREATE, &sdp->sd_rename_gl);
	if (error) {
		fs_err(sdp, "can't create rename glock: %d\n", error);
		goto fail_live;
	}

	error = gfs2_glock_get(sdp, GFS2_TRANS_LOCK, &gfs2_trans_glops,
			       CREATE, &sdp->sd_trans_gl);
	if (error) {
		fs_err(sdp, "can't create transaction glock: %d\n", error);
		goto fail_rename;
	}
	set_bit(GLF_STICKY, &sdp->sd_trans_gl->gl_flags);

	return 0;

fail_trans:
	gfs2_glock_put(sdp->sd_trans_gl);
fail_rename:
	gfs2_glock_put(sdp->sd_rename_gl);
fail_live:
	gfs2_glock_dq_uninit(&sdp->sd_live_gh);
fail_mount:
	gfs2_glock_dq_uninit(mount_gh);
fail:
	while (sdp->sd_glockd_num--)
		kthread_stop(sdp->sd_glockd_process[sdp->sd_glockd_num]);

	kthread_stop(sdp->sd_scand_process);
	return error;
}

static struct inode *gfs2_lookup_root(struct super_block *sb,
				      struct gfs2_inum_host *inum)
{
	return gfs2_inode_lookup(sb, inum, DT_DIR);
}

static int init_sb(struct gfs2_sbd *sdp, int silent, int undo)
{
	struct super_block *sb = sdp->sd_vfs;
	struct gfs2_holder sb_gh;
	struct gfs2_inum_host *inum;
	struct inode *inode;
	int error = 0;

	if (undo) {
		if (sb->s_root) {
			dput(sb->s_root);
			sb->s_root = NULL;
		}
		return 0;
	}

	error = gfs2_glock_nq_num(sdp, GFS2_SB_LOCK, &gfs2_meta_glops,
				 LM_ST_SHARED, 0, &sb_gh);
	if (error) {
		fs_err(sdp, "can't acquire superblock glock: %d\n", error);
		return error;
	}

	error = gfs2_read_sb(sdp, sb_gh.gh_gl, silent);
	if (error) {
		fs_err(sdp, "can't read superblock: %d\n", error);
		goto out;
	}

	/* Set up the buffer cache and SB for real */
	if (sdp->sd_sb.sb_bsize < bdev_hardsect_size(sb->s_bdev)) {
		error = -EINVAL;
		fs_err(sdp, "FS block size (%u) is too small for device "
		       "block size (%u)\n",
		       sdp->sd_sb.sb_bsize, bdev_hardsect_size(sb->s_bdev));
		goto out;
	}
	if (sdp->sd_sb.sb_bsize > PAGE_SIZE) {
		error = -EINVAL;
		fs_err(sdp, "FS block size (%u) is too big for machine "
		       "page size (%u)\n",
		       sdp->sd_sb.sb_bsize, (unsigned int)PAGE_SIZE);
		goto out;
	}
	sb_set_blocksize(sb, sdp->sd_sb.sb_bsize);

	/* Get the root inode */
	inum = &sdp->sd_sb.sb_root_dir;
	if (sb->s_type == &gfs2meta_fs_type)
		inum = &sdp->sd_sb.sb_master_dir;
	inode = gfs2_lookup_root(sb, inum);
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		fs_err(sdp, "can't read in root inode: %d\n", error);
		goto out;
	}

	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root) {
		fs_err(sdp, "can't get root dentry\n");
		error = -ENOMEM;
		iput(inode);
	}
	sb->s_root->d_op = &gfs2_dops;
out:
	gfs2_glock_dq_uninit(&sb_gh);
	return error;
}

static int init_journal(struct gfs2_sbd *sdp, int undo)
{
	struct gfs2_holder ji_gh;
	struct task_struct *p;
	struct gfs2_inode *ip;
	int jindex = 1;
	int error = 0;

	if (undo) {
		jindex = 0;
		goto fail_recoverd;
	}

	sdp->sd_jindex = gfs2_lookup_simple(sdp->sd_master_dir, "jindex");
	if (IS_ERR(sdp->sd_jindex)) {
		fs_err(sdp, "can't lookup journal index: %d\n", error);
		return PTR_ERR(sdp->sd_jindex);
	}
	ip = GFS2_I(sdp->sd_jindex);
	set_bit(GLF_STICKY, &ip->i_gl->gl_flags);

	/* Load in the journal index special file */

	error = gfs2_jindex_hold(sdp, &ji_gh);
	if (error) {
		fs_err(sdp, "can't read journal index: %d\n", error);
		goto fail;
	}

	error = -EINVAL;
	if (!gfs2_jindex_size(sdp)) {
		fs_err(sdp, "no journals!\n");
		goto fail_jindex;
	}

	if (sdp->sd_args.ar_spectator) {
		sdp->sd_jdesc = gfs2_jdesc_find(sdp, 0);
		sdp->sd_log_blks_free = sdp->sd_jdesc->jd_blocks;
	} else {
		if (sdp->sd_lockstruct.ls_jid >= gfs2_jindex_size(sdp)) {
			fs_err(sdp, "can't mount journal #%u\n",
			       sdp->sd_lockstruct.ls_jid);
			fs_err(sdp, "there are only %u journals (0 - %u)\n",
			       gfs2_jindex_size(sdp),
			       gfs2_jindex_size(sdp) - 1);
			goto fail_jindex;
		}
		sdp->sd_jdesc = gfs2_jdesc_find(sdp, sdp->sd_lockstruct.ls_jid);

		error = gfs2_glock_nq_num(sdp, sdp->sd_lockstruct.ls_jid,
					  &gfs2_journal_glops,
					  LM_ST_EXCLUSIVE, LM_FLAG_NOEXP,
					  &sdp->sd_journal_gh);
		if (error) {
			fs_err(sdp, "can't acquire journal glock: %d\n", error);
			goto fail_jindex;
		}

		ip = GFS2_I(sdp->sd_jdesc->jd_inode);
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED,
					   LM_FLAG_NOEXP | GL_EXACT,
					   &sdp->sd_jinode_gh);
		if (error) {
			fs_err(sdp, "can't acquire journal inode glock: %d\n",
			       error);
			goto fail_journal_gh;
		}

		error = gfs2_jdesc_check(sdp->sd_jdesc);
		if (error) {
			fs_err(sdp, "my journal (%u) is bad: %d\n",
			       sdp->sd_jdesc->jd_jid, error);
			goto fail_jinode_gh;
		}
		sdp->sd_log_blks_free = sdp->sd_jdesc->jd_blocks;
	}

	if (sdp->sd_lockstruct.ls_first) {
		unsigned int x;
		for (x = 0; x < sdp->sd_journals; x++) {
			error = gfs2_recover_journal(gfs2_jdesc_find(sdp, x));
			if (error) {
				fs_err(sdp, "error recovering journal %u: %d\n",
				       x, error);
				goto fail_jinode_gh;
			}
		}

		gfs2_lm_others_may_mount(sdp);
	} else if (!sdp->sd_args.ar_spectator) {
		error = gfs2_recover_journal(sdp->sd_jdesc);
		if (error) {
			fs_err(sdp, "error recovering my journal: %d\n", error);
			goto fail_jinode_gh;
		}
	}

	set_bit(SDF_JOURNAL_CHECKED, &sdp->sd_flags);
	gfs2_glock_dq_uninit(&ji_gh);
	jindex = 0;

	p = kthread_run(gfs2_recoverd, sdp, "gfs2_recoverd");
	error = IS_ERR(p);
	if (error) {
		fs_err(sdp, "can't start recoverd thread: %d\n", error);
		goto fail_jinode_gh;
	}
	sdp->sd_recoverd_process = p;

	return 0;

fail_recoverd:
	kthread_stop(sdp->sd_recoverd_process);
fail_jinode_gh:
	if (!sdp->sd_args.ar_spectator)
		gfs2_glock_dq_uninit(&sdp->sd_jinode_gh);
fail_journal_gh:
	if (!sdp->sd_args.ar_spectator)
		gfs2_glock_dq_uninit(&sdp->sd_journal_gh);
fail_jindex:
	gfs2_jindex_free(sdp);
	if (jindex)
		gfs2_glock_dq_uninit(&ji_gh);
fail:
	iput(sdp->sd_jindex);
	return error;
}


static int init_inodes(struct gfs2_sbd *sdp, int undo)
{
	int error = 0;
	struct gfs2_inode *ip;
	struct inode *inode;

	if (undo)
		goto fail_qinode;

	inode = gfs2_lookup_root(sdp->sd_vfs, &sdp->sd_sb.sb_master_dir);
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		fs_err(sdp, "can't read in master directory: %d\n", error);
		goto fail;
	}
	sdp->sd_master_dir = inode;

	error = init_journal(sdp, undo);
	if (error)
		goto fail_master;

	/* Read in the master inode number inode */
	sdp->sd_inum_inode = gfs2_lookup_simple(sdp->sd_master_dir, "inum");
	if (IS_ERR(sdp->sd_inum_inode)) {
		error = PTR_ERR(sdp->sd_inum_inode);
		fs_err(sdp, "can't read in inum inode: %d\n", error);
		goto fail_journal;
	}


	/* Read in the master statfs inode */
	sdp->sd_statfs_inode = gfs2_lookup_simple(sdp->sd_master_dir, "statfs");
	if (IS_ERR(sdp->sd_statfs_inode)) {
		error = PTR_ERR(sdp->sd_statfs_inode);
		fs_err(sdp, "can't read in statfs inode: %d\n", error);
		goto fail_inum;
	}

	/* Read in the resource index inode */
	sdp->sd_rindex = gfs2_lookup_simple(sdp->sd_master_dir, "rindex");
	if (IS_ERR(sdp->sd_rindex)) {
		error = PTR_ERR(sdp->sd_rindex);
		fs_err(sdp, "can't get resource index inode: %d\n", error);
		goto fail_statfs;
	}
	ip = GFS2_I(sdp->sd_rindex);
	set_bit(GLF_STICKY, &ip->i_gl->gl_flags);
	sdp->sd_rindex_vn = ip->i_gl->gl_vn - 1;

	/* Read in the quota inode */
	sdp->sd_quota_inode = gfs2_lookup_simple(sdp->sd_master_dir, "quota");
	if (IS_ERR(sdp->sd_quota_inode)) {
		error = PTR_ERR(sdp->sd_quota_inode);
		fs_err(sdp, "can't get quota file inode: %d\n", error);
		goto fail_rindex;
	}
	return 0;

fail_qinode:
	iput(sdp->sd_quota_inode);
fail_rindex:
	gfs2_clear_rgrpd(sdp);
	iput(sdp->sd_rindex);
fail_statfs:
	iput(sdp->sd_statfs_inode);
fail_inum:
	iput(sdp->sd_inum_inode);
fail_journal:
	init_journal(sdp, UNDO);
fail_master:
	iput(sdp->sd_master_dir);
fail:
	return error;
}

static int init_per_node(struct gfs2_sbd *sdp, int undo)
{
	struct inode *pn = NULL;
	char buf[30];
	int error = 0;
	struct gfs2_inode *ip;

	if (sdp->sd_args.ar_spectator)
		return 0;

	if (undo)
		goto fail_qc_gh;

	pn = gfs2_lookup_simple(sdp->sd_master_dir, "per_node");
	if (IS_ERR(pn)) {
		error = PTR_ERR(pn);
		fs_err(sdp, "can't find per_node directory: %d\n", error);
		return error;
	}

	sprintf(buf, "inum_range%u", sdp->sd_jdesc->jd_jid);
	sdp->sd_ir_inode = gfs2_lookup_simple(pn, buf);
	if (IS_ERR(sdp->sd_ir_inode)) {
		error = PTR_ERR(sdp->sd_ir_inode);
		fs_err(sdp, "can't find local \"ir\" file: %d\n", error);
		goto fail;
	}

	sprintf(buf, "statfs_change%u", sdp->sd_jdesc->jd_jid);
	sdp->sd_sc_inode = gfs2_lookup_simple(pn, buf);
	if (IS_ERR(sdp->sd_sc_inode)) {
		error = PTR_ERR(sdp->sd_sc_inode);
		fs_err(sdp, "can't find local \"sc\" file: %d\n", error);
		goto fail_ir_i;
	}

	sprintf(buf, "quota_change%u", sdp->sd_jdesc->jd_jid);
	sdp->sd_qc_inode = gfs2_lookup_simple(pn, buf);
	if (IS_ERR(sdp->sd_qc_inode)) {
		error = PTR_ERR(sdp->sd_qc_inode);
		fs_err(sdp, "can't find local \"qc\" file: %d\n", error);
		goto fail_ut_i;
	}

	iput(pn);
	pn = NULL;

	ip = GFS2_I(sdp->sd_ir_inode);
	error = gfs2_glock_nq_init(ip->i_gl,
				   LM_ST_EXCLUSIVE, 0,
				   &sdp->sd_ir_gh);
	if (error) {
		fs_err(sdp, "can't lock local \"ir\" file: %d\n", error);
		goto fail_qc_i;
	}

	ip = GFS2_I(sdp->sd_sc_inode);
	error = gfs2_glock_nq_init(ip->i_gl,
				   LM_ST_EXCLUSIVE, 0,
				   &sdp->sd_sc_gh);
	if (error) {
		fs_err(sdp, "can't lock local \"sc\" file: %d\n", error);
		goto fail_ir_gh;
	}

	ip = GFS2_I(sdp->sd_qc_inode);
	error = gfs2_glock_nq_init(ip->i_gl,
				   LM_ST_EXCLUSIVE, 0,
				   &sdp->sd_qc_gh);
	if (error) {
		fs_err(sdp, "can't lock local \"qc\" file: %d\n", error);
		goto fail_ut_gh;
	}

	return 0;

fail_qc_gh:
	gfs2_glock_dq_uninit(&sdp->sd_qc_gh);
fail_ut_gh:
	gfs2_glock_dq_uninit(&sdp->sd_sc_gh);
fail_ir_gh:
	gfs2_glock_dq_uninit(&sdp->sd_ir_gh);
fail_qc_i:
	iput(sdp->sd_qc_inode);
fail_ut_i:
	iput(sdp->sd_sc_inode);
fail_ir_i:
	iput(sdp->sd_ir_inode);
fail:
	if (pn)
		iput(pn);
	return error;
}

static int init_threads(struct gfs2_sbd *sdp, int undo)
{
	struct task_struct *p;
	int error = 0;

	if (undo)
		goto fail_quotad;

	sdp->sd_log_flush_time = jiffies;
	sdp->sd_jindex_refresh_time = jiffies;

	p = kthread_run(gfs2_logd, sdp, "gfs2_logd");
	error = IS_ERR(p);
	if (error) {
		fs_err(sdp, "can't start logd thread: %d\n", error);
		return error;
	}
	sdp->sd_logd_process = p;

	sdp->sd_statfs_sync_time = jiffies;
	sdp->sd_quota_sync_time = jiffies;

	p = kthread_run(gfs2_quotad, sdp, "gfs2_quotad");
	error = IS_ERR(p);
	if (error) {
		fs_err(sdp, "can't start quotad thread: %d\n", error);
		goto fail;
	}
	sdp->sd_quotad_process = p;

	return 0;


fail_quotad:
	kthread_stop(sdp->sd_quotad_process);
fail:
	kthread_stop(sdp->sd_logd_process);
	return error;
}

/**
 * fill_super - Read in superblock
 * @sb: The VFS superblock
 * @data: Mount options
 * @silent: Don't complain if it's not a GFS2 filesystem
 *
 * Returns: errno
 */

static int fill_super(struct super_block *sb, void *data, int silent)
{
	struct gfs2_sbd *sdp;
	struct gfs2_holder mount_gh;
	int error;

	sdp = init_sbd(sb);
	if (!sdp) {
		printk(KERN_WARNING "GFS2: can't alloc struct gfs2_sbd\n");
		return -ENOMEM;
	}

	error = gfs2_mount_args(sdp, (char *)data, 0);
	if (error) {
		printk(KERN_WARNING "GFS2: can't parse mount arguments\n");
		goto fail;
	}

	init_vfs(sb, SDF_NOATIME);

	/* Set up the buffer cache and fill in some fake block size values
	   to allow us to read-in the on-disk superblock. */
	sdp->sd_sb.sb_bsize = sb_min_blocksize(sb, GFS2_BASIC_BLOCK);
	sdp->sd_sb.sb_bsize_shift = sb->s_blocksize_bits;
	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift -
                               GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;

	error = init_names(sdp, silent);
	if (error)
		goto fail;

	error = gfs2_sys_fs_add(sdp);
	if (error)
		goto fail;

	error = gfs2_lm_mount(sdp, silent);
	if (error)
		goto fail_sys;

	error = init_locking(sdp, &mount_gh, DO);
	if (error)
		goto fail_lm;

	error = init_sb(sdp, silent, DO);
	if (error)
		goto fail_locking;

	error = init_inodes(sdp, DO);
	if (error)
		goto fail_sb;

	error = init_per_node(sdp, DO);
	if (error)
		goto fail_inodes;

	error = gfs2_statfs_init(sdp);
	if (error) {
		fs_err(sdp, "can't initialize statfs subsystem: %d\n", error);
		goto fail_per_node;
	}

	error = init_threads(sdp, DO);
	if (error)
		goto fail_per_node;

	if (!(sb->s_flags & MS_RDONLY)) {
		error = gfs2_make_fs_rw(sdp);
		if (error) {
			fs_err(sdp, "can't make FS RW: %d\n", error);
			goto fail_threads;
		}
	}

	gfs2_glock_dq_uninit(&mount_gh);

	return 0;

fail_threads:
	init_threads(sdp, UNDO);
fail_per_node:
	init_per_node(sdp, UNDO);
fail_inodes:
	init_inodes(sdp, UNDO);
fail_sb:
	init_sb(sdp, 0, UNDO);
fail_locking:
	init_locking(sdp, &mount_gh, UNDO);
fail_lm:
	gfs2_gl_hash_clear(sdp, WAIT);
	gfs2_lm_unmount(sdp);
	while (invalidate_inodes(sb))
		yield();
fail_sys:
	gfs2_sys_fs_del(sdp);
fail:
	kfree(sdp);
	sb->s_fs_info = NULL;
	return error;
}

static int gfs2_get_sb(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data, struct vfsmount *mnt)
{
	struct super_block *sb;
	struct gfs2_sbd *sdp;
	int error = get_sb_bdev(fs_type, flags, dev_name, data, fill_super, mnt);
	if (error)
		goto out;
	sb = mnt->mnt_sb;
	sdp = sb->s_fs_info;
	sdp->sd_gfs2mnt = mnt;
out:
	return error;
}

static int fill_super_meta(struct super_block *sb, struct super_block *new,
			   void *data, int silent)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct inode *inode;
	int error = 0;

	new->s_fs_info = sdp;
	sdp->sd_vfs_meta = sb;

	init_vfs(new, SDF_NOATIME);

        /* Get the master inode */
	inode = igrab(sdp->sd_master_dir);

	new->s_root = d_alloc_root(inode);
	if (!new->s_root) {
		fs_err(sdp, "can't get root dentry\n");
		error = -ENOMEM;
		iput(inode);
	} else
		new->s_root->d_op = &gfs2_dops;

	return error;
}

static int set_bdev_super(struct super_block *s, void *data)
{
	s->s_bdev = data;
	s->s_dev = s->s_bdev->bd_dev;
	return 0;
}

static int test_bdev_super(struct super_block *s, void *data)
{
	return s->s_bdev == data;
}

static struct super_block* get_gfs2_sb(const char *dev_name)
{
	struct kstat stat;
	struct nameidata nd;
	struct file_system_type *fstype;
	struct super_block *sb = NULL, *s;
	struct list_head *l;
	int error;

	error = path_lookup(dev_name, LOOKUP_FOLLOW, &nd);
	if (error) {
		printk(KERN_WARNING "GFS2: path_lookup on %s returned error\n",
		       dev_name);
		goto out;
	}
	error = vfs_getattr(nd.mnt, nd.dentry, &stat);

	fstype = get_fs_type("gfs2");
	list_for_each(l, &fstype->fs_supers) {
		s = list_entry(l, struct super_block, s_instances);
		if ((S_ISBLK(stat.mode) && s->s_dev == stat.rdev) ||
		    (S_ISDIR(stat.mode) && s == nd.dentry->d_inode->i_sb)) {
			sb = s;
			goto free_nd;
		}
	}

	printk(KERN_WARNING "GFS2: Unrecognized block device or "
	       "mount point %s\n", dev_name);

free_nd:
	path_release(&nd);
out:
	return sb;
}

static int gfs2_get_sb_meta(struct file_system_type *fs_type, int flags,
			    const char *dev_name, void *data, struct vfsmount *mnt)
{
	int error = 0;
	struct super_block *sb = NULL, *new;
	struct gfs2_sbd *sdp;

	sb = get_gfs2_sb(dev_name);
	if (!sb) {
		printk(KERN_WARNING "GFS2: gfs2 mount does not exist\n");
		error = -ENOENT;
		goto error;
	}
	sdp = (struct gfs2_sbd*) sb->s_fs_info;
	if (sdp->sd_vfs_meta) {
		printk(KERN_WARNING "GFS2: gfs2meta mount already exists\n");
		error = -EBUSY;
		goto error;
	}
	down(&sb->s_bdev->bd_mount_sem);
	new = sget(fs_type, test_bdev_super, set_bdev_super, sb->s_bdev);
	up(&sb->s_bdev->bd_mount_sem);
	if (IS_ERR(new)) {
		error = PTR_ERR(new);
		goto error;
	}
	module_put(fs_type->owner);
	new->s_flags = flags;
	strlcpy(new->s_id, sb->s_id, sizeof(new->s_id));
	sb_set_blocksize(new, sb->s_blocksize);
	error = fill_super_meta(sb, new, data, flags & MS_SILENT ? 1 : 0);
	if (error) {
		up_write(&new->s_umount);
		deactivate_super(new);
		goto error;
	}

	new->s_flags |= MS_ACTIVE;

	/* Grab a reference to the gfs2 mount point */
	atomic_inc(&sdp->sd_gfs2mnt->mnt_count);
	return simple_set_mnt(mnt, new);
error:
	return error;
}

static void gfs2_kill_sb(struct super_block *sb)
{
	kill_block_super(sb);
}

static void gfs2_kill_sb_meta(struct super_block *sb)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	generic_shutdown_super(sb);
	sdp->sd_vfs_meta = NULL;
	atomic_dec(&sdp->sd_gfs2mnt->mnt_count);
}

struct file_system_type gfs2_fs_type = {
	.name = "gfs2",
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = gfs2_get_sb,
	.kill_sb = gfs2_kill_sb,
	.owner = THIS_MODULE,
};

struct file_system_type gfs2meta_fs_type = {
	.name = "gfs2meta",
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = gfs2_get_sb_meta,
	.kill_sb = gfs2_kill_sb_meta,
	.owner = THIS_MODULE,
};

