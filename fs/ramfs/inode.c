/*
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * yest as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does yest
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include "internal.h"

struct ramfs_mount_opts {
	umode_t mode;
};

struct ramfs_fs_info {
	struct ramfs_mount_opts mount_opts;
};

#define RAMFS_DEFAULT_MODE	0755

static const struct super_operations ramfs_ops;
static const struct iyesde_operations ramfs_dir_iyesde_operations;

static const struct address_space_operations ramfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.set_page_dirty	= __set_page_dirty_yes_writeback,
};

struct iyesde *ramfs_get_iyesde(struct super_block *sb,
				const struct iyesde *dir, umode_t mode, dev_t dev)
{
	struct iyesde * iyesde = new_iyesde(sb);

	if (iyesde) {
		iyesde->i_iyes = get_next_iyes();
		iyesde_init_owner(iyesde, dir, mode);
		iyesde->i_mapping->a_ops = &ramfs_aops;
		mapping_set_gfp_mask(iyesde->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(iyesde->i_mapping);
		iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
		switch (mode & S_IFMT) {
		default:
			init_special_iyesde(iyesde, mode, dev);
			break;
		case S_IFREG:
			iyesde->i_op = &ramfs_file_iyesde_operations;
			iyesde->i_fop = &ramfs_file_operations;
			break;
		case S_IFDIR:
			iyesde->i_op = &ramfs_dir_iyesde_operations;
			iyesde->i_fop = &simple_dir_operations;

			/* directory iyesdes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(iyesde);
			break;
		case S_IFLNK:
			iyesde->i_op = &page_symlink_iyesde_operations;
			iyesde_yeshighmem(iyesde);
			break;
		}
	}
	return iyesde;
}

/*
 * File creation. Allocate an iyesde, and we're done..
 */
/* SMP-safe */
static int
ramfs_mkyesd(struct iyesde *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct iyesde * iyesde = ramfs_get_iyesde(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (iyesde) {
		d_instantiate(dentry, iyesde);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}
	return error;
}

static int ramfs_mkdir(struct iyesde * dir, struct dentry * dentry, umode_t mode)
{
	int retval = ramfs_mkyesd(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ramfs_create(struct iyesde *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return ramfs_mkyesd(dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct iyesde * dir, struct dentry *dentry, const char * symname)
{
	struct iyesde *iyesde;
	int error = -ENOSPC;

	iyesde = ramfs_get_iyesde(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (iyesde) {
		int l = strlen(symname)+1;
		error = page_symlink(iyesde, symname, l);
		if (!error) {
			d_instantiate(dentry, iyesde);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = current_time(dir);
		} else
			iput(iyesde);
	}
	return error;
}

static const struct iyesde_operations ramfs_dir_iyesde_operations = {
	.create		= ramfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= ramfs_symlink,
	.mkdir		= ramfs_mkdir,
	.rmdir		= simple_rmdir,
	.mkyesd		= ramfs_mkyesd,
	.rename		= simple_rename,
};

/*
 * Display the mount options in /proc/mounts.
 */
static int ramfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct ramfs_fs_info *fsi = root->d_sb->s_fs_info;

	if (fsi->mount_opts.mode != RAMFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", fsi->mount_opts.mode);
	return 0;
}

static const struct super_operations ramfs_ops = {
	.statfs		= simple_statfs,
	.drop_iyesde	= generic_delete_iyesde,
	.show_options	= ramfs_show_options,
};

enum ramfs_param {
	Opt_mode,
};

static const struct fs_parameter_spec ramfs_param_specs[] = {
	fsparam_u32oct("mode",	Opt_mode),
	{}
};

const struct fs_parameter_description ramfs_fs_parameters = {
	.name		= "ramfs",
	.specs		= ramfs_param_specs,
};

static int ramfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct ramfs_fs_info *fsi = fc->s_fs_info;
	int opt;

	opt = fs_parse(fc, &ramfs_fs_parameters, param, &result);
	if (opt < 0) {
		/*
		 * We might like to report bad mount options here;
		 * but traditionally ramfs has igyesred all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to igyesre other mount options.
		 */
		if (opt == -ENOPARAM)
			opt = 0;
		return opt;
	}

	switch (opt) {
	case Opt_mode:
		fsi->mount_opts.mode = result.uint_32 & S_IALLUGO;
		break;
	}

	return 0;
}

static int ramfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct ramfs_fs_info *fsi = sb->s_fs_info;
	struct iyesde *iyesde;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= RAMFS_MAGIC;
	sb->s_op		= &ramfs_ops;
	sb->s_time_gran		= 1;

	iyesde = ramfs_get_iyesde(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(iyesde);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static int ramfs_get_tree(struct fs_context *fc)
{
	return get_tree_yesdev(fc, ramfs_fill_super);
}

static void ramfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations ramfs_context_ops = {
	.free		= ramfs_free_fc,
	.parse_param	= ramfs_parse_param,
	.get_tree	= ramfs_get_tree,
};

int ramfs_init_fs_context(struct fs_context *fc)
{
	struct ramfs_fs_info *fsi;

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	fsi->mount_opts.mode = RAMFS_DEFAULT_MODE;
	fc->s_fs_info = fsi;
	fc->ops = &ramfs_context_ops;
	return 0;
}

static void ramfs_kill_sb(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static struct file_system_type ramfs_fs_type = {
	.name		= "ramfs",
	.init_fs_context = ramfs_init_fs_context,
	.parameters	= &ramfs_fs_parameters,
	.kill_sb	= ramfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

static int __init init_ramfs_fs(void)
{
	return register_filesystem(&ramfs_fs_type);
}
fs_initcall(init_ramfs_fs);
