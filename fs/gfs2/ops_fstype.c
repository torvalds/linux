// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/backing-dev.h>
#include <linux/fs_parser.h>

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
#include "meta_io.h"
#include "trace_gfs2.h"
#include "lops.h"

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
	gt->gt_max_readahead = BIT(18);
	gt->gt_complain_secs = 10;
}

void free_sbd(struct gfs2_sbd *sdp)
{
	if (sdp->sd_lkstats)
		free_percpu(sdp->sd_lkstats);
	kfree(sdp);
}

static struct gfs2_sbd *init_sbd(struct super_block *sb)
{
	struct gfs2_sbd *sdp;
	struct address_space *mapping;

	sdp = kzalloc(sizeof(struct gfs2_sbd), GFP_KERNEL);
	if (!sdp)
		return NULL;

	sdp->sd_vfs = sb;
	sdp->sd_lkstats = alloc_percpu(struct gfs2_pcpu_lkstats);
	if (!sdp->sd_lkstats)
		goto fail;
	sb->s_fs_info = sdp;

	set_bit(SDF_NOJOURNALID, &sdp->sd_flags);
	gfs2_tune_init(&sdp->sd_tune);

	init_waitqueue_head(&sdp->sd_glock_wait);
	init_waitqueue_head(&sdp->sd_async_glock_wait);
	atomic_set(&sdp->sd_glock_disposal, 0);
	init_completion(&sdp->sd_locking_init);
	init_completion(&sdp->sd_wdack);
	spin_lock_init(&sdp->sd_statfs_spin);

	spin_lock_init(&sdp->sd_rindex_spin);
	sdp->sd_rindex_tree.rb_node = NULL;

	INIT_LIST_HEAD(&sdp->sd_jindex_list);
	spin_lock_init(&sdp->sd_jindex_spin);
	mutex_init(&sdp->sd_jindex_mutex);
	init_completion(&sdp->sd_journal_ready);

	INIT_LIST_HEAD(&sdp->sd_quota_list);
	mutex_init(&sdp->sd_quota_mutex);
	mutex_init(&sdp->sd_quota_sync_mutex);
	init_waitqueue_head(&sdp->sd_quota_wait);
	INIT_LIST_HEAD(&sdp->sd_trunc_list);
	spin_lock_init(&sdp->sd_trunc_lock);
	spin_lock_init(&sdp->sd_bitmap_lock);

	INIT_LIST_HEAD(&sdp->sd_sc_inodes_list);

	mapping = &sdp->sd_aspace;

	address_space_init_once(mapping);
	mapping->a_ops = &gfs2_rgrp_aops;
	mapping->host = sb->s_bdev->bd_inode;
	mapping->flags = 0;
	mapping_set_gfp_mask(mapping, GFP_NOFS);
	mapping->private_data = NULL;
	mapping->writeback_index = 0;

	spin_lock_init(&sdp->sd_log_lock);
	atomic_set(&sdp->sd_log_pinned, 0);
	INIT_LIST_HEAD(&sdp->sd_log_revokes);
	INIT_LIST_HEAD(&sdp->sd_log_ordered);
	spin_lock_init(&sdp->sd_ordered_lock);

	init_waitqueue_head(&sdp->sd_log_waitq);
	init_waitqueue_head(&sdp->sd_logd_waitq);
	spin_lock_init(&sdp->sd_ail_lock);
	INIT_LIST_HEAD(&sdp->sd_ail1_list);
	INIT_LIST_HEAD(&sdp->sd_ail2_list);

	init_rwsem(&sdp->sd_log_flush_lock);
	atomic_set(&sdp->sd_log_in_flight, 0);
	atomic_set(&sdp->sd_reserving_log, 0);
	init_waitqueue_head(&sdp->sd_reserving_log_wait);
	init_waitqueue_head(&sdp->sd_log_flush_wait);
	atomic_set(&sdp->sd_freeze_state, SFS_UNFROZEN);
	mutex_init(&sdp->sd_freeze_mutex);

	return sdp;

fail:
	free_sbd(sdp);
	return NULL;
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
			pr_warn("not a GFS2 filesystem\n");
		return -EINVAL;
	}

	if (sb->sb_fs_format != GFS2_FORMAT_FS ||
	    sb->sb_multihost_format != GFS2_FORMAT_MULTI) {
		fs_warn(sdp, "Unknown on-disk format, unable to mount\n");
		return -EINVAL;
	}

	if (sb->sb_bsize < 512 || sb->sb_bsize > PAGE_SIZE ||
	    (sb->sb_bsize & (sb->sb_bsize - 1))) {
		pr_warn("Invalid superblock size\n");
		return -EINVAL;
	}
	if (sb->sb_bsize_shift != ffs(sb->sb_bsize) - 1) {
		pr_warn("Invalid block size shift\n");
		return -EINVAL;
	}
	return 0;
}

static void end_bio_io_page(struct bio *bio)
{
	struct page *page = bio->bi_private;

	if (!bio->bi_status)
		SetPageUptodate(page);
	else
		pr_warn("error %d reading superblock\n", bio->bi_status);
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
	memcpy(&s->s_uuid, str->sb_uuid, 16);
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
		return -ENOMEM;

	ClearPageUptodate(page);
	ClearPageDirty(page);
	lock_page(page);

	bio = bio_alloc(GFP_NOFS, 1);
	bio->bi_iter.bi_sector = sector * (sb->s_blocksize >> 9);
	bio_set_dev(bio, sb->s_bdev);
	bio_add_page(bio, page, PAGE_SIZE, 0);

	bio->bi_end_io = end_bio_io_page;
	bio->bi_private = page;
	bio_set_op_attrs(bio, REQ_OP_READ, REQ_META);
	submit_bio(bio);
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
	sdp->sd_fsb2bb = BIT(sdp->sd_fsb2bb_shift);
	sdp->sd_diptrs = (sdp->sd_sb.sb_bsize -
			  sizeof(struct gfs2_dinode)) / sizeof(u64);
	sdp->sd_inptrs = (sdp->sd_sb.sb_bsize -
			  sizeof(struct gfs2_meta_header)) / sizeof(u64);
	sdp->sd_ldptrs = (sdp->sd_sb.sb_bsize -
			  sizeof(struct gfs2_log_descriptor)) / sizeof(u64);
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

	hash_blocks = DIV_ROUND_UP(sizeof(u64) * BIT(GFS2_DIR_MAX_DEPTH),
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

	sdp->sd_max_dents_per_leaf = (sdp->sd_sb.sb_bsize -
				      sizeof(struct gfs2_leaf)) /
				     GFS2_MIN_DIRENT_SIZE;
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

	BUILD_BUG_ON(GFS2_LOCKNAME_LEN > GFS2_FSNAME_LEN);

	strscpy(sdp->sd_proto_name, proto, GFS2_LOCKNAME_LEN);
	strscpy(sdp->sd_table_name, table, GFS2_LOCKNAME_LEN);

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

	error = gfs2_glock_get(sdp, GFS2_FREEZE_LOCK, &gfs2_freeze_glops,
			       CREATE, &sdp->sd_freeze_gl);
	if (error) {
		fs_err(sdp, "can't create transaction glock: %d\n", error);
		goto fail_rename;
	}

	return 0;

fail_trans:
	gfs2_glock_put(sdp->sd_freeze_gl);
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

	inode = gfs2_inode_lookup(sb, DT_DIR, no_addr, 0,
				  GFS2_BLKST_FREE /* ignore */);
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
		struct gfs2_inode *jip;

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
		INIT_LIST_HEAD(&jd->jd_revoke_list);

		INIT_WORK(&jd->jd_work, gfs2_recover_func);
		jd->jd_inode = gfs2_lookupi(sdp->sd_jindex, &name, 1);
		if (IS_ERR_OR_NULL(jd->jd_inode)) {
			if (!jd->jd_inode)
				error = -ENOENT;
			else
				error = PTR_ERR(jd->jd_inode);
			kfree(jd);
			break;
		}

		spin_lock(&sdp->sd_jindex_spin);
		jd->jd_jid = sdp->sd_journals++;
		jip = GFS2_I(jd->jd_inode);
		jd->jd_no_addr = jip->i_no_addr;
		list_add_tail(&jd->jd_list, &sdp->sd_jindex_list);
		spin_unlock(&sdp->sd_jindex_spin);
	}

	mutex_unlock(&sdp->sd_jindex_mutex);

	return error;
}

/**
 * init_statfs - look up and initialize master and local (per node) statfs inodes
 * @sdp: The GFS2 superblock
 *
 * This should be called after the jindex is initialized in init_journal() and
 * before gfs2_journal_recovery() is called because we need to be able to write
 * to these inodes during recovery.
 *
 * Returns: errno
 */
static int init_statfs(struct gfs2_sbd *sdp)
{
	int error = 0;
	struct inode *master = d_inode(sdp->sd_master_dir);
	struct inode *pn = NULL;
	char buf[30];
	struct gfs2_jdesc *jd;
	struct gfs2_inode *ip;

	sdp->sd_statfs_inode = gfs2_lookup_simple(master, "statfs");
	if (IS_ERR(sdp->sd_statfs_inode)) {
		error = PTR_ERR(sdp->sd_statfs_inode);
		fs_err(sdp, "can't read in statfs inode: %d\n", error);
		goto out;
	}
	if (sdp->sd_args.ar_spectator)
		goto out;

	pn = gfs2_lookup_simple(master, "per_node");
	if (IS_ERR(pn)) {
		error = PTR_ERR(pn);
		fs_err(sdp, "can't find per_node directory: %d\n", error);
		goto put_statfs;
	}

	/* For each jid, lookup the corresponding local statfs inode in the
	 * per_node metafs directory and save it in the sdp->sd_sc_inodes_list. */
	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		struct local_statfs_inode *lsi =
			kmalloc(sizeof(struct local_statfs_inode), GFP_NOFS);
		if (!lsi) {
			error = -ENOMEM;
			goto free_local;
		}
		sprintf(buf, "statfs_change%u", jd->jd_jid);
		lsi->si_sc_inode = gfs2_lookup_simple(pn, buf);
		if (IS_ERR(lsi->si_sc_inode)) {
			error = PTR_ERR(lsi->si_sc_inode);
			fs_err(sdp, "can't find local \"sc\" file#%u: %d\n",
			       jd->jd_jid, error);
			kfree(lsi);
			goto free_local;
		}
		lsi->si_jid = jd->jd_jid;
		if (jd->jd_jid == sdp->sd_jdesc->jd_jid)
			sdp->sd_sc_inode = lsi->si_sc_inode;

		list_add_tail(&lsi->si_list, &sdp->sd_sc_inodes_list);
	}

	iput(pn);
	pn = NULL;
	ip = GFS2_I(sdp->sd_sc_inode);
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0,
				   &sdp->sd_sc_gh);
	if (error) {
		fs_err(sdp, "can't lock local \"sc\" file: %d\n", error);
		goto free_local;
	}
	return 0;

free_local:
	free_local_statfs_inodes(sdp);
	iput(pn);
put_statfs:
	iput(sdp->sd_statfs_inode);
out:
	return error;
}

/* Uninitialize and free up memory used by the list of statfs inodes */
static void uninit_statfs(struct gfs2_sbd *sdp)
{
	if (!sdp->sd_args.ar_spectator) {
		gfs2_glock_dq_uninit(&sdp->sd_sc_gh);
		free_local_statfs_inodes(sdp);
	}
	iput(sdp->sd_statfs_inode);
}

static int init_journal(struct gfs2_sbd *sdp, int undo)
{
	struct inode *master = d_inode(sdp->sd_master_dir);
	struct gfs2_holder ji_gh;
	struct gfs2_inode *ip;
	int jindex = 1;
	int error = 0;

	if (undo) {
		jindex = 0;
		goto fail_statfs;
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

	atomic_set(&sdp->sd_log_blks_needed, 0);
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
					  LM_ST_EXCLUSIVE,
					  LM_FLAG_NOEXP | GL_NOCACHE,
					  &sdp->sd_journal_gh);
		if (error) {
			fs_err(sdp, "can't acquire journal glock: %d\n", error);
			goto fail_jindex;
		}

		ip = GFS2_I(sdp->sd_jdesc->jd_inode);
		sdp->sd_jinode_gl = ip->i_gl;
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
		gfs2_map_journal_extents(sdp, sdp->sd_jdesc);
	}
	trace_gfs2_log_blocks(sdp, atomic_read(&sdp->sd_log_blks_free));

	/* Lookup statfs inodes here so journal recovery can use them. */
	error = init_statfs(sdp);
	if (error)
		goto fail_jinode_gh;

	if (sdp->sd_lockstruct.ls_first) {
		unsigned int x;
		for (x = 0; x < sdp->sd_journals; x++) {
			struct gfs2_jdesc *jd = gfs2_jdesc_find(sdp, x);

			if (sdp->sd_args.ar_spectator) {
				error = check_journal_clean(sdp, jd, true);
				if (error)
					goto fail_statfs;
				continue;
			}
			error = gfs2_recover_journal(jd, true);
			if (error) {
				fs_err(sdp, "error recovering journal %u: %d\n",
				       x, error);
				goto fail_statfs;
			}
		}

		gfs2_others_may_mount(sdp);
	} else if (!sdp->sd_args.ar_spectator) {
		error = gfs2_recover_journal(sdp->sd_jdesc, true);
		if (error) {
			fs_err(sdp, "error recovering my journal: %d\n", error);
			goto fail_statfs;
		}
	}

	sdp->sd_log_idle = 1;
	set_bit(SDF_JOURNAL_CHECKED, &sdp->sd_flags);
	gfs2_glock_dq_uninit(&ji_gh);
	jindex = 0;
	INIT_WORK(&sdp->sd_freeze_work, gfs2_freeze_func);
	return 0;

fail_statfs:
	uninit_statfs(sdp);
fail_jinode_gh:
	/* A withdraw may have done dq/uninit so now we need to check it */
	if (!sdp->sd_args.ar_spectator &&
	    gfs2_holder_initialized(&sdp->sd_jinode_gh))
		gfs2_glock_dq_uninit(&sdp->sd_jinode_gh);
fail_journal_gh:
	if (!sdp->sd_args.ar_spectator &&
	    gfs2_holder_initialized(&sdp->sd_journal_gh))
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
	struct inode *master = d_inode(sdp->sd_master_dir);

	if (undo)
		goto fail_qinode;

	error = init_journal(sdp, undo);
	complete_all(&sdp->sd_journal_ready);
	if (error)
		goto fail;

	/* Read in the resource index inode */
	sdp->sd_rindex = gfs2_lookup_simple(master, "rindex");
	if (IS_ERR(sdp->sd_rindex)) {
		error = PTR_ERR(sdp->sd_rindex);
		fs_err(sdp, "can't get resource index inode: %d\n", error);
		goto fail_journal;
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
	 * i_rwsem on quota files is special. Since this inode is hidden system
	 * file, we are safe to define locking ourselves.
	 */
	lockdep_set_class(&sdp->sd_quota_inode->i_rwsem,
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
	struct inode *master = d_inode(sdp->sd_master_dir);

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

	sprintf(buf, "quota_change%u", sdp->sd_jdesc->jd_jid);
	sdp->sd_qc_inode = gfs2_lookup_simple(pn, buf);
	if (IS_ERR(sdp->sd_qc_inode)) {
		error = PTR_ERR(sdp->sd_qc_inode);
		fs_err(sdp, "can't find local \"qc\" file: %d\n", error);
		goto fail_ut_i;
	}

	iput(pn);
	pn = NULL;

	ip = GFS2_I(sdp->sd_qc_inode);
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0,
				   &sdp->sd_qc_gh);
	if (error) {
		fs_err(sdp, "can't lock local \"qc\" file: %d\n", error);
		goto fail_qc_i;
	}

	return 0;

fail_qc_gh:
	gfs2_glock_dq_uninit(&sdp->sd_qc_gh);
fail_qc_i:
	iput(sdp->sd_qc_inode);
fail_ut_i:
	iput(pn);
	return error;
}

static const match_table_t nolock_tokens = {
	{ Opt_jid, "jid=%d", },
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
		pr_info("can't find protocol %s\n", proto);
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
	if (likely(!gfs2_withdrawn(sdp)) && lm->lm_unmount)
		lm->lm_unmount(sdp);
}

static int wait_on_journal(struct gfs2_sbd *sdp)
{
	if (sdp->sd_lockstruct.ls_ops->lm_mount == NULL)
		return 0;

	return wait_on_bit(&sdp->sd_flags, SDF_NOJOURNALID, TASK_INTERRUPTIBLE)
		? -EINTR : 0;
}

void gfs2_online_uevent(struct gfs2_sbd *sdp)
{
	struct super_block *sb = sdp->sd_vfs;
	char ro[20];
	char spectator[20];
	char *envp[] = { ro, spectator, NULL };
	sprintf(ro, "RDONLY=%d", sb_rdonly(sb));
	sprintf(spectator, "SPECTATOR=%d", sdp->sd_args.ar_spectator ? 1 : 0);
	kobject_uevent_env(&sdp->sd_kobj, KOBJ_ONLINE, envp);
}

static int init_threads(struct gfs2_sbd *sdp)
{
	struct task_struct *p;
	int error = 0;

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

fail:
	kthread_stop(sdp->sd_logd_process);
	sdp->sd_logd_process = NULL;
	return error;
}

/**
 * gfs2_fill_super - Read in superblock
 * @sb: The VFS superblock
 * @args: Mount options
 * @silent: Don't complain if it's not a GFS2 filesystem
 *
 * Returns: -errno
 */
static int gfs2_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct gfs2_args *args = fc->fs_private;
	int silent = fc->sb_flags & SB_SILENT;
	struct gfs2_sbd *sdp;
	struct gfs2_holder mount_gh;
	struct gfs2_holder freeze_gh;
	int error;

	sdp = init_sbd(sb);
	if (!sdp) {
		pr_warn("can't alloc struct gfs2_sbd\n");
		return -ENOMEM;
	}
	sdp->sd_args = *args;

	if (sdp->sd_args.ar_spectator) {
                sb->s_flags |= SB_RDONLY;
		set_bit(SDF_RORECOVERY, &sdp->sd_flags);
	}
	if (sdp->sd_args.ar_posix_acl)
		sb->s_flags |= SB_POSIXACL;
	if (sdp->sd_args.ar_nobarrier)
		set_bit(SDF_NOBARRIERS, &sdp->sd_flags);

	sb->s_flags |= SB_NOSEC;
	sb->s_magic = GFS2_MAGIC;
	sb->s_op = &gfs2_super_ops;
	sb->s_d_op = &gfs2_dops;
	sb->s_export_op = &gfs2_export_ops;
	sb->s_xattr = gfs2_xattr_handlers;
	sb->s_qcop = &gfs2_quotactl_ops;
	sb->s_quota_types = QTYPE_MASK_USR | QTYPE_MASK_GRP;
	sb_dqopt(sb)->flags |= DQUOT_QUOTA_SYS_FILE;
	sb->s_time_gran = 1;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	/* Set up the buffer cache and fill in some fake block size values
	   to allow us to read-in the on-disk superblock. */
	sdp->sd_sb.sb_bsize = sb_min_blocksize(sb, GFS2_BASIC_BLOCK);
	sdp->sd_sb.sb_bsize_shift = sb->s_blocksize_bits;
	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift -
                               GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = BIT(sdp->sd_fsb2bb_shift);

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
	if (error)
		goto fail_free;

	snprintf(sdp->sd_fsname, sizeof(sdp->sd_fsname), "%s", sdp->sd_table_name);

	error = gfs2_sys_fs_add(sdp);
	if (error)
		goto fail_free;

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
		snprintf(sdp->sd_fsname, sizeof(sdp->sd_fsname), "%s.s",
			 sdp->sd_table_name);
	else
		snprintf(sdp->sd_fsname, sizeof(sdp->sd_fsname), "%s.%u",
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

	if (!sb_rdonly(sb)) {
		error = init_threads(sdp);
		if (error) {
			gfs2_withdraw_delayed(sdp);
			goto fail_per_node;
		}
	}

	error = gfs2_freeze_lock(sdp, &freeze_gh, 0);
	if (error)
		goto fail_per_node;

	if (!sb_rdonly(sb))
		error = gfs2_make_fs_rw(sdp);

	gfs2_freeze_unlock(&freeze_gh);
	if (error) {
		if (sdp->sd_quotad_process)
			kthread_stop(sdp->sd_quotad_process);
		sdp->sd_quotad_process = NULL;
		if (sdp->sd_logd_process)
			kthread_stop(sdp->sd_logd_process);
		sdp->sd_logd_process = NULL;
		fs_err(sdp, "can't make FS RW: %d\n", error);
		goto fail_per_node;
	}
	gfs2_glock_dq_uninit(&mount_gh);
	gfs2_online_uevent(sdp);
	return 0;

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
	complete_all(&sdp->sd_journal_ready);
	gfs2_gl_hash_clear(sdp);
	gfs2_lm_unmount(sdp);
fail_debug:
	gfs2_delete_debugfs_file(sdp);
	gfs2_sys_fs_del(sdp);
fail_free:
	free_sbd(sdp);
	sb->s_fs_info = NULL;
	return error;
}

/**
 * gfs2_get_tree - Get the GFS2 superblock and root directory
 * @fc: The filesystem context
 *
 * Returns: 0 or -errno on error
 */
static int gfs2_get_tree(struct fs_context *fc)
{
	struct gfs2_args *args = fc->fs_private;
	struct gfs2_sbd *sdp;
	int error;

	error = get_tree_bdev(fc, gfs2_fill_super);
	if (error)
		return error;

	sdp = fc->root->d_sb->s_fs_info;
	dput(fc->root);
	if (args->ar_meta)
		fc->root = dget(sdp->sd_master_dir);
	else
		fc->root = dget(sdp->sd_root_dir);
	return 0;
}

static void gfs2_fc_free(struct fs_context *fc)
{
	struct gfs2_args *args = fc->fs_private;

	kfree(args);
}

enum gfs2_param {
	Opt_lockproto,
	Opt_locktable,
	Opt_hostdata,
	Opt_spectator,
	Opt_ignore_local_fs,
	Opt_localflocks,
	Opt_localcaching,
	Opt_debug,
	Opt_upgrade,
	Opt_acl,
	Opt_quota,
	Opt_quota_flag,
	Opt_suiddir,
	Opt_data,
	Opt_meta,
	Opt_discard,
	Opt_commit,
	Opt_errors,
	Opt_statfs_quantum,
	Opt_statfs_percent,
	Opt_quota_quantum,
	Opt_barrier,
	Opt_rgrplvb,
	Opt_loccookie,
};

static const struct constant_table gfs2_param_quota[] = {
	{"off",        GFS2_QUOTA_OFF},
	{"account",    GFS2_QUOTA_ACCOUNT},
	{"on",         GFS2_QUOTA_ON},
	{}
};

enum opt_data {
	Opt_data_writeback = GFS2_DATA_WRITEBACK,
	Opt_data_ordered   = GFS2_DATA_ORDERED,
};

static const struct constant_table gfs2_param_data[] = {
	{"writeback",  Opt_data_writeback },
	{"ordered",    Opt_data_ordered },
	{}
};

enum opt_errors {
	Opt_errors_withdraw = GFS2_ERRORS_WITHDRAW,
	Opt_errors_panic    = GFS2_ERRORS_PANIC,
};

static const struct constant_table gfs2_param_errors[] = {
	{"withdraw",   Opt_errors_withdraw },
	{"panic",      Opt_errors_panic },
	{}
};

static const struct fs_parameter_spec gfs2_fs_parameters[] = {
	fsparam_string ("lockproto",          Opt_lockproto),
	fsparam_string ("locktable",          Opt_locktable),
	fsparam_string ("hostdata",           Opt_hostdata),
	fsparam_flag   ("spectator",          Opt_spectator),
	fsparam_flag   ("norecovery",         Opt_spectator),
	fsparam_flag   ("ignore_local_fs",    Opt_ignore_local_fs),
	fsparam_flag   ("localflocks",        Opt_localflocks),
	fsparam_flag   ("localcaching",       Opt_localcaching),
	fsparam_flag_no("debug",              Opt_debug),
	fsparam_flag   ("upgrade",            Opt_upgrade),
	fsparam_flag_no("acl",                Opt_acl),
	fsparam_flag_no("suiddir",            Opt_suiddir),
	fsparam_enum   ("data",               Opt_data, gfs2_param_data),
	fsparam_flag   ("meta",               Opt_meta),
	fsparam_flag_no("discard",            Opt_discard),
	fsparam_s32    ("commit",             Opt_commit),
	fsparam_enum   ("errors",             Opt_errors, gfs2_param_errors),
	fsparam_s32    ("statfs_quantum",     Opt_statfs_quantum),
	fsparam_s32    ("statfs_percent",     Opt_statfs_percent),
	fsparam_s32    ("quota_quantum",      Opt_quota_quantum),
	fsparam_flag_no("barrier",            Opt_barrier),
	fsparam_flag_no("rgrplvb",            Opt_rgrplvb),
	fsparam_flag_no("loccookie",          Opt_loccookie),
	/* quota can be a flag or an enum so it gets special treatment */
	fsparam_flag_no("quota",	      Opt_quota_flag),
	fsparam_enum("quota",		      Opt_quota, gfs2_param_quota),
	{}
};

/* Parse a single mount parameter */
static int gfs2_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct gfs2_args *args = fc->fs_private;
	struct fs_parse_result result;
	int o;

	o = fs_parse(fc, gfs2_fs_parameters, param, &result);
	if (o < 0)
		return o;

	switch (o) {
	case Opt_lockproto:
		strscpy(args->ar_lockproto, param->string, GFS2_LOCKNAME_LEN);
		break;
	case Opt_locktable:
		strscpy(args->ar_locktable, param->string, GFS2_LOCKNAME_LEN);
		break;
	case Opt_hostdata:
		strscpy(args->ar_hostdata, param->string, GFS2_LOCKNAME_LEN);
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
		if (result.boolean && args->ar_errors == GFS2_ERRORS_PANIC)
			return invalfc(fc, "-o debug and -o errors=panic are mutually exclusive");
		args->ar_debug = result.boolean;
		break;
	case Opt_upgrade:
		/* Retained for backwards compat only */
		break;
	case Opt_acl:
		args->ar_posix_acl = result.boolean;
		break;
	case Opt_quota_flag:
		args->ar_quota = result.negated ? GFS2_QUOTA_OFF : GFS2_QUOTA_ON;
		break;
	case Opt_quota:
		args->ar_quota = result.int_32;
		break;
	case Opt_suiddir:
		args->ar_suiddir = result.boolean;
		break;
	case Opt_data:
		/* The uint_32 result maps directly to GFS2_DATA_* */
		args->ar_data = result.uint_32;
		break;
	case Opt_meta:
		args->ar_meta = 1;
		break;
	case Opt_discard:
		args->ar_discard = result.boolean;
		break;
	case Opt_commit:
		if (result.int_32 <= 0)
			return invalfc(fc, "commit mount option requires a positive numeric argument");
		args->ar_commit = result.int_32;
		break;
	case Opt_statfs_quantum:
		if (result.int_32 < 0)
			return invalfc(fc, "statfs_quantum mount option requires a non-negative numeric argument");
		args->ar_statfs_quantum = result.int_32;
		break;
	case Opt_quota_quantum:
		if (result.int_32 <= 0)
			return invalfc(fc, "quota_quantum mount option requires a positive numeric argument");
		args->ar_quota_quantum = result.int_32;
		break;
	case Opt_statfs_percent:
		if (result.int_32 < 0 || result.int_32 > 100)
			return invalfc(fc, "statfs_percent mount option requires a numeric argument between 0 and 100");
		args->ar_statfs_percent = result.int_32;
		break;
	case Opt_errors:
		if (args->ar_debug && result.uint_32 == GFS2_ERRORS_PANIC)
			return invalfc(fc, "-o debug and -o errors=panic are mutually exclusive");
		args->ar_errors = result.uint_32;
		break;
	case Opt_barrier:
		args->ar_nobarrier = result.boolean;
		break;
	case Opt_rgrplvb:
		args->ar_rgrplvb = result.boolean;
		break;
	case Opt_loccookie:
		args->ar_loccookie = result.boolean;
		break;
	default:
		return invalfc(fc, "invalid mount option: %s", param->key);
	}
	return 0;
}

static int gfs2_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_args *oldargs = &sdp->sd_args;
	struct gfs2_args *newargs = fc->fs_private;
	struct gfs2_tune *gt = &sdp->sd_tune;
	int error = 0;

	sync_filesystem(sb);

	spin_lock(&gt->gt_spin);
	oldargs->ar_commit = gt->gt_logd_secs;
	oldargs->ar_quota_quantum = gt->gt_quota_quantum;
	if (gt->gt_statfs_slow)
		oldargs->ar_statfs_quantum = 0;
	else
		oldargs->ar_statfs_quantum = gt->gt_statfs_quantum;
	spin_unlock(&gt->gt_spin);

	if (strcmp(newargs->ar_lockproto, oldargs->ar_lockproto)) {
		errorfc(fc, "reconfiguration of locking protocol not allowed");
		return -EINVAL;
	}
	if (strcmp(newargs->ar_locktable, oldargs->ar_locktable)) {
		errorfc(fc, "reconfiguration of lock table not allowed");
		return -EINVAL;
	}
	if (strcmp(newargs->ar_hostdata, oldargs->ar_hostdata)) {
		errorfc(fc, "reconfiguration of host data not allowed");
		return -EINVAL;
	}
	if (newargs->ar_spectator != oldargs->ar_spectator) {
		errorfc(fc, "reconfiguration of spectator mode not allowed");
		return -EINVAL;
	}
	if (newargs->ar_localflocks != oldargs->ar_localflocks) {
		errorfc(fc, "reconfiguration of localflocks not allowed");
		return -EINVAL;
	}
	if (newargs->ar_meta != oldargs->ar_meta) {
		errorfc(fc, "switching between gfs2 and gfs2meta not allowed");
		return -EINVAL;
	}
	if (oldargs->ar_spectator)
		fc->sb_flags |= SB_RDONLY;

	if ((sb->s_flags ^ fc->sb_flags) & SB_RDONLY) {
		struct gfs2_holder freeze_gh;

		error = gfs2_freeze_lock(sdp, &freeze_gh, 0);
		if (error)
			return -EINVAL;

		if (fc->sb_flags & SB_RDONLY) {
			error = gfs2_make_fs_ro(sdp);
			if (error)
				errorfc(fc, "unable to remount read-only");
		} else {
			error = gfs2_make_fs_rw(sdp);
			if (error)
				errorfc(fc, "unable to remount read-write");
		}
		gfs2_freeze_unlock(&freeze_gh);
	}
	sdp->sd_args = *newargs;

	if (sdp->sd_args.ar_posix_acl)
		sb->s_flags |= SB_POSIXACL;
	else
		sb->s_flags &= ~SB_POSIXACL;
	if (sdp->sd_args.ar_nobarrier)
		set_bit(SDF_NOBARRIERS, &sdp->sd_flags);
	else
		clear_bit(SDF_NOBARRIERS, &sdp->sd_flags);
	spin_lock(&gt->gt_spin);
	gt->gt_logd_secs = newargs->ar_commit;
	gt->gt_quota_quantum = newargs->ar_quota_quantum;
	if (newargs->ar_statfs_quantum) {
		gt->gt_statfs_slow = 0;
		gt->gt_statfs_quantum = newargs->ar_statfs_quantum;
	}
	else {
		gt->gt_statfs_slow = 1;
		gt->gt_statfs_quantum = 30;
	}
	spin_unlock(&gt->gt_spin);

	gfs2_online_uevent(sdp);
	return error;
}

static const struct fs_context_operations gfs2_context_ops = {
	.free        = gfs2_fc_free,
	.parse_param = gfs2_parse_param,
	.get_tree    = gfs2_get_tree,
	.reconfigure = gfs2_reconfigure,
};

/* Set up the filesystem mount context */
static int gfs2_init_fs_context(struct fs_context *fc)
{
	struct gfs2_args *args;

	args = kmalloc(sizeof(*args), GFP_KERNEL);
	if (args == NULL)
		return -ENOMEM;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		struct gfs2_sbd *sdp = fc->root->d_sb->s_fs_info;

		*args = sdp->sd_args;
	} else {
		memset(args, 0, sizeof(*args));
		args->ar_quota = GFS2_QUOTA_DEFAULT;
		args->ar_data = GFS2_DATA_DEFAULT;
		args->ar_commit = 30;
		args->ar_statfs_quantum = 30;
		args->ar_quota_quantum = 60;
		args->ar_errors = GFS2_ERRORS_DEFAULT;
	}
	fc->fs_private = args;
	fc->ops = &gfs2_context_ops;
	return 0;
}

static int set_meta_super(struct super_block *s, struct fs_context *fc)
{
	return -EINVAL;
}

static int test_meta_super(struct super_block *s, struct fs_context *fc)
{
	return (fc->sget_key == s->s_bdev);
}

static int gfs2_meta_get_tree(struct fs_context *fc)
{
	struct super_block *s;
	struct gfs2_sbd *sdp;
	struct path path;
	int error;

	if (!fc->source || !*fc->source)
		return -EINVAL;

	error = kern_path(fc->source, LOOKUP_FOLLOW, &path);
	if (error) {
		pr_warn("path_lookup on %s returned error %d\n",
		        fc->source, error);
		return error;
	}
	fc->fs_type = &gfs2_fs_type;
	fc->sget_key = path.dentry->d_sb->s_bdev;
	s = sget_fc(fc, test_meta_super, set_meta_super);
	path_put(&path);
	if (IS_ERR(s)) {
		pr_warn("gfs2 mount does not exist\n");
		return PTR_ERR(s);
	}
	if ((fc->sb_flags ^ s->s_flags) & SB_RDONLY) {
		deactivate_locked_super(s);
		return -EBUSY;
	}
	sdp = s->s_fs_info;
	fc->root = dget(sdp->sd_master_dir);
	return 0;
}

static const struct fs_context_operations gfs2_meta_context_ops = {
	.free        = gfs2_fc_free,
	.get_tree    = gfs2_meta_get_tree,
};

static int gfs2_meta_init_fs_context(struct fs_context *fc)
{
	int ret = gfs2_init_fs_context(fc);

	if (ret)
		return ret;

	fc->ops = &gfs2_meta_context_ops;
	return 0;
}

static void gfs2_kill_sb(struct super_block *sb)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;

	if (sdp == NULL) {
		kill_block_super(sb);
		return;
	}

	gfs2_log_flush(sdp, NULL, GFS2_LOG_HEAD_FLUSH_SYNC | GFS2_LFC_KILL_SB);
	dput(sdp->sd_root_dir);
	dput(sdp->sd_master_dir);
	sdp->sd_root_dir = NULL;
	sdp->sd_master_dir = NULL;
	shrink_dcache_sb(sb);
	kill_block_super(sb);
}

struct file_system_type gfs2_fs_type = {
	.name = "gfs2",
	.fs_flags = FS_REQUIRES_DEV,
	.init_fs_context = gfs2_init_fs_context,
	.parameters = gfs2_fs_parameters,
	.kill_sb = gfs2_kill_sb,
	.owner = THIS_MODULE,
};
MODULE_ALIAS_FS("gfs2");

struct file_system_type gfs2meta_fs_type = {
	.name = "gfs2meta",
	.fs_flags = FS_REQUIRES_DEV,
	.init_fs_context = gfs2_meta_init_fs_context,
	.owner = THIS_MODULE,
};
MODULE_ALIAS_FS("gfs2meta");
