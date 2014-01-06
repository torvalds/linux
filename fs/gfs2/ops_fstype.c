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
#include <linux/export.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/gfs2_ondisk.h>
#include <linux/quotaops.h>
#include <linux/lockdep.h>
#include <linux/module.h>

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
#include "trace_gfs2.h"

#define DO 0
#define UNDO 1

/**
 * gfs2_tune_init - Fill a gfs2_tune structure with default values
 * @gt: tune
 *
 */

static void gfs2_tune_init(struct gfs2_tune *gt)
{
	spin_lock_init(&gt->gt_spin);

	gt->gt_quota_warn_period = 10;
	gt->gt_quota_scale_num = 1;
	gt->gt_quota_scale_den = 1;
	gt->gt_new_files_jdata = 0;
	gt->gt_max_readahead = 1 << 18;
	gt->gt_complain_secs = 10;
}

static struct gfs2_sbd *init_sbd(struct super_block *sb)
{
	struct gfs2_sbd *sdp;

	sdp = kzalloc(sizeof(struct gfs2_sbd), GFP_KERNEL);
	if (!sdp)
		return NULL;

	sb->s_fs_info = sdp;
	sdp->sd_vfs = sb;
	sdp->sd_lkstats = alloc_percpu(struct gfs2_pcpu_lkstats);
	if (!sdp->sd_lkstats) {
		kfree(sdp);
		return NULL;
	}

	set_bit(SDF_NOJOURNALID, &sdp->sd_flags);
	gfs2_tune_init(&sdp->sd_tune);

	init_waitqueue_head(&sdp->sd_glock_wait);
	atomic_set(&sdp->sd_glock_disposal, 0);
	init_completion(&sdp->sd_locking_init);
	init_completion(&sdp->sd_wdack);
	spin_lock_init(&sdp->sd_statfs_spin);

	spin_lock_init(&sdp->sd_rindex_spin);
	sdp->sd_rindex_tree.rb_node = NULL;

	INIT_LIST_HEAD(&sdp->sd_jindex_list);
	spin_lock_init(&sdp->sd_jindex_spin);
	mutex_init(&sdp->sd_jindex_mutex);

	INIT_LIST_HEAD(&sdp->sd_quota_list);
	mutex_init(&sdp->sd_quota_mutex);
	mutex_init(&sdp->sd_quota_sync_mutex);
	init_waitqueue_head(&sdp->sd_quota_wait);
	INIT_LIST_HEAD(&sdp->sd_trunc_list);
	spin_lock_init(&sdp->sd_trunc_lock);

	spin_lock_init(&sdp->sd_log_lock);
	atomic_set(&sdp->sd_log_pinned, 0);
	INIT_LIST_HEAD(&sdp->sd_log_le_buf);
	INIT_LIST_HEAD(&sdp->sd_log_le_revoke);
	INIT_LIST_HEAD(&sdp->sd_log_le_databuf);
	INIT_LIST_HEAD(&sdp->sd_log_le_ordered);
	spin_lock_init(&sdp->sd_ordered_lock);

	init_waitqueue_head(&sdp->sd_log_waitq);
	init_waitqueue_head(&sdp->sd_logd_waitq);
	spin_lock_init(&sdp->sd_ail_lock);
	INIT_LIST_HEAD(&sdp->sd_ail1_list);
	INIT_LIST_HEAD(&sdp->sd_ail2_list);

	init_rwsem(&sdp->sd_log_flush_lock);
	atomic_set(&sdp->sd_log_in_flight, 0);
	init_waitqueue_head(&sdp->sd_log_flush_wait);

	INIT_LIST_HEAD(&sdp->sd_revoke_list);

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

static int gfs2_check_sb(struct gfs2_sbd *sdp, int silent)
{
	struct gfs2_sb_host *sb = &sdp->sd_sb;

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

	fs_warn(sdp, "Unknown on-disk format, unable to mount\n");

	return -EINVAL;
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

static void gfs2_sb_in(struct gfs2_sbd *sdp, const void *buf)
{
	struct gfs2_sb_host *sb = &sdp->sd_sb;
	struct super_block *s = sdp->sd_vfs;
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
	memcpy(s->s_uuid, str->sb_uuid, 16);
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

static int gfs2_read_super(struct gfs2_sbd *sdp, sector_t sector, int silent)
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
	submit_bio(READ_SYNC | REQ_META, bio);
	wait_on_page_locked(page);
	bio_put(bio);
	if (!PageUptodate(page)) {
		__free_page(page);
		return -EIO;
	}
	p = kmap(page);
	gfs2_sb_in(sdp, p);
	kunmap(page);
	__free_page(page);
	return gfs2_check_sb(sdp, silent);
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

	error = gfs2_read_super(sdp, GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift, silent);
	if (error) {
		if (!silent)
			fs_err(sdp, "can't read superblock\n");
		return error;
	}

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
	sdp->sd_blocks_per_bitmap = (sdp->sd_sb.sb_bsize -
				     sizeof(struct gfs2_meta_header))
		* GFS2_NBBY; /* not the rgrp bitmap, subsequent bitmaps only */

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
		error = gfs2_read_super(sdp, GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift, silent);
		if (error)
			return error;

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
	dentry = d_make_root(inode);
	if (!dentry) {
		fs_err(sdp, "can't alloc %s dentry\n", name);
		return -ENOMEM;
	}
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
	if (sdp->sd_sb.sb_bsize < bdev_logical_block_size(sb->s_bdev)) {
		ret = -EINVAL;
		fs_err(sdp, "FS block size (%u) is too small for device "
		       "block size (%u)\n",
		       sdp->sd_sb.sb_bsize, bdev_logical_block_size(sb->s_bdev));
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

	for (lb = 0; lb < i_size_read(jd->jd_inode) >> sdp->sd_sb.sb_bsize_shift; lb++) {
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

	fs_info(sdp, "first mount done, others may mount\n");

	if (sdp->sd_lockstruct.ls_ops->lm_first_done)
		sdp->sd_lockstruct.ls_ops->lm_first_done(sdp);

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
		INIT_WORK(&jd->jd_work, gfs2_recover_func);
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

/**
 * check_journal_clean - Make sure a journal is clean for a spectator mount
 * @sdp: The GFS2 superblock
 * @jd: The journal descriptor
 *
 * Returns: 0 if the journal is clean or locked, else an error
 */
static int check_journal_clean(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd)
{
	int error;
	struct gfs2_holder j_gh;
	struct gfs2_log_header_host head;
	struct gfs2_inode *ip;

	ip = GFS2_I(jd->jd_inode);
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_NOEXP |
				   GL_EXACT | GL_NOCACHE, &j_gh);
	if (error) {
		fs_err(sdp, "Error locking journal for spectator mount.\n");
		return -EPERM;
	}
	error = gfs2_jdesc_check(jd);
	if (error) {
		fs_err(sdp, "Error checking journal for spectator mount.\n");
		goto out_unlock;
	}
	error = gfs2_find_jhead(jd, &head);
	if (error) {
		fs_err(sdp, "Error parsing journal for spectator mount.\n");
		goto out_unlock;
	}
	if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
		error = -EPERM;
		fs_err(sdp, "jid=%u: Journal is dirty, so the first mounter "
		       "must not be a spectator.\n", jd->jd_jid);
	}

out_unlock:
	gfs2_glock_dq_uninit(&j_gh);
	return error;
}

static int init_journal(struct gfs2_sbd *sdp, int undo)
{
	struct inode *master = sdp->sd_master_dir->d_inode;
	struct gfs2_holder ji_gh;
	struct gfs2_inode *ip;
	int jindex = 1;
	int error = 0;

	if (undo) {
		jindex = 0;
		goto fail_jinode_gh;
	}

	sdp->sd_jindex = gfs2_lookup_simple(master, "jindex");
	if (IS_ERR(sdp->sd_jindex)) {
		fs_err(sdp, "can't lookup journal index: %d\n", error);
		return PTR_ERR(sdp->sd_jindex);
	}

	/* Load in the journal index special file */

	error = gfs2_jindex_hold(sdp, &ji_gh);
	if (error) {
		fs_err(sdp, "can't read journal index: %d\n", error);
		goto fail;
	}

	error = -EUSERS;
	if (!gfs2_jindex_size(sdp)) {
		fs_err(sdp, "no journals!\n");
		goto fail_jindex;
	}

	if (sdp->sd_args.ar_spectator) {
		sdp->sd_jdesc = gfs2_jdesc_find(sdp, 0);
		atomic_set(&sdp->sd_log_blks_free, sdp->sd_jdesc->jd_blocks);
		atomic_set(&sdp->sd_log_thresh1, 2*sdp->sd_jdesc->jd_blocks/5);
		atomic_set(&sdp->sd_log_thresh2, 4*sdp->sd_jdesc->jd_blocks/5);
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
		atomic_set(&sdp->sd_log_thresh1, 2*sdp->sd_jdesc->jd_blocks/5);
		atomic_set(&sdp->sd_log_thresh2, 4*sdp->sd_jdesc->jd_blocks/5);

		/* Map the extents for this journal's blocks */
		map_journal_extents(sdp);
	}
	trace_gfs2_log_blocks(sdp, atomic_read(&sdp->sd_log_blks_free));

	if (sdp->sd_lockstruct.ls_first) {
		unsigned int x;
		for (x = 0; x < sdp->sd_journals; x++) {
			struct gfs2_jdesc *jd = gfs2_jdesc_find(sdp, x);

			if (sdp->sd_args.ar_spectator) {
				error = check_journal_clean(sdp, jd);
				if (error)
					goto fail_jinode_gh;
				continue;
			}
			error = gfs2_recover_journal(jd, true);
			if (error) {
				fs_err(sdp, "error recovering journal %u: %d\n",
				       x, error);
				goto fail_jinode_gh;
			}
		}

		gfs2_others_may_mount(sdp);
	} else if (!sdp->sd_args.ar_spectator) {
		error = gfs2_recover_journal(sdp->sd_jdesc, true);
		if (error) {
			fs_err(sdp, "error recovering my journal: %d\n", error);
			goto fail_jinode_gh;
		}
	}

	set_bit(SDF_JOURNAL_CHECKED, &sdp->sd_flags);
	gfs2_glock_dq_uninit(&ji_gh);
	jindex = 0;

	return 0;

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

static struct lock_class_key gfs2_quota_imutex_key;

static int init_inodes(struct gfs2_sbd *sdp, int undo)
{
	int error = 0;
	struct inode *master = sdp->sd_master_dir->d_inode;

	if (undo)
		goto fail_qinode;

	error = init_journal(sdp, undo);
	if (error)
		goto fail;

	/* Read in the master statfs inode */
	sdp->sd_statfs_inode = gfs2_lookup_simple(master, "statfs");
	if (IS_ERR(sdp->sd_statfs_inode)) {
		error = PTR_ERR(sdp->sd_statfs_inode);
		fs_err(sdp, "can't read in statfs inode: %d\n", error);
		goto fail_journal;
	}

	/* Read in the resource index inode */
	sdp->sd_rindex = gfs2_lookup_simple(master, "rindex");
	if (IS_ERR(sdp->sd_rindex)) {
		error = PTR_ERR(sdp->sd_rindex);
		fs_err(sdp, "can't get resource index inode: %d\n", error);
		goto fail_statfs;
	}
	sdp->sd_rindex_uptodate = 0;

	/* Read in the quota inode */
	sdp->sd_quota_inode = gfs2_lookup_simple(master, "quota");
	if (IS_ERR(sdp->sd_quota_inode)) {
		error = PTR_ERR(sdp->sd_quota_inode);
		fs_err(sdp, "can't get quota file inode: %d\n", error);
		goto fail_rindex;
	}
	/*
	 * i_mutex on quota files is special. Since this inode is hidden system
	 * file, we are safe to define locking ourselves.
	 */
	lockdep_set_class(&sdp->sd_quota_inode->i_mutex,
			  &gfs2_quota_imutex_key);

	error = gfs2_rindex_update(sdp);
	if (error)
		goto fail_qinode;

	return 0;

fail_qinode:
	iput(sdp->sd_quota_inode);
fail_rindex:
	gfs2_clear_rgrpd(sdp);
	iput(sdp->sd_rindex);
fail_statfs:
	iput(sdp->sd_statfs_inode);
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

	sprintf(buf, "statfs_change%u", sdp->sd_jdesc->jd_jid);
	sdp->sd_sc_inode = gfs2_lookup_simple(pn, buf);
	if (IS_ERR(sdp->sd_sc_inode)) {
		error = PTR_ERR(sdp->sd_sc_inode);
		fs_err(sdp, "can't find local \"sc\" file: %d\n", error);
		goto fail;
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

	ip = GFS2_I(sdp->sd_sc_inode);
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0,
				   &sdp->sd_sc_gh);
	if (error) {
		fs_err(sdp, "can't lock local \"sc\" file: %d\n", error);
		goto fail_qc_i;
	}

	ip = GFS2_I(sdp->sd_qc_inode);
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0,
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
fail_qc_i:
	iput(sdp->sd_qc_inode);
fail_ut_i:
	iput(sdp->sd_sc_inode);
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

	p = kthread_run(gfs2_logd, sdp, "gfs2_logd");
	if (IS_ERR(p)) {
		error = PTR_ERR(p);
		fs_err(sdp, "can't start logd thread: %d\n", error);
		return error;
	}
	sdp->sd_logd_process = p;

	p = kthread_run(gfs2_quotad, sdp, "gfs2_quotad");
	if (IS_ERR(p)) {
		error = PTR_ERR(p);
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
	.lm_put_lock = gfs2_glock_free,
	.lm_tokens = &nolock_tokens,
};

/**
 * gfs2_lm_mount - mount a locking protocol
 * @sdp: the filesystem
 * @args: mount arguments
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
	char *o, *options;
	int ret;

	if (!strcmp("lock_nolock", proto)) {
		lm = &nolock_ops;
		sdp->sd_args.ar_localflocks = 1;
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
			if (test_and_clear_bit(SDF_NOJOURNALID, &sdp->sd_flags))
				ls->ls_jid = option;
			break;
		case Opt_id:
		case Opt_nodir:
			/* Obsolete, but left for backward compat purposes */
			break;
		case Opt_first:
			ret = match_int(&tmp[0], &option);
			if (ret || (option != 0 && option != 1))
				goto hostdata_error;
			ls->ls_first = option;
			break;
		case Opt_err:
		default:
hostdata_error:
			fs_info(sdp, "unknown hostdata (%s)\n", o);
			return -EINVAL;
		}
	}

	if (lm->lm_mount == NULL) {
		fs_info(sdp, "Now mounting FS...\n");
		complete_all(&sdp->sd_locking_init);
		return 0;
	}
	ret = lm->lm_mount(sdp, table);
	if (ret == 0)
		fs_info(sdp, "Joined cluster. Now mounting FS...\n");
	complete_all(&sdp->sd_locking_init);
	return ret;
}

void gfs2_lm_unmount(struct gfs2_sbd *sdp)
{
	const struct lm_lockops *lm = sdp->sd_lockstruct.ls_ops;
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)) &&
	    lm->lm_unmount)
		lm->lm_unmount(sdp);
}

static int gfs2_journalid_wait(void *word)
{
	if (signal_pending(current))
		return -EINTR;
	schedule();
	return 0;
}

static int wait_on_journal(struct gfs2_sbd *sdp)
{
	if (sdp->sd_lockstruct.ls_ops->lm_mount == NULL)
		return 0;

	return wait_on_bit(&sdp->sd_flags, SDF_NOJOURNALID, gfs2_journalid_wait, TASK_INTERRUPTIBLE);
}

void gfs2_online_uevent(struct gfs2_sbd *sdp)
{
	struct super_block *sb = sdp->sd_vfs;
	char ro[20];
	char spectator[20];
	char *envp[] = { ro, spectator, NULL };
	sprintf(ro, "RDONLY=%d", (sb->s_flags & MS_RDONLY) ? 1 : 0);
	sprintf(spectator, "SPECTATOR=%d", sdp->sd_args.ar_spectator ? 1 : 0);
	kobject_uevent_env(&sdp->sd_kobj, KOBJ_ONLINE, envp);
}

/**
 * fill_super - Read in superblock
 * @sb: The VFS superblock
 * @data: Mount options
 * @silent: Don't complain if it's not a GFS2 filesystem
 *
 * Returns: errno
 */

static int fill_super(struct super_block *sb, struct gfs2_args *args, int silent)
{
	struct gfs2_sbd *sdp;
	struct gfs2_holder mount_gh;
	int error;

	sdp = init_sbd(sb);
	if (!sdp) {
		printk(KERN_WARNING "GFS2: can't alloc struct gfs2_sbd\n");
		return -ENOMEM;
	}
	sdp->sd_args = *args;

	if (sdp->sd_args.ar_spectator) {
                sb->s_flags |= MS_RDONLY;
		set_bit(SDF_RORECOVERY, &sdp->sd_flags);
	}
	if (sdp->sd_args.ar_posix_acl)
		sb->s_flags |= MS_POSIXACL;
	if (sdp->sd_args.ar_nobarrier)
		set_bit(SDF_NOBARRIERS, &sdp->sd_flags);

	sb->s_flags |= MS_NOSEC;
	sb->s_magic = GFS2_MAGIC;
	sb->s_op = &gfs2_super_ops;
	sb->s_d_op = &gfs2_dops;
	sb->s_export_op = &gfs2_export_ops;
	sb->s_xattr = gfs2_xattr_handlers;
	sb->s_qcop = &gfs2_quotactl_ops;
	sb_dqopt(sb)->flags |= DQUOT_QUOTA_SYS_FILE;
	sb->s_time_gran = 1;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	/* Set up the buffer cache and fill in some fake block size values
	   to allow us to read-in the on-disk superblock. */
	sdp->sd_sb.sb_bsize = sb_min_blocksize(sb, GFS2_BASIC_BLOCK);
	sdp->sd_sb.sb_bsize_shift = sb->s_blocksize_bits;
	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift -
                               GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;

	sdp->sd_tune.gt_logd_secs = sdp->sd_args.ar_commit;
	sdp->sd_tune.gt_quota_quantum = sdp->sd_args.ar_quota_quantum;
	if (sdp->sd_args.ar_statfs_quantum) {
		sdp->sd_tune.gt_statfs_slow = 0;
		sdp->sd_tune.gt_statfs_quantum = sdp->sd_args.ar_statfs_quantum;
	} else {
		sdp->sd_tune.gt_statfs_slow = 1;
		sdp->sd_tune.gt_statfs_quantum = 30;
	}

	error = init_names(sdp, silent);
	if (error) {
		/* In this case, we haven't initialized sysfs, so we have to
		   manually free the sdp. */
		free_percpu(sdp->sd_lkstats);
		kfree(sdp);
		sb->s_fs_info = NULL;
		return error;
	}

	snprintf(sdp->sd_fsname, GFS2_FSNAME_LEN, "%s", sdp->sd_table_name);

	error = gfs2_sys_fs_add(sdp);
	/*
	 * If we hit an error here, gfs2_sys_fs_add will have called function
	 * kobject_put which causes the sysfs usage count to go to zero, which
	 * causes sysfs to call function gfs2_sbd_release, which frees sdp.
	 * Subsequent error paths here will call gfs2_sys_fs_del, which also
	 * kobject_put to free sdp.
	 */
	if (error)
		return error;

	gfs2_create_debugfs_file(sdp);

	error = gfs2_lm_mount(sdp, silent);
	if (error)
		goto fail_debug;

	error = init_locking(sdp, &mount_gh, DO);
	if (error)
		goto fail_lm;

	error = init_sb(sdp, silent);
	if (error)
		goto fail_locking;

	error = wait_on_journal(sdp);
	if (error)
		goto fail_sb;

	/*
	 * If user space has failed to join the cluster or some similar
	 * failure has occurred, then the journal id will contain a
	 * negative (error) number. This will then be returned to the
	 * caller (of the mount syscall). We do this even for spectator
	 * mounts (which just write a jid of 0 to indicate "ok" even though
	 * the jid is unused in the spectator case)
	 */
	if (sdp->sd_lockstruct.ls_jid < 0) {
		error = sdp->sd_lockstruct.ls_jid;
		sdp->sd_lockstruct.ls_jid = 0;
		goto fail_sb;
	}

	if (sdp->sd_args.ar_spectator)
		snprintf(sdp->sd_fsname, GFS2_FSNAME_LEN, "%s.s",
			 sdp->sd_table_name);
	else
		snprintf(sdp->sd_fsname, GFS2_FSNAME_LEN, "%s.%u",
			 sdp->sd_table_name, sdp->sd_lockstruct.ls_jid);

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
	gfs2_online_uevent(sdp);
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
fail_debug:
	gfs2_delete_debugfs_file(sdp);
	free_percpu(sdp->sd_lkstats);
	/* gfs2_sys_fs_del must be the last thing we do, since it causes
	 * sysfs to call function gfs2_sbd_release, which frees sdp. */
	gfs2_sys_fs_del(sdp);
	sb->s_fs_info = NULL;
	return error;
}

static int set_gfs2_super(struct super_block *s, void *data)
{
	s->s_bdev = data;
	s->s_dev = s->s_bdev->bd_dev;

	/*
	 * We set the bdi here to the queue backing, file systems can
	 * overwrite this in ->fill_super()
	 */
	s->s_bdi = &bdev_get_queue(s->s_bdev)->backing_dev_info;
	return 0;
}

static int test_gfs2_super(struct super_block *s, void *ptr)
{
	struct block_device *bdev = ptr;
	return (bdev == s->s_bdev);
}

/**
 * gfs2_mount - Get the GFS2 superblock
 * @fs_type: The GFS2 filesystem type
 * @flags: Mount flags
 * @dev_name: The name of the device
 * @data: The mount arguments
 *
 * Q. Why not use get_sb_bdev() ?
 * A. We need to select one of two root directories to mount, independent
 *    of whether this is the initial, or subsequent, mount of this sb
 *
 * Returns: 0 or -ve on error
 */

static struct dentry *gfs2_mount(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *data)
{
	struct block_device *bdev;
	struct super_block *s;
	fmode_t mode = FMODE_READ | FMODE_EXCL;
	int error;
	struct gfs2_args args;
	struct gfs2_sbd *sdp;

	if (!(flags & MS_RDONLY))
		mode |= FMODE_WRITE;

	bdev = blkdev_get_by_path(dev_name, mode, fs_type);
	if (IS_ERR(bdev))
		return ERR_CAST(bdev);

	/*
	 * once the super is inserted into the list by sget, s_umount
	 * will protect the lockfs code from trying to start a snapshot
	 * while we are mounting
	 */
	mutex_lock(&bdev->bd_fsfreeze_mutex);
	if (bdev->bd_fsfreeze_count > 0) {
		mutex_unlock(&bdev->bd_fsfreeze_mutex);
		error = -EBUSY;
		goto error_bdev;
	}
	s = sget(fs_type, test_gfs2_super, set_gfs2_super, flags, bdev);
	mutex_unlock(&bdev->bd_fsfreeze_mutex);
	error = PTR_ERR(s);
	if (IS_ERR(s))
		goto error_bdev;

	if (s->s_root) {
		/*
		 * s_umount nests inside bd_mutex during
		 * __invalidate_device().  blkdev_put() acquires
		 * bd_mutex and can't be called under s_umount.  Drop
		 * s_umount temporarily.  This is safe as we're
		 * holding an active reference.
		 */
		up_write(&s->s_umount);
		blkdev_put(bdev, mode);
		down_write(&s->s_umount);
	}

	memset(&args, 0, sizeof(args));
	args.ar_quota = GFS2_QUOTA_DEFAULT;
	args.ar_data = GFS2_DATA_DEFAULT;
	args.ar_commit = 30;
	args.ar_statfs_quantum = 30;
	args.ar_quota_quantum = 60;
	args.ar_errors = GFS2_ERRORS_DEFAULT;

	error = gfs2_mount_args(&args, data);
	if (error) {
		printk(KERN_WARNING "GFS2: can't parse mount arguments\n");
		goto error_super;
	}

	if (s->s_root) {
		error = -EBUSY;
		if ((flags ^ s->s_flags) & MS_RDONLY)
			goto error_super;
	} else {
		char b[BDEVNAME_SIZE];

		s->s_mode = mode;
		strlcpy(s->s_id, bdevname(bdev, b), sizeof(s->s_id));
		sb_set_blocksize(s, block_size(bdev));
		error = fill_super(s, &args, flags & MS_SILENT ? 1 : 0);
		if (error)
			goto error_super;
		s->s_flags |= MS_ACTIVE;
		bdev->bd_super = s;
	}

	sdp = s->s_fs_info;
	if (args.ar_meta)
		return dget(sdp->sd_master_dir);
	else
		return dget(sdp->sd_root_dir);

error_super:
	deactivate_locked_super(s);
	return ERR_PTR(error);
error_bdev:
	blkdev_put(bdev, mode);
	return ERR_PTR(error);
}

static int set_meta_super(struct super_block *s, void *ptr)
{
	return -EINVAL;
}

static struct dentry *gfs2_mount_meta(struct file_system_type *fs_type,
			int flags, const char *dev_name, void *data)
{
	struct super_block *s;
	struct gfs2_sbd *sdp;
	struct path path;
	int error;

	error = kern_path(dev_name, LOOKUP_FOLLOW, &path);
	if (error) {
		printk(KERN_WARNING "GFS2: path_lookup on %s returned error %d\n",
		       dev_name, error);
		return ERR_PTR(error);
	}
	s = sget(&gfs2_fs_type, test_gfs2_super, set_meta_super, flags,
		 path.dentry->d_inode->i_sb->s_bdev);
	path_put(&path);
	if (IS_ERR(s)) {
		printk(KERN_WARNING "GFS2: gfs2 mount does not exist\n");
		return ERR_CAST(s);
	}
	if ((flags ^ s->s_flags) & MS_RDONLY) {
		deactivate_locked_super(s);
		return ERR_PTR(-EBUSY);
	}
	sdp = s->s_fs_info;
	return dget(sdp->sd_master_dir);
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
	gfs2_delete_debugfs_file(sdp);
	free_percpu(sdp->sd_lkstats);
	kill_block_super(sb);
}

struct file_system_type gfs2_fs_type = {
	.name = "gfs2",
	.fs_flags = FS_REQUIRES_DEV,
	.mount = gfs2_mount,
	.kill_sb = gfs2_kill_sb,
	.owner = THIS_MODULE,
};
MODULE_ALIAS_FS("gfs2");

struct file_system_type gfs2meta_fs_type = {
	.name = "gfs2meta",
	.fs_flags = FS_REQUIRES_DEV,
	.mount = gfs2_mount_meta,
	.owner = THIS_MODULE,
};
MODULE_ALIAS_FS("gfs2meta");
