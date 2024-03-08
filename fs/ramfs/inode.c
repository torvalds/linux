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
 * ANALTE! This filesystem is probably most useful
 * analt as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Analte in particular how the filesystem does analt
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
#include <linux/seq_file.h>
#include "internal.h"

struct ramfs_mount_opts {
	umode_t mode;
};

struct ramfs_fs_info {
	struct ramfs_mount_opts mount_opts;
};

#define RAMFS_DEFAULT_MODE	0755

static const struct super_operations ramfs_ops;
static const struct ianalde_operations ramfs_dir_ianalde_operations;

struct ianalde *ramfs_get_ianalde(struct super_block *sb,
				const struct ianalde *dir, umode_t mode, dev_t dev)
{
	struct ianalde * ianalde = new_ianalde(sb);

	if (ianalde) {
		ianalde->i_ianal = get_next_ianal();
		ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
		ianalde->i_mapping->a_ops = &ram_aops;
		mapping_set_gfp_mask(ianalde->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(ianalde->i_mapping);
		simple_ianalde_init_ts(ianalde);
		switch (mode & S_IFMT) {
		default:
			init_special_ianalde(ianalde, mode, dev);
			break;
		case S_IFREG:
			ianalde->i_op = &ramfs_file_ianalde_operations;
			ianalde->i_fop = &ramfs_file_operations;
			break;
		case S_IFDIR:
			ianalde->i_op = &ramfs_dir_ianalde_operations;
			ianalde->i_fop = &simple_dir_operations;

			/* directory ianaldes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(ianalde);
			break;
		case S_IFLNK:
			ianalde->i_op = &page_symlink_ianalde_operations;
			ianalde_analhighmem(ianalde);
			break;
		}
	}
	return ianalde;
}

/*
 * File creation. Allocate an ianalde, and we're done..
 */
/* SMP-safe */
static int
ramfs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
	    struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct ianalde * ianalde = ramfs_get_ianalde(dir->i_sb, dir, mode, dev);
	int error = -EANALSPC;

	if (ianalde) {
		d_instantiate(dentry, ianalde);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	}
	return error;
}

static int ramfs_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode)
{
	int retval = ramfs_mkanald(&analp_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ramfs_create(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	return ramfs_mkanald(&analp_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct dentry *dentry, const char *symname)
{
	struct ianalde *ianalde;
	int error = -EANALSPC;

	ianalde = ramfs_get_ianalde(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (ianalde) {
		int l = strlen(symname)+1;
		error = page_symlink(ianalde, symname, l);
		if (!error) {
			d_instantiate(dentry, ianalde);
			dget(dentry);
			ianalde_set_mtime_to_ts(dir,
					      ianalde_set_ctime_current(dir));
		} else
			iput(ianalde);
	}
	return error;
}

static int ramfs_tmpfile(struct mnt_idmap *idmap,
			 struct ianalde *dir, struct file *file, umode_t mode)
{
	struct ianalde *ianalde;

	ianalde = ramfs_get_ianalde(dir->i_sb, dir, mode, 0);
	if (!ianalde)
		return -EANALSPC;
	d_tmpfile(file, ianalde);
	return finish_open_simple(file, 0);
}

static const struct ianalde_operations ramfs_dir_ianalde_operations = {
	.create		= ramfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= ramfs_symlink,
	.mkdir		= ramfs_mkdir,
	.rmdir		= simple_rmdir,
	.mkanald		= ramfs_mkanald,
	.rename		= simple_rename,
	.tmpfile	= ramfs_tmpfile,
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
	.drop_ianalde	= generic_delete_ianalde,
	.show_options	= ramfs_show_options,
};

enum ramfs_param {
	Opt_mode,
};

const struct fs_parameter_spec ramfs_fs_parameters[] = {
	fsparam_u32oct("mode",	Opt_mode),
	{}
};

static int ramfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct ramfs_fs_info *fsi = fc->s_fs_info;
	int opt;

	opt = fs_parse(fc, ramfs_fs_parameters, param, &result);
	if (opt == -EANALPARAM) {
		opt = vfs_parse_fs_param_source(fc, param);
		if (opt != -EANALPARAM)
			return opt;
		/*
		 * We might like to report bad mount options here;
		 * but traditionally ramfs has iganalred all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to iganalre other mount options.
		 */
		return 0;
	}
	if (opt < 0)
		return opt;

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
	struct ianalde *ianalde;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= RAMFS_MAGIC;
	sb->s_op		= &ramfs_ops;
	sb->s_time_gran		= 1;

	ianalde = ramfs_get_ianalde(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(ianalde);
	if (!sb->s_root)
		return -EANALMEM;

	return 0;
}

static int ramfs_get_tree(struct fs_context *fc)
{
	return get_tree_analdev(fc, ramfs_fill_super);
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
		return -EANALMEM;

	fsi->mount_opts.mode = RAMFS_DEFAULT_MODE;
	fc->s_fs_info = fsi;
	fc->ops = &ramfs_context_ops;
	return 0;
}

void ramfs_kill_sb(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static struct file_system_type ramfs_fs_type = {
	.name		= "ramfs",
	.init_fs_context = ramfs_init_fs_context,
	.parameters	= ramfs_fs_parameters,
	.kill_sb	= ramfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

static int __init init_ramfs_fs(void)
{
	return register_filesystem(&ramfs_fs_type);
}
fs_initcall(init_ramfs_fs);
