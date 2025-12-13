// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/mount.h>
#include <linux/cred.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>
#include <linux/fs_struct.h>
#include <linux/iversion.h>
#include <linux/nls.h>
#include <linux/buffer_head.h>
#include <linux/magic.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

static char exfat_default_iocharset[] = CONFIG_EXFAT_DEFAULT_IOCHARSET;
static struct kmem_cache *exfat_inode_cachep;

static void exfat_free_iocharset(struct exfat_sb_info *sbi)
{
	if (sbi->options.iocharset != exfat_default_iocharset)
		kfree(sbi->options.iocharset);
}

static void exfat_set_iocharset(struct exfat_mount_options *opts,
				char *iocharset)
{
	opts->iocharset = iocharset;
	if (!strcmp(opts->iocharset, "utf8"))
		opts->utf8 = 1;
	else
		opts->utf8 = 0;
}

static void exfat_put_super(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	mutex_lock(&sbi->s_lock);
	exfat_clear_volume_dirty(sb);
	exfat_free_bitmap(sbi);
	brelse(sbi->boot_bh);
	mutex_unlock(&sbi->s_lock);
}

static int exfat_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	unsigned long long id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type = sb->s_magic;
	buf->f_bsize = sbi->cluster_size;
	buf->f_blocks = sbi->num_clusters - 2; /* clu 0 & 1 */
	buf->f_bfree = buf->f_blocks - sbi->used_clusters;
	buf->f_bavail = buf->f_bfree;
	buf->f_fsid = u64_to_fsid(id);
	/* Unicode utf16 255 characters */
	buf->f_namelen = EXFAT_MAX_FILE_LEN * NLS_MAX_CHARSET_SIZE;
	return 0;
}

static int exfat_set_vol_flags(struct super_block *sb, unsigned short new_flags)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct boot_sector *p_boot = (struct boot_sector *)sbi->boot_bh->b_data;

	/* retain persistent-flags */
	new_flags |= sbi->vol_flags_persistent;

	/* flags are not changed */
	if (sbi->vol_flags == new_flags)
		return 0;

	sbi->vol_flags = new_flags;

	/* skip updating volume dirty flag,
	 * if this volume has been mounted with read-only
	 */
	if (sb_rdonly(sb))
		return 0;

	p_boot->vol_flags = cpu_to_le16(new_flags);

	set_buffer_uptodate(sbi->boot_bh);
	mark_buffer_dirty(sbi->boot_bh);

	__sync_dirty_buffer(sbi->boot_bh, REQ_SYNC | REQ_FUA | REQ_PREFLUSH);

	return 0;
}

int exfat_set_volume_dirty(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	return exfat_set_vol_flags(sb, sbi->vol_flags | VOLUME_DIRTY);
}

int exfat_clear_volume_dirty(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	return exfat_set_vol_flags(sb, sbi->vol_flags & ~VOLUME_DIRTY);
}

static int exfat_show_options(struct seq_file *m, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_mount_options *opts = &sbi->options;

	/* Show partition info */
	if (!uid_eq(opts->fs_uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
				from_kuid_munged(&init_user_ns, opts->fs_uid));
	if (!gid_eq(opts->fs_gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
				from_kgid_munged(&init_user_ns, opts->fs_gid));
	seq_printf(m, ",fmask=%04o,dmask=%04o", opts->fs_fmask, opts->fs_dmask);
	if (opts->allow_utime)
		seq_printf(m, ",allow_utime=%04o", opts->allow_utime);
	if (opts->utf8)
		seq_puts(m, ",iocharset=utf8");
	else if (sbi->nls_io)
		seq_printf(m, ",iocharset=%s", sbi->nls_io->charset);
	if (opts->errors == EXFAT_ERRORS_CONT)
		seq_puts(m, ",errors=continue");
	else if (opts->errors == EXFAT_ERRORS_PANIC)
		seq_puts(m, ",errors=panic");
	else
		seq_puts(m, ",errors=remount-ro");
	if (opts->discard)
		seq_puts(m, ",discard");
	if (opts->keep_last_dots)
		seq_puts(m, ",keep_last_dots");
	if (opts->sys_tz)
		seq_puts(m, ",sys_tz");
	else if (opts->time_offset)
		seq_printf(m, ",time_offset=%d", opts->time_offset);
	if (opts->zero_size_dir)
		seq_puts(m, ",zero_size_dir");
	return 0;
}

int exfat_force_shutdown(struct super_block *sb, u32 flags)
{
	int ret;
	struct exfat_sb_info *sbi = sb->s_fs_info;
	struct exfat_mount_options *opts = &sbi->options;

	if (exfat_forced_shutdown(sb))
		return 0;

	switch (flags) {
	case EXFAT_GOING_DOWN_DEFAULT:
	case EXFAT_GOING_DOWN_FULLSYNC:
		ret = bdev_freeze(sb->s_bdev);
		if (ret)
			return ret;
		bdev_thaw(sb->s_bdev);
		set_bit(EXFAT_FLAGS_SHUTDOWN, &sbi->s_exfat_flags);
		break;
	case EXFAT_GOING_DOWN_NOSYNC:
		set_bit(EXFAT_FLAGS_SHUTDOWN, &sbi->s_exfat_flags);
		break;
	default:
		return -EINVAL;
	}

	if (opts->discard)
		opts->discard = 0;
	return 0;
}

static void exfat_shutdown(struct super_block *sb)
{
	exfat_force_shutdown(sb, EXFAT_GOING_DOWN_NOSYNC);
}

static struct inode *exfat_alloc_inode(struct super_block *sb)
{
	struct exfat_inode_info *ei;

	ei = alloc_inode_sb(sb, exfat_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;

	init_rwsem(&ei->truncate_lock);
	return &ei->vfs_inode;
}

static void exfat_free_inode(struct inode *inode)
{
	kmem_cache_free(exfat_inode_cachep, EXFAT_I(inode));
}

static const struct super_operations exfat_sops = {
	.alloc_inode	= exfat_alloc_inode,
	.free_inode	= exfat_free_inode,
	.write_inode	= exfat_write_inode,
	.evict_inode	= exfat_evict_inode,
	.put_super	= exfat_put_super,
	.statfs		= exfat_statfs,
	.show_options	= exfat_show_options,
	.shutdown	= exfat_shutdown,
};

enum {
	Opt_uid,
	Opt_gid,
	Opt_umask,
	Opt_dmask,
	Opt_fmask,
	Opt_allow_utime,
	Opt_charset,
	Opt_errors,
	Opt_discard,
	Opt_keep_last_dots,
	Opt_sys_tz,
	Opt_time_offset,
	Opt_zero_size_dir,

	/* Deprecated options */
	Opt_utf8,
	Opt_debug,
	Opt_namecase,
	Opt_codepage,
};

static const struct constant_table exfat_param_enums[] = {
	{ "continue",		EXFAT_ERRORS_CONT },
	{ "panic",		EXFAT_ERRORS_PANIC },
	{ "remount-ro",		EXFAT_ERRORS_RO },
	{}
};

static const struct fs_parameter_spec exfat_parameters[] = {
	fsparam_uid("uid",			Opt_uid),
	fsparam_gid("gid",			Opt_gid),
	fsparam_u32oct("umask",			Opt_umask),
	fsparam_u32oct("dmask",			Opt_dmask),
	fsparam_u32oct("fmask",			Opt_fmask),
	fsparam_u32oct("allow_utime",		Opt_allow_utime),
	fsparam_string("iocharset",		Opt_charset),
	fsparam_enum("errors",			Opt_errors, exfat_param_enums),
	fsparam_flag_no("discard",		Opt_discard),
	fsparam_flag("keep_last_dots",		Opt_keep_last_dots),
	fsparam_flag("sys_tz",			Opt_sys_tz),
	fsparam_s32("time_offset",		Opt_time_offset),
	fsparam_flag_no("zero_size_dir",	Opt_zero_size_dir),
	__fsparam(NULL, "utf8",			Opt_utf8, fs_param_deprecated,
		  NULL),
	__fsparam(NULL, "debug",		Opt_debug, fs_param_deprecated,
		  NULL),
	__fsparam(fs_param_is_u32, "namecase",	Opt_namecase,
		  fs_param_deprecated, NULL),
	__fsparam(fs_param_is_u32, "codepage",	Opt_codepage,
		  fs_param_deprecated, NULL),
	{}
};

static int exfat_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct exfat_sb_info *sbi = fc->s_fs_info;
	struct exfat_mount_options *opts = &sbi->options;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, exfat_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		opts->fs_uid = result.uid;
		break;
	case Opt_gid:
		opts->fs_gid = result.gid;
		break;
	case Opt_umask:
		opts->fs_fmask = result.uint_32;
		opts->fs_dmask = result.uint_32;
		break;
	case Opt_dmask:
		opts->fs_dmask = result.uint_32;
		break;
	case Opt_fmask:
		opts->fs_fmask = result.uint_32;
		break;
	case Opt_allow_utime:
		opts->allow_utime = result.uint_32 & 0022;
		break;
	case Opt_charset:
		exfat_free_iocharset(sbi);
		exfat_set_iocharset(opts, param->string);
		param->string = NULL;
		break;
	case Opt_errors:
		opts->errors = result.uint_32;
		break;
	case Opt_discard:
		opts->discard = !result.negated;
		break;
	case Opt_keep_last_dots:
		opts->keep_last_dots = 1;
		break;
	case Opt_sys_tz:
		opts->sys_tz = 1;
		break;
	case Opt_time_offset:
		/*
		 * Make the limit 24 just in case someone invents something
		 * unusual.
		 */
		if (result.int_32 < -24 * 60 || result.int_32 > 24 * 60)
			return -EINVAL;
		opts->time_offset = result.int_32;
		break;
	case Opt_zero_size_dir:
		opts->zero_size_dir = !result.negated;
		break;
	case Opt_utf8:
	case Opt_debug:
	case Opt_namecase:
	case Opt_codepage:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void exfat_hash_init(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	int i;

	spin_lock_init(&sbi->inode_hash_lock);
	for (i = 0; i < EXFAT_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&sbi->inode_hashtable[i]);
}

static int exfat_read_root(struct inode *inode, struct exfat_chain *root_clu)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	int num_subdirs;

	exfat_chain_set(&ei->dir, sbi->root_dir, 0, ALLOC_FAT_CHAIN);
	ei->entry = -1;
	ei->start_clu = sbi->root_dir;
	ei->flags = ALLOC_FAT_CHAIN;
	ei->type = TYPE_DIR;
	ei->version = 0;
	ei->hint_bmap.off = EXFAT_EOF_CLUSTER;
	ei->hint_stat.eidx = 0;
	ei->hint_stat.clu = sbi->root_dir;
	ei->hint_femp.eidx = EXFAT_HINT_NONE;

	i_size_write(inode, EXFAT_CLU_TO_B(root_clu->size, sbi));

	num_subdirs = exfat_count_dir_entries(sb, root_clu);
	if (num_subdirs < 0)
		return -EIO;
	set_nlink(inode, num_subdirs + EXFAT_MIN_SUBDIR);

	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	inode_inc_iversion(inode);
	inode->i_generation = 0;
	inode->i_mode = exfat_make_mode(sbi, EXFAT_ATTR_SUBDIR, 0777);
	inode->i_op = &exfat_dir_inode_operations;
	inode->i_fop = &exfat_dir_operations;

	inode->i_blocks = round_up(i_size_read(inode), sbi->cluster_size) >> 9;
	ei->i_pos = ((loff_t)sbi->root_dir << 32) | 0xffffffff;

	exfat_save_attr(inode, EXFAT_ATTR_SUBDIR);
	ei->i_crtime = simple_inode_init_ts(inode);
	exfat_truncate_inode_atime(inode);
	return 0;
}

static int exfat_calibrate_blocksize(struct super_block *sb, int logical_sect)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	if (!is_power_of_2(logical_sect)) {
		exfat_err(sb, "bogus logical sector size %u", logical_sect);
		return -EIO;
	}

	if (logical_sect < sb->s_blocksize) {
		exfat_err(sb, "logical sector size too small for device (logical sector size = %u)",
			  logical_sect);
		return -EIO;
	}

	if (logical_sect > sb->s_blocksize) {
		brelse(sbi->boot_bh);
		sbi->boot_bh = NULL;

		if (!sb_set_blocksize(sb, logical_sect)) {
			exfat_err(sb, "unable to set blocksize %u",
				  logical_sect);
			return -EIO;
		}
		sbi->boot_bh = sb_bread(sb, 0);
		if (!sbi->boot_bh) {
			exfat_err(sb, "unable to read boot sector (logical sector size = %lu)",
				  sb->s_blocksize);
			return -EIO;
		}
	}
	return 0;
}

static int exfat_read_boot_sector(struct super_block *sb)
{
	struct boot_sector *p_boot;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	/* set block size to read super block */
	if (!sb_min_blocksize(sb, 512)) {
		exfat_err(sb, "unable to set blocksize");
		return -EINVAL;
	}

	/* read boot sector */
	sbi->boot_bh = sb_bread(sb, 0);
	if (!sbi->boot_bh) {
		exfat_err(sb, "unable to read boot sector");
		return -EIO;
	}
	p_boot = (struct boot_sector *)sbi->boot_bh->b_data;

	/* check the validity of BOOT */
	if (le16_to_cpu((p_boot->signature)) != BOOT_SIGNATURE) {
		exfat_err(sb, "invalid boot record signature");
		return -EINVAL;
	}

	if (memcmp(p_boot->fs_name, STR_EXFAT, BOOTSEC_FS_NAME_LEN)) {
		exfat_err(sb, "invalid fs_name"); /* fs_name may unprintable */
		return -EINVAL;
	}

	/*
	 * must_be_zero field must be filled with zero to prevent mounting
	 * from FAT volume.
	 */
	if (memchr_inv(p_boot->must_be_zero, 0, sizeof(p_boot->must_be_zero)))
		return -EINVAL;

	if (p_boot->num_fats != 1 && p_boot->num_fats != 2) {
		exfat_err(sb, "bogus number of FAT structure");
		return -EINVAL;
	}

	/*
	 * sect_size_bits could be at least 9 and at most 12.
	 */
	if (p_boot->sect_size_bits < EXFAT_MIN_SECT_SIZE_BITS ||
	    p_boot->sect_size_bits > EXFAT_MAX_SECT_SIZE_BITS) {
		exfat_err(sb, "bogus sector size bits : %u",
				p_boot->sect_size_bits);
		return -EINVAL;
	}

	/*
	 * sect_per_clus_bits could be at least 0 and at most 25 - sect_size_bits.
	 */
	if (p_boot->sect_per_clus_bits > EXFAT_MAX_SECT_PER_CLUS_BITS(p_boot)) {
		exfat_err(sb, "bogus sectors bits per cluster : %u",
				p_boot->sect_per_clus_bits);
		return -EINVAL;
	}

	sbi->sect_per_clus = 1 << p_boot->sect_per_clus_bits;
	sbi->sect_per_clus_bits = p_boot->sect_per_clus_bits;
	sbi->cluster_size_bits = p_boot->sect_per_clus_bits +
		p_boot->sect_size_bits;
	sbi->cluster_size = 1 << sbi->cluster_size_bits;
	sbi->num_FAT_sectors = le32_to_cpu(p_boot->fat_length);
	sbi->FAT1_start_sector = le32_to_cpu(p_boot->fat_offset);
	sbi->FAT2_start_sector = le32_to_cpu(p_boot->fat_offset);
	if (p_boot->num_fats == 2)
		sbi->FAT2_start_sector += sbi->num_FAT_sectors;
	sbi->data_start_sector = le32_to_cpu(p_boot->clu_offset);
	sbi->num_sectors = le64_to_cpu(p_boot->vol_length);
	/* because the cluster index starts with 2 */
	sbi->num_clusters = le32_to_cpu(p_boot->clu_count) +
		EXFAT_RESERVED_CLUSTERS;

	sbi->root_dir = le32_to_cpu(p_boot->root_cluster);
	sbi->dentries_per_clu = 1 <<
		(sbi->cluster_size_bits - DENTRY_SIZE_BITS);

	sbi->vol_flags = le16_to_cpu(p_boot->vol_flags);
	sbi->vol_flags_persistent = sbi->vol_flags & (VOLUME_DIRTY | MEDIA_FAILURE);
	sbi->clu_srch_ptr = EXFAT_FIRST_CLUSTER;

	/* check consistencies */
	if ((u64)sbi->num_FAT_sectors << p_boot->sect_size_bits <
	    (u64)sbi->num_clusters * 4) {
		exfat_err(sb, "bogus fat length");
		return -EINVAL;
	}

	if (sbi->data_start_sector <
	    (u64)sbi->FAT1_start_sector +
	    (u64)sbi->num_FAT_sectors * p_boot->num_fats) {
		exfat_err(sb, "bogus data start sector");
		return -EINVAL;
	}

	if (sbi->vol_flags & VOLUME_DIRTY)
		exfat_warn(sb, "Volume was not properly unmounted. Some data may be corrupt. Please run fsck.");
	if (sbi->vol_flags & MEDIA_FAILURE)
		exfat_warn(sb, "Medium has reported failures. Some data may be lost.");

	/* exFAT file size is limited by a disk volume size */
	sb->s_maxbytes = (u64)(sbi->num_clusters - EXFAT_RESERVED_CLUSTERS) <<
		sbi->cluster_size_bits;

	/* check logical sector size */
	if (exfat_calibrate_blocksize(sb, 1 << p_boot->sect_size_bits))
		return -EIO;

	return 0;
}

static int exfat_verify_boot_region(struct super_block *sb)
{
	struct buffer_head *bh = NULL;
	u32 chksum = 0;
	__le32 *p_sig, *p_chksum;
	int sn, i;

	/* read boot sector sub-regions */
	for (sn = 0; sn < 11; sn++) {
		bh = sb_bread(sb, sn);
		if (!bh)
			return -EIO;

		if (sn != 0 && sn <= 8) {
			/* extended boot sector sub-regions */
			p_sig = (__le32 *)&bh->b_data[sb->s_blocksize - 4];
			if (le32_to_cpu(*p_sig) != EXBOOT_SIGNATURE)
				exfat_warn(sb, "Invalid exboot-signature(sector = %d): 0x%08x",
					   sn, le32_to_cpu(*p_sig));
		}

		chksum = exfat_calc_chksum32(bh->b_data, sb->s_blocksize,
			chksum, sn ? CS_DEFAULT : CS_BOOT_SECTOR);
		brelse(bh);
	}

	/* boot checksum sub-regions */
	bh = sb_bread(sb, sn);
	if (!bh)
		return -EIO;

	for (i = 0; i < sb->s_blocksize; i += sizeof(u32)) {
		p_chksum = (__le32 *)&bh->b_data[i];
		if (le32_to_cpu(*p_chksum) != chksum) {
			exfat_err(sb, "Invalid boot checksum (boot checksum : 0x%08x, checksum : 0x%08x)",
				  le32_to_cpu(*p_chksum), chksum);
			brelse(bh);
			return -EINVAL;
		}
	}
	brelse(bh);
	return 0;
}

/* mount the file system volume */
static int __exfat_fill_super(struct super_block *sb,
		struct exfat_chain *root_clu)
{
	int ret;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	ret = exfat_read_boot_sector(sb);
	if (ret) {
		exfat_err(sb, "failed to read boot sector");
		goto free_bh;
	}

	ret = exfat_verify_boot_region(sb);
	if (ret) {
		exfat_err(sb, "invalid boot region");
		goto free_bh;
	}

	/*
	 * Call exfat_count_num_cluster() before searching for up-case and
	 * bitmap directory entries to avoid infinite loop if they are missing
	 * and the cluster chain includes a loop.
	 */
	exfat_chain_set(root_clu, sbi->root_dir, 0, ALLOC_FAT_CHAIN);
	ret = exfat_count_num_clusters(sb, root_clu, &root_clu->size);
	if (ret) {
		exfat_err(sb, "failed to count the number of clusters in root");
		goto free_bh;
	}

	ret = exfat_create_upcase_table(sb);
	if (ret) {
		exfat_err(sb, "failed to load upcase table");
		goto free_bh;
	}

	ret = exfat_load_bitmap(sb);
	if (ret) {
		exfat_err(sb, "failed to load alloc-bitmap");
		goto free_bh;
	}

	ret = exfat_count_used_clusters(sb, &sbi->used_clusters);
	if (ret) {
		exfat_err(sb, "failed to scan clusters");
		goto free_alloc_bitmap;
	}

	return 0;

free_alloc_bitmap:
	exfat_free_bitmap(sbi);
free_bh:
	brelse(sbi->boot_bh);
	return ret;
}

static int exfat_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct exfat_sb_info *sbi = sb->s_fs_info;
	struct exfat_mount_options *opts = &sbi->options;
	struct inode *root_inode;
	struct exfat_chain root_clu;
	int err;

	if (opts->allow_utime == (unsigned short)-1)
		opts->allow_utime = ~opts->fs_dmask & 0022;

	if (opts->discard && !bdev_max_discard_sectors(sb->s_bdev)) {
		exfat_warn(sb, "mounting with \"discard\" option, but the device does not support discard");
		opts->discard = 0;
	}

	sb->s_flags |= SB_NODIRATIME;
	sb->s_magic = EXFAT_SUPER_MAGIC;
	sb->s_op = &exfat_sops;

	sb->s_time_gran = 10 * NSEC_PER_MSEC;
	sb->s_time_min = EXFAT_MIN_TIMESTAMP_SECS;
	sb->s_time_max = EXFAT_MAX_TIMESTAMP_SECS;

	err = __exfat_fill_super(sb, &root_clu);
	if (err) {
		exfat_err(sb, "failed to recognize exfat type");
		goto check_nls_io;
	}

	/* set up enough so that it can read an inode */
	exfat_hash_init(sb);

	if (sbi->options.utf8)
		set_default_d_op(sb, &exfat_utf8_dentry_ops);
	else {
		sbi->nls_io = load_nls(sbi->options.iocharset);
		if (!sbi->nls_io) {
			exfat_err(sb, "IO charset %s not found",
				  sbi->options.iocharset);
			err = -EINVAL;
			goto free_table;
		}
		set_default_d_op(sb, &exfat_dentry_ops);
	}

	root_inode = new_inode(sb);
	if (!root_inode) {
		exfat_err(sb, "failed to allocate root inode");
		err = -ENOMEM;
		goto free_table;
	}

	root_inode->i_ino = EXFAT_ROOT_INO;
	inode_set_iversion(root_inode, 1);
	err = exfat_read_root(root_inode, &root_clu);
	if (err) {
		exfat_err(sb, "failed to initialize root inode");
		goto put_inode;
	}

	exfat_hash_inode(root_inode, EXFAT_I(root_inode)->i_pos);
	insert_inode_hash(root_inode);

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		exfat_err(sb, "failed to get the root dentry");
		err = -ENOMEM;
		goto free_table;
	}

	return 0;

put_inode:
	iput(root_inode);
	sb->s_root = NULL;

free_table:
	exfat_free_bitmap(sbi);
	brelse(sbi->boot_bh);

check_nls_io:
	return err;
}

static int exfat_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, exfat_fill_super);
}

static void exfat_free_sbi(struct exfat_sb_info *sbi)
{
	exfat_free_iocharset(sbi);
	kfree(sbi);
}

static void exfat_free(struct fs_context *fc)
{
	struct exfat_sb_info *sbi = fc->s_fs_info;

	if (sbi)
		exfat_free_sbi(sbi);
}

static int exfat_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct exfat_sb_info *remount_sbi = fc->s_fs_info;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_mount_options *new_opts = &remount_sbi->options;
	struct exfat_mount_options *cur_opts = &sbi->options;

	fc->sb_flags |= SB_NODIRATIME;

	sync_filesystem(sb);
	mutex_lock(&sbi->s_lock);
	exfat_clear_volume_dirty(sb);
	mutex_unlock(&sbi->s_lock);

	if (new_opts->allow_utime == (unsigned short)-1)
		new_opts->allow_utime = ~new_opts->fs_dmask & 0022;

	/*
	 * Since the old settings of these mount options are cached in
	 * inodes or dentries, they cannot be modified dynamically.
	 */
	if (strcmp(new_opts->iocharset, cur_opts->iocharset) ||
	    new_opts->keep_last_dots != cur_opts->keep_last_dots ||
	    new_opts->sys_tz != cur_opts->sys_tz ||
	    new_opts->time_offset != cur_opts->time_offset ||
	    !uid_eq(new_opts->fs_uid, cur_opts->fs_uid) ||
	    !gid_eq(new_opts->fs_gid, cur_opts->fs_gid) ||
	    new_opts->fs_fmask != cur_opts->fs_fmask ||
	    new_opts->fs_dmask != cur_opts->fs_dmask ||
	    new_opts->allow_utime != cur_opts->allow_utime)
		return -EINVAL;

	if (new_opts->discard != cur_opts->discard &&
	    new_opts->discard &&
	    !bdev_max_discard_sectors(sb->s_bdev)) {
		exfat_warn(sb, "remounting with \"discard\" option, but the device does not support discard");
		return -EINVAL;
	}

	swap(*cur_opts, *new_opts);

	return 0;
}

static const struct fs_context_operations exfat_context_ops = {
	.parse_param	= exfat_parse_param,
	.get_tree	= exfat_get_tree,
	.free		= exfat_free,
	.reconfigure	= exfat_reconfigure,
};

static int exfat_init_fs_context(struct fs_context *fc)
{
	struct exfat_sb_info *sbi;

	sbi = kzalloc(sizeof(struct exfat_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	mutex_init(&sbi->s_lock);
	mutex_init(&sbi->bitmap_lock);
	ratelimit_state_init(&sbi->ratelimit, DEFAULT_RATELIMIT_INTERVAL,
			DEFAULT_RATELIMIT_BURST);

	sbi->options.fs_uid = current_uid();
	sbi->options.fs_gid = current_gid();
	sbi->options.fs_fmask = current->fs->umask;
	sbi->options.fs_dmask = current->fs->umask;
	sbi->options.allow_utime = -1;
	sbi->options.errors = EXFAT_ERRORS_RO;
	exfat_set_iocharset(&sbi->options, exfat_default_iocharset);

	fc->s_fs_info = sbi;
	fc->ops = &exfat_context_ops;
	return 0;
}

static void delayed_free(struct rcu_head *p)
{
	struct exfat_sb_info *sbi = container_of(p, struct exfat_sb_info, rcu);

	unload_nls(sbi->nls_io);
	exfat_free_upcase_table(sbi);
	exfat_free_sbi(sbi);
}

static void exfat_kill_sb(struct super_block *sb)
{
	struct exfat_sb_info *sbi = sb->s_fs_info;

	kill_block_super(sb);
	if (sbi)
		call_rcu(&sbi->rcu, delayed_free);
}

static struct file_system_type exfat_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "exfat",
	.init_fs_context	= exfat_init_fs_context,
	.parameters		= exfat_parameters,
	.kill_sb		= exfat_kill_sb,
	.fs_flags		= FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
};

static void exfat_inode_init_once(void *foo)
{
	struct exfat_inode_info *ei = (struct exfat_inode_info *)foo;

	spin_lock_init(&ei->cache_lru_lock);
	ei->nr_caches = 0;
	ei->cache_valid_id = EXFAT_CACHE_VALID + 1;
	INIT_LIST_HEAD(&ei->cache_lru);
	INIT_HLIST_NODE(&ei->i_hash_fat);
	inode_init_once(&ei->vfs_inode);
}

static int __init init_exfat_fs(void)
{
	int err;

	err = exfat_cache_init();
	if (err)
		return err;

	exfat_inode_cachep = kmem_cache_create("exfat_inode_cache",
			sizeof(struct exfat_inode_info),
			0, SLAB_RECLAIM_ACCOUNT,
			exfat_inode_init_once);
	if (!exfat_inode_cachep) {
		err = -ENOMEM;
		goto shutdown_cache;
	}

	err = register_filesystem(&exfat_fs_type);
	if (err)
		goto destroy_cache;

	return 0;

destroy_cache:
	kmem_cache_destroy(exfat_inode_cachep);
shutdown_cache:
	exfat_cache_shutdown();
	return err;
}

static void __exit exit_exfat_fs(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(exfat_inode_cachep);
	unregister_filesystem(&exfat_fs_type);
	exfat_cache_shutdown();
}

module_init(init_exfat_fs);
module_exit(exit_exfat_fs);

MODULE_ALIAS_FS("exfat");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("exFAT filesystem support");
MODULE_AUTHOR("Samsung Electronics Co., Ltd.");
