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
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/gfs2_ondisk.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "sys.h"
#include "util.h"
#include "log.h"
#include "quota.h"
#include "dir.h"

#define DO 0
#define UNDO 1

static const u32 gfs2_old_fs_formats[] = {
        0
};

static const u32 gfs2_old_multihost_formats[] = {
        0
};

/**
 * gfs2_tune_init - Fill a gfs2_tune structure with default values
 * @gt: tune
 *
 */

static void gfs2_tune_init(struct gfs2_tune *gt)
{
	spin_lock_init(&gt->gt_spin);

	gt->gt_incore_log_blocks = 1024;
	gt->gt_log_flush_secs = 60;
	gt->gt_recoverd_secs = 60;
	gt->gt_logd_secs = 1;
	gt->gt_quota_simul_sync = 64;
	gt->gt_quota_warn_period = 10;
	gt->gt_quota_scale_num = 1;
	gt->gt_quota_scale_den = 1;
	gt->gt_quota_quantum = 60;
	gt->gt_new_files_jdata = 0;
	gt->gt_max_readahead = 1 << 18;
	gt->gt_stall_secs = 600;
	gt->gt_complain_secs = 10;
	gt->gt_statfs_quantum = 30;
	gt->gt_statfs_slow = 0;
}

static struct gfs2_sbd *init_sbd(struct super_block *sb)
{
	struct gfs2_sbd *sdp;

	sdp = kzalloc(sizeof(struct gfs2_sbd), GFP_KERNEL);
	if (!sdp)
		return NULL;

	sb->s_fs_info = sdp;
	sdp->sd_vfs = sb;

	gfs2_tune_init(&sdp->sd_tune);

	mutex_init(&sdp->sd_inum_mutex);
	spin_lock_init(&sdp->sd_statfs_spin);

	spin_lock_init(&sdp->sd_rindex_spin);
	mutex_init(&sdp->sd_rindex_mutex);
	INIT_LIST_HEAD(&sdp->sd_rindex_list);
	INIT_LIST_HEAD(&sdp->sd_rindex_mru_list);

	INIT_LIST_HEAD(&sdp->sd_jindex_list);
	spin_lock_init(&sdp->sd_jindex_spin);
	mutex_init(&sdp->sd_jindex_mutex);

	INIT_LIST_HEAD(&sdp->sd_quota_list);
	mutex_init(&sdp->sd_quota_mutex);
	init_waitqueue_head(&sdp->sd_quota_wait);
	INIT_LIST_HEAD(&sdp->sd_trunc_list);
	spin_lock_init(&sdp->sd_trunc_lock);

	spin_lock_init(&sdp->sd_log_lock);

	INIT_LIST_HEAD(&sdp->sd_log_le_buf);
	INIT_LIST_HEAD(&sdp->sd_log_le_revoke);
	INIT_LIST_HEAD(&sdp->sd_log_le_rg);
	INIT_LIST_HEAD(&sdp->sd_log_le_databuf);
	INIT_LIST_HEAD(&sdp->sd_log_le_ordered);

	mutex_init(&sdp->sd_log_reserve_mutex);
	INIT_LIST_HEAD(&sdp->sd_ail1_list);
	INIT_LIST_HEAD(&sdp->sd_ail2_list);

	init_rwsem(&sdp->sd_log_flush_lock);
	atomic_set(&sdp->sd_log_in_flight, 0);
	init_waitqueue_head(&sdp->sd_log_flush_wait);

	INIT_LIST_HEAD(&sdp->sd_revoke_list);

	mutex_init(&sdp->sd_freeze_lock);

	return sdp;
}


/**
 * gfs2_check_sb - Check superblock
 * @sdp: the filesystem
 * @sb: The superblock
 * @silent: Don't print a message if the check fails
 *
 * Checks the version code of the FS is one that we understand how to
 * read and that the sizes of the various on-disk structures have not
 * changed.
 */

static int gfs2_check_sb(struct gfs2_sbd *sdp, struct gfs2_sb_host *sb, int silent)
{
	unsigned int x;

	if (sb->sb_magic != GFS2_MAGIC ||
	    sb->sb_type != GFS2_METATYPE_SB) {
		if (!silent)
			printk(KERN_WARNING "GFS2: not a GFS2 filesystem\n");
		return -EINVAL;
	}

	/*  If format numbers match exactly, we're done.  */

	if (sb->sb_fs_format == GFS2_FORMAT_FS &&
	    sb->sb_multihost_format == GFS2_FORMAT_MULTI)
		return 0;

	if (sb->sb_fs_format != GFS2_FORMAT_FS) {
		for (x = 0; gfs2_old_fs_formats[x]; x++)
			if (gfs2_old_fs_formats[x] == sb->sb_fs_format)
				break;

		if (!gfs2_old_fs_formats[x]) {
			printk(KERN_WARNING
			       "GFS2: code version (%u, %u) is incompatible "
			       "with ondisk format (%u, %u)\n",
			       GFS2_FORMAT_FS, GFS2_FORMAT_MULTI,
			       sb->sb_fs_format, sb->sb_multihost_format);
			printk(KERN_WARNING
			       "GFS2: I don't know how to upgrade this FS\n");
			return -EINVAL;
		}
	}

	if (sb->sb_multihost_format != GFS2_FORMAT_MULTI) {
		for (x = 0; gfs2_old_multihost_formats[x]; x++)
			if (gfs2_old_multihost_formats[x] ==
			    sb->sb_multihost_format)
				break;

		if (!gfs2_old_multihost_formats[x]) {
			printk(KERN_WARNING
			       "GFS2: code version (%u, %u) is incompatible "
			       "with ondisk format (%u, %u)\n",
			       GFS2_FORMAT_FS, GFS2_FORMAT_MULTI,
			       sb->sb_fs_format, sb->sb_multihost_format);
			printk(KERN_WARNING
			       "GFS2: I don't know how to upgrade this FS\n");
			return -EINVAL;
		}
	}

	if (!sdp->sd_args.ar_upgrade) {
		printk(KERN_WARNING
		       "GFS2: code version (%u, %u) is incompatible "
		       "with ondisk format (%u, %u)\n",
		       GFS2_FORMAT_FS, GFS2_FORMAT_MULTI,
		       sb->sb_fs_format, sb->sb_multihost_format);
		printk(KERN_INFO
		       "GFS2: Use the \"upgrade\" mount option to upgrade "
		       "the FS\n");
		printk(KERN_INFO "GFS2: See the manual for more details\n");
		return -EINVAL;
	}

	return 0;
}

static void end_bio_io_page(struct bio *bio, int error)
{
	struct page *page = bio->bi_private;

	if (!error)
		SetPageUptodate(page);
	else
		printk(KERN_WARNING "gfs2: error %d reading superblock\n", error);
	unlock_page(page);
}

static void gfs2_sb_in(struct gfs2_sb_host *sb, const void *buf)
{
	const struct gfs2_sb *str = buf;

	sb->sb_magic = be32_to_cpu(str->sb_header.mh_magic);
	sb->sb_type = be32_to_cpu(str->sb_header.mh_type);
	sb->sb_format = be32_to_cpu(str->sb_header.mh_format);
	sb->sb_fs_format = be32_to_cpu(str->sb_fs_format);
	sb->sb_multihost_format = be32_to_cpu(str->sb_multihost_format);
	sb->sb_bsize = be32_to_cpu(str->sb_bsize);
	sb->sb_bsize_shift = be32_to_cpu(str->sb_bsize_shift);
	sb->sb_master_dir.no_addr = be64_to_cpu(str->sb_master_dir.no_addr);
	sb->sb_master_dir.no_formal_ino = be64_to_cpu(str->sb_master_dir.no_formal_ino);
	sb->sb_root_dir.no_addr = be64_to_cpu(str->sb_root_dir.no_addr);
	sb->sb_root_dir.no_formal_ino = be64_to_cpu(str->sb_root_dir.no_formal_ino);

	memcpy(sb->sb_lockproto, str->sb_lockproto, GFS2_LOCKNAME_LEN);
	memcpy(sb->sb_locktable, str->sb_locktable, GFS2_LOCKNAME_LEN);
	memcpy(sb->sb_uuid, str->sb_uuid, 16);
}

/**
 * gfs2_read_super - Read the gfs2 super block from disk
 * @sdp: The GFS2 super block
 * @sector: The location of the super block
 * @error: The error code to return
 *
 * This uses the bio functions to read the super block from disk
 * because we want to be 100% sure that we never read cached data.
 * A super block is read twice only during each GFS2 mount and is
 * never written to by the filesystem. The first time its read no
 * locks are held, and the only details which are looked at are those
 * relating to the locking protocol. Once locking is up and working,
 * the sb is read again under the lock to establish the location of
 * the master directory (contains pointers to journals etc) and the
 * root directory.
 *
 * Returns: 0 on success or error
 */

static int gfs2_read_super(struct gfs2_sbd *sdp, sector_t sector)
{
	struct super_block *sb = sdp->sd_vfs;
	struct gfs2_sb *p;
	struct page *page;
	struct bio *bio;

	page = alloc_page(GFP_NOFS);
	if (unlikely(!page))
		return -ENOBUFS;

	ClearPageUptodate(page);
	ClearPageDirty(page);
	lock_page(page);

	bio = bio_alloc(GFP_NOFS, 1);
	bio->bi_sector = sector * (sb->s_blocksize >> 9);
	bio->bi_bdev = sb->s_bdev;
	bio_add_page(bio, page, PAGE_SIZE, 0);

	bio->bi_end_io = end_bio_io_page;
	bio->bi_private = page;
	submit_bio(READ_SYNC | (1 << BIO_RW_META), bio);
	wait_on_page_locked(page);
	bio_put(bio);
	if (!PageUptodate(page)) {
		__free_page(page);
		return -EIO;
	}
	p = kmap(page);
	gfs2_sb_in(&sdp->sd_sb, p);
	kunmap(page);
	__free_page(page);
	return 0;
}

/**
 * gfs2_read_sb - Read super block
 * @sdp: The GFS2 superblock
 * @silent: Don't print message if mount fails
 *
 */

static int gfs2_read_sb(struct gfs2_sbd *sdp, int silent)
{
	u32 hash_blocks, ind_blocks, leaf_blocks;
	u32 tmp_blocks;
	unsigned int x;
	int error;

	error = gfs2_read_super(sdp, GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift);
	if (error) {
		if (!silent)
			fs_err(sdp, "can't read superblock\n");
		return error;
	}

	error = gfs2_check_sb(sdp, &sdp->sd_sb, silent);
	if (error)
		return error;

	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift -
			       GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;
	sdp->sd_diptrs = (sdp->sd_sb.sb_bsize -
			  sizeof(struct gfs2_dinode)) / sizeof(u64);
	sdp->sd_inptrs = (sdp->sd_sb.sb_bsize -
			  sizeof(struct gfs2_meta_header)) / sizeof(u64);
	sdp->sd_jbsize = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
	sdp->sd_hash_bsize = sdp->sd_sb.sb_bsize / 2;
	sdp->sd_hash_bsize_shift = sdp->sd_sb.sb_bsize_shift - 1;
	sdp->sd_hash_ptrs = sdp->sd_hash_bsize / sizeof(u64);
	sdp->sd_qc_per_block = (sdp->sd_sb.sb_bsize -
				sizeof(struct gfs2_meta_header)) /
			        sizeof(struct gfs2_quota_change);

	/* Compute maximum reservation required to add a entry to a directory */

	hash_blocks = DIV_ROUND_UP(sizeof(u64) * (1 << GFS2_DIR_MAX_DEPTH),
			     sdp->sd_jbsize);

	ind_blocks = 0;
	for (tmp_blocks = hash_blocks; tmp_blocks > sdp->sd_diptrs;) {
		tmp_blocks = DIV_ROUND_UP(tmp_blocks, sdp->sd_inptrs);
		ind_blocks += tmp_blocks;
	}

	leaf_blocks = 2 + GFS2_DIR_MAX_DEPTH;

	sdp->sd_max_dirres = hash_blocks + ind_blocks + leaf_blocks;

	sdp->sd_heightsize[0] = sdp->sd_sb.sb_bsize -
				sizeof(struct gfs2_dinode);
	sdp->sd_heightsize[1] = sdp->sd_sb.sb_bsize * sdp->sd_diptrs;
	for (x = 2;; x++) {
		u64 space, d;
		u32 m;

		space = sdp->sd_heightsize[x - 1] * sdp->sd_inptrs;
		d = space;
		m = do_div(d, sdp->sd_inptrs);

		if (d != sdp->sd_heightsize[x - 1] || m)
			break;
		sdp->sd_heightsize[x] = space;
	}
	sdp->sd_max_height = x;
	sdp->sd_heightsize[x] = ~0;
	gfs2_assert(sdp, sdp->sd_max_height <= GFS2_MAX_META_HEIGHT);

	sdp->sd_jheightsize[0] = sdp->sd_sb.sb_bsize -
				 sizeof(struct gfs2_dinode);
	sdp->sd_jheightsize[1] = sdp->sd_jbsize * sdp->sd_diptrs;
	for (x = 2;; x++) {
		u64 space, d;
		u32 m;

		space = sdp->sd_jheightsize[x - 1] * sdp->sd_inptrs;
		d = space;
		m = do_div(d, sdp->sd_inptrs);

		if (d != sdp->sd_jheightsize[x - 1] || m)
			break;
		sdp->sd_jheightsize[x] = space;
	}
	sdp->sd_max_jheight = x;
	sdp->sd_jheightsize[x] = ~0;
	gfs2_assert(sdp, sdp->sd_max_jheight <= GFS2_MAX_META_HEIGHT);

	return 0;
}

static int init_names(struct gfs2_sbd *sdp, int silent)
{
	char *proto, *table;
	int error = 0;

	proto = sdp->sd_args.ar_lockproto;
	table = sdp->sd_args.ar_locktable;

	/*  Try to autodetect  */

	if (!proto[0] || !table[0]) {
		error = gfs2_read_super(sdp, GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift);
		if (error)
			return error;

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

	strlcpy(sdp->sd_proto_name, proto, GFS2_FSNAME_LEN);
	strlcpy(sdp->sd_table_name, table, GFS2_FSNAME_LEN);

	table = sdp->sd_table_name;
	while ((table = strchr(table, '/')))
		*table = '_';

out:
	return error;
}

static int init_locking(struct gfs2_sbd *sdp, struct gfs2_holder *mount_gh,
			int undo)
{
	int error = 0;

	if (undo)
		goto fail_trans;

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
	return error;
}

static int gfs2_lookup_root(struct super_block *sb, struct dentry **dptr,
			    u64 no_addr, const char *name)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct dentry *dentry;
	struct inode *inode;

	inode = gfs2_inode_lookup(sb, DT_DIR, no_addr, 0, 0);
	if (IS_ERR(inode)) {
		fs_err(sdp, "can't read in %s inode: %ld\n", name, PTR_ERR(inode));
		return PTR_ERR(inode);
	}
	dentry = d_alloc_root(inode);
	if (!dentry) {
		fs_err(sdp, "can't alloc %s dentry\n", name);
		iput(inode);
		return -ENOMEM;
	}
	dentry->d_op = &gfs2_dops;
	*dptr = dentry;
	return 0;
}

static int init_sb(struct gfs2_sbd *sdp, int silent)
{
	struct super_block *sb = sdp->sd_vfs;
	struct gfs2_holder sb_gh;
	u64 no_addr;
	int ret;

	ret = gfs2_glock_nq_num(sdp, GFS2_SB_LOCK, &gfs2_meta_glops,
				LM_ST_SHARED, 0, &sb_gh);
	if (ret) {
		fs_err(sdp, "can't acquire superblock glock: %d\n", ret);
		return ret;
	}

	ret = gfs2_read_sb(sdp, silent);
	if (ret) {
		fs_err(sdp, "can't read superblock: %d\n", ret);
		goto out;
	}

	/* Set up the buffer cache and SB for real */
	if (sdp->sd_sb.sb_bsize < bdev_hardsect_size(sb->s_bdev)) {
		ret = -EINVAL;
		fs_err(sdp, "FS block size (%u) is too small for device "
		       "block size (%u)\n",
		       sdp->sd_sb.sb_bsize, bdev_hardsect_size(sb->s_bdev));
		goto out;
	}
	if (sdp->sd_sb.sb_bsize > PAGE_SIZE) {
		ret = -EINVAL;
		fs_err(sdp, "FS block size (%u) is too big for machine "
		       "page size (%u)\n",
		       sdp->sd_sb.sb_bsize, (unsigned int)PAGE_SIZE);
		goto out;
	}
	sb_set_blocksize(sb, sdp->sd_sb.sb_bsize);

	/* Get the root inode */
	no_addr = sdp->sd_sb.sb_root_dir.no_addr;
	ret = gfs2_lookup_root(sb, &sdp->sd_root_dir, no_addr, "root");
	if (ret)
		goto out;

	/* Get the master inode */
	no_addr = sdp->sd_sb.sb_master_dir.no_addr;
	ret = gfs2_lookup_root(sb, &sdp->sd_master_dir, no_addr, "master");
	if (ret) {
		dput(sdp->sd_root_dir);
		goto out;
	}
	sb->s_root = dget(sdp->sd_args.ar_meta ? sdp->sd_master_dir : sdp->sd_root_dir);
out:
	gfs2_glock_dq_uninit(&sb_gh);
	return ret;
}

/**
 * map_journal_extents - create a reusable "extent" mapping from all logical
 * blocks to all physical blocks for the given journal.  This will save
 * us time when writing journal blocks.  Most journals will have only one
 * extent that maps all their logical blocks.  That's because gfs2.mkfs
 * arranges the journal blocks sequentially to maximize performance.
 * So the extent would map the first block for the entire file length.
 * However, gfs2_jadd can happen while file activity is happening, so
 * those journals may not be sequential.  Less likely is the case where
 * the users created their own journals by mounting the metafs and
 * laying it out.  But it's still possible.  These journals might have
 * several extents.
 *
 * TODO: This should be done in bigger chunks rather than one block at a time,
 *       but since it's only done at mount time, I'm not worried about the
 *       time it takes.
 */
static int map_journal_extents(struct gfs2_sbd *sdp)
{
	struct gfs2_jdesc *jd = sdp->sd_jdesc;
	unsigned int lb;
	u64 db, prev_db; /* logical block, disk block, prev disk block */
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_journal_extent *jext = NULL;
	struct buffer_head bh;
	int rc = 0;

	prev_db = 0;

	for (lb = 0; lb < ip->i_disksize >> sdp->sd_sb.sb_bsize_shift; lb++) {
		bh.b_state = 0;
		bh.b_blocknr = 0;
		bh.b_size = 1 << ip->i_inode.i_blkbits;
		rc = gfs2_block_map(jd->jd_inode, lb, &bh, 0);
		db = bh.b_blocknr;
		if (rc || !db) {
			printk(KERN_INFO "GFS2 journal mapping error %d: lb="
			       "%u db=%llu\n", rc, lb, (unsigned long long)db);
			break;
		}
		if (!prev_db || db != prev_db + 1) {
			jext = kzalloc(sizeof(struct gfs2_journal_extent),
				       GFP_KERNEL);
			if (!jext) {
				printk(KERN_INFO "GFS2 error: out of memory "
				       "mapping journal extents.\n");
				rc = -ENOMEM;
				break;
			}
			jext->dblock = db;
			jext->lblock = lb;
			jext->blocks = 1;
			list_add_tail(&jext->extent_list, &jd->extent_list);
		} else {
			jext->blocks++;
		}
		prev_db = db;
	}
	return rc;
}

static void gfs2_others_may_mount(struct gfs2_sbd *sdp)
{
	char *message = "FIRSTMOUNT=Done";
	char *envp[] = { message, NULL };
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	ls->ls_first_done = 1;
	kobject_uevent_env(&sdp->sd_kobj, KOBJ_CHANGE, envp);
}

/**
 * gfs2_jindex_hold - Grab a lock on the jindex
 * @sdp: The GFS2 superblock
 * @ji_gh: the holder for the jindex glock
 *
 * Returns: errno
 */

static int gfs2_jindex_hold(struct gfs2_sbd *sdp, struct gfs2_holder *ji_gh)
{
	struct gfs2_inode *dip = GFS2_I(sdp->sd_jindex);
	struct qstr name;
	char buf[20];
	struct gfs2_jdesc *jd;
	int error;

	name.name = buf;

	mutex_lock(&sdp->sd_jindex_mutex);

	for (;;) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, ji_gh);
		if (error)
			break;

		name.len = sprintf(buf, "journal%u", sdp->sd_journals);
		name.hash = gfs2_disk_hash(name.name, name.len);

		error = gfs2_dir_check(sdp->sd_jindex, &name, NULL);
		if (error == -ENOENT) {
			error = 0;
			break;
		}

		gfs2_glock_dq_uninit(ji_gh);

		if (error)
			break;

		error = -ENOMEM;
		jd = kzalloc(sizeof(struct gfs2_jdesc), GFP_KERNEL);
		if (!jd)
			break;

		INIT_LIST_HEAD(&jd->extent_list);
		jd->jd_inode = gfs2_lookupi(sdp->sd_jindex, &name, 1);
		if (!jd->jd_inode || IS_ERR(jd->jd_inode)) {
			if (!jd->jd_inode)
				error = -ENOENT;
			else
				error = PTR_ERR(jd->jd_inode);
			kfree(jd);
			break;
		}

		spin_lock(&sdp->sd_jindex_spin);
		jd->jd_jid = sdp->sd_journals++;
		list_add_tail(&jd->jd_list, &sdp->sd_jindex_list);
		spin_unlock(&sdp->sd_jindex_spin);
	}

	mutex_unlock(&sdp->sd_jindex_mutex);

	return error;
}

static int init_journal(struct gfs2_sbd *sdp, int undo)
{
	struct inode *master = sdp->sd_master_dir->d_inode;
	struct gfs2_holder ji_gh;
	struct task_struct *p;
	struct gfs2_inode *ip;
	int jindex = 1;
	int error = 0;

	if (undo) {
		jindex = 0;
		goto fail_recoverd;
	}

	sdp->sd_jindex = gfs2_lookup_simple(master, "jindex");
	if (IS_ERR(sdp->sd_jindex)) {
		fs_err(sdp, "can't lookup journal index: %d\n", error);
		return PTR_ERR(sdp->sd_jindex);
	}
	ip = GFS2_I(sdp->sd_jindex);

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
		atomic_set(&sdp->sd_log_blks_free, sdp->sd_jdesc->jd_blocks);
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
					   LM_FLAG_NOEXP | GL_EXACT | GL_NOCACHE,
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
		atomic_set(&sdp->sd_log_blks_free, sdp->sd_jdesc->jd_blocks);

		/* Map the extents for this journal's blocks */
		map_journal_extents(sdp);
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

		gfs2_others_may_mount(sdp);
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
	struct inode *master = sdp->sd_master_dir->d_inode;

	if (undo)
		goto fail_qinode;

	error = init_journal(sdp, undo);
	if (error)
		goto fail;

	/* Read in the master inode number inode */
	sdp->sd_inum_inode = gfs2_lookup_simple(master, "inum");
	if (IS_ERR(sdp->sd_inum_inode)) {
		error = PTR_ERR(sdp->sd_inum_inode);
		fs_err(sdp, "can't read in inum inode: %d\n", error);
		goto fail_journal;
	}


	/* Read in the master statfs inode */
	sdp->sd_statfs_inode = gfs2_lookup_simple(master, "statfs");
	if (IS_ERR(sdp->sd_statfs_inode)) {
		error = PTR_ERR(sdp->sd_statfs_inode);
		fs_err(sdp, "can't read in statfs inode: %d\n", error);
		goto fail_inum;
	}

	/* Read in the resource index inode */
	sdp->sd_rindex = gfs2_lookup_simple(master, "rindex");
	if (IS_ERR(sdp->sd_rindex)) {
		error = PTR_ERR(sdp->sd_rindex);
		fs_err(sdp, "can't get resource index inode: %d\n", error);
		goto fail_statfs;
	}
	ip = GFS2_I(sdp->sd_rindex);
	sdp->sd_rindex_uptodate = 0;

	/* Read in the quota inode */
	sdp->sd_quota_inode = gfs2_lookup_simple(master, "quota");
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
fail:
	return error;
}

static int init_per_node(struct gfs2_sbd *sdp, int undo)
{
	struct inode *pn = NULL;
	char buf[30];
	int error = 0;
	struct gfs2_inode *ip;
	struct inode *master = sdp->sd_master_dir->d_inode;

	if (sdp->sd_args.ar_spectator)
		return 0;

	if (undo)
		goto fail_qc_gh;

	pn = gfs2_lookup_simple(master, "per_node");
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

	p = kthread_run(gfs2_logd, sdp, "gfs2_logd");
	error = IS_ERR(p);
	if (error) {
		fs_err(sdp, "can't start logd thread: %d\n", error);
		return error;
	}
	sdp->sd_logd_process = p;

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

static const match_table_t nolock_tokens = {
	{ Opt_jid, "jid=%d\n", },
	{ Opt_err, NULL },
};

static const struct lm_lockops nolock_ops = {
	.lm_proto_name = "lock_nolock",
	.lm_put_lock = kmem_cache_free,
	.lm_tokens = &nolock_tokens,
};

/**
 * gfs2_lm_mount - mount a locking protocol
 * @sdp: the filesystem
 * @args: mount arguements
 * @silent: if 1, don't complain if the FS isn't a GFS2 fs
 *
 * Returns: errno
 */

static int gfs2_lm_mount(struct gfs2_sbd *sdp, int silent)
{
	const struct lm_lockops *lm;
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	struct gfs2_args *args = &sdp->sd_args;
	const char *proto = sdp->sd_proto_name;
	const char *table = sdp->sd_table_name;
	const char *fsname;
	char *o, *options;
	int ret;

	if (!strcmp("lock_nolock", proto)) {
		lm = &nolock_ops;
		sdp->sd_args.ar_localflocks = 1;
		sdp->sd_args.ar_localcaching = 1;
#ifdef CONFIG_GFS2_FS_LOCKING_DLM
	} else if (!strcmp("lock_dlm", proto)) {
		lm = &gfs2_dlm_ops;
#endif
	} else {
		printk(KERN_INFO "GFS2: can't find protocol %s\n", proto);
		return -ENOENT;
	}

	fs_info(sdp, "Trying to join cluster \"%s\", \"%s\"\n", proto, table);

	ls->ls_ops = lm;
	ls->ls_first = 1;
	ls->ls_id = 0;

	for (options = args->ar_hostdata; (o = strsep(&options, ":")); ) {
		substring_t tmp[MAX_OPT_ARGS];
		int token, option;

		if (!o || !*o)
			continue;

		token = match_token(o, *lm->lm_tokens, tmp);
		switch (token) {
		case Opt_jid:
			ret = match_int(&tmp[0], &option);
			if (ret || option < 0) 
				goto hostdata_error;
			ls->ls_jid = option;
			break;
		case Opt_id:
			ret = match_int(&tmp[0], &option);
			if (ret)
				goto hostdata_error;
			ls->ls_id = option;
			break;
		case Opt_first:
			ret = match_int(&tmp[0], &option);
			if (ret || (option != 0 && option != 1))
				goto hostdata_error;
			ls->ls_first = option;
			break;
		case Opt_nodir:
			ret = match_int(&tmp[0], &option);
			if (ret || (option != 0 && option != 1))
				goto hostdata_error;
			ls->ls_nodir = option;
			break;
		case Opt_err:
		default:
hostdata_error:
			fs_info(sdp, "unknown hostdata (%s)\n", o);
			return -EINVAL;
		}
	}

	if (sdp->sd_args.ar_spectator)
		snprintf(sdp->sd_fsname, GFS2_FSNAME_LEN, "%s.s", table);
	else
		snprintf(sdp->sd_fsname, GFS2_FSNAME_LEN, "%s.%u", table,
			 sdp->sd_lockstruct.ls_jid);

	fsname = strchr(table, ':');
	if (fsname)
		fsname++;
	if (lm->lm_mount == NULL) {
		fs_info(sdp, "Now mounting FS...\n");
		return 0;
	}
	ret = lm->lm_mount(sdp, fsname);
	if (ret == 0)
		fs_info(sdp, "Joined cluster. Now mounting FS...\n");
	return ret;
}

void gfs2_lm_unmount(struct gfs2_sbd *sdp)
{
	const struct lm_lockops *lm = sdp->sd_lockstruct.ls_ops;
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)) &&
	    lm->lm_unmount)
		lm->lm_unmount(sdp);
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

	sdp->sd_args.ar_quota = GFS2_QUOTA_DEFAULT;
	sdp->sd_args.ar_data = GFS2_DATA_DEFAULT;

	error = gfs2_mount_args(sdp, &sdp->sd_args, data);
	if (error) {
		printk(KERN_WARNING "GFS2: can't parse mount arguments\n");
		goto fail;
	}

	if (sdp->sd_args.ar_spectator)
                sb->s_flags |= MS_RDONLY;
	if (sdp->sd_args.ar_posix_acl)
		sb->s_flags |= MS_POSIXACL;

	sb->s_magic = GFS2_MAGIC;
	sb->s_op = &gfs2_super_ops;
	sb->s_export_op = &gfs2_export_ops;
	sb->s_time_gran = 1;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

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

	gfs2_create_debugfs_file(sdp);

	error = gfs2_sys_fs_add(sdp);
	if (error)
		goto fail;

	error = gfs2_lm_mount(sdp, silent);
	if (error)
		goto fail_sys;

	error = init_locking(sdp, &mount_gh, DO);
	if (error)
		goto fail_lm;

	error = init_sb(sdp, silent);
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
	if (sdp->sd_root_dir)
		dput(sdp->sd_root_dir);
	if (sdp->sd_master_dir)
		dput(sdp->sd_master_dir);
	if (sb->s_root)
		dput(sb->s_root);
	sb->s_root = NULL;
fail_locking:
	init_locking(sdp, &mount_gh, UNDO);
fail_lm:
	gfs2_gl_hash_clear(sdp);
	gfs2_lm_unmount(sdp);
	while (invalidate_inodes(sb))
		yield();
fail_sys:
	gfs2_sys_fs_del(sdp);
fail:
	gfs2_delete_debugfs_file(sdp);
	kfree(sdp);
	sb->s_fs_info = NULL;
	return error;
}

static int gfs2_get_sb(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, fill_super, mnt);
}

static struct super_block *get_gfs2_sb(const char *dev_name)
{
	struct super_block *sb;
	struct path path;
	int error;

	error = kern_path(dev_name, LOOKUP_FOLLOW, &path);
	if (error) {
		printk(KERN_WARNING "GFS2: path_lookup on %s returned error %d\n",
		       dev_name, error);
		return NULL;
	}
	sb = path.dentry->d_inode->i_sb;
	if (sb && (sb->s_type == &gfs2_fs_type))
		atomic_inc(&sb->s_active);
	else
		sb = NULL;
	path_put(&path);
	return sb;
}

static int gfs2_get_sb_meta(struct file_system_type *fs_type, int flags,
			    const char *dev_name, void *data, struct vfsmount *mnt)
{
	struct super_block *sb = NULL;
	struct gfs2_sbd *sdp;

	sb = get_gfs2_sb(dev_name);
	if (!sb) {
		printk(KERN_WARNING "GFS2: gfs2 mount does not exist\n");
		return -ENOENT;
	}
	sdp = sb->s_fs_info;
	mnt->mnt_sb = sb;
	mnt->mnt_root = dget(sdp->sd_master_dir);
	return 0;
}

static void gfs2_kill_sb(struct super_block *sb)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;

	if (sdp == NULL) {
		kill_block_super(sb);
		return;
	}

	gfs2_meta_syncfs(sdp);
	dput(sdp->sd_root_dir);
	dput(sdp->sd_master_dir);
	sdp->sd_root_dir = NULL;
	sdp->sd_master_dir = NULL;
	shrink_dcache_sb(sb);
	kill_block_super(sb);
	gfs2_delete_debugfs_file(sdp);
	kfree(sdp);
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
	.owner = THIS_MODULE,
};

