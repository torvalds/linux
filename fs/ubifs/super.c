// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS initialization and VFS superblock operations. Some
 * initialization stuff which is rather large and complex is placed at
 * corresponding subsystems, but most of it is here.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/kthread.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/math64.h>
#include <linux/writeback.h>
#include "ubifs.h"

static int ubifs_default_version_set(const char *val, const struct kernel_param *kp)
{
	int n = 0, ret;

	ret = kstrtoint(val, 10, &n);
	if (ret != 0 || n < 4 || n > UBIFS_FORMAT_VERSION)
		return -EINVAL;
	return param_set_int(val, kp);
}

static const struct kernel_param_ops ubifs_default_version_ops = {
	.set = ubifs_default_version_set,
	.get = param_get_int,
};

int ubifs_default_version = UBIFS_FORMAT_VERSION;
module_param_cb(default_version, &ubifs_default_version_ops, &ubifs_default_version, 0600);

/*
 * Maximum amount of memory we may 'kmalloc()' without worrying that we are
 * allocating too much.
 */
#define UBIFS_KMALLOC_OK (128*1024)

/* Slab cache for UBIFS inodes */
static struct kmem_cache *ubifs_inode_slab;

/* UBIFS TNC shrinker description */
static struct shrinker *ubifs_shrinker_info;

/**
 * validate_inode - validate inode.
 * @c: UBIFS file-system description object
 * @inode: the inode to validate
 *
 * This is a helper function for 'ubifs_iget()' which validates various fields
 * of a newly built inode to make sure they contain sane values and prevent
 * possible vulnerabilities. Returns zero if the inode is all right and
 * a non-zero error code if not.
 */
static int validate_inode(struct ubifs_info *c, const struct inode *inode)
{
	int err;
	const struct ubifs_inode *ui = ubifs_inode(inode);

	if (inode->i_size > c->max_inode_sz) {
		ubifs_err(c, "inode is too large (%lld)",
			  (long long)inode->i_size);
		return 1;
	}

	if (ui->compr_type >= UBIFS_COMPR_TYPES_CNT) {
		ubifs_err(c, "unknown compression type %d", ui->compr_type);
		return 2;
	}

	if (ui->xattr_names + ui->xattr_cnt > XATTR_LIST_MAX)
		return 3;

	if (ui->data_len < 0 || ui->data_len > UBIFS_MAX_INO_DATA)
		return 4;

	if (ui->xattr && !S_ISREG(inode->i_mode))
		return 5;

	if (!ubifs_compr_present(c, ui->compr_type)) {
		ubifs_warn(c, "inode %lu uses '%s' compression, but it was not compiled in",
			   inode->i_ino, ubifs_compr_name(c, ui->compr_type));
	}

	err = dbg_check_dir(c, inode);
	return err;
}

struct inode *ubifs_iget(struct super_block *sb, unsigned long inum)
{
	int err;
	union ubifs_key key;
	struct ubifs_ino_node *ino;
	struct ubifs_info *c = sb->s_fs_info;
	struct inode *inode;
	struct ubifs_inode *ui;

	dbg_gen("inode %lu", inum);

	inode = iget_locked(sb, inum);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	ui = ubifs_inode(inode);

	ino = kmalloc(UBIFS_MAX_INO_NODE_SZ, GFP_NOFS);
	if (!ino) {
		err = -ENOMEM;
		goto out;
	}

	ino_key_init(c, &key, inode->i_ino);

	err = ubifs_tnc_lookup(c, &key, ino);
	if (err)
		goto out_ino;

	inode->i_flags |= S_NOCMTIME;

	if (!IS_ENABLED(CONFIG_UBIFS_ATIME_SUPPORT))
		inode->i_flags |= S_NOATIME;

	set_nlink(inode, le32_to_cpu(ino->nlink));
	i_uid_write(inode, le32_to_cpu(ino->uid));
	i_gid_write(inode, le32_to_cpu(ino->gid));
	inode_set_atime(inode, (int64_t)le64_to_cpu(ino->atime_sec),
			le32_to_cpu(ino->atime_nsec));
	inode_set_mtime(inode, (int64_t)le64_to_cpu(ino->mtime_sec),
			le32_to_cpu(ino->mtime_nsec));
	inode_set_ctime(inode, (int64_t)le64_to_cpu(ino->ctime_sec),
			le32_to_cpu(ino->ctime_nsec));
	inode->i_mode = le32_to_cpu(ino->mode);
	inode->i_size = le64_to_cpu(ino->size);

	ui->data_len    = le32_to_cpu(ino->data_len);
	ui->flags       = le32_to_cpu(ino->flags);
	ui->compr_type  = le16_to_cpu(ino->compr_type);
	ui->creat_sqnum = le64_to_cpu(ino->creat_sqnum);
	ui->xattr_cnt   = le32_to_cpu(ino->xattr_cnt);
	ui->xattr_size  = le32_to_cpu(ino->xattr_size);
	ui->xattr_names = le32_to_cpu(ino->xattr_names);
	ui->synced_i_size = ui->ui_size = inode->i_size;

	ui->xattr = (ui->flags & UBIFS_XATTR_FL) ? 1 : 0;

	err = validate_inode(c, inode);
	if (err)
		goto out_invalid;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_mapping->a_ops = &ubifs_file_address_operations;
		inode->i_op = &ubifs_file_inode_operations;
		inode->i_fop = &ubifs_file_operations;
		if (ui->xattr) {
			ui->data = kmalloc(ui->data_len + 1, GFP_NOFS);
			if (!ui->data) {
				err = -ENOMEM;
				goto out_ino;
			}
			memcpy(ui->data, ino->data, ui->data_len);
			((char *)ui->data)[ui->data_len] = '\0';
		} else if (ui->data_len != 0) {
			err = 10;
			goto out_invalid;
		}
		break;
	case S_IFDIR:
		inode->i_op  = &ubifs_dir_inode_operations;
		inode->i_fop = &ubifs_dir_operations;
		if (ui->data_len != 0) {
			err = 11;
			goto out_invalid;
		}
		break;
	case S_IFLNK:
		inode->i_op = &ubifs_symlink_inode_operations;
		if (ui->data_len <= 0 || ui->data_len > UBIFS_MAX_INO_DATA) {
			err = 12;
			goto out_invalid;
		}
		ui->data = kmalloc(ui->data_len + 1, GFP_NOFS);
		if (!ui->data) {
			err = -ENOMEM;
			goto out_ino;
		}
		memcpy(ui->data, ino->data, ui->data_len);
		((char *)ui->data)[ui->data_len] = '\0';
		break;
	case S_IFBLK:
	case S_IFCHR:
	{
		dev_t rdev;
		union ubifs_dev_desc *dev;

		ui->data = kmalloc(sizeof(union ubifs_dev_desc), GFP_NOFS);
		if (!ui->data) {
			err = -ENOMEM;
			goto out_ino;
		}

		dev = (union ubifs_dev_desc *)ino->data;
		if (ui->data_len == sizeof(dev->new))
			rdev = new_decode_dev(le32_to_cpu(dev->new));
		else if (ui->data_len == sizeof(dev->huge))
			rdev = huge_decode_dev(le64_to_cpu(dev->huge));
		else {
			err = 13;
			goto out_invalid;
		}
		memcpy(ui->data, ino->data, ui->data_len);
		inode->i_op = &ubifs_file_inode_operations;
		init_special_inode(inode, inode->i_mode, rdev);
		break;
	}
	case S_IFSOCK:
	case S_IFIFO:
		inode->i_op = &ubifs_file_inode_operations;
		init_special_inode(inode, inode->i_mode, 0);
		if (ui->data_len != 0) {
			err = 14;
			goto out_invalid;
		}
		break;
	default:
		err = 15;
		goto out_invalid;
	}

	kfree(ino);
	ubifs_set_inode_flags(inode);
	unlock_new_inode(inode);
	return inode;

out_invalid:
	ubifs_err(c, "inode %lu validation failed, error %d", inode->i_ino, err);
	ubifs_dump_node(c, ino, UBIFS_MAX_INO_NODE_SZ);
	ubifs_dump_inode(c, inode);
	err = -EINVAL;
out_ino:
	kfree(ino);
out:
	ubifs_err(c, "failed to read inode %lu, error %d", inode->i_ino, err);
	iget_failed(inode);
	return ERR_PTR(err);
}

static struct inode *ubifs_alloc_inode(struct super_block *sb)
{
	struct ubifs_inode *ui;

	ui = alloc_inode_sb(sb, ubifs_inode_slab, GFP_NOFS);
	if (!ui)
		return NULL;

	memset((void *)ui + sizeof(struct inode), 0,
	       sizeof(struct ubifs_inode) - sizeof(struct inode));
	mutex_init(&ui->ui_mutex);
	init_rwsem(&ui->xattr_sem);
	spin_lock_init(&ui->ui_lock);
	return &ui->vfs_inode;
};

static void ubifs_free_inode(struct inode *inode)
{
	struct ubifs_inode *ui = ubifs_inode(inode);

	kfree(ui->data);
	fscrypt_free_inode(inode);

	kmem_cache_free(ubifs_inode_slab, ui);
}

/*
 * Note, Linux write-back code calls this without 'i_mutex'.
 */
static int ubifs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int err = 0;
	struct ubifs_info *c = inode->i_sb->s_fs_info;
	struct ubifs_inode *ui = ubifs_inode(inode);

	ubifs_assert(c, !ui->xattr);
	if (is_bad_inode(inode))
		return 0;

	mutex_lock(&ui->ui_mutex);
	/*
	 * Due to races between write-back forced by budgeting
	 * (see 'sync_some_inodes()') and background write-back, the inode may
	 * have already been synchronized, do not do this again. This might
	 * also happen if it was synchronized in an VFS operation, e.g.
	 * 'ubifs_link()'.
	 */
	if (!ui->dirty) {
		mutex_unlock(&ui->ui_mutex);
		return 0;
	}

	/*
	 * As an optimization, do not write orphan inodes to the media just
	 * because this is not needed.
	 */
	dbg_gen("inode %lu, mode %#x, nlink %u",
		inode->i_ino, (int)inode->i_mode, inode->i_nlink);
	if (inode->i_nlink) {
		err = ubifs_jnl_write_inode(c, inode);
		if (err)
			ubifs_err(c, "can't write inode %lu, error %d",
				  inode->i_ino, err);
		else
			err = dbg_check_inode_size(c, inode, ui->ui_size);
	}

	ui->dirty = 0;
	mutex_unlock(&ui->ui_mutex);
	ubifs_release_dirty_inode_budget(c, ui);
	return err;
}

static int ubifs_drop_inode(struct inode *inode)
{
	int drop = generic_drop_inode(inode);

	if (!drop)
		drop = fscrypt_drop_inode(inode);

	return drop;
}

static void ubifs_evict_inode(struct inode *inode)
{
	int err;
	struct ubifs_info *c = inode->i_sb->s_fs_info;
	struct ubifs_inode *ui = ubifs_inode(inode);

	if (ui->xattr)
		/*
		 * Extended attribute inode deletions are fully handled in
		 * 'ubifs_removexattr()'. These inodes are special and have
		 * limited usage, so there is nothing to do here.
		 */
		goto out;

	dbg_gen("inode %lu, mode %#x", inode->i_ino, (int)inode->i_mode);
	ubifs_assert(c, !atomic_read(&inode->i_count));

	truncate_inode_pages_final(&inode->i_data);

	if (inode->i_nlink)
		goto done;

	if (is_bad_inode(inode))
		goto out;

	ui->ui_size = inode->i_size = 0;
	err = ubifs_jnl_delete_inode(c, inode);
	if (err)
		/*
		 * Worst case we have a lost orphan inode wasting space, so a
		 * simple error message is OK here.
		 */
		ubifs_err(c, "can't delete inode %lu, error %d",
			  inode->i_ino, err);

out:
	if (ui->dirty)
		ubifs_release_dirty_inode_budget(c, ui);
	else {
		/* We've deleted something - clean the "no space" flags */
		c->bi.nospace = c->bi.nospace_rp = 0;
		smp_wmb();
	}
done:
	clear_inode(inode);
	fscrypt_put_encryption_info(inode);
}

static void ubifs_dirty_inode(struct inode *inode, int flags)
{
	struct ubifs_info *c = inode->i_sb->s_fs_info;
	struct ubifs_inode *ui = ubifs_inode(inode);

	ubifs_assert(c, mutex_is_locked(&ui->ui_mutex));
	if (!ui->dirty) {
		ui->dirty = 1;
		dbg_gen("inode %lu",  inode->i_ino);
	}
}

static int ubifs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct ubifs_info *c = dentry->d_sb->s_fs_info;
	unsigned long long free;
	__le32 *uuid = (__le32 *)c->uuid;

	free = ubifs_get_free_space(c);
	dbg_gen("free space %lld bytes (%lld blocks)",
		free, free >> UBIFS_BLOCK_SHIFT);

	buf->f_type = UBIFS_SUPER_MAGIC;
	buf->f_bsize = UBIFS_BLOCK_SIZE;
	buf->f_blocks = c->block_cnt;
	buf->f_bfree = free >> UBIFS_BLOCK_SHIFT;
	if (free > c->report_rp_size)
		buf->f_bavail = (free - c->report_rp_size) >> UBIFS_BLOCK_SHIFT;
	else
		buf->f_bavail = 0;
	buf->f_files = 0;
	buf->f_ffree = 0;
	buf->f_namelen = UBIFS_MAX_NLEN;
	buf->f_fsid.val[0] = le32_to_cpu(uuid[0]) ^ le32_to_cpu(uuid[2]);
	buf->f_fsid.val[1] = le32_to_cpu(uuid[1]) ^ le32_to_cpu(uuid[3]);
	ubifs_assert(c, buf->f_bfree <= c->block_cnt);
	return 0;
}

static int ubifs_show_options(struct seq_file *s, struct dentry *root)
{
	struct ubifs_info *c = root->d_sb->s_fs_info;

	if (c->mount_opts.unmount_mode == 2)
		seq_puts(s, ",fast_unmount");
	else if (c->mount_opts.unmount_mode == 1)
		seq_puts(s, ",norm_unmount");

	if (c->mount_opts.bulk_read == 2)
		seq_puts(s, ",bulk_read");
	else if (c->mount_opts.bulk_read == 1)
		seq_puts(s, ",no_bulk_read");

	if (c->mount_opts.chk_data_crc == 2)
		seq_puts(s, ",chk_data_crc");
	else if (c->mount_opts.chk_data_crc == 1)
		seq_puts(s, ",no_chk_data_crc");

	if (c->mount_opts.override_compr) {
		seq_printf(s, ",compr=%s",
			   ubifs_compr_name(c, c->mount_opts.compr_type));
	}

	seq_printf(s, ",assert=%s", ubifs_assert_action_name(c));
	seq_printf(s, ",ubi=%d,vol=%d", c->vi.ubi_num, c->vi.vol_id);

	return 0;
}

static int ubifs_sync_fs(struct super_block *sb, int wait)
{
	int i, err;
	struct ubifs_info *c = sb->s_fs_info;

	/*
	 * Zero @wait is just an advisory thing to help the file system shove
	 * lots of data into the queues, and there will be the second
	 * '->sync_fs()' call, with non-zero @wait.
	 */
	if (!wait)
		return 0;

	/*
	 * Synchronize write buffers, because 'ubifs_run_commit()' does not
	 * do this if it waits for an already running commit.
	 */
	for (i = 0; i < c->jhead_cnt; i++) {
		err = ubifs_wbuf_sync(&c->jheads[i].wbuf);
		if (err)
			return err;
	}

	/*
	 * Strictly speaking, it is not necessary to commit the journal here,
	 * synchronizing write-buffers would be enough. But committing makes
	 * UBIFS free space predictions much more accurate, so we want to let
	 * the user be able to get more accurate results of 'statfs()' after
	 * they synchronize the file system.
	 */
	err = ubifs_run_commit(c);
	if (err)
		return err;

	return ubi_sync(c->vi.ubi_num);
}

/**
 * init_constants_early - initialize UBIFS constants.
 * @c: UBIFS file-system description object
 *
 * This function initialize UBIFS constants which do not need the superblock to
 * be read. It also checks that the UBI volume satisfies basic UBIFS
 * requirements. Returns zero in case of success and a negative error code in
 * case of failure.
 */
static int init_constants_early(struct ubifs_info *c)
{
	if (c->vi.corrupted) {
		ubifs_warn(c, "UBI volume is corrupted - read-only mode");
		c->ro_media = 1;
	}

	if (c->di.ro_mode) {
		ubifs_msg(c, "read-only UBI device");
		c->ro_media = 1;
	}

	if (c->vi.vol_type == UBI_STATIC_VOLUME) {
		ubifs_msg(c, "static UBI volume - read-only mode");
		c->ro_media = 1;
	}

	c->leb_cnt = c->vi.size;
	c->leb_size = c->vi.usable_leb_size;
	c->leb_start = c->di.leb_start;
	c->half_leb_size = c->leb_size / 2;
	c->min_io_size = c->di.min_io_size;
	c->min_io_shift = fls(c->min_io_size) - 1;
	c->max_write_size = c->di.max_write_size;
	c->max_write_shift = fls(c->max_write_size) - 1;

	if (c->leb_size < UBIFS_MIN_LEB_SZ) {
		ubifs_errc(c, "too small LEBs (%d bytes), min. is %d bytes",
			   c->leb_size, UBIFS_MIN_LEB_SZ);
		return -EINVAL;
	}

	if (c->leb_cnt < UBIFS_MIN_LEB_CNT) {
		ubifs_errc(c, "too few LEBs (%d), min. is %d",
			   c->leb_cnt, UBIFS_MIN_LEB_CNT);
		return -EINVAL;
	}

	if (!is_power_of_2(c->min_io_size)) {
		ubifs_errc(c, "bad min. I/O size %d", c->min_io_size);
		return -EINVAL;
	}

	/*
	 * Maximum write size has to be greater or equivalent to min. I/O
	 * size, and be multiple of min. I/O size.
	 */
	if (c->max_write_size < c->min_io_size ||
	    c->max_write_size % c->min_io_size ||
	    !is_power_of_2(c->max_write_size)) {
		ubifs_errc(c, "bad write buffer size %d for %d min. I/O unit",
			   c->max_write_size, c->min_io_size);
		return -EINVAL;
	}

	/*
	 * UBIFS aligns all node to 8-byte boundary, so to make function in
	 * io.c simpler, assume minimum I/O unit size to be 8 bytes if it is
	 * less than 8.
	 */
	if (c->min_io_size < 8) {
		c->min_io_size = 8;
		c->min_io_shift = 3;
		if (c->max_write_size < c->min_io_size) {
			c->max_write_size = c->min_io_size;
			c->max_write_shift = c->min_io_shift;
		}
	}

	c->ref_node_alsz = ALIGN(UBIFS_REF_NODE_SZ, c->min_io_size);
	c->mst_node_alsz = ALIGN(UBIFS_MST_NODE_SZ, c->min_io_size);

	/*
	 * Initialize node length ranges which are mostly needed for node
	 * length validation.
	 */
	c->ranges[UBIFS_PAD_NODE].len  = UBIFS_PAD_NODE_SZ;
	c->ranges[UBIFS_SB_NODE].len   = UBIFS_SB_NODE_SZ;
	c->ranges[UBIFS_MST_NODE].len  = UBIFS_MST_NODE_SZ;
	c->ranges[UBIFS_REF_NODE].len  = UBIFS_REF_NODE_SZ;
	c->ranges[UBIFS_TRUN_NODE].len = UBIFS_TRUN_NODE_SZ;
	c->ranges[UBIFS_CS_NODE].len   = UBIFS_CS_NODE_SZ;
	c->ranges[UBIFS_AUTH_NODE].min_len = UBIFS_AUTH_NODE_SZ;
	c->ranges[UBIFS_AUTH_NODE].max_len = UBIFS_AUTH_NODE_SZ +
				UBIFS_MAX_HMAC_LEN;
	c->ranges[UBIFS_SIG_NODE].min_len = UBIFS_SIG_NODE_SZ;
	c->ranges[UBIFS_SIG_NODE].max_len = c->leb_size - UBIFS_SB_NODE_SZ;

	c->ranges[UBIFS_INO_NODE].min_len  = UBIFS_INO_NODE_SZ;
	c->ranges[UBIFS_INO_NODE].max_len  = UBIFS_MAX_INO_NODE_SZ;
	c->ranges[UBIFS_ORPH_NODE].min_len =
				UBIFS_ORPH_NODE_SZ + sizeof(__le64);
	c->ranges[UBIFS_ORPH_NODE].max_len = c->leb_size;
	c->ranges[UBIFS_DENT_NODE].min_len = UBIFS_DENT_NODE_SZ;
	c->ranges[UBIFS_DENT_NODE].max_len = UBIFS_MAX_DENT_NODE_SZ;
	c->ranges[UBIFS_XENT_NODE].min_len = UBIFS_XENT_NODE_SZ;
	c->ranges[UBIFS_XENT_NODE].max_len = UBIFS_MAX_XENT_NODE_SZ;
	c->ranges[UBIFS_DATA_NODE].min_len = UBIFS_DATA_NODE_SZ;
	c->ranges[UBIFS_DATA_NODE].max_len = UBIFS_MAX_DATA_NODE_SZ;
	/*
	 * Minimum indexing node size is amended later when superblock is
	 * read and the key length is known.
	 */
	c->ranges[UBIFS_IDX_NODE].min_len = UBIFS_IDX_NODE_SZ + UBIFS_BRANCH_SZ;
	/*
	 * Maximum indexing node size is amended later when superblock is
	 * read and the fanout is known.
	 */
	c->ranges[UBIFS_IDX_NODE].max_len = INT_MAX;

	/*
	 * Initialize dead and dark LEB space watermarks. See gc.c for comments
	 * about these values.
	 */
	c->dead_wm = ALIGN(MIN_WRITE_SZ, c->min_io_size);
	c->dark_wm = ALIGN(UBIFS_MAX_NODE_SZ, c->min_io_size);

	/*
	 * Calculate how many bytes would be wasted at the end of LEB if it was
	 * fully filled with data nodes of maximum size. This is used in
	 * calculations when reporting free space.
	 */
	c->leb_overhead = c->leb_size % UBIFS_MAX_DATA_NODE_SZ;

	/* Buffer size for bulk-reads */
	c->max_bu_buf_len = UBIFS_MAX_BULK_READ * UBIFS_MAX_DATA_NODE_SZ;
	if (c->max_bu_buf_len > c->leb_size)
		c->max_bu_buf_len = c->leb_size;

	/* Log is ready, preserve one LEB for commits. */
	c->min_log_bytes = c->leb_size;

	return 0;
}

/**
 * bud_wbuf_callback - bud LEB write-buffer synchronization call-back.
 * @c: UBIFS file-system description object
 * @lnum: LEB the write-buffer was synchronized to
 * @free: how many free bytes left in this LEB
 * @pad: how many bytes were padded
 *
 * This is a callback function which is called by the I/O unit when the
 * write-buffer is synchronized. We need this to correctly maintain space
 * accounting in bud logical eraseblocks. This function returns zero in case of
 * success and a negative error code in case of failure.
 *
 * This function actually belongs to the journal, but we keep it here because
 * we want to keep it static.
 */
static int bud_wbuf_callback(struct ubifs_info *c, int lnum, int free, int pad)
{
	return ubifs_update_one_lp(c, lnum, free, pad, 0, 0);
}

/*
 * init_constants_sb - initialize UBIFS constants.
 * @c: UBIFS file-system description object
 *
 * This is a helper function which initializes various UBIFS constants after
 * the superblock has been read. It also checks various UBIFS parameters and
 * makes sure they are all right. Returns zero in case of success and a
 * negative error code in case of failure.
 */
static int init_constants_sb(struct ubifs_info *c)
{
	int tmp, err;
	long long tmp64;

	c->main_bytes = (long long)c->main_lebs * c->leb_size;
	c->max_znode_sz = sizeof(struct ubifs_znode) +
				c->fanout * sizeof(struct ubifs_zbranch);

	tmp = ubifs_idx_node_sz(c, 1);
	c->ranges[UBIFS_IDX_NODE].min_len = tmp;
	c->min_idx_node_sz = ALIGN(tmp, 8);

	tmp = ubifs_idx_node_sz(c, c->fanout);
	c->ranges[UBIFS_IDX_NODE].max_len = tmp;
	c->max_idx_node_sz = ALIGN(tmp, 8);

	/* Make sure LEB size is large enough to fit full commit */
	tmp = UBIFS_CS_NODE_SZ + UBIFS_REF_NODE_SZ * c->jhead_cnt;
	tmp = ALIGN(tmp, c->min_io_size);
	if (tmp > c->leb_size) {
		ubifs_err(c, "too small LEB size %d, at least %d needed",
			  c->leb_size, tmp);
		return -EINVAL;
	}

	/*
	 * Make sure that the log is large enough to fit reference nodes for
	 * all buds plus one reserved LEB.
	 */
	tmp64 = c->max_bud_bytes + c->leb_size - 1;
	c->max_bud_cnt = div_u64(tmp64, c->leb_size);
	tmp = (c->ref_node_alsz * c->max_bud_cnt + c->leb_size - 1);
	tmp /= c->leb_size;
	tmp += 1;
	if (c->log_lebs < tmp) {
		ubifs_err(c, "too small log %d LEBs, required min. %d LEBs",
			  c->log_lebs, tmp);
		return -EINVAL;
	}

	/*
	 * When budgeting we assume worst-case scenarios when the pages are not
	 * be compressed and direntries are of the maximum size.
	 *
	 * Note, data, which may be stored in inodes is budgeted separately, so
	 * it is not included into 'c->bi.inode_budget'.
	 */
	c->bi.page_budget = UBIFS_MAX_DATA_NODE_SZ * UBIFS_BLOCKS_PER_PAGE;
	c->bi.inode_budget = UBIFS_INO_NODE_SZ;
	c->bi.dent_budget = UBIFS_MAX_DENT_NODE_SZ;

	/*
	 * When the amount of flash space used by buds becomes
	 * 'c->max_bud_bytes', UBIFS just blocks all writers and starts commit.
	 * The writers are unblocked when the commit is finished. To avoid
	 * writers to be blocked UBIFS initiates background commit in advance,
	 * when number of bud bytes becomes above the limit defined below.
	 */
	c->bg_bud_bytes = (c->max_bud_bytes * 13) >> 4;

	/*
	 * Ensure minimum journal size. All the bytes in the journal heads are
	 * considered to be used, when calculating the current journal usage.
	 * Consequently, if the journal is too small, UBIFS will treat it as
	 * always full.
	 */
	tmp64 = (long long)(c->jhead_cnt + 1) * c->leb_size + 1;
	if (c->bg_bud_bytes < tmp64)
		c->bg_bud_bytes = tmp64;
	if (c->max_bud_bytes < tmp64 + c->leb_size)
		c->max_bud_bytes = tmp64 + c->leb_size;

	err = ubifs_calc_lpt_geom(c);
	if (err)
		return err;

	/* Initialize effective LEB size used in budgeting calculations */
	c->idx_leb_size = c->leb_size - c->max_idx_node_sz;
	return 0;
}

/*
 * init_constants_master - initialize UBIFS constants.
 * @c: UBIFS file-system description object
 *
 * This is a helper function which initializes various UBIFS constants after
 * the master node has been read. It also checks various UBIFS parameters and
 * makes sure they are all right.
 */
static void init_constants_master(struct ubifs_info *c)
{
	long long tmp64;

	c->bi.min_idx_lebs = ubifs_calc_min_idx_lebs(c);
	c->report_rp_size = ubifs_reported_space(c, c->rp_size);

	/*
	 * Calculate total amount of FS blocks. This number is not used
	 * internally because it does not make much sense for UBIFS, but it is
	 * necessary to report something for the 'statfs()' call.
	 *
	 * Subtract the LEB reserved for GC, the LEB which is reserved for
	 * deletions, minimum LEBs for the index, the LEBs which are reserved
	 * for each journal head.
	 */
	tmp64 = c->main_lebs - 1 - 1 - MIN_INDEX_LEBS - c->jhead_cnt;
	tmp64 *= (long long)c->leb_size - c->leb_overhead;
	tmp64 = ubifs_reported_space(c, tmp64);
	c->block_cnt = tmp64 >> UBIFS_BLOCK_SHIFT;
}

/**
 * take_gc_lnum - reserve GC LEB.
 * @c: UBIFS file-system description object
 *
 * This function ensures that the LEB reserved for garbage collection is marked
 * as "taken" in lprops. We also have to set free space to LEB size and dirty
 * space to zero, because lprops may contain out-of-date information if the
 * file-system was un-mounted before it has been committed. This function
 * returns zero in case of success and a negative error code in case of
 * failure.
 */
static int take_gc_lnum(struct ubifs_info *c)
{
	int err;

	if (c->gc_lnum == -1) {
		ubifs_err(c, "no LEB for GC");
		return -EINVAL;
	}

	/* And we have to tell lprops that this LEB is taken */
	err = ubifs_change_one_lp(c, c->gc_lnum, c->leb_size, 0,
				  LPROPS_TAKEN, 0, 0);
	return err;
}

/**
 * alloc_wbufs - allocate write-buffers.
 * @c: UBIFS file-system description object
 *
 * This helper function allocates and initializes UBIFS write-buffers. Returns
 * zero in case of success and %-ENOMEM in case of failure.
 */
static int alloc_wbufs(struct ubifs_info *c)
{
	int i, err;

	c->jheads = kcalloc(c->jhead_cnt, sizeof(struct ubifs_jhead),
			    GFP_KERNEL);
	if (!c->jheads)
		return -ENOMEM;

	/* Initialize journal heads */
	for (i = 0; i < c->jhead_cnt; i++) {
		INIT_LIST_HEAD(&c->jheads[i].buds_list);
		err = ubifs_wbuf_init(c, &c->jheads[i].wbuf);
		if (err)
			goto out_wbuf;

		c->jheads[i].wbuf.sync_callback = &bud_wbuf_callback;
		c->jheads[i].wbuf.jhead = i;
		c->jheads[i].grouped = 1;
		c->jheads[i].log_hash = ubifs_hash_get_desc(c);
		if (IS_ERR(c->jheads[i].log_hash)) {
			err = PTR_ERR(c->jheads[i].log_hash);
			goto out_log_hash;
		}
	}

	/*
	 * Garbage Collector head does not need to be synchronized by timer.
	 * Also GC head nodes are not grouped.
	 */
	c->jheads[GCHD].wbuf.no_timer = 1;
	c->jheads[GCHD].grouped = 0;

	return 0;

out_log_hash:
	kfree(c->jheads[i].wbuf.buf);
	kfree(c->jheads[i].wbuf.inodes);

out_wbuf:
	while (i--) {
		kfree(c->jheads[i].wbuf.buf);
		kfree(c->jheads[i].wbuf.inodes);
		kfree(c->jheads[i].log_hash);
	}
	kfree(c->jheads);
	c->jheads = NULL;

	return err;
}

/**
 * free_wbufs - free write-buffers.
 * @c: UBIFS file-system description object
 */
static void free_wbufs(struct ubifs_info *c)
{
	int i;

	if (c->jheads) {
		for (i = 0; i < c->jhead_cnt; i++) {
			kfree(c->jheads[i].wbuf.buf);
			kfree(c->jheads[i].wbuf.inodes);
			kfree(c->jheads[i].log_hash);
		}
		kfree(c->jheads);
		c->jheads = NULL;
	}
}

/**
 * free_orphans - free orphans.
 * @c: UBIFS file-system description object
 */
static void free_orphans(struct ubifs_info *c)
{
	struct ubifs_orphan *orph;

	while (c->orph_dnext) {
		orph = c->orph_dnext;
		c->orph_dnext = orph->dnext;
		list_del(&orph->list);
		kfree(orph);
	}

	while (!list_empty(&c->orph_list)) {
		orph = list_entry(c->orph_list.next, struct ubifs_orphan, list);
		list_del(&orph->list);
		kfree(orph);
		ubifs_err(c, "orphan list not empty at unmount");
	}

	vfree(c->orph_buf);
	c->orph_buf = NULL;
}

/**
 * free_buds - free per-bud objects.
 * @c: UBIFS file-system description object
 */
static void free_buds(struct ubifs_info *c)
{
	struct ubifs_bud *bud, *n;

	rbtree_postorder_for_each_entry_safe(bud, n, &c->buds, rb) {
		kfree(bud->log_hash);
		kfree(bud);
	}
}

/**
 * check_volume_empty - check if the UBI volume is empty.
 * @c: UBIFS file-system description object
 *
 * This function checks if the UBIFS volume is empty by looking if its LEBs are
 * mapped or not. The result of checking is stored in the @c->empty variable.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int check_volume_empty(struct ubifs_info *c)
{
	int lnum, err;

	c->empty = 1;
	for (lnum = 0; lnum < c->leb_cnt; lnum++) {
		err = ubifs_is_mapped(c, lnum);
		if (unlikely(err < 0))
			return err;
		if (err == 1) {
			c->empty = 0;
			break;
		}

		cond_resched();
	}

	return 0;
}

/*
 * UBIFS mount options.
 *
 * Opt_fast_unmount: do not run a journal commit before un-mounting
 * Opt_norm_unmount: run a journal commit before un-mounting
 * Opt_bulk_read: enable bulk-reads
 * Opt_no_bulk_read: disable bulk-reads
 * Opt_chk_data_crc: check CRCs when reading data nodes
 * Opt_no_chk_data_crc: do not check CRCs when reading data nodes
 * Opt_override_compr: override default compressor
 * Opt_assert: set ubifs_assert() action
 * Opt_auth_key: The key name used for authentication
 * Opt_auth_hash_name: The hash type used for authentication
 * Opt_err: just end of array marker
 */
enum {
	Opt_fast_unmount,
	Opt_norm_unmount,
	Opt_bulk_read,
	Opt_no_bulk_read,
	Opt_chk_data_crc,
	Opt_no_chk_data_crc,
	Opt_override_compr,
	Opt_assert,
	Opt_auth_key,
	Opt_auth_hash_name,
	Opt_ignore,
	Opt_err,
};

static const match_table_t tokens = {
	{Opt_fast_unmount, "fast_unmount"},
	{Opt_norm_unmount, "norm_unmount"},
	{Opt_bulk_read, "bulk_read"},
	{Opt_no_bulk_read, "no_bulk_read"},
	{Opt_chk_data_crc, "chk_data_crc"},
	{Opt_no_chk_data_crc, "no_chk_data_crc"},
	{Opt_override_compr, "compr=%s"},
	{Opt_auth_key, "auth_key=%s"},
	{Opt_auth_hash_name, "auth_hash_name=%s"},
	{Opt_ignore, "ubi=%s"},
	{Opt_ignore, "vol=%s"},
	{Opt_assert, "assert=%s"},
	{Opt_err, NULL},
};

/**
 * parse_standard_option - parse a standard mount option.
 * @option: the option to parse
 *
 * Normally, standard mount options like "sync" are passed to file-systems as
 * flags. However, when a "rootflags=" kernel boot parameter is used, they may
 * be present in the options string. This function tries to deal with this
 * situation and parse standard options. Returns 0 if the option was not
 * recognized, and the corresponding integer flag if it was.
 *
 * UBIFS is only interested in the "sync" option, so do not check for anything
 * else.
 */
static int parse_standard_option(const char *option)
{

	pr_notice("UBIFS: parse %s\n", option);
	if (!strcmp(option, "sync"))
		return SB_SYNCHRONOUS;
	return 0;
}

/**
 * ubifs_parse_options - parse mount parameters.
 * @c: UBIFS file-system description object
 * @options: parameters to parse
 * @is_remount: non-zero if this is FS re-mount
 *
 * This function parses UBIFS mount options and returns zero in case success
 * and a negative error code in case of failure.
 */
static int ubifs_parse_options(struct ubifs_info *c, char *options,
			       int is_remount)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];

	if (!options)
		return 0;

	while ((p = strsep(&options, ","))) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		/*
		 * %Opt_fast_unmount and %Opt_norm_unmount options are ignored.
		 * We accept them in order to be backward-compatible. But this
		 * should be removed at some point.
		 */
		case Opt_fast_unmount:
			c->mount_opts.unmount_mode = 2;
			break;
		case Opt_norm_unmount:
			c->mount_opts.unmount_mode = 1;
			break;
		case Opt_bulk_read:
			c->mount_opts.bulk_read = 2;
			c->bulk_read = 1;
			break;
		case Opt_no_bulk_read:
			c->mount_opts.bulk_read = 1;
			c->bulk_read = 0;
			break;
		case Opt_chk_data_crc:
			c->mount_opts.chk_data_crc = 2;
			c->no_chk_data_crc = 0;
			break;
		case Opt_no_chk_data_crc:
			c->mount_opts.chk_data_crc = 1;
			c->no_chk_data_crc = 1;
			break;
		case Opt_override_compr:
		{
			char *name = match_strdup(&args[0]);

			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "none"))
				c->mount_opts.compr_type = UBIFS_COMPR_NONE;
			else if (!strcmp(name, "lzo"))
				c->mount_opts.compr_type = UBIFS_COMPR_LZO;
			else if (!strcmp(name, "zlib"))
				c->mount_opts.compr_type = UBIFS_COMPR_ZLIB;
			else if (!strcmp(name, "zstd"))
				c->mount_opts.compr_type = UBIFS_COMPR_ZSTD;
			else {
				ubifs_err(c, "unknown compressor \"%s\"", name); //FIXME: is c ready?
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			c->mount_opts.override_compr = 1;
			c->default_compr = c->mount_opts.compr_type;
			break;
		}
		case Opt_assert:
		{
			char *act = match_strdup(&args[0]);

			if (!act)
				return -ENOMEM;
			if (!strcmp(act, "report"))
				c->assert_action = ASSACT_REPORT;
			else if (!strcmp(act, "read-only"))
				c->assert_action = ASSACT_RO;
			else if (!strcmp(act, "panic"))
				c->assert_action = ASSACT_PANIC;
			else {
				ubifs_err(c, "unknown assert action \"%s\"", act);
				kfree(act);
				return -EINVAL;
			}
			kfree(act);
			break;
		}
		case Opt_auth_key:
			if (!is_remount) {
				c->auth_key_name = kstrdup(args[0].from,
								GFP_KERNEL);
				if (!c->auth_key_name)
					return -ENOMEM;
			}
			break;
		case Opt_auth_hash_name:
			if (!is_remount) {
				c->auth_hash_name = kstrdup(args[0].from,
								GFP_KERNEL);
				if (!c->auth_hash_name)
					return -ENOMEM;
			}
			break;
		case Opt_ignore:
			break;
		default:
		{
			unsigned long flag;
			struct super_block *sb = c->vfs_sb;

			flag = parse_standard_option(p);
			if (!flag) {
				ubifs_err(c, "unrecognized mount option \"%s\" or missing value",
					  p);
				return -EINVAL;
			}
			sb->s_flags |= flag;
			break;
		}
		}
	}

	return 0;
}

/*
 * ubifs_release_options - release mount parameters which have been dumped.
 * @c: UBIFS file-system description object
 */
static void ubifs_release_options(struct ubifs_info *c)
{
	kfree(c->auth_key_name);
	c->auth_key_name = NULL;
	kfree(c->auth_hash_name);
	c->auth_hash_name = NULL;
}

/**
 * destroy_journal - destroy journal data structures.
 * @c: UBIFS file-system description object
 *
 * This function destroys journal data structures including those that may have
 * been created by recovery functions.
 */
static void destroy_journal(struct ubifs_info *c)
{
	while (!list_empty(&c->unclean_leb_list)) {
		struct ubifs_unclean_leb *ucleb;

		ucleb = list_entry(c->unclean_leb_list.next,
				   struct ubifs_unclean_leb, list);
		list_del(&ucleb->list);
		kfree(ucleb);
	}
	while (!list_empty(&c->old_buds)) {
		struct ubifs_bud *bud;

		bud = list_entry(c->old_buds.next, struct ubifs_bud, list);
		list_del(&bud->list);
		kfree(bud->log_hash);
		kfree(bud);
	}
	ubifs_destroy_idx_gc(c);
	ubifs_destroy_size_tree(c);
	ubifs_tnc_close(c);
	free_buds(c);
}

/**
 * bu_init - initialize bulk-read information.
 * @c: UBIFS file-system description object
 */
static void bu_init(struct ubifs_info *c)
{
	ubifs_assert(c, c->bulk_read == 1);

	if (c->bu.buf)
		return; /* Already initialized */

again:
	c->bu.buf = kmalloc(c->max_bu_buf_len, GFP_KERNEL | __GFP_NOWARN);
	if (!c->bu.buf) {
		if (c->max_bu_buf_len > UBIFS_KMALLOC_OK) {
			c->max_bu_buf_len = UBIFS_KMALLOC_OK;
			goto again;
		}

		/* Just disable bulk-read */
		ubifs_warn(c, "cannot allocate %d bytes of memory for bulk-read, disabling it",
			   c->max_bu_buf_len);
		c->mount_opts.bulk_read = 1;
		c->bulk_read = 0;
		return;
	}
}

/**
 * check_free_space - check if there is enough free space to mount.
 * @c: UBIFS file-system description object
 *
 * This function makes sure UBIFS has enough free space to be mounted in
 * read/write mode. UBIFS must always have some free space to allow deletions.
 */
static int check_free_space(struct ubifs_info *c)
{
	ubifs_assert(c, c->dark_wm > 0);
	if (c->lst.total_free + c->lst.total_dirty < c->dark_wm) {
		ubifs_err(c, "insufficient free space to mount in R/W mode");
		ubifs_dump_budg(c, &c->bi);
		ubifs_dump_lprops(c);
		return -ENOSPC;
	}
	return 0;
}

/**
 * mount_ubifs - mount UBIFS file-system.
 * @c: UBIFS file-system description object
 *
 * This function mounts UBIFS file system. Returns zero in case of success and
 * a negative error code in case of failure.
 */
static int mount_ubifs(struct ubifs_info *c)
{
	int err;
	long long x, y;
	size_t sz;

	c->ro_mount = !!sb_rdonly(c->vfs_sb);
	/* Suppress error messages while probing if SB_SILENT is set */
	c->probing = !!(c->vfs_sb->s_flags & SB_SILENT);

	err = init_constants_early(c);
	if (err)
		return err;

	err = ubifs_debugging_init(c);
	if (err)
		return err;

	err = ubifs_sysfs_register(c);
	if (err)
		goto out_debugging;

	err = check_volume_empty(c);
	if (err)
		goto out_free;

	if (c->empty && (c->ro_mount || c->ro_media)) {
		/*
		 * This UBI volume is empty, and read-only, or the file system
		 * is mounted read-only - we cannot format it.
		 */
		ubifs_err(c, "can't format empty UBI volume: read-only %s",
			  c->ro_media ? "UBI volume" : "mount");
		err = -EROFS;
		goto out_free;
	}

	if (c->ro_media && !c->ro_mount) {
		ubifs_err(c, "cannot mount read-write - read-only media");
		err = -EROFS;
		goto out_free;
	}

	/*
	 * The requirement for the buffer is that it should fit indexing B-tree
	 * height amount of integers. We assume the height if the TNC tree will
	 * never exceed 64.
	 */
	err = -ENOMEM;
	c->bottom_up_buf = kmalloc_array(BOTTOM_UP_HEIGHT, sizeof(int),
					 GFP_KERNEL);
	if (!c->bottom_up_buf)
		goto out_free;

	c->sbuf = vmalloc(c->leb_size);
	if (!c->sbuf)
		goto out_free;

	if (!c->ro_mount) {
		c->ileb_buf = vmalloc(c->leb_size);
		if (!c->ileb_buf)
			goto out_free;
	}

	if (c->bulk_read == 1)
		bu_init(c);

	if (!c->ro_mount) {
		c->write_reserve_buf = kmalloc(COMPRESSED_DATA_NODE_BUF_SZ + \
					       UBIFS_CIPHER_BLOCK_SIZE,
					       GFP_KERNEL);
		if (!c->write_reserve_buf)
			goto out_free;
	}

	c->mounting = 1;

	if (c->auth_key_name) {
		if (IS_ENABLED(CONFIG_UBIFS_FS_AUTHENTICATION)) {
			err = ubifs_init_authentication(c);
			if (err)
				goto out_free;
		} else {
			ubifs_err(c, "auth_key_name, but UBIFS is built without"
				  " authentication support");
			err = -EINVAL;
			goto out_free;
		}
	}

	err = ubifs_read_superblock(c);
	if (err)
		goto out_auth;

	c->probing = 0;

	/*
	 * Make sure the compressor which is set as default in the superblock
	 * or overridden by mount options is actually compiled in.
	 */
	if (!ubifs_compr_present(c, c->default_compr)) {
		ubifs_err(c, "'compressor \"%s\" is not compiled in",
			  ubifs_compr_name(c, c->default_compr));
		err = -ENOTSUPP;
		goto out_auth;
	}

	err = init_constants_sb(c);
	if (err)
		goto out_auth;

	sz = ALIGN(c->max_idx_node_sz, c->min_io_size) * 2;
	c->cbuf = kmalloc(sz, GFP_NOFS);
	if (!c->cbuf) {
		err = -ENOMEM;
		goto out_auth;
	}

	err = alloc_wbufs(c);
	if (err)
		goto out_cbuf;

	sprintf(c->bgt_name, BGT_NAME_PATTERN, c->vi.ubi_num, c->vi.vol_id);
	if (!c->ro_mount) {
		/* Create background thread */
		c->bgt = kthread_run(ubifs_bg_thread, c, "%s", c->bgt_name);
		if (IS_ERR(c->bgt)) {
			err = PTR_ERR(c->bgt);
			c->bgt = NULL;
			ubifs_err(c, "cannot spawn \"%s\", error %d",
				  c->bgt_name, err);
			goto out_wbufs;
		}
	}

	err = ubifs_read_master(c);
	if (err)
		goto out_master;

	init_constants_master(c);

	if ((c->mst_node->flags & cpu_to_le32(UBIFS_MST_DIRTY)) != 0) {
		ubifs_msg(c, "recovery needed");
		c->need_recovery = 1;
	}

	if (c->need_recovery && !c->ro_mount) {
		err = ubifs_recover_inl_heads(c, c->sbuf);
		if (err)
			goto out_master;
	}

	err = ubifs_lpt_init(c, 1, !c->ro_mount);
	if (err)
		goto out_master;

	if (!c->ro_mount && c->space_fixup) {
		err = ubifs_fixup_free_space(c);
		if (err)
			goto out_lpt;
	}

	if (!c->ro_mount && !c->need_recovery) {
		/*
		 * Set the "dirty" flag so that if we reboot uncleanly we
		 * will notice this immediately on the next mount.
		 */
		c->mst_node->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
		err = ubifs_write_master(c);
		if (err)
			goto out_lpt;
	}

	/*
	 * Handle offline signed images: Now that the master node is
	 * written and its validation no longer depends on the hash
	 * in the superblock, we can update the offline signed
	 * superblock with a HMAC version,
	 */
	if (ubifs_authenticated(c) && ubifs_hmac_zero(c, c->sup_node->hmac)) {
		err = ubifs_hmac_wkm(c, c->sup_node->hmac_wkm);
		if (err)
			goto out_lpt;
		c->superblock_need_write = 1;
	}

	if (!c->ro_mount && c->superblock_need_write) {
		err = ubifs_write_sb_node(c, c->sup_node);
		if (err)
			goto out_lpt;
		c->superblock_need_write = 0;
	}

	err = dbg_check_idx_size(c, c->bi.old_idx_sz);
	if (err)
		goto out_lpt;

	err = ubifs_replay_journal(c);
	if (err)
		goto out_journal;

	/* Calculate 'min_idx_lebs' after journal replay */
	c->bi.min_idx_lebs = ubifs_calc_min_idx_lebs(c);

	err = ubifs_mount_orphans(c, c->need_recovery, c->ro_mount);
	if (err)
		goto out_orphans;

	if (!c->ro_mount) {
		int lnum;

		err = check_free_space(c);
		if (err)
			goto out_orphans;

		/* Check for enough log space */
		lnum = c->lhead_lnum + 1;
		if (lnum >= UBIFS_LOG_LNUM + c->log_lebs)
			lnum = UBIFS_LOG_LNUM;
		if (lnum == c->ltail_lnum) {
			err = ubifs_consolidate_log(c);
			if (err)
				goto out_orphans;
		}

		if (c->need_recovery) {
			if (!ubifs_authenticated(c)) {
				err = ubifs_recover_size(c, true);
				if (err)
					goto out_orphans;
			}

			err = ubifs_rcvry_gc_commit(c);
			if (err)
				goto out_orphans;

			if (ubifs_authenticated(c)) {
				err = ubifs_recover_size(c, false);
				if (err)
					goto out_orphans;
			}
		} else {
			err = take_gc_lnum(c);
			if (err)
				goto out_orphans;

			/*
			 * GC LEB may contain garbage if there was an unclean
			 * reboot, and it should be un-mapped.
			 */
			err = ubifs_leb_unmap(c, c->gc_lnum);
			if (err)
				goto out_orphans;
		}

		err = dbg_check_lprops(c);
		if (err)
			goto out_orphans;
	} else if (c->need_recovery) {
		err = ubifs_recover_size(c, false);
		if (err)
			goto out_orphans;
	} else {
		/*
		 * Even if we mount read-only, we have to set space in GC LEB
		 * to proper value because this affects UBIFS free space
		 * reporting. We do not want to have a situation when
		 * re-mounting from R/O to R/W changes amount of free space.
		 */
		err = take_gc_lnum(c);
		if (err)
			goto out_orphans;
	}

	spin_lock(&ubifs_infos_lock);
	list_add_tail(&c->infos_list, &ubifs_infos);
	spin_unlock(&ubifs_infos_lock);

	if (c->need_recovery) {
		if (c->ro_mount)
			ubifs_msg(c, "recovery deferred");
		else {
			c->need_recovery = 0;
			ubifs_msg(c, "recovery completed");
			/*
			 * GC LEB has to be empty and taken at this point. But
			 * the journal head LEBs may also be accounted as
			 * "empty taken" if they are empty.
			 */
			ubifs_assert(c, c->lst.taken_empty_lebs > 0);
		}
	} else
		ubifs_assert(c, c->lst.taken_empty_lebs > 0);

	err = dbg_check_filesystem(c);
	if (err)
		goto out_infos;

	dbg_debugfs_init_fs(c);

	c->mounting = 0;

	ubifs_msg(c, "UBIFS: mounted UBI device %d, volume %d, name \"%s\"%s",
		  c->vi.ubi_num, c->vi.vol_id, c->vi.name,
		  c->ro_mount ? ", R/O mode" : "");
	x = (long long)c->main_lebs * c->leb_size;
	y = (long long)c->log_lebs * c->leb_size + c->max_bud_bytes;
	ubifs_msg(c, "LEB size: %d bytes (%d KiB), min./max. I/O unit sizes: %d bytes/%d bytes",
		  c->leb_size, c->leb_size >> 10, c->min_io_size,
		  c->max_write_size);
	ubifs_msg(c, "FS size: %lld bytes (%lld MiB, %d LEBs), max %d LEBs, journal size %lld bytes (%lld MiB, %d LEBs)",
		  x, x >> 20, c->main_lebs, c->max_leb_cnt,
		  y, y >> 20, c->log_lebs + c->max_bud_cnt);
	ubifs_msg(c, "reserved for root: %llu bytes (%llu KiB)",
		  c->report_rp_size, c->report_rp_size >> 10);
	ubifs_msg(c, "media format: w%d/r%d (latest is w%d/r%d), UUID %pUB%s",
		  c->fmt_version, c->ro_compat_version,
		  UBIFS_FORMAT_VERSION, UBIFS_RO_COMPAT_VERSION, c->uuid,
		  c->big_lpt ? ", big LPT model" : ", small LPT model");

	dbg_gen("default compressor:  %s", ubifs_compr_name(c, c->default_compr));
	dbg_gen("data journal heads:  %d",
		c->jhead_cnt - NONDATA_JHEADS_CNT);
	dbg_gen("log LEBs:            %d (%d - %d)",
		c->log_lebs, UBIFS_LOG_LNUM, c->log_last);
	dbg_gen("LPT area LEBs:       %d (%d - %d)",
		c->lpt_lebs, c->lpt_first, c->lpt_last);
	dbg_gen("orphan area LEBs:    %d (%d - %d)",
		c->orph_lebs, c->orph_first, c->orph_last);
	dbg_gen("main area LEBs:      %d (%d - %d)",
		c->main_lebs, c->main_first, c->leb_cnt - 1);
	dbg_gen("index LEBs:          %d", c->lst.idx_lebs);
	dbg_gen("total index bytes:   %llu (%llu KiB, %llu MiB)",
		c->bi.old_idx_sz, c->bi.old_idx_sz >> 10,
		c->bi.old_idx_sz >> 20);
	dbg_gen("key hash type:       %d", c->key_hash_type);
	dbg_gen("tree fanout:         %d", c->fanout);
	dbg_gen("reserved GC LEB:     %d", c->gc_lnum);
	dbg_gen("max. znode size      %d", c->max_znode_sz);
	dbg_gen("max. index node size %d", c->max_idx_node_sz);
	dbg_gen("node sizes:          data %zu, inode %zu, dentry %zu",
		UBIFS_DATA_NODE_SZ, UBIFS_INO_NODE_SZ, UBIFS_DENT_NODE_SZ);
	dbg_gen("node sizes:          trun %zu, sb %zu, master %zu",
		UBIFS_TRUN_NODE_SZ, UBIFS_SB_NODE_SZ, UBIFS_MST_NODE_SZ);
	dbg_gen("node sizes:          ref %zu, cmt. start %zu, orph %zu",
		UBIFS_REF_NODE_SZ, UBIFS_CS_NODE_SZ, UBIFS_ORPH_NODE_SZ);
	dbg_gen("max. node sizes:     data %zu, inode %zu dentry %zu, idx %d",
		UBIFS_MAX_DATA_NODE_SZ, UBIFS_MAX_INO_NODE_SZ,
		UBIFS_MAX_DENT_NODE_SZ, ubifs_idx_node_sz(c, c->fanout));
	dbg_gen("dead watermark:      %d", c->dead_wm);
	dbg_gen("dark watermark:      %d", c->dark_wm);
	dbg_gen("LEB overhead:        %d", c->leb_overhead);
	x = (long long)c->main_lebs * c->dark_wm;
	dbg_gen("max. dark space:     %lld (%lld KiB, %lld MiB)",
		x, x >> 10, x >> 20);
	dbg_gen("maximum bud bytes:   %lld (%lld KiB, %lld MiB)",
		c->max_bud_bytes, c->max_bud_bytes >> 10,
		c->max_bud_bytes >> 20);
	dbg_gen("BG commit bud bytes: %lld (%lld KiB, %lld MiB)",
		c->bg_bud_bytes, c->bg_bud_bytes >> 10,
		c->bg_bud_bytes >> 20);
	dbg_gen("current bud bytes    %lld (%lld KiB, %lld MiB)",
		c->bud_bytes, c->bud_bytes >> 10, c->bud_bytes >> 20);
	dbg_gen("max. seq. number:    %llu", c->max_sqnum);
	dbg_gen("commit number:       %llu", c->cmt_no);
	dbg_gen("max. xattrs per inode: %d", ubifs_xattr_max_cnt(c));
	dbg_gen("max orphans:           %d", c->max_orphans);

	return 0;

out_infos:
	spin_lock(&ubifs_infos_lock);
	list_del(&c->infos_list);
	spin_unlock(&ubifs_infos_lock);
out_orphans:
	free_orphans(c);
out_journal:
	destroy_journal(c);
out_lpt:
	ubifs_lpt_free(c, 0);
out_master:
	kfree(c->mst_node);
	kfree(c->rcvrd_mst_node);
	if (c->bgt)
		kthread_stop(c->bgt);
out_wbufs:
	free_wbufs(c);
out_cbuf:
	kfree(c->cbuf);
out_auth:
	ubifs_exit_authentication(c);
out_free:
	kfree(c->write_reserve_buf);
	kfree(c->bu.buf);
	vfree(c->ileb_buf);
	vfree(c->sbuf);
	kfree(c->bottom_up_buf);
	kfree(c->sup_node);
	ubifs_sysfs_unregister(c);
out_debugging:
	ubifs_debugging_exit(c);
	return err;
}

/**
 * ubifs_umount - un-mount UBIFS file-system.
 * @c: UBIFS file-system description object
 *
 * Note, this function is called to free allocated resourced when un-mounting,
 * as well as free resources when an error occurred while we were half way
 * through mounting (error path cleanup function). So it has to make sure the
 * resource was actually allocated before freeing it.
 */
static void ubifs_umount(struct ubifs_info *c)
{
	dbg_gen("un-mounting UBI device %d, volume %d", c->vi.ubi_num,
		c->vi.vol_id);

	dbg_debugfs_exit_fs(c);
	spin_lock(&ubifs_infos_lock);
	list_del(&c->infos_list);
	spin_unlock(&ubifs_infos_lock);

	if (c->bgt)
		kthread_stop(c->bgt);

	destroy_journal(c);
	free_wbufs(c);
	free_orphans(c);
	ubifs_lpt_free(c, 0);
	ubifs_exit_authentication(c);

	ubifs_release_options(c);
	kfree(c->cbuf);
	kfree(c->rcvrd_mst_node);
	kfree(c->mst_node);
	kfree(c->write_reserve_buf);
	kfree(c->bu.buf);
	vfree(c->ileb_buf);
	vfree(c->sbuf);
	kfree(c->bottom_up_buf);
	kfree(c->sup_node);
	ubifs_debugging_exit(c);
	ubifs_sysfs_unregister(c);
}

/**
 * ubifs_remount_rw - re-mount in read-write mode.
 * @c: UBIFS file-system description object
 *
 * UBIFS avoids allocating many unnecessary resources when mounted in read-only
 * mode. This function allocates the needed resources and re-mounts UBIFS in
 * read-write mode.
 */
static int ubifs_remount_rw(struct ubifs_info *c)
{
	int err, lnum;

	if (c->rw_incompat) {
		ubifs_err(c, "the file-system is not R/W-compatible");
		ubifs_msg(c, "on-flash format version is w%d/r%d, but software only supports up to version w%d/r%d",
			  c->fmt_version, c->ro_compat_version,
			  UBIFS_FORMAT_VERSION, UBIFS_RO_COMPAT_VERSION);
		return -EROFS;
	}

	mutex_lock(&c->umount_mutex);
	dbg_save_space_info(c);
	c->remounting_rw = 1;
	c->ro_mount = 0;

	if (c->space_fixup) {
		err = ubifs_fixup_free_space(c);
		if (err)
			goto out;
	}

	err = check_free_space(c);
	if (err)
		goto out;

	if (c->need_recovery) {
		ubifs_msg(c, "completing deferred recovery");
		err = ubifs_write_rcvrd_mst_node(c);
		if (err)
			goto out;
		if (!ubifs_authenticated(c)) {
			err = ubifs_recover_size(c, true);
			if (err)
				goto out;
		}
		err = ubifs_clean_lebs(c, c->sbuf);
		if (err)
			goto out;
		err = ubifs_recover_inl_heads(c, c->sbuf);
		if (err)
			goto out;
	} else {
		/* A readonly mount is not allowed to have orphans */
		ubifs_assert(c, c->tot_orphans == 0);
		err = ubifs_clear_orphans(c);
		if (err)
			goto out;
	}

	if (!(c->mst_node->flags & cpu_to_le32(UBIFS_MST_DIRTY))) {
		c->mst_node->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
		err = ubifs_write_master(c);
		if (err)
			goto out;
	}

	if (c->superblock_need_write) {
		struct ubifs_sb_node *sup = c->sup_node;

		err = ubifs_write_sb_node(c, sup);
		if (err)
			goto out;

		c->superblock_need_write = 0;
	}

	c->ileb_buf = vmalloc(c->leb_size);
	if (!c->ileb_buf) {
		err = -ENOMEM;
		goto out;
	}

	c->write_reserve_buf = kmalloc(COMPRESSED_DATA_NODE_BUF_SZ + \
				       UBIFS_CIPHER_BLOCK_SIZE, GFP_KERNEL);
	if (!c->write_reserve_buf) {
		err = -ENOMEM;
		goto out;
	}

	err = ubifs_lpt_init(c, 0, 1);
	if (err)
		goto out;

	/* Create background thread */
	c->bgt = kthread_run(ubifs_bg_thread, c, "%s", c->bgt_name);
	if (IS_ERR(c->bgt)) {
		err = PTR_ERR(c->bgt);
		c->bgt = NULL;
		ubifs_err(c, "cannot spawn \"%s\", error %d",
			  c->bgt_name, err);
		goto out;
	}

	c->orph_buf = vmalloc(c->leb_size);
	if (!c->orph_buf) {
		err = -ENOMEM;
		goto out;
	}

	/* Check for enough log space */
	lnum = c->lhead_lnum + 1;
	if (lnum >= UBIFS_LOG_LNUM + c->log_lebs)
		lnum = UBIFS_LOG_LNUM;
	if (lnum == c->ltail_lnum) {
		err = ubifs_consolidate_log(c);
		if (err)
			goto out;
	}

	if (c->need_recovery) {
		err = ubifs_rcvry_gc_commit(c);
		if (err)
			goto out;

		if (ubifs_authenticated(c)) {
			err = ubifs_recover_size(c, false);
			if (err)
				goto out;
		}
	} else {
		err = ubifs_leb_unmap(c, c->gc_lnum);
	}
	if (err)
		goto out;

	dbg_gen("re-mounted read-write");
	c->remounting_rw = 0;

	if (c->need_recovery) {
		c->need_recovery = 0;
		ubifs_msg(c, "deferred recovery completed");
	} else {
		/*
		 * Do not run the debugging space check if the were doing
		 * recovery, because when we saved the information we had the
		 * file-system in a state where the TNC and lprops has been
		 * modified in memory, but all the I/O operations (including a
		 * commit) were deferred. So the file-system was in
		 * "non-committed" state. Now the file-system is in committed
		 * state, and of course the amount of free space will change
		 * because, for example, the old index size was imprecise.
		 */
		err = dbg_check_space_info(c);
	}

	mutex_unlock(&c->umount_mutex);
	return err;

out:
	c->ro_mount = 1;
	vfree(c->orph_buf);
	c->orph_buf = NULL;
	if (c->bgt) {
		kthread_stop(c->bgt);
		c->bgt = NULL;
	}
	kfree(c->write_reserve_buf);
	c->write_reserve_buf = NULL;
	vfree(c->ileb_buf);
	c->ileb_buf = NULL;
	ubifs_lpt_free(c, 1);
	c->remounting_rw = 0;
	mutex_unlock(&c->umount_mutex);
	return err;
}

/**
 * ubifs_remount_ro - re-mount in read-only mode.
 * @c: UBIFS file-system description object
 *
 * We assume VFS has stopped writing. Possibly the background thread could be
 * running a commit, however kthread_stop will wait in that case.
 */
static void ubifs_remount_ro(struct ubifs_info *c)
{
	int i, err;

	ubifs_assert(c, !c->need_recovery);
	ubifs_assert(c, !c->ro_mount);

	mutex_lock(&c->umount_mutex);
	if (c->bgt) {
		kthread_stop(c->bgt);
		c->bgt = NULL;
	}

	dbg_save_space_info(c);

	for (i = 0; i < c->jhead_cnt; i++) {
		err = ubifs_wbuf_sync(&c->jheads[i].wbuf);
		if (err)
			ubifs_ro_mode(c, err);
	}

	c->mst_node->flags &= ~cpu_to_le32(UBIFS_MST_DIRTY);
	c->mst_node->flags |= cpu_to_le32(UBIFS_MST_NO_ORPHS);
	c->mst_node->gc_lnum = cpu_to_le32(c->gc_lnum);
	err = ubifs_write_master(c);
	if (err)
		ubifs_ro_mode(c, err);

	vfree(c->orph_buf);
	c->orph_buf = NULL;
	kfree(c->write_reserve_buf);
	c->write_reserve_buf = NULL;
	vfree(c->ileb_buf);
	c->ileb_buf = NULL;
	ubifs_lpt_free(c, 1);
	c->ro_mount = 1;
	err = dbg_check_space_info(c);
	if (err)
		ubifs_ro_mode(c, err);
	mutex_unlock(&c->umount_mutex);
}

static void ubifs_put_super(struct super_block *sb)
{
	int i;
	struct ubifs_info *c = sb->s_fs_info;

	ubifs_msg(c, "un-mount UBI device %d", c->vi.ubi_num);

	/*
	 * The following asserts are only valid if there has not been a failure
	 * of the media. For example, there will be dirty inodes if we failed
	 * to write them back because of I/O errors.
	 */
	if (!c->ro_error) {
		ubifs_assert(c, c->bi.idx_growth == 0);
		ubifs_assert(c, c->bi.dd_growth == 0);
		ubifs_assert(c, c->bi.data_growth == 0);
	}

	/*
	 * The 'c->umount_lock' prevents races between UBIFS memory shrinker
	 * and file system un-mount. Namely, it prevents the shrinker from
	 * picking this superblock for shrinking - it will be just skipped if
	 * the mutex is locked.
	 */
	mutex_lock(&c->umount_mutex);
	if (!c->ro_mount) {
		/*
		 * First of all kill the background thread to make sure it does
		 * not interfere with un-mounting and freeing resources.
		 */
		if (c->bgt) {
			kthread_stop(c->bgt);
			c->bgt = NULL;
		}

		/*
		 * On fatal errors c->ro_error is set to 1, in which case we do
		 * not write the master node.
		 */
		if (!c->ro_error) {
			int err;

			/* Synchronize write-buffers */
			for (i = 0; i < c->jhead_cnt; i++) {
				err = ubifs_wbuf_sync(&c->jheads[i].wbuf);
				if (err)
					ubifs_ro_mode(c, err);
			}

			/*
			 * We are being cleanly unmounted which means the
			 * orphans were killed - indicate this in the master
			 * node. Also save the reserved GC LEB number.
			 */
			c->mst_node->flags &= ~cpu_to_le32(UBIFS_MST_DIRTY);
			c->mst_node->flags |= cpu_to_le32(UBIFS_MST_NO_ORPHS);
			c->mst_node->gc_lnum = cpu_to_le32(c->gc_lnum);
			err = ubifs_write_master(c);
			if (err)
				/*
				 * Recovery will attempt to fix the master area
				 * next mount, so we just print a message and
				 * continue to unmount normally.
				 */
				ubifs_err(c, "failed to write master node, error %d",
					  err);
		} else {
			for (i = 0; i < c->jhead_cnt; i++)
				/* Make sure write-buffer timers are canceled */
				hrtimer_cancel(&c->jheads[i].wbuf.timer);
		}
	}

	ubifs_umount(c);
	ubi_close_volume(c->ubi);
	mutex_unlock(&c->umount_mutex);
}

static int ubifs_remount_fs(struct super_block *sb, int *flags, char *data)
{
	int err;
	struct ubifs_info *c = sb->s_fs_info;

	sync_filesystem(sb);
	dbg_gen("old flags %#lx, new flags %#x", sb->s_flags, *flags);

	err = ubifs_parse_options(c, data, 1);
	if (err) {
		ubifs_err(c, "invalid or unknown remount parameter");
		return err;
	}

	if (c->ro_mount && !(*flags & SB_RDONLY)) {
		if (c->ro_error) {
			ubifs_msg(c, "cannot re-mount R/W due to prior errors");
			return -EROFS;
		}
		if (c->ro_media) {
			ubifs_msg(c, "cannot re-mount R/W - UBI volume is R/O");
			return -EROFS;
		}
		err = ubifs_remount_rw(c);
		if (err)
			return err;
	} else if (!c->ro_mount && (*flags & SB_RDONLY)) {
		if (c->ro_error) {
			ubifs_msg(c, "cannot re-mount R/O due to prior errors");
			return -EROFS;
		}
		ubifs_remount_ro(c);
	}

	if (c->bulk_read == 1)
		bu_init(c);
	else {
		dbg_gen("disable bulk-read");
		mutex_lock(&c->bu_mutex);
		kfree(c->bu.buf);
		c->bu.buf = NULL;
		mutex_unlock(&c->bu_mutex);
	}

	if (!c->need_recovery)
		ubifs_assert(c, c->lst.taken_empty_lebs > 0);

	return 0;
}

const struct super_operations ubifs_super_operations = {
	.alloc_inode   = ubifs_alloc_inode,
	.free_inode    = ubifs_free_inode,
	.put_super     = ubifs_put_super,
	.write_inode   = ubifs_write_inode,
	.drop_inode    = ubifs_drop_inode,
	.evict_inode   = ubifs_evict_inode,
	.statfs        = ubifs_statfs,
	.dirty_inode   = ubifs_dirty_inode,
	.remount_fs    = ubifs_remount_fs,
	.show_options  = ubifs_show_options,
	.sync_fs       = ubifs_sync_fs,
};

/**
 * open_ubi - parse UBI device name string and open the UBI device.
 * @name: UBI volume name
 * @mode: UBI volume open mode
 *
 * The primary method of mounting UBIFS is by specifying the UBI volume
 * character device node path. However, UBIFS may also be mounted without any
 * character device node using one of the following methods:
 *
 * o ubiX_Y    - mount UBI device number X, volume Y;
 * o ubiY      - mount UBI device number 0, volume Y;
 * o ubiX:NAME - mount UBI device X, volume with name NAME;
 * o ubi:NAME  - mount UBI device 0, volume with name NAME.
 *
 * Alternative '!' separator may be used instead of ':' (because some shells
 * like busybox may interpret ':' as an NFS host name separator). This function
 * returns UBI volume description object in case of success and a negative
 * error code in case of failure.
 */
static struct ubi_volume_desc *open_ubi(const char *name, int mode)
{
	struct ubi_volume_desc *ubi;
	int dev, vol;
	char *endptr;

	if (!name || !*name)
		return ERR_PTR(-EINVAL);

	/* First, try to open using the device node path method */
	ubi = ubi_open_volume_path(name, mode);
	if (!IS_ERR(ubi))
		return ubi;

	/* Try the "nodev" method */
	if (name[0] != 'u' || name[1] != 'b' || name[2] != 'i')
		return ERR_PTR(-EINVAL);

	/* ubi:NAME method */
	if ((name[3] == ':' || name[3] == '!') && name[4] != '\0')
		return ubi_open_volume_nm(0, name + 4, mode);

	if (!isdigit(name[3]))
		return ERR_PTR(-EINVAL);

	dev = simple_strtoul(name + 3, &endptr, 0);

	/* ubiY method */
	if (*endptr == '\0')
		return ubi_open_volume(0, dev, mode);

	/* ubiX_Y method */
	if (*endptr == '_' && isdigit(endptr[1])) {
		vol = simple_strtoul(endptr + 1, &endptr, 0);
		if (*endptr != '\0')
			return ERR_PTR(-EINVAL);
		return ubi_open_volume(dev, vol, mode);
	}

	/* ubiX:NAME method */
	if ((*endptr == ':' || *endptr == '!') && endptr[1] != '\0')
		return ubi_open_volume_nm(dev, ++endptr, mode);

	return ERR_PTR(-EINVAL);
}

static struct ubifs_info *alloc_ubifs_info(struct ubi_volume_desc *ubi)
{
	struct ubifs_info *c;

	c = kzalloc(sizeof(struct ubifs_info), GFP_KERNEL);
	if (c) {
		spin_lock_init(&c->cnt_lock);
		spin_lock_init(&c->cs_lock);
		spin_lock_init(&c->buds_lock);
		spin_lock_init(&c->space_lock);
		spin_lock_init(&c->orphan_lock);
		init_rwsem(&c->commit_sem);
		mutex_init(&c->lp_mutex);
		mutex_init(&c->tnc_mutex);
		mutex_init(&c->log_mutex);
		mutex_init(&c->umount_mutex);
		mutex_init(&c->bu_mutex);
		mutex_init(&c->write_reserve_mutex);
		init_waitqueue_head(&c->cmt_wq);
		init_waitqueue_head(&c->reserve_space_wq);
		atomic_set(&c->need_wait_space, 0);
		c->buds = RB_ROOT;
		c->old_idx = RB_ROOT;
		c->size_tree = RB_ROOT;
		c->orph_tree = RB_ROOT;
		INIT_LIST_HEAD(&c->infos_list);
		INIT_LIST_HEAD(&c->idx_gc);
		INIT_LIST_HEAD(&c->replay_list);
		INIT_LIST_HEAD(&c->replay_buds);
		INIT_LIST_HEAD(&c->uncat_list);
		INIT_LIST_HEAD(&c->empty_list);
		INIT_LIST_HEAD(&c->freeable_list);
		INIT_LIST_HEAD(&c->frdi_idx_list);
		INIT_LIST_HEAD(&c->unclean_leb_list);
		INIT_LIST_HEAD(&c->old_buds);
		INIT_LIST_HEAD(&c->orph_list);
		INIT_LIST_HEAD(&c->orph_new);
		c->no_chk_data_crc = 1;
		c->assert_action = ASSACT_RO;

		c->highest_inum = UBIFS_FIRST_INO;
		c->lhead_lnum = c->ltail_lnum = UBIFS_LOG_LNUM;

		ubi_get_volume_info(ubi, &c->vi);
		ubi_get_device_info(c->vi.ubi_num, &c->di);
	}
	return c;
}

static int ubifs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct ubifs_info *c = sb->s_fs_info;
	struct inode *root;
	int err;

	c->vfs_sb = sb;
	/* Re-open the UBI device in read-write mode */
	c->ubi = ubi_open_volume(c->vi.ubi_num, c->vi.vol_id, UBI_READWRITE);
	if (IS_ERR(c->ubi)) {
		err = PTR_ERR(c->ubi);
		goto out;
	}

	err = ubifs_parse_options(c, data, 0);
	if (err)
		goto out_close;

	/*
	 * UBIFS provides 'backing_dev_info' in order to disable read-ahead. For
	 * UBIFS, I/O is not deferred, it is done immediately in read_folio,
	 * which means the user would have to wait not just for their own I/O
	 * but the read-ahead I/O as well i.e. completely pointless.
	 *
	 * Read-ahead will be disabled because @sb->s_bdi->ra_pages is 0. Also
	 * @sb->s_bdi->capabilities are initialized to 0 so there won't be any
	 * writeback happening.
	 */
	err = super_setup_bdi_name(sb, "ubifs_%d_%d", c->vi.ubi_num,
				   c->vi.vol_id);
	if (err)
		goto out_close;
	sb->s_bdi->ra_pages = 0;
	sb->s_bdi->io_pages = 0;

	sb->s_fs_info = c;
	sb->s_magic = UBIFS_SUPER_MAGIC;
	sb->s_blocksize = UBIFS_BLOCK_SIZE;
	sb->s_blocksize_bits = UBIFS_BLOCK_SHIFT;
	sb->s_maxbytes = c->max_inode_sz = key_max_inode_size(c);
	if (c->max_inode_sz > MAX_LFS_FILESIZE)
		sb->s_maxbytes = c->max_inode_sz = MAX_LFS_FILESIZE;
	sb->s_op = &ubifs_super_operations;
	sb->s_xattr = ubifs_xattr_handlers;
	fscrypt_set_ops(sb, &ubifs_crypt_operations);

	mutex_lock(&c->umount_mutex);
	err = mount_ubifs(c);
	if (err) {
		ubifs_assert(c, err < 0);
		goto out_unlock;
	}

	/* Read the root inode */
	root = ubifs_iget(sb, UBIFS_ROOT_INO);
	if (IS_ERR(root)) {
		err = PTR_ERR(root);
		goto out_umount;
	}

	generic_set_sb_d_ops(sb);
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_umount;
	}

	super_set_uuid(sb, c->uuid, sizeof(c->uuid));

	mutex_unlock(&c->umount_mutex);
	return 0;

out_umount:
	ubifs_umount(c);
out_unlock:
	mutex_unlock(&c->umount_mutex);
out_close:
	ubifs_release_options(c);
	ubi_close_volume(c->ubi);
out:
	return err;
}

static int sb_test(struct super_block *sb, void *data)
{
	struct ubifs_info *c1 = data;
	struct ubifs_info *c = sb->s_fs_info;

	return c->vi.cdev == c1->vi.cdev;
}

static int sb_set(struct super_block *sb, void *data)
{
	sb->s_fs_info = data;
	return set_anon_super(sb, NULL);
}

static struct dentry *ubifs_mount(struct file_system_type *fs_type, int flags,
			const char *name, void *data)
{
	struct ubi_volume_desc *ubi;
	struct ubifs_info *c;
	struct super_block *sb;
	int err;

	dbg_gen("name %s, flags %#x", name, flags);

	/*
	 * Get UBI device number and volume ID. Mount it read-only so far
	 * because this might be a new mount point, and UBI allows only one
	 * read-write user at a time.
	 */
	ubi = open_ubi(name, UBI_READONLY);
	if (IS_ERR(ubi)) {
		if (!(flags & SB_SILENT))
			pr_err("UBIFS error (pid: %d): cannot open \"%s\", error %d",
			       current->pid, name, (int)PTR_ERR(ubi));
		return ERR_CAST(ubi);
	}

	c = alloc_ubifs_info(ubi);
	if (!c) {
		err = -ENOMEM;
		goto out_close;
	}

	dbg_gen("opened ubi%d_%d", c->vi.ubi_num, c->vi.vol_id);

	sb = sget(fs_type, sb_test, sb_set, flags, c);
	if (IS_ERR(sb)) {
		err = PTR_ERR(sb);
		kfree(c);
		goto out_close;
	}

	if (sb->s_root) {
		struct ubifs_info *c1 = sb->s_fs_info;
		kfree(c);
		/* A new mount point for already mounted UBIFS */
		dbg_gen("this ubi volume is already mounted");
		if (!!(flags & SB_RDONLY) != c1->ro_mount) {
			err = -EBUSY;
			goto out_deact;
		}
	} else {
		err = ubifs_fill_super(sb, data, flags & SB_SILENT ? 1 : 0);
		if (err)
			goto out_deact;
		/* We do not support atime */
		sb->s_flags |= SB_ACTIVE;
		if (IS_ENABLED(CONFIG_UBIFS_ATIME_SUPPORT))
			ubifs_msg(c, "full atime support is enabled.");
		else
			sb->s_flags |= SB_NOATIME;
	}

	/* 'fill_super()' opens ubi again so we must close it here */
	ubi_close_volume(ubi);

	return dget(sb->s_root);

out_deact:
	deactivate_locked_super(sb);
out_close:
	ubi_close_volume(ubi);
	return ERR_PTR(err);
}

static void kill_ubifs_super(struct super_block *s)
{
	struct ubifs_info *c = s->s_fs_info;
	kill_anon_super(s);
	kfree(c);
}

static struct file_system_type ubifs_fs_type = {
	.name    = "ubifs",
	.owner   = THIS_MODULE,
	.mount   = ubifs_mount,
	.kill_sb = kill_ubifs_super,
};
MODULE_ALIAS_FS("ubifs");

/*
 * Inode slab cache constructor.
 */
static void inode_slab_ctor(void *obj)
{
	struct ubifs_inode *ui = obj;
	inode_init_once(&ui->vfs_inode);
}

static int __init ubifs_init(void)
{
	int err = -ENOMEM;

	BUILD_BUG_ON(sizeof(struct ubifs_ch) != 24);

	/* Make sure node sizes are 8-byte aligned */
	BUILD_BUG_ON(UBIFS_CH_SZ        & 7);
	BUILD_BUG_ON(UBIFS_INO_NODE_SZ  & 7);
	BUILD_BUG_ON(UBIFS_DENT_NODE_SZ & 7);
	BUILD_BUG_ON(UBIFS_XENT_NODE_SZ & 7);
	BUILD_BUG_ON(UBIFS_DATA_NODE_SZ & 7);
	BUILD_BUG_ON(UBIFS_TRUN_NODE_SZ & 7);
	BUILD_BUG_ON(UBIFS_SB_NODE_SZ   & 7);
	BUILD_BUG_ON(UBIFS_MST_NODE_SZ  & 7);
	BUILD_BUG_ON(UBIFS_REF_NODE_SZ  & 7);
	BUILD_BUG_ON(UBIFS_CS_NODE_SZ   & 7);
	BUILD_BUG_ON(UBIFS_ORPH_NODE_SZ & 7);

	BUILD_BUG_ON(UBIFS_MAX_DENT_NODE_SZ & 7);
	BUILD_BUG_ON(UBIFS_MAX_XENT_NODE_SZ & 7);
	BUILD_BUG_ON(UBIFS_MAX_DATA_NODE_SZ & 7);
	BUILD_BUG_ON(UBIFS_MAX_INO_NODE_SZ  & 7);
	BUILD_BUG_ON(UBIFS_MAX_NODE_SZ      & 7);
	BUILD_BUG_ON(MIN_WRITE_SZ           & 7);

	/* Check min. node size */
	BUILD_BUG_ON(UBIFS_INO_NODE_SZ  < MIN_WRITE_SZ);
	BUILD_BUG_ON(UBIFS_DENT_NODE_SZ < MIN_WRITE_SZ);
	BUILD_BUG_ON(UBIFS_XENT_NODE_SZ < MIN_WRITE_SZ);
	BUILD_BUG_ON(UBIFS_TRUN_NODE_SZ < MIN_WRITE_SZ);

	BUILD_BUG_ON(UBIFS_MAX_DENT_NODE_SZ > UBIFS_MAX_NODE_SZ);
	BUILD_BUG_ON(UBIFS_MAX_XENT_NODE_SZ > UBIFS_MAX_NODE_SZ);
	BUILD_BUG_ON(UBIFS_MAX_DATA_NODE_SZ > UBIFS_MAX_NODE_SZ);
	BUILD_BUG_ON(UBIFS_MAX_INO_NODE_SZ  > UBIFS_MAX_NODE_SZ);

	/* Defined node sizes */
	BUILD_BUG_ON(UBIFS_SB_NODE_SZ  != 4096);
	BUILD_BUG_ON(UBIFS_MST_NODE_SZ != 512);
	BUILD_BUG_ON(UBIFS_INO_NODE_SZ != 160);
	BUILD_BUG_ON(UBIFS_REF_NODE_SZ != 64);

	/*
	 * We use 2 bit wide bit-fields to store compression type, which should
	 * be amended if more compressors are added. The bit-fields are:
	 * @compr_type in 'struct ubifs_inode', @default_compr in
	 * 'struct ubifs_info' and @compr_type in 'struct ubifs_mount_opts'.
	 */
	BUILD_BUG_ON(UBIFS_COMPR_TYPES_CNT > 4);

	/*
	 * We require that PAGE_SIZE is greater-than-or-equal-to
	 * UBIFS_BLOCK_SIZE. It is assumed that both are powers of 2.
	 */
	if (PAGE_SIZE < UBIFS_BLOCK_SIZE) {
		pr_err("UBIFS error (pid %d): VFS page cache size is %u bytes, but UBIFS requires at least 4096 bytes",
		       current->pid, (unsigned int)PAGE_SIZE);
		return -EINVAL;
	}

	ubifs_inode_slab = kmem_cache_create("ubifs_inode_slab",
				sizeof(struct ubifs_inode), 0,
				SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
				&inode_slab_ctor);
	if (!ubifs_inode_slab)
		return -ENOMEM;

	ubifs_shrinker_info = shrinker_alloc(0, "ubifs-slab");
	if (!ubifs_shrinker_info)
		goto out_slab;

	ubifs_shrinker_info->count_objects = ubifs_shrink_count;
	ubifs_shrinker_info->scan_objects = ubifs_shrink_scan;

	shrinker_register(ubifs_shrinker_info);

	err = ubifs_compressors_init();
	if (err)
		goto out_shrinker;

	dbg_debugfs_init();

	err = ubifs_sysfs_init();
	if (err)
		goto out_dbg;

	err = register_filesystem(&ubifs_fs_type);
	if (err) {
		pr_err("UBIFS error (pid %d): cannot register file system, error %d",
		       current->pid, err);
		goto out_sysfs;
	}
	return 0;

out_sysfs:
	ubifs_sysfs_exit();
out_dbg:
	dbg_debugfs_exit();
	ubifs_compressors_exit();
out_shrinker:
	shrinker_free(ubifs_shrinker_info);
out_slab:
	kmem_cache_destroy(ubifs_inode_slab);
	return err;
}
/* late_initcall to let compressors initialize first */
late_initcall(ubifs_init);

static void __exit ubifs_exit(void)
{
	WARN_ON(!list_empty(&ubifs_infos));
	WARN_ON(atomic_long_read(&ubifs_clean_zn_cnt) != 0);

	dbg_debugfs_exit();
	ubifs_sysfs_exit();
	ubifs_compressors_exit();
	shrinker_free(ubifs_shrinker_info);

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ubifs_inode_slab);
	unregister_filesystem(&ubifs_fs_type);
}
module_exit(ubifs_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION(__stringify(UBIFS_VERSION));
MODULE_AUTHOR("Artem Bityutskiy, Adrian Hunter");
MODULE_DESCRIPTION("UBIFS - UBI File System");
